#ifndef TOKEN_STORE_H
#define TOKEN_STORE_H

#include "bot_result.h"
#include "twitch_auth.h"

BotResult token_store_save(
    const TwitchAuthToken *token
);

BotResult token_store_load(
    TwitchAuthToken *token
);

BotResult token_store_save_broadcast(const TwitchAuthToken *token);
BotResult token_store_load_broadcast(TwitchAuthToken *token);
BotResult token_store_delete_broadcast(void);

BotResult token_store_delete(void);

#endif
