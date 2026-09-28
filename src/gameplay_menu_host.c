#include "gameplay_menu_host.h"
#include "gameplay_menu.h"
#include "gameplay_save_profile.h"
#include "gameplay_content.h"
#include "gameplay_bootstrap.h"
#include "gameplay_results_context.h"
#include "gameplay_prize_context.h"
#include "gameplay_audio_bank_transport.h"
#include "hsd_native_joint.h"
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gmmenumode.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gmtitle.h>
#include <melee/gm/gmtitlemode.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gm_unsplit.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/rumble.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/video.h>
#include <sysdolphin/baselib/random.h>
#include <melee/mn/mnmain.h>
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
extern int melee_web_vs_mode_pending_mode(void);
extern int melee_web_vs_mode_set_route(int current_mode, int previous_mode);
extern struct GameSceneInfo* melee_web_current_scene_info(void);
enum {
    MELEE_WEB_HOST_SCENE_NONE = 0,
    MELEE_WEB_HOST_SCENE_CSS = 1,
    MELEE_WEB_HOST_SCENE_SSS = 2,
    MELEE_WEB_HOST_SCENE_TITLE = 3,
    MELEE_WEB_HOST_SCENE_MAIN = 4,
};
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
    uint8_t initial_game_rules[0x18];
    uint8_t initial_save_data[0x55E8];
    int initial_replay_context;
    VsModeData route_saved_vs;
    MatchExitInfo route_saved_exit;
    ResultsMatchInfo route_saved_result;
    ChallengerData route_saved_challenger;
    u8 route_saved_ko[GM_MAX_PLAYERS];
    /* Persistent source mode/scene payloads.  The source callbacks retain
     * GameSceneInfo through every tick, so gm_GetCurrentSceneExitData never
     * observes a stack object or a browser-owned substitute. */
    struct GameSceneInfo* saved_scene_info;
    GameModeState source_state;
    GameSceneInfo source_scene_info;
    GameModeState vs_css_state;
    GameModeState vs_sss_state;
    MenuEnterData main_enter;
    MenuExitData main_exit;
    int title_exit_payload;
    int source_scene;
    int source_target_mode;
    int vs_mode_owned;
    int aborted_source_scene;
    int css_parent_route_requested;
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
    MeleeWebMenuHost* h=data;
    if(!live(h,e,n)||!melee_web_menu_clock_request(request))return fail(e,n,"Invalid original menu transition state");
    h->css_parent_route_requested = 0;
    if (scene == MELEE_WEB_MENU_SCENE_CSS && *request != 0 &&
        gm_GetCurrentGameMode() == GM_VS &&
        melee_web_vs_mode_pending_mode() == GM_MENU) {
        if (!melee_web_menu_mark_css_parent_route(h->session, e, n)) {
            return 0;
        }
        h->css_parent_route_requested = 1;
    }
    h->transition=*request;return 1;
}

static int source_scene_enter(void* data, MeleeWebMenuScene scene,
                              char* e, size_t n)
{
    MeleeWebMenuHost* h = data;
    GameModeState* state;
    CSSData* css;
    SSSData* sss;

    if (!live(h, e, n)) {
        return 0;
    }
    memset(&h->source_scene_info, 0, sizeof(h->source_scene_info));
    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        css = (CSSData*) melee_web_menu_css(h->session);
        if (css == NULL) {
            return fail(e, n, "Original CSS payload is unavailable at mode entry");
        }
        h->vs_css_state = gm_Mode_Vs_States[gmVsMode_State_Css];
        h->vs_css_state.info.enter_data = css;
        h->vs_css_state.info.exit_data = css;
        *gmVsMelee_GetVsData() = css->vs;
        state = &h->vs_css_state;
        h->source_scene_info.scene_kind = GS_CSS;
        h->source_scene_info.enter_data = css;
        h->source_scene_info.exit_data = css;
        state->on_enter(state);
        h->source_scene = MELEE_WEB_HOST_SCENE_CSS;
    } else if (scene == MELEE_WEB_MENU_SCENE_SSS) {
        sss = (SSSData*) melee_web_menu_sss(h->session);
        if (sss == NULL) {
            return fail(e, n, "Original SSS payload is unavailable at mode entry");
        }
        h->vs_sss_state = gm_Mode_Vs_States[gmVsMode_State_Sss];
        h->vs_sss_state.info.enter_data = sss;
        h->vs_sss_state.info.exit_data = sss;
        *gmVsMelee_GetVsData() = sss->vs;
        state = &h->vs_sss_state;
        h->source_scene_info.scene_kind = GS_SSS;
        h->source_scene_info.enter_data = sss;
        h->source_scene_info.exit_data = sss;
        state->on_enter(state);
        h->source_scene = MELEE_WEB_HOST_SCENE_SSS;
    } else {
        return fail(e, n, "Unsupported menu scene lifecycle callback");
    }
    gm_801A4B88(&h->source_scene_info);
    return ok(e, n);
}

static int source_scene_exit(void* data, MeleeWebMenuScene scene,
                             char* e, size_t n)
{
    MeleeWebMenuHost* h = data;
    CSSData* css;
    SSSData* sss;

    if (!live(h, e, n)) {
        return 0;
    }
    if (scene == MELEE_WEB_MENU_SCENE_CSS) {
        const int parent_route = h->css_parent_route_requested;
        css = (CSSData*) melee_web_menu_css(h->session);
        if (css == NULL || h->source_scene != MELEE_WEB_HOST_SCENE_CSS) {
            return fail(e, n, "Original CSS payload is unavailable at mode exit");
        }
        h->vs_css_state.info.exit_data = css;
        if (parent_route &&
            (gm_GetCurrentGameMode() != GM_VS ||
             melee_web_vs_mode_pending_mode() != GM_MENU)) {
            return fail(e, n,
                        "Original CSS parent route was no longer pending before OnExit");
        }
        gm_Mode_Vs_States[gmVsMode_State_Css].on_exit(&h->vs_css_state);
        if (parent_route) {
            if (gm_GetCurrentGameMode() != GM_VS ||
                melee_web_vs_mode_pending_mode() != GM_MENU) {
                return fail(e, n,
                            "Original CSS parent route changed before its OnExit completed");
            }
            if (!h->vs_mode_owned) {
                if (!melee_web_vs_mode_begin()) {
                    return fail(e, n,
                                "Original VS mode lease unavailable for CSS parent return");
                }
                h->vs_mode_owned = 1;
            }
            h->source_target_mode = GM_MENU;
            if (!melee_web_vs_mode_set_route(GM_MENU, GM_VS)) {
                return fail(e, n,
                            "Original GM_MENU route owner could not commit the CSS parent return");
            }
        } else if (css->pending_scene_change == CSSPendingSceneChange_2) {
            /* CSS parent return keeps a checked VS lease through GM_MENU. */
            if (!h->vs_mode_owned) {
                if (!melee_web_vs_mode_begin()) {
                    return fail(e, n,
                                "Original VS mode lease unavailable for CSS parent return");
                }
                h->vs_mode_owned = 1;
            }
            if (!melee_web_vs_mode_set_route(GM_MENU, GM_VS)) {
                return fail(e, n,
                            "Original GM_MENU route owner could not set GM_VS provenance");
            }
        } else if (css->pending_scene_change == 1) {
            /* CSS -> SSS remains inside the same GM_VS owner. */
            h->source_target_mode = -1;
        } else if (h->vs_mode_owned) {
            if (!melee_web_vs_mode_end()) {
                return fail(e, n, "Original VS mode lease did not release after CSS");
            }
            h->vs_mode_owned = 0;
            h->source_target_mode = -1;
        } else {
            h->source_target_mode = -1;
        }
    } else if (scene == MELEE_WEB_MENU_SCENE_SSS) {
        sss = (SSSData*) melee_web_menu_sss(h->session);
        if (sss == NULL || h->source_scene != MELEE_WEB_HOST_SCENE_SSS) {
            return fail(e, n, "Original SSS payload is unavailable at mode exit");
        }
        h->vs_sss_state.info.exit_data = sss;
        gm_Mode_Vs_States[gmVsMode_State_Sss].on_exit(&h->vs_sss_state);
        if (sss->start_game && h->vs_mode_owned) {
            if (!melee_web_vs_mode_end()) {
                return fail(e, n, "Original VS mode lease did not release before match");
            }
            h->vs_mode_owned = 0;
        }
    } else {
        return fail(e, n, "Unsupported menu scene lifecycle callback");
    }
    return ok(e, n);
}

static int source_scene_tick(MeleeWebMenuHost* h, char* e, size_t n)
{
    int request = 0;

    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE) {
        gm_Scene_Title_OnFrame();
    } else if (h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        mnMain_Scene_OnFrame();
    } else {
        return fail(e, n, "Original title/main tick has no live source scene");
    }
    if (!runtime_scheduler(h, e, n)) {
        return 0;
    }
    if (!melee_web_menu_clock_request(&request)) {
        return fail(e, n, "Invalid original title/main transition state");
    }
    h->transition = request;
    return request == 0 ? MELEE_WEB_MENU_RESULT_TICKED
                        : MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED;
}

MeleeWebMenuHost* melee_web_menu_host_create(char* e,size_t n){
    if(owner||!seed_ptr){fail(e,n,"A menu host already exists or source RNG is unavailable");return NULL;}
    MeleeWebMenuHost* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate native menu host");return NULL;}
    h->source_target_mode = -1;
    h->saved_scene_info = melee_web_current_scene_info();
    MeleeWebMenuRuntime runtime={h,runtime_check,runtime_scheduler,runtime_transition,
                                 source_scene_enter,source_scene_exit};
    MeleeWebMenuConfig config={4,0,0};
    h->profile=melee_web_save_profile_owner_create(e,n);
    if(!h->profile){free(h);return NULL;}
    if(!melee_web_save_profile_owner_activate(h->profile,e,n)){
        if(!melee_web_save_profile_owner_destroy(h->profile,NULL,0))abort();
        free(h);return NULL;
    }
    /* Keep the original all-unlocked character roster. Source-owned unlock
     * and notification routines establish the same completed notification
     * state expected by the Title callback. The stage mask stays limited to
     * the four authored stages supported by this browser route. */
    if(!melee_web_save_profile_owner_initialize_default(h->profile,e,n)||
       !melee_web_save_profile_owner_initialize_menu_roster(
           h->profile,0x01c0,e,n)){
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

int melee_web_menu_host_apply_replay_context(
    MeleeWebMenuHost* h, uint32_t random_seed,
    const uint8_t pad_state[MELEE_WEB_PAD_STATE_BYTES],
    const uint8_t css_data[0x148], const uint8_t ko_counts[GM_MAX_PLAYERS],
    const uint8_t game_rules[0x18], const uint8_t save_data[0x55E8],
    char* e, size_t n)
{
    MeleeWebPadState* input;
    if(!h||h!=owner||h->entered||h->audio||h->initial_replay_context||
       !pad_state||!css_data||!ko_counts||!game_rules||!save_data||
       melee_web_menu_phase(h->session)!=MELEE_WEB_MENU_CREATED||
       seed_ptr!=&h->seed)
        return fail(e,n,"Whole-session first-CSS context requires an unentered menu host");
    /* Profile application is the other owner mutation in this operation.
     * Check its aliases before decoding or installing CSS so a stale profile
     * cannot leave an unentered session holding a committed CSS context. */
    if(!melee_web_save_profile_owner_live(h->profile,e,n))return 0;
    input=melee_web_pad_state_decode(pad_state,MELEE_WEB_PAD_STATE_BYTES,e,n);
    if(!input)return 0;
    if(!melee_web_menu_apply_reference_css_context(
           h->session,css_data,ko_counts,e,n)){
        melee_web_pad_state_free(input);
        return 0;
    }
    if(!melee_web_save_profile_owner_apply_reference_context(
           h->profile,game_rules,save_data,e,n)){
        melee_web_pad_state_free(input);
        return 0;
    }
    memcpy(h->initial_game_rules,game_rules,sizeof(h->initial_game_rules));
    memcpy(h->initial_save_data,save_data,sizeof(h->initial_save_data));
    melee_web_pad_state_free(h->input);h->input=input;
    h->seed=random_seed;h->initial_replay_context=1;
    return ok(e,n);
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
static int host_prepare_world(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                              MeleeWebMenuPhase phase, int source_scene,
                              char* e, size_t n)
{
    const uint64_t audio_generation=melee_web_audio_generation(audio);
    if(!h||h!=owner||h->entered||h->audio||h->results_active||seed_ptr!=&h->seed||!melee_web_audio_is_active(audio)||
       !audio_generation||!melee_web_audio_bank_transport_active()||!melee_web_gameplay_stats().generation)
        return fail(e,n,"Native menu enter requires a fresh owned world and source audio");
    if(!source_scene&&(phase!=MELEE_WEB_MENU_CREATED&&phase!=MELEE_WEB_MENU_CSS_READY&&phase!=MELEE_WEB_MENU_SSS_READY&&phase!=MELEE_WEB_MENU_READY))
        return fail(e,n,"Native menu session cannot enter from this phase");
    if(!source_scene&&phase!=MELEE_WEB_MENU_CREATED&&!h->input)
        return fail(e,n,"Returning menu scene requires retained source PAD history");
    if(!melee_web_native_world_enable(e,n))return 0;
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
        if(h->initial_replay_context){
            /* Re-apply the copied source ranges immediately before OnEnter so
             * construction cannot accidentally replace the declared context.
             * The owner performs endian-aware layout translation. */
            if(!melee_web_save_profile_owner_apply_reference_context(
                   h->profile,h->initial_game_rules,h->initial_save_data,e,n)){
                restore_context(h);return 0;
            }
        }else{
            *gmMainLib_GetGameRules()=gmMainLib_803D4A48;
            gmMainLib_GetGameRules()->mode=1;gmMainLib_GetGameRules()->stock_count=4;
        }
        if(!h->initial_replay_context){
            gmMainLib_8015CC58()->item_freq=(u8)-1;
            gmMainLib_8015CC58()->item_mask=UINT64_MAX;
            gmMainLib_8015CC58()->rumble_enabled[0]=true;
            gmMainLib_8015CC58()->rumble_enabled[1]=true;
        }
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
    HSD_SisLib_803A6048(source_scene||phase==MELEE_WEB_MENU_SSS_READY?0x4800:0x2400);
    return ok(e,n);
}

int melee_web_menu_host_enter(MeleeWebMenuHost* h,MeleeWebAudio* audio,char* e,size_t n){
    const MeleeWebMenuPhase phase=melee_web_menu_phase(h?h->session:NULL);
    int acquired_vs = 0;
    if(!host_prepare_world(h,audio,phase,0,e,n))return 0;
    if ((phase == MELEE_WEB_MENU_CREATED || phase == MELEE_WEB_MENU_READY) &&
        !h->vs_mode_owned) {
        const int began_vs = melee_web_vs_mode_begin();
        if (!began_vs || !melee_web_vs_mode_set_route(GM_VS, GM_MENU)) {
            if (began_vs && !melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
            HSD_SisLib_803A5FBC();
            restore_context(h);
            return fail(e, n, "Original VS mode lease unavailable for CSS entry");
        }
        h->vs_mode_owned = 1;
        acquired_vs = 1;
    }
    int accepted=phase==MELEE_WEB_MENU_SSS_READY?melee_web_menu_enter_sss(h->session,e,n):
        phase==MELEE_WEB_MENU_READY?melee_web_menu_return_to_css(h->session,e,n):melee_web_menu_enter_css(h->session,e,n);
    if(!accepted){
        if (acquired_vs) {
            if (!melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
        }
        HSD_SisLib_803A5FBC();restore_context(h);return 0;
    }
    h->source_scene=phase==MELEE_WEB_MENU_SSS_READY?MELEE_WEB_HOST_SCENE_SSS:MELEE_WEB_HOST_SCENE_CSS;
    h->entered=1;lb_8001CF18();return ok(e,n);
}

static int host_enter_title_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    int previous;
    int acquired = 0;

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE) {
        return fail(e, n, "A source menu scene is already active");
    }
    previous = h->source_target_mode == GM_TITLE ? GM_MENU : GM_TITLE;
    if (!h->vs_mode_owned) {
        if (!melee_web_vs_mode_begin()) {
            return fail(e, n, "Original VS mode is already owned by another route");
        }
        h->vs_mode_owned = 1;
        acquired = 1;
    }
    if (!melee_web_vs_mode_set_route(GM_TITLE, previous)) {
        if (acquired) {
            if (!melee_web_vs_mode_end()) abort();
            h->vs_mode_owned = 0;
        }
        return fail(e, n, "Original title route could not set mode provenance");
    }
    h->source_state = gm_Mode_Title_States[0];
    h->source_state.info.scene_kind = GS_TITLE;
    h->source_state.info.enter_data = NULL;
    h->source_state.info.exit_data = &h->title_exit_payload;
    h->title_exit_payload = 0;
    h->source_target_mode = -1;
    h->source_state.on_enter(&h->source_state);
    /* The GameModeState is persistent in the host, and its embedded
     * GameSceneInfo owns the exact payload written by gm_Scene_Title_OnFrame. */
    gm_801A4B88(&h->source_state.info);
    gm_Scene_Title_OnEnter(NULL);
    h->source_scene = MELEE_WEB_HOST_SCENE_TITLE;
    return ok(e, n);
}

static int host_enter_main_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    const int previous = gm_GetPreviousGameMode();

    if (h->source_scene != MELEE_WEB_HOST_SCENE_NONE) {
        return fail(e, n, "A source menu scene is already active");
    }
    /* The mode callback owns menu_kind/hovered_selection.  Check the route
     * provenance before it runs so a stale game-mode lease cannot silently
     * open the wrong parent menu. */
    if (!h->vs_mode_owned) {
        return fail(e, n,
                    "Original GM_MENU entry has no retained source mode lease");
    }
    if (h->vs_mode_owned && previous != GM_VS && previous != GM_TITLE &&
        previous != GM_MENU) {
        return fail(e, n,
                    "Original GM_MENU entry lacks a supported previous-mode route");
    }
    if (!melee_web_vs_mode_set_route(GM_MENU, previous)) {
        return fail(e, n, "Original GM_MENU route could not set mode provenance");
    }
    h->source_state = gm_Mode_Menu_States[0];
    h->source_state.info.scene_kind = GS_MENU;
    h->source_state.info.enter_data = &h->main_enter;
    h->source_state.info.exit_data = &h->main_exit;
    memset(&h->main_enter, 0, sizeof(h->main_enter));
    memset(&h->main_exit, 0, sizeof(h->main_exit));
    h->source_target_mode = -1;
    h->source_state.on_enter(&h->source_state);
    h->source_scene_info.scene_kind = GS_MENU;
    h->source_scene_info.enter_data = &h->main_enter;
    h->source_scene_info.exit_data = &h->main_exit;
    gm_801A4B88(&h->source_scene_info);
    mnMain_Scene_OnEnter(&h->main_enter);
    h->source_scene = MELEE_WEB_HOST_SCENE_MAIN;
    return ok(e, n);
}

int melee_web_menu_host_enter_title(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                                    char* e, size_t n)
{
    const MeleeWebMenuPhase phase = melee_web_menu_phase(h ? h->session : NULL);

    if (!host_prepare_world(h, audio, phase, 1, e, n)) {
        return 0;
    }
    if (!host_enter_title_scene(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}

int melee_web_menu_host_enter_main(MeleeWebMenuHost* h, MeleeWebAudio* audio,
                                   char* e, size_t n)
{
    const MeleeWebMenuPhase phase = melee_web_menu_phase(h ? h->session : NULL);

    if (!host_prepare_world(h, audio, phase, 1, e, n)) {
        return 0;
    }
    if (!host_enter_main_scene(h, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}

int melee_web_menu_host_tick(MeleeWebMenuHost* h,const PADStatus raw[4],char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing||!raw)return fail(e,n,"Native menu tick requires an idle live scene and four raw ports");
    if(HSD_PadLibData.queue!=&h->queue||HSD_PadLibData.qcount)return fail(e,n,"Native menu raw PAD queue is not idle");
    memset(&h->queue,0,sizeof(h->queue));memcpy(h->queue.stat,raw,sizeof(h->queue.stat));
    HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;HSD_PadLibData.qcount=1;
    /* Supplied raw samples replace PADRead, not the rumble interpreter that
     * precedes it in HSD_PadRenewRawStatus. Match stepping uses this same
     * source boundary; menu confirmations must advance and release requests. */
    HSD_PadRumbleInterpret();
    HSD_PadRenewMasterStatus();HSD_PadRenewCopyStatus();HSD_PadRenewGameStatus();
    if(HSD_PadLibData.qcount)return fail(e,n,"Source PAD processing did not consume its sample");
    gm_EvaluateAllControllerInputs();
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE ||
        h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        return source_scene_tick(h, e, n);
    }
    return melee_web_menu_tick(h->session,e,n);
}
int melee_web_menu_host_draw(MeleeWebMenuHost* h,char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu draw requires an idle live scene");
    if(h->transition!=0)return ok(e,n);
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

static int host_leave_source_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES];
    MeleeWebPadState* next_input;

    if (h->transition == 0) {
        return fail(e, n, "Original title/main scene has no completed transition");
    }
    melee_web_pad_state_capture(bytes);
    next_input = melee_web_pad_state_decode(bytes, sizeof(bytes), e, n);
    if (next_input == NULL) {
        return 0;
    }
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE) {
        int* source_exit_payload = (int*) gm_GetCurrentSceneExitData();
        if (source_exit_payload != &h->title_exit_payload ||
            h->source_state.info.exit_data != source_exit_payload) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                        "Original title exit payload lost its persistent GameSceneInfo owner");
        }
        if ((*source_exit_payload & HSD_PAD_START) == 0) {
            melee_web_pad_state_free(next_input);
            return fail(e, n,
                        "Original title transition did not contain the retail Start route");
        }
        h->source_state.on_exit(&h->source_state);
        const int requested_mode = melee_web_vs_mode_pending_mode();
        if (requested_mode != GM_MENU) {
            melee_web_pad_state_free(next_input);
            if (e && n)
                snprintf(e, n,
                         "Original title requested unsupported destination %d from buttons 0x%x",
                         requested_mode, *source_exit_payload);
            return 0;
        }
        h->source_target_mode = requested_mode;
        if (!melee_web_vs_mode_set_route(requested_mode, GM_TITLE)) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original title exit could not set GM_MENU provenance");
        }
    } else if (h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        const int requested_mode = h->main_exit.pending_mode;
        if (requested_mode != GM_TITLE && requested_mode != GM_MENU &&
            requested_mode != GM_VS) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main scene requested an unsupported mode");
        }
        h->source_state.info.exit_data = &h->main_exit;
        h->source_state.on_exit(&h->source_state);
        if (melee_web_vs_mode_pending_mode() != requested_mode) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main exit changed its checked destination");
        }
        h->source_target_mode = requested_mode;
        if (!melee_web_vs_mode_set_route(requested_mode, GM_MENU)) {
            melee_web_pad_state_free(next_input);
            return fail(e, n, "Original main exit could not set mode provenance");
        }
    } else {
        melee_web_pad_state_free(next_input);
        return fail(e, n, "Source scene is not title or main");
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
    h->entered = 0;
    h->transition = 0;
    h->css_parent_route_requested = 0;
    melee_web_pad_state_free(h->input);
    h->input = next_input;
    restore_context(h);
    return ok(e, n);
}

/* Eject is a host teardown, not a source menu choice.  Title and Main have no
 * scene OnExit callback; their GameModeState OnExit callbacks schedule a real
 * destination and must not be called just to retire browser-owned resources.
 * GameplayMenuWorld::close() owns GObj/card/audio teardown after this retires
 * the host's live-scene lease. */
static int host_abort_source_scene(MeleeWebMenuHost* h, char* e, size_t n)
{
    if ((h->source_scene != MELEE_WEB_HOST_SCENE_TITLE &&
         h->source_scene != MELEE_WEB_HOST_SCENE_MAIN) ||
        !h->entered || h->drawing) {
        return fail(e, n, "Source-scene abort requires an idle live title or main scene");
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_NONE;
    h->entered = 0;
    h->transition = 0;
    h->source_target_mode = -1;
    h->css_parent_route_requested = 0;
    h->aborted_source_scene = 1;
    restore_context(h);
    return ok(e, n);
}

int melee_web_menu_host_leave(MeleeWebMenuHost* h,int abort_scene,char* e,size_t n){
    if(!live(h,e,n)||!h->entered||h->drawing)return fail(e,n,"Native menu leave requires an idle live scene");
    if (h->source_scene == MELEE_WEB_HOST_SCENE_TITLE ||
        h->source_scene == MELEE_WEB_HOST_SCENE_MAIN) {
        if (abort_scene) {
            return host_abort_source_scene(h, e, n);
        }
        return host_leave_source_scene(h, e, n);
    }
    uint8_t bytes[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(bytes);
    MeleeWebPadState* input=melee_web_pad_state_decode(bytes,sizeof(bytes),e,n);
    if(!input)return 0;
    const int was_sss=melee_web_menu_phase(h->session)==MELEE_WEB_MENU_SSS;
    const int was_css=melee_web_menu_phase(h->session)==MELEE_WEB_MENU_CSS;
    const int result=abort_scene?melee_web_menu_abort(h->session,e,n):
        melee_web_menu_phase(h->session)==MELEE_WEB_MENU_CSS?melee_web_menu_leave_css(h->session,e,n):melee_web_menu_leave_sss(h->session,e,n);
    if(!result){melee_web_pad_state_free(input);return 0;}
    /* SSS has no SIS table of its own; the scene preparation heap is ours. */
    if(was_sss)HSD_SisLib_803A5FBC();
    h->selected_characters=*gmMainLib_GetUnlockedCharactersBitmaskPtr();
    h->selected_stages=*gmMainLib_8015EDA4();
    h->selected_rules=*gmMainLib_GetGameRules();
    h->selected_preferences=*gmMainLib_8015CC58();
    if (!abort_scene && was_css) {
        const CSSData* css = melee_web_menu_css(h->session);
        if (css != NULL && css->pending_scene_change == CSSPendingSceneChange_2) {
            h->source_target_mode = GM_MENU;
        }
    }
    melee_web_pad_state_free(h->input);h->input=input;
    h->entered=0;h->source_scene=MELEE_WEB_HOST_SCENE_NONE;
    /* The source transition has been consumed by menu_leave_* above. Keep
     * only the separately captured route target; a closed READY host must
     * not retain a stale transition that prevents its owned teardown. */
    h->transition=0;h->css_parent_route_requested=0;
    restore_context(h);return ok(e,n);
}
int melee_web_menu_host_phase(const MeleeWebMenuHost* h){return h&&h==owner?melee_web_menu_phase(h->session):MELEE_WEB_MENU_CLOSED;}
int melee_web_menu_host_source_scene(const MeleeWebMenuHost* h){
    return h&&h==owner?h->source_scene:MELEE_WEB_HOST_SCENE_NONE;
}
int melee_web_menu_host_route_target_mode(const MeleeWebMenuHost* h){
    return h&&h==owner&&!h->entered&&
        h->source_scene==MELEE_WEB_HOST_SCENE_NONE?h->source_target_mode:-1;
}

int melee_web_menu_host_reenter_css_after_parent(MeleeWebMenuHost* h,
                                                   MeleeWebAudio* audio,
                                                   char* e, size_t n)
{
    const MeleeWebMenuPhase actual = melee_web_menu_phase(h ? h->session : NULL);

    if (!h || h != owner || h->source_scene != MELEE_WEB_HOST_SCENE_NONE ||
        h->entered || h->audio || h->source_target_mode != GM_VS ||
        actual != MELEE_WEB_MENU_CLOSED || !h->vs_mode_owned) {
        return fail(e, n,
                    "CSS re-entry requires a completed checked GM_MENU -> GM_VS route");
    }
    if (!host_prepare_world(h, audio, MELEE_WEB_MENU_CSS_READY, 0, e, n)) {
        return 0;
    }
    if (!melee_web_menu_reopen_css_after_parent(h->session, e, n)) {
        HSD_SisLib_803A5FBC();
        restore_context(h);
        return 0;
    }
    h->source_scene = MELEE_WEB_HOST_SCENE_CSS;
    h->entered = 1;
    lb_8001CF18();
    return ok(e, n);
}
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
static int results_handoff_owned(MeleeWebMenuHost* h,const char* phase,char* e,size_t n){
    char profile_error[256];
    if(!melee_web_save_profile_owner_live(h->profile,profile_error,sizeof(profile_error))){
        if(e&&n)snprintf(e,n,"Results %s: %s",phase,profile_error);
        return 0;
    }
    return melee_web_results_context_check_handoff(phase,e,n);
}
int melee_web_menu_host_results_exit(MeleeWebMenuHost* h,char* e,size_t n){
    if(!h||h!=owner||!h->results_active||h->results_exited)
        return fail(e,n,"Results mode exit requires its live source world");
    if(!results_handoff_owned(h,"mode OnExit entry",e,n))return 0;
    gm_Mode_Vs_States[4].on_exit(&gm_Mode_Vs_States[4]);
    /* The callback is consumed even if its postcondition fails. Never replay
     * its save writes, allocations or RNG on a rejected handoff. */
    h->results_exited=1;
    if(!results_handoff_owned(h,"mode OnExit",e,n))return 0;
    const int next=melee_web_vs_mode_next_state();
    if(next==gmVsMode_State_Prize)return ok(e,n);
    if(next!=gmVsMode_State_Css){
        if(e&&n)snprintf(e,n,"Original Results requested unsupported state %d",next);
        return 0;
    }
    if(!commit_results_route(h,e,n))return 0;
    return results_handoff_owned(h,"route commit",e,n);
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
    if(!h||h!=owner||h->entered||h->source_scene!=MELEE_WEB_HOST_SCENE_NONE||
       h->transition||h->audio||seed_ptr!=&h->seed)
        return fail(e,n,"Close native menu scene and restore RNG before destroying host");
    const int owns_scene_info =
        melee_web_current_scene_info() == &h->source_scene_info ||
        melee_web_current_scene_info() == &h->source_state.info;
    if (h->aborted_source_scene && !owns_scene_info) {
        return fail(e, n,
                    "Aborted source scene lost its checked GameSceneInfo ownership");
    }
    if(h->results_active)restore_results_route(h);
    if(h->vs_mode_owned){
        if(!melee_web_vs_mode_end())return fail(e,n,"Original VS mode lease did not release at host destroy");
        h->vs_mode_owned=0;
    }
    if(melee_web_menu_parent_route_pending(h->session) &&
       !melee_web_menu_cancel_parent_route(h->session,e,n))return 0;
    if(!melee_web_menu_session_destroy(h->session,e,n))return 0;
    if(!melee_web_save_profile_owner_deactivate(h->profile,e,n)||
       !melee_web_save_profile_owner_destroy(h->profile,e,n))return 0;
    if (owns_scene_info)
        gm_801A4B88(h->saved_scene_info);
    seed_ptr=h->saved_seed;owner=NULL;melee_web_pad_state_free(h->input);free(h);return ok(e,n);
}
