#ifndef CHAT_PROMO_H
#define CHAT_PROMO_H

#include <stddef.h>
#include <stdint.h>
#include "config.h"

/* The scheduler runs in the existing EventSub loop, not in another thread. */
typedef struct
{
    uint32_t started_tick;
    uint32_t interval_ms;
    uint32_t random_state;
    int armed;
} ChatPromo;

void chat_promo_init(ChatPromo *promo, uint32_t random_seed);

/* True at most once per scheduled interval. Offline resets the schedule.
 * If the chat is disconnected, a due message waits for reconnection. */
int chat_promo_due(ChatPromo *promo,
                   const PromoConfig *settings,
                   uint32_t now_tick,
                   int stream_live,
                   int chat_connected);

/* Rearm after one attempt (successful or failed), preventing floods on errors. */
void chat_promo_schedule_next(ChatPromo *promo,
                              const PromoConfig *settings,
                              uint32_t now_tick);

/* Build one Twitch chat message from configured TG/Discord links.
 * Missing links are omitted; when both are missing nothing is posted. */
int chat_promo_build_message(const TelegramConfig *telegram,
                             const DiscordConfig *discord,
                             char *buffer, size_t capacity);

#endif
