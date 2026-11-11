#ifndef AI_ACTION_H
#define AI_ACTION_H

#include "ai_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Guard inputs must come from trusted application state, never from the LLM. */
typedef struct
{
    int stream_online;
    int shoutout_enabled;
    int shoutout_scope_granted;
    int moderator_authorized;
    int ai_enabled;
    int obs_available;
    unsigned long long now_seconds;
    unsigned long long last_global_shoutout_seconds;
    unsigned long long last_target_shoutout_seconds;
} AiActionContext;

typedef enum
{
    AI_ACTION_PREVIEW_NO_ACTION = 0,
    AI_ACTION_PREVIEW_ALLOWED,
    AI_ACTION_PREVIEW_BLOCKED
} AiActionPreviewStatus;

typedef enum
{
    AI_ACTION_GUARD_NONE = 0,
    AI_ACTION_GUARD_INVALID_DECISION,
    AI_ACTION_GUARD_UNSUPPORTED_ACTION,
    AI_ACTION_GUARD_DISABLED,
    AI_ACTION_GUARD_OFFLINE,
    AI_ACTION_GUARD_PERMISSION,
    AI_ACTION_GUARD_COOLDOWN,
    AI_ACTION_GUARD_UNAVAILABLE
} AiActionGuardReason;

typedef struct
{
    AiActionPreviewStatus status;
    AiActionGuardReason reason;
    AiManagerActionType action;
    /* This field is always 0; there is no executor in this version. */
    int executed;
} AiActionPreview;

/* Read-only policy check. Never calls Twitch, OBS, Ollama, or a file API.
 * A preview does not reserve cooldown or guarantee a later execution. */
void ai_action_preview(const AiManagerDecision *decision,
                       const AiActionContext *context,
                       AiActionPreview *result);

#ifdef __cplusplus
}
#endif

#endif
