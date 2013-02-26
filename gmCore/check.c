#include "gmCore.h"

static BOOL taskCheck_cmpOld(struct task *task, struct idx *idxOld)
{
    UINT32 i, j, k, pieceLen;
    CHAR szTmp[512];

    task->bytesDownloaded = 0;
    task->piecesDownloaded = 0;
    bitset_clearAll(&task->bitset);
    for (i=0; i<task->idx.fileCount; i++)
    {
        task->idx.files[i]->bytesDownloaded = 0;
        task->idx.files[i]->piecesDownloaded = 0;
    }

    for (i=0; i<task->idx.fileCount; i++)
    {
        for (j=0; j<idxOld->fileCount; j++)
        {
            if (_wcsicmp(task->idx.files[i]->fileName, idxOld->files[j]->fileName))
                continue;

            for (k=0; k<min(task->idx.files[i]->pieceCount, idxOld->files[j]->pieceCount); k++)
            {
                if (0==memcmp(task->idx.files[i]->hash+k*20, idxOld->files[j]->hash+k*20, 20))
                {
                    bitset_set(&task->bitset, task->idx.files[i]->pieceOffset+k);
                    pieceLen = task_getPieceLength(task, task->idx.files[i], k);
                    task->idx.files[i]->bytesDownloaded += pieceLen;
                    task->idx.files[i]->piecesDownloaded ++;
                    task->bytesDownloaded += pieceLen;
                    task->piecesDownloaded ++;
                }
            }
            //debugf("%S %I64u:%u %I64u:%u %I64u:%u\r\n", task->idx.files[i]->fileName,
            //    task->idx.files[i]->bytes, task->idx.files[i]->pieceCount,
            //    idxOld->files[j]->bytes, idxOld->files[j]->pieceCount,
            //    task->idx.files[i]->bytesDownloaded, task->idx.files[i]->piecesDownloaded);
            break;
        }
    }
    task->bytesToDownload = task->idx.bytes - task->bytesDownloaded;
    task->piecesToDownload = task->idx.pieceCount - task->piecesDownloaded;

    bitset_copy(&task->bitsetRequested, &task->bitset);


    if (task->bytesToDownload == 0)
    {
        debugf("[check] %s task completed, seeding...\r\n", task->idx.id);
        task->action = TS_SEEDING;

        task_correcrAllFileTime(task);
        idx_cleanDirectory(&task->idx, task->dir);
    }
    else if (!g_options.updateMode || task->bytesToDownload > task->idx.bytes*7/10)
    {
        debugf("[check] %s should download: [%04x], "
            "bytesTotal: %I64u, piecesTotal: %u, "
            "bytestoDownload: %I64u piecesToDownload: %u\r\n",
            task->idx.id, task->action, task->idx.bytes, task->idx.pieceCount,
            task->bytesToDownload, task->piecesToDownload);

        task->action = TS_DOWNLOADING;

        if (!task_createDirectories(task) || !task_allocFiles(task, NULL))
        {
            task_setError(task, ERR_DISK_SPACE);
            debugf("[check] %s cannot allocate disk space\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_DISK_SPACE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }
    }
    else
    {
        debugf("[check] %s should update: [%04x], "
            "bytesTotal: %I64u, piecesTotal: %u, "
            "bytesToDownload: %I64d, piecesToDownload: %u\r\n",
            task->idx.id, task->action, task->idx.bytes, task->idx.pieceCount,
            task->bytesToDownload, task->piecesToDownload);

        task->action = TS_UPDATING;

        if (!task_allocTmpFile(task))
        {
            task_setError(task, ERR_DISK_SPACE);
            debugf("[check] %s cannot create temp file\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_DISK_SPACE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }
    }

    return TRUE;
}


static void CALLBACK taskCheck_CreateIdxCB(struct create_idx *cs)
{
    struct task *task = cs->cbParam;
    time_t currTime;

    if (WaitForSingleObject(task->check.stopEvent, 0)==WAIT_OBJECT_0)
    {
        SetEvent(cs->hEventStop);
        return;
    }

    task = cs->cbParam;

    switch (cs->status)
    {
    case 1:     // 查找文件
        task->check.total = 0;
        task->check.completed = 0;
        task->check.status = 1;
        break;
    case 2:     // 计算HASH
        task->check.total = cs->totalPieces;
        task->check.completed = cs->completedPieces;
        task->check.status = 2;
        break;
    }

    time(&currTime);
    if (currTime > task->lastSentProgressTime)
    {
        task->lastSentProgressTime = currTime;
        admin_sendCheckingProgress(task, task->check.completed, task->check.total);
    }
}

// create seed of old version
static BOOL taskCheck_createIdxOfOldVer(struct task *task)
{
    struct create_idx cs = { 0 };
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    CHAR szTmp[512];
    struct idx idx;
    int errCreateIdx;

    strcpy_s(cs.id, MAX_ID_LEN, task->idx.id);
    wcscpy_s(cs.dir, MAX_PATH, task->dir);
    cs.pieceLength = task->idx.pieceLength;
    cs.createCB = taskCheck_CreateIdxCB;
    cs.cbParam = task;

    errCreateIdx = createIdx(&cs);

    if (errCreateIdx)
    {
        switch (errCreateIdx)
        {
        case ERR_CREATE_IDX_USER_BREAK:
            debugf("[check] creating idx of old version stopped by user: %s\r\n", task->idx.id);
            break;
        default:
            task_setError(task, ERR_NEW_IDX);
            debugf("[check] creating idx FAILED: %s (errCode:%d)\r\n", task->idx.id, errCreateIdx);
            sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_NEW_IDX);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            break;
        }
        return FALSE;
    }
    //debugf("[check] creating idx of old version OK: %s %S\r\n", task->idx.id, task->dir);

    swprintf_s(fileName, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        task->dir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(fileName, &idx))
    {
        task_setError(task, ERR_NEW_IDX);
        debugf("cannot load new created idx: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_NEW_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        return FALSE;
    }

    taskCheck_cmpOld(task, &idx);

    idx_free(&idx);

    return TRUE;
}


static void taskCheck_PostTcpEvent(struct task *task, const CHAR *hashOld, BOOL success)
{
    struct tcp_custom_task_check *tcpCustom;

    tcpCustom = malloc(sizeof(struct tcp_custom_task_check));
    if (tcpCustom)
    {
        memset(tcpCustom, 0, sizeof(struct tcp_custom_task_check));
        tcpCustom->ioType = TCP_CUSTOM_TASK_CHECK;
        tcpCustom->task = task;
        strcpy_s(tcpCustom->hashOld, MAX_HASH_LEN, hashOld);
        tcpCustom->success = success;
        tcp_postEvent(tcpCustom);
    }
}

BOOL taskCheck_checkTmpFile(struct task *task)
{
    UINT32 i, piece, pieceInFile, pieceLen, read, success;
    struct file *file;
    LARGE_INTEGER li;
    UCHAR *pieceData, hash[20];

    if (!task_openTmpFile(task)) return FALSE;

    if (!GetFileSizeEx(task->tmp.hFile, &li) ||
        li.QuadPart != ((INT64)task->piecesToDownload)*(sizeof(UINT32)+task->idx.pieceLength))
        return FALSE;

    pieceData = malloc(task->idx.pieceLength);
    SetFilePointer(task->tmp.hFile, 0, NULL, FILE_BEGIN);
    success = TRUE;

    task->tmp.bytesDownloaded = 0;
    task->tmp.piecesDownloaded = 0;
    bitset_clearAll(&task->tmp.bitset);
    for (i=0; i<task->idx.fileCount; i++)
    {
        task->tmp.files[i].bytesDownloaded = 0;
        task->tmp.files[i].piecesDownloaded = 0;
    }

    for (i=0; i<task->piecesToDownload; i++)
    {
        if (WaitForSingleObject(task->check.stopEvent, 0)==WAIT_OBJECT_0)
        { success = FALSE; break; }

        piece = task->tmp.pieceTable[i];

        file = task_getFileInfo(task, piece);
        if (!file)
        { success = FALSE; break; }

        piece -= file->pieceOffset;
        pieceLen = task_getPieceLength(task, file, piece);

        if (!ReadFile(task->tmp.hFile, &pieceInFile, sizeof(pieceInFile), &read, NULL) ||
            read != sizeof(pieceInFile) ||
            pieceInFile != piece ||
            !ReadFile(task->tmp.hFile, pieceData, task->idx.pieceLength, &read, NULL) ||
            read != task->idx.pieceLength)
        { success = FALSE; break; }

        sha1(pieceData, pieceLen, hash);
        if (memcmp(file->hash+piece*20, hash, 20))
        { success = FALSE; break; }

        if (1==bitset_check(&task->bitset, piece+file->pieceOffset)) // dup
        { success = FALSE; break; }

        task->tmp.bytesDownloaded += pieceLen;
        task->tmp.piecesDownloaded ++;
        task->tmp.files[file->idxInFiles].bytesDownloaded += pieceLen;
        task->tmp.files[file->idxInFiles].piecesDownloaded ++;
        bitset_set(&task->tmp.bitset, piece+file->pieceOffset);

        task->check.completed ++;
    }

    free(pieceData);

    return success;
}

BOOL taskCheck_doJob(struct task *task)
{
    struct idx idx;
    WCHAR wId[MAX_ID_LEN], szPath[MAX_PATH];
    CHAR szTmp[512];

    task_closeAllFiles(task);

    time(&task->check.lastActionTime);
    task->errorCode = 0;

    if (!svr_GetIdx(g_svr, task->idx.id))
    {
        task_setError(task, ERR_NET_IDX);
        debugf("[taskCheck] cannot get idx from server: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_NET_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        return FALSE;
    }
    //debugf("[check] getIdx OK: %s\r\n", task->idx.id);

    swprintf_s(szPath, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idx) || strcmp(task->idx.id, idx.id))
    {
        task_setError(task, ERR_IDX);
        debugf("[taskCheck] cannot load idx file: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        idx_free(&idx);
        return FALSE;
    }
    //debugf("[check] idx_open OK: %s\r\n", task->idx.id);

    if (strcmp(task->idx.hash, idx.hash)==0) // hash not changed
        idx_free(&idx);
    else if (task->idx.hash[0]) // hash changed
    {
        idx_free(&task->idx);
        task->idx = idx;
    }
    else // new task
    {
        task->idx = idx;

        task_getDestDir(task->idx.id, task->idx.name, szPath);
        if (!szPath[0])
        {
            task_setError(task, ERR_IDX);
            debugf("[taskCheck] cannot locate dir: %s\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, ERR_IDX);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }

        wcscpy_s(task->dir, MAX_PATH, szPath);
        //debugf("[check] got dir: %s %S\r\n", task->idx.id, task->dir);
    }

    bitset_init(&task->bitset, task->idx.pieceCount);
    bitset_init(&task->bitsetRequested, task->idx.pieceCount);

    return taskCheck_createIdxOfOldVer(task);
}

static unsigned __stdcall TaskCheck_ThreadProc(LPVOID param)
{
    struct task *task = (struct task *)param;
    CHAR hashOld[MAX_HASH_LEN];
    BOOL success;

    ResetEvent(task->check.stoppedEvent);

    strcpy_s(hashOld, MAX_HASH_LEN, task->idx.hash);
    success = taskCheck_doJob(task);
    taskCheck_PostTcpEvent(task, hashOld, success);

    CloseHandle(task->check.stopEvent);
    task->check.stopEvent = NULL;

    SetEvent(task->check.stoppedEvent);

    return 0;
}

BOOL taskCheck_begin(struct task *task)
{
    if (task->check.stopEvent) return TRUE;

    task->check.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskCheck_ThreadProc, task, 0, NULL));

    return TRUE;
}

void taskCheck_end(struct task *task)
{
    if (!task->check.stopEvent) return;

    SetEvent(task->check.stopEvent);
    WaitForSingleObject(task->check.stoppedEvent, INFINITE);

    ResetEvent(task->check.stoppedEvent);
}

