#include "gmCore.h"

static void taskContinue_PostTcpEvent(struct task *task, BOOL success)
{
    struct tcp_custom_task_continue *tcpCustom;

    tcpCustom = malloc(sizeof(struct tcp_custom_task_continue));
    if (tcpCustom)
    {
        memset(tcpCustom, 0, sizeof(struct tcp_custom_task_continue));
        tcpCustom->ioType = TCP_CUSTOM_TASK_CONTINUE;
        tcpCustom->task = task;
        tcpCustom->success = success;
        tcp_postEvent(tcpCustom);
    }
}

static BOOL taskContinue_readTmpFileBrief(struct task *task)
{
    struct file *file;
    LARGE_INTEGER li;
    UINT32 i, piece, pieceInFile, pieceLen;
    DWORD dwRead;

    task_resetTmpData(task);

    if (!task_openTmpFile(task)) return FALSE;
    SetFilePointer(task->tmp.hFile, 0, NULL, FILE_BEGIN);

    for (i=0; i<task->piecesToDownload; i++)
    {
        piece = task->tmp.pieceTable[i];

        file = task_getFileInfo(task, piece);
        pieceLen = task_getPieceLength(task, file, piece-file->pieceOffset);

        li.QuadPart = ((INT64)i) * (sizeof(UINT32) + task->idx.pieceLength);
        SetFilePointerEx(task->tmp.hFile, li, NULL, FILE_BEGIN);
        if (!ReadFile(task->tmp.hFile, &pieceInFile, sizeof(UINT32), &dwRead, NULL) ||
            dwRead != sizeof(UINT32))
            return FALSE;
        if (pieceInFile == piece)
        {
            bitset_set(&task->tmp.bitset, piece);
            task->tmp.bytesDownloaded += pieceLen;
            task->tmp.piecesDownloaded ++;
            task->tmp.files[file->idxInFiles].bytesDownloaded += pieceLen;
            task->tmp.files[file->idxInFiles].piecesDownloaded ++;
        }
    }

    return TRUE;
}

static BOOL taskContinue_checkTmpFileTimeAndSize(struct task *task)
{
    LARGE_INTEGER li;
    UINT32 timeCount;
    UINT64 *times = NULL;

    if (!task_openTmpFile(task)) return FALSE;

    if (!GetFileSizeEx(task->tmp.hFile, &li) ||
        li.QuadPart != ((INT64)task->piecesToDownload)*(sizeof(UINT32)+task->idx.pieceLength))
        return FALSE;

    if (!task_readFileTime(task, &timeCount, &times) || timeCount != 1)
    {
        if (times) free(times);
        return FALSE;
    }

    if (FileGetLastWriteTime(task->tmp.hFile) != times[0])
    {
        if (times) free(times);
        return FALSE;
    }

    if (times) free(times);

    return TRUE;
}

static BOOL taskContinue_checkSeedingFilesTimeAndSize(struct task *task)
{
    UINT32 i;
    LARGE_INTEGER li;

    for (i=0; i<task->idx.fileCount; i++)
    {
        if (!task_openFile(task, task->idx.files[i], FALSE, NULL))
            return FALSE;

        if (!GetFileSizeEx(task->idx.files[i]->hFile, &li) ||
            (UINT64)li.QuadPart != task->idx.files[i]->bytes)
            return FALSE;

        if (FileGetLastWriteTime(task->idx.files[i]->hFile) != task->idx.files[i]->fileTime)
            return FALSE;
    }

    return TRUE;
}

static BOOL taskContinue_checkDownloadingFilesTimeAndSize(struct task *task)
{
    UINT32 i, timeCount;
    UINT64 *times = NULL;
    LARGE_INTEGER li;

    if (!task_readFileTime(task, &timeCount, &times) || timeCount != task->idx.fileCount)
    {
        if (times) free(times);
        return FALSE;
    }

    for (i=0; i<task->idx.fileCount; i++)
    {
        if (!task_openFile(task, task->idx.files[i], FALSE, NULL))
            return FALSE;

        if (!GetFileSizeEx(task->idx.files[i]->hFile, &li) ||
            (UINT64)li.QuadPart != task->idx.files[i]->bytes)
            return FALSE;

        if (FileGetLastWriteTime(task->idx.files[i]->hFile) != times[i])
            return FALSE;
    }

    if (times) free(times);

    return TRUE;
}

BOOL taskContinue_doJob(struct task *task)
{
    struct idx idx = { 0 };
    struct idx_net *idxn, idxnf = { 0 };
    struct file *fi;
    WCHAR wId[MAX_ID_LEN], szPath[MAX_PATH];
    CHAR szTmp[512];
    UINT32 i, pieceLen;

    time(&task->cont.lastActionTime);

    swprintf_s(szPath, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idx) ||
        strcmp(task->idx.id, idx.id) ||
        strcmp(task->idx.hash, idx.hash))
    {
        task_setError(task, ERR_IDX);

        debugf("[taskContinue] cannot load idx: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, ERR_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);

        idx_free(&idx);
        return FALSE;
    }

    task->idx = idx;

    if (task->action & TS_SEEDING)
    {
        if (!taskContinue_checkSeedingFilesTimeAndSize(task))
        {
            task_setError(task, ERR_FILES);

            debugf("[taskContinue] continue task(seeding): %s file size/time error\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, ERR_FILES);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);

            return FALSE;
        }

        bitset_init(&task->bitset, task->idx.pieceCount);
        bitset_init(&task->bitsetRequested, task->idx.pieceCount);
        bitset_setAll(&task->bitset);
        task->bytesToDownload = 0;
        task->piecesToDownload = 0;
        task->bytesDownloaded = task->idx.bytes;
        task->piecesDownloaded = task->idx.pieceCount;
        for (i=0; i<task->idx.fileCount; i++)
        {
            task->idx.files[i]->bytesDownloaded = task->idx.files[i]->bytes;
            task->idx.files[i]->piecesDownloaded = task->idx.files[i]->pieceCount;
        }
        task->downLimit = 0;
        task->upLimit = 0;

        strcpy_s(idxnf.id, MAX_ID_LEN, task->idx.id);
        idxn = ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
        if (idxn && strcmp(task->idx.hash, idxn->hash))
        {
            debugf("[taskContinue] idx changed: %s need reload\r\n", task->idx.id);
            task->action |= TS_PREPARING;
            task_saveStatus(task);
            sprintf_s(szTmp, 512, "%s\tnet_idx\t%d", task->idx.id, task->action);
            admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
        }
        return TRUE;
    }

    bitset_init(&task->bitset, task->idx.pieceCount);
    bitset_init(&task->bitsetRequested, task->idx.pieceCount);

    if (!task_readStatus(task) || !task_readBitset(task))
    {
        task_setError(task, ERR_CONTINUE);

        debugf("[taskContinue] cannot readStatus or readBitset %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, ERR_CONTINUE);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);

        return FALSE;
    }

    task->bytesDownloaded = 0;
    task->piecesDownloaded = 0;
    for (i=0; i<task->idx.fileCount; i++)
    {
        task->idx.files[i]->bytesDownloaded = 0;
        task->idx.files[i]->piecesDownloaded = 0;
    }
    for (i=0; i<task->idx.pieceCount; i++)
    {
        if (1==bitset_check(&task->bitset, i))
        {
            fi = task_getFileInfo(task, i);
            if (!fi || i<fi->pieceOffset) break;
            pieceLen = task_getPieceLength(task, fi, i-fi->pieceOffset);
            task->bytesDownloaded += pieceLen;
            task->piecesDownloaded ++;
            fi->bytesDownloaded += pieceLen;
            fi->piecesDownloaded ++;
        }
    }

    if (task->action & TS_DOWNLOADING)
    {
        if (!taskContinue_checkDownloadingFilesTimeAndSize(task))
        {
            task_setError(task, ERR_CONTINUE);

            debugf("[taskContinue] continue task(downloading): %s file size/time error\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, ERR_CONTINUE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);

            return FALSE;
        }

        debugf("[taskContinue] continue task: %s downloading...\r\n", task->idx.id);

        strcpy_s(idxnf.id, MAX_ID_LEN, task->idx.id);
        idxn = ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
        if (idxn && strcmp(task->idx.hash, idxn->hash))
        {
            task->action |= TS_PREPARING;
            task_saveStatus(task);
            sprintf_s(szTmp, 512, "%s\tnet_idx\t%d", task->idx.id, task->action);
            admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
        }
        return TRUE;
    }

    if (task->action & TS_UPDATING)
    {
        struct idx siOld = { 0 };

        swprintf_s(szPath, MAX_PATH, L"%s\\%s"IDX_EXTNAME, task->dir, wId);
        if (!idx_open(szPath, &siOld) ||
            !idx_checkFilesTimeAndSize(&siOld, task->dir) ||
            !taskContinue_checkTmpFileTimeAndSize(task) ||
            !taskContinue_readTmpFileBrief(task))
        {
            task_setError(task, ERR_FILES);

            debugf("[taskContinue] cannot load old idx file(updating): %s\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, ERR_FILES);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);

            idx_free(&siOld);
            return FALSE;
        }

        idx_free(&siOld);

        debugf("[taskContinue] continue task: %s updating...\r\n", task->idx.id);

        strcpy_s(idxnf.id, MAX_ID_LEN, task->idx.id);
        idxn = ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
        if (idxn && strcmp(task->idx.hash, idxn->hash))
        {
            task->action |= TS_PREPARING;
            task_saveStatus(task);
            sprintf_s(szTmp, 512, "%s\tnet_idx\t%d", task->idx.id, task->action);
            admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
        }
        return TRUE;
    }
    return FALSE;
}

static unsigned __stdcall TaskContinue_ThreadProc(LPVOID param)
{
    struct task *task = param;
    BOOL success;

    ResetEvent(task->cont.stoppedEvent);

    success = taskContinue_doJob(task);
    taskContinue_PostTcpEvent(task, success);

    CloseHandle(task->cont.stopEvent);
    task->cont.stopEvent = NULL;

    SetEvent(task->cont.stoppedEvent);

    return 0;
}

BOOL taskContinue_begin(struct task *task)
{
    if (task->cont.stopEvent) return TRUE;

    task->cont.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskContinue_ThreadProc, task, 0, NULL));

    return TRUE;
}

void taskContinue_end(struct task *task)
{
    if (!task->cont.stopEvent) return;

    SetEvent(task->cont.stopEvent);
    WaitForSingleObject(task->cont.stoppedEvent, INFINITE);

    ResetEvent(task->cont.stoppedEvent);

    task->action &= ~TS_CONTINUING;
}

