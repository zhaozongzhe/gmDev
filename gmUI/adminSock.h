#ifndef _ADMIN_SOCK_H
#define _ADMIN_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <WinSock2.h>
#include <Windows.h>
#include <time.h>
#include <process.h>

#include <ptrList.h>
#include <mxml.h>

// -------------------------------------------------------------------------------------
// kv
struct kv
{
    char *k;
    char *v;
};
struct kv *make_kv(const CHAR *k, const CHAR *v);
void free_kv(void *kv);
CHAR *find_kv(struct ptrList *keyVals, const CHAR *k);
CHAR *KeyValueToXml(struct ptrList *keyVals);
void KeyValueFromXml(const CHAR *szXml, struct ptrList **keyVals);

// -----------------------------------------------------------------------------------------
// 同步消息发送/接收线程
// -----------------------------------------------------------------------------------------

// 创建网络连接
SOCKET CmdSocket_CreateAndConnect(const CHAR *ip, WORD port);
// 网络发送
BOOL CmdSocket_Send(SOCKET cmdSock, const UCHAR *data, int dataLen);
// 网络接收
BOOL CmdSocket_Receive(SOCKET cmdSock, UCHAR **recvBuf, int *recvLen);
// 关闭连接
void CmdSocket_Close(SOCKET cmdSock);


// -----------------------------------------------------------------------------------------
// 异步消息接收线程
// 服务器会将一些通知信息通过此接口发送给客户端，格式为XML
// -----------------------------------------------------------------------------------------

// 回调函数
typedef void (CALLBACK MSGSOCKET_CB)(CHAR *msgData, int msgLen);

// 开始线程
BOOL BeginMsgSocketThread(MSGSOCKET_CB *msgSockCB,
                          const CHAR *svrIp, WORD svrPort,
                          const CHAR *svrPwd);
// 结束线程
void StopMsgSocketThread();


#ifdef __cplusplus
}
#endif

#endif
