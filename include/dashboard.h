#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "config.h"
#include "dashboard_obs.h"
#include <stddef.h>

#define DASHBOARD_LEADERS 5
typedef struct {
    char name[128];
    long long balance;
} DashboardLeader;

/* Snapshot supplied by the bot main loop; no private Twitch tokens. */
typedef struct
{
    int live, viewers, chat_connected, ai_enabled, promo_enabled;
    int reports_enabled, obs_enabled, peak_observed;
    unsigned int promo_min, promo_max;
    size_t queue_count, profile_count;
    unsigned long uptime_seconds;
    char broadcaster[128], game[256], title[512], track[256];
    DashboardObs obs_state;
    DashboardLeader leaders[DASHBOARD_LEADERS];
    size_t leader_count;
    unsigned int obs_result_sequence;
    char obs_result[160];
} DashboardStatus;

typedef struct
{
    int ai, promo, reports; /* -1 = unchanged, 0 = disable, 1 = enable */
    int obs_action;  /* 0: none; 1: scene, 2: start record, 3: stop record */
    char obs_scene[DASH_OBS_SCENE_LENGTH];
} DashboardChanges;

/* HTTP server on loopback only. Returns 0 if disabled/unavailable. */
int dashboard_start(int enabled, unsigned short port);
void dashboard_stop(void);
void dashboard_publish(const DashboardStatus *status);
/* Process pending UI changes exclusively on the bot's main thread. */
int dashboard_take_changes(DashboardChanges *changes);

/* Preference file stores ONLY the three booleans, never OAuth credentials.
 * Saved UI values override config.ini at the next process startup. */
void dashboard_load_preferences(AppConfig *config);
int dashboard_save_preferences(const AppConfig *config);

#endif
