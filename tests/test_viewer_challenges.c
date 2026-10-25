/* Build from repository root on Windows MinGW:
 * gcc -std=c11 -Wall -Wextra -Iinclude tests/test_viewer_challenges.c \
 *   src/viewer_challenges.c src/viewer_challenge_chat.c -o test_challenges.exe
 * The stubs below avoid touching real data/viewers.dat or requiring OAuth.
 */
#include "viewer_challenges.h"
#include "viewer_challenge_chat.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_local(p) _mkdir(p)
#define chdir_local(p) _chdir(p)
#define rmdir_local(p) _rmdir(p)
#else
#include <unistd.h>
#include <sys/stat.h>
#define mkdir_local(p) mkdir(p,0700)
#define chdir_local(p) chdir(p)
#define rmdir_local(p) rmdir(p)
#endif

static ViewerProfile profiles[15];
static size_t profile_count;
static int fail_saves;

void log_info(const char *fmt, ...) {(void)fmt;}
void log_warning(const char *fmt, ...) {(void)fmt;}
ViewerProfile *viewer_profile_find(const char *id)
{
    size_t i;
    for(i=0;i<profile_count;++i)
        if(!strcmp(profiles[i].user_id,id))return &profiles[i];
    return NULL;
}
ViewerProfile *viewer_profile_get_or_create(const char *id,
    const char *login,const char *name)
{
    ViewerProfile *v=viewer_profile_find(id);
    if(v)return v;
    assert(profile_count<15);
    v=&profiles[profile_count++];
    memset(v,0,sizeof(*v));
    snprintf(v->user_id,sizeof(v->user_id),"%s",id);
    snprintf(v->login,sizeof(v->login),"%s",login);
    snprintf(v->display_name,sizeof(v->display_name),"%s",name);
    v->balance=175;
    return v;
}
int viewer_profile_save(void){return !fail_saves;}
static void request(ViewerChallenges *q,const char *sender,
                    const char *command,char *answer,size_t capacity)
{
    TwitchChatMessage chat={0};
    snprintf(chat.chatter_user_id,sizeof(chat.chatter_user_id),"%s",sender);
    snprintf(chat.chatter_user_login,sizeof(chat.chatter_user_login),
             "viewer%s",sender);
    snprintf(chat.chatter_user_name,sizeof(chat.chatter_user_name),
             "Viewer%s",sender);
    snprintf(chat.text,sizeof(chat.text),"%s",command);
    assert(viewer_challenge_chat_process(q,&chat,"999",'!',answer,capacity)==1);
}
int main(void)
{
    ViewerChallenges q={0};
    ViewerChallenge got;
    ViewerProfile *a,*b;
    char answer[2048],saved_name[160];
    FILE *file;
    int refunded=-1, i;
    assert(mkdir_local("tests/.tmp_challenges")==0);
    assert(chdir_local("tests/.tmp_challenges")==0);
    assert(mkdir_local("data")==0);

    a=viewer_profile_get_or_create("123","alpha","Alpha");
    assert(viewer_challenges_init(&q));
    assert(q.count==0 && q.next_id==1);
    request(&q,"123","!испытание ets2",answer,sizeof(answer));
    assert(q.count==1 && !q.items[0].active);
    assert(a->balance==175); /* No charge before broadcaster acceptance. */
    assert(strstr(answer,"120") && strstr(answer,"ETS2"));
    strcpy(saved_name,q.items[0].name);
    request(&q,"123","!испытание fifa",answer,sizeof(answer));
    assert(q.count==1 && strstr(answer,"уже есть"));
    assert(viewer_challenges_init(&q));
    assert(q.count==1 && !strcmp(q.items[0].name,saved_name));

    request(&q,"123","!челлендж принять",answer,sizeof(answer));
    assert(!q.items[0].active && a->balance==175);
    request(&q,"999","!челлендж принять",answer,sizeof(answer));
    assert(q.items[0].active && q.items[0].paid && a->balance==55);
    assert(strstr(answer,"120"));
    assert(viewer_challenges_init(&q));
    assert(q.items[0].active && q.items[0].paid);

    b=viewer_profile_get_or_create("456","beta","Beta");
    b->balance=300;
    request(&q,"456","!испытание fifa",answer,sizeof(answer));
    assert(q.count==2 && b->balance==300);
    request(&q,"999","!челлендж принять",answer,sizeof(answer));
    assert(q.items[0].id==1 && q.count==2);
    request(&q,"999","!челлендж отклонить",answer,sizeof(answer));
    assert(q.count==1 && a->balance==175 && !q.items[0].active);
    assert(strstr(answer,"возвращена"));

    request(&q,"999","!челлендж принять",answer,sizeof(answer));
    assert(q.items[0].active && b->balance==200);
    request(&q,"999","!челлендж готово",answer,sizeof(answer));
    assert(q.count==0 && b->balance==200);
    assert(viewer_challenges_init(&q));
    assert(q.count==0); /* Persisted complete status. */

    request(&q,"123","!испытание phasma",answer,sizeof(answer));
    assert(q.count==1 && a->balance==175);
    request(&q,"999","!челлендж отклонить",answer,sizeof(answer));
    assert(q.count==0 && a->balance==175); /* Free proposal. */

    /* Insufficient funds AFTER proposing: no charge, proposal intact. */
    request(&q,"123","!испытание ets2",answer,sizeof(answer));
    a->balance=5;
    assert(viewer_challenges_accept(&q,&got)==CHALLENGE_NO_BALANCE);
    assert(q.count==1 && !q.items[0].active && a->balance==5);
    request(&q,"999","!челлендж отклонить",answer,sizeof(answer));
    assert(q.count==0);
    a->balance=175;

    request(&q,"123","!испытание ets2",answer,sizeof(answer));
    fail_saves=1;
    assert(viewer_challenges_accept(&q,&got)==CHALLENGE_IO_ERROR);
    assert(a->balance==175 && !q.items[0].active);
    fail_saves=0;
    request(&q,"999","!челлендж отклонить",answer,sizeof(answer));

    /* No user-supplied raw text or forged Twitch login can control the queue. */
    request(&q,"123","!челлендж отклонить",answer,sizeof(answer));
    assert(strstr(answer,"только стример"));
    request(&q,"123","!испытания",answer,sizeof(answer));
    assert(strstr(answer,"нет"));

    /* At most eight pending, one per Twitch ID. */
    for(i=0;i<8;++i) {
        char id[32];
        ViewerProfile *p;
        snprintf(id,sizeof(id),"%d",1000+i);
        p=viewer_profile_get_or_create(id,id,id);
        assert(viewer_challenges_enqueue(&q,p,CHALLENGE_GAME_FC,0,&got)
               ==CHALLENGE_OK);
    }
    assert(q.count==8);
    {
        ViewerProfile *extra=viewer_profile_get_or_create("2000","extra","Extra");
        assert(viewer_challenges_enqueue(&q,extra,CHALLENGE_GAME_FC,0,&got)
               ==CHALLENGE_FULL);
    }
    /* File corruption is not silently replaced by an empty queue. */
    file=fopen(CHALLENGE_PATH,"wb");assert(file);
    assert(fputs("invalid data\n",file)>0);
    assert(fclose(file)==0);
    assert(!viewer_challenges_init(&q));
    file=fopen(CHALLENGE_PATH,"rb");assert(file);
    assert(fgets(answer,sizeof(answer),file));
    assert(strstr(answer,"invalid data"));
    fclose(file);

    remove(CHALLENGE_PATH);
    remove("data/challenges.tmp");
    assert(rmdir_local("data")==0);
    assert(chdir_local("../..")==0);
    assert(rmdir_local("tests/.tmp_challenges")==0);
    puts("Viewer challenge tests passed");
    return 0;
}
