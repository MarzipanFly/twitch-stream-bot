#include "telegram_api.h"

#include "http_client.h"
#include "logger.h"
#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TELEGRAM_API_HOST L"api.telegram.org"
#define TELEGRAM_PATH_SIZE 512

static int utf8_to_wide(
    const char *source,
    wchar_t *destination,
    int destination_size
)
{
    if (source == NULL || destination == NULL || destination_size <= 0)
    {
        return 0;
    }

    return MultiByteToWideChar(
        CP_UTF8,
        0,
        source,
        -1,
        destination,
        destination_size
    ) > 0;
}

/*
 * Экранирует обычную UTF-8 строку для JSON.
 * UTF-8 байты выше ASCII копируются без изменения.
 */
static char *json_escape_string(const char *source)
{
    size_t i;
    size_t length;
    size_t capacity;
    size_t position = 0;
    char *result;

    if (source == NULL)
    {
        return NULL;
    }

    length = strlen(source);
    capacity = length * 6 + 32;

    result = (char *)malloc(capacity);
    if (result == NULL)
    {
        return NULL;
    }

    for (i = 0; i < length; ++i)
    {
        unsigned char c = (unsigned char)source[i];
        const char *escape = NULL;

        switch (c)
        {
            case '"': escape = "\\\""; break;
            case '\\': escape = "\\\\"; break;
            case '\b': escape = "\\b"; break;
            case '\f': escape = "\\f"; break;
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            default: break;
        }

        if (escape != NULL)
        {
            size_t escape_length = strlen(escape);
            memcpy(result + position, escape, escape_length);
            position += escape_length;
        }
        else if (c < 0x20)
        {
            int written = snprintf(
                result + position,
                capacity - position,
                "\\u%04x",
                (unsigned int)c
            );

            if (written < 0)
            {
                free(result);
                return NULL;
            }

            position += (size_t)written;
        }
        else
        {
            result[position++] = (char)c;
        }
    }

    result[position] = '\0';
    return result;
}

BotResult telegram_send_message(
    const TelegramConfig *config,
    const char *text
)
{
    HttpResponse response = {0};
    BotResult result;
    cJSON *response_json = NULL;
    cJSON *ok_item;
    cJSON *description_item;

    char path_utf8[TELEGRAM_PATH_SIZE];
    wchar_t path_wide[TELEGRAM_PATH_SIZE];

    char *escaped_chat_id = NULL;
    char *escaped_text = NULL;
    char *request_body = NULL;
    size_t request_size;

    if (config == NULL || text == NULL)
    {
        return BOT_ERR_TELEGRAM;
    }

    if (config->bot_token[0] == '\0')
    {
        log_error("Telegram bot token is empty");
        return BOT_ERR_CONFIG;
    }

    if (config->chat_id[0] == '\0')
    {
        log_error("Telegram chat ID is empty");
        return BOT_ERR_CONFIG;
    }

    if (text[0] == '\0')
    {
        log_error("Telegram message text is empty");
        return BOT_ERR_TELEGRAM;
    }

    if (snprintf(
            path_utf8,
            sizeof(path_utf8),
            "/bot%s/sendMessage",
            config->bot_token
        ) >= (int)sizeof(path_utf8))
    {
        log_error("Telegram API path is too long");
        return BOT_ERR_TELEGRAM;
    }

    if (!utf8_to_wide(
            path_utf8,
            path_wide,
            (int)(sizeof(path_wide) / sizeof(path_wide[0]))
        ))
    {
        log_error("Failed to convert Telegram API path to UTF-16");
        return BOT_ERR_TELEGRAM;
    }

    escaped_chat_id = json_escape_string(config->chat_id);
    escaped_text = json_escape_string(text);

    if (escaped_chat_id == NULL || escaped_text == NULL)
    {
        free(escaped_chat_id);
        free(escaped_text);
        return BOT_ERR_JSON;
    }

    request_size = strlen(escaped_chat_id) + strlen(escaped_text) + 64;
    request_body = (char *)malloc(request_size);

    if (request_body == NULL)
    {
        free(escaped_chat_id);
        free(escaped_text);
        return BOT_ERR_UNKNOWN;
    }

    snprintf(
        request_body,
        request_size,
        "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
        escaped_chat_id,
        escaped_text
    );

    free(escaped_chat_id);
    free(escaped_text);

    log_debug("Sending Telegram message...");

    result = http_post(
        TELEGRAM_API_HOST,
        path_wide,
        L"Content-Type: application/json; charset=utf-8\r\n",
        request_body,
        &response
    );

    free(request_body);

    if (result != BOT_OK)
    {
        log_error(
            "Telegram HTTP request failed: %s",
            bot_result_to_string(result)
        );
        http_response_free(&response);
        return result;
    }

    log_debug(
        "Telegram HTTP status: %lu",
        response.status_code
    );

    if (response.status_code != 200)
    {
        log_error(
            "Telegram API returned HTTP %lu",
            response.status_code
        );

        if (response.body != NULL && response.body[0] != '\0')
        {
            log_error("Telegram API response: %s", response.body);
        }

        http_response_free(&response);
        return BOT_ERR_TELEGRAM;
    }

    response_json = cJSON_Parse(response.body);
    if (response_json == NULL)
    {
        log_error("Failed to parse Telegram response");
        http_response_free(&response);
        return BOT_ERR_JSON;
    }

    ok_item = cJSON_GetObjectItemCaseSensitive(response_json, "ok");

    if (ok_item == NULL || (ok_item->type & cJSON_True) == 0)
    {
        description_item = cJSON_GetObjectItemCaseSensitive(
            response_json,
            "description"
        );

        if (cJSON_IsString(description_item) &&
            description_item->valuestring != NULL)
        {
            log_error(
                "Telegram API error: %s",
                description_item->valuestring
            );
        }
        else
        {
            log_error("Telegram API returned an error");
        }

        cJSON_Delete(response_json);
        http_response_free(&response);
        return BOT_ERR_TELEGRAM;
    }

    cJSON_Delete(response_json);
    http_response_free(&response);

    log_info("Telegram message sent successfully");
    return BOT_OK;
}
