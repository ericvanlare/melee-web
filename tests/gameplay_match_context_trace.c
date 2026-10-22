/* Integration helper for the real common/stage/effect/fighter-assets harness.
 * No source consumer is replaced. The caller keeps all asset owners alive. */
#include "gameplay_match_context.h"
#include "gameplay_match_flow.h"
#include <sysdolphin/baselib/gobj.h>
#include <melee/pl/player.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern u16 staleAttackInstance, unk_804D6480;
extern uint64_t melee_web_gameplay_generation(void);
static int same_range(const MeleeWebMatchContextRange* a,
    const MeleeWebMatchContextRange* b)
{
    return a->address==b->address&&a->byte_extent==b->byte_extent&&
        a->world_generation==b->world_generation&&a->owner_epoch==b->owner_epoch;
}
static int check_wait(const MeleeWebMatchStats* stats,char* error,size_t size)
{
    if(stats->motion_id!=14||stats->ground_or_air!=0||stats->live_fighters!=1||
       stats->camera_subjects!=1||stats->extra_model_objects!=8||stats->eye_count!=2||
       stats->eyes[0].image_is_base||stats->eyes[1].image_is_base||
       stats->eyes[0].image_index>=stats->eyes[0].image_count||
       stats->eyes[1].image_index>=stats->eyes[1].image_count){
        snprintf(error,size,"Expected original grounded Wait14, one fighter/subject, eight extra DObjs and two eyes; got motion%d ground%d fighters%u subjects%u DObjs%u eyes%u",
            stats->motion_id,stats->ground_or_air,stats->live_fighters,stats->camera_subjects,
            stats->extra_model_objects,stats->eye_count);return 0;
    }
    return 1;
}
int melee_web_match_context_trace(MeleeWebCollision* collision,
    const MeleeWebMatchSettings* settings, uint32_t ticks, char* error,size_t size)
{
    MeleeWebMatchContext* context=NULL;
    uint8_t* owner_bytes_before=NULL;
    static uint64_t previous_world_generation,previous_owner_epoch;
    MeleeWebMatchContextRange refused_range={0};
    MeleeWebMatchContextRange refused_sentinel={0};
    int range_marker=0;
    refused_range.address=&range_marker;refused_range.byte_extent=17;
    refused_range.world_generation=23;refused_range.owner_epoch=31;
    refused_sentinel=refused_range;
    if(melee_web_match_context_range_read(NULL,&refused_range,error,size)!=
           MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER||
       !same_range(&refused_range,&refused_sentinel)){
        snprintf(error,size,"Null match-context range query was not refused atomically");return 0;
    }
    MeleeWebMatchContext* wrong=(MeleeWebMatchContext*)(uintptr_t)1;
    if(melee_web_match_context_range_read(wrong,&refused_range,error,size)!=
           MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER||
       !same_range(&refused_range,&refused_sentinel)){
        snprintf(error,size,"Wrong match-context handle was not refused before dereference");return 0;
    }
    HSD_PadStatus pads[4];memcpy(pads,HSD_PadGameStatus,sizeof(pads));
    u32* previous_seed=seed_ptr;
    u16 previous_stale=staleAttackInstance,previous_attack=unk_804D6480;
    StaticPlayer prior=*Player_GetPtrForSlot(settings->player.slot);
    for(uint32_t slot=4;slot<6;slot++) {
        MeleeWebMatchSettings invalid=*settings;invalid.player.slot=slot;
        if((context=melee_web_match_begin(&invalid,collision,error,size))) {
            snprintf(error,size,"Match accepted player slot outside source PAD array");goto cleanup;
        }
    }
    uint64_t previous_cycle_epoch=0;
    MeleeWebMatchContextRange last_range={0};
    for(int cycle=0;cycle<2;cycle++){
        context=melee_web_match_begin(settings,collision,error,size);
        if(!context)goto cleanup;
        MeleeWebMatchContextRange range_before={0};
        if(melee_web_match_context_range_read(context,&range_before,error,size)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE||
           range_before.address!=context||range_before.byte_extent==0||
           range_before.world_generation!=melee_web_gameplay_generation()||
           !range_before.owner_epoch||
           (previous_cycle_epoch&&range_before.owner_epoch==previous_cycle_epoch)){
            snprintf(error,size,"Match-context range did not identify the current exact owner allocation");goto cleanup;
        }
        if(previous_world_generation&&
           (range_before.world_generation==previous_world_generation||
            range_before.owner_epoch==previous_owner_epoch)){
            snprintf(error,size,"Replacement context in a second world reused its world/lifetime identity");goto cleanup;
        }
        previous_cycle_epoch=range_before.owner_epoch;
        owner_bytes_before=(uint8_t*)malloc(range_before.byte_extent);
        if(!owner_bytes_before){snprintf(error,size,"Cannot allocate context alias-control copy");goto cleanup;}
        memcpy(owner_bytes_before,context,range_before.byte_extent);
        const size_t alias_interior_offset=_Alignof(MeleeWebMatchContextRange);
        if(range_before.byte_extent<alias_interior_offset+sizeof(MeleeWebMatchContextRange)){
            snprintf(error,size,"Context allocation is too small for interior alias controls");goto cleanup;
        }
        char alias_error[64];memset(alias_error,'E',sizeof(alias_error));
        char alias_error_before[sizeof(alias_error)];memcpy(alias_error_before,alias_error,sizeof(alias_error));
        MeleeWebMatchContextRange alias_output=range_before;
        const MeleeWebMatchContextRange alias_output_before=alias_output;
        char handle_error[128]={0};
        if(melee_web_match_context_range_read(NULL,&alias_output,handle_error,sizeof(handle_error))!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER||
           !same_range(&alias_output,&range_before)||
           memcmp(context,owner_bytes_before,range_before.byte_extent)){
            snprintf(error,size,"Null live-owner handle refusal changed output or context bytes");goto cleanup;
        }
        MeleeWebMatchContext* wrong_live_handle=(MeleeWebMatchContext*)(uintptr_t)1;
        if(melee_web_match_context_range_read(wrong_live_handle,&alias_output,handle_error,sizeof(handle_error))!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER||
           !same_range(&alias_output,&range_before)||
           memcmp(context,owner_bytes_before,range_before.byte_extent)){
            snprintf(error,size,"Wrong live-owner handle refusal changed output or context bytes");goto cleanup;
        }
        MeleeWebMatchContextRange* owner_base_output=(MeleeWebMatchContextRange*)context;
        if(melee_web_match_context_range_read(context,owner_base_output,alias_error,sizeof(alias_error))!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(context,owner_bytes_before,range_before.byte_extent)||
           memcmp(alias_error,alias_error_before,sizeof(alias_error))){
            snprintf(error,size,"Owner-base descriptor alias was not refused without writes");goto cleanup;
        }
        MeleeWebMatchContextRange* owner_interior_output=(MeleeWebMatchContextRange*)
            ((uint8_t*)context+alias_interior_offset);
        if(melee_web_match_context_range_read(context,owner_interior_output,alias_error,sizeof(alias_error))!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(context,owner_bytes_before,range_before.byte_extent)||
           memcmp(alias_error,alias_error_before,sizeof(alias_error))){
            snprintf(error,size,"Interior descriptor alias was not refused without writes");goto cleanup;
        }
        if(melee_web_match_context_range_read(context,&alias_output,(char*)context,16)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(context,owner_bytes_before,range_before.byte_extent)||
           !same_range(&alias_output,&alias_output_before)){
            snprintf(error,size,"Owner-base error alias was not refused without writes");goto cleanup;
        }
        if(melee_web_match_context_range_read(context,&alias_output,
               (char*)context+alias_interior_offset,16)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(context,owner_bytes_before,range_before.byte_extent)||
           !same_range(&alias_output,&alias_output_before)){
            snprintf(error,size,"Interior error alias was not refused without writes");goto cleanup;
        }
        if(melee_web_match_context_range_read(context,NULL,(char*)context,16)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(context,owner_bytes_before,range_before.byte_extent)){
            snprintf(error,size,"Null-output aliased error was not refused without writes");goto cleanup;
        }
        union MatchContextRangeAliasIO {
            MeleeWebMatchContextRange aligned;
            uint8_t bytes[sizeof(MeleeWebMatchContextRange)+16];
        } overlap_io;
        memset(overlap_io.bytes,0xA5,sizeof(overlap_io.bytes));
        uint8_t overlap_io_before[sizeof(overlap_io.bytes)];
        memcpy(overlap_io_before,overlap_io.bytes,sizeof(overlap_io.bytes));
        if(melee_web_match_context_range_read(context,&overlap_io.aligned,
               (char*)overlap_io.bytes+4,8)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
           memcmp(overlap_io.bytes,overlap_io_before,sizeof(overlap_io.bytes))||
           memcmp(context,owner_bytes_before,range_before.byte_extent)){
            snprintf(error,size,"Descriptor/error mutual alias was not refused without writes");goto cleanup;
        }
        if(!melee_web_match_create_fighter(context,error,size))goto cleanup;
        MeleeWebMatchStats before,after;
        if(!melee_web_match_stats(context,&before,error,size))goto cleanup;
        memcpy(owner_bytes_before,context,range_before.byte_extent);
        const u32* range_seed_before=seed_ptr;
        const u16 range_stale_before=staleAttackInstance,range_attack_before=unk_804D6480;
        HSD_PadStatus range_pads_before[4];
        memcpy(range_pads_before,HSD_PadGameStatus,sizeof(range_pads_before));
        MeleeWebMatchContextRange range_repeat={0};
        if(melee_web_match_context_range_read(context,&range_repeat,error,size)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE||
           !same_range(&range_before,&range_repeat)||
           !melee_web_match_stats(context,&after,error,size)||
           memcmp(&before,&after,sizeof(before))||seed_ptr!=range_seed_before||
           staleAttackInstance!=range_stale_before||unk_804D6480!=range_attack_before||
           memcmp(range_pads_before,HSD_PadGameStatus,sizeof(range_pads_before))){
            snprintf(error,size,"Read-only match-context range query changed owner/source state");goto cleanup;
        }
        HSD_GObj range_marker_gobj={0};
        HSD_GObj** range_callback_slots[]={&HSD_GObj_804D781C,&HSD_GObj_804D7818,&HSD_GObj_804D7814};
        for(unsigned callback=0;callback<3;callback++){
            *range_callback_slots[callback]=&range_marker_gobj;
            alias_output=range_before;
            const MeleeWebMatchContextRangeResult alias_result=
                melee_web_match_context_range_read(context,&alias_output,(char*)context,16);
            *range_callback_slots[callback]=NULL;
            if(alias_result!=MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_ARGUMENT||
               memcmp(context,owner_bytes_before,range_before.byte_extent)||
               !same_range(&alias_output,&range_before)){
                snprintf(error,size,"Callback-time owner error alias was not refused without writes");goto cleanup;
            }
            *range_callback_slots[callback]=&range_marker_gobj;
            refused_range=range_before;
            const MeleeWebMatchContextRangeResult result=
                melee_web_match_context_range_read(context,&refused_range,error,size);
            *range_callback_slots[callback]=NULL;
            MeleeWebMatchContextRange after_callback={0};
            if(result!=MELEE_WEB_MATCH_CONTEXT_RANGE_UNSAFE_BOUNDARY||
               !same_range(&refused_range,&range_before)||
               memcmp(context,owner_bytes_before,range_before.byte_extent)||
               melee_web_match_context_range_read(context,&after_callback,error,size)!=
                   MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE||
               !same_range(&after_callback,&range_before)){
                snprintf(error,size,"Match-context range callback refusal was not atomic");goto cleanup;
            }
        }
        uint32_t alias_test_ticks=0;
        if(ticks){
            MeleeWebMatchContextRange healthy_range={0};
            MeleeWebMatchStats healthy_step_before,healthy_step_after;
            if(melee_web_match_context_range_read(context,&healthy_range,error,size)!=
                   MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE||
               !same_range(&range_before,&healthy_range)||
               !melee_web_match_stats(context,&healthy_step_before,error,size)||
               !melee_web_match_step(context,1,error,size)||
               !melee_web_match_stats(context,&healthy_step_after,error,size)||
               healthy_step_after.ticks!=healthy_step_before.ticks+1){
                snprintf(error,size,"Healthy range read/source step failed after alias refusals");goto cleanup;
            }
            before=healthy_step_after;
            alias_test_ticks=1;
        }
        /* Let source collision and motion callbacks settle the constructor's
         * bind pose onto the floor. Never assign a motion or grounded flag. */
        uint32_t settle_ticks=0;
        while(before.motion_id!=ftCo_MS_Wait&&settle_ticks<120){
            if(before.motion_id!=ftCo_MS_Fall&&before.motion_id!=ftCo_MS_Landing){
                snprintf(error,size,"Unexpected source settling motion %d",before.motion_id);goto cleanup;
            }
            if(!melee_web_match_step(context,1,error,size)||
               !melee_web_match_stats(context,&before,error,size))goto cleanup;
            ++settle_ticks;
        }
        if(!check_wait(&before,error,size))goto cleanup;
        printf("Source constructor settled into Wait after %u ticks\n",settle_ticks);
        after=before;
        uint32_t image_changes[2]={0},palette_changes[2]={0};
        for(uint32_t tick=alias_test_ticks;tick<ticks;tick++){
            MeleeWebMatchStats sample;
            if(!melee_web_match_step(context,1,error,size)||
               !melee_web_match_stats(context,&sample,error,size)||
               !check_wait(&sample,error,size))goto cleanup;
            for(unsigned eye=0;eye<2;eye++){
                image_changes[eye]+=sample.eyes[eye].image_index!=after.eyes[eye].image_index;
                palette_changes[eye]+=sample.eyes[eye].palette_index!=after.eyes[eye].palette_index;
            }
            after=sample;
        }
        MeleeWebMatchContextRange range_after_steps={0};
        if(melee_web_match_context_range_read(context,&range_after_steps,error,size)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_COMPLETE||
           !same_range(&range_before,&range_after_steps)){
            snprintf(error,size,"Source stepping changed match-context owner range identity or lifetime");goto cleanup;
        }
        if(after.ticks!=ticks+settle_ticks){snprintf(error,size,"Original scheduler tick count disagrees");goto cleanup;}
        /* Initial source Wait2 script blinks both eyes through its finite
         * timer sequence. Observe changes without assuming an exact frame or
         * later RNG-selected Wait variant. Short constructor-only runs report
         * ownership but are insufficient to assert execution of the blink. */
        if(ticks>=16&&(!image_changes[0]||!image_changes[1])){
            snprintf(error,size,"Original Wait command execution did not change both owned eye images");goto cleanup;
        }
        printf("Owned eye command selections: image changes %u/%u, palette changes %u/%u, final indices %u/%u, extra DObjs %u\n",
            image_changes[0],image_changes[1],palette_changes[0],palette_changes[1],
            after.eyes[0].image_index,after.eyes[1].image_index,after.extra_model_objects);
        printf("Match cycle %d: source motion %d->%d, ground/air %d->%d, y %.9g->%.9g, frame %.9g->%.9g, ticks %llu\n",
            cycle,before.motion_id,after.motion_id,before.ground_or_air,after.ground_or_air,
            before.position[1],after.position[1],before.animation_frame,after.animation_frame,
            (unsigned long long)after.ticks);
        last_range=range_after_steps;
        MeleeWebMatchContext* retired=context;
        if(!melee_web_match_end(context,error,size))goto cleanup;
        free(owner_bytes_before);owner_bytes_before=NULL;
        context=NULL;
        refused_range=last_range;
        if(melee_web_match_context_range_read(retired,&refused_range,error,size)!=
               MELEE_WEB_MATCH_CONTEXT_RANGE_INVALID_OWNER||
           !same_range(&refused_range,&last_range)){
            snprintf(error,size,"Retired match-context range was not refused atomically");goto cleanup;
        }
        if(seed_ptr!=previous_seed||staleAttackInstance!=previous_stale||unk_804D6480!=previous_attack||
           memcmp(pads,HSD_PadGameStatus,sizeof(pads))||
           memcmp(&prior,Player_GetPtrForSlot(settings->player.slot),sizeof(prior))){
            snprintf(error,size,"Original match teardown failed to restore scoped source state");goto cleanup;
        }
    }
    if(!last_range.address||!last_range.owner_epoch){
        snprintf(error,size,"Match-context range trace did not retain an owner identity");return 0;
    }
    previous_world_generation=last_range.world_generation;
    previous_owner_epoch=last_range.owner_epoch;
    return 1;
cleanup:
    free(owner_bytes_before);
    if(context){
        char cleanup_error[256];
        if(!melee_web_match_end(context,cleanup_error,sizeof(cleanup_error)))
            fprintf(stderr,"Match trace cleanup failed: %s\n",cleanup_error);
    }
    return 0;
}

/* Root supplies stage-derived, separated spawns. Optional movement sample runs
 * after the neutral/teardown proof and can intentionally expose the next exact
 * unsupported original action rather than treating that gate as success. */
int melee_web_match_two_player_trace(MeleeWebCollision* collision,
    const MeleeWebPlayerSettings players[2],uint32_t neutral_ticks,
    const PADStatus* movement,char* error,size_t size)
{
    MeleeWebMatchContext* context=NULL;
    MeleeWebMatchContextSnapshot* snapshot=NULL;
    StaticPlayer previous[2]={*Player_GetPtrForSlot(players[0].slot),*Player_GetPtrForSlot(players[1].slot)};
    HSD_PadStatus saved_pads[4],saved_master[4],saved_copy[4];
    memcpy(saved_pads,HSD_PadGameStatus,sizeof(saved_pads));
    memcpy(saved_master,HSD_PadMasterStatus,sizeof(saved_master));
    memcpy(saved_copy,HSD_PadCopyStatus,sizeof(saved_copy));
    PadLibData saved_library=HSD_PadLibData;
    const PADStatus neutral_raw[4]={{0}};
    u32* previous_seed=seed_ptr;u16 stale=staleAttackInstance,attack=unk_804D6480;
    MeleeWebPlayerSettings invalid[2]={players[0],players[1]};
    invalid[1].slot=invalid[0].slot;
    if((context=melee_web_match_begin_players(invalid,2,70,1,collision,error,size)))goto cleanup;
    invalid[1]=players[1];invalid[1].controller=invalid[0].controller;
    if((context=melee_web_match_begin_players(invalid,2,70,1,collision,error,size)))goto cleanup;
    for(unsigned cycle=0;cycle<2;cycle++){
        context=melee_web_match_begin_players(players,2,70,1,cycle?collision:NULL,error,size);
        if(!context)goto cleanup;
        if(snapshot){
            if(melee_web_match_context_snapshot_admit(context,snapshot,error,size)){
                snprintf(error,size,"Retired context snapshot admitted a replacement owner");goto cleanup;
            }
            melee_web_match_context_snapshot_destroy(snapshot);snapshot=NULL;
        }
        snapshot=melee_web_match_context_snapshot_capture(context,error,size);
        if(!snapshot || !melee_web_match_context_snapshot_admit(context,snapshot,error,size) ||
           !melee_web_match_context_snapshot_restore(context,snapshot,error,size))goto cleanup;

        if(cycle==0){
            MeleeWebMatchStats pending;
            if(melee_web_match_player_stats(context,0,&pending,error,size)){
                snprintf(error,size,"Unattached source-ordered match allowed a collision-dependent query");goto cleanup;
            }
            if(!melee_web_match_attach_collision(context,collision,error,size))goto cleanup;
            if(melee_web_match_attach_collision(context,collision,error,size)){
                snprintf(error,size,"Match accepted a second collision owner");goto cleanup;
            }
        }
        if(!melee_web_match_create_fighters(context,error,size))goto cleanup;
        melee_web_match_context_snapshot_destroy(snapshot);
        snapshot=melee_web_match_context_snapshot_capture(context,error,size);
        if(!snapshot)goto cleanup;
        HSD_GObj marker={0};
        HSD_GObj** callback_slots[]={&HSD_GObj_804D781C,&HSD_GObj_804D7818,&HSD_GObj_804D7814};
        for(unsigned callback=0;callback<3;++callback){
            MeleeWebMatchStats owner_before,owner_after;
            if(!melee_web_match_player_stats(context,0,&owner_before,error,size))goto cleanup;
            *callback_slots[callback]=&marker;
            MeleeWebMatchContextSnapshot* refused=melee_web_match_context_snapshot_capture(context,error,size);
            int admitted=melee_web_match_context_snapshot_admit(context,snapshot,error,size);
            int restored=melee_web_match_context_snapshot_restore(context,snapshot,error,size);
            int pointer_unchanged=*callback_slots[callback]==&marker;
            *callback_slots[callback]=NULL;
            melee_web_match_context_snapshot_destroy(refused);
            if(refused||admitted||restored||!pointer_unchanged||
               !melee_web_match_player_stats(context,0,&owner_after,error,size)||
               memcmp(&owner_before,&owner_after,sizeof(owner_before))){
                snprintf(error,size,"Active process/GX callback did not refuse context snapshot atomically");goto cleanup;
            }
        }

        MeleeWebMatchStats stats[2];uint32_t settle=0;
        for(;;){
            int ready=1;
            for(unsigned i=0;i<2;i++){
                if(!melee_web_match_player_stats(context,i,&stats[i],error,size))goto cleanup;
                if(stats[i].motion_id!=ftCo_MS_Wait)ready=0;
                if(stats[i].motion_id!=ftCo_MS_Wait&&stats[i].motion_id!=ftCo_MS_Fall&&stats[i].motion_id!=ftCo_MS_Landing){
                    snprintf(error,size,"Two-player source settling entered unexpected motion %d",stats[i].motion_id);goto cleanup;
                }
            }
            if(ready)break;
            if(settle++>=120){snprintf(error,size,"Two source fighters did not settle into Wait");goto cleanup;}
            if(!melee_web_match_step_raw(context,neutral_raw,error,size))goto cleanup;
        }
        MeleeWebControllerSample rejected[4]={{0}};rejected[0].stick_x=2;
        uint64_t before=stats[0].ticks;
        if(melee_web_match_step_inputs(context,rejected,error,size))goto cleanup;
        if(!melee_web_match_player_stats(context,0,&stats[0],error,size)||stats[0].ticks!=before){
            snprintf(error,size,"Rejected input mutated scheduler state");goto cleanup;
        }
        for(uint32_t tick=0;tick<neutral_ticks;tick++){
            if(!melee_web_match_step_raw(context,neutral_raw,error,size))goto cleanup;
            for(unsigned i=0;i<2;i++){
                if(!melee_web_match_player_stats(context,i,&stats[i],error,size))goto cleanup;
                if(stats[i].motion_id!=ftCo_MS_Wait||stats[i].ground_or_air!=0||stats[i].live_fighters!=1||
                   stats[i].camera_subjects!=2||stats[i].extra_model_objects!=8||stats[i].held_buttons||
                   stats[i].player_slot!=players[i].slot){
                    snprintf(error,size,"Two-player neutral source state disagrees for index%u",i);goto cleanup;
                }
            }
        }
        printf("Two-player cycle%u: slots%u/%u source Wait14 after%u settle ticks, positions %.9g/%.9g, %u neutral ticks, two subjects and 8/8 extra DObjs\n",
            cycle,players[0].slot,players[1].slot,settle,stats[0].position[0],stats[1].position[0],neutral_ticks);
        if(!melee_web_match_end(context,error,size))goto cleanup;
        context=NULL;
        for(unsigned i=0;i<2;i++)if(memcmp(&previous[i],Player_GetPtrForSlot(players[i].slot),sizeof(StaticPlayer))){
            snprintf(error,size,"Two-player source slot restoration failed");goto cleanup;
        }
        if(seed_ptr!=previous_seed||staleAttackInstance!=stale||unk_804D6480!=attack||memcmp(saved_pads,HSD_PadGameStatus,sizeof(saved_pads))||
           memcmp(saved_master,HSD_PadMasterStatus,sizeof(saved_master))||memcmp(saved_copy,HSD_PadCopyStatus,sizeof(saved_copy))||
           memcmp(&saved_library,&HSD_PadLibData,sizeof(saved_library))){
            snprintf(error,size,"Two-player PAD/RNG/counter restoration failed");goto cleanup;
        }
    }
    if(movement){
        context=melee_web_match_begin_players(players,2,70,1,collision,error,size);
        if(!context||!melee_web_match_create_fighters(context,error,size))goto cleanup;
        int ready=0;
        for(unsigned tick=0;tick<120&&!ready;tick++){
            MeleeWebMatchStats a,b;
            if(!melee_web_match_player_stats(context,0,&a,error,size)||!melee_web_match_player_stats(context,1,&b,error,size))goto cleanup;
            ready=a.motion_id==ftCo_MS_Wait&&b.motion_id==ftCo_MS_Wait;
            if(!ready&&!melee_web_match_step_raw(context,neutral_raw,error,size))goto cleanup;
        }
        if(!ready){snprintf(error,size,"Movement input requires original settled fighters");goto cleanup;}
        PADStatus samples[4]={{0}};samples[players[0].controller]=*movement;
        fprintf(stderr,"Applying raw movement input on controller%u mapped to source slot%u: stick=(%d,%d) buttons=%u\n",
            players[0].controller,players[0].slot,movement->stickX,movement->stickY,movement->button);
        MeleeWebMatchStats a,b;int consumed=0;
        for(unsigned frame=0;frame<3&&!consumed;frame++){
            if(!melee_web_match_step_raw(context,samples,error,size))goto cleanup;
            if(!melee_web_match_player_stats(context,0,&a,error,size)||!melee_web_match_player_stats(context,1,&b,error,size))goto cleanup;
            consumed=(movement->stickX&&a.source_stick[0])||(movement->stickY&&a.source_stick[1])||
                (movement->button&&(a.held_buttons&movement->button));
        }
        printf("Source input result: motions%d/%d, first stick=(%.9g,%.9g), held%u pressed%u, second stick=(%.9g,%.9g)\n",
            a.motion_id,b.motion_id,a.source_stick[0],a.source_stick[1],a.held_buttons,a.pressed_buttons,b.source_stick[0],b.source_stick[1]);
        if(!consumed||b.source_stick[0]||b.source_stick[1]||b.held_buttons){
            snprintf(error,size,"Positive raw fixture did not reach only its original fighter within three source ticks");goto cleanup;
        }
        const float input_start_x=a.position[0];
        for(unsigned frame=1;frame<60;frame++){
            if(!melee_web_match_step_raw(context,samples,error,size)||
               !melee_web_match_player_stats(context,0,&a,error,size)||
               !melee_web_match_player_stats(context,1,&b,error,size))goto cleanup;
            if((movement->stickX&&!a.source_stick[0])||(movement->stickY&&!a.source_stick[1])||
               b.source_stick[0]||b.source_stick[1]||b.held_buttons){
                snprintf(error,size,"Sustained source input routing changed during movement");goto cleanup;
            }
        }
        if(movement->stickX&&a.position[0]==input_start_x){
            snprintf(error,size,"Sustained horizontal input did not move the original fighter");goto cleanup;
        }
        printf("Sustained original movement: 60 input frames, motion%d/%d x %.9g->%.9g, source stick %.9g/%.9g\n",
            a.motion_id,b.motion_id,input_start_x,a.position[0],a.source_stick[0],b.source_stick[0]);
        if(!melee_web_match_end(context,error,size))goto cleanup;
        context=NULL;
    }
    melee_web_match_context_snapshot_destroy(snapshot);
    return 1;
cleanup:
    melee_web_match_context_snapshot_destroy(snapshot);
    if(context){
        char cleanup_error[256];
        if(!melee_web_match_end(context,cleanup_error,sizeof(cleanup_error)))
            fprintf(stderr,"Match trace cleanup failed: %s\n",cleanup_error);
    }
    return 0;
}
