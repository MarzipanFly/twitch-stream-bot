#ifndef AI_MANAGER_H
#define AI_MANAGER_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AI_MANAGER_ID_SIZE 128
#define AI_MANAGER_LOGIN_SIZE 128
#define AI_MANAGER_REASON_SIZE 192
#define AI_MANAGER_HISTORY_SIZE 32
#define AI_MANAGER_DEDUP_SIZE 64
typedef enum {
 AI_MANAGER_EVENT_UNKNOWN=0, AI_MANAGER_EVENT_CHAT, AI_MANAGER_EVENT_RAID,
 AI_MANAGER_EVENT_FOLLOW, AI_MANAGER_EVENT_SUBSCRIPTION,
 AI_MANAGER_EVENT_STREAM_STARTED, AI_MANAGER_EVENT_STREAM_ENDED,
 AI_MANAGER_EVENT_OBS_CHANGED, AI_MANAGER_EVENT_COMPONENT_DEGRADED
} AiManagerEventType;
typedef enum {
 AI_MANAGER_ACTION_NONE=0, AI_MANAGER_ACTION_AI_REPLY,
 AI_MANAGER_ACTION_SHOUTOUT, AI_MANAGER_ACTION_WELCOME,
 AI_MANAGER_ACTION_STREAM_REPORT, AI_MANAGER_ACTION_NOTIFY_OWNER,
 AI_MANAGER_ACTION_OBS_SCENE
} AiManagerActionType;
typedef enum {
 AI_MANAGER_DECISION_IGNORED=0, AI_MANAGER_DECISION_PROPOSED,
 AI_MANAGER_DECISION_DUPLICATE, AI_MANAGER_DECISION_REJECTED
} AiManagerDecisionStatus;
typedef struct {
 AiManagerEventType type;
 char event_id[AI_MANAGER_ID_SIZE];
 char actor_id[AI_MANAGER_ID_SIZE];
 char actor_login[AI_MANAGER_LOGIN_SIZE];
 unsigned long long timestamp;
 int stream_online;
 int viewer_count;
 int direct_mention;
} AiManagerEvent;
typedef struct {
 int enable_raid_shoutout;
 int enable_new_viewer_welcome;
 int enable_stream_report;
 int enable_owner_notifications;
} AiManagerPolicy;
typedef struct {
 unsigned long long sequence;
 AiManagerEventType event_type;
 AiManagerActionType proposed_action;
 AiManagerDecisionStatus status;
 unsigned long long timestamp;
 char actor_login[AI_MANAGER_LOGIN_SIZE];
 char reason[AI_MANAGER_REASON_SIZE];
} AiManagerDecision;
typedef struct {
 size_t observed_events, proposed_actions, duplicate_events, rejected_events;
 size_t history_count;
 unsigned long long last_sequence;
 int monitor_only;
} AiManagerStatus;
/* Observe-only: this module never executes external actions. */
void ai_manager_init(const AiManagerPolicy *policy);
void ai_manager_reset(void);
int ai_manager_observe(const AiManagerEvent *event, AiManagerDecision *decision);
void ai_manager_get_status(AiManagerStatus *status);
size_t ai_manager_get_history(AiManagerDecision *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
