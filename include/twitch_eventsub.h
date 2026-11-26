#ifndef TWITCH_EVENTSUB_H
#define TWITCH_EVENTSUB_H

#include "bot_result.h"
#include <stddef.h>

#define TWITCH_EVENTSUB_ID_SIZE       128
#define TWITCH_EVENTSUB_LOGIN_SIZE    128
#define TWITCH_EVENTSUB_NAME_SIZE     128
#define TWITCH_EVENTSUB_MESSAGE_SIZE  1024
#define TWITCH_EVENTSUB_URL_SIZE      1024
#define TWITCH_EVENTSUB_REWARD_TITLE_SIZE 256
#define TWITCH_EVENTSUB_USER_INPUT_SIZE   512

typedef enum
{
    TWITCH_EVENTSUB_UNKNOWN = 0,
    TWITCH_EVENTSUB_SESSION_WELCOME,
    TWITCH_EVENTSUB_NOTIFICATION,
    TWITCH_EVENTSUB_KEEPALIVE,
    TWITCH_EVENTSUB_RECONNECT

} TwitchEventSubMessageType;

typedef struct
{
    char session_id[TWITCH_EVENTSUB_ID_SIZE];
    int keepalive_timeout_seconds;

} TwitchEventSubSession;

typedef struct
{
    char redemption_id[TWITCH_EVENTSUB_ID_SIZE];
    char broadcaster_user_id[TWITCH_EVENTSUB_ID_SIZE];
    char broadcaster_user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char user_id[TWITCH_EVENTSUB_ID_SIZE];
    char user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char user_name[TWITCH_EVENTSUB_NAME_SIZE];
    char reward_id[TWITCH_EVENTSUB_ID_SIZE];
    char reward_title[TWITCH_EVENTSUB_REWARD_TITLE_SIZE];
    char user_input[TWITCH_EVENTSUB_USER_INPUT_SIZE];
    int reward_cost;
} TwitchChannelPointsRedemption;

typedef struct
{
    char broadcaster_user_id[TWITCH_EVENTSUB_ID_SIZE];
    char broadcaster_user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char broadcaster_user_name[TWITCH_EVENTSUB_NAME_SIZE];

    char chatter_user_id[TWITCH_EVENTSUB_ID_SIZE];
    char chatter_user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char chatter_user_name[TWITCH_EVENTSUB_NAME_SIZE];

    char message_id[TWITCH_EVENTSUB_ID_SIZE];
    char text[TWITCH_EVENTSUB_MESSAGE_SIZE];

    /* Roles read from the badges array in channel.chat.message. */
    int is_broadcaster;
    int is_moderator;
    int is_vip;
    int is_subscriber; /* Includes founder badges. */
    /* Follower status is filled by the Helix lookup, not chat badges. */
    int is_follower;
    int follower_status_known;

} TwitchChatMessage;

/* Strictly extract EventSub metadata.message_id, never truncate it.
 * Caller must check this before any side effects. */
BotResult twitch_eventsub_get_message_id(
    const char *json,
    char *message_id,
    size_t capacity
);

BotResult twitch_eventsub_get_message_type(
    const char *json,
    TwitchEventSubMessageType *type
);

BotResult twitch_eventsub_parse_welcome(
    const char *json,
    TwitchEventSubSession *session
);

/* Parse Twitch-supplied reconnect URL. The URL must stay on the trusted
 * EventSub host; path retains original query string. */
BotResult twitch_eventsub_parse_reconnect_url(
    const char *json, char *reconnect_url, size_t capacity
);
int twitch_eventsub_reconnect_path(
    const char *reconnect_url, char *path, size_t capacity
);

BotResult twitch_eventsub_parse_chat_message(
    const char *json,
    TwitchChatMessage *message
);

BotResult twitch_eventsub_parse_channel_points_redemption(
    const char *json,
    TwitchChannelPointsRedemption *redemption
);

#endif
