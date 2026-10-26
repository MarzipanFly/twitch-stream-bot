#include "obs_director.h"

#include "logger.h"
#include "cJSON.h"

#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define OBS_DIRECTOR_RECONNECT_MS 15000UL
#define OBS_DIRECTOR_ERROR_LOG_MS 30000UL

static int read_current_scene(ObsDirector *director, char *scene, size_t size)
{
    char response[4096];
    cJSON *root;
    const cJSON *name;
    int ok = 0;

    if (obs_websocket_get_scene(&director->obs, response, sizeof(response)) != BOT_OK)
        return 0;
    root = cJSON_Parse(response);
    if (!root) return 0;
    name = cJSON_GetObjectItemCaseSensitive(root, "currentProgramSceneName");
    if (cJSON_IsString(name) && name->valuestring &&
        strlen(name->valuestring) < size)
    {
        snprintf(scene, size, "%s", name->valuestring);
        ok = 1;
    }
    cJSON_Delete(root);
    return ok;
}

static int read_game_window(ObsDirector *director, const AppConfig *config,
                            char *window, size_t window_size)
{
    char response[8192];
    cJSON *root;
    const cJSON *settings, *value;
    int ok = 0;

    if (obs_websocket_get_input_settings(&director->obs,
            config->obs_director.game_input, response, sizeof(response)) != BOT_OK)
        return 0;
    root = cJSON_Parse(response);
    if (!root) return 0;
    settings = cJSON_GetObjectItemCaseSensitive(root, "inputSettings");
    value = cJSON_GetObjectItemCaseSensitive(settings, "window");
    if (cJSON_IsString(value) && value->valuestring &&
        strlen(value->valuestring) < window_size)
    {
        snprintf(window, window_size, "%s", value->valuestring);
        ok = 1;
    }
    cJSON_Delete(root);
    return ok;
}

/*
 * OBS Game Capture usually stores a target as title:window-class:process.exe.
 * Use the final component as the process name. Generic capture targets that
 * don't identify one executable are intentionally treated as unknown.
 */
static int extract_process_name(const char *window, char *process, size_t size)
{
    const char *name, *colon, *dot;
    size_t length;

    if (!window || !window[0] || !process || size == 0) return 0;
    colon = strrchr(window, ':');
    name = colon ? colon + 1 : window;
    while (*name == ' ' || *name == '\t') ++name;
    length = strlen(name);
    while (length > 0 && (name[length - 1] == ' ' || name[length - 1] == '\t'))
        --length;
    if (length < 5 || length >= size) return 0;
    dot = name + length - 4;
    if (dot <= name || tolower((unsigned char)dot[0]) != '.' ||
        tolower((unsigned char)dot[1]) != 'e' ||
        tolower((unsigned char)dot[2]) != 'x' ||
        tolower((unsigned char)dot[3]) != 'e')
        return 0;
    memcpy(process, name, length);
    process[length] = '\0';
    return 1;
}

static int is_process_running(const char *wanted_process)
{
    HANDLE snapshot;
    PROCESSENTRY32 entry;
    int found = -1;

    if (!wanted_process || !wanted_process[0]) return -1;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;

    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry))
    {
        found = 0;
        do
        {
            if (_stricmp(entry.szExeFile, wanted_process) == 0)
            {
                found = 1;
                break;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

static void log_director_error(ObsDirector *director, DWORD now,
                               const char *message)
{
    if (director->last_error_tick == 0 ||
        (DWORD)(now - director->last_error_tick) >= OBS_DIRECTOR_ERROR_LOG_MS)
    {
        log_warning("OBS director: %s", message);
        director->last_error_tick = now;
    }
}

BotResult obs_director_start(const AppConfig *config, ObsDirector *director)
{
    if (!config || !director) return BOT_ERR_CONFIG;
    memset(director, 0, sizeof(*director));

    if (!config->obs_director.enabled)
    {
        log_info("OBS director is disabled in [obs_director]");
        return BOT_OK;
    }
    if (!config->obs.enabled || !config->obs.password[0])
    {
        log_warning("OBS director is enabled but OBS WebSocket is disabled or has no password");
        return BOT_OK;
    }
    if (!config->obs_director.game_scene[0] ||
        !config->obs_director.chat_scene[0] ||
        !config->obs_director.game_input[0])
    {
        log_warning("OBS director requires game_scene, chat_scene and game_input");
        return BOT_OK;
    }

    director->active = 1;
    log_info("OBS director enabled: game scene '%s', chat scene '%s', Game Capture input '%s'",
             config->obs_director.game_scene, config->obs_director.chat_scene,
             config->obs_director.game_input);
    return BOT_OK;
}

void obs_director_tick(const AppConfig *config, ObsDirector *director)
{
    DWORD now;
    unsigned int poll_seconds;
    char scene[CONFIG_STRING_SIZE] = "";
    char window[CONFIG_STRING_SIZE] = "";
    char process[CONFIG_STRING_SIZE] = "";
    int game_running;
    int window_changed;

    if (!config || !director || !director->active) return;
    now = GetTickCount();
    poll_seconds = config->obs_director.poll_seconds ?
                   config->obs_director.poll_seconds : 3;
    if ((DWORD)(now - director->last_poll_tick) < (DWORD)poll_seconds * 1000UL)
        return;
    director->last_poll_tick = now;

    if (!director->obs.authenticated)
    {
        if (director->last_connect_attempt_tick != 0 &&
            (DWORD)(now - director->last_connect_attempt_tick) < OBS_DIRECTOR_RECONNECT_MS)
            return;
        director->last_connect_attempt_tick = now;
        if (obs_websocket_connect(&director->obs, config->obs.password) != BOT_OK)
        {
            log_director_error(director, now, "could not connect to OBS WebSocket; retrying later");
            return;
        }
        log_info("OBS director connected to OBS WebSocket");
    }

    if (!read_current_scene(director, scene, sizeof(scene)))
    {
        log_director_error(director, now, "could not read current scene; reconnecting");
        obs_websocket_close(&director->obs);
        return;
    }

    /* Any scene change not initiated below is treated as manual/external.
     * Director-initiated changes update last_seen_scene immediately. */
    if (director->state_known &&
        strcmp(scene, director->last_seen_scene) != 0)
    {
        unsigned int override_seconds = config->obs_director.manual_override_seconds;
        director->manual_override_until_tick =
            now + (DWORD)override_seconds * 1000UL;
        log_info("OBS director: manual scene change to '%s'; automatic switching paused for %u seconds",
                 scene, override_seconds);
    }
    snprintf(director->last_seen_scene, sizeof(director->last_seen_scene), "%s", scene);
    director->state_known = 1;

    if (director->manual_override_until_tick != 0 &&
        (LONG)(now - director->manual_override_until_tick) < 0)
    {
        director->game_present_since_tick = 0;
        director->game_absent_since_tick = 0;
        return;
    }

    /* Only act from the two managed scenes; never take over break/custom scenes. */
    if (strcmp(scene, config->obs_director.game_scene) != 0 &&
        strcmp(scene, config->obs_director.chat_scene) != 0)
        return;

    if (!read_game_window(director, config, window, sizeof(window)))
    {
        log_director_error(director, now, "could not read the configured Game Capture window; no scene change made");
        return;
    }

    window_changed = strcmp(window, director->last_window_setting) != 0;
    if (window_changed)
    {
        snprintf(director->last_window_setting, sizeof(director->last_window_setting),
                 "%s", window);
        log_debug("OBS director selected game window: '%s'", window);
    }

    if (!extract_process_name(window, process, sizeof(process)))
    {
        log_director_error(director, now,
            "Game Capture target does not identify a specific .exe process; automatic switching skipped");
        return;
    }

    game_running = is_process_running(process);
    if (game_running < 0)
    {
        log_director_error(director, now, "could not enumerate Windows processes; no scene change made");
        return;
    }

    /*
     * Delays and state confirmation are tracked separately from scene changes.
     * A process must remain in the same state for the configured duration
     * before the director changes scenes.
     */
    if (game_running)
    {
        if (director->game_absent_since_tick != 0)
            director->game_absent_since_tick = 0;
        if (strcmp(scene, config->obs_director.chat_scene) == 0 &&
            config->obs_director.auto_game_scene)
        {
            if (director->game_present_since_tick == 0)
                director->game_present_since_tick = now;
            if ((DWORD)(now - director->game_present_since_tick) >=
                (DWORD)config->obs_director.game_start_delay_seconds * 1000UL)
            {
                if (obs_websocket_set_scene(&director->obs,
                                            config->obs_director.game_scene) == BOT_OK)
                {
                    snprintf(director->last_director_scene,
                             sizeof(director->last_director_scene), "%s",
                             config->obs_director.game_scene);
                    snprintf(director->last_seen_scene,
                             sizeof(director->last_seen_scene), "%s",
                             config->obs_director.game_scene);
                    director->manual_override_until_tick = 0;
                    director->game_present_since_tick = 0;
                    log_info("OBS director switched scene to '%s' (process %s detected)",
                             config->obs_director.game_scene, process);
                }
                else
                    log_director_error(director, now, "failed to switch to the game scene");
            }
        }
        else
            director->game_present_since_tick = 0;
    }
    else
    {
        director->game_present_since_tick = 0;
        if (director->game_absent_since_tick == 0)
            director->game_absent_since_tick = now;
        if (strcmp(scene, config->obs_director.game_scene) == 0 &&
            config->obs_director.auto_chat_scene &&
            (DWORD)(now - director->game_absent_since_tick) >=
            (DWORD)config->obs_director.game_stop_delay_seconds * 1000UL)
        {
            if (obs_websocket_set_scene(&director->obs,
                                        config->obs_director.chat_scene) == BOT_OK)
            {
                snprintf(director->last_director_scene,
                         sizeof(director->last_director_scene), "%s",
                         config->obs_director.chat_scene);
                snprintf(director->last_seen_scene,
                         sizeof(director->last_seen_scene), "%s",
                         config->obs_director.chat_scene);
                director->game_absent_since_tick = 0;
                log_info("OBS director switched scene to '%s' (process %s is no longer running)",
                         config->obs_director.chat_scene, process);
            }
            else
                log_director_error(director, now, "failed to switch to the chat scene");
        }
    }
}

void obs_director_stop(ObsDirector *director)
{
    if (!director) return;
    obs_websocket_close(&director->obs);
    director->active = 0;
}
