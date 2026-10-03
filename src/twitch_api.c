#include "twitch_api.h"

#include "logger.h"
#include "cJSON.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define TWITCH_API_HOST L"api.twitch.tv"

static int utf8_to_wide(
    const char *input,
    wchar_t *output,
    int output_size
)
{
    int result;

    if (input == NULL ||
        output == NULL ||
        output_size <= 0)
    {
        return 0;
    }

    result =
        MultiByteToWideChar(
            CP_UTF8,
            0,
            input,
            -1,
            output,
            output_size
        );

    return result > 0;
}

static void copy_json_string(
    cJSON *object,
    const char *field_name,
    char *destination,
    size_t destination_size
)
{
    cJSON *item;

    if (object == NULL ||
        field_name == NULL ||
        destination == NULL ||
        destination_size == 0)
    {
        return;
    }

    item =
        cJSON_GetObjectItemCaseSensitive(
            object,
            field_name
        );

    if (!cJSON_IsString(item) ||
        item->valuestring == NULL)
    {
        destination[0] = '\0';
        return;
    }

    snprintf(
        destination,
        destination_size,
        "%s",
        item->valuestring
    );
}

BotResult twitch_get_user(
    const TwitchConfig *config,
    const char *login,
    HttpResponse *response
)
{
    wchar_t wide_client_id[1024];
    wchar_t wide_access_token[2048];
    wchar_t wide_login[1024];

    wchar_t headers[4096];
    wchar_t path[2048];

    BotResult result;

    if (config == NULL ||
        login == NULL ||
        response == NULL)
    {
        return BOT_ERR_TWITCH;
    }

    if (config->client_id[0] == '\0' ||
        config->access_token[0] == '\0')
    {
        return BOT_ERR_AUTH;
    }

    if (!utf8_to_wide(
            config->client_id,
            wide_client_id,
            (int)(sizeof(wide_client_id) /
                  sizeof(wide_client_id[0]))))
    {
        return BOT_ERR_TWITCH;
    }

    if (!utf8_to_wide(
            config->access_token,
            wide_access_token,
            (int)(sizeof(wide_access_token) /
                  sizeof(wide_access_token[0]))))
    {
        return BOT_ERR_TWITCH;
    }

    if (!utf8_to_wide(
            login,
            wide_login,
            (int)(sizeof(wide_login) /
                  sizeof(wide_login[0]))))
    {
        return BOT_ERR_TWITCH;
    }

    swprintf(
        headers,
        sizeof(headers) /
            sizeof(headers[0]),
        L"Authorization: Bearer %ls\r\n"
        L"Client-Id: %ls\r\n",
        wide_access_token,
        wide_client_id
    );

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

    result =
        http_get(
            TWITCH_API_HOST,
            path,
            headers,
            response
        );

    if (result != BOT_OK)
    {
        return result;
    }

    log_debug(
        "Twitch HTTP status: %lu",
        response->status_code
    );

    if (response->status_code == 200)
    {
        return BOT_OK;
    }

    if (response->body != NULL)
    {
        log_error(
            "Twitch API error response: %s",
            response->body
        );
    }

    if (response->status_code == 401 ||
        response->status_code == 403)
    {
        return BOT_ERR_AUTH;
    }

    return BOT_ERR_TWITCH;
}

BotResult twitch_parse_user_response(
    const char *json,
    TwitchUser *user
)
{
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *item = NULL;

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

    root =
        cJSON_Parse(
            json
        );

    if (root == NULL)
    {
        return BOT_ERR_JSON;
    }

    data =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "data"
        );

    if (!cJSON_IsArray(data) ||
        cJSON_GetArraySize(data) == 0)
    {
        cJSON_Delete(root);
        return BOT_ERR_TWITCH;
    }

    item =
        cJSON_GetArrayItem(
            data,
            0
        );

    if (!cJSON_IsObject(item))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    copy_json_string(
        item,
        "id",
        user->id,
        sizeof(user->id)
    );

    copy_json_string(
        item,
        "login",
        user->login,
        sizeof(user->login)
    );

    copy_json_string(
        item,
        "display_name",
        user->display_name,
        sizeof(user->display_name)
    );

    copy_json_string(
        item,
        "broadcaster_type",
        user->broadcaster_type,
        sizeof(user->broadcaster_type)
    );

    copy_json_string(
        item,
        "description",
        user->description,
        sizeof(user->description)
    );

    copy_json_string(
        item,
        "profile_image_url",
        user->profile_image_url,
        sizeof(user->profile_image_url)
    );

    if (user->id[0] == '\0' ||
        user->login[0] == '\0')
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    cJSON_Delete(root);

    return BOT_OK;
}
