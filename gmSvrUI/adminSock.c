#include "adminSock.h"

static MSGSOCKET_CB *g_msgSockCB = NULL;
static SOCKET g_msgSock = INVALID_SOCKET;
static WSAEVENT g_msgSockEvent = NULL;
static WSAEVENT g_msgSockEventStop = NULL;
static WSAEVENT g_msgSockEventStopped = NULL;
static time_t g_msgSockNextConnectTime = 0;
static time_t g_msgSockLastReceiveTime = 0;
static CHAR *g_msgSockRecvBuf = NULL;
static int g_msgSockRecvSize = 0;
static int g_msgSockRecvLen = 0;
static CHAR g_msgSvrIp[32] = { 0 };
static WORD g_msgSvrPort = 0;
static CHAR g_msgSvrPwd[32] = { 0 };

__inline static BOOL Socket_Send(SOCKET cmdSock, const CHAR *data, int dataLen)
{
    int sent, totalSent = 0;

    while (totalSent < dataLen)
    {
        sent = send(cmdSock, data+totalSent, dataLen-totalSent, 0);
        if (sent == SOCKET_ERROR) return FALSE;
        totalSent += sent;
    }
    return TRUE;
}

static void MsgSocket_Close()
{
    if (g_msgSock != INVALID_SOCKET)
    {
        closesocket(g_msgSock);
        g_msgSock = INVALID_SOCKET;
    }
}

static void MsgSocket_SendRegister()
{
    char buf[64];

    *((UINT32 *)buf) = htonl(6);
    *(buf+4) = 30;
    *(buf+5) = 86;
    *((UINT32 *)(buf+6)) = 0;

    Socket_Send(g_msgSock, buf, 10);
}

static void MsgSocket_SendTest()
{
    char buf[64];

    *((UINT32 *)buf) = htonl(6);
    *(buf+4) = 30;
    *(buf+5) = 0;
    *((UINT32 *)(buf+6)) = 0;

    Socket_Send(g_msgSock, buf, 10);
}

static void MsgSocket_Connect()
{
    struct sockaddr_in addr;
    time_t currTime;

    time(&currTime);

    if (g_msgSock != INVALID_SOCKET)
    {
        if (currTime - g_msgSockLastReceiveTime >= 120)
            MsgSocket_SendTest();
        return;
    }

    if (currTime < g_msgSockNextConnectTime) return;
    g_msgSockNextConnectTime = currTime + 5;

    g_msgSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (g_msgSock == INVALID_SOCKET) return;

    ResetEvent(g_msgSockEvent);
    WSAEventSelect(g_msgSock, g_msgSockEvent, FD_CLOSE|FD_CONNECT|FD_READ);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(g_msgSvrIp);
    addr.sin_port = htons(g_msgSvrPort);
    connect(g_msgSock, (struct sockaddr *)&addr, sizeof(addr));
}

static void MsgSocket_ProcessReceived()
{
    int msgLen;

    while (1)
    {
        if (g_msgSockRecvLen < 5) break;
        msgLen = 4 + ntohl(*((UINT32 *)g_msgSockRecvBuf));
        if (msgLen > g_msgSockRecvLen) break;
        if (*(g_msgSockRecvBuf+4) != 30)
        { g_msgSockRecvLen = 0; break; }

        g_msgSockCB((CHAR *)g_msgSockRecvBuf, msgLen);

        if (msgLen < g_msgSockRecvLen)
        {
            memmove(g_msgSockRecvBuf, g_msgSockRecvBuf+msgLen, g_msgSockRecvLen-msgLen);
            g_msgSockRecvLen -= msgLen;
        }
        else
        {
            g_msgSockRecvLen = 0;
            break;
        }
    }
}

static void MsgSocket_Receive()
{
    CHAR buf[8192], *pTmp;
    int received;

    while (1)
    {
        received = recv(g_msgSock, buf, 8192, 0);
        if (received == SOCKET_ERROR) break;
        if (received > 0)
        {
            if (!g_msgSockRecvBuf)
            {
                g_msgSockRecvBuf = (CHAR *)malloc(16384);
                g_msgSockRecvSize = 16384;
                g_msgSockRecvLen = received;
                memcpy(g_msgSockRecvBuf, buf, received);
            }
            else
            {
                if (g_msgSockRecvSize < g_msgSockRecvLen+received)
                {
                    pTmp = (CHAR *)realloc(g_msgSockRecvBuf, g_msgSockRecvSize + 16384);
                    if (!pTmp) return;
                    g_msgSockRecvBuf = pTmp;
                    g_msgSockRecvSize += 16384;
                }
                memcpy(g_msgSockRecvBuf+g_msgSockRecvLen, buf, received);
                g_msgSockRecvLen += received;
            }
            time(&g_msgSockLastReceiveTime);
        }
    }
}

static void MsgSocket_ProcessEvent()
{
    WSANETWORKEVENTS ne;

    if (SOCKET_ERROR == WSAEnumNetworkEvents(g_msgSock, g_msgSockEvent, &ne))
        return;

    if (ne.lNetworkEvents & FD_READ)
    {
        if (ne.iErrorCode[FD_READ_BIT])
        {
            closesocket(g_msgSock);
            g_msgSock = INVALID_SOCKET;
            time(&g_msgSockNextConnectTime);
            g_msgSockNextConnectTime += 5;
            g_msgSockCB(NULL, -1);
            return;
        }
        MsgSocket_Receive();
        MsgSocket_ProcessReceived();
    }
    else if (ne.lNetworkEvents & FD_CONNECT)
    {
        if (ne.iErrorCode[FD_CONNECT_BIT])
        {
            closesocket(g_msgSock);
            g_msgSock = INVALID_SOCKET;
            time(&g_msgSockNextConnectTime);
            g_msgSockNextConnectTime += 5;
            g_msgSockCB(NULL, -1);
            return;
        }
        MsgSocket_SendRegister();
    }
    else if (ne.lNetworkEvents & FD_CLOSE)
    {
        closesocket(g_msgSock);
        g_msgSock = INVALID_SOCKET;
        time(&g_msgSockNextConnectTime);
        g_msgSockNextConnectTime += 5;
        g_msgSockCB(NULL, -1);
    }
}

static unsigned __stdcall MsgSocketThreadProc(LPVOID param)
{
    WSAEVENT eh[2];
    DWORD dwWait;

    eh[0] = g_msgSockEvent;
    eh[1] = g_msgSockEventStop;

    while (1)
    {
        dwWait = WSAWaitForMultipleEvents(2, eh, FALSE, 1000, FALSE);
        if (dwWait == WSA_WAIT_EVENT_0)
            MsgSocket_ProcessEvent();
        else if (dwWait == WSA_WAIT_TIMEOUT)
            MsgSocket_Connect();
        else
            break;
    }

    WSASetEvent(g_msgSockEventStopped);
    return 0;
}

BOOL BeginMsgSocketThread(MSGSOCKET_CB *msgSockCB,
                          const CHAR *svrIp, WORD svrPort,
                          const CHAR *svrPwd)
{
    g_msgSockCB = msgSockCB;
    strcpy_s(g_msgSvrIp, 32, svrIp);
    g_msgSvrPort = svrPort;
    strcpy_s(g_msgSvrPwd, 32, svrPwd);
    g_msgSockEvent = WSACreateEvent();
    g_msgSockEventStop = WSACreateEvent();
    g_msgSockEventStopped = WSACreateEvent();
    CloseHandle((HANDLE)_beginthreadex(NULL, 0, MsgSocketThreadProc, NULL, 0, NULL));

    return TRUE;
}

void StopMsgSocketThread()
{
    if (!g_msgSockEventStop) return;

    WSASetEvent(g_msgSockEventStop);

    while(TRUE)
    {
        if (WAIT_OBJECT_0==MsgWaitForMultipleObjects(1, &g_msgSockEventStopped, FALSE, INFINITE, QS_ALLINPUT))
            break;
        else
        {
            MSG msg;
            PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
            DispatchMessage(&msg);
        }
    }

    WSACloseEvent(g_msgSockEvent);
    WSACloseEvent(g_msgSockEventStop);
    WSACloseEvent(g_msgSockEventStopped);
    g_msgSockEvent = NULL;
    g_msgSockEventStop = NULL;
    g_msgSockEventStopped = NULL;
    if (g_msgSockRecvBuf) free(g_msgSockRecvBuf);

    MsgSocket_Close();
}


// ------------------------------------------------------------------------------------------
//
SOCKET CmdSocket_CreateAndConnect(const CHAR *ip, WORD port)
{
    SOCKET cmdSock;
    struct sockaddr_in addr;

    cmdSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (cmdSock == INVALID_SOCKET) return INVALID_SOCKET;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip);
    addr.sin_port = htons(port);
    if (SOCKET_ERROR == connect(cmdSock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        closesocket(cmdSock);
        return INVALID_SOCKET;
    }

    return cmdSock;
}

BOOL CmdSocket_Send(SOCKET cmdSock, const UCHAR *data, int dataLen)
{
    int sent, totalSent = 0;

    while (totalSent < dataLen)
    {
        sent = send(cmdSock, (CHAR*)(data+totalSent), dataLen-totalSent, 0);
        if (sent == SOCKET_ERROR) return FALSE;
        totalSent += sent;
    }
    return TRUE;
}

BOOL CmdSocket_Receive(SOCKET cmdSock, UCHAR **recvBuf, int *recvLen)
{
    int bufLen, cmdLen, recvLen1, recvTotal = 0;
    CHAR *buf, *bufNew;

    bufLen = 8192;
    buf = malloc(bufLen);
    if (!buf) return FALSE;

    recvTotal = 0;
    while (1)
    {
        recvLen1 = recv(cmdSock, buf+recvTotal, bufLen-recvTotal, 0);
        if (recvLen1 == SOCKET_ERROR) return FALSE;

        recvTotal += recvLen1;
        if (recvTotal < 5) return FALSE;
        cmdLen = 4 + ntohl(*((UINT32 *)buf));
        if (*(buf+4) != 30) return FALSE;
        if (bufLen < cmdLen)
        {
            bufLen = ((cmdLen + 1) / 2048 + 1) * 2048;
            bufNew = (CHAR *)realloc(buf, bufLen);
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
    *recvBuf = (UCHAR *)buf;
    *recvLen = recvTotal;

    return TRUE;
}

void CmdSocket_Close(SOCKET cmdSock)
{
    if (cmdSock != INVALID_SOCKET)
        closesocket(cmdSock);
}
