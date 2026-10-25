/* Local dashboard smoke test, Windows only.
 * From repo root (MinGW):
 * gcc -std=c11 -Wall -Wextra -Iinclude tests/test_dashboard.c \
 *     src/dashboard_server.c src/logger.c -lws2_32 -o test_dashboard.exe
 * Then run test_dashboard.exe; it uses tests/.tmp_dashboard, not real data/.
 */
#include "dashboard.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <direct.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int request(const char *req, char *out, int cap)
{
    WSADATA ws;
    SOCKET sock;
    struct sockaddr_in address;
    int total=0,n;
    assert(WSAStartup(MAKEWORD(2,2),&ws)==0);
    sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    assert(sock!=INVALID_SOCKET);
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(18765);
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(connect(sock,(struct sockaddr*)&address,sizeof(address))==0);
    assert(send(sock,req,(int)strlen(req),0)>0);
    while (total<cap-1 && (n=recv(sock,out+total,cap-total-1,0))>0)
        total+=n;
    out[total]='\0';
    closesocket(sock);
    WSACleanup();
    return total;
}
int main(void)
{
    AppConfig config={0};
    DashboardStatus data={0};
    DashboardChanges change;
    char buffer[12000];
    const char *get =
        "GET /api/status HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nConnection: close\r\n\r\n";
    const char *cross_site =
        "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: https://evil.test\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 23\r\n\r\nfeature=promo&enabled=0";
    const char *same_site =
        "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 25\r\n\r\nfeature=reports&enabled=0";
    const char *scene_change =
        "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 20\r\n\r\naction=scene&index=1";
    const char *record_stop =
        "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 23\r\n\r\naction=record&enabled=0";
    const char *req_action_setting_key_promo_min_value_ = "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 36\r\n\r\naction=setting&key=promo_min&value=6";
    const char *req_action_audio_index_mode_volume_value_ = "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 41\r\n\r\naction=audio&index=0&mode=volume&value=55";
    const char *req_action_music_mode_clear = "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 23\r\n\r\naction=music&mode=clear";
    const char *req_action_moment = "POST /api/control HTTP/1.1\r\n"
        "Host: 127.0.0.1:18765\r\nOrigin: http://127.0.0.1:18765\r\n"
        "X-Apelsin-Local: 1\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 13\r\n\r\naction=moment";
    const char *bad_host =
        "GET /api/status HTTP/1.1\r\n"
        "Host: malicious.example\r\nConnection: close\r\n\r\n";

    _mkdir("tests/.tmp_dashboard");
    assert(_chdir("tests/.tmp_dashboard")==0);
    _mkdir("data");
    config.ai.enabled=1;
    config.promo.enabled=1;
    config.stream_report.enabled=0;
    assert(dashboard_save_preferences(&config));
    memset(&config,0,sizeof(config));
    dashboard_load_preferences(&config);
    assert(config.ai.enabled==1 && config.promo.enabled==1);
    assert(config.stream_report.enabled==0);

    assert(dashboard_start(1,18765));
    data.live=1;
    data.viewers=8;
    data.profile_count=17;
    strcpy(data.broadcaster,"aleg_opelsin1");
    strcpy(data.title,"Играю с <друзьями> & веселюсь");
    data.obs_enabled=1;
    data.obs_state.connected=1;
    data.obs_state.recording=1;
    data.obs_state.streaming=1;
    data.obs_state.scene_count=2;
    data.obs_state.audio_count=1;
    strcpy(data.obs_state.audio[0].name,"Mic/Aux");
    data.obs_state.audio[0].volume=0.65;
    data.obs_state.audio[0].muted=0;
    data.ai_cooldown=30;
    data.cooldown_normal=10;
    data.cooldown_game=5;
    data.cooldown_bruh=15;
    strcpy(data.telegram_url,"https://t.me/channel");
    data.chart.count=3;
    data.chart.viewers[0]=1;data.chart.viewers[1]=6;data.chart.viewers[2]=3;
    data.queued_count=1;
    strcpy(data.queued[0].title,"Трек 1");
    strcpy(data.queued[0].requester,"Viewer");
    data.sound_count=1;
    strcpy(data.sounds[0].command,"мем");
    data.sounds[0].cost=200;
    strcpy(data.obs_state.scene,"Игра");
    strcpy(data.obs_state.scenes[0],"Игра");
    strcpy(data.obs_state.scenes[1],"Перерыв");
    data.leader_count=2;
    strcpy(data.leaders[0].name,"Oleg");
    data.leaders[0].balance=1700;
    strcpy(data.leaders[1].name,"A<Bot>");
    data.leaders[1].balance=500;
    dashboard_publish(&data);
    request(get,buffer,sizeof(buffer));
    assert(strstr(buffer,"200 OK"));
    assert(strstr(buffer,"\"viewers\":8"));
    assert(strstr(buffer,"\"profiles\":17"));
    assert(!strstr(buffer,"access_token"));
    assert(strstr(buffer,"\"obs_connected\":true"));
    assert(strstr(buffer,"\"obs_recording\":1"));
    assert(strstr(buffer,"\"obs_scenes\":["));
    assert(strstr(buffer,"Перерыв"));
    assert(strstr(buffer,"\"leaderboard\":["));
    assert(strstr(buffer,"\"balance\":1700"));
    assert(strstr(buffer,"A<Bot>"));
    assert(strstr(buffer,"\"obs_audio\":["));
    assert(strstr(buffer,"Mic/Aux"));
    assert(strstr(buffer,"\"music_queue\":["));
    assert(strstr(buffer,"Трек 1"));
    assert(strstr(buffer,"\"view_chart\":[1,6,3]"));
    assert(strstr(buffer,"\"telegram_url\":"));
    assert(strstr(buffer,"\"cost\":200"));
    assert(!strstr(buffer,"obs_password"));
    assert(!strstr(buffer,"telegram_token"));

    request(bad_host,buffer,sizeof(buffer));
    assert(strstr(buffer,"403 Forbidden"));
    request(cross_site,buffer,sizeof(buffer));
    assert(strstr(buffer,"403 Forbidden"));
    assert(!dashboard_take_changes(&change));

    request(same_site,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.reports==0 && change.ai==-1 && change.promo==-1);
    assert(!dashboard_take_changes(&change));

    request(scene_change,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.obs_action==1);
    assert(strcmp(change.obs_scene,"Перерыв")==0);
    assert(!dashboard_take_changes(&change));

    request(record_stop,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.obs_action==3);
    assert(!dashboard_take_changes(&change));

    request(req_action_setting_key_promo_min_value_,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.settings_action==1);
    assert(!dashboard_take_changes(&change));
    request(req_action_audio_index_mode_volume_value_,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.audio_action==1);
    assert(!dashboard_take_changes(&change));
    request(req_action_music_mode_clear,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.music_action==2);
    assert(!dashboard_take_changes(&change));
    request(req_action_moment,buffer,sizeof(buffer));
    assert(strstr(buffer,"202 Accepted"));
    assert(dashboard_take_changes(&change));
    assert(change.mark_moment==1);
    assert(!dashboard_take_changes(&change));
    dashboard_stop();
    _chdir("../..");
    remove("tests/.tmp_dashboard/data/dashboard_controls.ini");
    _rmdir("tests/.tmp_dashboard/data");
    _rmdir("tests/.tmp_dashboard");
    puts("dashboard smoke tests passed");
    return 0;
}
