/* cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_ai_manager.c src/ai_manager.c -o test_ai_manager */
#include "ai_manager.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static AiManagerEvent raid(const char *id) {
 AiManagerEvent e; memset(&e,0,sizeof(e)); e.type=AI_MANAGER_EVENT_RAID;
 snprintf(e.event_id,sizeof(e.event_id),"%s",id); snprintf(e.actor_id,sizeof(e.actor_id),"12345");
 snprintf(e.actor_login,sizeof(e.actor_login),"raider_channel"); e.timestamp=1000;
 e.stream_online=1;e.viewer_count=12;return e;
}
int main(void) {
 AiManagerPolicy p={0};AiManagerEvent e;AiManagerDecision d,h[8];AiManagerStatus s;
 p.enable_raid_shoutout=1;p.enable_stream_report=1;p.enable_owner_notifications=1;
 ai_manager_init(&p);e=raid("raid-1");
 assert(ai_manager_observe(&e,&d));assert(d.status==AI_MANAGER_DECISION_PROPOSED);
 assert(d.proposed_action==AI_MANAGER_ACTION_SHOUTOUT&&d.sequence==1);
 assert(ai_manager_observe(&e,&d));assert(d.status==AI_MANAGER_DECISION_DUPLICATE);
 assert(d.proposed_action==AI_MANAGER_ACTION_NONE);
 e=raid("raid-2");e.viewer_count=0;assert(ai_manager_observe(&e,&d));
 assert(d.status==AI_MANAGER_DECISION_IGNORED&&d.proposed_action==AI_MANAGER_ACTION_NONE);
 memset(&e,0,sizeof(e));e.type=AI_MANAGER_EVENT_CHAT;
 assert(ai_manager_observe(&e,&d));assert(d.status==AI_MANAGER_DECISION_IGNORED);
 e.direct_mention=1;assert(ai_manager_observe(&e,&d));
 assert(d.proposed_action==AI_MANAGER_ACTION_AI_REPLY);
 e.type=AI_MANAGER_EVENT_COMPONENT_DEGRADED;e.direct_mention=0;
 assert(ai_manager_observe(&e,&d));assert(d.proposed_action==AI_MANAGER_ACTION_NOTIFY_OWNER);
 ai_manager_get_status(&s);assert(s.monitor_only==1);assert(s.observed_events==6);
 assert(s.proposed_actions==3&&s.duplicate_events==1&&s.history_count==6);
 assert(ai_manager_get_history(h,8)==6);assert(h[0].sequence==1);
 assert(h[1].status==AI_MANAGER_DECISION_DUPLICATE);
 assert(h[5].proposed_action==AI_MANAGER_ACTION_NOTIFY_OWNER);
 memset(&e,0,sizeof(e));e.type=AI_MANAGER_EVENT_UNKNOWN;
 assert(!ai_manager_observe(&e,&d));assert(d.status==AI_MANAGER_DECISION_REJECTED);
 assert(d.proposed_action==AI_MANAGER_ACTION_NONE);
 /* Reject fixed-size fields that are not NUL-terminated. */
 memset(&e, 'x', sizeof(e));
 e.type=AI_MANAGER_EVENT_RAID;
 snprintf(e.actor_id,sizeof(e.actor_id),"id");
 snprintf(e.actor_login,sizeof(e.actor_login),"login");
 assert(!ai_manager_observe(&e,&d));
 assert(d.status==AI_MANAGER_DECISION_REJECTED);
 /* Changing toggle policy must preserve history and suppress proposals. */
 p.enable_raid_shoutout=0;
 ai_manager_set_policy(&p);
 e=raid("raid-3");
 assert(ai_manager_observe(&e,&d));
 assert(d.status==AI_MANAGER_DECISION_IGNORED);
 assert(d.proposed_action==AI_MANAGER_ACTION_NONE);
 ai_manager_get_status(&s);
 assert(s.history_count>=7);
 assert(s.monitor_only==1);
 puts("AI Manager observe-only tests passed");return 0;
}
