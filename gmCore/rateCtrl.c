#include <winsock2.h>
#include <windows.h>
#include "rateCtrl.h"

void rateCtrl_init(struct rate_control *rc, int history, int interval)
{
    if (rc->transfers) free(rc->transfers);
    memset(rc, 0, sizeof(struct rate_control));
    if (history < 1000) history = 1000;
    if (interval < 10) interval = 100;
    rc->history = history;
    rc->interval = interval;
    rc->transferSize = history / interval;
    rc->transfers = (struct transfer *)calloc(rc->transferSize, sizeof(struct transfer));
}

void rateCtrl_uninit(struct rate_control *rc)
{
    if (rc->transfers)
    {
        free(rc->transfers);
        rc->transfers = NULL;
    }
    memset(rc, 0, sizeof(struct rate_control));
}

void rateCtrl_reset(struct rate_control *rc)
{
    int i;

    for (i=0; i<rc->transferSize; i++)
    {
        rc->transfers[i].bytes = 0;
        rc->transfers[i].time = 0;
    }
    rc->newest = 0;
}

void rateCtrl_updateTransfer(struct rate_control *rc, DWORD currTick, UINT64 bytes)
{
    int i, idx;

    currTick /= rc->interval;

    if (bytes)
    {
        for (i=0,idx=-1; i<rc->transferSize; i++)
        { if (rc->transfers[i].time == currTick) { idx = i; break; } }
        if (idx < 0)
        {
            idx = rc->newest;
            rc->transfers[idx].time = currTick;
            rc->transfers[idx].bytes = 0;
            rc->newest ++;
            if (rc->newest == rc->transferSize)
                rc->newest = 0;
        }
        rc->transfers[idx].bytes += bytes;
    }
}

UINT32 rateCtrl_getSpeed(struct rate_control *rc, DWORD currTick)
{
    DWORD startTime;
    UINT64 bytes;
    int i;

    currTick /= rc->interval;

    for (i=0, bytes=0, startTime=0xFFFFFFFF; i<rc->transferSize; i++)
    {
        if (rc->transfers[i].time >= (currTick - rc->transferSize))
        {
            bytes += rc->transfers[i].bytes;
            startTime = min(startTime, rc->transfers[i].time);
        }
    }
    if (currTick > startTime)  /* bytes per second */
        return (UINT32)((bytes * 1000) / ((currTick - startTime) * rc->interval));
    else
        return 0;
}

void rateCtrl_getTransfers(struct rate_control *rc, DWORD currTick,
                           int interval, struct transfer *tr)
{
    int i;

    tr->time = 0xFFFFFFFF;
    tr->bytes = 0;

    currTick /= rc->interval;
    interval /= rc->interval;

    for (i=0; i<rc->transferSize; i++)
    {
        if (rc->transfers[i].time >= currTick - interval)
        {
            tr->bytes += rc->transfers[i].bytes;
            tr->time = min(tr->time, rc->transfers[i].time);
        }
    }
}

