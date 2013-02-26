#ifndef _TCP_SOCKET_H_
#define _TCP_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <time.h>
#include <process.h>

#define TCP_TYPE_LISTEN         1  // 监听
#define TCP_TYPE_ACCEPT         2
#define TCP_TYPE_IN             3  // 接受的连接
#define TCP_TYPE_OUT            4  // 连接到外部服务器

#define TCP_EV_ACCEPT           1
#define TCP_EV_CONNECT          2
#define TCP_EV_CLOSE            3
#define TCP_EV_SEND             4  // data sent
#define TCP_EV_RECEIVE          5  // if returns -1, the sock will be CLOSED
#define TCP_EV_CUSTOM           6  // custom event
#define TCP_EV_TIMER            7  // abount 100ms

// 事件回调函数
// TCP_EV_RECEIVE:，msgData和msgLen为收到的数据和数据大小
// TCP_EV_CUSTOM: msgData为传入的key
typedef int (CALLBACK TCPSOCKETEVENTPROC)(int sockIdx, int msgCode, UCHAR *msgData, int msgLen);

BOOL tcp_startup(int maxSockets, TCPSOCKETEVENTPROC *lpfnEventProc);
void tcp_cleanup();

int tcp_listen(const char *ip, u_short port, TCPSOCKETEVENTPROC *lpfnEventProc);
int tcp_connect(const char *ip, u_short port, TCPSOCKETEVENTPROC *lpfnEventProc);
BOOL tcp_send(int sockIdx, const void *data1, int dataLen1, const void *data2, int dataLen2);

void tcp_close(int sockIdx);

void tcp_pause(int sockIdx);
void tcp_resume(int sockIdx);

void tcp_setUserData(int sockIdx, void *lpUserData);
void *tcp_getUserData(int sockIdx);

BOOL tcp_getSendStat(int sockIdx, int *sendingSize, int *bufferedSize);
void tcp_setExpectPackLen(int sockIdx, int packLen);

void tcp_postEvent(void *key);

BOOL tcp_getInfo(int sockIdx, struct tcp_info *tsi);

struct tcp_info
{
    SOCKET sock;
    LONG type;
    LONG connecting;
    LONG sending;
    LONG paused;
    LONG closing;
    struct
    {
        char ip[20];
        u_short port;
    } peerAddr, hostAddr;
    TCPSOCKETEVENTPROC *lpfnEventProc;
    LPVOID lpUserData;

    int sockIdxListen;
    time_t lastReceiveTime;
    time_t lastSendTime;
    time_t acceptTime;
    time_t connectTime;
};


#ifdef __cplusplus
}
#endif

#endif

