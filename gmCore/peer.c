#include "gmCore.h"

static void request_free(struct request *rq)
{
    if (rq->data) free(rq->data);
    free(rq);
}

static void peer_getDesc(struct peer *peer)
{
    struct task *task = peer->task;
    CHAR *szInOut = peer->isOutgoing?"out":"in";

    if (peer->sock < 0)
        sprintf_s(peer->szDesc, 128, "peer %p(%s%s)", peer, peer->pid, szInOut);
    else
    {
        if (task) sprintf_s(peer->szDesc, 128, "peer(%s %s), sock:%d(%s:%d) task:%s",
            szInOut, peer->pid, peer->sock, peer->ip, peer->port, task->idx.id);
        else sprintf_s(peer->szDesc, 128, "peer(%s %s), sock:%d(%s:%d)",
            szInOut, peer->pid, peer->sock, peer->ip, peer->port);
    }
}

struct peer *peer_new()
{
    struct peer *peer;
    int i;

    peer = (struct peer *)malloc(sizeof(struct peer));
    if (!peer) return NULL;

    memset(peer, 0, sizeof(struct peer));
    peer->task = NULL;
    peer->sock = -1;
    peer->isOutgoing = 0;
    peer->amChokingToPeer = TRUE;
    peer->isPeerChokingToMe = TRUE;
    rateCtrl_init(&peer->rcDown, 4000, 100);
    rateCtrl_init(&peer->rcUp, 4000, 100);
    speed_init(&peer->speedDown, 3*60, 5);
    speed_init(&peer->speedUp, 3*60, 5);
    for (i=0; i<10; i++)
        peer->recommendedPieces[i] = MAXUINT32;
    peer_getDesc(peer);

    return peer;
}

void peer_delete(struct peer *peer)
{
    if (peer->sock >= 0)
    {
        tcp_setUserData(peer->sock, NULL);
        tcp_close(peer->sock);
    }

    bitset_free(&peer->bitset);

    rateCtrl_uninit(&peer->rcDown);
    rateCtrl_uninit(&peer->rcUp);
    speed_uninit(&peer->speedDown);
    speed_uninit(&peer->speedUp);

    ptrList_free(&peer->requests, request_free);
    ptrList_free(&peer->peerRequests, request_free);
    ptrList_free(&peer->readCache, piece_free);

    free(peer);
}

// --------------------------------------------------------------------------
//
__inline static BOOL peer_sendData(struct peer *peer, const void *data1, int dataLen1,
                                   const void *data2, int dataLen2)
{
    return tcp_send(peer->sock, data1, dataLen1, data2, dataLen2);
}

void peer_sendAlive(struct peer *peer)
{
    UCHAR buf[4] = { 0 };
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

void peer_sendPing(struct peer *peer)
{
    UCHAR buf[41];

    *((UINT32 *)buf) = htonl(37);
    *(buf+4) = CMD_PING;
    *((UINT32 *)(buf+5)) = htonl(GetTickCount());
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

__inline static void peer_sendHandShake(struct peer *peer)
{
    struct task *task = peer->task;
    UCHAR buf[86] = { 0 };

    *((UINT32 *)buf) = htonl(82);
    *(buf+4) = CMD_HANDSHAKE;
    *((UINT16 *)(buf+5)) = htons(VERSION);
    *((UINT16 *)(buf+7)) = htons(g_options.portNum);
    strcpy_s((CHAR *)(buf+9), 32, task->idx.hash);
    strcpy_s((CHAR *)(buf+41), 32, g_options.myId);
    *(buf+73) = g_options.userPrvc;
    *(buf+74) = g_options.userType;
    *(buf+75) = g_options.userAttr;
    *(buf+76) = g_options.lineType;
    *(buf+77) = g_options.lineSpeed;
    *((UINT32 *)(buf+78)) = htonl(g_options.downLimit);
    *((UINT32 *)(buf+82)) = htonl(g_options.upLimit);
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

static void peer_sendChoke(struct peer *peer)
{
    UCHAR buf[5];

    *((UINT32 *)buf) = htonl(1);
    *(buf+4) = CMD_CHOKE;
    peer->amChokingToPeer = TRUE;
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

void peer_sendUnchoke(struct peer *peer)
{
    UCHAR buf[5];

    *((UINT32 *)buf) = htonl(1);
    *((UCHAR *)(buf+4)) = CMD_UNCHOKE;
    peer->amChokingToPeer = FALSE;
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

__inline static void peer_sendInterested(struct peer *peer)
{
    UCHAR buf[5];

    *((UINT32 *)buf) = htonl(1);
    *(buf+4) = CMD_INTERESTED;
    peer->amInterestingToPeer = TRUE;
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

__inline static void peer_sendNotInterested(struct peer *peer)
{
    UCHAR buf[5];

    *((UINT32 *)buf) = htonl(1);
    *(buf+4) = CMD_NOTINTERESTED;
    peer->amInterestingToPeer = FALSE;
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

__inline static void peer_sendHave(struct peer *peer, UINT32 piece, UINT32 piecesHave)
{
    UCHAR buf[13];

    *((UINT32 *)buf) = htonl(9);
    *(buf+4) = CMD_HAVE;
    *((UINT32 *)(buf+5)) = htonl(piece);
    *((UINT32 *)(buf+9)) = htonl(piecesHave);
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

static void peer_sendBitField(struct peer *peer)
{
    struct task *task = peer->task;
    UCHAR buf[6];

    *((UINT32 *)buf) = htonl(2+task->bitset.byteCount);
    *(buf+4) = CMD_BITFIELD;
    *(buf+5) = 0;

    if (task->bytesDownloaded+task->tmp.bytesDownloaded >= task->idx.bytes)
    {
        // have ALL
        *((UINT32 *)buf) = htonl(2);
        *(buf+5) = 1;
        peer_sendData(peer, buf, sizeof(buf), NULL, 0);
    }
    else if (task->action & TS_UPDATING)
    {
        // 更新模式，bitset需要计算
        struct bitset bs = { 0 };
        bitset_copy(&bs, &task->bitset);
        if (task->tmp.bitset.bits) bitset_or(&bs, &task->tmp.bitset);
        peer_sendData(peer, buf, sizeof(buf), bs.bits, bs.byteCount);
        bitset_free(&bs);
    }
    else if (!task->bytesDownloaded)
    {
        // have NONE
        *((UINT32 *)buf) = htonl(2);
        *(buf+5) = 2;
        peer_sendData(peer, buf, sizeof(buf), NULL, 0);
    }
    else
        peer_sendData(peer, buf, sizeof(buf), task->bitset.bits, task->bitset.byteCount);
}

__inline static void peer_sendRequest(struct peer *peer, UINT32 piece, UINT32 offset, UINT32 len)
{
    UCHAR buf[17];

    *((UINT32 *)buf) = htonl(13);
    *(buf+4) = CMD_REQUEST;
    *((UINT32 *)(buf+5)) = htonl(piece);
    *((UINT32 *)(buf+9)) = htonl(offset);
    *((UINT32 *)(buf+13)) = htonl(len);
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

__inline static void peer_sendCancel(struct peer *peer, UINT32 pieceIndex, UINT32 offset, UINT32 len)
{
    UCHAR buf[17];

    *((UINT32 *)buf) = htonl(13);
    *(buf+4) = CMD_CANCEL;
    *((UINT32 *)(buf+5)) = htonl(pieceIndex);
    *((UINT32 *)(buf+9)) = htonl(offset);
    *((UINT32 *)(buf+13)) = htonl(len);
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

static BOOL peer_sendPiece(struct peer *peer, struct file *file,
                           UINT32 piece, UINT32 offset, UINT32 len, UCHAR *pieceData)
{
    UCHAR buf[13];
    const UCHAR *pData;

    *((UINT32 *)buf) = htonl(9+len);
    *(buf+4) = CMD_PIECE;
    *((UINT32 *)(buf+5)) = htonl(piece+file->pieceOffset);
    *((UINT32 *)(buf+9)) = htonl(offset);

    pData = pieceData;
    if (offset != MAXUINT32) pData += offset;

    return peer_sendData(peer, buf, sizeof(buf), pData, len);
}

__inline static void peer_sendLimitSpeed(struct peer *peer)
{
    UCHAR buf[13];

    *((UINT32 *)buf) = htonl(9);
    *(buf+4) = CMD_LIMITSPEED;
    *((UINT32 *)(buf+5)) = htonl(g_options.downLimit);
    *((UINT32 *)(buf+9)) = htonl(g_options.upLimit);
    peer_sendData(peer, buf, sizeof(buf), NULL, 0);
}

void peer_sendRecommend(struct peer *peer, UINT32 piece)
{
    UCHAR buf[46];

    if (piece != MAXUINT32)
    {
        *((UINT32 *)buf) = htonl(6);
        *(buf+4) = CMD_RECOMMEND;
        *(buf+5) = 1;
        *((UINT32 *)(buf+6)) = htonl(piece);
        peer_sendData(peer, buf, 10, NULL, 0);
    }
    else
    {
        struct task *task = peer->task;
        struct piece *pc;
        struct ptrList *list, *list1;
        int i;

        if (!task->readCache) return;

        *((UINT32 *)buf) = htonl(42);
        *(buf+4) = CMD_RECOMMEND;
        *(buf+5) = 10;

        for (list=task->readCache; list; list=list->next) list1 = list;
        for (i=0; i<10; i++) if (list1->prev) list1 = list1->prev; else break;
        for (i=0; i<10; i++) *((UINT32 *)(buf+6+i*4)) = htonl(MAXUINT32);
        for (i=0, list=list1; list; list=list->next, i++)
        {
            pc = (struct piece *)list->data;
            *((UINT32 *)(buf+6+i*4)) = htonl(pc->file->pieceOffset+pc->piece);
        }
        peer_sendData(peer, buf, sizeof(buf), NULL, 0);
    }
}

// piece: index in task
static void peer_cancelRequests(struct peer *peer, UINT32 piece)
{
    struct task *task = peer->task;
    struct file *file;
    struct request *rq;
    struct ptrList *li, *liDel;

    if (piece==MAXUINT32)
    {
        li = peer->requests;
        while (li)
        {
            rq = (struct request *)li->data;
            bitset_clear(&task->bitsetRequested, rq->file->pieceOffset+rq->piece);
            request_free(rq);
            liDel = li; li = li->next;
            ptrList_remove_node(&peer->requests, liDel);
        }
        return;
    }

    file = task_getFileInfo(task, piece);
    if (!file) return;

    piece -= file->pieceOffset;

    li = peer->requests;
    while (li)
    {
        rq = (struct request *)li->data;
        if (rq->file==file && rq->piece==piece)
        {
            request_free(rq);
            liDel = li; li = li->next;
            ptrList_remove_node(&peer->requests, liDel);
        }
        else
            li = li->next;
    }
}

static UINT32 peer_allocPieceRequest(struct task *task, struct peer *peer,
                                     struct file *file, UINT32 piece)
{
    struct request *rq;

    rq = (struct request *)malloc(sizeof(struct request));
    if (!rq) return MAXUINT32;
    memset(rq, 0, sizeof(struct request));
    rq->file = file;
    rq->piece = piece;
    rq->offset = MAXUINT32;
    rq->length = task_getPieceLength(task, file, piece);
    ptrList_append(&peer->requests, rq);

    return 1;
}

static UINT32 peer_allocSliceRequests(struct task *task, struct peer *peer,
                                      struct file *file, UINT32 piece)
{
    UINT32 pieceLen, leftLen, requestLen, sliceSize, sliceCount;
    struct request *rq;

    if (task->downLimit > 2000)
        sliceSize = min(task->idx.pieceLength, 256*1024);
    else if (task->downLimit > 800)
        sliceSize = min(task->idx.pieceLength, 64*1024);
    else sliceSize = 16*1024;

    pieceLen = task_getPieceLength(task, file, piece);

    sliceCount = 0;
    leftLen = pieceLen;
    while (leftLen > 0)
    {
        requestLen = ((leftLen >= sliceSize) ? sliceSize : leftLen);

        rq = (struct request *)malloc(sizeof(struct request));
        if (!rq) return max(sliceCount, 1); // avoid dead loop
        memset(rq, 0, sizeof(struct request));
        rq->file = file;
        rq->piece = piece;
        rq->offset = pieceLen-leftLen;
        rq->length = requestLen;
        ptrList_append(&peer->requests, rq);

        leftLen -= requestLen;
        sliceCount ++;
    }

    return sliceCount;
}

static struct file *peer_getNewFileToDownload(struct task *task, struct peer *peer,
                                              struct bitset *bsDiff, UINT32 *newPiece)
{
    struct file *file;
    UINT32 iFile, i;
    UINT32 piece, pieceStart;
    UINT64 fileBytes;

    iFile = ((UINT32)rand()) % task->idx.fileCount;

    for (i=iFile; i<task->idx.fileCount; i++)
    {
        file = task->idx.files[i];
        fileBytes = file->bytesDownloaded;
        if (task->action & TS_UPDATING && task->tmp.files)
            fileBytes += task->tmp.files[file->idxInFiles].bytesDownloaded;
        if (fileBytes >= file->bytes) continue;
        pieceStart = file->pieceOffset;
        while (1)
        {
            piece = bitset_getNextTrue(bsDiff, pieceStart, file->pieceOffset+file->pieceCount);
            if (piece == MAXUINT32) break;
            if (1 == bitset_check(&task->bitsetRequested, piece))
            { pieceStart = piece + 1; continue; }
            *newPiece = piece - file->pieceOffset;
            return file;
        }
    }
    for (i=0; i<iFile; i++)
    {
        file = task->idx.files[i];
        fileBytes = file->bytesDownloaded;
        if (task->action & TS_UPDATING && task->tmp.files)
            fileBytes += task->tmp.files[file->idxInFiles].bytesDownloaded;
        if (fileBytes >= file->bytes) continue;
        pieceStart = file->pieceOffset;
        while (1)
        {
            piece = bitset_getNextTrue(bsDiff, pieceStart, file->pieceOffset+file->pieceCount);
            if (piece == MAXUINT32) break;
            if (1 == bitset_check(&task->bitsetRequested, piece))
            { pieceStart = piece + 1; continue; }
            *newPiece = piece - file->pieceOffset;
            return file;
        }
    }

    return NULL;
}

void peer_tryDownloadPiece(struct peer *peer, struct file *file, UINT32 piece)
{
    struct ptrList *list;
    struct request *rq;

    for (list=peer->requests; list; list=list->next)
    {
        rq = (struct request *)list->data;
        if (rq->file == file && rq->piece == piece)
            return;
    }

    if (!g_options.downLimit && !peer->upLimit)
        peer_allocPieceRequest(peer->task, peer, file, piece);
    else peer_allocSliceRequests(peer->task, peer, file, piece);
}

static void peer_onFinalStageDupRequests(struct peer *peer)
{
    //struct task *task = peer->task;
    //struct request *rqOld, *rqNew;
    //struct ptrList *li, *liDup = NULL;

    //if (peer->finalStage) return;
    //peer->finalStage = TRUE;

    //for (li=peer->requests; li; li=li->next)
    //{
    //    rqOld = li->data;
    //    rqNew = malloc(sizeof(struct request));
    //    if (!rqNew) break;
    //    *rqNew = *rqOld;
    //    ptrList_append(&liDup, rqNew);
    //}

    //debugf("onFinalStage: requested:%d/%d, pending:%d, dup:%d\r\n",
    //    bitset_countTrueBits(&task->bitsetRequested),
    //    task->idx.pieceCount, ptrList_size(peer->requests),
    //    ptrList_size(liDup));

    //for (li=liDup; li; li=li->next)
    //{
    //    rqNew = li->data;
    //    ptrList_append(&peer->requests, rqNew);
    //}
    //ptrList_free(&liDup, NULL);
}

static void peer_allocNewPiecesToDownload(struct task *task, struct peer *peer, UINT32 piecesNeed)
{
    struct bitset bsDiff = { 0 };
    struct file *file;
    UINT32 piece, i, piecesAllocated;

    piecesAllocated = 0;

    for (i=0; i<10; i++) // 推荐
    {
        piece = peer->recommendedPieces[i];
        if (piece == MAXUINT32) continue;

        peer->recommendedPieces[i] = MAXUINT32;

        if (1==bitset_check(&task->bitset, piece) ||
            1==bitset_check(&task->tmp.bitset, piece) ||
            1==bitset_check(&task->bitsetRequested, piece))
            continue;

        file = task_getFileInfo(task, piece);
        if (!file) break;

        piece -= file->pieceOffset;

        if (!g_options.downLimit && !peer->upLimit)
            piecesAllocated += peer_allocPieceRequest(task, peer, file, piece);
        else piecesAllocated += peer_allocSliceRequests(task, peer, file, piece);

        bitset_set(&task->bitsetRequested, file->pieceOffset+piece);

        if (piecesAllocated >= piecesNeed) return;
    }

    // 差异计算
    bitset_copy(&bsDiff, &peer->bitset);
    bitset_diff(&bsDiff, &task->bitsetRequested);
    if (!bitset_countTrueBits(&bsDiff)) return;

    if (!peer->downloadingFile) // 随机确定一个文件，按照顺序请求这个文件
    {
        file = peer_getNewFileToDownload(task, peer, &bsDiff, &piece);
        if (!file) // 未找到可下载的文件
        {
            peer_onFinalStageDupRequests(peer);
            bitset_free(&bsDiff);
            return;
        }
    }
    else
    {
        file = peer->downloadingFile;
        piece = peer->downloadingPiece;
    }

    while (1)
    {
        while (2)
        {
            if (0 == bitset_check(&task->bitsetRequested, file->pieceOffset+piece))
                break;
            piece ++;
            if (piece >= file->pieceCount)
            {
                file = peer_getNewFileToDownload(task, peer, &bsDiff, &piece);
                if (!file) // 未找到可下载的文件
                {
                    peer_onFinalStageDupRequests(peer);
                    bitset_free(&bsDiff);
                    return;
                }
            }
        }

        if (!g_options.downLimit && !peer->upLimit)
            piecesAllocated += peer_allocPieceRequest(task, peer, file, piece);
        else piecesAllocated += peer_allocSliceRequests(task, peer, file, piece);

        bitset_set(&bsDiff, file->pieceOffset+piece);
        bitset_set(&task->bitsetRequested, file->pieceOffset+piece);

        piece ++;
        if (piece >= file->pieceCount)
        {
            file = peer_getNewFileToDownload(task, peer, &bsDiff, &piece);
            if (!file) // 未找到可下载的文件
            {
                peer_onFinalStageDupRequests(peer);
                bitset_free(&bsDiff);
                return;
            }
        }
        peer->downloadingFile = file;
        peer->downloadingPiece = piece;

        if (piecesAllocated >= piecesNeed) break;
    }

    bitset_free(&bsDiff);
}

#define MAX_PENDING_REQUESTS    16

void peer_doPieceRequest(struct peer *peer)
{
    struct task *task = peer->task;
    struct file *file;
    struct ptrList *list;
    struct request *rq;
    UINT32 canSendPerSec, canSendThisTime;
    UINT32 requestsSent, lastSent;
    UINT32 pendingRequests, pendingRequestBytes;
    UINT32 allocRequests, allocRequestBytes;
    UINT32 maxPendingRequests;
    DWORD currTick;
    time_t currTime;
    struct transfer trans;

    if (task->action & TS_STOP_DOWNLOAD ||
        task->bytesDownloaded + task->tmp.bytesDownloaded >= task->idx.bytes ||
        peer->isPeerChokingToMe)
        return;

    // 流量控制
    currTick = GetTickCount();
    canSendThisTime = MAXUINT32;
    if (g_options.downLimit)
    {
        rateCtrl_getTransfers(&g_rcRequest, currTick, 1000, &trans);
        lastSent = (UINT32)trans.bytes;
        canSendPerSec = g_options.downLimit * 1024;
        canSendThisTime = min(canSendThisTime,
            (lastSent >= canSendPerSec) ? 0 : (canSendPerSec - lastSent));
    }
    if (task->downLimit)
    {
        rateCtrl_getTransfers(&task->rcRequest, currTick, 1000, &trans);
        lastSent = (UINT32)trans.bytes;
        canSendPerSec = task->downLimit * 1024;
        canSendThisTime = min(canSendThisTime,
            (lastSent >= canSendPerSec) ? 0 : (canSendPerSec - lastSent));
    }
    if (!canSendThisTime) return;

    // 计算已经发送但尚未收到数据的请求数
    pendingRequests = 0;
    pendingRequestBytes = 0;
    allocRequests = 0;
    allocRequestBytes = 0;
    for (list=peer->requests; list; list=list->next)
    {
        rq = (struct request *)list->data;
        if (rq->requestTime && rq->dataLen < (int)rq->length)
        {
            pendingRequests ++;
            pendingRequestBytes += rq->length;
        }
        if (!rq->requestTime)
        {
            allocRequests ++;
            allocRequestBytes += rq->length;
        }
    }

    if (!g_options.downLimit && !peer->upLimit)
        maxPendingRequests = MAX_PENDING_REQUESTS;
    else
        maxPendingRequests = MAX_PENDING_REQUESTS/2;

    // 已发送但尚未收到数据的块数已足够
    if (pendingRequests >= maxPendingRequests) return;

    // 确定需要下载的块
    if (pendingRequests+allocRequests < maxPendingRequests)
    {
        peer_allocNewPiecesToDownload(task, peer,
            maxPendingRequests-pendingRequests-allocRequests);
    }

    time(&currTime);

    for (list=peer->requests, requestsSent=0;
        list && pendingRequests<maxPendingRequests && requestsSent<canSendThisTime;
        list=list->next)
    {
        rq = (struct request *)list->data;
        if (!rq->requestTime && !rq->dataLen)
        {
            file = rq->file;
            peer_sendRequest(peer, file->pieceOffset+rq->piece, rq->offset, rq->length);
            rq->requestTime = currTime;
            pendingRequests ++;
            pendingRequestBytes += rq->length;
            requestsSent += rq->length;
        }
    }

    rateCtrl_updateTransfer(&task->rcRequest, currTick, requestsSent);
    rateCtrl_updateTransfer(&g_rcRequest, currTick, requestsSent);
}

struct piece *peer_addReadCache(struct peer *peer, struct file *file, UINT32 piece, UCHAR *data, int dataLen)
{
    struct piece *pc;

    pc = piece_new(file, piece, data, dataLen);
    if (!pc) return NULL;
    ptrList_append(&peer->readCache, pc);

    return pc;
}

void peer_releaseReadCache(struct peer *peer, struct piece *pc)
{
    ptrList_remove_data(&peer->readCache, pc);
    piece_free(pc);
}

static struct piece *peer_getPieceData(struct peer *peer, struct file *file, UINT32 piece)
{
    struct piece *pc;
    struct ptrList *li;

    for (li=peer->readCache; li; li=li->next)
    {
        pc = (struct piece *)li->data;
        if (pc->file==file && pc->piece==piece) return pc;
    }

    pc = task_getPieceData(peer->task, file, piece, peer);
    if (pc) return peer_addReadCache(peer, file, piece, pc->data, pc->dataLen);

    return NULL;
}

static BOOL peer_doSendPiece1(struct peer *peer)
{
    struct task *task = peer->task;
    struct piece *pc;
    DWORD currTick;
    INT64 lastSent, canSendPerSec, canSendThisTime, sentSuccess;
    struct transfer trans;
    struct request *rq;
    time_t currTime;

    if (!peer->peerRequests) return FALSE;

    currTick = GetTickCount();
    canSendThisTime = MAXINT64;
    if (g_options.upLimit)
    {
        rateCtrl_getTransfers(&g_rcUp, currTick, 1000, &trans);
        lastSent = (UINT32)trans.bytes;
        canSendPerSec = g_options.upLimit * 1024;
        canSendThisTime = min(canSendThisTime,
            (lastSent >= canSendPerSec) ? 0 : (canSendPerSec - lastSent));
    }
    if (task->upLimit)
    {
        rateCtrl_getTransfers(&task->rcUp, currTick, 1000, &trans);
        lastSent = (UINT32)trans.bytes;
        canSendPerSec = task->upLimit * 1024;
        canSendThisTime = min(canSendThisTime,
            (lastSent >= canSendPerSec) ? 0 : (canSendPerSec - lastSent));
    }
    if (canSendThisTime < 1024) return FALSE;

    rq = (struct request *)peer->peerRequests->data;

    pc = peer_getPieceData(peer, rq->file, rq->piece);
    if (!pc) return FALSE;
    sentSuccess = peer_sendPiece(peer, rq->file, rq->piece, rq->offset, rq->length, pc->data);
    if (sentSuccess && (rq->offset==MAXUINT32 || rq->offset+rq->length >= (UINT32)pc->dataLen))
        peer_releaseReadCache(peer, pc);
    if (!sentSuccess) return FALSE;

    rateCtrl_updateTransfer(&peer->rcUp, currTick, rq->length);
    rateCtrl_updateTransfer(&task->rcUp, currTick, rq->length);
    rateCtrl_updateTransfer(&g_rcUp, currTick, rq->length);
    time(&currTime);
    speed_update(&peer->speedUp, currTime, rq->length);

    request_free(rq);
    ptrList_pop_front(&peer->peerRequests);

    return TRUE;
}

void peer_doSendPiece(struct peer *peer)
{
    int sendingSize, bufferedSize;

    while (1)
    {
        if (!peer_doSendPiece1(peer)) break;

        if (!tcp_getSendStat(peer->sock, &sendingSize, &bufferedSize) ||
            bufferedSize > 6*1024*1024)
            break;
    }
}

__inline static BOOL peer_processCmdChoke(struct peer *peer)
{
    struct task *task = peer->task;

    if (task)
    {
        peer->isPeerChokingToMe = TRUE;
        peer_cancelRequests(peer, MAXUINT32);
    }
    return TRUE;
}

__inline static BOOL peer_processCmdUnchoke(struct peer *peer)
{
    struct task *task = peer->task;

    if (task)
    {
        peer->isPeerChokingToMe = FALSE;
        if (peer->amInterestingToPeer) peer_doPieceRequest(peer);
    }
    return TRUE;
}

__inline static BOOL peer_processCmdInterested(struct peer *peer)
{
    peer->isPeerInterestingToMe = TRUE;
    return TRUE;
}

__inline static BOOL peer_processCmdNotInterested(struct peer *peer)
{
    peer->isPeerInterestingToMe = FALSE;
    if (!peer->amChokingToPeer)
    {
        struct task *task = peer->task;
        task->unchokedPeerCount --;
        peer_sendChoke(peer);
    }
    return TRUE;
}

static void peer_checkDiff(struct peer *peer)
{
    struct task *task = peer->task;
    struct bitset diff = { 0 };

    if (!(task->action & (TS_DOWNLOADING|TS_UPDATING)) ||
        task->bytesDownloaded + task->tmp.bytesDownloaded >= task->idx.bytes)
    {
        peer_sendNotInterested(peer);
        return;
    }

    bitset_copy(&diff, &peer->bitset);
    if (task->action & TS_UPDATING && task->tmp.bitset.bits)
    {
        struct bitset tmp = { 0 };
        bitset_copy(&tmp, &task->bitset);
        bitset_or(&tmp, &task->tmp.bitset);
        bitset_diff(&diff, &tmp);
        bitset_free(&tmp);
    }
    else
        bitset_diff(&diff, &task->bitset);

    if (bitset_countTrueBits(&diff) > 0)
    {
        if (!peer->amInterestingToPeer) peer_sendInterested(peer);
        if (!peer->isPeerChokingToMe) peer_doPieceRequest(peer);
    }
    else if (peer->amInterestingToPeer)
    {
        if (peer->requests)
        {
            struct request *rq = (struct request *)peer->requests->data;
            debugf("checkDiff sendNotInterested %s, BUT req: %d, %u/%u %d/%d\r\n",
                peer->szDesc, rq->file->pieceOffset+rq->piece,
                bitset_countTrueBits(&peer->bitset), bitset_countTrueBits(&task->bitset),
                bitset_check(&peer->bitset, rq->file->pieceOffset+rq->piece),
                bitset_check(&task->bitset, rq->file->pieceOffset+rq->piece));
        }
        peer_sendNotInterested(peer);
    }

    bitset_free(&diff);
}

static BOOL peer_processCmdHave(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;
    UINT32 piece;

    if (dataLen != 13) return FALSE;

    piece = (UINT32)ntohl(*((UINT32 *)(data+5)));
    peer->havePieces = (UINT32)ntohl(*((UINT32 *)(data+9)));
    if (0==bitset_check(&peer->bitset, piece))
    {
        bitset_set(&peer->bitset, piece);
        peer_checkDiff(peer);
    }

    return TRUE;
}

static BOOL peer_processCmdBitField(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;

    if (dataLen-6 != task->bitset.byteCount && dataLen-6 != 0) return FALSE;

    peer->status |= PEER_BITFIELDED;

    bitset_init(&peer->bitset, task->idx.pieceCount);
    switch (*(data+5))
    {
    case 0:
        memcpy(peer->bitset.bits, data+6, dataLen-6);
        break;
    case 1:
        bitset_setAll(&peer->bitset);
        break;
    case 2:
        bitset_clearAll(&peer->bitset);
        break;
    }

    peer->havePieces = bitset_countTrueBits(&peer->bitset);

    peer_checkDiff(peer);

    return TRUE;
}

static BOOL peer_processCmdRequest(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;
    struct file *file;
    struct request request, *rqNew;

    if (dataLen != 17) return FALSE;

    if (peer->amChokingToPeer) return FALSE;

    request.piece = (UINT32)ntohl(*((UINT32 *)(data + 5)));
    request.offset = (UINT32)ntohl(*((UINT32 *)(data + 9)));
    request.length = (UINT32)ntohl(*((UINT32 *)(data + 13)));

    if (0 == bitset_check(&task->bitset, request.piece) &&
        0 == bitset_check(&task->tmp.bitset, request.piece))
    {
        debugf("peer request non exist piece: %d %s\r\n", request.piece, peer->szDesc);
        return FALSE;
    }

    file = task_getFileInfo(task, request.piece);
    if (!file) return FALSE;

    request.piece -= file->pieceOffset;

    if (ptrList_size(peer->peerRequests) > MAX_PENDING_REQUESTS*2) return FALSE;

    rqNew = (struct request *)malloc(sizeof(struct request));
    if (!rqNew) return FALSE;
    memset(rqNew, 0, sizeof(struct request));
    rqNew->file = file;
    rqNew->piece = request.piece;
    rqNew->offset = request.offset;
    rqNew->length = request.length;
    ptrList_append(&peer->peerRequests, rqNew);

    peer_doSendPiece(peer);

    return TRUE;
}

static BOOL peer_isRequestCompleted(struct peer *peer, struct file *file, UINT32 piece)
{
    struct request *rq;
    struct ptrList *list;

    for (list=peer->requests; list; list=list->next)
    {
        rq = (struct request *)list->data;
        if (rq->file == file &&
            rq->piece == piece &&
            rq->dataLen != (int)rq->length)
            return FALSE;
    }
    return TRUE;
}

void peer_sendHaveToPeers(struct task *task, UINT32 piece)
{
    struct ptrList *li;
    struct peer *pr;

    for (li=task->peersOutgoing; li; li=li->next)
    {
        pr = (struct peer *)li->data;
        if (pr->status & PEER_BITFIELDED)
        {
            peer_cancelRequests(pr, piece);
            peer_sendHave(pr, piece, task->piecesDownloaded+task->tmp.piecesDownloaded);
        }
    }
    for (li=task->peersIncoming; li; li=li->next)
    {
        pr = (struct peer *)li->data;
        if (pr->status & PEER_BITFIELDED)
        {
            peer_cancelRequests(pr, piece);
            peer_sendHave(pr, piece, task->piecesDownloaded+task->tmp.piecesDownloaded);
        }
    }
}

static BOOL peer_onPieceCompleted(struct task *task, struct peer *peer,
                                  struct file *file, UINT32 piece,
                                  const UCHAR *pieceData, int pieceDataLen)
{
    UCHAR sha1Data[20];

    sha1(pieceData, pieceDataLen, sha1Data);
    if (memcmp(file->hash+20*piece, sha1Data, 20))
    {
        debugf("[CmdPiece] ERROR piece hash mismatch: %s #%d\r\n",
            peer->szDesc, file->pieceOffset+piece);
        peer_cancelRequests(peer, file->pieceOffset+piece);
        debugf("peer_onPieceCompleted %s cancelRequests[%u]\r\n", peer->szDesc, file->pieceOffset+piece);
        bitset_clear(&task->bitsetRequested, file->pieceOffset+piece);
        return FALSE;
    }

    task_setPieceData(task, file, piece, pieceData, pieceDataLen);

    //debugf("%s RequestCompleted: #%d requested:%d/%d, pending:%d\r\n",
    //    task->idx.id, file->pieceOffset+piece,
    //    bitset_countTrueBits(&task->bitsetRequested),
    //    task->idx.pieceCount, ptrList_size(peer->requests));

    return TRUE;
}

static BOOL peer_processCmdPiece(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;
    UINT32 piece, offset, dataSize, pieceLen;
    struct file *file;
    DWORD currTick;
    time_t currTime;

    piece = (UINT32)ntohl(*((UINT32 *)(data + 5)));
    offset = (UINT32)ntohl(*((UINT32 *)(data + 9)));

    dataSize = (UINT32)(dataLen - 13);

    currTick = GetTickCount();
    rateCtrl_updateTransfer(&peer->rcDown, currTick, dataSize);
    rateCtrl_updateTransfer(&task->rcDown, currTick, dataSize);
    rateCtrl_updateTransfer(&g_rcDown, currTick, dataSize);
    time(&currTime);
    speed_update(&peer->speedDown, currTime, dataSize);

    file = task_getFileInfo(task, piece);
    if (!file)
    {
        debugf("Error OnPieceData cannot locate file! %s piece:%u offset: %u length: %u\r\n",
            peer->szDesc, piece, offset, dataSize);
        bitset_clear(&task->bitsetRequested, piece);
        return FALSE;
    }

    piece -= file->pieceOffset;
    pieceLen = task_getPieceLength(task, file, piece);

    if (offset == MAXUINT32) // 全部的piece数据
    {
        if (dataSize != pieceLen) return FALSE;
        if (!peer_onPieceCompleted(task, peer, file, piece, data+13, dataSize)) return FALSE;
    }
    else
    {
        BOOL pieceDataAdded;
        struct ptrList *list;
        struct request *rq;

        for (pieceDataAdded=FALSE, list=peer->requests; list; list=list->next)
        {
            rq = (struct request *)list->data;
            if (rq->file == file &&
                rq->piece == piece &&
                rq->offset == offset)
            {
                if (!rq->requestTime)
                    debugf("Warning OnPieceData requestTime==0 %s piece:%u offset: %u length %u/%u\r\n",
                        peer->szDesc, file->pieceOffset+piece, offset, rq->length, dataSize);
                if (rq->length != dataSize)
                {
                    debugf("Error OnPieceData dataSize! %s piece:%u offset: %u length %u/%u\r\n",
                        peer->szDesc, file->pieceOffset+piece, offset, rq->length, dataSize);
                    break;
                }
                if (rq->data)
                {
                    debugf("Error OnPieceData already has data! %s piece:%u offset: %u length %u/%u\r\n",
                        peer->szDesc, file->pieceOffset+piece, offset, rq->length, dataSize);
                    break;
                }
                rq->dataLen = dataSize;
                rq->data = (UCHAR *)malloc(dataSize);
                memcpy(rq->data, data+13, dataSize);
                pieceDataAdded = TRUE;
                break;
            }
        }
        if (!pieceDataAdded)
        {
            debugf("[CmdPiece] ERROR(not added): %s piece:%u offset: %u length %u [%d/%d]\r\n",
                peer->szDesc, file->pieceOffset+piece, offset, dataSize,
                bitset_check(&peer->bitset, file->pieceOffset+piece),
                bitset_check(&task->bitset, file->pieceOffset+piece));
            bitset_clear(&task->bitsetRequested, file->pieceOffset+piece);
            return TRUE;
        }

        if (peer_isRequestCompleted(peer, file, piece))
        {
            UCHAR *pieceData;
            int pieceDataLen;

            pieceData = (UCHAR *)malloc(task->idx.pieceLength);
            pieceDataLen = 0;
            for (list=peer->requests; list; list=list->next)
            {
                rq = (struct request *)list->data;
                if (rq->file == file && rq->piece == piece)
                {
                    memcpy(pieceData+pieceDataLen, rq->data, rq->dataLen);
                    pieceDataLen += (int)rq->dataLen;
                }
            }
            if (!peer_onPieceCompleted(task, peer, file, piece, pieceData, pieceDataLen))
            {
                free(pieceData);
                return FALSE;
            }

            free(pieceData);
        }
    }

    peer_doPieceRequest(peer);

    return TRUE;
}

static BOOL peer_processCmdCancel(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;
    struct file *file;
    UINT32 piece, offset, len;
    struct request *rq;
    struct ptrList *li, *liDel;

    if (dataLen != 17) return FALSE;

    piece = (UINT32)ntohl(*((UINT32 *)(data + 5)));
    offset = (UINT32)ntohl(*((UINT32 *)(data + 9)));
    len = (UINT32)ntohl(*((UINT32 *)(data + 13)));

    file = task_getFileInfo(task, piece);
    if (!file) return FALSE;

    piece -= file->pieceOffset;

    li = peer->peerRequests;
    while (li)
    {
        rq = (struct request *)li->data;
        if (rq->file == file && rq->piece == piece)
        {
            request_free(rq);
            liDel = li; li = li->next;
            ptrList_remove_node(&peer->requests, liDel);
            break;
        }
        else
            li = li->next;
    }

    return TRUE;
}

static BOOL peer_processCmdRecommend(struct peer *peer, const u_char* data, size_t dataLen)
{
    struct task *task = peer->task;
    UINT32 piece, i, j;
    UCHAR count;

    if (dataLen != 10 && dataLen != 46) return FALSE;

    count = *(data+5);

    for (i=0; i<count; i++)
    {
        piece = ntohl(*((UINT32 *)(data+6+4*i)));

        if (1 == bitset_check(&task->bitset, piece) ||
            1 == bitset_check(&task->tmp.bitset, piece))
            continue;

        for (j=0; j<10; j++)
        {
            if (peer->recommendedPieces[j] == MAXUINT32)
            {
                peer->recommendedPieces[j] = piece;
                break;
            }
        }
    }

    return TRUE;
}

static BOOL peer_processCmdLimitSpeed(struct peer *peer, const u_char* data, size_t dataLen)
{
    if (dataLen != 13) return FALSE;

    peer->downLimit = ntohl(*((UINT32 *)(data+5)));
    peer->upLimit = ntohl(*((UINT32 *)(data+9)));

    return TRUE;
}

static struct peer *peer_processIncomingHandShake(int sockIdx, UCHAR *data)
{
    struct peer *peer;
    struct task *task;
    UINT16 port, btVer;
    struct tcp_info tsi;
    CHAR hash[MAX_HASH_LEN], szDesc[256];

    tcp_getInfo(sockIdx, &tsi);
    sprintf_s(szDesc, 256, "[sock %d] %s:%d,", sockIdx, tsi.peerAddr.ip, tsi.peerAddr.port);

    btVer = ntohs(*((UINT16 *)(data+5)));
    if (btVer != VERSION) return NULL;

    port = ntohs(*((UINT16 *)(data+7)));

    if (strlen((CHAR *)(data+9)) >= MAX_HASH_LEN) return NULL;
    strcpy_s(hash, MAX_HASH_LEN, (CHAR *)(data+9));
    task = task_findHash(hash);
    if (!task)
    {
        debugf("[IncomingHandShake] ERROR no torrent %s %s\r\n", szDesc, hash);
        return NULL;
    }
    if (task->action & TS_STOP_UPLOAD)
    {
        debugf("[IncomingHandShake] ERROR torrent NOT working %s %p\r\n", szDesc, task->idx.id);
        return NULL;
    }

    if (ptrList_size(task->peersIncoming) >= (int)g_options.maxUpPeersPerTask)
    {
        debugf("[IncomingHandShake] ERROR torrent busy: %s %s(max:%d)\r\n",
            szDesc, task->idx.id, ptrList_size(task->peersIncoming));
        return NULL;
    }

    peer = peer_new();
    peer->task = task;
    peer->sock = sockIdx;
    peer->isOutgoing = 0;
    peer->status = PEER_HANDSHAKED;
    strcpy_s(peer->ip, MAX_IP_LEN, tsi.peerAddr.ip);
    peer->port = port;

    ptrList_append(&task->peersIncoming, peer);

    peer_getDesc(peer);
    time(&peer->connectTime);
    time(&peer->lastReceiveTime);
    strcpy_s(peer->pid, MAX_PID_LEN, (CHAR *)(data+41));
    peer->userPrvc = *(data+73);
    peer->userType = *(data+74);
    peer->userAttr = *(data+75);
    peer->lineType = *(data+76);
    peer->lineSpeed = *(data+77);
    peer->downLimit = ntohl(*((UINT32 *)(data+78)));
    peer->upLimit = ntohl(*((UINT32 *)(data+82)));

    peer_sendHandShake(peer);
    peer_sendBitField(peer);
    peer_sendRecommend(peer, MAXUINT32);

    tcp_setUserData(sockIdx, peer);
    return peer;
}

static BOOL peer_processOutgoingHandShake(int sockIdx, struct peer *peer, u_char *data)
{
    struct task *task = peer->task;

    if (strcmp((CHAR *)(data+9), task->idx.hash)) return FALSE;
    if (strcmp((CHAR *)(data+41), peer->pid)) return FALSE;

    peer->userPrvc = *(data+73);
    peer->userType = *(data+74);
    peer->userAttr = *(data+75);
    peer->lineType = *(data+76);
    peer->lineSpeed = *(data+77);
    peer->downLimit = ntohl(*((UINT32 *)(data+78)));
    peer->upLimit = ntohl(*((UINT32 *)(data+82)));

    time(&peer->lastReceiveTime);
    peer->status |= PEER_HANDSHAKED;
    peer_sendBitField(peer);

    return TRUE;
}

void peer_getSpeed(struct peer *peer, struct speed *speed)
{
    DWORD currTick = GetTickCount();

    speed->down = rateCtrl_getSpeed(&peer->rcDown, currTick);
    speed->up = rateCtrl_getSpeed(&peer->rcUp, currTick);
}

// -------------------------------------------------------------------------------------------------------
//
void peer_OnWritable(int sockIdx)
{
    struct peer *peer;

    peer = (struct peer *)tcp_getUserData(sockIdx);
    if (!peer || !(peer->status & PEER_BITFIELDED)) return;

    if (peer->amInterestingToPeer && !peer->isPeerChokingToMe) peer_doPieceRequest(peer);
    if (!peer->amChokingToPeer) peer_doSendPiece(peer);
}

void peer_OnConnect(int sockIdx)
{
    struct peer *peer;

    peer = (struct peer *)tcp_getUserData(sockIdx);
    if (!peer) return;

    time(&peer->connectTime);
    time(&peer->lastReceiveTime);
    peer->status |= PEER_CONNECTED;
    peer_sendHandShake(peer);
}

void peer_onCloseCancelRequests(struct peer *peer)
{
    struct task *task = peer->task;
    struct ptrList *list;
    struct request *rq;

    if (!(peer->status & PEER_BITFIELDED)) return;

    for (list=peer->requests; list; list=list->next)
    {
        rq = (struct request *)list->data;
        bitset_clear(&task->bitsetRequested, rq->file->pieceOffset+rq->piece);
    }
}

static void peer_OnClose(int sockIdx)
{
    struct peer *peer;
    struct task *task;
    struct tcp_info tcpInfo;

    if (g_adminSocket >= 0 && sockIdx == g_adminSocket)
    {
        g_adminSocket = -1;
        ptrArray_removeSorted(&g_adminSockets, (void *)sockIdx);
        return;
    }

    if (ptrArray_removeSorted(&g_adminSockets, (void*)sockIdx))
        return;

    tcp_getInfo(sockIdx, &tcpInfo);
    if (tcpInfo.type == TCP_TYPE_LISTEN)
    {
        debugf("listen socket(#%d) closed\r\n", sockIdx);
        return;
    }

    peer = (struct peer *)tcp_getUserData(sockIdx);
    if (!peer) return;
    task = peer->task;

    tcp_setUserData(sockIdx, 0);
    if (!peer->amChokingToPeer) task->unchokedPeerCount --;
    peer_onCloseCancelRequests(peer);
    ptrList_remove_data(&task->peersIncoming, peer);
    ptrList_remove_data(&task->peersOutgoing, peer);
    peer->sock = -1;
    peer_delete(peer);
}

static void peer_OnAccept(int sockIdx)
{
}


static int peer_OnMsgReceived(int sockIdx, UCHAR *msgData, int msgLen)
{
    struct peer *peer;
    struct task *task;
    int cmdLen;
    BOOL success;

    if (!msgData || msgLen < 4) return 0;

    cmdLen = 4 + (int)ntohl(*((UINT32 *)msgData));

    if (cmdLen < 4) return -1;
    if (cmdLen > 4096 + 4*1024*1024) return -1;
    if (cmdLen > msgLen)
    { tcp_setExpectPackLen(sockIdx, cmdLen); return 0; }

    if (msgLen > 4 && msgData[4] == CMD_ADMIN) // UI指令
    {
        if (msgData[5] == GM_NEED_SEED)
            admin_onNeedSeed((CHAR *)(msgData+10));
        else if (msgData[5] == GM_NEED_UPDATE)
            admin_onNeedUpdate((CHAR *)(msgData+10));
        else
        {
            ptrArray_insertSorted(&g_adminSockets, (void *)sockIdx);
            admin_processCmd(sockIdx, msgData, cmdLen);
        }
        return cmdLen;
    }

    if (msgData[4] == CMD_PING)
    {
        tcp_send(sockIdx, msgData, cmdLen, NULL, 0);
        return cmdLen;
    }

    peer = (struct peer *)tcp_getUserData(sockIdx);

    if (msgLen == 4) // keep alive
    {
        if (peer) time(&peer->lastReceiveTime);
        return cmdLen;
    }

    if (!peer)
    {
        if (msgData[4] != CMD_HANDSHAKE) return -1;
        return (peer_processIncomingHandShake(sockIdx, msgData) ? cmdLen : -1);
    }

    time(&peer->lastReceiveTime);

    if (!(peer->status & PEER_HANDSHAKED))
    {
        if (msgData[4] != CMD_HANDSHAKE) return -1;
        if (!peer_processOutgoingHandShake(sockIdx, peer, msgData)) return -1;
        else return cmdLen;
    }

    task = peer->task;

    switch (msgData[4])
    {
    case CMD_CHOKE:         success = peer_processCmdChoke(peer); break;
    case CMD_UNCHOKE:       success = peer_processCmdUnchoke(peer); break;
    case CMD_INTERESTED:    success = peer_processCmdInterested(peer); break;
    case CMD_NOTINTERESTED: success = peer_processCmdNotInterested(peer); break;
    case CMD_HAVE:          success = peer_processCmdHave(peer, msgData, cmdLen); break;
    case CMD_BITFIELD:      success = peer_processCmdBitField(peer, msgData, cmdLen); break;
    case CMD_REQUEST:       success = peer_processCmdRequest(peer, msgData, cmdLen); break;
    case CMD_PIECE:         success = peer_processCmdPiece(peer, msgData, cmdLen); break;
    case CMD_CANCEL:        success = peer_processCmdCancel(peer, msgData, cmdLen); break;
    case CMD_RECOMMEND:     success = peer_processCmdRecommend(peer, msgData, cmdLen); break;
    case CMD_LIMITSPEED:    success = peer_processCmdLimitSpeed(peer, msgData, cmdLen); break;
    default:
        success = FALSE;
        debugf("[processReceivedData] nuknown cmd: %d sock=%d\r\n", msgData[4], peer->sock);
        break;
    }

    if (!success)
    {
        debugf("cmd process error: %d %s\r\n", msgData[4], peer->szDesc);
        peer_onCloseCancelRequests(peer);
        ptrList_remove_data(&task->peersOutgoing, peer);
        ptrList_remove_data(&task->peersIncoming, peer);
        peer_delete(peer);
        return -1;
    }

    return cmdLen;
}

int CALLBACK peer_OnTcpEvent(int sockIdx, int msgCode, UCHAR *msgData, int msgLen)
{
    switch (msgCode)
    {
    case TCP_EV_SEND:
        peer_OnWritable(sockIdx);
        break;

    case TCP_EV_RECEIVE:
        return peer_OnMsgReceived(sockIdx, msgData, msgLen);

    case TCP_EV_ACCEPT:
        peer_OnAccept(sockIdx);
        break;

    case TCP_EV_CONNECT:
        peer_OnConnect(sockIdx);
        break;

    case TCP_EV_CLOSE:
        peer_OnClose(sockIdx);
        break;

    case TCP_EV_TIMER:
        task_OnTcpTimer();
        break;

    case TCP_EV_CUSTOM:
        task_OnTcpCustom((struct tcp_custom *)msgData);
        break;
    }

    return 0;
}

BOOL peer_connect(struct peer *peer)
{
    peer->sock = tcp_connect(peer->ip, peer->port, NULL);
    if (peer->sock < 0) return FALSE;

    tcp_setUserData(peer->sock, peer);
    peer_getDesc(peer);

    return TRUE;
}

