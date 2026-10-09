#include "app.h"
#include "config.h"
#include "obs_websocket.h"
#include "bot_result.h"
#include "music_audio.h"
#include <windows.h>

#include <stdio.h>
#include <string.h>

/* Local OBS diagnostic, independent of Twitch and Internet. */
int main(int argc, char *argv[])
{
    if (argc == 3 && strcmp(argv[1], "--test-obs-music") == 0)
    {
        AppConfig config;
        ObsWebSocket client = {0};
        BotResult result;
        char path[MAX_PATH];
        char absolute[MAX_PATH];
        DWORD attributes;
        if (config_load("config.ini", &config) != BOT_OK)
        {
            fprintf(stderr, "Cannot read config.ini\n");
            return 1;
        }
        if (!CreateDirectoryA("music_cache", NULL))
        {
            attributes = GetFileAttributesA("music_cache");
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY))
                return 1;
        }
        if (!music_audio_prepare(argv[2], "music_cache", path, sizeof(path)) ||
            !GetFullPathNameA(path, sizeof(absolute), absolute, NULL))
        {
            fprintf(stderr, "Audio preparation failed.\n");
            return 1;
        }
        result = obs_websocket_connect(&client, config.obs.password);
        if (result == BOT_OK)
            result = obs_websocket_set_media_file(&client, "Bot_Music", absolute);
        if (result == BOT_OK)
            result = obs_websocket_restart_media(&client, "Bot_Music");
        obs_websocket_close(&client);
        if (result != BOT_OK)
        {
            fprintf(stderr, "OBS music playback failed (%d).\n", (int)result);
            return 1;
        }
        printf("OBS Bot_Music playing: %s\n", absolute);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--test-music") == 0)
    {
        char path[MAX_PATH];
        DWORD attributes;
        if (!CreateDirectoryA("music_cache", NULL))
        {
            attributes = GetFileAttributesA("music_cache");
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                fprintf(stderr, "Cannot create music_cache directory.\n");
                return 1;
            }
        }
        printf("Preparing audio for video ID: %s\n", argv[2]);
        if (!music_audio_prepare(argv[2], "music_cache",
                                 path, sizeof(path)))
        {
            fprintf(stderr, "Audio preparation failed. Check yt-dlp and FFmpeg output.\n");
            return 1;
        }
        printf("Audio ready: %s\n", path);
        return 0;
    }
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
