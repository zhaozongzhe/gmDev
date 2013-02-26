#include "adminCli.h"

#include "helper.h"
#include "debugf.h"
#include "ptrArray.h"
#include "ptrList.h"

CHAR g_seedListVer[32] = { 0 };
struct ptrList *g_downloading = NULL;
struct ptrList *g_waiting = NULL;
struct ptrList *g_uploading = NULL;
struct ptrArray g_seeding = { 0 };
struct ptrArray g_autoUpdate = { 0 };
struct ptrArray g_localIdx = { 0 };
struct ptrArray g_netIdx = { 0 };
struct ptrArray g_searchRes = { 0 };

struct ptrArray g_progress = { 0 };

struct ptrArray g_categories = { 0 };

struct ptrArray g_trackerInfo = { 0 };

int g_downloadingCnt = 0;
int g_waitingCnt = 0;
int g_uploadingCnt = 0;
int g_seedingCnt = 0;

int g_maxId = 10000;

BOOL CmdSocket_SendListXml(SOCKET cmdSock, struct ptrList *keyVals)
{
    CHAR buf[64];
    CHAR *xml;
    int xmlLen;
    BOOL success;

    xml = KeyValueToXml(keyVals);
    xmlLen = strlen(xml) + 1;

    *((UINT32*)buf) = htonl(6+xmlLen);
    *(buf+4) = 30;
    *(buf+5) = 0;
    *((UINT32*)(buf+6)) = 0;

    success = (CmdSocket_Send(cmdSock, buf, 10) && CmdSocket_Send(cmdSock, xml, xmlLen));

    free(xml);
    return success;
}

static BOOL GetValueOfKey(struct ptrList *keyVals, const CHAR *key, CHAR *val, int maxValLen)
{
    struct ptrList *list;
    struct kv *kv;

    for (list=keyVals; list; list=list->next)
    {
        kv = list->data;
        if (strcmp(kv->k, key)==0)
        {
            if ((int)strlen(kv->v) < maxValLen)
            {
                strcpy_s(val, maxValLen, kv->v);
                return TRUE;
            }
            else return FALSE;
        }
    }

    return FALSE;
}

static BOOL GetValueOfKeyW(struct ptrList *keyVals, const CHAR *key, WCHAR *val, int maxValLen)
{
    CHAR *utf8;
    int utf8Len;

    utf8Len = maxValLen * 3 + 1;
    utf8 = malloc(utf8Len);
    if (!GetValueOfKey(keyVals, key, utf8, utf8Len))
    {
        free(utf8);
        return FALSE;
    }
    Utf8ToUnicode(utf8, val, maxValLen);
    free(utf8);
    return TRUE;
}

BOOL CmdSocket_GetOptions(SOCKET cmdSock, struct options *o)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf, szTmp[512];
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_OPTIONS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_OPTIONS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    free(recvBuf);

    if (!GetValueOfKey(keyVals, "svrAddr", o->svrAddr, 256)) goto _GetOptions_Error;

    if (!GetValueOfKey(keyVals, "updateMode", szTmp, 256)) goto _GetOptions_Error;
    o->updateMode = (UCHAR)atoi(szTmp);
    if (!GetValueOfKeyW(keyVals, "tmpDir", o->tmpDir, MAX_PATH)) goto _GetOptions_Error;

    if (!GetValueOfKey(keyVals, "dirMode", szTmp, 256)) goto _GetOptions_Error;
    o->dirMode = (UCHAR)atoi(szTmp);
    if (!GetValueOfKeyW(keyVals, "dir", o->dir, MAX_PATH)) goto _GetOptions_Error;

    o->PID;

    if (!GetValueOfKey(keyVals, "portNum", szTmp, 256)) goto _GetOptions_Error;
    o->portNum = (WORD)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "userPrvc", szTmp, 256)) goto _GetOptions_Error;
    o->userPrvc = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "userType", szTmp, 256)) goto _GetOptions_Error;
    o->userType = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "userAttr", szTmp, 256)) goto _GetOptions_Error;
    o->userAttr = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "lineType", szTmp, 256)) goto _GetOptions_Error;
    o->lineType = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "lineSpeed", szTmp, 256)) goto _GetOptions_Error;
    o->lineSpeed = (UCHAR)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "priorityMode", szTmp, 256)) goto _GetOptions_Error;
    o->priorityMode = (UCHAR)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "downLimit", szTmp, 256)) goto _GetOptions_Error;
    o->downLimit = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "upLimit", szTmp, 256)) goto _GetOptions_Error;
    o->upLimit = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "maxConcurrentTasks", szTmp, 256)) goto _GetOptions_Error;
    o->maxConcurrentTasks = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "minDownloadSpeed", szTmp, 256)) goto _GetOptions_Error;
    o->minDownloadSpeed = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "maxDownPeersPerTask", szTmp, 256)) goto _GetOptions_Error;
    o->maxDownPeersPerTask = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(keyVals, "maxUpPeersPerTask", szTmp, 256)) goto _GetOptions_Error;
    o->maxUpPeersPerTask = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "maxCachesPerTask", szTmp, 256)) goto _GetOptions_Error;
    o->maxCachesPerTask = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "seedMinutes", szTmp, 256)) goto _GetOptions_Error;
    o->seedMinutes = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(keyVals, "diskSpaceReserve", szTmp, 256)) goto _GetOptions_Error;
    o->diskSpaceReserve = (DWORD)atoi(szTmp);

    ptrList_free(&keyVals, free_kv);
    return TRUE;

_GetOptions_Error:
    ptrList_free(&keyVals, free_kv);
    return FALSE;
}

BOOL CmdSocket_SetOptions(SOCKET cmdSock, struct options *o)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf, szTmp[512];
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_SET_OPTIONS));

    ptrList_append(&keyVals, make_kv("PID", o->PID));

    ptrList_append(&keyVals, make_kv("svrAddr", o->svrAddr));

    sprintf_s(szTmp, 256, "%u", o->updateMode);
    ptrList_append(&keyVals, make_kv("updateMode", szTmp));
    ptrList_append(&keyVals, make_kv("tmpDir", UnicodeToUtf8(o->tmpDir, szTmp, 512)));

    sprintf_s(szTmp, 256, "%u", o->dirMode);
    ptrList_append(&keyVals, make_kv("dirMode", szTmp));
    ptrList_append(&keyVals, make_kv("dir", UnicodeToUtf8(o->dir, szTmp, 512)));

    sprintf_s(szTmp, 256, "%u", o->portNum);
    ptrList_append(&keyVals, make_kv("portNum", szTmp));

    sprintf_s(szTmp, 256, "%u", o->userPrvc);
    ptrList_append(&keyVals, make_kv("userPrvc", szTmp));
    sprintf_s(szTmp, 256, "%u", o->userType);
    ptrList_append(&keyVals, make_kv("userType", szTmp));
    sprintf_s(szTmp, 256, "%u", o->userAttr);
    ptrList_append(&keyVals, make_kv("userAttr", szTmp));
    sprintf_s(szTmp, 256, "%u", o->lineType);
    ptrList_append(&keyVals, make_kv("lineType", szTmp));
    sprintf_s(szTmp, 256, "%u", o->lineSpeed);
    ptrList_append(&keyVals, make_kv("lineSpeed", szTmp));

    sprintf_s(szTmp, 256, "%u", o->priorityMode);
    ptrList_append(&keyVals, make_kv("priorityMode", szTmp));

    sprintf_s(szTmp, 256, "%u", o->downLimit);
    ptrList_append(&keyVals, make_kv("downLimit", szTmp));
    sprintf_s(szTmp, 256, "%u", o->upLimit);
    ptrList_append(&keyVals, make_kv("upLimit", szTmp));

    sprintf_s(szTmp, 256, "%u", o->maxConcurrentTasks);
    ptrList_append(&keyVals, make_kv("maxConcurrentTasks", szTmp));
    sprintf_s(szTmp, 256, "%u", o->minDownloadSpeed);
    ptrList_append(&keyVals, make_kv("minDownloadSpeed", szTmp));
    sprintf_s(szTmp, 256, "%u", o->maxDownPeersPerTask);
    ptrList_append(&keyVals, make_kv("maxDownPeersPerTask", szTmp));
    sprintf_s(szTmp, 256, "%u", o->maxUpPeersPerTask);
    ptrList_append(&keyVals, make_kv("maxUpPeersPerTask", szTmp));

    sprintf_s(szTmp, 256, "%u", o->maxCachesPerTask);
    ptrList_append(&keyVals, make_kv("maxCachesPerTask", szTmp));

    sprintf_s(szTmp, 256, "%u", o->seedMinutes);
    ptrList_append(&keyVals, make_kv("seedMinutes", szTmp));

    sprintf_s(szTmp, 256, "%u", o->diskSpaceReserve);
    ptrList_append(&keyVals, make_kv("diskSpaceReserve", szTmp));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);
    free(recvBuf);

    if (strcmp(find_kv(keyVals, "command"), STR_SET_OPTIONS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    { ptrList_free(&keyVals, free_kv); return FALSE; }

    ptrList_free(&keyVals, free_kv);
    return TRUE;
}


BOOL DownloadingFromString(struct idx_downloading *dn, CHAR *id)
{
    CHAR *hash, *dir, *action, *err, *pCrlf;

    pCrlf = strstr(id, "\r\n"); if (pCrlf) *pCrlf = 0;
    hash = strchr(id, '\t'); if (!hash) return FALSE; *hash = 0; hash ++;
    dir = strchr(hash, '\t'); if (!dir) return FALSE; *dir = 0; dir ++;
    action = strchr(dir, '\t'); if (!action) return FALSE; *action = 0; action ++;
    err = strchr(action, '\t'); if (!err) return FALSE; *err = 0; err ++;

    strcpy_s(dn->id ,MAX_ID_LEN, id);
    strcpy_s(dn->hash, MAX_HASH_LEN, hash);
    Utf8ToUnicode(dir, dn->dir, MAX_PATH);
    dn->action = (UINT32)atoi(action);
    dn->err = (UINT32)atoi(err);
    return TRUE;
}

static void CmdSocket_ParseDownloadingTasks(CHAR *listData, UINT32 listDataLen)
{
    CHAR *pLine, *pTmp;
    struct idx_downloading *dn;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        dn = malloc(sizeof(struct idx_downloading)); if (!dn) break;
        memset(dn, 0, sizeof(struct idx_downloading));
        if (!DownloadingFromString(dn, pLine)) { free(dn); break; }
        ptrList_append(&g_downloading, dn);

        pLine = pTmp;
    }
    g_downloadingCnt = ptrList_size(g_downloading);
}

void MsgSocket_OnDownloadingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseDownloadingTasks(listData, listDataLen);
}

BOOL CmdSocket_GetDownloadingTasks(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_DOWNLOADING_TASKS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_DOWNLOADING_TASKS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnDownloadingTasks(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void CmdSocket_ParseWaitingTasks(CHAR *listData, UINT32 listDataLen)
{
    struct idx_downloading *dn;
    CHAR *pLine, *pTmp;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        dn = malloc(sizeof(struct idx_downloading)); if (!dn) break;
        memset(dn, 0, sizeof(struct idx_downloading));
        strcpy_s(dn->id, MAX_ID_LEN, pLine);
        ptrList_append(&g_waiting, dn);

        pLine = pTmp;
    }
    g_waitingCnt = ptrList_size(g_waiting);
}

void MsgSocket_OnWaitingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseWaitingTasks(listData, listDataLen);
}

BOOL CmdSocket_GetWaitingTasks(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_WAITING_TASKS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_WAITING_TASKS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnWaitingTasks(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void CmdSocket_ParseSeedingTasks(CHAR *listData, UINT32 listDataLen)
{
    struct idx_downloading *dn;
    struct idx_local *idxl, idxlf = { 0 };
    CHAR *pLine, *pTmp;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        strcpy_s(idxlf.id, MAX_ID_LEN, pLine);
        idxl = ptrArray_findSorted(&g_localIdx, &idxlf);
        if (!idxl) continue;

        dn = malloc(sizeof(struct idx_downloading)); if (!dn) break;
        memset(dn, 0, sizeof(struct idx_downloading));
        strcpy_s(dn->id, MAX_ID_LEN, pLine);
        strcpy_s(dn->hash, MAX_HASH_LEN, idxl->hash);
        wcscpy_s(dn->dir, MAX_PATH, idxl->dir);
        dn->action = TS_SEEDING;
        ptrArray_insertSorted(&g_seeding, dn);

        pLine = pTmp;
    }
    g_seedingCnt = ptrArray_size(&g_seeding);
}

void MsgSocket_OnSeedingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseSeedingTasks(listData, listDataLen);
}

BOOL CmdSocket_GetSeedingTasks(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_SEEDING_TASKS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_SEEDING_TASKS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnSeedingTasks(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void CmdSocket_ParseUploadingTasks(CHAR *listData, UINT32 listDataLen)
{
    struct idx_downloading *dn;
    CHAR *pLine, *pTmp;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        dn = malloc(sizeof(struct idx_downloading)); if (!dn) break;
        memset(dn, 0, sizeof(struct idx_downloading));
        if (!DownloadingFromString(dn, pLine)) { free(dn); break; }
        ptrList_append(&g_uploading, dn);

        pLine = pTmp;
    }
    g_uploadingCnt = ptrList_size(g_uploading);
}

void MsgSocket_OnUploadingTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseUploadingTasks(listData, listDataLen);
}

BOOL CmdSocket_GetUploadingTasks(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_UPLOADING_TASKS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_UPLOADING_TASKS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnUploadingTasks(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void CmdSocket_ParseAutoUpdateTaskList(CHAR *listData, UINT32 listDataLen)
{
    CHAR *pLine, *pTmp;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;

        ptrArray_insertSorted(&g_autoUpdate, _strdup(pLine));

        pLine = pTmp;
    }
}

void MsgSocket_OnAutoUpdateTasks(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseAutoUpdateTaskList(listData, listDataLen);
}

BOOL CmdSocket_GetAutoUpdateTasks(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_AUTO_UPDATE_TASKS));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_AUTO_UPDATE_TASKS) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnAutoUpdateTasks(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void CmdSocket_ParseLocalIdxList(CHAR *listData, UINT32 listDataLen)
{
    CHAR *pLine, *pTmp;
    struct idx_local *idxl;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;
        idxl = malloc(sizeof(struct idx_local)); if (!idxl) break;
        if (!IdxLocalFromUtf8String(idxl, pLine)) { free(idxl); break; }

        ptrArray_insertSorted(&g_localIdx, idxl);

        pLine = pTmp;
    }
}

void MsgSocket_OnLocalIdxList(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseLocalIdxList(listData, listDataLen);
}

BOOL CmdSocket_GetLocalIdxList(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_LOCAL_IDX_LIST));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_LOCAL_IDX_LIST) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnLocalIdxList(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static void _NetIdx_GetCategories()
{
    struct idx_net *pIdxn, idxn = { 0 };
    WCHAR cate[64];
    int i;

    ptrArray_free(&g_categories, free);

    for (i=0; i<ptrArray_size(&g_netIdx); i++)
    {
        pIdxn = (struct idx_net *)ptrArray_nth(&g_netIdx, i);
        wcscpy_s(cate, 64, pIdxn->category);

        if (cate[0] == L'~') continue;
        if (!cate[0]) wcscpy_s(cate, 64, L"~Œ¥∑÷¿‡");
        if (!ptrArray_findSorted(&g_categories, cate))
            ptrArray_insertSorted(&g_categories, _wcsdup(cate));
    }
}

static void CmdSocket_ParseNetIdxList(CHAR *listData, UINT32 listDataLen)
{
    CHAR *pLine, *pTmp;
    struct idx_net *idxn;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break;
        *pTmp = 0; pTmp += 2;
        idxn = malloc(sizeof(struct idx_net)); if (!idxn) break;
        if (!IdxNetFromUtf8String(idxn, pLine)) { free(idxn); break; }

        ptrArray_insertSorted(&g_netIdx, idxn);

        g_maxId = max(g_maxId, atoi(idxn->id));

        pLine = pTmp;
    }

    _NetIdx_GetCategories();
}

void MsgSocket_OnNetIdxList(CHAR *msgData, int msgLen, struct ptrList *keyVals)
{
    CHAR *listData;
    int listDataLen;

    listData = msgData+10+strlen(msgData+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParseNetIdxList(listData, listDataLen);
}

BOOL CmdSocket_GetNetIdxList(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_NET_IDX_LIST));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_NET_IDX_LIST) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    MsgSocket_OnNetIdxList(recvBuf, recvLen, keyVals);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}


static BOOL CmdSocket_OperateTasks(SOCKET cmdSock, struct ptrList *ids,
                                   const CHAR *op, const CHAR *opKey, const CHAR *opVal)
{
    struct ptrList *keyVals = NULL, *list;
    CHAR *recvBuf, *szIds;
    int recvLen, idsSize, idsLen;

    idsSize = ptrList_size(ids)*64;
    szIds = malloc(idsSize);
    if (!szIds) return FALSE;
    for (idsLen=0, list=ids; list; list=list->next)
        idsLen += sprintf_s(szIds+idsLen, idsSize-idsLen, "%s;", list->data);
    ptrList_append(&keyVals, make_kv("ids", szIds));
    free(szIds);
    if (opKey && opVal)
        ptrList_append(&keyVals, make_kv(opKey, opVal));
    ptrList_append(&keyVals, make_kv("command", op));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), op) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

BOOL CmdSocket_AddTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_ADD_TASKS, NULL, NULL);
}

BOOL CmdSocket_SetDownloadingPriority(SOCKET cmdSock, struct ptrList *ids, int upDown)
{
    CHAR szTmp[64];
    sprintf_s(szTmp, 64, "%d", upDown);
    return CmdSocket_OperateTasks(cmdSock, ids, STR_SET_DOWNLOADING_PRIORITY, "UpDown", szTmp);
}

BOOL CmdSocket_SetWaitingPriority(SOCKET cmdSock, struct ptrList *ids, int upDown)
{
    CHAR szTmp[64];
    sprintf_s(szTmp, 64, "%d", upDown);
    return CmdSocket_OperateTasks(cmdSock, ids, STR_SET_WAITING_PRIORITY, "UpDown", szTmp);
}

BOOL CmdSocket_SuspendTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_SUSPEND_TASKS, NULL, NULL);
}

BOOL CmdSocket_ResumeTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_RESUME_TASKS, NULL, NULL);
}

BOOL CmdSocket_RemoveDownloadingTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_REMOVE_DOWNLOADING_TASKS, NULL, NULL);
}

BOOL CmdSocket_RemoveWaitingTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_REMOVE_WAITING_TASKS, NULL, NULL);
}

BOOL CmdSocket_RemoveSeedingTasks(SOCKET cmdSock, struct ptrList *ids, BOOL deleteFiles)
{
    CHAR szTmp[64];
    sprintf_s(szTmp, 64, "%d", deleteFiles);
    return CmdSocket_OperateTasks(cmdSock, ids, STR_REMOVE_SEEDING_TASKS, "DeleteFiles", szTmp);
}

BOOL CmdSocket_RemoveUploadingTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_REMOVE_UPLOADING_TASKS, NULL, NULL);
}

BOOL CmdSocket_RemoveLocalIdx(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_REMOVE_LOCAL_IDX, NULL, NULL);
}

BOOL CmdSocket_CheckTasks(SOCKET cmdSock, struct ptrList *ids)
{
    return CmdSocket_OperateTasks(cmdSock, ids, STR_CHECK_TASKS, NULL, NULL);
}

BOOL CmdSocket_SetAutoUpdateTasks(SOCKET cmdSock, struct ptrList *ids, BOOL autoUpdate)
{
    CHAR szTmp[64];
    sprintf_s(szTmp, 64, "%d", autoUpdate);
    return CmdSocket_OperateTasks(cmdSock, ids, STR_SET_AUTO_UPDATE_TASKS, "AutoUpdate", szTmp);
}


BOOL CmdSocket_UploadResource(SOCKET cmdSock, const CHAR *id, const WCHAR *dir,
                              const WCHAR *cate, const CHAR *pwd,
                              const CHAR *notifyPeers)
{
    struct ptrList *keyVals = NULL;
    CHAR szDir[384], szCate[MAX_CATEGORY_LEN], *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_UPLOAD_RESOURCE));
    ptrList_append(&keyVals, make_kv("id", id));
    ptrList_append(&keyVals, make_kv("dir", UnicodeToUtf8(dir, szDir, 384)));
    ptrList_append(&keyVals, make_kv("category", UnicodeToUtf8(cate, szCate, MAX_CATEGORY_LEN)));
    ptrList_append(&keyVals, make_kv("pwd", pwd));
    ptrList_append(&keyVals, make_kv("notifyPeers", notifyPeers));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_UPLOAD_RESOURCE) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

BOOL CmdSocket_DeleteResource(SOCKET cmdSock, const CHAR *id, const CHAR *pwd)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_DELETE_RESOURCE));
    ptrList_append(&keyVals, make_kv("id", id));
    ptrList_append(&keyVals, make_kv("pwd", pwd));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_DELETE_RESOURCE) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

static BOOL PeerInfoFromString(struct peer_info *pr, CHAR *pid)
{
    CHAR *ipport, *bytes, *dnSpeed, *upSpeed, *outgoing, *connected;

    ipport = strchr(pid, '\t'); if (!ipport) return FALSE; *ipport = 0; ipport ++;
    bytes = strchr(ipport, '\t'); if (!bytes) return FALSE; *bytes = 0; bytes ++;
    dnSpeed = strchr(bytes, '\t'); if (!dnSpeed) return FALSE; *dnSpeed = 0; dnSpeed ++;
    upSpeed = strchr(dnSpeed, '\t'); if (!upSpeed) return FALSE; *upSpeed = 0; upSpeed ++;
    outgoing = strchr(upSpeed, '\t'); if (!outgoing) return FALSE; *outgoing = 0; outgoing ++;
    connected = strchr(outgoing, '\t'); if (!connected) return FALSE; *connected = 0; connected ++;

    strcpy_s(pr->pid, MAX_PID_LEN, pid);
    strcpy_s(pr->ipport, 32, ipport);
    pr->piecesHave = (UINT32)atoi(bytes);
    pr->dnSpeed = (UINT32)atoi(dnSpeed);
    pr->upSpeed = (UINT32)atoi(upSpeed);
    pr->isOutgoing = (UINT32)atoi(outgoing);
    pr->isConnected = (UINT32)atoi(connected);
    return TRUE;
}

static void CmdSocket_ParsePeerInfo(CHAR *listData, UINT32 listDataLen, struct ptrList **peers)
{
    CHAR *pLine, *pTmp;
    struct peer_info *pr;

    pLine = listData;
    while (pLine < listData+listDataLen)
    {
        pTmp = strstr(pLine, "\r\n");
        if (!pTmp) break; *pTmp = 0; pTmp += 2;
        pr = malloc(sizeof(struct peer_info)); if (!pr) break;
        if (!PeerInfoFromString(pr, pLine)) { free(pr); break; }

        ptrList_append(peers, pr);
        pLine = pTmp;
    }
}

BOOL CmdSocket_GetPeerInfo(SOCKET cmdSock, const CHAR *id, struct ptrList **peers)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf, *listData;
    int recvLen, listDataLen;

    ptrList_append(&keyVals, make_kv("command", STR_GET_PEER_INFO));
    ptrList_append(&keyVals, make_kv("id", id));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_GET_PEER_INFO) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    listData = recvBuf+10+strlen(recvBuf+10)+1;
    listDataLen = atoi(find_kv(keyVals, "content_length"));

    CmdSocket_ParsePeerInfo(listData, listDataLen, peers);

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

BOOL CmdSocket_StopService(SOCKET cmdSock)
{
    struct ptrList *keyVals = NULL;
    CHAR *recvBuf;
    int recvLen;

    ptrList_append(&keyVals, make_kv("command", STR_STOP_SERVICE));

    if (!CmdSocket_SendListXml(cmdSock, keyVals))
    {
        ptrList_free(&keyVals, free_kv);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);

    if (!CmdSocket_Receive(cmdSock, &recvBuf, &recvLen))
        return FALSE;

    KeyValueFromXml(recvBuf+10, &keyVals);

    if (strcmp(find_kv(keyVals, "command"), STR_STOP_SERVICE) ||
        strcmp(find_kv(keyVals, "result"), "ok"))
    {
        ptrList_free(&keyVals, free_kv);
        free(recvBuf);
        return FALSE;
    }

    ptrList_free(&keyVals, free_kv);
    free(recvBuf);
    return TRUE;
}

