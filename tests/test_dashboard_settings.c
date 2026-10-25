/* Standalone Windows smoke test:
 * gcc -std=c11 -Iinclude tests/test_dashboard_settings.c \
 *   src/dashboard_settings.c src/logger.c -lcrypt32 -o test_settings.exe
 */
#include "dashboard_settings.h"
#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
int main(void)
{
    AppConfig cfg={0},loaded={0};
    FILE *f;
    char content[3000]={0};
    _mkdir("tests/.tmp_settings");
    assert(_chdir("tests/.tmp_settings")==0);
    _mkdir("data");
    cfg.promo.min_minutes=5;
    cfg.promo.max_minutes=10;
    cfg.ai.cooldown_seconds=30;
    cfg.cooldowns.normal_seconds=10;
    cfg.cooldowns.game_seconds=5;
    cfg.cooldowns.bruh_seconds=15;
    cfg.sound_count=1;
    strcpy(cfg.sounds[0].command,"meme");
    strcpy(cfg.sounds[0].input,"MemeSound");
    assert(dashboard_settings_apply(&cfg,"promo_min","6"));
    assert(!dashboard_settings_apply(&cfg,"promo_min","70"));
    assert(!dashboard_settings_apply(&cfg,"discord_url","http://evil.com/"));
    assert(dashboard_settings_apply(&cfg,"telegram_url","https://t.me/my_channel"));
    assert(dashboard_settings_apply(&cfg,"discord_url","https://discord.gg/example"));
    assert(dashboard_settings_apply(&cfg,"ai_cooldown","60"));
    assert(dashboard_settings_apply(&cfg,"normal_cooldown","20"));
    assert(dashboard_settings_apply(&cfg,"sound_cost_0","350"));
    assert(dashboard_settings_apply(&cfg,"sound_cooldown_0","40"));
    assert(!dashboard_settings_apply(&cfg,"sound_cost_1","7"));
    assert(dashboard_settings_apply(&cfg,"obs_password","example-private-obs-pass"));
    assert(dashboard_settings_apply(&cfg,"telegram_token","sample-private-telegram-token"));
    loaded.promo.min_minutes=5;loaded.promo.max_minutes=10;
    loaded.ai.cooldown_seconds=30;loaded.sound_count=1;
    dashboard_settings_load(&loaded);
    assert(loaded.promo.min_minutes==6);
    assert(loaded.ai.cooldown_seconds==60);
    assert(loaded.cooldowns.normal_seconds==20);
    assert(loaded.sounds[0].cost==350);
    assert(loaded.sounds[0].cooldown_seconds==40);
    assert(strcmp(loaded.obs.password,"example-private-obs-pass")==0);
    assert(strcmp(loaded.telegram.bot_token,"sample-private-telegram-token")==0);
    f=fopen("data/dashboard_secrets.dat","rb");assert(f);
    {
      size_t size=fread(content,1,sizeof(content)-1,f);content[size]='\0';
    }
    fclose(f);
    assert(!strstr(content,"example-private-obs-pass"));
    assert(!strstr(content,"sample-private-telegram-token"));
    remove("data/dashboard_settings.ini");
    remove("data/dashboard_secrets.dat");
    _rmdir("data");
    _chdir("../..");
    _rmdir("tests/.tmp_settings");
    puts("dashboard settings tests passed");
    return 0;
}
