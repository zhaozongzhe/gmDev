#include <winsock2.h>
#include <windows.h>
#include "speed.h"

void speed_init(struct speed_data *sd, int history, int interval)
{
    if (sd->items) free(sd->items);
    memset(sd, 0, sizeof(struct speed_data));
    if (history < 60) history = 60;
    if (interval < 2) interval = 2;
    sd->history = history;
    sd->interval = interval;
    sd->itemSize = history / interval;
    sd->items = (struct speed_item *)calloc(sd->itemSize, sizeof(struct speed_item));
}

void speed_uninit(struct speed_data *sd)
{
    if (sd->items)
    {
        free(sd->items);
        sd->items = NULL;
    }
    memset(sd, 0, sizeof(struct speed_data));
}

void speed_reset(struct speed_data *sd)
{
    int i;

    for (i=0; i<sd->itemSize; i++)
    {
        sd->items[i].bytes = 0;
        sd->items[i].time = 0;
    }
    sd->newest = 0;
}

void speed_update(struct speed_data *sd, time_t currTime, UINT64 bytes)
{
    int i, idx;

    currTime /= sd->interval;

    if (bytes)
    {
        for (i=0,idx=-1; i<sd->itemSize; i++)
        { if (sd->items[i].time == currTime) { idx = i; break; } }
        if (idx < 0)
        {
            idx = sd->newest;
            sd->items[idx].time = currTime;
            sd->items[idx].bytes = 0;
            sd->newest ++;
            if (sd->newest == sd->itemSize)
                sd->newest = 0;
        }
        sd->items[idx].bytes += bytes;
    }
}

UINT32 speed_getSpeed(struct speed_data *sd, time_t currTime)
{
    time_t startTime;
    UINT64 bytes;
    int i;

    currTime /= sd->interval;

    for (i=0, bytes=0, startTime=0xFFFFFFFF; i<sd->itemSize; i++)
    {
        if (sd->items[i].time >= (currTime - sd->itemSize))
        {
            bytes += sd->items[i].bytes;
            startTime = min(startTime, sd->items[i].time);
        }
    }
    if (currTime > startTime)
        return (UINT32)(bytes / ((currTime - startTime) * sd->interval));
    else
        return 0;
}

