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
    SECTION_BOT
} ConfigSection;

/*
 * Удаляет пробелы в начале строки.
 */
static char *trim_left(char *str)
{
    while (*str != '\0' && isspace((unsigned char)*str))
        str++;

    return str;
}

/*
* Удаляет пробелы и перенос строки в конце.
*/
static void trim_right(char *str)
{
   size_t length;

   length = strlen(str);

   while (length > 0 &&
          isspace((unsigned char)str[length - 1]))
   {
       str[length - 1] = '\0';
       length--;
   }
}

/*
* Определяет секцию INI-файла.
*/
static ConfigSection parse_section(const char *line)
{
   if (strcmp(line, "[twitch]") == 0)
   {
       return SECTION_TWITCH;
   }

   if (strcmp(line, "[telegram]") == 0)
   {
       return SECTION_TELEGRAM;
   }

   if (strcmp(line, "[bot]") == 0)
   {
       return SECTION_BOT;
   }

   return SECTION_NONE;
}

/*
* Записывает строку в буфер ограниченного размера.
*/
static void copy_string(
   char *destination,
   size_t destination_size,
   const char *source
)
{
   if (destination_size == 0)
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
* Обрабатывает одну пару:
*
* key=value
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

            break;


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


BotResult config_load(const char *filename, AppConfig *config)
{
   FILE *file;

   char line[512];

   ConfigSection current_section = SECTION_NONE;


   if (filename == NULL || config == NULL)
   {
	   return BOT_ERR_CONFIG;
   }


   /*
    * Значения по умолчанию.
    */
   memset(config, 0, sizeof(*config));

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


   while (fgets(line, sizeof(line), file) != NULL)
   {
       char *content;
       char *separator;

       char *key;
       char *value;


       trim_right(line);

       content = trim_left(line);


       /*
        * Пустая строка.
        */
       if (*content == '\0')
       {
           continue;
       }


       /*
        * Комментарий.
        */
       if (*content == '#' || *content == ';')
       {
           continue;
       }


       /*
        * Секция.
        */
       if (*content == '[')
       {
           current_section = parse_section(content);
           continue;
       }


       /*
        * Ищем символ =
        */
       separator = strchr(content, '=');

       if (separator == NULL)
       {
           continue;
       }


       /*
        * Разбиваем строку:
        *
        * key=value
        *
        * на две отдельных строки.
        */
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

BotResult config_validate(const AppConfig *config)
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
