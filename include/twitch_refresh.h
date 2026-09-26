#ifndef TWITCH_REFRESH_H
#define TWITCH_REFRESH_H

#include "bot_result.h"
#include "config.h"
#include "twitch_auth.h"


BotResult twitch_refresh_access_token(
    const TwitchConfig *config,
    const char *refresh_token,
    TwitchAuthToken *new_token
);


#endif
