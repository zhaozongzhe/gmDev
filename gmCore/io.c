#include "gmCore.h"

struct io_thread
{
    HANDLE hEvent;
    HANDLE hEventStop;
    HANDLE hEventStopped;
    CRITICAL_SECTION cs;
    struct ptrList *writes;
    struct ptrList *reads;
};
#define MAX_IO_THREADS              4

struct io_thread g_ioThreads[MAX_IO_THREADS] = { 0 };


void piece_free(struct piece *pc)
{
    if (pc->data) free(pc->data);
    free(pc);
}

struct piece *piece_new(struct file *file, UINT32 piece, UCHAR *data, int dataLen)
{
    struct piece *pc;

    pc = (struct piece *)malloc(sizeof(struct piece));
    if (!pc) return NULL;
    memset(pc, 0, sizeof(struct piece));
    pc->file = file;
    pc->piece = piece;
    pc->data = (UCHAR *)malloc(dataLen);
    if (!pc->data) { free(pc); return NULL; }
    memcpy(pc->data, data, dataLen);
    pc->dataLen = dataLen;
    time(&pc->lastRefTime);

    return pc;
}

struct piece *piece_dup(struct piece *pc)
{
    struct piece *pcNew;

    pcNew = (struct piece *)malloc(sizeof(struct piece));
    if (!pcNew) return NULL;
    memset(pcNew, 0, sizeof(struct piece));
    pcNew->file = pc->file;
    pcNew->piece = pc->piece;
    pcNew->data = (UCHAR *)malloc(pc->dataLen);
    if (!pcNew->data) { free(pcNew); return NULL; }
    memcpy(pcNew->data, pc->data, pc->dataLen);
    pcNew->dataLen = pc->dataLen;
    time(&pcNew->lastRefTime);

    return pcNew;
}

void task_addReadCache(struct task *task, struct file *file, UINT32 piece, UCHAR *data, int dataLen)
{
    //struct piece *pc;

    //pc = piece_new(file, piece, data, dataLen);
    //if (!pc) return NULL;
    //ptrList_append(&task->readCache, pc);
    //return pc;
}

void task_onPieceRead(struct tcp_custom_task_io *io)
{
    struct task *task;
    struct peer *pr, *peer;
    struct ptrList *li;

    EnterCriticalSection(&g_ioThreads[io->iThread].cs);
    ptrList_remove_data(&g_ioThreads[io->iThread].reads, io);
    LeaveCriticalSection(&g_ioThreads[io->iThread].cs);

    if (!io->success)
    {
        debugf("onPieceRead failed %s #%d\r\n", io->taskHash, io->piece);
        free(io->pieceData); free(io);
        return;
    }

    task = task_findHash(io->taskHash);
    if (!task || task != io->task)
    {
        debugf("onPieceRead task not exist %s #%d\r\n", io->taskHash, io->piece);
        free(io->pieceData); free(io);
        return;
    }

    for (li=task->peersIncoming, peer=NULL; li; li=li->next)
    {
        pr = (struct peer *)li->data;
        if (0==strcmp(pr->pid, io->peerId) && pr==io->peer)
        { peer = pr; break; }
    }
    if (!peer) for (li=task->peersOutgoing; li; li=li->next)
    {
        pr = (struct peer *)li->data;
        if (0==strcmp(pr->pid, io->peerId) && pr==io->peer)
        { peer = pr; break; }
    }
    if (!peer)
    {
        debugf("onPieceRead peer not exist %s %s #%d\r\n", io->taskHash, io->peerId, io->piece);
        free(io->pieceData); free(io);
        return;
    }

    peer_addReadCache(peer, io->file, io->piece, io->pieceData, io->pieceLen);
    task_addReadCache(task, io->file, io->piece, io->pieceData, io->pieceLen);

    free(io->pieceData); free(io);

    peer_doSendPiece(peer);
}

static BOOL task_readFilePiece(struct task *task, struct file *file,
                               UINT32 piece, CHAR *pieceData, UINT32 pieceLen)
{
    DWORD dwRead;
    LARGE_INTEGER li;
    BOOL success;

    if (!task_openFile(task, file, 0, NULL)) return FALSE;

    li.QuadPart = ((INT64)task->idx.pieceLength) * piece;
    SetFilePointerEx(file->hFile, li, NULL, FILE_BEGIN);

    success = (ReadFile(file->hFile, pieceData, pieceLen, &dwRead, NULL) &&
        dwRead == pieceLen);

    return success;
}

static BOOL task_readTmpPiece(struct task *task, struct file *file,
                              UINT32 piece, CHAR *pieceData, UINT32 pieceLen)
{
    UINT32 tmpPiece, i, piece1;
    DWORD dwRead;
    LARGE_INTEGER li;
    BOOL success;

    if (!task_openTmpFile(task) || !task->tmp.pieceTable)
        return FALSE;

    for (tmpPiece=MAXUINT32, i=0; i<task->piecesToDownload; i++)
    {
        if (task->tmp.pieceTable[i]==file->pieceOffset+piece)
        {
            tmpPiece = i;
            break;
        }
    }
    if (tmpPiece>=task->piecesToDownload) return FALSE;

    li.QuadPart = ((INT64)tmpPiece) * (sizeof(UINT32) + task->idx.pieceLength);
    SetFilePointerEx(task->tmp.hFile, li, NULL, FILE_BEGIN);

    success = (ReadFile(task->tmp.hFile, &piece1, sizeof(UINT32), &dwRead, NULL) &&
        dwRead == sizeof(UINT32) &&
        piece1 == file->pieceOffset+piece &&
        ReadFile(task->tmp.hFile, pieceData, pieceLen, &dwRead, NULL) &&
        dwRead == pieceLen);

    return success;
}

static struct tcp_custom_task_io *new_task_io_read(struct task *task, struct peer *peer,
                                                   struct file *file, UINT32 piece, int iThread)
{
    struct tcp_custom_task_io *io;

    io = (struct tcp_custom_task_io *)malloc(sizeof(struct tcp_custom_task_io)); if (!io) return NULL;
    memset(io, 0, sizeof(struct tcp_custom_task_io));
    io->ioType = TCP_CUSTOM_TASK_IO_READ;
    io->iThread = iThread;
    strcpy_s(io->taskHash, MAX_HASH_LEN, task->idx.hash);
    strcpy_s(io->peerId, MAX_PID_LEN, peer->pid);
    io->task = task;
    io->peer = peer;
    io->file = file;
    io->piece = piece;
    io->pieceLen = task_getPieceLength(task, file, piece);
    if (!io->pieceLen) { free(io); return NULL; }
    io->pieceData = (UCHAR *)malloc(io->pieceLen);
    if (!io->pieceData) { free(io); return NULL; }

    return io;
}

struct piece *task_getPieceData(struct task *task, struct file *file, UINT32 piece, struct peer *peer)
{
    struct piece *pc;
    struct tcp_custom_task_io *io;
    int iThread, isReading;
    struct ptrList *li;

    time(&task->lastSeedingTime);

    for (li=task->readCache; li; li=li->next)
    {
        pc = (struct piece *)li->data;
        if (pc->file==file && pc->piece==piece) return pc;
    }

    iThread = file->idxInFiles % MAX_IO_THREADS;

    if (1 == bitset_check(&task->bitset, file->pieceOffset+piece) ||
        1 == bitset_check(&task->tmp.bitset, file->pieceOffset+piece))
    {
        EnterCriticalSection(&g_ioThreads[iThread].cs);
        for (isReading=0, li=g_ioThreads[iThread].reads; li; li=li->next)
        {
            io = li->data;
            if (io->task==task && io->peer==peer && io->file==file && io->piece==piece)
            { isReading=1; break; }
        }
        if (!isReading)
        {
            io = new_task_io_read(task, peer, file, piece, iThread);
            if (io)
            {
                ptrList_append(&g_ioThreads[iThread].reads, io);
                SetEvent(g_ioThreads[iThread].hEvent);
            }
        }
        LeaveCriticalSection(&g_ioThreads[iThread].cs);
    }

    return NULL;
}

static void task_onFilePieceWritten(struct task *task, struct file *file, UINT32 piece, UINT32 pieceLen)
{
    struct idx_local *idxl;
    CHAR buf[1600];
    int checked;

    checked = bitset_check(&task->bitset, file->pieceOffset+piece);
    if (checked != 0)
    {
        debugf("onPieceWritten: %s piece %u, already has data [%d]\r\n",
            task->idx.id, file->pieceOffset+piece, checked);
        return;
    }

    bitset_set(&task->bitset, file->pieceOffset+piece);

    task->bytesDownloaded += pieceLen;
    task->piecesDownloaded ++;
    file->bytesDownloaded += pieceLen;
    file->piecesDownloaded ++;
    time(&file->lastAccessTime);

    task_saveBitset(task);
    //debugf("write piece %s %u requested:%u %u/%u\r\n",
    //    task->idx.id, file->pieceOffset+piece,
    //    bitset_countTrueBits(&task->bitsetRequested),
    //    bitset_countTrueBits(&task->bitset),task->idx.pieceCount);

    if (task->bytesDownloaded >= task->idx.bytes)
    {
        admin_sendDownloadingProgress(task);

        debugf("---Download completed: %s %I64d--%d\r\n",
            task->idx.id, task->idx.bytes, bitset_countTrueBits(&task->bitset));
        task->action = TS_SEEDING;
        task->bytesToDownload = 0;
        task->piecesToDownload = 0;
        task_saveStatus(task);

        task_correcrAllFileTime(task);
        task_closeAllFiles(task);
        task_copySeedFile(task);
        idx_cleanDirectory(&task->idx, task->dir);

        idxl = AddLocalIdx(&task->idx, task->dir);
        IdxLocalToUtf8String(idxl, buf, 1600);
        admin_sendMsg(STR_LOCAL_IDX_ADDED, buf);

        ptrList_remove_data(&g_tasksDownloading, task);
        SaveDownloadingTaskList();
        admin_sendMsg(STR_DOWNLOADING_COMPLETED, task->idx.id);
        admin_sendMsg(STR_DOWNLOADING_DELETED, task->idx.id);

        time(&task->startSeedingTime);
        ptrArray_insertSorted(&g_tasksSeedingSI, task);
        ptrArray_insertSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        admin_sendMsg(STR_SEEDING_ADDED, task->idx.id);

        task_arrangePriorities();
    }
}

static void task_onTmpPieceWritten(struct task *task, struct file *file,
                                   UINT32 piece, UINT32 pieceLen)
{
    if (1 == bitset_check(&task->tmp.bitset, file->pieceOffset+piece))
    {
        debugf("onTmpPieceWritten: %s piece %u already written\r\n", task->idx.id, file->pieceOffset+piece);
        return;
    }

    bitset_set(&task->tmp.bitset, file->pieceOffset+piece);
    task->tmp.bytesDownloaded += pieceLen;
    task->tmp.piecesDownloaded ++;
    task->tmp.files[file->idxInFiles].bytesDownloaded += pieceLen;
    task->tmp.files[file->idxInFiles].piecesDownloaded ++;

    if (task->bytesDownloaded + task->tmp.bytesDownloaded >= task->idx.bytes)
    {
        admin_sendDownloadingProgress(task);
        admin_sendMsg(STR_DOWNLOADING_UPDATED, task->idx.id);

        debugf("---update completed: %s %I64d, %I64d\r\n",
            task->idx.id, task->idx.bytes, task->tmp.bytesDownloaded);

        task_arrangePriorities();
    }
}

void task_onPieceWritten(struct tcp_custom_task_io *io)
{
    struct task *task;

    task = task_findHash(io->taskHash);
    if (!task || task != io->task)
    {
        debugf("task_write error: %s %u\r\n", io->taskHash, io->piece);
        free(io->pieceData);
        free(io);
        return;
    }

    if (!io->success)
        bitset_clear(&task->bitsetRequested, io->file->pieceOffset+io->piece);
    else
    {
        if (task->action & TS_DOWNLOADING)
            task_onFilePieceWritten(task, io->file, io->piece, io->pieceLen);
        else if (task->action & TS_UPDATING)
            task_onTmpPieceWritten(task, io->file, io->piece, io->pieceLen);

        //debugf("sendHaveToPeers: %s %u\r\n", task->idx.id, io->file->pieceOffset+io->piece);
        peer_sendHaveToPeers(task, io->file->pieceOffset+io->piece);
    }

    free(io->pieceData);
    free(io);
}

static BOOL task_writeFilePiece(struct task *task, struct file *file,
                                UINT32 piece, const UCHAR *data, int dataLen)
{
    UINT32 pieceLen, dwWritten;
    LARGE_INTEGER li;
    CHAR szTmp[512];

    if (!task_openFile(task, file, TRUE, NULL))
    {
        debugf("[task_writePiece] cannot open file: %s %s\r\n",
            task->idx.id, UnicodeToMbcs(file->fileName, szTmp, 512));
        return FALSE;
    }

    pieceLen = task_getPieceLength(task, file, piece);
    if (dataLen != (int)pieceLen)
    {
        debugf("task_writePiece error pieceLength: %s #%d pl:%d dl:%d\r\n",
            task->idx.id, file->pieceOffset+piece, pieceLen, dataLen);
        return FALSE;
    }

    li.QuadPart = ((INT64)task->idx.pieceLength) * piece;
    SetFilePointerEx(file->hFile, li, NULL, FILE_BEGIN);

    if (!WriteFile(file->hFile, data, pieceLen, &dwWritten, NULL) ||
        dwWritten != pieceLen)
    {
        debugf("task_writePiece error %d: %s %u pl:%d dl:%d\r\n",
            GetLastError(), task->idx.id, file->pieceOffset+piece, pieceLen, dataLen);
        return FALSE;
    }

    return TRUE;
}

static BOOL task_writeTmpPiece(struct task *task, struct file *file,
                               UINT32 piece, const UCHAR *data, int dataLen)
{
    UINT32 pieceInTask, pieceInTmp, pieceLen, i;
    DWORD dwWritten;
    LARGE_INTEGER li;

    if (!task_openTmpFile(task) || !task->tmp.pieceTable)
        return FALSE;

    pieceInTask = file->pieceOffset + piece;

    for (i=0, pieceInTmp=MAXUINT32; i<task->piecesToDownload; i++)
    {
        if (task->tmp.pieceTable[i] == pieceInTask)
        {
            pieceInTmp = i;
            break;
        }
    }
    if (pieceInTmp >= task->piecesToDownload) return FALSE;

    pieceLen = task_getPieceLength(task, file, piece);
    if (dataLen != (int)pieceLen) return FALSE;

    li.QuadPart = ((INT64)pieceInTmp) * (sizeof(UINT32)+task->idx.pieceLength);
    SetFilePointerEx(task->tmp.hFile, li, NULL, FILE_BEGIN);

    if (!WriteFile(task->tmp.hFile, &pieceInTask, sizeof(UINT32), &dwWritten, NULL) ||
        dwWritten != sizeof(UINT32) ||
        !WriteFile(task->tmp.hFile, data, dataLen, &dwWritten, NULL) ||
        dwWritten != pieceLen)
    {
        debugf("task_writeTmpPiece error %u %u\r\n", file->pieceOffset, piece);
        return FALSE;
    }

    return TRUE;
}

static struct tcp_custom_task_io *new_task_io_write(struct task *task,
                                                    struct file *file, UINT32 piece,
                                                    const UCHAR *data, int dataLen,
                                                    int iThread)
{
    struct tcp_custom_task_io *io;
    int pieceLen;

    pieceLen = task_getPieceLength(task, file, piece);
    if (dataLen != pieceLen) return NULL;

    io = (struct tcp_custom_task_io *)malloc(sizeof(struct tcp_custom_task_io));
    if (!io) return NULL;
    memset(io, 0, sizeof(struct tcp_custom_task_io));
    io->ioType = TCP_CUSTOM_TASK_IO_WRITE;
    io->iThread = iThread;
    strcpy_s(io->taskHash, MAX_HASH_LEN, task->idx.hash);
    io->task = task;
    io->file = file;
    io->piece = piece;
    io->pieceData = (UCHAR *)malloc(dataLen);
    if (!io->pieceData)
    {
        free(io);
        return NULL;
    }
    memcpy(io->pieceData, data, dataLen);
    io->pieceLen = dataLen;

    return io;
}

BOOL task_setPieceData(struct task *task, struct file *file,
                       UINT32 piece, const UCHAR *data, int dataLen)
{
    struct tcp_custom_task_io *io;
    int iThread;

    if (0 == bitset_check(&task->bitset, file->pieceOffset+piece) &&
        0 >= bitset_check(&task->tmp.bitset, file->pieceOffset+piece))
    {
        iThread = file->idxInFiles % MAX_IO_THREADS;

        io = new_task_io_write(task, file, piece, data, dataLen, iThread);
        if (!io) return FALSE;

        EnterCriticalSection(&g_ioThreads[iThread].cs);
        ptrList_append(&g_ioThreads[iThread].writes, io);
        SetEvent(g_ioThreads[iThread].hEvent);
        LeaveCriticalSection(&g_ioThreads[iThread].cs);

        return TRUE;
    }
    else
    {
        debugf("setPieceData ERROR: #%d(already have)\r\n", file->pieceOffset+piece);
        return FALSE;
    }
}

static unsigned __stdcall TaskIoThreadProc(void* pArguments)
{
    struct io_thread *ioThread = (struct io_thread *)pArguments;
    struct task *task;
    HANDLE evh[2];
    struct tcp_custom_task_io *io;
    struct ptrList *list;

    evh[0] = ioThread->hEventStop;
    evh[1] = ioThread->hEvent;

    while (1)
    {
        if (WAIT_OBJECT_0 == WaitForMultipleObjects(2, evh, FALSE, 200))
            break;

        EnterCriticalSection(&ioThread->cs);
        io = ptrList_pop_front(&ioThread->writes);
        LeaveCriticalSection(&ioThread->cs);
        if (io)
        {
            task = task_findHash(io->taskHash);
            if (task)
            {
                if (task->action & TS_DOWNLOADING)
                    io->success = task_writeFilePiece(task, io->file, io->piece, io->pieceData, io->pieceLen);
                else if (task->action & TS_UPDATING)
                    io->success = task_writeTmpPiece(task, io->file, io->piece, io->pieceData, io->pieceLen);
                else
                    io->success = FALSE;

                tcp_postEvent(io);
            }
            else
            {
                debugf("IO write error: task not exist: %s\r\n", io->taskHash);
                free(io->pieceData); free(io);
            }

            continue;
        }

        EnterCriticalSection(&ioThread->cs);
        for (io=NULL, list=ioThread->reads; list; list=list->next)
        {
            io = list->data;
            if (!io->bOperated) break;
        }
        LeaveCriticalSection(&ioThread->cs);
        if (io && !io->bOperated)
        {
            io->success = FALSE;
            io->bOperated = 1;

            task = task_findHash(io->taskHash);
            if (task)
            {
                if (1 == bitset_check(&task->bitset, io->file->pieceOffset+io->piece))
                    io->success = task_readFilePiece(task, io->file, io->piece, io->pieceData, io->pieceLen);
                else if (1 == bitset_check(&task->tmp.bitset, io->file->pieceOffset+io->piece))
                    io->success = task_readTmpPiece(task, io->file, io->piece, io->pieceData, io->pieceLen);
            }
            tcp_postEvent(io);

            continue;
        }

        EnterCriticalSection(&ioThread->cs);
        for (io=NULL, list=ioThread->reads; list; list=list->next)
        {
            io = list->data;
            if (!io->bOperated) break;
        }
        if (!ioThread->writes && !(io && !io->bOperated))
            ResetEvent(ioThread->hEvent);
        LeaveCriticalSection(&ioThread->cs);
    }

    for (list=ioThread->reads; list; list=list->next)
    {
        io = list->data;
        free(io->pieceData);
    }
    ptrList_free(&ioThread->reads, free);

    for (list=ioThread->writes; list; list=list->next)
    {
        io = list->data;
        free(io->pieceData);
    }
    ptrList_free(&ioThread->writes, free);

    SetEvent(ioThread->hEventStopped);

    return 0;
}

void task_cancelAllIo(struct task *task)
{
    struct ptrList *list, *listD;
    struct tcp_custom_task_io *io;
    int i;

    for (i=0; i<MAX_IO_THREADS; i++)
    {
        EnterCriticalSection(&g_ioThreads[i].cs);

        list = g_ioThreads[i].writes;
        while (list)
        {
            io = list->data;
            if (io->task == task)
            {
                listD = list; list = list->next;
                ptrList_remove_node(&g_ioThreads[i].writes, listD);

                free(io->pieceData);
                free(io);
            }
            else list = list->next;
        }

        list = g_ioThreads[i].reads;
        while (list)
        {
            io = list->data;
            if (io->task == task)
            {
                listD = list; list = list->next;
                ptrList_remove_node(&g_ioThreads[i].reads, listD);

                free(io->pieceData);
                free(io);
            }
            else list = list->next;
        }

        LeaveCriticalSection(&g_ioThreads[i].cs);
    }
}


BOOL taskIo_startup()
{
    int i;

    for (i=0; i<MAX_IO_THREADS; i++)
    {
        InitializeCriticalSection(&g_ioThreads[i].cs);
        g_ioThreads[i].hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        g_ioThreads[i].hEventStop = CreateEvent(NULL, TRUE, FALSE, NULL);
        g_ioThreads[i].hEventStopped = CreateEvent(NULL, TRUE, FALSE, NULL);
        CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskIoThreadProc, &g_ioThreads[i], 0, NULL));
    }

    return TRUE;
}

void taskIo_cleanup()
{
    int i;

    for (i=0; i<MAX_IO_THREADS; i++)
    {
        if (!g_ioThreads[i].hEventStop) continue;

        SetEvent(g_ioThreads[i].hEventStop);
        WaitForSingleObject(g_ioThreads[i].hEventStopped, INFINITE);
        CloseHandle(g_ioThreads[i].hEvent);
        CloseHandle(g_ioThreads[i].hEventStop);
        CloseHandle(g_ioThreads[i].hEventStopped);
        DeleteCriticalSection(&g_ioThreads[i].cs);

        memset(&g_ioThreads[i], 0, sizeof(struct io_thread));
    }
}

