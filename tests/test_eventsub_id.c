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

    {
        char path[TWITCH_EVENTSUB_URL_SIZE];
        char url[TWITCH_EVENTSUB_URL_SIZE];
        const char *valid_reconnect =
            "{\"metadata\":{\"message_type\":\"session_reconnect\"},"
            "\"payload\":{\"session\":{\"reconnect_url\":"
            "\"wss://eventsub.wss.twitch.tv/ws?reconnect=123abc\"}}}";
        assert(twitch_eventsub_reconnect_path(
            "wss://eventsub.wss.twitch.tv/ws?reconnect=123abc",
            path, sizeof(path)));
        assert(strcmp(path, "/ws?reconnect=123abc") == 0);
        assert(twitch_eventsub_reconnect_path(
            "wss://eventsub.wss.twitch.tv?reconnect=xyz",
            path, sizeof(path)));
        assert(strcmp(path, "/?reconnect=xyz") == 0);
        assert(!twitch_eventsub_reconnect_path(
            "wss://eventsub.wss.twitch.tv.evil/ws?reconnect=1",
            path, sizeof(path)));
        assert(!twitch_eventsub_reconnect_path(
            "ws://eventsub.wss.twitch.tv/ws?reconnect=1",
            path, sizeof(path)));
        assert(!twitch_eventsub_reconnect_path(
            "wss://eventsub.wss.twitch.tv/ws?reconnect=1#fragment",
            path, sizeof(path)));
        assert(!twitch_eventsub_reconnect_path(
            "wss://eventsub.wss.twitch.tv/ws?reconnect=1",
            path, 4));
        assert(twitch_eventsub_parse_reconnect_url(
            valid_reconnect, url, sizeof(url)) == BOT_OK);
        assert(strcmp(url,
            "wss://eventsub.wss.twitch.tv/ws?reconnect=123abc") == 0);
        assert(twitch_eventsub_parse_reconnect_url(
            valid_reconnect, url, 6) == BOT_ERR_JSON);
        assert(!url[0]);
    }
    puts("EventSub metadata and reconnect URL parsing tests passed");
    return 0;
}
