#ifndef TWITCH_API_H
#define TWITCH_API_H

#include "bot_result.h"
#include "config.h"
#include "http_client.h"
#include "twitch_user.h"

BotResult twitch_get_user(
    const TwitchConfig *config,
    const char *login,
    HttpResponse *response
);

BotResult twitch_parse_user_response(
    const char *json,
    TwitchUser *user
);

BotResult twitch_search_category(
    const TwitchConfig *config,
    const char *query,
    char *category_id,
    size_t category_id_size,
    char *category_name,
    size_t category_name_size
);

/* Resolve and cache whether a user follows this broadcaster. */
BotResult twitch_get_channel_follower_status(
    const TwitchConfig *config,
    const char *user_id,
    int *is_follower
);

/* Mark a newly received channel.follow event as a positive cache entry. */
void twitch_follower_cache_mark(
    const TwitchConfig *config,
    const char *user_id,
    int is_follower
);

BotResult twitch_send_shoutout(
    const TwitchConfig *config,
    const char *to_broadcaster_id
);

BotResult twitch_update_channel_category(
    const TwitchConfig *config,
    const char *broadcaster_id,
    const char *category_id
);

#endif
