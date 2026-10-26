#include "dashboard_settings.h"
#include "logger.h"

#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_PATH "data/dashboard_settings.ini"
#define SETTINGS_TEMP "data/dashboard_settings.tmp"
#define SECRETS_PATH "data/dashboard_secrets.dat"
#define SECRETS_TEMP "data/dashboard_secrets.tmp"
#define SETTINGS_LINE 700

static int validate_url(const char *text, const char *prefix, size_t max)
{
    size_t i, len = strlen(text);
    if (len >= max || strncmp(text, prefix, strlen(prefix))) return 0;
    for (i = 0; i < len; ++i)
        if ((unsigned char)text[i] <= 32 || text[i] == 127 ||
            text[i] == '"' || text[i] == '\\') return 0;
    return 1;
}
static int safe_text(const char *v, size_t max)
{
    size_t i, len;
    if (!v) return 0;
    len = strlen(v);
    if (len >= max) return 0;
    for (i=0;i<len;++i)
        if ((unsigned char)v[i] < 32 || (unsigned char)v[i]==127 ||
            v[i]=='=' || v[i]=='[' || v[i]==']') return 0;
    return 1;
}
static int integer(const char *s, long long min, long long max, long long *value)
{
    char *end;
    long long x;
    if (!s || !*s) return 0;
    x = _strtoi64(s, &end, 10);
    if (*end || end == s || x < min || x > max) return 0;
    *value=x; return 1;
}
static int set_plain(AppConfig *cfg, const char *key, const char *value)
{
    long long x;
    size_t i;
    if (!strcmp(key,"telegram_url"))
    {
        if (!validate_url(value,"https://t.me/",sizeof(cfg->telegram.channel_url)))
            return 0;
        snprintf(cfg->telegram.channel_url,sizeof(cfg->telegram.channel_url),"%s",value);
    }
    else if (!strcmp(key,"discord_url"))
    {
        if (!validate_url(value,"https://discord.com/invite/",sizeof(cfg->discord.invite_url)) &&
            !validate_url(value,"https://discord.gg/",sizeof(cfg->discord.invite_url)))
            return 0;
        snprintf(cfg->discord.invite_url,sizeof(cfg->discord.invite_url),"%s",value);
    }
    else if (!strcmp(key,"telegram_chat_id"))
    {
        if (!safe_text(value,sizeof(cfg->telegram.chat_id))) return 0;
        snprintf(cfg->telegram.chat_id,sizeof(cfg->telegram.chat_id),"%s",value);
    }
    else if (!strcmp(key,"obs_enabled") ||
             !strcmp(key,"category_sync_enabled") ||
             !strcmp(key,"director_enabled") ||
             !strcmp(key,"director_auto_game") ||
             !strcmp(key,"director_auto_chat"))
    {
        if (!integer(value,0,1,&x)) return 0;
        if (!strcmp(key,"obs_enabled")) cfg->obs.enabled=(int)x;
        else if (!strcmp(key,"category_sync_enabled")) cfg->category_sync.enabled=(int)x;
        else if (!strcmp(key,"director_enabled")) cfg->obs_director.enabled=(int)x;
        else if (!strcmp(key,"director_auto_game")) cfg->obs_director.auto_game_scene=(int)x;
        else cfg->obs_director.auto_chat_scene=(int)x;
    }
    else if (!strcmp(key,"category_chat_scene") ||
             !strcmp(key,"category_game_scene") ||
             !strcmp(key,"category_chat_category") ||
             !strcmp(key,"director_game_scene") ||
             !strcmp(key,"director_chat_scene") ||
             !strcmp(key,"director_break_scene"))
    {
        char *destination=NULL;
        size_t capacity=0;
        if (!safe_text(value,CONFIG_STRING_SIZE) || !value[0]) return 0;
        if (!strcmp(key,"category_chat_scene"))
        { destination=cfg->category_sync.chat_scene; capacity=sizeof(cfg->category_sync.chat_scene); }
        else if (!strcmp(key,"category_game_scene"))
        { destination=cfg->category_sync.game_scene; capacity=sizeof(cfg->category_sync.game_scene); }
        else if (!strcmp(key,"category_chat_category"))
        { destination=cfg->category_sync.chat_category; capacity=sizeof(cfg->category_sync.chat_category); }
        else if (!strcmp(key,"director_game_scene"))
        { destination=cfg->obs_director.game_scene; capacity=sizeof(cfg->obs_director.game_scene); }
        else if (!strcmp(key,"director_chat_scene"))
        { destination=cfg->obs_director.chat_scene; capacity=sizeof(cfg->obs_director.chat_scene); }
        else
        { destination=cfg->obs_director.break_scene; capacity=sizeof(cfg->obs_director.break_scene); }
        snprintf(destination,capacity,"%s",value);
    }
    else if (!strcmp(key,"category_poll_seconds") ||
             !strcmp(key,"director_start_delay") ||
             !strcmp(key,"director_stop_delay") ||
             !strcmp(key,"director_manual_override") ||
             !strcmp(key,"director_poll_seconds"))
    {
        long long minimum=0,maximum=600;
        if (!strcmp(key,"category_poll_seconds")) { minimum=2; maximum=60; }
        else if (!strcmp(key,"director_start_delay")) maximum=120;
        else if (!strcmp(key,"director_stop_delay")) maximum=300;
        else if (!strcmp(key,"director_poll_seconds")) { minimum=1; maximum=30; }
        if (!integer(value,minimum,maximum,&x)) return 0;
        if (!strcmp(key,"category_poll_seconds")) cfg->category_sync.poll_seconds=(unsigned int)x;
        else if (!strcmp(key,"director_start_delay")) cfg->obs_director.game_start_delay_seconds=(unsigned int)x;
        else if (!strcmp(key,"director_stop_delay")) cfg->obs_director.game_stop_delay_seconds=(unsigned int)x;
        else if (!strcmp(key,"director_manual_override")) cfg->obs_director.manual_override_seconds=(unsigned int)x;
        else cfg->obs_director.poll_seconds=(unsigned int)x;
    }
    else if (!strcmp(key,"promo_min"))
    {
        if (!integer(value,1,60,&x) || x>cfg->promo.max_minutes) return 0;
        cfg->promo.min_minutes=(unsigned int)x;
    }
    else if (!strcmp(key,"promo_max"))
    {
        if (!integer(value,1,60,&x) || x<cfg->promo.min_minutes) return 0;
        cfg->promo.max_minutes=(unsigned int)x;
    }
    else if (!strcmp(key,"ai_cooldown"))
    {
        if (!integer(value,5,300,&x)) return 0;
        cfg->ai.cooldown_seconds=(unsigned int)x;
    }
    else if (!strcmp(key,"normal_cooldown") ||
             !strcmp(key,"game_cooldown") ||
             !strcmp(key,"bruh_cooldown"))
    {
        if (!integer(value,0,300,&x)) return 0;
        if (!strcmp(key,"normal_cooldown")) cfg->cooldowns.normal_seconds=(unsigned int)x;
        else if (!strcmp(key,"game_cooldown")) cfg->cooldowns.game_seconds=(unsigned int)x;
        else cfg->cooldowns.bruh_seconds=(unsigned int)x;
    }
    else if (strncmp(key,"sound_cost_",11)==0 ||
             strncmp(key,"sound_cooldown_",15)==0)
    {
        int cost = strncmp(key,"sound_cost_",11)==0;
        const char *index_str=key + (cost?11:15);
        if (!integer(index_str,0,SOUND_MAX_COUNT-1,&x)) return 0;
        i=(size_t)x;
        if (i>=cfg->sound_count) return 0;
        if (!integer(value,0,cost?1000000000LL:3600,&x)) return 0;
        if (cost) cfg->sounds[i].cost=x;
        else cfg->sounds[i].cooldown_seconds=(unsigned int)x;
    }
    else return 0;
    return 1;
}
static int save_plain(const AppConfig *cfg)
{
    FILE *f=fopen(SETTINGS_TEMP,"wb");
    size_t i;
    int ok=1;
    if (!f) return 0;
    if (fprintf(f,"telegram_url=%s\ndiscord_url=%s\ntelegram_chat_id=%s\n"
                "promo_min=%u\npromo_max=%u\nai_cooldown=%u\n"
                "normal_cooldown=%u\ngame_cooldown=%u\nbruh_cooldown=%u\n"
                "obs_enabled=%d\ncategory_sync_enabled=%d\ndirector_enabled=%d\n"
                "director_auto_game=%d\ndirector_auto_chat=%d\n"
                "category_chat_scene=%s\ncategory_game_scene=%s\ncategory_chat_category=%s\n"
                "category_poll_seconds=%u\ndirector_game_scene=%s\ndirector_chat_scene=%s\n"
                "director_break_scene=%s\ndirector_start_delay=%u\ndirector_stop_delay=%u\n"
                "director_manual_override=%u\ndirector_poll_seconds=%u\n",
                cfg->telegram.channel_url,cfg->discord.invite_url,
                cfg->telegram.chat_id,cfg->promo.min_minutes,
                cfg->promo.max_minutes,cfg->ai.cooldown_seconds,
                cfg->cooldowns.normal_seconds,cfg->cooldowns.game_seconds,
                cfg->cooldowns.bruh_seconds,cfg->obs.enabled,
                cfg->category_sync.enabled,cfg->obs_director.enabled,
                cfg->obs_director.auto_game_scene,cfg->obs_director.auto_chat_scene,
                cfg->category_sync.chat_scene,cfg->category_sync.game_scene,
                cfg->category_sync.chat_category,cfg->category_sync.poll_seconds,
                cfg->obs_director.game_scene,cfg->obs_director.chat_scene,
                cfg->obs_director.break_scene,cfg->obs_director.game_start_delay_seconds,
                cfg->obs_director.game_stop_delay_seconds,
                cfg->obs_director.manual_override_seconds,
                cfg->obs_director.poll_seconds)<0) ok=0;
    for(i=0;i<cfg->sound_count;++i)
    {
        if (fprintf(f,"sound_cost_%u=%lld\nsound_cooldown_%u=%u\n",
                    (unsigned)i,cfg->sounds[i].cost,(unsigned)i,
                    cfg->sounds[i].cooldown_seconds)<0) ok=0;
    }
    if (fclose(f)!=0)ok=0;
    if (!ok || !MoveFileExA(SETTINGS_TEMP,SETTINGS_PATH,
                            MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(SETTINGS_TEMP);
        return 0;
    }
    return 1;
}
/* Single encrypted structure, no secret ever returned by an HTTP GET. */
typedef struct {char obs_password[CONFIG_STRING_SIZE];
                char telegram_token[CONFIG_STRING_SIZE];} SecretValues;
static int crypt_save(const SecretValues *values)
{
    DATA_BLOB input,output={0};
    FILE *f;
    int ok=0;
    input.pbData=(BYTE*)values;
    input.cbData=sizeof(*values);
    if (!CryptProtectData(&input,L"ApelsinBot Dashboard",NULL,NULL,NULL,
                          CRYPTPROTECT_UI_FORBIDDEN,&output))return 0;
    f=fopen(SECRETS_TEMP,"wb");
    if (f)
    {
        int wrote = fwrite(output.pbData,1,output.cbData,f)==output.cbData;
        int closed = fclose(f)==0;
        f=NULL;
        if (wrote && closed)
            ok=MoveFileExA(SECRETS_TEMP,SECRETS_PATH,
                           MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    }
    if(!ok)DeleteFileA(SECRETS_TEMP);
    if(output.pbData){SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);}
    return ok;
}
static int crypt_load(SecretValues *values)
{
    BYTE raw[4096];
    FILE *f=fopen(SECRETS_PATH,"rb");
    size_t count;
    DATA_BLOB input, output={0};
    int ok=0;
    if (!f)return 0;
    count=fread(raw,1,sizeof(raw),f);
    fclose(f);
    if(!count || count>=sizeof(raw))goto done;
    input.pbData=raw; input.cbData=(DWORD)count;
    if(!CryptUnprotectData(&input,NULL,NULL,NULL,NULL,
                            CRYPTPROTECT_UI_FORBIDDEN,&output))goto done;
    if(output.cbData==sizeof(*values))
    {
        memcpy(values,output.pbData,sizeof(*values));
        values->obs_password[CONFIG_STRING_SIZE-1]='\0';
        values->telegram_token[CONFIG_STRING_SIZE-1]='\0';
        ok=1;
    }
done:
    SecureZeroMemory(raw,sizeof(raw));
    if(output.pbData){SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);}
    return ok;
}
void dashboard_settings_load(AppConfig *cfg)
{
    char line[SETTINGS_LINE];
    FILE *f;
    SecretValues values={0};
    if(!cfg)return;
    f=fopen(SETTINGS_PATH,"rb");
    if(f)
    {
        while(fgets(line,sizeof(line),f))
        {
            char *sep, *val;
            if(!strchr(line,'\n')&&!feof(f))
            {
                int c;
                while((c=fgetc(f))!=EOF && c!='\n'){}
                continue;
            }
            line[strcspn(line,"\r\n")]='\0';
            sep=strchr(line,'=');
            if(!sep)continue;
            *sep='\0';val=sep+1;
            if(*val)set_plain(cfg,line,val);
        }
        fclose(f);
    }
    if(crypt_load(&values))
    {
        if(values.obs_password[0])snprintf(cfg->obs.password,
            sizeof(cfg->obs.password),"%s",values.obs_password);
        if(values.telegram_token[0])snprintf(cfg->telegram.bot_token,
            sizeof(cfg->telegram.bot_token),"%s",values.telegram_token);
    }
    SecureZeroMemory(&values,sizeof(values));
}
int dashboard_settings_apply(AppConfig *cfg, const char *key, const char *value)
{
    AppConfig *copy;
    int ok=0;
    if(!cfg||!key||!value)return 0;
    if(!strcmp(key,"obs_password") || !strcmp(key,"telegram_token"))
    {
        SecretValues secret={0};
        if(!*value || !safe_text(value,CONFIG_STRING_SIZE))return 0;
        /* Preserve the other secret and ensure saving works before changes. */
        (void)crypt_load(&secret);
        if(!strcmp(key,"obs_password"))
            snprintf(secret.obs_password,sizeof(secret.obs_password),"%s",value);
        else
            snprintf(secret.telegram_token,sizeof(secret.telegram_token),"%s",value);
        ok=crypt_save(&secret);
        SecureZeroMemory(&secret,sizeof(secret));
        if(ok)
        {
            if(!strcmp(key,"obs_password"))
                snprintf(cfg->obs.password,sizeof(cfg->obs.password),"%s",value);
            else
                snprintf(cfg->telegram.bot_token,sizeof(cfg->telegram.bot_token),"%s",value);
        }
        return ok;
    }
    copy=(AppConfig*)malloc(sizeof(*copy));
    if(!copy)return 0;
    memcpy(copy,cfg,sizeof(*copy));
    if(set_plain(copy,key,value)&&save_plain(copy))
    {
        *cfg=*copy;
        ok=1;
    }
    free(copy);
    return ok;
}
