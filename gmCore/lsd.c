#include "gmCore.h"

//
// 存在虚拟网卡时，multicast不正常
//

// BT: 239.192.152.143:6771
// multicast: 224.0.0.0 - 239.255.255.255
static SOCKET g_lsdSock = INVALID_SOCKET;
const char *g_lsdAddr = "235.1.1.168";
static WORD g_lsdPort = 18900;

const char *SEARCH = "GM-SEARCH";
const char *RESPONSE = "GM-RESPONSE";

static CHAR *GetAnnounceString(struct task *task, const CHAR *method, CHAR *szAnnounce)
{
    sprintf_s(szAnnounce, 256,
        "%s * HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Infohash: %s\r\n"
        "Pid: %s\r\n"
        "Port: %u\r\n"
        "\r\n\r\n",
        method,
        g_lsdAddr, g_lsdPort,
        task->idx.hash,
        g_options.myId,
        g_options.portNum);
    return szAnnounce;
}

static BOOL ParseAnnounceString(CHAR *szAnnounce, struct tcp_custom_local_peer *tcpCustom)
{
    CHAR *pMethod, *pMethodEnd;
    CHAR *pHash, *pHashEnd;
    CHAR *pPid, *pPidEnd;
    CHAR *pPort, *pPortEnd;

    memset(tcpCustom, 0, sizeof(struct tcp_custom_local_peer));
    tcpCustom->ioType = TCP_CUSTOM_LOCAL_PEER;

    pMethod = szAnnounce;
    pMethodEnd = strchr(pMethod, ' '); if (!pMethodEnd) return FALSE;

    pHash = strstr(szAnnounce, "Infohash: "); if (!pHash) return FALSE;
    pHashEnd = strstr(pHash, "\r\n"); if (!pHashEnd) return FALSE;

    pPid = strstr(szAnnounce, "Pid: "); if (!pPid) return FALSE;
    pPidEnd = strstr(pPid, "\r\n"); if (!pPidEnd) return FALSE;

    pPort = strstr(szAnnounce, "Port: "); if (!pPort) return FALSE;
    pPortEnd = strstr(pPort, "\r\n"); if (!pPortEnd) return FALSE;

    *pMethodEnd = 0;
    if (strcmp(pMethod, SEARCH) && strcmp(pMethod, RESPONSE)) return FALSE;
    strcpy_s(tcpCustom->method, 16, pMethod);

    pHash += 10; *pHashEnd = 0;
    if (strlen(pHash) >= MAX_HASH_LEN) return FALSE;
    strcpy_s(tcpCustom->hash, MAX_HASH_LEN, pHash);

    pPid += 5; *pPidEnd = 0;
    if (strlen(pPid) >= MAX_PID_LEN) return FALSE;
    strcpy_s(tcpCustom->pid, MAX_PID_LEN, pPid);

    pPort += 6; *pPortEnd = 0;
    tcpCustom->port = (WORD)atoi(pPort);

    return TRUE;
}

BOOL lsd_sendAnnounce(struct task *task, const CHAR *method)
{
    struct sockaddr_in peerAddr;
    char szAnnounce[256];

    if (!method) method = SEARCH;
    GetAnnounceString(task, method, szAnnounce);

    memset(&peerAddr, 0, sizeof(peerAddr));
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(g_lsdPort);
    peerAddr.sin_addr.s_addr = inet_addr(g_lsdAddr);
    if (sendto(g_lsdSock, szAnnounce, strlen(szAnnounce)+1, 0,
        (struct sockaddr *)&peerAddr, sizeof(peerAddr)) < 0)
    {
        debugf("lsd_sendAnnounce failed %d\r\n", WSAGetLastError());
        return FALSE;
    }
    return TRUE;
}

static void lsd_PostTcpEvent(struct tcp_custom_local_peer *pTcpCustom)
{
    struct tcp_custom_local_peer *tcpCustom;

    tcpCustom = (struct tcp_custom_local_peer *)malloc(sizeof(struct tcp_custom_local_peer));
    if (tcpCustom)
    {
        *tcpCustom = *pTcpCustom;
        tcp_postEvent(tcpCustom);
    }
}

static BOOL lsd_replaceLocalPeer(struct ptrList *peers, const CHAR *pid, const CHAR *ip, WORD port)
{
    struct peer *peer;
    struct ptrList *list;

    for (list=peers; list; list=list->next)
    {
        peer = (struct peer *)list->data;
        if (0 == strcmp(peer->pid, pid))
        {
            strcpy_s(peer->ip, MAX_IP_LEN, ip);
            peer->port = port;
            return TRUE;
        }
    }
    return FALSE;
}

void lsd_onAnnounce(struct tcp_custom_local_peer *tcpCustom)
{
    struct idx_net *idxn, idxnf = { 0 };
    struct task *task;
    struct peer *peer;
    time_t currTime;

    strcpy_s(idxnf.hash, MAX_HASH_LEN, tcpCustom->hash);
    idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedByHash, &idxnf);
    if (!idxn) return;

    task = task_isSeeding(idxn->id);
    if (task)
    {
        if (0 == strcmp(tcpCustom->method, SEARCH))
            lsd_sendAnnounce(task, RESPONSE);
        return;
    }

    task = task_isDownloading(idxn->id);
    if (!task) return;

    if (0 == strcmp(tcpCustom->method, SEARCH))
        lsd_sendAnnounce(task, RESPONSE);

    if (!lsd_replaceLocalPeer(task->peersCandidate, tcpCustom->pid, tcpCustom->ip, tcpCustom->port) &&
        !lsd_replaceLocalPeer(task->peersOutgoing, tcpCustom->pid, tcpCustom->ip, tcpCustom->port))
    {
        peer = peer_new();
        peer->task = task;
        peer->sock = -1;
        peer->isOutgoing = 1;
        strcpy_s(peer->pid, MAX_PID_LEN, tcpCustom->pid);
        strcpy_s(peer->ip, MAX_IP_LEN, tcpCustom->ip);
        peer->port = tcpCustom->port;

        ptrList_append(&task->peersCandidate, peer);

        time(&currTime);
        task_checkCandidatePeers(task, currTime);
    }
}

static unsigned __stdcall lsd_ThreadProc(LPVOID param)
{
    struct sockaddr_in peerAddr, *addr;
    char szAnnounce[256];
    int socklen, n;
    DWORD ttl;
    BOOL reuseAddr, recvBuf, broadCast;
    struct ip_mreq mreq;
    IP_ADAPTER_ADDRESSES *adapterAddresses, *aa;
    IP_ADAPTER_PREFIX *pPrefix;
    DWORD dwBuffLen;
    struct tcp_custom_local_peer tcpCustom;

    g_lsdSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_lsdSock == INVALID_SOCKET) return 0;

    reuseAddr = TRUE;
    if (setsockopt(g_lsdSock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR)
    {
        debugf("lsd setsockopt(REUSEADDR) FAILED\r\n");
        return 0;
    }
    recvBuf = 4096;
    if (setsockopt(g_lsdSock, SOL_SOCKET, SO_RCVBUF, (char *)&recvBuf, sizeof(recvBuf)) == SOCKET_ERROR)
    {
        debugf("lsd setsockopt(RCVBUF) FAILED\r\n");
        return 0;
    }
    broadCast = TRUE;
    if (setsockopt(g_lsdSock, SOL_SOCKET, SO_BROADCAST, (char *)&broadCast, sizeof(BOOL)) == SOCKET_ERROR)
    {
        debugf("lsd setsockopt(BROADCAST) FAILED\r\n");
        return 0;
    }

    socklen = sizeof(struct sockaddr_in);
    memset(&peerAddr, 0, socklen);
    peerAddr.sin_family = AF_INET;
    peerAddr.sin_port = htons(g_lsdPort);
    peerAddr.sin_addr.s_addr = htonl(INADDR_ANY);  // inet_addr(g_lsdAddr)
    if (bind(g_lsdSock, (struct sockaddr *)&peerAddr, sizeof(peerAddr)) == SOCKET_ERROR)
    {
        debugf("lsd Bind ERROR\r\n");
        return 0;
    }

    dwBuffLen = 0;
    if (ERROR_BUFFER_OVERFLOW != GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, NULL, &dwBuffLen)) return 0;
    adapterAddresses = (IP_ADAPTER_ADDRESSES *)malloc(dwBuffLen);
    if (ERROR_SUCCESS != GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, adapterAddresses, &dwBuffLen))
    { free(adapterAddresses); return 0; }

    for (aa = adapterAddresses; aa != NULL; aa = aa->Next)
    {
        UCHAR mac0[6], mac[16];
        int ipCnt;

        pPrefix = aa->FirstPrefix;
        memcpy(mac, aa->PhysicalAddress, 6);
        memset(mac0, 0, 6);
        if (0==memcmp(mac, mac0, 6) || !pPrefix) continue;

        for (ipCnt=0; pPrefix; ipCnt++)
            pPrefix = pPrefix->Next;
        if (ipCnt < 3) continue;

        pPrefix = aa->FirstPrefix->Next;
        addr = (struct sockaddr_in *)pPrefix->Address.lpSockaddr;
        debugf("lsd bind: %02X-%02X-%02X-%02X-%02X-%02X %s\r\n", 
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], inet_ntoa(addr->sin_addr));

        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_multiaddr.s_addr = inet_addr(g_lsdAddr);
        mreq.imr_interface.s_addr = addr->sin_addr.s_addr;
        if (setsockopt(g_lsdSock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) == SOCKET_ERROR)
        {
            debugf("lsd setsockopt(ADD_MEMBERSHIP) FAILED\r\n");
            continue;
        }
    }
    free(adapterAddresses);

    ttl = 8;
    if (setsockopt(g_lsdSock, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(DWORD)) == SOCKET_ERROR)
    {
        debugf("lsd setsockopt(TTL) FAILED\r\n");
        return 0;
    }

    while (1)
    {
        memset(szAnnounce, 0, sizeof(szAnnounce));
        socklen = sizeof(struct sockaddr_in);
        n = recvfrom(g_lsdSock, szAnnounce, sizeof(szAnnounce), 0,(struct sockaddr *)&peerAddr, &socklen);
        if (n == SOCKET_ERROR) break;
        if (n > 20 && n < 256 &&
            ParseAnnounceString(szAnnounce, &tcpCustom) &&
            strcmp(tcpCustom.pid, g_options.myId))
        {
            strcpy_s(tcpCustom.ip, MAX_IP_LEN, inet_ntoa(peerAddr.sin_addr));
            lsd_PostTcpEvent(&tcpCustom);
        }
    }

    setsockopt(g_lsdSock, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq, sizeof(mreq));
    closesocket(g_lsdSock);
    g_lsdSock = INVALID_SOCKET;

    return 0;
}

BOOL lsd_startup()
{
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, lsd_ThreadProc, NULL, 0, NULL));
    return TRUE;
}

void lsd_cleanup()
{
    if (g_lsdSock != INVALID_SOCKET)
    {
        closesocket(g_lsdSock);
        g_lsdSock = INVALID_SOCKET;
    }
}
