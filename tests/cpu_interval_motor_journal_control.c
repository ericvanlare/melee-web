/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual upstream HSD_PadRumbleInterpret body is inserted by the Python
 * control. This checks source state and the private capture-only wrapper;
 * it does not claim physical haptics or browser timing. */
#include "gameplay_cpu_interval.h"
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Fixture assertions must remain enabled
#endif

PadLibData HSD_PadLibData;
HSD_RumbleData HSD_Rumble_804C22E0[4];
static unsigned provider_calls;
static uint32_t provider_channel[16],provider_command[16];
void PADControlMotor(unsigned channel,unsigned command)
{
 assert(provider_calls<16);provider_channel[provider_calls]=channel;
 provider_command[provider_calls]=command;++provider_calls;
}
int HSD_PadRumbleInterpret1(HSD_PadRumbleListData* item,u8* status)
{(void)item;(void)status;assert(!"fixture has no list items");return 0;}
void HSD_PadRumbleFree(HSD_RumbleData* owner,HSD_PadRumbleListData* item)
{(void)owner;(void)item;assert(!"fixture has no list items");}

#define PADControlMotor melee_web_cpu_interval_pad_motor
@ACTUAL_INTERPRETER@
#undef PADControlMotor

static void source_init(void)
{
 memset(&HSD_PadLibData,0,sizeof(HSD_PadLibData));
 memset(HSD_Rumble_804C22E0,0,sizeof(HSD_Rumble_804C22E0));
 HSD_PadLibData.rumble_info.unk2=0;
 for(unsigned i=0;i<4;i++)HSD_Rumble_804C22E0[i].listdatap=NULL;
 HSD_Rumble_804C22E0[0].direct_status=1;
 HSD_Rumble_804C22E0[1].direct_status=2;
 HSD_Rumble_804C22E0[2].direct_status=0;
 HSD_Rumble_804C22E0[2].last_status=2;
 HSD_Rumble_804C22E0[3].direct_status=1;
 provider_calls=0;memset(provider_channel,0,sizeof(provider_channel));
 memset(provider_command,0,sizeof(provider_command));
}
static void frame(uint64_t token,const void* context,uint32_t frame_number)
{
 assert(melee_web_cpu_interval_motor_frame_begin(token,context,9,11,frame_number));
 HSD_PadRumbleInterpret();
 assert(melee_web_cpu_interval_motor_frame_end(token,context,9,11,frame_number));
}
static uint32_t run_selected(uint32_t lane,const void* context)
{
 source_init();
 const HSD_RumbleData source_before[4]={
  HSD_Rumble_804C22E0[0],HSD_Rumble_804C22E0[1],
  HSD_Rumble_804C22E0[2],HSD_Rumble_804C22E0[3]};
 const PadLibData pad_before=HSD_PadLibData;
 uint64_t token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,context,11,100,2));
 assert(melee_web_cpu_interval_motor_evidence_for(token,9,context,11,100,2));
 int other_context=0;
 assert(!melee_web_cpu_interval_motor_evidence_for(token+1,9,context,11,100,2));
 assert(!melee_web_cpu_interval_motor_evidence_for(token,10,context,11,100,2));
 assert(!melee_web_cpu_interval_motor_evidence_for(token,9,&other_context,11,100,2));
 assert(!melee_web_cpu_interval_motor_evidence_for(token,9,context,12,100,2));
 assert(!melee_web_cpu_interval_motor_evidence_for(token,9,context,11,101,2));
 assert(!melee_web_cpu_interval_motor_evidence_for(token,9,context,11,100,1));
 frame(token,context,100);frame(token,context,101);
 assert(provider_calls==0); /* Capture-only: no PAD provider call. */
 assert(!melee_web_cpu_interval_motor_capture_begin(token,9,context,11,999,1));
 assert(melee_web_cpu_interval_motor_evidence()->baseline.event_count==4&&
        melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_NONE);
 HSD_RumbleData baseline_after[4];memcpy(baseline_after,HSD_Rumble_804C22E0,sizeof(baseline_after));
 assert(!memcmp(&HSD_PadLibData,&pad_before,sizeof(pad_before)));
 assert(melee_web_cpu_interval_motor_baseline_seal(token,102));
 const MeleeWebCpuMotorEvidence* evidence=melee_web_cpu_interval_motor_evidence();
 assert(evidence&&evidence->schema==1&&evidence->bytes==1016&&evidence->mode==1&&
        evidence->published==0&&evidence->selected_lane==MELEE_WEB_CPU_MOTOR_LANE_BASELINE);
 assert(evidence->baseline.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
        evidence->baseline.span==2&&evidence->baseline.first_frame==100&&
        evidence->baseline.frames_seen==2&&evidence->baseline.event_count==4);
 assert(evidence->selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
        evidence->selected.event_count==4);
 const uint32_t expected_command[4]={0,1,2,0};
 for(unsigned i=0;i<4;i++){
  const MeleeWebCpuMotorEvent* event=&evidence->baseline.events[i];
  assert(event->source_frame==100&&event->channel==i&&
         event->command==expected_command[i]&&event->ordinal==i);
 }

 if(lane==MELEE_WEB_CPU_MOTOR_LANE_REPLAY||lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED){
  assert(melee_web_cpu_interval_motor_restore_admit(token,lane));
  melee_web_cpu_interval_motor_restore_commit(token,lane);
  /* This reproduces an exact typed restore of the original source rumble
   * owner and then a corrected direct status for channel 3. */
  memcpy(HSD_Rumble_804C22E0,source_before,sizeof(source_before));
  HSD_Rumble_804C22E0[3].direct_status=2;
  frame(token,context,100);frame(token,context,101);
  assert(provider_calls==0);
  assert(melee_web_cpu_interval_motor_selected_finish(token,102));
  evidence=melee_web_cpu_interval_motor_evidence();
  assert(evidence->selected_lane==lane&&
         evidence->selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
         evidence->selected.frames_seen==2&&evidence->selected.event_count==4);
  assert(evidence->baseline.event_count==4&&evidence->baseline.events[3].command==0);
  assert(evidence->selected.events[3].source_frame==100&&
         evidence->selected.events[3].channel==3&&
         evidence->selected.events[3].command==1&&
         evidence->selected.events[3].ordinal==3);
  assert(!melee_web_cpu_interval_motor_commit_admit(token+1,9,context,11,100,2));
  melee_web_cpu_interval_motor_capture_commit(token+1);
  assert(evidence->commit_state==MELEE_WEB_CPU_MOTOR_COMMIT_PENDING&&
         !melee_web_cpu_interval_motor_commit_receipt());
 }
 return (uint32_t)token;
}
static void test_source_state_and_lanes(void)
{
 source_init();
 const HSD_RumbleData source_before[4]={
  HSD_Rumble_804C22E0[0],HSD_Rumble_804C22E0[1],
  HSD_Rumble_804C22E0[2],HSD_Rumble_804C22E0[3]};
 const PadLibData pad_before=HSD_PadLibData;
 HSD_PadRumbleInterpret();
 assert(provider_calls==4);
 const uint32_t expected_command[4]={0,1,2,0};
 for(unsigned i=0;i<4;i++)assert(provider_channel[i]==i&&provider_command[i]==expected_command[i]);
 HSD_RumbleData original_after[4];memcpy(original_after,HSD_Rumble_804C22E0,sizeof(original_after));

 static int context;
 source_init();uint64_t token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,100,2));
 frame(token,&context,100);frame(token,&context,101);assert(provider_calls==0);
 assert(!memcmp(original_after,HSD_Rumble_804C22E0,sizeof(original_after)));
 assert(!memcmp(&pad_before,&HSD_PadLibData,sizeof(pad_before)));
 assert(melee_web_cpu_interval_motor_baseline_seal(token,102));
 assert(melee_web_cpu_interval_end(token));

 const uint32_t lanes[2]={MELEE_WEB_CPU_MOTOR_LANE_REPLAY,MELEE_WEB_CPU_MOTOR_LANE_CORRECTED};
 for(unsigned i=0;i<2;i++){
  /* A new interval owns one selected lane; the previous receipt is consumed
   * before interval_begin clears its private evidence storage. */
  token=run_selected(lanes[i],&context);
  assert(melee_web_cpu_interval_motor_commit_admit(token,9,&context,11,100,2));
  melee_web_cpu_interval_motor_capture_commit(token);
  const MeleeWebCpuMotorEvidence* evidence=melee_web_cpu_interval_motor_evidence();
  assert(!melee_web_cpu_interval_motor_commit_receipt());
  assert(evidence->commit_state==MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY&&evidence->published==0);
  melee_web_cpu_interval_motor_capture_commit(token);
  assert(evidence->commit_state==MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY&&
         !melee_web_cpu_interval_motor_commit_receipt());
  assert(melee_web_cpu_interval_end(token));
  const MeleeWebCpuMotorCommitReceipt* receipt=melee_web_cpu_interval_motor_commit_receipt();
  assert(receipt&&melee_web_cpu_interval_motor_commit_receipt_bytes()==64&&
         receipt->schema==1&&receipt->bytes==64&&
         receipt->status==MELEE_WEB_CPU_MOTOR_COMMIT_CAPTURE_ONLY&&
         receipt->selected_lane==lanes[i]&&receipt->published==0&&
         receipt->event_count==4&&receipt->first_frame==100&&receipt->span==2&&
         receipt->token_lo==(uint32_t)token&&receipt->token_hi==(uint32_t)(token>>32)&&
         receipt->world_lo==9&&receipt->world_hi==0&&
         receipt->context_epoch_lo==11&&receipt->context_epoch_hi==0&&
         (receipt->event_hash_lo||receipt->event_hash_hi));
 }
 puts("Actual HSD rumble interpreter preserves source state; ordered capture-only baseline/replay/corrected receipts passed");
}
static void test_refusals_and_legacy(void)
{
 static int context;
 source_init();HSD_PadRumbleInterpret();
 HSD_RumbleData legacy_after[4];memcpy(legacy_after,HSD_Rumble_804C22E0,sizeof(legacy_after));
 source_init();
 uint64_t token=melee_web_cpu_interval_begin();assert(token);
 HSD_PadRumbleInterpret();
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(!memcmp(legacy_after,HSD_Rumble_804C22E0,sizeof(legacy_after)));
 assert(melee_web_cpu_interval_end(token));

 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,200,1));
 assert(!melee_web_cpu_interval_motor_frame_begin(token+1,&context,9,11,200));
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_STALE_TOKEN);
 assert(melee_web_cpu_interval_end(token));

 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,300,1));
 melee_web_cpu_interval_pad_motor(0,1); /* out-of-frame */
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_FRAME);
 assert(melee_web_cpu_interval_end(token));

 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,400,1));
 assert(melee_web_cpu_interval_motor_frame_begin(token,&context,9,11,400));
 melee_web_cpu_interval_pad_motor(0,1);melee_web_cpu_interval_pad_motor(0,0);
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_DUPLICATE_CHANNEL);
 assert(melee_web_cpu_interval_end(token));

 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,500,1));
 assert(melee_web_cpu_interval_motor_frame_begin(token,&context,9,11,500));
 melee_web_cpu_interval_pad_motor(4,0);
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_COMMAND);
 assert(melee_web_cpu_interval_end(token));

 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,600,7));
 for(uint32_t frame_no=600;frame_no<607;frame_no++){
  assert(melee_web_cpu_interval_motor_frame_begin(token,&context,9,11,frame_no));
  for(uint32_t channel=0;channel<4;channel++)melee_web_cpu_interval_pad_motor(channel,channel%3);
  assert(melee_web_cpu_interval_motor_frame_end(token,&context,9,11,frame_no));
 }
 assert(melee_web_cpu_interval_motor_evidence()->baseline.event_count==28);
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==0);
 assert(melee_web_cpu_interval_end(token));

 /* Fault-inject a full fixed slot to prove overflow refuses before any
  * provider call; normal source permits at most 4x7 and cannot reach it. */
 token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,9,&context,11,700,1));
 assert(melee_web_cpu_interval_motor_frame_begin(token,&context,9,11,700));
 MeleeWebCpuMotorEvidence* mutable_evidence=(MeleeWebCpuMotorEvidence*)
  melee_web_cpu_interval_motor_evidence();
 mutable_evidence->baseline.event_count=28;
 melee_web_cpu_interval_pad_motor(0,0);
 assert(provider_calls==0&&melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
 assert(melee_web_cpu_interval_motor_evidence()->failure==MELEE_WEB_CPU_MOTOR_FAILURE_OVERFLOW);
 assert(melee_web_cpu_interval_end(token));
 puts("Legacy MOTOR barrier, token/frame/channel validation and fixed journal bounds passed without provider dispatch");
}
int main(void)
{
 test_source_state_and_lanes();test_refusals_and_legacy();return 0;
}
