#include "twitch_auth.h"

#include "http_client.h"
#include "logger.h"
#include "bot_result.h"
#include "cJSON.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>


#define TWITCH_AUTH_HOST L"id.twitch.tv"

#define TWITCH_AUTH_DEVICE_PATH L"/oauth2/device"
#define TWITCH_AUTH_TOKEN_PATH L"/oauth2/token"
#define TWITCH_AUTH_VALIDATE_PATH L"/oauth2/validate"


BotResult twitch_auth_request_device_code(
    const TwitchConfig *config,
    TwitchDeviceCode *device
)
{
    HttpResponse response = {0};
    BotResult result;

    cJSON *root = NULL;

    cJSON *device_code_json = NULL;
    cJSON *user_code_json = NULL;
    cJSON *verification_uri_json = NULL;
    cJSON *expires_in_json = NULL;
    cJSON *interval_json = NULL;

    char body[1024];


    if (config == NULL ||
        device == NULL)
    {
        return BOT_ERR_AUTH;
    }


    if (config->client_id[0] == '\0')
    {
        log_error(
            "Cannot start Twitch authentication: Client ID is empty"
        );

        return BOT_ERR_AUTH;
    }


    memset(
        device,
        0,
        sizeof(*device)
    );


    snprintf(
        body,
        sizeof(body),

        "client_id=%s"
        "&scopes="
        "user%%3Aread%%3Achat"
        "%%20"
        "user%%3Awrite%%3Achat",

        config->client_id
    );


    log_info(
        "Requesting Twitch device authorization code..."
    );


    result = http_post(
        TWITCH_AUTH_HOST,
        TWITCH_AUTH_DEVICE_PATH,
        L"Content-Type: application/x-www-form-urlencoded\r\n",
        body,
        &response
    );


    if (result != BOT_OK)
    {
        log_error(
            "Twitch device authorization request failed: %s",
            bot_result_to_string(result)
        );

        http_response_free(
            &response
        );

        return result;
    }


    log_debug(
        "Twitch OAuth HTTP status: %lu",
        response.status_code
    );


    if (response.status_code != 200)
    {
        log_error(
            "Twitch OAuth returned HTTP %lu",
            response.status_code
        );


        if (response.body != NULL)
        {
            log_debug(
                "Twitch OAuth response: %s",
                response.body
            );
        }


        http_response_free(
            &response
        );

        return BOT_ERR_AUTH;
    }


    if (response.body == NULL)
    {
        log_error(
            "Twitch OAuth response body is empty"
        );

        http_response_free(
            &response
        );

        return BOT_ERR_JSON;
    }


    root = cJSON_Parse(
        response.body
    );


    if (root == NULL)
    {
        log_error(
            "Failed to parse Twitch OAuth JSON"
        );

        http_response_free(
            &response
        );

        return BOT_ERR_JSON;
    }


    device_code_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "device_code"
        );


    user_code_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "user_code"
        );


    verification_uri_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "verification_uri"
        );


    expires_in_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "expires_in"
        );


    interval_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "interval"
        );


    if (!cJSON_IsString(device_code_json) ||
        !cJSON_IsString(user_code_json) ||
        !cJSON_IsString(verification_uri_json) ||
        !cJSON_IsNumber(expires_in_json) ||
        !cJSON_IsNumber(interval_json))
    {
        log_error(
            "Invalid Twitch device authorization response"
        );


        cJSON_Delete(
            root
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    snprintf(
        device->device_code,
        sizeof(device->device_code),
        "%s",
        device_code_json->valuestring
    );


    snprintf(
        device->user_code,
        sizeof(device->user_code),
        "%s",
        user_code_json->valuestring
    );


    snprintf(
        device->verification_uri,
        sizeof(device->verification_uri),
        "%s",
        verification_uri_json->valuestring
    );


    device->expires_in =
        expires_in_json->valueint;


    device->interval =
        interval_json->valueint;


    cJSON_Delete(
        root
    );


    http_response_free(
        &response
    );


    return BOT_OK;
}


BotResult twitch_auth_poll_token(
    const TwitchConfig *config,
    const TwitchDeviceCode *device,
    TwitchAuthToken *token
)
{
    HttpResponse response = {0};
    BotResult result;

    cJSON *root = NULL;

    cJSON *access_token_json = NULL;
    cJSON *refresh_token_json = NULL;
    cJSON *expires_in_json = NULL;

    cJSON *message_json = NULL;

    char body[2048];


    if (config == NULL ||
        device == NULL ||
        token == NULL)
    {
        return BOT_ERR_AUTH;
    }


    if (config->client_id[0] == '\0')
    {
        log_error(
            "Cannot poll Twitch authorization: Client ID is empty"
        );

        return BOT_ERR_AUTH;
    }


    if (device->device_code[0] == '\0')
    {
        log_error(
            "Cannot poll Twitch authorization: Device Code is empty"
        );

        return BOT_ERR_AUTH;
    }


    memset(
        token,
        0,
        sizeof(*token)
    );


    snprintf(
        body,
        sizeof(body),

        "client_id=%s"

        "&scopes="
        "user%%3Aread%%3Achat"
        "%%20"
        "user%%3Awrite%%3Achat"

        "&device_code=%s"

        "&grant_type="
        "urn%%3Aietf%%3Aparams%%3Aoauth"
        "%%3Agrant-type%%3Adevice_code",

        config->client_id,
        device->device_code
    );


    result = http_post(
        TWITCH_AUTH_HOST,
        TWITCH_AUTH_TOKEN_PATH,
        L"Content-Type: application/x-www-form-urlencoded\r\n",
        body,
        &response
    );


    if (result != BOT_OK)
    {
        log_error(
            "Twitch token request failed: %s",
            bot_result_to_string(result)
        );


        http_response_free(
            &response
        );


        return result;
    }


    if (response.body == NULL)
    {
        log_error(
            "Twitch token response body is empty"
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    root = cJSON_Parse(
        response.body
    );


    if (root == NULL)
    {
        log_error(
            "Failed to parse Twitch OAuth token response"
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    if (response.status_code == 200)
    {
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


        if (!cJSON_IsString(access_token_json) ||
            !cJSON_IsString(refresh_token_json) ||
            !cJSON_IsNumber(expires_in_json))
        {
            log_error(
                "Invalid Twitch OAuth token response"
            );


            cJSON_Delete(
                root
            );


            http_response_free(
                &response
            );


            return BOT_ERR_JSON;
        }


        snprintf(
            token->access_token,
            sizeof(token->access_token),
            "%s",
            access_token_json->valuestring
        );


        snprintf(
            token->refresh_token,
            sizeof(token->refresh_token),
            "%s",
            refresh_token_json->valuestring
        );


        token->expires_in =
            expires_in_json->valueint;


        cJSON_Delete(
            root
        );


        http_response_free(
            &response
        );


        return BOT_OK;
    }


    if (response.status_code == 400)
    {
        message_json =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "message"
            );


        if (cJSON_IsString(message_json))
        {
            if (strcmp(
                    message_json->valuestring,
                    "authorization_pending"
                ) == 0)
            {
                cJSON_Delete(
                    root
                );


                http_response_free(
                    &response
                );


                return BOT_AUTH_PENDING;
            }


            log_error(
                "Twitch OAuth error: %s",
                message_json->valuestring
            );
        }
        else
        {
            log_error(
                "Twitch OAuth returned HTTP 400 without error message"
            );
        }


        cJSON_Delete(
            root
        );


        http_response_free(
            &response
        );


        return BOT_ERR_AUTH;
    }


    log_error(
        "Unexpected Twitch OAuth HTTP status: %lu",
        response.status_code
    );


    message_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "message"
        );


    if (cJSON_IsString(message_json))
    {
        log_error(
            "Twitch OAuth error: %s",
            message_json->valuestring
        );
    }


    cJSON_Delete(
        root
    );


    http_response_free(
        &response
    );


    return BOT_ERR_AUTH;
}


BotResult twitch_auth_validate_token(
    const char *access_token,
    TwitchTokenValidation *validation
)
{
    HttpResponse response = {0};
    BotResult result;

    cJSON *root = NULL;

    cJSON *client_id_json = NULL;
    cJSON *login_json = NULL;
    cJSON *user_id_json = NULL;
    cJSON *expires_in_json = NULL;

    wchar_t wide_token[2048];
    wchar_t headers[2300];

    int convert_result;


    if (access_token == NULL ||
        validation == NULL)
    {
        return BOT_ERR_AUTH;
    }


    if (access_token[0] == '\0')
    {
        log_error(
            "Cannot validate Twitch token: token is empty"
        );

        return BOT_ERR_AUTH;
    }


    memset(
        validation,
        0,
        sizeof(*validation)
    );


    convert_result = MultiByteToWideChar(
        CP_UTF8,
        0,
        access_token,
        -1,
        wide_token,
        (int)(
            sizeof(wide_token) /
            sizeof(wide_token[0])
        )
    );


    if (convert_result == 0)
    {
        log_error(
            "Failed to convert Twitch access token to wide string"
        );

        return BOT_ERR_AUTH;
    }


    /*
     * ВАЖНО:
     *
     * Для /oauth2/validate используем OAuth.
     */
    swprintf(
        headers,
        sizeof(headers) /
            sizeof(headers[0]),

        L"Authorization: OAuth %ls\r\n",

        wide_token
    );


    log_info(
        "Validating Twitch access token..."
    );


    result = http_get(
        TWITCH_AUTH_HOST,
        TWITCH_AUTH_VALIDATE_PATH,
        headers,
        &response
    );


    if (result != BOT_OK)
    {
        log_error(
            "Twitch token validation request failed: %s",
            bot_result_to_string(result)
        );


        http_response_free(
            &response
        );


        return result;
    }


    log_debug(
        "Twitch token validation HTTP status: %lu",
        response.status_code
    );


    if (response.status_code != 200)
    {
        log_error(
            "Twitch access token is invalid"
        );


        if (response.body != NULL)
        {
            log_debug(
                "Twitch validation response: %s",
                response.body
            );
        }


        http_response_free(
            &response
        );


        return BOT_ERR_AUTH;
    }


    if (response.body == NULL)
    {
        log_error(
            "Twitch validation response body is empty"
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    root = cJSON_Parse(
        response.body
    );


    if (root == NULL)
    {
        log_error(
            "Failed to parse Twitch token validation JSON"
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    client_id_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "client_id"
        );


    login_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "login"
        );


    user_id_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "user_id"
        );


    expires_in_json =
        cJSON_GetObjectItemCaseSensitive(
            root,
            "expires_in"
        );


    if (!cJSON_IsString(client_id_json) ||
        !cJSON_IsString(login_json) ||
        !cJSON_IsString(user_id_json) ||
        !cJSON_IsNumber(expires_in_json))
    {
        log_error(
            "Invalid Twitch token validation response"
        );


        cJSON_Delete(
            root
        );


        http_response_free(
            &response
        );


        return BOT_ERR_JSON;
    }


    snprintf(
        validation->client_id,
        sizeof(validation->client_id),
        "%s",
        client_id_json->valuestring
    );


    snprintf(
        validation->login,
        sizeof(validation->login),
        "%s",
        login_json->valuestring
    );


    snprintf(
        validation->user_id,
        sizeof(validation->user_id),
        "%s",
        user_id_json->valuestring
    );


    validation->expires_in =
        expires_in_json->valueint;


    cJSON_Delete(
        root
    );


    http_response_free(
        &response
    );


    return BOT_OK;
}
