#ifndef TWITCH_API_H
#define TWITCH_API_H

#include "bot_result.h"
#include "config.h"
#include "http_client.h"
#include "twitch_user.h"

/*
 * Получает информацию о Twitch-пользователе
 * по его login
 */

BotResult twitch_get_user(
		const TwitchConfig *config,
		const char *login,
		HttpResponse *response
);

BotResult twitch_parse_user_response(
		const char *json,
		TwitchUser *user
);

#endif // TWITCH_API_H
