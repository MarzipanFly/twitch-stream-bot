#ifndef AI_CHAT_H
#define AI_CHAT_H

#include "config.h"
#include "twitch_eventsub.h"

typedef enum
{
    AI_CHAT_QUEUED = 0,
    AI_CHAT_DISABLED,
    AI_CHAT_COOLDOWN,
    AI_CHAT_QUEUE_FULL,
    AI_CHAT_TOO_LONG,
    AI_CHAT_INVALID
} AiChatStatus;

/* Runs Ollama and Helix chat delivery in a dedicated Windows thread. */
int ai_chat_start(const AiConfig *config);
AiChatStatus ai_chat_enqueue(const TwitchChatMessage *message,
                             const char *question,
                             const TwitchConfig *twitch);
void ai_chat_stop(void);
int ai_chat_running(void);

#endif
