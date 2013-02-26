#include "gmCore.h"

// ------------------------------------------------------------------------------------
// 已完成的下载任务：id,hash,dir,time
BOOL ReadLocalIdxList()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize, fileDirty = 0;
    CHAR *pId, *pTmp;
    struct idx_local *idxl;
    struct idx_net idxnf = { 0 };

    swprintf_s(fileName, MAX_PATH, L"%s\\LocalIdx.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;
        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;

        idxl = (struct idx_local *)malloc(sizeof(struct idx_local)); if (!idxl) break;
        if (!IdxLocalFromMbcsString(idxl, pId)) { free(idxl); break; }

        strcpy_s(idxnf.id, MAX_ID_LEN, pId);
        if (!ptrArray_findSorted(&g_netIdxSortedById, &idxnf) ||
            ptrArray_findSorted(&g_localIdxSortedById, idxl))
        { fileDirty = 1; free(idxl); }
        else
        {
            ptrArray_insertSorted(&g_localIdxSortedById, idxl);
            ptrArray_insertSorted(&g_localIdxSortedByHash, idxl);
        }
    }
    free(fileData);

    if (fileDirty) SaveLocalIdxList();
    return TRUE;
}

BOOL SaveLocalIdxList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    struct idx_local *idxl;
    char szBuf[2048];
    int i, len;
    DWORD dwWritten;

    swprintf_s(fileName, MAX_PATH, L"%s\\LocalIdx.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (i=0; i<ptrArray_size(&g_localIdxSortedById); i++)
    {
        idxl = ptrArray_nth(&g_localIdxSortedById, i);
        len = IdxLocalToMbcsString(idxl, szBuf, 2048);
        if (!WriteFile(hFile, szBuf, len, &dwWritten, NULL) || dwWritten!=(DWORD)len)
        { CloseHandle(hFile); return FALSE; }
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);

    return TRUE;
}

struct idx_local *AddLocalIdx(struct idx *idx, const WCHAR *dir)
{
    struct idx_local *idxl, *idxlOld;

    idxl = malloc(sizeof(struct idx_local)); if (!idxl) return NULL;
    memset(idxl, 0, sizeof(struct idx_local));
    strcpy_s(idxl->id, MAX_ID_LEN, idx->id);
    strcpy_s(idxl->hash, MAX_HASH_LEN, idx->hash);
    wcscpy_s(idxl->name, MAX_NAME_LEN, idx->name);
    wcscpy_s(idxl->category, MAX_CATEGORY_LEN, idx->category);
    strcpy_s(idxl->extraInfo, MAX_EXTRA_LEN, idx->extraInfo);
    idxl->size = idx->bytes;
    time(&idxl->completeTime);
    wcscpy_s(idxl->dir, MAX_PATH, dir);

    idxlOld = ptrArray_findSorted(&g_localIdxSortedById, idxl);
    if (idxlOld)
    {
        if (strcmp(idxlOld->hash, idxl->hash))
        {
            ptrArray_removeSorted(&g_localIdxSortedByHash, idxlOld);
            *idxlOld = *idxl;
            ptrArray_insertSorted(&g_localIdxSortedByHash, idxlOld);
            SaveLocalIdxList();
        }
        free(idxl);
        return idxlOld;
    }

    ptrArray_insertSorted(&g_localIdxSortedById, idxl);
    ptrArray_insertSorted(&g_localIdxSortedByHash, idxl);
    SaveLocalIdxList();

    return idxl;
}

BOOL RemoveLocalIdx(const CHAR *id)
{
    struct idx_local *idx1, idx1f = { 0 };

    strcpy_s(idx1f.id, MAX_ID_LEN, id);
    idx1 = ptrArray_findSorted(&g_localIdxSortedById, &idx1f);
    if (!idx1) return FALSE;

    ptrArray_removeSorted(&g_localIdxSortedById, idx1);
    ptrArray_removeSorted(&g_localIdxSortedByHash, idx1);

    free(idx1);

    SaveLocalIdxList();

    return TRUE;
}


// ------------------------------------------------------------------------------------
// 要求自动更新的任务：id
BOOL ReadAutoUpdateTaskList()
{
    struct idx_net idxnf = { 0 };
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pId, *pTmp;
    int len, fileDirty = 0;

    swprintf_s(fileName, MAX_PATH, L"%s\\AutoUpdate.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;

        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        len = strlen(pId); if (len<MIN_ID_LEN || len>=MAX_ID_LEN) continue;

        strcpy_s(idxnf.id, MAX_ID_LEN, pId);
        if (!ptrArray_findSorted(&g_netIdxSortedById, &idxnf)) { fileDirty = 1; continue; }

        if (!ptrArray_findSorted(&g_tasksAutoUpdate, pId))
            ptrArray_insertSorted(&g_tasksAutoUpdate, _strdup(pId));
    }
    free(fileData);

    if (fileDirty) SaveAutoUpdateTaskList();
    return TRUE;
}

BOOL SaveAutoUpdateTaskList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    CHAR sz[256], *id;
    DWORD len, dwWritten;
    int i;

    swprintf_s(fileName, MAX_PATH, L"%s\\AutoUpdate.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (i=0; i<ptrArray_size(&g_tasksAutoUpdate); i++)
    {
        id = ptrArray_nth(&g_tasksAutoUpdate, i);
        len = sprintf_s(sz, 256, "%s\r\n", id);
        if (!WriteFile(hFile, sz, len, &dwWritten, NULL) || dwWritten!=len) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

// ------------------------------------------------------------------------------------
// 要求自动更新的任务：id
BOOL ReadWaitingTaskList()
{
    struct idx_net idxnf = { 0 };
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pId, *pTmp;
    int len, fileDirty = 0;

    swprintf_s(fileName, MAX_PATH, L"%s\\Waiting.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;

        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        len = strlen(pId); if (len<MIN_ID_LEN || len>=MAX_ID_LEN) { fileDirty = 1; continue; }

        strcpy_s(idxnf.id, MAX_ID_LEN, pId);
        if (!ptrArray_findSorted(&g_netIdxSortedById, &idxnf)) { fileDirty = 1; continue; }

        if (task_isWaiting(pId) || task_isDownloading(pId) || task_isSeeding(pId)) { fileDirty = 1; continue; }

        ptrList_append(&g_tasksWaiting, _strdup(pId));
    }
    free(fileData);

    if (fileDirty) SaveWaitingTaskList();
    return TRUE;
}

BOOL SaveWaitingTaskList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    CHAR sz[256], *id;
    struct ptrList *li;
    DWORD len, dwWritten;

    swprintf_s(fileName, MAX_PATH, L"%s\\Waiting.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (li=g_tasksWaiting; li; li=li->next)
    {
        id = li->data;
        len = sprintf_s(sz, 256, "%s\r\n", id);
        if (!WriteFile(hFile, sz, len, &dwWritten, NULL) || dwWritten!=len) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

// ------------------------------------------------------------------------------------
// 正在做种的任务：id
BOOL ReadSeedingTaskList()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pId, *pTmp;
    int len, fileDirty = 0;
    struct task *task, taskf = { 0 };
    struct idx_local *idxl, idxlf = { 0 };

    swprintf_s(fileName, MAX_PATH, L"%s\\Seeding.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;

        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        len = strlen(pId); if (len>=MAX_ID_LEN) { fileDirty = 1; continue; }

        if (task_isDownloading(pId)) { fileDirty = 1; continue; }

        strcpy_s(idxlf.id, MAX_ID_LEN, pId);
        idxl = ptrArray_findSorted(&g_localIdxSortedById, &idxlf);
        if (!idxl) { fileDirty = 1; continue; }

        task = task_new();
        strcpy_s(task->idx.id, MAX_ID_LEN, idxl->id);
        strcpy_s(task->idx.hash, MAX_HASH_LEN, idxl->hash);
        wcscpy_s(task->dir, MAX_PATH, idxl->dir);
        task->action = TS_CONTINUING|TS_SEEDING;
        ptrList_append(&g_tasksDownloading, task);
    }
    free(fileData);

    if (fileDirty) SaveSeedingTaskList();
    return TRUE;
}

BOOL SaveSeedingTaskList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    CHAR sz[256];
    DWORD len, dwWritten;
    struct task *task;
    int i;

    swprintf_s(fileName, MAX_PATH, L"%s\\Seeding.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (i=0; i<ptrArray_size(&g_tasksSeedingSI); i++)
    {
        task = ptrArray_nth(&g_tasksSeedingSI, i);
        len = sprintf_s(sz, 256, "%s\r\n", task->idx.id);
        if (!WriteFile(hFile, sz, len, &dwWritten, NULL) || dwWritten!=len) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

// ------------------------------------------------------------------------------------
// 正在工作的下载任务：id,dir
BOOL ReadDownloadingTaskList()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    struct task *task;
    CHAR *pId, *pHash, *pDir, *pAction, *pTmp;
    int len, fileDirty = 0;
    struct idx_net idxnf = { 0 };

    swprintf_s(fileName, MAX_PATH, L"%s\\Downloading.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;

        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        pHash = strchr(pId, '\t'); if (!pHash) continue; *pHash = 0; pHash ++;
        pAction = strchr(pHash, '\t'); if (!pAction) continue; *pAction = 0; pAction ++;
        pDir = strchr(pAction, '\t'); if (!pDir) continue; *pDir = 0; pDir ++;
        len = strlen(pId); if (len<MIN_ID_LEN || len>=MAX_ID_LEN) continue;
        if (strlen(pHash)>=MAX_HASH_LEN) continue;
        if (strlen(pDir)>=MAX_PATH) continue;

        strcpy_s(idxnf.id, MAX_ID_LEN, pId);
        if (!ptrArray_findSorted(&g_netIdxSortedById, &idxnf)) { fileDirty = 1; continue; }

        task = task_new();
        strcpy_s(task->idx.id, MAX_ID_LEN, pId);
        strcpy_s(task->idx.hash, MAX_HASH_LEN, pHash);
        task->action = TS_CONTINUING|(UINT32)atoi(pAction);
        wcscpy_s(task->dir, MAX_PATH, MbcsToUnicode(pDir, fileName, MAX_PATH));
        ptrList_append(&g_tasksDownloading, task);
    }
    free(fileData);

    if (fileDirty) SaveDownloadingTaskList();
    return TRUE;
}

BOOL SaveDownloadingTaskList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    struct task *task;
    struct ptrList *li;
    CHAR sz[1024], szDir[384];
    DWORD len, dwWritten;

    swprintf_s(fileName, MAX_PATH, L"%s\\Downloading.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = li->data;
        len = sprintf_s(sz, 1024, "%s\t%s\t%u\t%s\r\n",
            task->idx.id, task->idx.hash, task->action,
            UnicodeToMbcs(task->dir, szDir, 384));
        if (!WriteFile(hFile, sz, len, &dwWritten, NULL) || dwWritten!=len) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

// ------------------------------------------------------------------------------------
// 正在工作的下载任务：id,dir
BOOL ReadUploadingTaskList()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    struct task *task;
    CHAR *pId, *pDir, *pCate, *pErr, *pPwd, *pTmp;
    int len, fileDirty = 0;
    struct idx_local idxnl = { 0 };

    swprintf_s(fileName, MAX_PATH, L"%s\\Uploading.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pId = pTmp;

        pTmp = strstr(pId, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        pDir = strchr(pId, '\t'); if (!pDir) continue; *pDir = 0; pDir ++;
        pCate = strchr(pDir, '\t'); if (!pCate) continue; *pCate = 0; pCate ++;
        pErr = strchr(pCate, '\t'); if (!pErr) continue; *pErr = 0; pErr ++;
        pPwd = strchr(pErr, '\t'); if (!pPwd) continue; *pPwd = 0; pPwd ++;
        len = strlen(pId); if (len<MIN_ID_LEN || len>=MAX_ID_LEN) continue;
        if (strlen(pCate)>=MAX_CATEGORY_LEN) continue;
        if (strlen(pDir)>=MAX_PATH) continue;

        strcpy_s(idxnl.id, MAX_ID_LEN, pId);
        if (!ptrArray_findSorted(&g_localIdxSortedById, &idxnl)) { fileDirty = 1; continue; }

        task = task_new();
        strcpy_s(task->idx.id, MAX_ID_LEN, pId);
        wcscpy_s(task->dir, MAX_PATH, MbcsToUnicode(pDir, fileName, MAX_PATH));
        wcscpy_s(task->idx.category, MAX_CATEGORY_LEN, MbcsToUnicode(pCate, fileName, 32));
        strcpy_s(task->uploadPwd, MAX_PWD_LEN, pPwd);
        task->action = TS_UPLOADING|atoi(pErr);
        ptrList_append(&g_tasksUploading, task);
    }
    free(fileData);

    if (fileDirty) SaveUploadingTaskList();
    return TRUE;
}

BOOL SaveUploadingTaskList()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    struct task *task;
    struct ptrList *li;
    CHAR sz[1024], szDir[384], szCate[32];
    DWORD len, dwWritten;

    swprintf_s(fileName, MAX_PATH, L"%s\\Uploading.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = li->data;
        len = sprintf_s(sz, 1024, "%s\t%s\t%s\t%u\t%s\r\n",
            task->idx.id, UnicodeToMbcs(task->dir, szDir, 384),
            UnicodeToMbcs(task->idx.category, szCate, 32),
            task->action, task->uploadPwd);
        if (!WriteFile(hFile, sz, len, &dwWritten, NULL) || dwWritten!=len) break;
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

// ------------------------------------------------------------------------------------
// 正在工作的任务状态 id.status
BOOL task_readStatus(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;
    CHAR *id = NULL, *hash = NULL, *tmpFile = NULL,
        *action = NULL, *downLimit = NULL, *upLimit = NULL,
        *bytesToDownload = NULL, *piecesToDownload = NULL;

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.status",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;
        pTmp = strstr(pLine, "\r\n"); if (!pTmp) break; *pTmp = 0; pTmp += 2;
        pEq = strchr(pLine, '='); if (!pEq) continue; *pEq = 0; pEq ++;

        if (0==_stricmp(pLine, "id")) id = pEq;
        else if (0==_stricmp(pLine, "hash")) hash = pEq;
        else if (0==_stricmp(pLine, "tmpFile")) tmpFile = pEq;
        else if (0==_stricmp(pLine, "action")) action = pEq;
        else if (0==_stricmp(pLine, "downLimit")) downLimit = pEq;
        else if (0==_stricmp(pLine, "upLimit")) upLimit = pEq;
        else if (0==_stricmp(pLine, "bytesToDownload")) bytesToDownload = pEq;
        else if (0==_stricmp(pLine, "piecesToDownload")) piecesToDownload = pEq;
    }

    if (!id || !hash || !tmpFile || !action ||
        !downLimit || !upLimit || !bytesToDownload || !piecesToDownload)
    { free(fileData); return FALSE; }

    if (strcmp(task->idx.id, id) || strcmp(task->idx.hash, hash))
    { free(fileData); return FALSE; }

    wcscpy_s(task->tmp.fileName, MAX_PATH, MbcsToUnicode(tmpFile, fileName, MAX_PATH));
    task->action = (UINT32)atoi(action);
    task->downLimit = (UINT32)atoi(downLimit);
    task->upLimit = (UINT32)atoi(upLimit);
    task->bytesToDownload = (UINT64)_atoi64(bytesToDownload);
    task->piecesToDownload = (UINT32)atoi(piecesToDownload);

    free(fileData);
    return TRUE;
}

BOOL task_saveStatus(struct task *task)
{
    HANDLE hFile;
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    DWORD dwWritten, bufLen;
    CHAR buf[1024], szTmp[512];

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.status",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    bufLen = sprintf_s(buf, 1024, "id=%s\r\n", task->idx.id);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "hash=%s\r\n", task->idx.hash);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);

    bufLen = sprintf_s(buf, 1024, "tmpFile=%s\r\n", UnicodeToMbcs(task->tmp.fileName, szTmp, 512));
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "action=%u\r\n", task->action);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "downLimit=%u\r\n", task->downLimit);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "upLimit=%u\r\n", task->upLimit);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "bytesToDownload=%I64u\r\n", task->bytesToDownload);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);
    bufLen = sprintf_s(buf, 1024, "piecesToDownload=%u\r\n", task->piecesToDownload);
    WriteFile(hFile, buf, bufLen, &dwWritten, NULL);

    SetEndOfFile(hFile);
    CloseHandle(hFile);

    return TRUE;
}

void task_delStatus(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.status",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    DeleteFileW(fileName);
}


BOOL task_readBitset(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.bitset",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) return FALSE;

    if (fileSize != task->bitset.byteCount)
    {
        free(fileData);
        return FALSE;
    }

    memcpy(task->bitset.bits, fileData, fileSize);
    free(fileData);

    return TRUE;
}

BOOL task_saveBitset(struct task *task)
{
    HANDLE hFile;
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    DWORD dwWritten;

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.bitset",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (task->bitset.bitCount)
        WriteFile(hFile, task->bitset.bits, task->bitset.byteCount, &dwWritten, NULL);

    SetEndOfFile(hFile);
    CloseHandle(hFile);

    return TRUE;
}

void task_delBitset(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.bitset",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    DeleteFileW(fileName);
}


BOOL task_readFileTime(struct task *task, UINT32 *timeCount, UINT64 **times)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    HANDLE hFile;
    LARGE_INTEGER li;
    DWORD dwRead, i;
    UINT32 timeCount1;
    UINT64 *times1;

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.filetime",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (!GetFileSizeEx(hFile, &li) || 1 > li.QuadPart/sizeof(UINT64) || li.QuadPart%sizeof(UINT64))
    {
        CloseHandle(hFile);
        return FALSE;
    }

    timeCount1 = (UINT32)(li.QuadPart/sizeof(UINT64));
    times1 = calloc(timeCount1, sizeof(UINT64));

    for (i=0; i<timeCount1; i++)
        ReadFile(hFile, &times1[i], sizeof(UINT64), &dwRead, NULL);

    *timeCount = timeCount1;
    *times = times1;

    CloseHandle(hFile);
    DeleteFileW(fileName);
    return TRUE;
}

BOOL task_saveFileTime(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    HANDLE hFile;
    DWORD dwWritten, i;
    UINT64 ft64;

    if (!(task->action & (TS_DOWNLOADING|TS_UPDATING))) return TRUE;

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.filetime",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    hFile = CreateFileW(fileName, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    if (task->action & TS_UPDATING)
    {
        if (task_openTmpFile(task))
        {
            ft64 = FileGetLastWriteTime(task->tmp.hFile);
            WriteFile(hFile, &ft64, sizeof(ft64), &dwWritten, NULL);
        }
    }
    else
    {
        for (i=0; i<task->idx.fileCount; i++)
        {
            if (task_openFile(task, task->idx.files[i], FALSE, NULL))
            {
                ft64 = FileGetLastWriteTime(task->idx.files[i]->hFile);
                WriteFile(hFile, &ft64, sizeof(ft64), &dwWritten, NULL);
            }
        }
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

void task_delFileTime(struct task *task)
{
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];

    swprintf_s(fileName, MAX_PATH, L"%s\\Tmp\\%s.working.filetime",
        g_workDir, MbcsToUnicode(task->idx.id, wId, MAX_ID_LEN));
    DeleteFileW(fileName);
}
