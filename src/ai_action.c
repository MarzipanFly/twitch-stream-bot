#include "ai_action.h"

#include <string.h>

static int match_event(AiManagerEventType event, AiManagerActionType action)
{
    switch (action)
    {
        case AI_MANAGER_ACTION_SHOUTOUT:
            return event == AI_MANAGER_EVENT_RAID;
        case AI_MANAGER_ACTION_AI_REPLY:
            return event == AI_MANAGER_EVENT_CHAT;
        case AI_MANAGER_ACTION_WELCOME:
            return event == AI_MANAGER_EVENT_FOLLOW;
        case AI_MANAGER_ACTION_STREAM_REPORT:
            return event == AI_MANAGER_EVENT_STREAM_STARTED ||
                   event == AI_MANAGER_EVENT_STREAM_ENDED;
        case AI_MANAGER_ACTION_NOTIFY_OWNER:
            return event == AI_MANAGER_EVENT_COMPONENT_DEGRADED;
        case AI_MANAGER_ACTION_OBS_SCENE:
            return event == AI_MANAGER_EVENT_OBS_CHANGED;
        default:
            return 0;
    }
}

static int in_cooldown(unsigned long long now,
                       unsigned long long last,
                       unsigned long long period)
{
    /* Fail closed on future timestamps and clock rollback. */
    return last != 0 && (now < last || now - last < period);
}

void ai_action_preview(const AiManagerDecision *decision,
                       const AiActionContext *context,
                       AiActionPreview *result)
{
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    result->status = AI_ACTION_PREVIEW_BLOCKED;
    result->reason = AI_ACTION_GUARD_INVALID_DECISION;

    if (!decision || !context)
        return;
    if (decision->status != AI_MANAGER_DECISION_PROPOSED ||
        decision->proposed_action == AI_MANAGER_ACTION_NONE)
    {
        result->status = AI_ACTION_PREVIEW_NO_ACTION;
        result->reason = AI_ACTION_GUARD_NONE;
        return;
    }

    result->action = decision->proposed_action;
    if (!match_event(decision->event_type, decision->proposed_action))
        return;

    switch (decision->proposed_action)
    {
        case AI_MANAGER_ACTION_SHOUTOUT:
            if (!context->shoutout_enabled)
                result->reason = AI_ACTION_GUARD_DISABLED;
            else if (!context->stream_online)
                result->reason = AI_ACTION_GUARD_OFFLINE;
            else if (!context->shoutout_scope_granted ||
                     !context->moderator_authorized)
                result->reason = AI_ACTION_GUARD_PERMISSION;
            else if (in_cooldown(context->now_seconds,
                                 context->last_global_shoutout_seconds, 120) ||
                     in_cooldown(context->now_seconds,
                                 context->last_target_shoutout_seconds, 3600))
                result->reason = AI_ACTION_GUARD_COOLDOWN;
            else
                result->status = AI_ACTION_PREVIEW_ALLOWED;
            break;
        case AI_MANAGER_ACTION_AI_REPLY:
            if (!context->ai_enabled)
                result->reason = AI_ACTION_GUARD_DISABLED;
            else
                result->status = AI_ACTION_PREVIEW_ALLOWED;
            break;
        case AI_MANAGER_ACTION_OBS_SCENE:
            if (!context->obs_available)
                result->reason = AI_ACTION_GUARD_UNAVAILABLE;
            else
                result->reason = AI_ACTION_GUARD_UNSUPPORTED_ACTION;
            break;
        case AI_MANAGER_ACTION_WELCOME:
        case AI_MANAGER_ACTION_NOTIFY_OWNER:
        case AI_MANAGER_ACTION_STREAM_REPORT:
            /* No executor or dedicated authority switch yet. */
            result->reason = AI_ACTION_GUARD_UNSUPPORTED_ACTION;
            break;
        default:
            result->reason = AI_ACTION_GUARD_UNSUPPORTED_ACTION;
            break;
    }

    if (result->status == AI_ACTION_PREVIEW_ALLOWED)
        result->reason = AI_ACTION_GUARD_NONE;
    /* Intentionally never execute anything, including "allowed" proposals. */
}
