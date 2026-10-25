#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "config.h"
#include <stddef.h>

/* Snapshot supplied by the bot main loop; no private Twitch tokens. */
typedef struct
{
    int live, viewers, chat_connected, ai_enabled, promo_enabled;
    int reports_enabled, obs_enabled, peak_observed;
    unsigned int promo_min, promo_max;
    size_t queue_count, profile_count;
    unsigned long uptime_seconds;
    char broadcaster[128], game[256], title[512], track[256];
    char top_viewers[1500];
} DashboardStatus;

typedef struct
{
    int ai, promo, reports; /* -1 = unchanged, 0 = disable, 1 = enable */
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
