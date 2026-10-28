#ifndef DASHBOARD_H
#define DASHBOARD_H

#include "config.h"
#include "dashboard_obs.h"
#include "dashboard_stats.h"
#include "music_queue.h"
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
    char ai_suggestion[512];
    unsigned int ai_suggestion_sequence, chat_idle_seconds;
    int ai_suggestion_pending;
    char ai_test_reply[800];
    unsigned int ai_test_sequence;
    unsigned int ai_test_history_count;
    int ai_test_pending;
    DashboardObs obs_state;
    DashboardLeader leaders[DASHBOARD_LEADERS];
    size_t leader_count;
    unsigned int obs_result_sequence;
    char obs_result[160];
    char telegram_url[CONFIG_STRING_SIZE],discord_url[CONFIG_STRING_SIZE];
    char telegram_chat_id[CONFIG_STRING_SIZE];
    unsigned int ai_cooldown, cooldown_normal, cooldown_game, cooldown_bruh;
    /* Editable integration settings exposed to the loopback-only settings page. */
    int obs_config_enabled, category_sync_enabled, obs_director_enabled;
    int director_auto_game, director_auto_chat;
    unsigned int category_poll_seconds, director_start_delay, director_stop_delay;
    unsigned int director_manual_override, director_poll_seconds;
    char category_chat_scene[CONFIG_STRING_SIZE], category_game_scene[CONFIG_STRING_SIZE];
    char category_chat_category[CONFIG_STRING_SIZE];
    char director_game_scene[CONFIG_STRING_SIZE], director_chat_scene[CONFIG_STRING_SIZE];
    char director_break_scene[CONFIG_STRING_SIZE];
    SoundConfig sounds[SOUND_MAX_COUNT];
    size_t sound_count;
    MusicTrack queued[MUSIC_QUEUE_CAPACITY];
    size_t queued_count;
    char current_requester[MUSIC_REQUESTER_SIZE];
    DashboardChart chart;
    char reports_history[1400];
    unsigned int command_seq;
    char command_result[200];
} DashboardStatus;

typedef struct
{
    int ai, promo, reports; /* -1 = unchanged, 0 = disable, 1 = enable */
    int obs_action;  /* 0: none; 1: scene, 2: start record, 3: stop record */
    char obs_scene[DASH_OBS_SCENE_LENGTH];
    int audio_action; /* 0 none, 1 set volume, 2 mute */
    char audio_input[256];
    int audio_mute;
    double audio_volume;
    int music_action; /* 1 skip current; 2 clear upcoming */
    int mark_moment;
    int ai_test_action;
    char ai_test_message[512];
    int settings_action;
    char settings_key[80];
    char settings_value[CONFIG_STRING_SIZE];
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
