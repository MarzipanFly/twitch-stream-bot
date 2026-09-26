#ifndef CONFIG_H
#define CONFIG_H

#include "bot_result.h"
#define CONFIG_STRING_SIZE 256

typedef struct
{
    char client_id[CONFIG_STRING_SIZE];
    char client_secret[CONFIG_STRING_SIZE];

    char access_token[CONFIG_STRING_SIZE];
    char refresh_token[CONFIG_STRING_SIZE];

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
    TwitchConfig    twitch;
    TelegramConfig  telegram;
    BotConfig       bot;
} AppConfig;

/*
 * Загружает конфигурацию из файла.
 *
 * Возвращает:
 *  0  - успешно
 * -1  - ошибка открытия файла
 */
BotResult config_load(
        const char *filename,
        AppConfig *config
);

/*
 * Проверяет основные настройки.
 */
BotResult config_validate(
    const AppConfig *config
);

#endif // CONFIG_H
