#include "achievements.h"
#include "twitch_chat.h"
#include "logger.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>

#define FOLLOW_REWARD_ORANGES 500LL

static const cJSON *json_field(const cJSON *object, const char *name)
{
    return cJSON_IsObject(object)
        ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
}

static const char *json_string(const cJSON *object, const char *name)
{
    const cJSON *field = json_field(object, name);
    return cJSON_IsString(field) && field->valuestring
        ? field->valuestring : NULL;
}

static const cJSON *event_payload(const cJSON *root)
{
    return json_field(json_field(root, "payload"), "event");
}

AchievementEventKind achievements_event_kind(const char *json)
{
    cJSON *root;
    const cJSON *subscription;
    const char *type;
    AchievementEventKind kind = ACHIEVEMENT_EVENT_NONE;
    if (!json) return kind;
    root = cJSON_Parse(json);
    if (!root) return kind;
    subscription = json_field(json_field(root, "payload"), "subscription");
    type = json_string(subscription, "type");
    if (type && strcmp(type, "channel.follow") == 0)
        kind = ACHIEVEMENT_EVENT_FOLLOW;
    else if (type && strcmp(type, "channel.raid") == 0)
        kind = ACHIEVEMENT_EVENT_RAID;
    cJSON_Delete(root);
    return kind;
}

BotResult achievements_process_event(const TwitchConfig *twitch,
                                     const char *json,
                                     AchievementEventKind kind)
{
    cJSON *root;
    const cJSON *event;
    const char *id, *login, *name, *channel;
    ViewerProfile *profile;
    char announcement[512];
    long long previous;
    BotResult result;
    if (!twitch || !json || kind == ACHIEVEMENT_EVENT_NONE)
        return BOT_ERR_CONFIG;
    root = cJSON_Parse(json);
    if (!root) return BOT_ERR_JSON;
    event = event_payload(root);
    if (!cJSON_IsObject(event))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    if (kind == ACHIEVEMENT_EVENT_FOLLOW)
    {
        id = json_string(event, "user_id");
        login = json_string(event, "user_login");
        name = json_string(event, "user_name");
        channel = json_string(event, "broadcaster_user_id");
    }
    else
    {
        id = json_string(event, "from_broadcaster_user_id");
        login = json_string(event, "from_broadcaster_user_login");
        name = json_string(event, "from_broadcaster_user_name");
        channel = json_string(event, "to_broadcaster_user_id");
    }

    /* Never grant rewards for a different channel or for incomplete events. */
    if (!id || !id[0] || !login || !login[0] ||
        !channel || strcmp(channel, twitch->broadcaster_id) != 0)
    {
        cJSON_Delete(root);
        return BOT_ERR_TWITCH;
    }
    if (!name || !name[0]) name = login;
    if (strcmp(id, twitch->broadcaster_id) == 0 ||
        strcmp(id, twitch->bot_user_id) == 0)
    {
        cJSON_Delete(root);
        return BOT_OK;
    }

    profile = viewer_profile_get_or_create(id, login, name);
    if (!profile)
    {
        log_warning("Achievements: cannot create viewer profile for %s", login);
        cJSON_Delete(root);
        return BOT_ERR_FILE;
    }

    if (kind == ACHIEVEMENT_EVENT_FOLLOW)
    {
        if (profile->follow_rewarded)
        {
            log_info("Follow reward already received by %s", login);
            cJSON_Delete(root);
            return BOT_OK;
        }
        if (profile->balance > LLONG_MAX - FOLLOW_REWARD_ORANGES)
        {
            cJSON_Delete(root);
            return BOT_ERR_CONFIG;
        }
        previous = profile->balance;
        profile->balance += FOLLOW_REWARD_ORANGES;
        profile->follow_rewarded = 1;
        if (!viewer_profile_save())
        {
            profile->balance = previous;
            profile->follow_rewarded = 0;
            log_error("Achievements: could not save follow reward for %s", login);
            cJSON_Delete(root);
            return BOT_ERR_FILE;
        }
        snprintf(announcement, sizeof(announcement),
                 "🍊 Спасибо за фоллов, @%.60s! Достижение «В команде» и +500 апельсинов!",
                 login);
        log_info("Achievement follower: %s got 500 oranges", login);
    }
    else
    {
        const cJSON *viewers_field = json_field(event, "viewers");
        int viewers = 0;
        if (cJSON_IsNumber(viewers_field) && viewers_field->valueint > 0)
            viewers = viewers_field->valueint;
        if (profile->raids_sent == LLONG_MAX)
        {
            cJSON_Delete(root);
            return BOT_ERR_CONFIG;
        }
        previous = profile->raids_sent;
        profile->raids_sent++;
        if (!viewer_profile_save())
        {
            profile->raids_sent = previous;
            log_error("Achievements: could not save raid for %s", login);
            cJSON_Delete(root);
            return BOT_ERR_FILE;
        }
        snprintf(announcement, sizeof(announcement),
                 "🚀 Рейд от @%.60s! С нами %d зрителей! Спасибо за поддержку! Достижение «Рейдер»!",
                 login, viewers);
        log_info("Achievement raid from %s (viewers=%d, total=%lld)",
                 login, viewers, profile->raids_sent);
    }

    cJSON_Delete(root);
    result = twitch_chat_send_message(twitch, announcement);
    if (result != BOT_OK)
        log_warning("Achievement announcement failed (%d)", (int)result);
    return BOT_OK;
}

void achievements_format(const ViewerProfile *profile,
                         char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    if (!profile)
    {
        snprintf(buffer, buffer_size, "Не удалось найти достижения.");
        return;
    }
    snprintf(buffer, buffer_size,
             "%s | Достижения: %s В команде (+500 🍊 за фоллов); %s Рейдер (рейдов: %lld).",
             profile->display_name[0] ? profile->display_name : profile->login,
             profile->follow_rewarded ? "✅" : "🔒",
             profile->raids_sent > 0 ? "✅" : "🔒",
             profile->raids_sent);
}
