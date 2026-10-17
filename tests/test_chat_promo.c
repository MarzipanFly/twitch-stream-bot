/* Self-contained test:
 * gcc -std=c11 -Wall -Wextra -Iinclude tests/test_chat_promo.c \
 *     src/chat_promo.c -o test_chat_promo && ./test_chat_promo
 */
#include "chat_promo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    ChatPromo promo;
    PromoConfig settings = {1, 5, 10};
    TelegramConfig tg = {0};
    DiscordConfig ds = {0};
    char message[500];
    uint32_t now = 1000U, first_delay;
    int i;

    chat_promo_init(&promo, 12345);
    assert(!chat_promo_due(&promo, &settings, now, 1, 1));
    first_delay = promo.interval_ms;
    assert(first_delay >= 300000U && first_delay <= 600000U);
    assert(!chat_promo_due(&promo, &settings, now + first_delay - 1U, 1, 1));
    assert(!chat_promo_due(&promo, &settings, now + first_delay, 1, 0));
    assert(chat_promo_due(&promo, &settings, now + first_delay, 1, 1));

    chat_promo_schedule_next(&promo, &settings, now + first_delay);
    assert(!chat_promo_due(&promo, &settings,
                           now + first_delay + 1000U, 1, 1));
    assert(!chat_promo_due(&promo, &settings, now + first_delay + 2000U, 0, 1));
    assert(!promo.armed);
    assert(!chat_promo_due(&promo, &settings, now + first_delay + 3000U, 1, 1));

    settings.enabled = 0;
    assert(!chat_promo_due(&promo, &settings, 5000000U, 1, 1));
    assert(!promo.armed);
    settings.enabled = 1;
    assert(!chat_promo_due(&promo, &settings, 6000000U, 1, 1));
    settings.min_minutes = 10;
    settings.max_minutes = 10;
    for (i = 0; i < 8; ++i)
    {
        chat_promo_schedule_next(&promo, &settings, (uint32_t)i * 600000U);
        assert(promo.interval_ms == 600000U);
    }

    assert(!chat_promo_build_message(&tg, &ds, message, sizeof(message)));
    snprintf(tg.channel_url, sizeof(tg.channel_url), "%s",
             "https://t.me/opelsinchyki");
    snprintf(ds.invite_url, sizeof(ds.invite_url), "%s",
             "https://discord.gg/example");
    assert(chat_promo_build_message(&tg, &ds, message, sizeof(message)));
    assert(strstr(message, "https://t.me/opelsinchyki"));
    assert(strstr(message, "https://discord.gg/example"));

    ds.invite_url[0] = '\0';
    assert(chat_promo_build_message(&tg, &ds, message, sizeof(message)));
    assert(strstr(message, "Telegram"));
    assert(!strstr(message, "Discord"));

    snprintf(tg.channel_url, sizeof(tg.channel_url), "%s",
             "https://t.me/bad\nurl");
    assert(!chat_promo_build_message(&tg, &ds, message, sizeof(message)));

    /* Validate uint32 wrap-around without using GetTickCount64. */
    settings.min_minutes = settings.max_minutes = 5;
    chat_promo_init(&promo, 1);
    assert(!chat_promo_due(&promo, &settings, UINT32_MAX - 1000U, 1, 1));
    assert(chat_promo_due(&promo, &settings,
                          (uint32_t)((UINT32_MAX - 1000U) + 300000U), 1, 1));

    puts("chat_promo tests passed");
    return 0;
}
