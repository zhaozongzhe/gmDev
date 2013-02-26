#include "gmCore.h"

static void CALLBACK taskUpload_CreateIdxCB(struct create_idx *cs)
{
    struct task *task = (struct task *)cs->cbParam;
    time_t currTime;

    if (WaitForSingleObject(task->upload.stopEvent, 0)==WAIT_OBJECT_0)
    {
        SetEvent(cs->hEventStop);
        return;
    }

    task = (struct task *)cs->cbParam;

    switch (cs->status)
    {
    case 1:     // 查找文件
        task->upload.total = 0;
        task->upload.completed = 0;
        task->upload.status = 1;
        break;
    case 2:     // 计算HASH
        task->upload.total = cs->totalPieces;
        task->upload.completed = cs->completedPieces;
        task->upload.status = 2;
        break;
    }

    time(&currTime);
    if (currTime > task->lastSentProgressTime)
    {
        task->lastSentProgressTime = currTime;
        admin_sendUploadingProgress(task, "creating_idx", task->upload.completed, task->upload.total);
    }
}

static BOOL taskUpload_createIdx(struct task *task)
{
    struct create_idx cs = { 0 };
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH], fileName2[MAX_PATH];
    CHAR szTmp[512];
    int errCreateIdx;

    strcpy_s(cs.id, MAX_ID_LEN, task->idx.id);
    wcscpy_s(cs.dir, MAX_PATH, task->dir);
    wcscpy_s(cs.category, MAX_CATEGORY_LEN, task->idx.category);
    strcpy_s(cs.extraInfo, MAX_EXTRA_LEN, task->idx.extraInfo);
    cs.pieceLength = task->idx.pieceLength;
    cs.createCB = taskUpload_CreateIdxCB;
    cs.cbParam = task;

    errCreateIdx = createIdx(&cs);

    if (errCreateIdx)
    {
        switch (errCreateIdx)
        {
        case ERR_CREATE_IDX_USER_BREAK:
            debugf("[upload] creating idx of old version stopped by user: %s\r\n", task->idx.id);
            break;
        default:
            task_setError(task, ERR_NEW_IDX);
            debugf("[upload] creating idx FAILED: %s (errCode:%d)\r\n", task->idx.id, errCreateIdx);
            sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_NEW_IDX);
            admin_sendMsg(STR_UPLOADING_ERROR, szTmp);
            break;
        }
        return FALSE;
    }

    debugf("[upload] idx created: %s\r\n", task->idx.id);

    swprintf_s(fileName, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        task->dir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    swprintf_s(fileName2, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME, g_workDir, wId);
    CopyFileW(fileName, fileName2, FALSE);

    return TRUE;
}


static void taskUpload_PostTcpEvent(struct task *task, const CHAR *hashOld, BOOL success)
{
    struct tcp_custom_task_upload *tcpCustom;

    tcpCustom = (struct tcp_custom_task_upload *)malloc(sizeof(struct tcp_custom_task_upload));
    if (tcpCustom)
    {
        memset(tcpCustom, 0, sizeof(struct tcp_custom_task_upload));
        tcpCustom->ioType = TCP_CUSTOM_TASK_UPLOAD;
        tcpCustom->task = task;
        strcpy_s(tcpCustom->hashOld, MAX_HASH_LEN, hashOld);
        tcpCustom->success = success;
        tcp_postEvent(tcpCustom);
    }
}

struct uploadNotify_data
{
    int sockIdx;
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
};
static struct ptrList *g_uploadNotifyData = NULL;

static BOOL SendUploadNotify(int sockIdx)
{
    struct uploadNotify_data *ud;
    struct ptrList *list;
    CHAR buf[128];

    for (list=g_uploadNotifyData, ud=NULL; list; list=list->next)
    {
        ud = (struct uploadNotify_data *)list->data;
        if (ud->sockIdx == sockIdx) break;
    }
    if (!ud || ud->sockIdx != sockIdx) return FALSE;

    *((UINT32 *)buf) = htonl(6+MAX_ID_LEN+MAX_HASH_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_NEED_UPDATE;
    *((UINT32 *)(buf+6)) = 0; // no crypt
    strcpy_s(buf+10, MAX_ID_LEN, ud->id);
    strcpy_s(buf+10+MAX_ID_LEN, MAX_HASH_LEN, ud->hash);

    tcp_send(sockIdx, buf, 10+MAX_ID_LEN+MAX_HASH_LEN, NULL, 0);

    ptrList_remove_node(&g_uploadNotifyData, list);
    free(ud);

    return TRUE;
}

static BOOL RemoveUpdateNotify(int sockIdx)
{
    struct uploadNotify_data *ud;
    struct ptrList *list;

    for (list=g_uploadNotifyData, ud=NULL; list; list=list->next)
    {
        ud = list->data;
        if (ud->sockIdx == sockIdx) break;
    }
    if (!ud || ud->sockIdx != sockIdx) return FALSE;

    ptrList_remove_node(&g_uploadNotifyData, list);
    free(ud);

    return TRUE;
}

static int CALLBACK OnUploadNotifyTcpEvent(int sockIdx, int msgCode, UCHAR *data, int dataLen)
{
    switch (msgCode)
    {
    case TCP_EV_SEND:
        return -1;

    case TCP_EV_RECEIVE:
        return -1;

    case TCP_EV_ACCEPT:
        break;

    case TCP_EV_CONNECT:
        while (SendUploadNotify(sockIdx));
        break;

    case TCP_EV_CLOSE:
        while (RemoveUpdateNotify(sockIdx));
        break;

    case TCP_EV_TIMER:
        break;
    }

    return 0;
}

static void UploadNotifyPeers(struct task *task)
{
    CHAR *pIp, *pPort, *pNext;
    int sockIdx;

    pIp = task->uploadNotifyPeers;
    while (1)
    {
        pNext = strstr(pIp, "\r\n");
        if (!pNext) break; *pNext = 0; pNext += 2;
        pPort = strchr(pIp, ':');
        if (!pPort) break; *pPort = 0; pPort ++;

        sockIdx = tcp_connect(pIp, atoi(pPort), OnUploadNotifyTcpEvent);
        if (sockIdx >= 0)
        {
            struct uploadNotify_data *ud;
            ud = (struct uploadNotify_data *)malloc(sizeof(struct uploadNotify_data));
            memset(ud, 0, sizeof(struct uploadNotify_data));
            ud->sockIdx = sockIdx;
            strcpy_s(ud->id, MAX_ID_LEN, task->idx.id);
            strcpy_s(ud->hash, MAX_HASH_LEN, task->idx.hash);
            ptrList_append(&g_uploadNotifyData, ud);
        }
    }
}

static BOOL CALLBACK idx_FindFileCB(struct find_files_stat *ffs)
{
    return TRUE;
}

static BOOL _IsIdxFile(WCHAR *fileName)
{
    int i;
    for (i=(int)wcslen(fileName); i>0; i--)
        if (fileName[i] == L'.')
            return (0==_wcsicmp(&fileName[i], IDX_EXTNAME));
    return FALSE;
}

static BOOL taskUpload_checkOldVer(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], szPath[MAX_PATH];
    struct idx idx;
    struct find_files ff;
    struct ptrList *files = NULL;
    struct ptrList *dirs = NULL;
    struct ptrList *li;

    swprintf_s(szPath, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idx) || strcmp(task->idx.id, idx.id))
    { idx_free(&idx); return FALSE; }

    if (!idx_checkFilesTimeAndSize(&idx, task->dir))
    { idx_free(&idx); return FALSE; }

    // 查找目录和文件
    memset(&ff, 0, sizeof(ff));
    ff.files = &files;
    ff.directories = &dirs;
    ff.dir = task->dir;
    ff.findFilesCB = idx_FindFileCB;
    ff.cbParam = NULL;
    FindFiles(&ff);

    for (li=files; li; li=li->next)
    {
        if (_IsIdxFile((WCHAR *)li->data))
        { free(li->data); ptrList_remove_node(&files, li); break; }
    }
    if (ptrList_size(files) != idx.fileCount ||
        ptrList_size(dirs) != idx.directoryCount)
    {
        ptrList_free(&files, free);
        ptrList_free(&dirs, free);
        idx_free(&idx);
        return FALSE;
    }

    ptrList_free(&files, free);
    ptrList_free(&dirs, free);
    idx_free(&idx);
    return TRUE;
}

static BOOL taskUpload_doJob(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], szPath[MAX_PATH];
    struct idx idx;
    UINT32 i;
    CHAR szTmp[512];

    if (!taskUpload_checkOldVer(task))
    {
        if (!taskUpload_createIdx(task)) return FALSE;
    }

    if (!svr_SetIdx(g_svr, task->idx.id, task->uploadPwd))
    {
        debugf("[taskUpload] cannot upload idx to server: %s\r\n", task->idx.id);
        task_setError(task, ERR_NET_IDX2);
        sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_NET_IDX2);
        admin_sendMsg(STR_UPLOADING_ERROR, szTmp);
        return FALSE;
    }

    admin_sendUploadingProgress(task, "uploading_idx", 0, 0);

    swprintf_s(szPath, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    if (!idx_open(szPath, &idx) || strcmp(task->idx.id, idx.id))
    {
        debugf("[taskUpload] cannot load idx: %s\r\n", task->idx.id);
        task_setError(task, ERR_IDX);
        sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, ERR_IDX);
        admin_sendMsg(STR_UPLOADING_ERROR, szTmp);
        idx_free(&idx);
        return FALSE;
    }

    idx_free(&task->idx);
    task->idx = idx;

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

    // notify dedicated servers
    if (task->uploadNotifyPeers)
    {
        UploadNotifyPeers(task);

        free(task->uploadNotifyPeers);
        task->uploadNotifyPeers = NULL;
    }

    sprintf_s(szTmp, 512, "%s\t%d", task->idx.id, 0);
    admin_sendMsg(STR_UPLOADING_ERROR, szTmp);
    debugf("taskUpload OK %s\r\n", task->idx.id);

    return TRUE;
}

static unsigned __stdcall TaskUpload_ThreadProc(LPVOID param)
{
    struct task *task = (struct task *)param;
    CHAR hashOld[MAX_HASH_LEN];
    BOOL success;

    ResetEvent(task->upload.stoppedEvent);

    strcpy_s(hashOld, MAX_HASH_LEN, task->idx.hash);
    success = taskUpload_doJob(task);
    taskUpload_PostTcpEvent(task, hashOld, success);

    CloseHandle(task->upload.stopEvent);
    task->upload.stopEvent = NULL;

    SetEvent(task->upload.stoppedEvent);

    return 0;
}

BOOL taskUpload_begin(struct task *task)
{
    if (task->upload.stopEvent) return TRUE;

    task->upload.stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, TaskUpload_ThreadProc, task, 0, NULL));

    return TRUE;
}

void taskUpload_end(struct task *task)
{
    if (!task->upload.stopEvent) return;

    SetEvent(task->upload.stopEvent);
    WaitForSingleObject(task->upload.stoppedEvent, INFINITE);

    ResetEvent(task->upload.stoppedEvent);

    task->action &= ~TS_UPLOADING;
}

