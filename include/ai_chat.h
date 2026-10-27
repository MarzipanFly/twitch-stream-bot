#ifndef AI_CHAT_H
#define AI_CHAT_H

#include "config.h"
#include "twitch_eventsub.h"
#include "twitch_stream.h"

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
AiChatStatus ai_chat_enqueue_live(const TwitchChatMessage *message,
                                  const char *question,
                                  const TwitchConfig *twitch);
/* Reset recent history (and update the local file if opted in). */
int ai_chat_forget(const char *user_id);
/* Explicit per-viewer opt-in/opt-out for memory across streams.
 * Disabling deletes stored turns for this Twitch user ID. */
int ai_chat_persistent_set(const char *user_id, int enable);
void ai_chat_set_stream_context(const TwitchStream *stream);
void ai_chat_set_current_track(const char *title);
/* Latest OBS scene and fullscreen process detected by the OBS director. */
void ai_chat_set_obs_context(const char *scene, const char *process_name);
void ai_chat_stop(void);
int ai_chat_running(void);
void ai_chat_set_cooldown(unsigned int seconds);
/* Queue one private streamer prompt; it is never sent to Twitch chat. */
int ai_chat_request_cohost_suggestion(void);
void ai_chat_get_cohost_suggestion(char *text, size_t capacity,
                                   unsigned int *sequence, int *pending);

#endif
