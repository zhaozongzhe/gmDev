#include "gmCore.h"

struct ptrArray g_localIdxSortedById = { 0 };   // array of idx_local
struct ptrArray g_localIdxSortedByHash = { 0 };

struct ptrArray g_tasksAutoUpdate = { 0 };      // array of id
struct ptrArray g_tasksSeedingSI = { 0 };       // array of task sorted by id
struct ptrArray g_tasksSeedingSH = { 0 };       // array of task sorted by hash
struct ptrList *g_tasksDownloading = NULL;      // list of task, include: downloading,continuing,preparing,checking,transfering
struct ptrList *g_tasksUploading = NULL;        // list of task
struct ptrList *g_tasksWaiting = NULL;          // list of id

int g_listenSock = -1;

struct rate_control g_rcRequest = { 0 };
struct rate_control g_rcDown = { 0 };
struct rate_control g_rcUp = { 0 };

int g_adminSocket = -1;
CRITICAL_SECTION g_csAdminMsg;
struct ptrArray g_adminSockets = { 0 };

struct task *g_taskContinuing = NULL;
struct task *g_taskPreparing = NULL;
struct task *g_taskChecking = NULL;
struct task *g_taskUploading = NULL;

CHAR *task_isWaiting(const CHAR *id)
{
    struct ptrList *li;

    for (li=g_tasksWaiting; li; li=li->next)
    { if (strcmp((CHAR *)li->data, id) == 0) return (CHAR *)li->data; }

    return NULL;
}
CHAR *task_isAutoUpdate(const CHAR *id)
{
    return (CHAR *)ptrArray_findSorted(&g_tasksAutoUpdate, id);
}
struct task *task_isSeeding(const CHAR *id)
{
    struct task taskf = { 0 };

    strcpy_s(taskf.idx.id, MAX_ID_LEN, id);
    return (struct task *)ptrArray_findSorted(&g_tasksSeedingSI, &taskf);
}
struct task *task_isDownloading(const CHAR *id)
{
    struct ptrList *li;
    struct task *task;

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.id, id)) return task;
    }
    return NULL;
}
struct task *task_isUploading(const CHAR *id)
{
    struct ptrList *li;
    struct task *task;

    for (li=g_tasksUploading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.id, id)) return task;
    }
    return NULL;
}
struct task *task_findHash(const CHAR *hash)
{
    struct task *task, taskf = { 0 };
    struct ptrList *li;

    strcpy_s(taskf.idx.hash, MAX_HASH_LEN, hash);
    task = (struct task *)ptrArray_findSorted(&g_tasksSeedingSH, &taskf);
    if (task) return task;

    for(li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.hash, hash)) return task;
    }

    return NULL;
}
void task_setError(struct task *task, UINT32 errCode)
{
    task->action |= TS_ERROR;
    task->errorCode = errCode;
}

void task_getSpeed(struct task *task, struct speed *speed)
{
    DWORD currTick = GetTickCount();

    speed->down = rateCtrl_getSpeed(&task->rcDown, currTick);
    speed->up = rateCtrl_getSpeed(&task->rcUp, currTick);
}

UINT32 task_getPieceLength(struct task *task, struct file *fi, UINT32 piece)
{
    if (piece < fi->pieceCount - 1)
        return task->idx.pieceLength;
    else if (piece == fi->pieceCount - 1)
        return (UINT32)(fi->bytes - ((UINT64)task->idx.pieceLength)*piece);
    else return 0;
}

struct file *task_getFileInfo(struct task *task, UINT32 piece)
{
    struct file *file;
    UINT32 i;

    for (i=0; i<task->idx.fileCount; i++)
    {
        file = task->idx.files[i];
        if (piece >= file->pieceOffset &&
            piece < (file->pieceOffset + file->pieceCount))
            return file;
    }

    return NULL;
}

BOOL task_openFile(struct task *task, struct file *file, BOOL forceWrite, DWORD *errCode)
{
    UINT32 op = GENERIC_READ;
    WCHAR fileName[MAX_PATH];
    CHAR szFileName[MAX_PATH];

    if (forceWrite || (task->action & TS_DOWNLOADING && file->bytesDownloaded < file->bytes))
        op |= GENERIC_WRITE;

    if (file->hFile != INVALID_HANDLE_VALUE)
    {
        if (file->accessMode >= op) return TRUE;
        CloseHandle(file->hFile);
        file->hFile = INVALID_HANDLE_VALUE;
    }

    swprintf_s(fileName, MAX_PATH, L"%s\\%s", task->dir, file->fileName);
    file->hFile = CreateFileW(fileName, op, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file->hFile == INVALID_HANDLE_VALUE)
    {
        if (errCode) *errCode = GetLastError();
        debugf("task_openFile ERROR %s code=%d\r\n",
            UnicodeToMbcs(fileName, szFileName, MAX_PATH), GetLastError());
        return FALSE;
    }

    time(&file->lastAccessTime);
    file->accessMode = op;

    if (errCode) *errCode = 0;

    return TRUE;
}

void task_closeFile(struct task *task, struct file *file)
{
    if (file->hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file->hFile);
        file->hFile = INVALID_HANDLE_VALUE;
    }
}

void task_closeAllFiles(struct task *task)
{
    struct file *file;
    UINT32 i;

    for (i=0; i<task->idx.fileCount; i++)
    {
        file = task->idx.files[i];
        if (file->hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(file->hFile);
            file->hFile = INVALID_HANDLE_VALUE;
        }
    }
}

static int cmp_dir(const void *p1, const void *p2) /* ptrList_sort */
{
    return ((int)wcslen(*((WCHAR **)p1)) - (int)wcslen(*((WCHAR **)p2)));
}
BOOL task_createDirectories(struct task *task)
{
    WCHAR szDir[MAX_PATH];
    CHAR szTmp[512];
    UINT32 i, success = TRUE;

    wcscpy_s(szDir, MAX_PATH, task->dir);
    wcscat_s(szDir, MAX_PATH, L"\\");
    if (!SureCreateDir(szDir))
    {
        debugf("cannot create dir: %s %s\r\n", task->idx.id, UnicodeToMbcs(szDir, szTmp, 512));
        success = FALSE;
    }

    for (i=0; i<task->idx.directoryCount; i++)
    {
        swprintf_s(szDir, MAX_PATH, L"%s\\%s\\", task->dir, task->idx.directories[i]);
        if (!SureCreateDir(szDir))
        {
            debugf("cannot create dir: %s %s\r\n", task->idx.id, UnicodeToMbcs(szDir, szTmp, 512));
            success = FALSE;
        }
    }

    return success;
}

static BOOL task_allocFile(struct task *task, struct file *file, DWORD *errCode)
{
    LARGE_INTEGER li;
    WCHAR fileName[MAX_PATH];
    CHAR szTmp[512];

    if (file->hFile != INVALID_HANDLE_VALUE &&
        !(file->accessMode & GENERIC_WRITE))
    {
        CloseHandle(file->hFile);
        file->hFile = INVALID_HANDLE_VALUE;
    }

    if (file->hFile == INVALID_HANDLE_VALUE)
    {
        swprintf_s(fileName, MAX_PATH, L"%s\\%s", task->dir, file->fileName);
        file->hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        if (file->hFile == INVALID_HANDLE_VALUE)
        {
            if (GetLastError() == ERROR_SHARING_VIOLATION)
            {
                if (errCode) *errCode = GetLastError();
                return FALSE;
            }
            file->hFile = SureCreateFile(fileName);
        }

        if (file->hFile == INVALID_HANDLE_VALUE)
        {
            debugf("[task_allocFile] cannot open file %s %s code=%d\r\n",
                task->idx.id, UnicodeToMbcs(fileName, szTmp, 512), GetLastError());
            return FALSE;
        }

        time(&file->lastAccessTime);
        file->accessMode = GENERIC_READ|GENERIC_WRITE;
    }

    if (!GetFileSizeEx(file->hFile, &li))
    {
        debugf("[task_allocFile] cannot SetFileSize %s %s code=%d\r\n",
            task->idx.id, UnicodeToMbcs(fileName, szTmp, 512), GetLastError());
        task_closeFile(task, file);
        return FALSE;
    }

    if (li.QuadPart != file->bytes)
    {
        li.QuadPart = (LONGLONG)file->bytes;
        SetFilePointerEx(file->hFile, li, NULL, FILE_BEGIN);
        if (!SetEndOfFile(file->hFile))
        {
            debugf("[task_allocFile] cannot SetFileSize %s %s code=%d\r\n",
                task->idx.id, UnicodeToMbcs(fileName, szTmp, 512), GetLastError());
            task_closeFile(task, file);
            return FALSE;
        }
    }

    task_closeFile(task, file);
    return TRUE;
}

BOOL task_allocFiles(struct task *task, DWORD *errCode)
{
    UINT32 i;

    if (errCode) *errCode = 0;

    for (i=0; i<task->idx.fileCount; i++)
        if (!task_allocFile(task, task->idx.files[i], errCode)) return FALSE;

    return TRUE;
}

#define TMP_DIR     L"X:\\TEMP"
#define TMP_SUFFIX  L".gmTmp"

static void task_setTmpFileName(struct task *task, const WCHAR *tmpDir)
{
    WCHAR wId[MAX_ID_LEN], szTmpDir[MAX_PATH];

    if (!tmpDir || !wcslen(tmpDir))
    {
        wcscpy_s(szTmpDir, MAX_PATH, TMP_DIR);
        szTmpDir[0] = task->dir[0];
        swprintf_s(task->tmp.fileName, MAX_PATH, L"%s\\%s"TMP_SUFFIX,
            szTmpDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    }
    else
    {
        wcscpy_s(szTmpDir, MAX_PATH, tmpDir);
        if (szTmpDir[wcslen(szTmpDir-1)] == L'\\')
            szTmpDir[wcslen(szTmpDir-1)] = L'\0';
        swprintf_s(task->tmp.fileName, MAX_PATH, L"%s\\%s"TMP_SUFFIX,
            szTmpDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    }
}

static BOOL task_chooseTmpDir(struct task *task)
{
    UINT64 tmpFileSize;
    ULARGE_INTEGER diskFree;
    WCHAR tmpPath[MAX_PATH];

    memset(task->tmp.fileName, 0, MAX_PATH*sizeof(WCHAR));

    tmpFileSize = ((UINT64)task->piecesToDownload) * (sizeof(UINT32)+task->idx.pieceLength);

    if (g_options.tmpDir[0])
    {
        wcscpy_s(tmpPath, MAX_PATH, g_options.tmpDir); tmpPath[2] = 0;
        GetDiskFreeSpaceExW(tmpPath, &diskFree, NULL, NULL);
        if (diskFree.QuadPart > (tmpFileSize + ((UINT64)g_options.diskSpaceReserve)*1024*1024))
        {
            task_setTmpFileName(task, g_options.tmpDir);
            return TRUE;
        }
    }

    wcscpy_s(tmpPath, MAX_PATH, task->dir); tmpPath[2] = 0;
    GetDiskFreeSpaceExW(tmpPath, &diskFree, NULL, NULL);
    if (diskFree.QuadPart > (tmpFileSize + ((UINT64)g_options.diskSpaceReserve)*1024*1024))
    {
        task_setTmpFileName(task, task->dir);
        return TRUE;
    }

    return FALSE;
}

BOOL task_allocTmpFile(struct task *task)
{
    LARGE_INTEGER li, fileSize;
    UINT32 i, pieceInFile;
    DWORD dwWriten;

    task_resetTmpData(task);

    if (!task_chooseTmpDir(task)) return FALSE;
    if (!task->tmp.fileName[0]) return FALSE;

    if (!task_openTmpFile(task)) return FALSE;

    if (!GetFileSizeEx(task->tmp.hFile, &fileSize))
        fileSize.QuadPart = 0;

    li.QuadPart = ((INT64)task->piecesToDownload) * (sizeof(UINT32) + task->idx.pieceLength);

    if (fileSize.QuadPart != li.QuadPart)
    {
        debugf("[allocTmpFile] %s, require:%I64d, bytesToDownload:%I64u piecesToDownload:%u...alocated: %I64d\r\n",
            task->idx.id, li.QuadPart, task->bytesToDownload, task->piecesToDownload, fileSize.QuadPart);

        SetFilePointerEx(task->tmp.hFile, li, NULL, FILE_BEGIN);
        if (!SetEndOfFile(task->tmp.hFile))
        {
            debugf("[allocTmpFile] FAILED: %s, %I64d\r\n", task->idx.id, li.QuadPart);
            return FALSE;
        }
    }

    for (i=0, pieceInFile=MAXUINT32; i<task->piecesToDownload; i++)
    {
        li.QuadPart = ((INT64)i) * (sizeof(UINT32) + task->idx.pieceLength);
        SetFilePointerEx(task->tmp.hFile, li, NULL, FILE_BEGIN);
        if (!WriteFile(task->tmp.hFile, &pieceInFile, sizeof(UINT32), &dwWriten, NULL) ||
            dwWriten != sizeof(UINT32))
        {
            debugf("[allocTmpFile] write brief FAILED: %s, %I64d\r\n", task->idx.id, li.QuadPart);
            return FALSE;
        }
    }

    return TRUE;
}

void task_resetTmpData(struct task *task)
{
    UINT32 i, j;

    task->tmp.bytesDownloaded = 0;
    task->tmp.piecesDownloaded = 0;

    if (task->tmp.pieceTable)
    {
        free(task->tmp.pieceTable);
        task->tmp.pieceTable = NULL;
    }
    if (task->piecesToDownload)
    {
        task->tmp.pieceTable = calloc(task->piecesToDownload, sizeof(UINT32));
        for (i=j=0; i<task->idx.pieceCount; i++)
        {
            if (0==bitset_check(&task->bitset, i))
                task->tmp.pieceTable[j++] = i;
        }
    }

    bitset_init(&task->tmp.bitset, task->bitset.bitCount);

    if (task->tmp.files) free(task->tmp.files);
    task->tmp.files = calloc(task->idx.fileCount, sizeof(struct file_tmp));
}

void task_discardTmpData(struct task *task)
{
    task->tmp.bytesDownloaded = 0;
    task->tmp.piecesDownloaded = 0;

    if (task->tmp.pieceTable)
    {
        free(task->tmp.pieceTable);
        task->tmp.pieceTable = NULL;
    }

    if (task->tmp.hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(task->tmp.hFile);
        task->tmp.hFile = INVALID_HANDLE_VALUE;
    }
    if (task->tmp.fileName[0])
    {
        DeleteFileW(task->tmp.fileName);
        task->tmp.fileName[0] = 0;
    }

    bitset_free(&task->tmp.bitset);
    if (task->tmp.files)
    {
        free(task->tmp.files);
        task->tmp.files = NULL;
    }
}

BOOL task_openTmpFile(struct task *task)
{
    if (task->tmp.hFile == INVALID_HANDLE_VALUE)
        task->tmp.hFile = CreateFileW(task->tmp.fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);

    return (task->tmp.hFile != INVALID_HANDLE_VALUE);
}

void task_correcrAllFileTime(struct task *task)
{
    UINT32 i;

    for (i=0; i<task->idx.fileCount; i++)
    {
        struct file *file = task->idx.files[i];
        UINT64 ft;

        if (!task_openFile(task, file, TRUE, NULL)) continue;

        ft = FileGetLastWriteTime(file->hFile);
        if (ft == file->fileTime)
        { task_closeFile(task, file); continue; }

        FileSetLastWriteTime(file->hFile, file->fileTime);

        task_closeFile(task, file);
    }
}

void task_copySeedFile(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName1[MAX_PATH], fileName2[MAX_PATH];

    swprintf_s(fileName1, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    swprintf_s(fileName2, MAX_PATH, L"%s\\%s"IDX_EXTNAME,
        task->dir, wId);
    CopyFileW(fileName1, fileName2, FALSE);
}

BOOL task_getDestDir(const CHAR *id, const WCHAR *name, WCHAR *destDir)
{
    struct idx_net *idxn, idxnf = { 0 };
    struct idx_local *idxl, idxlf = { 0 };

    destDir[0] = 0;

    strcpy_s(idxnf.id, MAX_ID_LEN, id);
    idxn = ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
    if (!idxn)
    {
        debugf("[getDestDir] invalid id: %s\r\n", id);
        return FALSE;
    }

    strcpy_s(idxlf.id, MAX_ID_LEN, id);
    idxl = ptrArray_findSorted(&g_localIdxSortedById, &idxlf);
    if (idxl)
        wcscpy_s(destDir, MAX_PATH, idxl->dir);
    else
    {
        if (g_options.dirMode == 0 && idxn->category[0]) // 游戏类别+游戏名称
            swprintf_s(destDir, MAX_PATH, L"%s\\%s", g_options.dir, idxn->category);
        else
            swprintf_s(destDir, MAX_PATH, L"%s", g_options.dir);
    }

    if (destDir[0] && destDir[wcslen(destDir)-1] == L'\\')
        destDir[wcslen(destDir)-1] = L'\0';

    if (name && name[0])
    {
        WCHAR *p, tmpPath[MAX_PATH];
        int i;

        for (p=NULL, i=(int)wcslen(destDir); i>=0; i--)
        {
            if (destDir[i] == L'\\')
            { p = destDir + i + 1; break; }
        }
        if (!p || _wcsicmp(p, name))
        {
            wcscat_s(destDir, MAX_PATH, L"\\");
            wcscat_s(destDir, MAX_PATH, name);
        }

        wcscpy_s(tmpPath, MAX_PATH, destDir);
        wcscat_s(tmpPath, MAX_PATH, L"\\");
        SureCreateDir(tmpPath);
    }

    return TRUE;
}

struct task *task_new()
{
    UINT32 g_lastTaskPriority = 0;
    struct task *task;

    task = malloc(sizeof(struct task));
    if (!task) return NULL;

    memset(task, 0, sizeof(struct task));

    rateCtrl_init(&task->rcRequest, 4000, 100);
    rateCtrl_init(&task->rcDown, 4000, 100);
    rateCtrl_init(&task->rcUp, 4000, 100);
    task->tmp.hFile = INVALID_HANDLE_VALUE;

    task->upload.stoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    task->cont.stoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    task->prepare.stoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    task->check.stoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    task->transfer.stoppedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    return task;
}

static void piece_free(struct piece *pc)
{
    if (pc->data) free(pc->data);
    free(pc);
}

void task_delete(struct task *task)
{
    struct peer *pr;
    struct ptrList *list;

    for (list=task->peersIncoming; list; list=list->next)
    {
        pr = (struct peer *)list->data;
        peer_delete(pr);
    }
    ptrList_free(&task->peersIncoming, NULL);

    for (list=task->peersCandidate; list; list=list->next)
    {
        pr = (struct peer *)list->data;
        peer_delete(pr);
    }
    ptrList_free(&task->peersCandidate, NULL);
    ptrList_free(&task->peersOutgoing, NULL);

    ptrList_free(&task->readCache, piece_free);

    bitset_free(&task->bitset);
    bitset_free(&task->bitsetRequested);

    if (task->tmp.hFile != INVALID_HANDLE_VALUE) CloseHandle(task->tmp.hFile);
    if (task->tmp.pieceTable) free(task->tmp.pieceTable);
    bitset_free(&task->tmp.bitset);
    if (task->tmp.files) free(task->tmp.files);

    idx_free(&task->idx);

    rateCtrl_uninit(&task->rcRequest);
    rateCtrl_uninit(&task->rcDown);
    rateCtrl_uninit(&task->rcUp);

    CloseHandle(task->upload.stoppedEvent);
    CloseHandle(task->cont.stoppedEvent);
    CloseHandle(task->prepare.stoppedEvent);
    CloseHandle(task->check.stoppedEvent);
    CloseHandle(task->transfer.stoppedEvent);

    free(task);
}

// -----------------------------------------------------------------------------------------
//
void task_arrangePriorities()
{
    int i, total, minSpeed;
    struct ptrList *li;
    struct task *task;

    if (!g_options.priorityMode || !g_options.downLimit)
    {
        for (li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            task->downLimit = 0;
        }
        return;
    }

    total = ptrList_size(g_tasksDownloading);
    if (!total) return;
    if (total == 1)
    {
        task = g_tasksDownloading->data;
        task->downLimit = 0;
        return;
    }

    switch (g_options.priorityMode)
    {
    case 1:
        minSpeed = (total > 2) ? g_options.downLimit / (10*(total-2)) : 100;
        for (i=0, li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            if (task->action!=TS_UPDATING && task->action!=TS_DOWNLOADING) continue;
            if (i==0) task->downLimit = total==2?g_options.downLimit*4/5:g_options.downLimit*7/10;
            else if (i==1) task->downLimit = g_options.downLimit/5;
            else task->downLimit = minSpeed;
            i ++;
        }
        break;
    case 2:
        minSpeed = (total > 2) ? g_options.downLimit*3/(10*(total-2)) : 100;
        for (i=0, li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            if (task->action!=TS_UPDATING && task->action!=TS_DOWNLOADING) continue;
            if (i==0) task->downLimit = total==2?g_options.downLimit*4/5:g_options.downLimit/2;
            else if (i==1) task->downLimit = g_options.downLimit/5;
            else task->downLimit = minSpeed;
            i ++;
        }
        break;
    case 3:
        minSpeed = (total > 2) ? g_options.downLimit/(5*(total-2)) : 100;
        for (i=0, li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            if (task->action!=TS_UPDATING && task->action!=TS_DOWNLOADING) continue;
            if (i==0) task->downLimit = total==2?g_options.downLimit*4/5:g_options.downLimit*7/20;
            else if (i==1) task->downLimit = g_options.downLimit/5;
            else task->downLimit = minSpeed;
            i ++;
        }
        break;
    default:
        for (li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            task->downLimit = g_options.downLimit/total;
        }
        break;
    }
}

static void task_onCheckSuccess(struct task *task)
{
    CHAR buf[1600], szDir[384];
    struct idx_local *idxl;

    if (task->action & TS_SEEDING)
    {
        task->action = TS_SEEDING;

        task_delStatus(task);
        task_delBitset(task);
        task_delFileTime(task);

        idxl = AddLocalIdx(&task->idx, task->dir);
        IdxLocalToUtf8String(idxl, buf, 1600);
        admin_sendMsg(STR_LOCAL_IDX_ADDED, buf);

        if (ptrList_remove_data(&g_tasksDownloading, task))
        {
            SaveDownloadingTaskList();
            task_arrangePriorities();
            admin_sendMsg(STR_DOWNLOADING_DELETED, task->idx.id);
        }
        if (!ptrArray_findSorted(&g_tasksSeedingSI, task))
        {
            time(&task->startSeedingTime);
            ptrArray_insertSorted(&g_tasksSeedingSI, task);
            ptrArray_insertSorted(&g_tasksSeedingSH, task);
            SaveSeedingTaskList();
            admin_sendMsg(STR_SEEDING_ADDED, task->idx.id);
        }
    }
    else
    {
        if (task->action & TS_UPDATING) task->action = TS_UPDATING;
        if (task->action & TS_DOWNLOADING) task->action = TS_DOWNLOADING;

        task_saveStatus(task);
        task_saveBitset(task);

        if (ptrArray_removeSorted(&g_tasksSeedingSI, task))
        {
            ptrArray_removeSorted(&g_tasksSeedingSH, task);
            SaveSeedingTaskList();
            admin_sendMsg(STR_SEEDING_DELETED, task->idx.id);
        }
        if (!task_isDownloading(task->idx.id))
        {
            ptrList_append(&g_tasksDownloading, task);
            task_arrangePriorities();
        }
        SaveDownloadingTaskList();

        sprintf_s(buf, 1600,
            "%s\t%s\t%s\t%d\t%d\r\n",
            task->idx.id, task->idx.hash,
            UnicodeToUtf8(task->dir, szDir, 384),
            task->action, task->errorCode);
        admin_sendMsg(STR_DOWNLOADING_ADDED, buf);
    }

    task_checkServer(task, TRUE, 0, 0);
}

static void task_onContinueResult(struct tcp_custom_task_continue *result)
{
    struct task *task = result->task;
    CHAR szTmp[512];

    g_taskContinuing = NULL;

    if (!(task->action & TS_ERROR) && !(task->action & TS_PREPARING))
    {
        task_onCheckSuccess(task);
        sprintf_s(szTmp, 512, "%s\tcontinue\t%d", task->idx.id, task->action);
        admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
    }

    task->action &= ~TS_CONTINUING;

    free(result);
}

static void task_onPrepareResult(struct tcp_custom_task_prepare *result)
{
    struct task *task = result->task;
    CHAR szTmp[512];

    g_taskPreparing = NULL;

    if (!(task->action & TS_ERROR))
    {
        task_onCheckSuccess(task);
        sprintf_s(szTmp, 512, "%s\tprepare\t%d", task->idx.id, task->action);
        admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
    }
    else if (task->errorCode != ERR_NET_IDX)
        task->action &= ~TS_PREPARING;

    free(result);
}

static void task_onCheckResult(struct tcp_custom_task_check *result)
{
    struct task *task = result->task;
    CHAR szTmp[512];

    g_taskChecking = NULL;

    if (!(task->action & TS_ERROR))
    {
        task_onCheckSuccess(task);
        sprintf_s(szTmp, 512, "%s\tcheck\t%d", task->idx.id, task->action);
        admin_sendMsg(STR_DOWNLOADING_CHANGED, szTmp);
    }
    else if (task->errorCode != ERR_NET_IDX)
        task->action &= ~TS_CHECKING;

    free(result);
}

static void task_onDeleteResult(struct tcp_custom_task_delete *result)
{
    struct task *task = result->task;

    if (result->deleteFiles)
        deleteDir_begin(task->dir);

    task_delete(task);
    task_arrangePriorities();

    free(result);
}

static void task_onTransferResult(struct tcp_custom_task_transfer *result)
{
    struct task *task = result->task;
    struct idx_local *idxl;
    CHAR buf[1600];
    UINT32 i;

    task->action &= ~TS_TRANSFERING;

    if (!(task->action & TS_ERROR))
    {
        task->bytesDownloaded += task->tmp.bytesDownloaded;
        task->piecesDownloaded += task->tmp.piecesDownloaded;
        for (i=0; i<task->idx.fileCount; i++)
        {
            task->idx.files[i]->bytesDownloaded += task->tmp.files[i].bytesDownloaded;
            task->idx.files[i]->piecesDownloaded += task->tmp.files[i].piecesDownloaded;
        }
        task->bytesToDownload = 0;
        task->piecesToDownload = 0;
        task->action = TS_SEEDING;

        debugf("[transfer] OK, %s %I64d-%I64d %u-%u\r\n", task->idx.id,
            task->bytesDownloaded, task->idx.bytes,
            task->piecesDownloaded, task->idx.pieceCount);

        task_delStatus(task);
        task_delBitset(task);
        task_delFileTime(task);

        task_discardTmpData(task);

        task_copySeedFile(task);

        idxl = AddLocalIdx(&task->idx, task->dir);
        IdxLocalToUtf8String(idxl, buf, 1600);
        admin_sendMsg(STR_LOCAL_IDX_ADDED, buf);

        ptrList_remove_data(&g_tasksDownloading, task);
        SaveDownloadingTaskList();
        admin_sendMsg(STR_DOWNLOADING_DELETED, task->idx.id);

        time(&task->startSeedingTime);
        ptrArray_insertSorted(&g_tasksSeedingSI, task);
        ptrArray_insertSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        admin_sendMsg(STR_SEEDING_ADDED, task->idx.id);
    }

    free(result);
}

static struct idx_net *AddNetIdx(struct idx *idx)
{
    struct idx_net *idxn, *idxnOld;

    idxn = (struct idx_net *)malloc(sizeof(struct idx_net)); if (!idxn) return NULL;
    memset(idxn, 0, sizeof(struct idx_net));
    strcpy_s(idxn->id, MAX_ID_LEN, idx->id);
    strcpy_s(idxn->hash, MAX_HASH_LEN, idx->hash);
    wcscpy_s(idxn->name, MAX_NAME_LEN, idx->name);
    wcscpy_s(idxn->category, MAX_CATEGORY_LEN, idx->category);
    strcpy_s(idxn->extraInfo, MAX_EXTRA_LEN, idx->extraInfo);
    idxn->size = idx->bytes;
    idxn->lastUpdateTime = idx->creationDate;
    idxn->pieceCnt = idx->pieceCount;
    idxn->pieceLen = idx->pieceLength;

    idxnOld = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedById, idxn);
    if (idxnOld)
    {
        if (strcmp(idxnOld->hash, idxn->hash))
        {
            ptrArray_removeSorted(&g_netIdxSortedByHash, idxnOld);
            *idxnOld = *idxn;
            ptrArray_insertSorted(&g_netIdxSortedByHash, idxnOld);
        }
        free(idxn);
        return idxnOld;
    }

    ptrArray_insertSorted(&g_netIdxSortedById, idxn);
    ptrArray_insertSorted(&g_netIdxSortedByHash, idxn);

    return idxn;
}

static void task_onUploadResult(struct tcp_custom_task_upload *result)
{
    struct task *task = result->task;
    struct idx_net *idxn;
    struct idx_local *idxl;
    CHAR buf[1600];
    time_t currTime;

    g_taskUploading = NULL;

    if (result->success && !(task->action & TS_ERROR))
    {
        task->action = TS_SEEDING;

        ptrList_remove_data(&g_tasksUploading, task);
        admin_sendMsg(STR_UPLOADING_DELETED, task->idx.id);

        idxn = AddNetIdx(&task->idx);
        idxl = AddLocalIdx(&task->idx, task->dir);
        if (!idxn || !idxl) return;

        IdxNetToUtf8String(idxn, buf, 1600);
        admin_sendMsg(STR_NET_IDX_ADDED, buf);
        IdxLocalToUtf8String(idxl, buf, 1600);
        admin_sendMsg(STR_LOCAL_IDX_ADDED, buf);

        time(&task->startSeedingTime);
        ptrArray_insertSorted(&g_tasksSeedingSI, task);
        ptrArray_insertSorted(&g_tasksSeedingSH, task);
        admin_sendMsg(STR_SEEDING_ADDED, task->idx.id);
        SaveSeedingTaskList();

        time(&currTime);
        task_checkServer(task, TRUE, FALSE, currTime);
    }

    free(result);
}


// ----------------------------------------------------------------------------------
//
static void task_deletePeersInList(struct ptrList **peers)
{
    struct ptrList *list, *listD;
    struct peer *peer;

    list = *peers;
    while (list)
    {
        peer = (struct peer *)list->data;

        listD = list; list = list->next;
        ptrList_remove_node(peers, listD);

        peer_onCloseCancelRequests(peer);
        peer_delete(peer);
    }
}
void task_closeAllPeers(struct task *task)
{
    task_deletePeersInList(&task->peersIncoming);
    task_deletePeersInList(&task->peersCandidate);
    task_deletePeersInList(&task->peersOutgoing);
}

#define MAX_FILE_IDLE   30

static void task_closeUnusedFiles(struct task *task, time_t currTime)
{
    UINT32 i;

    if (currTime - task->lastCheckIdleFileTime < 5) return;
    task->lastCheckIdleFileTime = currTime;

    for (i=0; i<task->idx.fileCount; i++)
    {
        if (task->idx.files[i]->hFile != INVALID_HANDLE_VALUE &&
            currTime - task->idx.files[i]->lastAccessTime > MAX_FILE_IDLE)
        {
            CloseHandle(task->idx.files[i]->hFile);
            task->idx.files[i]->hFile = INVALID_HANDLE_VALUE;
        }
    }
}

static void task_freeUnusedCaches(struct task *task, time_t currTime)
{
    struct ptrList *list, *listD;
    struct piece *pc;
    int deleted;

    if (task->action & TS_STOP_UPLOAD) return;

    if (currTime - task->lastCheckIdleCacheTime < 2) return;
    task->lastCheckIdleCacheTime = currTime;

    list = task->readCache;
    deleted = 0;
    while (list)
    {
        pc = (struct piece *)list->data;
        if (currTime - pc->lastRefTime >= MAX_CACHE_IDLE)
        {
            listD = list; list = list->next;
            ptrList_remove_node(&task->readCache, listD);
            piece_free(pc);
            deleted ++;
        }
        else list = list->next;
    }
}

static void task_trySendData(struct task *task)
{
    struct peer *peer;
    struct ptrList *list;

    for (list=task->peersOutgoing; list; list=list->next)
    {
        peer = (struct peer *)list->data;
        if (peer && peer->status & PEER_BITFIELDED)
        {
            if (peer->amInterestingToPeer && !peer->isPeerChokingToMe) peer_doPieceRequest(peer);
            if (!peer->amChokingToPeer) peer_doSendPiece(peer);
        }
    }
    for (list=task->peersIncoming; list; list=list->next)
    {
        peer = (struct peer *)list->data;
        if (peer && peer->status & PEER_BITFIELDED)
        {
            if (peer->amInterestingToPeer && !peer->isPeerChokingToMe) peer_doPieceRequest(peer);
            if (!peer->amChokingToPeer) peer_doSendPiece(peer);
        }
    }
}

#define MAX_PEER_IDLE   50

static BOOL task_isSpeedEnough(struct task *task)
{
    if (task->downLimit && (task->action==TS_DOWNLOADING || task->action==TS_UPDATING))
    {
        struct speed spd;

        task_getSpeed(task, &spd);
        return (spd.down > task->downLimit);
    }
    return FALSE;
}

void task_checkCandidatePeers(struct task *task, time_t currTime)
{
    struct ptrList *li, *liD;
    struct peer *peer;

    if (task_isSpeedEnough(task) ||
        ptrList_size(task->peersOutgoing) >= (int)g_options.maxDownPeersPerTask)
        return;

    li = task->peersCandidate;
    while (li)
    {
        peer = (struct peer *)li->data;

        if (!peer_connect(peer)) break;

        liD = li; li = li->next;
        ptrList_remove_node(&task->peersCandidate, liD);
        ptrList_append(&task->peersOutgoing, peer);

        peer->connectTime = currTime;
        peer->lastReceiveTime = currTime;
    }
}

static void task_checkOutgoingPeers(struct task *task, time_t currTime)
{
    struct peer *peer;
    struct ptrList *li, *liD;
    UINT32 shouldClose, downSpeed, upSpeed;
    BOOL needCheckSpeed;

    if (currTime - task->lastCheckPeerSpeedTime >= 15)
    {
        needCheckSpeed = TRUE;
        task->lastCheckPeerSpeedTime = currTime;
    }
    else needCheckSpeed = FALSE;

    li = task->peersOutgoing;
    while (li)
    {
        peer = (struct peer *)li->data;
        shouldClose = 0;

        if (currTime - peer->lastReceiveTime >= MAX_PEER_IDLE)
            shouldClose = 1;

        if (!shouldClose && needCheckSpeed && currTime - peer->connectTime >= 60)
        {
            downSpeed = speed_getSpeed(&peer->speedDown, currTime);
            upSpeed = speed_getSpeed(&peer->speedUp, currTime);
            if (downSpeed < g_options.minDownloadSpeed &&
                upSpeed < g_options.minDownloadSpeed)
                shouldClose = 1;
        }

        if (shouldClose)
        {
            liD = li; li = li->next;
            peer_onCloseCancelRequests(peer);
            if (!peer->amChokingToPeer) task->unchokedPeerCount --;
            ptrList_remove_node(&task->peersOutgoing, liD);
            peer_delete(peer);
        }
        else
        {
            if (peer->isPeerInterestingToMe &&
                peer->amChokingToPeer &&
                task->unchokedPeerCount < (int)g_options.maxUpPeersPerTask)
            {
                peer_sendUnchoke(peer);
                task->unchokedPeerCount ++;
            }

            li = li->next;
        }
    }
}

static void task_checkIncomingPeers(struct task *task, time_t currTime)
{
    struct peer *peer;
    struct ptrList *list, *listD;
    UINT32 shouldClose;

    list = task->peersIncoming;
    while (list)
    {
        peer = (struct peer *)list->data;
        shouldClose = 0;

        if (currTime - peer->lastReceiveTime > MAX_PEER_IDLE)
            shouldClose = TRUE;

        if (shouldClose)
        {
            listD = list; list = list->next;
            peer_onCloseCancelRequests(peer);
            if (!peer->amChokingToPeer) task->unchokedPeerCount --;
            ptrList_remove_node(&task->peersIncoming, listD);
            peer_delete(peer);
        }
        else
        {
            if (peer->isPeerInterestingToMe &&
                peer->amChokingToPeer &&
                task->unchokedPeerCount < (int)g_options.maxUpPeersPerTask)
            {
                peer_sendUnchoke(peer);
                task->unchokedPeerCount ++;
            }

            list = list->next;
        }
    }
}

static void task_checkPeers(struct task *task, time_t currTime)
{
    if (currTime - task->lastCheckPeersTime < 2) return;
    task->lastCheckPeersTime = currTime;

    task_checkCandidatePeers(task, currTime);
    task_checkOutgoingPeers(task, currTime);
    task_checkIncomingPeers(task, currTime);
}

void task_checkTransfer(struct task *task, time_t currTime)
{
    if (!(task->action == TS_UPDATING &&
        task->tmp.bytesDownloaded >= task->bytesToDownload))
        return;

    if (currTime - task->lastCheckTransferTime < 30) return;
    task->lastCheckTransferTime = currTime;

    if (currTime - task->transfer.lastActionTime < 60) return;

    task->action |= TS_TRANSFERING;
    task_saveStatus(task);

    task_closeAllPeers(task);
    task_closeAllFiles(task);

    taskTransfer_begin(task);
}


// 防止过多的空闲admin连接
static void task_checkAdminPeers(time_t currTime)
{
    static time_t g_lastCheckTime = 0;
    int i, sockIdx;
    struct tcp_info ti;

    if (currTime - g_lastCheckTime < 5) return;
    g_lastCheckTime = currTime;

    for (i=ptrArray_size(&g_adminSockets)-1; i>=0; i--)
    {
        sockIdx = (int)ptrArray_nth(&g_adminSockets, i);
        if (sockIdx == g_adminSocket) continue;

        tcp_getInfo(sockIdx, &ti);

        if (currTime - ti.lastReceiveTime >= 60)
        {
            ptrArray_removeSorted(&g_adminSockets, (void *)sockIdx);
            tcp_close(sockIdx);
        }
    }
}

static void task_searchLoaclPeers(struct task *task, time_t currTime)
{
    if (task->action & TS_STOP_DOWNLOAD ||
        task->bytesDownloaded+task->tmp.bytesDownloaded >= task->idx.bytes)
        return;

    if (currTime - task->lastLocalSearchTime < 30) return;
    task->lastLocalSearchTime = currTime;

    lsd_sendAnnounce(task, NULL);
}

static void task_checkServer(struct task *task, BOOL force, BOOL exiting, time_t currTime)
{
    int minInterval, peersWant;

    if (g_svr < 0) return;
    if (task->action & TS_STOP_UPLOAD) return;

    if (!force)
    {
        minInterval = max(60, g_svrInteval);
        if (!task->totalPeers || !task->totalSeeders) minInterval = 60;
        if ((int)(currTime - task->lastGetPeersTime) < minInterval) return;
    }

    if (currTime) task->lastGetPeersTime = currTime;
    peersWant = 50;
    svr_sendPeersRequest(g_svr, task, exiting, peersWant);
}

static void task_checkWaiting()
{
    struct task *task;
    CHAR *pId, buf[1600], szDir[384];
    int i, j;

    for (i=ptrList_size(g_tasksDownloading),j=0; i<(int)g_options.maxConcurrentTasks; i++)
    {
        if (!g_tasksWaiting) break;

        pId = ptrList_pop_front(&g_tasksWaiting);
        admin_sendMsg(STR_WAITING_DELETED, pId);

        if (task_isDownloading(pId) || task_isUploading(pId) || task_isSeeding(pId))
        { free(pId); continue; }

        task = task_new();
        strcpy_s(task->idx.id, MAX_ID_LEN, pId);
        task->action = TS_PREPARING;
        ptrList_append(&g_tasksDownloading, task);

        sprintf_s(buf, 1600,
            "%s\t%s\t%s\t%d\t%d\r\n",
            task->idx.id, task->idx.hash,
            UnicodeToUtf8(task->dir, szDir, 384),
            task->action, task->errorCode);
        admin_sendMsg(STR_DOWNLOADING_ADDED, buf);

        free(pId);
        j ++;
    }
    if (j)
    {
        SaveWaitingTaskList();
        SaveDownloadingTaskList();
    }
}

static void task_checkSeedingTimeOut(time_t currTime)
{
    static time_t lastCheckTime = 0;
    struct task *task;
    DWORD seedSeconds;
    int i;

    if (!g_options.seedMinutes) return;

    if (currTime - lastCheckTime < 10) return;
    lastCheckTime = currTime;

    if (g_options.seedMinutes >= 10000) seedSeconds = 30 * 60;
    else seedSeconds = g_options.seedMinutes * 60;

    for (i=ptrArray_size(&g_tasksSeedingSI)-1; i>=0; i--)
    {
        task = (struct task *)ptrArray_nth(&g_tasksSeedingSI, i);
        if (task->action & (TS_PAUSED|TS_ERROR)) continue;

        if (task->startSeedingTime && currTime - task->startSeedingTime > seedSeconds)
        {
            task_checkServer(task, TRUE, TRUE, currTime);

            ptrArray_erase(&g_tasksSeedingSI, i, i+1);
            ptrArray_removeSorted(&g_tasksSeedingSH, task);
            SaveSeedingTaskList();
            admin_sendMsg(STR_SEEDING_TIME_OUT, task->idx.id);
            admin_sendMsg(STR_SEEDING_DELETED, task->idx.id);
            task_remove(task);
        }
    }
}

void task_OnTcpTimer()
{
    struct ptrList *li;
    struct task *task;
    time_t currTime;
    int i;

    time(&currTime);

    svr_sendIdxListRequest(g_svr, currTime);
    if (!g_idxListInitialized) return;

    task_checkAdminPeers(currTime);

    task_checkSeedingTimeOut(currTime);

    for (i=ptrArray_size(&g_tasksSeedingSI)-1; i>=0; i--)
    {
        task = (struct task *)ptrArray_nth(&g_tasksSeedingSI, i);
        if (task->action & (TS_PAUSED|TS_ERROR)) continue;

        task_trySendData(task);
        task_checkPeers(task, currTime);
        task_closeUnusedFiles(task, currTime);
        task_freeUnusedCaches(task, currTime);
        task_checkServer(task, FALSE, FALSE, currTime);
    }

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (task->action==TS_DOWNLOADING || task->action==TS_UPDATING)
        {
            task_trySendData(task);
            task_checkPeers(task, currTime);
            task_closeUnusedFiles(task, currTime);
            task_freeUnusedCaches(task, currTime);
            task_checkServer(task, FALSE, FALSE, currTime);
            task_searchLoaclPeers(task, currTime);
            task_checkTransfer(task, currTime);
            if (currTime > task->lastSentProgressTime)
            {
                task->lastSentProgressTime = currTime;
                admin_sendDownloadingProgress(task);
            }
            continue;
        }

        if (task->action & TS_PAUSED) continue;

        if (task->action & TS_CHECKING && !g_taskChecking)
        {
            if (currTime-task->check.lastActionTime > 15)
            {
                g_taskChecking = task;
                taskCheck_begin(task);
            }
        }
        else if (task->action & TS_CONTINUING && !g_taskContinuing)
        {
            if (!(task->action & TS_ERROR))
            {
                g_taskContinuing = task;
                taskContinue_begin(task);
            }
        }
        else if (task->action & TS_PREPARING && !g_taskPreparing)
        {
            if (!(task->action & TS_ERROR) ||
                (task->errorCode == ERR_NET_IDX && currTime-task->prepare.lastActionTime > 15))
            {
                g_taskPreparing = task;
                taskPrepare_begin(task);
            }
        }
    }

    for (li=g_tasksUploading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (task->action & (TS_PAUSED|TS_ERROR)) continue;
        if (task->action & TS_UPLOADING && !g_taskUploading)
        {
            g_taskUploading = task;
            taskUpload_begin(task);
        }
    }

    task_checkWaiting();
}

void task_OnTcpCustom(struct tcp_custom *tc)
{
    struct tcp_custom_task_peers *tt;
    struct tcp_custom_idx_list *il;

    switch (tc->ioType)
    {
    case TCP_CUSTOM_TASK_IO_READ:
        task_onPieceRead((struct tcp_custom_task_io *)tc);
        break;
    case TCP_CUSTOM_TASK_IO_WRITE:
        task_onPieceWritten((struct tcp_custom_task_io *)tc);
        break;

    case TCP_CUSTOM_TASK_UPLOAD:
        task_onUploadResult((struct tcp_custom_task_upload *)tc);
        break;
    case TCP_CUSTOM_TASK_CONTINUE:
        task_onContinueResult((struct tcp_custom_task_continue *)tc);
        break;
    case TCP_CUSTOM_TASK_PREPARE:
        task_onPrepareResult((struct tcp_custom_task_prepare *)tc);
        break;
    case TCP_CUSTOM_TASK_CHECK:
        task_onCheckResult((struct tcp_custom_task_check *)tc);
        break;
    case TCP_CUSTOM_TASK_DELETE:
        task_onDeleteResult((struct tcp_custom_task_delete *)tc);
        break;
    case TCP_CUSTOM_TASK_TRANSFER:
        task_onTransferResult((struct tcp_custom_task_transfer *)tc);
        break;

    case TCP_CUSTOM_TASK_PEERS:
        tt = (struct tcp_custom_task_peers *)tc;
        svr_onPeers((UCHAR *)tt->data, tt->dataLen);
        free(tt);
        break;
    case TCP_CUSTOM_IDX_LIST:
        il = (struct tcp_custom_idx_list *)tc;
        svr_onIdxList(&il->netIdxList1, &il->netIdxList2);
        free(il);
        break;

    case TCP_CUSTOM_LOCAL_PEER:
        lsd_onAnnounce((struct tcp_custom_local_peer *)tc);
        break;
    }
}

static int task_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct task *)p1)->idx.id, ((struct task *)p2)->idx.id);
}
static int task_cmpHash(const void *p1, const void *p2)
{
    return strcmp(((struct task *)p1)->idx.hash, ((struct task *)p2)->idx.hash);
}
static int task_cmpStr(const void *p1, const void *p2)
{
    return strcmp(((CHAR *)p1), ((CHAR *)p2));
}
static int adminSocket_cmp(const void *p1, const void *p2)
{
    if (p1 > p2) return 1;
    if (p1 < p2) return -1;
    return 0;
}
static int idxLocal_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_local *)p1)->id, ((struct idx_local *)p2)->id);
}
static int idxLocal_cmpHash(const void *p1, const void *p2)
{
    return strcmp(((struct idx_local *)p1)->hash, ((struct idx_local *)p2)->hash);
}
static int idxNet_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_net *)p1)->id, ((struct idx_net *)p2)->id);
}
static int idxNet_cmpHash(const void *p1, const void *p2)
{
    return strcmp(((struct idx_net *)p1)->hash, ((struct idx_net *)p2)->hash);
}

static void task_getMyId()
{
    struct ptrList *macIps = SysGetMacIps();
    struct ptrList *list;
    struct MAC_IP *macIp;
    CHAR sz[1024], hash[20], szMac[20], szIp[MAX_IP_LEN];
    int len;
    struct in_addr addr;

    for (list=macIps,len=0; list; list=list->next)
    {
        macIp = (struct MAC_IP *)list->data;
        len += sprintf_s(&sz[len], 1024-len,
            "%02x%02x%02x%02x%02x%02x",
            macIp->mac[0], macIp->mac[1], macIp->mac[2],
            macIp->mac[3], macIp->mac[4], macIp->mac[5]);

        sprintf_s(szMac, 20, "%02x-%02x-%02x-%02x-%02x-%02x",
            macIp->mac[0], macIp->mac[1], macIp->mac[2],
            macIp->mac[3], macIp->mac[4], macIp->mac[5]);
        addr.s_addr = htonl(macIp->ip);
        strcpy_s(szIp, MAX_IP_LEN, inet_ntoa(addr));

        if (!g_options.myMac[0]) strcpy_s(g_options.myMac, 20, szMac);
        if (!g_options.myIp[0]) strcpy_s(g_options.myIp, MAX_IP_LEN, szIp);
    }

    if (!len)
    {
        *((UINT32 *)sz) = GetTickCount();
        *((UINT32 *)(sz+4)) = rand();
        len = 8;
    }

    sha1((UCHAR *)sz, len, (UCHAR *)hash);

    base64Encode(sz, 1024, (UCHAR *)hash, 20);
    strcpy_s(&g_options.myId[1], MAX_PID_LEN-1, sz);
    g_options.myId[0] = (len == 8 ? 'a' : 'A');
    debugf("my id: %s, mac: %s\r\n", g_options.myId, g_options.myMac);

    ptrList_free(&macIps, free);
}

BOOL task_startup()
{
    WCHAR dir[MAX_PATH];
    int i;

    debugf_startup(L"gmCore.log", 10*1024*1024, DEBUGF_DEBUG|DEBUGF_FILE|DEBUGF_STDIO);

    GetModuleFileNameW(NULL, g_workDir, MAX_PATH);
    for (i=(int)wcslen(g_workDir); i>0; i--)
    { if (g_workDir[i] == L'\\') { g_workDir[i] = 0; break; } }

    swprintf_s(dir, MAX_PATH, L"%s\\IdxFiles\\", g_workDir);
    SureCreateDir(dir);
    swprintf_s(dir, MAX_PATH, L"%s\\Tmp\\", g_workDir);
    SureCreateDir(dir);

    SetOptionsDefault();
    ReadOptions();

    task_getMyId();

    ptrArray_init(&g_tasksAutoUpdate, task_cmpStr);
    ptrArray_init(&g_tasksSeedingSI, task_cmpId);
    ptrArray_init(&g_tasksSeedingSH, task_cmpHash);

    ptrArray_init(&g_localIdxSortedById, idxLocal_cmpId);
    ptrArray_init(&g_localIdxSortedByHash, idxLocal_cmpHash);
    ptrArray_init(&g_netIdxSortedById, idxNet_cmpId);
    ptrArray_init(&g_netIdxSortedByHash, idxNet_cmpHash);

    ptrArray_init(&g_adminSockets, adminSocket_cmp);

    rateCtrl_init(&g_rcRequest, 4000, 100);
    rateCtrl_init(&g_rcDown, 4000, 100);
    rateCtrl_init(&g_rcUp, 4000, 100);

    InitializeCriticalSection(&g_csAdminMsg);

    deleteDir_startup();

    taskIo_startup();

    svr_restart();

    tcp_startup(20*1024, peer_OnTcpEvent);

    g_listenSock = tcp_listen("", g_options.portNum, NULL);

    return TRUE;
}

void task_cleanup()
{
    struct task *task;
    struct ptrList *li;
    int i;

    if (g_svr >= 0) svr_sendExitRequest(g_svr);

    lsd_cleanup();

    tcp_close(g_listenSock);
    tcp_cleanup();

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;

        taskContinue_end(task);
        taskPrepare_end(task);
        taskCheck_end(task);
        taskTransfer_end(task);
        taskUpload_end(task);

        task_closeAllPeers(task);
        task_closeAllFiles(task);

        if (task->action & (TS_DOWNLOADING|TS_UPDATING))
            task_saveFileTime(task);
    }
    for (i=0; i<ptrArray_size(&g_tasksSeedingSI); i++)
    {
        task = (struct task *)ptrArray_nth(&g_tasksSeedingSI, i);
        task_closeAllPeers(task);
        task_closeAllFiles(task);
    }

    svr_cleanup();

    ptrArray_free(&g_tasksAutoUpdate, free);
    ptrArray_free(&g_tasksSeedingSI, task_delete);
    ptrArray_free(&g_tasksSeedingSH, NULL);

    ptrList_free(&g_tasksDownloading, task_delete);
    ptrList_free(&g_tasksUploading, task_delete);
    ptrList_free(&g_tasksWaiting, free);

    ptrArray_free(&g_localIdxSortedById, free);
    ptrArray_free(&g_localIdxSortedByHash, NULL);
    ptrArray_free(&g_netIdxSortedById, free);
    ptrArray_free(&g_netIdxSortedByHash, NULL);

    ptrArray_free(&g_adminSockets, NULL);

    rateCtrl_uninit(&g_rcRequest);
    rateCtrl_uninit(&g_rcDown);
    rateCtrl_uninit(&g_rcUp);

    taskIo_cleanup();

    deleteDir_cleanup();

    DeleteCriticalSection(&g_csAdminMsg);

    debugf_cleanup();
}

