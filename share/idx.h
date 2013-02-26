#ifndef _IDX_FILE_H
#define _IDX_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <winsock2.h>
#include <windows.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "ptrList.h"
#include "helper.h"
#include "base64.h"
#include "sha1.h"
#include "arc4.h"
#include "zlib.h"

#define IDX_EXTNAME                     L".gmIdx"

    // cmd
#define CMD_HANDSHAKE                   10
#define CMD_BITFIELD                    11
#define CMD_CHOKE                       12
#define CMD_UNCHOKE                     13
#define CMD_INTERESTED                  14
#define CMD_NOTINTERESTED               15
#define CMD_HAVE                        16  // 包含已下载总块数报告
#define CMD_REQUEST                     17
#define CMD_PIECE                       18
#define CMD_CANCEL                      19
#define CMD_RECOMMEND                   20
#define CMD_LIMITSPEED                  21
#define CMD_PING                        22

#define CMD_ADMIN                       30

#define GM_GET_IDX_LIST                 1   // 请求文件列表
#define GM_GET_IDX                      2   // 请求文件
#define GM_SET_IDX                      3   // 修改文件
#define GM_DEL_IDX                      4   // 删除文件
#define GM_GET_PEERS                    5   // 请求源
#define GM_NEED_SEED                    6   // 需要补种，服务器至客户端
#define GM_NEED_UPDATE                  7   // 需要补种，服务器至客户端
#define GM_GET_IDX_LIST_RESP            101
#define GM_GET_IDX_RESP                 102
#define GM_SET_IDX_RESP                 103
#define GM_DEL_IDX_RESP                 104
#define GM_GET_PEERS_RESP               105
#define GM_NEED_SEED_RESP               106
#define GM_NEED_UPDATE_RESP             107

#define GM_GET_SVR_OPTIONS              81
#define GM_SET_SVR_OPTIONS              82
#define GM_GET_SVR_STATUS               83
#define GM_GET_SVR_PEERS                84
#define GM_GET_SVR_IDX_LIST             85
#define GM_REG_SVR_SOCK                 86
#define GM_SVR_IDX_CHANGED              87
#define GM_GET_SVR_OPTIONS_RESP         181
#define GM_SET_SVR_OPTIONS_RESP         182
#define GM_GET_SVR_STATUS_RESP          183
#define GM_GET_SVR_PEERS_RESP           184
#define GM_GET_SVR_IDX_LIST_RESP        185
#define GM_REG_SVR_SOCK_RESP            186
#define GM_SVR_IDX_CHANGED_RESP         187

#define MAX_VERSION_LEN                 24
#define MAX_PID_LEN                     30
#define MAX_IP_LEN                      20
#define MIN_ID_LEN                      4
#define MAX_ID_LEN                      12
#define MAX_HASH_LEN                    30
#define MAX_PWD_LEN                     30
#define MAX_CATEGORY_LEN                16
#define MAX_NAME_LEN                    128
#define MAX_EXTRA_LEN                   512

#define MAX_IDXFILE_SIZE                (10*1024*1024)

// 回调函数, SetEvent(cs->hEventStop); 可以终止操作
typedef void (CALLBACK IDXCREATECB)(struct create_idx *cs);

struct create_idx
{
    CHAR id[MAX_ID_LEN];                // 种子ID，必须与文件名一致，最长63字节
    WCHAR dir[MAX_PATH];                // 文件路径，该目录及其子目录下的所有文件将被包含进去
    WCHAR category[MAX_CATEGORY_LEN];   // 类别
    DWORD pieceLength;                  // 块大小，应该是256*1024、512*1024、1024*1024等数值，以字节为单位，0表示自动匹配
    time_t creationDate;                // 1970年1月1日起的秒数，如果是0，则默认当前时间
    CHAR extraInfo[MAX_EXTRA_LEN];      // 附加信息

    IDXCREATECB *createCB;              // 回调函数
    void *cbParam;                      // 回调函数的参数
    HANDLE hEventStop;                  // 停止创建

    DWORD status;                       // 1:正在扫描目录，2:正在计算sha1，3:已经完成
    DWORD totalPieces;                  // 用于存放总块数, MAXDWORD表示正在初始化(搜索文件)
    DWORD completedPieces;              // 用于存放已经完成块数
};

// 创建种子文件，TRUE表示成功
// idxFileCreateCB返回FALSE时可中断操作
// 种子文件将被保存到：sourceDir\ID.IDXFILE_EXTNAME
int createIdx(struct create_idx *);
#define ERR_CREATE_IDX_SUCCESS          0
#define ERR_CREATE_IDX_USER_BREAK       1
#define ERR_CREATE_IDX_PARAM            2
#define ERR_CREATE_IDX_WRITE            3
#define ERR_CREATE_IDX_READ             4

// -------------------------------------------------------------------------------------------------
struct idx
{
    CHAR id[MAX_ID_LEN];                // 种子ID，种子文件名xxxxxx.idx，xxxxxx为ID，建议是字母和数字
    CHAR hash[MAX_HASH_LEN];            // 本种子的hash，20字节的base64
    WCHAR name[MAX_NAME_LEN];           // 名称
    WCHAR category[MAX_CATEGORY_LEN];   // 类别
    UINT64 creationDate;                // 种子创建时间
    UINT32 pieceLength;                 // 块大小，字节
    UINT32 pieceCount;                  // 所有文件总块数
    UINT64 bytes;                       // 所有文件总长度，字节
    UINT32 fileCount;                   // 文件数
    struct file **files;                // 文件信息
    UINT32 directoryCount;              // 目录数
    WCHAR **directories;                // 目录信息
    CHAR extraInfo[MAX_EXTRA_LEN];      // 附加信息,可存储任意信息
};

struct file
{
    WCHAR fileName[MAX_PATH];           // 文件名
    UINT64 bytes;                       // 文件长度，字节
    UINT32 pieceOffset;                 // 起始块
    UINT32 pieceCount;                  // 文件包含的块数
    UINT32 fileAttr;                    // 文件属性: 0x0001-只读、0x0002-系统、0x0004-隐藏
    UINT64 fileTime;                    // 文件的最后修改时间
    UCHAR *hash;                        // 文件hash，长度为每块20字节

    INT idxInFiles;                     // 在任务文件列表中的序号
    HANDLE hFile;                       // 文件句柄
    UINT64 bytesDownloaded;             // 已经下载字节数
    UINT32 piecesDownloaded;            // 已经下载块数
    UINT32 accessMode;                  // GENERIC_READ/GENERIC_WRITE
    time_t lastAccessTime;              // 最后访问时间
};

//----------------------------------------------------------------------------------------
BOOL idx_save(WCHAR *fileName, UCHAR *data, int dataLen);

/* 分析种子文件(仅仅获取种子文件信息，与下载上传任务无关)
  参数:
fileName: 文件名
si: 指向struct idx_file_info结构的指针
  备注:
分析过程中会分配一些内存，因此使用完毕后需要调用IdxFile_freeInfo来释放内存 */
BOOL idx_open(const WCHAR *idxFileName, struct idx *); // from file
BOOL idx_load(UCHAR *fileData, int fileDataLen, struct idx *); // from memory

/* 完成分析种子文件
参数:
ti: 指向struct idx_file_info结构的指针 */
void idx_free(struct idx *si);

//----------------------------------------------------------------------------------------
// 检查目录中文件的大小和时间是否与种子文件相符
BOOL idx_checkFilesTimeAndSize(struct idx *idx, const WCHAR *dir);

//----------------------------------------------------------------------------------------
// 删除多余的文件和目录
void idx_cleanDirectory(struct idx *idx, WCHAR *dir);

struct idx_net
{
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
    WCHAR name[MAX_NAME_LEN];
    WCHAR category[MAX_CATEGORY_LEN];
    CHAR extraInfo[MAX_EXTRA_LEN];
    UINT32 pieceLen, pieceCnt;
    INT64 size;
    time_t lastUpdateTime;
};

struct idx_local
{
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
    WCHAR name[MAX_NAME_LEN];
    WCHAR category[MAX_CATEGORY_LEN];
    CHAR extraInfo[MAX_EXTRA_LEN];
    INT64 size;
    time_t completeTime;
    WCHAR dir[MAX_PATH];
};

struct idx_downloading
{
    CHAR id[MAX_ID_LEN];
    CHAR hash[MAX_HASH_LEN];
    WCHAR dir[MAX_PATH];
    UINT32 action;
    UINT32 err;
};

static int IdxToUtf8String(struct idx *pIdx, CHAR *buf, int bufSize)
{
    CHAR name[256], category[64];

    return sprintf_s(buf, bufSize,
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%u\t"
        "%u\t"
        "%I64d\t"
        "%I64d\r\n",
        pIdx->id,
        pIdx->hash,
        UnicodeToUtf8(pIdx->name, name, 256),
        UnicodeToUtf8(pIdx->category, category, 64),
        pIdx->extraInfo,
        pIdx->pieceLength,
        pIdx->pieceCount,
        pIdx->bytes,
        pIdx->creationDate);
}

static int IdxNetToUtf8String(struct idx_net *pIdxn, CHAR *buf, int bufSize)
{
    CHAR name[256], category[64];

    return sprintf_s(buf, bufSize,
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%u\t"
        "%u\t"
        "%I64d\t"
        "%I64d\r\n",
        pIdxn->id,
        pIdxn->hash,
        UnicodeToUtf8(pIdxn->name, name, 256),
        UnicodeToUtf8(pIdxn->category, category, 64),
        pIdxn->extraInfo,
        pIdxn->pieceLen,
        pIdxn->pieceCnt,
        pIdxn->size,
        pIdxn->lastUpdateTime);
}

static BOOL IdxNetFromUtf8String(struct idx_net *idxn, CHAR *id)
{
    CHAR *hash, *name, *cate, *extra, *pieceLen, *pieceCnt, *size, *lastUpd, *pCrlf;

    pCrlf = strstr(id, "\r\n"); if (pCrlf) *pCrlf = 0;
    hash = strchr(id, '\t'); if (!hash) return FALSE; *hash = 0; hash ++;
    name = strchr(hash, '\t'); if (!name) return FALSE; *name = 0; name ++;
    cate = strchr(name, '\t'); if (!cate) return FALSE; *cate = 0; cate ++;
    extra = strchr(cate, '\t'); if (!extra) return FALSE; *extra = 0; extra ++;
    pieceLen = strchr(extra, '\t'); if (!pieceLen) return FALSE; *pieceLen = 0; pieceLen ++;
    pieceCnt = strchr(pieceLen, '\t'); if (!pieceCnt) return FALSE; *pieceCnt = 0; pieceCnt ++;
    size = strchr(pieceCnt, '\t'); if (!size) return FALSE; *size = 0; size ++;
    lastUpd = strchr(size, '\t'); if (!lastUpd) return FALSE; *lastUpd = 0; lastUpd ++;

    strcpy_s(idxn->id ,MAX_ID_LEN, id);
    strcpy_s(idxn->hash, MAX_HASH_LEN, hash);
    Utf8ToUnicode(name, idxn->name, MAX_NAME_LEN);
    Utf8ToUnicode(cate, idxn->category, MAX_CATEGORY_LEN);
    strcpy_s(idxn->extraInfo, MAX_EXTRA_LEN, extra);
    idxn->pieceLen = (UINT32)atoi(pieceLen);
    idxn->pieceCnt = (UINT32)atoi(pieceCnt);
    idxn->size = (INT64)_atoi64(size);
    idxn->lastUpdateTime = (time_t)_atoi64(lastUpd);

    return TRUE;
}

static int IdxLocalToUtf8String(struct idx_local *pIdxl, CHAR *buf, int bufSize)
{
    CHAR name[256], category[64], szDir[384];

    return sprintf_s(buf, bufSize,
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%I64d\t"
        "%I64d\t"
        "%s\r\n",
        pIdxl->id,
        pIdxl->hash,
        UnicodeToUtf8(pIdxl->name, name, 256),
        UnicodeToUtf8(pIdxl->category, category, 64),
        pIdxl->extraInfo,
        pIdxl->size,
        pIdxl->completeTime,
        UnicodeToUtf8(pIdxl->dir, szDir, 384));
}

static int IdxLocalToMbcsString(struct idx_local *pIdxl, CHAR *buf, int bufSize)
{
    CHAR name[256], category[64], szDir[384];

    return sprintf_s(buf, bufSize,
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%s\t"
        "%I64d\t"
        "%I64d\t"
        "%s\r\n",
        pIdxl->id,
        pIdxl->hash,
        UnicodeToMbcs(pIdxl->name, name, 256),
        UnicodeToMbcs(pIdxl->category, category, 64),
        pIdxl->extraInfo,
        pIdxl->size,
        pIdxl->completeTime,
        UnicodeToMbcs(pIdxl->dir, szDir, 384));
}

static BOOL IdxLocalFromUtf8String(struct idx_local *idxl, CHAR *pId)
{
    CHAR *pHash, *pCate, *pName, *pExtra, *pSize, *pTime, *pDir, *pCrlf;

    pCrlf = strstr(pId, "\r\n"); if (pCrlf) *pCrlf = 0;
    pHash = strchr(pId, '\t'); if (!pHash) return FALSE; *pHash = 0; pHash ++;
    pName = strchr(pHash, '\t'); if (!pName) return FALSE; *pName = 0; pName ++;
    pCate = strchr(pName, '\t'); if (!pCate) return FALSE; *pCate = 0; pCate ++;
    pExtra = strchr(pCate, '\t'); if (!pExtra) return FALSE; *pExtra = 0; pExtra ++;
    pSize = strchr(pExtra, '\t'); if (!pSize) return FALSE; *pSize = 0; pSize ++;
    pTime = strchr(pSize, '\t'); if (!pTime) return FALSE; *pTime = 0; pTime ++;
    pDir = strchr(pTime, '\t'); if (!pDir) return FALSE; *pDir = 0; pDir ++;

    memset(idxl, 0, sizeof(struct idx_local));
    strcpy_s(idxl->id, MAX_ID_LEN, pId);
    strcpy_s(idxl->hash, MAX_HASH_LEN, pHash);
    Utf8ToUnicode(pName, idxl->name, MAX_NAME_LEN);
    Utf8ToUnicode(pCate, idxl->category, MAX_CATEGORY_LEN);
    strcpy_s(idxl->extraInfo, MAX_EXTRA_LEN, pExtra);
    idxl->size = (UINT64)_atoi64(pSize);
    idxl->completeTime = (UINT64)_atoi64(pTime);
    Utf8ToUnicode(pDir, idxl->dir, MAX_PATH);

    return TRUE;
}

static BOOL IdxLocalFromMbcsString(struct idx_local *idxl, CHAR *pId)
{
    CHAR *pHash, *pCate, *pName, *pExtra, *pSize, *pTime, *pDir, *pCrlf;
    WCHAR wszCate[MAX_CATEGORY_LEN], wszName[MAX_NAME_LEN], wszDir[MAX_PATH];

    pCrlf = strstr(pId, "\r\n"); if (pCrlf) *pCrlf = 0;
    pHash = strchr(pId, '\t'); if (!pHash) return FALSE; *pHash = 0; pHash ++;
    pName = strchr(pHash, '\t'); if (!pName) return FALSE; *pName = 0; pName ++;
    pCate = strchr(pName, '\t'); if (!pCate) return FALSE; *pCate = 0; pCate ++;
    pExtra = strchr(pCate, '\t'); if (!pExtra) return FALSE; *pExtra = 0; pExtra ++;
    pSize = strchr(pExtra, '\t'); if (!pSize) return FALSE; *pSize = 0; pSize ++;
    pTime = strchr(pSize, '\t'); if (!pTime) return FALSE; *pTime = 0; pTime ++;
    pDir = strchr(pTime, '\t'); if (!pDir) return FALSE; *pDir = 0; pDir ++;

    MbcsToUnicode(pName, wszName, MAX_NAME_LEN);
    MbcsToUnicode(pCate, wszCate, MAX_CATEGORY_LEN);
    MbcsToUnicode(pDir, wszDir, MAX_PATH);

    if (strlen(pId)>=MAX_ID_LEN ||
        strlen(pHash)>=MAX_HASH_LEN ||
        wcslen(wszName)>=MAX_NAME_LEN ||
        wcslen(wszCate)>=MAX_CATEGORY_LEN ||
        strlen(pExtra)>=MAX_EXTRA_LEN ||
        wcslen(wszDir)>=MAX_PATH)
        return FALSE;

    memset(idxl, 0, sizeof(struct idx_local));
    strcpy_s(idxl->id, MAX_ID_LEN, pId);
    strcpy_s(idxl->hash, MAX_HASH_LEN, pHash);
    wcscpy_s(idxl->name, MAX_NAME_LEN, wszName);
    wcscpy_s(idxl->category, MAX_CATEGORY_LEN, wszCate);
    strcpy_s(idxl->extraInfo, MAX_EXTRA_LEN, pExtra);
    idxl->size = (UINT64)_atoi64(pSize);
    idxl->completeTime = (UINT64)_atoi64(pTime);
    wcscpy_s(idxl->dir, MAX_PATH, wszDir);

    return TRUE;
}

#ifdef __cplusplus
}
#endif

#endif
