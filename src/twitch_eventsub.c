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
