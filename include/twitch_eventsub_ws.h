#ifndef TWITCH_EVENTSUB_WS_H
#define TWITCH_EVENTSUB_WS_H

#include "bot_result.h"
#include "config.h"

#include <stddef.h>
#include <windows.h>
#include <winhttp.h>

typedef struct
{
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;
    HINTERNET websocket;

    /*
     * Старый MinGW 7.3 из Qt 5.12 не содержит объявлений
     * WinHTTP WebSocket API. Поэтому адреса нужных функций
     * получаем во время выполнения из системной winhttp.dll.
     */
    HMODULE winhttp_module;
    FARPROC websocket_complete_upgrade;
    FARPROC websocket_receive;

} TwitchEventSubWebSocket;

BotResult twitch_eventsub_ws_connect(
    TwitchEventSubWebSocket *client
);

BotResult twitch_eventsub_ws_receive(
    TwitchEventSubWebSocket *client,
    char *buffer,
    size_t buffer_size
);

BotResult twitch_eventsub_subscribe_chat(
    const TwitchConfig *config,
    const char *session_id
);

void twitch_eventsub_ws_close(
    TwitchEventSubWebSocket *client
);

#endif
