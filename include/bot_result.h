#ifndef BOT_RESULT_H
#define BOT_RESULT_H

typedef enum
{
    BOT_OK = 0,

    BOT_AUTH_PENDING,

    BOT_ERR_UNKNOWN,
    BOT_ERR_CONFIG,
    BOT_ERR_FILE,
    BOT_ERR_NETWORK,
    BOT_ERR_AUTH,
    BOT_ERR_JSON,
    BOT_ERR_TWITCH,
    BOT_ERR_TELEGRAM,
    BOT_ERR_STORAGE

} BotResult;

/*
 * Возвращает текстовое сообщение ошибки
 */
const char *bot_result_to_string(BotResult result);

#endif // BOT_RESULT_H
