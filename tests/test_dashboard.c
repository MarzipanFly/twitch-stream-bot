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
    dashboard_publish(&data);
    request(get,buffer,sizeof(buffer));
    assert(strstr(buffer,"200 OK"));
    assert(strstr(buffer,"\"viewers\":8"));
    assert(strstr(buffer,"\"profiles\":17"));
    assert(!strstr(buffer,"access_token"));

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

    dashboard_stop();
    _chdir("../..");
    remove("tests/.tmp_dashboard/data/dashboard_controls.ini");
    _rmdir("tests/.tmp_dashboard/data");
    _rmdir("tests/.tmp_dashboard");
    puts("dashboard smoke tests passed");
    return 0;
}
