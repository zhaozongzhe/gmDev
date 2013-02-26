#include "gmCore.h"

#define MAX_SERVERS     5
struct server
{
    CHAR ip[MAX_IP_LEN];
    u_short port;

    SOCKET sock;
    WSAEVENT hEventSock;
    WSAEVENT hEventSend;
    time_t nextConnectTime;
    time_t lastReceiveTime;
    struct buffer recvBuf;
    struct buffer sendBuf;
    CRITICAL_SECTION cs;
};

BOOL g_svrInitOk = TRUE;
int g_svr = 0;              // selected server index, default=0
int g_svrInteval = 2*60;    // inteval get peers
UCHAR g_amConnectable = 0;  // am connectable
static struct server g_servers[MAX_SERVERS] = { 0 };
static WSAEVENT g_svrThreadStopEvent = NULL;
static WSAEVENT g_svrThreadStoppedEvent = NULL;

// ----------------------------------------------------------------------------
struct ptrArray g_netIdxSortedById = { 0 };
struct ptrArray g_netIdxSortedByHash = { 0 };

BOOL g_idxListInitialized = FALSE;
CHAR g_idxListVer1[MAX_VERSION_LEN] = { 0 };
CHAR g_idxListVer2[MAX_VERSION_LEN] = { 0 };

static time_t g_lastIdxListTime = 0;
static time_t g_lastIdxListSuccessTime = 0;
#define MIN_IDX_LIST_INTEVAL        60
#define MAX_IDX_LIST_INTEVAL        3*60


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

static int svr_makePeersRequestBuf(struct task *task, BOOL exiting, int peersWant, CHAR buf[512])
{
    int reqLen, action = 0;

    if (task->bytesDownloaded+task->tmp.bytesDownloaded >= task->idx.bytes) action |= 1;
    if (exiting) action |= 2;

    reqLen = sprintf_s(buf+10, 502,
        "%s," // hash
        "%s," // pid
        "%d," // port
        "%d," // downLimit
        "%d," // upLimit
        "%I64u," // downloaded
        "%d," // action: starting/completed/exiting
        "%d," // peersWant
        "%d," // userPrvc
        "%d," // userType
        "%d," // userAttr
        "%d," // lineType
        "%d,", // lineSpeed
        task->idx.hash,
        g_options.myId,
        g_options.portNum,
        g_options.downLimit,
        g_options.upLimit,
        task->bytesDownloaded+task->tmp.bytesDownloaded,
        action,
        peersWant,
        g_options.userPrvc,
        g_options.userType,
        g_options.userAttr,
        g_options.lineType,
        g_options.lineSpeed) + 1;

    *((UINT32 *)buf) = htonl(6+reqLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_PEERS;
    *((UINT32 *)(buf+6)) = htonl(GetTickCount());

    EncryptData((UCHAR *)buf+6, 4+reqLen);

    return (10+reqLen);
}

void svr_sendPeersRequest(int iSvr, struct task *task, BOOL exiting, int peersWant)
{
    int reqLen;
    CHAR buf[512];

    reqLen = svr_makePeersRequestBuf(task, exiting, peersWant, buf);

    EnterCriticalSection(&g_servers[iSvr].cs);
    buffer_append(&g_servers[iSvr].sendBuf, buf, reqLen);
    WSASetEvent(g_servers[iSvr].hEventSend);
    LeaveCriticalSection(&g_servers[iSvr].cs);
}

static void svr_postTcpEventPeers(CHAR *data, int dataLen)
{
    struct tcp_custom_task_peers *tcpCustom;

    tcpCustom = malloc(sizeof(struct tcp_custom_task_peers));
    if (!tcpCustom) return;

    memset(tcpCustom, 0, sizeof(struct tcp_custom_task_peers));
    tcpCustom->ioType = TCP_CUSTOM_TASK_PEERS;
    tcpCustom->data = (CHAR *)malloc(dataLen);
    if (!tcpCustom->data)
    {
        free(tcpCustom);
        return;
    }
    memcpy(tcpCustom->data, data, dataLen);
    tcpCustom->dataLen = dataLen;

    tcp_postEvent(tcpCustom);
}

struct svr_peer
{
    CHAR pid[MAX_PID_LEN];
    CHAR ip[MAX_IP_LEN];
    UINT16 port;
    UINT32 downLimit;
    UINT32 upLimit;
    UCHAR userPrvc;
    UCHAR userType;
    UCHAR userAttr;
    UCHAR lineType;
    UCHAR lineSpeed;
};

// 10.0.0.0~10.255.255.255
// 172.16.0.0~172.31.255.255
// 192.168.0.0~192.168.255.255
static BOOL IsLocalIpAddr(const CHAR *ip)
{
    if (0 == memcmp(ip, "10.", 3)) return TRUE;
    if (0 == memcmp(ip, "192.168.", 8)) return TRUE;
    if (0 == memcmp(ip, "172.", 4))
    {
        CHAR sz[32];
        int s;
        strcpy_s(sz, 32, ip+4);
        if (sz[2] == '.')
        {
            sz[2] = 0;
            s = atoi(sz);
            if (s >= 16 && s <= 31) return TRUE;
        }
    }
    return FALSE;
}

static BOOL IsPeerExistInList(struct ptrList *peers, struct svr_peer *pr)
{
    struct peer *peer;
    struct ptrList *list;

    for (list=peers; list; list=list->next)
    {
        peer = list->data;
        if (0 == strcmp(peer->pid, pr->pid))
        {
            if (!IsLocalIpAddr(peer->ip))
            {
                strcpy_s(peer->ip, MAX_IP_LEN, pr->ip);
                peer->port = pr->port;
            }
            return TRUE;
        }
    }
    return FALSE;
}

void svr_onPeers(UCHAR *msgData, int msgLen)
{
    UCHAR *p;
    CHAR hash[MAX_HASH_LEN];
    int cmdLen, failure, interval = 0;
    int peerCount, i, newPeersAdded;
    struct svr_peer *svrPeers;
    struct task *task;
    struct peer *peer;
    struct in_addr addr;

    cmdLen = 4 + (int)ntohl(*((UINT32 *)msgData));
    if (cmdLen != msgLen) return;

    p = msgData + 10;

    failure = ntohl(*((int *)p)); p += 4;
    interval = ntohl(*((int *)p)); p += 4;
    if (strlen((CHAR *)p) >= MAX_HASH_LEN) return;
    strcpy_s(hash, MAX_HASH_LEN, (CHAR *)p); p += MAX_HASH_LEN;
    if (failure)
    {
        if (failure == 2)
        {
            task = task_findHash(hash);
            if (task) debugf("got peers response failure: %d %s\r\n", failure, task->idx.id);
            svr_sendIdxListRequest(g_svr, 0);
        }
        else if (failure != 99)
            debugf("got peers response failure: %d\r\n", failure);

        return;
    }
    g_amConnectable = *p; p ++;

    task = task_findHash(hash);
    if (!task)
    {
        debugf("got peers, but task NOT exist: %s\r\n", hash);
        return;
    }
    task->totalSeeders = ntohl(*((int *)p)); p += 4;
    task->totalPeers = ntohl(*((int *)p)); p += 4;
    peerCount = ntohl(*((int *)p)); p += 4;
    if (msgLen != 31 + MAX_HASH_LEN + (MAX_PID_LEN+19)*peerCount)
    {
        //debugf("msgLen: %d %d\r\n", msgLen, 31 + MAX_HASH_LEN + (MAX_PID_LEN+19)*peerCount);
        return;
    }

    if (peerCount <= 0 || peerCount > 100)
    {
        admin_sendTrackerInfo(task);
        return;
    }

    svrPeers = (struct svr_peer *)calloc(peerCount, sizeof(struct svr_peer));
    for (i=0; i<peerCount; i++)
    {
        if (strlen((CHAR *)p) >= MAX_PID_LEN) break;
        strcpy_s(svrPeers[i].pid, MAX_PID_LEN, (CHAR *)p);  p += MAX_PID_LEN;
        addr.s_addr = *((UINT32 *)p); p += 4;
        strcpy_s(svrPeers[i].ip, MAX_IP_LEN, inet_ntoa(addr));
        svrPeers[i].port = ntohs(*((UINT16 *)p)); p += 2;
        svrPeers[i].downLimit = ntohl(*((UINT32 *)p)); p += 4;
        svrPeers[i].upLimit = ntohl(*((UINT32 *)p)); p += 4;
        svrPeers[i].userPrvc = *p; p ++;
        svrPeers[i].userType = *p; p ++;
        svrPeers[i].userAttr = *p; p ++;
        svrPeers[i].lineType = *p; p ++;
        svrPeers[i].lineSpeed = *p; p ++;
    }

    for (i=0, newPeersAdded=0; i<peerCount; i++)
    {
        if (IsPeerExistInList(task->peersCandidate, &svrPeers[i])) continue;
        if (IsPeerExistInList(task->peersOutgoing, &svrPeers[i])) continue;

        peer = peer_new();
        peer->task = task;
        peer->sock = -1;
        peer->isOutgoing = 1;
        strcpy_s(peer->pid, MAX_PID_LEN, svrPeers[i].pid);
        strcpy_s(peer->ip, MAX_IP_LEN, svrPeers[i].ip);
        peer->port = svrPeers[i].port;
        peer->downLimit = svrPeers[i].downLimit;
        peer->upLimit = svrPeers[i].upLimit;
        peer->userPrvc = svrPeers[i].userPrvc;
        peer->userType = svrPeers[i].userType;
        peer->userAttr = svrPeers[i].userAttr;
        peer->lineType = svrPeers[i].lineType;
        peer->lineSpeed = svrPeers[i].lineSpeed;

        ptrList_append(&task->peersCandidate, peer);

        newPeersAdded ++;
    }
    free(svrPeers);

    if (newPeersAdded)
    {
        time_t currTime;
        time(&currTime);
        task_checkCandidatePeers(task, currTime);
    }

    admin_sendTrackerInfo(task);
}

void svr_sendIdxListRequest(int iSvr, time_t currTime)
{
    CHAR buf[128];

    if (!currTime) time(&currTime);

    if (currTime - g_lastIdxListTime < (g_idxListInitialized?MIN_IDX_LIST_INTEVAL:15))
        return;
    g_lastIdxListTime = currTime;

    if (!g_svrInitOk) svr_restart();

    memset(buf, 0, sizeof(buf));
    *((UINT32 *)buf) = htonl(6+2*MAX_VERSION_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_IDX_LIST;
    *((UINT32 *)(buf+6)) = htonl(GetTickCount());
    strcpy_s(buf+10, MAX_VERSION_LEN, g_idxListVer1);
    strcpy_s(buf+10+MAX_VERSION_LEN, MAX_VERSION_LEN, g_idxListVer2);

    EncryptData((UCHAR *)(buf+6), 4+2*MAX_VERSION_LEN);

    EnterCriticalSection(&g_servers[iSvr].cs);
    buffer_append(&g_servers[iSvr].sendBuf, buf, 10+2*MAX_VERSION_LEN);
    WSASetEvent(g_servers[iSvr].hEventSend);
    LeaveCriticalSection(&g_servers[iSvr].cs);
}

static void NetIdxFromCompressed(struct ptrArray *netIdxList, UCHAR *pCompressed, int compressedLen)
{
    UCHAR *listData;
    uLongf listDataLen;
    CHAR *pLine, *pTmp;
    struct idx_net idxn, *pIdxn;

    listDataLen = ntohl(*((UINT32 *)pCompressed)) + 8192;
    listData = (UCHAR *)malloc(listDataLen);
    if (!listData)
    {
        debugf("uncompress malloc() FAILED: %d\r\n", listDataLen);
        return;
    }
    if (Z_OK != uncompress(listData, &listDataLen, pCompressed+4, compressedLen-4))
    {
        debugf("uncompress FAILED\r\n");
        free(listData);
        return;
    }

    pLine = (CHAR *)listData;
    while (1)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break; *pTmp = 0; pTmp += 2;
        if (!IdxNetFromUtf8String(&idxn, pLine)) break;

        pIdxn = (struct idx_net *)malloc(sizeof(struct idx_net)); if (!pIdxn) break;
        *pIdxn = idxn;
        ptrArray_insertSorted(netIdxList, pIdxn);

        pLine = pTmp;
    }

    free(listData);
}

static int netIdx_cmpId(const void *p1, const void *p2)
{
    return strcmp(((struct idx_net *)p1)->id, ((struct idx_net *)p2)->id);
}
static void svr_onIdxListResp(CHAR *msgData, int msgLen)
{
    CHAR *p;
    UINT32 idxListSize1, idxListSize2;
    struct tcp_custom_idx_list *tcpCustom;

    time(&g_lastIdxListSuccessTime);

    p = msgData + 10;

    idxListSize1 = (UINT32)ntohl(*((UINT32 *)p)); p += 4;
    idxListSize2 = (UINT32)ntohl(*((UINT32 *)p)); p += 4;
    if (msgLen != 18+2*MAX_VERSION_LEN+idxListSize1+idxListSize2) return;

    if (strlen((CHAR *)p) >= MAX_VERSION_LEN) return;
    strcpy_s(g_idxListVer1, MAX_VERSION_LEN, (CHAR *)p); p += MAX_VERSION_LEN;
    if (strlen((CHAR *)p) >= MAX_VERSION_LEN) return;
    strcpy_s(g_idxListVer2, MAX_VERSION_LEN, (CHAR *)p); p += MAX_VERSION_LEN;

    tcpCustom = (struct tcp_custom_idx_list *)malloc(sizeof(struct tcp_custom_idx_list));
    if (!tcpCustom) return;
    memset(tcpCustom, 0, sizeof(struct tcp_custom_idx_list));
    tcpCustom->ioType = TCP_CUSTOM_IDX_LIST;
    ptrArray_init(&tcpCustom->netIdxList1, netIdx_cmpId);
    ptrArray_init(&tcpCustom->netIdxList2, netIdx_cmpId);

    if (idxListSize1)
    {
        NetIdxFromCompressed(&tcpCustom->netIdxList1, (UCHAR *)p, idxListSize1);
        p += idxListSize1;
    }
    if (idxListSize2)
    {
        NetIdxFromCompressed(&tcpCustom->netIdxList2, (UCHAR *)p, idxListSize2);
        p += idxListSize2;
    }
    if (ptrArray_size(&tcpCustom->netIdxList1))
    {
        struct idx_net *pIdxn1, *pIdxn2;
        int i;

        for (i=ptrArray_size(&tcpCustom->netIdxList2)-1; i>=0; i--)
        {
            pIdxn2 = (struct idx_net *)ptrArray_nth(&tcpCustom->netIdxList2, i);
            pIdxn1 = (struct idx_net *)ptrArray_findSorted(&tcpCustom->netIdxList1, pIdxn2);
            if (!pIdxn1) ptrArray_insertSorted(&tcpCustom->netIdxList1, pIdxn2);
            else { *pIdxn1 = *pIdxn2; free(pIdxn2); }
            ptrArray_erase(&tcpCustom->netIdxList2, i, i+1);
        }
    }

    tcp_postEvent(tcpCustom);
}


void svr_onNetIdxDeleted(const CHAR *id)
{
    task_removeLocalIdx(id, FALSE);
    admin_sendMsg(STR_NET_IDX_DELETED, id);
}

void svr_onNetIdxChanged(const CHAR *id)
{
    struct task *task;
    CHAR buf[1600], szDir[384];

    task = task_isUploading(id);
    if (task)
    {
        taskUpload_end(task);
        ptrList_remove_data(&g_tasksUploading, task);
        admin_sendMsg(STR_UPLOADING_DELETED, id);

        task->action |= TS_PREPARING;
        ptrList_append(&g_tasksDownloading, task);
        SaveDownloadingTaskList();
        sprintf_s(buf, 1600,
            "%s\t%s\t%s\t%d\t%d\r\n",
            task->idx.id, task->idx.hash,
            UnicodeToUtf8(task->dir, szDir, 384),
            task->action, task->errorCode);
        admin_sendMsg(STR_DOWNLOADING_ADDED, buf);
        return;
    }
    task = task_isSeeding(id);
    if (task)
    {
        task_closeAllPeers(task);

        ptrArray_removeSorted(&g_tasksSeedingSI, task);
        ptrArray_removeSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        admin_sendMsg(STR_SEEDING_DELETED, id);

        task->action |= TS_PREPARING;
        ptrList_append(&g_tasksDownloading, task);
        SaveDownloadingTaskList();
        sprintf_s(buf, 1600,
            "%s\t%s\t%s\t%d\t%d\r\n",
            task->idx.id, task->idx.hash,
            UnicodeToUtf8(task->dir, szDir, 384),
            task->action, task->errorCode);
        admin_sendMsg(STR_DOWNLOADING_ADDED, buf);
        return;
    }
    task = task_isDownloading(id);
    if (task)
    {
        task->action |= TS_PREPARING;
        task_saveStatus(task);
        task_closeAllPeers(task);
        sprintf_s(buf, 512, "%s\tnet_idx\t%d", id, task->action);
        admin_sendMsg(STR_DOWNLOADING_CHANGED, buf);
        return;
    }

    if (g_options.isDedicatedServer ||
        (task_isAutoUpdate(id) && !task_isWaiting(id)))
    {
        ptrList_append(&g_tasksWaiting, _strdup(id));
        SaveWaitingTaskList();
        admin_sendMsg(STR_WAITING_ADDED, id);
    }
}


void svr_onIdxList(struct ptrArray *netIdxList1, struct ptrArray *netIdxList2)
{
    struct ptrArray *netIdxList;
    struct idx_net idxn, *pIdxn, *pIdxn2;
    struct idx_local *idxl;
    CHAR szTmp[2048];
    int i;

    if (!g_idxListInitialized)
    {
        g_idxListInitialized = TRUE;

        for (i=ptrArray_size(netIdxList1)-1; i>=0; i--)
        {
            pIdxn = (struct idx_net *)ptrArray_nth(netIdxList1, i);
            ptrArray_insertSorted(&g_netIdxSortedById, pIdxn);
            ptrArray_insertSorted(&g_netIdxSortedByHash, pIdxn);
            //debugf("net: %s %s\r\n", pIdxn->id, pIdxn->hash);
        }
        for (i=ptrArray_size(netIdxList2)-1; i>=0; i--)
        {
            pIdxn = (struct idx_net *)ptrArray_nth(netIdxList2, i);
            ptrArray_insertSorted(&g_netIdxSortedById, pIdxn);
            ptrArray_insertSorted(&g_netIdxSortedByHash, pIdxn);
            //debugf("net: %s %s\r\n", pIdxn->id, pIdxn->hash);
        }
        ptrArray_free(netIdxList1, NULL);
        ptrArray_free(netIdxList2, NULL);

        ReadLocalIdxList();
        ReadAutoUpdateTaskList();
        ReadDownloadingTaskList();
        ReadSeedingTaskList();
        ReadWaitingTaskList();

        for (i=0; i<ptrArray_size(&g_localIdxSortedById); i++)
        {
            idxl = (struct idx_local *)ptrArray_nth(&g_localIdxSortedById, i);

            strcpy_s(idxn.id, MAX_ID_LEN, idxl->id);
            pIdxn = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedById, &idxn);
            if (!pIdxn) continue;

            if (strcmp(idxl->hash, pIdxn->hash) &&
                task_isAutoUpdate(idxl->id) &&
                !task_isWaiting(idxl->id) &&
                !task_isDownloading(idxl->id))
            {
                debugf("local task hash: %s, net: %s\r\n", idxl->hash, pIdxn->hash);
                ptrList_append(&g_tasksWaiting, _strdup(idxl->id));
            }
        }

        admin_sendMsg(STR_GENERAL_INFO, "server connected");
        admin_sendAllLists();

        lsd_startup();

        return;
    }

    if (ptrArray_size(netIdxList1))
    {
        // some deleted
        for (i=ptrArray_size(&g_netIdxSortedById)-1; i>=0; i--)
        {
            pIdxn = (struct idx_net *)ptrArray_nth(&g_netIdxSortedById, i);
            if (!ptrArray_findSorted(netIdxList1, pIdxn))
            {
                debugf("net_idx deleted: %s\r\n", pIdxn->id);

                svr_onNetIdxDeleted(pIdxn->id);

                ptrArray_erase(&g_netIdxSortedById, i, i+1);
                ptrArray_removeSorted(&g_netIdxSortedByHash, pIdxn);
                free(pIdxn);
            }
        }
    }

    if (ptrArray_size(netIdxList1))
        netIdxList = netIdxList1;
    else netIdxList = netIdxList2;

    // some added
    for (i=ptrArray_size(netIdxList)-1; i>=0; i--)
    {
        pIdxn = (struct idx_net *)ptrArray_nth(netIdxList, i);
        if (!ptrArray_findSorted(&g_netIdxSortedById, pIdxn))
        {
            ptrArray_erase(netIdxList, i, i+1);
            ptrArray_insertSorted(&g_netIdxSortedById, pIdxn);
            ptrArray_insertSorted(&g_netIdxSortedByHash, pIdxn);

            IdxNetToUtf8String(pIdxn, szTmp, 2048);
            admin_sendMsg(STR_NET_IDX_ADDED, szTmp);

            if (g_options.isDedicatedServer)
            {
                ptrList_append(&g_tasksWaiting, _strdup(pIdxn->id));
                SaveWaitingTaskList();
                admin_sendMsg(STR_WAITING_ADDED, pIdxn->id);
            }
        }
    }

    // some changed
    for (i=0; i<ptrArray_size(netIdxList); i++)
    {
        pIdxn = (struct idx_net *)ptrArray_nth(netIdxList, i);
        pIdxn2 = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedById, pIdxn);

        if (strcmp(pIdxn->hash, pIdxn2->hash))
        {
            ptrArray_removeSorted(&g_netIdxSortedByHash, pIdxn2);
            *pIdxn2 = *pIdxn;
            ptrArray_insertSorted(&g_netIdxSortedByHash, pIdxn2);

            IdxNetToUtf8String(pIdxn, szTmp, 2048);
            admin_sendMsg(STR_NET_IDX_ADDED, szTmp);

            svr_onNetIdxChanged(pIdxn->id);
        }
    }

    ptrArray_free(netIdxList, free);
}


static BOOL svr_Send(int iSvr)
{
    CHAR *data;
    int dataLen, sent, totalSent = 0;

    data = (CHAR *)g_servers[iSvr].sendBuf.buff;
    dataLen = g_servers[iSvr].sendBuf.len;

    if (!dataLen) return TRUE;

    while (totalSent < dataLen)
    {
        sent = send(g_servers[iSvr].sock, data+totalSent, dataLen-totalSent, 0);
        if (sent == SOCKET_ERROR)
        {
            buffer_clear(&g_servers[iSvr].sendBuf);
            return FALSE;
        }
        totalSent += sent;
    }

    buffer_clear(&g_servers[iSvr].sendBuf);
    return TRUE;
}

static void svr_Connect(int iSvr)
{
    time_t currTime;
    struct sockaddr_in addr;

    time(&currTime);
    if (currTime < g_servers[iSvr].nextConnectTime) return;

    if (g_servers[iSvr].sock != INVALID_SOCKET) return;
    g_servers[iSvr].sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_servers[iSvr].sock == INVALID_SOCKET) return;

    WSAEventSelect(g_servers[iSvr].sock, g_servers[iSvr].hEventSock, FD_CLOSE|FD_CONNECT|FD_READ);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(g_servers[iSvr].ip);
    addr.sin_port = htons(g_servers[iSvr].port);
    connect(g_servers[iSvr].sock, (struct sockaddr *)&addr, sizeof(addr));
}

static void svr_Receive(int iSvr)
{
    CHAR buf[8192], *msgData;
    int received;
    int msgLen = 0;

    while (1)
    {
        received = recv(g_servers[iSvr].sock, buf, 8192, 0);
        if (received > 0)
            buffer_append(&g_servers[iSvr].recvBuf, buf, received);
        if (!msgLen && g_servers[iSvr].recvBuf.len > 4)
        {
            msgData = (CHAR *)g_servers[iSvr].recvBuf.buff;
            msgLen = 4 + ntohl(*((UINT32 *)msgData));
        }
        if (msgLen && g_servers[iSvr].recvBuf.len >= msgLen) break;
        if (received == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) break;
        if (!received) break;
    }

    while (1)
    {
        if (g_servers[iSvr].recvBuf.len < 4) break;

        msgData = (CHAR *)g_servers[iSvr].recvBuf.buff;
        msgLen = 4 + ntohl(*((UINT32 *)msgData));
        if (msgLen > g_servers[iSvr].recvBuf.len)
        {
            debugf("_Receive: error not enough data %d %d\r\n", msgLen, g_servers[iSvr].recvBuf.len);
            break;
        }
        if (msgData[4] != CMD_ADMIN)
        {
            debugf("_Receive: error cmd %d\r\n", msgData[4]);
            buffer_clear(&g_servers[iSvr].recvBuf);
            break;
        }
        switch (msgData[5])
        {
        case GM_GET_IDX_LIST_RESP:
            svr_onIdxListResp(msgData, msgLen);
            break;
        case GM_GET_PEERS_RESP:
            svr_postTcpEventPeers(msgData, msgLen);
            break;
        }

        if (msgLen < g_servers[iSvr].recvBuf.len)
            buffer_erase(&g_servers[iSvr].recvBuf, 0, msgLen);
        else
            buffer_clear(&g_servers[iSvr].recvBuf);
    }
}

static void svr_CheckConnect(int iSvr)
{
    time_t currTime;

    if (g_servers[iSvr].sock != INVALID_SOCKET)
    {
        EnterCriticalSection(&g_servers[iSvr].cs);
        svr_Send(iSvr);
        LeaveCriticalSection(&g_servers[iSvr].cs);

        time(&currTime);
        if (currTime - g_servers[iSvr].lastReceiveTime > 60)
        {
            closesocket(g_servers[iSvr].sock);
            g_servers[iSvr].sock = INVALID_SOCKET;
        }
    }
    else if (g_servers[iSvr].sendBuf.len)
        svr_Connect(iSvr);
}

static void svr_ProcessSocketEvent(int iSvr)
{
    WSANETWORKEVENTS ne;

    if (SOCKET_ERROR == WSAEnumNetworkEvents(
        g_servers[iSvr].sock, g_servers[iSvr].hEventSock, &ne))
    {
        closesocket(g_servers[iSvr].sock);
        g_servers[iSvr].sock = INVALID_SOCKET;
        WSAResetEvent(g_servers[iSvr].hEventSock);
        return;
    }

    if (ne.lNetworkEvents & FD_READ)
    {
        if (ne.iErrorCode[FD_READ_BIT])
        {
            closesocket(g_servers[iSvr].sock);
            g_servers[iSvr].sock = INVALID_SOCKET;
            time(&g_servers[iSvr].nextConnectTime);
            g_servers[iSvr].nextConnectTime += 5;
            return;
        }

        time(&g_servers[iSvr].lastReceiveTime);
        svr_Receive(iSvr);
    }
    else if (ne.lNetworkEvents & FD_CONNECT)
    {
        if (ne.iErrorCode[FD_CONNECT_BIT])
        {
            closesocket(g_servers[iSvr].sock);
            g_servers[iSvr].sock = INVALID_SOCKET;
            time(&g_servers[iSvr].nextConnectTime);
            g_servers[iSvr].nextConnectTime += 5;
            debugf("connect server failed: %s:%d code: %d\r\n",
                g_servers[iSvr].ip, g_servers[iSvr].port,
                ne.iErrorCode[FD_CONNECT_BIT]);
            return;
        }

        time(&g_servers[iSvr].lastReceiveTime);
        svr_Send(iSvr);
    }
    else if (ne.lNetworkEvents & FD_CLOSE)
    {
        closesocket(g_servers[iSvr].sock);
        g_servers[iSvr].sock = INVALID_SOCKET;
        time(&g_servers[iSvr].nextConnectTime);
        g_servers[iSvr].nextConnectTime += 5;
    }
}

static unsigned __stdcall ServerSockThreadProc(LPVOID param)
{
    WSAEVENT eh[64];
    DWORD dwWait, i;

    WSAResetEvent(g_svrThreadStoppedEvent);

    for (i=0; i<MAX_SERVERS; i++)
    {
        eh[i*2] = g_servers[i].hEventSock;
        eh[i*2+1] = g_servers[i].hEventSend;
    }
    eh[MAX_SERVERS*2] = g_svrThreadStopEvent;

    while (1)
    {
        dwWait = WSAWaitForMultipleEvents(MAX_SERVERS*2+1, eh, FALSE, 500, FALSE);

        if (dwWait == WSA_WAIT_TIMEOUT)
        {
            for (i=0; i<MAX_SERVERS; i++)
            {
                if (!g_servers[i].ip[0]) break;
                svr_CheckConnect(i);
            }
        }
        else if (dwWait >= WSA_WAIT_EVENT_0 && dwWait < WSA_WAIT_EVENT_0+MAX_SERVERS*2)
        {
            dwWait -= WSA_WAIT_EVENT_0;
            i = dwWait / 2;
            if ((dwWait % 2) == 0)
                svr_ProcessSocketEvent(i);
            else
            {
                EnterCriticalSection(&g_servers[i].cs);
                if (g_servers[i].sock != INVALID_SOCKET) svr_Send(i);
                WSAResetEvent(g_servers[i].hEventSend);
                LeaveCriticalSection(&g_servers[i].cs);
            }
        }
        else break;
    }

    WSASetEvent(g_svrThreadStoppedEvent);

    return 0;
}

BOOL svr_startup()
{
    HANDLE hThread;
    int i;

    if (g_svrThreadStopEvent) return TRUE;

    for (i=0; i<MAX_SERVERS; i++)
    {
        g_servers[i].sock = INVALID_SOCKET;
        g_servers[i].hEventSock = WSACreateEvent();
        g_servers[i].hEventSend = WSACreateEvent();
        g_servers[i].nextConnectTime = 0;
        buffer_init(&g_servers[i].recvBuf);
        buffer_init(&g_servers[i].sendBuf);
        InitializeCriticalSection(&g_servers[i].cs);
    }

    g_svrThreadStopEvent = WSACreateEvent();
    g_svrThreadStoppedEvent = WSACreateEvent();
    hThread = (HANDLE)_beginthreadex(NULL, 0, ServerSockThreadProc, NULL, 0, NULL);
    CloseHandle(hThread);

    return TRUE;
}

void svr_cleanup()
{
    int i;

    if (!g_svrThreadStopEvent) return;

    WSASetEvent(g_svrThreadStopEvent);
    WaitForSingleObject(g_svrThreadStoppedEvent, WSA_INFINITE);

    WSACloseEvent(g_svrThreadStopEvent);
    WSACloseEvent(g_svrThreadStoppedEvent);
    g_svrThreadStopEvent = NULL;
    g_svrThreadStoppedEvent = NULL;

    for (i=0; i<MAX_SERVERS; i++)
    {
        if (g_servers[i].sock != INVALID_SOCKET)
            closesocket(g_servers[i].sock);

        g_servers[i].sock = INVALID_SOCKET;
        WSACloseEvent(g_servers[i].hEventSock);
        WSACloseEvent(g_servers[i].hEventSend);
        g_servers[i].nextConnectTime = 0;
        g_servers[i].lastReceiveTime = 0;
        buffer_free(&g_servers[i].recvBuf);
        buffer_free(&g_servers[i].sendBuf);
        DeleteCriticalSection(&g_servers[i].cs);
    }
}

void svr_restart()
{
    CHAR sz[512], *pAddr, *pPort, *pNext;
    struct addrinfo hints, *result = NULL, *ptr;
    struct in_addr addr;
    int ret, iSvr = 0;

    svr_cleanup();
    g_svrInitOk = TRUE;

    strcpy_s(sz, 512, g_options.svrAddr);
    strcat_s(sz, 512, ";");
    pAddr = sz;
    while (1)
    {
        pNext = strstr(pAddr, ";");
        if (!pNext) break; *pNext = 0; pNext ++;
        pPort = strstr(pAddr, ":");
        if (!pPort) break; *pPort = 0; pPort ++;

        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_family = AF_INET;//AF_UNSPEC ;//
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol =  IPPROTO_TCP;
        ret = getaddrinfo(pAddr, 0, &hints, &result);
        if (ret != 0)
        {
            debugf("cannot analyse server address: %s\r\n", pAddr);
            g_svrInitOk = FALSE;
            break;
        }
        for(ptr=result; ptr!=NULL; ptr=ptr->ai_next)
        {
            if(ptr->ai_family != AF_INET) continue;

            addr = (((struct sockaddr_in *)(ptr->ai_addr))->sin_addr);
            if (addr.s_addr)
            {
                strcpy_s(g_servers[iSvr].ip, MAX_IP_LEN, inet_ntoa(addr));
                break;
            }
        }
        if (g_servers[iSvr].ip[0])
        {
            g_servers[iSvr].port = (u_short)atoi(pPort);
            iSvr ++;
        }
    }
    for ( ; iSvr<MAX_SERVERS; iSvr++)
    {
        strcpy_s(g_servers[iSvr].ip, MAX_IP_LEN, "");
        g_servers[iSvr].port = 0;
    }

    svr_startup();
}


// -------------------------------------------------------------------------------
// GetIdx,AddIdx,SetIdx,DelIdx

static SOCKET Socket_CreateAndConnect(int iSvr)
{
    SOCKET sock;
    struct sockaddr_in addr;

    if (iSvr < 0 || iSvr >= MAX_SERVERS || !g_servers[iSvr].ip[0] || !g_servers[iSvr].ip)
        return INVALID_SOCKET;

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return INVALID_SOCKET;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(g_servers[iSvr].ip);
    addr.sin_port = htons(g_servers[iSvr].port);
    if (SOCKET_ERROR == connect(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

static BOOL Socket_Send(SOCKET sock, const CHAR *data, int dataLen)
{
    int sent, totalSent = 0;

    while (totalSent < dataLen)
    {
        sent = send(sock, data+totalSent, dataLen-totalSent, 0);
        if (sent == SOCKET_ERROR) return FALSE;
        totalSent += sent;
    }
    return TRUE;
}

static BOOL Socket_Receive(SOCKET sock, CHAR **recvBuf, int *recvLen)
{
    int bufLen, cmdLen, recvLen1, recvTotal = 0;
    CHAR *buf, *bufNew;

    bufLen = 8192;
    buf = (CHAR *)malloc(bufLen);
    if (!buf) return FALSE;

    recvTotal = 0;
    while (1)
    {
        recvLen1 = recv(sock, buf+recvTotal, bufLen-recvTotal, 0);
        if (recvLen1 == SOCKET_ERROR) return FALSE;
        recvTotal += recvLen1;
        if (recvTotal < 5) return FALSE;
        cmdLen = 4 + ntohl(*((UINT32 *)buf));
        if (*(buf+4) != CMD_ADMIN) return FALSE;
        if (bufLen < cmdLen)
        {
            bufLen = ((cmdLen + 1) / 2048 + 1) * 2048;
            bufNew = realloc(buf, bufLen);
            if (!bufNew)
            {
                free(buf);
                return FALSE;
            }
            buf = bufNew;
        }
        if (recvTotal > cmdLen)
        {
            free(buf);
            return FALSE;
        }
        if (recvTotal == cmdLen) break;
    }
    *recvBuf = buf;
    *recvLen = recvTotal;

    return TRUE;
}

void Socket_Close(SOCKET sock)
{
    closesocket(sock);
}

BOOL svr_GetIdx(int iSvr, const CHAR *id)
{
    SOCKET sock;
    CHAR buf[256];
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    CHAR *data;
    int dataLen;

    sock = Socket_CreateAndConnect(iSvr);
    if (sock == INVALID_SOCKET) return FALSE;

    *((UINT32 *)buf) = htonl(6+MAX_ID_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_IDX;
    *((UINT32 *)(buf+6)) = htonl(GetTickCount());
    strcpy_s(buf+10, MAX_ID_LEN, id);

    EncryptData((UCHAR *)(buf+6), 4+MAX_ID_LEN);

    if (!Socket_Send(sock, buf, 10+MAX_ID_LEN))
    { Socket_Close(sock); return FALSE; }

    if (!Socket_Receive(sock, &data, &dataLen))
    { Socket_Close(sock); return FALSE; }

    Socket_Close(sock);

    if (strcmp(data+10, id) || *(data+10+MAX_ID_LEN))
    {
        debugf("GetIdx failed: %s %d\r\n", id, *(data+10+MAX_ID_LEN));
        free(data);
        return FALSE;
    }

    swprintf_s(fileName, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(id, wId, MAX_ID_LEN));
    SetFileContent(fileName, data+11+MAX_ID_LEN, dataLen-11-MAX_ID_LEN);

    free(data);

    return TRUE;
}

BOOL svr_SetIdx(int iSvr, const CHAR *id, const CHAR *pwd)
{
    SOCKET sock;
    CHAR buf[256];
    WCHAR wId[MAX_ID_LEN], fileName[MAX_PATH];
    CHAR *data;
    int dataLen;

    swprintf_s(fileName, MAX_PATH, L"%s\\IdxFiles\\%s"IDX_EXTNAME,
        g_workDir, MbcsToUnicode(id, wId, MAX_ID_LEN));
    data = (CHAR *)GetFileContent(fileName, &dataLen);
    if (!data)
    {
        debugf("SetIdx: cannot open idx %s\r\n", id);
        return FALSE;
    }

    sock = Socket_CreateAndConnect(iSvr);
    if (sock == INVALID_SOCKET) { free(data); return FALSE; }

    *((UINT32 *)buf) = htonl(6+MAX_ID_LEN+MAX_PWD_LEN+dataLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_SET_IDX;
    *((UINT32 *)(buf+6)) = 0;
    strcpy_s(buf+10, MAX_ID_LEN, id);
    strcpy_s(buf+10+MAX_ID_LEN, MAX_PWD_LEN, pwd);

    if (!Socket_Send(sock, buf, 10+MAX_ID_LEN+MAX_PWD_LEN) ||
        !Socket_Send(sock, data, dataLen))
    { free(data); Socket_Close(sock); return FALSE; }

    free(data);

    if (!Socket_Receive(sock, &data, &dataLen))
    { Socket_Close(sock); return FALSE; }

    Socket_Close(sock);

    if (strcmp(data+10, id) || *(data+10+MAX_ID_LEN))
    {
        free(data);
        return FALSE;
    }

    free(data);
    debugf("SetIdx OK: %s\r\n", id);
    return TRUE;
}

BOOL svr_DelIdx(int iSvr, const CHAR *id, const CHAR *pwd)
{
    SOCKET sock;
    CHAR buf[256], *data;
    int dataLen;

    sock = Socket_CreateAndConnect(iSvr);
    if (sock == INVALID_SOCKET) return FALSE;

    *((UINT32 *)buf) = htonl(6+MAX_ID_LEN+MAX_PWD_LEN);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_DEL_IDX;
    *((UINT32 *)(buf+6)) = 0;
    strcpy_s(buf+10, MAX_ID_LEN, id);
    strcpy_s(buf+10+MAX_ID_LEN, MAX_PWD_LEN, pwd);

    EncryptData((UCHAR *)(buf+6), 4+MAX_ID_LEN+MAX_HASH_LEN);

    if (!Socket_Send(sock, buf, 10+MAX_ID_LEN+MAX_PWD_LEN))
    { Socket_Close(sock); return FALSE; }

    if (!Socket_Receive(sock, &data, &dataLen))
    { Socket_Close(sock); return FALSE; }

    Socket_Close(sock);

    if (strcmp(data+10, id) || *(data+10+MAX_ID_LEN))
    {
        debugf("DelIdx failed: %s %d\r\n", id, *(data+10+MAX_ID_LEN));
        free(data);
        return FALSE;
    }

    free(data);

    debugf("DelIdx OK: %s\r\n", id);
    return TRUE;
}

void svr_sendExitRequest(int iSvr)
{
    SOCKET sock;
    int reqLen;
    CHAR buf[512];
    CHAR *data;
    int dataLen;

    sock = Socket_CreateAndConnect(iSvr);
    if (sock == INVALID_SOCKET) return;

    reqLen = sprintf_s(buf+10, 502, "exit all,%s,", g_options.myId) + 1;

    *((UINT32 *)buf) = htonl(6+reqLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = GM_GET_PEERS;
    *((UINT32 *)(buf+6)) = htonl(GetTickCount());

    EncryptData((UCHAR *)(buf+6), 4+reqLen);

    Socket_Send(sock, buf, 10+reqLen);
    if (Socket_Receive(sock, &data, &dataLen)) free(data);

    Socket_Close(sock);
}
