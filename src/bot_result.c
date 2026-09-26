#include "bot_result.h"

const char *bot_result_to_string(BotResult result)
{
	switch(result)
	{
		case BOT_OK:
			return "OK";

		case BOT_ERR_UNKNOWN:
			return "Unknown error";

		case BOT_ERR_CONFIG:
			return "Configuration error";

		case BOT_ERR_FILE:
			return "File error";

		case BOT_ERR_NETWORK:
			return "Network error";

		case BOT_ERR_AUTH:
			return "Authentication error";

		case BOT_ERR_JSON:
			return "JSON error";

		case BOT_ERR_TWITCH:
			return "Twitch API error";

		case BOT_ERR_TELEGRAM:
			return "Telegram API error";

		case BOT_ERR_STORAGE:
			return "Storage error";

        case BOT_AUTH_PENDING:
            return "Authorization pending";

		default:
			return "Invalid result code ";
	}
}
