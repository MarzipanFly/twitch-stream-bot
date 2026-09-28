#ifndef CONFIG_H
#define CONFIG_H

#include "bot_result.h"

/*
 * Обычные строковые параметры:
 * login, user id, client id, chat id и т.д.
 */
#define CONFIG_STRING_SIZE 256

#define CONFIG_TOKEN_SIZE 1024


typedef struct
{
    char client_id[CONFIG_STRING_SIZE];
    char client_secret[CONFIG_STRING_SIZE];

    char access_token[CONFIG_TOKEN_SIZE];
    char refresh_token[CONFIG_TOKEN_SIZE];

    char broadcaster_login[CONFIG_STRING_SIZE];
    char broadcaster_id[CONFIG_STRING_SIZE];

    char bot_login[CONFIG_STRING_SIZE];
    char bot_user_id[CONFIG_STRING_SIZE];

} TwitchConfig;


typedef struct
{
    char bot_token[CONFIG_STRING_SIZE];
    char chat_id[CONFIG_STRING_SIZE];

} TelegramConfig;


typedef struct
{
    char command_prefix;

} BotConfig;


typedef struct
{
    TwitchConfig   twitch;
    TelegramConfig telegram;
    BotConfig      bot;

} AppConfig;


/*
 * Загружает конфигурацию из INI-файла.
 */
BotResult config_load(
    const char *filename,
    AppConfig *config
);


/*
 * Проверяет основные обязательные настройки.
 */
BotResult config_validate(
    const AppConfig *config
);


#endif
