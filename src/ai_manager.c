#include "ai_manager.h"
#include <stdio.h>
#include <string.h>
typedef struct {
 AiManagerPolicy policy;
 AiManagerDecision history[AI_MANAGER_HISTORY_SIZE];
 char seen_ids[AI_MANAGER_DEDUP_SIZE][AI_MANAGER_ID_SIZE];
 size_t history_next, history_count, seen_next, seen_count;
 size_t observed_events, proposed_actions, duplicate_events, rejected_events;
 unsigned long long sequence;
 int initialized;
} AiManagerState;
static AiManagerState g_manager;
static void copy_text(char *d,size_t cap,const char *s) {
 size_t n; if(!d||!cap)return; d[0]='\0'; if(!s)return;
 n=strlen(s); if(n>=cap)n=cap-1; memcpy(d,s,n); d[n]='\0';
}
static int valid_type(AiManagerEventType t) {
 return t>AI_MANAGER_EVENT_UNKNOWN && t<=AI_MANAGER_EVENT_COMPONENT_DEGRADED;
}
static int seen(const char *id) {
 size_t i; if(!id||!id[0])return 0;
 for(i=0;i<g_manager.seen_count;++i)if(strcmp(g_manager.seen_ids[i],id)==0)return 1;
 return 0;
}
static void remember(const char *id) {
 if(!id||!id[0])return;
 copy_text(g_manager.seen_ids[g_manager.seen_next],AI_MANAGER_ID_SIZE,id);
 g_manager.seen_next=(g_manager.seen_next+1)%AI_MANAGER_DEDUP_SIZE;
 if(g_manager.seen_count<AI_MANAGER_DEDUP_SIZE)++g_manager.seen_count;
}
static void save(AiManagerDecision *d) {
 d->sequence=++g_manager.sequence;
 g_manager.history[g_manager.history_next]=*d;
 g_manager.history_next=(g_manager.history_next+1)%AI_MANAGER_HISTORY_SIZE;
 if(g_manager.history_count<AI_MANAGER_HISTORY_SIZE)++g_manager.history_count;
}
static void reject(AiManagerDecision *d,const char *reason) {
 d->status=AI_MANAGER_DECISION_REJECTED; d->proposed_action=AI_MANAGER_ACTION_NONE;
 copy_text(d->reason,sizeof(d->reason),reason); ++g_manager.rejected_events;
}
void ai_manager_init(const AiManagerPolicy *p) {
 memset(&g_manager,0,sizeof(g_manager)); if(p)g_manager.policy=*p; g_manager.initialized=1;
}
void ai_manager_reset(void) { memset(&g_manager,0,sizeof(g_manager)); }
int ai_manager_observe(const AiManagerEvent *e,AiManagerDecision *out) {
 AiManagerDecision local; AiManagerDecision *d=out?out:&local;
 memset(d,0,sizeof(*d)); d->status=AI_MANAGER_DECISION_IGNORED;
 if(!g_manager.initialized)ai_manager_init(NULL);
 if(!e||!valid_type(e->type)){reject(d,"Invalid or unsupported event type.");save(d);return 0;}
 ++g_manager.observed_events; d->event_type=e->type; d->timestamp=e->timestamp;
 copy_text(d->actor_login,sizeof(d->actor_login),e->actor_login);
 if(e->event_id[0]&&seen(e->event_id)){
  d->status=AI_MANAGER_DECISION_DUPLICATE;
  copy_text(d->reason,sizeof(d->reason),"Event ID already observed; duplicate suppressed.");
  ++g_manager.duplicate_events; save(d); return 1;
 }
 remember(e->event_id);
 switch(e->type) {
 case AI_MANAGER_EVENT_RAID:
  if(!e->actor_id[0]||!e->actor_login[0])reject(d,"Raid event has no validated raider identity.");
  else if(!g_manager.policy.enable_raid_shoutout)copy_text(d->reason,sizeof(d->reason),"Raid observed; Shoutout policy disabled.");
  else if(!e->stream_online||e->viewer_count<1)copy_text(d->reason,sizeof(d->reason),"Raid observed; Twitch Shoutout prerequisites not met.");
  else {d->status=AI_MANAGER_DECISION_PROPOSED;d->proposed_action=AI_MANAGER_ACTION_SHOUTOUT;copy_text(d->reason,sizeof(d->reason),"Incoming raid; propose Twitch native Shoutout.");++g_manager.proposed_actions;}
  break;
 case AI_MANAGER_EVENT_FOLLOW:
  if(g_manager.policy.enable_new_viewer_welcome&&e->actor_login[0]){d->status=AI_MANAGER_DECISION_PROPOSED;d->proposed_action=AI_MANAGER_ACTION_WELCOME;copy_text(d->reason,sizeof(d->reason),"New follow observed; propose welcome.");++g_manager.proposed_actions;}
  else copy_text(d->reason,sizeof(d->reason),"Follow observed; welcome disabled or identity missing.");
  break;
 case AI_MANAGER_EVENT_STREAM_STARTED:
 case AI_MANAGER_EVENT_STREAM_ENDED:
  if(g_manager.policy.enable_stream_report){d->status=AI_MANAGER_DECISION_PROPOSED;d->proposed_action=AI_MANAGER_ACTION_STREAM_REPORT;copy_text(d->reason,sizeof(d->reason),e->type==AI_MANAGER_EVENT_STREAM_STARTED?"Stream started; propose session tracking.":"Stream ended; propose finalizing session report.");++g_manager.proposed_actions;}
  else copy_text(d->reason,sizeof(d->reason),"Stream transition observed; reporting policy disabled.");
  break;
 case AI_MANAGER_EVENT_COMPONENT_DEGRADED:
  if(g_manager.policy.enable_owner_notifications){d->status=AI_MANAGER_DECISION_PROPOSED;d->proposed_action=AI_MANAGER_ACTION_NOTIFY_OWNER;copy_text(d->reason,sizeof(d->reason),"Component health degraded; propose owner notification.");++g_manager.proposed_actions;}
  else copy_text(d->reason,sizeof(d->reason),"Component degradation observed; owner notifications disabled.");
  break;
 case AI_MANAGER_EVENT_CHAT:
  if(e->direct_mention){d->status=AI_MANAGER_DECISION_PROPOSED;d->proposed_action=AI_MANAGER_ACTION_AI_REPLY;copy_text(d->reason,sizeof(d->reason),"Explicit bot mention; propose AI reply.");++g_manager.proposed_actions;}
  else copy_text(d->reason,sizeof(d->reason),"Ordinary chat; no autonomous response proposed.");
  break;
 case AI_MANAGER_EVENT_OBS_CHANGED:
  copy_text(d->reason,sizeof(d->reason),"OBS state change observed; no action policy configured yet."); break;
 default: reject(d,"Event type not handled by current policy."); break;
 }
 save(d); return d->status!=AI_MANAGER_DECISION_REJECTED;
}
void ai_manager_get_status(AiManagerStatus *s) {
 if(!s)return; memset(s,0,sizeof(*s)); s->observed_events=g_manager.observed_events;
 s->proposed_actions=g_manager.proposed_actions;s->duplicate_events=g_manager.duplicate_events;
 s->rejected_events=g_manager.rejected_events;s->history_count=g_manager.history_count;
 s->last_sequence=g_manager.sequence;s->monitor_only=1;
}
size_t ai_manager_get_history(AiManagerDecision *out,size_t cap) {
 size_t n,i,first; if(!out||!cap)return 0;
 n=g_manager.history_count<cap?g_manager.history_count:cap;
 first=(g_manager.history_next+AI_MANAGER_HISTORY_SIZE-g_manager.history_count)%AI_MANAGER_HISTORY_SIZE;
 if(n<g_manager.history_count)first=(first+g_manager.history_count-n)%AI_MANAGER_HISTORY_SIZE;
 for(i=0;i<n;++i)out[i]=g_manager.history[(first+i)%AI_MANAGER_HISTORY_SIZE];
 return n;
}
