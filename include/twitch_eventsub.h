#ifndef TWITCH_EVENTSUB_H
#define TWITCH_EVENTSUB_H

#include "bot_result.h"

#define TWITCH_EVENTSUB_ID_SIZE       128
#define TWITCH_EVENTSUB_LOGIN_SIZE    128
#define TWITCH_EVENTSUB_NAME_SIZE     128
#define TWITCH_EVENTSUB_MESSAGE_SIZE  1024
#define TWITCH_EVENTSUB_URL_SIZE      1024

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
    char broadcaster_user_id[TWITCH_EVENTSUB_ID_SIZE];
    char broadcaster_user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char broadcaster_user_name[TWITCH_EVENTSUB_NAME_SIZE];

    char chatter_user_id[TWITCH_EVENTSUB_ID_SIZE];
    char chatter_user_login[TWITCH_EVENTSUB_LOGIN_SIZE];
    char chatter_user_name[TWITCH_EVENTSUB_NAME_SIZE];

    char message_id[TWITCH_EVENTSUB_ID_SIZE];
    char text[TWITCH_EVENTSUB_MESSAGE_SIZE];

} TwitchChatMessage;

BotResult twitch_eventsub_get_message_type(
    const char *json,
    TwitchEventSubMessageType *type
);

BotResult twitch_eventsub_parse_welcome(
    const char *json,
    TwitchEventSubSession *session
);

BotResult twitch_eventsub_parse_chat_message(
    const char *json,
    TwitchChatMessage *message
);

#endif
