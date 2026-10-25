#ifndef CONFIG_H
#define CONFIG_H

#include "bot_result.h"
#include <stddef.h>

/*
 * Обычные строковые параметры:
 * login, user id, client id, chat id и т.д.
 */
#define CONFIG_STRING_SIZE 256

/*
 * OAuth-токены могут быть значительно длиннее
 * обычных строковых параметров.
 */
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
    /*
     * Токен Telegram-бота.
     * Используется Telegram Bot API.
     */
    char bot_token[CONFIG_STRING_SIZE];

    /*
     * ID Telegram-чата или канала.
     * Используется Telegram Bot API для отправки сообщений.
     */
    char chat_id[CONFIG_STRING_SIZE];

    /*
     * Публичная ссылка на Telegram-канал.
     * Используется, например, командой !тг.
     */
    char channel_url[CONFIG_STRING_SIZE];

} TelegramConfig;

typedef struct
{
	char invite_url[CONFIG_STRING_SIZE];
} DiscordConfig;
typedef struct
{
    char command_prefix;

} BotConfig;


typedef struct
{
    int enabled;
    char password[CONFIG_STRING_SIZE];
} ObsConfig;

/* Local Ollama model; never requires an Internet API key. */
typedef struct
{
    int enabled;
    char model[128];
    unsigned int cooldown_seconds;
    int semantic_enabled; /* Optional Ollama embedding search. */
    char semantic_model[128];
} AiConfig;

/* Periodic social links announcement in the Twitch chat. */
typedef struct
{
    int enabled;
    unsigned int min_minutes;
    unsigned int max_minutes;
} PromoConfig;

/* Automatic local stream report (no extra Twitch scopes required). */
typedef struct
{
    int enabled;
} StreamReportConfig;

/* Configurable OBS sounds, UTF-8 command names without prefix. */
#define SOUND_MAX_COUNT 16
#define SOUND_NAME_SIZE 64

typedef struct
{
    char command[SOUND_NAME_SIZE];
    char input[CONFIG_STRING_SIZE];
    unsigned int cooldown_seconds;
    long long cost;
} SoundConfig;

typedef struct
{
    TwitchConfig twitch;
    TelegramConfig telegram;
	DiscordConfig discord;
    BotConfig bot;
    ObsConfig obs;
    AiConfig ai;
    PromoConfig promo;
    StreamReportConfig stream_report;
    SoundConfig sounds[SOUND_MAX_COUNT];
    size_t sound_count;

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
