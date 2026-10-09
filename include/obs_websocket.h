#ifndef OBS_WEBSOCKET_H
#define OBS_WEBSOCKET_H

#include "bot_result.h"
#include <windows.h>
#include <winhttp.h>
#include <stddef.h>

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
/* Generic OBS v5 request; checks requestStatus.result and returns responseData. */
BotResult obs_websocket_request(ObsWebSocket *client, const char *request_type,
                                const char *request_data_json,
                                char *response, size_t response_size);
BotResult obs_websocket_get_version(ObsWebSocket *client, char *response, size_t size);
BotResult obs_websocket_get_scene(ObsWebSocket *client, char *response, size_t size);
BotResult obs_websocket_set_scene(ObsWebSocket *client, const char *scene_name);
BotResult obs_websocket_restart_media(ObsWebSocket *client, const char *input_name);
BotResult obs_websocket_set_input_mute(ObsWebSocket *client, const char *input_name, int muted);
void obs_websocket_close(ObsWebSocket *client);

#endif
