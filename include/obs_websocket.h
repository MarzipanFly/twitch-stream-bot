#ifndef OBS_WEBSOCKET_H
#define OBS_WEBSOCKET_H

#include "bot_result.h"
#include <windows.h>
#include <winhttp.h>

typedef struct
{
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;
    HINTERNET websocket;
    HMODULE winhttp_module;
    FARPROC websocket_complete_upgrade;
    FARPROC websocket_receive;
    FARPROC websocket_send;
    FARPROC websocket_close;
    int authenticated;
} ObsWebSocket;

/* Returns BOT_OK only after OBS WebSocket v5 Identify/Identified. */
BotResult obs_websocket_connect(ObsWebSocket *client, const char *password);
void obs_websocket_close(ObsWebSocket *client);

#endif
