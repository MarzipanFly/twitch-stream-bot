#include "twitch_stream.h"

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

BotResult twitch_get_stream(
    const TwitchConfig *config,
    const char *user_id,
    HttpResponse *response
)
{
    wchar_t wide_client_id[1024];
    wchar_t wide_access_token[2048];
    wchar_t wide_user_id[256];

    wchar_t headers[4096];
    wchar_t path[1024];

    BotResult result;

    if (config == NULL ||
        user_id == NULL ||
        response == NULL)
    {
        return BOT_ERR_TWITCH;
    }

    if (config->client_id[0] == '\0' ||
        config->access_token[0] == '\0' ||
        user_id[0] == '\0')
    {
        return BOT_ERR_CONFIG;
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
            user_id,
            wide_user_id,
            (int)(sizeof(wide_user_id) /
                  sizeof(wide_user_id[0]))))
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
        L"/helix/streams?user_id=%ls",
        wide_user_id
    );

    log_debug(
        "Requesting Twitch stream status for user ID: %s",
        user_id
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
        "Twitch stream HTTP status: %lu",
        response->status_code
    );

    if (response->status_code == 200)
    {
        return BOT_OK;
    }

    if (response->status_code == 401)
    {
        log_error(
            "Twitch stream request authentication failed"
        );

        return BOT_ERR_AUTH;
    }

    if (response->body != NULL)
    {
        log_error(
            "Twitch stream API error: %s",
            response->body
        );
    }

    return BOT_ERR_TWITCH;
}

BotResult twitch_parse_stream_response(
    const char *json,
    TwitchStream *stream
)
{
    cJSON *root;
    cJSON *data;
    cJSON *item;
    cJSON *viewer_count;

    if (json == NULL ||
        stream == NULL)
    {
        return BOT_ERR_JSON;
    }

    memset(
        stream,
        0,
        sizeof(*stream)
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

    if (!cJSON_IsArray(data))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    /*
     * Empty data array = broadcaster is offline.
     */
    if (cJSON_GetArraySize(data) == 0)
    {
        stream->is_live = 0;

        cJSON_Delete(root);

        return BOT_OK;
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

    stream->is_live = 1;

    copy_json_string(
        item,
        "id",
        stream->id,
        sizeof(stream->id)
    );

    copy_json_string(
        item,
        "user_id",
        stream->user_id,
        sizeof(stream->user_id)
    );

    copy_json_string(
        item,
        "user_login",
        stream->user_login,
        sizeof(stream->user_login)
    );

    copy_json_string(
        item,
        "user_name",
        stream->user_name,
        sizeof(stream->user_name)
    );

    copy_json_string(
        item,
        "game_id",
        stream->game_id,
        sizeof(stream->game_id)
    );

    copy_json_string(
        item,
        "game_name",
        stream->game_name,
        sizeof(stream->game_name)
    );

    copy_json_string(
        item,
        "title",
        stream->title,
        sizeof(stream->title)
    );

    copy_json_string(
        item,
        "started_at",
        stream->started_at,
        sizeof(stream->started_at)
    );

    copy_json_string(
        item,
        "language",
        stream->language,
        sizeof(stream->language)
    );

    viewer_count =
        cJSON_GetObjectItemCaseSensitive(
            item,
            "viewer_count"
        );

    if (cJSON_IsNumber(viewer_count))
    {
        stream->viewer_count =
            viewer_count->valueint;
    }

    cJSON_Delete(root);

    return BOT_OK;
}
