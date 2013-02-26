#ifndef _ADMIN_H
#define _ADMIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <WinSock2.h>
#include <Windows.h>
#include <time.h>
#include <process.h>

#include "idx.h"
#include "mxml.h"
#include "zlib.h"
#include "adminSock.h"

#define STR_REGISTER_SOCKET         "register message socket"
#define STR_TEST_SOCKET             "test message socket"
#define STR_GENERAL_INFO            "general info"

#define STR_GET_OPTIONS             "get options"
#define STR_SET_OPTIONS             "set options"

#define STR_GET_DOWNLOADING_TASKS   "get downloading tasks" // 获取正在下载或更新列表
#define STR_GET_WAITING_TASKS       "get waiting tasks"     // 获取正在等待列表
#define STR_GET_AUTO_UPDATE_TASKS   "get auto update tasks" // 获取需要自动更新的资源列表
#define STR_GET_SEEDING_TASKS       "get seeding tasks"     // 获取正在做种列表
#define STR_GET_UPLOADING_TASKS     "get uploading tasks"   // 获取正在上传列表
#define STR_GET_LOCAL_IDX_LIST      "get local idx list"    // 本机资源
#define STR_GET_NET_IDX_LIST        "get net idx list"      // 网络资源

#define STR_ADD_TASKS               "add tasks"             // 添加任务到等待列表
#define STR_SET_DOWNLOADING_PRIORITY "set downloading priority"// 调整优先级,负数上移，-1000移动到顶端
#define STR_SET_WAITING_PRIORITY    "set waiting priority"  // 调整优先级,负数上移，-1000移动到顶端
#define STR_SUSPEND_TASKS           "suspend tasks"         // 暂停正在进行的任务
#define STR_RESUME_TASKS            "resume tasks"          // 启动暂停的任务
#define STR_REMOVE_DOWNLOADING_TASKS "remove downloading tasks"
#define STR_REMOVE_WAITING_TASKS    "remove waiting tasks"
#define STR_REMOVE_SEEDING_TASKS    "remove seeding tasks"
#define STR_REMOVE_UPLOADING_TASKS  "remove uploading tasks"
#define STR_REMOVE_LOCAL_IDX        "remove local idx"
#define STR_CHECK_TASKS             "check tasks"           // 检查数据，若发现错误，则转入更新
#define STR_SET_AUTO_UPDATE_TASKS   "set auto update tasks" // 设置任务为自动更新(或取消自动更新)

#define STR_UPLOAD_RESOURCE         "upload resource"       // 上传更新的资源
#define STR_DELETE_RESOURCE         "delete resource"       // 删除服务器资源

#define STR_GET_PEER_INFO           "get peer info"         // 获取节点信息

#define STR_STOP_SERVICE            "stop service"          // 停止服务

struct options
{
    CHAR PID[MAX_PID_LEN];          // 用户PID: 32字节的字符串
    CHAR svrAddr[256];              // server，用于提供种子列表、种子文件下载/上传、节点数据, 格式: 192.168.0.48:8080;
    WORD portNum;                   // 端口号，包括TCP和UDP，如:26500，此操作将会使服务重新初始化
    UCHAR updateMode;               // 设置更新模式：0(默认): 使用文件缓冲，1: 直接写入游戏目录
    WCHAR tmpDir[MAX_PATH];         // 用于更新的临时目录，仅用于更新文件的临时存储
    UCHAR dirMode;                  // 0:游戏类别+游戏名称,1:游戏名称,0为默认值
    WCHAR dir[MAX_PATH];            // 游戏存放目录
    UCHAR userPrvc;                 // 用户所处的省份代码
    UCHAR userType;                 // 0: 免费用户, 1: 付费用户, 3: vip
    UCHAR userAttr;                 // 可补种标志等
    UCHAR lineType;                 // 线路类型: 0: 电信, 1: 网通, ......
    UCHAR lineSpeed;                // 线路速度: 单位100K
    UCHAR priorityMode;             // 优先级模式, 0:无, 1:70%,20%..., 2:50%,%25,%15..., 2:40%,%30,%20..., 仅限速下载时才有效
    DWORD downLimit;                // 最大下载速度(KB/s)
    DWORD upLimit;                  // 最大上传速度(KB/s)
    DWORD maxConcurrentTasks;       // 最大同时任务数
    DWORD minDownloadSpeed;         // 低于此速度的节点将被删除
    DWORD maxDownPeersPerTask;      // 最多下载连接数
    DWORD maxUpPeersPerTask;        // 最多上传连接数
    DWORD maxCachesPerTask;         // 每个任务最大缓存
    DWORD seedMinutes;              // 下载完成以后，做种时间
    DWORD diskSpaceReserve;         // 磁盘保留空间，下载时参与计算剩余空间，MB
};

BOOL CmdSocket_GetOptions(SOCKET, struct options *o);
BOOL CmdSocket_SetOptions(SOCKET, struct options *o);

// 列表
BOOL CmdSocket_GetDownloadingTasks(SOCKET);
BOOL CmdSocket_GetWaitingTasks(SOCKET);
BOOL CmdSocket_GetSeedingTasks(SOCKET);
BOOL CmdSocket_GetUploadingTasks(SOCKET);
BOOL CmdSocket_GetAutoUpdateTasks(SOCKET);
BOOL CmdSocket_GetLocalIdxList(SOCKET);
BOOL CmdSocket_GetNetIdxList(SOCKET);

void MsgSocket_OnDownloadingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnWaitingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnSeedingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnUploadingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnAutoUpdateTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnLocalIdxList(CHAR *msgData, int msgLen, struct ptrList *keyVals);
void MsgSocket_OnNetIdxList(CHAR *msgData, int msgLen, struct ptrList *keyVals);

// 添加任务到"等待下载"列表,任务始终是先进入等待列表
BOOL CmdSocket_AddTasks(SOCKET, struct ptrList *);

// 参数upDown: 负数上移、正数下移，小于等于-1000置顶，大于等于1000置底
BOOL CmdSocket_SetDownloadingPriority(SOCKET cmdSock, struct ptrList *ids, int upDown);
BOOL CmdSocket_SetWaitingPriority(SOCKET cmdSock, struct ptrList *ids, int upDown);

// 暂停任务
BOOL CmdSocket_SuspendTasks(SOCKET, struct ptrList *);
// 重启暂停的任务
BOOL CmdSocket_ResumeTasks(SOCKET, struct ptrList *);

// 删除任务
BOOL CmdSocket_RemoveDownloadingTasks(SOCKET cmdSock, struct ptrList *ids);
BOOL CmdSocket_RemoveWaitingTasks(SOCKET cmdSock, struct ptrList *ids);
BOOL CmdSocket_RemoveSeedingTasks(SOCKET cmdSock, struct ptrList *ids, BOOL deleteFiles);
BOOL CmdSocket_RemoveUploadingTasks(SOCKET cmdSock, struct ptrList *ids);
BOOL CmdSocket_RemoveLocalIdx(SOCKET cmdSock, struct ptrList *ids);

// 检查数据，如果发现错误，则转入更新
BOOL CmdSocket_CheckTasks(SOCKET, struct ptrList *);

// 设置任务为自动更新(或取消自动更新)
BOOL CmdSocket_SetAutoUpdateTasks(SOCKET, struct ptrList *, BOOL autoUpdate);

// 上传资源
BOOL CmdSocket_UploadResource(SOCKET, const CHAR *id, const WCHAR *dir,
                              const WCHAR *cate, const CHAR *pwd,
                              const CHAR *notifyPeers);
// 删除服务器资源
BOOL CmdSocket_DeleteResource(SOCKET, const CHAR *id, const CHAR *pwd);

// 获取节点信息
BOOL CmdSocket_GetPeerInfo(SOCKET, const CHAR *id, struct ptrList **peers);

// 停止下载核心服务
BOOL CmdSocket_StopService(SOCKET cmdSock);

struct downloading_progress
{
    CHAR id[MAX_ID_LEN];
    CHAR action[16];
    INT64 total, completed;
    int upSpeed, dnSpeed;
    int seederCnt, peerCnt;
};

struct tracker_info
{
    CHAR id[MAX_ID_LEN];
    int seeders, peers;
    int incoming, outgoing;
};

struct peer_info
{
    CHAR pid[MAX_PID_LEN];
    CHAR ipport[32];
    UINT32 piecesHave;
    UINT32 dnSpeed, upSpeed;
    BOOL isOutgoing, isConnected;
};


extern CHAR g_seedListVer[];
extern struct ptrList *g_downloading;
extern struct ptrList *g_waiting;
extern struct ptrList *g_uploading;
extern struct ptrArray g_seeding;
extern struct ptrArray g_autoUpdate;
extern struct ptrArray g_localIdx;
extern struct ptrArray g_netIdx;
extern struct ptrArray g_searchRes;

extern struct ptrArray g_progress;

extern struct ptrArray g_categories;

extern struct ptrArray g_trackerInfo;

extern int g_downloadingCnt;
extern int g_waitingCnt;
extern int g_uploadingCnt;
extern int g_seedingCnt;

extern int g_maxId;

#define TS_ERROR            0x00000001  // 出错, “检查”后仍可继续下载
#define TS_PAUSED           0x00000002  // 暂停标志
#define TS_UPLOADING        0x00000008  // 正在创建并上传种子
#define TS_CONTINUING       0x00000010  // 正在继续(系统重启后继续任务)
#define TS_PREPARING        0x00000020  // 正在准备
#define TS_CHECKING         0x00000040  // 正在检查
#define TS_DELETING         0x00000080  // 正在删除
#define TS_DOWNLOADING      0x00000100  // 正在下载
#define TS_UPDATING         0x00000200  // 正在更新
#define TS_SEEDING          0x00001000  // 已经完成了下载或完成了更新并转储, 正在做种
#define TS_TRANSFERING      0x00002000  // 正在转储(已完成需要更新部分的下载)


#define STR_NET_IDX_ADDED           "net_idx_added"
#define STR_NET_IDX_DELETED         "net_idx_deleted"

#define STR_LOCAL_IDX_ADDED         "local_idx_added"
#define STR_LOCAL_IDX_DELETED       "local_idx_deleted"

#define STR_UPLOADING_ADDED         "uploading_added"
#define STR_UPLOADING_DELETED       "uploading_deleted"
#define STR_UPLOADING_PROGRESS      "uploading_progress"
#define STR_UPLOADING_ERROR         "uploading_error"

#define STR_SEEDING_ADDED           "seeding_added"
#define STR_SEEDING_DELETED         "seeding_deleted"
#define STR_SEEDING_TIME_OUT        "seeding_time_out"

#define STR_AUTOUPDATE_ADDED        "autoupdate_added"
#define STR_AUTOUPDATE_DELETED      "autoupdate_deleted"

#define STR_WAITING_ADDED           "waiting_added"
#define STR_WAITING_DELETED         "waiting_deleted"

#define STR_DOWNLOADING_PAUSED      "downloading_paused"
#define STR_DOWNLOADING_RESUMED     "downloading_resumed"

#define STR_DOWNLOADING_ADDED       "downloading_added"
#define STR_DOWNLOADING_DELETED     "downloading_deleted"
#define STR_DOWNLOADING_COMPLETED   "downloading_completed"
#define STR_DOWNLOADING_UPDATED     "downloading_updated"
#define STR_DOWNLOADING_PROGRESS    "downloading_progress"
#define STR_DOWNLOADING_CHANGED     "downloading_changed"
#define STR_DOWNLOADING_ERROR       "downloading_error"

#define STR_TRANSFER_BEGIN          "transfer_begin"
#define STR_TRANSFER_ERROR          "transfer_error"

#define STR_TRACKER_INFO            "tracker_info"


#define ERR_SUCCESS         0  // 无错
#define ERR_IDX             1  // 种子文件错，文件未找到、文件格式不对、文件名id与种子里面的id不一致
#define ERR_NEW_IDX         2  // 创建种子文件出错
#define ERR_DISK_SPACE      3  // 磁盘空间不够
#define ERR_FILE_READ       4  // 文件读写错
#define ERR_FILE_WRITE      5  // 文件读写错
#define ERR_FILES           6  // 文件错, 需完全检查
#define ERR_TMP_FILE        7  // 临时文件读写错
#define ERR_NET_IDX         8  // 下载种子文件出错
#define ERR_NET_IDX2        9  // 上传种子文件出错
#define ERR_CONTINUE        10 // 继续下载时，旧的记录有错

#ifdef __cplusplus
}
#endif

#endif
