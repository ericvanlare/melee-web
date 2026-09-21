#include "gameplay_menu_host.h"
#include "gameplay_menu.h"
#include "gameplay_save_profile.h"
#include "gameplay_content.h"
#include "gameplay_bootstrap.h"
#include "gameplay_results_context.h"
#include "gameplay_prize_context.h"
#include "gameplay_audio_bank_transport.h"
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/video.h>
#include <sysdolphin/baselib/random.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern GameRules gmMainLib_803D4A48;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
extern int melee_web_audio_is_active(MeleeWebAudio*);
extern int melee_web_menu_clock_begin(void);
extern int melee_web_menu_clock_request(int*);
extern int melee_web_menu_clock_tick(void);
extern int melee_web_menu_clock_present(void);
extern int melee_web_menu_clock_end(void);
extern int melee_web_vs_mode_begin(void);
extern int melee_web_vs_mode_end(void);
extern int melee_web_vs_mode_select_state(int);
extern int melee_web_vs_mode_next_state(void);
struct MeleeWebMenuHost {
    MeleeWebMenuSession* session;
    MeleeWebSaveProfileOwner* profile;
    MeleeWebAudio* audio;
    uint64_t generation,audio_generation;
    u32 seed,*saved_seed;
    HSD_PadData queue;
    PadLibData saved_library;
    HSD_PadStatus saved_game[4],saved_master[4],saved_copy[4];
    MeleeWebPadState* input;
    GameRules saved_rules;
    GameRules selected_rules, route_saved_rules;
    struct gmm_x1CB0 saved_preferences;
    struct gmm_x1CB0 selected_preferences, route_saved_preferences;
    int saved_language,saved_saved_language;
    u16 saved_characters,saved_stages;
    u16 selected_characters,selected_stages;
    u16 route_saved_characters,route_saved_stages;
    VsModeData route_saved_vs;
    MatchExitInfo route_saved_exit;
    ResultsMatchInfo route_saved_result;
    ChallengerData route_saved_challenger;
    u8 route_saved_ko[GM_MAX_PLAYERS];
    int results_active,results_exited,results_committed,prize_active;
    int entered,drawing,transition;
};
static MeleeWebMenuHost* owner;
static int fail(char* e,size_t n,const char* text){if(e&&n)snprintf(e,n,"%s",text);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int live(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->audio||!h->generation||
       h->generation!=melee_web_gameplay_stats().generation||
       !melee_web_audio_is_active(h->audio)||!melee_web_audio_bank_transport_active())
        return fail(e,n,"Native menu world/audio ownership changed");
    return melee_web_save_profile_owner_live(h->profile,e,n);
}
static int runtime_check(void* data,MeleeWebMenuScene scene,char* e,size_t n){
    (void)scene;return live(data,e,n);
}
static int runtime_scheduler(void* data,char* e,size_t n){
    if(!live(data,e,n))return 0;
    lbAudioAx_80027DF8();
    if(!melee_web_gameplay_step(e,n))return 0;
    if(!melee_web_menu_clock_tick())return fail(e,n,"Unsupported native menu pause/control state");
    return 1;
}
static int runtime_transition(void* data,MeleeWebMenuScene scene,int* request,char* e,size_t n){
    MeleeWebMenuHost* h=data;(void)scene;
    if(!live(h,e,n)||!melee_web_menu_clock_request(request))return fail(e,n,"Invalid original menu transition state");
    h->transition=*request;return 1;
}
MeleeWebMenuHost* melee_web_menu_host_create(char* e,size_t n){
    if(owner||!seed_ptr){fail(e,n,"A menu host already exists or source RNG is unavailable");return NULL;}
    MeleeWebMenuHost* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate native menu host");return NULL;}
    MeleeWebMenuRuntime runtime={h,runtime_check,runtime_scheduler,runtime_transition};
    MeleeWebMenuConfig config={4,0,0};
    h->profile=melee_web_save_profile_owner_create(e,n);
    if(!h->profile){free(h);return NULL;}
    if(!melee_web_save_profile_owner_activate(h->profile,e,n)){
        if(!melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    if(!melee_web_save_profile_owner_initialize_default(h->profile,e,n)||
       !melee_web_save_profile_owner_set_roster(h->profile,0x07ff,0x01c0,e,n)){
        if(!melee_web_save_profile_owner_deactivate(h->profile,NULL,0)||
           !melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    h->session=melee_web_menu_session_create(&runtime,&config,e,n);
    if(!h->session){
        if(!melee_web_save_profile_owner_deactivate(h->profile,NULL,0)||
           !melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    h->saved_seed=seed_ptr;h->seed=*seed_ptr;seed_ptr=&h->seed;
    owner=h;ok(e,n);return h;
}
static void restore_context(MeleeWebMenuHost* h){
    HSD_PadLibData=h->saved_library;
    memcpy(HSD_PadGameStatus,h->saved_game,sizeof(h->saved_game));
    memcpy(HSD_PadMasterStatus,h->saved_master,sizeof(h->saved_master));
    memcpy(HSD_PadCopyStatus,h->saved_copy,sizeof(h->saved_copy));
    *gmMainLib_GetGameRules()=h->saved_rules;
    *gmMainLib_8015CC58()=h->saved_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->saved_characters;
    *gmMainLib_8015EDA4()=h->saved_stages;
    lbLang_SetLanguageSetting(h->saved_language);lbLang_SetSavedLanguage(h->saved_saved_language);
    if(!melee_web_menu_clock_end())abort();
    h->audio=NULL;h->generation=0;
}
int melee_web_menu_host_enter(MeleeWebMenuHost* h,MeleeWebAudio* audio,char* e,size_t n){
    const uint64_t audio_generation=melee_web_audio_generation(audio);
    if(!h||h!=owner||h->entered||h->audio||h->results_active||seed_ptr!=&h->seed||!melee_web_audio_is_active(audio)||
       !audio_generation||!melee_web_audio_bank_transport_active()||!melee_web_gameplay_stats().generation)
        return fail(e,n,"Native menu enter requires a fresh owned world and source audio");
    const MeleeWebMenuPhase phase=melee_web_menu_phase(h->session);
    if(phase!=MELEE_WEB_MENU_CREATED&&phase!=MELEE_WEB_MENU_CSS_READY&&phase!=MELEE_WEB_MENU_SSS_READY&&phase!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Native menu session cannot enter from this phase");
    if(phase!=MELEE_WEB_MENU_CREATED&&!h->input)
        return fail(e,n,"Returning menu scene requires retained source PAD history");
    if(!melee_web_menu_clock_begin())return fail(e,n,"Original scene clock is already owned");
    h->audio=audio;h->generation=melee_web_gameplay_stats().generation;h->transition=0;
    h->saved_rules=*gmMainLib_GetGameRules();
    h->saved_preferences=*gmMainLib_8015CC58();
    h->saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();h->saved_stages=*gmMainLib_8015EDA4();
    h->saved_language=lbLang_GetLanguageSetting();h->saved_saved_language=lbLang_GetSavedLanguage();
    h->saved_library=HSD_PadLibData;
    memcpy(h->saved_game,HSD_PadGameStatus,sizeof(h->saved_game));
    memcpy(h->saved_master,HSD_PadMasterStatus,sizeof(h->saved_master));
    memcpy(h->saved_copy,HSD_PadCopyStatus,sizeof(h->saved_copy));
    lbLang_SetLanguageSetting(LANG_US);lbLang_SetSavedLanguage(LANG_US);
    if(phase==MELEE_WEB_MENU_CREATED){
        *gmMainLib_GetGameRules()=gmMainLib_803D4A48;
        gmMainLib_GetGameRules()->mode=1;gmMainLib_GetGameRules()->stock_count=4;
        gmMainLib_8015CC58()->item_freq=(u8)-1;
        gmMainLib_8015CC58()->item_mask=UINT64_MAX;
        gmMainLib_8015CC58()->rumble_enabled[0]=true;
        gmMainLib_8015CC58()->rumble_enabled[1]=true;
        /* Exact authored profile masks were installed once at host creation.
         * Scene entry must not reset source unlock or Prize progress. */
    }else{
        *gmMainLib_GetGameRules()=h->selected_rules;
        *gmMainLib_8015CC58()=h->selected_preferences;
        *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->selected_characters;
        *gmMainLib_8015EDA4()=h->selected_stages;
    }
    HSD_PadLibData=default_libinfo_data;
    HSD_PadLibData.rumble_info=h->saved_library.rumble_info;
    HSD_PadLibData.qnum=1;HSD_PadLibData.queue=&h->queue;
    HSD_PadLibData.clamp_stickType=0;HSD_PadLibData.clamp_stickShift=1;
    HSD_PadLibData.clamp_stickMax=80;HSD_PadLibData.clamp_stickMin=0;HSD_PadLibData.scale_stick=80;
    HSD_PadLibData.clamp_analogLRShift=1;HSD_PadLibData.clamp_analogLRMax=140;
    HSD_PadLibData.clamp_analogLRMin=0;HSD_PadLibData.scale_analogLR=140;
    for(unsigned i=0;i<4;i++)HSD_PadGameStatus[i]=HSD_PadMasterStatus[i]=HSD_PadCopyStatus[i]=default_status_data;
    /* Retail flushes the sample queue at a scene boundary, but retains HSD's
     * edge/repeat history. Restoring only the external owner here fabricates
     * a new LRAS press when the quitting chord is still held on CSS entry. */
    if(h->input)melee_web_pad_state_apply(h->input);
    /* The retail bootstrap initializes the AX driver and language banks once,
     * outside ordinary CSS/SSS scene changes. The per-world audio GObj
     * allocator must still be rebound because its storage uses the fresh HSD
     * heap. A newly owned provider gets the full initialization; a retained
     * menu provider keeps its HPS voice and source stream position. */
    lbAudioAx_8002835C();
    if(h->audio_generation!=audio_generation){
        lbAudioAx_8002838C();lbAudioAx_80028690();
        h->audio_generation=audio_generation;
    }
    HSD_ZListInitAllocData();
    HSD_SisLib_803A6048(phase==MELEE_WEB_MENU_SSS_READY?0x4800:0x2400);
    int accepted=phase==MELEE_WEB_MENU_SSS_READY?melee_web_menu_enter_sss(h->session,e,n):
        phase==MELEE_WEB_MENU_READY?melee_web_menu_return_to_css(h->session,e,n):melee_web_menu_enter_css(h->session,e,n);
    if(!accepted){HSD_SisLib_803A5FBC();restore_context(h);return 0;}
    h->entered=1;lb_8001CF18();return ok(e,n);
}
int melee_web_menu_host_tick(MeleeWebMenuHost* h,const PADStatus raw[4],char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing||!raw)return fail(e,n,"Native menu tick requires an idle live scene and four raw ports");
    if(HSD_PadLibData.queue!=&h->queue||HSD_PadLibData.qcount)return fail(e,n,"Native menu raw PAD queue is not idle");
    memset(&h->queue,0,sizeof(h->queue));memcpy(h->queue.stat,raw,sizeof(h->queue.stat));
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    if(HSD_PadLibData.qcount)return fail(e,n,"Source PAD processing did not consume its sample");
    gm_EvaluateAllControllerInputs();return melee_web_menu_tick(h->session,e,n);
}
int melee_web_menu_host_draw(MeleeWebMenuHost* h,char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu draw requires an idle live scene");
    if(h->transition==2)return ok(e,n);
    h->drawing=1;
    /* The source screen camera scales its authored viewport by the current
     * VI mode. Bootstrap owns HSD objects; Aurora owns display startup. Supply
     * the same NTSC mode selected by gmMain for this scoped source draw. */
    GXRenderModeObj saved_mode=*HSD_VIGetRenderMode();
    *HSD_VIGetRenderMode()=GXNtsc480IntDf;
    GXInvalidateVtxCache();GXInvalidateTexAll();
    HSD_StartRender(HSD_RP_SCREEN);HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_GObj_80390FC0();HSD_Init_803755A8();
    *HSD_VIGetRenderMode()=saved_mode;
    h->drawing=0;
    if(!melee_web_menu_clock_present())return fail(e,n,"Original scene presentation clock lost ownership");
    return ok(e,n);
}
int melee_web_menu_host_leave(MeleeWebMenuHost* h,int abort_scene,char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu leave requires an idle live scene");
    uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(bytes);
    MeleeWebPadState* input=melee_web_pad_state_decode(bytes,sizeof(bytes),e,n);
    if(!input)return 0;
    const int was_sss=melee_web_menu_phase(h->session)==MELEE_WEB_MENU_SSS;
    const int result=abort_scene?melee_web_menu_abort(h->session,e,n):
        melee_web_menu_phase(h->session)==MELEE_WEB_MENU_CSS?melee_web_menu_leave_css(h->session,e,n):melee_web_menu_leave_sss(h->session,e,n);
    if(!result){melee_web_pad_state_free(input);return 0;}
    /* SSS has no SIS table of its own; the scene preparation heap is ours. */
    if(was_sss)HSD_SisLib_803A5FBC();
    h->selected_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->selected_stages=*gmMainLib_8015EDA4();
    h->selected_rules=*gmMainLib_GetGameRules();
    h->selected_preferences=*gmMainLib_8015CC58();
    melee_web_pad_state_free(h->input);h->input=input;
    h->entered=0;restore_context(h);return ok(e,n);
}
int melee_web_menu_host_phase(const MeleeWebMenuHost* h){return h&&h==owner?melee_web_menu_phase(h->session):MELEE_WEB_MENU_CLOSED;}
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
int melee_web_menu_host_provenance(const MeleeWebMenuHost* h,MeleeWebPipelineSourceContext* out){
    if(!h||h!=owner||!out)return 0;
    const int phase=melee_web_menu_phase(h->session);
    const int sss=phase==MELEE_WEB_MENU_SSS||phase==MELEE_WEB_MENU_SSS_READY;
    const CSSData* css=melee_web_menu_css(h->session);
    const SSSData* stages=melee_web_menu_sss(h->session);
    if(!css||!stages)return 0;
    const StartMeleeData* start=sss?&stages->vs.start:&css->vs.start;
    memset(out,0,sizeof(*out));
    for(unsigned i=0;i<4;++i){out->players[i].motion_id=-1;out->players[i].stocks=-1;}
    out->scene=sss?MELEE_WEB_PIPELINE_SCENE_SSS:MELEE_WEB_PIPELINE_SCENE_CSS;
    out->phase=MELEE_WEB_PIPELINE_PHASE_INTERACTIVE;
    out->world_generation=melee_web_gameplay_generation();
    out->source_tick=melee_web_gameplay_provenance_tick();
    out->owner_kind=MELEE_WEB_PIPELINE_OWNER_MENU_SCENE;
    out->owner_id=out->scene;
    out->stage=start->rules.stkind;out->hud_layout=start->rules.x0_3;
    const MeleeWebStageContent* stage=melee_web_stage_content(start->rules.stkind);
    out->ground=stage?stage->ground_kind:UINT32_MAX;
    int count=melee_web_menu_active_player_count(start);
    if(count<0||count>4)return 0;
    out->active_player_count=(uint32_t)count;
    for(int i=0;i<count;++i){
        const PlayerInitData* source=&start->players[i];
        const MeleeWebFighterContent* fighter=melee_web_fighter_content(source->ckind);
        out->players[i].character=source->ckind;
        out->players[i].fighter_kind=fighter?fighter->fighter_kind:UINT32_MAX;
        out->players[i].costume=source->color;out->players[i].subcolor=source->sub_color;
        out->players[i].effect_bank=fighter?fighter->effect_bank:UINT32_MAX;
        out->players[i].motion_id=-1;out->players[i].stocks=source->stocks;
    }
    return 1;
}
#endif
int melee_web_menu_host_selection(const MeleeWebMenuHost* h,MeleeWebMenuMatchSelection* out,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed)
        return fail(e,n,"Selection requires a closed menu scene with owned RNG");
    const VsModeData* vs=melee_web_menu_ready_vs(h->session);
    if(!vs||!melee_web_menu_sss_selection_valid(melee_web_menu_sss(h->session)))
        return fail(e,n,"Original menus have not committed a supported selection");
    out->start=vs->start;
    const int count=melee_web_menu_active_player_count(&vs->start);
    if(count<MELEE_WEB_MENU_MIN_PLAYERS||count>MELEE_WEB_MENU_MAX_PLAYERS)
        return fail(e,n,"Original menu did not commit two through four active players");
    memset(out->players,0,sizeof(out->players));
    out->player_count=(uint32_t)count;
    for(unsigned i=0;i<(unsigned)count;i++){
        const PlayerInitData* p=&vs->start.players[i];
        const unsigned port=p->slot?p->slot-1:i;
        const MeleeWebFighterContent* content=melee_web_fighter_content(p->ckind);
        if(!content||port!=i||p->color>=content->costumes||p->sub_color>4)
            return fail(e,n,"Unsupported original menu port, costume or tint");
        out->players[i].controller=port;out->players[i].stocks=p->stocks;
        out->players[i].costume=p->color;out->players[i].sub_color=p->sub_color;
    }
    if(vs->start.rules.x0_3<1||vs->start.rules.x0_3>6)
        return fail(e,n,"Original HUD layout is unsupported");
    out->hud_layout=vs->start.rules.x0_3;
    out->random_seed=h->seed;
    out->unlocked_characters=h->selected_characters;
    out->unlocked_stages=h->selected_stages;
    out->save_profile_present=1;
    return ok(e,n);
}
int melee_web_menu_host_raw_selection(const MeleeWebMenuHost* h,StartMeleeData* out,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||!out||seed_ptr!=&h->seed||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Raw selection requires a completed closed SSS scene");
    const SSSData* sss=melee_web_menu_sss(h->session);
    if(!sss||!sss->start_game||!melee_web_menu_sss_selection_valid(sss))
        return fail(e,n,"Original SSS has not committed a supported raw selection");
    *out=sss->vs.start;return ok(e,n);
}
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost* h){
    if(!h||h!=owner||h->entered||h->audio||melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)return NULL;
    return h->input;
}
int melee_web_menu_host_match_finished(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||h->results_active||seed_ptr!=&h->seed||melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Match must restore its source ownership before returning to CSS");
    MeleeWebPadState* input=melee_web_pad_state_decode(bytes,MELEE_WEB_PAD_STATE_BYTES,e,n);
    if(!input)return 0;
    melee_web_pad_state_free(h->input);h->input=input;
    h->seed=seed;return ok(e,n);
}
static void restore_results_route(MeleeWebMenuHost* h){
    *gmVsMelee_GetVsData()=h->route_saved_vs;
    gmVsMelee_VsExitInfo=h->route_saved_exit;
    gmVsMelee_ResultsEnterData=h->route_saved_result;
    *gm_GetChallengerData()=h->route_saved_challenger;
    memcpy(gmVsMelee_GetKOCounts(),h->route_saved_ko,sizeof(h->route_saved_ko));
    *gmMainLib_GetGameRules()=h->route_saved_rules;
    *gmMainLib_8015CC58()=h->route_saved_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->route_saved_characters;
    *gmMainLib_8015EDA4()=h->route_saved_stages;
    if(!melee_web_vs_mode_end())abort();
    h->results_active=h->results_exited=h->results_committed=h->prize_active=0;
}
int melee_web_menu_host_results_begin(MeleeWebMenuHost* h,
    const MatchExitInfo* exit_info,uint32_t seed,ResultsMatchInfo* result,
    char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||h->results_active||
       seed_ptr!=&h->seed||!exit_info||!result||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_READY)
        return fail(e,n,"Results routing requires a completed closed match");
    if(!melee_web_vs_mode_begin())return fail(e,n,"Original VS mode is already owned");
    h->route_saved_vs=*gmVsMelee_GetVsData();
    h->route_saved_exit=gmVsMelee_VsExitInfo;
    h->route_saved_result=gmVsMelee_ResultsEnterData;
    h->route_saved_challenger=*gm_GetChallengerData();
    memcpy(h->route_saved_ko,gmVsMelee_GetKOCounts(),sizeof(h->route_saved_ko));
    h->route_saved_rules=*gmMainLib_GetGameRules();
    h->route_saved_preferences=*gmMainLib_8015CC58();
    h->route_saved_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->route_saved_stages=*gmMainLib_8015EDA4();
    h->results_active=1;h->results_exited=h->results_committed=0;h->seed=seed;
    *gmMainLib_GetGameRules()=h->selected_rules;
    *gmMainLib_8015CC58()=h->selected_preferences;
    *gmMainLib_GetUnlockedCharactersBitmaskPtr()=h->selected_characters;
    *gmMainLib_8015EDA4()=h->selected_stages;
    const CSSData* css=melee_web_menu_css(h->session);
    *gmVsMelee_GetVsData()=css->vs;
    memcpy(gmVsMelee_GetKOCounts(),css->ko_counts,sizeof(h->route_saved_ko));
    gmVsMelee_VsExitInfo=*exit_info;
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Vs))abort();
    gm_Mode_Vs_States[2].on_exit(&gm_Mode_Vs_States[2]);
    const int next=melee_web_vs_mode_next_state();
    if(next!=gmVsMode_State_Results){
        restore_results_route(h);
        if(e&&n)snprintf(e,n,"Original VS requested unsupported state %d before Results",next);
        return 0;
    }
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Results))abort();
    gm_Mode_Vs_States[4].on_enter(&gm_Mode_Vs_States[4]);
    *result=gmVsMelee_ResultsEnterData;
    return ok(e,n);
}
static int commit_results_route(MeleeWebMenuHost* h,char* e,size_t n){
    if(!melee_web_menu_commit_results(h->session,gmVsMelee_GetVsData(),
        gmVsMelee_GetKOCounts(),e,n))return 0;
    h->selected_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->selected_stages=*gmMainLib_8015EDA4();
    h->selected_rules=*gmMainLib_GetGameRules();
    h->selected_preferences=*gmMainLib_8015CC58();
    h->results_committed=1;
    return ok(e,n);
}
int melee_web_menu_host_results_exit(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->results_active||h->results_exited||
       !melee_web_results_context_exit_ready())
        return fail(e,n,"Results mode exit requires its live source world");
    gm_Mode_Vs_States[4].on_exit(&gm_Mode_Vs_States[4]);
    h->results_exited=1;
    const int next=melee_web_vs_mode_next_state();
    if(next==gmVsMode_State_Prize)return ok(e,n);
    if(next!=gmVsMode_State_Css){
        if(e&&n)snprintf(e,n,"Original Results requested unsupported state %d",next);
        return 0;
    }
    return commit_results_route(h,e,n);
}
int melee_web_menu_host_results_end(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||!h->results_active||!h->results_exited||h->prize_active||
       melee_web_gameplay_generation()||seed_ptr!=&h->seed)
        return fail(e,n,"Results must tear down before restoring its route owner");
    if(!h->results_committed){
        if(melee_web_vs_mode_next_state()!=gmVsMode_State_Prize)
            return fail(e,n,"Results has no supported continuation");
        MeleeWebPadState* next_input=melee_web_pad_state_decode(input,MELEE_WEB_PAD_STATE_BYTES,e,n);
        if(!next_input)return 0;
        melee_web_pad_state_free(h->input);h->input=next_input;h->seed=seed;
        return ok(e,n);
    }
    restore_results_route(h);
    return melee_web_menu_host_match_finished(h,seed,input,e,n);
}
int melee_web_menu_host_results_destination(const MeleeWebMenuHost* h){
    if(!h||h!=owner)return -1;
    return h->results_active&&h->results_exited?melee_web_vs_mode_next_state():gmVsMode_State_Css;
}
int melee_web_menu_host_prize_enter(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->results_active||!h->results_exited||h->results_committed||
       h->prize_active||!melee_web_gameplay_generation()||
       melee_web_vs_mode_next_state()!=gmVsMode_State_Prize)
        return fail(e,n,"Prize mode entry requires the retained Results route and a prepared world");
    if(!melee_web_vs_mode_select_state(gmVsMode_State_Prize))abort();
    gm_Mode_Vs_States[7].on_enter(&gm_Mode_Vs_States[7]);
    h->prize_active=1;
    return ok(e,n);
}
int melee_web_menu_host_prize_exit(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->prize_active||h->results_committed||
       !melee_web_prize_context_exit_ready())
        return fail(e,n,"Prize mode exit requires its completed live source scene");
    gm_Mode_Vs_States[7].on_exit(&gm_Mode_Vs_States[7]);
    const int next=melee_web_vs_mode_next_state();
    if(next!=gmVsMode_State_Css){
        if(e&&n)snprintf(e,n,"Original Prize requested unsupported state %d",next);
        return 0;
    }
    return commit_results_route(h,e,n);
}
int melee_web_menu_host_prize_end(MeleeWebMenuHost* h,uint32_t seed,
    const uint8_t input[MELEE_WEB_PAD_STATE_BYTES],char* e,size_t n){
    if(!h||h!=owner||!h->prize_active||!h->results_committed||
       melee_web_gameplay_generation()||seed_ptr!=&h->seed)
        return fail(e,n,"Prize must tear down before restoring its route owner");
    restore_results_route(h);
    return melee_web_menu_host_match_finished(h,seed,input,e,n);
}
int melee_web_menu_host_destroy(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||h->entered||h->audio||seed_ptr!=&h->seed)return fail(e,n,"Close native menu scene and restore RNG before destroying host");
    if(h->results_active)restore_results_route(h);
    if(!melee_web_menu_session_destroy(h->session,e,n))return 0;
    if(!melee_web_save_profile_owner_deactivate(h->profile,e,n)||
       !melee_web_save_profile_owner_destroy(h->profile,e,n))return 0;
    seed_ptr=h->saved_seed;owner=NULL;melee_web_pad_state_free(h->input);free(h);return ok(e,n);
}
