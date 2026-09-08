#include "gameplay_compat.h"
#include "gameplay_bootstrap.h"
#include <melee/mn/mnstagesel.h>
#include <melee/mn/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/controller.h>
#include <stdio.h>
#include <string.h>
extern GameRules gmMainLib_803D4A48;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
static SSSData selection;
static HSD_PadData input_queue;
/* Source scene diagnostic only: explicit all-unlocked/default-save fixture,
 * two human ports, original raw PAD processing. No rendered/selection claim. */
int melee_web_test_sss_enter(void)
{
    lbLang_SetLanguageSetting(1);lbLang_SetSavedLanguage(1);
    *gmMainLib_GetGameRules()=gmMainLib_803D4A48;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=0xffff;
    *gmMainLib_8015EDA4()=0xffff;
    memset(&selection,0,sizeof(selection));
    gm_InitVsMode(&selection.vs);
    selection.force_stage_id=-1;
    selection.vs.start.rules.stkind=0x20;
    selection.vs.start.rules.match_kind=MatchKind_Stock;
    selection.vs.start.rules.is_stock=true;
    selection.vs.start.rules.is_vs=true;
    for(unsigned i=0;i<2;i++){
        selection.vs.start.players[i].ckind=CKIND_MARIO;
        selection.vs.start.players[i].slot_type=Gm_PKind_Human;
        selection.vs.start.players[i].stocks=4;
    }
    HSD_PadLibData=default_libinfo_data;
    HSD_PadLibData.qnum=1;HSD_PadLibData.queue=&input_queue;
    HSD_PadLibData.clamp_stickType=0;HSD_PadLibData.clamp_stickShift=1;
    HSD_PadLibData.clamp_stickMax=80;HSD_PadLibData.clamp_stickMin=0;
    HSD_PadLibData.scale_stick=80;
    HSD_PadLibData.clamp_analogLRShift=1;HSD_PadLibData.clamp_analogLRMax=140;
    HSD_PadLibData.clamp_analogLRMin=0;HSD_PadLibData.scale_analogLR=140;
    for(unsigned i=0;i<4;i++){
        HSD_PadGameStatus[i]=HSD_PadMasterStatus[i]=HSD_PadCopyStatus[i]=default_status_data;
    }
    gm_801A3E88();
    mnStageSel_Scene_OnEnter(&selection);
    return melee_web_gameplay_stats().objects>20;
}
int melee_web_test_sss_tick(void)
{
    char error[256];
    memset(&input_queue,0,sizeof(input_queue));
    input_queue.stat[2].err=input_queue.stat[3].err=-1;
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    gm_EvaluateAllControllerInputs();
    mnStageSel_Scene_OnFrame();
    if(!melee_web_gameplay_step(error,sizeof(error))){fprintf(stderr,"%s\n",error);return 0;}
    return 1;
}
int melee_web_test_sss_exit(void)
{
    mnStageSel_Scene_OnExit(&selection);
    return !selection.start_game;
}

/* The neutral SSS lifecycle never requests scene preloading. Keep the linked
 * preloader's unported alarm service fatal in this test only; it must not
 * manufacture completion or become a runtime implementation. */
#include <dolphin/os/OSAlarm.h>
#include <stdlib.h>
void OSCreateAlarm(OSAlarm* alarm)
{
    (void)alarm;fputs("SSS trace reached unported scene-preload alarm\n",stderr);abort();
}
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void)alarm;(void)tick;(void)handler;
    fputs("SSS trace reached unported scene-preload alarm\n",stderr);abort();
}
