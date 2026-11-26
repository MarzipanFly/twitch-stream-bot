#include "twitch_eventsub.h"

#include "cJSON.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

static void copy_json_string(
    char *destination,
    size_t destination_size,
    const cJSON *json
)
{
    if (destination == NULL || destination_size == 0)
    {
        return;
    }

    destination[0] = '\0';

    if (!cJSON_IsString(json) || json->valuestring == NULL)
    {
        return;
    }

    snprintf(
        destination,
        destination_size,
        "%s",
        json->valuestring
    );
}

BotResult twitch_eventsub_get_message_id(
    const char *json,
    char *message_id,
    size_t capacity
)
{
    cJSON *root;
    const cJSON *metadata, *id;
    size_t length;

    if (!json || !message_id || capacity < 2)
        return BOT_ERR_CONFIG;
    message_id[0] = '\0';
    root = cJSON_Parse(json);
    if (!root)
        return BOT_ERR_JSON;
    metadata = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    id = cJSON_GetObjectItemCaseSensitive(metadata, "message_id");
    if (!cJSON_IsString(id) || !id->valuestring || !id->valuestring[0])
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }
    length = strlen(id->valuestring);
    if (length >= capacity)
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }
    memcpy(message_id, id->valuestring, length + 1);
    cJSON_Delete(root);
    return BOT_OK;
}

/* A reconnect URL has a signed query. Never send it to another host,
 * log it, truncate it, or replace any query parameter. */
int twitch_eventsub_reconnect_path(
    const char *reconnect_url, char *path, size_t capacity
)
{
    static const char prefix[] = "wss://eventsub.wss.twitch.tv";
    const char *tail;
    size_t length, i, prefix_len = sizeof(prefix) - 1;
    if (!reconnect_url || !path || capacity < 4)
        return 0;
    path[0] = '\0';
    if (strncmp(reconnect_url, prefix, prefix_len) != 0)
        return 0;
    tail = reconnect_url + prefix_len;
    if (*tail != '/' && *tail != '?')
        return 0;
    length = strlen(tail);
    if (length + (*tail == '?' ? 2U : 1U) > capacity)
        return 0;
    for (i = 0; i < length; ++i)
    {
        unsigned char ch = (unsigned char)tail[i];
        if (ch < 0x21 || ch > 0x7e ||
            ch == '#' || ch == '@' || ch == '\\')
            return 0;
    }
    if (*tail == '?')
    {
        path[0] = '/';  /* HTTP path for an omitted path is '/'. */
        memcpy(path + 1, tail, length + 1);
    }
    else
        memcpy(path, tail, length + 1);
    return 1;
}

BotResult twitch_eventsub_parse_reconnect_url(
    const char *json, char *reconnect_url, size_t capacity
)
{
    cJSON *root;
    const cJSON *payload, *session, *url;
    size_t length;
    char candidate[TWITCH_EVENTSUB_URL_SIZE];
    if (!json || !reconnect_url || capacity < 2)
        return BOT_ERR_CONFIG;
    reconnect_url[0] = '\0';
    root = cJSON_Parse(json);
    if (!root)
        return BOT_ERR_JSON;
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    session = cJSON_GetObjectItemCaseSensitive(payload, "session");
    url = cJSON_GetObjectItemCaseSensitive(session, "reconnect_url");
    if (!cJSON_IsString(url) || !url->valuestring)
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }
    length = strlen(url->valuestring);
    if (!length || length >= capacity)
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }
    memcpy(reconnect_url, url->valuestring, length + 1);
    cJSON_Delete(root);
    if (!twitch_eventsub_reconnect_path(
            reconnect_url, candidate, sizeof(candidate)))
    {
        reconnect_url[0] = '\0';
        return BOT_ERR_JSON;
    }
    return BOT_OK;
}

BotResult twitch_eventsub_get_message_type(
    const char *json,
    TwitchEventSubMessageType *type
)
{
    cJSON *root = NULL;
    cJSON *metadata = NULL;
    cJSON *message_type = NULL;

    if (json == NULL || type == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    *type = TWITCH_EVENTSUB_UNKNOWN;

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        log_error("Failed to parse Twitch EventSub JSON");
        return BOT_ERR_JSON;
    }

    metadata = cJSON_GetObjectItemCaseSensitive(root, "metadata");
    if (!cJSON_IsObject(metadata))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    message_type = cJSON_GetObjectItemCaseSensitive(metadata, "message_type");
    if (!cJSON_IsString(message_type))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    if (strcmp(message_type->valuestring, "session_welcome") == 0)
    {
        *type = TWITCH_EVENTSUB_SESSION_WELCOME;
    }
    else if (strcmp(message_type->valuestring, "notification") == 0)
    {
        *type = TWITCH_EVENTSUB_NOTIFICATION;
    }
    else if (strcmp(message_type->valuestring, "session_keepalive") == 0)
    {
        *type = TWITCH_EVENTSUB_KEEPALIVE;
    }
    else if (strcmp(message_type->valuestring, "session_reconnect") == 0)
    {
        *type = TWITCH_EVENTSUB_RECONNECT;
    }

    cJSON_Delete(root);
    return BOT_OK;
}

BotResult twitch_eventsub_parse_welcome(
    const char *json,
    TwitchEventSubSession *session
)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;
    cJSON *session_json = NULL;
    cJSON *id = NULL;
    cJSON *keepalive = NULL;

    if (json == NULL || session == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    memset(session, 0, sizeof(*session));

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        return BOT_ERR_JSON;
    }

    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    session_json = cJSON_GetObjectItemCaseSensitive(payload, "session");

    if (!cJSON_IsObject(session_json))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    id = cJSON_GetObjectItemCaseSensitive(session_json, "id");
    keepalive = cJSON_GetObjectItemCaseSensitive(
        session_json,
        "keepalive_timeout_seconds"
    );

    if (!cJSON_IsString(id))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    copy_json_string(
        session->session_id,
        sizeof(session->session_id),
        id
    );

    if (cJSON_IsNumber(keepalive))
    {
        session->keepalive_timeout_seconds = keepalive->valueint;
    }

    cJSON_Delete(root);
    return BOT_OK;
}

BotResult twitch_eventsub_parse_chat_message(
    const char *json,
    TwitchChatMessage *message
)
{
    cJSON *root = NULL;
    cJSON *payload = NULL;
    cJSON *subscription = NULL;
    cJSON *subscription_type = NULL;
    cJSON *event = NULL;
    cJSON *message_object = NULL;

    if (json == NULL || message == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    memset(message, 0, sizeof(*message));

    root = cJSON_Parse(json);
    if (root == NULL)
    {
        log_error("Failed to parse Twitch chat EventSub JSON");
        return BOT_ERR_JSON;
    }

    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (!cJSON_IsObject(payload))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    subscription = cJSON_GetObjectItemCaseSensitive(payload, "subscription");
    subscription_type = cJSON_GetObjectItemCaseSensitive(subscription, "type");

    if (!cJSON_IsString(subscription_type) ||
        strcmp(subscription_type->valuestring, "channel.chat.message") != 0)
    {
        cJSON_Delete(root);
        return BOT_ERR_TWITCH;
    }

    event = cJSON_GetObjectItemCaseSensitive(payload, "event");
    if (!cJSON_IsObject(event))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    copy_json_string(
        message->broadcaster_user_id,
        sizeof(message->broadcaster_user_id),
        cJSON_GetObjectItemCaseSensitive(event, "broadcaster_user_id")
    );

    copy_json_string(
        message->broadcaster_user_login,
        sizeof(message->broadcaster_user_login),
        cJSON_GetObjectItemCaseSensitive(event, "broadcaster_user_login")
    );

    copy_json_string(
        message->broadcaster_user_name,
        sizeof(message->broadcaster_user_name),
        cJSON_GetObjectItemCaseSensitive(event, "broadcaster_user_name")
    );

    copy_json_string(
        message->chatter_user_id,
        sizeof(message->chatter_user_id),
        cJSON_GetObjectItemCaseSensitive(event, "chatter_user_id")
    );

    copy_json_string(
        message->chatter_user_login,
        sizeof(message->chatter_user_login),
        cJSON_GetObjectItemCaseSensitive(event, "chatter_user_login")
    );

    copy_json_string(
        message->chatter_user_name,
        sizeof(message->chatter_user_name),
        cJSON_GetObjectItemCaseSensitive(event, "chatter_user_name")
    );

    copy_json_string(
        message->message_id,
        sizeof(message->message_id),
        cJSON_GetObjectItemCaseSensitive(event, "message_id")
    );

    message_object = cJSON_GetObjectItemCaseSensitive(event, "message");
    if (!cJSON_IsObject(message_object))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    copy_json_string(
        message->text,
        sizeof(message->text),
        cJSON_GetObjectItemCaseSensitive(message_object, "text")
    );

    if (message->text[0] == '\0')
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    cJSON_Delete(root);
    return BOT_OK;
}

static int copy_json_string_strict(char *destination, size_t destination_size,
                                   const cJSON *json, int required)
{
    size_t length;
    if (!destination || destination_size == 0) return 0;
    destination[0] = '\0';
    if (!json || json->type == cJSON_NULL) return !required;
    if (!cJSON_IsString(json) || !json->valuestring) return 0;
    length = strlen(json->valuestring);
    if (length >= destination_size) return 0;
    memcpy(destination, json->valuestring, length + 1);
    return !required || length > 0;
}

BotResult twitch_eventsub_parse_channel_points_redemption(
    const char *json, TwitchChannelPointsRedemption *redemption)
{
    cJSON *root = NULL;
    const cJSON *payload, *subscription, *subscription_type;
    const cJSON *event, *reward, *cost;
    if (!json || !redemption) return BOT_ERR_CONFIG;
    memset(redemption, 0, sizeof(*redemption));
    root = cJSON_Parse(json);
    if (!root) return BOT_ERR_JSON;
    payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
    subscription = cJSON_GetObjectItemCaseSensitive(payload, "subscription");
    subscription_type = cJSON_GetObjectItemCaseSensitive(subscription, "type");
    if (!cJSON_IsString(subscription_type) ||
        strcmp(subscription_type->valuestring,
               "channel.channel_points_custom_reward_redemption.add") != 0)
    {
        cJSON_Delete(root);
        return BOT_ERR_TWITCH;
    }
    event = cJSON_GetObjectItemCaseSensitive(payload, "event");
    reward = cJSON_GetObjectItemCaseSensitive(event, "reward");
    cost = cJSON_GetObjectItemCaseSensitive(reward, "cost");
    if (!cJSON_IsObject(event) || !cJSON_IsObject(reward) ||
        !cJSON_IsNumber(cost) || cost->valuedouble < 0.0 ||
        cost->valuedouble > 2147483647.0)
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }
    if (!copy_json_string_strict(redemption->redemption_id, sizeof(redemption->redemption_id),
            cJSON_GetObjectItemCaseSensitive(event, "id"), 1) ||
        !copy_json_string_strict(redemption->broadcaster_user_id, sizeof(redemption->broadcaster_user_id),
            cJSON_GetObjectItemCaseSensitive(event, "broadcaster_user_id"), 1) ||
        !copy_json_string_strict(redemption->broadcaster_user_login, sizeof(redemption->broadcaster_user_login),
            cJSON_GetObjectItemCaseSensitive(event, "broadcaster_user_login"), 1) ||
        !copy_json_string_strict(redemption->user_id, sizeof(redemption->user_id),
            cJSON_GetObjectItemCaseSensitive(event, "user_id"), 1) ||
        !copy_json_string_strict(redemption->user_login, sizeof(redemption->user_login),
            cJSON_GetObjectItemCaseSensitive(event, "user_login"), 1) ||
        !copy_json_string_strict(redemption->user_name, sizeof(redemption->user_name),
            cJSON_GetObjectItemCaseSensitive(event, "user_name"), 1) ||
        !copy_json_string_strict(redemption->reward_id, sizeof(redemption->reward_id),
            cJSON_GetObjectItemCaseSensitive(reward, "id"), 1) ||
        !copy_json_string_strict(redemption->reward_title, sizeof(redemption->reward_title),
            cJSON_GetObjectItemCaseSensitive(reward, "title"), 1) ||
        !copy_json_string_strict(redemption->user_input, sizeof(redemption->user_input),
            cJSON_GetObjectItemCaseSensitive(event, "user_input"), 0))
    {
        cJSON_Delete(root);
        memset(redemption, 0, sizeof(*redemption));
        return BOT_ERR_JSON;
    }
    redemption->reward_cost = cost->valueint;
    cJSON_Delete(root);
    return BOT_OK;
}
