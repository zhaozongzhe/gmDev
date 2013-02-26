#include "gmCore.h"

static BOOL taskPrepare_cmpOld(struct task *task, struct idx *idxOld, struct bitset *bsOld)
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
                if ((!bsOld || (bsOld && 1==bitset_check(bsOld, idxOld->files[j]->pieceOffset+k))) &&
                    memcmp(task->idx.files[i]->hash+k*20, idxOld->files[j]->hash+k*20, 20)==0)
                {
                    bitset_set(&task->bitset, task->idx.files[i]->pieceOffset+k);
                    pieceLen = task_getPieceLength(task, task->idx.files[i], k);
                    task->idx.files[i]->bytesDownloaded += pieceLen;
                    task->idx.files[i]->piecesDownloaded ++;
                    task->bytesDownloaded += pieceLen;
                    task->piecesDownloaded ++;
                }
            }
            break;
        }
    }
    task->bytesToDownload = task->idx.bytes - task->bytesDownloaded;
    task->piecesToDownload = task->idx.pieceCount - task->piecesDownloaded;

    bitset_copy(&task->bitsetRequested, &task->bitset);


    if (task->bytesToDownload == 0)
    {
        debugf("[prepare] %s task completed, seeding...\r\n", task->idx.id);
        task->action |= TS_SEEDING;
        task->action &= ~TS_DOWNLOADING;
        task->action &= ~TS_UPDATING;

        task_correcrAllFileTime(task);
        idx_cleanDirectory(&task->idx, task->dir);
    }
    else if (!g_options.updateMode || task->bytesToDownload > task->idx.bytes*7/10)
    {
        debugf("[prepare] %s should download: [%04x], "
            "bytesTotal: %I64u, piecesTotal: %u, "
            "bytestoDownload: %I64u piecesToDownload: %u\r\n",
            task->idx.id, task->action, task->idx.bytes, task->idx.pieceCount,
            task->bytesToDownload, task->piecesToDownload);

        task->action |= TS_DOWNLOADING;
        task->action &= ~TS_UPDATING;
        task->action &= ~TS_SEEDING;

        if (!task_createDirectories(task) || !task_allocFiles(task, NULL))
        {
            task_setError(task, ERR_DISK_SPACE);
            debugf("[prepare] %s cannot allocate disk space\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_DISK_SPACE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }
    }
    else
    {
        debugf("[prepare] %s should update: [%04x], "
            "bytesTotal: %I64u, piecesTotal: %u, "
            "bytesToDownload: %I64d, piecesToDownload: %u\r\n",
            task->idx.id, task->action, task->idx.bytes, task->idx.pieceCount,
            task->bytesToDownload, task->piecesToDownload);

        task->action = TS_UPDATING;
        task->action &= ~TS_DOWNLOADING;
        task->action &= ~TS_SEEDING;

        if (!task_allocTmpFile(task))
        {
            task_setError(task, ERR_DISK_SPACE);
            debugf("[prepare] %s cannot create temp file\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_DISK_SPACE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }
    }

    return TRUE;
}

static void CALLBACK taskPrepare_CreateIdxCB(struct create_idx *cs)
{
    struct task *task = cs->cbParam;

    if (WaitForSingleObject(task->prepare.stopEvent, 0)==WAIT_OBJECT_0)
    {
        SetEvent(cs->hEventStop);
        return;
    }

    task = (struct task *)cs->cbParam;

    switch (cs->status)
    {
    case 1:     // 查找文件
        task->prepare.total = 0;
        task->prepare.completed = 0;
        task->prepare.status = 1;
        break;
    case 2:     // 计算HASH
        task->prepare.total = cs->totalPieces;
        task->prepare.completed = cs->completedPieces;
        task->prepare.status = 2;
        break;
    }
}

// create seed of old version
static BOOL taskPrepare_createIdxOfOldVer(struct task *task)
{
    struct create_idx cs = { 0 };
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    CHAR szTmp[512];
    struct idx idx;
    int errCreateIdx;

    strcpy_s(cs.id, MAX_ID_LEN, task->idx.id);
    wcscpy_s(cs.dir, MAX_PATH, task->dir);
    cs.pieceLength = task->idx.pieceLength;
    cs.createCB = taskPrepare_CreateIdxCB;
    cs.cbParam = task;

    errCreateIdx = createIdx(&cs);

    if (errCreateIdx)
    {
        switch (errCreateIdx)
        {
        case ERR_CREATE_IDX_USER_BREAK:
            debugf("[prepare] creating idx of old version stopped by user: %s\r\n", task->idx.id);
            break;
        default:
            task_setError(task, ERR_NEW_IDX);
            debugf("[prepare] creating idx of old version FAILED: %s (errCode:%d)\r\n", task->idx.id, errCreateIdx);
            sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_NEW_IDX);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            break;
        }
        return FALSE;
    }

    debugf("old version idx created: %s\r\n", task->idx.id);

    swprintf_s(fileName, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        task->dir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(fileName, &idx))
    {
        task_setError(task, ERR_NEW_IDX);
        debugf("cannot load new created idx: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_NEW_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        return FALSE;
    }

    taskPrepare_cmpOld(task, &idx, NULL);

    idx_free(&idx);
    return TRUE;
}

static void taskPrepare_PostTcpEvent(struct task *task, const CHAR *hashOld, BOOL success)
{
    struct tcp_custom_task_prepare *tcpCustom;

    tcpCustom = malloc(sizeof(struct tcp_custom_task_prepare));
    if (tcpCustom)
    {
        memset(tcpCustom, 0, sizeof(struct tcp_custom_task_prepare));
        tcpCustom->ioType = TCP_CUSTOM_TASK_PREPARE;
        tcpCustom->task = task;
        strcpy_s(tcpCustom->hashOld, MAX_HASH_LEN, hashOld);
        tcpCustom->success = success;
        tcp_postEvent(tcpCustom);
    }
}

// 2种情况:
// 1、用户开要求始新的任务
// 2、旧的任务尚在运行中(可能未完成也可能已经完成)
BOOL taskPrepare_doJob(struct task *task)
{
    struct idx idx;
    WCHAR wId[MAX_ID_LEN], szPath[MAX_PATH];
    DWORD tickCnt;
    struct idx idxOld = { 0 };
    struct bitset bsOld = { 0 };
    CHAR szTmp[512];

    task_closeAllFiles(task);

    time(&task->prepare.lastActionTime);
    task->errorCode = 0;

    // 从服务器获取最新的种子文件
    tickCnt = GetTickCount();
    if (!svr_GetIdx(g_svr, task->idx.id))
    {
        task_setError(task, ERR_NET_IDX);

        debugf("[taskPrepare] cannot get idx from server: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_NET_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        return FALSE;
    }

    swprintf_s(szPath, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idx) || strcmp(task->idx.id, idx.id))
    {
        task_setError(task, ERR_IDX);

        debugf("[taskPrepare] cannot load idx: %s\r\n", task->idx.id);
        sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_IDX);
        admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
        idx_free(&idx);
        return FALSE;
    }

    // 任务已经在运行
    if (0 == strcmp(idx.hash, task->idx.hash))
    {
        debugf("[taskPrepare] task already running with same hash: %s %s\r\n", idx.id, idx.hash);
        idx_free(&idx);
        return TRUE;
    }

    // 任务已经在运行, 但资源已经更新
    if (task->idx.hash[0])
    {
        debugf("[taskPrepare] task running with a different hash, %s %s(new) %s\r\n",
            idx.id, idx.hash, task->idx.hash);

        idxOld = task->idx;
        task->idx = idx;

        bitset_copy(&bsOld, &task->bitset);
        bitset_init(&task->bitset, task->idx.pieceCount);
        bitset_init(&task->bitsetRequested, task->idx.pieceCount);

        if (task->action & TS_SEEDING)
            taskPrepare_cmpOld(task, &idxOld, NULL);
        else if (task->action & TS_UPDATING)
        {
            task_discardTmpData(task);
            taskPrepare_cmpOld(task, &idxOld, NULL);
        }
        else if (task->action & TS_DOWNLOADING)
            taskPrepare_cmpOld(task, &idxOld, &bsOld);

        idx_free(&idxOld);
        bitset_free(&bsOld);

        debugf("[taskPrepare] reload %s bytesTotal:%I64u, piecesTotal:%u, "
            "bytesToDownload: %I64u, piecesToDownload: %u\r\n",
            task->idx.id, task->idx.bytes, task->idx.pieceCount,
            task->bytesToDownload, task->piecesToDownload);

        return TRUE;
    }


    // 新的任务, 需要检查旧版本

    task->idx = idx;
    bitset_init(&task->bitset, task->idx.pieceCount);
    bitset_init(&task->bitsetRequested, task->idx.pieceCount);

    // 确定目录
    if (!task->dir[0])
    {
        WCHAR destDir[MAX_PATH];

        task_getDestDir(task->idx.id, task->idx.name, destDir);
        if (!destDir[0])
        {
            task_setError(task, ERR_DISK_SPACE);
            debugf("[taskPrepare] cannot locate dir: %s\r\n", task->idx.id);
            sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, ERR_DISK_SPACE);
            admin_sendMsg(STR_DOWNLOADING_ERROR, szTmp);
            return FALSE;
        }

        wcscpy_s(task->dir, MAX_PATH, destDir);
    }

    // 检查旧版本
    swprintf_s(szPath, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        task->dir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idxOld))
    {
        debugf("[taskPrepare] Old version not exist: %s, now check...\r\n", task->idx.id);
        return taskPrepare_createIdxOfOldVer(task);
    }

    if (!idx_checkFilesTimeAndSize(&idxOld, task->dir))
    {
        debugf("[taskPrepare] old version error: %s，will check...\r\n", task->idx.id);
        idx_free(&idxOld);
        return taskPrepare_createIdxOfOldVer(task);
    }
    else
    {
        BOOL success;
        debugf("[taskPrepare] old version is OK, just compare: %s\r\n", task->idx.id);
        success = taskPrepare_cmpOld(task, &idxOld, NULL);
        idx_free(&idxOld);
        return success;
    }
}

static unsigned __stdcall TaskPrepare_ThreadProc(LPVOID param)
{
    struct task *task = (struct task *)param;
    CHAR hashOld[MAX_HASH_LEN];
    BOOL success;

    ResetEvent(task->prepare.stoppedEvent);

    strcpy_s(hashOld, MAX_HASH_LEN, task->idx.hash);
    success = taskPrepare_doJob(task);
    taskPrepare_PostTcpEvent(task, hashOld, success);

    CloseHandle(task->prepare.stopEvent);
    task->prepare.stopEvent = NULL;

    SetEvent(task->prepare.stoppedEvent);

    return 0;
}

BOOL taskPrepare_begin(struct task *task)
{
    if (task->prepare.stopEvent) return TRUE;

    task->prepare.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskPrepare_ThreadProc, task, 0, NULL));

    return TRUE;
}

void taskPrepare_end(struct task *task)
{
    if (!task->prepare.stopEvent) return;

    SetEvent(task->prepare.stopEvent);
    WaitForSingleObject(task->prepare.stoppedEvent, INFINITE);

    ResetEvent(task->prepare.stoppedEvent);

    task->action &= ~TS_PREPARING;
}

