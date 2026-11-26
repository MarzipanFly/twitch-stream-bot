#ifndef CHANNEL_POINTS_H
#define CHANNEL_POINTS_H
#include "config.h"
#include "twitch_eventsub.h"
#include <windows.h>
#include <stddef.h>
#define CHANNEL_POINTS_QUEUE_CAPACITY 64
typedef struct
{
    HANDLE thread;
    HANDLE stop_event;
    CRITICAL_SECTION queue_lock;
    int queue_lock_initialized;
    TwitchConfig auth_config;
    TwitchChannelPointsRedemption queue[CHANNEL_POINTS_QUEUE_CAPACITY];
    size_t queue_head;
    size_t queue_count;
    unsigned long dropped;
} ChannelPointsWorker;
BotResult channel_points_start(const AppConfig *config, ChannelPointsWorker *worker);
int channel_points_pop(ChannelPointsWorker *worker, TwitchChannelPointsRedemption *redemption);
void channel_points_stop(ChannelPointsWorker *worker);
#endif
