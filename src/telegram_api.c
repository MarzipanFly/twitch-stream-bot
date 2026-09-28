#include "telegram_api.h"

#include "http_client.h"
#include "logger.h"
#include "cJSON.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>


#define TELEGRAM_API_HOST L"api.telegram.org"

#define TELEGRAM_PATH_SIZE 512


/*
 * ============================================================
 * UTF-8 -> UTF-16
 * ============================================================
 *
 * WinHTTP принимает host/path как wchar_t.
 *
 * Наши обычные C-строки — UTF-8.
 */
static int utf8_to_wide(
    const char *source,
    wchar_t *destination,
    int destination_size
)
{
    int result;


    if (source == NULL ||
        destination == NULL ||
        destination_size <= 0)
    {
        return 0;
    }


    result =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            source,
            -1,
            destination,
            destination_size
        );


    return result > 0;
}


/*
 * ============================================================
 * TELEGRAM SEND MESSAGE
 * ============================================================
 */
BotResult telegram_send_message(
    const TelegramConfig *config,
    const char *text
)
{
    HttpResponse response =
        {0};

    BotResult result;


    cJSON *request_json = NULL;
    cJSON *response_json = NULL;

    cJSON *ok_item;
    cJSON *description_item;


    char *request_body = NULL;


    char path_utf8[
        TELEGRAM_PATH_SIZE
    ];


    wchar_t path_wide[
        TELEGRAM_PATH_SIZE
    ];


    /*
     * --------------------------------------------------------
     * Проверяем аргументы.
     * --------------------------------------------------------
     */
    if (config == NULL ||
        text == NULL)
    {
        return BOT_ERR_TELEGRAM;
    }


    if (config->bot_token[0] == '\0')
    {
        log_error(
            "Telegram bot token is empty"
        );


        return BOT_ERR_CONFIG;
    }


    if (config->chat_id[0] == '\0')
    {
        log_error(
            "Telegram chat ID is empty"
        );


        return BOT_ERR_CONFIG;
    }


    if (text[0] == '\0')
    {
        log_error(
            "Telegram message text is empty"
        );


        return BOT_ERR_TELEGRAM;
    }


    /*
     * --------------------------------------------------------
     * Формируем путь:
     *
     * /bot<TOKEN>/sendMessage
     *
     * Сам token в лог НЕ выводим.
     * --------------------------------------------------------
     */
    if (snprintf(
            path_utf8,
            sizeof(path_utf8),
            "/bot%s/sendMessage",
            config->bot_token
        ) >=
        (int)sizeof(path_utf8))
    {
        log_error(
            "Telegram API path is too long"
        );


        return BOT_ERR_TELEGRAM;
    }


    if (!utf8_to_wide(
            path_utf8,
            path_wide,
            (int)(
                sizeof(path_wide) /
                sizeof(path_wide[0])
            )
        ))
    {
        log_error(
            "Failed to convert Telegram API path to UTF-16"
        );


        return BOT_ERR_TELEGRAM;
    }


    /*
     * --------------------------------------------------------
     * Создаём JSON:
     *
     * {
     *     "chat_id": "...",
     *     "text": "..."
     * }
     * --------------------------------------------------------
     */
    request_json =
        cJSON_CreateObject();


    if (request_json == NULL)
    {
        log_error(
            "Failed to create Telegram JSON request"
        );


        return BOT_ERR_JSON;
    }


    if (cJSON_AddStringToObject(
            request_json,
            "chat_id",
            config->chat_id
        ) == NULL)
    {
        cJSON_Delete(
            request_json
        );


        return BOT_ERR_JSON;
    }


    if (cJSON_AddStringToObject(
            request_json,
            "text",
            text
        ) == NULL)
    {
        cJSON_Delete(
            request_json
        );


        return BOT_ERR_JSON;
    }


    request_body =
        cJSON_PrintUnformatted(
            request_json
        );


    cJSON_Delete(
        request_json
    );


    request_json = NULL;


    if (request_body == NULL)
    {
        log_error(
            "Failed to serialize Telegram JSON request"
        );


        return BOT_ERR_JSON;
    }


    /*
     * --------------------------------------------------------
     * Выполняем POST.
     * --------------------------------------------------------
     */
    log_debug(
        "Sending Telegram message..."
    );


    result =
        http_post(
            TELEGRAM_API_HOST,
            path_wide,
            L"Content-Type: application/json; charset=utf-8\r\n",
            request_body,
            &response
        );


    cJSON_free(
        request_body
    );


    request_body = NULL;


    if (result != BOT_OK)
    {
        log_error(
            "Telegram HTTP request failed: %s",
            bot_result_to_string(result)
        );


        http_response_free(
            &response
        );


        return result;
    }


    log_debug(
        "Telegram HTTP status: %lu",
        response.status_code
    );


    /*
     * Telegram Bot API при нормальном запросе
     * должен вернуть HTTP 200.
     */
    if (response.status_code != 200)
    {
        log_error(
            "Telegram API returned HTTP %lu",
            response.status_code
        );


        if (response.body != NULL &&
            response.body[0] != '\0')
        {
            log_error(
                "Telegram API response: %s",
                response.body
            );
        }


        http_response_free(
            &response
        );


        return BOT_ERR_TELEGRAM;
    }


    /*
     * --------------------------------------------------------
     * Разбираем ответ.
     *
     * Успех:
     *
     * {
     *     "ok": true,
     *     ...
     * }
     * --------------------------------------------------------
     */
    response_json =
        cJSON_Parse(
            response.body
        );


    if (response_json == NULL)
    {
        log_error(
            "Failed to parse Telegram response"
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    ok_item =
        cJSON_GetObjectItemCaseSensitive(
            response_json,
            "ok"
        );


    if (!cJSON_IsTrue(
            ok_item
        ))
    {
        description_item =
            cJSON_GetObjectItemCaseSensitive(
                response_json,
                "description"
            );


        if (cJSON_IsString(
                description_item
            ) &&
            description_item->valuestring != NULL)
        {
            log_error(
                "Telegram API error: %s",
                description_item->valuestring
            );
        }
        else
        {
            log_error(
                "Telegram API returned an error"
            );
        }


        cJSON_Delete(
            response_json
        );


        http_response_free(
            &response
        );


        return BOT_ERR_TELEGRAM;
    }


    cJSON_Delete(
        response_json
    );


    http_response_free(
        &response
    );


    log_info(
        "Telegram message sent successfully"
    );


    return BOT_OK;
}
