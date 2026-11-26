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
    {
        const char *redemption_json =
            "{\"metadata\":{\"message_type\":\"notification\","
            "\"message_id\":\"evt-redemption-1\"},"
            "\"payload\":{"
            "\"subscription\":{\"type\":"
            "\"channel.channel_points_custom_reward_redemption.add\"},"
            "\"event\":{"
            "\"id\":\"redemption-1\","
            "\"broadcaster_user_id\":\"1001\","
            "\"broadcaster_user_login\":\"aleg_opelsin1\","
            "\"user_id\":\"2002\","
            "\"user_login\":\"viewer_login\","
            "\"user_name\":\"ViewerName\","
            "\"user_input\":\"hello\","
            "\"reward\":{"
            "\"id\":\"reward-1\","
            "\"title\":\"Orange rain\","
            "\"cost\":250,"
            "\"prompt\":\"Optional prompt\"}}}}";
        const char *wrong_type =
            "{\"payload\":{\"subscription\":{"
            "\"type\":\"channel.chat.message\"},\"event\":{}}}";
        TwitchChannelPointsRedemption redemption;
        assert(twitch_eventsub_parse_channel_points_redemption(redemption_json, &redemption) == BOT_OK);
        assert(strcmp(redemption.redemption_id, "redemption-1") == 0);
        assert(strcmp(redemption.broadcaster_user_login, "aleg_opelsin1") == 0);
        assert(strcmp(redemption.user_login, "viewer_login") == 0);
        assert(strcmp(redemption.reward_id, "reward-1") == 0);
        assert(strcmp(redemption.reward_title, "Orange rain") == 0);
        assert(strcmp(redemption.user_input, "hello") == 0);
        assert(redemption.reward_cost == 250);
        assert(twitch_eventsub_parse_channel_points_redemption(wrong_type, &redemption) == BOT_ERR_TWITCH);
        assert(twitch_eventsub_parse_channel_points_redemption(redemption_json, NULL) == BOT_ERR_CONFIG);
    }

    {
        const char *chat_json =
            "{\"payload\":{\"subscription\":{\"type\":\"channel.chat.message\"},"
            "\"event\":{"
            "\"broadcaster_user_id\":\"1001\","
            "\"broadcaster_user_login\":\"aleg_opelsin1\","
            "\"broadcaster_user_name\":\"aleg_opelsin1\","
            "\"chatter_user_id\":\"2002\","
            "\"chatter_user_login\":\"viewer_login\","
            "\"chatter_user_name\":\"ViewerName\","
            "\"message_id\":\"chat-1\","
            "\"message\":{\"text\":\"!баланс\"},"
            "\"badges\":["
            "{\"set_id\":\"moderator\",\"id\":\"1\",\"info\":\"\"},"
            "{\"set_id\":\"vip\",\"id\":\"1\",\"info\":\"\"},"
            "{\"set_id\":\"subscriber\",\"id\":\"12\",\"info\":\"16\"},"
            "{\"set_id\":\"founder\",\"id\":\"1\",\"info\":\"40\"},"
            "{\"set_id\":\"unknown-badge\",\"id\":\"1\",\"info\":\"\"}"
            "]}}}";
        TwitchChatMessage chat;
        assert(twitch_eventsub_parse_chat_message(chat_json, &chat) == BOT_OK);
        assert(chat.is_moderator == 1);
        assert(chat.is_vip == 1);
        assert(chat.is_subscriber == 1);
        assert(chat.is_broadcaster == 0);
        assert(chat.is_follower == 0);
        assert(chat.follower_status_known == 0);
        assert(strcmp(chat.text, "!баланс") == 0);
    }

    puts("EventSub metadata, reconnect, Channel Points and chat role parsing tests passed");
    return 0;
}
