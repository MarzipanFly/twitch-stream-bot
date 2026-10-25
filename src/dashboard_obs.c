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
static int audio_request(ObsWebSocket *ws, const char *request,
                         const char *name, cJSON **json)
{
    char data[600], reply[16000];
    size_t i = 0, n = 0;
    char quoted[530];
    if (!name || strlen(name)>240) return 0;
    quoted[n++]='"';
    for(i=0;name[i];++i)
    {
        unsigned char c=(unsigned char)name[i];
        if(c<32)return 0;
        if(c=='"' || c=='\\')quoted[n++]='\\';
        quoted[n++]=(char)c;
    }
    quoted[n++]='"';quoted[n]='\0';
    snprintf(data,sizeof(data),"{\"inputName\":%s}",quoted);
    if(obs_websocket_request(ws,request,data,reply,sizeof(reply))!=BOT_OK)
        return 0;
    *json=cJSON_Parse(reply);
    return *json!=NULL;
}
static void load_audio(ObsWebSocket *ws, DashboardObs *out)
{
    cJSON *list=NULL, *root=NULL, *state=NULL;
    const cJSON *inputs,*item,*name,*value;
    int i;
    char request[700], reply[256];
    if(!obs_get_json(ws,"GetInputList",&list))return;
    inputs=cJSON_GetObjectItemCaseSensitive(list,"inputs");
    if(!cJSON_IsArray(inputs)){cJSON_Delete(list);return;}
    for(i=0;i<cJSON_GetArraySize(inputs) &&
          out->audio_count<DASH_OBS_MAX_AUDIO;++i)
    {
        item=cJSON_GetArrayItem(inputs,i);
        name=cJSON_GetObjectItemCaseSensitive(item,"inputName");
        if(!cJSON_IsString(name)||!name->valuestring ||
           strlen(name->valuestring)>240)continue;
        if(!audio_request(ws,"GetInputVolume",name->valuestring,&root))
            continue; /* non-audio OBS input */
        value=cJSON_GetObjectItemCaseSensitive(root,"inputVolumeMul");
        if(!cJSON_IsNumber(value)){cJSON_Delete(root);root=NULL;continue;}
        out->audio[out->audio_count].volume=value->valuedouble;
        cJSON_Delete(root);root=NULL;
        if(!audio_request(ws,"GetInputMute",name->valuestring,&state))continue;
        value=cJSON_GetObjectItemCaseSensitive(state,"inputMuted");
        if(!value || (!(value->type & cJSON_True) &&
                      !(value->type & cJSON_False)))
        {cJSON_Delete(state);state=NULL;continue;}
        out->audio[out->audio_count].muted=(value->type & cJSON_True)!=0;
        snprintf(out->audio[out->audio_count].name,
                 sizeof(out->audio[out->audio_count].name),
                 "%s",name->valuestring);
        ++out->audio_count;
        cJSON_Delete(state);state=NULL;
    }
    cJSON_Delete(list);
    (void)request;(void)reply;
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
    load_audio(&ws,out);
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

int dashboard_obs_audio(const char *password, const char *name,
                        int set_mute, int mute, double volume)
{
    ObsWebSocket ws={0};
    cJSON *json=NULL;
    const cJSON *inputs,*item,*item_name;
    char quoted[530], body[730], reply[2048];
    int i, found=0, ok=0, n=0;
    size_t k;
    if(!name || !*name || strlen(name)>240 ||
       (set_mute!=0 && set_mute!=1) ||
       (volume<0 || volume>1))return 0;
    if(obs_websocket_connect(&ws,password)!=BOT_OK)return 0;
    if(!obs_get_json(&ws,"GetInputList",&json))goto done;
    inputs=cJSON_GetObjectItemCaseSensitive(json,"inputs");
    for(i=0;inputs&&i<cJSON_GetArraySize(inputs);++i)
    {
        item=cJSON_GetArrayItem(inputs,i);
        item_name=cJSON_GetObjectItemCaseSensitive(item,"inputName");
        if(cJSON_IsString(item_name)&&!strcmp(item_name->valuestring,name))
            found=1;
    }
    cJSON_Delete(json);json=NULL;
    if(!found)goto done;
    quoted[n++]='"';
    for(k=0;name[k];++k)
    {
        unsigned char c=(unsigned char)name[k];
        if(c<32)goto done;
        if(c=='"'||c=='\\')quoted[n++]='\\';
        quoted[n++]=(char)c;
    }
    quoted[n++]='"';quoted[n]='\0';
    if(set_mute)
        snprintf(body,sizeof(body),"{\"inputName\":%s,\"inputMuted\":%s}",
                 quoted,mute?"true":"false");
    else
        snprintf(body,sizeof(body),"{\"inputName\":%s,\"inputVolumeMul\":%.3f}",
                 quoted,volume);
    ok=obs_websocket_request(&ws,set_mute?"SetInputMute":"SetInputVolume",
                              body,reply,sizeof(reply))==BOT_OK;
done:
    cJSON_Delete(json);
    obs_websocket_close(&ws);
    return ok;
}
