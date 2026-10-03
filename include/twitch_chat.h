#ifndef TWITCH_CHAT_H
#define TWITCH_CHAT_H

#include "bot_result.h"
#include "config.h"

BotResult twitch_chat_send_message(
    const TwitchConfig *config,
    const char *message
);

#endif
