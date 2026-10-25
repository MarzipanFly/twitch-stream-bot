#ifndef DASHBOARD_OBS_H
#define DASHBOARD_OBS_H

#include "obs_websocket.h"
#include <stddef.h>

#define DASH_OBS_MAX_SCENES 24
#define DASH_OBS_SCENE_LENGTH 256
#define DASH_OBS_MAX_AUDIO 8

typedef struct
{
    int connected;
    int recording;          /* -1 unknown, 0 inactive, 1 active */
    int streaming;          /* -1 unknown, 0 inactive, 1 active */
    size_t scene_count;
    size_t audio_count;
    struct {char name[256]; int muted; double volume;} audio[DASH_OBS_MAX_AUDIO];
    char scene[DASH_OBS_SCENE_LENGTH];
    char scenes[DASH_OBS_MAX_SCENES][DASH_OBS_SCENE_LENGTH];
} DashboardObs;

/* GetCurrentProgramScene, GetSceneList, GetRecordStatus, GetStreamStatus.
 * All OBS connections stay on the bot main thread; no passwords in snapshots. */
int dashboard_obs_refresh(const char *password, DashboardObs *out);
/* Confirms requested scene belongs to the current OBS scene list. */
int dashboard_obs_switch_scene(const char *password,
                               const char *scene_name);
/* Stops recording only after explicitly verifying GetRecordStatus. */
int dashboard_obs_record(const char *password, int start);
int dashboard_obs_audio(const char *password, const char *name,
                        int set_mute, int mute, double volume);

#endif
