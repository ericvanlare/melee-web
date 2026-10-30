#include "gameplay_cpu_interval.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void PADControlMotor(unsigned channel,unsigned command)
{ (void)channel;(void)command;assert(!"history journal must never dispatch hardware rumble"); }

static void tick(uint64_t token,const void* context,uint64_t world,
                 uint64_t owner_epoch,uint32_t frame,unsigned command)
{
    assert(melee_web_cpu_interval_motor_frame_begin(token,context,world,
        owner_epoch,frame));
    melee_web_cpu_interval_pad_motor(frame&3u,command);
    if(!melee_web_cpu_interval_motor_frame_end(token,context,world,
        owner_epoch,frame)){
        fprintf(stderr,"history frame end failed at source frame %u command %u failure %u\n",
            frame,command,melee_web_cpu_interval_failure());
        assert(!"history frame end refused");
    }
}

int main(void)
{
    const uint64_t world=0x1234,owner_epoch=0x5678;
    static unsigned context;
    uint64_t token=melee_web_cpu_interval_begin();assert(token);
    assert(melee_web_cpu_interval_motor_history_capture_begin(token,world,
        &context,owner_epoch,100));
    assert(melee_web_cpu_interval_motor_history_begin_incarnation(token,1,100));

    tick(token,&context,world,owner_epoch,100,0);
    tick(token,&context,world,owner_epoch,101,1);
    assert(melee_web_cpu_interval_motor_history_seal(token,1,102));
    const MeleeWebCpuMotorEvidence* evidence=
        melee_web_cpu_interval_motor_history_evidence_for(token,world,&context,
            owner_epoch,100,1,102);
    assert(evidence&&evidence->mode==2&&evidence->baseline.event_count==2&&
           evidence->selected.event_count==2&&evidence->selected_lane==
             MELEE_WEB_CPU_MOTOR_LANE_BASELINE);
    assert(evidence->baseline.events[0].source_frame==100&&
           evidence->baseline.events[1].ordinal==1);
    assert(!melee_web_cpu_interval_motor_history_restore_admit(token+1,1,102));
    assert(!melee_web_cpu_interval_motor_history_restore_admit(token,2,102));
    assert(!melee_web_cpu_interval_motor_history_restore_admit(token,1,101));
    assert(evidence->selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_COMPLETE&&
           evidence->selected.event_count==2&&
           evidence->selected.events[0].command==0&&
           evidence->selected.events[1].command==1);
    assert(melee_web_cpu_interval_motor_history_restore_admit(token,1,102));
    melee_web_cpu_interval_motor_history_restore_commit(token,1,102);
    evidence=melee_web_cpu_interval_motor_evidence();
    assert(evidence->baseline.event_count==2&&evidence->selected.event_count==0&&
           evidence->selected.state==MELEE_WEB_CPU_MOTOR_SEQUENCE_INCOMPLETE);
    assert(!melee_web_cpu_interval_motor_history_restore_admit(token,1,102));
    tick(token,&context,world,owner_epoch,100,2);
    tick(token,&context,world,owner_epoch,101,2);
    assert(melee_web_cpu_interval_motor_history_replay_finish(token,1,102));
    evidence=melee_web_cpu_interval_motor_history_evidence_for(token,world,&context,
        owner_epoch,100,1,102);
    assert(evidence&&evidence->baseline.events[0].command==0&&
           evidence->selected.event_count==2&&evidence->selected.events[0].command==2&&
           evidence->selected.events[1].ordinal==1);

    /* A correction is complete at this same END. Only after the selected
     * endpoint is copied/closed may the next incarnation append to its tape. */
    assert(melee_web_cpu_interval_motor_history_begin_incarnation(token,2,102));
    tick(token,&context,world,owner_epoch,102,0);
    tick(token,&context,world,owner_epoch,103,1);
    assert(melee_web_cpu_interval_motor_history_seal(token,2,104));
    assert(melee_web_cpu_interval_motor_history_restore_admit(token,2,104));
    melee_web_cpu_interval_motor_history_restore_commit(token,2,104);
    evidence=melee_web_cpu_interval_motor_evidence();
    assert(evidence->baseline.event_count==2&&evidence->selected.event_count==0);
    tick(token,&context,world,owner_epoch,100,0);
    tick(token,&context,world,owner_epoch,101,1);
    tick(token,&context,world,owner_epoch,102,2);
    tick(token,&context,world,owner_epoch,103,2);
    assert(melee_web_cpu_interval_motor_history_replay_finish(token,2,104));
    evidence=melee_web_cpu_interval_motor_history_evidence_for(token,world,&context,
        owner_epoch,100,2,104);
    assert(evidence&&evidence->selected.event_count==4&&
           evidence->selected.events[0].ordinal==0&&
           evidence->selected.events[3].ordinal==3);

    assert(melee_web_cpu_interval_motor_history_begin_incarnation(token,3,104));
    tick(token,&context,world,owner_epoch,104,0);
    tick(token,&context,world,owner_epoch,105,1);
    tick(token,&context,world,owner_epoch,106,2);
    assert(melee_web_cpu_interval_motor_history_seal(token,3,107));
    assert(!melee_web_cpu_interval_motor_history_restore_admit(token,3,107));
    assert(melee_web_cpu_interval_motor_history_retire_admit(token,3,107));
    melee_web_cpu_interval_motor_history_retire_commit(token,3,107);
    assert(melee_web_cpu_interval_end(token));
    const MeleeWebCpuMotorCommitReceipt* receipt=
        melee_web_cpu_interval_motor_commit_receipt();
    assert(receipt&&receipt->span==7&&receipt->event_count==7&&
           receipt->selected_lane==MELEE_WEB_CPU_MOTOR_LANE_CORRECTED);
    evidence=melee_web_cpu_interval_motor_evidence();
    assert(evidence->baseline.events[0].command==0&&
           evidence->selected.events[0].source_frame==100&&
           evidence->selected.events[6].source_frame==106&&
           evidence->selected.events[6].ordinal==6);

    /* Seven ticks is the actual PRE-relative ceiling, including continued
     * forward work after a selected correction. The eighth frame refuses. */
    token=melee_web_cpu_interval_begin();assert(token);
    assert(melee_web_cpu_interval_motor_history_capture_begin(token,world,
        &context,owner_epoch,200));
    assert(melee_web_cpu_interval_motor_history_begin_incarnation(token,1,200));
    for(uint32_t frame=200;frame<207;++frame)
        tick(token,&context,world,owner_epoch,frame,frame%3u);
    const MeleeWebCpuMotorEvidence* bounded=melee_web_cpu_interval_motor_evidence();
    assert(bounded->baseline.frames_seen==7&&bounded->baseline.event_count==7);
    assert(!melee_web_cpu_interval_motor_frame_begin(token,&context,world,
        owner_epoch,207));
    assert(bounded->baseline.frames_seen==7&&bounded->baseline.event_count==7);
    assert(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_MOTOR);
    assert(melee_web_cpu_interval_end(token));

    /* Duplicate/stale incarnation admission fails closed without reviving a
     * previous journal. Legacy fixed-span behavior is checked independently. */
    token=melee_web_cpu_interval_begin();assert(token);
    assert(melee_web_cpu_interval_motor_history_capture_begin(token,world,
        &context,owner_epoch,300));
    assert(melee_web_cpu_interval_motor_history_begin_incarnation(token,1,300));
    assert(!melee_web_cpu_interval_motor_history_begin_incarnation(token,1,300));
    assert(melee_web_cpu_interval_end(token));
    puts("Actual CPU motor retained-history journal passed two same-PRE restores, bounded continuation, stale-ticket refusal and legacy-safe control");
}
