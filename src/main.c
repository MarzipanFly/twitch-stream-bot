#include "app.h"
#include "config.h"
#include "obs_websocket.h"
#include "bot_result.h"

#include <stdio.h>
#include <string.h>

/* Local OBS diagnostic, independent of Twitch and Internet. */
int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--test-obs") == 0)
    {
        AppConfig config;
        ObsWebSocket client;
        BotResult result;
        char version[4096];
        if (config_load("config.ini", &config) != BOT_OK)
        {
            fprintf(stderr, "Cannot read config.ini\n");
            return 1;
        }
        result = obs_websocket_connect(&client, config.obs.password);
        if (result != BOT_OK)
        {
            fprintf(stderr, "OBS connection or authentication failed (%d)\n", (int)result);
            return 1;
        }
        result = obs_websocket_get_version(&client, version, sizeof(version));
        if (result == BOT_OK) printf("OBS connected and authorized: %s\n", version);
        else fprintf(stderr, "OBS GetVersion failed (%d)\n", (int)result);
        obs_websocket_close(&client);
        return result == BOT_OK ? 0 : 1;
    }
    return app_run(argc, argv);
}
