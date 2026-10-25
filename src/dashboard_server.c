/* Native WinSock loopback dashboard for ApelsinBot (pure C / MinGW).
 * Never bind to 0.0.0.0; only serve a static local page and tiny JSON API.
 */
#include "dashboard.h"
#include "logger.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DASHBOARD_PREFS "data/dashboard_controls.ini"
#define DASHBOARD_PREFS_TMP "data/dashboard_controls.tmp"
#define DASHBOARD_REQ_MAX 8192
#define DASHBOARD_BODY_MAX 2500
#define DASHBOARD_JSON_MAX 38000
#define DASHBOARD_HTML_MAX (96 * 1024)
#define DASHBOARD_PORT_DEFAULT 8765

typedef struct
{
    CRITICAL_SECTION lock;
    HANDLE thread;
    volatile LONG running;
    SOCKET listener;
    int started;
    unsigned short port;
    DashboardStatus status;
    DashboardChanges pending;
} DashboardServer;

static DashboardServer g_dashboard;

static void changes_reset(DashboardChanges *v)
{
    v->ai = v->promo = v->reports = -1;
    v->obs_action = 0;
    v->obs_scene[0] = '\0';
    v->audio_action=0;v->audio_input[0]='\0';
    v->audio_mute=0;v->audio_volume=0;
    v->music_action=0;v->mark_moment=0;
    v->settings_action=0;
    v->settings_key[0]='\0';v->settings_value[0]='\0';
}
void dashboard_load_preferences(AppConfig *cfg)
{
    FILE *f;
    char line[100];
    if (!cfg) return;
    f = fopen(DASHBOARD_PREFS, "rb");
    if (!f) return;
    while (fgets(line, sizeof(line), f))
    {
        int x;
        if (sscanf(line, "ai=%d", &x) == 1 && (x == 0 || x == 1))
            cfg->ai.enabled = x;
        if (sscanf(line, "promo=%d", &x) == 1 && (x == 0 || x == 1))
            cfg->promo.enabled = x;
        if (sscanf(line, "reports=%d", &x) == 1 && (x == 0 || x == 1))
            cfg->stream_report.enabled = x;
    }
    fclose(f);
    log_info("Local dashboard saved controls loaded");
}
int dashboard_save_preferences(const AppConfig *cfg)
{
    FILE *f;
    int good;
    if (!cfg) return 0;
    f = fopen(DASHBOARD_PREFS_TMP, "wb");
    if (!f) return 0;
    good = fprintf(f, "ai=%d\npromo=%d\nreports=%d\n",
                   cfg->ai.enabled != 0, cfg->promo.enabled != 0,
                   cfg->stream_report.enabled != 0) > 0;
    if (fclose(f) != 0) good = 0;
    if (!good || !MoveFileExA(DASHBOARD_PREFS_TMP, DASHBOARD_PREFS,
                              MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(DASHBOARD_PREFS_TMP);
        return 0;
    }
    return 1;
}
static int json_append(char *out, size_t cap, size_t *pos, const char *s)
{
    size_t n = strlen(s);
    if (*pos + n >= cap) return 0;
    memcpy(out + *pos, s, n+1);
    *pos += n;
    return 1;
}
static int json_quote(char *out, size_t cap, size_t *pos, const char *raw)
{
    const unsigned char *p = (const unsigned char *)(raw ? raw : "");
    if (!json_append(out, cap, pos, "\"")) return 0;
    while (*p)
    {
        char escaped[8];
        if (*p == '"' || *p == '\\')
        {
            escaped[0] = '\\'; escaped[1] = (char)*p;
            escaped[2] = '\0';
            if (!json_append(out, cap, pos, escaped)) return 0;
        }
        else if (*p < 32 || *p == 127)
        {
            snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*p);
            if (!json_append(out, cap, pos, escaped)) return 0;
        }
        else
        {
            if (*pos + 2 >= cap) return 0;
            out[(*pos)++] = (char)*p;
            out[*pos] = '\0';
        }
        ++p;
    }
    return json_append(out, cap, pos, "\"");
}
static int status_json(char *json, size_t capacity)
{
    DashboardStatus s;
    size_t n = 0, i;
    int len;
    EnterCriticalSection(&g_dashboard.lock);
    s = g_dashboard.status;
    LeaveCriticalSection(&g_dashboard.lock);
    len = snprintf(json, capacity,
        "{\"live\":%s,\"viewers\":%d,\"chat\":%s,"
        "\"ai\":%s,\"promo\":%s,\"reports\":%s,\"obs\":%s,"
        "\"queue\":%u,\"profiles\":%u,\"peak\":%d,\"uptime\":%lu,"
        "\"promo_min\":%u,\"promo_max\":%u,\"obs_connected\":%s,"
        "\"obs_recording\":%d,\"obs_streaming\":%d,"
        "\"obs_result_seq\":%u,\"broadcaster\":",
        s.live ? "true" : "false", s.viewers, s.chat_connected ? "true" : "false",
        s.ai_enabled ? "true" : "false", s.promo_enabled ? "true" : "false",
        s.reports_enabled ? "true" : "false", s.obs_enabled ? "true" : "false",
        (unsigned int)s.queue_count, (unsigned int)s.profile_count,
        s.peak_observed, s.uptime_seconds, s.promo_min, s.promo_max,
        s.obs_state.connected ? "true" : "false",
        s.obs_state.recording, s.obs_state.streaming, s.obs_result_sequence);
    if (len < 0 || (size_t)len >= capacity) return 0;
    n = (size_t)len;
    if (!json_quote(json,capacity,&n,s.broadcaster) ||
        !json_append(json,capacity,&n,",\"game\":") ||
        !json_quote(json,capacity,&n,s.game) ||
        !json_append(json,capacity,&n,",\"title\":") ||
        !json_quote(json,capacity,&n,s.title) ||
        !json_append(json,capacity,&n,",\"track\":") ||
        !json_quote(json,capacity,&n,s.track) ||
        !json_append(json,capacity,&n,",\"obs_scene\":") ||
        !json_quote(json,capacity,&n,s.obs_state.scene) ||
        !json_append(json,capacity,&n,",\"obs_result\":") ||
        !json_quote(json,capacity,&n,s.obs_result) ||
        !json_append(json,capacity,&n,",\"obs_scenes\":["))
        return 0;
    for (i = 0; i < s.obs_state.scene_count && i < DASH_OBS_MAX_SCENES; ++i)
    {
        if (i && !json_append(json, capacity, &n, ",")) return 0;
        if (!json_quote(json, capacity, &n, s.obs_state.scenes[i]))
            return 0;
    }
    if (!json_append(json,capacity,&n,"],\"leaderboard\":["))
        return 0;
    for (i = 0; i < s.leader_count && i < DASHBOARD_LEADERS; ++i)
    {
        char value[60];
        if (i && !json_append(json,capacity,&n,",")) return 0;
        if (!json_append(json,capacity,&n,"{\"name\":") ||
            !json_quote(json,capacity,&n,s.leaders[i].name))
            return 0;
        snprintf(value,sizeof(value),",\"balance\":%lld}",s.leaders[i].balance);
        if (!json_append(json,capacity,&n,value)) return 0;
    }
    if(!json_append(json,capacity,&n,"],\"obs_audio\":["))return 0;
    for(i=0;i<s.obs_state.audio_count && i<DASH_OBS_MAX_AUDIO;++i)
    {
        char value[110];
        if(i && !json_append(json,capacity,&n,","))return 0;
        if(!json_append(json,capacity,&n,"{\"name\":") ||
           !json_quote(json,capacity,&n,s.obs_state.audio[i].name))return 0;
        snprintf(value,sizeof(value),
                 ",\"volume\":%.3f,\"muted\":%s}",
                 s.obs_state.audio[i].volume,
                 s.obs_state.audio[i].muted?"true":"false");
        if(!json_append(json,capacity,&n,value))return 0;
    }
    if(!json_append(json,capacity,&n,"],\"music_queue\":["))return 0;
    for(i=0;i<s.queued_count && i<MUSIC_QUEUE_CAPACITY;++i)
    {
        if(i&&!json_append(json,capacity,&n,","))return 0;
        if(!json_append(json,capacity,&n,"{\"title\":") ||
           !json_quote(json,capacity,&n,s.queued[i].title) ||
           !json_append(json,capacity,&n,",\"requester\":") ||
           !json_quote(json,capacity,&n,s.queued[i].requester) ||
           !json_append(json,capacity,&n,"}"))return 0;
    }
    if(!json_append(json,capacity,&n,"],\"view_chart\":["))return 0;
    for(i=0;i<s.chart.count && i<DASH_CHART_SAMPLES;++i)
    {
        char value[20];
        if(i&&!json_append(json,capacity,&n,","))return 0;
        snprintf(value,sizeof(value),"%d",s.chart.viewers[i]);
        if(!json_append(json,capacity,&n,value))return 0;
    }
    if(!json_append(json,capacity,&n,"],\"history\":") ||
       !json_quote(json,capacity,&n,s.reports_history) ||
       !json_append(json,capacity,&n,",\"current_requester\":") ||
       !json_quote(json,capacity,&n,s.current_requester) ||
       !json_append(json,capacity,&n,",\"telegram_url\":") ||
       !json_quote(json,capacity,&n,s.telegram_url) ||
       !json_append(json,capacity,&n,",\"discord_url\":") ||
       !json_quote(json,capacity,&n,s.discord_url) ||
       !json_append(json,capacity,&n,",\"telegram_chat_id\":") ||
       !json_quote(json,capacity,&n,s.telegram_chat_id))
       return 0;
    {
        char value[250];
        snprintf(value,sizeof(value),
                 ",\"ai_cooldown\":%u,\"normal_cooldown\":%u,"
                 "\"game_cooldown\":%u,\"bruh_cooldown\":%u,"
                 "\"command_seq\":%u,",
                 s.ai_cooldown,s.cooldown_normal,s.cooldown_game,
                 s.cooldown_bruh,s.command_seq);
        if(!json_append(json,capacity,&n,value))return 0;
    }
    if(!json_append(json,capacity,&n,"\"command_result\":") ||
       !json_quote(json,capacity,&n,s.command_result) ||
       !json_append(json,capacity,&n,",\"sounds\":["))return 0;
    for(i=0;i<s.sound_count && i<SOUND_MAX_COUNT;++i)
    {
        char value[140];
        if(i&&!json_append(json,capacity,&n,","))return 0;
        if(!json_append(json,capacity,&n,"{\"command\":") ||
           !json_quote(json,capacity,&n,s.sounds[i].command) ||
           !json_append(json,capacity,&n,",\"input\":") ||
           !json_quote(json,capacity,&n,s.sounds[i].input))return 0;
        snprintf(value,sizeof(value),
                 ",\"cost\":%lld,\"cooldown\":%u}",
                 s.sounds[i].cost,s.sounds[i].cooldown_seconds);
        if(!json_append(json,capacity,&n,value))return 0;
    }
    return json_append(json,capacity,&n,"]}");
}

static int starts_with(const char *data, const char *prefix)
{
    return strncmp(data, prefix, strlen(prefix)) == 0;
}
static const char *header_value(const char *req, const char *name)
{
    const char *p = req;
    const size_t length = strlen(name);
    while ((p = strstr(p, "\r\n")) != NULL)
    {
        p += 2;
        if (*p == '\r' || *p == '\0') break;
        if (_strnicmp(p, name, length) == 0 && p[length] == ':')
        {
            p += length+1;
            while (*p == ' ' || *p == '\t') ++p;
            return p;
        }
    }
    return NULL;
}
static int header_equals(const char *request, const char *name, const char *value)
{
    const char *p = header_value(request, name);
    size_t len = strlen(value);
    return p && strncmp(p,value,len) == 0 && p[len] == '\r';
}
static int send_all(SOCKET socket, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length)
    {
        int n = send(socket, data + sent, (int)(length-sent), 0);
        if (n == SOCKET_ERROR || !n) return 0;
        sent += (size_t)n;
    }
    return 1;
}
static void http_reply(SOCKET sock, int status, const char *type,
                       const char *body, size_t length)
{
    char headers[720];
    int n = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Referrer-Policy: no-referrer\r\n"
        "Content-Security-Policy: default-src 'self'; "
        "style-src 'self' 'unsafe-inline'; script-src 'self'; "
        "connect-src 'self'; img-src 'self' data:; object-src 'none'; "
        "base-uri 'none'; frame-ancestors 'none'\r\n"
        "Connection: close\r\n\r\n",
        status,
        status == 200 ? "OK" : status == 202 ? "Accepted" :
        status == 403 ? "Forbidden" : status == 404 ? "Not Found" :
        status == 405 ? "Method Not Allowed" : "Bad Request",
        type, (unsigned int)length);
    if (n <= 0 || (size_t)n >= sizeof(headers)) return;
    if (!send_all(sock, headers, (size_t)n)) return;
    send_all(sock, body, length);
}
static int load_asset(const char *file, char **html, size_t *length)
{
    static const char *prefixes[] = {
        "dashboard/", "../twitch-stream-bot/dashboard/", "../dashboard/"
    };
    char file_path[3][MAX_PATH];
    size_t i;
    FILE *f = NULL;
    long n;
    *html = NULL;
    *length = 0;
    if (strcmp(file,"index.html") && strcmp(file,"app.js") &&
        strcmp(file,"settings.html") && strcmp(file,"settings.js"))return 0;
    for (i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); ++i)
    {
        snprintf(file_path[i], sizeof(file_path[i]), "%s%s", prefixes[i], file);
        if ((f = fopen(file_path[i], "rb")) != NULL) break;
    }
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        n > DASHBOARD_HTML_MAX || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f); return 0;
    }
    *html = (char *)malloc((size_t)n);
    if (!*html) { fclose(f); return 0; }
    if (fread(*html, 1, (size_t)n, f) != (size_t)n)
    {
        free(*html); *html = NULL; fclose(f); return 0;
    }
    fclose(f);
    *length = (size_t)n;
    return 1;
}
static int valid_local_request(const char *req)
{
    char expected[64];
    const char *host = header_value(req, "Host");
    snprintf(expected, sizeof(expected), "127.0.0.1:%u", g_dashboard.port);
    return host && strncmp(host, expected, strlen(expected)) == 0 &&
           host[strlen(expected)] == '\r';
}
static int valid_control_request(const char *req)
{
    char expected[128];
    snprintf(expected, sizeof(expected),
             "http://127.0.0.1:%u", g_dashboard.port);
    return header_equals(req, "Origin", expected) &&
           header_equals(req, "X-Apelsin-Local", "1") &&
           header_equals(req, "Content-Type",
                         "application/x-www-form-urlencoded");
}
static int queue_setting(const char *body)
{
    const char *key = NULL, *value = NULL;
    int on;
    DashboardChanges *p = &g_dashboard.pending;
    /* Only these fixed command shapes are accepted; never URL-decode
     * arbitrary scene names or accept raw OBS commands. */
    if (starts_with(body, "action=scene&index="))
    {
        unsigned int index = 0;
        char extra;
        if (sscanf(body, "action=scene&index=%u%c", &index, &extra) != 1)
            return 0;
        EnterCriticalSection(&g_dashboard.lock);
        if (index >= g_dashboard.status.obs_state.scene_count ||
            index >= DASH_OBS_MAX_SCENES ||
            !g_dashboard.status.obs_state.connected ||
            p->obs_action)
        {
            LeaveCriticalSection(&g_dashboard.lock);
            return 0;
        }
        p->obs_action = 1;
        snprintf(p->obs_scene, sizeof(p->obs_scene), "%s",
                 g_dashboard.status.obs_state.scenes[index]);
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if (starts_with(body, "action=record&enabled="))
    {
        value = body + strlen("action=record&enabled=");
        if ((value[0] != '0' && value[0] != '1') || value[1] != '\0')
            return 0;
        on = value[0] - '0';
        EnterCriticalSection(&g_dashboard.lock);
        if (!g_dashboard.status.obs_state.connected ||
            g_dashboard.status.obs_state.recording == -1 ||
            p->obs_action)
        {
            LeaveCriticalSection(&g_dashboard.lock);
            return 0;
        }
        p->obs_action = on ? 2 : 3;
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if(!strcmp(body,"action=music&mode=skip") ||
       !strcmp(body,"action=music&mode=clear"))
    {
        EnterCriticalSection(&g_dashboard.lock);
        if(p->music_action){LeaveCriticalSection(&g_dashboard.lock);return 0;}
        p->music_action=strstr(body,"skip")?1:2;
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if(!strcmp(body,"action=moment"))
    {
        EnterCriticalSection(&g_dashboard.lock);
        if(!g_dashboard.status.live || p->mark_moment)
        {LeaveCriticalSection(&g_dashboard.lock);return 0;}
        p->mark_moment=1;
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if(starts_with(body,"action=audio&index="))
    {
        unsigned int idx,vol=0;
        char type[10],extra;
        if(sscanf(body,"action=audio&index=%u&mode=%9[a-z]&value=%u%c",
                  &idx,type,&vol,&extra)!=3 ||
           idx>=DASH_OBS_MAX_AUDIO || vol>100 ||
           (strcmp(type,"mute")&&strcmp(type,"volume")))return 0;
        EnterCriticalSection(&g_dashboard.lock);
        if(!g_dashboard.status.obs_state.connected ||
           idx>=g_dashboard.status.obs_state.audio_count ||
           p->audio_action)
        {LeaveCriticalSection(&g_dashboard.lock);return 0;}
        p->audio_action=!strcmp(type,"mute")?2:1;
        p->audio_mute=vol?1:0;
        p->audio_volume=vol/100.0;
        snprintf(p->audio_input,sizeof(p->audio_input),"%s",
                 g_dashboard.status.obs_state.audio[idx].name);
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if(starts_with(body,"action=setting&key="))
    {
        const char *sep=strstr(body+19,"&value=");
        char key[80],value[CONFIG_STRING_SIZE];
        size_t len;
        const char *cur;
        size_t n=0;
        if(!sep)return 0;
        len=(size_t)(sep-(body+19));
        if(!len||len>=sizeof(key))return 0;
        memcpy(key,body+19,len);key[len]='\0';
        for(cur=key;*cur;++cur)
            if(!((*cur>='a'&&*cur<='z')||(*cur>='0'&&*cur<='9')||*cur=='_'))
                return 0;
        cur=sep+7;
        while(*cur)
        {
            unsigned char c=(unsigned char)*cur++;
            if(c=='%')
            {
                int a,b;unsigned char x,y;
                if(!cur[0]||!cur[1])return 0;
                x=(unsigned char)cur[0];y=(unsigned char)cur[1];
                a=(x>='0'&&x<='9')?x-'0':(x>='A'&&x<='F')?x-'A'+10:
                  (x>='a'&&x<='f')?x-'a'+10:-1;
                b=(y>='0'&&y<='9')?y-'0':(y>='A'&&y<='F')?y-'A'+10:
                  (y>='a'&&y<='f')?y-'a'+10:-1;
                if(a<0||b<0)return 0;
                c=(unsigned char)(a*16+b);cur+=2;
            }
            else if(c=='+')c=' ';
            if(c<32 || c==127 || n+1>=sizeof(value))return 0;
            value[n++]=(char)c;
        }
        value[n]='\0';
        EnterCriticalSection(&g_dashboard.lock);
        if(p->settings_action){LeaveCriticalSection(&g_dashboard.lock);return 0;}
        p->settings_action=1;
        snprintf(p->settings_key,sizeof(p->settings_key),"%s",key);
        memcpy(p->settings_value,value,n+1);
        LeaveCriticalSection(&g_dashboard.lock);
        return 1;
    }
    if (starts_with(body, "feature=ai&enabled=")) key = "ai";
    if (starts_with(body, "feature=promo&enabled=")) key = "promo";
    if (starts_with(body, "feature=reports&enabled=")) key = "reports";
    if (!key) return 0;
    value = strstr(body, "&enabled=") + 9;
    if ((value[0] != '0' && value[0] != '1') || value[1] != '\0')
        return 0;
    on = value[0] - '0';
    EnterCriticalSection(&g_dashboard.lock);
    if (!strcmp(key,"ai")) p->ai = on;
    if (!strcmp(key,"promo")) p->promo = on;
    if (!strcmp(key,"reports")) p->reports = on;
    LeaveCriticalSection(&g_dashboard.lock);
    return 1;
}
static void handle_connection(SOCKET client)
{
    char request[DASHBOARD_REQ_MAX+1];
    char *body;
    char payload[DASHBOARD_JSON_MAX];
    int received, cl = 0;
    DWORD timeout = 2500;
    const char *len;
    size_t used = 0;
    int is_get = 0, is_post = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
               sizeof(timeout));
    for (;;)
    {
        received = recv(client, request + used,
                        (int)(DASHBOARD_REQ_MAX-used), 0);
        if (received <= 0) return;
        used += (size_t)received;
        request[used] = '\0';
        body = strstr(request, "\r\n\r\n");
        if (body) break;
        if (used >= DASHBOARD_REQ_MAX) return;
    }
    if (!valid_local_request(request))
    {
        http_reply(client,403,"text/plain; charset=utf-8","Forbidden",9);
        return;
    }
    is_get = starts_with(request,"GET ");
    is_post = starts_with(request,"POST ");
    if (is_get && (starts_with(request,"GET / ") ||
                   starts_with(request,"GET /index.html ")))
    {
        char *html = NULL;
        size_t html_len = 0;
        if (load_asset("index.html", &html, &html_len))
        {
            http_reply(client,200,"text/html; charset=utf-8",html,html_len);
            free(html);
        }
        else
            http_reply(client,404,"text/plain; charset=utf-8",
                       "dashboard/index.html not found",30);
    }
    else if (is_get && starts_with(request,"GET /app.js "))
    {
        char *js = NULL;
        size_t js_len = 0;
        if (load_asset("app.js", &js, &js_len))
        {
            http_reply(client,200,"text/javascript; charset=utf-8",js,js_len);
            free(js);
        }
        else http_reply(client,404,"text/plain; charset=utf-8",
                        "dashboard/app.js not found",26);
    }
    else if (is_get && starts_with(request,"GET /settings "))
    {
        char *html=NULL;size_t size=0;
        if(load_asset("settings.html",&html,&size))
        {
            http_reply(client,200,"text/html; charset=utf-8",html,size);
            free(html);
        }
        else http_reply(client,404,"text/plain","Not found",9);
    }
    else if (is_get && starts_with(request,"GET /settings.js "))
    {
        char *js=NULL;size_t size=0;
        if(load_asset("settings.js",&js,&size))
        {
            http_reply(client,200,"text/javascript; charset=utf-8",js,size);
            free(js);
        }
        else http_reply(client,404,"text/plain","Not found",9);
    }
    else if (is_get && starts_with(request,"GET /api/status "))
    {
        if (status_json(payload,sizeof(payload)))
            http_reply(client,200,"application/json; charset=utf-8",
                       payload,strlen(payload));
        else
            http_reply(client,400,"application/json; charset=utf-8",
                       "{}",2);
    }
    else if (is_post && starts_with(request,"POST /api/control "))
    {
        if (!valid_control_request(request))
        {
            http_reply(client,403,"application/json; charset=utf-8",
                       "{\"error\":\"forbidden\"}",21);
            return;
        }
        len = header_value(request,"Content-Length");
        if (!len || sscanf(len,"%d",&cl) != 1 || cl < 1 ||
            cl > DASHBOARD_BODY_MAX)
        {
            http_reply(client,400,"application/json; charset=utf-8",
                       "{\"error\":\"length\"}",18);
            return;
        }
        body += 4;
        {
            size_t buffered = used-(size_t)(body-request);
            while (buffered < (size_t)cl && used < DASHBOARD_REQ_MAX)
            {
                received = recv(client,request+used,
                                (int)(DASHBOARD_REQ_MAX-used),0);
                if (received <= 0) return;
                used += (size_t)received;
                buffered += (size_t)received;
            }
            if (buffered < (size_t)cl) return;
            body[cl] = '\0';
        }
        if (queue_setting(body))
            http_reply(client,202,"application/json; charset=utf-8",
                       "{\"queued\":true}",15);
        else
            http_reply(client,400,"application/json; charset=utf-8",
                       "{\"error\":\"invalid\"}",19);
    }
    else if (is_get || is_post)
        http_reply(client,404,"text/plain; charset=utf-8","Not found",9);
    else
        http_reply(client,405,"text/plain; charset=utf-8",
                   "Method not allowed",18);
}
static DWORD WINAPI server_thread(LPVOID unused)
{
    (void)unused;
    while (InterlockedCompareExchange(&g_dashboard.running,0,0))
    {
        fd_set readfds;
        struct timeval wait = {0,250000};
        int n;
        FD_ZERO(&readfds);
        FD_SET(g_dashboard.listener,&readfds);
        n = select(0,&readfds,NULL,NULL,&wait);
        if (n > 0)
        {
            SOCKET peer = accept(g_dashboard.listener,NULL,NULL);
            if (peer != INVALID_SOCKET)
            {
                handle_connection(peer);
                closesocket(peer);
            }
        }
    }
    return 0;
}
int dashboard_start(int enabled, unsigned short port)
{
    struct sockaddr_in addr;
    WSADATA ws;
    SOCKET sock;
    if (!enabled) return 0;
    memset(&g_dashboard,0,sizeof(g_dashboard));
    if (WSAStartup(MAKEWORD(2,2),&ws) != 0) return 0;
    sock = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return 0; }
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port ? port : DASHBOARD_PORT_DEFAULT);
    if (bind(sock,(struct sockaddr*)&addr,sizeof(addr)) == SOCKET_ERROR ||
        listen(sock,5) == SOCKET_ERROR)
    {
        closesocket(sock); WSACleanup();
        log_warning("Dashboard unable to bind 127.0.0.1:%u",
                    (unsigned int)(port ? port : DASHBOARD_PORT_DEFAULT));
        return 0;
    }
    InitializeCriticalSection(&g_dashboard.lock);
    changes_reset(&g_dashboard.pending);
    g_dashboard.listener = sock;
    g_dashboard.port = port ? port : DASHBOARD_PORT_DEFAULT;
    InterlockedExchange(&g_dashboard.running,1);
    g_dashboard.thread = CreateThread(NULL,0,server_thread,NULL,0,NULL);
    if (!g_dashboard.thread)
    {
        closesocket(sock);
        DeleteCriticalSection(&g_dashboard.lock);
        WSACleanup();
        return 0;
    }
    g_dashboard.started = 1;
    log_info("Apelsin dashboard: http://127.0.0.1:%u",
             (unsigned int)g_dashboard.port);
    return 1;
}
void dashboard_stop(void)
{
    if (!g_dashboard.started) return;
    InterlockedExchange(&g_dashboard.running,0);
    WaitForSingleObject(g_dashboard.thread,6000);
    CloseHandle(g_dashboard.thread);
    closesocket(g_dashboard.listener);
    DeleteCriticalSection(&g_dashboard.lock);
    WSACleanup();
    g_dashboard.started = 0;
}
void dashboard_publish(const DashboardStatus *status)
{
    if (!status || !g_dashboard.started) return;
    EnterCriticalSection(&g_dashboard.lock);
    g_dashboard.status = *status;
    LeaveCriticalSection(&g_dashboard.lock);
}
int dashboard_take_changes(DashboardChanges *changes)
{
    int pending;
    if (!changes || !g_dashboard.started) return 0;
    EnterCriticalSection(&g_dashboard.lock);
    *changes = g_dashboard.pending;
    changes_reset(&g_dashboard.pending);
    LeaveCriticalSection(&g_dashboard.lock);
    pending = changes->ai != -1 || changes->promo != -1 ||
              changes->reports != -1 || changes->obs_action != 0 ||
              changes->audio_action || changes->music_action ||
              changes->mark_moment || changes->settings_action;
    return pending;
}
