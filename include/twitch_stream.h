#ifndef TWITCH_STREAM_H
#define TWITCH_STREAM_H

#include "bot_result.h"
#include "config.h"
#include "http_client.h"

#define TWITCH_STREAM_ID_SIZE       64
#define TWITCH_STREAM_USER_SIZE     128
#define TWITCH_STREAM_GAME_SIZE     256
#define TWITCH_STREAM_TITLE_SIZE    512
#define TWITCH_STREAM_TIME_SIZE     64
#define TWITCH_STREAM_LANGUAGE_SIZE 16


typedef struct
{
    int is_live;

    char id[TWITCH_STREAM_ID_SIZE];

    char user_id[TWITCH_STREAM_ID_SIZE];
    char user_login[TWITCH_STREAM_USER_SIZE];
    char user_name[TWITCH_STREAM_USER_SIZE];

    char game_id[TWITCH_STREAM_ID_SIZE];
    char game_name[TWITCH_STREAM_GAME_SIZE];

    char title[TWITCH_STREAM_TITLE_SIZE];

    int viewer_count;

    char started_at[TWITCH_STREAM_TIME_SIZE];
    char language[TWITCH_STREAM_LANGUAGE_SIZE];

} TwitchStream;


BotResult twitch_get_stream(
    const TwitchConfig *config,
    const char *user_id,
    HttpResponse *response
);


BotResult twitch_parse_stream_response(
    const char *json,
    TwitchStream *stream
);


#endif
