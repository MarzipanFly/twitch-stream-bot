#include "app.h"
#include "logger.h"
#include "config.h"
#include "bot_result.h"
#include "platform.h"
#include "telegram_api.h"
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
#define STREAM_POLL_INTERVAL_SECONDS 30
/*
 * Twitch требует валидировать OAuth-сессию
 * при запуске и затем примерно раз в час.
 */
#define TOKEN_VALIDATION_INTERVAL_MS \
    (60ULL * 60ULL * 1000ULL)
/*
 * Отправляет уведомление о старте стрима в Telegram.
 */
static void notify_stream_started(
    const AppConfig *config,
    const TwitchStream *stream
);
/*
 * Флаг завершения программы.
 *
 * 0 = продолжаем работать
 * 1 = пользователь попросил завершить программу
 */
static volatile LONG g_stop_requested = 0;
/*
 * ============================================================
 * CTRL+C HANDLER
 * ============================================================
 *
 * Когда пользователь нажимает Ctrl+C,
 * Windows вызывает эту функцию.
 *
 * Мы не завершаем программу мгновенно,
 * а просто выставляем флаг.
 */
static BOOL WINAPI console_ctrl_handler(
    DWORD control_type
)
{
    switch (control_type)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            InterlockedExchange(
                &g_stop_requested,
                1
            );
            return TRUE;
        default:
            return FALSE;
    }
}
/*
 * Проверка флага остановки.
 */
static int stop_requested(void)
{
    return
        InterlockedCompareExchange(
            &g_stop_requested,
            0,
            0
        ) != 0;
}
/*
 * ============================================================
 * INTERRUPTIBLE SLEEP
 * ============================================================
 *
 * Вместо одного Sleep(30000) спим
 * по одной секунде.
 *
 * Поэтому после Ctrl+C программе не придётся
 * ждать все 30 секунд.
 */
static void sleep_interruptible(
    int seconds
)
{
    int i;
    for (i = 0; i < seconds; ++i)
    {
        if (stop_requested())
        {
            return;
        }
        Sleep(1000);
    }
}
/*
 * ============================================================
 * TOKEN -> CONFIG
 * ============================================================
 */
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
/*
 * ============================================================
 * TOKEN VALIDATION
 * ============================================================
 */
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
     * Проверяем, что токен принадлежит
     * именно нашему Twitch Application.
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
/*
 * ============================================================
 * DEVICE CODE AUTHORIZATION
 * ============================================================
 */
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
/*
 * ============================================================
 * ENSURE TWITCH AUTH
 * ============================================================
 *
 * Логика:
 *
 * 1. Пытаемся загрузить сохранённые токены.
 * 2. Проверяем access token.
 * 3. Если умер — refresh.
 * 4. Если refresh тоже не работает —
 *    Device Code Authorization.
 */
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
        log_warning(
            "Stored Twitch OAuth tokens are invalid"
        );
        token_store_delete();
    }
    /*
     * Есть сохранённая OAuth-сессия.
     */
    if (have_stored_token)
    {
        result =
            validate_current_token(
                &config->twitch
            );
        if (result == BOT_OK)
        {
            log_info(
                "Using stored Twitch OAuth session"
            );
            return BOT_OK;
        }
        /*
         * Если это не проблема авторизации,
         * не пытаемся делать refresh.
         */
        if (result != BOT_ERR_AUTH)
        {
            return result;
        }
        /*
         * Access token умер.
         *
         * Пробуем refresh token.
         */
        if (token.refresh_token[0] != '\0')
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
                 * Twitch может вернуть новый
                 * refresh token.
                 *
                 * Поэтому сразу сохраняем
                 * новую пару.
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
     * OAuth-сессии нет.
     *
     * Запускаем Device Code Flow.
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
/*
 * ============================================================
 * GET CURRENT STREAM
 * ============================================================
 *
 * Маленькая вспомогательная функция.
 *
 * twitch_get_stream()
 *      получает JSON
 *
 * twitch_parse_stream_response()
 *      превращает JSON в TwitchStream
 */
static BotResult get_current_stream(
    const TwitchConfig *config,
    TwitchStream *stream
)
{
    HttpResponse response =
        {0};
    BotResult result;
    result =
        twitch_get_stream(
            config,
            config->broadcaster_id,
            &response
        );
    if (result != BOT_OK)
    {
        http_response_free(
            &response
        );
        return result;
    }
    result =
        twitch_parse_stream_response(
            response.body,
            stream
        );
    http_response_free(
        &response
    );
    return result;
}
/*
 * ============================================================
 * STREAM INFORMATION
 * ============================================================
 */
static void log_stream_information(
    const TwitchStream *stream
)
{
    if (stream == NULL)
    {
        return;
    }
    log_info(
        "Stream title: %s",
        stream->title
    );
    log_info(
        "Category: %s",
        stream->game_name
    );
    log_info(
        "Viewers: %d",
        stream->viewer_count
    );
    log_info(
        "Started at: %s",
        stream->started_at
    );
    log_info(
        "Language: %s",
        stream->language
    );
}

/*
 * ============================================================
 * TELEGRAM STREAM NOTIFICATION
 * ============================================================
 *
 * Формирует сообщение о начале трансляции
 * и отправляет его в Telegram.
 *
 * Ошибка Telegram НЕ должна останавливать Twitch-бота.
 */
static void notify_stream_started(
    const AppConfig *config,
    const TwitchStream *stream
)
{
    char message[2048];
    BotResult result;
    const char *title;
    const char *game;
    if (config == NULL ||
        stream == NULL)
    {
        return;
    }
    /*
     * Telegram пока не настроен.
     */
    if (config->telegram.bot_token[0] == '\0' ||
        config->telegram.chat_id[0] == '\0')
    {
        log_warning(
            "Telegram notification skipped: configuration is incomplete"
        );
        return;
    }
    title =
        stream->title[0] != '\0'
            ? stream->title
            : "Без названия";
    game =
        stream->game_name[0] != '\0'
            ? stream->game_name
            : "Категория не указана";
    snprintf(
        message,
        sizeof(message),
        "🔴 Стрим начался!\n\n"
        "🎮 %s\n"
        "📝 %s\n\n"
        "🍊 Залетай на стрим:\n"
        "https://twitch.tv/%s",
        game,
        title,
        config->twitch.broadcaster_login
    );
    log_info(
        "Sending stream notification to Telegram..."
    );
    result =
        telegram_send_message(
            &config->telegram,
            message
        );
    if (result != BOT_OK)
    {
        /*
         * Важно:
         *
         * Telegram упал —
         * Twitch monitor продолжает работать.
         */
        log_warning(
            "Failed to send Telegram stream notification: %s",
            bot_result_to_string(result)
        );
        return;
    }
    log_info(
        "Telegram stream notification sent"
    );
}

/*
 * ============================================================
 * STREAM MONITOR
 * ============================================================
 *
 * Именно здесь программа становится
 * постоянно работающим ботом.
 */
static BotResult monitor_stream(
    AppConfig *config
)
{
    TwitchStream stream =
        {0};
    TwitchStream current_stream =
        {0};
    BotResult result;
    int previous_live_state;
    DWORD last_token_validation;
    /*
     * ------------------------------------------------------------
     * Получаем состояние на момент запуска.
     * ------------------------------------------------------------
     *
     * ВАЖНО:
     *
     * если бот запустился, а стрим уже идёт,
     * это НЕ считается событием STREAM STARTED.
     */
    result =
        get_current_stream(
            &config->twitch,
            &stream
        );
    if (result != BOT_OK)
    {
        return result;
    }
    previous_live_state =
        stream.is_live;
    if (stream.is_live)
    {
        log_info(
            "Initial stream status: ONLINE"
        );
        log_stream_information(
            &stream
        );
    }
    else
    {
        log_info(
            "Initial stream status: OFFLINE"
        );
    }
    log_info(
        "Stream monitor started"
    );
    log_info(
        "Polling interval: %d seconds",
        STREAM_POLL_INTERVAL_SECONDS
    );
    log_info(
        "Press Ctrl+C to stop the bot"
    );
    /*
     * Мы только что проверили OAuth при запуске.
     */
    last_token_validation =
        GetTickCount();
    /*
     * ============================================================
     * MAIN BOT LOOP
     * ============================================================
     */
    while (!stop_requested())
    {
        sleep_interruptible(
            STREAM_POLL_INTERVAL_SECONDS
        );
        if (stop_requested())
        {
            break;
        }
        /*
         * --------------------------------------------------------
         * HOURLY TOKEN VALIDATION
         * --------------------------------------------------------
         *
         * Twitch требует проверять токен
         * не только при запуске, но и периодически.
         */
        if (
            GetTickCount() -
            last_token_validation
            >=
            TOKEN_VALIDATION_INTERVAL_MS)
        {
            log_info(
                "Performing scheduled Twitch token validation..."
            );
            result =
                ensure_twitch_auth(
                    config
                );
            if (result != BOT_OK)
            {
                log_error(
                    "Scheduled Twitch authentication check failed: %s",
                    bot_result_to_string(result)
                );
                return result;
            }
            last_token_validation =
                GetTickCount();
        }
        memset(
            &current_stream,
            0,
            sizeof(current_stream)
        );
        result =
            get_current_stream(
                &config->twitch,
                &current_stream
            );
        /*
         * --------------------------------------------------------
         * Если Twitch вернул 401,
         * пытаемся восстановить OAuth-сессию.
         * --------------------------------------------------------
         */
        if (result == BOT_ERR_AUTH)
        {
            log_warning(
                "Twitch access token was rejected"
            );
            result =
                ensure_twitch_auth(
                    config
                );
            if (result != BOT_OK)
            {
                log_error(
                    "Failed to restore Twitch OAuth session: %s",
                    bot_result_to_string(result)
                );
                return result;
            }
            /*
             * OAuth восстановлен.
             *
             * Повторяем запрос.
             */
            result =
                get_current_stream(
                    &config->twitch,
                    &current_stream
                );
        }
        /*
         * Временная ошибка сети не должна
         * убивать постоянно работающего бота.
         */
        if (result != BOT_OK)
        {
            log_warning(
                "Failed to check stream status: %s",
                bot_result_to_string(result)
            );
            continue;
        }
        /*
         * ========================================================
         * OFFLINE -> ONLINE
         * ========================================================
         */
        if (
            previous_live_state == 0 &&
            current_stream.is_live != 0)
        {
            log_info(
                "========================================"
            );
            log_info(
                "STREAM STARTED"
            );
            log_info(
                "========================================"
            );
            log_stream_information(
                &current_stream
            );
            notify_stream_started(
                config,
                &current_stream
            );
        }
        /*
         * ========================================================
         * ONLINE -> OFFLINE
         * ========================================================
         */
        else if (
            previous_live_state != 0 &&
            current_stream.is_live == 0)
        {
            log_info(
                "========================================"
            );
            log_info(
                "STREAM ENDED"
            );
            log_info(
                "========================================"
            );
        }
        /*
         * Если состояние не изменилось:
         *
         * OFFLINE -> OFFLINE
         * ONLINE  -> ONLINE
         *
         * ничего в INFO не спамим.
         */
        else
        {
            log_debug(
                "Stream state unchanged: %s",
                current_stream.is_live
                    ? "ONLINE"
                    : "OFFLINE"
            );
        }
        previous_live_state =
            current_stream.is_live;
    }
    log_info(
        "Stream monitor stopped"
    );
    return BOT_OK;
}
/*
 * ============================================================
 * APPLICATION
 * ============================================================
 */
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
     * CTRL+C
     * ============================================================
     */
    if (!SetConsoleCtrlHandler(
            console_ctrl_handler,
            TRUE))
    {
        log_warning(
            "Failed to install console control handler"
        );
    }
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
    if (config.twitch.client_id[0] == '\0')
    {
        log_error(
            "Twitch Client ID is not configured"
        );
        logger_shutdown();
        return 1;
    }
    if (config.telegram.bot_token[0] == '\0')
    {
        log_warning(
            "Telegram bot token is not configured yet"
        );
    }
    /*
     * ============================================================
     * INTERNET TEST
     * ============================================================
     *
     * Это только диагностическая проверка.
     * Ошибка доступа к example.com НЕ должна завершать бота:
     * Twitch и Telegram проверяют свои соединения отдельно.
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
            log_warning(
                "Internet test failed: %s",
                bot_result_to_string(result)
            );
            log_warning(
                "Continuing startup; service connections will be checked separately"
            );
        }
        else
        {
            log_debug(
                "Connection test HTTP status: %lu",
                response.status_code
            );
            if (response.status_code == 200)
            {
                log_info(
                    "Internet connection is available"
                );
            }
            else
            {
                log_warning(
                    "Internet test returned HTTP %lu",
                    response.status_code
                );
            }
        }
        http_response_free(
            &response
        );
    }

    /*
     * ============================================================
     * TWITCH AUTH
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
     * BROADCASTER INFORMATION
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
        if (user.broadcaster_type[0] != '\0')
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
            sizeof(config.twitch.broadcaster_id),
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
     * INITIALIZATION COMPLETE
     * ============================================================
     */
    log_info(
        "Application initialization completed successfully"
    );
    /*
     * ============================================================
     * MAIN BOT LOOP
     * ============================================================
     */
    result =
        monitor_stream(
            &config
        );
    if (result != BOT_OK)
    {
        log_error(
            "Stream monitor stopped with error: %s",
            bot_result_to_string(result)
        );
    }
    /*
     * ============================================================
     * SHUTDOWN
     * ============================================================
     */
    log_info(
        "Application shutdown"
    );
    SetConsoleCtrlHandler(
        console_ctrl_handler,
        FALSE
    );
    logger_shutdown();
    return
        result == BOT_OK
            ? 0
            : 1;
}
