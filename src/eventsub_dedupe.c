#include "eventsub_dedupe.h"

#include <string.h>

int eventsub_dedupe_accept(EventSubDedupe *guard,
                           const char *message_id,
                           unsigned long long now_seconds)
{
    size_t i, length;
    if (!guard)
        return 0;
    if (!message_id || !message_id[0] || !now_seconds)
    {
        ++guard->invalid;
        return 0;
    }
    /* Bound the read without relying on a terminated external buffer. */
    for (length = 0; length < EVENTSUB_DEDUPE_ID_BYTES; ++length)
        if (!message_id[length])
            break;
    if (length == 0 || length == EVENTSUB_DEDUPE_ID_BYTES)
    {
        ++guard->invalid;
        return 0;
    }

    for (i = 0; i < EVENTSUB_DEDUPE_CAPACITY; ++i)
    {
        const EventSubDedupeEntry *entry = &guard->entries[i];
        if (!entry->id[0])
            continue;
        if (now_seconds >= entry->timestamp &&
            now_seconds - entry->timestamp >= EVENTSUB_DEDUPE_TTL_SECONDS)
            continue;
        /* Clock rollback: keep the old ID protected, fail closed. */
        if (strcmp(entry->id, message_id) == 0)
        {
            ++guard->duplicates;
            return 0;
        }
    }

    {
        EventSubDedupeEntry *slot = &guard->entries[guard->next];
        memcpy(slot->id, message_id, length);
        slot->id[length] = '\0';
        slot->timestamp = now_seconds;
    }
    guard->next = (guard->next + 1U) % EVENTSUB_DEDUPE_CAPACITY;
    ++guard->accepted;
    return 1;
}
