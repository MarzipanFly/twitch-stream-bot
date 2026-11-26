#include "channel_points.h"
#include "eventsub_dedupe.h"
#include "logger.h"
#include "token_store.h"
#include "twitch_auth.h"
#include "twitch_eventsub.h"
#include "twitch_eventsub_ws.h"
#include "twitch_refresh.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#define CHANNEL_POINTS_SCOPE "channel:read:redemptions"
#define CHANNEL_POINTS_RETRY_MS 5000UL
#define CHANNEL_POINTS_AUTH_RETRY_MS 30000UL
#define CHANNEL_POINTS_WS_RECEIVE_TIMEOUT_MS 15000
static int stopping(const ChannelPointsWorker *w)
{
    return !w || !w->stop_event || WaitForSingleObject(w->stop_event, 0) == WAIT_OBJECT_0;
}
static int wait_stop(ChannelPointsWorker *w, DWORD ms)
{
    return !w || !w->stop_event || WaitForSingleObject(w->stop_event, ms) == WAIT_OBJECT_0;
}
static int login_equal(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; ++a; ++b; }
    return *a == '\0' && *b == '\0';
}
static int has_scope(const TwitchTokenValidation *v)
{
    size_t i;
    if (!v) return 0;
    for (i=0;i<v->scope_count;++i) if (!strcmp(v->scopes[i],CHANNEL_POINTS_SCOPE)) return 1;
    return 0;
}
static BotResult apply_token(ChannelPointsWorker *w, const TwitchAuthToken *t,
                             TwitchConfig *owner, int persist)
{
    TwitchTokenValidation v={0};
    BotResult r;
    if (!w || !t || !owner || !t->access_token[0]) return BOT_ERR_AUTH;
    r=twitch_auth_validate_token(t->access_token,&v);
    if (r!=BOT_OK) return r;
    if (strcmp(v.client_id,w->auth_config.client_id) ||
        !login_equal(v.login,w->auth_config.broadcaster_login) ||
        (w->auth_config.broadcaster_id[0] && strcmp(v.user_id,w->auth_config.broadcaster_id)) ||
        !has_scope(&v))
    {
        log_warning("Channel Points OAuth must belong to broadcaster %s and include %s",
                    w->auth_config.broadcaster_login,CHANNEL_POINTS_SCOPE);
        return BOT_ERR_AUTH;
    }
    memset(owner,0,sizeof(*owner));
    snprintf(owner->client_id,sizeof(owner->client_id),"%s",w->auth_config.client_id);
    snprintf(owner->broadcaster_login,sizeof(owner->broadcaster_login),"%s",v.login);
    snprintf(owner->broadcaster_id,sizeof(owner->broadcaster_id),"%s",v.user_id);
    snprintf(owner->access_token,sizeof(owner->access_token),"%s",t->access_token);
    snprintf(owner->refresh_token,sizeof(owner->refresh_token),"%s",t->refresh_token);
    if (persist && (r=token_store_save_channel_points(t))!=BOT_OK) return r;
    log_info("Channel Points broadcaster OAuth validated for %s",v.login);
    return BOT_OK;
}
static BotResult authorize(ChannelPointsWorker *w,TwitchConfig *owner)
{
    TwitchDeviceCode d={0}; TwitchAuthToken t={0}; BotResult r; int elapsed=0,interval;
    r=twitch_auth_request_device_code_with_scopes(&w->auth_config,CHANNEL_POINTS_SCOPE,&d);
    if(r!=BOT_OK)return r;
    printf("\n[ApelsinBot / Channel Points]\nAuthorize broadcaster account: %s\nOpen %s and enter code: %s\n",
           w->auth_config.broadcaster_login,d.verification_uri,d.user_code);
    fflush(stdout);
    interval=d.interval>0?d.interval:5;
    while(elapsed<d.expires_in&&!stopping(w))
    {
        if(wait_stop(w,(DWORD)interval*1000UL)){SecureZeroMemory(&d,sizeof(d));return BOT_AUTH_PENDING;}
        elapsed+=interval;
        r=twitch_auth_poll_token_with_scopes(&w->auth_config,&d,CHANNEL_POINTS_SCOPE,&t);
        if(r==BOT_AUTH_PENDING)continue;
        if(r==BOT_OK)r=apply_token(w,&t,owner,1);
        SecureZeroMemory(&t,sizeof(t)); SecureZeroMemory(&d,sizeof(d)); return r;
    }
    SecureZeroMemory(&t,sizeof(t)); SecureZeroMemory(&d,sizeof(d)); return BOT_ERR_AUTH;
}
static BotResult prepare_owner(ChannelPointsWorker *w,TwitchConfig *owner)
{
    TwitchAuthToken stored={0},fresh={0}; BotResult r=token_store_load_channel_points(&stored);
    if(r==BOT_OK)
    {
        r=apply_token(w,&stored,owner,0);
        if(r==BOT_OK){SecureZeroMemory(&stored,sizeof(stored));return BOT_OK;}
        if(r!=BOT_ERR_AUTH){SecureZeroMemory(&stored,sizeof(stored));return r;}
        if(stored.refresh_token[0])
        {
            r=twitch_refresh_access_token(&w->auth_config,stored.refresh_token,&fresh);
            if(r==BOT_OK)r=apply_token(w,&fresh,owner,1);
            if(r==BOT_OK){SecureZeroMemory(&stored,sizeof(stored));SecureZeroMemory(&fresh,sizeof(fresh));return BOT_OK;}
            if(r!=BOT_ERR_AUTH){SecureZeroMemory(&stored,sizeof(stored));SecureZeroMemory(&fresh,sizeof(fresh));return r;}
        }
        SecureZeroMemory(&stored,sizeof(stored));SecureZeroMemory(&fresh,sizeof(fresh));
        (void)token_store_delete_channel_points();
    }
    else if(r!=BOT_ERR_FILE){SecureZeroMemory(&stored,sizeof(stored));(void)token_store_delete_channel_points();}
    if(stopping(w))return BOT_AUTH_PENDING;
    log_info("Channel Points requires broadcaster OAuth approval (%s)",CHANNEL_POINTS_SCOPE);
    return authorize(w,owner);
}
static int queue_push(ChannelPointsWorker *w,const TwitchChannelPointsRedemption *e)
{
    size_t tail; int ok=0;
    if(!w||!e||!w->queue_lock_initialized)return 0;
    EnterCriticalSection(&w->queue_lock);
    if(w->queue_count<CHANNEL_POINTS_QUEUE_CAPACITY)
    {
        tail=(w->queue_head+w->queue_count)%CHANNEL_POINTS_QUEUE_CAPACITY;
        w->queue[tail]=*e; ++w->queue_count; ok=1;
    }
    else ++w->dropped;
    LeaveCriticalSection(&w->queue_lock);
    if(!ok)log_warning("Channel Points queue full; redemption %s dropped",e->redemption_id);
    return ok;
}
static void handle_reconnect(TwitchEventSubWebSocket *ws,const char *msg)
{
    char url[TWITCH_EVENTSUB_URL_SIZE]={0},welcome[32768]={0};
    TwitchEventSubWebSocket next={0}; TwitchEventSubSession s={0};
    TwitchEventSubMessageType type=TWITCH_EVENTSUB_UNKNOWN; BotResult r;
    r=twitch_eventsub_parse_reconnect_url(msg,url,sizeof(url));
    if(r==BOT_OK)r=twitch_eventsub_ws_connect_reconnect(&next,url);
    SecureZeroMemory(url,sizeof(url));
    if(r==BOT_OK)
    {
        (void)WinHttpSetTimeouts(next.session,5000,5000,5000,CHANNEL_POINTS_WS_RECEIVE_TIMEOUT_MS);
        r=twitch_eventsub_ws_receive(&next,welcome,sizeof(welcome));
    }
    if(r==BOT_OK)r=twitch_eventsub_get_message_type(welcome,&type);
    if(r==BOT_OK&&type==TWITCH_EVENTSUB_SESSION_WELCOME)r=twitch_eventsub_parse_welcome(welcome,&s);
    else if(r==BOT_OK)r=BOT_ERR_TWITCH;
    if(r==BOT_OK&&s.session_id[0])
    {
        twitch_eventsub_ws_close(ws); *ws=next; memset(&next,0,sizeof(next));
        log_info("Channel Points EventSub seamlessly reconnected");
    }
    else log_warning("Channel Points EventSub reconnect handover failed; retaining current socket");
    twitch_eventsub_ws_close(&next); SecureZeroMemory(welcome,sizeof(welcome));
}
static BotResult run_session(ChannelPointsWorker *w,const TwitchConfig *owner)
{
    TwitchEventSubWebSocket ws={0}; TwitchEventSubSession session={0};
    TwitchEventSubMessageType type=TWITCH_EVENTSUB_UNKNOWN; EventSubDedupe dedupe={0};
    char json[32768]={0},id[TWITCH_EVENTSUB_ID_SIZE]={0}; BotResult r;
    r=twitch_eventsub_ws_connect(&ws); if(r!=BOT_OK)return r;
    (void)WinHttpSetTimeouts(ws.session,5000,5000,5000,CHANNEL_POINTS_WS_RECEIVE_TIMEOUT_MS);
    r=twitch_eventsub_ws_receive(&ws,json,sizeof(json));
    if(r==BOT_OK)r=twitch_eventsub_get_message_type(json,&type);
    if(r==BOT_OK&&type==TWITCH_EVENTSUB_SESSION_WELCOME)r=twitch_eventsub_parse_welcome(json,&session);
    else if(r==BOT_OK)r=BOT_ERR_TWITCH;
    if(r==BOT_OK)r=twitch_eventsub_subscribe_channel_points(owner,session.session_id);
    if(r!=BOT_OK){twitch_eventsub_ws_close(&ws);return r;}
    log_info("Channel Points EventSub subscribed for %s",owner->broadcaster_login);
    while(!stopping(w))
    {
        TwitchChannelPointsRedemption e;
        r=twitch_eventsub_ws_receive(&ws,json,sizeof(json)); if(r!=BOT_OK)break;
        r=twitch_eventsub_get_message_type(json,&type); if(r!=BOT_OK)continue;
        if(type==TWITCH_EVENTSUB_KEEPALIVE)continue;
        if(type==TWITCH_EVENTSUB_RECONNECT){handle_reconnect(&ws,json);continue;}
        if(type!=TWITCH_EVENTSUB_NOTIFICATION)continue;
        if(twitch_eventsub_get_message_id(json,id,sizeof(id))!=BOT_OK)continue;
        if(!eventsub_dedupe_accept(&dedupe,id,(unsigned long long)time(NULL)))continue;
        r=twitch_eventsub_parse_channel_points_redemption(json,&e);
        if(r!=BOT_OK)continue;
        if(strcmp(e.broadcaster_user_id,owner->broadcaster_id)){SecureZeroMemory(&e,sizeof(e));continue;}
        (void)queue_push(w,&e); SecureZeroMemory(&e,sizeof(e));
    }
    twitch_eventsub_ws_close(&ws); SecureZeroMemory(json,sizeof(json)); SecureZeroMemory(id,sizeof(id));
    return r;
}
static DWORD WINAPI worker_main(LPVOID p)
{
    ChannelPointsWorker *w=(ChannelPointsWorker *)p;
    while(!stopping(w))
    {
        TwitchConfig owner={0}; BotResult r=prepare_owner(w,&owner);
        if(r==BOT_OK)r=run_session(w,&owner);
        SecureZeroMemory(&owner,sizeof(owner));
        if(stopping(w))break;
        if(r!=BOT_OK)log_warning("Channel Points listener retry: %s",bot_result_to_string(r));
        (void)wait_stop(w,r==BOT_AUTH_PENDING?CHANNEL_POINTS_AUTH_RETRY_MS:CHANNEL_POINTS_RETRY_MS);
    }
    return 0;
}
BotResult channel_points_start(const AppConfig *config,ChannelPointsWorker *w)
{
    if(!config||!w)return BOT_ERR_CONFIG;
    memset(w,0,sizeof(*w));
    if(!config->channel_points.enabled){log_info("Twitch Channel Points integration disabled");return BOT_OK;}
    if(!config->twitch.client_id[0]||!config->twitch.broadcaster_login[0])return BOT_ERR_CONFIG;
    snprintf(w->auth_config.client_id,sizeof(w->auth_config.client_id),"%s",config->twitch.client_id);
    snprintf(w->auth_config.broadcaster_login,sizeof(w->auth_config.broadcaster_login),"%s",config->twitch.broadcaster_login);
    snprintf(w->auth_config.broadcaster_id,sizeof(w->auth_config.broadcaster_id),"%s",config->twitch.broadcaster_id);
    w->stop_event=CreateEventA(NULL,TRUE,FALSE,NULL); if(!w->stop_event)return BOT_ERR_UNKNOWN;
    InitializeCriticalSection(&w->queue_lock); w->queue_lock_initialized=1;
    w->thread=CreateThread(NULL,0,worker_main,w,0,NULL);
    if(!w->thread){DeleteCriticalSection(&w->queue_lock);w->queue_lock_initialized=0;CloseHandle(w->stop_event);w->stop_event=NULL;return BOT_ERR_UNKNOWN;}
    log_info("Twitch Channel Points worker started for %s",w->auth_config.broadcaster_login);
    return BOT_OK;
}
int channel_points_pop(ChannelPointsWorker *w,TwitchChannelPointsRedemption *e)
{
    if(!w||!e||!w->queue_lock_initialized)return 0;
    EnterCriticalSection(&w->queue_lock);
    if(!w->queue_count){LeaveCriticalSection(&w->queue_lock);return 0;}
    *e=w->queue[w->queue_head];memset(&w->queue[w->queue_head],0,sizeof(w->queue[w->queue_head]));
    w->queue_head=(w->queue_head+1U)%CHANNEL_POINTS_QUEUE_CAPACITY;--w->queue_count;
    LeaveCriticalSection(&w->queue_lock);return 1;
}
void channel_points_stop(ChannelPointsWorker *w)
{
    if(!w)return;
    if(w->stop_event)SetEvent(w->stop_event);
    if(w->thread){(void)WaitForSingleObject(w->thread,INFINITE);CloseHandle(w->thread);}
    if(w->queue_lock_initialized)DeleteCriticalSection(&w->queue_lock);
    if(w->stop_event)CloseHandle(w->stop_event);
    SecureZeroMemory(w,sizeof(*w));
}
