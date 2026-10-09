#include "config.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>


typedef enum
{
    SECTION_NONE = 0,
    SECTION_TWITCH,
    SECTION_TELEGRAM,
	SECTION_DISCORD,
    SECTION_BOT,
    SECTION_OBS,
    SECTION_SOUNDS

} ConfigSection;


/*
 * Удаляет пробелы в начале строки.
 */
static char *trim_left(
    char *str
)
{
    while (
        *str != '\0' &&
        isspace(
            (unsigned char)*str
        ))
    {
        ++str;
    }

    return str;
}


/*
 * Удаляет пробелы и переносы строки
 * в конце строки.
 */
static void trim_right(
    char *str
)
{
    size_t length;

    length =
        strlen(
            str
        );

    while (
        length > 0 &&
        isspace(
            (unsigned char)str[length - 1]
        ))
    {
        str[length - 1] = '\0';
        --length;
    }
}


/*
 * Определяет секцию INI-файла.
 */
static ConfigSection parse_section(
    const char *line
)
{
    if (strcmp(line, "[twitch]") == 0)
    {
        return SECTION_TWITCH;
    }

    if (strcmp(line, "[telegram]") == 0)
    {
        return SECTION_TELEGRAM;
    }

	if (strcmp(line, "[discord]") == 0)
	{
		return SECTION_DISCORD;
	}

    if (strcmp(line, "[obs]") == 0) return SECTION_OBS;
    if (strcmp(line, "[sounds]") == 0) return SECTION_SOUNDS;

    if (strcmp(line, "[bot]") == 0)
    {
        return SECTION_BOT;
    }

    return SECTION_NONE;
}


/*
 * Копирует строку в буфер
 * ограниченного размера.
 */
static void copy_string(
    char *destination,
    size_t destination_size,
    const char *source
)
{
    if (
        destination == NULL ||
        source == NULL ||
        destination_size == 0)
    {
        return;
    }

    snprintf(
        destination,
        destination_size,
        "%s",
        source
    );
}


/*
 * Обрабатывает одну пару key=value.
 */
static void parse_key_value(
    ConfigSection section,
    const char *key,
    const char *value,
    AppConfig *config
)
{
    switch (section)
    {
        case SECTION_TWITCH:

            if (strcmp(key, "client_id") == 0)
            {
                copy_string(
                    config->twitch.client_id,
                    sizeof(config->twitch.client_id),
                    value
                );
            }
            else if (strcmp(key, "client_secret") == 0)
            {
                copy_string(
                    config->twitch.client_secret,
                    sizeof(config->twitch.client_secret),
                    value
                );
            }
            else if (strcmp(key, "access_token") == 0)
            {
                copy_string(
                    config->twitch.access_token,
                    sizeof(config->twitch.access_token),
                    value
                );
            }
            else if (strcmp(key, "refresh_token") == 0)
            {
                copy_string(
                    config->twitch.refresh_token,
                    sizeof(config->twitch.refresh_token),
                    value
                );
            }
            else if (strcmp(key, "broadcaster_login") == 0)
            {
                copy_string(
                    config->twitch.broadcaster_login,
                    sizeof(config->twitch.broadcaster_login),
                    value
                );
            }
            else if (strcmp(key, "broadcaster_id") == 0)
            {
                copy_string(
                    config->twitch.broadcaster_id,
                    sizeof(config->twitch.broadcaster_id),
                    value
                );
            }
            else if (strcmp(key, "bot_login") == 0)
            {
                copy_string(
                    config->twitch.bot_login,
                    sizeof(config->twitch.bot_login),
                    value
                );
            }
            else if (strcmp(key, "bot_user_id") == 0)
            {
                copy_string(
                    config->twitch.bot_user_id,
                    sizeof(config->twitch.bot_user_id),
                    value
                );
            }

            break;


        case SECTION_TELEGRAM:

            if (strcmp(key, "bot_token") == 0)
            {
                copy_string(
                    config->telegram.bot_token,
                    sizeof(config->telegram.bot_token),
                    value
                );
            }
            else if (strcmp(key, "chat_id") == 0)
            {
                copy_string(
                    config->telegram.chat_id,
                    sizeof(config->telegram.chat_id),
                    value
                );
            }
            else if (strcmp(key, "channel_url") == 0)
            {
                copy_string(
                    config->telegram.channel_url,
                    sizeof(config->telegram.channel_url),
                    value
                );
            }

            break;

		case SECTION_DISCORD:
	{
		if (strcmp(key, "invite_url") == 0)
		{
			copy_string(
					config->discord.invite_url,
					sizeof (config->discord.invite_url),
					value
			);
		}
	}
            break;

        case SECTION_OBS:
            if (strcmp(key, "enabled") == 0)
                config->obs.enabled = strcmp(value, "true") == 0 || strcmp(value, "1") == 0;
            else if (strcmp(key, "password") == 0)
                copy_string(config->obs.password, sizeof(config->obs.password), value);
            break;

        case SECTION_SOUNDS:
        {
            /* command=OBS_input,cooldown_seconds,cost; legacy cost=0 */
            SoundConfig *sound;
            const char *first = strchr(value, ',');
            const char *second = first ? strchr(first + 1, ',') : NULL;
            size_t input_length = first ? (size_t)(first - value) : strlen(value);
            unsigned int seconds = 15;
            long long cost = 0;
            size_t i;
            char extra;

            if (key[0] == '\0' || strlen(key) >= SOUND_NAME_SIZE ||
                input_length == 0 || input_length >= CONFIG_STRING_SIZE ||
                config->sound_count >= SOUND_MAX_COUNT)
                break;

            for (i = 0; i < config->sound_count; ++i)
                if (strcmp(config->sounds[i].command, key) == 0)
                    break;
            if (i < config->sound_count)
                break;

            if (first)
            {
                if (second)
                {
                    char cooldown_text[32];
                    size_t n = (size_t)(second - (first + 1));
                    if (n == 0 || n >= sizeof(cooldown_text))
                        break;
                    memcpy(cooldown_text, first + 1, n);
                    cooldown_text[n] = '\0';
                    if (sscanf(cooldown_text, " %u %c", &seconds, &extra) != 1)
                        break;
                    if (sscanf(second + 1, " %lld %c", &cost, &extra) != 1 ||
                        cost < 0 || cost > 1000000000LL)
                        break;
                }
                else if (sscanf(first + 1, " %u %c", &seconds, &extra) != 1)
                    break;
            }
            if (seconds > 3600)
                break;

            sound = &config->sounds[config->sound_count++];
            copy_string(sound->command, sizeof(sound->command), key);
            memcpy(sound->input, value, input_length);
            sound->input[input_length] = '\0';
            while (input_length > 0 &&
                   isspace((unsigned char)sound->input[input_length - 1]))
                sound->input[--input_length] = '\0';
            if (sound->input[0] == '\0')
            {
                --config->sound_count;
                break;
            }
            sound->cooldown_seconds = seconds;
            sound->cost = cost;
            break;
        }

        case SECTION_BOT:

            if (strcmp(key, "command_prefix") == 0)
            {
                if (value[0] != '\0')
                {
                    config->bot.command_prefix = value[0];
                }
            }

            break;


        default:

            break;
    }
}


BotResult config_load(
    const char *filename,
    AppConfig *config
)
{
    FILE *file;
    char line[2048];
    ConfigSection current_section = SECTION_NONE;

    if (
        filename == NULL ||
        config == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    memset(
        config,
        0,
        sizeof(*config)
    );

    config->bot.command_prefix = '!';

    file = fopen(filename, "r");

    if (file == NULL)
    {
        log_error(
            "Failed to open configuration file: %s",
            filename
        );

        return BOT_ERR_FILE;
    }

    while (
        fgets(
            line,
            sizeof(line),
            file
        ) != NULL)
    {
        char *content;
        char *separator;
        char *key;
        char *value;

        trim_right(line);
        content = trim_left(line);

        if (*content == '\0')
        {
            continue;
        }

        if (
            *content == '#' ||
            *content == ';')
        {
            continue;
        }

        if (*content == '[')
        {
            current_section = parse_section(content);
            continue;
        }

        separator = strchr(content, '=');

        if (separator == NULL)
        {
            continue;
        }

        *separator = '\0';

        key = trim_left(content);
        trim_right(key);

        value = trim_left(separator + 1);
        trim_right(value);

        parse_key_value(
            current_section,
            key,
            value,
            config
        );
    }

    fclose(file);

    return BOT_OK;
}


BotResult config_validate(
    const AppConfig *config
)
{
    if (config == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    if (config->twitch.broadcaster_login[0] == '\0')
    {
        log_error(
            "Configuration error: twitch.broadcaster_login is empty"
        );

        return BOT_ERR_CONFIG;
    }

    if (config->bot.command_prefix == '\0')
    {
        log_error(
            "Configuration error: bot.command_prefix is empty"
        );

        return BOT_ERR_CONFIG;
    }

    return BOT_OK;
}
