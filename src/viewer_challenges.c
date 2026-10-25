#include "viewer_challenges.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define CHALLENGE_TEMP "data/challenges.tmp"
#define CHALLENGE_LINE 520

/* Curated, version-agnostic challenges: no dangerous or harassing tasks. */
static const char *g_ets2[] = {
    "Доставь один груз без использования GPS (остановки разрешены).",
    "Припаркуй прицеп вручную на ближайшей доставке.",
    "Заверши следующий рейс без штрафов и столкновений.",
    "Проедь следующий маршрут без переключения на вид от третьего лица."
};
static const char *g_fc[] = {
    "Сыграй следующий матч со схемой 4-3-3.",
    "Попробуй забить гол после атаки через фланг.",
    "Сыграй первый тайм без использования высокого прессинга.",
    "На следующий матч выбери 4-2-3-1 и попробуй забить через комбинацию."
};
static const char *g_phasmo[] = {
    "На следующем расследовании первым проверь температуру.",
    "Попробуй определить призрака до заполнения всех доступных улик.",
    "На следующем расследовании назначь роли участникам команды.",
    "В следующем расследовании собери хотя бы одно фото для журнала."
};
/* Fixed reward costs: no random betting and no guaranteed success. */
long long viewer_challenges_price(ChallengeGame game)
{
    switch (game) {
        case CHALLENGE_GAME_ETS2: return 120;
        case CHALLENGE_GAME_FC: return 100;
        case CHALLENGE_GAME_PHASMO: return 150;
        default: return 0;
    }
}
ChallengeGame viewer_challenges_game(const char *text)
{
    if (!text) return CHALLENGE_GAME_NONE;
    if (!strcmp(text, "ets2") || !strcmp(text, "ets") ||
        !strcmp(text, "етс2")) return CHALLENGE_GAME_ETS2;
    if (!strcmp(text, "fifa") || !strcmp(text, "fc") ||
        !strcmp(text, "fc26") || !strcmp(text, "фифа")) return CHALLENGE_GAME_FC;
    if (!strcmp(text, "phasma") || !strcmp(text, "phasmophobia") ||
        !strcmp(text, "фазма") || !strcmp(text, "фазмо")) return CHALLENGE_GAME_PHASMO;
    return CHALLENGE_GAME_NONE;
}
const char *viewer_challenges_game_name(ChallengeGame game)
{
    switch (game) {
        case CHALLENGE_GAME_ETS2: return "ETS2";
        case CHALLENGE_GAME_FC: return "FIFA/FC";
        case CHALLENGE_GAME_PHASMO: return "Phasmophobia";
        default: return "неизвестная игра";
    }
}
const char *viewer_challenges_task(ChallengeGame game, unsigned int index)
{
    if (index >= 4) return NULL;
    switch (game) {
        case CHALLENGE_GAME_ETS2: return g_ets2[index];
        case CHALLENGE_GAME_FC: return g_fc[index];
        case CHALLENGE_GAME_PHASMO: return g_phasmo[index];
        default: return NULL;
    }
}
static int valid_id(const char *id)
{
    size_t i;
    if (!id || !*id || strlen(id) >= VIEWER_PROFILE_ID_SIZE) return 0;
    for (i=0; id[i]; ++i) if (id[i]<'0' || id[i]>'9') return 0;
    return 1;
}
static int name_ok(const char *name)
{
    size_t i;
    if (!name || !*name || strlen(name) >= VIEWER_PROFILE_NAME_SIZE) return 0;
    for (i=0; name[i]; ++i)
        if (name[i]=='\t' || name[i]=='\r' || name[i]=='\n' ||
            (unsigned char)name[i]<32) return 0;
    return 1;
}
static void safe_name(char *dst,size_t cap,const char *name)
{
    size_t i=0;
    if (!name) name="Зритель";
    while (*name && i+1<cap) {
        unsigned char ch=(unsigned char)*name++;
        dst[i++]=(ch<32 || ch==127 || ch=='|' || ch=='\t')?'_':(char)ch;
    }
    if (i==0 && cap>8) {
        memcpy(dst,"Viewer",7); return;
    }
    dst[i]='\0';
}
static int replace_file(void)
{
#ifdef _WIN32
    if (!MoveFileExA(CHALLENGE_TEMP,CHALLENGE_PATH,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return 0;
#else
    if (rename(CHALLENGE_TEMP,CHALLENGE_PATH) != 0) return 0;
#endif
    return 1;
}
int viewer_challenges_save(const ViewerChallenges *q)
{
    FILE *f;
    size_t i;
    int ok;
    if (!q || q->count > CHALLENGE_ITEMS_MAX) return 0;
    f=fopen(CHALLENGE_TEMP,"wb");
    if (!f) return 0;
    ok=fprintf(f,"APELSIN_CHALLENGES_V1\t%u\n",q->next_id)>0;
    for(i=0;i<q->count && ok;++i) {
        const ViewerChallenge *a=&q->items[i];
        if (!valid_id(a->user_id) || !name_ok(a->name) ||
            !viewer_challenges_task(a->game,a->task) ||
            (a->active!=0 && a->active!=1) ||
            (a->paid!=0 && a->paid!=1) ||
            (a->active!=a->paid)) {ok=0; break;}
        ok=fprintf(f,"%u\t%d\t%u\t%d\t%d\t%s\t%s\n",
                    a->id,(int)a->game,a->task,a->active,a->paid,
                    a->user_id,a->name)>0;
    }
    if (fclose(f)!=0) ok=0;
    if (!ok || !replace_file()) {
        remove(CHALLENGE_TEMP);
        return 0;
    }
    return 1;
}
int viewer_challenges_init(ViewerChallenges *q)
{
    FILE *f; char line[CHALLENGE_LINE];
    unsigned int declared_id = 0;
    int saw_active=0;
    if (!q) return 0;
    memset(q,0,sizeof(*q));
    q->next_id=1;
    f=fopen(CHALLENGE_PATH,"rb");
    if(!f) return 1; /* New queue. Viewer balance untouched. */
    if(!fgets(line,sizeof(line),f) ||
       sscanf(line,"APELSIN_CHALLENGES_V1\t%u",&declared_id)!=1 ||
       declared_id==0) {fclose(f);return 0;}
    q->next_id=declared_id;
    while(fgets(line,sizeof(line),f)) {
        ViewerChallenge item={0};
        int game,active,paid;
        size_t i;
        if(q->count>=CHALLENGE_ITEMS_MAX ||
           sscanf(line,"%u\t%d\t%u\t%d\t%d\t%63[^\t]\t%127[^\r\n]",
                  &item.id,&game,&item.task,&active,&paid,
                  item.user_id,item.name)!=7 ||
           game<1 || game>3 ||
           !viewer_challenges_task((ChallengeGame)game,item.task) ||
           !valid_id(item.user_id) || !name_ok(item.name) ||
           (active!=0 && active!=1) || active!=paid ||
           (active && (saw_active || q->count!=0)) ||
           item.id>=q->next_id) {
            fclose(f); memset(q,0,sizeof(*q));q->next_id=1;return 0;
        }
        for (i=0;i<q->count;++i)
            if (!strcmp(q->items[i].user_id,item.user_id) || q->items[i].id==item.id) {
                fclose(f);memset(q,0,sizeof(*q));q->next_id=1;return 0;
            }
        item.game=(ChallengeGame)game;
        item.active=active;
        item.paid=paid;
        if(active)saw_active=1;
        q->items[q->count++]=item;
    }
    if(ferror(f)){fclose(f);memset(q,0,sizeof(*q));q->next_id=1;return 0;}
    fclose(f);
    return 1;
}
ChallengeResult viewer_challenges_enqueue(ViewerChallenges *q,
        ViewerProfile *viewer,ChallengeGame game,unsigned int task,
        ViewerChallenge *chosen)
{
    ViewerChallenges previous; ViewerChallenge *item; size_t i;
    if(!q || !viewer || !valid_id(viewer->user_id) ||
       !viewer_challenges_task(game,task)) return CHALLENGE_ERROR;
    if(q->count >= CHALLENGE_ITEMS_MAX || 
       (q->count==CHALLENGE_QUEUE_MAX && !q->items[0].active))
        return CHALLENGE_FULL;
    for (i=0;i<q->count;++i)
        if (!strcmp(q->items[i].user_id,viewer->user_id)) return CHALLENGE_DUPLICATE;
    if(viewer->balance < viewer_challenges_price(game))return CHALLENGE_NO_BALANCE;
    if (q->next_id == UINT_MAX) return CHALLENGE_FULL;
    previous=*q;
    item=&q->items[q->count++];
    memset(item,0,sizeof(*item));
    item->id=q->next_id++;
    item->game=game;
    item->task=task;
    snprintf(item->user_id,sizeof(item->user_id),"%s",viewer->user_id);
    safe_name(item->name,sizeof(item->name),viewer->display_name[0]?
              viewer->display_name:viewer->login);
    if(!viewer_challenges_save(q)){*q=previous;return CHALLENGE_IO_ERROR;}
    if(chosen)*chosen=*item;
    return CHALLENGE_OK;
}
ChallengeResult viewer_challenges_accept(ViewerChallenges *q,ViewerChallenge *selected)
{
    ViewerChallenges previous;
    ViewerProfile *profile;
    ViewerChallenge *head;
    long long cost, old_balance;
    if(!q || !q->count)return CHALLENGE_NO_TASK;
    if(q->items[0].active)return CHALLENGE_ALREADY_ACTIVE;
    head=&q->items[0];
    profile=viewer_profile_find(head->user_id);
    if(!profile)return CHALLENGE_ERROR;
    cost=viewer_challenges_price(head->game);
    if(profile->balance<cost)return CHALLENGE_NO_BALANCE;
    old_balance=profile->balance;
    previous=*q;
    /* Persist escrow before activating a challenge. Compensate on failure. */
    profile->balance-=cost;
    if(!viewer_profile_save()){profile->balance=old_balance;return CHALLENGE_IO_ERROR;}
    head->paid=1;head->active=1;
    if(!viewer_challenges_save(q)) {
        *q=previous;
        profile->balance=old_balance;
        if(!viewer_profile_save())return CHALLENGE_IO_ERROR; 
        return CHALLENGE_IO_ERROR;
    }
    if(selected)*selected=q->items[0];
    return CHALLENGE_OK;
}
static void pop_front(ViewerChallenges *q)
{
    if(q->count>1)
        memmove(q->items,q->items+1,sizeof(q->items[0])*(q->count-1));
    --q->count;
    memset(&q->items[q->count],0,sizeof(q->items[0]));
}
ChallengeResult viewer_challenges_finish(ViewerChallenges *q,ViewerChallenge *finished)
{
    ViewerChallenges before;
    if(!q || !q->count || !q->items[0].active) return CHALLENGE_NO_TASK;
    before=*q;
    if(finished)*finished=q->items[0];
    pop_front(q);
    if(!viewer_challenges_save(q)){*q=before;return CHALLENGE_IO_ERROR;}
    return CHALLENGE_OK;
}
ChallengeResult viewer_challenges_decline(ViewerChallenges *q,ViewerChallenge *declined,int *refunded)
{
    ViewerChallenges before;
    ViewerProfile *profile=NULL;
    long long balance=0,cost=0;
    int paid;
    if(refunded)*refunded=0;
    if(!q || !q->count)return CHALLENGE_NO_TASK;
    before=*q;
    if(declined)*declined=q->items[0];
    paid=q->items[0].paid;
    if(paid) {
        profile=viewer_profile_find(q->items[0].user_id);
        if(!profile)return CHALLENGE_ERROR;
        cost=viewer_challenges_price(q->items[0].game);
        if(profile->balance>LLONG_MAX-cost)return CHALLENGE_ERROR;
        balance=profile->balance;
        profile->balance+=cost;
        if(!viewer_profile_save()){profile->balance=balance;return CHALLENGE_IO_ERROR;}
    }
    pop_front(q);
    if(!viewer_challenges_save(q)) {
        *q=before;
        if(paid) {
            profile->balance=balance;
            (void)viewer_profile_save();
        }
        return CHALLENGE_IO_ERROR;
    }
    if(refunded)*refunded=paid;
    return CHALLENGE_OK;
}
void viewer_challenges_overview(const ViewerChallenges *q,char *out,size_t cap)
{
    size_t pending;
    if(!out || !cap)return;
    if(!q || !q->count){snprintf(out,cap,"Испытаний пока нет. !испытание ets2 | fifa | phasma");return;}
    pending=q->count-(q->items[0].active?1:0);
    if(q->items[0].active) {
        snprintf(out,cap,"Активное #%u (%s): %s От @%s. В очереди: %u.",
                 q->items[0].id,viewer_challenges_game_name(q->items[0].game),
                 viewer_challenges_task(q->items[0].game,q->items[0].task),
                 q->items[0].name,(unsigned int)pending);
    } else {
        snprintf(out,cap,"Следующее #%u (%s): %s От @%s. Ожидают: %u.",
                 q->items[0].id,viewer_challenges_game_name(q->items[0].game),
                 viewer_challenges_task(q->items[0].game,q->items[0].task),
                 q->items[0].name,(unsigned int)pending);
    }
}
