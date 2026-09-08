#include "gameplay_action_trace.h"
#include "gameplay_render.h"
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/pl/player.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
extern u16 staleAttackInstance,unk_804D6480;
static Fighter* source_fighter(uint32_t slot)
{
    HSD_GObj* entity=Player_GetPtrForSlot(slot)->player_entity[0];
    return entity?entity->user_data:NULL;
}
static int error_text(char* error,size_t size,const char* text)
{if(error&&size)snprintf(error,size,"%s",text);return 0;}
int melee_web_match_action_trace(MeleeWebCollision* collision,const MeleeWebPlayerSettings players[2],
    MeleeWebActionTraceKind kind,MeleeWebActionTraceReport* report,char* error,size_t size)
{
    if(!collision||!players||!report||kind<MeleeWebActionTrace_Jab||kind>MeleeWebActionTrace_ContactDamage||
       players[0].slot>=4||players[1].slot>=4||players[0].slot==players[1].slot)
        return error_text(error,size,"Invalid original action trace inputs");
    memset(report,0,sizeof(*report));
    const StaticPlayer saved_players[2]={*Player_GetPtrForSlot(players[0].slot),*Player_GetPtrForSlot(players[1].slot)};
    HSD_PadStatus saved_game[4],saved_master[4],saved_copy[4];
    memcpy(saved_game,HSD_PadGameStatus,sizeof(saved_game));
    memcpy(saved_master,HSD_PadMasterStatus,sizeof(saved_master));
    memcpy(saved_copy,HSD_PadCopyStatus,sizeof(saved_copy));
    PadLibData saved_library=HSD_PadLibData;
    u32* saved_seed=seed_ptr;u16 saved_stale=staleAttackInstance,saved_attack=unk_804D6480;
    MeleeWebMatchContext* context=melee_web_match_begin_players(players,2,70,1,collision,error,size);
    int passed=0;
    MeleeWebRender* render=NULL;
    if(!context)return 0;
    if(!melee_web_match_create_fighters(context,error,size))goto cleanup;
    const MeleeWebRenderSettings settings={640,480,{0,25,180},{0,15,0},30,1,1000,(UINT64_C(1)<<5)|(UINT64_C(1)<<3)};
    render=melee_web_render_begin_match(&settings,error,size);
    if(!render)goto cleanup;
    PADStatus raw[4]={{0}};
    Fighter* fighters[2];
    for(;;){
        fighters[0]=source_fighter(players[0].slot);fighters[1]=source_fighter(players[1].slot);
        if(!fighters[0]||!fighters[1]){error_text(error,size,"Source action trace lost a fighter");goto cleanup;}
        if(fighters[0]->motion_id==ftCo_MS_Wait&&fighters[1]->motion_id==ftCo_MS_Wait&&
           fighters[0]->ground_or_air==GA_Ground&&fighters[1]->ground_or_air==GA_Ground)break;
        if(report->settle_frames++>=120){error_text(error,size,"Source action trace did not settle into grounded Wait");goto cleanup;}
        if(!melee_web_match_step_raw(context,raw,error,size))goto cleanup;
    }
    for(unsigned i=0;i<2;i++)report->initial_percent[i]=report->peak_percent[i]=fighters[i]->dmg.x1830_percent;
    report->initial_shield=report->minimum_shield=fighters[0]->shield_health;
    int prior_motion[2]={fighters[0]->motion_id,fighters[1]->motion_id};
    fprintf(stderr,"Original action trace kind%d begins: slots%u/%u x%.9g/%.9g facing%.1f/%.1f\n",
        kind,players[0].slot,players[1].slot,fighters[0]->cur_pos.x,fighters[1]->cur_pos.x,
        fighters[0]->facing_dir,fighters[1]->facing_dir);
    const unsigned frame_limit=kind==MeleeWebActionTrace_JumpLanding?240:120;
    for(unsigned frame=0;frame<frame_limit;frame++){
        memset(raw,0,sizeof(raw));
        if(frame==0&&(kind==MeleeWebActionTrace_Jab||kind==MeleeWebActionTrace_ContactDamage))
            raw[players[0].controller].button=PAD_BUTTON_A;
        if(frame==0&&kind==MeleeWebActionTrace_JumpLanding)
            raw[players[0].controller].button=PAD_BUTTON_X;
        if(frame<12&&kind==MeleeWebActionTrace_Shield){
            raw[players[0].controller].button=PAD_TRIGGER_R;
            raw[players[0].controller].triggerRight=140;
        }
        if(!melee_web_match_step_raw(context,raw,error,size))goto cleanup;
        report->frames++;
        for(unsigned i=0;i<2;i++){
            fighters[i]=source_fighter(players[i].slot);
            if(!fighters[i]){error_text(error,size,"Source action trace unexpectedly despawned a fighter");goto cleanup;}
            if(!isfinite(fighters[i]->dmg.x1830_percent)){error_text(error,size,"Source damage became nonfinite");goto cleanup;}
            if(fighters[i]->dmg.x1830_percent>report->peak_percent[i])report->peak_percent[i]=fighters[i]->dmg.x1830_percent;
            report->final_motion[i]=fighters[i]->motion_id;
            if(prior_motion[i]!=fighters[i]->motion_id){
                fprintf(stderr,"Action frame%u player%u: motion%d->%d percent%.9g\n",frame,i,
                    prior_motion[i],fighters[i]->motion_id,fighters[i]->dmg.x1830_percent);
                prior_motion[i]=fighters[i]->motion_id;
            }
        }
        Fighter* first=fighters[0];Fighter* second=fighters[1];
        unsigned hits=0;for(unsigned i=0;i<4;i++)hits+=first->x914[i].state!=HitCapsule_Disabled;
        if(hits>report->peak_hitboxes)report->peak_hitboxes=hits;
        report->air_frames+=first->ground_or_air==GA_Air;
        report->hitlag_frames+=first->dmg.x195c_hitlag_frames>0||second->dmg.x195c_hitlag_frames>0;
        report->saw_jab|=first->motion_id==ftCo_MS_Attack11;
        report->saw_jump|=first->motion_id>=ftCo_MS_JumpF&&first->motion_id<=ftCo_MS_JumpAerialB;
        report->saw_landing|=first->motion_id==ftCo_MS_Landing;
        report->saw_shield|=first->motion_id==ftCo_MS_GuardOn||first->motion_id==ftCo_MS_Guard;
        report->saw_damage|=second->motion_id>=ftCo_MS_DamageHi1&&second->motion_id<=ftCo_MS_DamageFlyRoll;
        if(first->shield_health<report->minimum_shield)report->minimum_shield=first->shield_health;
        if(frame==0){
            uint32_t expected=kind==MeleeWebActionTrace_JumpLanding?PAD_BUTTON_X:
                kind==MeleeWebActionTrace_Shield?PAD_TRIGGER_R:PAD_BUTTON_A;
            if(!(first->input.held_buttons[0]&expected)||second->input.held_buttons[0]){
                error_text(error,size,"Raw action button did not reach only its source fighter");goto cleanup;
            }
        }
        if(frame>=12&&first->motion_id==ftCo_MS_Wait&&second->motion_id==ftCo_MS_Wait&&
           first->ground_or_air==GA_Ground&&second->ground_or_air==GA_Ground){
            if(kind==MeleeWebActionTrace_Jab)passed=report->saw_jab&&report->peak_hitboxes>0;
            if(kind==MeleeWebActionTrace_JumpLanding)passed=report->saw_jump&&report->air_frames>0;
            if(kind==MeleeWebActionTrace_Shield)passed=report->saw_shield&&report->minimum_shield<report->initial_shield;
            if(kind==MeleeWebActionTrace_ContactDamage)passed=report->saw_jab&&report->peak_hitboxes>0&&
                report->saw_damage&&report->peak_percent[1]>report->initial_percent[1]&&report->hitlag_frames>0;
            if(passed)break;
        }
    }
    if(!passed){
        if(error&&size)snprintf(error,size,"Original action kind%d failed: jab%u hits%u jump%u air%u landing%u shield%u damage%u percent%.9g->%.9g hitlag%u",
            kind,report->saw_jab,report->peak_hitboxes,report->saw_jump,report->air_frames,report->saw_landing,
            report->saw_shield,report->saw_damage,report->initial_percent[1],report->peak_percent[1],report->hitlag_frames);
        goto cleanup;
    }
cleanup:
    {
        char cleanup_error[256];
        if(render&&!melee_web_render_end(render,cleanup_error,sizeof(cleanup_error))){
            fprintf(stderr,"Original action camera cleanup failed: %s\n",cleanup_error);
            if(passed)error_text(error,size,cleanup_error);
            return 0;
        }
        if(!melee_web_match_end(context,cleanup_error,sizeof(cleanup_error))){
            fprintf(stderr,"Original action trace cleanup failed: %s\n",cleanup_error);
            if(passed)error_text(error,size,cleanup_error);
            return 0;
        }
    }
    if(memcmp(saved_game,HSD_PadGameStatus,sizeof(saved_game))||memcmp(saved_master,HSD_PadMasterStatus,sizeof(saved_master))||
       memcmp(saved_copy,HSD_PadCopyStatus,sizeof(saved_copy))||memcmp(&saved_library,&HSD_PadLibData,sizeof(saved_library))||
       seed_ptr!=saved_seed||staleAttackInstance!=saved_stale||unk_804D6480!=saved_attack||
       memcmp(&saved_players[0],Player_GetPtrForSlot(players[0].slot),sizeof(StaticPlayer))||
       memcmp(&saved_players[1],Player_GetPtrForSlot(players[1].slot),sizeof(StaticPlayer)))
        return error_text(error,size,"Action trace did not restore original player/PAD/RNG state");
    if(passed){
    printf("Original action kind%d passed: %u frames, peak hitboxes%u, air%u, landing%u, shield%.9g->%.9g, target damage%.9g->%.9g, hitlag%u\n",
        kind,report->frames,report->peak_hitboxes,report->air_frames,report->saw_landing,
        report->initial_shield,report->minimum_shield,report->initial_percent[1],report->peak_percent[1],report->hitlag_frames);
    }
    return passed;
}
