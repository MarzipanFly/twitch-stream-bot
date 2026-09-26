#include "app.h"

#include "logger.h"
#include "config.h"
#include "bot_result.h"
#include "platform.h"
#include "http_client.h"
#include "twitch_auth.h"
#include "twitch_refresh.h"
#include "twitch_api.h"
#include "twitch_user.h"
#include "twitch_stream.h"
#include "token_store.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


static void copy_token_to_config(
    TwitchConfig *config,
    const TwitchAuthToken *token
)
{
    snprintf(
        config->access_token,
        sizeof(config->access_token),
        "%s",
        token->access_token
    );


    snprintf(
        config->refresh_token,
        sizeof(config->refresh_token),
        "%s",
        token->refresh_token
    );
}


static BotResult validate_current_token(
    TwitchConfig *config
)
{
    TwitchTokenValidation validation =
        {0};

    BotResult result;


    result =
        twitch_auth_validate_token(
            config->access_token,
            &validation
        );


    if (result != BOT_OK)
    {
        return result;
    }


    /*
     * Токен должен принадлежать
     * нашему Twitch Application.
     */
    if (strcmp(
            validation.client_id,
            config->client_id) != 0)
    {
        log_error(
            "Stored Twitch token belongs to another Client ID"
        );


        return BOT_ERR_AUTH;
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


    return BOT_OK;
}


static BotResult run_device_authorization(
    const TwitchConfig *config,
    TwitchAuthToken *token
)
{
    TwitchDeviceCode device =
        {0};

    BotResult result;

    int elapsed = 0;
    int interval;


    log_info(
        "Starting Twitch Device Code authorization..."
    );


    result =
        twitch_auth_request_device_code(
            config,
            &device
        );


    if (result != BOT_OK)
    {
        return result;
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


    interval =
        device.interval > 0
            ? device.interval
            : 5;


    log_info(
        "Waiting for Twitch authorization..."
    );


    result =
        BOT_AUTH_PENDING;


    while (
        elapsed <
        device.expires_in)
    {
        Sleep(
            (DWORD)interval *
            1000
        );


        elapsed +=
            interval;


        result =
            twitch_auth_poll_token(
                config,
                &device,
                token
            );


        if (result == BOT_OK)
        {
            log_info(
                "Twitch authorization completed successfully"
            );


            log_info(
                "Access token received"
            );


            log_info(
                "Refresh token received"
            );


            return BOT_OK;
        }


        if (result ==
            BOT_AUTH_PENDING)
        {
            log_debug(
                "Twitch authorization pending..."
            );


            continue;
        }


        return result;
    }


    log_error(
        "Twitch authorization code expired"
    );


    return BOT_ERR_AUTH;
}


static BotResult ensure_twitch_auth(
    AppConfig *config
)
{
    TwitchAuthToken token =
        {0};

    TwitchAuthToken refreshed_token =
        {0};

    BotResult result;

    int have_stored_token = 0;


    /*
     * ============================================================
     * Сначала пробуем достать токены из DPAPI-хранилища.
     * ============================================================
     */

    result =
        token_store_load(
            &token
        );


    if (result == BOT_OK)
    {
        have_stored_token = 1;


        log_info(
            "Stored Twitch OAuth tokens loaded"
        );


        copy_token_to_config(
            &config->twitch,
            &token
        );
    }
    else if (result != BOT_ERR_FILE)
    {
        /*
         * Файл существует, но повреждён
         * или его невозможно расшифровать.
         */
        log_warning(
            "Stored Twitch OAuth tokens are invalid"
        );


        token_store_delete();
    }


    /*
     * ============================================================
     * Если токены нашли — проверяем access_token.
     * ============================================================
     */

    if (have_stored_token)
    {
        result =
            validate_current_token(
                &config->twitch
            );


        /*
         * Всё хорошо.
         */
        if (result == BOT_OK)
        {
            log_info(
                "Using stored Twitch OAuth session"
            );


            return BOT_OK;
        }


        /*
         * Если это не AUTH-ошибка,
         * значит проблема другого типа.
         */
        if (result !=
            BOT_ERR_AUTH)
        {
            return result;
        }


        /*
         * ========================================================
         * Access token больше невалиден.
         *
         * Пробуем refresh_token.
         * ========================================================
         */

        if (token.refresh_token[0] !=
            '\0')
        {
            log_warning(
                "Twitch access token is invalid; trying refresh token"
            );


            result =
                twitch_refresh_access_token(
                    &config->twitch,
                    token.refresh_token,
                    &refreshed_token
                );


            if (result == BOT_OK)
            {
                /*
                 * ВАЖНО:
                 *
                 * старый refresh token после обмена
                 * больше использовать нельзя.
                 *
                 * Поэтому сразу сохраняем новую пару.
                 */
                result =
                    token_store_save(
                        &refreshed_token
                    );


                if (result != BOT_OK)
                {
                    log_warning(
                        "New Twitch tokens could not be saved"
                    );
                }


                copy_token_to_config(
                    &config->twitch,
                    &refreshed_token
                );


                result =
                    validate_current_token(
                        &config->twitch
                    );


                if (result == BOT_OK)
                {
                    log_info(
                        "Twitch OAuth session refreshed successfully"
                    );


                    return BOT_OK;
                }
            }
        }


        /*
         * Refresh не помог.
         *
         * Удаляем старое хранилище
         * и запускаем Device Code Flow.
         */
        log_warning(
            "Stored Twitch authorization cannot be restored"
        );


        token_store_delete();


        config->twitch.access_token[0] =
            '\0';


        config->twitch.refresh_token[0] =
            '\0';
    }


    /*
     * ============================================================
     * Нет рабочих токенов.
     *
     * Запускаем браузерную авторизацию.
     * ============================================================
     */

    memset(
        &token,
        0,
        sizeof(token)
    );


    result =
        run_device_authorization(
            &config->twitch,
            &token
        );


    if (result != BOT_OK)
    {
        return result;
    }


    /*
     * СРАЗУ сохраняем пару токенов.
     */
    result =
        token_store_save(
            &token
        );


    if (result != BOT_OK)
    {
        log_warning(
            "Twitch tokens were received but could not be saved"
        );
    }


    copy_token_to_config(
        &config->twitch,
        &token
    );


    /*
     * Проверяем только что полученный token.
     */
    result =
        validate_current_token(
            &config->twitch
        );


    if (result != BOT_OK)
    {
        return result;
    }


    return BOT_OK;
}


int app_run(void)
{
    AppConfig config;

    BotResult result;


    /*
     * ============================================================
     * DIRECTORIES
     * ============================================================
     */

    result =
        platform_create_directory(
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
            "logs/bot.log") != 0)
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


    /*
     * ============================================================
     * DATA DIRECTORY
     * ============================================================
     */

    result =
        platform_create_directory(
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

    result =
        config_load(
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


    result =
        config_validate(
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


    if (config.twitch.client_id[0] ==
        '\0')
    {
        log_error(
            "Twitch Client ID is not configured"
        );


        logger_shutdown();


        return 1;
    }


    if (config.telegram.bot_token[0] ==
        '\0')
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
        HttpResponse response =
            {0};


        log_info(
            "Testing Internet connection..."
        );


        result =
            http_get(
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


        if (response.status_code !=
            200)
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
     * TWITCH AUTH SESSION
     * ============================================================
     */

    result =
        ensure_twitch_auth(
            &config
        );


    if (result != BOT_OK)
    {
        log_error(
            "Failed to establish Twitch OAuth session: %s",
            bot_result_to_string(result)
        );


        logger_shutdown();


        return 1;
    }


    /*
     * ============================================================
     * TWITCH USER
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


        if (user.broadcaster_type[0] !=
            '\0')
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
    }

    /*
     * ============================================================
     * CURRENT STREAM STATUS
     * ============================================================
     */

    {
        HttpResponse response =
            {0};

        TwitchStream stream =
            {0};


        log_info(
            "Checking Twitch stream status..."
        );


        result =
            twitch_get_stream(
                &config.twitch,
                config.twitch.broadcaster_id,
                &response
            );


        if (result != BOT_OK)
        {
            log_error(
                "Failed to get Twitch stream status: %s",
                bot_result_to_string(result)
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        result =
            twitch_parse_stream_response(
                response.body,
                &stream
            );


        if (result != BOT_OK)
        {
            log_error(
                "Failed to parse Twitch stream status: %s",
                bot_result_to_string(result)
            );


            http_response_free(
                &response
            );


            logger_shutdown();


            return 1;
        }


        if (stream.is_live)
        {
            log_info(
                "Stream status: ONLINE"
            );


            log_info(
                "Stream title: %s",
                stream.title
            );


            log_info(
                "Category: %s",
                stream.game_name
            );


            log_info(
                "Viewers: %d",
                stream.viewer_count
            );


            log_info(
                "Started at: %s",
                stream.started_at
            );


            log_info(
                "Language: %s",
                stream.language
            );
        }
        else
        {
            log_info(
                "Stream status: OFFLINE"
            );
        }


        http_response_free(
            &response
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
