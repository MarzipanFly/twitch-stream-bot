#ifndef OBS_DIRECTOR_H
#define OBS_DIRECTOR_H

#include <windows.h>
#include "config.h"
#include "obs_websocket.h"

typedef struct
{
    int active;
    int state_known;
    int window_setting_known;
    int last_game_running; /* -1 unknown, 0 absent, 1 running */
    ObsWebSocket obs;
    DWORD last_poll_tick;
    DWORD last_connect_attempt_tick;
    DWORD last_error_tick;
    DWORD manual_override_until_tick;
    DWORD game_present_since_tick;
    DWORD game_absent_since_tick;
    char last_seen_scene[CONFIG_STRING_SIZE];
    char last_director_scene[CONFIG_STRING_SIZE];
    char last_window_setting[CONFIG_STRING_SIZE];
} ObsDirector;

/* Starts the director only when [obs_director] is explicitly enabled. */
BotResult obs_director_start(const AppConfig *config, ObsDirector *director);
/* Periodically switches only between configured chat/game scenes. */
void obs_director_tick(const AppConfig *config, ObsDirector *director);
void obs_director_stop(ObsDirector *director);

#endif
