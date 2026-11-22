#ifndef EVENTSUB_DEDUPE_H
#define EVENTSUB_DEDUPE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2048 recent EventSub IDs; 10-minute replay window.
 * Process events on one thread. This is a bounded in-memory safety layer,
 * not durable cross-restart exactly-once delivery. */
#define EVENTSUB_DEDUPE_CAPACITY 2048
#define EVENTSUB_DEDUPE_ID_BYTES 128
#define EVENTSUB_DEDUPE_TTL_SECONDS 600ULL

typedef struct
{
    char id[EVENTSUB_DEDUPE_ID_BYTES];
    unsigned long long timestamp;
} EventSubDedupeEntry;

typedef struct
{
    EventSubDedupeEntry entries[EVENTSUB_DEDUPE_CAPACITY];
    size_t next;
    unsigned long long accepted;
    unsigned long long duplicates;
    unsigned long long invalid;
} EventSubDedupe;

/* Return 1 on new ID, 0 if replay/invalid. Never mutates state on invalid.
 * Only call after authentic EventSub WebSocket notification parsing.
 * now_seconds is application clock time, not untrusted event metadata. */
int eventsub_dedupe_accept(EventSubDedupe *guard,
                           const char *message_id,
                           unsigned long long now_seconds);

#ifdef __cplusplus
}
#endif

#endif
