#include "gmCore.h"

// transfer:
// 暂时无法打开更新文件时，下次重试；
// 临时文件出错时，删除该文件，重新下载更新
// 操作一旦开始，不可中断

static void taskTransfer_PostTcpEvent(struct task *task, BOOL success)
{
    struct tcp_custom_task_transfer *tcpCustom;

    tcpCustom = malloc(sizeof(struct tcp_custom_task_transfer));
    if (tcpCustom)
    {
        memset(tcpCustom, 0, sizeof(struct tcp_custom_task_transfer));
        tcpCustom->ioType = TCP_CUSTOM_TASK_TRANSFER;
        tcpCustom->task = task;
        tcpCustom->success = success;
        tcp_postEvent(tcpCustom);
    }
}

static BOOL taskTransfer_doJob(struct task *task)
{
    struct file *file;
    LARGE_INTEGER li;
    UCHAR *pieceData = NULL, hash[20];
    UINT32 i, piece, pieceInFile, pieceLen, read, written;
    DWORD errCode = 0;
    CHAR szTmp[512];

    time(&task->transfer.lastActionTime);

    admin_sendMsg(STR_TRANSFER_BEGIN, task->idx.id);

    if (!task_createDirectories(task))
    {
        task_setError(task, ERR_FILE_WRITE);
        debugf("[Transfer]创建目录时出错 %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_FILE_WRITE);
        admin_sendMsg(STR_TRANSFER_ERROR, szTmp);
        return FALSE;
    }
    if (!task_allocFiles(task, &errCode))
    {
        if (errCode == ERROR_SHARING_VIOLATION)
        {
             ("[Transfer] 打开文件时共享冲突 %s\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, -1);
            admin_sendMsg(STR_TRANSFER_ERROR, task->idx.id);
        }
        else
        {
            task_setError(task, ERR_FILE_WRITE);
            debugf("[Transfer] 无法打开文件或磁盘空间不够 %s\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_FILE_WRITE);
            admin_sendMsg(STR_TRANSFER_ERROR, szTmp);
        }
        return FALSE;
    }

    pieceData = malloc(task->idx.pieceLength);
    SetFilePointer(task->tmp.hFile, 0, NULL, FILE_BEGIN);

    for (i=0; i<task->piecesToDownload; i++)
    {
        piece = task->tmp.pieceTable[i];

        file = task_getFileInfo(task, piece);
        if (!file)
        {
            debugf("[Transfer] error %s: piece %d no file\r\n", task->idx.id, piece);
            goto _error_transfer;
        }

        if (!ReadFile(task->tmp.hFile, &pieceInFile, sizeof(UINT32), &read, NULL) ||
            read != sizeof(UINT32) ||
            pieceInFile != piece)
        {
            debugf("[Transfer] error %s: piece:%u pieceInFile:%u\r\n", task->idx.id, piece, pieceInFile);
            goto _error_transfer;
        }

        piece -= file->pieceOffset;
        pieceLen = task_getPieceLength(task, file, piece);

        if (!ReadFile(task->tmp.hFile, pieceData, task->idx.pieceLength, &read, NULL) ||
            read != task->idx.pieceLength)
        {
            debugf("[Transfer] error %s: piece %d tmp read error %d\r\n", task->idx.id, piece, GetLastError());
            goto _error_transfer;
        }

        sha1(pieceData, pieceLen, hash);
        if (memcmp(file->hash + piece * 20, hash, 20))
        {
            debugf("[Transfer] error %s: piece %d hash mismatch\r\n", task->idx.id, piece);
            goto _error_transfer;
        }

        if (1==bitset_check(&task->bitset, piece + file->pieceOffset)) // dup
        {
            debugf("[Transfer] error %s: piece %u dup\r\n", task->idx.id, piece + file->pieceOffset);
            goto _error_transfer;
        }

        li.QuadPart = ((INT64)piece) * task->idx.pieceLength;
        SetFilePointerEx(file->hFile, li, NULL, FILE_BEGIN);
        if (!WriteFile(file->hFile, pieceData, pieceLen, &written, NULL) ||
            written != pieceLen)
        {
            debugf("[Transfer] error %s: piece %d(%d) write error %d\r\n",
                task->idx.id, piece+file->pieceOffset, i, GetLastError());
            goto _error_transfer;
        }

        bitset_set(&task->bitset, piece+file->pieceOffset);

        task->transfer.completed ++;
    }

    free(pieceData);
    task_correcrAllFileTime(task);
    task_closeAllFiles(task);
    idx_cleanDirectory(&task->idx, task->dir);

    sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, 0);
    admin_sendMsg(STR_TRANSFER_ERROR, szTmp);

    return TRUE;

_error_transfer:
    free(pieceData);

    task_closeAllFiles(task);
    task_resetTmpData(task);

    task_setError(task, ERR_TMP_FILE);
    sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_TMP_FILE);
    admin_sendMsg(STR_TRANSFER_ERROR, szTmp);

    return FALSE;
}

static unsigned __stdcall TaskTransfer_ThreadProc(LPVOID param)
{
    struct task *task = param;
    BOOL success;

    ResetEvent(task->transfer.stoppedEvent);

    success = taskTransfer_doJob(task);
    taskTransfer_PostTcpEvent(task, success);

    CloseHandle(task->transfer.stopEvent);
    task->transfer.stopEvent = NULL;

    SetEvent(task->transfer.stoppedEvent);

    return 0;
}

BOOL taskTransfer_begin(struct task *task)
{
    if (task->transfer.stopEvent) return TRUE;

    task->transfer.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskTransfer_ThreadProc, task, 0, NULL));

    return TRUE;
}

void taskTransfer_end(struct task *task)
{
    if (!task->transfer.stopEvent) return;

    SetEvent(task->transfer.stopEvent);
    WaitForSingleObject(task->transfer.stoppedEvent, INFINITE);

    ResetEvent(task->transfer.stoppedEvent);
}

