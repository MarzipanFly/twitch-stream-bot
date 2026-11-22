/* cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_eventsub_dedupe.c src/eventsub_dedupe.c -o test_eventsub_dedupe */
#include "eventsub_dedupe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static EventSubDedupe state; /* static to keep Windows stack small */

int main(void)
{
    char long_id[EVENTSUB_DEDUPE_ID_BYTES + 2];
    memset(&state, 0, sizeof(state));

    assert(eventsub_dedupe_accept(&state, "raid-uuid-1", 1000));
    assert(!eventsub_dedupe_accept(&state, "raid-uuid-1", 1001));
    assert(state.accepted == 1 && state.duplicates == 1);
    assert(eventsub_dedupe_accept(&state, "follow-uuid-1", 1002));
    assert(!eventsub_dedupe_accept(&state, "follow-uuid-1", 1002));

    assert(!eventsub_dedupe_accept(&state, NULL, 1005));
    assert(!eventsub_dedupe_accept(&state, "", 1005));
    assert(!eventsub_dedupe_accept(&state, "bad-clock", 0));
    memset(long_id, 'a', sizeof(long_id));
    long_id[sizeof(long_id) - 1] = '\0';
    assert(!eventsub_dedupe_accept(&state, long_id, 1005));
    assert(state.invalid == 4);

    assert(!eventsub_dedupe_accept(&state, "raid-uuid-1", 999));
    assert(eventsub_dedupe_accept(&state, "raid-uuid-1",
                                  1000 + EVENTSUB_DEDUPE_TTL_SECONDS));

    /* Once the finite ring wraps, newer IDs must still be tracked. */
    for (unsigned int i = 0; i < EVENTSUB_DEDUPE_CAPACITY + 3; ++i)
    {
        char id[50];
        snprintf(id, sizeof(id), "test-message-%u", i);
        assert(eventsub_dedupe_accept(&state, id, 3000 + i));
        assert(!eventsub_dedupe_accept(&state, id, 3000 + i));
    }
    puts("EventSub pre-side-effect deduplication tests passed");
    return 0;
}
