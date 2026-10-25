#ifndef VIEWER_CHALLENGE_CHAT_H
#define VIEWER_CHALLENGE_CHAT_H
#include "viewer_challenges.h"
#include "twitch_eventsub.h"
#include <stddef.h>

/* Return 1 when recognized (even if operation failed), 0 otherwise.
 * Never sends Twitch messages itself; caller uses existing OAuth recovery. */
int viewer_challenge_chat_process(ViewerChallenges *queue,
    const TwitchChatMessage *chat, const char *broadcaster_id,
    char prefix, char *response, size_t capacity);
#endif
