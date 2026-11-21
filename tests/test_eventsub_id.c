/* Pure parser tests; no Twitch network access. */
#include "twitch_eventsub.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* twitch_eventsub.c only needs log_error from the logger module. */
void log_error(const char *format, ...) { (void)format; }

int main(void)
{
    const char *good =
        "{\"metadata\":{\"message_type\":\"notification\","
        "\"message_id\":\"evt-123\"},\"payload\":{}}";
    const char *missing =
        "{\"metadata\":{\"message_type\":\"notification\"},\"payload\":{}}";
    const char *empty =
        "{\"metadata\":{\"message_id\":\"\"},\"payload\":{}}";
    char id[TWITCH_EVENTSUB_ID_SIZE];
    char short_id[5];

    assert(twitch_eventsub_get_message_id(good, id, sizeof(id)) == BOT_OK);
    assert(strcmp(id, "evt-123") == 0);
    assert(twitch_eventsub_get_message_id(good, short_id, sizeof(short_id)) ==
           BOT_ERR_JSON);
    assert(short_id[0] == '\0');
    assert(twitch_eventsub_get_message_id(missing, id, sizeof(id)) == BOT_ERR_JSON);
    assert(twitch_eventsub_get_message_id(empty, id, sizeof(id)) == BOT_ERR_JSON);
    assert(twitch_eventsub_get_message_id("not-json", id, sizeof(id)) ==
           BOT_ERR_JSON);
    assert(twitch_eventsub_get_message_id(good, NULL, 10) == BOT_ERR_CONFIG);

    puts("EventSub metadata.message_id parsing tests passed");
    return 0;
}
