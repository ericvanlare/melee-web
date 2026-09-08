/* Integration helper for the real common/stage/effect/fighter-assets harness.
 * No source consumer is replaced. The caller keeps all asset owners alive. */
#include "gameplay_match_context.h"
#include <melee/pl/player.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <string.h>
extern u16 staleAttackInstance, unk_804D6480;
static int check_wait(const MeleeWebMatchStats* stats,char* error,size_t size)
{
    if(stats->motion_id!=14||stats->ground_or_air!=0||stats->live_fighters!=1||
       stats->camera_subjects!=1||stats->extra_model_objects!=8||stats->eye_count!=2){
        snprintf(error,size,"Expected original grounded Wait14, one fighter/subject, eight extra DObjs and two eyes; got motion%d ground%d fighters%u subjects%u DObjs%u eyes%u",
            stats->motion_id,stats->ground_or_air,stats->live_fighters,stats->camera_subjects,
            stats->extra_model_objects,stats->eye_count);return 0;
    }
    return 1;
}
int melee_web_match_context_trace(MeleeWebCollision* collision,
    const MeleeWebMatchSettings* settings, uint32_t ticks, char* error,size_t size)
{
    HSD_PadStatus pads[4];memcpy(pads,HSD_PadGameStatus,sizeof(pads));
    u32* previous_seed=seed_ptr;
    u16 previous_stale=staleAttackInstance,previous_attack=unk_804D6480;
    StaticPlayer prior=*Player_GetPtrForSlot(settings->player.slot);
    for(uint32_t slot=4;slot<6;slot++) {
        MeleeWebMatchSettings invalid=*settings;invalid.player.slot=slot;
        if(melee_web_match_begin(&invalid,collision,error,size)) {
            snprintf(error,size,"Match accepted player slot outside source PAD array");return 0;
        }
    }
    for(int cycle=0;cycle<2;cycle++){
        MeleeWebMatchContext* context=melee_web_match_begin(settings,collision,error,size);
        if(!context)return 0;
        if(!melee_web_match_create_fighter(context,error,size))return 0;
        MeleeWebMatchStats before,after;
        if(!melee_web_match_stats(context,&before,error,size))return 0;
        /* Let source collision and motion callbacks settle the constructor's
         * bind pose onto the floor. Never assign a motion or grounded flag. */
        uint32_t settle_ticks=0;
        while(before.motion_id!=ftCo_MS_Wait&&settle_ticks<120){
            if(before.motion_id!=ftCo_MS_Fall&&before.motion_id!=ftCo_MS_Landing){
                snprintf(error,size,"Unexpected source settling motion %d",before.motion_id);return 0;
            }
            if(!melee_web_match_step(context,1,error,size)||
               !melee_web_match_stats(context,&before,error,size))return 0;
            ++settle_ticks;
        }
        if(!check_wait(&before,error,size))return 0;
        printf("Source constructor settled into Wait after %u ticks\n",settle_ticks);
        after=before;
        uint32_t image_changes[2]={0},palette_changes[2]={0};
        for(uint32_t tick=0;tick<ticks;tick++){
            MeleeWebMatchStats sample;
            if(!melee_web_match_step(context,1,error,size)||
               !melee_web_match_stats(context,&sample,error,size)||
               !check_wait(&sample,error,size))return 0;
            for(unsigned eye=0;eye<2;eye++){
                image_changes[eye]+=sample.eyes[eye].image_index!=after.eyes[eye].image_index;
                palette_changes[eye]+=sample.eyes[eye].palette_index!=after.eyes[eye].palette_index;
            }
            after=sample;
        }
        if(after.ticks!=ticks+settle_ticks){snprintf(error,size,"Original scheduler tick count disagrees");return 0;}
        /* Initial source Wait2 script blinks both eyes through its finite
         * timer sequence. Observe changes without assuming an exact frame or
         * later RNG-selected Wait variant. Short constructor-only runs report
         * ownership but are insufficient to assert execution of the blink. */
        if(ticks>=16&&(!image_changes[0]||!image_changes[1])){
            snprintf(error,size,"Original Wait command execution did not change both owned eye images");return 0;
        }
        printf("Owned eye command selections: image changes %u/%u, palette changes %u/%u, final indices %u/%u, extra DObjs %u\n",
            image_changes[0],image_changes[1],palette_changes[0],palette_changes[1],
            after.eyes[0].image_index,after.eyes[1].image_index,after.extra_model_objects);
        printf("Match cycle %d: source motion %d->%d, ground/air %d->%d, y %.9g->%.9g, frame %.9g->%.9g, ticks %llu\n",
            cycle,before.motion_id,after.motion_id,before.ground_or_air,after.ground_or_air,
            before.position[1],after.position[1],before.animation_frame,after.animation_frame,
            (unsigned long long)after.ticks);
        if(!melee_web_match_end(context,error,size))return 0;
        if(seed_ptr!=previous_seed||staleAttackInstance!=previous_stale||unk_804D6480!=previous_attack||
           memcmp(pads,HSD_PadGameStatus,sizeof(pads))||
           memcmp(&prior,Player_GetPtrForSlot(settings->player.slot),sizeof(prior))){
            snprintf(error,size,"Original match teardown failed to restore scoped source state");return 0;
        }
    }
    return 1;
}
