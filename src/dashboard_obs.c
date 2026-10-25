#include "dashboard_obs.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static int obs_get_json(ObsWebSocket *client, const char *method,
                        cJSON **out)
{
    char response[16000];
    if (!out) return 0;
    *out = NULL;
    if (obs_websocket_request(client, method, NULL, response,
                              sizeof(response)) != BOT_OK)
        return 0;
    *out = cJSON_Parse(response[0] ? response : "{}");
    return *out != NULL;
}
static int obs_read_active(ObsWebSocket *client, const char *method, int *result)
{
    cJSON *json = NULL;
    const cJSON *active;
    int ok = 0;
    if (!obs_get_json(client, method, &json)) return 0;
    active = cJSON_GetObjectItemCaseSensitive(json, "outputActive");
    if (active && (active->type & cJSON_True))
    {
        *result = 1;
        ok = 1;
    }
    else if (active && (active->type & cJSON_False))
    {
        *result = 0;
        ok = 1;
    }
    cJSON_Delete(json);
    return ok;
}
static int obs_scene_list(ObsWebSocket *client, DashboardObs *out)
{
    cJSON *json = NULL;
    const cJSON *scenes, *scene, *name, *current;
    size_t count = 0;
    int index, array_size;
    if (!obs_get_json(client, "GetSceneList", &json)) return 0;
    scenes = cJSON_GetObjectItemCaseSensitive(json, "scenes");
    current = cJSON_GetObjectItemCaseSensitive(json, "currentProgramSceneName");
    if (cJSON_IsString(current) && current->valuestring)
        snprintf(out->scene, sizeof(out->scene), "%s", current->valuestring);
    if (!cJSON_IsArray(scenes)) { cJSON_Delete(json); return 0; }
    array_size = cJSON_GetArraySize(scenes);
    for (index = 0; index < array_size && count < DASH_OBS_MAX_SCENES; ++index)
    {
        scene = cJSON_GetArrayItem(scenes, index);
        name = cJSON_GetObjectItemCaseSensitive(scene, "sceneName");
        if (!cJSON_IsString(name) || !name->valuestring ||
            strlen(name->valuestring) >= DASH_OBS_SCENE_LENGTH)
            continue;
        snprintf(out->scenes[count], sizeof(out->scenes[count]),
                 "%s", name->valuestring);
        ++count;
    }
    out->scene_count = count;
    cJSON_Delete(json);
    return 1;
}
int dashboard_obs_refresh(const char *password, DashboardObs *out)
{
    ObsWebSocket ws = {0};
    int connected, scenes_ok;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->recording = -1;
    out->streaming = -1;
    connected = obs_websocket_connect(&ws, password) == BOT_OK;
    if (!connected) return 0;
    out->connected = 1;
    scenes_ok = obs_scene_list(&ws, out);
    (void)obs_read_active(&ws, "GetRecordStatus", &out->recording);
    (void)obs_read_active(&ws, "GetStreamStatus", &out->streaming);
    obs_websocket_close(&ws);
    return scenes_ok;
}
int dashboard_obs_switch_scene(const char *password, const char *scene_name)
{
    ObsWebSocket ws = {0};
    DashboardObs scenes = {0};
    BotResult result = BOT_ERR_CONFIG;
    size_t i;
    if (!scene_name || !scene_name[0] ||
        strlen(scene_name) >= DASH_OBS_SCENE_LENGTH)
        return 0;
    if (obs_websocket_connect(&ws, password) != BOT_OK) return 0;
    if (obs_scene_list(&ws, &scenes))
    {
        for (i = 0; i < scenes.scene_count; ++i)
            if (!strcmp(scenes.scenes[i], scene_name))
            {
                result = obs_websocket_set_scene(&ws, scene_name);
                break;
            }
    }
    obs_websocket_close(&ws);
    return result == BOT_OK;
}
int dashboard_obs_record(const char *password, int start)
{
    ObsWebSocket ws = {0};
    char response[4096];
    int active = -1, ok = 0;
    if (obs_websocket_connect(&ws, password) != BOT_OK) return 0;
    if (!obs_read_active(&ws, "GetRecordStatus", &active)) goto cleanup;
    if (start && active == 1) { ok = 1; goto cleanup; }
    if (!start && active == 0) { ok = 1; goto cleanup; }
    if (obs_websocket_request(&ws, start ? "StartRecord" : "StopRecord",
                              NULL, response, sizeof(response)) == BOT_OK)
        ok = 1;
cleanup:
    obs_websocket_close(&ws);
    return ok;
}
