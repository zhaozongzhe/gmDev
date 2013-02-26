#include "gmSvr.h"

TCHAR *g_serviceName = _T("gmServer");
TCHAR *g_serviceDesc = _T("gmServer");
TCHAR *g_serviceDep = _T("rpcss");
TCHAR *g_serviceParams = _T("");

UINT16 g_portNum = 8080;
int g_listenSock = -1;
WCHAR g_workDir[MAX_PATH] = { 0 };
time_t g_startTime = 0;

CHAR g_adminIps[512] = { 0 };
CHAR g_adminPwd[MAX_PWD_LEN] = { 0 };
INT g_peersInterval = 60;                // inteval between access
struct ptrArray g_dedicatedServers = { 0 };

// 增量传送游戏列表
static UCHAR *g_idxListData1 = NULL;
static UINT32 g_idxListSize1 = 0;
static CHAR g_idxListVer1[MAX_VERSION_LEN];    // 2012-05-27 00:00:00:000;2012-05-28 22:17:09:001
static UCHAR *g_idxListData2 = NULL;
static UINT32 g_idxListSize2 = 0;
static CHAR g_idxListVer2[MAX_VERSION_LEN];

#define PEER_CONNECTABLE    1
#define PEER_UNCONNECTABLE  2

void ReadOptions();
BOOL SaveOptions();

struct ptrArray g_peerConnectable = { 0 };

struct ptrArray g_cachesSortedById = { 0 };
struct ptrArray g_cachesSortedByHash = { 0 };
struct ptrArray g_cachesDirty = { 0 };

struct ptrArray g_crowds = { 0 };


#define ACTION_COMPLETED        1               // download completed
#define ACTION_EXITING          2               // exiting downloading

static int g_uiSock = -1;
void SendUiSocketNotify(struct cache *cache, BOOL del);

static struct cache *cache_new()
{
    struct cache *cache;
    cache = (struct cache *)malloc(sizeof(struct cache));
    memset(cache, 0, sizeof(struct cache));
    return cache;
}

static void cache_free(struct cache *cache)
{
    idx_free(&cache->idx);
    if (cache->fileData) free(cache->fileData);
    free(cache);
}

static struct group *group_new()
{
    struct group *grp;
    grp = (struct group *)malloc(sizeof(struct group));
    memset(grp, 0, sizeof(struct group));
    return grp;
}

static void group_free(struct group *grp)
{
    ptrList_free(&grp->peers, NULL);
    free(grp);
}

static int peerCmp(const void *p1, const void *p2)
{
    return strcmp(((struct peer *)p1)->pid, ((struct peer *)p2)->pid);
}
static int groupCmp(const void *p1, const void *p2)
{
    return ((struct group *)p1)->group - ((struct group *)p2)->group;
}
static struct crowd *crowd_new()
{
    struct crowd *crd;
    crd = malloc(sizeof(struct crowd));
    memset(crd, 0, sizeof(struct crowd));
    ptrArray_init(&crd->peers, peerCmp);
    ptrArray_init(&crd->groups, groupCmp);
    return crd;
}

static void crowd_free(struct crowd *crd)
{
    ptrArray_free(&crd->peers, free);
    ptrArray_free(&crd->groups, group_free);
    free(crd);
}

static void EncryptData(UCHAR *data, int dataLen)
{
    arc4_context ctx;
    unsigned char buf[20], hash[20];
    DWORD tick;

    tick = ntohl(*((DWORD *)data));
    if (!tick) return;

    *((DWORD *)buf) = tick;
    sha1(buf, 4, hash);

    arc4_setup(&ctx, hash, 20);
    arc4_crypt(&ctx, dataLen-4, data+4, data+4);
}

static BOOL BuildIdxListData1()
{
    struct cache *cache;
    UCHAR *idxListData, *idxListDataCompressed;
    UINT32 idxListSize, idxListSizeMax, tmpLen;
    uLong idxListSizeCompressed;
    SYSTEMTIME ct;
    CHAR buf[2048];
    INT i;

    if (g_idxListData1) free(g_idxListData1);
    g_idxListData1 = NULL;
    g_idxListSize1 = 0;

    GetLocalTime(&ct);
    sprintf_s(g_idxListVer1, MAX_VERSION_LEN,
        "%04d-%02d-%02d %02d:%02d:%02d:%03d",
        ct.wYear, ct.wMonth, ct.wDay, ct.wHour, ct.wMinute, ct.wSecond, ct.wMilliseconds);

    if (!ptrArray_size(&g_cachesSortedById)) return TRUE;

    idxListSizeMax = ptrArray_size(&g_cachesSortedById)*1024+4096;
    idxListData = (UCHAR *)malloc(idxListSizeMax);
    if (!idxListData) return FALSE;
    idxListSize = 0;
    for (i=0; i<ptrArray_size(&g_cachesSortedById); i++)
    {
        cache = (struct cache *)ptrArray_nth(&g_cachesSortedById, i);
        tmpLen = IdxToUtf8String(&cache->idx, buf, 2048);

        if (idxListSize+tmpLen >= idxListSizeMax) break;
        memcpy(idxListData+idxListSize, buf, tmpLen+1);
        idxListSize += tmpLen;
    }
    idxListSize ++; // tail 0

    debugf("IdxListSize: %u count: %d\r\n", idxListSize, ptrArray_size(&g_cachesSortedById));

    //SetFileContent(_T("D:\\Tmp\\1.txt"), idxListData, idxListSize);

    idxListSizeCompressed = compressBound(idxListSize);
    idxListDataCompressed = (UCHAR *)malloc(idxListSizeCompressed);
    if (!idxListDataCompressed)
    {
        free(idxListData);
        return FALSE;
    }
    if (Z_OK != compress(
        (Bytef *)idxListDataCompressed, &idxListSizeCompressed,
        (Bytef *)idxListData, idxListSize))
    {
        free(idxListDataCompressed);
        free(idxListData);
        return FALSE;
    }

    g_idxListSize1 = 4 + idxListSizeCompressed;
    g_idxListData1 = (UCHAR *)malloc(g_idxListSize1);
    if (!g_idxListData1)
    {
        free(idxListDataCompressed);
        free(idxListData);
        return FALSE;
    }
    *((UINT32 *)g_idxListData1) = htonl(idxListSize);
    memcpy(g_idxListData1+4, idxListDataCompressed, idxListSizeCompressed);

    free(idxListDataCompressed);
    free(idxListData);

    return TRUE;
}

static BOOL BuildIdxListData2(BOOL force1)
{
    struct cache *cache;
    UCHAR *idxListData, *idxListDataCompressed;
    UINT32 idxListSize, idxListSizeMax, tmpLen;
    uLong idxListSizeCompressed;
    SYSTEMTIME ct;
    CHAR buf[2048], ymd[32], ymdOld[32], *pTmp;
    INT i;

    if (g_idxListData2) free(g_idxListData2);
    g_idxListData2 = NULL;
    g_idxListSize2 = 0;

    strcpy_s(ymdOld, MAX_VERSION_LEN, g_idxListVer2);
    pTmp = strchr(ymdOld, ' '); if (pTmp) *pTmp = 0;

    GetLocalTime(&ct);
    sprintf_s(g_idxListVer2, MAX_VERSION_LEN,
        "%04d-%02d-%02d %02d:%02d:%02d:%03d",
        ct.wYear, ct.wMonth, ct.wDay, ct.wHour, ct.wMinute, ct.wSecond, ct.wMilliseconds);
    debugf("IdxList Ver2: %s\r\n", g_idxListVer2);

    if (force1)
    {
        ptrArray_free(&g_cachesDirty, NULL);
        return BuildIdxListData1();
    }

    strcpy_s(ymd, MAX_VERSION_LEN, g_idxListVer2);
    pTmp = strchr(ymd, ' '); if (pTmp) *pTmp = 0;
    if (strcmp(ymd, ymdOld))
    {
        ptrArray_free(&g_cachesDirty, NULL);
        return BuildIdxListData1();
    }

    if (!ptrArray_size(&g_cachesDirty)) return TRUE;

    idxListSizeMax = ptrArray_size(&g_cachesDirty)*1024+4096;
    idxListData = (UCHAR *)malloc(idxListSizeMax);
    if (!idxListData) return FALSE;
    idxListSize = 0;
    for (i=0; i<ptrArray_size(&g_cachesDirty); i++)
    {
        cache = (struct cache *)ptrArray_nth(&g_cachesDirty, i);
        tmpLen = IdxToUtf8String(&cache->idx, buf, 2048);

        if (idxListSize+tmpLen >= idxListSizeMax) break;
        memcpy(idxListData+idxListSize, buf, tmpLen+1);
        idxListSize += tmpLen;
    }
    idxListSize ++; // tail 0

    debugf("IdxListSize2: %u count: %d\r\n", idxListSize, ptrArray_size(&g_cachesDirty));

    idxListSizeCompressed = compressBound(idxListSize);
    idxListDataCompressed = (UCHAR *)malloc(idxListSizeCompressed);
    if (!idxListDataCompressed)
    {
        free(idxListData);
        return FALSE;
    }
    if (Z_OK != compress(
        (Bytef *)idxListDataCompressed, &idxListSizeCompressed,
        (Bytef *)idxListData, idxListSize))
    {
        free(idxListDataCompressed);
        free(idxListData);
        return FALSE;
    }

    g_idxListSize2 = 4 + idxListSizeCompressed;
    g_idxListData2 = (UCHAR *)malloc(g_idxListSize2);
    if (!g_idxListData2)
    {
        free(idxListDataCompressed);
        free(idxListData);
        return FALSE;
    }
    *((UINT32 *)g_idxListData2) = htonl(idxListSize);
    memcpy(g_idxListData2+4, idxListDataCompressed, idxListSizeCompressed);

    free(idxListDataCompressed);
    free(idxListData);

    return TRUE;
}

static void OnGetIdxList(int sockIdx, CHAR *clientVer1)
{
    CHAR *clientVer2;
    UCHAR buf[128];

    clientVer2 = clientVer1 + MAX_VERSION_LEN;

    *((UINT32 *)buf) = 0;
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_IDX_LIST_RESP;
    *((UINT32 *)(buf+6)) = 0;   // no crypt
    *((UINT32 *)(buf+10)) = 0;
    *((UINT32 *)(buf+14)) = 0;
    strcpy_s((CHAR *)(buf+18), MAX_VERSION_LEN, g_idxListVer1);
    strcpy_s((CHAR *)(buf+18+MAX_VERSION_LEN), MAX_VERSION_LEN, g_idxListVer2);

    if (0==strcmp(clientVer1, g_idxListVer1) &&
        0==strcmp(clientVer2, g_idxListVer2))
    {
        *((UINT32 *)buf) = htonl(14+2*MAX_VERSION_LEN);
        tcp_send(sockIdx, buf, 18+2*MAX_VERSION_LEN, NULL, 0);
    }
    else if (0==strcmp(clientVer1, g_idxListVer1))
    {
        *((UINT32 *)buf) = htonl(14+2*MAX_VERSION_LEN+g_idxListSize2);
        *((UINT32 *)(buf+14)) = htonl(g_idxListSize2);
        tcp_send(sockIdx, buf, 18+2*MAX_VERSION_LEN, g_idxListData2, g_idxListSize2);
    }
    else
    {
        UINT32 dataLen;
        CHAR *data = NULL;

        dataLen = g_idxListSize1 + g_idxListSize2;
        if (dataLen)
        {
            data = (CHAR *)malloc(dataLen);
            if (data)
            {
                if (g_idxListSize1) memcpy(data, g_idxListData1, g_idxListSize1);
                if (g_idxListSize2) memcpy(data+g_idxListSize1, g_idxListData2, g_idxListSize2);
            }
        }
        *((UINT32 *)buf) = htonl(14+2*MAX_VERSION_LEN+dataLen);
        *((UINT32 *)(buf+10)) = htonl(g_idxListSize1);
        *((UINT32 *)(buf+14)) = htonl(g_idxListSize2);
        tcp_send(sockIdx, buf, 18+2*MAX_VERSION_LEN, data, dataLen);

        if (data) free(data);
    }
}

static void OnGetIdx(int sockIdx, CHAR *id)
{
    UCHAR buf[128];
    struct cache *cache, cachef = { 0 };
    WCHAR szFileName[MAX_PATH], wszId[MAX_ID_LEN];
    UCHAR *fileData;
    DWORD fileSize;

    *((UINT32 *)buf) = htonl(7+MAX_ID_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_IDX_RESP;
    *((UINT32 *)(buf+6)) = 0;   // no crypt
    strcpy_s((CHAR *)(buf+10), MAX_ID_LEN, id);
    *(buf+10+MAX_ID_LEN) = 1;   // failed

    strcpy_s(cachef.idx.id, MAX_ID_LEN, id);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);
    if (!cache)
    {
        debugf("OnGetIdx error: %s not exist\r\n", id);
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }

    if (!cache->fileData)
    {
        swprintf_s(szFileName, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
            g_workDir, MbcsToUnicode(id, wszId, MAX_ID_LEN));
        fileData = GetFileContent(szFileName, &fileSize);
        if (!fileData)
        {
            debugf("OnGetIdx error: %s cannot read file content\r\n", id);
            *(buf+10+MAX_ID_LEN) = 2;   // failed
            tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
            return;
        }

        cache->fileData = fileData;
        cache->fileSize = (UINT32)fileSize;
    }
    time(&cache->lastAccessTime);

    *(buf+10+MAX_ID_LEN) = 0;   // success
    *((UINT32 *)buf) = htonl(7+MAX_ID_LEN+cache->fileSize);
    tcp_send(sockIdx, buf, 11+MAX_ID_LEN, cache->fileData, cache->fileSize);
}

struct updateNotify_data
{
    int sockIdx;
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
};
static struct ptrList *g_updateNotifyData = NULL;

static BOOL SendUpdateNotify(int sockIdx)
{
    struct updateNotify_data *ud;
    struct ptrList *list;
    CHAR buf[128];

    for (list=g_updateNotifyData, ud=NULL; list; list=list->next)
    {
        ud = list->data;
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

    ptrList_remove_node(&g_updateNotifyData, list);
    free(ud);

    return TRUE;
}

static BOOL RemoveUpdateNotify(int sockIdx)
{
    struct updateNotify_data *ud;
    struct ptrList *list;

    for (list=g_updateNotifyData, ud=NULL; list; list=list->next)
    {
        ud = list->data;
        if (ud->sockIdx == sockIdx) break;
    }
    if (!ud || ud->sockIdx != sockIdx) return FALSE;

    ptrList_remove_node(&g_updateNotifyData, list);
    free(ud);

    return TRUE;
}

int CALLBACK OnUpdateNotifyTcpEvent(int sockIdx, int msgCode, UCHAR *data, int dataLen)
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
        while (SendUpdateNotify(sockIdx));
        break;

    case TCP_EV_CLOSE:
        while (RemoveUpdateNotify(sockIdx));
        break;

    case TCP_EV_TIMER:
        break;
    }

    return 0;
}

static void UpdateNotifyDedicatedServers(const CHAR *id, const CHAR *hash)
{
    int i, total, sockIdx;
    struct ip_port *ipport;

    total = ptrArray_size(&g_dedicatedServers);

    for (i=0; i<total; i++)
    {
        ipport = ptrArray_nth(&g_dedicatedServers, i);
        sockIdx = tcp_connect(ipport->ip, ipport->port, OnUpdateNotifyTcpEvent);
        if (sockIdx >= 0)
        {
            struct updateNotify_data *ud;
            ud = (struct updateNotify_data *)malloc(sizeof(struct updateNotify_data));
            memset(ud, 0, sizeof(struct updateNotify_data));
            ud->sockIdx = sockIdx;
            strcpy_s(ud->id, MAX_ID_LEN, id);
            strcpy_s(ud->hash, MAX_HASH_LEN, hash);
            ptrList_append(&g_updateNotifyData, ud);
        }
    }
}

static BOOL VerifyUpload(int sockIdx, const CHAR *id, const CHAR *pwd)
{
    if (strlen(g_adminIps) > 5)
    {
        struct tcp_info ti;
        tcp_getInfo(sockIdx, &ti);
        if (!strstr(g_adminIps, ti.peerAddr.ip))
        {
            debugf("SetIdx %s ERROR, IP restrict\r\n", id);
            return FALSE;
        }
    }
    if (strcmp(pwd, g_adminPwd))
    {
        debugf("SetIdx %s ERROR, PWD error\r\n", id);
        return FALSE;
    }
    return TRUE;
}

static void OnSetIdx(int sockIdx, CHAR *msgData, int msgLen)
{
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN], *p, *fileData;
    DWORD len, fileDataLen;
    struct idx idx = { 0 };
    struct cache *cache, cachef = { 0 };
    struct crowd *crowd, crowdf = { 0 };
    UCHAR buf[64] = { 0 };
    WCHAR fileName[MAX_PATH], wszId[MAX_ID_LEN];

    // parse data, format: id,pwd,fileData
    p = msgData;
    len = strlen(p); if (len < MIN_ID_LEN || len >= MAX_ID_LEN) return;
    strcpy_s(id, MAX_ID_LEN, p); p += MAX_ID_LEN;
    len = strlen(p); if (len >= MAX_PWD_LEN) return;
    strcpy_s(pwd, MAX_PWD_LEN, p); p += MAX_PWD_LEN;
    fileData = p;
    fileDataLen = msgLen - (p-msgData);

    //debugf("SetIdx :%s %s %d\r\n", id, pwd, fileDataLen);

    // prepare response
    *((UINT32 *)buf) = htonl(7+MAX_ID_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_SET_IDX_RESP;
    *((UINT32 *)(buf+6)) = 0;   // no crypt
    strcpy_s((CHAR *)(buf+10), MAX_ID_LEN, id);
    *(buf+10+MAX_ID_LEN) = 1;   // failed

    if (!VerifyUpload(sockIdx, id, pwd))
    {
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }

    // load and parse data
    if (!idx_load((UCHAR *)fileData, fileDataLen, &idx))
    {
        debugf("SetIdx: invalid idx data format: %s\r\n", id);
        *(buf+10+MAX_ID_LEN) = 3;
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }
    if (strcmp(id, idx.id))
    {
        debugf("SetIdx: invalid id: %s %s %s\r\n", id, idx.id, idx.hash);
        *(buf+10+MAX_ID_LEN) = 4;
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }
    strcpy_s(cachef.idx.hash, MAX_HASH_LEN, idx.hash);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedByHash, &cachef);
    if (cache)
    {
        if (strcmp(id, cache->idx.id))
        {
            debugf("SetIdx: %s hash already exist in: %s %s\r\n", id, cache->idx.id, idx.hash);
            *(buf+10+MAX_ID_LEN) = 5;
            tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
            return;
        }
        debugf("SetIdx: hash not changed: %s %s\r\n", id, idx.hash);
    }

    // send response
    *(buf+10+MAX_ID_LEN) = 0;   // success
    tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);


    // new or update?
    strcpy_s(cachef.idx.id, MAX_ID_LEN, id);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);

    if (cache) // update
    {
        // hash先remove
        ptrArray_removeSorted(&g_cachesSortedByHash, cache); //

        strcpy_s(crowdf.hash, MAX_HASH_LEN, cache->idx.hash);
        crowd = (struct crowd *)ptrArray_removeSorted(&g_crowds, &crowdf);
        if (crowd) crowd_free(crowd);

        if (cache->fileData)
        {
            free(cache->fileData);
            cache->fileData = NULL;
            cache->fileSize = 0;
        }
        idx_free(&cache->idx);

        cache->idx = idx;
        idx_free(&cache->idx);
        ptrArray_insertSorted(&g_cachesSortedByHash, cache);
    }
    else
    {
        cache = cache_new();
        cache->idx = idx;
        idx_free(&cache->idx);
        ptrArray_insertSorted(&g_cachesSortedById, cache);
        ptrArray_insertSorted(&g_cachesSortedByHash, cache);
    }

    // 更新数据
    ptrArray_insertSorted(&g_cachesDirty, cache);
    BuildIdxListData2(FALSE);

    swprintf_s(fileName, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(cache->idx.id, wszId, MAX_ID_LEN));
    idx_save(fileName, (UCHAR *)fileData, fileDataLen);

    UpdateNotifyDedicatedServers(cache->idx.id, cache->idx.hash);

    SendUiSocketNotify(cache, FALSE);
}

static void OnDelIdx(int sockIdx, CHAR *msgData, int msgLen)
{
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN], *p;
    struct cache *cache, cachef = { 0 };
    struct crowd *crowd, crowdf = { 0 };
    UCHAR buf[128];
    DWORD len;
    WCHAR fileName[MAX_PATH], wszId[MAX_ID_LEN];

    p = msgData;
    len = strlen(p); if (len < MIN_ID_LEN || len >= MAX_ID_LEN) return;
    strcpy_s(id, MAX_ID_LEN, p); p += MAX_ID_LEN;
    len = strlen(p); if (len >= MAX_PWD_LEN) return;
    strcpy_s(pwd, MAX_PWD_LEN, p); p += MAX_PWD_LEN;

    *((UINT32 *)buf) = htonl(7+MAX_ID_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_DEL_IDX_RESP;
    *((UINT32 *)(buf+6)) = 0;   // no crypt
    strcpy_s((CHAR *)(buf+10), MAX_ID_LEN, id);
    *(buf+10+MAX_ID_LEN) = 1;   // failed

    if (!VerifyUpload(sockIdx, id, pwd))
    {
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }

    strcpy_s(cachef.idx.id, MAX_ID_LEN, id);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);
    if (!cache)
    {
        debugf("DelIdx: id NOT exist: %s\r\n", id);
        *(buf+10+MAX_ID_LEN) = 4;
        tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);
        return;
    }

    debugf("DelIdx: OK %s\r\n", id);
    *(buf+10+MAX_ID_LEN) = 0; // success
    tcp_send(sockIdx, buf, 11+MAX_ID_LEN, NULL, 0);

    strcpy_s(crowdf.hash, MAX_HASH_LEN, cache->idx.hash);
    crowd = (struct crowd *)ptrArray_removeSorted(&g_crowds, &crowdf);
    if (crowd) crowd_free(crowd);

    // notify UI
    SendUiSocketNotify(cache, TRUE);

    ptrArray_removeSorted(&g_cachesSortedById, cache);
    ptrArray_removeSorted(&g_cachesSortedByHash, cache);
    cache_free(cache);

    BuildIdxListData2(TRUE);

    swprintf_s(fileName, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(id, wszId, MAX_ID_LEN));
    DeleteFileW(fileName);
}

static void InitializeIdxCaches()
{
    WIN32_FIND_DATAW ffd;
    HANDLE hFind;
    WCHAR szFileName[MAX_PATH];
    struct ptrList *files = NULL, *li;
    struct idx idx;
    struct cache *cache, cachef = { 0 };

    swprintf_s(szFileName, MAX_PATH, L"%s\\IdxFiles\\*"IDX_EXTNAME, g_workDir);
    SureCreateDir(szFileName);
    hFind = FindFirstFileW(szFileName, &ffd);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        debugf("FindFirstFileW ERROR: %d %S\r\n", GetLastError(), szFileName);
        return;
    }
    while (1)
    {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            ptrList_append(&files, _wcsdup(ffd.cFileName));

        if (!FindNextFileW(hFind, &ffd)) break;
    }
    FindClose(hFind);

    for (li=files; li; li=li->next)
    {
        swprintf_s(szFileName, MAX_PATH, L"%s\\IdxFiles\\%s", g_workDir, li->data);
        if (!idx_open(szFileName, &idx))
        {
            debugf("Cannot open idx file: %S\r\n", szFileName);
            continue;
        }

        strcpy_s(cachef.idx.id, MAX_ID_LEN, idx.id);
        strcpy_s(cachef.idx.hash, MAX_HASH_LEN, idx.hash);
        if (ptrArray_findSorted(&g_cachesSortedById, &cachef) ||
            ptrArray_findSorted(&g_cachesSortedByHash, &cachef))
        {
            idx_free(&idx);
            continue;
        }

        cache = cache_new();
        cache->idx = idx;
        idx_free(&cache->idx);
        ptrArray_insertSorted(&g_cachesSortedById, cache);
        ptrArray_insertSorted(&g_cachesSortedByHash, cache);
    }
    ptrList_free(&files, free);
    debugf("Initialize: %d idx loaded\r\n", ptrArray_size(&g_cachesSortedById));
}

static void UninitializeIdxCaches()
{
    ptrArray_free(&g_cachesSortedById, cache_free);
    ptrArray_free(&g_cachesSortedByHash, NULL);
}


int CALLBACK OnPeerConnectableTcpEvent(int sockIdx, int msgCode, UCHAR *data, int dataLen)
{
    struct peer_connectable *pr;

    switch (msgCode)
    {
    case TCP_EV_SEND:
        return -1;

    case TCP_EV_RECEIVE:
        return -1;

    case TCP_EV_ACCEPT:
        break;

    case TCP_EV_CONNECT:
        pr = (struct peer_connectable *)tcp_getUserData(sockIdx);
        if (pr) pr->connectable = PEER_CONNECTABLE;
        return -1;

    case TCP_EV_CLOSE:
        pr = (struct peer_connectable *)tcp_getUserData(sockIdx);
        if (pr) pr->connectable = PEER_UNCONNECTABLE;
        break;

    case TCP_EV_TIMER:
        break;
    }

    return 0;
}

static void CheckPeerConnectable(struct peer_connectable *pr, time_t currTime)
{
    int sockIdx;
    struct in_addr addr;

    addr.s_addr = htonl(pr->ip);
    sockIdx = tcp_connect(inet_ntoa(addr), pr->port, OnPeerConnectableTcpEvent);
    if (sockIdx >= 0) tcp_setUserData(sockIdx, pr);

    pr->lastCheckTime = currTime;
}

static int IsPeerConnectable(struct peer *peer, time_t currTime)
{
    struct peer_connectable *pr, prf = { 0 };

    if (!currTime) time(&currTime);

    prf.ip = peer->ip;
    prf.port = peer->port;
    pr = (struct peer_connectable *)ptrArray_findSorted(&g_peerConnectable, &prf);
    if (pr)
    {
        pr->lastRefTime = currTime;
        if (currTime - pr->lastCheckTime > 120)
            CheckPeerConnectable(pr, currTime);
        return pr->connectable;
    }

    pr = (struct peer_connectable *)malloc(sizeof(struct peer_connectable));
    *pr = prf;
    pr->lastRefTime = currTime;
    ptrArray_insertSorted(&g_peerConnectable, pr);
    CheckPeerConnectable(pr, currTime);

    return 0;
}


struct PeerRequest
{
    CHAR hash[MAX_HASH_LEN];    // task hash
    CHAR pid[MAX_PID_LEN];      // peer id
    UINT16 port;
    INT32 downLimit;
    INT32 upLimit;
    INT64 downloaded;
    UCHAR action;               // 0x01:completed 0x02:exiting
    UINT32 peersWant;
    UCHAR userPrvc;
    UCHAR userType;
    UCHAR userAttr;
    UCHAR lineType;
    UCHAR lineSpeed;
};
static BOOL ParsePeerRequest(CHAR *szReq, struct PeerRequest *req)
{
    CHAR *p = szReq, *next;

    memset(req, 0, sizeof(struct PeerRequest));

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    if (strlen(p)>=MAX_HASH_LEN) return FALSE;
    strcpy_s(req->hash, MAX_HASH_LEN, (CHAR *)p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    if (strlen(p)>=MAX_PID_LEN) return FALSE;
    strcpy_s(req->pid, MAX_PID_LEN, (CHAR *)p); p = next;

    if (!strcmp(req->hash, "exit all")) return TRUE;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->port = (UINT16)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->downLimit = (INT32)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->upLimit = (INT32)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->downloaded = (INT64)_atoi64(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->action = (UCHAR)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->peersWant = (UINT32)atoi(p); p = next;
    req->peersWant = min(50, req->peersWant);

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->userPrvc = (UCHAR)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->userType = (UCHAR)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->userAttr = (UCHAR)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->lineType = (UCHAR)atoi(p); p = next;

    next = strchr(p, ','); if (!next) return FALSE; *next = 0; next ++;
    req->lineSpeed = (UCHAR)atoi(p); p = next;

    return TRUE;
}

static void InsertPeerIntoGroup(struct crowd *crd, INT32 iGroup, struct peer *peer)
{
    struct group *grp, grpf = { 0 };

    grpf.group = iGroup;
    grp = (struct group *)ptrArray_findSorted(&crd->groups, &grpf);
    if (!grp)
    {
        grp = group_new();
        grp->group = iGroup;
        ptrArray_insertSorted(&crd->groups, grp);
    }
    ptrList_append(&grp->peers, peer);
}

static void RemovePeerFromGroup(struct crowd *crd, INT32 iGroup, CHAR *pid)
{
    struct group *grp, grpf = { 0 };
    struct peer *peer;
    struct ptrList *li;

    grpf.group = iGroup;
    grp = (struct group *)ptrArray_findSorted(&crd->groups, &grpf);
    if (grp)
    {
        for (li=grp->peers; li; li=li->next)
        {
            peer = (struct peer *)li->data;
            if (0==strcmp(peer->pid, pid))
            {
                ptrList_remove_node(&grp->peers, li);
                break;
            }
        }
    }
}

static int PeerListCmpFunc(const void *p1, const void *p2)
{
    return strcmp(((struct peer *)p1)->pid, ((struct peer *)p2)->pid);
}

static void GetPeersFromGroup(struct crowd *crd, INT32 iGroup, struct peer *peer,
                              struct ptrList **peers, int peersWant)
{
    struct group *grp, grpf = { 0 };
    struct ptrList *grpPeers, *li;
    struct peer *pr;
    time_t currTime;
    int cnt, got;

    grpf.group = iGroup;
    grp = (struct group *)ptrArray_findSorted(&crd->groups, &grpf);
    if (!grp) return;

    grpPeers = grp->peers;
    cnt = ptrList_size(grpPeers);
    if (cnt <= 1) return;
    got = ptrList_size(*peers);

    li = grpPeers;
    if (cnt > peersWant)
    {
        INT32 n, b;
        b = (cnt + peersWant - 1) / peersWant;
        n = (rand() % b) * peersWant;
        n = min(n, cnt + got - peersWant);
        for (b=0; b<n; b++) li = li->next;
    }

    time(&currTime);

    for (; li; li=li->next)
    {
        pr = (struct peer *)li->data;
        if (0 == strcmp(pr->pid, peer->pid) ||
            (pr->completed && peer->completed))
            continue;

        if (ptrList_find(*peers, pr, PeerListCmpFunc))
            continue;

        pr->connectable = IsPeerConnectable(pr, currTime);
        ptrList_append(peers, pr);
        got ++; if (got >= peersWant) break;
    }
}

static void GetPeersFromCrowd(struct crowd *crd, struct peer *peer, struct ptrList **peers, int peersWant)
{
    struct peer *pr;
    time_t currTime;
    int cnt, got, n;

    cnt = ptrArray_size(&crd->peers);
    if (cnt <= 1) return;
    got = ptrList_size(*peers);

    n = 0;
    if (cnt > peersWant)
    {
        INT32 b;
        b = (cnt + peersWant - 1) / peersWant;
        n = (rand() % b) * peersWant;
        n = min(n, cnt + got - peersWant);
    }

    time(&currTime);

    for (; n<cnt; n++)
    {
        pr = (struct peer *)ptrArray_nth(&crd->peers, n);
        if (0 == strcmp(pr->pid, peer->pid) ||
            (pr->completed && peer->completed))
            continue;

        if (ptrList_find(*peers, pr, PeerListCmpFunc))
            continue;

        pr->connectable = IsPeerConnectable(pr, currTime);
        ptrList_append(peers, pr);
        got ++; if (got >= peersWant) break;
    }
}

struct wakeup_data
{
    int sockIdx;
    CHAR hash[MAX_HASH_LEN];    // hash, NOT id
    time_t timeRequest;
};

static struct ptrList *g_wakeupData = NULL;

static BOOL SendWakeupData(int sockIdx)
{
    struct wakeup_data *wd;
    struct ptrList *list;
    CHAR buf[128];

    for (list=g_wakeupData, wd=NULL; list; list=list->next)
    {
        wd = list->data;
        if (wd->sockIdx == sockIdx) break;
    }
    if (!wd || wd->sockIdx != sockIdx) return FALSE;

    *((UINT32 *)buf) = htonl(6+MAX_HASH_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_NEED_SEED;
    *((UINT32 *)(buf+6)) = 0; // no crypt
    strcpy_s(buf+10, MAX_HASH_LEN, wd->hash);

    tcp_send(sockIdx, buf, 10+MAX_HASH_LEN, NULL, 0);

    ptrList_remove_node(&g_wakeupData, list);
    free(wd);

    return TRUE;
}

static BOOL RemoveWakeupData(int sockIdx)
{
    struct wakeup_data *wd;
    struct ptrList *list;

    for (list=g_wakeupData, wd=NULL; list; list=list->next)
    {
        wd = list->data;
        if (wd->sockIdx == sockIdx) break;
    }
    if (!wd || wd->sockIdx != sockIdx) return FALSE;

    ptrList_remove_node(&g_wakeupData, list);
    free(wd);

    return TRUE;
}

int CALLBACK OnWakeupTcpEvent(int sockIdx, int msgCode, UCHAR *data, int dataLen)
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
        while (SendWakeupData(sockIdx));
        break;

    case TCP_EV_CLOSE:
        while (RemoveWakeupData(sockIdx));
        break;

    case TCP_EV_TIMER:
        break;
    }

    return 0;
}

static void WakeupDedicatedServers(const CHAR *hash)
{
    int i, total, cnt, iStart, sockIdx;

    total = ptrArray_size(&g_dedicatedServers);

    if (total <= 3)
    {
        iStart = 0;
        cnt = total;
    }
    else if (total <= 6)
    {
        iStart = rand() % total;
        cnt = total / 2;
        iStart = min(iStart, total-cnt);
    }
    else
    {
        iStart = rand() % total;
        cnt = total / 3;
        iStart = min(iStart, total-cnt);
    }

    for (i=0; i<cnt; i++)
    {
        struct ip_port *ipport;
        ipport = ptrArray_nth(&g_dedicatedServers, i+iStart);
        sockIdx = tcp_connect(ipport->ip, ipport->port, OnWakeupTcpEvent);
        if (sockIdx >= 0)
        {
            struct wakeup_data *wd;
            wd = malloc(sizeof(struct wakeup_data));
            memset(wd, 0, sizeof(struct wakeup_data));
            wd->sockIdx = sockIdx;
            strcpy_s(wd->hash, MAX_HASH_LEN, hash);
            time(&wd->timeRequest);
            ptrList_append(&g_wakeupData, wd);
        }
    }
}

static BOOL AnyCompletedPeerInPeers(struct ptrList *peers)
{
    struct ptrList *list;
    struct peer *peer;

    for (list=peers; list; list=list->next)
    {
        peer = (struct peer *)list->data;
        if (peer->completed && peer->connectable==PEER_CONNECTABLE) return TRUE;
    }
    return FALSE;
}

static BOOL AnyCompletedPeerInCrowd(struct crowd *crd)
{
    int i;
    struct peer *peer;

    for (i=0; i<ptrArray_size(&crd->peers); i++)
    {
        peer = (struct peer *)ptrArray_nth(&crd->peers, i);
        if (peer->completed && peer->connectable==PEER_CONNECTABLE) return TRUE;
    }
    return FALSE;
}

static void SendPeersResponse0(int sockIdx, int err, CHAR *hash)
{
    CHAR buf[64], *p;

    memset(buf, 0, 64);
    p = buf;
    *((UINT32 *)p) = (UINT32)htonl(15+MAX_HASH_LEN); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_PEERS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4; // no crypt
    *((UINT32 *)p) = (UINT32)htonl(err); p += 4;  // failure
    *((UINT32 *)p) = (UINT32)htonl(g_peersInterval); p += 4; // interval
    strcpy_s(p, MAX_HASH_LEN, hash); p += MAX_HASH_LEN;
    *p = 0; p ++;

    tcp_send(sockIdx, buf, 19+MAX_HASH_LEN, NULL, 0);
}

static BOOL SendPeersResponse(int sockIdx, struct peer *peer, struct crowd *crd, int peersWant)
{
    struct ptrList *peers, *list;
    struct peer *pr;
    INT32 iGroup;
    CHAR buf[2048], *p;
    int bufLen;

    peers = NULL;
    if (peersWant > 0)
    {
        iGroup = peer->lineType * 256 + peer->userPrvc;
        GetPeersFromGroup(crd, iGroup, peer, &peers, peersWant); // 1st, same line and province

        if (ptrList_size(peers) < peersWant)
        {
            iGroup = peer->lineType;
            GetPeersFromGroup(crd, iGroup, peer, &peers, peersWant); // 2nd, same line

            if (ptrList_size(peers) < peersWant)
                GetPeersFromCrowd(crd, peer, &peers, peersWant); // 3rd, global
        }
    }

    bufLen = 31 + MAX_HASH_LEN + (MAX_PID_LEN+19)*ptrList_size(peers);

    p = buf;
    *((UINT32 *)p) = (UINT32)htonl(bufLen-4); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_PEERS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4; // no crypt
    *((UINT32 *)p) = (UINT32)htonl(0); p += 4;  // failure
    *((UINT32 *)p) = (UINT32)htonl(g_peersInterval); p += 4; // interval
    strcpy_s(p, MAX_HASH_LEN, crd->hash); p += MAX_HASH_LEN; // hash
    *p = peer->connectable; p ++;
    *((UINT32 *)p) = (UINT32)htonl(crd->seederCnt); p += 4; // seederCnt
    *((UINT32 *)p) = (UINT32)htonl(ptrArray_size(&crd->peers)); p += 4; // peerCnt
    *((UINT32 *)p) = (UINT32)htonl((int)ptrList_size(peers)); p += 4;
    for (list=peers; list; list=list->next)
    {
        pr = (struct peer *)list->data;
        strcpy_s(p, MAX_PID_LEN, pr->pid); p += MAX_PID_LEN;
        *((UINT32 *)p) = htonl(pr->ip); p += 4;
        *((UINT16 *)p) = htons(pr->port); p += 2;
        *((UINT32 *)p) = htonl(pr->downLimit); p += 4;
        *((UINT32 *)p) = htonl(pr->upLimit); p += 4;
        *p = pr->userPrvc; p ++;
        *p = pr->userType; p ++;
        *p = pr->userAttr|pr->connectable; p ++;
        *p = pr->lineType; p ++;
        *p = pr->lineSpeed; p ++;
    }
    ptrList_free(&peers, NULL);

    tcp_send(sockIdx, buf, bufLen, NULL, 0);

    return (AnyCompletedPeerInPeers(peers) ||
        AnyCompletedPeerInCrowd(crd));
}

static BOOL OnGetPeers(int sockIdx, CHAR *data, int dataLen)
{
    struct tcp_info ti;
    struct PeerRequest req;
    struct cache *cache, cachef = { 0 };
    struct crowd *crd, crdf = { 0 };
    struct peer *peer, peerf = { 0 };
    INT32 ip;
    UCHAR completed;
    int i;

    if (!ParsePeerRequest(data, &req))
    {
        SendPeersResponse0(sockIdx, 1, "");   // req format error
        return FALSE;
    }

    if (!strcmp(req.hash, "exit all"))
    {
        strcpy_s(peerf.pid, MAX_PID_LEN, req.pid);

        for (i=0; i<ptrArray_size(&g_crowds); i++)
        {
            crd = (struct crowd *)ptrArray_nth(&g_crowds, i);
            peer = (struct peer *)ptrArray_removeSorted(&crd->peers, &peerf);
            if (!peer) continue;
            strcpy_s(cachef.idx.id, MAX_ID_LEN, crd->id);
            cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);
            if (!cache) continue;

            //debugf("peer exit %s of %s\r\n", req.pid, crd->id);

            RemovePeerFromGroup(crd, peer->lineType * 256 + peer->userPrvc, req.pid);
            RemovePeerFromGroup(crd, peer->lineType, req.pid);

            if (peer->completed) crd->seederCnt --;
            free(peer);

            SendUiSocketNotify(cache, FALSE);
        }

        SendPeersResponse0(sockIdx, 99, "exit all");
        return FALSE;
    }


    strcpy_s(cachef.idx.hash, MAX_HASH_LEN, req.hash);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedByHash, &cachef);
    if (!cache)
    {
        SendPeersResponse0(sockIdx, 2, req.hash);  // no hash
        return FALSE;
    }

    strcpy_s(crdf.hash, MAX_HASH_LEN, req.hash);
    crd = (struct crowd *)ptrArray_findSorted(&g_crowds, &crdf);
    if (!crd)
    {
        crd = crowd_new();
        strcpy_s(crd->hash, MAX_HASH_LEN, req.hash);
        strcpy_s(crd->id, MAX_ID_LEN, cache->idx.id);
        ptrArray_insertSorted(&g_crowds, crd);
    }

    tcp_getInfo(sockIdx, &ti);
    ip = ntohl(inet_addr(ti.peerAddr.ip));

    //debugf("Request from: %s:%d\r\n", ti.peerAddr.ip, ti.peerAddr.port);
    //debugf("Request: %s %s %d %d %d %d %d %d %d %d %d %d %d %d\r\n",
    //    req.hash, req.id, req.port, req.downLimit, req.upLimit,
    //    req.downloaded, req.completed, req.exiting, req.peersWant,
    //    req.userPrvc, req.userType, req.userAttr, req.lineType, req.lineSpeed);

    strcpy_s(peerf.pid, MAX_PID_LEN, req.pid);
    peer = (struct peer *)ptrArray_findSorted(&crd->peers, &peerf);

    if (req.action & ACTION_EXITING)
    {
        if (peer)
        {
            //debugf("peer exit %s of %s\r\n", req.pid, cache->idx.id);

            RemovePeerFromGroup(crd, req.lineType * 256 + req.userPrvc, req.pid);
            RemovePeerFromGroup(crd, req.lineType, req.pid);

            if (peer->lineType != req.lineType || peer->userPrvc != req.userPrvc)
            {
                RemovePeerFromGroup(crd, peer->lineType * 256 + peer->userPrvc, req.pid);
                if (peer->lineType != req.lineType)
                    RemovePeerFromGroup(crd, peer->lineType, req.pid);
            }

            if (peer->completed) crd->seederCnt --;
            ptrArray_removeSorted(&crd->peers, &peerf);
            free(peer);

            SendUiSocketNotify(cache, FALSE);
        }

        SendPeersResponse0(sockIdx, 99, req.hash);
        return FALSE;
    }

    if (!peer)
    {
        //debugf("new peer %s of %s\r\n", req.pid, cache->idx.id);
        peer = (struct peer *)malloc(sizeof(struct peer));
        memset(peer, 0, sizeof(struct peer));
        strcpy_s(peer->pid, MAX_PID_LEN, req.pid);
        peer->ip = ip;
        peer->port = req.port;
        peer->downLimit = req.downLimit;
        peer->upLimit = req.upLimit;
        peer->downloaded = req.downloaded;
        peer->completed  = req.action & ACTION_COMPLETED ? 1 : 0;
        peer->userPrvc = req.userPrvc;
        peer->userType = req.userType;
        peer->userAttr = req.userAttr;
        peer->lineType = req.lineType;
        peer->lineSpeed = req.lineSpeed;
        peer->connectable = IsPeerConnectable(peer, 0);
        time(&peer->lastAccessTime);

        ptrArray_insertSorted(&crd->peers, peer);

        InsertPeerIntoGroup(crd, req.lineType * 256 + req.userPrvc, peer);
        InsertPeerIntoGroup(crd, req.lineType, peer);
        if (peer->completed) crd->seederCnt ++;

        SendUiSocketNotify(cache, FALSE);
    }
    else
    {
        //debugf("old peer %s of %s\r\n", req.pid, cache->idx.id);
        if (peer->lineType != req.lineType || peer->userPrvc != req.userPrvc)
        {
            RemovePeerFromGroup(crd, peer->lineType * 256 + peer->userPrvc, req.pid);
            InsertPeerIntoGroup(crd, req.lineType * 256 + req.userPrvc, peer);

            if (peer->lineType != req.lineType)
            {
                RemovePeerFromGroup(crd, peer->lineType, req.pid);
                InsertPeerIntoGroup(crd, req.lineType, peer);
            }
        }
        completed = req.action & ACTION_COMPLETED ? 1 : 0;
        if (peer->completed != completed)
        {
            if (peer->completed) crd->seederCnt --;
            else crd->seederCnt ++;

            SendUiSocketNotify(cache, FALSE);
        }

        if (peer->ip != ip || peer->port != req.port)
        {
            peer->connectable = 0;
            peer->ip = ip;
            peer->port = req.port;
        }
        peer->connectable = IsPeerConnectable(peer, 0);

        peer->downLimit = req.downLimit;
        peer->upLimit = req.upLimit;
        peer->downloaded = req.downloaded;
        peer->completed  = completed;
        peer->userPrvc = req.userPrvc;
        peer->userType = req.userType;
        peer->userAttr = req.userAttr;
        peer->lineType = req.lineType;
        peer->lineSpeed = req.lineSpeed;
        time(&peer->lastAccessTime);
    }

    if (!SendPeersResponse(sockIdx, peer, crd, req.peersWant))
        WakeupDedicatedServers(req.hash);

    return TRUE;
}


static void DedicatedServersToString(CHAR **data, int *dataLen, const CHAR *delimiter)
{
    CHAR *buf = NULL, szTmp[256];
    int bufSize, bufLen, i, l;
    struct ip_port *ipport;

    bufSize = ptrArray_size(&g_dedicatedServers);
    if (bufSize)
    {
        bufSize *= 32;
        buf = (CHAR *)malloc(bufSize);
        for (i=bufLen=0; i<ptrArray_size(&g_dedicatedServers); i++)
        {
            ipport = (struct ip_port *)ptrArray_nth(&g_dedicatedServers, i);
            l = sprintf_s(szTmp, 256, "%s:%d%s", ipport->ip, ipport->port, delimiter);
            if (bufLen+l >= bufSize) break;
            memcpy(buf+bufLen, szTmp, l+1);
            bufLen += l;
        }
        bufLen ++;

        *data = buf;
        *dataLen = bufLen;
    }
    else
    {
        *data = NULL;
        *dataLen = 0;
    }
}

static void DedicatedServersFromString(CHAR *data, const CHAR *delimiter)
{
    CHAR *p1, *p2, *pe;
    struct ip_port *ipPort;
    int delimiterLen;

    ptrArray_free(&g_dedicatedServers, free);
    delimiterLen = strlen(delimiter);

    p2 = data;
    while (1)
    {
        p1 = p2;

        p2 = strstr(p1, delimiter);
        if (!p2) break; *p2 = 0; p2 += delimiterLen;

        pe = strchr(p1, ':');
        if (!pe) continue; *pe = 0; pe ++;

        if (strlen(p1) >= MAX_IP_LEN) continue;

        ipPort = (struct ip_port *)malloc(sizeof(struct ip_port));
        strcpy_s(ipPort->ip, MAX_IP_LEN, p1);
        ipPort->port = (u_short)atoi(pe);
        ptrArray_insertSorted(&g_dedicatedServers, ipPort);
    }
}

static void OnGetSvrOptions(int sockIdx, CHAR *data, int dataLen)
{
    CHAR buf[2048], *p, *svrs;
    int svrsLen, l;

     p = buf;
    *((UINT32 *)p) = htonl(0); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_OPTIONS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    *((UINT16 *)p) = htons(g_portNum); p += 2;
    *((UINT32 *)p) = htonl(g_peersInterval); p += 4;
    strcpy_s(p, 30, g_adminPwd); p += strlen(g_adminPwd) + 1;
    strcpy_s(p, 512, g_adminIps); p += strlen(g_adminIps) + 1;
    DedicatedServersToString(&svrs, &svrsLen, "\r\n");
    if (!svrsLen) { *p = 0; p ++; }
    else
    {
        if (svrsLen >= 1024) { *p = 0; p ++; }
        else { memcpy(p, svrs, svrsLen); p += svrsLen; }
        free(svrs);
    }

    l = (int)(p - buf);
    *((UINT32 *)buf) = htonl(l-4);

    tcp_send(sockIdx, buf, l, NULL, 0);
}

static void OnSetSvrOptions(int sockIdx, CHAR *data, int dataLen)
{
    CHAR *p = data;
    CHAR buf[256];

    g_portNum = ntohs(*((UINT16 *)p)); p += 2;
    g_peersInterval = ntohl(*((UINT32 *)p)); p += 4;
    strcpy_s(g_adminPwd, 30, p); p += strlen(g_adminPwd) + 1;
    strcpy_s(g_adminIps, 512, p); p += strlen(g_adminIps) + 1;
    DedicatedServersFromString(p, "\r\n");

    SaveOptions();

    p = buf;
    *((UINT32 *)p) = htonl(6); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_SET_SVR_OPTIONS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    tcp_send(sockIdx, buf, 10, NULL, 0);
}

static void OnGetSvrStatus(int sockIdx, CHAR *data, int dataLen)
{
    CHAR buf[256], *p = buf;

    *((UINT32 *)p) = htonl(22); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_STATUS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    *((UINT32 *)p) = htonl((INT32)g_startTime); p += 4;
    *((UINT32 *)p) = htonl(ptrArray_size(&g_cachesSortedById)); p += 4;
    *((UINT32 *)p) = htonl(ptrArray_size(&g_crowds)); p += 4;
    *((UINT32 *)p) = htonl(ptrArray_size(&g_peerConnectable)); p += 4;

    tcp_send(sockIdx, buf, 26, NULL, 0);
}

static void _CachePeersToString(struct cache *cache, CHAR **data, int *dataLen)
{
    struct crowd *crd, crowdf = { 0 };
    CHAR *buf = NULL, szTmp[256];
    int bufSize, bufLen, i, l;
    struct peer *peer;
    struct in_addr inaddr;

    *data = NULL;
    *dataLen = 0;

    strcpy_s(crowdf.hash, MAX_HASH_LEN, cache->idx.hash);
    crd = (struct crowd *)ptrArray_findSorted(&g_crowds, &crowdf);
    if (!crd) return;

    bufSize = ptrArray_size(&crd->peers);
    if (bufSize)
    {
        bufSize *= 64;
        buf = (CHAR *)malloc(bufSize);
        for (i=bufLen=0; i<ptrArray_size(&crd->peers); i++)
        {
            peer = (struct peer *)ptrArray_nth(&crd->peers, i);
            inaddr.s_addr = htonl(peer->ip);
            l = sprintf_s(szTmp, 256, "%s:%u:%d:%u;",
                inet_ntoa(inaddr), peer->port,
                IsPeerConnectable(peer, 0), peer->completed);
            if (bufLen+l >= bufSize) break;
            memcpy(buf+bufLen, szTmp, l+1);
            bufLen += l;
        }
        bufLen ++;

        *data = buf;
        *dataLen = bufLen;
    }
}

static void OnGetSvrPeers(int sockIdx, CHAR *data, int dataLen)
{
    CHAR buf[64], *p, *peers;
    int peersLen;
    struct cache *cache, cachef = { 0 };

    if (strlen(data) >= MAX_ID_LEN) return;
    strcpy_s(cachef.idx.id, MAX_ID_LEN, data);
    cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);
    if (!cache) return;

    _CachePeersToString(cache, &peers, &peersLen);

    p = buf;
    *((UINT32 *)p) = htonl(6+MAX_ID_LEN+peersLen); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_PEERS_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt
    strcpy_s(p, MAX_ID_LEN, data); p += MAX_ID_LEN;

    tcp_send(sockIdx, buf, 10+MAX_ID_LEN, peers, peersLen);
    if (peers) free(peers);
}

static int _IdxCacheToUtf8String(struct cache *cache, CHAR *buf, int bufSize, BOOL del)
{
    struct crowd *crowd, crowdf = { 0 };
    INT32 seeders, leechers;
    CHAR name[256], category[64];

    strcpy_s(crowdf.hash, MAX_HASH_LEN, cache->idx.hash);
    crowd = (struct crowd *)ptrArray_findSorted(&g_crowds, &crowdf);
    if (crowd)
    {
        seeders = crowd->seederCnt;
        leechers = ptrArray_size(&crowd->peers) - seeders;
    }
    else seeders = leechers = 0;

    return sprintf_s(buf, bufSize,
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%u\t"
        "%u\t"
        "%I64d\t"
        "%I64d\t"
        "%d\t"
        "%d\r\n",
        cache->idx.id,
        del?"delete":cache->idx.hash,
        UnicodeToUtf8(cache->idx.name, name, 256),
        UnicodeToUtf8(cache->idx.category, category, 64),
        cache->idx.extraInfo,
        cache->idx.pieceLength,
        cache->idx.pieceCount,
        cache->idx.bytes,
        cache->idx.creationDate,
        seeders,
        leechers);
}

void SendUiSocketNotify(struct cache *cache, BOOL del)
{
    CHAR buf[2048];
    int tmpLen;

    if (g_uiSock < 0) return;

    *((UINT32 *)buf) = htonl(0);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_SVR_IDX_CHANGED;
    *((UINT32 *)(buf+6)) = 0; // no crypt

    tmpLen = _IdxCacheToUtf8String(cache, buf+10, 2000, del) + 1;
    *((UINT32 *)buf) = htonl(6+tmpLen);

    tcp_send(g_uiSock, buf, 10+tmpLen, NULL, 0);
}

static void _BuildIdxListData(CHAR **idxListData_, int *idxListSize_)
{
    struct cache *cache;
    CHAR *idxListData;
    int idxListSize;
    CHAR buf[2048];
    INT i, tmpLen, idxListSizeMax;

    *idxListData_ = NULL;
    *idxListSize_ = 0;

    if (!ptrArray_size(&g_cachesSortedById)) return;

    idxListSizeMax = ptrArray_size(&g_cachesSortedById)*1024+4096;
    idxListData = (CHAR *)malloc(idxListSizeMax);
    if (!idxListData) return;
    idxListSize = 0;
    for (i=0; i<ptrArray_size(&g_cachesSortedById); i++)
    {
        cache = (struct cache *)ptrArray_nth(&g_cachesSortedById, i);
        tmpLen = _IdxCacheToUtf8String(cache, buf, 2048, FALSE);

        if (idxListSize+tmpLen >= idxListSizeMax) break;
        memcpy(idxListData+idxListSize, buf, tmpLen+1);
        idxListSize += tmpLen;
    }
    idxListSize ++; // tail 0

    *idxListData_ = idxListData;
    *idxListSize_ = idxListSize;
}

static void OnGetSvrIdxList(int sockIdx, CHAR *data, int dataLen)
{
    CHAR buf[200], *p = buf;
    CHAR *idxListData;
    int idxListSize;

    *((UINT32 *)p) = htonl(0); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_GET_SVR_IDX_LIST_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    _BuildIdxListData(&idxListData, &idxListSize);

    *((UINT32 *)buf) = htonl(6 + idxListSize);
    tcp_send(sockIdx, buf, 10, idxListData, idxListSize);

    if (idxListData) free(idxListData);
}

static void OnRegSvrSock(int sockIdx, CHAR *data, int dataLen)
{
    CHAR buf[64], *p = buf;

    OnGetSvrIdxList(sockIdx, NULL, 0);

    *((UINT32 *)p) = htonl(6); p += 4;
    *p = CMD_ADMIN; p ++;
    *p = GM_REG_SVR_SOCK_RESP; p ++;
    *((UINT32 *)p) = 0; p += 4;  // no crypt

    tcp_send(sockIdx, buf, 10, NULL, 0);

    g_uiSock = sockIdx;
}

static void OnTcpTimer()
{
    static time_t lastCheckTime = 0;
    struct cache *cache, cachef = { 0 };
    struct crowd *crd;
    struct peer *peer;
    struct peer_connectable *pr;
    time_t currTime;
    int i, j;

    time(&currTime);

    if (currTime - lastCheckTime < 30) return;
    lastCheckTime = currTime;

    for (i=0; i<ptrArray_size(&g_crowds); i++)
    {
        crd = (struct crowd *)ptrArray_nth(&g_crowds, i);
        for (j=ptrArray_size(&crd->peers)-1; j>=0; j--)
        {
            peer = (struct peer *)ptrArray_nth(&crd->peers, j);
            if (currTime - peer->lastAccessTime >= g_peersInterval*2+30)
            {
                RemovePeerFromGroup(crd, peer->lineType * 256 + peer->userPrvc, peer->pid);
                RemovePeerFromGroup(crd, peer->lineType * 256, peer->pid);
                ptrArray_erase(&crd->peers, j, j+1);
                if (peer->completed) crd->seederCnt --;
                free(peer);

                strcpy_s(cachef.idx.id, MAX_ID_LEN, crd->id);
                cache = (struct cache *)ptrArray_findSorted(&g_cachesSortedById, &cachef);
                if (cache) SendUiSocketNotify(cache, FALSE);
            }
        }
    }

    for (i=ptrArray_size(&g_peerConnectable)-1; i>=0; i--)
    {
        pr = (struct peer_connectable *)ptrArray_nth(&g_peerConnectable, i);
        if (currTime - pr->lastRefTime > 600)
        {
            ptrArray_erase(&g_peerConnectable, i, i+1);
            free(pr);
        }
    }
}

static int OnMsgReceived(int sockIdx, UCHAR *msgData, int msgLen)
{
    int cmdLen;

    if (!msgData || msgLen < 4) return 0;

    cmdLen = 4 + (int)ntohl(*((UINT32 *)msgData));

    if (cmdLen < 4) return -1;
    if (cmdLen > 10*1024*1024) return -1; // max packet
    if (cmdLen > msgLen)
    { tcp_setExpectPackLen(sockIdx, cmdLen); return 0; }

    if (msgData[4] != CMD_ADMIN)
    {
        debugf("error cmd: %u\r\n", msgData[4]);
        return -1;
    }

    EncryptData(msgData+6, cmdLen-6);

    switch (msgData[5])
    {
    case GM_GET_IDX_LIST:
        OnGetIdxList(sockIdx, (CHAR *)&msgData[10]);
        break;
    case GM_GET_IDX:
        OnGetIdx(sockIdx, (CHAR *)&msgData[10]);
        break;

    case GM_SET_IDX:
        OnSetIdx(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;
    case GM_DEL_IDX:
        OnDelIdx(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_GET_PEERS:
        OnGetPeers(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_GET_SVR_OPTIONS:
        OnGetSvrOptions(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_SET_SVR_OPTIONS:
        OnSetSvrOptions(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_GET_SVR_STATUS:
        OnGetSvrStatus(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_GET_SVR_PEERS:
        OnGetSvrPeers(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_GET_SVR_IDX_LIST:
        OnGetSvrIdxList(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    case GM_REG_SVR_SOCK:
        OnRegSvrSock(sockIdx, (CHAR *)&msgData[10], msgLen-10);
        break;

    default:
        break;
    }

    return cmdLen;
}

int CALLBACK OnTcpEvent(int sockIdx, int msgCode, UCHAR *data, int dataLen)
{
    switch (msgCode)
    {
    case TCP_EV_SEND: // once sent, close
        break;

    case TCP_EV_RECEIVE:
        return OnMsgReceived(sockIdx, data, dataLen);

    case TCP_EV_ACCEPT:
        break;

    case TCP_EV_CONNECT:
        break;

    case TCP_EV_CLOSE:
        if (sockIdx == g_uiSock) g_uiSock = -1;
        break;

    case TCP_EV_TIMER:
        OnTcpTimer();
        break;
    }

    return 0;
}

static BOOL ini_writeUint(HANDLE hFile, const CHAR *key, UINT32 val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024];

    bufLen = sprintf_s(buf, 1024, "%s=%u\r\n", key, val);
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}
static BOOL ini_writeStr(HANDLE hFile, const CHAR *key, const CHAR *val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024];

    bufLen = sprintf_s(buf, 1024, "%s=%s\r\n", key, val);
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}
static BOOL ini_writeWstr(HANDLE hFile, const CHAR *key, const WCHAR *val)
{
    DWORD dwWritten, bufLen;
    CHAR buf[1024], szTmp[512];

    bufLen = sprintf_s(buf, 1024, "%s=%s\r\n", key, UnicodeToMbcs(val, szTmp, 512));
    return WriteFile(hFile, buf, bufLen, &dwWritten, NULL) && dwWritten==bufLen;
}

BOOL SaveOptions()
{
    HANDLE hFile;
    WCHAR fileName[MAX_PATH];
    CHAR *buf;
    int bufLen;

    swprintf_s(fileName, MAX_PATH, L"%s\\options.txt", g_workDir);
    hFile = CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    ini_writeUint(hFile, "port_num", g_portNum);
    ini_writeUint(hFile, "peers_interval", g_peersInterval);
    ini_writeStr(hFile, "admin_ips", g_adminIps);
    ini_writeStr(hFile, "admin_pwd", g_adminPwd);
    DedicatedServersToString(&buf, &bufLen, ";");
    if (!bufLen)
        ini_writeStr(hFile, "dedicated_servers", "");
    else
    {
        ini_writeStr(hFile, "dedicated_servers", buf);
        free(buf);
    }

    SetEndOfFile(hFile);
    CloseHandle(hFile);
    return TRUE;
}

void ReadOptions()
{
    WCHAR fileName[MAX_PATH];
    UCHAR *fileData;
    DWORD fileSize;
    CHAR *pLine, *pTmp, *pEq;

    swprintf_s(fileName, MAX_PATH, L"%s\\options.txt", g_workDir);
    fileData = GetFileContent(fileName, &fileSize);
    if (!fileData) { SaveOptions(); return; }

    pTmp = (CHAR *)fileData;
    while (1)
    {
        pLine = pTmp;

        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        pEq = strchr(pLine, '=');
        if (!pEq) continue;
        *pEq = 0; pEq ++;

        if (0==strcmp(pLine, "port_num"))
            g_portNum = (UINT16)atoi(pEq);
        else if (0==strcmp(pLine, "peers_interval"))
            g_peersInterval = (INT)atoi(pEq);
        else if (0==strcmp(pLine, "admin_ips"))
        {
            if (strlen(pEq) < 512)
                strcpy_s(g_adminIps, 512, pEq);
        }
        else if (0==strcmp(pLine, "admin_pwd"))
        {
            if (strlen(pEq) < MAX_PWD_LEN)
                strcpy_s(g_adminPwd, MAX_PWD_LEN, pEq);
        }
        else if (0==strcmp(pLine, "dedicated_servers"))
            DedicatedServersFromString(pEq, ";");
    }

    free(fileData);
}

void Service_Init(int argc, TCHAR *argv[])
{
}

int ipportCmp(const void *p1, const void *p2)
{
    return strcmp(((struct ip_port *)p1)->ip, ((struct ip_port *)p2)->ip);
}
int cacheIdCmp(const void *p1, const void *p2)
{
    return strcmp(((struct cache *)p1)->idx.id, ((struct cache *)p2)->idx.id);
}
int cacheHashCmp(const void *p1, const void *p2)
{
    return strcmp(((struct cache *)p1)->idx.hash, ((struct cache *)p2)->idx.hash);
}
int crowdCmp(const void *p1, const void *p2)
{
    return strcmp(((struct crowd *)p1)->hash, ((struct crowd *)p2)->hash);
}
int connectableCmp(const void *p1, const void *p2)
{
    const struct peer_connectable *pr1 = p1, *pr2 = p2;

    if (pr1->ip == pr2->ip)
    {
        if (pr1->port == pr2->port) return 0;
        else if (pr1->port < pr2->port) return -1;
        else return 1;
    }
    else if (pr1->ip < pr2->ip) return -1;
    else return 1;
}

int Service_Run()
{
    WSADATA wsd;
    int i;

    time(&g_startTime);

    if (WSAStartup(MAKEWORD(2, 2), &wsd)) return 0;

    GetModuleFileNameW(NULL, g_workDir, MAX_PATH);
    for (i=(int)wcslen(g_workDir); i>0; i--)
    { if (g_workDir[i] == L'\\') { g_workDir[i] = 0; break; } }

    debugf_startup(L"gmSvr.log", 10*1024*1024, DEBUGF_DEBUG|DEBUGF_FILE|DEBUGF_STDIO);

    debugf("work dir: %S\r\n", g_workDir);

    ptrArray_init(&g_dedicatedServers, ipportCmp);
    ptrArray_init(&g_cachesSortedById, cacheIdCmp);
    ptrArray_init(&g_cachesSortedByHash, cacheHashCmp);
    ptrArray_init(&g_cachesDirty, cacheIdCmp);
    ptrArray_init(&g_crowds, crowdCmp);

    ptrArray_init(&g_peerConnectable, connectableCmp);

    ReadOptions();

    InitializeIdxCaches();
    BuildIdxListData2(TRUE);

    tcp_startup(64*1024, OnTcpEvent);

    g_listenSock = tcp_listen("", g_portNum, NULL);
    debugf("listen socket port: %u sock: %d\r\n", g_portNum, g_listenSock);

    //getchar();
    WaitForSingleObject(g_hEventStopService, INFINITE);

    UninitializeIdxCaches();

    tcp_close(g_listenSock);

    tcp_cleanup();

    WSACleanup();

    ptrArray_free(&g_crowds, crowd_free);
    ptrArray_init(&g_cachesSortedByHash, NULL);
    ptrArray_free(&g_cachesSortedById, cache_free);
    ptrArray_free(&g_cachesDirty, NULL);
    ptrArray_free(&g_dedicatedServers, free);

    ptrArray_free(&g_peerConnectable, free);

    ptrList_free(&g_wakeupData, free);
    ptrList_free(&g_updateNotifyData, free);

    if (g_idxListData1) free(g_idxListData1);
    if (g_idxListData2) free(g_idxListData2);

    return 0;
}

