#include "category_sync.h"
#include "game_detector.h"

#include "logger.h"
#include "twitch_api.h"
#include "twitch_auth.h"
#include "twitch_refresh.h"
#include "token_store.h"
#include "cJSON.h"

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define CATEGORY_SYNC_SCOPE "channel:manage:broadcast"
#define CATEGORY_SYNC_OBS_RETRY_MS 15000UL
#define CATEGORY_SYNC_LOOKUP_RETRY_MS 60000UL
#define CATEGORY_SYNC_UPDATE_RETRY_MS 10000UL

static int text_case_equal(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right)
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int token_has_required_scope(const TwitchTokenValidation *validation)
{
    size_t i;
    if (!validation) return 0;
    for (i = 0; i < validation->scope_count; ++i)
        if (strcmp(validation->scopes[i], CATEGORY_SYNC_SCOPE) == 0)
            return 1;
    return 0;
}

static BotResult category_sync_apply_token(
    CategorySync *sync,
    const AppConfig *config,
    const TwitchAuthToken *token,
    int persist
)
{
    TwitchTokenValidation validation = {0};
    BotResult result;

    if (!sync || !config || !token || token->access_token[0] == '\0')
        return BOT_ERR_AUTH;

    result = twitch_auth_validate_token(token->access_token, &validation);
    if (result != BOT_OK) return result;

    if (strcmp(validation.client_id, config->twitch.client_id) != 0)
    {
        log_error("Category-sync OAuth token belongs to a different Twitch Client ID");
        return BOT_ERR_AUTH;
    }

    if (!text_case_equal(validation.login, config->twitch.broadcaster_login))
    {
        log_error("Category-sync token belongs to '%s', but [twitch] broadcaster_login is '%s'",
                  validation.login, config->twitch.broadcaster_login);
        return BOT_ERR_AUTH;
    }

    if (!token_has_required_scope(&validation))
    {
        log_warning("Stored category-sync token lacks %s; a new authorization is required",
                    CATEGORY_SYNC_SCOPE);
        return BOT_ERR_AUTH;
    }

    if (persist)
    {
        result = token_store_save_broadcast(token);
        if (result != BOT_OK) return result;
    }

    snprintf(sync->broadcaster_twitch.access_token,
             sizeof(sync->broadcaster_twitch.access_token), "%s", token->access_token);
    snprintf(sync->broadcaster_twitch.refresh_token,
             sizeof(sync->broadcaster_twitch.refresh_token), "%s", token->refresh_token);
    snprintf(sync->broadcaster_twitch.broadcaster_login,
             sizeof(sync->broadcaster_twitch.broadcaster_login), "%s", validation.login);
    snprintf(sync->broadcaster_twitch.broadcaster_id,
             sizeof(sync->broadcaster_twitch.broadcaster_id), "%s", validation.user_id);

    return BOT_OK;
}

static BotResult category_sync_authorize(
    CategorySync *sync,
    const AppConfig *config
)
{
    TwitchDeviceCode device = {0};
    BotResult result;

    log_info("Category sync needs a separate authorization for channel:manage:broadcast");
    log_info("Authorize the Twitch channel owner account: %s", config->twitch.broadcaster_login);

    result = twitch_auth_request_device_code_with_scopes(
        &config->twitch, CATEGORY_SYNC_SCOPE, &device);
    if (result != BOT_OK) return result;

    log_info("Category-sync authorization URL: %s", device.verification_uri);
    log_info("Category-sync authorization code: %s", device.user_code);
    log_info("Approve this code using the Twitch account configured as broadcaster_login.");

    sync->pending_device = device;
    sync->auth_pending = 1;
    sync->auth_poll_interval_seconds = device.interval > 0 ? device.interval : 5;
    sync->auth_started_tick = GetTickCount();
    sync->auth_next_poll_tick = sync->auth_started_tick +
        (DWORD)sync->auth_poll_interval_seconds * 1000UL;
    return BOT_AUTH_PENDING;
}

static BotResult category_sync_poll_authorization(
    CategorySync *sync,
    const AppConfig *config,
    DWORD now
)
{
    TwitchAuthToken token = {0};
    BotResult result;
    DWORD elapsed_ms;

    if (!sync || !config || !sync->auth_pending)
        return BOT_ERR_CONFIG;

    elapsed_ms = (DWORD)(now - sync->auth_started_tick);
    if (sync->pending_device.expires_in <= 0 ||
        elapsed_ms >= (DWORD)sync->pending_device.expires_in * 1000UL)
    {
        log_warning("Category-sync authorization code expired; restart the bot to request a new code");
        SecureZeroMemory(&sync->pending_device, sizeof(sync->pending_device));
        sync->auth_pending = 0;
        sync->active = 0;
        return BOT_ERR_AUTH;
    }

    if ((LONG)(now - sync->auth_next_poll_tick) < 0)
        return BOT_AUTH_PENDING;

    sync->auth_next_poll_tick = now +
        (DWORD)sync->auth_poll_interval_seconds * 1000UL;
    result = twitch_auth_poll_token_with_scopes(
        &config->twitch, &sync->pending_device,
        CATEGORY_SYNC_SCOPE, &token);

    if (result == BOT_AUTH_PENDING)
    {
        SecureZeroMemory(&token, sizeof(token));
        return BOT_AUTH_PENDING;
    }

    if (result == BOT_OK)
        result = category_sync_apply_token(sync, config, &token, 1);

    SecureZeroMemory(&token, sizeof(token));
    SecureZeroMemory(&sync->pending_device, sizeof(sync->pending_device));
    sync->auth_pending = 0;

    if (result == BOT_OK)
    {
        sync->active = 1;
        sync->last_poll_tick = now -
            (DWORD)(config->category_sync.poll_seconds ?
                    config->category_sync.poll_seconds : 5) * 1000UL;
        log_info("Category-sync broadcaster authorization saved securely");
        return BOT_OK;
    }

    sync->active = 0;
    log_warning("Category-sync authorization failed: %s",
                bot_result_to_string(result));
    return result;
}

static BotResult category_sync_refresh_token(
    CategorySync *sync,
    const AppConfig *config
)
{
    TwitchAuthToken refreshed = {0};
    BotResult result;

    if (!sync || !config || sync->broadcaster_twitch.refresh_token[0] == '\0')
        return BOT_ERR_AUTH;

    result = twitch_refresh_access_token(
        &config->twitch, sync->broadcaster_twitch.refresh_token, &refreshed);
    if (result == BOT_OK)
        result = category_sync_apply_token(sync, config, &refreshed, 1);

    SecureZeroMemory(&refreshed, sizeof(refreshed));
    return result;
}

static BotResult category_sync_prepare_auth(
    CategorySync *sync,
    const AppConfig *config
)
{
    TwitchAuthToken stored = {0};
    TwitchAuthToken refreshed = {0};
    BotResult result;

    result = token_store_load_broadcast(&stored);
    if (result == BOT_OK)
    {
        result = category_sync_apply_token(sync, config, &stored, 0);
        if (result == BOT_OK)
        {
            SecureZeroMemory(&stored, sizeof(stored));
            log_info("Stored broadcaster category-sync authorization is valid");
            return BOT_OK;
        }

        if (result != BOT_ERR_AUTH)
        {
            SecureZeroMemory(&stored, sizeof(stored));
            return result;
        }

        if (stored.refresh_token[0] != '\0')
        {
            result = twitch_refresh_access_token(
                &config->twitch, stored.refresh_token, &refreshed);
            if (result == BOT_OK)
            {
                result = category_sync_apply_token(sync, config, &refreshed, 1);
                if (result == BOT_OK)
                {
                    SecureZeroMemory(&stored, sizeof(stored));
                    SecureZeroMemory(&refreshed, sizeof(refreshed));
                    log_info("Broadcaster category-sync authorization refreshed");
                    return BOT_OK;
                }
                if (result != BOT_ERR_AUTH)
                {
                    SecureZeroMemory(&stored, sizeof(stored));
                    SecureZeroMemory(&refreshed, sizeof(refreshed));
                    return result;
                }
            }
            else if (result != BOT_ERR_AUTH)
            {
                SecureZeroMemory(&stored, sizeof(stored));
                SecureZeroMemory(&refreshed, sizeof(refreshed));
                return result;
            }
        }

        SecureZeroMemory(&stored, sizeof(stored));
        SecureZeroMemory(&refreshed, sizeof(refreshed));
        (void)token_store_delete_broadcast();
    }
    else if (result != BOT_ERR_FILE)
    {
        (void)token_store_delete_broadcast();
    }

    return category_sync_authorize(sync, config);
}

BotResult category_sync_start(const AppConfig *config, CategorySync *sync)
{
    BotResult result;

    if (!config || !sync) return BOT_ERR_CONFIG;
    memset(sync, 0, sizeof(*sync));

    if (!config->category_sync.enabled)
    {
        log_info("Automatic Twitch category sync is disabled in [category_sync]");
        return BOT_OK;
    }

    if (!config->obs.enabled || config->obs.password[0] == '\0')
    {
        log_warning("Category sync is enabled but OBS WebSocket is disabled or has no password");
        return BOT_OK;
    }

    if (config->twitch.client_id[0] == '\0' ||
        config->twitch.broadcaster_login[0] == '\0')
    {
        log_warning("Category sync requires twitch.client_id and twitch.broadcaster_login");
        return BOT_OK;
    }

    sync->broadcaster_twitch = config->twitch;
    sync->broadcaster_twitch.access_token[0] = '\0';
    sync->broadcaster_twitch.refresh_token[0] = '\0';

    result = category_sync_prepare_auth(sync, config);
    if (result == BOT_AUTH_PENDING && sync->auth_pending)
    {
        sync->active = 1;
        log_info("Category sync is waiting for broadcaster OAuth approval; other bot features will continue running");
        return BOT_OK;
    }
    if (result != BOT_OK)
    {
        log_warning("Automatic category sync could not authorize and will remain disabled: %s",
                    bot_result_to_string(result));
        return BOT_OK;
    }

    sync->active = 1;
    sync->last_poll_tick = (DWORD)(GetTickCount() -
        (config->category_sync.poll_seconds ? config->category_sync.poll_seconds : 5) * 1000UL);

    log_info("Automatic Twitch category sync enabled: scene '%s' -> '%s', scene '%s' -> foreground game category",
             config->category_sync.chat_scene, config->category_sync.chat_category,
             config->category_sync.game_scene);
    return BOT_OK;
}

static void trim_whitespace(char *text)
{
    size_t length;
    char *start;
    if (!text) return;

    start = text;
    while (*start && isspace((unsigned char)*start)) ++start;
    if (start != text) memmove(text, start, strlen(start) + 1);

    length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1]))
        text[--length] = '\0';
}

static int contains_case_insensitive(const char *text, const char *needle)
{
    size_t i, j;
    if (!text || !needle || !needle[0]) return 0;
    for (i = 0; text[i]; ++i)
    {
        for (j = 0; needle[j] && text[i + j] &&
             tolower((unsigned char)text[i + j]) ==
             tolower((unsigned char)needle[j]); ++j)
        {
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

static void strip_technical_window_suffix(char *title)
{
    char *opening, *closing;
    int technical;

    if (!title || !title[0]) return;
    opening = strrchr(title, '(');
    if (!opening || opening == title) return;
    closing = strrchr(opening, ')');
    if (!closing || closing[1] != '\0') return;

    technical =
        contains_case_insensitive(opening, "directx") ||
        contains_case_insensitive(opening, "vulkan") ||
        contains_case_insensitive(opening, "opengl") ||
        contains_case_insensitive(opening, "64bit") ||
        contains_case_insensitive(opening, "32bit") ||
        contains_case_insensitive(opening, "64-bit") ||
        contains_case_insensitive(opening, "32-bit") ||
        contains_case_insensitive(opening, "dx11") ||
        contains_case_insensitive(opening, "dx12");
    if (!technical) return;

    while (opening > title &&
           (opening[-1] == ' ' || opening[-1] == '\t'))
        --opening;
    *opening = '\0';
    trim_whitespace(title);
}

static int contains_exe_suffix(const char *text)
{
    size_t i, length;
    if (!text) return 0;
    length = strlen(text);
    if (length < 4) return 0;
    for (i = 0; i + 4 <= length; ++i)
        if (tolower((unsigned char)text[i]) == '.' &&
            tolower((unsigned char)text[i + 1]) == 'e' &&
            tolower((unsigned char)text[i + 2]) == 'x' &&
            tolower((unsigned char)text[i + 3]) == 'e')
            return 1;
    return 0;
}

static void extract_game_query(const char *window_value, char *output, size_t output_size)
{
    char title[CONFIG_STRING_SIZE];
    char *last_colon;
    char *previous_colon;
    size_t length;

    if (!output || output_size == 0) return;
    output[0] = '\0';
    if (!window_value || !window_value[0]) return;

    snprintf(title, sizeof(title), "%s", window_value);
    trim_whitespace(title);

    /* OBS stores a selected Windows target as title:class:process.exe. */
    if (contains_exe_suffix(title))
    {
        last_colon = strrchr(title, ':');
        if (last_colon)
        {
            *last_colon = '\0';
            previous_colon = strrchr(title, ':');
            if (previous_colon) *previous_colon = '\0';
        }
    }

    trim_whitespace(title);
    length = strlen(title);
    if (length >= 4 && text_case_equal(title + length - 4, ".exe"))
        title[length - 4] = '\0';
    trim_whitespace(title);

    /* Common executable-only targets whose names are not Twitch category names. */
    if (text_case_equal(title, "cs2")) snprintf(title, sizeof(title), "%s", "Counter-Strike 2");
    else if (text_case_equal(title, "valorant-win64-shipping")) snprintf(title, sizeof(title), "%s", "VALORANT");
    else if (text_case_equal(title, "javaw")) snprintf(title, sizeof(title), "%s", "Minecraft");
    else if (text_case_equal(title, "r5apex")) snprintf(title, sizeof(title), "%s", "Apex Legends");
    else if (text_case_equal(title, "dota2")) snprintf(title, sizeof(title), "%s", "Dota 2");
    else if (text_case_equal(title, "leagueclient")) snprintf(title, sizeof(title), "%s", "League of Legends");
    else if (text_case_equal(title, "overwatch")) snprintf(title, sizeof(title), "%s", "Overwatch 2");
    else if (text_case_equal(title, "fortniteclient-win64-shipping")) snprintf(title, sizeof(title), "%s", "Fortnite");

    snprintf(output, output_size, "%s", title);
}

static BotResult category_sync_read_scene(CategorySync *sync, char *scene, size_t scene_size)
{
    char response[4096];
    cJSON *root;
    cJSON *name;
    BotResult result;

    result = obs_websocket_get_scene(&sync->obs, response, sizeof(response));
    if (result != BOT_OK) return result;

    root = cJSON_Parse(response);
    if (!root) return BOT_ERR_JSON;
    name = cJSON_GetObjectItemCaseSensitive(root, "currentProgramSceneName");
    if (!cJSON_IsString(name) || !name->valuestring)
    {
        cJSON_Delete(root);
        return BOT_ERR_JSON;
    }

    snprintf(scene, scene_size, "%s", name->valuestring);
    cJSON_Delete(root);
    return BOT_OK;
}

/*
 * Use the shared fullscreen-window detector so category sync and scene
 * switching agree on which application is currently being played.
 */
static BotResult category_sync_read_fullscreen_game(
    char *query, size_t query_size,
    char *process_query, size_t process_query_size,
    char *process_name, size_t process_name_size)
{
    GameDetectorWindow candidate;
    char executable_query[CONFIG_STRING_SIZE];
    size_t length;
    int detected;

    if (!query || query_size == 0 || !process_query || process_query_size == 0 ||
        !process_name || process_name_size == 0)
        return BOT_ERR_CONFIG;

    query[0] = '\0';
    process_query[0] = '\0';
    process_name[0] = '\0';
    executable_query[0] = '\0';

    detected = game_detector_find_fullscreen(&candidate);
    if (detected <= 0)
        return BOT_ERR_TWITCH;

    snprintf(query, query_size, "%s", candidate.title);
    strip_technical_window_suffix(query);
    snprintf(process_name, process_name_size, "%s", candidate.process_name);
    snprintf(executable_query, sizeof(executable_query), "%s", candidate.process_name);

    length = strlen(executable_query);
    if (length > 4 && text_case_equal(executable_query + length - 4, ".exe"))
        executable_query[length - 4] = '\0';
    extract_game_query(executable_query, process_query, process_query_size);

    return query[0] ? BOT_OK : BOT_ERR_TWITCH;
}

static BotResult category_sync_search(
    CategorySync *sync,
    const AppConfig *config,
    const char *query,
    char *category_id,
    size_t category_id_size,
    char *category_name,
    size_t category_name_size
)
{
    BotResult result = twitch_search_category(
        &sync->broadcaster_twitch, query, category_id, category_id_size,
        category_name, category_name_size);

    if (result != BOT_ERR_AUTH) return result;

    result = category_sync_refresh_token(sync, config);
    if (result != BOT_OK)
    {
        log_error("Category sync token refresh failed; automatic switching is paused");
        sync->active = 0;
        return result;
    }

    result = twitch_search_category(
        &sync->broadcaster_twitch, query, category_id, category_id_size,
        category_name, category_name_size);
    if (result == BOT_ERR_AUTH)
    {
        log_error("Twitch rejected category-sync authorization; automatic switching is paused");
        sync->active = 0;
    }
    return result;
}

static BotResult category_sync_apply_category(
    CategorySync *sync,
    const AppConfig *config,
    const char *category_id,
    const char *category_name
)
{
    DWORD now = GetTickCount();
    BotResult result;

    if (!category_id || !category_id[0]) return BOT_ERR_CONFIG;
    if (strcmp(sync->last_category_id, category_id) == 0) return BOT_OK;
    if (sync->last_category_update_tick != 0 &&
        (DWORD)(now - sync->last_category_update_tick) < CATEGORY_SYNC_UPDATE_RETRY_MS)
        return BOT_AUTH_PENDING;

    sync->last_category_update_tick = now;
    result = twitch_update_channel_category(
        &sync->broadcaster_twitch,
        sync->broadcaster_twitch.broadcaster_id,
        category_id);

    if (result == BOT_ERR_AUTH)
    {
        result = category_sync_refresh_token(sync, config);
        if (result == BOT_OK)
            result = twitch_update_channel_category(
                &sync->broadcaster_twitch,
                sync->broadcaster_twitch.broadcaster_id,
                category_id);
        if (result == BOT_ERR_AUTH)
        {
            log_error("Twitch rejected category update. Check that channel:manage:broadcast is authorized; category sync is paused");
            sync->active = 0;
        }
    }

    if (result == BOT_OK)
    {
        snprintf(sync->last_category_id, sizeof(sync->last_category_id), "%s", category_id);
        log_info("Twitch category changed to '%s' (id=%s)",
                 category_name ? category_name : category_id, category_id);
    }
    else if (result != BOT_AUTH_PENDING)
    {
        log_warning("Could not update Twitch category to '%s': %s",
                    category_name ? category_name : category_id,
                    bot_result_to_string(result));
    }

    return result;
}

void category_sync_tick(const AppConfig *config, CategorySync *sync)
{
    DWORD now;
    unsigned int poll_seconds;
    char scene[CONFIG_STRING_SIZE];
    char query[CONFIG_STRING_SIZE];
    char process_query[CONFIG_STRING_SIZE];
    char process_name[CONFIG_STRING_SIZE];
    char category_id[64];
    char category_name[CONFIG_STRING_SIZE];
    BotResult result;

    if (!config || !sync || !sync->active ||
        !config->category_sync.enabled || !config->obs.enabled) return;

    now = GetTickCount();

    /* Poll OAuth incrementally so the main bot loop never waits for approval. */
    if (sync->auth_pending)
    {
        (void)category_sync_poll_authorization(sync, config, now);
        if (!sync->active || sync->auth_pending) return;
    }

    poll_seconds = config->category_sync.poll_seconds ? config->category_sync.poll_seconds : 5;
    if ((DWORD)(now - sync->last_poll_tick) < (DWORD)poll_seconds * 1000UL) return;
    sync->last_poll_tick = now;

    if (!sync->obs.authenticated)
    {
        if (sync->last_obs_connect_attempt_tick != 0 &&
            (DWORD)(now - sync->last_obs_connect_attempt_tick) < CATEGORY_SYNC_OBS_RETRY_MS)
            return;
        sync->last_obs_connect_attempt_tick = now;
        result = obs_websocket_connect(&sync->obs, config->obs.password);
        if (result != BOT_OK) return;
        log_info("Category sync connected to OBS WebSocket");
    }

    result = category_sync_read_scene(sync, scene, sizeof(scene));
    if (result != BOT_OK)
    {
        log_warning("Category sync lost its OBS connection; will reconnect");
        obs_websocket_close(&sync->obs);
        return;
    }

    if (strcmp(scene, config->category_sync.chat_scene) == 0)
    {
        if (!sync->chat_category_id[0])
        {
            if (sync->last_chat_lookup_tick != 0 &&
                (DWORD)(now - sync->last_chat_lookup_tick) < CATEGORY_SYNC_LOOKUP_RETRY_MS)
                return;
            sync->last_chat_lookup_tick = now;
            result = category_sync_search(
                sync, config, config->category_sync.chat_category,
                category_id, sizeof(category_id), category_name, sizeof(category_name));
            if (result != BOT_OK) return;
            snprintf(sync->chat_category_id, sizeof(sync->chat_category_id), "%s", category_id);
        }
        (void)category_sync_apply_category(
            sync, config, sync->chat_category_id, config->category_sync.chat_category);
        return;
    }

    if (strcmp(scene, config->category_sync.game_scene) != 0)
        return;

    result = category_sync_read_fullscreen_game(
        query, sizeof(query), process_query, sizeof(process_query),
        process_name, sizeof(process_name));
    if (result != BOT_OK)
    {
        if (sync->last_source_error_tick == 0 ||
            (DWORD)(now - sync->last_source_error_tick) >= 30000UL)
        {
            log_warning("Category sync could not identify a fullscreen game window; keeping the current Twitch category");
            sync->last_source_error_tick = now;
        }
        return;
    }

    if (strcmp(query, sync->last_game_query) == 0 &&
        strcmp(process_query, sync->last_game_process) == 0 &&
        sync->last_game_category_id[0])
    {
        (void)category_sync_apply_category(
            sync, config, sync->last_game_category_id,
            sync->last_game_category_name[0] ? sync->last_game_category_name : query);
        return;
    }

    if (strcmp(query, sync->last_game_query) == 0 &&
        strcmp(process_query, sync->last_game_process) == 0 &&
        !sync->last_game_category_id[0] &&
        sync->last_game_lookup_tick != 0 &&
        (DWORD)(now - sync->last_game_lookup_tick) < CATEGORY_SYNC_LOOKUP_RETRY_MS)
        return;

    snprintf(sync->last_game_query, sizeof(sync->last_game_query), "%s", query);
    snprintf(sync->last_game_process, sizeof(sync->last_game_process), "%s", process_query);
    sync->last_game_category_id[0] = '\0';
    sync->last_game_category_name[0] = '\0';
    sync->last_game_lookup_tick = now;

    log_info("Category sync fullscreen app: title='%s', process='%s'",
             query, process_name[0] ? process_name : "unknown");
    result = category_sync_search(
        sync, config, query, category_id, sizeof(category_id),
        category_name, sizeof(category_name));

    if (result == BOT_ERR_TWITCH && process_query[0] &&
        !text_case_equal(process_query, query))
    {
        log_info("Category sync title did not match a Twitch category; retrying with process name '%s'",
                 process_query);
        result = category_sync_search(
            sync, config, process_query, category_id, sizeof(category_id),
            category_name, sizeof(category_name));
    }
    if (result != BOT_OK) return;

    snprintf(sync->last_game_category_id, sizeof(sync->last_game_category_id), "%s", category_id);
    snprintf(sync->last_game_category_name, sizeof(sync->last_game_category_name), "%s", category_name);
    (void)category_sync_apply_category(sync, config, category_id, category_name);
}

void category_sync_stop(CategorySync *sync)
{
    if (!sync) return;
    obs_websocket_close(&sync->obs);
    SecureZeroMemory(&sync->broadcaster_twitch, sizeof(sync->broadcaster_twitch));
    SecureZeroMemory(&sync->pending_device, sizeof(sync->pending_device));
    sync->auth_pending = 0;
    sync->active = 0;
}
