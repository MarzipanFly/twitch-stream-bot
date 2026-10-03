#include "twitch_refresh.h"

#include "http_client.h"
#include "logger.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>

#define TWITCH_AUTH_HOST L"id.twitch.tv"
#define TWITCH_TOKEN_PATH L"/oauth2/token"

static int url_encode_component(
    const char *input,
    char *output,
    size_t output_size
)
{
    static const char hex[] =
        "0123456789ABCDEF";

    size_t input_index = 0;
    size_t output_index = 0;

    if (input == NULL ||
        output == NULL ||
        output_size == 0)
    {
        return 0;
    }

    while (input[input_index] != '\0')
    {
        unsigned char c =
            (unsigned char)input[input_index];

        int safe =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~';

        if (safe)
        {
            if (output_index + 1 >=
                output_size)
            {
                return 0;
            }

            output[output_index++] =
                (char)c;
        }
        else
        {
            if (output_index + 3 >=
                output_size)
            {
                return 0;
            }

            output[output_index++] = '%';
            output[output_index++] =
                hex[(c >> 4) & 0x0F];
            output[output_index++] =
                hex[c & 0x0F];
        }

        ++input_index;
    }

    output[output_index] = '\0';

    return 1;
}

BotResult twitch_refresh_access_token(
    const TwitchConfig *config,
    const char *refresh_token,
    TwitchAuthToken *new_token
)
{
    HttpResponse response = {0};

    BotResult result;

    cJSON *root = NULL;

    cJSON *access_token_json = NULL;
    cJSON *refresh_token_json = NULL;
    cJSON *expires_in_json = NULL;
    cJSON *message_json = NULL;

    char encoded_refresh_token[8192];
    char body[10000];

    if (config == NULL ||
        refresh_token == NULL ||
        new_token == NULL)
    {
        return BOT_ERR_AUTH;
    }

    if (config->client_id[0] == '\0' ||
        refresh_token[0] == '\0')
    {
        return BOT_ERR_AUTH;
    }

    memset(
        new_token,
        0,
        sizeof(*new_token)
    );

    if (!url_encode_component(
            refresh_token,
            encoded_refresh_token,
            sizeof(encoded_refresh_token)))
    {
        log_error(
            "Failed to URL encode Twitch refresh token"
        );

        return BOT_ERR_AUTH;
    }

    snprintf(
        body,
        sizeof(body),
        "grant_type=refresh_token"
        "&refresh_token=%s"
        "&client_id=%s",
        encoded_refresh_token,
        config->client_id
    );

    log_info(
        "Refreshing Twitch access token..."
    );

    result =
        http_post(
            TWITCH_AUTH_HOST,
            TWITCH_TOKEN_PATH,
            L"Content-Type: application/x-www-form-urlencoded\r\n",
            body,
            &response
        );

    if (result != BOT_OK)
    {
        http_response_free(
            &response
        );

        return result;
    }

    if (response.body == NULL)
    {
        http_response_free(
            &response
        );

        return BOT_ERR_JSON;
    }

    root =
        cJSON_Parse(
            response.body
        );

    if (root == NULL)
    {
        log_error(
            "Failed to parse Twitch refresh response"
        );

        http_response_free(
            &response
        );

        return BOT_ERR_JSON;
    }

    if (response.status_code != 200)
    {
        message_json =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "message"
            );

        if (cJSON_IsString(
                message_json))
        {
            log_error(
                "Twitch refresh failed: %s",
                message_json->valuestring
            );
        }
        else
        {
            log_error(
                "Twitch refresh failed with HTTP %lu",
                response.status_code
            );
        }

        cJSON_Delete(root);
        http_response_free(&response);

        return BOT_ERR_AUTH;
    }

    access_token_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "access_token"
        );

    refresh_token_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "refresh_token"
        );

    expires_in_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "expires_in"
        );

    if (!cJSON_IsString(
            access_token_json) ||
        !cJSON_IsString(
            refresh_token_json) ||
        !cJSON_IsNumber(
            expires_in_json))
    {
        log_error(
            "Invalid Twitch refresh response"
        );

        cJSON_Delete(root);
        http_response_free(&response);

        return BOT_ERR_JSON;
    }

    snprintf(
        new_token->access_token,
        sizeof(new_token->access_token),
        "%s",
        access_token_json->valuestring
    );

    snprintf(
        new_token->refresh_token,
        sizeof(new_token->refresh_token),
        "%s",
        refresh_token_json->valuestring
    );

    new_token->expires_in =
        expires_in_json->valueint;

    cJSON_Delete(root);
    http_response_free(&response);

    log_info(
        "Twitch access token refreshed successfully"
    );

    return BOT_OK;
}
