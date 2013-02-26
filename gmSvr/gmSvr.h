#ifndef _GMSVR_H
#define _GMSVR_H

#include <winsock2.h>
#include <windows.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include <zlib.h>

#include "base64.h"
#include "sha1.h"
#include "tcp.h"
#include "debugf.h"
#include "NtSvc.h"
#include "ptrList.h"
#include "ptrArray.h"
#include "helper.h"
#include "Idx.h"

struct cache
{
    struct idx idx;

    UCHAR *fileData;            // 文件缓存
    UINT32 fileSize;            // 文件缓存大小
    time_t lastAccessTime;      // 文件缓存最后访问时间，闲置将清除
};

struct peer
{
    CHAR pid[MAX_PID_LEN];
    UINT32 ip;
    UINT16 port;
    INT32 downLimit;
    INT32 upLimit;
    INT64 downloaded;
    UCHAR completed;
    UCHAR userPrvc;
    UCHAR userType;
    UCHAR userAttr;
    UCHAR lineType;
    UCHAR lineSpeed;
    UCHAR connectable;
    time_t lastAccessTime;
};

struct group
{
    INT32 group;
    struct ptrList *peers;
};

struct crowd
{
    CHAR hash[MAX_HASH_LEN];    // idx id
    CHAR id[MAX_ID_LEN];
    int seederCnt;
    struct ptrArray peers;      // of peer
    struct ptrArray groups;     // of group
};

struct ip_port
{
    CHAR ip[MAX_IP_LEN];
    u_short port;
};

struct peer_connectable
{
    UINT32 ip;
    u_short port;
    UCHAR connectable;
    time_t lastCheckTime;
    time_t lastRefTime;
};

#pragma comment (lib, "wsock32")
#pragma comment (lib, "comctl32")
#pragma comment (lib, "comdlg32")
#pragma comment (lib, "shell32")
#pragma comment (lib, "shlwapi")
#pragma comment (lib, "wininet")


#endif
