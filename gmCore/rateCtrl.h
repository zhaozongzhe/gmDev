#ifndef _RATECTRL_H_
#define _RATECTRL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <malloc.h>
#include <windows.h>

struct transfer
{
    DWORD time;
    UINT64 bytes;
};

struct rate_control
{
    int history;  /* ms */
    int interval;
    int transferSize;
    struct transfer *transfers;
    int newest;
};

void rateCtrl_init(struct rate_control *rc, int history, int interval);
void rateCtrl_uninit(struct rate_control *rc);
void rateCtrl_reset(struct rate_control *rc);

void rateCtrl_updateTransfer(struct rate_control *rc, DWORD currTick, UINT64 size);
UINT32 rateCtrl_getSpeed(struct rate_control *rc, DWORD currTick);
void rateCtrl_getTransfers(struct rate_control *rc, DWORD currTick, int interval, struct transfer *tr);

#ifdef __cplusplus
}
#endif

#endif

