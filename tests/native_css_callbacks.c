#include "gameplay_compat.h"
#include "gameplay_bootstrap.h"
#include <melee/mn/mncharsel.h>
#include <melee/mn/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/lb/lbaudio_ax.h>
#include <sysdolphin/baselib/axdriver.h>
#include "gameplay_audio_bank_transport.h"
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/sislib.h>
#include <stdio.h>
#include <string.h>
extern GameRules gmMainLib_803D4A48;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
static CSSData selection;
static u8 ko_counts[GM_MAX_PLAYERS];
static HSD_PadData input_queue;
/* Source callback diagnostic. This fixture supplies two neutral human ports;
 * no rendered selection or completed preloading claim follows from entry. */
int melee_web_test_css_enter(void)
{
    lbLang_SetLanguageSetting(1);lbLang_SetSavedLanguage(1);
    *gmMainLib_GetGameRules()=gmMainLib_803D4A48;
    gmMainLib_GetGameRules()->mode=1;
    gmMainLib_GetGameRules()->stock_count=4;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=0xffff;
    *gmMainLib_8015EDA4()=0xffff;
    memset(&selection,0,sizeof(selection));
    memset(ko_counts,0,sizeof(ko_counts));
    gm_InitVsMode(&selection.vs);
    selection.match_type=VS_MELEE;
    selection.ko_counts=ko_counts;
    selection.vs.start.rules.stkind=0x20;
    selection.vs.start.rules.match_kind=MatchKind_Stock;
    selection.vs.start.rules.is_stock=true;
    selection.vs.start.rules.is_vs=true;
    for(unsigned i=0;i<2;i++){
        selection.vs.start.players[i].ckind=CKIND_MARIO;
        selection.vs.start.players[i].slot_type=Gm_PKind_Human;
        selection.vs.start.players[i].stocks=4;
        selection.vs.start.players[i].slot=i;
        selection.vs.start.players[i].nametag=0x78;
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
    /* Original preloadState uses this text heap size for GS_CSS. */
    HSD_SisLib_803A6048(0x2400);
    lbAudioAx_8002835C();
    lbAudioAx_8002838C();
    lbAudioAx_80028690();
    mnCharSel_Scene_OnEnter(&selection);
    /* gm_801A4D34 creates these common card indicator objects after OnEnter. */
    lb_8001CF18();
    return melee_web_gameplay_stats().objects>20;
}
int melee_web_test_css_tick(void)
{
    char error[256];
    memset(&input_queue,0,sizeof(input_queue));
    input_queue.stat[2].err=input_queue.stat[3].err=-1;
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    gm_EvaluateAllControllerInputs();
    mnCharSel_Scene_OnFrame();
    if(!melee_web_gameplay_step(error,sizeof(error))){fprintf(stderr,"%s\n",error);return 0;}
    return 1;
}
int melee_web_test_css_exit(void)
{
    mnCharSel_Scene_OnExit(NULL);
    lbAudioAx_80027DBC();
    melee_web_audio_bank_transport_pump();
    return selection.pending_scene_change==0;
}
void melee_web_test_css_forget(void)
{
    lb_8001D1F4();lb_8001C5A4();
}

/* Explicitly unimplemented preloader service in this diagnostic only. */
#include <dolphin/os/OSAlarm.h>
#include <stdlib.h>
void OSCreateAlarm(OSAlarm* alarm)
{
    (void)alarm;fputs("CSS trace reached unported scene-preload alarm\n",stderr);abort();
}
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void)alarm;(void)tick;(void)handler;
    fputs("CSS trace reached unported scene-preload alarm\n",stderr);abort();
}
