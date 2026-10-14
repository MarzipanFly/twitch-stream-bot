#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#include "config.h"
#include "bot_result.h"
#include "viewer_profile.h"
#include <stddef.h>

typedef enum
{
    ACHIEVEMENT_EVENT_NONE = 0,
    ACHIEVEMENT_EVENT_FOLLOW,
    ACHIEVEMENT_EVENT_RAID
} AchievementEventKind;

/* Determine which EventSub notification arrived without touching chat events. */
AchievementEventKind achievements_event_kind(const char *json);

/* Persists rewards and raid counts before posting announcements. */
BotResult achievements_process_event(const TwitchConfig *twitch,
                                     const char *json,
                                     AchievementEventKind kind);

/* Text for !достижения / !ачивки */
void achievements_format(const ViewerProfile *profile,
                         char *buffer, size_t buffer_size);

#endif
