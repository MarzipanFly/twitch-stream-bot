#include "obs_director.h"
#include "game_detector.h"

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
        !config->obs_director.chat_scene[0])
    {
        log_warning("OBS director requires game_scene and chat_scene");
        return BOT_OK;
    }

    director->last_game_running = -1;
    director->active = 1;
    log_info("OBS director enabled: game scene '%s', chat scene '%s', detecting fullscreen Windows applications",
             config->obs_director.game_scene, config->obs_director.chat_scene);
    return BOT_OK;
}

void obs_director_tick(const AppConfig *config, ObsDirector *director)
{
    DWORD now;
    unsigned int poll_seconds;
    char scene[CONFIG_STRING_SIZE] = "";
    char process[CONFIG_STRING_SIZE] = "";
    GameDetectorWindow candidate;
    int detection_result;
    int game_running;

    if (!config || !director || !director->active ||
        !config->obs_director.enabled || !config->obs.enabled) return;
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
    if (!director->state_known)
        log_info("OBS director initial scene: '%s'", scene);
    else if (strcmp(scene, director->last_seen_scene) != 0)
        log_info("OBS director observed current scene: '%s'", scene);

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

    detection_result = game_detector_find_fullscreen(&candidate);
    if (detection_result < 0)
    {
        log_director_error(director, now,
            "could not enumerate fullscreen Windows applications; no scene change made");
        return;
    }

    game_running = detection_result == 1;
    if (game_running)
    {
        snprintf(process, sizeof(process), "%s", candidate.process_name);
        snprintf(director->last_detected_process,
                 sizeof(director->last_detected_process), "%s", process);
    }
    else if (director->last_detected_process[0])
    {
        snprintf(process, sizeof(process), "%s", director->last_detected_process);
    }

    if (director->last_game_running != game_running)
    {
        log_info("OBS director fullscreen application: %s (%s)",
                 process[0] ? process : "none",
                 game_running ? "detected" : "not detected");
        director->last_game_running = game_running;
    }

    /*
     * Delays and state confirmation are tracked separately from scene changes.
     * A fullscreen application must remain present/absent for the configured
     * duration before the director changes scenes.
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
                    log_info("OBS director switched scene to '%s' (fullscreen application %s detected)",
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
                log_info("OBS director switched scene to '%s' (no fullscreen application detected; last was %s)",
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
