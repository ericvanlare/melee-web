#define MELEE_WEB_CPU_INTERVAL_IMPLEMENTATION 1
#include "gameplay_cpu_interval.h"
#include <dolphin/pad.h>
#include <limits.h>
#include <string.h>
static uint64_t epoch;
static unsigned active, failure;
static struct {
 MeleeWebCpuMotorEvidence evidence;
 MeleeWebCpuMotorCommitReceipt receipt;
 uint64_t token,world,context_epoch;
 const void* context;
 uint32_t span,first_frame,active_lane,open_frame,frame_channels;
 unsigned capture_enabled,restore_used,frame_open,retired;
 unsigned history_mode,history_restore_count,history_sealed,history_replay_active;
 uint64_t history_incarnation;
 uint32_t history_end_frame,history_replay_end;
} motor;

static uint32_t low32(uint64_t value){return (uint32_t)value;}
static uint32_t high32(uint64_t value){return (uint32_t)(value>>32);}
static uint64_t join64(uint32_t lo,uint32_t hi){return (uint64_t)lo|((uint64_t)hi<<32);}
static MeleeWebCpuMotorSequence* active_sequence(void)
{
 if(motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE)return &motor.evidence.baseline;
 if(motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_REPLAY||
    motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED)return &motor.evidence.selected;
 return NULL;
}
static void sequence_init(MeleeWebCpuMotorSequence* sequence,uint32_t state)
{
 memset(sequence,0,sizeof(*sequence));sequence->state=state;
 sequence->span=motor.span;sequence->first_frame=motor.first_frame;
 sequence->token_lo=low32(motor.token);sequence->token_hi=high32(motor.token);
 sequence->world_lo=low32(motor.world);sequence->world_hi=high32(motor.world);
 sequence->context_epoch_lo=low32(motor.context_epoch);
 sequence->context_epoch_hi=high32(motor.context_epoch);
}
static int motor_owner_matches(uint64_t token,const void* context,uint64_t world,
 uint64_t context_epoch)
{
 return motor.capture_enabled&&active&&token&&token==motor.token&&token==epoch&&
        context&&context==motor.context&&world&&world==motor.world&&
        context_epoch&&context_epoch==motor.context_epoch;
}
static void motor_refuse(uint32_t reason)
{
 if(motor.capture_enabled&&!motor.evidence.failure)motor.evidence.failure=reason;
 motor.frame_open=0;
 if(active&&!failure)failure=MELEE_WEB_CPU_EFFECT_MOTOR;
}
static int sequence_owner_matches(const MeleeWebCpuMotorSequence* sequence)
{
 return (motor.history_mode?
          (sequence->span>=1u&&sequence->span<=MELEE_WEB_CPU_MOTOR_MAX_SPAN&&
           sequence->first_frame==motor.first_frame):
          (sequence->span==motor.evidence.baseline.span&&
           sequence->first_frame==motor.evidence.baseline.first_frame))&&
        join64(sequence->token_lo,sequence->token_hi)==motor.token&&
        join64(sequence->world_lo,sequence->world_hi)==motor.world&&
        join64(sequence->context_epoch_lo,sequence->context_epoch_hi)==motor.context_epoch;
}
static uint64_t motor_hash(const MeleeWebCpuMotorSequence* sequence);
uint64_t melee_web_cpu_interval_begin(void)
{
#if defined(__EMSCRIPTEN_PTHREADS__)
 return 0;
#else
 if(active||epoch==UINT64_MAX)return 0;
 ++epoch;active=1;failure=0;memset(&motor,0,sizeof(motor));return epoch;
#endif
}
int melee_web_cpu_interval_admit(uint64_t token)
{return active&&token&&token==epoch&&!failure;}
int melee_web_cpu_interval_active(void){return active!=0;}
uint64_t melee_web_cpu_interval_token(void){return active?epoch:0;}
int melee_web_cpu_interval_attempt(enum MeleeWebCpuIntervalEffect effect)
{
 if(!active)return 1;
 if(!failure)failure=effect?effect:UINT_MAX;
 return 0;
}
unsigned melee_web_cpu_interval_failure(void){return failure;}
int melee_web_cpu_interval_active_failure(void){return active&&failure;}
int melee_web_cpu_interval_end(uint64_t token)
{
 if(!active||!token||token!=epoch)return 0;
 if(motor.capture_enabled){
  MeleeWebCpuMotorSequence* sequence=active_sequence();
  if(sequence&&sequence->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE)
   sequence->state=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE;
  if(motor.evidence.commit_state==MELEE_WEB_CPU_MOTOR_COMMIT_PENDING)
   motor.evidence.commit_state=MELEE_WEB_CPU_MOTOR_COMMIT_REFUSED;
  motor.retired=motor.receipt.status==MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY&&
      !failure&&!motor.evidence.failure;
  motor.capture_enabled=0;motor.frame_open=0;motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 }
 active=0;return 1; /* Failure and immutable evidence stay readable until the next begin. */
}
int melee_web_cpu_interval_motor_capture_begin(uint64_t token,uint64_t world,
 const void* context,uint64_t context_epoch,uint32_t first_frame,uint32_t span)
{
 if(motor.capture_enabled)return 0; /* Never clear a live or failed journal. */
 if(!active||!token||token!=epoch||!melee_web_cpu_interval_admit(token))return 0;
 memset(&motor,0,sizeof(motor));
 motor.evidence.schema=1;motor.evidence.bytes=sizeof(motor.evidence);
 motor.evidence.mode=1;motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 motor.evidence.published=0;
 motor.evidence.commit_state=MELEE_WEB_CPU_MOTOR_COMMIT_PENDING;
 if(!world||!context||!context_epoch||span<1u||span>MELEE_WEB_CPU_MOTOR_MAX_SPAN||
    first_frame>UINT32_MAX-span){
  motor.capture_enabled=1;motor.token=token;motor.world=world;
  motor.context=context;motor.context_epoch=context_epoch;
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INVALID_OWNER);return 0;
 }
 motor.token=token;motor.world=world;motor.context=context;
 motor.context_epoch=context_epoch;motor.capture_enabled=1;
 motor.span=span;motor.first_frame=first_frame;
 sequence_init(&motor.evidence.baseline,MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE);
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_BASELINE;
 return 1;
}
int melee_web_cpu_interval_motor_frame_begin(uint64_t token,const void* context,
 uint64_t world,uint64_t context_epoch,uint64_t source_frame)
{
 if(!motor.capture_enabled)return 1;
 MeleeWebCpuMotorSequence* sequence=active_sequence();
 if(!melee_web_cpu_interval_admit(token)||!motor_owner_matches(token,context,world,context_epoch)||
    !sequence||sequence->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
    !sequence_owner_matches(sequence)||motor.frame_open||
    source_frame>UINT32_MAX||
    sequence->frames_seen>=sequence->span||
    source_frame!=(uint64_t)sequence->first_frame+sequence->frames_seen){
  motor_refuse(!token||token!=motor.token?MELEE_WEB_CPU_MOTOR_FAILURE_STALE_TOKEN:
   MELEE_WEB_CPU_MOTOR_FAILURE_FRAME);return 0;
 }
 motor.open_frame=(uint32_t)source_frame;motor.frame_channels=0;motor.frame_open=1;return 1;
}
int melee_web_cpu_interval_motor_frame_end(uint64_t token,const void* context,
 uint64_t world,uint64_t context_epoch,uint64_t source_frame)
{
 if(!motor.capture_enabled)return 1;
 MeleeWebCpuMotorSequence* sequence=active_sequence();
 if(!melee_web_cpu_interval_admit(token)||!motor_owner_matches(token,context,world,context_epoch)||
    !sequence||!motor.frame_open||source_frame>UINT32_MAX||source_frame!=motor.open_frame||
    !sequence_owner_matches(sequence)){
  motor_refuse(!token||token!=motor.token?MELEE_WEB_CPU_MOTOR_FAILURE_STALE_TOKEN:
   MELEE_WEB_CPU_MOTOR_FAILURE_FRAME);return 0;
 }
 ++sequence->frames_seen;motor.frame_open=0;motor.frame_channels=0;return 1;
}
int melee_web_cpu_interval_motor_baseline_seal(uint64_t token,uint32_t post_frame)
{
 if(!motor.capture_enabled)return 1;
 MeleeWebCpuMotorSequence* baseline=&motor.evidence.baseline;
 if(!melee_web_cpu_interval_admit(token)||token!=motor.token||
    motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_BASELINE||motor.frame_open||
    baseline->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
    !sequence_owner_matches(baseline)||baseline->frames_seen!=baseline->span||
    post_frame!=baseline->first_frame+baseline->span){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INCOMPLETE);return 0;
 }
 baseline->state=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE;
 memcpy(&motor.evidence.selected,baseline,sizeof(*baseline));
 motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_BASELINE;
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 return 1;
}
int melee_web_cpu_interval_motor_restore_admit(uint64_t token,uint32_t lane)
{
 if(!motor.capture_enabled)return 1;
 return !motor.history_mode&&melee_web_cpu_interval_admit(token)&&token==motor.token&&!motor.restore_used&&
  (lane==MELEE_WEB_CPU_MOTOR_LANE_REPLAY||lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED)&&
  !motor.frame_open&&motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_NONE&&
  motor.evidence.baseline.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
  motor.evidence.selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
  motor.evidence.selected_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE&&
  sequence_owner_matches(&motor.evidence.baseline)&&sequence_owner_matches(&motor.evidence.selected);
}
void melee_web_cpu_interval_motor_restore_commit(uint64_t token,uint32_t lane)
{
 if(!motor.capture_enabled)return;
 if(!melee_web_cpu_interval_motor_restore_admit(token,lane)){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_LANE);return;
 }
 /* Caller ran pure admission immediately before the checked typed restore and
  * invokes this only after that restore committed successfully. */
 motor.restore_used=1;motor.evidence.selected_lane=lane;
 sequence_init(&motor.evidence.selected,MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE);
 motor.active_lane=lane;
 (void)token;
}
int melee_web_cpu_interval_motor_selected_finish(uint64_t token,uint32_t post_frame)
{
 if(!motor.capture_enabled)return 1;
 MeleeWebCpuMotorSequence* selected=&motor.evidence.selected;
 if(!melee_web_cpu_interval_admit(token)||token!=motor.token||motor.frame_open||
    (motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_REPLAY&&
     motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED)||
    !sequence_owner_matches(selected)||selected->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
    selected->frames_seen!=selected->span||post_frame!=selected->first_frame+selected->span){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INCOMPLETE);return 0;
 }
 selected->state=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE;
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;return 1;
}

int melee_web_cpu_interval_motor_history_capture_begin(uint64_t token,uint64_t world,
 const void* context,uint64_t context_epoch,uint32_t pre_frame)
{
 if(motor.capture_enabled)return 0; /* Never clear a live or failed journal. */
 if(!active||!token||token!=epoch||!melee_web_cpu_interval_admit(token))return 0;
 memset(&motor,0,sizeof(motor));
 motor.evidence.schema=1;motor.evidence.bytes=sizeof(motor.evidence);
 motor.evidence.mode=2;motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 motor.evidence.published=0;
 motor.evidence.commit_state=MELEE_WEB_CPU_MOTOR_COMMIT_PENDING;
 motor.capture_enabled=1;motor.history_mode=1;motor.token=token;motor.world=world;
 motor.context=context;motor.context_epoch=context_epoch;
 motor.span=MELEE_WEB_CPU_MOTOR_MAX_SPAN;motor.first_frame=pre_frame;
 if(!world||!context||!context_epoch||pre_frame>UINT32_MAX-MELEE_WEB_CPU_MOTOR_MAX_SPAN){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INVALID_OWNER);return 0;
 }
 sequence_init(&motor.evidence.baseline,MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE);
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_BASELINE;
 return 1;
}

int melee_web_cpu_interval_motor_history_begin_incarnation(uint64_t token,
 uint64_t incarnation,uint32_t source_frame)
{
 if(!motor.capture_enabled||!motor.history_mode||!incarnation||incarnation>3u||
    !melee_web_cpu_interval_admit(token)||token!=motor.token||motor.frame_open||
    motor.history_replay_active||motor.history_restore_count>2u){
  if(motor.capture_enabled&&motor.history_mode)motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_LANE);
  return 0;
 }
 if(!motor.history_incarnation){
  if(incarnation!=1u||source_frame!=motor.first_frame||motor.history_sealed||
     motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_BASELINE||
     motor.evidence.baseline.state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
     motor.evidence.baseline.frames_seen!=0u){
   motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_LANE);return 0;
  }
  motor.history_incarnation=incarnation;
  return 1;
 }
 if(motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_NONE||!motor.history_sealed||
    !sequence_owner_matches(&motor.evidence.selected)||
    motor.evidence.selected.state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE||
    source_frame!=motor.history_end_frame||motor.history_incarnation>=3u||
    incarnation!=motor.history_incarnation+1u){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_LANE);return 0;
 }
 motor.history_incarnation=incarnation;
 motor.history_sealed=0;
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED;
 motor.evidence.selected.state=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE;
 motor.evidence.selected.span=MELEE_WEB_CPU_MOTOR_MAX_SPAN;
 return 1;
}

int melee_web_cpu_interval_motor_history_seal(uint64_t token,uint64_t incarnation,
 uint32_t actual_end)
{
 if(!motor.capture_enabled||!motor.history_mode||!incarnation||
    incarnation!=motor.history_incarnation||!melee_web_cpu_interval_admit(token)||
    token!=motor.token||motor.frame_open||motor.history_sealed||motor.history_replay_active||
    (motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_BASELINE&&
     motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED)){
  if(motor.capture_enabled&&motor.history_mode)motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_LANE);
  return 0;
 }
 MeleeWebCpuMotorSequence* selected=active_sequence();
 if(!selected||!sequence_owner_matches(selected)||
    selected->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
    selected->frames_seen<1u||selected->frames_seen>MELEE_WEB_CPU_MOTOR_MAX_SPAN||
    actual_end!=motor.first_frame+selected->frames_seen||
    (motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE&&
     (incarnation!=1u||motor.history_restore_count||
      motor.evidence.baseline.state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE))||
    (motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED&&
     (motor.history_incarnation==0u||
      (motor.history_replay_active&&incarnation!=motor.history_incarnation)))){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INCOMPLETE);return 0;
 }
 selected->span=selected->frames_seen;
 selected->state=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE;
 if(motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE){
  motor.evidence.selected=motor.evidence.baseline;
  motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_BASELINE;
 }else{
  motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED;
 }
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 motor.history_end_frame=actual_end;motor.history_sealed=1;
 return 1;
}

int melee_web_cpu_interval_motor_history_restore_admit(uint64_t token,
 uint64_t incarnation,uint32_t actual_end)
{
 if(!motor.capture_enabled||!motor.history_mode||!incarnation||
    incarnation!=motor.history_incarnation||motor.history_restore_count>=2u||
    !motor.history_sealed||motor.history_replay_active||motor.frame_open||
    motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_NONE||
    actual_end!=motor.history_end_frame||actual_end<=motor.first_frame||
    actual_end-motor.first_frame>MELEE_WEB_CPU_MOTOR_MAX_SPAN||
    !melee_web_cpu_interval_admit(token)||token!=motor.token||
    !sequence_owner_matches(&motor.evidence.baseline)||
    motor.evidence.baseline.state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE||
    !sequence_owner_matches(&motor.evidence.selected)||
    motor.evidence.selected.state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE||
    motor.evidence.selected.frames_seen!=actual_end-motor.first_frame||
    (motor.evidence.selected_lane!=MELEE_WEB_CPU_MOTOR_LANE_BASELINE&&
     motor.evidence.selected_lane!=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED))return 0;
 return 1;
}

void melee_web_cpu_interval_motor_history_restore_commit(uint64_t token,
 uint64_t incarnation,uint32_t actual_end)
{
 /* Identity was checked by restore_admit before the aggregate PRE commit.
  * No allocation, callback, or fallible admission belongs in this commit. */
 (void)token;
 (void)incarnation;
 sequence_init(&motor.evidence.selected,MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE);
 motor.evidence.selected.span=MELEE_WEB_CPU_MOTOR_MAX_SPAN;
 motor.evidence.selected_lane=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED;
 motor.history_restore_count++;motor.history_replay_active=1;
 motor.history_replay_end=actual_end;motor.history_sealed=0;
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED;
 motor.restore_used=1;
}

int melee_web_cpu_interval_motor_history_replay_finish(uint64_t token,
 uint64_t incarnation,uint32_t actual_end)
{
 MeleeWebCpuMotorSequence* selected=&motor.evidence.selected;
 if(!motor.capture_enabled||!motor.history_mode||!incarnation||
    incarnation!=motor.history_incarnation||!motor.history_replay_active||
    actual_end!=motor.history_replay_end||actual_end<=motor.first_frame||
    actual_end-motor.first_frame>MELEE_WEB_CPU_MOTOR_MAX_SPAN||
    !melee_web_cpu_interval_admit(token)||token!=motor.token||motor.frame_open||
    motor.active_lane!=MELEE_WEB_CPU_MOTOR_LANE_CORRECTED||
    !sequence_owner_matches(selected)||
    selected->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE||
    selected->frames_seen!=actual_end-motor.first_frame){
  motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_INCOMPLETE);return 0;
 }
 selected->span=selected->frames_seen;
 selected->state=MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE;
 motor.active_lane=MELEE_WEB_CPU_MOTOR_LANE_NONE;
 motor.history_replay_active=0;motor.history_end_frame=actual_end;
 motor.history_sealed=1;
 return 1;
}

int melee_web_cpu_interval_motor_history_retire_admit(uint64_t token,
 uint64_t incarnation,uint32_t actual_end)
{
 return motor.capture_enabled&&motor.history_mode&&incarnation&&
    incarnation==motor.history_incarnation&&incarnation<=3u&&
    motor.history_restore_count<=2u&&motor.history_sealed&&
    !motor.history_replay_active&&!motor.frame_open&&
    motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_NONE&&
    actual_end==motor.history_end_frame&&actual_end>motor.first_frame&&
    actual_end-motor.first_frame<=MELEE_WEB_CPU_MOTOR_MAX_SPAN&&
    melee_web_cpu_interval_admit(token)&&token==motor.token&&
    sequence_owner_matches(&motor.evidence.baseline)&&
    motor.evidence.baseline.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
    sequence_owner_matches(&motor.evidence.selected)&&
    motor.evidence.selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
    motor.evidence.selected.frames_seen==actual_end-motor.first_frame&&
    motor.evidence.failure==MELEE_WEB_CPU_MOTOR_FAILURE_NONE;
}

void melee_web_cpu_interval_motor_history_retire_commit(uint64_t token,
 uint64_t incarnation,uint32_t actual_end)
{
 if(!melee_web_cpu_interval_motor_history_retire_admit(token,incarnation,actual_end))return;
 const MeleeWebCpuMotorSequence* selected=&motor.evidence.selected;
 const uint64_t hash=motor_hash(selected);
 motor.evidence.commit_state=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY;
 motor.evidence.published=0;
 memset(&motor.receipt,0,sizeof(motor.receipt));
 motor.receipt.schema=1;motor.receipt.bytes=sizeof(motor.receipt);
 motor.receipt.status=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY;
 motor.receipt.selected_lane=motor.evidence.selected_lane;motor.receipt.published=0;
 motor.receipt.event_count=selected->event_count;motor.receipt.first_frame=selected->first_frame;
 motor.receipt.span=selected->span;motor.receipt.token_lo=low32(token);motor.receipt.token_hi=high32(token);
 motor.receipt.world_lo=selected->world_lo;motor.receipt.world_hi=selected->world_hi;
 motor.receipt.context_epoch_lo=selected->context_epoch_lo;
 motor.receipt.context_epoch_hi=selected->context_epoch_hi;
 motor.receipt.event_hash_lo=low32(hash);motor.receipt.event_hash_hi=high32(hash);
}

const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_history_evidence_for(
 uint64_t token,uint64_t world,const void* context,uint64_t context_epoch,
 uint32_t pre_frame,uint64_t incarnation,uint32_t actual_end)
{
 if(!motor.history_mode||!motor.history_sealed||incarnation!=motor.history_incarnation||
    actual_end!=motor.history_end_frame||motor.first_frame!=pre_frame||
    !motor_owner_matches(token,context,world,context_epoch)||
    !sequence_owner_matches(&motor.evidence.baseline)||
    !sequence_owner_matches(&motor.evidence.selected))return NULL;
 return &motor.evidence;
}
int melee_web_cpu_interval_motor_commit_admit(uint64_t token,uint64_t world,
 const void* context,uint64_t context_epoch,uint32_t first_frame,uint32_t span)
{
 if(!motor.capture_enabled)return 1;
 const MeleeWebCpuMotorSequence* baseline=&motor.evidence.baseline;
 const MeleeWebCpuMotorSequence* selected=&motor.evidence.selected;
 return melee_web_cpu_interval_admit(token)&&motor_owner_matches(token,context,world,context_epoch)&&
  motor.evidence.mode==1u&&motor.evidence.published==0u&&!motor.frame_open&&
  motor.evidence.commit_state==MELEE_WEB_CPU_MOTOR_COMMIT_PENDING&&
  motor.receipt.status==0u&&motor.evidence.failure==MELEE_WEB_CPU_MOTOR_FAILURE_NONE&&
  motor.active_lane==MELEE_WEB_CPU_MOTOR_LANE_NONE&&first_frame==baseline->first_frame&&
  span==baseline->span&&baseline->state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
  selected->state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
  (motor.evidence.selected_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE||
   motor.evidence.selected_lane==MELEE_WEB_CPU_MOTOR_LANE_REPLAY||
   motor.evidence.selected_lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED)&&
  sequence_owner_matches(baseline)&&sequence_owner_matches(selected)&&
  selected->frames_seen==selected->span;
}
static uint64_t motor_hash(const MeleeWebCpuMotorSequence* sequence)
{
 uint64_t hash=UINT64_C(14695981039346656037);
 for(uint32_t i=0;i<sequence->event_count&&i<MELEE_WEB_CPU_MOTOR_MAX_EVENTS;i++){
  const uint32_t fields[4]={sequence->events[i].source_frame,sequence->events[i].channel,
   sequence->events[i].command,sequence->events[i].ordinal};
  for(unsigned j=0;j<4;j++)for(unsigned b=0;b<4;b++){
   hash^=(uint8_t)(fields[j]>>(b*8));hash*=UINT64_C(1099511628211);
  }
 }
 return hash;
}
void melee_web_cpu_interval_motor_capture_commit(uint64_t token)
{
 if(!motor.capture_enabled)return;
 if(!melee_web_cpu_interval_motor_commit_admit(token,motor.world,motor.context,
    motor.context_epoch,motor.first_frame,motor.span))return;
 const MeleeWebCpuMotorSequence* selected=&motor.evidence.selected;
 const uint64_t hash=motor_hash(selected);
 motor.evidence.commit_state=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY;
 motor.evidence.published=0;
 memset(&motor.receipt,0,sizeof(motor.receipt));
 motor.receipt.schema=1;motor.receipt.bytes=sizeof(motor.receipt);
 motor.receipt.status=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY;
 motor.receipt.selected_lane=motor.evidence.selected_lane;motor.receipt.published=0;
 motor.receipt.event_count=selected->event_count;motor.receipt.first_frame=selected->first_frame;
 motor.receipt.span=selected->span;motor.receipt.token_lo=low32(token);motor.receipt.token_hi=high32(token);
 motor.receipt.world_lo=selected->world_lo;motor.receipt.world_hi=selected->world_hi;
 motor.receipt.context_epoch_lo=selected->context_epoch_lo;
 motor.receipt.context_epoch_hi=selected->context_epoch_hi;
 motor.receipt.event_hash_lo=low32(hash);motor.receipt.event_hash_hi=high32(hash);
}
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence(void)
{return &motor.evidence;}
const MeleeWebCpuMotorEvidence* melee_web_cpu_interval_motor_evidence_for(
 uint64_t token,uint64_t world,const void* context,uint64_t context_epoch,
 uint32_t first_frame,uint32_t span)
{
 const MeleeWebCpuMotorSequence* baseline=&motor.evidence.baseline;
 if(!motor.capture_enabled||!motor_owner_matches(token,context,world,context_epoch)||
    motor.first_frame!=first_frame||motor.span!=span||
    !sequence_owner_matches(baseline))return NULL;
 return &motor.evidence;
}
uint32_t melee_web_cpu_interval_motor_evidence_bytes(void)
{return sizeof(motor.evidence);}
const MeleeWebCpuMotorCommitReceipt* melee_web_cpu_interval_motor_commit_receipt(void)
{
 if(!motor.retired||motor.capture_enabled||active||
    motor.evidence.commit_state!=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY||
    motor.evidence.failure!=MELEE_WEB_CPU_MOTOR_FAILURE_NONE||failure||
    motor.receipt.status!=MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY||
    motor.receipt.published!=0u||
    join64(motor.receipt.token_lo,motor.receipt.token_hi)!=motor.token||
    join64(motor.receipt.world_lo,motor.receipt.world_hi)!=motor.world||
    join64(motor.receipt.context_epoch_lo,motor.receipt.context_epoch_hi)!=motor.context_epoch||
    motor.receipt.first_frame!=motor.first_frame||
    (motor.history_mode?motor.receipt.span!=motor.history_end_frame-motor.first_frame:
                        motor.receipt.span!=motor.span)||
    motor.receipt.selected_lane!=motor.evidence.selected_lane||
    motor.receipt.event_count!=motor.evidence.selected.event_count||
    join64(motor.receipt.event_hash_lo,motor.receipt.event_hash_hi)!=
        motor_hash(&motor.evidence.selected))
  return NULL;
 return &motor.receipt;
}
uint32_t melee_web_cpu_interval_motor_commit_receipt_bytes(void)
{return sizeof(motor.receipt);}
void melee_web_cpu_interval_pad_motor(uint32_t channel,uint32_t command)
{
 if(!active){PADControlMotor(channel,command);return;}
 if(motor.capture_enabled){
  MeleeWebCpuMotorSequence* sequence=active_sequence();
  if(!melee_web_cpu_interval_admit(motor.token)||!motor.frame_open||!sequence||
     (sequence->state!=MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE)||
     channel>3u||command>2u||sequence->event_count>=MELEE_WEB_CPU_MOTOR_MAX_EVENTS){
   motor_refuse(sequence&&sequence->event_count>=MELEE_WEB_CPU_MOTOR_MAX_EVENTS?
    MELEE_WEB_CPU_MOTOR_FAILURE_OVERFLOW:
    (channel>3u||command>2u?MELEE_WEB_CPU_MOTOR_FAILURE_COMMAND:MELEE_WEB_CPU_MOTOR_FAILURE_FRAME));
   return;
  }
  const uint32_t channel_bit=1u<<channel;
  if(motor.frame_channels&channel_bit){motor_refuse(MELEE_WEB_CPU_MOTOR_FAILURE_DUPLICATE_CHANNEL);return;}
  motor.frame_channels|=channel_bit;
  MeleeWebCpuMotorEvent* event=&sequence->events[sequence->event_count];
  event->source_frame=motor.open_frame;event->channel=channel;event->command=command;
  event->ordinal=sequence->event_count;++sequence->event_count;return;
 }
 (void)melee_web_cpu_interval_attempt(MELEE_WEB_CPU_EFFECT_MOTOR);
}
