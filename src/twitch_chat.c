#include "twitch_chat.h"

#include "http_client.h"
#include "logger.h"
#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TWITCH_API_HOST L"api.twitch.tv"
#define TWITCH_CHAT_MESSAGES_PATH L"/helix/chat/messages"

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

BotResult twitch_chat_send_message(
    const TwitchConfig *config,
    const char *message
)
{
    HttpResponse response = {0};
    BotResult result;
    cJSON *response_root = NULL;
    cJSON *data = NULL;
    cJSON *item = NULL;
    cJSON *is_sent = NULL;
    cJSON *drop_reason = NULL;
    cJSON *drop_code = NULL;
    cJSON *drop_message = NULL;

    char *escaped_broadcaster_id = NULL;
    char *escaped_sender_id = NULL;
    char *escaped_message = NULL;
    char *request_body = NULL;
    size_t request_size;

    wchar_t wide_token[2048];
    wchar_t wide_client_id[512];
    wchar_t headers[4096];
    int convert_result;

    if (config == NULL || message == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    if (message[0] == '\0')
    {
        log_error("Cannot send Twitch chat message: message is empty");
        return BOT_ERR_CONFIG;
    }

    if (config->access_token[0] == '\0')
    {
        log_error("Cannot send Twitch chat message: access token is empty");
        return BOT_ERR_AUTH;
    }

    if (config->client_id[0] == '\0')
    {
        log_error("Cannot send Twitch chat message: Client ID is empty");
        return BOT_ERR_CONFIG;
    }

    if (config->broadcaster_id[0] == '\0')
    {
        log_error("Cannot send Twitch chat message: broadcaster ID is empty");
        return BOT_ERR_CONFIG;
    }

    if (config->bot_user_id[0] == '\0')
    {
        log_error("Cannot send Twitch chat message: bot user ID is empty");
        return BOT_ERR_CONFIG;
    }

    escaped_broadcaster_id = json_escape_string(config->broadcaster_id);
    escaped_sender_id = json_escape_string(config->bot_user_id);
    escaped_message = json_escape_string(message);

    if (escaped_broadcaster_id == NULL ||
        escaped_sender_id == NULL ||
        escaped_message == NULL)
    {
        free(escaped_broadcaster_id);
        free(escaped_sender_id);
        free(escaped_message);
        return BOT_ERR_JSON;
    }

    request_size =
        strlen(escaped_broadcaster_id) +
        strlen(escaped_sender_id) +
        strlen(escaped_message) +
        128;

    request_body = (char *)malloc(request_size);
    if (request_body == NULL)
    {
        free(escaped_broadcaster_id);
        free(escaped_sender_id);
        free(escaped_message);
        return BOT_ERR_UNKNOWN;
    }

    snprintf(
        request_body,
        request_size,
        "{\"broadcaster_id\":\"%s\",\"sender_id\":\"%s\",\"message\":\"%s\"}",
        escaped_broadcaster_id,
        escaped_sender_id,
        escaped_message
    );

    free(escaped_broadcaster_id);
    free(escaped_sender_id);
    free(escaped_message);

    convert_result = MultiByteToWideChar(
        CP_UTF8,
        0,
        config->access_token,
        -1,
        wide_token,
        (int)(sizeof(wide_token) / sizeof(wide_token[0]))
    );

    if (convert_result == 0)
    {
        log_error("Failed to convert Twitch access token");
        free(request_body);
        return BOT_ERR_AUTH;
    }

    convert_result = MultiByteToWideChar(
        CP_UTF8,
        0,
        config->client_id,
        -1,
        wide_client_id,
        (int)(sizeof(wide_client_id) / sizeof(wide_client_id[0]))
    );

    if (convert_result == 0)
    {
        log_error("Failed to convert Twitch Client ID");
        free(request_body);
        return BOT_ERR_CONFIG;
    }

    swprintf(
        headers,
        sizeof(headers) / sizeof(headers[0]),
        L"Authorization: Bearer %ls\r\n"
        L"Client-Id: %ls\r\n"
        L"Content-Type: application/json\r\n",
        wide_token,
        wide_client_id
    );

    log_info("Sending Twitch chat message...");

    result = http_post(
        TWITCH_API_HOST,
        TWITCH_CHAT_MESSAGES_PATH,
        headers,
        request_body,
        &response
    );

    free(request_body);

    if (result != BOT_OK)
    {
        http_response_free(&response);
        return result;
    }

    log_debug(
        "Twitch chat HTTP status: %lu",
        response.status_code
    );

    if (response.status_code != 200)
    {
        unsigned long status_code = response.status_code;

        log_error(
            "Twitch chat API returned HTTP %lu",
            status_code
        );

        if (response.body != NULL && response.body[0] != '\0')
        {
            log_debug("Twitch chat response: %s", response.body);
        }

        http_response_free(&response);

        if (status_code == 401 || status_code == 403)
        {
            return BOT_ERR_AUTH;
        }

        return BOT_ERR_TWITCH;
    }

    if (response.body == NULL || response.body[0] == '\0')
    {
        log_error("Twitch chat response body is empty");
        http_response_free(&response);
        return BOT_ERR_JSON;
    }

    response_root = cJSON_Parse(response.body);
    if (response_root == NULL)
    {
        log_error("Failed to parse Twitch chat response JSON");
        http_response_free(&response);
        return BOT_ERR_JSON;
    }

    data = cJSON_GetObjectItemCaseSensitive(response_root, "data");

    if (!cJSON_IsArray(data) || cJSON_GetArraySize(data) < 1)
    {
        log_error("Twitch chat response does not contain data");
        cJSON_Delete(response_root);
        http_response_free(&response);
        return BOT_ERR_JSON;
    }

    item = cJSON_GetArrayItem(data, 0);
    if (!cJSON_IsObject(item))
    {
        cJSON_Delete(response_root);
        http_response_free(&response);
        return BOT_ERR_JSON;
    }

    is_sent = cJSON_GetObjectItemCaseSensitive(item, "is_sent");

    if (is_sent == NULL || (is_sent->type & cJSON_True) == 0)
    {
        log_error("Twitch did not send the chat message");

        drop_reason = cJSON_GetObjectItemCaseSensitive(item, "drop_reason");
        if (cJSON_IsObject(drop_reason))
        {
            drop_code = cJSON_GetObjectItemCaseSensitive(drop_reason, "code");
            drop_message = cJSON_GetObjectItemCaseSensitive(drop_reason, "message");

            if (cJSON_IsString(drop_code) && drop_code->valuestring != NULL)
            {
                log_error("Twitch drop reason code: %s", drop_code->valuestring);
            }

            if (cJSON_IsString(drop_message) && drop_message->valuestring != NULL)
            {
                log_error("Twitch drop reason: %s", drop_message->valuestring);
            }
        }

        cJSON_Delete(response_root);
        http_response_free(&response);
        return BOT_ERR_TWITCH;
    }

    log_info("Twitch chat message sent successfully");

    cJSON_Delete(response_root);
    http_response_free(&response);

    return BOT_OK;
}
