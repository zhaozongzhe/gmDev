#include "tcp.h"

#pragma comment(lib, "ws2_32.lib")

struct tcp_buf
{
    int dlen;
    int alen;
    UCHAR *data;
};

static __inline void buf_free(struct tcp_buf *bf)
{
    if (bf->data != NULL)
        free(bf->data);
    bf->dlen = 0;
    bf->alen = 0;
    bf->data = NULL;
}

static __inline void buf_swap(struct tcp_buf *bf1, struct tcp_buf *bf2)
{
    struct tcp_buf bf;

    bf = *bf1;
    *bf1 = *bf2;
    *bf2 = bf;
}

static BOOL buf_alloc(struct tcp_buf *bf, int alloc)
{
    if (bf->alen < alloc + 1)
    {
        const int BLOCK_SIZE = 1024;
        int newAlloc;
        UCHAR *newBuff;

        if (alloc < BLOCK_SIZE)
            newAlloc = BLOCK_SIZE;
        else
            newAlloc = (alloc / BLOCK_SIZE + 1) * BLOCK_SIZE;

        newBuff = (UCHAR *)malloc(newAlloc);
        if (newBuff == NULL) return FALSE;
        if (bf->data)
        {
            if (bf->dlen)
                memcpy(newBuff, bf->data, bf->dlen);
            free(bf->data);
        }
        bf->data = newBuff;
        bf->alen = newAlloc;
    }
    return TRUE;
}

static BOOL buf_append(struct tcp_buf *bf, const void *data, int dlen)
{
    if (!buf_alloc(bf, bf->dlen + dlen + 1))
        return FALSE;

    memcpy(bf->data + bf->dlen, data, dlen);
    bf->dlen += dlen;
    return TRUE;
}

static void buf_erase(struct tcp_buf *bf, int erase_pos, int erase_len)
{
    if (erase_pos >= 0 &&
        erase_pos < bf->dlen &&
        (erase_pos + erase_len) <= bf->dlen)
    {
        if ((erase_pos + erase_len) < bf->dlen)
            memmove(bf->data + erase_pos,
                bf->data + erase_pos + erase_len,
                bf->dlen - erase_pos - erase_len);
        bf->dlen -= erase_len;
    }
}

// -----------------------------------------------------------------------
// socket status
#define STAT_CONNECTING             0x01
#define STAT_PAUSED                 0x02
#define STAT_CLOSING                0x04
#define STAT_RECEIVING              0x10
#define STAT_SENDING                0x20

struct tcp_socket
{
    UINT64 id;              // opt future
    SOCKET sock;
    LONG type;
    LONG stat;
    struct sockaddr_in addr;
    TCPSOCKETEVENTPROC *lpfnEventProc;
    PVOID lpUserData;
    LONG expectPackLen;

    OVERLAPPED ovlpSend;
    OVERLAPPED ovlpReceive;
    OVERLAPPED ovlpConnect;
    OVERLAPPED ovlpClose;

    struct tcp_buf bufSending;
    struct tcp_buf bufToSend;
    struct tcp_buf bufRecv;
    int sockIdxListen;
    int sockIdxAccept;
    time_t lastReceiveTime;
    time_t lastSendTime;
    time_t acceptTime;
    time_t connectTime;
};

static HANDLE g_completionPort = NULL;
static OVERLAPPED g_ovlpExit = { 0 };

static struct tcp_socket **g_sockets = NULL;
static int g_maxSockets = 0;
static TCPSOCKETEVENTPROC *g_lpfnEventProc = NULL;
static HANDLE g_thread = NULL;

static LPFN_CONNECTEX g_lpfnConnectEx = NULL;
static LPFN_ACCEPTEX g_lpfnAcceptEx = NULL;
static LPFN_DISCONNECTEX g_lpfnDisconnectEx = NULL;


void tcp_setUserData(int sockIdx, void *lpUserData)
{
    if (sockIdx >= 0 && sockIdx < g_maxSockets && g_sockets[sockIdx])
        g_sockets[sockIdx]->lpUserData = lpUserData;
}

void *tcp_getUserData(int sockIdx)
{
    return ((sockIdx >= 0 && sockIdx < g_maxSockets && g_sockets[sockIdx])
        ? g_sockets[sockIdx]->lpUserData : NULL);
}

void tcp_setExpectPackLen(int sockIdx, int packLen)
{
    if (sockIdx >= 0 && sockIdx < g_maxSockets && g_sockets[sockIdx])
        g_sockets[sockIdx]->expectPackLen = packLen;
}

BOOL tcp_getSendStat(int sockIdx, int *sendingSize, int *bufferedSize)
{
    *sendingSize = 0;
    *bufferedSize = 0;

    if (sockIdx >= 0 && sockIdx < g_maxSockets && g_sockets[sockIdx])
    {
        if (g_sockets[sockIdx]->stat & STAT_SENDING)
            *sendingSize = g_sockets[sockIdx]->bufSending.dlen;
        *bufferedSize = g_sockets[sockIdx]->bufToSend.dlen;
        return TRUE;
    }

    return FALSE;
}

// called from outside threads
void tcp_postEvent(void *key)
{
    PostQueuedCompletionStatus(g_completionPort, 0, (ULONG_PTR)(-11), (LPOVERLAPPED)key);
}

BOOL tcp_getInfo(int sockIdx, struct tcp_info *tsi)
{
    struct sockaddr_in addr;
    int addrLen = sizeof(tsi->hostAddr);

    memset(tsi, 0, sizeof(*tsi));
    tsi->sock = INVALID_SOCKET;
    tsi->sockIdxListen = -1;

    if (sockIdx < 0 || sockIdx >= g_maxSockets || !g_sockets[sockIdx])
        return FALSE;

    tsi->sock = g_sockets[sockIdx]->sock;
    tsi->type = g_sockets[sockIdx]->type;
    tsi->connecting = g_sockets[sockIdx]->stat & STAT_CONNECTING;
    tsi->sending = g_sockets[sockIdx]->stat & STAT_SENDING;
    tsi->paused = g_sockets[sockIdx]->stat & STAT_PAUSED;
    tsi->closing = g_sockets[sockIdx]->stat & STAT_CLOSING;
    getpeername(g_sockets[sockIdx]->sock, (struct sockaddr *)&addr, &addrLen);
    strcpy_s(tsi->peerAddr.ip, 20, inet_ntoa(addr.sin_addr));
    tsi->peerAddr.port = ntohs(addr.sin_port);
    getsockname(g_sockets[sockIdx]->sock, (struct sockaddr *)&addr, &addrLen);
    strcpy_s(tsi->hostAddr.ip, 20, inet_ntoa(addr.sin_addr));
    tsi->hostAddr.port = ntohs(addr.sin_port);
    tsi->lpfnEventProc = g_sockets[sockIdx]->lpfnEventProc;
    tsi->lpUserData = g_sockets[sockIdx]->lpUserData;
    tsi->sockIdxListen = g_sockets[sockIdx]->sockIdxListen;
    tsi->lastReceiveTime = g_sockets[sockIdx]->lastReceiveTime;
    tsi->lastSendTime = g_sockets[sockIdx]->lastSendTime;
    tsi->acceptTime = g_sockets[sockIdx]->acceptTime;
    tsi->connectTime = g_sockets[sockIdx]->connectTime;

    return TRUE;
}

static void tcp_clean(int sockIdx)
{
    g_sockets[sockIdx]->type = 0;
    g_sockets[sockIdx]->stat = 0;
    memset(&g_sockets[sockIdx]->addr, 0, sizeof(g_sockets[sockIdx]->addr));
    g_sockets[sockIdx]->lpUserData = NULL;

    memset(&g_sockets[sockIdx]->ovlpSend, 0, sizeof(OVERLAPPED));
    memset(&g_sockets[sockIdx]->ovlpReceive, 0, sizeof(OVERLAPPED));
    memset(&g_sockets[sockIdx]->ovlpConnect, 0, sizeof(OVERLAPPED));
    memset(&g_sockets[sockIdx]->ovlpClose, 0, sizeof(OVERLAPPED));
    g_sockets[sockIdx]->bufSending.dlen = 0;
    g_sockets[sockIdx]->bufToSend.dlen = 0;
    g_sockets[sockIdx]->bufRecv.dlen = 0;
    g_sockets[sockIdx]->sockIdxListen = -1;
    g_sockets[sockIdx]->sockIdxAccept = -1;
    g_sockets[sockIdx]->lastReceiveTime = 0;
    g_sockets[sockIdx]->lastSendTime = 0;
    g_sockets[sockIdx]->acceptTime = 0;
    g_sockets[sockIdx]->connectTime = 0;
}

static int tcp_new(int type)
{
    int i, sockIdx;

    for (i=0, sockIdx=-1; i<g_maxSockets; i++)
    {
        if (!g_sockets[i] || !g_sockets[i]->type)
        {
            sockIdx = i;
            break;
        }
    }

    if (sockIdx < 0) return -1;

    if (!g_sockets[sockIdx])
    {
        g_sockets[sockIdx] = (struct tcp_socket *)malloc(sizeof(struct tcp_socket));
        if (!g_sockets[sockIdx]) return -1;
        memset(g_sockets[sockIdx], 0, sizeof(struct tcp_socket));
        g_sockets[sockIdx]->sock = INVALID_SOCKET;
    }

    tcp_clean(sockIdx);

    g_sockets[sockIdx]->type = type;
    g_sockets[sockIdx]->lpUserData = NULL;

    return sockIdx;
}

static void tcp_release(int sockIdx, BOOL notify)
{
    if (sockIdx < 0 || sockIdx >= g_maxSockets ||
        !g_sockets[sockIdx] || !g_sockets[sockIdx]->type)
        return;

    if (g_sockets[sockIdx]->sock != INVALID_SOCKET)
    {
        CancelIo((HANDLE)g_sockets[sockIdx]->sock);
        closesocket(g_sockets[sockIdx]->sock);
        g_sockets[sockIdx]->sock = INVALID_SOCKET;
    }

    if (notify && g_sockets[sockIdx]->type &&
        g_sockets[sockIdx]->type != TCP_TYPE_ACCEPT &&
        g_sockets[sockIdx]->lpfnEventProc)
        g_sockets[sockIdx]->lpfnEventProc(sockIdx, TCP_EV_CLOSE, NULL, 0);

    tcp_clean(sockIdx);
}

void tcp_close(int sockIdx)
{
    if (sockIdx < 0 || sockIdx >= g_maxSockets ||
        !g_sockets[sockIdx] || !g_sockets[sockIdx]->type)
        return;

    g_sockets[sockIdx]->stat |= STAT_CLOSING;

    PostQueuedCompletionStatus(g_completionPort, 0, sockIdx, (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
}

BOOL tcp_send(int sockIdx,
              const void *data1, int dataLen1,
              const void *data2, int dataLen2)
{
    if (sockIdx < 0 || sockIdx >= g_maxSockets ||
        !g_sockets[sockIdx] ||
        (g_sockets[sockIdx]->type != TCP_TYPE_IN &&
        g_sockets[sockIdx]->type != TCP_TYPE_OUT) ||
        g_sockets[sockIdx]->sock == INVALID_SOCKET ||
        g_sockets[sockIdx]->stat & 0x0F)
        return FALSE;

    if (g_sockets[sockIdx]->stat & STAT_SENDING)
    {
        if (g_sockets[sockIdx]->bufToSend.dlen > 10*1024*1024) return FALSE;

        if (data1 && dataLen1)
            buf_append(&g_sockets[sockIdx]->bufToSend, data1, dataLen1);
        if (data2 && dataLen2)
            buf_append(&g_sockets[sockIdx]->bufToSend, data2, dataLen2);

        return TRUE;
    }

    if (data1 && dataLen1)
        buf_append(&g_sockets[sockIdx]->bufSending, data1, dataLen1);
    if (data2 && dataLen2)
        buf_append(&g_sockets[sockIdx]->bufSending, data2, dataLen2);

    if (!g_sockets[sockIdx]->bufSending.dlen) return TRUE;

    g_sockets[sockIdx]->stat |= STAT_SENDING;

    if (!WriteFile(
        (HANDLE)g_sockets[sockIdx]->sock,
        g_sockets[sockIdx]->bufSending.data,
        g_sockets[sockIdx]->bufSending.dlen,
        NULL,
        (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpSend))
    {
        DWORD lastErr = GetLastError();
        if (lastErr != ERROR_IO_PENDING)
        {
            PostQueuedCompletionStatus(g_completionPort, 0, sockIdx,
                (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
            return FALSE;
        }
    }

    return TRUE;
}

static void tcp_doReceive(int sockIdx)
{
    int bytesToRead;

    if (g_sockets[sockIdx]->expectPackLen > 0)
    {
        bytesToRead = max(128, g_sockets[sockIdx]->expectPackLen - g_sockets[sockIdx]->bufRecv.dlen);
        buf_alloc(&g_sockets[sockIdx]->bufRecv, g_sockets[sockIdx]->bufRecv.dlen + bytesToRead + 128);
        g_sockets[sockIdx]->expectPackLen = 0;
    }
    else
    {
        bytesToRead = 1600;
        buf_alloc(&g_sockets[sockIdx]->bufRecv, g_sockets[sockIdx]->bufRecv.dlen + bytesToRead);
    }

    g_sockets[sockIdx]->stat |= STAT_RECEIVING;

    if (!ReadFile((HANDLE)g_sockets[sockIdx]->sock,
        g_sockets[sockIdx]->bufRecv.data + g_sockets[sockIdx]->bufRecv.dlen,
        bytesToRead, NULL, (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpReceive))
    {
        DWORD lastErr = GetLastError();
        if (lastErr != ERROR_IO_PENDING)
        {
            PostQueuedCompletionStatus(g_completionPort, 0, sockIdx,
                (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
        }
    }
}

void tcp_pause(int sockIdx)
{
    if (sockIdx < 0 || sockIdx >= g_maxSockets || !g_sockets[sockIdx])
        return;

    g_sockets[sockIdx]->stat |= STAT_PAUSED;
}

void tcp_resume(int sockIdx)
{
    if (sockIdx < 0 || sockIdx >= g_maxSockets || !g_sockets[sockIdx])
        return;

    g_sockets[sockIdx]->stat &= ~STAT_PAUSED;

    if (!(g_sockets[sockIdx]->stat & STAT_RECEIVING))
        tcp_doReceive(sockIdx);
}

static int tcp_accept(int sockIdxListen)
{
    SOCKET newSocket;
    int sockIdx;

    newSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (newSocket == INVALID_SOCKET) return -1;

    sockIdx = tcp_new(TCP_TYPE_ACCEPT);
    if (sockIdx < 0)
    {
        closesocket(newSocket);
        return -1;
    }

    g_sockets[sockIdx]->sock = newSocket;
    g_sockets[sockIdx]->sockIdxListen = sockIdxListen;
    g_sockets[sockIdx]->lpfnEventProc = g_sockets[sockIdxListen]->lpfnEventProc;

    g_sockets[sockIdxListen]->sockIdxAccept = sockIdx;
    buf_alloc(&g_sockets[sockIdxListen]->bufRecv, 512);
    if (!g_lpfnAcceptEx(
        g_sockets[sockIdxListen]->sock, g_sockets[sockIdx]->sock,
        g_sockets[sockIdxListen]->bufRecv.data, 0,
        sizeof(struct sockaddr_in) + 16, sizeof(struct sockaddr_in) + 16,
        NULL, (LPOVERLAPPED)&g_sockets[sockIdxListen]->ovlpReceive))
    {
        DWORD lastErr = WSAGetLastError();
        if (lastErr != ERROR_IO_PENDING)
        {
            PostQueuedCompletionStatus(g_completionPort, 0, sockIdx,
                (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
            g_sockets[sockIdxListen]->stat |= STAT_CLOSING; // no notify
            PostQueuedCompletionStatus(g_completionPort, 0, sockIdxListen,
                (LPOVERLAPPED)&g_sockets[sockIdxListen]->ovlpClose);
            return -1;
        }
    }

    return sockIdx;
} 

int tcp_listen(const char *ip, u_short port, TCPSOCKETEVENTPROC *lpfnEventProc)
{
    SOCKET listenSocket;
    struct sockaddr_in addr;
    int sockIdx;

    if (!g_maxSockets || !g_sockets) return -1;

    if (port < 80 || port >= 65535) return -1;

    listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    if (addr.sin_addr.s_addr == INADDR_NONE)
        addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listenSocket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        return -1;
    }
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(listenSocket);
        return -1;
    }

    sockIdx = tcp_new(TCP_TYPE_LISTEN);
    if (sockIdx < 0)
    {
        closesocket(listenSocket);
        return -1;
    }

    g_sockets[sockIdx]->sock = listenSocket;
    g_sockets[sockIdx]->lpfnEventProc = (lpfnEventProc ? lpfnEventProc : g_lpfnEventProc);

    CreateIoCompletionPort((HANDLE)g_sockets[sockIdx]->sock, g_completionPort, (ULONG_PTR)sockIdx, 0);
    tcp_accept(sockIdx);

    return sockIdx;
}

int tcp_connect(const char *ip, u_short port, TCPSOCKETEVENTPROC *lpfnEventProc)
{
    SOCKET connectSocket;
    struct sockaddr_in addr;
    int sockIdx;

    if (!g_maxSockets || !g_sockets) return -1;

    connectSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (connectSocket == INVALID_SOCKET) return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ADDR_ANY;
    addr.sin_port = htons(0);
    if (bind(connectSocket, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        closesocket(connectSocket);
        return -1;
    }

    sockIdx = tcp_new(TCP_TYPE_OUT);
    if (sockIdx < 0)
    {
        closesocket(connectSocket);
        return -1;
    }

    g_sockets[sockIdx]->sock = connectSocket;
    g_sockets[sockIdx]->lpfnEventProc = (lpfnEventProc ? lpfnEventProc : g_lpfnEventProc);
    CreateIoCompletionPort((HANDLE)g_sockets[sockIdx]->sock, g_completionPort, (ULONG_PTR)sockIdx, 0);

    memset(&g_sockets[sockIdx]->addr, 0, sizeof(addr));
    g_sockets[sockIdx]->addr.sin_family = AF_INET;
    g_sockets[sockIdx]->addr.sin_addr.s_addr = inet_addr(ip);
    g_sockets[sockIdx]->addr.sin_port = htons(port);

    g_sockets[sockIdx]->stat |= STAT_CONNECTING;

    if (!g_lpfnConnectEx(g_sockets[sockIdx]->sock,
        (struct sockaddr *)&g_sockets[sockIdx]->addr, sizeof(g_sockets[sockIdx]->addr),
        NULL, 0, NULL, (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpConnect)
        && WSAGetLastError() != ERROR_IO_PENDING)
    {
        g_sockets[sockIdx]->stat |= STAT_CLOSING; // no notify
        PostQueuedCompletionStatus(g_completionPort, 0, sockIdx,
            (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
        return -1;
    }

    return sockIdx;
}

static BOOL tcp_processReceivedData(int sockIdx)
{
    INT continueProcess, processed;

    continueProcess = 1;
    do
    {
        processed = g_sockets[sockIdx]->lpfnEventProc(sockIdx, TCP_EV_RECEIVE,
            g_sockets[sockIdx]->bufRecv.data, g_sockets[sockIdx]->bufRecv.dlen);
        if (processed < 0)
        {
            g_sockets[sockIdx]->stat |= STAT_CLOSING;
            PostQueuedCompletionStatus(g_completionPort, 0, sockIdx,
                (LPOVERLAPPED)&g_sockets[sockIdx]->ovlpClose);
            continueProcess = -1;
        }
        else if (processed > 0)
        {
            buf_erase(&g_sockets[sockIdx]->bufRecv, 0, processed);
            continueProcess = (g_sockets[sockIdx]->bufRecv.dlen > 0 ? 1 : 0);
        }
        else
            continueProcess = 0;
    }
    while (continueProcess == 1);

    return continueProcess != -1;
}

static void tcp_checkIdleClients()
{
    static time_t _lastCheckTime = 0;
    time_t currTime;

    time(&currTime);
    if (currTime - _lastCheckTime < 2) return;
    _lastCheckTime = currTime;

    if (g_sockets)
    {
        int i;

        for (i=0; i<g_maxSockets; i++)
        {
            if (!g_sockets[i]) break;
            if (g_sockets[i]->type != TCP_TYPE_IN) continue;

            if (!g_sockets[i]->lastReceiveTime &&
                currTime - g_sockets[i]->acceptTime >= 20)
                tcp_release(i, FALSE);
        }
    }
}

#define IsValidSockIdx(sockIdx) (sockIdx >= 0 && sockIdx < g_maxSockets && g_sockets[sockIdx])

static unsigned __stdcall tcp_workerThread(void* pArguments)
{
    BOOL hasEntry;
    OVERLAPPED *lpo;
    DWORD dwBytes;
    ULONG_PTR key;
    int sockIdx, sockIdxAccepted, lastErr;
    DWORD currTickCount, nextTimerTickCount;

    nextTimerTickCount = GetTickCount() + 200;

    while (1)
    {
        hasEntry = GetQueuedCompletionStatus(g_completionPort, &dwBytes, &key, &lpo, 100);

        currTickCount = GetTickCount();
        if (currTickCount > nextTimerTickCount)
        {
            nextTimerTickCount = currTickCount + 100;
            if (g_lpfnEventProc)
                g_lpfnEventProc(-1, TCP_EV_TIMER, NULL, 0);
        }

        if (!lpo && !hasEntry)
        {
            lastErr = GetLastError();
            //if (lastErr && lastErr != WAIT_TIMEOUT && lastErr != ERROR_IO_PENDING) ;

            tcp_checkIdleClients();
            continue;
        }

        sockIdx = (int)key;

        if (lpo == &g_ovlpExit) break;

        if (sockIdx == -11) // custom event
        {
            if (g_lpfnEventProc)
                g_lpfnEventProc(0, TCP_EV_CUSTOM, (UCHAR *)lpo, 0);
            continue;
        }

        if (sockIdx == -111) // timer event
        {
            currTickCount = GetTickCount();
            if (currTickCount > nextTimerTickCount)
            {
                nextTimerTickCount = currTickCount + 100;
                if (g_lpfnEventProc)
                    g_lpfnEventProc(-1, TCP_EV_TIMER, NULL, 0);
            }
            continue;
        }

        if (!hasEntry) // IO operation failed, ie: socket closed
        {
            if (IsValidSockIdx(sockIdx))
            {
                if (g_sockets[sockIdx]->type == TCP_TYPE_LISTEN)
                    tcp_accept(sockIdx);
                else
                    tcp_release(sockIdx, TRUE);
            }
            continue;
        }

        if (!IsValidSockIdx(sockIdx)) continue;

        if (lpo == &g_sockets[sockIdx]->ovlpConnect)
        {
            if (g_sockets[sockIdx]->stat & STAT_CONNECTING &&
                g_sockets[sockIdx]->type == TCP_TYPE_OUT)
            {
                g_sockets[sockIdx]->stat &= ~STAT_CONNECTING;
                time(&g_sockets[sockIdx]->connectTime);

                if (g_sockets[sockIdx]->lpfnEventProc(sockIdx, TCP_EV_CONNECT, NULL, 0) < 0)
                    tcp_release(sockIdx, FALSE);
                else
                    tcp_doReceive(sockIdx);
            }
        }
        else if (lpo == &g_sockets[sockIdx]->ovlpClose)
        {
            tcp_release(sockIdx, g_sockets[sockIdx]->stat & STAT_CLOSING);
        }
        else if (lpo == &g_sockets[sockIdx]->ovlpSend)
        {
            g_sockets[sockIdx]->stat &= ~STAT_SENDING;

            time(&g_sockets[sockIdx]->lastSendTime);

            // socket关闭以后仍然可能收到此消息
            //if (dwBytes != g_sockets[sockIdx]->bufSending.dlen);

            g_sockets[sockIdx]->bufSending.dlen = 0;

            if (!(g_sockets[sockIdx]->stat & STAT_CLOSING))
            {
                if (!g_sockets[sockIdx]->bufToSend.dlen)
                    g_sockets[sockIdx]->lpfnEventProc(sockIdx, TCP_EV_SEND, NULL, 0);
                else
                {
                    buf_swap(&g_sockets[sockIdx]->bufToSend, &g_sockets[sockIdx]->bufSending);
                    tcp_send(sockIdx, NULL, 0, NULL, 0);
                }
            }
        }
        else if (lpo == &g_sockets[sockIdx]->ovlpReceive)
        {
            switch (g_sockets[sockIdx]->type)
            {
            case TCP_TYPE_LISTEN:
                sockIdxAccepted = g_sockets[sockIdx]->sockIdxAccept;
                g_sockets[sockIdxAccepted]->type = TCP_TYPE_IN;
                setsockopt(g_sockets[sockIdxAccepted]->sock,
                    SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                    (char *)&g_sockets[sockIdx]->sock, sizeof(SOCKET));
                CreateIoCompletionPort((HANDLE)g_sockets[sockIdxAccepted]->sock,
                    g_completionPort, (ULONG_PTR)sockIdxAccepted, 0);
                time(&g_sockets[sockIdxAccepted]->acceptTime);
                g_sockets[sockIdx]->lpfnEventProc(sockIdxAccepted, TCP_EV_ACCEPT, NULL, 0);
                tcp_doReceive(sockIdxAccepted);
                tcp_accept(sockIdx);
                break;

            case TCP_TYPE_IN:
            case TCP_TYPE_OUT:
                if (dwBytes == 0) // peer closed
                    tcp_release(sockIdx, TRUE);
                else
                {
                    g_sockets[sockIdx]->stat &= ~STAT_RECEIVING;
                    time(&g_sockets[sockIdx]->lastReceiveTime);
                    g_sockets[sockIdx]->bufRecv.dlen += dwBytes;

                    if (g_sockets[sockIdx]->sock == INVALID_SOCKET ||
                        g_sockets[sockIdx]->stat & 0x0F)
                        tcp_release(sockIdx, FALSE);
                    else if (tcp_processReceivedData(sockIdx) &&
                        !(g_sockets[sockIdx]->stat & STAT_PAUSED))
                        tcp_doReceive(sockIdx);
                }
                break;
            }
        }
    }

    return 0;
}

static BOOL tcp_prepare()
{
    GUID guidConnectEx = WSAID_CONNECTEX;
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    GUID guidDisconnectEx = WSAID_DISCONNECTEX;
    SOCKET tmpSocket;
    DWORD dwBytes;

    tmpSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (tmpSocket == INVALID_SOCKET) return FALSE;
    WSAIoctl(tmpSocket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidConnectEx,
        sizeof(guidConnectEx),
        &g_lpfnConnectEx,
        sizeof(g_lpfnConnectEx),
        &dwBytes,
        NULL,
        NULL);
    WSAIoctl(tmpSocket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidAcceptEx,
        sizeof(guidAcceptEx),
        &g_lpfnAcceptEx,
        sizeof(g_lpfnAcceptEx),
        &dwBytes,
        NULL,
        NULL);
    WSAIoctl(tmpSocket,
        SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guidDisconnectEx,
        sizeof(guidDisconnectEx),
        &g_lpfnDisconnectEx,
        sizeof(g_lpfnDisconnectEx),
        &dwBytes,
        NULL,
        NULL);
    closesocket(tmpSocket);

    return (g_lpfnConnectEx && g_lpfnAcceptEx && g_lpfnDisconnectEx);
}

BOOL tcp_startup(int maxSockets, TCPSOCKETEVENTPROC *lpfnEventProc)
{
    if (g_maxSockets) return TRUE;

    if (!tcp_prepare()) return FALSE;

    if (maxSockets < 512) maxSockets = 512;
    if (maxSockets > 50*1024) maxSockets = 50*1024;

    if (!lpfnEventProc) return FALSE;

    g_maxSockets = maxSockets;
    g_lpfnEventProc = lpfnEventProc;

    g_completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)NULL, 0);
    if (g_completionPort == NULL) return FALSE;

    g_sockets = (struct tcp_socket **)calloc(g_maxSockets, sizeof(struct tcp_socket *));
    if (!g_sockets) return FALSE;

    g_thread = (HANDLE)_beginthreadex(NULL, 0, tcp_workerThread, NULL, 0, NULL);
    if (!g_thread) return FALSE;

    return TRUE;
}

void tcp_cleanup()
{
    int i;

    if (!g_maxSockets || !g_sockets) return;

    for (i=0; i<g_maxSockets; i++)
    {
        if (!g_sockets[i]) break;
        g_sockets[i]->stat |= STAT_CLOSING;
    }
    for (i=0; i<g_maxSockets; i++)
    {
        if (!g_sockets[i]) break;
        PostQueuedCompletionStatus(g_completionPort, 0, i, &g_sockets[i]->ovlpClose);
    }

    PostQueuedCompletionStatus(g_completionPort, 0, 0, &g_ovlpExit);
    WaitForSingleObject(g_thread, INFINITE);
    CloseHandle(g_thread);
    g_thread = NULL;

    CloseHandle(g_completionPort);
    g_completionPort = NULL;

    for (i=0; i<g_maxSockets; i++)
    {
        if (!g_sockets[i]) break;

        buf_free(&g_sockets[i]->bufSending);
        buf_free(&g_sockets[i]->bufToSend);
        buf_free(&g_sockets[i]->bufRecv);
        free(g_sockets[i]);
    }
    free(g_sockets);
    g_sockets = NULL;
    g_maxSockets = 0;
}
