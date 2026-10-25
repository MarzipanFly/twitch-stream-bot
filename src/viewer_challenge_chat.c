#include "viewer_challenge_chat.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmd_match(const char *p,const char *command)
{
    size_t n=strlen(command);
    return !strncmp(p,command,n) &&
           (p[n]=='\0'||p[n]==' '||p[n]=='\t');
}
static const char *skip_spaces(const char *p)
{
    while (*p==' ' || *p=='\t')++p;
    return p;
}
static void arg_word(const char *src,char *dest,size_t cap)
{
    size_t i=0;
    src=skip_spaces(src);
    while(*src && *src!=' ' && *src!='\t' && i+1<cap)
        dest[i++]=*src++;
    dest[i]='\0';
}
static void make_task_response(const ViewerChallenge *item,char *out,size_t size)
{
    snprintf(out,size,"#%u, %s: %s",
             item->id,viewer_challenges_game_name(item->game),
             viewer_challenges_task(item->game,item->task));
}
int viewer_challenge_chat_process(ViewerChallenges *q,
    const TwitchChatMessage *chat,const char *owner,
    char prefix,char *out,size_t cap)
{
    const char *p,*args;
    char word[40];
    ViewerChallenge ticket;
    ChallengeResult result;
    ViewerProfile *profile;
    int is_owner,refunded=0;
    if(!chat || !out || !cap || chat->text[0]!=prefix)return 0;
    p=chat->text+1;
    if(cmd_match(p,"испытания")) {
        if(!q)snprintf(out,cap,"Очередь испытаний временно недоступна.");
        else viewer_challenges_overview(q,out,cap);
        return 1;
    }
    if(cmd_match(p,"испытание")) {
        args=skip_spaces(p+strlen("испытание"));
        if(!*args) {
            snprintf(out,cap,"Испытания за 🍊: !испытание ets2 (120), "
                     "!испытание fifa (100), !испытание phasma (150). "
                     "Очередь: !испытания. Списание только при принятии.");
            return 1;
        }
        arg_word(args,word,sizeof(word));
        {
            ChallengeGame game=viewer_challenges_game(word);
            unsigned int task;
            if(game==CHALLENGE_GAME_NONE) {
                snprintf(out,cap,"Выбери игру: ets2, fifa или phasma. "
                         "Пример: !испытание ets2");
                return 1;
            }
            if(!q) {
                snprintf(out,cap,"Очередь испытаний недоступна, данные не изменены.");
                return 1;
            }
            profile=viewer_profile_get_or_create(chat->chatter_user_id,
                      chat->chatter_user_login,chat->chatter_user_name);
            if(!profile) {
                snprintf(out,cap,"Не удалось открыть профиль зрителя.");
                return 1;
            }
            task=q->next_id%4;
            result=viewer_challenges_enqueue(q,profile,game,task,&ticket);
            switch(result) {
                case CHALLENGE_OK: {
                    char desc[420];
                    make_task_response(&ticket,desc,sizeof(desc));
                    snprintf(out,cap,"@%s предложил испытание %s "
                             "Цена %lld 🍊 при принятии. "
                             "Решение за стримером.",
                             ticket.name,desc,viewer_challenges_price(game));
                    log_info("Challenge queued: id=%u game=%d owner=%s",
                             ticket.id,(int)ticket.game,ticket.user_id);
                    break;
                }
                case CHALLENGE_NO_BALANCE:
                    snprintf(out,cap,"@%s, нужно минимум %lld 🍊 "
                             "для испытания %s. Баланс: %lld.",
                             chat->chatter_user_login,
                             viewer_challenges_price(game),
                             viewer_challenges_game_name(game),
                             profile->balance);
                    break;
                case CHALLENGE_DUPLICATE:
                    snprintf(out,cap,"@%s, у тебя уже есть испытание "
                             "в очереди или в работе.",
                             chat->chatter_user_login);
                    break;
                case CHALLENGE_FULL:
                    snprintf(out,cap,"Очередь испытаний заполнена. "
                             "Попробуй после выполнения текущих.");
                    break;
                default:
                    snprintf(out,cap,"Не удалось записать испытание. "
                             "Баланс не изменён.");
                    log_warning("Challenge enqueue failed: code=%d",(int)result);
                    break;
            }
            return 1;
        }
    }
    if(!cmd_match(p,"челлендж"))return 0;
    is_owner=owner && owner[0] && !strcmp(chat->chatter_user_id,owner);
    if(!is_owner) {
        snprintf(out,cap,"Управлять испытаниями может только стример. "
                 "Зрителям доступно !испытание ets2|fifa|phasma.");
        return 1;
    }
    if(!q) {
        snprintf(out,cap,"Очередь испытаний временно недоступна.");
        return 1;
    }
    args=skip_spaces(p+strlen("челлендж"));
    arg_word(args,word,sizeof(word));
    if(!strcmp(word,"принять")) {
        result=viewer_challenges_accept(q,&ticket);
        if(result==CHALLENGE_OK) {
            char desc[420];
            make_task_response(&ticket,desc,sizeof(desc));
            snprintf(out,cap,"Принято %s От @%s списано %lld 🍊. "
                     "После выполнения: !челлендж готово.",
                     desc,ticket.name,viewer_challenges_price(ticket.game));
            log_info("Challenge accepted: id=%u",ticket.id);
        } else if(result==CHALLENGE_ALREADY_ACTIVE)
            snprintf(out,cap,"Уже есть активное испытание. "
                     "!челлендж готово или !челлендж отклонить.");
        else if(result==CHALLENGE_NO_BALANCE)
            snprintf(out,cap,"У автора первого испытания недостаточно 🍊. "
                     "Его можно отклонить.");
        else if(result==CHALLENGE_NO_TASK)
            snprintf(out,cap,"Очередь испытаний пуста.");
        else
            snprintf(out,cap,"Не удалось принять испытание. "
                     "Проверь сохранение данных.");
        return 1;
    }
    if(!strcmp(word,"готово") || !strcmp(word,"выполнено")) {
        result=viewer_challenges_finish(q,&ticket);
        if(result==CHALLENGE_OK) {
            snprintf(out,cap,"Испытание #%u выполнено! "
                     "Спасибо @%s за челлендж. 🍊",
                     ticket.id,ticket.name);
            log_info("Challenge completed: id=%u",ticket.id);
        } else
            snprintf(out,cap,result==CHALLENGE_NO_TASK ?
                     "Сейчас нет принятого испытания." :
                     "Не удалось сохранить завершение испытания.");
        return 1;
    }
    if(!strcmp(word,"отклонить") || !strcmp(word,"отказ")) {
        result=viewer_challenges_decline(q,&ticket,&refunded);
        if(result==CHALLENGE_OK) {
            snprintf(out,cap,"Испытание #%u отклонено. @%s, %s",
                     ticket.id,ticket.name,
                     refunded ? "стоимость возвращена 🍊." :
                                "апельсины не списывались.");
            log_info("Challenge declined: id=%u refunded=%d",ticket.id,refunded);
        } else
            snprintf(out,cap,result==CHALLENGE_NO_TASK ?
                     "Очередь испытаний пуста." :
                     "Не удалось отклонить испытание: ошибка сохранения.");
        return 1;
    }
    if(!strcmp(word,"очередь")) {
        viewer_challenges_overview(q,out,cap);
        return 1;
    }
    snprintf(out,cap,"Команды стримера: !челлендж очередь, "
             "!челлендж принять, !челлендж готово, !челлендж отклонить.");
    return 1;
}
