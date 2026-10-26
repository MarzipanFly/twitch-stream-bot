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


static int url_encode_query(const char *input, char *output, size_t output_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, used = 0;

    if (!input || !output || output_size == 0) return 0;
    for (i = 0; input[i] != '\0'; ++i)
    {
        unsigned char ch = (unsigned char)input[i];
        int safe = (ch >= 'A' && ch <= 'Z') ||
                   (ch >= 'a' && ch <= 'z') ||
                   (ch >= '0' && ch <= '9') ||
                   ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (safe)
        {
            if (used + 1 >= output_size) return 0;
            output[used++] = (char)ch;
        }
        else
        {
            if (used + 3 >= output_size) return 0;
            output[used++] = '%';
            output[used++] = hex[(ch >> 4) & 0x0F];
            output[used++] = hex[ch & 0x0F];
        }
    }
    output[used] = '\0';
    return 1;
}

static void normalize_category_name(const char *input, char *output, size_t capacity)
{
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)input;
    if (!output || capacity == 0) return;
    output[0] = '\0';
    if (!input) return;

    while (*p && used + 1 < capacity)
    {
        unsigned char ch = *p++;
        if (ch >= 'A' && ch <= 'Z')
            output[used++] = (char)(ch - 'A' + 'a');
        else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            output[used++] = (char)ch;
        /* Ignore ASCII punctuation/whitespace and UTF-8 decoration bytes. */
    }
    output[used] = '\0';
}

static int category_match_score(const char *query, const char *name)
{
    char normalized_query[512], normalized_name[512];
    size_t qlen, nlen;

    if (!query || !name) return 0;
    if (strcmp(query, name) == 0) return 10000;

    normalize_category_name(query, normalized_query, sizeof(normalized_query));
    normalize_category_name(name, normalized_name, sizeof(normalized_name));
    if (!normalized_query[0] || !normalized_name[0]) return 0;
    if (strcmp(normalized_query, normalized_name) == 0) return 10000;

    qlen = strlen(normalized_query);
    nlen = strlen(normalized_name);
    if (nlen >= 5 && qlen >= nlen &&
        strncmp(normalized_query, normalized_name, nlen) == 0)
        return (int)nlen;
    if (qlen >= 5 && nlen >= qlen &&
        strncmp(normalized_name, normalized_query, qlen) == 0)
        return (int)qlen;
    return 0;
}

BotResult twitch_search_category(
    const TwitchConfig *config,
    const char *query,
    char *category_id,
    size_t category_id_size,
    char *category_name,
    size_t category_name_size
)
{
    wchar_t wide_client_id[1024];
    wchar_t wide_access_token[2048];
    wchar_t wide_query[2048];
    wchar_t wide_encoded_query[2048];
    wchar_t headers[4096];
    wchar_t path[4096];
    char encoded_query[2048];
    HttpResponse response = {0};
    BotResult result;
    cJSON *root = NULL;
    cJSON *data = NULL;
    cJSON *item = NULL;
    cJSON *id_json = NULL;
    cJSON *name_json = NULL;
    char best_id[64] = "";
    char best_name[256] = "";
    int best_score = 0;
    int ambiguous = 0;
    int i, count;

    if (!config || !query || !query[0] || !category_id ||
        category_id_size == 0 || config->client_id[0] == '\0' ||
        config->access_token[0] == '\0')
        return BOT_ERR_CONFIG;

    category_id[0] = '\0';
    if (category_name && category_name_size) category_name[0] = '\0';

    if (!url_encode_query(query, encoded_query, sizeof(encoded_query)) ||
        !utf8_to_wide(config->client_id, wide_client_id,
                      (int)(sizeof(wide_client_id) / sizeof(wide_client_id[0]))) ||
        !utf8_to_wide(config->access_token, wide_access_token,
                      (int)(sizeof(wide_access_token) / sizeof(wide_access_token[0]))) ||
        !utf8_to_wide(encoded_query, wide_encoded_query,
                      (int)(sizeof(wide_encoded_query) / sizeof(wide_encoded_query[0]))))
        return BOT_ERR_TWITCH;

    swprintf(headers, sizeof(headers) / sizeof(headers[0]),
             L"Authorization: Bearer %ls\r\nClient-Id: %ls\r\n",
             wide_access_token, wide_client_id);
    swprintf(path, sizeof(path) / sizeof(path[0]),
             L"/helix/search/categories?query=%ls&first=10", wide_encoded_query);

    result = http_get(TWITCH_API_HOST, path, headers, &response);
    if (result != BOT_OK)
    {
        http_response_free(&response);
        return result;
    }
    if (response.status_code == 401 || response.status_code == 403)
    {
        http_response_free(&response);
        return BOT_ERR_AUTH;
    }
    if (response.status_code != 200 || !response.body)
    {
        log_warning("Twitch category search returned HTTP %lu", response.status_code);
        http_response_free(&response);
        return BOT_ERR_TWITCH;
    }

    root = cJSON_Parse(response.body);
    http_response_free(&response);
    if (!root) return BOT_ERR_JSON;

    data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data))
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    count = cJSON_GetArraySize(data);
    for (i = 0; i < count; ++i)
    {
        int score;
        item = cJSON_GetArrayItem(data, i);
        id_json = cJSON_GetObjectItemCaseSensitive(item, "id");
        name_json = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(id_json) || !id_json->valuestring ||
            !cJSON_IsString(name_json) || !name_json->valuestring)
            continue;

        score = category_match_score(query, name_json->valuestring);
        if (score <= 0) continue;
        if (score > best_score)
        {
            best_score = score;
            ambiguous = 0;
            snprintf(best_id, sizeof(best_id), "%s", id_json->valuestring);
            snprintf(best_name, sizeof(best_name), "%s", name_json->valuestring);
        }
        else if (score == best_score && strcmp(best_id, id_json->valuestring) != 0)
        {
            ambiguous = 1;
        }
    }

    cJSON_Delete(root);
    if (best_score <= 0 || ambiguous)
    {
        log_warning("Could not identify an unambiguous Twitch category for '%s'", query);
        return BOT_ERR_TWITCH;
    }

    if (strlen(best_id) >= category_id_size) return BOT_ERR_CONFIG;
    snprintf(category_id, category_id_size, "%s", best_id);
    if (category_name && category_name_size)
        snprintf(category_name, category_name_size, "%s", best_name);
    return BOT_OK;
}

BotResult twitch_update_channel_category(
    const TwitchConfig *config,
    const char *broadcaster_id,
    const char *category_id
)
{
    wchar_t wide_client_id[1024];
    wchar_t wide_access_token[2048];
    wchar_t wide_broadcaster_id[128];
    wchar_t headers[4096];
    wchar_t path[2048];
    char body[256];
    HttpResponse response = {0};
    BotResult result;

    if (!config || !broadcaster_id || !broadcaster_id[0] ||
        !category_id || !category_id[0] ||
        config->client_id[0] == '\0' || config->access_token[0] == '\0')
        return BOT_ERR_CONFIG;

    if (!utf8_to_wide(config->client_id, wide_client_id,
                      (int)(sizeof(wide_client_id) / sizeof(wide_client_id[0]))) ||
        !utf8_to_wide(config->access_token, wide_access_token,
                      (int)(sizeof(wide_access_token) / sizeof(wide_access_token[0]))) ||
        !utf8_to_wide(broadcaster_id, wide_broadcaster_id,
                      (int)(sizeof(wide_broadcaster_id) / sizeof(wide_broadcaster_id[0]))))
        return BOT_ERR_TWITCH;

    swprintf(headers, sizeof(headers) / sizeof(headers[0]),
             L"Authorization: Bearer %ls\r\nClient-Id: %ls\r\n"
             L"Content-Type: application/json\r\n",
             wide_access_token, wide_client_id);
    swprintf(path, sizeof(path) / sizeof(path[0]),
             L"/helix/channels?broadcaster_id=%ls", wide_broadcaster_id);

    if (snprintf(body, sizeof(body), "{\"game_id\":\"%s\"}", category_id) >= (int)sizeof(body))
        return BOT_ERR_CONFIG;

    result = http_patch(TWITCH_API_HOST, path, headers, body, &response);
    if (result != BOT_OK)
    {
        http_response_free(&response);
        return result;
    }
    if (response.status_code == 204)
    {
        http_response_free(&response);
        return BOT_OK;
    }
    if (response.status_code == 401 || response.status_code == 403)
    {
        http_response_free(&response);
        return BOT_ERR_AUTH;
    }

    log_warning("Twitch category update returned HTTP %lu: %s",
                response.status_code,
                response.body ? response.body : "(empty response)");
    http_response_free(&response);
    return BOT_ERR_TWITCH;
}
