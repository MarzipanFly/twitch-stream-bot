#ifndef CATEGORY_SYNC_H
#define CATEGORY_SYNC_H

#include <windows.h>

#include "config.h"
#include "twitch_auth.h"
#include "obs_websocket.h"

typedef struct
{
    int active;
    int auth_pending;
    int auth_poll_interval_seconds;
    DWORD auth_started_tick;
    DWORD auth_next_poll_tick;
    TwitchDeviceCode pending_device;
    ObsWebSocket obs;
    TwitchConfig broadcaster_twitch;
    DWORD last_poll_tick;
    DWORD last_obs_connect_attempt_tick;
    DWORD last_source_error_tick;
    DWORD last_chat_lookup_tick;
    DWORD last_game_lookup_tick;
    DWORD last_category_update_tick;
    char last_category_id[64];
    char chat_category_id[64];
    char last_game_query[CONFIG_STRING_SIZE];
    char last_game_category_id[64];
    char last_game_category_name[CONFIG_STRING_SIZE];
} CategorySync;

BotResult category_sync_start(const AppConfig *config, CategorySync *sync);
void category_sync_tick(const AppConfig *config, CategorySync *sync);
void category_sync_stop(CategorySync *sync);

#endif
