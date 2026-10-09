#include "app.h"

#include "viewer_profile.h"
#include "viewer_duel.h"
#include "logger.h"
#include "config.h"
#include "bot_result.h"
#include "commands.h"
#include "command_cooldown.h"
#include "obs_websocket.h"
#include "platform.h"
#include "telegram_api.h"
#include "http_client.h"

#include "twitch_auth.h"
#include "twitch_refresh.h"
#include "twitch_api.h"
#include "twitch_user.h"
#include "twitch_stream.h"
#include "twitch_chat.h"
#include "twitch_eventsub.h"
#include "twitch_eventsub_ws.h"
#include "chat_event.h"
#include "viewer_rank.h"
#include "music_queue.h"
#include "youtube_metadata.h"

#include "token_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>


#define STREAM_POLL_INTERVAL_SECONDS 30
#define EVENTSUB_RECONNECT_DELAY_SECONDS 5
#define BRUH_GLOBAL_COOLDOWN_MS (15UL * 1000UL)

static DWORD g_last_bruh_tick = 0;
static int g_bruh_played = 0;
static DWORD g_sound_last_tick[SOUND_MAX_COUNT];
static int g_sound_played[SOUND_MAX_COUNT];
static DWORD g_sound_notice_tick[SOUND_MAX_COUNT];
static int g_sound_notice_sent[SOUND_MAX_COUNT];
#define SOUND_NOTICE_INTERVAL_MS 10000UL
/* 0.9: request queue, playback integration follows in a later step. */
static MusicQueue g_music_queue;

static int youtube_video_id(const char *url, char id[12])
{
    const char *p = NULL;
    const char *host;
    const char *end;
    size_t host_length;
    size_t i;

    if (strncmp(url, "https://", 8) == 0)
        host = url + 8;
    else if (strncmp(url, "http://", 7) == 0)
        host = url + 7;
    else
        return 0;
    end = strpbrk(host, "/?#");
    host_length = end ? (size_t)(end - host) : strlen(host);
    if ((host_length == 11 && strncmp(host, "youtube.com", 11) == 0) ||
        (host_length == 15 && strncmp(host, "www.youtube.com", 15) == 0) ||
        (host_length == 13 && strncmp(host, "m.youtube.com", 13) == 0))
    {
        const char *path = host + host_length;
        if (strncmp(path, "/watch?", 7) == 0)
        {
            const char *query = path + 7;
            while (*query && *query != '#')
            {
                if (strncmp(query, "v=", 2) == 0)
                {
                    p = query + 2;
                    break;
                }
                query = strchr(query, '&');
                if (!query)
                    break;
                ++query;
            }
        }
        else if (strncmp(path, "/shorts/", 8) == 0)
            p = path + 8;
        else if (strncmp(path, "/live/", 6) == 0)
            p = path + 6;
    }
    else if (host_length == 8 && strncmp(host, "youtu.be", 8) == 0)
    {
        const char *path = host + host_length;
        if (*path == '/')
            p = path + 1;
    }
    if (!p || strlen(p) < 11)
        return 0;
    for (i = 0; i < 11; ++i)
    {
        unsigned char ch = (unsigned char)p[i];
        if (!((ch >= 'A' && ch <= 'Z') ||
              (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-'))
            return 0;
        id[i] = (char)ch;
    }
    if (p[11] != '\0' && p[11] != '&' && p[11] != '?' &&
        p[11] != '#' && p[11] != '/')
        return 0;
    id[11] = '\0';
    return 1;
}

static int music_request_matches(const char *text, char prefix)
{
    const char *name = "заказать";
    size_t length = strlen(name);
    if (text == NULL || text[0] != prefix)
        return 0;
    ++text;
    return strncmp(text, name, length) == 0 &&
           (text[length] == '\0' || text[length] == ' ' ||
            text[length] == '\t');
}



/*
 * Twitch OAuth проверяем
 * примерно раз в час.
 */
#define TOKEN_VALIDATION_INTERVAL_MS \
    (60UL * 60UL * 1000UL)


static void notify_stream_started(
    const AppConfig *config,
    const TwitchStream *stream
);


/*
 * Флаг остановки приложения.
 */
static volatile LONG g_stop_requested =
    0;


/*
 * ============================================================
 * UTF-8 CONSOLE
 * ============================================================
 *
 * Twitch передаёт сообщения чата в UTF-8.
 * Переключаем ввод и вывод консоли Windows на UTF-8,
 * чтобы русские команды и ответы отображались нормально.
 */
static void setup_console_utf8(void)
{
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
}


/*
 * ============================================================
 * CTRL+C
 * ============================================================
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
 * Проверяет, была ли
 * запрошена остановка.
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
 * ПРЕРЫВАЕМОЕ ОЖИДАНИЕ
 * ============================================================
 */
static void sleep_interruptible(
    int seconds
)
{
    int i;


    for (
        i = 0;
        i < seconds;
        ++i)
    {
        if (
            stop_requested())
        {
            return;
        }


        Sleep(
            1000
        );
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


    if (
        result != BOT_OK)
    {
        return result;
    }


    /*
     * Проверяем, что токен
     * принадлежит нашему приложению.
     */
    if (
        strcmp(
            validation.client_id,
            config->client_id
        ) != 0)
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


    /*
     * Пользователь, которому принадлежит OAuth-токен,
     * является отправителем сообщений в Twitch-чате.
     */
    snprintf(
        config->bot_login,
        sizeof(config->bot_login),
        "%s",
        validation.login
    );


    snprintf(
        config->bot_user_id,
        sizeof(config->bot_user_id),
        "%s",
        validation.user_id
    );


    log_info(
        "Twitch chat bot account: %s (%s)",
        config->bot_login,
        config->bot_user_id
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

    int elapsed =
        0;

    int interval;


    log_info(
        "Starting Twitch Device Code authorization..."
    );


    result =
        twitch_auth_request_device_code(
            config,
            &device
        );


    if (
        result != BOT_OK)
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


        if (
            result == BOT_OK)
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


        if (
            result ==
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

    int have_stored_token =
        0;


    result =
        token_store_load(
            &token
        );


    if (
        result == BOT_OK)
    {
        have_stored_token =
            1;


        log_info(
            "Stored Twitch OAuth tokens loaded"
        );


        copy_token_to_config(
            &config->twitch,
            &token
        );
    }
    else if (
        result !=
        BOT_ERR_FILE)
    {
        log_warning(
            "Stored Twitch OAuth tokens are invalid"
        );


        token_store_delete();
    }


    if (
        have_stored_token)
    {
        result =
            validate_current_token(
                &config->twitch
            );


        if (
            result == BOT_OK)
        {
            log_info(
                "Using stored Twitch OAuth session"
            );


            return BOT_OK;
        }


        if (
            result !=
            BOT_ERR_AUTH)
        {
            return result;
        }


        if (
            token.refresh_token[0] !=
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


            if (
                result == BOT_OK)
            {
                result =
                    token_store_save(
                        &refreshed_token
                    );


                if (
                    result != BOT_OK)
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


                if (
                    result == BOT_OK)
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


    if (
        result != BOT_OK)
    {
        return result;
    }


    result =
        token_store_save(
            &token
        );


    if (
        result != BOT_OK)
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


    if (
        result != BOT_OK)
    {
        return result;
    }


    return BOT_OK;
}


/*
 * ============================================================
 * CURRENT STREAM
 * ============================================================
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


    if (
        result != BOT_OK)
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
 * STREAM INFO
 * ============================================================
 */
static void log_stream_information(
    const TwitchStream *stream
)
{
    if (
        stream == NULL)
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
 * TELEGRAM MESSAGE
 * ============================================================
 */
static int build_stream_started_message(
    const AppConfig *config,
    const TwitchStream *stream,
    char *buffer,
    size_t buffer_size
)
{
    const char *title;
    const char *game;

    int written;


    if (
        config == NULL ||
        stream == NULL ||
        buffer == NULL ||
        buffer_size == 0)
    {
        return 0;
    }


    title =
        stream->title[0] != '\0'
            ? stream->title
            : "Без названия";


    game =
        stream->game_name[0] != '\0'
            ? stream->game_name
            : "Категория не указана";


    written =
        snprintf(
            buffer,
            buffer_size,

            "? Стрим начался!\n\n"
            "? %s\n"
            "? %s\n\n"
            "? Залетай на стрим:\n"
            "https://twitch.tv/%s",

            game,
            title,
            config->twitch.broadcaster_login
        );


    if (
        written < 0 ||
        (size_t)written >=
        buffer_size)
    {
        log_error(
            "Failed to build stream notification: message is too long"
        );


        return 0;
    }


    return 1;
}


/*
 * ============================================================
 * TELEGRAM NOTIFICATION
 * ============================================================
 */
static void notify_stream_started(
    const AppConfig *config,
    const TwitchStream *stream
)
{
    char message[2048];

    BotResult result;


    if (
        config == NULL ||
        stream == NULL)
    {
        return;
    }


    if (
        config->telegram.bot_token[0] == '\0' ||
        config->telegram.chat_id[0] == '\0')
    {
        log_warning(
            "Telegram notification skipped: configuration is incomplete"
        );


        return;
    }


    if (
        !build_stream_started_message(
            config,
            stream,
            message,
            sizeof(message)
        ))
    {
        return;
    }


    log_info(
        "Sending stream notification to Telegram..."
    );


    result =
        telegram_send_message(
            &config->telegram,
            message
        );


    if (
        result != BOT_OK)
    {
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
 * TEST STREAM
 * ============================================================
 */
static void run_test_stream(
    const AppConfig *config,
    int dry_run
)
{
    TwitchStream stream =
        {0};

    char message[2048];


    stream.is_live =
        1;


    snprintf(
        stream.title,
        sizeof(stream.title),
        "%s",
        "Тестовый запуск TwitchBot"
    );


    snprintf(
        stream.game_name,
        sizeof(stream.game_name),
        "%s",
        "Just Chatting"
    );


    snprintf(
        stream.language,
        sizeof(stream.language),
        "%s",
        "ru"
    );


    stream.viewer_count =
        42;


    log_info(
        "Running simulated stream start"
    );


    log_stream_information(
        &stream
    );


    if (
        dry_run)
    {
        if (
            build_stream_started_message(
                config,
                &stream,
                message,
                sizeof(message)
            ))
        {
            log_info(
                "Dry run: Telegram message would be:"
            );


            log_info(
                "\n%s",
                message
            );
        }
    }
    else
    {
        notify_stream_started(
            config,
            &stream
        );
    }


    log_info(
        "Simulated stream start completed"
    );
}


/*
 * Убирает \n и \r,
 * оставленные fgets().
 */
static void trim_line_end(
    char *text
)
{
    size_t length;


    if (
        text == NULL)
    {
        return;
    }


    length =
        strlen(
            text
        );


    while (
        length > 0 &&
        (
            text[length - 1] == '\n' ||
            text[length - 1] == '\r'
        ))
    {
        text[length - 1] =
            '\0';


        --length;
    }
}


/*
 * ============================================================
 * LOCAL CHAT TEST
 * ============================================================
 */
static void run_chat_test_console(
    const AppConfig *config
)
{
    char input[1024];
    char response[2048];

    ChatCommand command;


    SetConsoleCP(
        CP_UTF8
    );


    SetConsoleOutputCP(
        CP_UTF8
    );


    printf(
        "\nLocal Twitch chat command test\n"
        "Prefix: %c\n"
        "Type exit to quit.\n\n",
        config->bot.command_prefix
    );


    for (;;)
    {
        printf(
            "> "
        );


        fflush(
            stdout
        );


        if (
            fgets(
                input,
                sizeof(input),
                stdin
            ) == NULL)
        {
            break;
        }


        trim_line_end(
            input
        );


        if (
            strcmp(
                input,
                "exit"
            ) == 0 ||
            strcmp(
                input,
                "quit"
            ) == 0)
        {
            break;
        }


        if (
            input[0] == '\0')
        {
            continue;
        }


        if (
            !chat_command_parse(
                input,
                config->bot.command_prefix,
                &command
            ))
        {
            printf(
                "BOT: это обычное сообщение, не команда.\n"
            );


            continue;
        }


        if (
            chat_command_build_response(
                &command,
                config->telegram.channel_url,
                config->discord.invite_url,
                response,
                sizeof(response)
            ))
        {
            printf(
                "BOT: %s\n",
                response
            );
        }
        else if (
            command.type ==
            CHAT_COMMAND_UNKNOWN)
        {
            printf(
                "BOT: неизвестная команда.\n"
            );
        }
    }


    printf(
        "Chat command test finished.\n"
    );
}


/*
 * ============================================================
 * OFFLINE EVENTSUB TEST
 * ============================================================
 */
static void run_eventsub_chat_test(
    const AppConfig *config
)
{
    const char *test_json =
        "{"
            "\"metadata\":{"
                "\"message_id\":\"test-eventsub-id\","
                "\"message_type\":\"notification\","
                "\"message_timestamp\":\"2026-10-02T10:00:00Z\","
                "\"subscription_type\":\"channel.chat.message\","
                "\"subscription_version\":\"1\""
            "},"

            "\"payload\":{"

                "\"subscription\":{"
                    "\"id\":\"test-subscription-id\","
                    "\"status\":\"enabled\","
                    "\"type\":\"channel.chat.message\","
                    "\"version\":\"1\""
                "},"

                "\"event\":{"
                    "\"broadcaster_user_id\":\"826686276\","
                    "\"broadcaster_user_login\":\"aleg_opelsin1\","
                    "\"broadcaster_user_name\":\"aleg_opelsin1\","

                    "\"chatter_user_id\":\"123456789\","
                    "\"chatter_user_login\":\"testviewer\","
                    "\"chatter_user_name\":\"TestViewer\","

                    "\"message_id\":\"test-chat-message-id\","

                    "\"message\":{"
                        "\"text\":\"!кости\""
                    "}"
                "}"
            "}"
        "}";


    TwitchEventSubMessageType event_type;

    TwitchChatMessage chat_message;

    ChatCommand command;

    BotResult result;

    char response[2048];


    log_info(
        "Running offline EventSub chat test..."
    );


    result =
        twitch_eventsub_get_message_type(
            test_json,
            &event_type
        );


    if (
        result != BOT_OK)
    {
        log_error(
            "Failed to detect EventSub message type: %s",
            bot_result_to_string(result)
        );


        return;
    }


    if (
        event_type !=
        TWITCH_EVENTSUB_NOTIFICATION)
    {
        log_error(
            "Unexpected EventSub message type"
        );


        return;
    }


    result =
        twitch_eventsub_parse_chat_message(
            test_json,
            &chat_message
        );


    if (
        result != BOT_OK)
    {
        log_error(
            "Failed to parse EventSub chat message: %s",
            bot_result_to_string(result)
        );


        return;
    }


    log_info(
        "Chat user: %s (%s)",
        chat_message.chatter_user_name,
        chat_message.chatter_user_id
    );


    log_info(
        "Chat message: %s",
        chat_message.text
    );


    if (
        !chat_command_parse(
            chat_message.text,
            config->bot.command_prefix,
            &command
        ))
    {
        log_info(
            "Chat message is not a command"
        );


        return;
    }


    if (
        !chat_command_build_response(
            &command,
            config->telegram.channel_url,
            config->discord.invite_url,
            response,
            sizeof(response)
        ))
    {
        log_info(
            "Command does not require a response"
        );


        return;
    }


    log_info(
        "BOT RESPONSE: %s",
        response
    );


    log_info(
        "Offline EventSub chat test completed successfully"
    );
}


/*
 * ============================================================
 * REAL TWITCH COMMAND TEST
 * ============================================================
 *
 * Подключается к настоящему EventSub WebSocket,
 * ждёт команду в Twitch-чате,
 * обрабатывает её и отвечает обратно.
 *
 * После первого успешного ответа
 * тест завершается.
 */
static BotResult run_real_twitch_command_test(
    AppConfig *config
)
{
    TwitchEventSubWebSocket websocket =
        {0};


    TwitchEventSubSession eventsub_session =
        {0};


    TwitchEventSubMessageType message_type;

    TwitchChatMessage chat_message;

    ChatCommand command;

    BotResult result;


    char json[32768];

    char response[2048];


    if (
        config == NULL)
    {
        return BOT_ERR_CONFIG;
    }


    log_info(
        "Starting real Twitch command test..."
    );


    /*
     * ========================================================
     * 1. WEBSOCKET CONNECT
     * ========================================================
     */
    result =
        twitch_eventsub_ws_connect(
            &websocket
        );


    if (
        result != BOT_OK)
    {
        return result;
    }


    /*
     * ========================================================
     * 2. SESSION WELCOME
     * ========================================================
     *
     * Первое сообщение от Twitch
     * должно быть session_welcome.
     */
    result =
        twitch_eventsub_ws_receive(
            &websocket,
            json,
            sizeof(json)
        );


    if (
        result != BOT_OK)
    {
        twitch_eventsub_ws_close(
            &websocket
        );


        return result;
    }


    result =
        twitch_eventsub_get_message_type(
            json,
            &message_type
        );


    if (
        result != BOT_OK ||
        message_type !=
            TWITCH_EVENTSUB_SESSION_WELCOME)
    {
        log_error(
            "Expected Twitch EventSub session_welcome"
        );


        twitch_eventsub_ws_close(
            &websocket
        );


        return BOT_ERR_TWITCH;
    }


    result =
        twitch_eventsub_parse_welcome(
            json,
            &eventsub_session
        );


    if (
        result != BOT_OK)
    {
        twitch_eventsub_ws_close(
            &websocket
        );


        return result;
    }


    log_info(
        "Twitch EventSub session ID: %s",
        eventsub_session.session_id
    );


    log_info(
        "Twitch EventSub keepalive timeout: %d seconds",
        eventsub_session.keepalive_timeout_seconds
    );


    /*
     * ========================================================
     * 3. CHANNEL.CHAT.MESSAGE SUBSCRIPTION
     * ========================================================
     */
    result =
        twitch_eventsub_subscribe_chat(
            &config->twitch,
            eventsub_session.session_id
        );


    if (
        result != BOT_OK)
    {
        twitch_eventsub_ws_close(
            &websocket
        );


        return result;
    }


    log_info(
        "Waiting for Twitch chat command..."
    );


    log_info(
        "Write !команды or !кости in Twitch chat"
    );


    /*
     * ========================================================
     * 4. EVENT LOOP
     * ========================================================
     */
    for (;;)
    {
        result =
            twitch_eventsub_ws_receive(
                &websocket,
                json,
                sizeof(json)
            );


        if (
            result != BOT_OK)
        {
            twitch_eventsub_ws_close(
                &websocket
            );


            return result;
        }


        result =
            twitch_eventsub_get_message_type(
                json,
                &message_type
            );


        if (
            result != BOT_OK)
        {
            log_warning(
                "Failed to detect Twitch EventSub message type"
            );


            continue;
        }


        /*
         * Keepalive означает,
         * что соединение живо.
         */
        if (
            message_type ==
            TWITCH_EVENTSUB_KEEPALIVE)
        {
            log_debug(
                "Twitch EventSub keepalive"
            );


            continue;
        }


        /*
         * Reconnect нормально реализуем
         * после первого рабочего теста.
         */
        if (
            message_type ==
            TWITCH_EVENTSUB_RECONNECT)
        {
            log_warning(
                "Twitch requested EventSub reconnect"
            );


            twitch_eventsub_ws_close(
                &websocket
            );


            return BOT_ERR_NETWORK;
        }


        if (
            message_type !=
            TWITCH_EVENTSUB_NOTIFICATION)
        {
            continue;
        }


        /*
         * ====================================================
         * 5. PARSE CHAT MESSAGE
         * ====================================================
         */
        result =
            twitch_eventsub_parse_chat_message(
                json,
                &chat_message
            );


        if (
            result != BOT_OK)
        {
            continue;
        }


        log_info(
            "Twitch chat: %s: %s",
            chat_message.chatter_user_name,
            chat_message.text
        );


        /*
         * ====================================================
         * 6. COMMAND PARSER
         * ====================================================
         */
        if (
            !chat_command_parse(
                chat_message.text,
                config->bot.command_prefix,
                &command
            ))
        {
            continue;
        }


        /*
         * ====================================================
         * 7. BUILD RESPONSE
         * ====================================================
         */
        if (
            !chat_command_build_response(
                &command,
                config->telegram.channel_url,
                config->discord.invite_url,
                response,
                sizeof(response)
            ))
        {
            continue;
        }


        log_info(
            "Bot response: %s",
            response
        );


        /*
         * ====================================================
         * 8. SEND RESPONSE
         * ====================================================
         */
        result =
            twitch_chat_send_message(
                &config->twitch,
                response
            );


        if (
            result != BOT_OK)
        {
            twitch_eventsub_ws_close(
                &websocket
            );


            return result;
        }


        log_info(
            "Real Twitch command test completed successfully"
        );


        twitch_eventsub_ws_close(
            &websocket
        );


        return BOT_OK;
    }
}


/*
 * ============================================================
 * TWITCH CHAT CONNECTION
 * ============================================================
 *
 * Открывает EventSub WebSocket, получает session_welcome
 * и создаёт подписку channel.chat.message.
 */
static BotResult connect_twitch_chat_listener(
    AppConfig *config,
    TwitchEventSubWebSocket *websocket
)
{
    TwitchEventSubSession eventsub_session = {0};
    TwitchEventSubMessageType message_type;
    BotResult result;
    char json[32768];

    if (config == NULL || websocket == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    result = twitch_eventsub_ws_connect(websocket);
    if (result != BOT_OK)
    {
        return result;
    }

    result = twitch_eventsub_ws_receive(
        websocket,
        json,
        sizeof(json)
    );

    if (result != BOT_OK)
    {
        twitch_eventsub_ws_close(websocket);
        return result;
    }

    result = twitch_eventsub_get_message_type(
        json,
        &message_type
    );

    if (result != BOT_OK ||
        message_type != TWITCH_EVENTSUB_SESSION_WELCOME)
    {
        log_error(
            "Expected Twitch EventSub session_welcome"
        );

        twitch_eventsub_ws_close(websocket);
        return BOT_ERR_TWITCH;
    }

    result = twitch_eventsub_parse_welcome(
        json,
        &eventsub_session
    );

    if (result != BOT_OK)
    {
        twitch_eventsub_ws_close(websocket);
        return result;
    }

    log_info(
        "Twitch EventSub session ID: %s",
        eventsub_session.session_id
    );

    log_info(
        "Twitch EventSub keepalive timeout: %d seconds",
        eventsub_session.keepalive_timeout_seconds
    );

    result = twitch_eventsub_subscribe_chat(
        &config->twitch,
        eventsub_session.session_id
    );

    if (result != BOT_OK)
    {
        twitch_eventsub_ws_close(websocket);
        return result;
    }

    log_info(
        "Twitch chat listener started"
    );

    return BOT_OK;
}


/*
 * ============================================================
 * ECONOMY HELPERS
 * ============================================================
 */
static int parse_orange_bet(
    const char *arguments,
    long long *bet
)
{
    char *end;
    long long value;

    if (arguments == NULL || bet == NULL)
    {
        return 0;
    }

    while (*arguments == ' ' || *arguments == '\t')
    {
        ++arguments;
    }

    if (*arguments == '\0')
    {
        return 0;
    }

    value = strtoll(
        arguments,
        &end,
        10
    );

    if (end == arguments)
    {
        return 0;
    }

    while (*end == ' ' || *end == '\t')
    {
        ++end;
    }

    if (
        *end != '\0' ||
        value < 1 ||
        value > 10000)
    {
        return 0;
    }

    *bet = value;

    return 1;
}


static void economy_random_init(void)
{
    static int initialized = 0;

    if (initialized)
    {
        return;
    }

    srand(
        (unsigned int)time(NULL) ^
        (unsigned int)GetTickCount()
    );

    initialized = 1;
}


static int parse_duel_arguments(
    const char *arguments,
    char *target_login,
    size_t target_login_size,
    long long *bet
)
{
    const char *start;
    const char *end;
    size_t length;

    if (
        arguments == NULL ||
        target_login == NULL ||
        target_login_size == 0 ||
        bet == NULL)
    {
        return 0;
    }

    start = arguments;

    while (*start == ' ' || *start == '\t')
    {
        ++start;
    }

    if (*start == '@')
    {
        ++start;
    }

    end = start;

    while (
        *end != '\0' &&
        *end != ' ' &&
        *end != '\t')
    {
        ++end;
    }

    length = (size_t)(end - start);

    if (
        length == 0 ||
        length >= target_login_size)
    {
        return 0;
    }

    memcpy(
        target_login,
        start,
        length
    );

    target_login[length] = '\0';

    return parse_orange_bet(
        end,
        bet
    );
}


static int ascii_equals_ignore_case(
    const char *left,
    const char *right
)
{
    unsigned char a;
    unsigned char b;

    if (left == NULL || right == NULL)
    {
        return 0;
    }

    while (*left != '\0' && *right != '\0')
    {
        a = (unsigned char)*left;
        b = (unsigned char)*right;

        if (a >= 'A' && a <= 'Z')
        {
            a = (unsigned char)(a - 'A' + 'a');
        }

        if (b >= 'A' && b <= 'Z')
        {
            b = (unsigned char)(b - 'A' + 'a');
        }

        if (a != b)
        {
            return 0;
        }

        ++left;
        ++right;
    }

    return *left == '\0' && *right == '\0';
}


/*
 * ============================================================
 * PROCESS TWITCH CHAT MESSAGE
 * ============================================================
 */
static BotResult process_twitch_chat_notification(
    AppConfig *config,
	const char *json,
	ChatEvent *chat_event
)
{
    TwitchChatMessage chat_message;
    ChatCommand command;
    ViewerProfile *profile;
    BotResult result;
    char response[2048];

    if (config == NULL || json == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    result = twitch_eventsub_parse_chat_message(
        json,
        &chat_message
    );

    if (result != BOT_OK)
    {
        return result;
    }

    log_info(
        "Twitch chat: %s: %s",
        chat_message.chatter_user_name,
        chat_message.text
    );

    /*
     * Configurable sound commands are checked before built-in commands.
     * Each sound has its own global cooldown.
     */
    if (chat_message.text[0] == config->bot.command_prefix)
    {
        const char *name = chat_message.text + 1;
        size_t i;
        for (i = 0; i < config->sound_count; ++i)
        {
            const SoundConfig *sound = &config->sounds[i];
            size_t length = strlen(sound->command);
            if (strncmp(name, sound->command, length) != 0 ||
                (name[length] != '\0' && name[length] != ' ' &&
                 name[length] != '\t'))
                continue;

            if (!config->obs.enabled)
            {
                log_warning("Sound command ignored: OBS disabled");
                return BOT_OK;
            }
            {
                DWORD now = GetTickCount();
                DWORD cooldown = sound->cooldown_seconds * 1000UL;
                ObsWebSocket obs = {0};
                ViewerProfile *sound_profile = NULL;
                if (g_sound_played[i] &&
                    (DWORD)(now - g_sound_last_tick[i]) < cooldown)
                {
                    if (!g_sound_notice_sent[i] ||
                        (DWORD)(now - g_sound_notice_tick[i]) >=
                            SOUND_NOTICE_INTERVAL_MS)
                    {
                        DWORD elapsed = (DWORD)(now - g_sound_last_tick[i]);
                        unsigned long remaining =
                            (unsigned long)((cooldown - elapsed + 999UL) / 1000UL);
                        snprintf(response, sizeof(response),
                                 "!%s ещё недоступен. Осталось %lu сек.",
                                 sound->command, remaining);
                        result = twitch_chat_send_message(&config->twitch, response);
                        if (result == BOT_OK)
                        {
                            g_sound_notice_tick[i] = now;
                            g_sound_notice_sent[i] = 1;
                        }
                        else
                            log_warning("Failed to send sound cooldown notice (result=%d)",
                                        (int)result);
                    }
                    return BOT_OK;
                }

                if (sound->cost > 0)
                {
                    sound_profile = viewer_profile_get_or_create(
                        chat_message.chatter_user_id,
                        chat_message.chatter_user_login,
                        chat_message.chatter_user_name);
                    if (sound_profile == NULL)
                    {
                        log_warning("Sound !%s: viewer profile unavailable",
                                    sound->command);
                        return BOT_OK;
                    }
                    if (sound_profile->balance < sound->cost)
                    {
                        snprintf(response, sizeof(response),
                                 "%s, для !%s нужно %lld апельсинов. Баланс: %lld.",
                                 chat_message.chatter_user_name,
                                 sound->command, sound->cost,
                                 sound_profile->balance);
                        (void)twitch_chat_send_message(&config->twitch, response);
                        return BOT_OK;
                    }
                }

                result = obs_websocket_connect(&obs, config->obs.password);
                if (result == BOT_OK)
                    result = obs_websocket_restart_media(&obs, sound->input);
                obs_websocket_close(&obs);
                if (result == BOT_OK)
                {
                    g_sound_last_tick[i] = GetTickCount();
                    g_sound_played[i] = 1;
                    if (sound_profile != NULL)
                    {
                        long long previous_balance = sound_profile->balance;
                        sound_profile->balance -= sound->cost;
                        if (!viewer_profile_save())
                        {
                            sound_profile->balance = previous_balance;
                            log_warning("Sound !%s played but payment could not be saved",
                                        sound->command);
                        }
                        else
                            log_info("Sound !%s cost %lld oranges; %s balance: %lld",
                                     sound->command, sound->cost,
                                     chat_message.chatter_user_name,
                                     sound_profile->balance);
                    }
                    log_info("OBS sound !%s played by %s",
                             sound->command, chat_message.chatter_user_name);
                }
                else
                    log_warning("OBS sound !%s failed (result=%d)",
                                sound->command, (int)result);
            }
            return BOT_OK;
        }
    }

    if (chat_message.text[0] == config->bot.command_prefix &&
        strcmp(chat_message.text + 1, "очередь") == 0)
    {
        size_t i;
        size_t used;
        if (g_music_queue.count == 0)
            snprintf(response, sizeof(response), "Очередь музыки пуста.");
        else
        {
            used = (size_t)snprintf(response, sizeof(response),
                                    "Очередь (%u): ",
                                    (unsigned)g_music_queue.count);
            for (i = 0; i < g_music_queue.count && i < 3; ++i)
            {
                int written;
                if (used >= sizeof(response))
                    break;
                written = snprintf(response + used, sizeof(response) - used,
                                   "%s%u) %s [%.40s]",
                                   i ? "; " : "",
                                   (unsigned)(i + 1),
                                   g_music_queue.tracks[i].track_id,
                                   g_music_queue.tracks[i].requester);
                if (written < 0)
                    break;
                used += (size_t)written;
            }
        }
        result = twitch_chat_send_message(&config->twitch, response);
        if (result != BOT_OK)
            log_warning("Music queue response failed (result=%d)", (int)result);
        return BOT_OK;
    }

    if (music_request_matches(chat_message.text, config->bot.command_prefix))
    {
        const char *argument = chat_message.text + 1 + strlen("заказать");
        char video_id[12];
        char url[256];
        size_t length;
        size_t i;
        int duplicate = 0;
        YouTubeMetadata metadata;

        while (*argument == ' ' || *argument == '\t')
            ++argument;
        length = strlen(argument);
        while (length > 0 &&
               (argument[length - 1] == ' ' ||
                argument[length - 1] == '\t' ||
                argument[length - 1] == '\r' ||
                argument[length - 1] == '\n'))
            --length;

        if (length == 0 || length >= sizeof(url))
            snprintf(response, sizeof(response),
                     "Использование: !заказать ссылка_на_YouTube");
        else
        {
            memcpy(url, argument, length);
            url[length] = '\0';
            if (!youtube_video_id(url, video_id))
                snprintf(response, sizeof(response),
                         "Нужна ссылка на видео YouTube.");
            else
            {
                for (i = 0; i < g_music_queue.count; ++i)
                    if (strcmp(g_music_queue.tracks[i].track_id, video_id) == 0)
                    {
                        duplicate = 1;
                        break;
                    }

                if (duplicate)
                    snprintf(response, sizeof(response),
                             "Это видео уже находится в очереди.");
                else if (music_queue_size(&g_music_queue) >= MUSIC_QUEUE_CAPACITY)
                    snprintf(response, sizeof(response),
                             "Очередь заполнена (максимум %d заявок).",
                             MUSIC_QUEUE_CAPACITY);
                else if (!youtube_metadata_fetch(video_id, &metadata))
                    snprintf(response, sizeof(response),
                             "Не удалось получить информацию о видео. "
                             "Проверь yt-dlp и доступность YouTube.");
                else if (metadata.duration_seconds > 600)
                    snprintf(response, sizeof(response),
                             "Видео слишком длинное: %u мин. Максимум 10 мин.",
                             (metadata.duration_seconds + 59) / 60);
                else if (!music_queue_push(&g_music_queue, video_id,
                                           chat_message.chatter_user_name))
                    snprintf(response, sizeof(response),
                             "Не удалось добавить видео в очередь.");
                else
                {
                    snprintf(response, sizeof(response),
                             "%s заказал: %.180s (%u:%02u). "
                             "Позиция: %u. Воспроизведение пока выключено.",
                             chat_message.chatter_user_name,
                             metadata.title,
                             metadata.duration_seconds / 60,
                             metadata.duration_seconds % 60,
                             (unsigned)music_queue_size(&g_music_queue));
                    log_info("YouTube request: %s (%s), %u seconds, by %s",
                             video_id, metadata.title,
                             metadata.duration_seconds,
                             chat_message.chatter_user_name);
                }
            }
        }
        result = twitch_chat_send_message(&config->twitch, response);
        if (result != BOT_OK)
            log_warning("Music request response failed (result=%d)", (int)result);
        return BOT_OK;
    }

    if (!chat_command_parse(
            chat_message.text,
            config->bot.command_prefix,
            &command))
    {
        return BOT_OK;
    }

    if (command.type == CHAT_COMMAND_SOUNDS)
    {
        size_t i;
        size_t used = 0;
        int n;

        if (config->sound_count == 0)
        {
            snprintf(response, sizeof(response), "Звуковые эффекты пока не настроены.");
        }
        else
        {
            n = snprintf(response, sizeof(response), "Звуки:");
            if (n < 0 || (size_t)n >= sizeof(response))
                return BOT_OK;
            used = (size_t)n;
            for (i = 0; i < config->sound_count; ++i)
            {
                const SoundConfig *sound = &config->sounds[i];
                n = snprintf(response + used, sizeof(response) - used,
                             "%s!%s — %s",
                             i == 0 ? " " : " | ",
                             sound->command,
                             sound->cost == 0 ? "бесплатно" : "");
                if (n < 0 || (size_t)n >= sizeof(response) - used)
                    break;
                used += (size_t)n;
                if (sound->cost > 0)
                {
                    n = snprintf(response + used, sizeof(response) - used,
                                 "%lld апельсинов", sound->cost);
                    if (n < 0 || (size_t)n >= sizeof(response) - used)
                        break;
                    used += (size_t)n;
                }
            }
        }
        result = twitch_chat_send_message(&config->twitch, response);
        if (result != BOT_OK)
            log_warning("Failed to send !звуки catalog (result=%d)", (int)result);
        return BOT_OK;
    }

    profile =
        viewer_profile_get_or_create(
            chat_message.chatter_user_id,
            chat_message.chatter_user_login,
            chat_message.chatter_user_name
        );


    if (profile == NULL)
    {
        log_warning(
            "Failed to load or create viewer profile: %s (%s)",
            chat_message.chatter_user_name,
            chat_message.chatter_user_id
        );
    }

    /*
     * Неизвестные команды не участвуют
     * в систему кулдаунов
     */
    if (command.type != CHAT_COMMAND_UNKNOWN &&
        !command_cooldown_try_use(
            chat_message.chatter_user_id,
            command.type))
    {
        log_debug(
            "Command cooldown: %s (%s) %s",
            chat_message.chatter_user_name,
            chat_message.chatter_user_id,
            chat_message.text
        );

        return BOT_OK;
    }

    /* Shared cooldown across viewers; OBS failures are non-fatal. */
    if (command.type == CHAT_COMMAND_BRUH)
    {
        DWORD now = GetTickCount();
        ObsWebSocket obs = {0};

        if (!config->obs.enabled)
        {
            log_warning("!bruh ignored: OBS integration disabled");
            return BOT_OK;
        }
        if (g_bruh_played &&
            (DWORD)(now - g_last_bruh_tick) < BRUH_GLOBAL_COOLDOWN_MS)
        {
            log_debug("!bruh global cooldown: %s", chat_message.chatter_user_name);
            return BOT_OK;
        }

        result = obs_websocket_connect(&obs, config->obs.password);
        if (result == BOT_OK)
            result = obs_websocket_restart_media(&obs, "Bot_Bruh");
        obs_websocket_close(&obs);

        if (result == BOT_OK)
        {
            g_last_bruh_tick = GetTickCount();
            g_bruh_played = 1;
            log_info("OBS !bruh played by %s", chat_message.chatter_user_name);
        }
        else
        {
            log_warning("OBS !bruh failed for %s (result=%d)",
                        chat_message.chatter_user_name, (int)result);
        }
        return BOT_OK;
    }

    /*
     * Баланс зависит от конкретного Twitch-пользователя,
     * поэтому ответ формируется здесь, а не в commands.c.
     */
	if (command.type == CHAT_COMMAND_CLAIM)
	{
		long long reward;

		if (profile == NULL)
		{
			snprintf(
				response,
				sizeof(response),
				"Не удалось загрузить профиль"
			);
		}
		else if (
			chat_event == NULL ||
			!chat_event->active ||
			time(NULL) >= chat_event->expires_at)
		{
			/*
			 * Нет активного дропа
			 * Молча игнорируем команду
			 */
			return BOT_OK;
		}
		else
		{
			long long old_balance = profile->balance;
			reward = chat_event->reward;
			profile->balance += reward;

			if (!viewer_profile_save())
			{
				profile->balance = old_balance;

				snprintf(
					response,
					sizeof (response),
					"Не удалось сохранить награду."
				);
			}
			else
			{
				long long claimed_reward;

				/*
				 * Завершаем событие только после
				 * успешного сохранения баланса.
				 */
				chat_event_claim(
					chat_event,
					&claimed_reward
				);

				snprintf(
					response,
					sizeof(response),
					"%s первым забрал золотой апельсин! "
					"Награда: %lld апельсинов. Баланс: %lld.",
					profile->display_name,
					reward,
					profile->balance
				);
			}
		}
	}
	else if (command.type == CHAT_COMMAND_BALANCE)
    {
        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else
        {
            snprintf(
                response,
                sizeof(response),
                "%s, у тебя %lld апельсинов.",
                profile->display_name,
                profile->balance
            );
        }
    }
	else if (command.type == CHAT_COMMAND_RANK)
	{
		if (profile == NULL)
		{
			snprintf(
				response,
				sizeof(response),
				"Не удалось загрузить профиль."
			);
		}
		else
		{
			const ViewerRank *current;
			const ViewerRank *next;

			current = viewer_rank_get(profile->balance);
			next = viewer_rank_next(profile->balance);

			if (next == NULL)
			{
				snprintf(
					response,
					sizeof(response),
					"%s, твое звание: %s!",
					profile->display_name,
					current->name
				);
			}
			else
			{
				long long remaining;

				remaining = next->minimum_balance -
				profile->balance;

				snprintf(
					response,
					sizeof(response),
					"%s, звание: %s. "
					"До звания \"%s\" осталось"
					"%lld апельсинов.",
					profile->display_name,
					current->name,
					next->name,
					remaining
				);
			}
		}
	}
    else if (command.type == CHAT_COMMAND_PROFILE)
    {
        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else
        {
            long long days_with_channel = 0;

			const ViewerRank *rank;

			rank = viewer_rank_get(profile->balance);

            if (profile->created_at > 0)
            {
                time_t now = time(NULL);

                if ((long long)now > profile->created_at)
                {
                    days_with_channel =
                        ((long long)now - profile->created_at) /
                        (24LL * 60LL * 60LL);
                }
            }

            snprintf(
                response,
                sizeof(response),
				"%s | Звание: %s | %lld апельсинов | "
				"с нами %lld дн. | ежедневок: %lld | "
				"монетка: %lld/%lld | "
				"слот: джекпот %lld, пары %lld, проигрыши %lld | "
				"дуэли: %lld/%lld",
                profile->display_name,
				rank->name,
                profile->balance,
                days_with_channel,
                profile->daily_count,
                profile->coin_wins,
                profile->coin_losses,
                profile->slot_jackpots,
                profile->slot_pairs,
                profile->slot_losses,
                profile->duel_wins,
                profile->duel_losses
            );
        }
    }
    else if (command.type == CHAT_COMMAND_TOP)
    {
        if (!viewer_profile_build_top(
                5,
                response,
                sizeof(response)))
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось сформировать рейтинг."
            );
        }
    }
    else if (command.type == CHAT_COMMAND_DAILY)
    {
        time_t now;
        long long elapsed;
        long long remaining;
        const long long daily_interval = 24LL * 60LL * 60LL;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else
        {
            now = time(NULL);
            elapsed = (long long)now - profile->last_daily;

            if (
                profile->last_daily != 0 &&
                elapsed < daily_interval)
            {
                long long hours;
                long long minutes;

                remaining = daily_interval - elapsed;

                /*
                 * Округляем оставшееся время вверх до минуты,
                 * чтобы не показывать 0 ч 0 мин за несколько секунд
                 * до следующей награды.
                 */
                minutes = (remaining + 59) / 60;
                hours = minutes / 60;
                minutes %= 60;

                snprintf(
                    response,
                    sizeof(response),
                    "%s, ежедневка уже получена. Следующая через %lld ч %lld мин.",
                    profile->display_name,
                    hours,
                    minutes
                );
            }
            else
            {
                const long long reward = 75;
                long long old_balance;
                long long old_last_daily;
                long long old_daily_count;

                old_balance = profile->balance;
                old_last_daily = profile->last_daily;
                old_daily_count = profile->daily_count;

                profile->balance += reward;
                profile->last_daily = (long long)now;
                profile->daily_count += 1;

                if (!viewer_profile_save())
                {
                    /*
                     * Если запись на диск не удалась, возвращаем профиль
                     * в прежнее состояние.
                     */
                    profile->balance = old_balance;
                    profile->last_daily = old_last_daily;
                    profile->daily_count = old_daily_count;

                    snprintf(
                        response,
                        sizeof(response),
                        "%s, не удалось сохранить ежедневную награду.",
                        profile->display_name
                    );
                }
                else
                {
                    snprintf(
                        response,
                        sizeof(response),
                        "%s получает %lld апельсинов! Баланс: %lld апельсинов.",
                        profile->display_name,
                        reward,
                        profile->balance
                    );
                }
            }
        }
    }
    else if (command.type == CHAT_COMMAND_COIN)
    {
        long long bet;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else if (!parse_orange_bet(command.arguments, &bet))
        {
            snprintf(
                response,
                sizeof(response),
                "Использование: !монетка <ставка 1-10000>"
            );
        }
        else if (bet > profile->balance)
        {
            snprintf(
                response,
                sizeof(response),
                "%s, не хватает апельсинов. Баланс: %lld.",
                profile->display_name,
                profile->balance
            );
        }
        else
        {
            long long old_balance = profile->balance;
            long long old_coin_wins = profile->coin_wins;
            long long old_coin_losses = profile->coin_losses;
            int win;

            economy_random_init();
            win = rand() % 2 == 0;

            if (win)
            {
                profile->balance += bet;
                profile->coin_wins += 1;
            }
            else
            {
                profile->balance -= bet;
                profile->coin_losses += 1;
            }

            if (!viewer_profile_save())
            {
                profile->balance = old_balance;
                profile->coin_wins = old_coin_wins;
                profile->coin_losses = old_coin_losses;

                snprintf(
                    response,
                    sizeof(response),
                    "%s, не удалось сохранить результат игры.",
                    profile->display_name
                );
            }
            else if (win)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "Монетка: орёл! %s выигрывает %lld апельсинов. Баланс: %lld.",
                    profile->display_name,
                    bet,
                    profile->balance
                );
            }
            else
            {
                snprintf(
                    response,
                    sizeof(response),
                    "Монетка: решка! %s проигрывает %lld апельсинов. Баланс: %lld.",
                    profile->display_name,
                    bet,
                    profile->balance
                );
            }
        }
    }
    else if (command.type == CHAT_COMMAND_SLOT)
    {
        static const char *symbols[] =
        {
            "7",
            "BAR",
            "АПЕЛЬСИН",
            "ВИШНЯ",
            "ЗВЕЗДА"
        };

        long long bet;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else if (!parse_orange_bet(command.arguments, &bet))
        {
            snprintf(
                response,
                sizeof(response),
                "Использование: !слот <ставка 1-10000>"
            );
        }
        else if (bet > profile->balance)
        {
            snprintf(
                response,
                sizeof(response),
                "%s, не хватает апельсинов. Баланс: %lld.",
                profile->display_name,
                profile->balance
            );
        }
        else
        {
            int first;
            int second;
            int third;
            int triple;
            int pair;
            long long old_balance = profile->balance;
            long long old_slot_jackpots = profile->slot_jackpots;
            long long old_slot_pairs = profile->slot_pairs;
            long long old_slot_losses = profile->slot_losses;

            economy_random_init();

            first = rand() % 5;
            second = rand() % 5;
            third = rand() % 5;

            triple =
                first == second &&
                second == third;

            pair =
                !triple &&
                (
                    first == second ||
                    first == third ||
                    second == third
                );

            if (triple)
            {
                /*
                 * Выплата 10x означает чистый выигрыш 9 ставок:
                 * сама поставленная сумма также возвращается игроку.
                 */
                profile->balance += bet * 9;
                profile->slot_jackpots += 1;
            }
            else if (pair)
            {
                profile->slot_pairs += 1;
            }
            else
            {
                profile->balance -= bet;
                profile->slot_losses += 1;
            }

            if (!viewer_profile_save())
            {
                profile->balance = old_balance;
                profile->slot_jackpots = old_slot_jackpots;
                profile->slot_pairs = old_slot_pairs;
                profile->slot_losses = old_slot_losses;

                snprintf(
                    response,
                    sizeof(response),
                    "%s, не удалось сохранить результат игры.",
                    profile->display_name
                );
            }
            else if (triple)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "[%s] [%s] [%s] ДЖЕКПОТ x10! %s выигрывает %lld апельсинов. Баланс: %lld.",
                    symbols[first],
                    symbols[second],
                    symbols[third],
                    profile->display_name,
                    bet * 9,
                    profile->balance
                );
            }
            else if (pair)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "[%s] [%s] [%s] Пара! Ставка возвращена. Баланс: %lld.",
                    symbols[first],
                    symbols[second],
                    symbols[third],
                    profile->balance
                );
            }
            else
            {
                snprintf(
                    response,
                    sizeof(response),
                    "[%s] [%s] [%s] Мимо! Потеряно %lld апельсинов. Баланс: %lld.",
                    symbols[first],
                    symbols[second],
                    symbols[third],
                    bet,
                    profile->balance
                );
            }
        }
    }
    else if (command.type == CHAT_COMMAND_DUEL)
    {
        char target_login[VIEWER_PROFILE_LOGIN_SIZE];
        long long bet;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else if (!parse_duel_arguments(
                    command.arguments,
                    target_login,
                    sizeof(target_login),
                    &bet))
        {
            snprintf(
                response,
                sizeof(response),
                "Использование: !дуэль @ник <ставка 1-10000>"
            );
        }
        else if (
            ascii_equals_ignore_case(
                target_login,
                chat_message.chatter_user_login
            ))
        {
            snprintf(
                response,
                sizeof(response),
                "%s, нельзя вызвать на дуэль самого себя.",
                profile->display_name
            );
        }
        else if (bet > profile->balance)
        {
            snprintf(
                response,
                sizeof(response),
                "%s, не хватает апельсинов. Баланс: %lld.",
                profile->display_name,
                profile->balance
            );
        }
        else if (!viewer_duel_create(
                    chat_message.chatter_user_id,
                    profile->display_name,
                    target_login,
                    bet))
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось создать дуэль: у одного из участников уже есть активный вызов."
            );
        }
        else
        {
            snprintf(
                response,
                sizeof(response),
                "%s вызывает @%s на дуэль за %lld апельсинов! @%s: !принять или !отказ. Вызов действует 60 сек.",
                profile->display_name,
                target_login,
                bet,
                target_login
            );
        }
    }
    else if (command.type == CHAT_COMMAND_ACCEPT)
    {
        char challenger_user_id[VIEWER_PROFILE_ID_SIZE];
        char challenger_name[VIEWER_PROFILE_NAME_SIZE];
        long long bet;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else if (!viewer_duel_accept(
                    chat_message.chatter_user_id,
                    chat_message.chatter_user_login,
                    profile->display_name,
                    challenger_user_id,
                    sizeof(challenger_user_id),
                    challenger_name,
                    sizeof(challenger_name),
                    &bet))
        {
            snprintf(
                response,
                sizeof(response),
                "%s, активного вызова на дуэль нет.",
                profile->display_name
            );
        }
        else
        {
            ViewerProfile *challenger =
                viewer_profile_find(
                    challenger_user_id
                );

            if (challenger == NULL)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "Профиль соперника не найден. Дуэль отменена."
                );
            }
            else if (
                challenger->balance < bet ||
                profile->balance < bet)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "Дуэль отменена: у одного из участников уже не хватает %lld апельсинов.",
                    bet
                );
            }
            else
            {
                long long challenger_old_balance =
                    challenger->balance;

                long long target_old_balance =
                    profile->balance;

                long long challenger_old_wins =
                    challenger->duel_wins;
                long long challenger_old_losses =
                    challenger->duel_losses;
                long long target_old_wins =
                    profile->duel_wins;
                long long target_old_losses =
                    profile->duel_losses;

                int challenger_wins;

                economy_random_init();

                challenger_wins =
                    rand() % 2 == 0;

                if (challenger_wins)
                {
                    challenger->balance += bet;
                    profile->balance -= bet;
                    challenger->duel_wins += 1;
                    profile->duel_losses += 1;
                }
                else
                {
                    challenger->balance -= bet;
                    profile->balance += bet;
                    challenger->duel_losses += 1;
                    profile->duel_wins += 1;
                }

                if (!viewer_profile_save())
                {
                    challenger->balance =
                        challenger_old_balance;

                    profile->balance =
                        target_old_balance;

                    challenger->duel_wins =
                        challenger_old_wins;
                    challenger->duel_losses =
                        challenger_old_losses;
                    profile->duel_wins =
                        target_old_wins;
                    profile->duel_losses =
                        target_old_losses;

                    snprintf(
                        response,
                        sizeof(response),
                        "Не удалось сохранить результат дуэли."
                    );
                }
                else if (challenger_wins)
                {
                    snprintf(
                        response,
                        sizeof(response),
                        "%s побеждает %s в дуэли и забирает %lld апельсинов! Балансы: %s %lld, %s %lld.",
                        challenger->display_name,
                        profile->display_name,
                        bet,
                        challenger->display_name,
                        challenger->balance,
                        profile->display_name,
                        profile->balance
                    );
                }
                else
                {
                    snprintf(
                        response,
                        sizeof(response),
                        "%s побеждает %s в дуэли и забирает %lld апельсинов! Балансы: %s %lld, %s %lld.",
                        profile->display_name,
                        challenger->display_name,
                        bet,
                        profile->display_name,
                        profile->balance,
                        challenger->display_name,
                        challenger->balance
                    );
                }
            }
        }
    }
    else if (command.type == CHAT_COMMAND_DECLINE)
    {
        char challenger_name[VIEWER_PROFILE_NAME_SIZE];
        long long bet;

        if (profile == NULL)
        {
            snprintf(
                response,
                sizeof(response),
                "Не удалось загрузить профиль."
            );
        }
        else if (!viewer_duel_decline(
                    chat_message.chatter_user_login,
                    challenger_name,
                    sizeof(challenger_name),
                    &bet))
        {
            snprintf(
                response,
                sizeof(response),
                "%s, активного вызова на дуэль нет.",
                profile->display_name
            );
        }
        else
        {
            snprintf(
                response,
                sizeof(response),
                "%s отказался от дуэли с %s за %lld апельсинов.",
                profile->display_name,
                challenger_name,
                bet
            );
        }
    }
    else if (!chat_command_build_response(
            &command,
            config->telegram.channel_url,
            config->discord.invite_url,
            response,
            sizeof(response)))
    {
        if (command.type == CHAT_COMMAND_UNKNOWN)
        {
            log_debug(
                "Unknown Twitch chat command: %s",
                chat_message.text
            );
        }

        return BOT_OK;
    }

    log_info(
        "Bot response: %s",
        response
    );

    result = twitch_chat_send_message(
        &config->twitch,
        response
    );

    /*
     * Если токен успел истечь именно во время отправки
     * сообщения, восстанавливаем OAuth и пробуем один раз ещё.
     */
    if (result == BOT_ERR_AUTH)
    {
        log_warning(
            "Twitch chat access token was rejected; restoring OAuth session"
        );

        result = ensure_twitch_auth(config);
        if (result != BOT_OK)
        {
            return result;
        }

        result = twitch_chat_send_message(
            &config->twitch,
            response
        );
    }

    return result;
}


/*
 * ============================================================
 * CHECK STREAM STATE
 * ============================================================
 */
static BotResult update_stream_state(
    AppConfig *config,
    int *previous_live_state
)
{
    TwitchStream current_stream = {0};
    BotResult result;

    if (config == NULL || previous_live_state == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    result = get_current_stream(
        &config->twitch,
        &current_stream
    );

    if (result == BOT_ERR_AUTH)
    {
        log_warning(
            "Twitch access token was rejected"
        );

        result = ensure_twitch_auth(config);
        if (result != BOT_OK)
        {
            return result;
        }

        result = get_current_stream(
            &config->twitch,
            &current_stream
        );
    }

    if (result != BOT_OK)
    {
        return result;
    }

    if (*previous_live_state == 0 &&
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
    else if (*previous_live_state != 0 &&
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
    else
    {
        log_debug(
            "Stream state unchanged: %s",
            current_stream.is_live
                ? "ONLINE"
                : "OFFLINE"
        );
    }

    *previous_live_state =
        current_stream.is_live;

    return BOT_OK;
}


/*
 * ============================================================
 * MAIN BOT LOOP
 * ============================================================
 *
 * Теперь обычный запуск одновременно:
 *
 * 1. держит EventSub WebSocket для Twitch-чата;
 * 2. принимает сколько угодно команд;
 * 3. отвечает на команды в Twitch;
 * 4. каждые 30 секунд проверяет состояние стрима;
 * 5. периодически валидирует OAuth;
 * 6. после разрыва EventSub автоматически подключается заново.
 *
 * Отдельный поток здесь не нужен: Twitch присылает EventSub
 * keepalive-сообщения, поэтому цикл регулярно просыпается и
 * выполняет периодические задачи мониторинга стрима.
 */
static BotResult run_bot_loop(
    AppConfig *config
)
{
    TwitchEventSubWebSocket websocket = {0};
    TwitchEventSubMessageType message_type;
    TwitchStream initial_stream = {0};

    BotResult result;
    BotResult chat_result;
	ChatEvent chat_event;

    int previous_live_state;
    int chat_connected = 0;

    DWORD last_stream_check;
    DWORD last_token_validation;
    DWORD now;

    char json[32768];

    if (config == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    /*
     * Получаем базовое состояние стрима.
     * Если бот запущен во время уже идущего стрима,
     * ложное событие STREAM STARTED не создаётся.
     */
    result = get_current_stream(
        &config->twitch,
        &initial_stream
    );

    if (result != BOT_OK)
    {
        return result;
    }

    previous_live_state =
        initial_stream.is_live;

    if (initial_stream.is_live)
    {
        log_info(
            "Initial stream status: ONLINE"
        );

        log_stream_information(
            &initial_stream
        );
    }
    else
    {
        log_info(
            "Initial stream status: OFFLINE"
        );
    }

    log_info(
        "Main bot loop started"
    );

    log_info(
        "Stream polling interval: %d seconds",
        STREAM_POLL_INTERVAL_SECONDS
    );

    log_info(
        "Twitch chat commands are enabled"
    );

    log_info(
        "Press Ctrl+C to stop the bot"
    );

    last_stream_check =
        GetTickCount();

    last_token_validation =
        GetTickCount();

	chat_event_init(
			&chat_event
	);

    while (!stop_requested())
    {
        /*
         * ----------------------------------------------------
         * EVENTSUB CONNECT / RECONNECT
         * ----------------------------------------------------
         */
        if (!chat_connected)
        {
            result = connect_twitch_chat_listener(
                config,
                &websocket
            );

            if (result == BOT_ERR_AUTH)
            {
                log_warning(
                    "Twitch EventSub authorization failed; restoring OAuth session"
                );

                result = ensure_twitch_auth(config);

                if (result == BOT_OK)
                {
                    result = connect_twitch_chat_listener(
                        config,
                        &websocket
                    );
                }
            }

            if (result != BOT_OK)
            {
                log_warning(
                    "Failed to start Twitch chat listener: %s",
                    bot_result_to_string(result)
                );

                twitch_eventsub_ws_close(
                    &websocket
                );

                sleep_interruptible(
                    EVENTSUB_RECONNECT_DELAY_SECONDS
                );
            }
            else
            {
                chat_connected = 1;
            }
        }

        /*
         * ----------------------------------------------------
         * RECEIVE ONE EVENTSUB MESSAGE
         * ----------------------------------------------------
         *
         * Если чат временно недоступен, этот блок пропускается,
         * но мониторинг стрима ниже продолжает работать.
         */
        if (chat_connected && !stop_requested())
        {
            result = twitch_eventsub_ws_receive(
                &websocket,
                json,
                sizeof(json)
            );

            if (result != BOT_OK)
            {
                if (!stop_requested())
                {
                    log_warning(
                        "Twitch EventSub connection lost: %s",
                        bot_result_to_string(result)
                    );

                    log_info(
                        "Twitch chat reconnect in %d seconds",
                        EVENTSUB_RECONNECT_DELAY_SECONDS
                    );
                }

                twitch_eventsub_ws_close(
                    &websocket
                );

                chat_connected = 0;

                if (!stop_requested())
                {
                    sleep_interruptible(
                        EVENTSUB_RECONNECT_DELAY_SECONDS
                    );
                }
            }
            else
            {
                result = twitch_eventsub_get_message_type(
                    json,
                    &message_type
                );

                if (result != BOT_OK)
                {
                    log_warning(
                        "Failed to detect Twitch EventSub message type"
                    );
                }
                else if (message_type == TWITCH_EVENTSUB_KEEPALIVE)
                {
                    log_debug(
                        "Twitch EventSub keepalive"
                    );
                }
                else if (message_type == TWITCH_EVENTSUB_RECONNECT)
                {
                    /*
                     * В этой версии открываем новую EventSub-сессию
                     * и создаём подписку заново. Это сохраняет работу
                     * бота после серверного reconnect-события.
                     *
                     * Бесшовный переход по reconnect_url можно добавить
                     * отдельным улучшением позже.
                     */
                    log_warning(
                        "Twitch requested EventSub reconnect"
                    );

                    twitch_eventsub_ws_close(
                        &websocket
                    );

                    chat_connected = 0;
                }
                else if (message_type == TWITCH_EVENTSUB_NOTIFICATION)
                {
                    chat_result = process_twitch_chat_notification(
                        config,
						json,
						&chat_event
                    );

                    if (chat_result != BOT_OK)
                    {
                        log_warning(
                            "Failed to process Twitch chat message: %s",
                            bot_result_to_string(chat_result)
                        );
                    }
                }
            }
        }

        now = GetTickCount();

        /*
         * ----------------------------------------------------
         * PERIODIC OAUTH VALIDATION
         * ----------------------------------------------------
         */
        if (now - last_token_validation >=
            TOKEN_VALIDATION_INTERVAL_MS)
        {
            log_info(
                "Performing scheduled Twitch token validation..."
            );

            result = ensure_twitch_auth(
                config
            );

            if (result != BOT_OK)
            {
                log_warning(
                    "Scheduled Twitch authentication check failed: %s",
                    bot_result_to_string(result)
                );
            }
            else
            {
                last_token_validation =
                    now;
            }
        }

        /*
         * ----------------------------------------------------
         * STREAM POLLING
         * ----------------------------------------------------
         */
        if (now - last_stream_check >=
            (DWORD)(STREAM_POLL_INTERVAL_SECONDS * 1000UL))
        {
            result = update_stream_state(
                config,
                &previous_live_state
            );

            if (result != BOT_OK)
            {
                log_warning(
                    "Failed to check stream status: %s",
                    bot_result_to_string(result)
                );
            }

            last_stream_check =
                now;
        }
		/*
		 * ----------------------------------------------------
		 * RANDOM CHAT EVENT
		 * ----------------------------------------------------
		 */
		int event_result;

		event_result=
				chat_event_update(
					&chat_event
				);

		if (event_result == 1)
		{
			char event_message[512];

			snprintf(
				event_message,
				sizeof (event_message),
				" В чате появился золотой апельсин! "
				"Первый, уто напишет !забрать, получит %lld апельсинов. "
				"У вас 60 секунд",
				chat_event.reward
			);

			log_info(
				"Random chat event started: reward=%lld",
				chat_event.reward
			);

			result =
				twitch_chat_send_message(
					&config->twitch,
					event_message
			);

			if (result != BOT_OK)
			{
				log_warning(
					"Failed to send random chat event message: %s",
					bot_result_to_string(result)
				);
			}
		}
		else if (event_result == -1)
		{
			log_info(
				"Random chat event expired"
			);
		}
    }

    twitch_eventsub_ws_close(
        &websocket
    );

    log_info(
        "Twitch chat listener stopped"
    );

    log_info(
        "Main bot loop stopped"
    );

    return BOT_OK;
}

/*
 * ============================================================
 * APPLICATION
 * ============================================================
 */
int app_run(
    int argc,
    char *argv[]
)
{
    AppConfig config;

    CommandOptions command_options;

    BotResult result;


    setup_console_utf8();

    command_cooldown_init();
    viewer_duel_init();

    /*
     * ========================================================
     * COMMAND LINE
     * ========================================================
     */
    if (
        !command_parse(
            argc,
            argv,
            &command_options
        ))
    {
        command_print_help(
            argc > 0
                ? argv[0]
                : NULL
        );


        return 1;
    }


    if (
        command_options.show_help)
    {
        command_print_help(
            argc > 0
                ? argv[0]
                : NULL
        );


        return 0;
    }


    /*
     * ========================================================
     * LOG DIRECTORY
     * ========================================================
     */
    result =
        platform_create_directory(
            "logs"
        );


    if (
        result != BOT_OK)
    {
        fprintf(
            stderr,
            "Failed to create logs directory\n"
        );


        return 1;
    }


    /*
     * ========================================================
     * LOGGER
     * ========================================================
     */
    if (
        logger_init(
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


    /*
     * ========================================================
     * CTRL+C
     * ========================================================
     */
    if (
        !SetConsoleCtrlHandler(
            console_ctrl_handler,
            TRUE
        ))
    {
        log_warning(
            "Failed to install console control handler"
        );
    }


    /*
     * ========================================================
     * DATA DIRECTORY
     * ========================================================
     */
    result =
        platform_create_directory(
            "data"
        );


    if (
        result != BOT_OK)
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
     * ========================================================
     * VIEWER PROFILES
     * ========================================================
     */
    if (!viewer_profile_init())
    {
        log_error(
            "Failed to initialize viewer profiles"
        );

        logger_shutdown();

        return 1;
    }


    log_info(
        "Viewer profiles loaded: %u",
        (unsigned int)viewer_profile_count()
    );


    /*
     * ========================================================
     * CONFIG
     * ========================================================
     */
    result =
        config_load(
            "config.ini",
            &config
        );


    if (
        result != BOT_OK)
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


    if (
        result != BOT_OK)
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
     * ========================================================
     * OFFLINE TESTS
     * ========================================================
     */
    if (
        command_options.test_chat)
    {
        log_info(
            "Test mode requested: --test-chat"
        );


        run_chat_test_console(
            &config
        );


        logger_shutdown();


        return 0;
    }


    if (
        command_options.test_eventsub_chat)
    {
        log_info(
            "Test mode requested: --test-eventsub-chat"
        );


        run_eventsub_chat_test(
            &config
        );


        logger_shutdown();


        return 0;
    }


    if (
        command_options.test_stream)
    {
        log_info(
            "Test mode requested: --test-stream"
        );


        if (
            command_options.dry_run)
        {
            log_info(
                "Dry run enabled: network requests are disabled"
            );
        }


        run_test_stream(
            &config,
            command_options.dry_run
        );


        logger_shutdown();


        return 0;
    }


    /*
     * ========================================================
     * NETWORK CONFIG
     * ========================================================
     */
    if (
        config.twitch.client_id[0] ==
        '\0')
    {
        log_error(
            "Twitch Client ID is not configured"
        );


        logger_shutdown();


        return 1;
    }


    if (
        config.telegram.bot_token[0] ==
        '\0')
    {
        log_warning(
            "Telegram bot token is not configured yet"
        );
    }


    /*
     * ========================================================
     * INTERNET TEST
     * ========================================================
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


        if (
            result != BOT_OK)
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


            if (
                response.status_code ==
                200)
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
     * ========================================================
     * TWITCH AUTH
     * ========================================================
     */
    result =
        ensure_twitch_auth(
            &config
        );


    if (
        result != BOT_OK)
    {
        log_error(
            "Failed to establish Twitch OAuth session: %s",
            bot_result_to_string(result)
        );


        logger_shutdown();


        return 1;
    }


    /*
     * ========================================================
     * BROADCASTER INFORMATION
     * ========================================================
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


        if (
            result != BOT_OK)
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


        if (
            result != BOT_OK)
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
     * ========================================================
     * TEST REAL TWITCH MESSAGE
     * ========================================================
     */
    if (
        command_options.test_twitch_chat)
    {
        const char *test_message =
            "? TwitchBot подключён к чату. Тестовое сообщение.";


        log_info(
            "Test mode requested: --test-twitch-chat"
        );


        log_info(
            "Twitch chat test message: %s",
            test_message
        );


        result =
            twitch_chat_send_message(
                &config.twitch,
                test_message
            );


        if (
            result != BOT_OK)
        {
            log_error(
                "Twitch chat test failed: %s",
                bot_result_to_string(result)
            );


            logger_shutdown();


            return 1;
        }


        log_info(
            "Twitch chat test completed successfully"
        );


        logger_shutdown();


        return 0;
    }


    /*
     * ========================================================
     * TEST REAL TWITCH COMMAND
     * ========================================================
     */
    if (
        command_options.test_twitch_command)
    {
        log_info(
            "Test mode requested: --test-twitch-command"
        );


        result =
            run_real_twitch_command_test(
                &config
            );


        if (
            result != BOT_OK)
        {
            log_error(
                "Real Twitch command test failed: %s",
                bot_result_to_string(result)
            );


            logger_shutdown();


            return 1;
        }


        logger_shutdown();


        return 0;
    }


    /*
     * ========================================================
     * INITIALIZATION COMPLETE
     * ========================================================
     */
    log_info(
        "Application initialization completed successfully"
    );


    /*
     * ========================================================
     * MAIN LOOP
     * ========================================================
     */
    result =
        run_bot_loop(
            &config
        );


    if (
        result != BOT_OK)
    {
        log_error(
            "Main bot loop stopped with error: %s",
            bot_result_to_string(result)
        );
    }


    /*
     * ========================================================
     * SHUTDOWN
     * ========================================================
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
