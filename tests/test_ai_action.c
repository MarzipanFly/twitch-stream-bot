/* Portable and side-effect-free dry-run guard regression tests. */
#include "ai_action.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static AiManagerDecision proposal(AiManagerEventType event,
                                  AiManagerActionType action)
{
    AiManagerDecision d;
    memset(&d, 0, sizeof(d));
    d.status = AI_MANAGER_DECISION_PROPOSED;
    d.event_type = event;
    d.proposed_action = action;
    return d;
}

int main(void)
{
    AiManagerDecision d = proposal(AI_MANAGER_EVENT_RAID,
                                   AI_MANAGER_ACTION_SHOUTOUT);
    AiActionContext context;
    AiActionPreview result;
    memset(&context, 0, sizeof(context));
    context.now_seconds = 20000;
    context.stream_online = 1;
    context.shoutout_enabled = 1;
    context.shoutout_scope_granted = 1;
    context.moderator_authorized = 1;

    ai_action_preview(&d, &context, &result);
    assert(result.status == AI_ACTION_PREVIEW_ALLOWED);
    assert(result.reason == AI_ACTION_GUARD_NONE && result.executed == 0);

    context.shoutout_enabled = 0;
    ai_action_preview(&d, &context, &result);
    assert(result.status == AI_ACTION_PREVIEW_BLOCKED &&
           result.reason == AI_ACTION_GUARD_DISABLED && !result.executed);
    context.shoutout_enabled = 1;

    context.stream_online = 0;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_OFFLINE && !result.executed);
    context.stream_online = 1;

    context.shoutout_scope_granted = 0;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_PERMISSION && !result.executed);
    context.shoutout_scope_granted = 1;

    context.moderator_authorized = 0;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_PERMISSION && !result.executed);
    context.moderator_authorized = 1;

    context.last_global_shoutout_seconds = 19900;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_COOLDOWN && !result.executed);
    context.last_global_shoutout_seconds = 19879;

    context.last_target_shoutout_seconds = 17000;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_COOLDOWN && !result.executed);
    context.last_target_shoutout_seconds = 16000;
    ai_action_preview(&d, &context, &result);
    assert(result.status == AI_ACTION_PREVIEW_ALLOWED && !result.executed);

    context.last_global_shoutout_seconds = 21000;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_COOLDOWN && !result.executed);

    d = proposal(AI_MANAGER_EVENT_CHAT, AI_MANAGER_ACTION_SHOUTOUT);
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_INVALID_DECISION && !result.executed);

    d = proposal(AI_MANAGER_EVENT_RAID, AI_MANAGER_ACTION_NONE);
    ai_action_preview(&d, &context, &result);
    assert(result.status == AI_ACTION_PREVIEW_NO_ACTION && !result.executed);

    d = proposal(AI_MANAGER_EVENT_CHAT, AI_MANAGER_ACTION_AI_REPLY);
    context.ai_enabled = 0;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_DISABLED && !result.executed);
    context.ai_enabled = 1;
    ai_action_preview(&d, &context, &result);
    assert(result.status == AI_ACTION_PREVIEW_ALLOWED && !result.executed);

    d = proposal(AI_MANAGER_EVENT_OBS_CHANGED, AI_MANAGER_ACTION_OBS_SCENE);
    context.obs_available = 1;
    ai_action_preview(&d, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_UNSUPPORTED_ACTION &&
           !result.executed);

    ai_action_preview(NULL, &context, &result);
    assert(result.reason == AI_ACTION_GUARD_INVALID_DECISION && !result.executed);

    puts("AI Action Dispatcher dry-run tests passed");
    return 0;
}
