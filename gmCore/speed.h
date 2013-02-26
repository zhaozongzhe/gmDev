#ifndef _SPEED_H_
#define _SPEED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <malloc.h>
#include <windows.h>

struct speed_item
{
    time_t time;
    UINT64 bytes;
};

struct speed_data
{
    int history;  /* sec */
    int interval;
    int itemSize;
    struct speed_item *items;
    int newest;
};

void speed_init(struct speed_data *sd, int history, int interval);
void speed_uninit(struct speed_data *sd);
void speed_reset(struct speed_data *sd);

void speed_update(struct speed_data *sd, time_t currTiime, UINT64 size);
UINT32 speed_getSpeed(struct speed_data *sd, time_t currTiime);

#ifdef __cplusplus
}
#endif

#endif

