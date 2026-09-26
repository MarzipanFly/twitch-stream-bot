#include "app.h"

#include "logger.h"
#include "config.h"
#include "bot_result.h"
#include "platform.h"

#include "http_client.h"

#include "twitch_auth.h"
#include "twitch_api.h"
#include "twitch_user.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


int app_run(void)
{
    AppConfig config;
    BotResult result;


    /*
     * ============================================================
     * DIRECTORIES
     * ============================================================
     */

    result = platform_create_directory(
        "logs"
    );


    if (result != BOT_OK)
    {
        fprintf(
            stderr,
            "Failed to create logs directory\n"
        );

        return 1;
    }


    /*
     * ============================================================
     * LOGGER
     * ============================================================
     */

    if (logger_init(
            "logs/bot.log"
        ) != 0)
    {
        fprintf(
            stderr,
            "Failed to initialize logger\n"
        );

        return 1;
    }


    log_info(
        "Twitch Stream Bot starting..."
    );


    log_info(
        "Platform: Windows"
    );


    result = platform_create_directory(
        "data"
    );


    if (result != BOT_OK)
    {
        log_error(
            "Failed to create data directory"
        );


        logger_shutdown();


        return 1;
    }


    log_debug(
        "Application directories initialized"
    );


    /*
     * ============================================================
     * CONFIG
     * ============================================================
     */

    result = config_load(
        "config.ini",
        &config
    );


    if (result != BOT_OK)
    {
        log_fatal(
            "Failed to load configuration: %s",
            bot_result_to_string(result)
        );


        logger_shutdown();


        return 1;
    }


    result = config_validate(
        &config
    );


    if (result != BOT_OK)
    {
        log_fatal(
            "Invalid configuration: %s",
            bot_result_to_string(result)
        );


        logger_shutdown();


        return 1;
    }


    log_info(
        "Configuration loaded successfully"
    );


    log_info(
        "Broadcaster: %s",
        config.twitch.broadcaster_login
    );


    log_info(
        "Command prefix: %c",
        config.bot.command_prefix
    );


    /*
     * Client ID уже обязателен.
     */
    if (config.twitch.client_id[0] == '\0')
    {
        log_error(
            "Twitch Client ID is not configured"
        );


        logger_shutdown();


        return 1;
    }


    if (config.twitch.access_token[0] == '\0')
    {
        log_warning(
            "Twitch access token is not configured yet"
        );
    }


    if (config.telegram.bot_token[0] == '\0')
    {
        log_warning(
            "Telegram bot token is not configured yet"
        );
    }


    log_info(
        "HTTP client ready"
    );


    log_info(
        "Twitch API module ready"
    );


    /*
     * ============================================================
     * INTERNET TEST
     * ============================================================
     */

    {
        HttpResponse response = {0};


        log_info(
            "Testing Internet connection..."
        );


        result = http_get(
            L"example.com",
            L"/",
            NULL,
            &response
        );


        if (result != BOT_OK)
        {
            log_error(
                "Internet connection test failed: %s",
                bot_result_to_string(result)
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        log_debug(
            "Connection test HTTP status: %lu",
            response.status_code
        );


        log_debug(
            "Connection response size: %zu bytes",
            response.body_size
        );


        if (response.status_code != 200)
        {
            log_error(
                "Internet connection test returned HTTP %lu",
                response.status_code
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Internet connection is available"
        );


        http_response_free(
            &response
        );
    }


    /*
     * ============================================================
     * TWITCH OAUTH
     * ============================================================
     */

    if (config.twitch.access_token[0] == '\0')
    {
        TwitchDeviceCode device = {0};
        TwitchAuthToken token = {0};

        int elapsed = 0;


        log_info(
            "Starting Twitch Device Code authorization..."
        );


        result =
            twitch_auth_request_device_code(
                &config.twitch,
                &device
            );


        if (result != BOT_OK)
        {
            log_error(
                "Failed to start Twitch authorization: %s",
                bot_result_to_string(result)
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch authorization code received successfully"
        );


        log_info(
            "Authorization code: %s",
            device.user_code
        );


        log_info(
            "Open this URL:"
        );


        log_info(
            "%s",
            device.verification_uri
        );


        log_info(
            "Code expires in %d seconds",
            device.expires_in
        );


        log_debug(
            "Recommended polling interval: %d seconds",
            device.interval
        );


        log_info(
            "Waiting for Twitch authorization..."
        );


        result =
            BOT_AUTH_PENDING;


        while (
            elapsed <
            device.expires_in
        )
        {
            Sleep(
                (DWORD)
                device.interval *
                1000
            );


            elapsed +=
                device.interval;


            result =
                twitch_auth_poll_token(
                    &config.twitch,
                    &device,
                    &token
                );


            if (result == BOT_OK)
            {
                break;
            }


            if (
                result ==
                BOT_AUTH_PENDING
            )
            {
                log_debug(
                    "Twitch authorization pending..."
                );


                continue;
            }


            log_error(
                "Twitch authorization failed: %s",
                bot_result_to_string(result)
            );


            logger_shutdown();


            return 1;
        }


        if (result != BOT_OK)
        {
            log_error(
                "Twitch authorization code expired"
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch authorization completed successfully"
        );


        log_info(
            "Access token received"
        );


        log_info(
            "Refresh token received"
        );


        log_info(
            "Access token expires in %d seconds",
            token.expires_in
        );


        /*
         * Токены не выводим в лог.
         */
        snprintf(
            config.twitch.access_token,
            sizeof(
                config.twitch.access_token
            ),
            "%s",
            token.access_token
        );


        snprintf(
            config.twitch.refresh_token,
            sizeof(
                config.twitch.refresh_token
            ),
            "%s",
            token.refresh_token
        );
    }
    else
    {
        log_info(
            "Existing Twitch access token found"
        );
    }


    /*
     * ============================================================
     * TOKEN VALIDATION
     * ============================================================
     */

    {
        TwitchTokenValidation validation =
            {0};


        result =
            twitch_auth_validate_token(
                config.twitch.access_token,
                &validation
            );


        if (result != BOT_OK)
        {
            log_error(
                "Twitch access token validation failed: %s",
                bot_result_to_string(result)
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch access token is valid"
        );


        log_info(
            "Authorized Twitch account: %s",
            validation.login
        );


        log_info(
            "Authorized Twitch user ID: %s",
            validation.user_id
        );


        log_info(
            "Token expires in %d seconds",
            validation.expires_in
        );


        /*
         * Проверяем, что токен выдан именно
         * нашему Twitch Application.
         */
        if (strcmp(
                validation.client_id,
                config.twitch.client_id
            ) != 0)
        {
            log_error(
                "Twitch token Client ID does not match config Client ID"
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch Client ID matches token"
        );
    }


    /*
     * ============================================================
     * TWITCH HELIX / USERS
     * ============================================================
     */

    {
        HttpResponse response =
            {0};

        TwitchUser user =
            {0};


        log_info(
            "Requesting Twitch user information..."
        );


        result =
            twitch_get_user(
                &config.twitch,
                config.twitch.broadcaster_login,
                &response
            );


        if (result != BOT_OK)
        {
            log_error(
                "Failed to get Twitch user: %s",
                bot_result_to_string(result)
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        log_debug(
            "Twitch HTTP status: %lu",
            response.status_code
        );


        log_debug(
            "Twitch response size: %zu bytes",
            response.body_size
        );


        result =
            twitch_parse_user_response(
                response.body,
                &user
            );


        if (result != BOT_OK)
        {
            log_error(
                "Failed to parse Twitch user: %s",
                bot_result_to_string(result)
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch user received successfully"
        );


        log_info(
            "User ID: %s",
            user.id
        );


        log_info(
            "Login: %s",
            user.login
        );


        log_info(
            "Display name: %s",
            user.display_name
        );


        if (
            user.broadcaster_type[0] !=
            '\0'
        )
        {
            log_info(
                "Broadcaster type: %s",
                user.broadcaster_type
            );
        }
        else
        {
            log_info(
                "Broadcaster type: none"
            );
        }


        if (
            user.profile_image_url[0] !=
            '\0'
        )
        {
            log_debug(
                "Profile image: %s",
                user.profile_image_url
            );
        }


        snprintf(
            config.twitch.broadcaster_id,
            sizeof(
                config.twitch.broadcaster_id
            ),
            "%s",
            user.id
        );


        log_info(
            "Broadcaster ID: %s",
            config.twitch.broadcaster_id
        );


        http_response_free(
            &response
        );


        log_info(
            "Twitch API test completed successfully"
        );
    }


    log_info(
        "Application initialization completed successfully"
    );


    log_info(
        "Application shutdown"
    );


    logger_shutdown();


    return 0;
}
