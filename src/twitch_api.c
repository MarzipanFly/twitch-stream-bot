#include "twitch_api.h"

#include "logger.h"
#include "bot_result.h"
#include "http_client.h"
#include "twitch_user.h"
#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>


#define TWITCH_API_HOST L"api.twitch.tv"

#define TWITCH_WIDE_STRING_SIZE 2048
#define TWITCH_HEADER_SIZE 4096
#define TWITCH_PATH_SIZE 1024


/*
 * ================================================================
 * UTF-8 -> UTF-16
 * ================================================================
 *
 * Наш config хранит обычные char строки.
 *
 * WinHTTP использует wchar_t.
 *
 * Поэтому перед передачей Client ID, токена и login
 * переводим строки в wide-string.
 */
static BotResult utf8_to_wide(
    const char *source,
    wchar_t *destination,
    size_t destination_size
)
{
    int result;


    if (source == NULL ||
        destination == NULL ||
        destination_size == 0)
    {
        return BOT_ERR_UNKNOWN;
    }


    result = MultiByteToWideChar(
        CP_UTF8,
        0,
        source,
        -1,
        destination,
        (int)destination_size
    );


    if (result == 0)
    {
        log_error(
            "Failed to convert UTF-8 string to wide string"
        );

        return BOT_ERR_UNKNOWN;
    }


    return BOT_OK;
}


/*
 * ================================================================
 * GET TWITCH USER
 * ================================================================
 *
 * Выполняет:
 *
 * GET https://api.twitch.tv/helix/users?login=USERNAME
 *
 *
 * Заголовки:
 *
 * Authorization: Bearer ACCESS_TOKEN
 * Client-Id: CLIENT_ID
 */
BotResult twitch_get_user(
    const TwitchConfig *config,
    const char *login,
    HttpResponse *response
)
{
    BotResult result;

    wchar_t wide_client_id[
        TWITCH_WIDE_STRING_SIZE
    ];

    wchar_t wide_access_token[
        TWITCH_WIDE_STRING_SIZE
    ];

    wchar_t wide_login[
        TWITCH_WIDE_STRING_SIZE
    ];

    wchar_t headers[
        TWITCH_HEADER_SIZE
    ];

    wchar_t path[
        TWITCH_PATH_SIZE
    ];


    /*
     * ------------------------------------------------------------
     * Проверяем параметры.
     * ------------------------------------------------------------
     */

    if (config == NULL ||
        login == NULL ||
        response == NULL)
    {
        return BOT_ERR_TWITCH;
    }


    if (config->client_id[0] == '\0')
    {
        log_error(
            "Twitch Client ID is empty"
        );

        return BOT_ERR_AUTH;
    }


    if (config->access_token[0] == '\0')
    {
        log_error(
            "Twitch access token is empty"
        );

        return BOT_ERR_AUTH;
    }


    if (login[0] == '\0')
    {
        log_error(
            "Twitch login is empty"
        );

        return BOT_ERR_TWITCH;
    }


    /*
     * ------------------------------------------------------------
     * Переводим Client ID.
     * ------------------------------------------------------------
     */

    result = utf8_to_wide(
        config->client_id,
        wide_client_id,
        sizeof(wide_client_id) /
            sizeof(wide_client_id[0])
    );


    if (result != BOT_OK)
    {
        return result;
    }


    /*
     * ------------------------------------------------------------
     * Переводим Access Token.
     * ------------------------------------------------------------
     */

    result = utf8_to_wide(
        config->access_token,
        wide_access_token,
        sizeof(wide_access_token) /
            sizeof(wide_access_token[0])
    );


    if (result != BOT_OK)
    {
        return result;
    }


    /*
     * ------------------------------------------------------------
     * Переводим login.
     * ------------------------------------------------------------
     */

    result = utf8_to_wide(
        login,
        wide_login,
        sizeof(wide_login) /
            sizeof(wide_login[0])
    );


    if (result != BOT_OK)
    {
        return result;
    }


    /*
     * ------------------------------------------------------------
     * Формируем HTTP-заголовки.
     * ------------------------------------------------------------
     *
     * ВАЖНО:
     *
     * Для Helix используется:
     *
     * Authorization: Bearer TOKEN
     *
     * НЕ:
     *
     * Authorization: OAuth TOKEN
     *
     * OAuth мы использовали только для /oauth2/validate.
     */

    swprintf(
        headers,
        sizeof(headers) /
            sizeof(headers[0]),

        L"Authorization: Bearer %ls\r\n"
        L"Client-Id: %ls\r\n",

        wide_access_token,
        wide_client_id
    );


    /*
     * ------------------------------------------------------------
     * Формируем URL path.
     * ------------------------------------------------------------
     */

    swprintf(
        path,
        sizeof(path) /
            sizeof(path[0]),

        L"/helix/users?login=%ls",

        wide_login
    );


    log_debug(
        "Requesting Twitch user: %s",
        login
    );


    /*
     * ------------------------------------------------------------
     * HTTP GET
     * ------------------------------------------------------------
     */

    result = http_get(
        TWITCH_API_HOST,
        path,
        headers,
        response
    );


    if (result != BOT_OK)
    {
        log_error(
            "Twitch HTTP request failed: %s",
            bot_result_to_string(result)
        );

        return result;
    }


    /*
     * ------------------------------------------------------------
     * Проверяем HTTP status.
     * ------------------------------------------------------------
     */


    if (response->status_code == 200)
    {
        return BOT_OK;
    }


    /*
     * Теперь обязательно показываем BODY ошибки Twitch.
     *
     * Это безопасно:
     *
     * Twitch не возвращает access token в ответе /helix/users.
     *
     * Зато там будет реальная причина 401.
     */
    if (response->body != NULL)
    {
        log_error(
            "Twitch API response: %s",
            response->body
        );
    }


    /*
     * Unauthorized.
     */
    if (response->status_code == 401)
    {
        log_error(
            "Twitch authentication failed (HTTP 401)"
        );

        return BOT_ERR_AUTH;
    }


    /*
     * Bad Request.
     */
    if (response->status_code == 400)
    {
        log_error(
            "Twitch API returned bad request (HTTP 400)"
        );

        return BOT_ERR_TWITCH;
    }


    /*
     * Forbidden.
     */
    if (response->status_code == 403)
    {
        log_error(
            "Twitch API returned forbidden (HTTP 403)"
        );

        return BOT_ERR_AUTH;
    }


    /*
     * Другой HTTP-код.
     */
    log_error(
        "Twitch API returned HTTP %lu",
        response->status_code
    );


    return BOT_ERR_TWITCH;
}


/*
 * ================================================================
 * PARSE TWITCH USER JSON
 * ================================================================
 *
 * Twitch возвращает:
 *
 * {
 *     "data": [
 *         {
 *             "id": "...",
 *             "login": "...",
 *             "display_name": "...",
 *             "broadcaster_type": "...",
 *             "description": "...",
 *             "profile_image_url": "..."
 *         }
 *     ]
 * }
 */
BotResult twitch_parse_user_response(
    const char *json,
    TwitchUser *user
)
{
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *item = NULL;

    cJSON *id_json = NULL;
    cJSON *login_json = NULL;
    cJSON *display_name_json = NULL;
    cJSON *broadcaster_type_json = NULL;
    cJSON *description_json = NULL;
    cJSON *profile_image_url_json = NULL;


    /*
     * ------------------------------------------------------------
     * Проверяем параметры.
     * ------------------------------------------------------------
     */

    if (json == NULL ||
        user == NULL)
    {
        return BOT_ERR_JSON;
    }


    memset(
        user,
        0,
        sizeof(*user)
    );


    /*
     * ------------------------------------------------------------
     * Parse JSON.
     * ------------------------------------------------------------
     */

    root = cJSON_Parse(
        json
    );


    if (root == NULL)
    {
        log_error(
            "Failed to parse Twitch user JSON"
        );

        return BOT_ERR_JSON;
    }


    /*
     * ------------------------------------------------------------
     * Получаем data.
     * ------------------------------------------------------------
     */

    data =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "data"
        );


    if (!cJSON_IsArray(data))
    {
        log_error(
            "Twitch response does not contain data array"
        );


        cJSON_Delete(
            root
        );


        return BOT_ERR_JSON;
    }


    /*
     * Если массив пустой — такого пользователя Twitch не нашёл.
     */
    if (cJSON_GetArraySize(data) == 0)
    {
        log_error(
            "Twitch user was not found"
        );


        cJSON_Delete(
            root
        );


        return BOT_ERR_TWITCH;
    }


    /*
     * Нам нужен первый элемент.
     */
    item =
        cJSON_GetArrayItem(
            data,
            0
        );


    if (!cJSON_IsObject(item))
    {
        log_error(
            "Invalid Twitch user object"
        );


        cJSON_Delete(
            root
        );


        return BOT_ERR_JSON;
    }


    /*
     * ------------------------------------------------------------
     * Получаем поля пользователя.
     * ------------------------------------------------------------
     */

    id_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "id"
        );


    login_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "login"
        );


    display_name_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "display_name"
        );


    broadcaster_type_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "broadcaster_type"
        );


    description_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "description"
        );


    profile_image_url_json =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "profile_image_url"
        );


    /*
     * ID, login и display_name обязательны.
     */
    if (!cJSON_IsString(id_json) ||
        !cJSON_IsString(login_json) ||
        !cJSON_IsString(display_name_json))
    {
        log_error(
            "Invalid Twitch user response"
        );


        cJSON_Delete(
            root
        );


        return BOT_ERR_JSON;
    }


    /*
     * ------------------------------------------------------------
     * Копируем основные поля.
     * ------------------------------------------------------------
     */

    snprintf(
        user->id,
        sizeof(user->id),
        "%s",
        id_json->valuestring
    );


    snprintf(
        user->login,
        sizeof(user->login),
        "%s",
        login_json->valuestring
    );


    snprintf(
        user->display_name,
        sizeof(user->display_name),
        "%s",
        display_name_json->valuestring
    );


    /*
     * ------------------------------------------------------------
     * Необязательные поля.
     * ------------------------------------------------------------
     */

    if (cJSON_IsString(
            broadcaster_type_json
        ))
    {
        snprintf(
            user->broadcaster_type,
            sizeof(user->broadcaster_type),
            "%s",
            broadcaster_type_json->valuestring
        );
    }


    if (cJSON_IsString(
            description_json
        ))
    {
        snprintf(
            user->description,
            sizeof(user->description),
            "%s",
            description_json->valuestring
        );
    }


    if (cJSON_IsString(
            profile_image_url_json
        ))
    {
        snprintf(
            user->profile_image_url,
            sizeof(user->profile_image_url),
            "%s",
            profile_image_url_json->valuestring
        );
    }


    /*
     * cJSON больше не нужен.
     */
    cJSON_Delete(
        root
    );


    return BOT_OK;
}
