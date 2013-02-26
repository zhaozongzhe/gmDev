#include "gmCore.h"

#include <mxml.h>

struct kv
{
    char *k;
    char *v;
};

static struct kv *make_kv(const CHAR *k, const CHAR *v)
{
    struct kv *kv;

    kv = (struct kv *)malloc(sizeof(struct kv));
    kv->k = (char *)malloc(strlen(k)+1);
    kv->v = (char *)malloc(strlen(v)+1);
    strcpy_s(kv->k, strlen(k)+1, k);
    strcpy_s(kv->v, strlen(v)+1, v);

    return kv;
}

static void free_kv(struct kv *kv)
{
    free(kv->k);
    free(kv->v);
    free(kv);
}

static CHAR *find_kv(struct ptrList *keyVals, const CHAR *k)
{
    struct ptrList *list;
    struct kv *kv;

    for (list=keyVals; list; list=list->next)
    {
        kv = (struct kv *)list->data;
        if (0==strcmp(kv->k, k)) return kv->v;
    }

    return NULL;
}

static CHAR *KeyValueToXml(struct ptrList *keyVals)
{
    mxml_node_t *xml;    /* <?xml ?> */
    mxml_node_t *admin;  /* <admin> */
    mxml_node_t *node;   /* <node> */
    struct ptrList *list;
    struct kv *kv;
    CHAR *result;

    xml = mxmlNewXML("1.0");
    admin = mxmlNewElement(xml, "admin");

    for (list=keyVals; list; list=list->next)
    {
        kv = (struct kv *)list->data;
        node = mxmlNewElement(admin, kv->k);
        mxmlNewOpaque(node, kv->v);
    }

    result = mxmlSaveAllocString(xml, MXML_NO_CALLBACK);
    mxmlDelete(xml);

    return result;
}

static void KeyValueFromXml(const CHAR *szXml, struct ptrList **keyVals)
{
    mxml_node_t *xml;    /* <?xml ?> */
    mxml_node_t *admin;  /* <admin> */
    mxml_node_t *node;   /* <node> */
    struct kv *kv;
    const CHAR *k, *v;

    xml = mxmlLoadString(NULL, szXml, MXML_OPAQUE_CALLBACK);
    admin = mxmlFindElement(xml, xml, "admin", NULL, NULL, MXML_DESCEND);
    if (!admin) return;

    node = mxmlGetFirstChild(admin);
    while (node)
    {
        k = mxmlGetElement(node);
        v = mxmlGetOpaque(node);
        if (!v) v = "";
        kv = make_kv(k, v);
        ptrList_append(keyVals, kv);

        node = mxmlGetNextSibling(node);
    }

    mxmlDelete(xml);
}

// -------------------------------------------------------------------------------
//
void admin_sendMsg(const CHAR *cmd, const CHAR *id)
{
    struct ptrList *keyVals = NULL;
    CHAR *xml, buf[20];
    int xmlLen;

    if (g_adminSocket < 0) return;

    EnterCriticalSection(&g_csAdminMsg);

    ptrList_append(&keyVals, make_kv("command", cmd));
    ptrList_append(&keyVals, make_kv("id", id));

    xml = KeyValueToXml(keyVals);
    xmlLen = strlen(xml)+1;

    *((UINT32*)buf) = htonl(6+xmlLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = 0;
    *((UINT32*)(buf+6)) = 0;

    tcp_send(g_adminSocket, buf, 10, xml, xmlLen);

    free(xml);
    ptrList_free(&keyVals, free_kv);

    LeaveCriticalSection(&g_csAdminMsg);
}

void admin_sendDownloadingProgress(struct task *task)
{
    CHAR buf[1024];
    struct speed speed = { 0 };

    if (g_adminSocket < 0) return;

    task_getSpeed(task, &speed);
    sprintf_s(buf, 1024, "%s\tdownloading\t%I64d\t%I64d\t%d\t%d\t%d\t%d",
        task->idx.id,
        task->bytesDownloaded+task->tmp.bytesDownloaded, task->idx.bytes,
        speed.up, speed.down,
        task->totalSeeders, task->totalPeers);
    admin_sendMsg(STR_DOWNLOADING_PROGRESS, buf);
}

void admin_sendCheckingProgress(struct task *task, int done, int total)
{
    CHAR buf[1024];

    if (g_adminSocket < 0) return;

    sprintf_s(buf, 1024, "%s\tchecking\t%d\t%d", task->idx.id, done, total);
    admin_sendMsg(STR_DOWNLOADING_PROGRESS, buf);
}

void admin_sendUploadingProgress(struct task *task, const CHAR *action, int done, int total)
{
    CHAR buf[1024];

    if (g_adminSocket < 0) return;

    sprintf_s(buf, 1024, "%s\t%s\t%d\t%d", task->idx.id, action, done, total);
    admin_sendMsg(STR_UPLOADING_PROGRESS, buf);
}

void admin_sendTrackerInfo(struct task *task)
{
    CHAR buf[1024];

    if (g_adminSocket < 0) return;

    sprintf_s(buf, 1024, "%s\t%d\t%d\t%d\t%d", task->idx.id,
        task->totalSeeders, task->totalPeers,
        ptrList_size(task->peersIncoming), ptrList_size(task->peersOutgoing));
    admin_sendMsg(STR_TRACKER_INFO, buf);
}

static void admin_sendResp(int sockIdx, struct ptrList *keyVals)
{
    CHAR *xml, buf[10];
    int xmlLen;

    xml = KeyValueToXml(keyVals);
    xmlLen = strlen(xml)+1;

    *((UINT32*)buf) = htonl(6+xmlLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = 0;
    *((UINT32*)(buf+6)) = 0;

    tcp_send(sockIdx, buf, 10, xml, xmlLen);

    free(xml);
}

static void admin_sendResp2(int sockIdx, struct ptrList *keyVals, void *data, int dataLen)
{
    CHAR *xml, buf[10];
    int xmlLen;

    xml = KeyValueToXml(keyVals);
    xmlLen = strlen(xml)+1;

    *((UINT32*)buf) = htonl(6+xmlLen+dataLen);
    *(buf+4) = CMD_ADMIN;
    *(buf+5) = 0;
    *((UINT32*)(buf+6)) = 0;

    tcp_send(sockIdx, buf, 10, xml, xmlLen);

    if (data && dataLen)
        tcp_send(sockIdx, data, dataLen, NULL, 0);

    free(xml);
}

static void BuildLocalIdxListData(CHAR **listData, UINT32 *listDataLen)
{
    CHAR *data, buf[2048];
    UINT32 dataLen, dataSize, len, i;
    struct idx_local *idxl;

    dataLen = 0;
    dataSize = ptrArray_size(&g_localIdxSortedById)*1600+2048;
    data = malloc(dataSize);

    for (i=0; i<(UINT32)ptrArray_size(&g_localIdxSortedById); i++)
    {
        idxl = ptrArray_nth(&g_localIdxSortedById, i);
        len = IdxLocalToUtf8String(idxl, buf, 2048);
        if (dataLen+len > dataSize-1) break;
        memcpy(data+dataLen, buf, len+1);
        dataLen += len;
    }
    data[dataLen] = 0;
    *listData = data;
    *listDataLen = dataLen+1;
}

static void admin_onGetOptions(int sockIdx, struct ptrList **keyVals)
{
    CHAR szTmp[512];

    ptrList_append(keyVals, make_kv("PID", g_options.myId));

    ptrList_append(keyVals, make_kv("svrAddr", g_options.svrAddr));
    sprintf_s(szTmp, 256, "%u", g_options.portNum);
    ptrList_append(keyVals, make_kv("portNum", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.updateMode);
    ptrList_append(keyVals, make_kv("updateMode", szTmp));
    ptrList_append(keyVals, make_kv("tmpDir", UnicodeToUtf8(g_options.tmpDir, szTmp, 512)));

    sprintf_s(szTmp, 256, "%u", g_options.dirMode);
    ptrList_append(keyVals, make_kv("dirMode", szTmp));
    ptrList_append(keyVals, make_kv("dir", UnicodeToUtf8(g_options.dir, szTmp, 512)));

    sprintf_s(szTmp, 256, "%u", g_options.userPrvc);
    ptrList_append(keyVals, make_kv("userPrvc", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.userType);
    ptrList_append(keyVals, make_kv("userType", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.userAttr);
    ptrList_append(keyVals, make_kv("userAttr", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.lineType);
    ptrList_append(keyVals, make_kv("lineType", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.lineSpeed);
    ptrList_append(keyVals, make_kv("lineSpeed", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.priorityMode);
    ptrList_append(keyVals, make_kv("priorityMode", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.downLimit);
    ptrList_append(keyVals, make_kv("downLimit", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.upLimit);
    ptrList_append(keyVals, make_kv("upLimit", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.maxConcurrentTasks);
    ptrList_append(keyVals, make_kv("maxConcurrentTasks", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.minDownloadSpeed);
    ptrList_append(keyVals, make_kv("minDownloadSpeed", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.maxDownPeersPerTask);
    ptrList_append(keyVals, make_kv("maxDownPeersPerTask", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.maxUpPeersPerTask);
    ptrList_append(keyVals, make_kv("maxUpPeersPerTask", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.maxCachesPerTask);
    ptrList_append(keyVals, make_kv("maxCachesPerTask", szTmp));
    sprintf_s(szTmp, 256, "%u", g_options.seedMinutes);
    ptrList_append(keyVals, make_kv("seedMinutes", szTmp));

    sprintf_s(szTmp, 256, "%u", g_options.diskSpaceReserve);
    ptrList_append(keyVals, make_kv("diskSpaceReserve", szTmp));

    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp(sockIdx, *keyVals);
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
    return TRUE;
}

static BOOL admin_onSetOptions1(int sockIdx, struct ptrList **keyVals)
{
    struct options o = { 0 };
    CHAR szTmp[512];

    o.myIp;
    o.myMac;
    o.myId;

    if (!GetValueOfKey(*keyVals, "svrAddr", o.svrAddr, 256)) return FALSE;
    if (!GetValueOfKey(*keyVals, "portNum", szTmp, 256)) return FALSE;
    o.portNum = (WORD)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "updateMode", szTmp, 256)) return FALSE;
    o.updateMode = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "tmpDir", szTmp, 512)) return FALSE;
    Utf8ToUnicode(szTmp, o.tmpDir, MAX_PATH);

    if (!GetValueOfKey(*keyVals, "dirMode", szTmp, 256)) return FALSE;
    o.dirMode = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "dir", szTmp, 512)) return FALSE;
    Utf8ToUnicode(szTmp, o.dir, MAX_PATH);

    if (!GetValueOfKey(*keyVals, "userPrvc", szTmp, 256)) return FALSE;
    o.userPrvc = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "userType", szTmp, 256)) return FALSE;
    o.userType = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "userAttr", szTmp, 256)) return FALSE;
    o.userAttr = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "lineType", szTmp, 256)) return FALSE;
    o.lineType = (UCHAR)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "lineSpeed", szTmp, 256)) return FALSE;
    o.lineSpeed = (UCHAR)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "priorityMode", szTmp, 256)) return FALSE;
    o.priorityMode = (UCHAR)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "downLimit", szTmp, 256)) return FALSE;
    o.downLimit = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "upLimit", szTmp, 256)) return FALSE;
    o.upLimit = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "maxConcurrentTasks", szTmp, 256)) return FALSE;
    o.maxConcurrentTasks = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "minDownloadSpeed", szTmp, 256)) return FALSE;
    o.minDownloadSpeed = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "maxDownPeersPerTask", szTmp, 256)) return FALSE;
    o.maxDownPeersPerTask = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "maxUpPeersPerTask", szTmp, 256)) return FALSE;
    o.maxUpPeersPerTask = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "maxCachesPerTask", szTmp, 256)) return FALSE;
    o.maxCachesPerTask = (DWORD)atoi(szTmp);
    if (!GetValueOfKey(*keyVals, "seedMinutes", szTmp, 256)) return FALSE;
    o.seedMinutes = (DWORD)atoi(szTmp);

    if (!GetValueOfKey(*keyVals, "diskSpaceReserve", szTmp, 256)) return FALSE;
    o.diskSpaceReserve = (DWORD)atoi(szTmp);

    SetOptions(&o);

    return TRUE;
}

static void admin_onSetOptions(int sockIdx, struct ptrList **keyVals)
{
    if (admin_onSetOptions1(sockIdx, keyVals))
        ptrList_append(keyVals, make_kv("result", "ok"));
    else
        ptrList_append(keyVals, make_kv("result", "FAILED"));

    admin_sendResp(sockIdx, *keyVals);
}


BOOL task_removeAutoUpdate(const CHAR *id)
{
    CHAR *pId;

    pId = ptrArray_removeSorted(&g_tasksAutoUpdate, id);
    if (pId)
    {
        SaveAutoUpdateTaskList();
        admin_sendMsg(STR_AUTOUPDATE_DELETED, id);
        free(pId);
        return TRUE;
    }
    return FALSE;
}

BOOL task_removeWaiting(const CHAR *id)
{
    struct ptrList *li;

    for (li=g_tasksWaiting; li; li=li->next)
    {
        if (0==strcmp(li->data, id))
        {
            free(li->data);
            ptrList_remove_node(&g_tasksWaiting, li);
            SaveWaitingTaskList();
            admin_sendMsg(STR_WAITING_DELETED, id);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL task_removeWorking(const CHAR *id, BOOL deleteDir)
{
    struct ptrList *li;
    struct task *task, taskf = { 0 };

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.id, id))
        {
            svr_sendPeersRequest(g_svr, task, TRUE, 50);

            ptrList_remove_node(&g_tasksDownloading, li);
            SaveDownloadingTaskList();
            if (deleteDir) deleteDir_begin(task->dir);
            task_remove(task);
            admin_sendMsg(STR_DOWNLOADING_DELETED, id);

            return TRUE;
        }
    }

    strcpy_s(taskf.idx.id, MAX_ID_LEN, id);
    task = (struct task *)ptrArray_removeSorted(&g_tasksSeedingSI, &taskf);
    if (task)
    {
        svr_sendPeersRequest(g_svr, task, TRUE, 50);

        ptrArray_removeSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        if (deleteDir) deleteDir_begin(task->dir);
        task_remove(task);
        admin_sendMsg(STR_SEEDING_DELETED, id);
        return TRUE;
    }

    for (li=g_tasksUploading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.id, id))
        {
            taskUpload_end(task);
            ptrList_remove_node(&g_tasksUploading, li);
            task_delete(task);
            admin_sendMsg(STR_UPLOADING_DELETED, id);
            return TRUE;
        }
    }

    return FALSE;
}

BOOL task_removeDownloading(const CHAR *id)
{
    struct ptrList *li;
    struct task *task;

    for (li=g_tasksDownloading; li; li=li->next)
    {
        task = (struct task *)li->data;
        if (0==strcmp(task->idx.id, id))
        {
            svr_sendPeersRequest(g_svr, task, TRUE, 50);

            ptrList_remove_node(&g_tasksDownloading, li);
            SaveDownloadingTaskList();
            deleteDir_begin(task->dir);
            task_remove(task);
            admin_sendMsg(STR_DOWNLOADING_DELETED, id);

            return TRUE;
        }
    }
    return FALSE;
}

BOOL task_removeSeeding(const CHAR *id)
{
    struct task *task, taskf = { 0 };

    strcpy_s(taskf.idx.id, MAX_ID_LEN, id);
    task = (struct task *)ptrArray_removeSorted(&g_tasksSeedingSI, &taskf);
    if (task)
    {
        svr_sendPeersRequest(g_svr, task, TRUE, 50);

        ptrArray_removeSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        task_remove(task);
        admin_sendMsg(STR_SEEDING_DELETED, id);
        return TRUE;
    }
    return FALSE;
}

BOOL task_removeUploading(const CHAR *id)
{
    struct task *task;
    struct ptrList *li;

    for (li=g_tasksUploading; li; li=li->next)
    {
        task = li->data;
        if (0==strcmp(task->idx.id, id))
        {
            taskUpload_end(task);
            ptrList_remove_node(&g_tasksUploading, li);
            task_delete(task);
            admin_sendMsg(STR_UPLOADING_DELETED, id);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL task_removeLocalIdx(const CHAR *id, BOOL delFiles)
{
    struct idx_local *idxl, idxlf = { 0 };

    task_removeAutoUpdate(id);
    task_removeWaiting(id);

    task_removeWorking(id, delFiles);

    strcpy_s(idxlf.id, MAX_ID_LEN, id);
    idxl = (struct idx_local *)ptrArray_findSorted(&g_localIdxSortedById, &idxlf);
    if (idxl)
    {
        if (delFiles) deleteDir_begin(idxl->dir);
        ptrArray_removeSorted(&g_localIdxSortedById, idxl);
        ptrArray_removeSorted(&g_localIdxSortedByHash, idxl);
        SaveLocalIdxList();
        free(idxl);
        admin_sendMsg(STR_LOCAL_IDX_DELETED, id);
    }

    return TRUE;
}

void task_remove(struct task *task)
{
    task->action |= TS_DELETING;
    task_cancelAllIo(task);
    task_closeAllPeers(task);

    taskContinue_end(task);
    taskPrepare_end(task);
    taskCheck_end(task);
    taskUpload_end(task);
    taskTransfer_end(task);

    task_closeAllFiles(task); // may slow if many files opened

    task_delBitset(task);
    task_delStatus(task);
    task_delFileTime(task);

    task_delete(task);
    task_arrangePriorities();
}

static void admin_onGetDownloadingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *data, szTmp[1024], szDir[384];
    int dataSize, dataLen, itemLen;
    struct task *task;
    struct ptrList *li;

    if (ptrList_size(g_tasksDownloading))
    {
        dataSize = ptrList_size(g_tasksDownloading)*512+1024;
        data = malloc(dataSize);
        if (!data) return;
        dataLen = 0;

        for (li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            itemLen = sprintf_s(szTmp, 1024,
                "%s\t%s\t%s\t%d\t%d\r\n",
                task->idx.id, task->idx.hash,
                UnicodeToUtf8(task->dir, szDir, 384),
                task->action, task->errorCode);
            if (dataLen+itemLen > dataSize-1) break;
            memcpy(data+dataLen, szTmp, itemLen+1);
            dataLen += itemLen;
        }
        dataLen ++;
    }
    else
    {
        data = NULL;
        dataLen = 0;
    }

    sprintf_s(szTmp, 256, "%u", dataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, data, dataLen);

    if (data) free(data);
}

static void admin_onGetWaitingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *data, szTmp[512];
    int dataSize, dataLen, idLen;
    struct ptrList *li;

    if (ptrList_size(g_tasksWaiting))
    {
        dataSize = ptrList_size(g_tasksWaiting)*32+512;
        data = (CHAR *)malloc(dataSize);
        if (!data) return;
        dataLen = 0;

        for (li=g_tasksWaiting; li; li=li->next)
        {
            idLen = sprintf_s(szTmp, 512, "%s\r\n", li->data);
            if (dataLen+idLen > dataSize-1) break;
            memcpy(data+dataLen, szTmp, idLen+1);
            dataLen += idLen;
        }
        dataLen ++;
    }
    else
    {
        data = NULL;
        dataLen = 0;
    }

    sprintf_s(szTmp, 256, "%u", dataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, data, dataLen);

    if (data) free(data);
}

static void admin_onGetSeedingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *data, szTmp[256];
    int dataSize, dataLen, itemLen;
    struct task *task;
    int i;

    if (ptrArray_size(&g_tasksSeedingSI))
    {
        dataSize = ptrArray_size(&g_tasksSeedingSI)*64+512;
        data = malloc(dataSize);
        if (!data) return;
        dataLen = 0;

        for (i=0; i<ptrArray_size(&g_tasksSeedingSI); i++)
        {
            task = ptrArray_nth(&g_tasksSeedingSI, i);
            itemLen = sprintf_s(szTmp, 256, "%s\r\n", task->idx.id);
            if (dataLen+itemLen > dataSize-1) break;
            memcpy(data+dataLen, szTmp, itemLen+1);
            dataLen += itemLen;
        }
        dataLen ++;
    }
    else
    {
        data = NULL;
        dataLen = 0;
    }

    sprintf_s(szTmp, 256, "%u", dataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, data, dataLen);

    if (data) free(data);
}

static void admin_onGetUploadingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *data, szTmp[1024], szDir[384];
    int dataSize, dataLen, i, itemLen;
    struct task *task;
    struct ptrList *li;

    if (ptrList_size(g_tasksUploading))
    {
        dataSize = ptrList_size(g_tasksUploading)*512+2048;
        data = malloc(dataSize);
        if (!data) return;
        dataLen = 0;

        for (i=0, li=g_tasksUploading; li; li=li->next, i++)
        {
            task = li->data;
            itemLen = sprintf_s(szTmp, 1024,
                "%s\t%s\t%s\t%d\t%d\r\n",
                task->idx.id, task->idx.hash,
                UnicodeToUtf8(task->dir, szDir, 384),
                task->action, task->errorCode);
            if (dataLen+itemLen > dataSize-1) break;
            memcpy(data+dataLen, szTmp, itemLen+1);
            dataLen += itemLen;
        }
        dataLen ++;
    }
    else
    {
        data = NULL;
        dataLen = 0;
    }

    sprintf_s(szTmp, 256, "%u", dataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, data, dataLen);

    if (data) free(data);
}

static void admin_onGetAutoUpdateTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *data, szTmp[256];
    int dataSize, dataLen, i, idLen;
    CHAR *id;

    if (ptrArray_size(&g_tasksAutoUpdate))
    {
        dataSize = ptrArray_size(&g_tasksAutoUpdate)*64+512;
        data = malloc(dataSize);
        if (!data) return;
        dataLen = 0;

        for (i=0; i<ptrArray_size(&g_tasksAutoUpdate); i++)
        {
            id = ptrArray_nth(&g_tasksAutoUpdate, i);
            idLen = sprintf_s(szTmp, 256, "%s\r\n", id);
            if (dataLen+idLen > dataSize-1) break;
            memcpy(data+dataLen, szTmp, idLen+1);
            dataLen += idLen;
        }
        dataLen ++;
    }
    else
    {
        data = NULL;
        dataLen = 0;
    }

    sprintf_s(szTmp, 256, "%u", dataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, data, dataLen);

    if (data) free(data);
}

static void admin_onGetLocalIdxList(int sockIdx, struct ptrList **keyVals)
{
    CHAR *listData;
    UINT32 listDataLen;
    CHAR szTmp[256];

    BuildLocalIdxListData(&listData, &listDataLen);

    sprintf_s(szTmp, 256, "%u", listDataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, listData, listDataLen);

    free(listData);
}

static BOOL BuildNetIdxListData(UCHAR **idxListData_, UINT32 *idxListDataLen_)
{
    struct idx_net *idxn;
    UCHAR *idxListData;
    UINT32 idxListDataLen, idxListSizeMax, tmpLen;
    CHAR buf[1600];
    INT i;

    *idxListData_ = NULL;
    *idxListDataLen_ = 0;

    if (!ptrArray_size(&g_netIdxSortedById)) return TRUE;

    idxListSizeMax = ptrArray_size(&g_netIdxSortedById)*1024+4096;
    idxListData = malloc(idxListSizeMax);
    if (!idxListData) return FALSE;
    idxListDataLen = 0;
    for (i=0; i<ptrArray_size(&g_netIdxSortedById); i++)
    {
        idxn = ptrArray_nth(&g_netIdxSortedById, i);
        tmpLen = IdxNetToUtf8String(idxn, buf, 1600);

        if (idxListDataLen+tmpLen > idxListSizeMax-1) break;
        memcpy(idxListData+idxListDataLen, buf, tmpLen+1);
        idxListDataLen += tmpLen;
    }
    idxListDataLen ++; // tail 0

    *idxListData_ = idxListData;
    *idxListDataLen_ = idxListDataLen;
    return TRUE;
}

static void admin_onGetNetIdxList(int sockIdx, struct ptrList **keyVals)
{
    UCHAR *idxListData;
    UINT32 idxListDataLen;
    CHAR szTmp[256];

    BuildNetIdxListData(&idxListData, &idxListDataLen);

    sprintf_s(szTmp, 256, "%u", idxListDataLen);
    ptrList_append(keyVals, make_kv("content_length", szTmp));
    ptrList_append(keyVals, make_kv("result", "ok"));

    admin_sendResp2(sockIdx, *keyVals, idxListData, idxListDataLen);

    if (idxListData) free(idxListData);
}

static void admin_onAddTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;
    INT idLen, added;
    struct idx_net *idxn, idxnf = { 0 };

    szIds = malloc(65536);
    if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 65536))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    added = 0;
    pTmp = szIds;
    while (1)
    {
        p = pTmp;

        pTmp = strchr(p, ';'); if (!pTmp) break; *pTmp = 0; pTmp ++;

        idLen = strlen(p);
        if (idLen < MIN_ID_LEN || idLen >= MAX_ID_LEN) break;

        strcpy_s(idxnf.id, MAX_ID_LEN, p);
        idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
        if (!idxn) break;

        if (task_isDownloading(p) || task_isWaiting(p) ||
            task_isSeeding(p) || task_isUploading(p)) continue;

        ptrList_append(&g_tasksWaiting, _strdup(p));
        admin_sendMsg(STR_WAITING_ADDED, p);
        added ++;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);

    if (added > 0) SaveWaitingTaskList();
}

void admin_onNeedSeed(CHAR *hash)
{
    struct idx_local *idxl, idxlf = { 0 };

    if (strlen(hash) >= MAX_HASH_LEN) return;
    strcpy_s(idxlf.hash, MAX_HASH_LEN, hash);
    idxl = ptrArray_findSorted(&g_localIdxSortedByHash, &idxlf);
    if (!idxl) return;

    if (!task_isDownloading(idxl->id) &&
        !task_isWaiting(idxl->id) &&
        !task_isSeeding(idxl->id) &&
        !task_isUploading(idxl->id))
    {
        ptrList_append(&g_tasksWaiting, _strdup(idxl->id));
        admin_sendMsg(STR_WAITING_ADDED, idxl->id);
    }
}

void admin_onNeedUpdate(CHAR *id)
{
    CHAR *hash;

    hash = id + MAX_ID_LEN;

    if (strlen(id) >= MAX_ID_LEN) return;
    if (strlen(hash) >= MAX_HASH_LEN) return;

    svr_sendIdxListRequest(g_svr, 0);
}

static void admin_onSetDownloadingPriority(int sockIdx, struct ptrList **keyVals)
{
    struct ptrList *li, *li1;
    struct task *task1;
    CHAR *szIds, szUpDown[64];
    CHAR *p, *pTmp;
    INT upDown, idLen;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768) ||
        !GetValueOfKey(*keyVals, "UpDown", szUpDown, 64))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    li1 = NULL;
    p = szIds;
    while (!li1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        idLen = strlen(p);
        if (idLen < MIN_ID_LEN || idLen >= MAX_ID_LEN) break;

        for (li=g_tasksDownloading; li; li=li->next)
        {
            task1 = li->data;
            if (0==strcmp(task1->idx.id, p)) { li1 = li; break; }
        }

        p = pTmp;
    }
    free(szIds);

    upDown = atoi(szUpDown);

    if (!li1 || !upDown ||
        (!li1->prev && upDown < 0) ||
        (!li1->next && upDown > 0))
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    if (upDown <= -10000)  // top
    {
        ptrList_remove_node(&g_tasksDownloading, li1);
        ptrList_insert(&g_tasksDownloading, task1);
        SaveDownloadingTaskList();
    }
    else if (upDown >= 10000)  // bottom
    {
        ptrList_remove_node(&g_tasksDownloading, li1);
        ptrList_append(&g_tasksDownloading, task1);
        SaveDownloadingTaskList();
    }
    else if (upDown < 0)  // up
    {
        li = li1->prev;
        li1->data = li->data;
        li->data = task1;
        SaveDownloadingTaskList();
    }
    else if (upDown > 0)  // down
    {
        li = li1->next;
        li1->data = li->data;
        li->data = task1;
        SaveDownloadingTaskList();
    }

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);

    task_arrangePriorities();
}

static void admin_onSetWaitingPriority(int sockIdx, struct ptrList **keyVals)
{
    struct ptrList *li, *li1;
    CHAR *szIds, szUpDown[64];
    CHAR *p, *pTmp, *pId;
    INT upDown, idLen;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768) ||
        !GetValueOfKey(*keyVals, "UpDown", szUpDown, 64))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    li1 = NULL;
    p = szIds;
    while (!li1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        idLen = strlen(p);
        if (idLen < MIN_ID_LEN || idLen >= MAX_ID_LEN) break;

        for (li=g_tasksWaiting; li; li=li->next)
            if (0==strcmp(li->data, p)) { li1 = li; break; }

        p = pTmp;
    }
    free(szIds);

    upDown = atoi(szUpDown);

    if (!li1 || !upDown ||
        (!li1->prev && upDown < 0) ||
        (!li1->next && upDown > 0))
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    pId = li1->data;

    if (upDown <= -10000)  // top
    {
        ptrList_remove_node(&g_tasksWaiting, li1);
        ptrList_insert(&g_tasksWaiting, pId);
        SaveWaitingTaskList();
    }
    else if (upDown >= 10000)  // bottom
    {
        ptrList_remove_node(&g_tasksWaiting, li1);
        ptrList_append(&g_tasksWaiting, pId);
        SaveWaitingTaskList();
    }
    else if (upDown < 0)  // up
    {
        li = li1->prev;
        li1->data = li->data;
        li->data = pId;
        SaveWaitingTaskList();
    }
    else if (upDown > 0)  // down
    {
        li = li1->next;
        li1->data = li->data;
        li->data = pId;
        SaveWaitingTaskList();
    }

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onSuspendTasks(int sockIdx, struct ptrList **keyVals)
{
    struct ptrList *li;
    struct task *task;
    CHAR *szIds, *p, *pTmp;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        for (li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            if (0==strcmp(task->idx.id, p))
            {
                task->action |= TS_PAUSED;
                task_closeAllPeers(task);
                task_cancelAllIo(task);
                task_saveStatus(task);
                admin_sendMsg(STR_DOWNLOADING_PAUSED, p);
                break;
            }
        }

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onResumeTasks(int sockIdx, struct ptrList **keyVals)
{
    struct ptrList *li;
    struct task *task;
    CHAR *szIds, *p, *pTmp;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 4096))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        for (li=g_tasksDownloading; li; li=li->next)
        {
            task = li->data;
            if (0==strcmp(task->idx.id, p))
            {
                task->action &= ~TS_PAUSED;
                task_saveStatus(task);
                admin_sendMsg(STR_DOWNLOADING_RESUMED, p);
                break;
            }
        }

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onRemoveDownloadingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;

    szIds = malloc(4096); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 4096))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        task_removeDownloading(p);

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onRemoveWaitingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        task_removeWaiting(p);

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onRemoveSeedingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;

    szIds = (CHAR *)malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    pTmp = szIds;
    while (1)
    {
        p = pTmp;

        pTmp = strchr(p, ';');
        if (!pTmp) break; *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        task_removeSeeding(p);
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onRemoveUploadingTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;

    szIds = malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        task_removeUploading(p);

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onRemoveLocalIdx(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp;
    int localIdxDeleted;

    szIds = (CHAR *)malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 32768))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    localIdxDeleted = 0;
    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break; *pTmp = 0; pTmp ++;

        if (strlen(p) >= MAX_ID_LEN) break;
        if (task_removeLocalIdx(p, TRUE)) localIdxDeleted ++;

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}


static void admin_onCheckTasks(int sockIdx, struct ptrList **keyVals)
{
    CHAR *szIds, *p, *pTmp, buf[1600], szDir[384];
    struct idx_net *idxn, idxnf;
    struct task *task;
    INT idLen;

    szIds = (CHAR *)malloc(32768); if (!szIds) return;
    if (!GetValueOfKey(*keyVals, "ids", szIds, 4096))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;

        idLen = strlen(p);
        if (idLen < MIN_ID_LEN || idLen >= MAX_ID_LEN) break;

        if (task_isUploading(p)) break;

        strcpy_s(idxnf.id, MAX_ID_LEN, p);
        idxn = ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
        if (!idxn) break;

        task_removeWaiting(p);
        task_removeSeeding(p);

        task = task_isDownloading(p);
        if (!task)
        {
            task = task_new();
            strcpy_s(task->idx.id, MAX_ID_LEN, p);
            task->action = TS_CHECKING;
            ptrList_append(&g_tasksDownloading, task);
            SaveDownloadingTaskList();
        }
        else
        {
            task->action = TS_CHECKING;
            task_closeAllPeers(task);
            task_cancelAllIo(task);
            task_saveStatus(task);
        }
        sprintf_s(buf, 1600,
            "%s\t%s\t%s\t%d\t%d\r\n",
            task->idx.id, task->idx.hash,
            UnicodeToUtf8(task->dir, szDir, 384),
            task->action, task->errorCode);
        admin_sendMsg(STR_DOWNLOADING_ADDED, buf);

        p = pTmp;
    }
    free(szIds);

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onSetAutoUpdateTasks(int sockIdx, struct ptrList **keyVals)
{
    struct ptrList *ids = NULL, *list;
    CHAR *szIds, szAutoUpdate[64], *p, *pTmp;
    BOOL autoUpdate;

    szIds = malloc(65536);
    if (!GetValueOfKey(*keyVals, "ids", szIds, 65536) ||
        !GetValueOfKey(*keyVals, "AutoUpdate", szAutoUpdate, 64))
    {
        free(szIds);
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    p = szIds;
    while (1)
    {
        pTmp = strchr(p, ';');
        if (!pTmp) break;
        *pTmp = 0; pTmp ++;
        ptrList_append(&ids, _strdup(p));
        p = pTmp;
    }
    free(szIds);

    autoUpdate = (BOOL)atoi(szAutoUpdate);

    for (list=ids; list; list=list->next)
    {
        if (autoUpdate)
        {
            p = list->data;
            if (!ptrArray_findSorted(&g_tasksAutoUpdate, p))
            {
                ptrArray_insertSorted(&g_tasksAutoUpdate, p);
                admin_sendMsg(STR_AUTOUPDATE_ADDED, p);
            }
            else free(p);
        }
        else
        {
            p = list->data;
            pTmp = ptrArray_removeSorted(&g_tasksAutoUpdate, p);
            if (pTmp)
            {
                admin_sendMsg(STR_AUTOUPDATE_DELETED, p);
                free(pTmp);
            }
            free(p);
        }
    }

    ptrList_free(&ids, NULL);

    SaveAutoUpdateTaskList();

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}

static void admin_onUploadResource(int sockIdx, struct ptrList **keyVals)
{
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN], peers[1024], szDir[384], szTmp[1024];
    WCHAR dir[MAX_PATH], cate[MAX_CATEGORY_LEN];
    struct task *task;
    struct idx_net idxnf = { 0 };
    struct idx_local *idxl;
    int i, pathExist;

    if (!GetValueOfKey(*keyVals, "id", id, MAX_ID_LEN) ||
        !GetValueOfKeyW(*keyVals, "dir", dir, MAX_PATH) ||
        !GetValueOfKeyW(*keyVals, "category", cate, MAX_CATEGORY_LEN) ||
        !GetValueOfKey(*keyVals, "pwd", pwd, MAX_PWD_LEN) ||
        !GetValueOfKey(*keyVals, "notifyPeers", peers, 1024))
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    if (task_isDownloading(id))
    {
        ptrList_append(keyVals, make_kv("result", "task running"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    task_removeWaiting(id);

    if ((task = task_isUploading(id)))
    {
        strcpy_s(task->uploadPwd, MAX_PWD_LEN, pwd);
        if (!task->uploadNotifyPeers)
            task->uploadNotifyPeers = (CHAR *)malloc(1024);
        strcpy_s(task->uploadNotifyPeers, 1024, peers);
        task->action = TS_UPLOADING;
    }
    else if ((task = task_isSeeding(id)))
    {
        ptrArray_removeSorted(&g_tasksSeedingSI, task);
        ptrArray_removeSorted(&g_tasksSeedingSH, task);
        SaveSeedingTaskList();
        admin_sendMsg(STR_SEEDING_DELETED, id);

        strcpy_s(task->uploadPwd, MAX_PWD_LEN, pwd);
        if (!task->uploadNotifyPeers)
            task->uploadNotifyPeers = (CHAR *)malloc(1024);
        strcpy_s(task->uploadNotifyPeers, 1024, peers);
        task->action = TS_UPLOADING;
        ptrList_append(&g_tasksUploading, task);
    }
    else
    {
        for (i=0, pathExist=0; i<ptrArray_size(&g_localIdxSortedById); i++)
        {
            idxl = (struct idx_local *)ptrArray_nth(&g_localIdxSortedById, i);
            if (0==_wcsicmp(idxl->dir, dir)) { pathExist = 1; break; }
        }
        if (pathExist) // use the old ID
            strcpy_s(id, MAX_ID_LEN, idxl->id);
        else
        {
            strcpy_s(idxnf.id, MAX_ID_LEN, id);
            if (ptrArray_findSorted(&g_netIdxSortedById, &idxnf))
            {
                ptrList_append(keyVals, make_kv("result", "id in use"));
                admin_sendResp(sockIdx, *keyVals);
                return;
            }
        }

        task = task_new();
        strcpy_s(task->idx.id, MAX_ID_LEN, id);
        wcscpy_s(task->dir, MAX_PATH, dir);
        wcscpy_s(task->idx.category, MAX_CATEGORY_LEN, cate);
        strcpy_s(task->uploadPwd, MAX_PWD_LEN, pwd);
        if (!task->uploadNotifyPeers)
            task->uploadNotifyPeers = (CHAR *)malloc(1024);
        strcpy_s(task->uploadNotifyPeers, 1024, peers);
        task->action = TS_UPLOADING;
        ptrList_append(&g_tasksUploading, task);
    }

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);

    sprintf_s(szTmp, 1024, "%s\t%s\t%s\t%d\t%d\r\n",
        task->idx.id, task->idx.hash,
        UnicodeToUtf8(task->dir, szDir, 384),
        task->action, task->errorCode);
    admin_sendMsg(STR_UPLOADING_ADDED, szTmp);
}

static void admin_onDeleteResource(int sockIdx, struct ptrList **keyVals)
{
    struct idx_net *idxn, idxnf = { 0 };
    CHAR id[MAX_ID_LEN], pwd[MAX_PWD_LEN];

    if (!GetValueOfKey(*keyVals, "id", id, MAX_ID_LEN) ||
        !GetValueOfKey(*keyVals, "pwd", pwd, MAX_PWD_LEN))
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    strcpy_s(idxnf.id, MAX_ID_LEN, id);
    idxn = (struct idx_net *)ptrArray_findSorted(&g_netIdxSortedById, &idxnf);
    if (!idxn)
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    if (!svr_DelIdx(g_svr, id, pwd))
    {
        ptrList_append(keyVals, make_kv("result", "failed"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);

    task_removeAutoUpdate(id);
    task_removeWaiting(id);

    task_removeLocalIdx(id, FALSE);

    admin_sendMsg(STR_NET_IDX_DELETED, idxn->id);
    ptrArray_removeSorted(&g_netIdxSortedById, idxn);
    ptrArray_removeSorted(&g_netIdxSortedByHash, idxn);
    free(idxn);
}

static void BuildPeerInfo(struct task *task, CHAR **pBuf, int *pBufLen)
{
    struct ptrList *li;
    struct peer *peer;
    CHAR szTmp[256], *buf;
    int bufSize, bufLen, tmpLen;
    struct speed spd;

    bufSize = (ptrList_size(task->peersOutgoing)+ptrList_size(task->peersIncoming)+1) * 128;
    buf = (CHAR *)malloc(bufSize);
    bufLen = 0;

    for (li=task->peersOutgoing; li; li=li->next)
    {
        peer = (struct peer *)li->data;
        if (peer->status & PEER_BITFIELDED)
        {
            peer_getSpeed(peer, &spd);
            tmpLen = sprintf_s(szTmp, 256, "%s\t%s:%u\t%u\t%u\t%u\t1\t1\r\n",
                peer->pid, peer->ip, peer->port,
                peer->havePieces, spd.down, spd.up);
        }
        else tmpLen = sprintf_s(szTmp, 256, "%s\t%s:%u\t%u\t0\t0\t1\t0\r\n",
            peer->pid, peer->ip, peer->port, peer->havePieces);

        if (tmpLen+bufLen >= bufSize) break;
        memcpy(buf+bufLen, szTmp, tmpLen+1);
        bufLen += tmpLen;
    }
    for (li=task->peersIncoming; li; li=li->next)
    {
        peer = (struct peer *)li->data;
        if (peer->status & PEER_BITFIELDED)
        {
            peer_getSpeed(peer, &spd);
            tmpLen = sprintf_s(szTmp, 256, "%s\t%s:%u\t%u\t%u\t%u\t0\t1\r\n",
                peer->pid, peer->ip, peer->port,
                peer->havePieces, spd.down, spd.up);
        }
        else tmpLen = sprintf_s(szTmp, 256, "%s\t%s:%u\t%u\t0\t0\t0\t0\r\n",
            peer->pid, peer->ip, peer->port, peer->havePieces);

        if (tmpLen+bufLen >= bufSize) break;
        memcpy(buf+bufLen, szTmp, tmpLen+1);
        bufLen += tmpLen;
    }
    buf[bufLen] = 0;
    bufLen ++;

    *pBuf = buf;
    *pBufLen = bufLen;
}

static void admin_onGetPeerInfo(int sockIdx, struct ptrList **keyVals)
{
    struct task *task;
    CHAR id[MAX_ID_LEN], *buf, szTmp[256];
    int bufLen;

    if (!GetValueOfKey(*keyVals, "id", id, MAX_ID_LEN))
    {
        ptrList_append(keyVals, make_kv("result", "error param"));
        admin_sendResp(sockIdx, *keyVals);
        return;
    }

    task = task_isSeeding(id);
    if (!task) task = task_isDownloading(id);
    if (task)
    {
        BuildPeerInfo(task, &buf, &bufLen);
        sprintf_s(szTmp, 256, "%u", bufLen);
        ptrList_append(keyVals, make_kv("content_length", szTmp));
        ptrList_append(keyVals, make_kv("result", "ok"));
        admin_sendResp2(sockIdx, *keyVals, buf, bufLen);
        free(buf);
    }
    else
    {
        ptrList_append(keyVals, make_kv("result", "not working"));
        admin_sendResp(sockIdx, *keyVals);
    }
}


void admin_sendAllLists()
{
    struct ptrList *keyVals = NULL;
    struct ptrList *li;
    int i;

    EnterCriticalSection(&g_csAdminMsg);

    // send net idx list
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_NET_IDX_LIST));
    admin_onGetNetIdxList(g_adminSocket, &keyVals);
    // send local idx list
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_LOCAL_IDX_LIST));
    admin_onGetLocalIdxList(g_adminSocket, &keyVals);
    // send downloading tasks
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_DOWNLOADING_TASKS));
    admin_onGetDownloadingTasks(g_adminSocket, &keyVals);
    // send waiting tasks
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_WAITING_TASKS));
    admin_onGetWaitingTasks(g_adminSocket, &keyVals);
    // send seeding tasks
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_SEEDING_TASKS));
    admin_onGetSeedingTasks(g_adminSocket, &keyVals);
    // send uploading tasks
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_UPLOADING_TASKS));
    admin_onGetUploadingTasks(g_adminSocket, &keyVals);
    // send autoupdate tasks, first
    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_GET_AUTO_UPDATE_TASKS));
    admin_onGetAutoUpdateTasks(g_adminSocket, &keyVals);

    ptrList_free(&keyVals, free_kv);
    ptrList_append(&keyVals, make_kv("command", STR_REGISTER_SOCKET));
    ptrList_append(&keyVals, make_kv("result", "ok"));
    admin_sendResp(g_adminSocket, keyVals);

    ptrList_free(&keyVals, free_kv);

    for (li=g_tasksDownloading; li; li=li->next)
        admin_sendTrackerInfo(li->data);
    for (i=0; i<ptrArray_size(&g_tasksSeedingSI); i++)
        admin_sendTrackerInfo(ptrArray_nth(&g_tasksSeedingSI, i));

    LeaveCriticalSection(&g_csAdminMsg);
}

static void admin_onRegisterMessageSocket(int sockIdx, struct ptrList **keyVals)
{
    if (g_adminSocket >= 0 && g_adminSocket != sockIdx)
        admin_sendMsg(STR_GENERAL_INFO, "another admin");

    g_adminSocket = sockIdx;

    if (!g_idxListInitialized)
        admin_sendMsg(STR_GENERAL_INFO, "server not connected");
    else
        admin_sendAllLists();
}

static void admin_onTestSocket(int sockIdx, struct ptrList **keyVals)
{
    ptrList_append(keyVals, make_kv("result", "ok"));
    admin_sendResp(sockIdx, *keyVals);
}


BOOL admin_processCmd(int sockIdx, const UCHAR *data, int dataLen)
{
    struct ptrList *keyVals = NULL;
    CHAR *cmd;

    KeyValueFromXml((CHAR *)(data+10), &keyVals);

    cmd = find_kv(keyVals, "command");
    if (cmd)
    {
        if (strcmp(cmd, STR_REGISTER_SOCKET)==0)
            admin_onRegisterMessageSocket(sockIdx, &keyVals);
        if (strcmp(cmd, STR_TEST_SOCKET)==0)
            admin_onTestSocket(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_GET_OPTIONS)==0)
            admin_onGetOptions(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_SET_OPTIONS)==0)
            admin_onSetOptions(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_GET_DOWNLOADING_TASKS)==0)
            admin_onGetDownloadingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_WAITING_TASKS)==0)
            admin_onGetWaitingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_SEEDING_TASKS)==0)
            admin_onGetSeedingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_UPLOADING_TASKS)==0)
            admin_onGetUploadingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_AUTO_UPDATE_TASKS)==0)
            admin_onGetAutoUpdateTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_LOCAL_IDX_LIST)==0)
            admin_onGetLocalIdxList(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_GET_NET_IDX_LIST)==0)
            admin_onGetNetIdxList(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_ADD_TASKS)==0)
            admin_onAddTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_SET_DOWNLOADING_PRIORITY)==0)
            admin_onSetDownloadingPriority(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_SET_WAITING_PRIORITY)==0)
            admin_onSetWaitingPriority(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_SUSPEND_TASKS)==0)
            admin_onSuspendTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_RESUME_TASKS)==0)
            admin_onResumeTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_REMOVE_DOWNLOADING_TASKS)==0)
            admin_onRemoveDownloadingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_REMOVE_WAITING_TASKS)==0)
            admin_onRemoveWaitingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_REMOVE_SEEDING_TASKS)==0)
            admin_onRemoveSeedingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_REMOVE_UPLOADING_TASKS)==0)
            admin_onRemoveUploadingTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_REMOVE_LOCAL_IDX)==0)
            admin_onRemoveLocalIdx(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_CHECK_TASKS)==0)
            admin_onCheckTasks(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_SET_AUTO_UPDATE_TASKS)==0)
            admin_onSetAutoUpdateTasks(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_UPLOAD_RESOURCE)==0)
            admin_onUploadResource(sockIdx, &keyVals);
        else if (strcmp(cmd, STR_DELETE_RESOURCE)==0)
            admin_onDeleteResource(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_GET_PEER_INFO)==0)
            admin_onGetPeerInfo(sockIdx, &keyVals);

        else if (strcmp(cmd, STR_STOP_SERVICE)==0)
            SetEvent(g_hEventStopService);

        else
        {
            ptrList_append(&keyVals, make_kv("result", "error param"));
            admin_sendResp(sockIdx, keyVals);
        }
    }
    else
    {
        ptrList_append(&keyVals, make_kv("result", "invalid command"));
        admin_sendResp(sockIdx, keyVals);
    }

    ptrList_free(&keyVals, free_kv);

    return TRUE;
}

