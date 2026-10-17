#include "chat_promo.h"

#include <stdio.h>
#include <string.h>

/* Use a private PRNG so chat promotions don't disturb game/duel randomness. */
static uint32_t promo_random(ChatPromo *promo)
{
    uint32_t x = promo->random_state;
    if (x == 0) x = UINT32_C(0x9E3779B9);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    promo->random_state = x;
    return x;
}

static uint32_t next_interval_ms(ChatPromo *promo,
                                  const PromoConfig *settings)
{
    unsigned int min_minutes = settings->min_minutes;
    unsigned int max_minutes = settings->max_minutes;
    unsigned int width, minutes;
    /* Configuration is validated at load time. Keep a safe fallback. */
    if (min_minutes < 1 || max_minutes < min_minutes || max_minutes > 60)
    {
        min_minutes = 5;
        max_minutes = 10;
    }
    width = max_minutes - min_minutes + 1;
    minutes = min_minutes + (unsigned int)(promo_random(promo) % width);
    return (uint32_t)(minutes * 60000UL);
}

void chat_promo_init(ChatPromo *promo, uint32_t random_seed)
{
    if (!promo) return;
    memset(promo, 0, sizeof(*promo));
    promo->random_state = random_seed ? random_seed : UINT32_C(0x9E3779B9);
}

int chat_promo_due(ChatPromo *promo,
                   const PromoConfig *settings,
                   uint32_t now_tick,
                   int stream_live,
                   int chat_connected)
{
    if (!promo || !settings) return 0;
    if (!settings->enabled || !stream_live)
    {
        promo->armed = 0;
        return 0;
    }
    if (!promo->armed)
    {
        promo->started_tick = now_tick;
        promo->interval_ms = next_interval_ms(promo, settings);
        promo->armed = 1;
        return 0;
    }
    if (!chat_connected) return 0;
    return (uint32_t)(now_tick - promo->started_tick) >= promo->interval_ms;
}

void chat_promo_schedule_next(ChatPromo *promo,
                              const PromoConfig *settings,
                              uint32_t now_tick)
{
    if (!promo || !settings) return;
    promo->started_tick = now_tick;
    promo->interval_ms = next_interval_ms(promo, settings);
    promo->armed = 1;
}

/* Reject CR/LF and whitespace in URLs to avoid injecting extra chat content. */
static int usable_url(const char *url)
{
    const unsigned char *cursor = (const unsigned char *)url;
    if (!url || !(strncmp(url, "https://", 8) == 0 ||
                  strncmp(url, "http://", 7) == 0))
        return 0;
    while (*cursor)
    {
        if (*cursor <= 32 || *cursor == 127) return 0;
        ++cursor;
    }
    return 1;
}

int chat_promo_build_message(const TelegramConfig *telegram,
                             const DiscordConfig *discord,
                             char *buffer, size_t capacity)
{
    const char *tg = telegram && usable_url(telegram->channel_url)
        ? telegram->channel_url : NULL;
    const char *ds = discord && usable_url(discord->invite_url)
        ? discord->invite_url : NULL;
    int written;
    if (!buffer || capacity == 0) return 0;
    buffer[0] = '\0';
    if (!tg && !ds) return 0;
    if (tg && ds)
        written = snprintf(buffer, capacity,
                           "🍊 Присоединяйся к нашим! Telegram: %s | Discord: %s",
                           tg, ds);
    else if (tg)
        written = snprintf(buffer, capacity,
                           "🍊 Наш Telegram-канал: %s",
                           tg);
    else
        written = snprintf(buffer, capacity,
                           "🍊 Наш Discord-сервер: %s",
                           ds);

    /* Twitch chat message limit is 500 characters; stay under 500 UTF-8 bytes. */
    if (written < 0 || (size_t)written >= capacity || written > 495)
    {
        buffer[0] = '\0';
        return 0;
    }
    return 1;
}
