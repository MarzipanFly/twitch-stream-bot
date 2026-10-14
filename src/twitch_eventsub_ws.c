#include "twitch_eventsub_ws.h"

#include "http_client.h"
#include "logger.h"

#include <stdio.h>
#include <string.h>

#define TWITCH_EVENTSUB_HOST L"eventsub.wss.twitch.tv"
#define TWITCH_EVENTSUB_PATH L"/ws"
#define TWITCH_API_HOST L"api.twitch.tv"
#define TWITCH_EVENTSUB_SUBSCRIPTIONS_PATH L"/helix/eventsub/subscriptions"

/*
 * Значение WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET из современного Windows SDK.
 * В старом MinGW 7.3 эта константа отсутствует.
 */
#define TWITCH_WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114

/*
 * Совместимый локальный вариант WINHTTP_WEB_SOCKET_BUFFER_TYPE.
 */
typedef enum
{
    TWITCH_WS_BINARY_MESSAGE = 0,
    TWITCH_WS_BINARY_FRAGMENT = 1,
    TWITCH_WS_UTF8_MESSAGE = 2,
    TWITCH_WS_UTF8_FRAGMENT = 3,
    TWITCH_WS_CLOSE = 4

} TwitchWebSocketBufferType;

typedef HINTERNET (WINAPI *TwitchWebSocketCompleteUpgradeFunction)(
    HINTERNET request,
    DWORD_PTR context
);

typedef DWORD (WINAPI *TwitchWebSocketReceiveFunction)(
    HINTERNET websocket,
    PVOID buffer,
    DWORD buffer_length,
    DWORD *bytes_read,
    TwitchWebSocketBufferType *buffer_type
);

static void log_ws_error(
    const char *operation,
    DWORD error_code
)
{
    log_error(
        "%s failed. Windows error code: %lu",
        operation,
        (unsigned long)error_code
    );
}

static BotResult load_websocket_api(
    TwitchEventSubWebSocket *client
)
{
    if (client == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    client->winhttp_module = LoadLibraryW(L"winhttp.dll");

    if (client->winhttp_module == NULL)
    {
        log_ws_error(
            "LoadLibraryW(winhttp.dll)",
            GetLastError()
        );

        return BOT_ERR_NETWORK;
    }

    client->websocket_complete_upgrade = GetProcAddress(
        client->winhttp_module,
        "WinHttpWebSocketCompleteUpgrade"
    );

    client->websocket_receive = GetProcAddress(
        client->winhttp_module,
        "WinHttpWebSocketReceive"
    );

    if (client->websocket_complete_upgrade == NULL ||
        client->websocket_receive == NULL)
    {
        log_error(
            "Required WinHTTP WebSocket functions are not available"
        );

        return BOT_ERR_NETWORK;
    }

    log_debug(
        "WinHTTP WebSocket API loaded successfully"
    );

    return BOT_OK;
}

BotResult twitch_eventsub_ws_connect(
    TwitchEventSubWebSocket *client
)
{
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    BotResult result;
    TwitchWebSocketCompleteUpgradeFunction complete_upgrade;

    if (client == NULL)
    {
        return BOT_ERR_CONFIG;
    }

    memset(client, 0, sizeof(*client));

    result = load_websocket_api(client);
    if (result != BOT_OK)
    {
        twitch_eventsub_ws_close(client);
        return result;
    }

    log_info(
        "Connecting to Twitch EventSub WebSocket..."
    );

    client->session = WinHttpOpen(
        L"TwitchBot/0.6",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (client->session == NULL)
    {
        log_ws_error("WinHttpOpen", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    client->connection = WinHttpConnect(
        client->session,
        TWITCH_EVENTSUB_HOST,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (client->connection == NULL)
    {
        log_ws_error("WinHttpConnect", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    client->request = WinHttpOpenRequest(
        client->connection,
        L"GET",
        TWITCH_EVENTSUB_PATH,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (client->request == NULL)
    {
        log_ws_error("WinHttpOpenRequest", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    if (!WinHttpSetOption(
            client->request,
            TWITCH_WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
            NULL,
            0))
    {
        log_ws_error(
            "WinHttpSetOption(WEB_SOCKET)",
            GetLastError()
        );

        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    if (!WinHttpSendRequest(
            client->request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0))
    {
        log_ws_error("WinHttpSendRequest", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    if (!WinHttpReceiveResponse(client->request, NULL))
    {
        log_ws_error("WinHttpReceiveResponse", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    if (!WinHttpQueryHeaders(
            client->request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status_code,
            &status_size,
            WINHTTP_NO_HEADER_INDEX))
    {
        log_ws_error("WinHttpQueryHeaders", GetLastError());
        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    log_debug(
        "Twitch EventSub WebSocket HTTP status: %lu",
        (unsigned long)status_code
    );

    if (status_code != 101)
    {
        log_error(
            "Twitch EventSub WebSocket upgrade failed"
        );

        twitch_eventsub_ws_close(client);
        return BOT_ERR_TWITCH;
    }

    complete_upgrade =
        (TwitchWebSocketCompleteUpgradeFunction)
            client->websocket_complete_upgrade;

    client->websocket = complete_upgrade(
        client->request,
        0
    );

    if (client->websocket == NULL)
    {
        log_ws_error(
            "WinHttpWebSocketCompleteUpgrade",
            GetLastError()
        );

        twitch_eventsub_ws_close(client);
        return BOT_ERR_NETWORK;
    }

    log_info(
        "Twitch EventSub WebSocket connected"
    );

    return BOT_OK;
}

BotResult twitch_eventsub_ws_receive(
    TwitchEventSubWebSocket *client,
    char *buffer,
    size_t buffer_size
)
{
    DWORD error_code;
    DWORD bytes_read;
    TwitchWebSocketBufferType buffer_type;
    TwitchWebSocketReceiveFunction receive_function;
    size_t total_size = 0;

    if (client == NULL ||
        client->websocket == NULL ||
        client->websocket_receive == NULL ||
        buffer == NULL ||
        buffer_size < 2)
    {
        return BOT_ERR_CONFIG;
    }

    buffer[0] = '\0';

    receive_function =
        (TwitchWebSocketReceiveFunction)
            client->websocket_receive;

    for (;;)
    {
        if (total_size >= buffer_size - 1)
        {
            log_error(
                "Twitch EventSub message is too large"
            );

            return BOT_ERR_TWITCH;
        }

        bytes_read = 0;

        error_code = receive_function(
            client->websocket,
            buffer + total_size,
            (DWORD)(buffer_size - total_size - 1),
            &bytes_read,
            &buffer_type
        );

        if (error_code != ERROR_SUCCESS)
        {
            log_ws_error(
                "WinHttpWebSocketReceive",
                error_code
            );

            return BOT_ERR_NETWORK;
        }

        total_size += (size_t)bytes_read;
        buffer[total_size] = '\0';

        if (buffer_type == TWITCH_WS_UTF8_MESSAGE)
        {
            return BOT_OK;
        }

        if (buffer_type == TWITCH_WS_UTF8_FRAGMENT)
        {
            continue;
        }

        if (buffer_type == TWITCH_WS_CLOSE)
        {
            log_warning(
                "Twitch EventSub WebSocket closed"
            );

            return BOT_ERR_NETWORK;
        }

        log_warning(
            "Unexpected Twitch WebSocket buffer type: %d",
            (int)buffer_type
        );

        return BOT_ERR_TWITCH;
    }
}

typedef enum
{
    SUB_CHAT,
    SUB_FOLLOW,
    SUB_RAID
} EventSubTopic;

static BotResult twitch_eventsub_subscribe_topic(
    const TwitchConfig *config,
    const char *session_id,
    EventSubTopic topic
)
{
    HttpResponse response = {0};
    BotResult result;
    char body[2048];
    char condition[600];
    const char *type, *version;
    int written;
    wchar_t wide_token[2048];
    wchar_t wide_client_id[512];
    wchar_t headers[4096];

    if (!config || !session_id || !session_id[0] ||
        !config->broadcaster_id[0] || !config->bot_user_id[0] ||
        !config->access_token[0] || !config->client_id[0])
        return BOT_ERR_CONFIG;

    if (topic == SUB_CHAT)
    {
        type = "channel.chat.message";
        version = "1";
        written = snprintf(condition, sizeof(condition),
                           "\"broadcaster_user_id\":\"%s\",\"user_id\":\"%s\"",
                           config->broadcaster_id, config->bot_user_id);
    }
    else if (topic == SUB_FOLLOW)
    {
        type = "channel.follow";
        version = "2";
        written = snprintf(condition, sizeof(condition),
                           "\"broadcaster_user_id\":\"%s\",\"moderator_user_id\":\"%s\"",
                           config->broadcaster_id, config->bot_user_id);
    }
    else
    {
        type = "channel.raid";
        version = "1";
        written = snprintf(condition, sizeof(condition),
                           "\"to_broadcaster_user_id\":\"%s\"",
                           config->broadcaster_id);
    }

    if (written < 0 || (size_t)written >= sizeof(condition))
        return BOT_ERR_JSON;
    written = snprintf(
        body, sizeof(body),
        "{\"type\":\"%s\",\"version\":\"%s\","
        "\"condition\":{%s},"
        "\"transport\":{\"method\":\"websocket\",\"session_id\":\"%s\"}}",
        type, version, condition, session_id
    );
    if (written < 0 || (size_t)written >= sizeof(body))
        return BOT_ERR_JSON;

    if (!MultiByteToWideChar(CP_UTF8, 0, config->access_token, -1,
                             wide_token, (int)(sizeof(wide_token) / sizeof(wide_token[0]))) ||
        !MultiByteToWideChar(CP_UTF8, 0, config->client_id, -1,
                             wide_client_id, (int)(sizeof(wide_client_id) / sizeof(wide_client_id[0]))))
        return BOT_ERR_AUTH;

    swprintf(headers, sizeof(headers) / sizeof(headers[0]),
             L"Authorization: Bearer %ls\r\n"
             L"Client-Id: %ls\r\n"
             L"Content-Type: application/json\r\n",
             wide_token, wide_client_id);

    log_info("Creating Twitch %s EventSub subscription...", type);
    result = http_post(TWITCH_API_HOST, TWITCH_EVENTSUB_SUBSCRIPTIONS_PATH,
                       headers, body, &response);
    if (result != BOT_OK)
    {
        http_response_free(&response);
        return result;
    }
    if (response.status_code != 202)
    {
        unsigned long status_code = response.status_code;
        log_warning("Twitch %s subscription failed: HTTP %lu",
                    type, status_code);
        if (response.body && response.body[0])
            log_warning("Twitch EventSub response: %s", response.body);
        if (topic == SUB_FOLLOW && (status_code == 401 || status_code == 403))
            log_warning("Follow events need moderator:read:followers; reauthorize the moderator bot account.");
        http_response_free(&response);
        return (status_code == 401 || status_code == 403)
            ? BOT_ERR_AUTH : BOT_ERR_TWITCH;
    }
    log_info("Twitch %s EventSub subscription created", type);
    http_response_free(&response);
    return BOT_OK;
}

BotResult twitch_eventsub_subscribe_chat(
    const TwitchConfig *config,
    const char *session_id
)
{
    return twitch_eventsub_subscribe_topic(config, session_id, SUB_CHAT);
}

BotResult twitch_eventsub_subscribe_follow(
    const TwitchConfig *config,
    const char *session_id
)
{
    return twitch_eventsub_subscribe_topic(config, session_id, SUB_FOLLOW);
}

BotResult twitch_eventsub_subscribe_raid(
    const TwitchConfig *config,
    const char *session_id
)
{
    return twitch_eventsub_subscribe_topic(config, session_id, SUB_RAID);
}

void twitch_eventsub_ws_close(
    TwitchEventSubWebSocket *client
)
{
    if (client == NULL)
    {
        return;
    }

    if (client->websocket != NULL)
    {
        WinHttpCloseHandle(client->websocket);
        client->websocket = NULL;
    }

    if (client->request != NULL)
    {
        WinHttpCloseHandle(client->request);
        client->request = NULL;
    }

    if (client->connection != NULL)
    {
        WinHttpCloseHandle(client->connection);
        client->connection = NULL;
    }

    if (client->session != NULL)
    {
        WinHttpCloseHandle(client->session);
        client->session = NULL;
    }

    if (client->winhttp_module != NULL)
    {
        FreeLibrary(client->winhttp_module);
        client->winhttp_module = NULL;
    }

    client->websocket_complete_upgrade = NULL;
    client->websocket_receive = NULL;
}
