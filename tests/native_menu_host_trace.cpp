#include "gameplay_menu_world.hpp"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_content.h"
#include <algorithm>
#include "gameplay_menu_host.h"
#include "gameplay_save_profile.h"
#include "gameplay_match_session.hpp"
#include "runtime_archive_cache.hpp"
#include "gameplay_results_session.hpp"
#include "gameplay_prize_session.hpp"
#include "gameplay_match_rules.h"
#include "gameplay_bootstrap.h"
#include "gameplay_audio_stream.h"
#include "gameplay_retail_recipe.hpp"
#include "gameplay_source_files.h"
#include "gameplay_stage_map.h"
#include "gameplay_stage_stadium.h"
#include "native_menu_fighter_input.h"
#include "native_menu_stage_input.h"
#include "stadium_c1_stage_state_probe.h"
#include "gameplay_source_memory_runtime.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_stadium_display_owner.h"
#include "gameplay_heap.h"
#include "gameplay_match_context.h"
#include "hsd_native_joint.h"
#include "stadium_c1_heap_owner_observer.h"
#include "dat_archive.hpp"
#include "dat_color_animation.hpp"
#include "dat_effect_banks.hpp"
#include "dat_item_article.hpp"
#include "dat_lights.hpp"
#include "dat_item_registry.hpp"
#include "dat_item_registry_native.hpp"
#include "dat_native_stage.hpp"
#include "dat_native_joint.hpp"
#include "dat_scene.hpp"
#include "dat_sis.hpp"
#include "dat_stage.hpp"
#include "dat_stage_items.hpp"
#include "dat_stage_yaku.hpp"
#include "gameplay_effect_banks.h"
#include "gameplay_effect_runtime.h"
#include "gameplay_vs_sis.h"
#include "gameplay_ground_data.h"
#include "gameplay_item_runtime.h"
#include "gameplay_stage_last.h"
#include "gameplay_stage_map.h"
#include "native_dat.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include "stadium_screen_roots_probe.h"
#include "stadium_c1_e8_call_observer.h"
#include "stadium_c1_item_owner_negative_cases.hpp"
#include "stadium_c1_item_owner_preflight.hpp"
#include "stadium_live_image_consumer.hpp"
#include "stadium_buffer_consumer.hpp"
#include "stadium_ground_owner_contract.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include <limits>
#include <optional>
#include <cstdlib>
#endif
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
extern "C" {
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gm_1A45.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/gm_1B03.h>
#include <melee/gm/gmvsmode.h>
#include <melee/gm/gmvsmelee.h>
#include <melee/gm/gmresultplayer.h>
#include <sysdolphin/baselib/forward.h>
// Exact pinned player.h ABI without its C-only fighter type definitions.
HSD_GObj* Player_GetEntity(int slot);
int melee_web_css_observe_setup(int cursors[4][4],int doors[4][10],float geometry[4][12]);
void melee_web_css_observe_setup_diagnostic_once(
    void (*)(int,int,int,const void*,const void*,const void*,const void*,const void*,unsigned));
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <sysdolphin/baselib/controller.h>
extern ResultsData lbl_8046DBE8;
#include <melee/lb/lbfile.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnmain.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include <melee/gr/grdatfiles.h>
#include <melee/gr/stage.h>
#include <melee/mp/mpisland.h>
#include <melee/ef/eflib.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/memory.h>
void HSD_SisLib_C1TextProbeSet(int enabled);
#include <dolphin/os/OSAlloc.h>
#endif
#include <melee/ty/forward.h>
#include <melee/ty/toy.h>
#include <melee/ty/types.h>
extern HSD_Archive* _Toy_sbss_804D6ED0;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_ObjAllocData gobjproc_alloc_data;
#endif
}
#include <melee/gr/forward.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#endif
extern "C" {
#include <sysdolphin/baselib/random.h>
#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/rumble.h>
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
}
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <set>
#include <stdexcept>
#include <exception>
#include <string_view>
#include <vector>
extern "C" GameRules gmMainLib_803D4A48;
extern "C" int melee_web_vs_prepare_start_source(StartMeleeData*,const VsModeData*,VsModeData*);
extern "C" int melee_web_match_validate_source_start(StartMeleeData*,int,int);
extern "C" int melee_web_match_source_scene_supported(int,int,const void*,const void*,int,const StartMeleeRules*);
extern "C" int melee_web_match_prepare_source(StartMeleeData*,int);
extern "C" int melee_web_vs_mode_begin(void);
extern "C" int melee_web_vs_mode_end(void);
extern "C" int melee_web_vs_mode_select_state(int);
extern "C" int melee_web_vs_mode_resolve_next_state(GameModeState*);
extern "C" int melee_web_vs_mode_set_route(int current_mode, int previous_mode);
extern "C" void* melee_web_current_scene_info(void);
extern "C" void* melee_web_grpstadium_exchange_yakumono(void* value);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern "C" int melee_web_stage_selection_begin(int stage_kind);
extern "C" int melee_web_stage_selection_end(void);
#endif
static void check(int value,const char* error){if(!value){std::cerr<<"Check failed before teardown: "<<error<<"\n";throw std::runtime_error(error);}}

namespace {
void emit_native_bytes(const char* record,const void* data,std::size_t size)
{
    const auto* bytes=static_cast<const unsigned char*>(data);
    std::cout<<"{\"record\":\""<<record<<"\",\"native_abi_bytes\":[";
    for(std::size_t i=0;i<size;++i){if(i)std::cout<<',';std::cout<<unsigned(bytes[i]);}
    std::cout<<"]}\n"<<std::flush;
}
void run_post_vs_mode_asset_free_contract()
{
    const GameRules saved_rules=*gmMainLib_GetGameRules();
    const auto saved_preferences=*gmMainLib_8015CC58();
    try{
        gmMainLib_GetGameRules()->mode=1;
        gmMainLib_GetGameRules()->stock_count=4;
        gmMainLib_GetGameRules()->stock_time_limit=0;
        gmMainLib_8015CC58()->item_freq=-1;
        VsModeData raw{};gm_InitVsMode(&raw);raw.start.rules.stkind=St_Kind_Last;
        for(unsigned i=0;i<2;++i){
            raw.start.players[i].slot_type=Gm_PKind_Cpu;
            raw.start.players[i].ckind=CKIND_MARIO;
            raw.start.players[i].slot=i+1;
            raw.start.players[i].color=i;
            raw.start.players[i].cpu_kind=4;
            raw.start.players[i].cpu_level=9;
        }
        const VsModeData untouched=raw;
        VsModeData oracle=raw;gm_80167BC8(&oracle);
        StartMeleeData expected{};expected.rules=oracle.start.rules;
        expected.rules.is_stock=true;expected.rules.is_vs=true;
        for(unsigned i=0;i<GM_MAX_PLAYERS;++i)expected.players[i]=oracle.start.players[i];
        gm_SetupSubColors(&expected);gm_LoadRumbleEnabled(&expected);
        StartMeleeData output{};VsModeData retained{};
        check(melee_web_vs_prepare_start_source(&output,&raw,&retained),
              "VS preparation did not return its exact persistent mode state");
        check(std::memcmp(&raw,&untouched,sizeof(raw))==0&&
              std::memcmp(&retained,&oracle,sizeof(retained))==0&&
              std::memcmp(&output,&expected,sizeof(output))==0&&
              !retained.start.rules.is_stock&&!retained.start.rules.is_vs&&
              output.rules.is_stock&&output.rules.is_vs&&
              retained.start.rules.xB==-1&&retained.start.players[0].stocks==4,
              "VS preparation lost raw, persistent mode or normalized Start separation");
        CSSData fresh{};fresh.match_type=VS_MELEE;fresh.vs=raw;
        CSSData cached=fresh;cached.vs=retained;
        check(melee_web_menu_css_cache_selection_valid(&fresh,nullptr)&&
              melee_web_menu_css_cache_selection_valid(&cached,&retained.start.rules)&&
              !melee_web_menu_css_selection_valid(&cached),
              "Fresh or checked retained CSS cache contract failed; foreign cache admitted");
        gmMainLib_GetGameRules()->stock_time_limit=1;
        VsModeData timed=raw;gm_80167BC8(&timed);
        cached.vs=timed;
        check(melee_web_menu_css_cache_selection_valid(&cached,&timed.start.rules),
              "Checked one-minute post-VS cache was rejected");
        const CSSData unchanged=cached;
        for(unsigned negative=0;negative<6;++negative){
            CSSData bad=cached;
            if(negative==0)bad.vs.start.rules.is_vs=true;
            if(negative==1)bad.vs.start.rules.is_stock=true;
            if(negative==2)bad.vs.start.rules.time_limit=61;
            if(negative==3)bad.vs.start.players[0].stocks=0;
            if(negative==4)bad.vs.start.players[0].stocks=6;
            if(negative==5)bad.vs.start.players[1].slot=1;
            check(!melee_web_menu_css_cache_selection_valid(&bad,&timed.start.rules),
                  "Retained CSS cache accepted malformed flags/timer/stocks/ports");
        }
        auto bad_expected=timed.start.rules;bad_expected.time_limit=61;
        check(!melee_web_menu_css_cache_selection_valid(&cached,&bad_expected)&&
              std::memcmp(&cached,&unchanged,sizeof(cached))==0,
              "Cache validation changed source bytes or accepted invalid timer expectation");
        SSSData fresh_sss{};fresh_sss.force_stage_id=-1;fresh_sss.vs=raw;
        SSSData foreign_sss=fresh_sss;foreign_sss.vs=timed;
        check(melee_web_menu_sss_selection_valid(&fresh_sss)&&
              !melee_web_menu_sss_selection_valid(&foreign_sss),
              "Fresh SSS changed or foreign retained SSS bypassed owner binding");
        CSSData training=fresh;training.match_type=TRAINING_MODE;
        training.vs.start.players[0].slot_type=Gm_PKind_Human;
        check(melee_web_menu_css_selection_valid(&training),
              "Training CSS contract changed with returned VS cache support");
        training.vs.start.rules.timer_enabled=true;
        check(!melee_web_menu_css_selection_valid(&training),
              "Training CSS admitted an ordinary VS timer cache");
        gmMainLib_GetGameRules()->stock_time_limit=0;
        const auto retained_before=retained;const auto output_before=output;
        check(!melee_web_vs_prepare_start_source(&output,&raw,nullptr)&&
              std::memcmp(&retained,&retained_before,sizeof(retained))==0&&
              std::memcmp(&output,&output_before,sizeof(output))==0,
              "Missing retained VS output changed preparation state");
        check(melee_web_match_validate_source_start(&output,0,0),
              "Ordinary VS contract rejected its unchanged normalized Start");
        StartMeleeData ordinary_without_flags=output;
        ordinary_without_flags.rules.is_stock=false;ordinary_without_flags.rules.is_vs=false;
        check(!melee_web_match_validate_source_start(&ordinary_without_flags,0,0)&&
              !melee_web_match_prepare_source(&ordinary_without_flags,0),
              "Ordinary VS accepted missing stock/VS source flags");
        MatchEnd tie{};tie.match_kind=output.rules.match_kind;tie.outcome=OUTCOME_TIMEOUT;
        tie.n_winners=2;tie.winners[0]=0;tie.winners[1]=1;
        for(auto& player:tie.player_standings)player.slot_type=Gm_PKind_NA;
        for(unsigned i=0;i<2;++i){tie.player_standings[i].slot_type=Gm_PKind_Cpu;
            tie.player_standings[i].ckind=CKIND_MARIO;tie.player_standings[i].stocks=1;}
        StartMeleeData sd{};sd.rules=retained.start.rules;
        for(unsigned i=0;i<GM_MAX_PLAYERS;++i)sd.players[i]=retained.start.players[i];
        gm_SetupSubColors(&sd);gm_LoadRumbleEnabled(&sd);gm_SetupSuddenDeath(&sd,&tie);
        sd.rules.x6=true; // GS_SUDDEN_DEATH source scene entry adapter boundary.
        const auto sd_before=sd;
        check(melee_web_match_validate_source_start(&sd,0,1)&&
              std::memcmp(&sd,&sd_before,sizeof(sd))==0&&
              !sd.rules.is_stock&&!sd.rules.is_vs&&
              !melee_web_match_validate_source_start(&sd,0,0)&&
              !melee_web_match_prepare_source(&sd,0),
              "Explicit SD contract changed source flags or bypassed ordinary VS");
        check(melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&gmVsMelee_SuddenDeathExitInfo,1,&sd.rules)&&
              melee_web_match_source_scene_supported(GM_VS,GS_VS,
                  &gmVsMelee_StartData,&gmVsMelee_VsExitInfo,0,&output.rules),
              "Source frame admission rejected checked SD or unchanged ordinary VS");
        StartMeleeRules no_vs_flag=output.rules;no_vs_flag.is_vs=false;
        check(!melee_web_match_source_scene_supported(GM_VS,GS_VS,
                  &gmVsMelee_StartData,&gmVsMelee_VsExitInfo,0,&no_vs_flag)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&gmVsMelee_SuddenDeathExitInfo,1,&output.rules)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_VS,
                  &gmVsMelee_StartData,&gmVsMelee_VsExitInfo,1,&sd.rules),
              "Source frame admission used ordinary flags or bare x6 to bypass scene identity");
        check(!melee_web_match_source_scene_supported(GM_MENU,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&gmVsMelee_SuddenDeathExitInfo,1,&sd.rules)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &sd,&gmVsMelee_SuddenDeathExitInfo,1,&sd.rules)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&tie,1,&sd.rules)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&gmVsMelee_SuddenDeathExitInfo,0,&sd.rules)&&
              !melee_web_match_source_scene_supported(GM_VS,GS_SUDDEN_DEATH,
                  &gmVsMelee_StartData,&gmVsMelee_SuddenDeathExitInfo,1,nullptr)&&
              std::memcmp(&sd,&sd_before,sizeof(sd))==0,
              "Source frame admission accepted foreign mode/payload/missing source x6 or changed rules");
        for(unsigned negative=0;negative<4;++negative){
            StartMeleeData invalid=sd;
            if(negative==0)invalid.rules.x6=false;
            if(negative==1)invalid.rules.timer_enabled=true;
            if(negative==2)invalid.players[0].stocks=2;
            if(negative==3)invalid.players[0].x12=0;
            check(!melee_web_match_validate_source_start(&invalid,0,1),
                  "Explicit SD contract accepted wrong scene/timer/stock/damage");
        }
    }catch(...){*gmMainLib_GetGameRules()=saved_rules;
        *gmMainLib_8015CC58()=saved_preferences;throw;}
    *gmMainLib_GetGameRules()=saved_rules;*gmMainLib_8015CC58()=saved_preferences;
    std::cout<<"Asset-free post-VS mode retention and explicit SD/ordinary-VS validation contracts passed\n";
}

// Test-only declared Results boundary, independent of the observed result.
struct TwoHumanResultsExpectation {
    MatchOutcome outcome;
    int p1_stocks;
    int p2_stocks;
};
constexpr TwoHumanResultsExpectation kEliminationResults{OUTCOME_ELIMINATION,0,4};
constexpr TwoHumanResultsExpectation kTimeoutResults{OUTCOME_TIMEOUT,3,4};
bool declared_two_human_results_valid(const MatchEnd& end,
                                     const TwoHumanResultsExpectation& expected)
{
    if (!((expected.outcome==OUTCOME_ELIMINATION&&expected.p1_stocks==0&&expected.p2_stocks==4)||
          (expected.outcome==OUTCOME_TIMEOUT&&expected.p1_stocks==3&&expected.p2_stocks==4)) ||
        end.outcome!=expected.outcome || end.match_kind!=MatchKind_Stock ||
        end.n_winners!=1 || end.winners[0]!=1) return false;
    for(unsigned slot=0;slot<GM_MAX_PLAYERS;++slot){
        const auto& player=end.player_standings[slot];
        if(slot>=2){if(player.slot_type!=Gm_PKind_NA)return false;continue;}
        if(player.slot_type!=Gm_PKind_Human||player.ckind!=CKIND_MARIO||
           player.stocks!=(slot==0?expected.p1_stocks:expected.p2_stocks))return false;
    }
    return true;
}
void run_declared_results_asset_free_contract()
{
    for(const auto* expected:{&kEliminationResults,&kTimeoutResults}){
        MatchEnd end{};end.outcome=expected->outcome;end.match_kind=MatchKind_Stock;
        end.n_winners=1;end.winners[0]=1;
        for(auto& player:end.player_standings)player.slot_type=Gm_PKind_NA;
        for(unsigned slot=0;slot<2;++slot){auto& player=end.player_standings[slot];
            player.slot_type=Gm_PKind_Human;player.ckind=CKIND_MARIO;
            player.stocks=slot==0?expected->p1_stocks:expected->p2_stocks;}
        check(declared_two_human_results_valid(end,*expected),"Declared Results boundary rejected its exact fixture");
        for(unsigned negative=0;negative<8;++negative){auto invalid=end;
            if(negative==0)invalid.winners[0]=0;
            if(negative==1)invalid.n_winners=2;
            if(negative==2)invalid.player_standings[1].slot_type=Gm_PKind_Cpu;
            if(negative==3)invalid.player_standings[2].slot_type=Gm_PKind_Human;
            if(negative==4)invalid.outcome=OUTCOME_NO_CONTEST;
            if(negative==5)++invalid.player_standings[0].stocks;
            if(negative==6)invalid.player_standings[1].ckind=CKIND_FOX;
            if(negative==7)invalid.match_kind=MatchKind_Time;
            check(!declared_two_human_results_valid(invalid,*expected),"Declared Results boundary accepted malformed outcome/player/winner/stocks");
        }
        check(!declared_two_human_results_valid(end,expected==&kTimeoutResults?kEliminationResults:kTimeoutResults),
              "Declared Results boundary accepted a different caller expectation");
    }
    std::cout<<"Asset-free caller-bound two-Human Results admission controls passed\n";
}

void run_vs_sudden_death_source_control()
{
    char error[256]{};
    check(melee_web_gameplay_session_begin(32U * 1024U * 1024U,
                                           error, sizeof(error)), error);
    bool mode_owned = false;
    bool session_active = true;
    try {
        run_post_vs_mode_asset_free_contract();
        run_declared_results_asset_free_contract();
        check(melee_web_vs_mode_begin(),
              "Original VS source-mode lease did not begin");
        mode_owned = true;
        check(melee_web_vs_mode_set_route(GM_VS, GM_MENU),
              "Original VS source route was not selected");

        VsModeData* const vs = gmVsMelee_GetVsData();
        std::memset(vs, 0, sizeof(*vs));
        vs->start.rules.match_kind = MatchKind_Stock;
        vs->start.rules.is_stock = true;
        vs->start.rules.is_vs = true;
        vs->start.rules.timer_enabled = true;
        vs->start.rules.time_limit = 60;
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
            vs->start.players[i].slot_type = Gm_PKind_NA;
        vs->start.players[0].slot_type = Gm_PKind_Cpu;
        vs->start.players[0].ckind = CKIND_MARIO;
        vs->start.players[0].slot = 1;
        vs->start.players[0].stocks = 4;
        vs->start.players[0].color = 2;
        vs->start.players[0].sub_color = 1;
        vs->start.players[1].slot_type = Gm_PKind_Cpu;
        vs->start.players[1].ckind = CKIND_FOX;
        vs->start.players[1].slot = 2;
        vs->start.players[1].stocks = 4;
        vs->start.players[1].color = 1;
        vs->start.players[1].sub_color = 2;

        const auto route_vs_result = [](MatchOutcome outcome,
                                        unsigned winner_count) {
            MatchExitInfo& exit = gmVsMelee_VsExitInfo;
            std::memset(&exit, 0, sizeof(exit));
            exit.match_end.outcome = outcome;
            exit.match_end.match_kind = MatchKind_Stock;
            exit.match_end.n_winners = static_cast<u8>(winner_count);
            for (auto& player : exit.match_end.player_standings)
                player.slot_type = Gm_PKind_NA;
            for (unsigned i = 0; i < 2; ++i) {
                auto& standing = exit.match_end.player_standings[i];
                standing.slot_type = Gm_PKind_Cpu;
                standing.ckind = i == 0 ? CKIND_MARIO : CKIND_FOX;
                standing.stocks = winner_count == 2 || i == 0 ? 1 : 0;
                standing.is_big_loser = winner_count != 2 && i != 0;
            }
            check(melee_web_vs_mode_select_state(gmVsMode_State_Vs),
                  "Original VS state could not be selected");
            gm_Mode_Vs_States[gmVsMode_State_Vs].on_exit(
                &gm_Mode_Vs_States[gmVsMode_State_Vs]);
            return melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States);
        };
        check(route_vs_result(OUTCOME_TIMEOUT, 1) == gmVsMode_State_Results,
              "Original single-winner timeout did not select Results");
        check(route_vs_result(OUTCOME_ELIMINATION, 1) == gmVsMode_State_Results,
              "Original single-winner elimination did not select Results");
        check(route_vs_result(OUTCOME_TIMEOUT, 2) ==
                  gmVsMode_State_SuddenDeath,
              "Original tied timeout did not select Sudden Death");

        const StartMeleeData before_sudden_death = vs->start;
        StartMeleeData expected_start{};
        expected_start.rules = before_sudden_death.rules;
        for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
            expected_start.players[i] = before_sudden_death.players[i];
        gm_SetupSubColors(&expected_start);
        gm_LoadRumbleEnabled(&expected_start);
        gm_SetupSuddenDeath(&expected_start,
                            &gmVsMelee_VsExitInfo.match_end);

        StartMeleeData excluded_start{};
        excluded_start.players[0].slot_type = Gm_PKind_Cpu;
        excluded_start.players[1].slot_type = Gm_PKind_Cpu;
        MatchEnd excluded_end{};
        excluded_end.match_kind = MatchKind_Stock;
        excluded_end.outcome = OUTCOME_TIMEOUT;
        excluded_end.player_standings[0].slot_type = Gm_PKind_Cpu;
        excluded_end.player_standings[0].ckind = CKIND_MARIO;
        excluded_end.player_standings[0].stocks = 0;
        excluded_end.player_standings[0].is_big_loser = false;
        excluded_end.player_standings[1].slot_type = Gm_PKind_Cpu;
        excluded_end.player_standings[1].ckind = CKIND_FOX;
        excluded_end.player_standings[1].stocks = 1;
        excluded_end.player_standings[1].is_big_loser = true;
        gm_SetupSuddenDeath(&excluded_start, &excluded_end);
        check(excluded_start.players[0].slot_type == Gm_PKind_NA &&
                  excluded_start.players[1].slot_type == Gm_PKind_NA,
              "Original Sudden Death helper retained zero-stock or big-loser players");

        check(melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath),
              "Original Sudden Death state could not be selected");
        gm_Mode_Vs_States[gmVsMode_State_SuddenDeath].on_enter(
            &gm_Mode_Vs_States[gmVsMode_State_SuddenDeath]);
        check(std::memcmp(&gmVsMelee_StartData, &expected_start,
                          sizeof(expected_start)) == 0,
              "Original Sudden Death callback changed source-generated StartMeleeData");
        check(gmVsMelee_StartData.rules.match_kind == MatchKind_Stock &&
                  !gmVsMelee_StartData.rules.timer_enabled &&
                  gmVsMelee_StartData.players[0].slot_type == Gm_PKind_Cpu &&
                  gmVsMelee_StartData.players[1].slot_type == Gm_PKind_Cpu &&
                  gmVsMelee_StartData.players[0].stocks == 1 &&
                  gmVsMelee_StartData.players[1].stocks == 1,
              "Original Sudden Death callback did not produce its source rules and players");
        check(!melee_web_vs_mode_begin(),
              "Sudden Death did not retain its original VS mode owner");

        MatchExitInfo sudden_death_exit{};
        sudden_death_exit.x0 = 0x13579;
        sudden_death_exit.x4 = 0x2468;
        sudden_death_exit.x8 = 0x11223344;
        MatchEnd& end = sudden_death_exit.match_end;
        end.match_kind = MatchKind_Stock;
        end.outcome = OUTCOME_ELIMINATION;
        end.frame_count = 9876;
        end.n_winners = 1;
        end.winners[0] = 1;
        for (auto& player : end.player_standings)
            player.slot_type = Gm_PKind_NA;
        end.player_standings[1].slot_type = Gm_PKind_Cpu;
        end.player_standings[1].ckind = CKIND_FOX;
        end.player_standings[1].stocks = 1;
        gmVsMelee_SuddenDeathExitInfo = sudden_death_exit;

        MatchEnd expected_result = gmVsMelee_VsExitInfo.match_end;
        MatchEnd sudden_death_match_end = end;
        gm_80166CCC(&expected_result, &sudden_death_match_end);
        check(melee_web_vs_mode_select_state(gmVsMode_State_SuddenDeath),
              "Original Sudden Death state could not be reselected for exit");
        gm_Mode_Vs_States[gmVsMode_State_SuddenDeath].on_exit(
            &gm_Mode_Vs_States[gmVsMode_State_SuddenDeath]);
        check(melee_web_vs_mode_resolve_next_state(gm_Mode_Vs_States) ==
                  gmVsMode_State_Results,
              "Original Sudden Death state table did not select Results");
        check(std::memcmp(&gmVsMelee_VsExitInfo.match_end, &expected_result,
                          sizeof(expected_result)) == 0,
              "Original Sudden Death exit did not preserve its authored MatchEnd merge");
        check(gmVsMelee_VsExitInfo.x0 == 0 &&
                  gmVsMelee_VsExitInfo.x4 == 0 &&
                  gmVsMelee_VsExitInfo.x8 == 0,
              "Sudden Death source callback changed non-MatchEnd VS exit fields");

        check(melee_web_vs_mode_select_state(gmVsMode_State_Results),
              "Original Results state could not be selected after Sudden Death");
        gm_Mode_Vs_States[gmVsMode_State_Results].on_enter(
            &gm_Mode_Vs_States[gmVsMode_State_Results]);
        check(std::memcmp(&gmVsMelee_ResultsEnterData.match_end,
                          &expected_result, sizeof(expected_result)) == 0,
              "Original Results callback changed the Sudden Death MatchEnd payload");
        check(melee_web_vs_mode_end(),
              "Original VS source-mode lease did not retire after Results");
        mode_owned = false;
        check(melee_web_vs_mode_begin(),
              "Original VS source-mode lease could not be reacquired after retirement");
        check(melee_web_vs_mode_end(),
              "Reacquired Original VS source-mode lease did not retire");

        const GameRules caller_rules=*gmMainLib_GetGameRules();
        auto* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        MeleeWebMenuMatchContinuation rejected_continuation{};
        MatchExitInfo rejected_exit{};
        check(melee_web_menu_host_post_vs_mode(host)==nullptr&&
              melee_web_menu_host_post_vs_mode(nullptr)==nullptr,
              "Fresh or missing host exposed a prepared VS route");
        check(!melee_web_menu_host_match_continuation_begin(
                  host, &rejected_exit, 1, &rejected_continuation,
                  error, sizeof(error)),
              "Typed host accepted a match continuation before source menus closed");
        check(rejected_continuation.kind == 0,
              "Rejected typed continuation retained a stale payload");
        rejected_continuation.kind = MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS;
        check(!melee_web_menu_host_sudden_death_finish(
                  host,0,&rejected_exit,2,nullptr,
                  &rejected_continuation, error, sizeof(error)) &&
                  rejected_continuation.kind == 0,
              "Typed host accepted a Sudden Death finish without its live route");
        check(melee_web_vs_mode_begin(),
              "Rejected typed continuation leaked its original VS owner");
        check(melee_web_vs_mode_end(),
              "Typed continuation ownership control could not retire the VS lease");
        const GameRules before_fixture=*gmMainLib_GetGameRules();
        GameRules fixture=gmMainLib_803D4A48;fixture.mode=1;fixture.stock_count=4;
        GameRules invalid=fixture;invalid.stock_time_limit=100;
        check(!melee_web_menu_host_apply_initial_native_rules(nullptr,&fixture,error,sizeof(error))&&
              !melee_web_menu_host_apply_initial_native_rules(host,nullptr,error,sizeof(error))&&
              !melee_web_menu_host_apply_initial_native_rules(host,&invalid,error,sizeof(error)),
              "Initial native rules accepted missing owner/payload or unsupported timer");
        invalid=fixture;invalid.stock_count=3;
        check(!melee_web_menu_host_apply_initial_native_rules(host,&invalid,error,sizeof(error)),
              "Initial native rules weakened supported four-stock admission");
        invalid=fixture;invalid.damage_ratio^=1;
        check(!melee_web_menu_host_apply_initial_native_rules(host,&invalid,error,sizeof(error)),
              "Initial native rules admitted a foreign canonical field");
        fixture.stock_time_limit=99;
        check(melee_web_menu_host_apply_initial_native_rules(host,&fixture,error,sizeof(error))&&
              !melee_web_menu_host_apply_initial_native_rules(host,&fixture,error,sizeof(error))&&
              !melee_web_menu_host_apply_net_context(host,1,error,sizeof(error))&&
              std::memcmp(gmMainLib_GetGameRules(),&before_fixture,sizeof(before_fixture))==0,
              "Initial native fixture changed resident rules early or admitted repeated/net ownership");
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        check(std::memcmp(gmMainLib_GetGameRules(),&caller_rules,sizeof(caller_rules))==0,
              "Unentered fixture destruction changed resident GameRules");

        MeleeWebMenuMatchContinuation unowned_sudden_death{};
        unowned_sudden_death.kind=MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH;
        unowned_sudden_death.owner_id=1;
        MeleeWebMenuMatchSelection refused_selection{};
        check(!melee_web_menu_host_sudden_death_selection(
                  nullptr,&unowned_sudden_death,&refused_selection,
                  error,sizeof(error))&&refused_selection.player_count==0,
              "Sudden Death host accepted a continuation without its live owner");
        MeleeWebMenuMatchSelection bypass_selection{};
        bypass_selection.sudden_death=1;
        const melee_web::RuntimeFiles empty_files{};
        bool rejected_bypass=false;
        try{melee_web::GameplayMatchSession invalid(empty_files,bypass_selection);}
        catch(const std::exception& exception){
            rejected_bypass=std::string_view(exception.what())==
                "Sudden Death source payload requires a checked host-owned match claim";
        }
        check(rejected_bypass,
              "Native Sudden Death session accepted a source payload without its host claim");

        check(melee_web_gameplay_session_end(error, sizeof(error)), error);
        session_active = false;
    } catch (...) {
        if (mode_owned)
            (void)melee_web_vs_mode_end();
        if (session_active)
            (void)melee_web_gameplay_session_end(error, sizeof(error));
        throw;
    }
    std::cout << "Original VS timeout/tie and Sudden Death-to-Results source callbacks passed; "
                 "asset-free callback control only, no gameplay claim\n";
}

void run_typed_results_source_smoke(const melee_web::RuntimeFiles&,MeleeWebMenuHost*,
    const ResultsMatchInfo&,std::uint32_t&,std::uint8_t[MELEE_WEB_PAD_STATE_BYTES],
    const MeleeWebPadState* initial_input=nullptr,const TwoHumanResultsExpectation* expected=nullptr);
void run_results_source_smoke(const melee_web::RuntimeFiles&,MeleeWebMenuHost*,
    const MatchExitInfo&,std::uint32_t&,std::uint8_t[MELEE_WEB_PAD_STATE_BYTES],
    const TwoHumanResultsExpectation* expected=nullptr);
void run_returned_menu_selection(const melee_web::RuntimeFiles& files,
    MeleeWebMenuHost* host,char* error,std::size_t error_size,
    const StartMeleeRules* expected_cache=nullptr)
{
    float pcm[1068];unsigned audio_phase=0;
    auto check_cache=[&](int expected_scene){
        if(!expected_cache)return;
        StartMeleeData observed{};uint8_t header[6]{};
        check(melee_web_menu_host_selection_state(host,&observed,header)==expected_scene&&
              std::memcmp(&observed.rules,expected_cache,sizeof(observed.rules))==0,
              "Returned menu cache differs from the checked prior post-VS rules");
        emit_native_bytes(expected_scene==1?"ordinary_returned_css_rules":"ordinary_returned_sss_rules",
                          &observed.rules,sizeof(observed.rules));
        CSSData copy{};copy.match_type=VS_MELEE;copy.vs.start=observed;
        auto wrong=*expected_cache;wrong.timer_enabled=!wrong.timer_enabled;
        check(melee_web_menu_css_cache_selection_valid(&copy,expected_cache)&&
              !melee_web_menu_css_cache_selection_valid(&copy,&wrong)&&
              !melee_web_menu_css_selection_valid(&copy),
              "Retained cache mismatch or foreign-copy guard failed");
    };
    auto css=std::make_unique<melee_web::GameplayMenuWorld>(files);
    try {
        check(melee_web_menu_host_enter(host,css->audio(),error,error_size),error);
        auto menu_tick=[&](unsigned button){
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            pads[0].button=pads[1].button=button;
            const int result=melee_web_menu_host_tick(host,pads,error,error_size);
            check(result>0,error);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(css->audio(),pcm,count,error,error_size),error);
            return result;
        };
        for(unsigned tick=0;tick<120;++tick)
            check(menu_tick(0)==1,"Unexpected returned CSS transition");
        check(melee_web_menu_host_phase(host)==1,"Results did not return to original CSS");
        check_cache(1);
        int transition=1;
        for(unsigned tick=0;tick<600&&transition==1;++tick)
            transition=menu_tick(tick%90==0?PAD_BUTTON_START:0);
        check(transition==3,"Returned CSS did not request its original SSS transition");
        check(melee_web_menu_host_leave(host,0,error,error_size),error);
        css->verify_immutable_archives();
        auto* retained_audio=css->audio();
        const auto retained_generation=melee_web_audio_generation(retained_audio);
        uint32_t completed=0,revisited=0,after_completed=0,after_revisited=0;
        check(retained_generation&&melee_web_audio_stream_progress(retained_audio,&completed,&revisited),
              "Returned CSS audio owner/progress unavailable before SSS rebuild");
        css->rebuild_scene(melee_web::GameplayMenuScene::Stages);
        check(css->audio()==retained_audio&&melee_web_audio_generation(css->audio())==retained_generation&&
              melee_web_audio_stream_progress(css->audio(),&after_completed,&after_revisited)&&
              after_completed==completed&&after_revisited==revisited,
              "Returned SSS rebuild changed original menu audio owner or stream progress");
        std::cout<<"Returned CSS transition and explicit Stages hydration completed\n";
        check(melee_web_menu_host_enter(host,css->audio(),error,error_size),error);
        for(unsigned tick=0;tick<120;++tick)
            check(menu_tick(0)==1,"Unexpected returned SSS transition");
        check_cache(2);
        PADStatus stage_pads[4]{};stage_pads[2].err=stage_pads[3].err=-1;
        bool at_target=false;
        for(unsigned tick=0;tick<120;++tick){
            MeleeWebStageInputObservation observed{};
            check(melee_web_stage_input_observe(MELEE_WEB_MENU_FD_ST_KIND,&observed),
                  "Returned SSS cursor observation unavailable");
            const int state=melee_web_stage_input_drive(stage_pads,&observed,MELEE_WEB_MENU_FD_ST_KIND);
            check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"Returned SSS FD target invalid");
            if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET){at_target=true;break;}
            check(melee_web_menu_host_tick(host,stage_pads,error,error_size)==1,error);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(css->audio(),pcm,count,error,error_size),error);
        }
        check(at_target,"Returned SSS cursor did not reach original FD tile");
        transition=1;
        for(unsigned tick=0;tick<600&&transition==1;++tick)
            transition=menu_tick(tick%90==0?PAD_BUTTON_START:0);
        check(transition==3,"Returned SSS did not commit its source selection");
        check(melee_web_menu_host_leave(host,0,error,error_size),error);
        MeleeWebMenuMatchSelection next{};
        check(melee_web_menu_host_selection(host,&next,error,error_size),error);
        check(next.player_count==2&&next.start.rules.stkind==MELEE_WEB_MENU_FD_ST_KIND&&
              next.start.rules.is_stock&&next.start.rules.is_vs&&
              next.start.players[0].stocks==4&&next.start.players[1].stocks==4,
              "Returned CSS/SSS lost its supported normalized rematch selection");
        emit_native_bytes("returned_sss_normalized_start",&next.start,sizeof(next.start));
        css->verify_immutable_archives();css->close();css.reset();
    } catch (...) {
        const auto primary=std::current_exception();
        if(css){
            if(!melee_web_menu_host_leave(host,1,error,error_size))
                std::cerr<<"Secondary owned menu abort failure: "<<error<<'\n';
            try{css->close();css.reset();}
            catch(const std::exception& cleanup){
                std::cerr<<"Secondary owned menu world retirement failure: "<<cleanup.what()<<'\n';
            }
        }
        std::rethrow_exception(primary);
    }
    std::cout<<"Returned original CSS/SSS supported selection and owned menu close completed\n";
}

std::vector<std::string> sd_resolution_asset_names(const MeleeWebMenuMatchSelection& selection, bool include_match=true)
{
    auto names=melee_web::menu_asset_names();
    for(const auto& group:{include_match?melee_web::match_asset_names(selection):std::vector<std::string>{},
                          melee_web::results_asset_names(selection),melee_web::prize_asset_names()})
        for(const auto& name:group)
            if(std::find(names.begin(),names.end(),name)==names.end())names.push_back(name);
    return names;
}
void emit_sd_resolution_fixture_manifest(bool include_match=true)
{
    // Descriptor only: observed two-Mario/FD identity, never a gameplay setup.
    MeleeWebMenuMatchSelection selection{};selection.player_count=2;
    selection.start.rules.stkind=St_Kind_Last;
    for(auto& p:selection.start.players)p.slot_type=Gm_PKind_NA;
    for(unsigned i=0;i<2;++i){
        auto& p=selection.start.players[i];p.slot_type=Gm_PKind_Human;p.ckind=CKIND_MARIO;
        p.stocks=4;p.color=1-i;
        selection.players[i].controller=i;selection.players[i].stocks=4;
        selection.players[i].costume=p.color;
    }
    auto invalid=selection;invalid.players[1].controller=0;
    bool rejected=false;try{sd_resolution_asset_names(invalid,include_match);}catch(const std::exception&){rejected=true;}
    check(rejected,"SD resolution descriptor accepted a duplicate source controller");
    invalid=selection;invalid.start.players[1].color=255;
    rejected=false;try{sd_resolution_asset_names(invalid,include_match);}catch(const std::exception&){rejected=true;}
    check(rejected,"SD resolution descriptor accepted an unauthored costume");
    const auto names=sd_resolution_asset_names(selection,include_match);
    std::cout<<"{\"record\":\"sd_resolution_fixture_manifest\",\"required\":[";
    for(unsigned i=0;i<names.size();++i){if(i)std::cout<<',';std::cout<<'"'<<names[i]<<'"';}
    std::cout<<"]}\n";
}
static unsigned css_observer_failure_count;
static void css_observer_failure_diagnostic(int guard,int port,int joint,
    const void* data,const void* root,const void* cursor,const void* model,
    const void* slider,unsigned flags)
{
    ++css_observer_failure_count;
    std::cout<<"{\"record\":\"css_observer_first_failure\",\"guard\":"<<guard
        <<",\"port\":"<<port<<",\"slider_joint\":"<<joint
        <<",\"data\":"<<reinterpret_cast<uintptr_t>(data)
        <<",\"root\":"<<reinterpret_cast<uintptr_t>(root)
        <<",\"cursor\":"<<reinterpret_cast<uintptr_t>(cursor)
        <<",\"model\":"<<reinterpret_cast<uintptr_t>(model)
        <<",\"slider\":"<<reinterpret_cast<uintptr_t>(slider)
        <<",\"flags\":"<<flags<<"}\n";std::cout.flush();
}

// Actual source door transitions only. No menu payload mutation.
template<class Tick>
void drive_sparse_source_css(PADStatus (&raw)[4],Tick&& tick)
{
    int cursors[4][4]{},doors[4][10]{};float geometry[4][12]{};
    auto neutral=[&](){
        for(auto& pad:raw)pad={};
        raw[1].err=raw[3].err=-1; // Original ports0/2 remain connected.
    };
    auto observe=[&](const char* label){
        check(melee_web_css_observe_setup(cursors,doors,geometry),
              "Actual sparse CSS four-door source observation is unavailable");
        emit_native_bytes(label,doors,sizeof(doors));
        emit_native_bytes("actual_sparse_css_cursors",cursors,sizeof(cursors));
        emit_native_bytes("actual_sparse_css_geometry",geometry,sizeof(geometry));
        std::cout.flush();
    };
    auto step=[&](){check(tick()==1,"Sparse CSS input unexpectedly transitioned");};
    auto tap=[&](unsigned port){
        neutral();raw[port].button=PAD_BUTTON_A;step();neutral();step();
    };
    auto move_toggle=[&](unsigned door){
        bool inside=false;
        for(unsigned t=0;t<240;++t){
            observe("actual_sparse_css_toggle_step");
            const float x=geometry[0][0],y=geometry[0][1];
            const float left=geometry[door][4],right=geometry[door][5];
            check(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(left)&&
                  std::isfinite(right)&&right>left,"Sparse CSS toggle bounds invalid");
            if(x>left+0.2f&&x<right-0.2f&&y>-4.4f&&y<0){inside=true;break;}
            const float center=(left+right)/2;
            neutral();raw[0].stickX=x<center-0.5f?80:x>center+0.5f?-80:0;
            raw[0].stickY=y<-2.2f?80:y>-2.2f?-80:0;
            check(raw[0].stickX||raw[0].stickY,"Sparse CSS cursor made no progress");step();
        }
        check(inside,"Sparse CSS toggle exceeded existing 240 source-step cap");
    };
    neutral();step();observe("actual_sparse_css_initial");
    check(doors[0][0]==Gm_PKind_Human&&doors[2][0]==Gm_PKind_NA&&
          doors[3][0]==Gm_PKind_NA&&(doors[1][0]==Gm_PKind_Human||doors[1][0]==Gm_PKind_Cpu),
          "Actual sparse CSS initial source roster differs");
    move_toggle(1);
    for(unsigned changes=0;doors[1][0]!=Gm_PKind_NA&&changes<2;++changes){
        const int before=doors[1][0];tap(0);observe("actual_sparse_css_p2_transition");
        check(doors[1][0]==(before==Gm_PKind_Human?Gm_PKind_Cpu:Gm_PKind_NA)&&
              doors[1][4]==doors[1][0],"P2 source toggle did not follow Human/CPU/NA order");
    }
    check(doors[1][0]==Gm_PKind_NA,"P2 source did not become NA");
    move_toggle(2);tap(0);observe("actual_sparse_css_p3_transition");
    check(doors[2][0]==Gm_PKind_Human&&doors[2][4]==Gm_PKind_Human,
          "Connected original port2 did not join source door2 as Human");
    bool selected=false;
    for(unsigned t=0;t<180;++t){
        MeleeWebFighterInputObservation observed{};
        check(melee_web_fighter_input_observe_port(2,CKIND_MARIO,&observed),
              "Actual P3 source cursor observation unavailable");
        emit_native_bytes("actual_sparse_css_p3_fighter",&observed,sizeof(observed));
        const int state=melee_web_fighter_input_drive(raw,&observed,CKIND_MARIO);
        check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID&&observed.cursor_port==2,
              "Actual P3 source fighter cursor lost port2");
        // The shared dense helper deliberately disconnects2/3. Restore only
        // this recipe's declared connected ports, without changing its default.
        raw[1].err=raw[3].err=-1;raw[2].err=0;
        if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED){selected=true;break;}
        if(state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY)
            raw[2].button=PAD_BUTTON_A;
        step();neutral();step();
    }
    check(selected,"Actual P3 Mario selection exceeded existing 180-step cap");
    neutral();for(unsigned t=0;t<30;++t)step();observe("actual_sparse_css_committed");
    for(unsigned port=0;port<4;++port){
        const int expected=(port==0||port==2)?Gm_PKind_Human:Gm_PKind_NA;
        check(doors[port][0]==expected&&doors[port][4]==expected&&doors[port][6]==0,
              "Actual CSS lost sparse roster or original zero-slot encoding");
    }
    check(doors[0][3]==CKIND_MARIO&&doors[2][3]==CKIND_MARIO,
          "Actual CSS lost Mario source identity");
}

// Authored component fixture: retains source rows/controllers 0/2. This is
// not evidence that original CSS joined P3, nor a Results/full-route check.
void run_sparse_source_pad(const melee_web::RuntimeFiles& files,
    MeleeWebMenuHost* host,const MeleeWebMenuMatchSelection& selection,
    char* error,std::size_t error_size)
{
    const auto* input=melee_web_menu_host_input(host);
    check(input&&selection.player_count==2&&selection.start.rules.stkind==St_Kind_Last&&
          selection.start.players[0].ckind==CKIND_MARIO&&selection.start.players[2].ckind==CKIND_MARIO&&
          selection.start.players[0].stocks==4&&selection.start.players[2].stocks==4&&
          selection.start.players[0].color==1&&selection.start.players[2].color==0&&
          selection.start.players[0].slot_type==Gm_PKind_Human&&
          selection.start.players[2].slot_type==Gm_PKind_Human&&
          selection.start.players[1].slot_type==Gm_PKind_NA&&
          selection.start.players[3].slot_type==Gm_PKind_NA&&
          selection.players[0].controller==0&&selection.players[2].controller==2,
          "Sparse PAD fixture lost original source/controller identities");
    const auto* saved_rng=seed_ptr;const auto saved_seed=*seed_ptr;
    melee_web::GameplayMatchSession match(files,selection,*input);
    check(std::memcmp(&match.start_data(),&selection.start,sizeof(selection.start))==0,
          "Sparse source-start owner changed authored payload at construction");
    float pcm[1068];unsigned phase=0,ready_ticks=0;
    auto tick=[&](const PADStatus (&pads)[4]){
        match.tick(pads);
        check(std::memcmp(&match.start_data(),&selection.start,sizeof(selection.start))==0,
              "Sparse owner changed its copied authored source payload");
        phase+=32000;const unsigned count=phase/60;phase%=60;
        check(melee_web_audio_render(match.audio(),pcm,count,error,error_size),error);
    };
    PADStatus neutral[4]{};neutral[1].err=neutral[3].err=-1;
    for(;ready_ticks<600&&!match.ready();++ready_ticks)tick(neutral);
    check(match.ready(),"Sparse source Ready/Go exceeded existing 600-tick cap");
    PADStatus distinct[4]{};distinct[1].err=distinct[3].err=-1;
    distinct[0].stickX=-48;distinct[0].button=PAD_BUTTON_LEFT;
    distinct[2].stickX=48;distinct[2].button=PAD_BUTTON_RIGHT;
    MeleeWebMatchStats observed[2]{};unsigned input_ticks=0;bool consumed=false;
    for(;input_ticks<3&&!consumed;++input_ticks){
        tick(distinct);observed[0]=match.player_stats(0);observed[1]=match.player_stats(1);
        emit_native_bytes("sparse_input_observed_stats",observed,sizeof(observed));
        emit_native_bytes("sparse_input_observed_copy_pad",HSD_PadCopyStatus,sizeof(HSD_PadCopyStatus));
        std::cout.flush(); // Retain actual observations before any assertion.
        check(observed[0].player_slot==0&&observed[1].player_slot==2&&
              Player_GetEntity(0)&&Player_GetEntity(2)&&
              !Player_GetEntity(1)&&!Player_GetEntity(3)&&
              HSD_PadCopyStatus[1].err==-1&&HSD_PadCopyStatus[3].err==-1,
              "Sparse source fighters or absent ports were renumbered");
        consumed=observed[0].source_stick[0]<0&&observed[1].source_stick[0]>0&&
            (HSD_PadCopyStatus[0].button&(PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT))==PAD_BUTTON_LEFT&&
            (HSD_PadCopyStatus[2].button&(PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT))==PAD_BUTTON_RIGHT;
    }
    check(consumed,"Distinct ports0/2 did not reach their own source fighters within three ticks");
    tick(neutral);
    check(!match.player_stats(0).source_stick[0]&&!match.player_stats(1).source_stick[0]&&
          !((HSD_PadCopyStatus[0].button|HSD_PadCopyStatus[2].button)&
            (PAD_BUTTON_LEFT|PAD_BUTTON_RIGHT)),"Sparse source input did not release");
    const auto cursor=match.source_frames();
    std::cout<<"Sparse authored source PAD: ready ticks "<<ready_ticks
             <<", input ticks "<<input_ticks<<", source cursor "<<cursor
             <<", source slots "<<observed[0].player_slot<<'/'<<observed[1].player_slot
             <<", source stick "<<observed[0].source_stick[0]<<'/'<<observed[1].source_stick[0]<<'\n';
    match.close();match.close();
    check(seed_ptr==saved_rng&&*seed_ptr==saved_seed&&!melee_web_gameplay_world_exists(),
          "Sparse checked interrupted-match close lost RNG/world ownership");
}

void run_ordinary_nontied_timeout(const melee_web::RuntimeFiles& files,
    MeleeWebMenuHost* host,const MeleeWebMenuMatchSelection& selection,
    char* error,std::size_t error_size)
{
    check(selection.player_count==2&&selection.start.rules.stkind==St_Kind_Last&&
          selection.start.rules.timer_enabled&&selection.start.rules.time_limit==60&&
          selection.start.rules.is_stock&&selection.start.rules.is_vs&&
          selection.start.players[0].stocks==4&&selection.start.players[1].stocks==4&&
          selection.start.players[0].ckind==CKIND_MARIO&&selection.start.players[1].ckind==CKIND_MARIO&&
          selection.start.players[0].slot_type==Gm_PKind_Human&&selection.start.players[1].slot_type==Gm_PKind_Human&&
          selection.players[0].controller==0&&selection.players[1].controller==1,
          "Ordinary timeout requires the committed one-minute two-Human Mario/FD selection");
    const auto* prepared=melee_web_menu_host_post_vs_mode(host);
    const auto* input=melee_web_menu_host_input(host);
    check(prepared&&input,"Ordinary timeout lost its closed menu state/input owner");
    const VsModeData post_mode=*prepared;
    const GameRules rules_before=*gmMainLib_GetGameRules();
    const auto preferences_before=*gmMainLib_8015CC58();
    const auto* host_rng=seed_ptr;const auto host_seed=*seed_ptr;
    uint32_t final_seed=0;uint8_t final_pad[MELEE_WEB_PAD_STATE_BYTES]{};
    MatchExitInfo terminal{};
    {
        melee_web::GameplayMatchSession match(files,selection,*input);
        float pcm[1068];unsigned audio_phase=0,ticks=0,move_ticks=0,ending_ticks=0;
        bool lost=false;int outward_stick=0;MeleeWebMatchStats frozen[2]{};
        for(;ticks<4800&&!match.complete();++ticks){
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            if(match.ready()&&!lost){
                if(!outward_stick){
                    const auto p1=match.player_stats(0),p2=match.player_stats(1);
                    check(std::isfinite(p1.position[0])&&std::isfinite(p2.position[0])&&
                          p1.position[0]!=0&&p1.position[0]*p2.position[0]<0,
                          "Ordinary first-loss input requires actual opposite-side FD spawns");
                    outward_stick=p1.position[0]<0?-80:80;
                    std::cout<<"Ordinary actual ready spawn x: "<<p1.position[0]<<','<<p2.position[0]
                             <<", outward stick "<<outward_stick<<'\n';
                }
                pads[0].stickX=outward_stick;++move_ticks;
            }
            check(move_ticks<=600,"First ordinary stock loss exceeded its source-input cap");
            match.tick(pads);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(match.audio(),pcm,count,error,error_size),error);
            const auto p1=match.player_stats(0),p2=match.player_stats(1);
            check(p2.stocks==4&&p2.damage_percent==0.0f&&p1.damage_percent==0.0f&&
                  (p1.stocks==4||p1.stocks==3),"Ordinary timeout lost declared stock/damage state");
            if(p1.stocks==3)lost=true; // Release immediately on first source loss.
            if(lost)check(p1.stocks==3,"Neutral post-loss ordinary player lost another stock");
            if(match.ending()){
                int winner=-1;check(lost&&match.outcome(winner)==OUTCOME_TIMEOUT,
                                   "Ordinary ending was not the natural non-tied timeout");
                for(unsigned slot=0;slot<2;++slot){
                    const auto current=match.player_stats(slot);
                    if(ending_ticks)check(current.motion_id==frozen[slot].motion_id&&
                        current.animation_frame==frozen[slot].animation_frame&&
                        current.position[0]==frozen[slot].position[0]&&current.position[1]==frozen[slot].position[1]&&
                        current.stocks==frozen[slot].stocks,"Ordinary fighter advanced during GAME freeze");
                    else frozen[slot]=current;
                }
                ++ending_ticks;
            }
        }
        int winner=-1;const auto cursor=match.source_frames();
        check(ticks<4800&&lost&&ending_ticks&&match.complete()&&cursor==3600&&
              match.outcome(winner)==OUTCOME_TIMEOUT,"Ordinary non-tied timeout did not complete naturally");
        // Canonical public finish publishes before capturing live RNG/PAD and
        // closes/restores the world owner. Never capture before publication.
        match.finish_vs(final_seed,final_pad);
        check(seed_ptr==host_rng&&*host_rng==host_seed,"Ordinary finish did not restore host RNG owner/value");
        check(melee_web_match_rules_terminal_data(&terminal)&&terminal.match_end.outcome==OUTCOME_TIMEOUT&&
              terminal.match_end.match_kind==selection.start.rules.match_kind&&
              terminal.match_end.n_winners==1&&terminal.match_end.winners[0]==1,
              "Original ordinary terminal did not publish unique P2 timeout winner");
        for(unsigned slot=0;slot<2;++slot){
            const auto& p=terminal.match_end.player_standings[slot];const auto& start=selection.start.players[slot];
            check(p.slot_type==start.slot_type&&p.ckind==start.ckind&&p.x3==start.color&&
                  p.stocks==(slot==0?3:4),"Ordinary terminal lost original player identity/stocks");
        }
        check(!melee_web_match_rules_publish_result(),
              "Closed ordinary match accepted publication without its live source owner");
        MatchExitInfo repeated{};
        check(melee_web_match_rules_terminal_data(&repeated)&&
              std::memcmp(&terminal,&repeated,sizeof(terminal))==0,
              "Closed ordinary retained terminal data changed");
        emit_native_bytes("ordinary_timeout_terminal",&terminal,sizeof(terminal));
        emit_native_bytes("ordinary_timeout_final_pad",final_pad,sizeof(final_pad));
        std::cout<<"Natural non-tied ordinary timeout: input ticks "<<ticks<<", source cursor "<<cursor
                 <<", movement ticks "<<move_ticks<<", final seed "<<final_seed<<'\n';
    }
    MeleeWebMenuMatchContinuation continuation{};
    check(melee_web_menu_host_match_continuation_begin(host,&terminal,final_seed,&continuation,error,error_size),error);
    check(continuation.kind==MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS&&
          std::memcmp(&continuation.payload.results.match_end,&terminal.match_end,sizeof(terminal.match_end))==0&&
          std::memcmp(&gmVsMelee_GetVsData()->start.rules,&post_mode.start.rules,sizeof(post_mode.start.rules))==0,
          "Ordinary typed Results lost its terminal or checked post-VS rules");
    final_seed=*seed_ptr; // Observable host-owned Results-entry seed after original callbacks.
    run_typed_results_source_smoke(files,host,continuation.payload.results,final_seed,final_pad,nullptr,&kTimeoutResults);
    check(std::memcmp(gmMainLib_GetGameRules(),&rules_before,sizeof(rules_before))==0&&
          std::memcmp(gmMainLib_8015CC58(),&preferences_before,sizeof(preferences_before))==0,
          "Ordinary Results changed current GameRules or Items/preferences");
    run_returned_menu_selection(files,host,error,error_size,&post_mode.start.rules);
}
void run_sudden_death_host_control(
    MeleeWebMenuHost* host, const MeleeWebMenuMatchSelection& selection,
    char* error, std::size_t error_size,
    const melee_web::RuntimeFiles* world_files = nullptr, bool natural_timeout = false, bool resolve_sd = false)
{
    check(!resolve_sd||(natural_timeout&&world_files),
          "SD resolution requires the natural typed prior-VS world");
    check(host != nullptr && selection.player_count >= 2 &&
              selection.player_count <= GM_MAX_PLAYERS,
          "Sudden Death host control requires the committed source menu selection");
    check(melee_web_menu_host_input(host) != nullptr,
          "Closed SSS did not retain its source PAD owner");

    const VsModeData* prepared_mode=melee_web_menu_host_post_vs_mode(host);
    check(prepared_mode!=nullptr,"SD control has no retained original VS entry state");
    const VsModeData retained_mode=*prepared_mode;
    const StartMeleeData& route_start=retained_mode.start;
    const std::uint32_t menu_seed = selection.random_seed;
    std::uint32_t vs_final_seed = menu_seed ^ 0x6d2b79f5U;
    if (vs_final_seed == menu_seed) ++vs_final_seed;
    std::uint32_t sudden_death_final_seed = vs_final_seed ^ 0xa5c31e27U;
    if (sudden_death_final_seed == vs_final_seed) ++sudden_death_final_seed;

    const StartMeleeData original_start = gmVsMelee_StartData;
    const MatchExitInfo original_sudden_death_exit =
        gmVsMelee_SuddenDeathExitInfo;

    MatchExitInfo tied_timeout{};
    std::uint8_t natural_final_pad[MELEE_WEB_PAD_STATE_BYTES]{};
    if(natural_timeout){
        check(world_files&&selection.player_count==2&&
              selection.start.rules.timer_enabled&&selection.start.rules.time_limit==60,
              "Natural timeout requires original one-minute stock preparation");
        const auto* host_rng_owner=seed_ptr;
        const std::uint32_t host_rng_seed=*seed_ptr;
        try{
        melee_web::GameplayMatchSession prior(*world_files,selection,
                                              *melee_web_menu_host_input(host));
        float pcm[1068];unsigned audio_phase=0,input_ticks=0;
        for(;input_ticks<4800&&!prior.complete();++input_ticks){
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            prior.tick(pads);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(prior.audio(),pcm,count,error,error_size),error);
            for(unsigned slot=0;slot<2;++slot){
                const auto stats=prior.player_stats(slot);
                check(stats.stocks==4&&stats.damage_percent==0.0f,
                      "Neutral natural timeout changed player stocks or damage");
            }
        }
        int winner=-1;
        check(input_ticks<4800&&prior.complete()&&prior.source_frames()>0&&
              prior.outcome(winner)==OUTCOME_TIMEOUT,
              "Neutral original VS did not reach its natural timeout boundary");
        const auto* final_rng_owner=seed_ptr;
        const auto cursor=prior.source_frames();
        check(melee_web_match_rules_publish_result()&&
              melee_web_match_rules_terminal_data(&tied_timeout),
              "Natural timeout did not publish original terminal data");
        check(seed_ptr==final_rng_owner,"Original terminal publication replaced RNG owner");
        melee_web_pad_state_capture(natural_final_pad);vs_final_seed=prior.random_seed();
        check(tied_timeout.match_end.outcome==OUTCOME_TIMEOUT&&
              tied_timeout.match_end.match_kind==selection.start.rules.match_kind&&
              tied_timeout.match_end.n_winners==2&&
              tied_timeout.match_end.winners[0]==0&&tied_timeout.match_end.winners[1]==1,
              "Natural timeout did not retain the exact two-player tie");
        for(unsigned slot=0;slot<2;++slot){
            const auto& standing=tied_timeout.match_end.player_standings[slot];
            check(standing.slot_type==route_start.players[slot].slot_type&&
                  standing.ckind==route_start.players[slot].ckind&&
                  standing.x3==route_start.players[slot].color&&standing.stocks==4,
                  "Natural timeout lost active source player identity");
        }
        emit_native_bytes("natural_vs_terminal",&tied_timeout,sizeof(tied_timeout));
        emit_native_bytes("natural_vs_final_pad",natural_final_pad,sizeof(natural_final_pad));
        std::cout<<"Natural neutral VS timeout: input ticks "<<input_ticks
                 <<", source cursor "<<cursor<<", final RNG "<<vs_final_seed<<'\n';
        prior.close();prior.close();
        check(seed_ptr==host_rng_owner&&*seed_ptr==host_rng_seed,
              "Natural VS close did not restore the prior host RNG owner and value");
        MatchExitInfo after_close{};
        check(melee_web_match_rules_terminal_data(&after_close)&&
              std::memcmp(&after_close,&tied_timeout,sizeof(after_close))==0,
              "Natural terminal data changed at source close");
        }catch(...){
            check(melee_web_menu_host_destroy(host,error,error_size),error);
            throw;
        }
    }else{
    tied_timeout.match_end.outcome = OUTCOME_TIMEOUT;
    tied_timeout.match_end.match_kind = selection.start.rules.match_kind;
    tied_timeout.match_end.n_winners =
        static_cast<std::uint8_t>(selection.player_count);
    tied_timeout.match_end.frame_count = 3600;
    for (auto& player : tied_timeout.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    for (unsigned i = 0; i < selection.player_count; ++i) {
        const auto& source = route_start.players[i];
        auto& standing = tied_timeout.match_end.player_standings[i];
        check(source.slot_type != Gm_PKind_NA,
              "Committed VS source payload has a gap before its active players");
        standing.slot_type = source.slot_type;
        standing.ckind = source.ckind;
        standing.stocks = 1;
        standing.is_big_loser = false;
        tied_timeout.match_end.winners[i] = static_cast<u8>(i);
    }

    }

    StartMeleeData expected_sudden_death{};
    expected_sudden_death.rules = route_start.rules;
    for (unsigned i = 0; i < GM_MAX_PLAYERS; ++i)
        expected_sudden_death.players[i] = route_start.players[i];
    gm_SetupSubColors(&expected_sudden_death);
    gm_LoadRumbleEnabled(&expected_sudden_death);
    gm_SetupSuddenDeath(&expected_sudden_death, &tied_timeout.match_end);

    MeleeWebMenuMatchContinuation continuation{};
    check(melee_web_menu_host_match_continuation_begin(
              host, &tied_timeout, vs_final_seed, &continuation,
              error, error_size), error);
    check(continuation.kind == MELEE_WEB_MENU_MATCH_CONTINUATION_SUDDEN_DEATH &&
              std::memcmp(&continuation.payload.sudden_death_start,
                          &expected_sudden_death,
                          sizeof(expected_sudden_death)) == 0 &&
              std::memcmp(&gmVsMelee_StartData, &expected_sudden_death,
                          sizeof(expected_sudden_death)) == 0,
          "Typed host did not retain the original tie-selected Sudden Death payload");
    const std::uint64_t sudden_death_owner_id=continuation.owner_id;
    check(sudden_death_owner_id!=0,
          "Typed Sudden Death continuation has no checked owner identity");
    MeleeWebMenuMatchSelection sudden_death_selection{};
    check(melee_web_menu_host_sudden_death_selection(
              host,&continuation,&sudden_death_selection,error,error_size)&&
              sudden_death_selection.sudden_death&&
              sudden_death_selection.start.rules.x6,
          "Typed Sudden Death selection did not preserve original scene setup");
    if(world_files){
        // Retain the exact final prior-VS bank for natural handoff; the older
        // constructed control explicitly uses synthetic closed-SSS input.
        std::uint8_t entry_bytes[MELEE_WEB_PAD_STATE_BYTES];
        if(natural_timeout)std::memcpy(entry_bytes,natural_final_pad,sizeof(entry_bytes));
        else{
            melee_web_pad_state_apply(melee_web_menu_host_input(host));
            melee_web_pad_state_capture(entry_bytes);
        }
        std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> entry(
            melee_web_pad_state_decode(entry_bytes,sizeof(entry_bytes),error,error_size),
            melee_web_pad_state_free);
        check(entry!=nullptr,error);
        melee_web::RuntimeArchiveCache cache(*world_files);
        try{
            melee_web::GameplayMatchSession match(*world_files,host,continuation,
                cache,melee_web::GameplayMatchConstruction::Immediate,*entry);
            check(match.sudden_death()&&melee_web_gameplay_generation()!=0,
                  "Typed SD construction did not own an actual source world");
            const auto* scene=static_cast<const GameModeState::GameSceneInfo*>(
                melee_web_current_scene_info());
            check(scene&&scene->scene_kind==GS_SUDDEN_DEATH&&
                  gm_GetCurrentSceneEnterData()==&gmVsMelee_StartData&&
                  gm_GetCurrentSceneExitData()==&gmVsMelee_SuddenDeathExitInfo,
                  "Actual SD world lost original scene entry/exit identity");
            check(std::memcmp(&match.start_data(),&sudden_death_selection.start,
                              sizeof(StartMeleeData))==0,
                  "Actual SD world changed its source-generated payload");
            check(!match.start_data().rules.timer_enabled,
                  "Actual SD world retained ordinary VS timer");
            for(unsigned i=0;i<selection.player_count;++i){
                const auto stats=match.player_stats(i);
                check(stats.player_slot==i&&stats.stocks==1&&
                      stats.damage_percent==300.0f&&
                      match.start_data().players[i].x12==300&&
                      match.start_data().players[i].slot==route_start.players[i].slot,
                      "Actual SD world changed original slots, stock or 300-percent damage setup");
            }
            const auto retained_scene=melee_web_current_scene_info();
            const auto retained_start=gmVsMelee_StartData;
            const auto retained_exit=gmVsMelee_SuddenDeathExitInfo;
            const auto retained_generation=melee_web_gameplay_generation();
            const auto retained_frame=match.source_frames();
            const auto retained_seed=match.random_seed();
            const auto retained_player=match.player_stats(0);
            std::uint8_t before[MELEE_WEB_PAD_STATE_BYTES],after[MELEE_WEB_PAD_STATE_BYTES];
            melee_web_pad_state_capture(before);
            MeleeWebMenuMatchContinuation premature{};
            bool rejected=false;
            try{match.finish_sudden_death(premature);}
            catch(const std::exception& exception){
                rejected=std::string_view(exception.what())==
                    "Sudden Death Results handoff requires the completed original source flow";
            }
            melee_web_pad_state_capture(after);
            const auto player=match.player_stats(0);
            check(rejected&&premature.kind==0&&
                  retained_scene==melee_web_current_scene_info()&&
                  retained_generation==melee_web_gameplay_generation()&&
                  retained_frame==match.source_frames()&&retained_seed==match.random_seed()&&
                  std::memcmp(&retained_start,&gmVsMelee_StartData,sizeof(retained_start))==0&&
                  std::memcmp(&retained_exit,&gmVsMelee_SuddenDeathExitInfo,sizeof(retained_exit))==0&&
                  std::memcmp(before,after,sizeof(before))==0&&
                  player.player_slot==retained_player.player_slot&&
                  player.stocks==retained_player.stocks&&
                  player.motion_id==retained_player.motion_id&&
                  player.animation_frame==retained_player.animation_frame&&
                  std::memcmp(player.position,retained_player.position,sizeof(player.position))==0,
                  "Premature SD finish destructively changed the live source world");
            check(!melee_web_menu_host_destroy(host,error,error_size),
                  "Actual live SD world allowed host destruction");
            MeleeWebMenuMatchSelection duplicate{};
            check(!melee_web_menu_host_sudden_death_match_claim(
                      host,&continuation,&duplicate,error,error_size)&&
                  !melee_web_menu_host_sudden_death_scene_end(
                      host,sudden_death_owner_id+1,error,error_size)&&
                  !melee_web_menu_host_sudden_death_match_release(
                      host,sudden_death_owner_id+1,error,error_size),
                  "Actual live SD world accepted duplicate or stale ownership");
            auto observe_frame=[&](const char* event,unsigned tick){
                const auto* current=static_cast<const GameModeState::GameSceneInfo*>(
                    melee_web_current_scene_info());
                const auto& rules=*gm_GetRules();
                const auto* data=gm_16AE_GetUnkData_0();
                const int mode=gm_GetCurrentGameMode();
                const int kind=current?current->scene_kind:-1;
                const void* enter=current?current->enter_data:nullptr;
                const void* exit=current?current->exit_data:nullptr;
                std::cout<<"{\"record\":\"sd_source_frame_inputs\",\"event\":\""<<event
                         <<"\",\"tick\":"<<tick<<",\"source_cursor\":"<<match.source_frames()
                         <<",\"mode\":"<<mode<<",\"scene_kind\":"<<kind
                         <<",\"entry_original\":"<<(enter==&gmVsMelee_StartData?"true":"false")
                         <<",\"exit_original_sd\":"<<(exit==&gmVsMelee_SuddenDeathExitInfo?"true":"false")
                         <<",\"entry_x6\":"<<(gmVsMelee_StartData.rules.x6?"true":"false")
                         <<",\"rules_x6\":"<<(rules.x6?"true":"false")
                         <<",\"is_stock\":"<<(rules.is_stock?"true":"false")
                         <<",\"is_vs\":"<<(rules.is_vs?"true":"false")
                         <<",\"match_kind\":"<<unsigned(rules.match_kind)
                         <<",\"end_state\":"<<int(data->unk_0)
                         <<",\"singleplayer\":"<<int(data->is_singleplayer)
                         <<",\"x4_4\":"<<unsigned(rules.x4_4)
                         <<",\"x3_2\":"<<unsigned(rules.x3_2)
                         <<",\"frame_start_callback\":"<<(rules.on_frame_start?"true":"false")
                         <<",\"frame_end_callback\":"<<(rules.on_frame_end?"true":"false")
                         <<",\"scene_supported\":"
                         <<(melee_web_match_source_scene_supported(mode,kind,enter,exit,
                                  gmVsMelee_StartData.rules.x6,&rules)?"true":"false")
                         <<"}\n"<<std::flush;
            };
            float pcm[1068];unsigned audio_phase=0;
            if(resolve_sd){
                const auto tick_sd=[&](const PADStatus pads[4]){
                    match.tick(pads);audio_phase+=32000;
                    const unsigned count=audio_phase/60;audio_phase%=60;
                    check(melee_web_audio_render(match.audio(),pcm,count,error,error_size),error);
                };
                unsigned ready_ticks=0;
                for(;ready_ticks<600&&(!match.ready()||match.source_frames()==0);++ready_ticks){
                    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;tick_sd(pads);
                }
                observe_frame("ready_boundary",ready_ticks);
                check(match.ready()&&match.source_frames()>0&&!match.complete(),
                      "Actual SD did not reach ready with active source cursor within 600 ticks");
                const auto sd_active_start=match.source_frames();
                unsigned elimination_ticks=0,ending_ticks=0;MeleeWebMatchStats frozen[2]{};
                int winner=-1;
                for(;elimination_ticks<2400&&!match.complete();++elimination_ticks){
                    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;pads[0].stickX=80;
                    tick_sd(pads);
                    check(match.player_stats(1).stocks==1,"Neutral SD opponent lost its original stock");
                    if(match.ending()){
                        check(match.outcome(winner)==OUTCOME_ELIMINATION,
                              "SD ending began without original elimination");
                        for(unsigned slot=0;slot<2;++slot){
                            const auto current=match.player_stats(slot);
                            if(ending_ticks)check(current.motion_id==frozen[slot].motion_id&&
                                current.animation_frame==frozen[slot].animation_frame&&
                                current.position[0]==frozen[slot].position[0]&&
                                current.position[1]==frozen[slot].position[1]&&
                                current.stocks==frozen[slot].stocks,
                                "SD fighter processes advanced during original GAME freeze");
                            else frozen[slot]=current;
                        }
                        ++ending_ticks;
                    }
                }
                check(match.complete()&&ending_ticks>0&&match.player_stats(0).stocks==0&&
                      match.outcome(winner)==OUTCOME_ELIMINATION,
                      "Input-driven SD did not complete its original P2 elimination winner");
                const auto sd_final_frames=match.source_frames();
                // Exercise the public finish directly: no test-only terminal
                // prepublication or pre-close capture can mask its ordering.
                MeleeWebMenuMatchContinuation results{};match.finish_sudden_death(results);
                MatchExitInfo sd_exit{};
                check(melee_web_match_rules_terminal_data(&sd_exit)&&
                      sd_exit.match_end.outcome==OUTCOME_ELIMINATION&&
                      sd_exit.match_end.n_winners==1&&sd_exit.match_end.winners[0]==1,
                      "Direct SD finish did not retain original P2 elimination terminal");
                winner=sd_exit.match_end.winners[0];
                emit_native_bytes("natural_sd_terminal",&sd_exit,sizeof(sd_exit));
                MatchEnd expected_result=tied_timeout.match_end;MatchEnd sd_merge=sd_exit.match_end;
                gm_80166CCC(&expected_result,&sd_merge);
                check(results.kind==MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS&&
                      std::memcmp(&results.payload.results.match_end,&expected_result,sizeof(expected_result))==0&&
                      !melee_web_gameplay_generation()&&!melee_web_gameplay_world_exists(),
                      "Natural SD typed Results lost original merge or live owner cleanup");
                const auto results_entry_seed=*seed_ptr;
                std::cout<<"Natural SD elimination: ready input ticks "<<ready_ticks
                         <<", departure input ticks "<<elimination_ticks<<", active cursor "<<sd_active_start
                         <<", final SD cursor "<<sd_final_frames<<", frozen ending ticks "<<ending_ticks
                         <<", winner source slot "<<winner
                         <<", final SD internal RNG/PAD unobserved, Results entry RNG "<<results_entry_seed<<'\n';
                emit_native_bytes("natural_overall_results",&results.payload.results,sizeof(ResultsMatchInfo));
                auto result_seed=results_entry_seed;
                std::uint8_t results_pad[MELEE_WEB_PAD_STATE_BYTES]{};
                const auto* results_input=melee_web_menu_host_input(host);
                check(results_input!=nullptr,"Typed Results did not retain the actual source handoff bank");
                run_typed_results_source_smoke(*world_files,host,results.payload.results,
                                               result_seed,results_pad,results_input);
                run_returned_menu_selection(*world_files,host,error,error_size);
                std::cout<<"Natural SD typed Results and original CSS return completed\n";
            }else{
            for(unsigned tick=0;tick<8;++tick){
                PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
                observe_frame("pre",tick);
                try{match.tick(pads);}
                catch(...){observe_frame("failure",tick);throw;}
                audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
                check(melee_web_audio_render(match.audio(),pcm,count,error,error_size),error);
            }
            check(!match.complete(),"Short neutral SD prefix unexpectedly completed");
            std::cout<<"Actual SD neutral prefix: 8 input ticks, source cursor "
                     <<retained_frame<<" -> "<<match.source_frames()<<"\n";
            match.close();match.close();
            check(!melee_web_gameplay_generation()&&!melee_web_gameplay_world_exists(),
                  "Actual SD abort retained its source world");
            check(!melee_web_menu_host_sudden_death_match_claim(
                      host,&continuation,&duplicate,error,error_size),
                  "Aborted SD world allowed replay of its consumed continuation");
            }
        }catch(...){
            // Match RAII has already retired world/scene/claim before abort.
            const auto primary=std::current_exception();
            if(!melee_web_menu_host_destroy(host,error,error_size))
                std::cerr<<"Secondary host destroy failure: "<<error<<'\n';
            std::rethrow_exception(primary);
        }
        check(melee_web_menu_host_destroy(host,error,error_size),error);
        check(std::memcmp(&gmVsMelee_StartData,&original_start,sizeof(original_start))==0&&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo,&original_sudden_death_exit,
                          sizeof(original_sudden_death_exit))==0,
              "SD abort did not restore route globals");
        check(melee_web_vs_mode_begin(),"SD abort leaked original VS lease");
        check(melee_web_vs_mode_end(),"SD abort reacquired VS lease could not close");
        auto* replacement=melee_web_menu_host_create(error,error_size);
        check(replacement!=nullptr,error);
        MeleeWebMenuMatchSelection stale{};
        check(!melee_web_menu_host_sudden_death_selection(
                  replacement,&continuation,&stale,error,error_size),
              "Replacement host accepted stale SD continuation");
        check(melee_web_menu_host_destroy(replacement,error,error_size),error);
        if(resolve_sd)std::cout<<"Fixture-controlled natural VS timeout to input-driven SD elimination, Results/CSS and cleanup passed; no rendered/equivalence claim\n";
        else std::cout<<(natural_timeout?"Fixture-controlled original CSS/SSS natural VS tie -> actual SD world, early finish rejection, ":"Original CSS/SSS constructed tie -> actual SD world, early finish rejection, ")
                  <<"neutral prefix and abort/reacquisition passed; "
                  <<(natural_timeout?"actual final VS PAD origin, ":"synthetic SSS PAD origin, ")
                  <<"no winner, rendered or equivalence claim\n";
        return;
    }
    check(melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),error);
    check(!melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),
          "Typed Sudden Death continuation allowed a duplicate match owner");
    check(!melee_web_menu_host_destroy(host,error,error_size),
          "Typed Sudden Death host was destroyed before its match owner");
    check(!melee_web_menu_host_sudden_death_scene_begin(
              host,sudden_death_owner_id+1,error,error_size),
          "Typed Sudden Death scene accepted a stale owner identity");
    const void* saved_sudden_death_scene=melee_web_current_scene_info();
    check(melee_web_menu_host_sudden_death_scene_begin(
              host,sudden_death_owner_id,error,error_size),error);
    const auto* sudden_death_scene=
        static_cast<const GameModeState::GameSceneInfo*>(
            melee_web_current_scene_info());
    check(sudden_death_scene&&sudden_death_scene->scene_kind==GS_SUDDEN_DEATH,
          "Typed match owner did not assign the original GS_SUDDEN_DEATH scene");
    check(gmVsMelee_StartData.rules.x6,
          "Original GS_SUDDEN_DEATH scene setup did not set the global source payload");
    check(gm_GetCurrentSceneEnterData()==&gmVsMelee_StartData&&
              gm_GetCurrentSceneExitData()==&gmVsMelee_SuddenDeathExitInfo,
          "Sudden Death scene identity did not retain the original global entry and exit payloads");
    const auto* retained_sudden_death_scene=melee_web_current_scene_info();
    const StartMeleeData retained_sudden_death_start=gmVsMelee_StartData;
    const MatchExitInfo retained_sudden_death_exit=gmVsMelee_SuddenDeathExitInfo;
    std::uint8_t input_before_premature_finish[MELEE_WEB_PAD_STATE_BYTES];
    std::uint8_t input_after_premature_finish[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(input_before_premature_finish);
    MeleeWebMenuMatchContinuation premature_finish{};
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&tied_timeout,sudden_death_final_seed,
              input_before_premature_finish,&premature_finish,error,error_size)&&
              premature_finish.kind==0,
          "Typed host accepted Results handoff before its SD owner released the scene");
    melee_web_pad_state_capture(input_after_premature_finish);
    check(melee_web_current_scene_info()==retained_sudden_death_scene&&
              std::memcmp(&gmVsMelee_StartData,&retained_sudden_death_start,
                          sizeof(retained_sudden_death_start))==0&&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo,
                          &retained_sudden_death_exit,
                          sizeof(retained_sudden_death_exit))==0&&
              std::memcmp(input_before_premature_finish,
                          input_after_premature_finish,
                          sizeof(input_before_premature_finish))==0,
          "Rejected early Results handoff changed SD scene, source payloads or PAD state");
    MeleeWebMenuMatchSelection after_premature_selection{};
    check(melee_web_menu_host_selection(host,&after_premature_selection,
                                        error,error_size)&&
              after_premature_selection.random_seed==vs_final_seed&&
              !melee_web_gameplay_generation(),
          "Rejected early Results handoff changed the VS owner seed or world boundary");
    check(!melee_web_menu_host_sudden_death_match_release(
              host,sudden_death_owner_id,error,error_size),
          "Typed Sudden Death match claim released before its source scene restored");
    check(!melee_web_menu_host_sudden_death_scene_end(
              host,sudden_death_owner_id+1,error,error_size),
          "Typed Sudden Death scene restored for a stale owner identity");
    check(melee_web_menu_host_sudden_death_scene_end(
              host,sudden_death_owner_id,error,error_size),error);
    check(melee_web_current_scene_info()==saved_sudden_death_scene&&
              gm_GetCurrentSceneEnterData()!=&gmVsMelee_StartData,
          "Sudden Death scene end did not restore the exact prior host scene");
    check(melee_web_menu_host_sudden_death_match_release(
              host,sudden_death_owner_id,error,error_size),error);
    check(!melee_web_menu_host_sudden_death_match_claim(
              host,&continuation,&sudden_death_selection,error,error_size),
          "Typed Sudden Death continuation allowed a second match after release");
    MeleeWebMenuMatchSelection observed{};
    check(melee_web_menu_host_selection(host, &observed, error, error_size) &&
              observed.random_seed == vs_final_seed,
          "Typed VS continuation did not transfer its explicit match RNG seed");
    check(!melee_web_vs_mode_begin(),
          "Typed Sudden Death continuation released its original VS mode lease");

    MatchExitInfo sudden_death_exit{};
    sudden_death_exit.match_end.match_kind = route_start.rules.match_kind;
    sudden_death_exit.match_end.outcome = OUTCOME_ELIMINATION;
    sudden_death_exit.match_end.frame_count = 9876;
    sudden_death_exit.match_end.n_winners = 1;
    const unsigned winner = selection.player_count - 1;
    sudden_death_exit.match_end.winners[0] = static_cast<u8>(winner);
    for (auto& player : sudden_death_exit.match_end.player_standings)
        player.slot_type = Gm_PKind_NA;
    auto& winning_standing =
        sudden_death_exit.match_end.player_standings[winner];
    winning_standing.slot_type = route_start.players[winner].slot_type;
    winning_standing.ckind = route_start.players[winner].ckind;
    winning_standing.stocks = 1;

    MatchEnd expected_result = tied_timeout.match_end;
    MatchEnd sudden_death_result = sudden_death_exit.match_end;
    gm_80166CCC(&expected_result, &sudden_death_result);
    std::uint8_t final_sudden_death_input[MELEE_WEB_PAD_STATE_BYTES];
    melee_web_pad_state_capture(final_sudden_death_input);
    MeleeWebMenuMatchContinuation rejected_finish{};
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id+1,&sudden_death_exit,
              sudden_death_final_seed,final_sudden_death_input,
              &rejected_finish,error,error_size)&&rejected_finish.kind==0,
          "Typed Sudden Death finish accepted a stale match owner identity");
    check(melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&sudden_death_exit,sudden_death_final_seed,
              final_sudden_death_input,
              &continuation, error, error_size), error);
    check(continuation.kind == MELEE_WEB_MENU_MATCH_CONTINUATION_RESULTS &&
              std::memcmp(&continuation.payload.results.match_end,
                          &expected_result, sizeof(expected_result)) == 0 &&
              std::memcmp(&gmVsMelee_VsExitInfo.match_end, &expected_result,
                          sizeof(expected_result)) == 0 &&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo, &sudden_death_exit,
                          sizeof(sudden_death_exit)) == 0,
          "Typed Sudden Death finish did not preserve the source Results merge");
    check(melee_web_menu_host_selection(host, &observed, error, error_size) &&
              observed.random_seed == sudden_death_final_seed,
          "Typed Sudden Death finish did not transfer its explicit final RNG seed");
    std::uint8_t transferred_input[MELEE_WEB_PAD_STATE_BYTES];
    check(melee_web_menu_host_input(host)!=nullptr,
          "Sudden Death Results handoff lost its transferred source PAD owner");
    melee_web_pad_state_apply(melee_web_menu_host_input(host));
    melee_web_pad_state_capture(transferred_input);
    check(std::memcmp(transferred_input,final_sudden_death_input,
                      sizeof(transferred_input))==0,
          "Sudden Death Results handoff changed the captured full PAD bank");
    check(!melee_web_menu_host_sudden_death_finish(
              host,sudden_death_owner_id,&sudden_death_exit,sudden_death_final_seed,
              final_sudden_death_input,
              &continuation, error, error_size) && continuation.kind == 0,
          "Typed host replayed the Sudden Death exit callback");
    check(melee_web_menu_host_destroy(host, error, error_size), error);
    check(std::memcmp(&gmVsMelee_StartData, &original_start,
                      sizeof(original_start)) == 0 &&
              std::memcmp(&gmVsMelee_SuddenDeathExitInfo,
                          &original_sudden_death_exit,
                          sizeof(original_sudden_death_exit)) == 0,
          "Typed host did not restore its Sudden Death route globals");
    check(melee_web_vs_mode_begin(),
          "Typed Sudden Death host destruction leaked its VS mode lease");
    check(melee_web_vs_mode_end(),
          "Typed Sudden Death host control could not retire the VS lease");
    std::cout << "Original CSS/SSS typed VS tie to Sudden Death to Results handoff "
                 "retained both final RNG seeds and restored route globals; "
                 "menu-backed constructed-result host control only\n";
}

std::string hex32(uint32_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(8)<<value;return out.str();}
std::string hex64(uint64_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();}
uint32_t pad_wire_u32(const uint8_t* bytes,size_t offset){
 return (uint32_t(bytes[offset])<<24)|(uint32_t(bytes[offset+1])<<16)|
        (uint32_t(bytes[offset+2])<<8)|uint32_t(bytes[offset+3]);
}
int32_t pad_wire_i32(const uint8_t* bytes,size_t offset){
 const uint32_t bits=pad_wire_u32(bytes,offset);int32_t value;std::memcpy(&value,&bits,sizeof(value));return value;
}
int8_t pad_wire_i8(const uint8_t* bytes,size_t offset){
 int8_t value;std::memcpy(&value,bytes+offset,sizeof(value));return value;
}
float pad_wire_f32(const uint8_t* bytes,size_t offset){
 const uint32_t bits=pad_wire_u32(bytes,offset);float value;std::memcpy(&value,&bits,sizeof(value));return value;
}
uint64_t pad_wire_identity(const uint8_t* bytes){
 uint64_t hash=14695981039346656037ULL;
 for(size_t i=0;i<MELEE_WEB_PAD_STATE_BYTES;++i){hash^=bytes[i];hash*=1099511628211ULL;}
 return hash;
}
void report_pad_snapshot(const char* boundary,const uint8_t* bytes,const uint8_t* previous=nullptr){
 char error[128]{};
 std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> decoded(
     melee_web_pad_state_decode(bytes,MELEE_WEB_PAD_STATE_BYTES,error,sizeof(error)),
     melee_web_pad_state_free);
 const int32_t repeat_start=pad_wire_i32(bytes,0),repeat_interval=pad_wire_i32(bytes,4);
 const int8_t adc_type=pad_wire_i8(bytes,8),adc_th=pad_wire_i8(bytes,9);
 const float adc_angle=pad_wire_f32(bytes,10);
 const uint8_t clamp_stick_type=bytes[14],clamp_stick_shift=bytes[15];
 const int8_t clamp_stick_max=pad_wire_i8(bytes,16),clamp_stick_min=pad_wire_i8(bytes,17);
 const uint8_t clamp_lr_shift=bytes[18],clamp_lr_max=bytes[19],clamp_lr_min=bytes[20];
 const uint8_t clamp_ab_shift=bytes[21],clamp_ab_max=bytes[22],clamp_ab_min=bytes[23];
 const int8_t scale_stick=pad_wire_i8(bytes,24);
 const uint8_t scale_lr=bytes[25],scale_ab=bytes[26],cross_dir=bytes[27];
 const uint8_t reset_switch_status=bytes[28],reset_switch=bytes[29];
 const bool config_valid=repeat_start>0&&repeat_interval>0&&adc_type>=0&&adc_type<=3&&
     adc_th>=0&&std::isfinite(adc_angle)&&clamp_stick_type<=1&&clamp_stick_shift<=1&&
     clamp_stick_min>=0&&clamp_stick_max>clamp_stick_min&&clamp_lr_shift<=1&&
     clamp_lr_max>clamp_lr_min&&clamp_ab_shift<=1&&clamp_ab_max>clamp_ab_min&&
     scale_stick>0&&scale_lr&&scale_ab&&cross_dir<=3&&reset_switch_status<=1&&reset_switch<=1;
 const uint64_t identity=pad_wire_identity(bytes);
 std::cerr<<"C1_PAD_SNAPSHOT boundary="<<boundary<<" bytes="<<MELEE_WEB_PAD_STATE_BYTES
          <<" fnv64="<<hex64(identity)<<" decode="<<bool(decoded)
          <<" config_valid="<<config_valid<<" previous_equal="
          <<(previous?std::memcmp(bytes,previous,MELEE_WEB_PAD_STATE_BYTES)==0:-1);
 if(!decoded)std::cerr<<" decode_error="<<error;
 std::cerr<<'\n';
 std::ostringstream raw_hex;raw_hex<<std::hex<<std::setfill('0');
 for(size_t i=0;i<MELEE_WEB_PAD_STATE_BYTES;++i)raw_hex<<std::setw(2)<<unsigned(bytes[i]);
 std::cerr<<"C1_PAD_RAW boundary="<<boundary<<" hex="<<raw_hex.str()<<'\n';
 std::cerr<<"C1_PAD_CONFIG boundary="<<boundary<<" repeat_start="<<repeat_start
          <<" repeat_interval="<<repeat_interval<<" adc_type="<<int(adc_type)
          <<" adc_th="<<int(adc_th)<<" adc_angle="<<adc_angle
          <<" adc_angle_bits="<<hex32(pad_wire_u32(bytes,10))
          <<" clamp_stick_type="<<unsigned(clamp_stick_type)
          <<" clamp_stick_shift="<<unsigned(clamp_stick_shift)
          <<" clamp_stick_max="<<int(clamp_stick_max)<<" clamp_stick_min="<<int(clamp_stick_min)
          <<" clamp_lr="<<unsigned(clamp_lr_shift)<<','<<unsigned(clamp_lr_max)<<','<<unsigned(clamp_lr_min)
          <<" clamp_ab="<<unsigned(clamp_ab_shift)<<','<<unsigned(clamp_ab_max)<<','<<unsigned(clamp_ab_min)
          <<" scale="<<int(scale_stick)<<','<<unsigned(scale_lr)<<','<<unsigned(scale_ab)
          <<" cross_dir="<<unsigned(cross_dir)<<" reset="<<unsigned(reset_switch_status)
          <<','<<unsigned(reset_switch)<<'\n';
 for(unsigned bank=0;bank<3;++bank)for(unsigned slot=0;slot<4;++slot){
  const size_t base=30+(bank*4+slot)*66;
  std::ostringstream normalized;
  unsigned finite_count=0;
  for(unsigned i=0;i<8;++i){
   const uint32_t bits=pad_wire_u32(bytes,base+32+i*4);
   if(i)normalized<<',';
   normalized<<hex32(bits);
   if(std::isfinite(pad_wire_f32(bytes,base+32+i*4)))++finite_count;
  }
  std::cerr<<"C1_PAD_HISTORY boundary="<<boundary<<" bank="<<bank<<" slot="<<slot
           <<" button="<<hex32(pad_wire_u32(bytes,base))
           <<" last="<<hex32(pad_wire_u32(bytes,base+4))
           <<" trigger="<<hex32(pad_wire_u32(bytes,base+8))
           <<" repeat="<<hex32(pad_wire_u32(bytes,base+12))
           <<" release="<<hex32(pad_wire_u32(bytes,base+16))
           <<" repeat_count="<<pad_wire_i32(bytes,base+20)
           <<" sticks="<<int(pad_wire_i8(bytes,base+24))<<','<<int(pad_wire_i8(bytes,base+25))
           <<','<<int(pad_wire_i8(bytes,base+26))<<','<<int(pad_wire_i8(bytes,base+27))
           <<" analog="<<unsigned(bytes[base+28])<<','<<unsigned(bytes[base+29])
           <<','<<unsigned(bytes[base+30])<<','<<unsigned(bytes[base+31])
           <<" normalized_bits="<<normalized.str()<<" finite="<<finite_count<<"/8"
           <<" cross_dir="<<unsigned(bytes[base+64])<<" err="<<int(pad_wire_i8(bytes,base+65))<<'\n';
 }
 std::cerr.flush();
}
std::string stream_name(MeleeWebAudio* audio){
 const char* path=melee_web_audio_stream_path(audio);if(!path)return {};
 std::string result(path);const auto slash=result.find_last_of("/\\");return slash==std::string::npos?result:result.substr(slash+1);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
void run_stadium_yakumono_exchange_control()
{
    check(melee_web_stadium_c1_yakumono_exchange_baseline_empty() &&
              melee_web_stadium_c1_stage_object_failures() ==
                  MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE,
          "Asset-free exchange control must start before source Ground/GObj construction");
    char snapshot_error[160]{};
    MeleeWebStadiumC1StageInfoSnapshot* const stage_snapshot =
        melee_web_stadium_c1_stage_info_snapshot_begin(
            snapshot_error, sizeof(snapshot_error));
    check(stage_snapshot != nullptr, snapshot_error);
    std::array<unsigned char, sizeof(HSD_GObjLibInitData)> init_before{};
    std::array<unsigned char, sizeof(HSD_GObj_804CE3E4)> dispatch_before{};
    std::memcpy(init_before.data(), &HSD_GObjLibInitData,
                sizeof(HSD_GObjLibInitData));
    std::memcpy(dispatch_before.data(), &HSD_GObj_804CE3E4,
                sizeof(HSD_GObj_804CE3E4));
    const HSD_GObjList* entities_before = HSD_GObj_Entities;
    GObjFunc* funcs_before = HSD_GObj_804D7810;
    HSD_GObj* callback_a_before = HSD_GObj_804D7814;
    HSD_GObj* callback_b_before = HSD_GObj_804D7818;
    HSD_GObj* callback_c_before = HSD_GObj_804D781C;
    HSD_GObj** object_lists_before = HSD_GObj_804D7820;
    HSD_GObj** gx_lists_before = HSD_GObjGXLinkHead;
    HSD_GObj** low_lists_before = plinklow_gobjs;
    HSD_GObjProc* proc_a_before = HSD_GObj_804D7830;
    const s32 proc_a_count_before = HSD_GObj_804D7834;
    HSD_GObjProc* proc_b_before = HSD_GObj_804D7838;
    const s32 proc_b_count_before = HSD_GObj_804D783C;
    HSD_GObjProc** proc_lists_a_before = HSD_GObj_804D7840;
    HSD_GObjProc** proc_lists_b_before = HSD_GObj_804D7844;

    // Distinct, naturally aligned source-shaped sentinels; the exchange never dereferences them.
    MeleeWebStadiumYakumono a{};
    MeleeWebStadiumYakumono b{};
    void* original = melee_web_grpstadium_exchange_yakumono(&a);
    void* previous_b = melee_web_grpstadium_exchange_yakumono(&b);
    void* previous_a = melee_web_grpstadium_exchange_yakumono(&a);
    void* previous_restore = melee_web_grpstadium_exchange_yakumono(original);
    void* previous_verify = melee_web_grpstadium_exchange_yakumono(nullptr);

    check(original == nullptr && previous_b == &a && previous_a == &b &&
              previous_restore == &a && previous_verify == nullptr,
          "Stadium yakumono exchange did not preserve NULL/A/B/A/NULL pointer ownership");
    check(melee_web_stadium_c1_stage_info_snapshot_matches(stage_snapshot),
          "Stadium yakumono exchange changed byte-exact source StageInfo");
    check(melee_web_stadium_c1_stage_object_failures() ==
              MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE,
          "Stadium yakumono exchange changed source Stage/GObj owner state");
    check(HSD_GObj_Entities == entities_before &&
              HSD_GObj_804D7810 == funcs_before &&
              HSD_GObj_804D7814 == callback_a_before &&
              HSD_GObj_804D7818 == callback_b_before &&
              HSD_GObj_804D781C == callback_c_before &&
              HSD_GObj_804D7820 == object_lists_before &&
              HSD_GObjGXLinkHead == gx_lists_before &&
              plinklow_gobjs == low_lists_before &&
              HSD_GObj_804D7830 == proc_a_before &&
              HSD_GObj_804D7834 == proc_a_count_before &&
              HSD_GObj_804D7838 == proc_b_before &&
              HSD_GObj_804D783C == proc_b_count_before &&
              HSD_GObj_804D7840 == proc_lists_a_before &&
              HSD_GObj_804D7844 == proc_lists_b_before &&
              std::memcmp(init_before.data(), &HSD_GObjLibInitData,
                          sizeof(HSD_GObjLibInitData)) == 0 &&
              std::memcmp(dispatch_before.data(), &HSD_GObj_804CE3E4,
                          sizeof(HSD_GObj_804CE3E4)) == 0,
          "Stadium yakumono exchange changed original GObj registry/dispatch state");
    check(melee_web_stadium_c1_stage_info_snapshot_release_unchanged(
              stage_snapshot, snapshot_error, sizeof(snapshot_error)),
          snapshot_error);
    std::cout << "Stadium yakumono exchange asset-free control passed; pointer restored and "
                 "source StageInfo/GObj owner state unchanged\n";
}

void check_stadium_preflight_stage_empty()
{
    check(melee_web_stage_map_archives() == nullptr,
          "C1 context preflight found a pre-existing native stage map scope");
    const uint32_t failures = melee_web_stadium_c1_stage_state_failures();
    std::string detail = "C1 context preflight stage-state bridge reported mask=" +
                         std::to_string(failures) + ":";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE)
        detail += " HSD GObj list unavailable";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_MAP_GOBJ)
        detail += " StageInfo.map_gobjs occupied";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_INSTANCE)
        detail += " stage instance GObj present";
    if (failures & MELEE_WEB_STADIUM_C1_GROUND_GOBJ)
        detail += " Ground GObj present";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_ITEMS)
        detail += " StageInfo.itemdata published";
    if (failures & MELEE_WEB_STADIUM_C1_STAGE_LIGHTS)
        detail += " StageInfo.map_plit published";
    if (failures & MELEE_WEB_STADIUM_C1_ORDINARY_GRDAT_SLOT)
        detail += " ordinary grDatFiles slot occupied";
    check(failures == 0, detail.c_str());
}

struct GroundStoragePreCallFacts {
    MeleeWebSourceMemoryReadStatus memory_status{};
    MeleeWebSourceMemoryContext memory{};
    int os_heap = -1;
    int hsd_heap = -1;
    uint64_t world_generation = 0;
    bool world_active = false;
    bool memory_healthy = false;
    bool storage_available = false;
    MeleeWebGroundMapStorageView storage{};
    bool map_registry_empty = false;
    bool stage_gobj_list_empty = false;
    bool stage_markers_empty = false;
    bool dispatch_quiet = false;
};

bool ground_storage_pre_call_supported(const GroundStoragePreCallFacts& facts)
{
    return facts.memory_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
           facts.os_heap >= 0 && facts.os_heap == facts.hsd_heap &&
           facts.memory.source_heap_handle == facts.hsd_heap &&
           facts.memory.world_generation != 0 &&
           facts.memory.world_generation == facts.world_generation &&
           facts.world_active && facts.memory_healthy &&
           facts.storage_available && facts.storage.payload == nullptr &&
           facts.storage.requested_bytes == 64 &&
           facts.map_registry_empty && facts.stage_gobj_list_empty &&
           facts.stage_markers_empty && facts.dispatch_quiet;
}

void ground_storage_pre_call_controls()
{
    GroundStoragePreCallFacts valid{};
    valid.memory_status = MELEE_WEB_SOURCE_MEMORY_READ_OK;
    valid.memory = {7, 19, 23};
    valid.os_heap = 7;
    valid.hsd_heap = 7;
    valid.world_generation = 19;
    valid.world_active = true;
    valid.memory_healthy = true;
    valid.storage_available = true;
    valid.storage = {nullptr, 64};
    valid.map_registry_empty = true;
    valid.stage_gobj_list_empty = true;
    valid.stage_markers_empty = true;
    valid.dispatch_quiet = true;
    check(ground_storage_pre_call_supported(valid),
          "Ground storage pure gate rejected its supported synthetic context");
    auto rejected = [&](auto change, const char* message) {
        auto invalid = valid;
        change(invalid);
        check(!ground_storage_pre_call_supported(invalid), message);
    };
    rejected([](auto& f) { f.memory_status = MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE; },
             "Ground storage pure gate accepted inactive source memory");
    rejected([](auto& f) { f.hsd_heap += 1; },
             "Ground storage pure gate accepted a different HSD heap");
    rejected([](auto& f) { f.world_generation += 1; },
             "Ground storage pure gate accepted a stale world generation");
    rejected([](auto& f) { f.memory_healthy = false; },
             "Ground storage pure gate accepted unhealthy source memory");
    rejected([](auto& f) { f.storage_available = false; },
             "Ground storage pure gate accepted an already-owned buffer");
    rejected([](auto& f) { f.storage.payload = reinterpret_cast<void*>(1); },
             "Ground storage pure gate accepted a live buffer before begin");
    rejected([](auto& f) { f.storage.requested_bytes = 63; },
             "Ground storage pure gate accepted a changed authored request");
    rejected([](auto& f) { f.map_registry_empty = false; },
             "Ground storage pure gate accepted an occupied map registry");
    rejected([](auto& f) { f.stage_gobj_list_empty = false; },
             "Ground storage pure gate accepted an existing Stage GObj");
    rejected([](auto& f) { f.stage_markers_empty = false; },
             "Ground storage pure gate accepted pre-existing StageInfo marker owners");
    rejected([](auto& f) { f.dispatch_quiet = false; },
             "Ground storage pure gate accepted active object dispatch");
}

struct FtDeviceGlobalSnapshot {
    MeleeWebStadiumC1FtDeviceSnapshot* snapshot = nullptr;

    FtDeviceGlobalSnapshot() { capture(); }
    FtDeviceGlobalSnapshot(const FtDeviceGlobalSnapshot&) = delete;
    FtDeviceGlobalSnapshot& operator=(const FtDeviceGlobalSnapshot&) = delete;

    ~FtDeviceGlobalSnapshot()
    {
        if (snapshot != nullptr)
            melee_web_stadium_c1_ft_device_snapshot_release(snapshot);
    }

    void capture()
    {
        if (snapshot != nullptr) {
            check(melee_web_stadium_c1_ft_device_snapshot_release(snapshot),
                  "Typed ftDevice snapshot release failed before recapture");
            snapshot = nullptr;
        }
        snapshot = melee_web_stadium_c1_ft_device_snapshot_create();
        check(snapshot != nullptr,
              "Typed ftDevice source snapshot allocation failed");
    }

    void restore() const
    {
        check(melee_web_stadium_c1_ft_device_snapshot_restore(snapshot),
              "Typed ftDevice source snapshot restoration failed");
    }

    bool matches() const
    {
        return melee_web_stadium_c1_ft_device_snapshot_matches(snapshot) != 0;
    }

    std::array<const void*, 6> addresses() const
    {
        std::array<const void*, 6> result{};
        check(melee_web_stadium_c1_ft_device_snapshot_addresses(
                  snapshot, result.data(), result.size()) == result.size(),
              "Typed ftDevice snapshot did not expose its source identities");
        return result;
    }
};

bool ground_dispatch_quiet()
{
    return HSD_GObj_804D781C == nullptr && HSD_GObj_804D7818 == nullptr &&
           HSD_GObj_804D7814 == nullptr && HSD_GObj_804D7830 == nullptr &&
           HSD_GObj_804D7838 == nullptr && HSD_GObj_804CE3E4.flags == 0;
}

uint32_t source_stage_gobj_count()
{
    check(HSD_GObj_Entities != nullptr,
          "Ground map probe has no original GObj list owner");
    auto** heads = reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    const uint32_t used = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    uint32_t traversed = 0;
    uint32_t stage_count = 0;
    for (uint32_t link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link) {
        for (HSD_GObj* current = heads[link]; current;
             current = current->next) {
            check(++traversed <= used && current->p_link == link,
                  "Original GObj list is cyclic or disagrees with its pool owner");
            if (current->classifier == HSD_GOBJ_CLASS_STAGE) ++stage_count;
        }
    }
    return stage_count;
}

bool source_stage_registry_empty()
{
    for (size_t i = 0;
         i < melee_web_stadium_c1_ground_map_slot_count(); ++i)
        if (melee_web_stadium_c1_ground_map_slot(i) != nullptr) return false;
    return true;
}

bool source_jobj_owned_by(HSD_JObj* node, HSD_JObj* root)
{
    for (HSD_JObj* current = node; current;
         current = HSD_JObjGetParent(current))
        if (current == root) return true;
    return false;
}

std::vector<HSD_JObj*>
source_stage_marker_snapshot()
{
    std::vector<HSD_JObj*> result;
    const size_t count = melee_web_stadium_c1_ground_marker_slot_count();
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(static_cast<HSD_JObj*>(
            melee_web_stadium_c1_ground_marker_slot(i)));
    return result;
}

std::vector<void*>
source_stage_map_registry_snapshot()
{
    std::vector<void*> result;
    const size_t count = melee_web_stadium_c1_ground_map_slot_count();
    result.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.push_back(melee_web_stadium_c1_ground_map_slot(i));
    return result;
}

size_t source_stage_marker_count()
{
    return melee_web_stadium_c1_ground_marker_slot_count();
}

bool source_stage_markers_empty()
{
    const auto markers = source_stage_marker_snapshot();
    return std::all_of(markers.begin(), markers.end(),
                       [](HSD_JObj* marker) { return marker == nullptr; });
}

struct GroundStorageLease {
    GroundStoragePreCallFacts facts{};
    FtDeviceGlobalSnapshot devices_before{};
    FtDeviceGlobalSnapshot devices_after_begin{};
    const void* payload = nullptr;
    MeleeWebSourceMemoryAllocation allocation{};
    std::vector<uint8_t> bytes_after_begin;
    MeleeWebSourceMemoryContext context_after_competing_begin{};
    uint32_t stage_gobj_count_before = 0;
    bool begin_attempted = false;
    bool owned = false;
    bool ever_owned = false;
    bool devices_restored = false;
    bool finished = false;

    void begin()
    {
        check(!begin_attempted && !owned && !finished,
              "Ground storage lease begin was requested more than once");
        ground_storage_pre_call_controls();
        facts.memory_status =
            melee_web_source_memory_context_read(&facts.memory);
        const auto world = melee_web_gameplay_stats();
        facts.os_heap = __OSCurrHeap;
        facts.hsd_heap = HSD_GetHeap();
        facts.world_generation = world.generation;
        facts.world_active = melee_web_gameplay_world_exists() != 0;
        facts.memory_healthy = melee_web_source_memory_healthy() != 0;
        facts.storage_available = melee_web_ground_map_storage_available() != 0;
        check(melee_web_ground_map_storage_read(&facts.storage) != 0,
              "Ground storage preflight cannot observe its original owner");
        facts.map_registry_empty = source_stage_registry_empty();
        stage_gobj_count_before = source_stage_gobj_count();
        facts.stage_gobj_list_empty = stage_gobj_count_before == 0;
        facts.stage_markers_empty = source_stage_markers_empty();
        facts.dispatch_quiet = ground_dispatch_quiet();
        check(melee_web_stage_map_archives() == nullptr,
              "Ground storage preflight found a pre-existing native stage map scope");
        check(ground_storage_pre_call_supported(facts),
              "Ground storage owner preconditions are not satisfied");

        begin_attempted = true;
        if (melee_web_ground_map_storage_begin()) {
            owned = true;
            ever_owned = true;
        } else {
            MeleeWebGroundMapStorageView failed_view{};
            if (!melee_web_ground_map_storage_read(&failed_view)) {
                std::cerr << "Ground storage begin failed without a readable owner state; preserving it\n";
                std::abort();
            }
            owned = failed_view.payload != nullptr;
            ever_owned = owned;
            check(0, "Original Ground storage begin did not acquire its owner");
        }

        MeleeWebGroundMapStorageView storage{};
        check(melee_web_ground_map_storage_read(&storage) &&
                  storage.payload != nullptr && storage.requested_bytes == 64,
              "Original Ground storage accessor disagrees with its authored request");
        check(storage.requested_bytes <= UINT32_MAX,
              "Ground source allocation request exceeds observer representation");
        payload = storage.payload;
        const auto lease_status = melee_web_source_memory_allocation_read(
            payload, &allocation);
        check(melee_web::test::stadium_buffer::exact_new_allocation_supported(
                  lease_status, allocation, facts.memory,
                  static_cast<uint32_t>(storage.requested_bytes)) &&
                  allocation.source_heap_handle == facts.hsd_heap &&
                  allocation.world_generation == facts.world_generation,
              "Ground storage allocation identity differs from its exact live source owner");
        const auto* storage_bytes = static_cast<const uint8_t*>(storage.payload);
        check(std::all_of(storage_bytes,
                          storage_bytes + storage.requested_bytes,
                          [](uint8_t value) { return value == 0; }),
              "Original Ground storage bytes were not zeroed by Ground_801C0378");

        devices_after_begin.capture();
        const auto world_after_begin = melee_web_gameplay_stats();
        MeleeWebSourceMemoryContext before_competing_begin{};
        check(melee_web_source_memory_context_read(&before_competing_begin) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK,
              "Ground storage context became unavailable after original begin");
        bytes_after_begin.assign(storage_bytes,
                                 storage_bytes + storage.requested_bytes);
        check(melee_web_ground_map_storage_begin() == 0,
              "Competing Ground storage begin did not reject an owned buffer");
        MeleeWebGroundMapStorageView competing_view{};
        MeleeWebSourceMemoryAllocation competing_lease{};
        check(melee_web_ground_map_storage_read(&competing_view) &&
                  competing_view.payload == storage.payload &&
                  competing_view.requested_bytes == storage.requested_bytes &&
                  melee_web_source_memory_context_read(
                      &context_after_competing_begin) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  context_after_competing_begin.source_heap_handle ==
                      before_competing_begin.source_heap_handle &&
                  context_after_competing_begin.world_generation ==
                      before_competing_begin.world_generation &&
                  context_after_competing_begin.allocation_generation_watermark ==
                      before_competing_begin.allocation_generation_watermark &&
                  melee_web_source_memory_allocation_read(
                      storage.payload, &competing_lease) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  competing_lease.live == 1 &&
                  competing_lease.allocation_generation ==
                      allocation.allocation_generation &&
                  std::equal(bytes_after_begin.begin(), bytes_after_begin.end(),
                             storage_bytes) &&
                  devices_after_begin.matches() &&
                  melee_web_gameplay_stats().generation ==
                      world_after_begin.generation &&
                  melee_web_gameplay_stats().ticks == world_after_begin.ticks,
              "Competing Ground storage begin changed its live owner or source state");
    }

    void end()
    {
        if (!begin_attempted || finished) return;
        MeleeWebSourceMemoryContext before_free_context{};
        auto before_free_context_status =
            MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
        if (owned) {
            if (melee_web_stadium_c1_ground_map_lookup(1) != nullptr ||
                !source_stage_registry_empty() ||
                source_stage_gobj_count() != stage_gobj_count_before ||
                !source_stage_markers_empty()) {
                std::cerr << "Ground storage end refused while source map owners remain; preserving Ground storage\n";
                std::abort();
            }
            MeleeWebGroundMapStorageView before_free_storage{};
            MeleeWebSourceMemoryAllocation before_free_allocation{};
            before_free_context_status =
                melee_web_source_memory_context_read(&before_free_context);
            const auto before_free_allocation_status =
                melee_web_source_memory_allocation_read(
                    payload, &before_free_allocation);
            const auto world_before_free = melee_web_gameplay_stats();
            check(melee_web_ground_map_storage_read(&before_free_storage) &&
                      before_free_storage.payload == payload &&
                      before_free_storage.requested_bytes ==
                          allocation.requested_bytes &&
                      before_free_context_status ==
                          MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      before_free_context.source_heap_handle ==
                          facts.memory.source_heap_handle &&
                      before_free_context.world_generation ==
                          facts.memory.world_generation &&
                      melee_web::test::stadium_buffer::
                          exact_live_allocation_matches_context(
                          before_free_allocation_status, before_free_allocation,
                          before_free_context, allocation.requested_bytes,
                          allocation.allocation_generation) &&
                      __OSCurrHeap == facts.os_heap &&
                      HSD_GetHeap() == facts.hsd_heap &&
                      world_before_free.generation == facts.world_generation &&
                      melee_web_gameplay_world_exists() &&
                      melee_web_source_memory_healthy(),
                  "Ground storage end refused a changed exact live lease or owner context");
            if (!melee_web_ground_map_storage_end()) {
                std::cerr << "Ground storage end refused; preserving the live source owner\n";
                std::abort();
            }
            owned = false;
            devices_before.restore();
            devices_restored = true;
        } else if (!devices_restored) {
            devices_before.restore();
            devices_restored = true;
        }
        finished = true;
        if (!ever_owned) return;

        MeleeWebGroundMapStorageView ended_storage{};
        MeleeWebSourceMemoryAllocation retired_lease{};
        MeleeWebSourceMemoryContext after_end{};
        const auto retired_lease_status =
            melee_web_source_memory_allocation_read(payload, &retired_lease);
        const auto after_end_status =
            melee_web_source_memory_context_read(&after_end);
        const auto world_after_end = melee_web_gameplay_stats();
        check(melee_web_ground_map_storage_available() &&
                  melee_web_ground_map_storage_read(&ended_storage) &&
                  ended_storage.payload == nullptr &&
                  ended_storage.requested_bytes ==
                      allocation.requested_bytes &&
                  melee_web::test::stadium_buffer::
                      exact_retired_allocation_supported(
                      retired_lease_status, retired_lease,
                      before_free_context_status, before_free_context,
                      after_end_status, after_end) &&
                  after_end.source_heap_handle ==
                      facts.memory.source_heap_handle &&
                  after_end.world_generation == facts.memory.world_generation &&
                  __OSCurrHeap == facts.os_heap &&
                  HSD_GetHeap() == facts.hsd_heap &&
                  world_after_end.generation == facts.world_generation &&
                  melee_web_gameplay_world_exists() &&
                  devices_before.matches() && melee_web_source_memory_healthy(),
              "Ground storage end did not retire its exact lease and restore typed devices");
    }
};

melee_web::test::stadium_ground::SourceMarkerSelection
checked_ground_source_markers(
    const std::shared_ptr<const melee_web::DatArchive>& archive,
    const melee_web::DatStage& stage,
    const melee_web::DatStageEntry& map_entry,
    std::vector<uint16_t>& row_joint_indices)
{
    using melee_web::test::stadium_ground::SourceMarkerRow;
    check(archive && stage.joint_reference_table.count != 0 &&
              stage.joint_reference_table.data_offset.has_value(),
          "Stadium source marker reference table has no bounded authored rows");
    check(map_entry.joint_offset.has_value() &&
              map_entry.joint_indices.element_bytes == sizeof(uint16_t) &&
              (!map_entry.joint_indices.count ||
               map_entry.joint_indices.data_offset.has_value()),
          "Stadium map1 source joint-index row is incomplete");
    const uint32_t selected_root = *map_entry.joint_offset;
    std::vector<SourceMarkerRow> rows;
    rows.reserve(stage.joint_reference_table.count);
    for (uint32_t row_index = 0;
         row_index < stage.joint_reference_table.count; ++row_index) {
        const uint32_t marker_row =
            *stage.joint_reference_table.data_offset + row_index * 12;
        const auto marker_root = archive->pointer(marker_row, 64);
        check(marker_root.has_value(),
              "Stadium marker row has no bounded authored root pointer");
        const auto root_entry = std::find_if(
            stage.entries.begin(), stage.entries.end(),
            [&](const auto& entry) {
                return entry.joint_offset &&
                       *entry.joint_offset == *marker_root;
            });
        check(root_entry != stage.entries.end(),
              "Stadium marker root does not resolve to an authored stage entry");
        melee_web::DatNativeJoint joint_owner(archive, *marker_root);
        const auto& graph = joint_owner.graph();
        check(graph.joint_count != 0 && graph.root == 0 &&
                  graph.joints[0].source_offset == *marker_root,
              "Stadium marker row root differs from its checked source joint graph");

        const int32_t signed_pair_count =
            std::bit_cast<int32_t>(archive->be32(marker_row + 8));
        check(signed_pair_count >= 0,
              "Stadium marker row has a negative source signed pair count");
        const uint32_t pair_count = static_cast<uint32_t>(signed_pair_count);
        const uint64_t pair_bytes = uint64_t(pair_count) * 4;
        check(pair_bytes <= std::numeric_limits<size_t>::max(),
              "Stadium marker pair extent exceeds the host archive size type");
        std::optional<uint32_t> pairs;
        if (pair_count)
            pairs = archive->pointer(marker_row + 4,
                                     static_cast<size_t>(pair_bytes));
        check(pair_count == 0 || pairs.has_value(),
              "Stadium marker row pairs are not bounded by the checked archive");

        SourceMarkerRow row;
        row.root_offset = *marker_root;
        row.bindings.reserve(pair_count);
        for (uint32_t i = 0; i < pair_count; ++i) {
            const uint16_t joint_index = archive->be16(*pairs + i * 4);
            const uint16_t marker_id = archive->be16(*pairs + i * 4 + 2);
            check(joint_index < graph.joint_count &&
                      marker_id < source_stage_marker_count(),
                  "Stadium marker pair exceeds its own graph or actual marker-slot bound");
            row.bindings.push_back({joint_index, marker_id});
        }
        rows.push_back(std::move(row));
    }

    melee_web::DatNativeJoint map_joint_owner(archive, selected_root);
    const auto& map_graph = map_joint_owner.graph();
    check(map_graph.joint_count != 0 && map_graph.root == 0 &&
              map_graph.joints[0].source_offset == selected_root,
          "Stadium map1 root differs from its checked source joint graph");
    row_joint_indices.reserve(map_entry.joint_indices.count);
    for (uint32_t i = 0; i < map_entry.joint_indices.count; ++i) {
        const uint32_t at = *map_entry.joint_indices.data_offset +
                            i * map_entry.joint_indices.element_bytes;
        const int16_t index = std::bit_cast<int16_t>(archive->be16(at));
        check(index >= 0 && static_cast<uint16_t>(index) < map_graph.joint_count,
              "Stadium map1 source joint index is outside its checked joint graph");
        row_joint_indices.push_back(static_cast<uint16_t>(index));
    }
    return melee_web::test::stadium_ground::select_source_markers_for_root(
        rows, selected_root);
}

void run_stadium_ground_map1_owner(
    const std::shared_ptr<const melee_web::DatArchive>& archive,
    melee_web::DatNativeMap& map_owner,
    GroundStorageLease& storage_scope)
{
    constexpr int map_id = 1;
    namespace screen = melee_web::test::stadium_screen;
    check(archive && map_owner.map_head() != nullptr,
          "Ground map1 owner probe lost its retained C0 archive/map owner");
    check(storage_scope.owned && storage_scope.payload != nullptr &&
              storage_scope.allocation.live == 1,
          "Ground map1 constructor requires the already-owned source storage lease");

    const melee_web::DatStage source_stage(*archive);
    check(map_id < source_stage.entry_table.count &&
              map_id < source_stage.entries.size() &&
              map_id < melee_web_stadium_c1_ground_map_slot_count(),
          "Stadium map1 is outside an authored map or StageInfo table bound");
    const auto* ownership =
        melee_web::test::stadium_profile_data().map_ownership;
    check(ownership &&
              ownership->resident_entry_ids &&
              ownership->resident_entry_count != 0 &&
              std::find(ownership->resident_entry_ids,
                        ownership->resident_entry_ids +
                            ownership->resident_entry_count,
                        static_cast<uint32_t>(map_id)) !=
                  ownership->resident_entry_ids +
                      ownership->resident_entry_count,
          "C0 Stadium contract does not retain map1 in this archive");
    const auto& map_entry = source_stage.entries[map_id];
    check(map_entry.index == map_id && map_entry.joint_offset.has_value() &&
              stadium_screen_map_entry_joint(map_owner.map_head(), map_id) != nullptr,
          "Original Ground map1 has no resident source joint root");
    check(map_entry.collision_bindings.count == 0,
          "Stadium map1 has authored collision bindings; original removal is unsafe");
    MeleeWebStadiumC1GroundStageProfile stage_profile{};
    check(melee_web_stadium_c1_ground_map_profile(map_id, &stage_profile) &&
              stage_profile.grkind == Gr_Kind_PStadium &&
              stage_profile.callback_row_present &&
              stage_profile.callback_flags_b2 == 0,
          "Stadium map1 callback row enables a secondary camera");
    check(stage_profile.joint_table_present,
          "Stadium source collision joint table has no owner");
    check(!stage_profile.collision_row_present,
          "Stadium authored GrJoint table contains a map1 collision row");

    std::vector<uint16_t> row_joint_indices;
    const auto marker_selection = checked_ground_source_markers(
        archive, source_stage, map_entry, row_joint_indices);
    const auto& source_markers = marker_selection.map_bindings;
    check(marker_selection.matched_row_count == 0 && source_markers.empty(),
          "Bounded map1 probe requires no authored marker row for its exact root");
    check(melee_web_stadium_c1_stage_object_failures() == 0 &&
              source_stage_registry_empty() && source_stage_gobj_count() == 0,
          "Ground map1 preflight found an existing source Stage GObj owner");

    const auto world = melee_web_gameplay_stats();
    const auto stage_registry_before = source_stage_map_registry_snapshot();
    const auto stage_markers_before = source_stage_marker_snapshot();
    check(std::all_of(stage_markers_before.begin(), stage_markers_before.end(),
                      [](HSD_JObj* marker) { return marker == nullptr; }),
          "Ground map1 constructor refused pre-existing StageInfo marker owners");
    const auto classes_before = screen::live_class_counts();
    const auto pools_before = screen::live_pool_counts();
    const uint32_t gobj_used_before = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    const uint32_t gobjproc_used_before =
        HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
    const uint32_t stage_gobj_count_before = source_stage_gobj_count();
    check(stage_gobj_count_before == storage_scope.stage_gobj_count_before,
          "E8 source setup changed the pre-begin Stage GObj baseline");
    const int scheduler_cycle_before = HSD_GObj_804D783C;
    const uint64_t gameplay_ticks_before = world.ticks;

    HSD_GObj* map_object = nullptr;
    auto cleanup = [&]() {
        if (map_object == nullptr)
            map_object = static_cast<HSD_GObj*>(
                melee_web_stadium_c1_ground_map_lookup(map_id));
        if (map_object != nullptr) {
            MeleeWebStadiumC1GroundMapObjectView ground_view{};
            if (map_object->classifier != HSD_GOBJ_CLASS_STAGE ||
                !melee_web_stadium_c1_ground_map_object_view(
                    map_object->user_data, &ground_view) ||
                ground_view.map_id != map_id) {
                std::cerr << "Ground map1 teardown refused to remove an unowned object\n";
                std::abort();
            }
            if (!melee_web_stadium_c1_ground_map_remove(map_object)) {
                std::cerr << "Ground map1 original removal rejected its owned object\n";
                std::abort();
            }
            map_object = nullptr;
        }
        if (melee_web_stadium_c1_ground_map_lookup(map_id) != nullptr ||
            !source_stage_registry_empty() ||
            source_stage_gobj_count() != stage_gobj_count_before) {
            std::cerr << "Ground map1 teardown left a source Stage GObj owner; preserving Ground storage\n";
            std::abort();
        }
    };

    try {
        MeleeWebGroundMapStorageView storage{};
        check(melee_web_ground_map_storage_read(&storage) &&
                  storage.payload == storage_scope.payload &&
                  storage.requested_bytes == 64,
              "Original Ground storage owner changed before map1 construction");
        MeleeWebSourceMemoryContext before_constructor{};
        MeleeWebSourceMemoryAllocation live_lease{};
        check(melee_web_source_memory_context_read(&before_constructor) ==
                  MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  before_constructor.source_heap_handle ==
                      storage_scope.facts.memory.source_heap_handle &&
                  before_constructor.world_generation ==
                      storage_scope.facts.memory.world_generation &&
                  melee_web_source_memory_allocation_read(
                      storage_scope.payload, &live_lease) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                  live_lease.live == 1 &&
                  live_lease.allocation_generation ==
                      storage_scope.allocation.allocation_generation &&
                  live_lease.requested_bytes == storage.requested_bytes &&
                  __OSCurrHeap == storage_scope.facts.os_heap &&
                  HSD_GetHeap() == storage_scope.facts.hsd_heap &&
                  melee_web_gameplay_world_exists() &&
                  melee_web_source_memory_healthy(),
              "E8 preparation changed the original Ground storage lease context");
        const auto* storage_bytes =
            static_cast<const uint8_t*>(storage.payload);
        check(std::all_of(storage_bytes,
                          storage_bytes + storage.requested_bytes,
                          [](uint8_t value) { return value == 0; }),
              "Original Ground storage bytes were not zeroed by Ground_801C0378");

        check(source_stage_registry_empty() &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  ground_dispatch_quiet(),
              "Ground storage preparation changed Stage GObj or dispatch state");
        map_object = static_cast<HSD_GObj*>(
            melee_web_stadium_c1_ground_map_create(map_id));
        check(map_object != nullptr &&
                  melee_web_stadium_c1_ground_map_lookup(map_id) == map_object &&
                  map_object->classifier == HSD_GOBJ_CLASS_STAGE &&
                  map_object->user_data != nullptr && map_object->hsd_obj != nullptr,
              "Original Ground map1 constructor did not publish its exact source object");
        MeleeWebStadiumC1GroundMapObjectView ground_view{};
        check(melee_web_stadium_c1_ground_map_object_view(
                  map_object->user_data, &ground_view) &&
                  ground_view.map_id == map_id &&
                  ground_view.gobj == map_object &&
                  ground_view.camera == nullptr && map_object->render_cb == nullptr &&
                  map_object->gx_link == HSD_GOBJ_GXLINK_NONE,
              "Original Ground map1 object has a different owner, camera, or render link");
        check(source_stage_gobj_count() == stage_gobj_count_before + 1 &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before + 1 &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before + 2,
              "Ground map1 constructor changed the source Stage GObj/process counts unexpectedly");
        check(map_object->proc != nullptr && map_object->proc->child != nullptr &&
                  map_object->proc->child->child == nullptr &&
                  map_object->proc->gobj == map_object &&
                  map_object->proc->child->gobj == map_object &&
                  map_object->proc->on_invoke != nullptr &&
                  map_object->proc->child->on_invoke != nullptr,
              "Original Ground generic processes were not registered exactly once");

        HSD_JObj* const loaded_root =
            static_cast<HSD_JObj*>(map_object->hsd_obj);
        for (uint16_t joint_index : row_joint_indices) {
            HSD_JObj* const indexed = static_cast<HSD_JObj*>(
                melee_web_stadium_c1_ground_map_joint(map_object,
                                                      joint_index));
            check(indexed != nullptr && source_jobj_owned_by(indexed, loaded_root),
                  "Original Ground map1 row joint index did not resolve in its loaded JObj owner");
        }
        for (size_t i = 0; i < source_stage_marker_count(); ++i) {
            auto* current_marker = static_cast<HSD_JObj*>(
                melee_web_stadium_c1_ground_marker_slot(i));
            check(current_marker == stage_markers_before[i],
                  "Ground map1 changed the source StageInfo x280 marker baseline");
        }
        check(source_stage_markers_empty() &&
                  source_stage_marker_snapshot() == stage_markers_before,
              "Ground map1 changed the empty StageInfo x280 marker baseline");
        check(HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Ground generic process or stage callback dispatched during construction");

        check(melee_web_stadium_c1_ground_map_remove(map_object),
              "Original Ground map1 removal rejected its owned object");
        map_object = nullptr;
        check(source_stage_registry_empty() &&
                  source_stage_map_registry_snapshot() == stage_registry_before &&
                  source_stage_marker_snapshot() == stage_markers_before &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before &&
                  HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Original Ground map1 removal did not restore registry/marker/process state");
        cleanup();
        storage_scope.end();
        check(storage_scope.finished && storage_scope.devices_restored &&
                  storage_scope.devices_before.matches() &&
                  source_stage_map_registry_snapshot() == stage_registry_before &&
                  source_stage_marker_snapshot() == stage_markers_before &&
                  source_stage_gobj_count() == stage_gobj_count_before &&
                  HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_used_before &&
                  HSD_ObjAllocGetUsing(&gobjproc_alloc_data) ==
                      gobjproc_used_before &&
                  screen::live_class_counts() == classes_before &&
                  screen::live_pool_counts() == pools_before &&
                  HSD_GObj_804D783C == scheduler_cycle_before &&
                  melee_web_gameplay_stats().ticks == gameplay_ticks_before &&
                  ground_dispatch_quiet(),
              "Ground map1 owner cleanup did not restore its exact source baselines");

        const auto device_addresses =
            storage_scope.devices_before.addresses();
        std::cout << "{\"probe\":\"stadium-ground-map1-owner\","
                     "\"scope\":\"one original Ground_GetStageGObj(1)/Ground_801C4A08 lifetime\","
                     "\"ground_buffer\":\""
                  << hex64(reinterpret_cast<uintptr_t>(storage_scope.payload))
                  << "\",\"requested_bytes\":" << storage.requested_bytes
                  << ",\"source_heap\":"
                  << storage_scope.allocation.source_heap_handle
                  << ",\"world_generation\":"
                  << storage_scope.allocation.world_generation
                  << ",\"allocation_generation\":"
                  << storage_scope.allocation.allocation_generation
                  << ",\"ft_device_owner_ids\":[\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[0]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[1]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[2]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[3]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[4]))
                  << "\",\""
                  << hex64(reinterpret_cast<uintptr_t>(device_addresses[5]))
                  << "\"],\"authored_marker_pair_count\":"
                  << marker_selection.authored_pair_count
                  << ",\"map1_matched_marker_pair_count\":"
                  << source_markers.size()
                  << ",\"stage_info_marker_baseline_empty\":true"
                  << ",\"map1_joint_indices\":" << row_joint_indices.size()
                  << ",\"map_id\":1,\"device_bytes_restored\":true,"
                     "\"buffer_retired\":true,\"callback_dispatch\":false,"
                     "\"proc_ticks\":0,\"rendered\":false,"
                     "\"single_constructor_removal\":true}\n";
    } catch (...) {
        try {
            cleanup();
        } catch (...) {
            std::abort();
        }
        throw;
    }
}

melee_web::RuntimeFiles exact_stadium_runtime_union(
    const melee_web::RuntimeFiles& menu_files,
    const std::vector<std::string>& selected_names,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir)
{
    melee_web::RuntimeFiles result = menu_files;
    std::set<std::string, std::less<>> expected_names;
    for (const auto& [name, _] : menu_files) expected_names.insert(name);
    for (const auto& name : selected_names) {
        expected_names.insert(name);
        if (result.contains(name)) continue;

        const std::filesystem::path menu_path = menu_dir / name;
        const std::filesystem::path game_path = game_dir / name;
        const auto path = std::filesystem::is_regular_file(menu_path)
                              ? menu_path
                              : game_path;
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error(
                "Missing exact C1 source RuntimeFiles entry: " + name);
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error(
                "Cannot read exact C1 source RuntimeFiles entry: " + name);
        std::vector<std::uint8_t> bytes(
            (std::istreambuf_iterator<char>(input)), {});
        if (input.bad())
            throw std::runtime_error(
                "Cannot finish reading exact C1 source RuntimeFiles entry: " +
                name);
        result.emplace(name, std::move(bytes));
    }

    check(result.size() == expected_names.size(),
          "C1 reopened RuntimeFiles is not the exact menu/selection union");
    for (const auto& [name, _] : result)
        check(expected_names.contains(name),
              "C1 reopened RuntimeFiles contains an unselected extra entry");
    return result;
}

// The selected handoff remains immutable. This witness belongs to its live
// menu seed storage until the retained host is destroyed; only the immediate
// return from the authorized source OnInit can advance its expected value.
struct StadiumSelectionRngWitness {
    const uint32_t* owner;
    uint32_t initial;
    uint32_t expected_live;
    bool source_return_captured = false;
};

bool stadium_rng_witness_matches(const StadiumSelectionRngWitness& witness,
                                 const MeleeWebMenuMatchSelection& selected)
{
    return witness.owner != nullptr && seed_ptr == witness.owner &&
           selected.random_seed == witness.initial &&
           *witness.owner == witness.expected_live;
}

void check_stadium_rng_witness(const StadiumSelectionRngWitness& witness,
                               const MeleeWebMenuMatchSelection& selected,
                               const char* boundary)
{
    // Do not dereference the retained pointer after ownership changes.
    const bool owner_matches = witness.owner && seed_ptr == witness.owner;
    std::fprintf(stderr,
        "C1_SELECTION_RNG boundary=%s phase=%s owner=%p current_owner=%p "
        "initial=%u expected_live=%u live_available=%u live=%u\n",
        boundary, witness.source_return_captured ? "source-return" : "selected",
        static_cast<const void*>(witness.owner), static_cast<void*>(seed_ptr),
        witness.initial, witness.expected_live, owner_matches,
        owner_matches ? *witness.owner : 0);
    std::fflush(stderr);
    check(stadium_rng_witness_matches(witness, selected),
          "C1 selection RNG witness lost its initial seed, live owner or frozen phase value");
}

struct StadiumSelectionDifference {
    const char* field = nullptr;
    size_t byte_offset = 0;
    uint64_t expected = 0;
    uint64_t observed = 0;
};

StadiumSelectionDifference stadium_selection_difference(
    const MeleeWebMenuMatchSelection& observed,
    const MeleeWebMenuMatchSelection& expected, uint32_t expected_live_seed)
{
    auto bytes = [](const char* field, const void* actual, const void* initial,
                    size_t count) -> StadiumSelectionDifference {
        const auto* a = static_cast<const unsigned char*>(actual);
        const auto* b = static_cast<const unsigned char*>(initial);
        for (size_t i = 0; i < count; ++i)
            if (a[i] != b[i]) return {field, i, b[i], a[i]};
        return {};
    };
    if (auto d = bytes("start", &observed.start, &expected.start,
                       sizeof(expected.start)); d.field) return d;
    if (auto d = bytes("players", observed.players, expected.players,
                       sizeof(expected.players)); d.field) return d;
#define C1_COMPARE_FIELD(field) \
    if (observed.field != expected.field) \
        return {#field, 0, expected.field, observed.field}
    C1_COMPARE_FIELD(player_count);
    if (observed.random_seed != expected_live_seed)
        return {"random_seed", 0, expected_live_seed, observed.random_seed};
    C1_COMPARE_FIELD(hud_layout);
    C1_COMPARE_FIELD(unlocked_characters);
    C1_COMPARE_FIELD(unlocked_stages);
    C1_COMPARE_FIELD(save_profile_present);
    C1_COMPARE_FIELD(opening_demo);
#undef C1_COMPARE_FIELD
    return {};
}

// The active match compares its immutable copied handoff separately from its
// live RNG. This is never an export from the menu-owned source seed.
bool stadium_active_match_selection_witness_matches(
    const MeleeWebMenuMatchSelection& copied_selection,
    const MeleeWebMenuMatchSelection& current_selection,
    const StadiumSelectionRngWitness& active_match_rng)
{
    return !stadium_selection_difference(current_selection, copied_selection,
                                         copied_selection.random_seed).field &&
           current_selection.sudden_death == copied_selection.sudden_death &&
           stadium_rng_witness_matches(active_match_rng, copied_selection);
}

void run_stadium_selection_rng_controls()
{
    // Execute the linked original random.c body; no cloned LCG or map2 body.
    struct RestoreSeedOwner {
        uint32_t* prior = seed_ptr;
        ~RestoreSeedOwner() { seed_ptr = prior; }
    } restore;
    auto is_field = [](StadiumSelectionDifference difference, const char* field) {
        return difference.field && std::string_view(difference.field) == field;
    };
    for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
        uint32_t local_seed = 0x12345678u + lifetime;
        seed_ptr = &local_seed;
        MeleeWebMenuMatchSelection selected{};
        selected.random_seed = local_seed;
        StadiumSelectionRngWitness witness{seed_ptr, local_seed, local_seed};
        auto observed = selected;
        check(stadium_rng_witness_matches(witness, selected) &&
                  !stadium_selection_difference(observed, selected,
                                                witness.expected_live).field,
              "Selected-phase control rejected an unchanged selection");
        auto no_draw_return = witness;
        no_draw_return.source_return_captured = true;
        check(stadium_rng_witness_matches(no_draw_return, selected) &&
                  !stadium_selection_difference(observed, selected,
                                                no_draw_return.expected_live).field,
              "Source-return phase incorrectly required an RNG draw");
        (void)HSD_Randi(13);
        observed.random_seed = local_seed;
        check(local_seed != selected.random_seed &&
                  !stadium_rng_witness_matches(witness, selected) &&
                  is_field(stadium_selection_difference(
                      observed, selected, witness.expected_live), "random_seed"),
              "Actual source RNG draw did not reject the stale selected phase");
        witness.expected_live = local_seed;
        witness.source_return_captured = true;
        check(stadium_rng_witness_matches(witness, selected) &&
                  !stadium_selection_difference(observed, selected,
                                                witness.expected_live).field,
              "Frozen source-return control rejected the same owned live seed");
        uint32_t foreign_seed = local_seed;
        seed_ptr = &foreign_seed;
        check(!stadium_rng_witness_matches(witness, selected),
              "Same-value foreign RNG owner was accepted");
        seed_ptr = &local_seed;
        auto changed_initial = selected;
        changed_initial.random_seed ^= 1;
        check(!stadium_rng_witness_matches(witness, changed_initial),
              "Changed immutable selected seed was accepted");
        auto changed = observed;
        reinterpret_cast<unsigned char*>(&changed.start)[0] ^= 1;
        check(is_field(stadium_selection_difference(changed, selected,
                  witness.expected_live), "start"), "Start change was accepted");
        changed = observed;
        reinterpret_cast<unsigned char*>(changed.players)[0] ^= 1;
        check(is_field(stadium_selection_difference(changed, selected,
                  witness.expected_live), "players"), "Player change was accepted");
#define C1_REFUSE_FIELD(field) \
        changed = observed; changed.field ^= 1; \
        check(is_field(stadium_selection_difference(changed, selected, \
                  witness.expected_live), #field), "Selection field change was accepted")
        C1_REFUSE_FIELD(player_count);
        C1_REFUSE_FIELD(random_seed);
        C1_REFUSE_FIELD(hud_layout);
        C1_REFUSE_FIELD(unlocked_characters);
        C1_REFUSE_FIELD(unlocked_stages);
        C1_REFUSE_FIELD(save_profile_present);
        C1_REFUSE_FIELD(opening_demo);
#undef C1_REFUSE_FIELD
        (void)HSD_Randi(13);
        check(!stadium_rng_witness_matches(witness, selected),
              "An additional cleanup-phase RNG draw was accepted");
    }
    std::cout << "C1 asset-free original HSD_Randi and immutable selection/live-owner phase controls passed; two lifetimes and every compared field refused\n";
}

MeleeWebMenuMatchSelection check_stadium_selection_preserved(
    MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& expected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>&
        expected_baseline,
    const StadiumSelectionRngWitness* rng_witness = nullptr)
{
    if (rng_witness)
        check_stadium_rng_witness(*rng_witness, expected, "selection-export-before");
    check(host != nullptr &&
              melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY &&
              melee_web_menu_host_source_scene(host) == 0,
          "C1 context preflight changed the closed source SSS selection");
    MeleeWebMenuMatchSelection observed{};
    char error[256]{};
    check(melee_web_menu_host_stadium_c1a_selection(
              host, &observed, error, sizeof(error)), error);
    check(observed.start.rules.stkind == St_Kind_PStadium,
          "C1 selection export changed source-selected StKind 3");
    if (rng_witness)
        check_stadium_rng_witness(*rng_witness, expected, "selection-export-after");
    const auto difference = stadium_selection_difference(
        observed, expected, rng_witness ? rng_witness->expected_live
                                      : expected.random_seed);
    if (difference.field) {
        std::fprintf(stderr,
            "C1_SELECTION_DIFFERENCE field=%s byte_offset=%zu expected=%llu observed=%llu initial_seed=%u exported_seed=%u\n",
            difference.field, difference.byte_offset,
            static_cast<unsigned long long>(difference.expected),
            static_cast<unsigned long long>(difference.observed),
            expected.random_seed, observed.random_seed);
        std::fflush(stderr);
    }
    check(difference.field == nullptr,
          "C1 selection export changed a retained field or its frozen live RNG phase");
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> baseline{};
    check(melee_web_menu_host_snapshot_card_data(
              host, 1, baseline.data(), baseline.size(), error,
              sizeof(error)), error);
    check(baseline == expected_baseline,
          "C1 context preflight changed the retained save-owner baseline");
    return observed; // The actual checked host export, while its RNG is owned.
}
#endif
void run_results_source_smoke(const melee_web::RuntimeFiles& files,
                              MeleeWebMenuHost* host,
                              const MatchExitInfo& exit_info,
                              uint32_t& seed,
                              uint8_t input_bytes[MELEE_WEB_PAD_STATE_BYTES],const TwoHumanResultsExpectation* expected)
{
    ResultsMatchInfo result{};
    char error[256]{};
    const auto resets=*gmMainLib_GetMatchResetCounter();
    const auto stock_matches=*gmMainLib_GetStockMatchTotal();
    check(melee_web_menu_host_results_begin(host,&exit_info,seed,&result,error,sizeof(error)),error);
    const bool canceled=exit_info.match_end.outcome==OUTCOME_NO_CONTEST;
    check(*gmMainLib_GetMatchResetCounter()==resets+(canceled?1:0)&&
          *gmMainLib_GetStockMatchTotal()==stock_matches+(canceled?0:1),
          "Original VS exit did not update exactly one persistent result counter");
    check(std::memcmp(&result.match_end,&exit_info.match_end,sizeof(result.match_end))==0,
          "VS mode Results entry changed the completed MatchEnd");
    run_typed_results_source_smoke(files,host,result,seed,input_bytes,nullptr,expected);
}
void run_typed_results_source_smoke(const melee_web::RuntimeFiles& files,
    MeleeWebMenuHost* host,const ResultsMatchInfo& result,
    std::uint32_t& seed,std::uint8_t input_bytes[MELEE_WEB_PAD_STATE_BYTES],
    const MeleeWebPadState* initial_input,const TwoHumanResultsExpectation* expected)
{
    check(!expected||declared_two_human_results_valid(result.match_end,*expected),
          "Two-Human Results differs from its caller-declared outcome/player/winner/stocks");
    char error[256]{};
    std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> decoded(
        initial_input?nullptr:melee_web_pad_state_decode(input_bytes,MELEE_WEB_PAD_STATE_BYTES,error,sizeof(error)),
        melee_web_pad_state_free);
    check(initial_input||decoded!=nullptr,error);
    melee_web::GameplayResultsSession session(files,result,seed,initial_input?*initial_input:*decoded);
    melee_web_pad_state_capture(input_bytes);
    emit_native_bytes("typed_results_scene_entry_pad",input_bytes,MELEE_WEB_PAD_STATE_BYTES);
    check(!melee_web_menu_host_results_exit(host,error,sizeof(error)),
          "Mode exit was accepted before Results scene OnExit");
    float pcm[1068];unsigned audio_phase=0;
    auto tick=[&](const PADStatus pads[4]){
        session.tick(pads);
        audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
        check(melee_web_audio_render(session.audio(),pcm,count,error,sizeof(error)),error);
    };
    PADStatus neutral[4]{};neutral[2].err=neutral[3].err=-1;
    for(unsigned t=0;t<240;t++)tick(neutral);
    if(expected){
        emit_native_bytes("declared_results_entry_payload",&result,sizeof(result));
        std::cout<<"Declared Results session initial seed "<<seed<<'\n';
        unsigned input_ticks=0;
        auto observe=[&](const char* label){
            const auto& state=lbl_8046DBE8;
            std::cout<<"RESULTS_CONFIRMATION {\"boundary\":\""<<label
                <<"\",\"source_frame\":"<<session.source_frames()
                <<",\"post_neutral_input_ticks\":"<<input_ticks
                <<",\"phase\":"<<unsigned(state.x1)
                <<",\"stats_phase\":"<<unsigned(state.x0_23)
                <<",\"num_pages\":"<<unsigned(state.num_pages)
                <<",\"requested\":"<<(session.requested()?"true":"false")
                <<",\"live_seed\":"<<session.random_seed()<<",\"players\":[";
            for(unsigned port=0;port<4;++port){
                if(port)std::cout<<',';
                const auto& pad=HSD_PadCopyStatus[port];
                std::cout<<"{\"confirmed\":"<<unsigned(state.player_data[port].x0_0)
                    <<",\"page\":"<<unsigned(state.player_data[port].page)
                    <<",\"err\":"<<int(pad.err)<<",\"button\":"<<pad.button
                    <<",\"trigger\":"<<pad.trigger<<",\"last_button\":"<<pad.last_button<<'}';
            }
            std::cout<<"]}\n";
        };
        auto step=[&](int start_port){
            check(input_ticks<600,"Two-Human Results exceeded existing 600 tick confirmation bound");
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            if(start_port>=0)pads[start_port].button=PAD_BUTTON_START;
            tick(pads);++input_ticks;
        };
        auto flags=[&](bool p1,bool p2){
            check(bool(lbl_8046DBE8.player_data[0].x0_0)==p1&&
                  bool(lbl_8046DBE8.player_data[1].x0_0)==p2,
                  "Original per-Human Results confirmation flags diverged");
        };
        observe("presentation_after_240_neutral");
        check(lbl_8046DBE8.x1==2&&!session.requested(),
              "Original Results presentation was not phase 2 after neutral bound");
        // Presentation advance is distinct from each Human's later stats acknowledgement.
        step(0);observe("initial_P1_presentation_Start");step(-1);
        check(!(HSD_PadCopyStatus[0].trigger&PAD_BUTTON_START),
              "Neutral did not release presentation Start");
        while(input_ticks<480&&(lbl_8046DBE8.x1!=3||lbl_8046DBE8.x0_23!=2))step(-1);
        observe("stats_ready_before_confirmation");
        check(lbl_8046DBE8.x1==3&&lbl_8046DBE8.x0_23==2&&!session.requested(),
              "Original Results stats did not become ready within shared confirmation bound");
        flags(false,false);
        step(0);observe("P1_only_confirm");flags(true,false);
        check(!session.requested(),"P1-only confirmation incorrectly exited two-Human Results");
        step(-1);
        check(!(HSD_PadCopyStatus[0].trigger&PAD_BUTTON_START),"Neutral did not release P1 Start trigger");
        for(unsigned t=0;t<90;++t)step(-1);
        observe("P1_only_neutral_negative");flags(true,false);
        check(!session.requested(),"Connected unconfirmed P2 was silently acknowledged");
        step(0);observe("second_P1_toggles_off");flags(false,false);
        check(!session.requested(),"Unconfirmed Human Results requested exit");
        step(-1);observe("neutral_release");
        check(!(HSD_PadCopyStatus[0].trigger&PAD_BUTTON_START),"Neutral did not release second P1 Start");
        step(0);observe("P1_reconfirm");flags(true,false);
        step(-1);step(1);observe("P2_Start_all_confirmed");flags(true,true);
        check(lbl_8046DBE8.player_data[2].x0_0&&lbl_8046DBE8.player_data[3].x0_0,
              "Original NA slots did not participate in all-confirmed policy");
        while(input_ticks<600&&!session.requested())step(-1);
        observe("original_exit_requested");
    }else{
    for(unsigned t=0;t<600&&!session.requested();t++){
        PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
        // The original Results confirmation is Start; A changes stats pages.
        if(t%90==0)pads[0].button=pads[1].button=PAD_BUTTON_START;
        tick(pads);
    }
    }
    check(session.requested(),"Original Results scene did not request its source exit");
    session.exit_scene();
    check(melee_web_menu_host_results_exit(host,error,sizeof(error)),error);
    check(!melee_web_menu_host_results_exit(host,error,sizeof(error)),
          "Results mode exit ran twice");
    seed=session.random_seed();
    melee_web_pad_state_capture(input_bytes);
    session.close();
    check(melee_web_menu_host_results_end(host,seed,input_bytes,error,sizeof(error)),error);
    if(melee_web_menu_host_results_destination(host)==192){
        const MeleeWebPadState* retained=melee_web_menu_host_input(host);
        check(retained!=nullptr,"Results did not retain Prize PAD input");
        melee_web::GameplayPrizeSession prize(files,host,seed,*retained);
        check(!melee_web_menu_host_prize_exit(host,error,sizeof(error)),
              "Prize mode exited before source confirmation");
        for(unsigned t=0;t<3600&&!prize.requested();++t){
            PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
            if(t>=20&&t%20==0)pads[0].button=PAD_BUTTON_START;
            prize.tick(pads);
            audio_phase+=32000;const unsigned count=audio_phase/60;audio_phase%=60;
            check(melee_web_audio_render(prize.audio(),pcm,count,error,sizeof(error)),error);
        }
        check(prize.requested(),"Original Prize did not finish within the bounded confirmation script");
        prize.exit_scene();
        check(melee_web_menu_host_prize_exit(host,error,sizeof(error)),error);
        check(!melee_web_menu_host_prize_exit(host,error,sizeof(error)),"Prize mode exit ran twice");
        seed=prize.random_seed();melee_web_pad_state_capture(input_bytes);
        std::cout<<"Original Prize confirmed in "<<prize.source_frames()<<" source ticks\n";
        prize.close();
        check(melee_web_menu_host_prize_end(host,seed,input_bytes,error,sizeof(error)),error);
    }
}
void write_player(std::ostream& out,const PlayerInitData& player){
 const unsigned flags_c=(unsigned(player.rumble_enabled)<<7)|(unsigned(player.xC_b1)<<6)|
  (unsigned(player.xC_b2)<<5)|(unsigned(player.xC_b3)<<4)|(unsigned(player.vs_invisible)<<3)|
  (unsigned(player.xC_b5)<<2)|(unsigned(player.xC_b6)<<1)|unsigned(player.xC_b7);
 const unsigned flags_d=(unsigned(player.xD_b0)<<7)|(unsigned(player.xD_b1)<<6)|
  (unsigned(player.xD_b2)<<5)|(unsigned(player.xD_b3)<<4)|(unsigned(player.xD_b4)<<3)|
  (unsigned(player.xD_b5)<<2)|(unsigned(player.xD_b6)<<1)|unsigned(player.xD_b7);
 out<<"{\"ckind\":"<<int(player.ckind)<<",\"slot_type\":"<<unsigned(player.slot_type)
    <<",\"stocks\":"<<int(player.stocks)<<",\"color\":"<<unsigned(player.color)
    <<",\"slot\":"<<unsigned(player.slot)<<",\"spawn\":"<<int(player.x5)
    <<",\"spawn_direction\":"<<int(player.spawn_dir)<<",\"sub_color\":"<<unsigned(player.sub_color)
    <<",\"handicap\":"<<int(player.handicap)<<",\"team\":"<<unsigned(player.team)
    <<",\"nametag\":"<<unsigned(player.nametag)<<",\"flags_c\":"<<flags_c
    <<",\"flags_d\":"<<flags_d<<",\"cpu_kind\":"<<unsigned(player.cpu_kind)
    <<",\"cpu_level\":"<<unsigned(player.cpu_level)<<",\"damage_10\":"<<player.x10
    <<",\"damage_12\":"<<player.x12<<",\"hp\":"<<player.hp
    <<",\"attack_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.attack_ratio))
    <<"\",\"defense_ratio_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.defense_ratio))
    <<"\",\"model_scale_bits\":\""<<hex32(std::bit_cast<uint32_t>(player.model_scale))<<"\"}";
}
void write_selection(std::ostream& out,const MeleeWebMenuMatchSelection& selection){
 const auto& rules=selection.start.rules;
 out<<"{\"rules\":{\"match_kind\":"<<unsigned(rules.match_kind)
    <<",\"hud_layout\":"<<unsigned(rules.x0_3)
    <<",\"timer_enabled\":"<<(rules.timer_enabled?"true":"false")
    <<",\"timer_counts_up\":"<<(rules.timer_counts_up?"true":"false")
    <<",\"friendly_fire\":"<<(rules.friendly_fire?"true":"false")
    <<",\"is_stock\":"<<(rules.is_stock?"true":"false")
    <<",\"single_button\":"<<(rules.single_button?"true":"false")
    <<",\"disable_pausing\":"<<(rules.disable_pausing?"true":"false")
    <<",\"is_vs\":"<<(rules.is_vs?"true":"false")
    <<",\"is_teams\":"<<unsigned(rules.is_teams)<<",\"item_frequency\":"<<int(rules.xB)
    <<",\"stage_kind\":"<<rules.stkind<<",\"time_limit\":"<<rules.time_limit
    <<",\"item_mask\":\""<<hex64(rules.x20)<<"\",\"damage_ratio_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.x30))<<"\",\"game_speed_bits\":\""
    <<hex32(std::bit_cast<uint32_t>(rules.game_speed))<<"\"},\"players\":[";
 for(unsigned i=0;i<4;++i){if(i)out<<',';write_player(out,selection.start.players[i]);}
 out<<"]}";
}
class TransitionTrace {
 std::ofstream output;unsigned run_=0,index_=0;std::map<uint64_t,unsigned> epochs;
 unsigned epoch(MeleeWebAudio* audio){
  const uint64_t generation=melee_web_audio_generation(audio);auto found=epochs.find(generation);
  if(found!=epochs.end())return found->second;const unsigned result=epochs.size();epochs[generation]=result;return result;
 }
public:
 explicit TransitionTrace(const char* path,const char* source_revision,
                          const char* input_recipe){
  if(!path)return;const std::string revision=source_revision?source_revision:"";
  if(revision.size()!=40||revision.find_first_not_of("0123456789abcdef")!=std::string::npos)
   throw std::runtime_error("Transition trace requires a full lowercase source revision");
  output.open(path,std::ios::trunc);if(!output)throw std::runtime_error("Cannot open transition trace output");
  output<<"{\"record\":\"header\",\"schema\":\"melee-web-transition-trace\",\"version\":1,"
          "\"producer\":\"port\",\"game_revision\":\"GALE01r2\",\"source_revision\":\""
        <<revision<<"\",\"build_configuration\":\"browser-release\"";
  if(input_recipe)output<<",\"input_recipe\":\""<<input_recipe<<"\"";
  output<<"}\n";
 }
 void menu_selection(const StartMeleeData& raw,
                     const MeleeWebMenuMatchSelection& normalized,
                     const VsModeData& post_mode,
                     const uint8_t pad[MELEE_WEB_PAD_STATE_BYTES]){
  check(output.is_open(),"Menu selection diagnostic requires retained trace output");
  auto bytes=[&](const void* data,size_t size){
   const auto* value=static_cast<const uint8_t*>(data);
   const char digits[]="0123456789abcdef";
   output<<'"';for(size_t i=0;i<size;++i)
    output<<digits[value[i]>>4]<<digits[value[i]&15];output<<'"';
  };
  MeleeWebMenuMatchSelection raw_selection{};raw_selection.start=raw;
  output<<"{\"record\":\"menu_selection_diagnostic\",\"run\":"<<run_
        <<",\"boundary\":\"closed_original_sss\",\"rng\":"<<normalized.random_seed
        <<",\"raw\":";write_selection(output,raw_selection);
  output<<",\"normalized\":";write_selection(output,normalized);
  MeleeWebMenuMatchSelection post_selection{};post_selection.start=post_mode.start;
  output<<",\"post_vs_mode_start\":";write_selection(output,post_selection);
  output<<",\"post_vs_mode_hex\":";bytes(&post_mode,sizeof(post_mode));
  output<<",\"post_vs_start_hex\":";bytes(&post_mode.start,sizeof(post_mode.start));
  output<<",\"source_start_bytes_scope\":\"native ABI including pointers and padding\""
        <<",\"raw_start_hex\":";bytes(&raw,sizeof(raw));
  output<<",\"normalized_start_hex\":";bytes(&normalized.start,sizeof(normalized.start));
  output<<",\"pad_origin\":\"owned closed SSS host bank\",\"pad_hex\":";
  bytes(pad,MELEE_WEB_PAD_STATE_BYTES);
  output<<",\"player_count\":"<<normalized.player_count<<",\"compatibility_players\":[";
  for(unsigned i=0;i<normalized.player_count;++i){if(i)output<<',';
   const auto& player=normalized.players[i];
   output<<"{\"controller\":"<<player.controller<<",\"stocks\":"<<player.stocks
         <<",\"costume\":"<<player.costume<<",\"sub_color\":"<<player.sub_color<<'}';
  }
  output<<"]}\n";output.flush();
 }
 void begin_run(unsigned run){run_=run;index_=0;epochs.clear();}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 void sis_lease(const char* boundary, const MeleeWebRetiredSisLease* retired) {
  if(!output)return;
  const void* current_heap=HSD_SisLib_HeapOwner();
  MeleeWebSourceMemoryAllocation current{};
  const auto status=current_heap ? melee_web_source_memory_allocation_read(current_heap,&current)
                                : MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
  output<<"{\"record\":\"sis_lease\",\"boundary\":\""<<boundary
        <<"\",\"source_epoch\":"<<HSD_SisLib_HeapEpoch()
        <<",\"source_active\":"<<(HSD_SisLib_HeapActive()?"true":"false")
        <<",\"current_heap\":"<<reinterpret_cast<uintptr_t>(current_heap)
        <<",\"current_read_status\":"<<status
        <<",\"current_world\":"<<current.world_generation
        <<",\"current_allocation\":"<<current.allocation_generation
        <<",\"current_heap_handle\":"<<current.source_heap_handle
        <<",\"current_requested_bytes\":"<<current.requested_bytes
        <<",\"current_live\":"<<(current.live?"true":"false");
  if(retired)output<<",\"prior_heap\":"<<reinterpret_cast<uintptr_t>(retired->prior.heap)
        <<",\"prior_world\":"<<retired->prior.world_generation
        <<",\"prior_allocation\":"<<retired->prior.allocation_generation
        <<",\"prior_heap_handle\":"<<retired->prior.source_heap_handle
        <<",\"prior_requested_bytes\":"<<retired->requested_bytes
        <<",\"prior_source_epoch\":"<<retired->prior.source_epoch
        <<",\"retirement_verified\":"<<(retired->retirement_verified?"true":"false");
  output<<"}\n";output.flush();
 }
#endif

 void event(const char* name,MeleeWebAudio* audio,const char* route=nullptr,
            const MeleeWebMenuMatchSelection* selection=nullptr,const uint32_t* rng=nullptr){
  if(!output)return;const auto stream=stream_name(audio);
  output<<"{\"record\":\"event\",\"run\":"<<run_<<",\"index\":"<<index_++
        <<",\"event\":\""<<name<<"\",\"audio\":{\"active\":"
        <<(!stream.empty()?"true":"false")<<",\"owner_epoch\":"<<epoch(audio)
        <<",\"stream\":\""<<stream<<"\"}";
  if(route)output<<",\"route\":\""<<route<<"\"";
  const uint32_t* event_rng=rng?rng:seed_ptr;
  if(event_rng)output<<",\"rng\":"<<*event_rng;
  if(selection){
   output<<",\"selection\":";
   write_selection(output,*selection);
  }
  output<<"}\n";output.flush();
 }
};

void replay_start_marker(const char* marker, int value = -1) {
    std::cout << "{\"record\":\"b4_replay_start_marker\",\"marker\":\""
              << marker << "\"";
    if (value >= 0) std::cout << ",\"value\":" << value;
    std::cout << "}\n" << std::flush;
}

void run_v10_css_replay_start_prefix(const melee_web::RuntimeFiles& files,
                                     const char* recipe_path,
                                     TransitionTrace& trace) {
    std::ifstream input(recipe_path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open the exact MWRC v10 reducer recipe");
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
    auto recipe = melee_web::read_retail_replay(bytes);
    check(recipe.version == melee_web::kRetailReplayFighterVersion &&
              recipe.seed == 3336171383U && recipe.frames.size() == 50394 &&
              bytes.size() == 2241622 && recipe.initial_css && recipe.initial_input &&
              recipe.match_setups.size() == melee_web::kRetailReplayMaxMatchSetups &&
              !recipe.spans.empty() &&
              recipe.spans.front().scene == melee_web::kRetailReplayCss &&
              recipe.spans.front().first_frame == 0,
          "Reducer input is not the complete frozen MWRC v10 CSS-first recipe");
    replay_start_marker("full_v10_recipe_verified");

    char error[256]{};
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    std::unique_ptr<melee_web::GameplayMenuWorld> world;
    bool host_entered = false;
    auto best_effort_teardown = [&]() noexcept {
        if (host && host_entered) {
            char cleanup_error[256]{};
            (void)melee_web_menu_host_leave(host, 1, cleanup_error,
                                            sizeof(cleanup_error));
            host_entered = false;
        }
        if (world) {
            try { world->verify_immutable_archives(); } catch (...) {}
            try { world->close(); } catch (...) {}
            world.reset();
        }
        if (host) {
            char cleanup_error[256]{};
            (void)melee_web_menu_host_destroy(host, cleanup_error,
                                             sizeof(cleanup_error));
            host = nullptr;
        }
    };

    try {
        world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_apply_replay_context(
                  host, recipe.seed, recipe.pad_bytes.data(),
                  recipe.initial_css->css_data.data(),
                  recipe.initial_css->ko_counts.data(),
                  recipe.initial_css->game_rules.data(),
                  recipe.initial_css->save_data.data(), error, sizeof(error)),
              error);
        replay_start_marker("fresh_css_enter_begin");
        check(melee_web_menu_host_enter(host, world->audio(), error,
                                        sizeof(error)), error);
        host_entered = true;
        check(melee_web_menu_host_source_scene(host) ==
                  MELEE_WEB_MENU_HOST_SCENE_CSS,
              "Fresh reducer owner did not enter original CSS");
        trace.begin_run(0);
        trace.event("fresh_css_enter_complete", world->audio());
        replay_start_marker("fresh_css_enter_complete");

        replay_start_marker("retail_replay_session_initial_begin");
        melee_web::retail_replay_session_initial(recipe);
        std::cout << std::flush;
        replay_start_marker("retail_replay_session_initial_returned");

        float pcm[1068]{};
        unsigned audio_phase = 0;
        replay_start_marker("original_css_host_tick_begin");
        const int tick_result = melee_web_menu_host_tick(
            host, recipe.frames[0].pads.data(), error, sizeof(error));
        check(tick_result == 1 || tick_result == 3, error);
        trace.event("original_css_host_tick_returned", world->audio());
        replay_start_marker("original_css_host_tick_returned", tick_result);

        replay_start_marker("retail_replay_frame_css_begin");
        melee_web::retail_replay_frame(recipe, 0,
                                       melee_web::kRetailReplayCss);
        std::cout << std::flush;
        replay_start_marker("retail_replay_frame_css_returned");

        replay_start_marker("ordinary_audio_boundary_begin");
        audio_phase += 32000;
        const unsigned samples = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world->audio(), pcm, samples, error,
                                     sizeof(error)), error);
        trace.event("ordinary_audio_boundary_returned", world->audio());
        replay_start_marker("ordinary_audio_boundary_returned",
                            static_cast<int>(samples));

        replay_start_marker("source_teardown_begin");
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        host_entered = false;
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
        replay_start_marker("source_teardown_returned");
        std::cout << "{\"record\":\"b4_replay_start_prefix\","
                     "\"result\":\"bounded_one_frame_returned\","
                     "\"recipe_version\":10,\"recipe_frames\":50394,"
                     "\"source_frames_consumed\":1,\"draws\":0,"
                     "\"full_session_comparison\":false}\n" << std::flush;
    } catch (...) {
        best_effort_teardown();
        throw;
    }
}

void run_title_main_abort_smoke(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    float pcm[1068]{};
    const void* saved_scene_info = melee_web_current_scene_info();

    auto check_full_roster = [] {
        const uint16_t expected =
            static_cast<uint16_t>((1U << NUM_UNLOCKABLE_CHARACTERS) - 1U);
        check(expected == 0x07ff,
              "Pinned source unlock table no longer declares the existing 0x07ff roster");
        check(*gmMainLib_GetUnlockedCharactersBitmaskPtr() == expected,
              "Original fresh menu profile did not retain the all-unlocked character mask");
        for (int index = 0; index < NUM_UNLOCKABLE_CHARACTERS; ++index) {
            check(gm_IsCKindUnlocked(gm_GetCKindByUnlockIndex(index)),
                  "A source-unlocked character is unavailable to original CSS");
        }
    };

    auto tick = [&](MeleeWebMenuHost* host,
                    melee_web::GameplayMenuWorld& world, PADStatus raw[4],
                    unsigned& audio_phase) {
        const int result = melee_web_menu_host_tick(host, raw, error,
                                                    sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world.audio(), pcm, count, error,
                                     sizeof(error)), error);
        return result;
    };
    auto start_title = [&](MeleeWebMenuHost*& host,
                           std::unique_ptr<melee_web::GameplayMenuWorld>& world,
                           PADStatus raw[4], unsigned& audio_phase) {
        host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        raw[2].err = raw[3].err = -1;
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original title left before its Start input");
    };

    auto abort_and_destroy = [&](MeleeWebMenuHost* host,
                                 std::unique_ptr<melee_web::GameplayMenuWorld>& world,
                                 unsigned& audio_phase, const char* label) {
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        check(melee_web_menu_host_source_scene(host) == 0,
              "Eject did not retire the source-scene lease");
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        check(melee_web_current_scene_info() == saved_scene_info,
              "Eject did not restore the caller's GameSceneInfo owner");
        check(!melee_web_gameplay_world_exists(),
              "Eject retained the source SDK world");

        auto* css_host = melee_web_menu_host_create(error, sizeof(error));
        check(css_host != nullptr, error);
        auto css_world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_enter(css_host, css_world->audio(), error,
                                        sizeof(error)), error);
        check(melee_web_menu_host_source_scene(css_host) == 1,
              "A new source session did not enter original CSS after Eject");
        check_full_roster();
        PADStatus neutral[4]{};
        neutral[2].err = neutral[3].err = -1;
        for (unsigned frame = 0; frame < 4; ++frame) {
            check(tick(css_host, *css_world, neutral, audio_phase) == 1, error);
        }
        check(melee_web_menu_host_leave(css_host, 1, error, sizeof(error)), error);
        css_world->close();
        css_world.reset();
        check(melee_web_menu_host_destroy(css_host, error, sizeof(error)), error);
        check(melee_web_current_scene_info() == saved_scene_info,
              "CSS re-entry teardown changed the caller's GameSceneInfo owner");
        std::cout << "Original " << label
                  << " Eject released source ownership and allowed CSS re-entry\n";
    };

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        abort_and_destroy(host, world, audio_phase, "Title");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        check_full_roster();
        raw[0].button = PAD_BUTTON_START;
        int result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        check(result == 3, "Original Title did not expose its Start route");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
              "Source Title Start did not preserve its GM_MENU destination");
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original Main left before its Back input");
        raw[0].button = PAD_BUTTON_B;
        result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        check(result == 3, "Original Main did not expose its Back route");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
              "Source Main Back did not preserve its GM_TITLE destination");
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Returned Title left before Eject");
        abort_and_destroy(host, world, audio_phase,
                          "Title/Main route before Eject");
    }

    {
        MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        std::unique_ptr<melee_web::GameplayMenuWorld> world =
            std::make_unique<melee_web::GameplayMenuWorld>(
                files, melee_web::GameplayMenuScene::Title);
        PADStatus raw[4]{};
        raw[2].err = raw[3].err = -1;
        unsigned audio_phase = 0;
        check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                              sizeof(error)), error);
        check_full_roster();

        /* A held Start edge during the source guard is consumed there. It
         * cannot be replayed as a synthetic Start after the guard expires. */
        raw[1].button = PAD_BUTTON_START;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Held P2 Start bypassed the original Title input guard");
        raw[1].button = 0;
        check(tick(host, *world, raw, audio_phase) == 1,
              "Title left while Player 2 released Start");
        raw[1].button = PAD_BUTTON_START;
        check(raw[0].button == 0,
              "Player 1 must stay neutral in the Player 2 Title route test");
        check(tick(host, *world, raw, audio_phase) == 3,
              "Original Title did not accept a fresh Start edge from Player 2");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
              "Player 2 Title Start did not preserve the source GM_MENU destination");
        raw[1].button = 0;
        world->close();
        world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        abort_and_destroy(host, world, audio_phase, "Player 2 Title route");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        /* Recreate an unclaimed character unlock through source state
         * synchronization and award routines. The resulting notification is
         * genuinely pending and Title therefore requests Challenger Approach. */
        const u8 ckind = gm_GetCKindByUnlockIndex(0);
        gm_80164A0C(ckind);
        gm_801729EC();
        gm_UnlockCKind(static_cast<CharacterKind>(ckind));
        check(gm_801721EC(),
              "Original character unlock routine did not create pending source work");
        check_full_roster();
        raw[1].button = PAD_BUTTON_START;
        int result = 1;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        raw[1].button = 0;
        check(result == 3, "Original Title did not expose its Start route");
        check(!melee_web_menu_host_leave(host, 0, error, sizeof(error)) &&
                  std::string(error).find("unsupported destination 20") != std::string::npos,
              "An unsupported Title destination was not rejected explicitly");
        check(std::string(error).find("buttons 0x") != std::string::npos,
              "Unsupported Challenger route did not retain the source-written Title payload");
        abort_and_destroy(host, world, audio_phase,
                          "unsupported Title route recovery");
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        int result = 1;
        unsigned callbacks_to_timeout = 0;
        for (; callbacks_to_timeout < 700 && result != 3;
             ++callbacks_to_timeout)
            result = tick(host, *world, raw, audio_phase);
        check(result == 3, "Original Title timeout did not request its source exit");
        check(callbacks_to_timeout == 501,
              "Title timeout diverged from the 20-frame guard and 601-frame source timer");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_route_target_mode(host) == GM_OPENING_MV,
              "Title timeout did not preserve the source GM_OPENING_MV destination");
        check(melee_web_menu_host_route_target_state(host) == 1,
              "Title timeout did not preserve the source VS-demo state selected by Title");
        world->verify_immutable_archives();
        world->close();
        world.reset();

        /* Reduce the supported Opening VS handoff to its deterministic
         * boundary.  The retail demo match itself needs its separate match
         * assets and capture; this fixture verifies the source-selected
         * payload, the host's suspend boundary, and the retained PAD owner
         * before retiring the route. */
        MeleeWebOpeningPreview opening_preview{};
        check(melee_web_menu_host_opening_preview(host, &opening_preview,
                                                  error, sizeof(error)), error);
        auto opening_world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_opening(host, opening_world->audio(),
                                                error, sizeof(error)), error);
        MeleeWebMenuMatchSelection opening_selection{};
        check(melee_web_menu_host_opening_selection(
                  host, &opening_selection, error, sizeof(error)), error);
        check(opening_selection.opening_demo == 1 &&
                  opening_selection.player_count == 4 &&
                  opening_selection.start.rules.stkind == opening_preview.stage_kind &&
                  opening_selection.start.rules.match_kind == opening_preview.match_kind,
              "Opening VS handoff did not preserve its source-selected demo payload");
        const unsigned expected_opening_stocks =
            opening_selection.start.rules.match_kind == 1 ? 99u : 0u;
        for (unsigned i = 0; i < opening_selection.player_count; ++i) {
            const PlayerInitData& player = opening_selection.start.players[i];
            check(player.slot_type == Gm_PKind_Cpu && player.cpu_kind == CpuKind_4 &&
                      player.cpu_level == 9 && player.stocks == expected_opening_stocks,
                  "Opening VS handoff changed its authored four-CPU setup");
        }
        check(melee_web_menu_host_opening_input(host) == nullptr,
              "Opening VS exposed retained PAD input before source suspend");
        uint8_t opening_input_bytes[MELEE_WEB_PAD_STATE_BYTES];
        melee_web_pad_state_capture(opening_input_bytes);
        check(melee_web_menu_host_opening_match_suspend(host, error, sizeof(error)),
              error);
        const MeleeWebPadState* opening_input =
            melee_web_menu_host_opening_input(host);
        check(opening_input != nullptr,
              "Opening VS suspend did not retain a PAD input owner");
        opening_world->verify_immutable_archives();
        opening_world->close();
        opening_world.reset();
        check(melee_web_menu_host_opening_match_finish(
                  host, 0x13579bdfU, opening_input_bytes, error, sizeof(error)),
              error);
        check(melee_web_menu_host_opening_target_state(host) == 2 &&
                  melee_web_menu_host_opening_input(host) == nullptr,
              "Opening VS finish did not release the completed state owner");
        auto continuation_world = std::make_unique<melee_web::GameplayMenuWorld>(
            files, melee_web::GameplayMenuScene::Title);
        check(melee_web_menu_host_enter_opening(
                  host, continuation_world->audio(), error, sizeof(error)), error);
        check(melee_web_menu_host_opening_target_state(host) == 2 &&
                  melee_web_menu_host_source_scene(host) ==
                      MELEE_WEB_MENU_HOST_SCENE_TITLE,
              "Opening VS finish could not enter the authored next Title state");
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        continuation_world->verify_immutable_archives();
        continuation_world->close();
        continuation_world.reset();
        check(melee_web_menu_host_opening_input(host) == nullptr,
              "Opening next-state cleanup retained a PAD input route");
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
        check(melee_web_current_scene_info() == saved_scene_info,
              "Opening-mode handoff did not restore its caller GameSceneInfo owner");
        check(!melee_web_gameplay_world_exists(),
              "Opening-mode handoff retained the Title SDK world");

        std::cout << "Original Opening VS handoff selected four CPUs, suspended with retained PAD input, and cleaned up\n";

        /* An unsupported next owner can retire the completed Title world and
         * import the disc again without a page reload. */
        auto* css_host = melee_web_menu_host_create(error, sizeof(error));
        check(css_host != nullptr, error);
        auto css_world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        check(melee_web_menu_host_enter(css_host, css_world->audio(), error,
                                        sizeof(error)), error);
        check(melee_web_menu_host_source_scene(css_host) == 1,
              "Disc reimport after Title idle did not start at original CSS");
        check_full_roster();
        for (unsigned frame = 0; frame < 4; ++frame)
            check(tick(css_host, *css_world, raw, audio_phase) == 1,
                  "CSS after Title idle left before Eject");
        check(melee_web_menu_host_leave(css_host, 1, error, sizeof(error)), error);
        css_world->close();
        css_world.reset();
        check(melee_web_menu_host_destroy(css_host, error, sizeof(error)), error);
        std::cout << "Original Title timeout selected GM_OPENING_MV state 1 and allowed clean CSS reimport\n";
    }

    {
        MeleeWebMenuHost* host = nullptr;
        std::unique_ptr<melee_web::GameplayMenuWorld> world;
        PADStatus raw[4]{};
        unsigned audio_phase = 0;
        start_title(host, world, raw, audio_phase);
        /* Establish a checked Main entry fixture independently of the retail
         * Title exit route.  The retail profile can legitimately request the
         * unsupported Challenger Approach route from Title Start. */
        check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
        check(melee_web_vs_mode_set_route(GM_MENU, GM_TITLE),
              "Could not establish the supported GM_MENU test route");
        world->rebuild_scene(melee_web::GameplayMenuScene::Main);
        check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                             sizeof(error)), error);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original Main left before Eject");
        abort_and_destroy(host, world, audio_phase, "Main");
    }

    {
        auto* host = melee_web_menu_host_create(error, sizeof(error));
        check(host != nullptr, error);
        auto world = std::make_unique<melee_web::GameplayMenuWorld>(files);
        PADStatus raw[4]{};
        raw[2].err = raw[3].err = -1;
        unsigned audio_phase = 0;
        check(melee_web_menu_host_enter(host, world->audio(), error,
                                        sizeof(error)), error);
        for (unsigned frame = 0; frame < 120; ++frame)
            check(tick(host, *world, raw, audio_phase) == 1,
                  "Original CSS left before the normal-close regression");
        raw[0].button = PAD_BUTTON_START;
        int result = tick(host, *world, raw, audio_phase);
        raw[0].button = 0;
        for (unsigned frame = 0; frame < 120 && result != 3; ++frame)
            result = tick(host, *world, raw, audio_phase);
        check(result == 3, "Original CSS did not complete its SSS transition");
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        check(melee_web_menu_host_phase(host) == 2,
              "Normal CSS leave did not retain the closed SSS-ready session");
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        check(!melee_web_gameplay_world_exists(),
              "Normal CSS leave retained the owned source world");
        std::cout << "Normal CSS->SSS leave cleared its consumed transition before host teardown\n";
    }
    std::cout << "Original all-unlocked CSS roster, P1/P2 Title Start edges, unsupported Challenger, Title timeout to Opening state 1 and recovery passed\n";
}

struct SoundRouteTick {
    int result;
    unsigned audio_phase;
};

SoundRouteTick sound_route_tick(MeleeWebMenuHost* host,
                                melee_web::GameplayMenuWorld& world,
                                PADStatus raw[4], char* error,
                                float pcm[1068], unsigned audio_phase)
{
    const int result = melee_web_menu_host_tick(host, raw, error, 256);
    check(result == 1 || result == 3, error);
    audio_phase += 32000;
    const unsigned count = audio_phase / 60;
    audio_phase %= 60;
    if (!melee_web_audio_render(world.audio(), pcm, count, error, 256)) {
        std::cerr << "sound-route: audio render rejected result=" << result
                  << " frames=" << count
                  << " audio_generation=" << melee_web_audio_generation(world.audio())
                  << " menu_phase=" << melee_web_menu_host_phase(host)
                  << " source_scene=" << melee_web_menu_host_source_scene(host)
                  << " error=" << error << "\n";
        throw std::runtime_error(error);
    }
    return {result, audio_phase};
}

unsigned sound_route_neutral(MeleeWebMenuHost* host,
                             melee_web::GameplayMenuWorld& world,
                             PADStatus raw[4], char* error, float pcm[1068],
                             unsigned audio_phase, unsigned frames)
{
    raw[0].button = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto tick = sound_route_tick(host, world, raw, error, pcm,
                                           audio_phase);
        audio_phase = tick.audio_phase;
        check(tick.result == 1,
              "Original menu exited during a neutral guard");
    }
    return audio_phase;
}

unsigned sound_route_press(MeleeWebMenuHost* host,
                           melee_web::GameplayMenuWorld& world,
                           PADStatus raw[4], char* error, float pcm[1068],
                           unsigned audio_phase, u16 button)
{
    audio_phase = sound_route_neutral(host, world, raw, error, pcm,
                                      audio_phase, 8);
    raw[0].button = button;
    const auto tick = sound_route_tick(host, world, raw, error, pcm,
                                       audio_phase);
    audio_phase = tick.audio_phase;
    check(tick.result == 1,
          "Original menu exited while sampling an input edge");
    return sound_route_neutral(host, world, raw, error, pcm, audio_phase, 8);
}

void run_main_sound_mix_route(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    float pcm[1068]{};
    unsigned audio_phase = 0;
    const void* saved_scene_info = melee_web_current_scene_info();
    MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    PADStatus raw[4]{};
    raw[2].err = raw[3].err = -1;
    std::unique_ptr<melee_web::GameplayMenuWorld> world =
        std::make_unique<melee_web::GameplayMenuWorld>(files);
    check(melee_web_menu_host_enter(host, world->audio(), error,
                                    sizeof(error)), error);

    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START | PAD_TRIGGER_L | PAD_TRIGGER_R;
    auto route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                       audio_phase);
    int result = route_tick.result;
    audio_phase = route_tick.audio_phase;
    raw[0].button = 0;
    check(result == 3, "Original CSS did not accept its LR+Start parent route");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Original CSS LR+Start lost its GM_MENU destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    check(mn_804A04F0.cur_menu == MENU_KIND_MAIN &&
              mn_804A04F0.hovered_selection == SEL_MAIN_1P,
          "CSS parent return did not enter the original Main 1P root");
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);

    raw[0].button = PAD_BUTTON_B;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Main Back did not reach Title before Sound entry");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
          "Initial Main Back lost its GM_TITLE destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                         sizeof(error)), error);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Title Start did not return to Main before Sound entry");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Initial Title Start lost its GM_MENU destination");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);

    check(gmMainLib_8015ED74() == 0,
          "Fresh source profile did not initialize sound balance to its authored center");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_DOWN);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_LEFT);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Sound callback did not decrement the saved mix by five units");

    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    check(melee_web_menu_host_source_scene(host) == 4,
          "Sound Back did not remain in the original Main scene");
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Sound Back did not retain the source-written saved mix");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_A);
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 12);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Re-entering original Sound changed the saved mix");
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    audio_phase = sound_route_press(host, *world, raw, error, pcm,
                                      audio_phase, PAD_BUTTON_B);
    check(melee_web_menu_host_source_scene(host) == 4,
          "Sound route Back navigation did not return to original Main");

    raw[0].button = PAD_BUTTON_B;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Original Main Back did not return to Title");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_TITLE,
          "Original Main Back lost its GM_TITLE destination");
    std::vector<uint8_t> sound_card(MELEE_WEB_SAVE_PROFILE_CARD_BYTES);
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, sound_card.data(), sound_card.size(), error, sizeof(error)), error);
    check(sound_card[0x45C] == static_cast<uint8_t>(-5),
          "Closed Main snapshot lost the source Sound balance");

    world->verify_immutable_archives();
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, world->audio(), error,
                                         sizeof(error)), error);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Main-to-Title transition lost the source Sound balance");
    audio_phase = sound_route_neutral(host, *world, raw, error, pcm,
                                      audio_phase, 120);
    raw[0].button = PAD_BUTTON_START;
    result = 1;
    for (unsigned frame = 0; frame < 120 && result != 3; ++frame) {
        route_tick = sound_route_tick(host, *world, raw, error, pcm,
                                      audio_phase);
        result = route_tick.result;
        audio_phase = route_tick.audio_phase;
    }
    raw[0].button = 0;
    check(result == 3, "Returned Title did not accept its original Start route");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_route_target_mode(host) == GM_MENU,
          "Returned Title Start lost its GM_MENU destination");
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, sound_card.data(), sound_card.size(), error, sizeof(error)), error);
    check(sound_card[0x45C] == static_cast<uint8_t>(-5),
          "Closed Title snapshot lost the source Sound balance");
    world->close();
    world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Main);
    check(melee_web_menu_host_enter_main(host, world->audio(), error,
                                         sizeof(error)), error);
    check(gmMainLib_8015ED74() == static_cast<u8>(-5),
          "Original Title-to-Main transition lost the source Sound balance");
    check(melee_web_menu_host_leave(host, 1, error, sizeof(error)), error);
    world->verify_immutable_archives();
    world->close();
    world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    check(melee_web_current_scene_info() == saved_scene_info,
          "Original Sound route did not restore its caller scene owner");
    check(!melee_web_gameplay_world_exists(),
          "Original Sound route retained its native world after teardown");
    std::cout << "Original Main Settings Sound changed SaveData mix to -5, retained through Title/Main, returned, re-entered, and cleaned up\n";
}

void run_opening_movie_preload_smoke(melee_web::RuntimeFiles files)
{
    char error[256]{};
    float pcm[1068]{};
    const void* saved_scene_info = melee_web_current_scene_info();
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    auto title_world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_enter_title(host, title_world->audio(), error,
                                          sizeof(error)), error);
    PADStatus raw[4]{};
    raw[2].err = raw[3].err = -1;
    unsigned audio_phase = 0;
    for (unsigned frame = 0; frame < 120; ++frame) {
        check(melee_web_menu_host_tick(host, raw, error, sizeof(error)) == 1,
              error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(title_world->audio(), pcm, count, error,
                                     sizeof(error)), error);
    }
    int result = 1;
    unsigned callbacks_to_timeout = 0;
    for (; callbacks_to_timeout < 700 && result == 1; ++callbacks_to_timeout) {
        result = melee_web_menu_host_tick(host, raw, error, sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(title_world->audio(), pcm, count, error,
                                     sizeof(error)), error);
    }
    check(result == 3,
          "Opening movie probe did not request its authored Opening route");
    check(callbacks_to_timeout == 501,
          "Opening movie probe diverged from the source Title timeout");
    check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
    check(melee_web_menu_host_source_scene(host) == 0,
          "Opening movie probe did not retire its Title source owner at timeout");
    check(melee_web_menu_host_route_target_mode(host) == GM_OPENING_MV,
          "Opening movie probe lost its authored Opening route");
    check(melee_web_vs_mode_select_state(0),
          "Opening movie probe could not select authored state 0");
    title_world->close();
    title_world.reset();

    auto movie_world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(!melee_web_menu_host_enter_opening(host, movie_world->audio(), error,
                                             sizeof(error)),
          "Opening movie probe entered without a source heap owner");
    check(std::string(error).find("active source lbMemory/lbHeap owner") !=
              std::string::npos,
          "Opening movie probe did not reject its missing source heap owner explicitly");
    check(melee_web_menu_host_source_scene(host) == 0,
          "Rejected Opening movie probe retained a source scene owner");
    movie_world->verify_immutable_archives();
    movie_world->close();
    movie_world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    check(melee_web_current_scene_info() == saved_scene_info,
          "Opening movie probe did not restore its caller scene owner");
    check(!melee_web_gameplay_world_exists(),
          "Opening movie probe retained its source world");
    std::cout << "Original Opening movie route selected state 0 and rejected missing source heap/cache ownership explicitly; no movie decode or retail-route claim\n";
}

void run_trophy_baseline_smoke(const melee_web::RuntimeFiles& files)
{
    char error[256]{};
    auto* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    auto world = std::make_unique<melee_web::GameplayMenuWorld>(
        files, melee_web::GameplayMenuScene::Title);
    check(melee_web_menu_host_initialize_profile_baseline(
              host, error, sizeof(error)), error);

    const auto* save = gmMainLib_GetSaveData();
    check(save->trophy_count == TY_TROPHY_COUNT,
          "Original TyDatai-backed Everything baseline omitted trophies");
    for (size_t trophy = 0; trophy < TY_TROPHY_COUNT; ++trophy)
        check((save->trophy_flags[trophy] & 0x8000) != 0 &&
                  (save->trophy_flags[trophy] & 0x00FF) == 1,
              "Original TyDatai-backed trophy award diverged from Toy_SetUnlockState");
    check(gm_80164ABC() && gm_80164600() &&
              save->x1A68 == ((UINT64_C(1) << 51) - 1) &&
              gmMainLib_8015CF94(),
          "Original source roster, stage, or event baseline was not initialized");
    check(save->unk_1A8.x4 && save->unk_1A8.x5 && save->unk_1A8.x6,
          "Original source completion flags were not initialized");

    check(!gm_801721EC(),
          "Everything baseline retained transient new-completion notifications");
    check((save->x186C & 0x0F) == 0x0F && (save->x186C & 0xF0) == 0,
          "Everything baseline did not derive only the four source-supported feature bits");
    for (int selkind = 0; selkind < SELKIND_COUNT; ++selkind) {
        const auto ckind = static_cast<CharacterKind>(gm_SelKindToCKind((u8) selkind));
        const int clear_ids[] = {
            gm_80160474(ckind, GM_CLASSIC),
            gm_80160474(ckind, GM_ADVENTURE),
            gm_80160474(ckind, GM_ALLSTAR),
        };
        for (size_t mode = 0; mode < sizeof(clear_ids) / sizeof(clear_ids[0]); ++mode)
                check(gmMainLib_8015DA90(clear_ids[mode]) != 0,
                      "Everything baseline omitted a source-mapped 1P reward from the persisted ledger");
    }
    {
        size_t completed_challenges = 0;
        for (int challenge = 0; challenge < 0x100; ++challenge) {
            const int excluded = challenge == 9 || challenge == 0x29 ||
                challenge == 0x42 || challenge == 0x43 ||
                challenge == 0xB9 || challenge == 0xC9 || challenge == 0xCA;
            check((gmMainLib_8015DADC(challenge) != 0) == !excluded,
                  "Everything baseline challenge flags diverged from the source inventory");
            completed_challenges += !excluded;
        }
        check(completed_challenges == 249 && gmMainLib_8015D8D8(0x123),
              "Everything baseline omitted the source all-challenges award");
    }

    world->verify_immutable_archives();
    world->close_prepared();
    world.reset();
    check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
    std::cout << "Original TyDatai-backed save baseline initialized after source-file ownership; "
                 "trophy and source unlock tables passed\n";
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
// Diagnostic-only constructor-phase control in the existing SDK target.
// Uses actual Ground owner and unchanged retirement predicate; no fixture/tick.
extern "C" HSD_GObj* melee_web_stadium_c1_manager_create_control(void);
void run_stadium_retirement_phase_controls()
{
    using namespace melee_web::test::stadium_buffer;
    char error[256]{};
    check(melee_web_gameplay_startup(8U * 1024U * 1024U,error,sizeof(error)),error);
    check(melee_web_native_world_enable(error,sizeof(error)),error);
    try {
        const auto baseline = melee_web_gameplay_stats();
        GroundStorageLease storage;
        storage.begin();
        MeleeWebSourceMemoryContext oninit{}, retirement{}, after{};
        check(melee_web_source_memory_context_read(&oninit)==MELEE_WEB_SOURCE_MEMORY_READ_OK,"phase before constructor context");
        const auto original = storage.allocation;
        HSD_GObj* manager=melee_web_stadium_c1_manager_create_control();
        check(manager && manager->proc,"phase original manager constructor");
        auto* proc=manager->proc;
        MeleeWebSourceMemoryAllocation manager_lease{},proc_lease{},live{};
        check(melee_web_source_memory_context_read(&retirement)==MELEE_WEB_SOURCE_MEMORY_READ_OK,"phase after constructor context");
        const auto live_status=melee_web_source_memory_allocation_read(storage.payload,&live);
        const auto manager_status=melee_web_source_memory_allocation_read(manager,&manager_lease);
        const auto proc_status=melee_web_source_memory_allocation_read(proc,&proc_lease);
        auto numeric=[&](const char* phase,const MeleeWebSourceMemoryContext& context,
                         MeleeWebSourceMemoryReadStatus status,const MeleeWebSourceMemoryAllocation& lease){
            const auto stats=melee_web_gameplay_stats();
            std::fprintf(stderr,"C3_PHASE_CONTROL phase=%s world=%llu heap=%d watermark=%llu status=%d live=%d requested=%u generation=%llu lease_world=%llu lease_heap=%d current_heap_free=%d current_objects=%u current_processes=%u current_ticks=%llu payload=%p manager=%p proc=%p\n",
                phase,static_cast<unsigned long long>(context.world_generation),context.source_heap_handle,
                static_cast<unsigned long long>(context.allocation_generation_watermark),status,lease.live,lease.requested_bytes,
                static_cast<unsigned long long>(lease.allocation_generation),static_cast<unsigned long long>(lease.world_generation),
                lease.source_heap_handle,stats.heap_free_bytes,stats.objects,stats.processes,
                static_cast<unsigned long long>(stats.ticks),storage.payload,static_cast<void*>(manager),static_cast<void*>(proc));
            std::fflush(stderr);
        };
        numeric("original-oninit",oninit,live_status,original);
        numeric("after-original-manager-construction",retirement,live_status,live);
        numeric("manager-object",retirement,manager_status,manager_lease);
        numeric("manager-proc",retirement,proc_status,proc_lease);
        check(retirement.world_generation==oninit.world_generation && retirement.source_heap_handle==oninit.source_heap_handle &&
                  retirement.allocation_generation_watermark>oninit.allocation_generation_watermark &&
                  exact_live_allocation_matches_context(live_status,live,retirement,64,original.allocation_generation) &&
                  exact_new_allocation_supported(manager_status,manager_lease,oninit,sizeof(HSD_GObj)) &&
                  exact_new_allocation_supported(proc_status,proc_lease,oninit,sizeof(HSD_GObjProc)) && melee_web_source_memory_healthy(),
              "phase constructor changed owner or exact Ground lease");
        HSD_GObjPLink_80390228(manager); // Exact direct original constructor owner.
        storage.end(); // Existing checked owner restores original typed devices.
        MeleeWebSourceMemoryAllocation dead{};
        const auto dead_status=melee_web_source_memory_allocation_read(storage.payload,&dead);
        const auto after_status=melee_web_source_memory_context_read(&after);
        numeric("after-retirement",after,dead_status,dead);
        const auto accepts=[&](MeleeWebSourceMemoryReadStatus status,const MeleeWebSourceMemoryAllocation& lease,
                               const MeleeWebSourceMemoryContext& before,const MeleeWebSourceMemoryContext& finish){
            return exact_retired_allocation_supported(status,lease,MELEE_WEB_SOURCE_MEMORY_READ_OK,before,after_status,finish);
        };
        check(!accepts(dead_status,dead,oninit,after),"unchanged old helper unexpectedly accepted stale construction baseline");
        check(accepts(dead_status,dead,retirement,after),"unchanged helper refused actual pre-retirement baseline");
        check(!accepts(live_status,live,retirement,after),"live exact Ground lease accepted as retired");
        auto bad=dead;bad.requested_bytes=64;check(!accepts(dead_status,bad,retirement,after),"nonzero dead request accepted");
        bad=dead;bad.allocation_generation=original.allocation_generation;check(!accepts(dead_status,bad,retirement,after),"nonzero dead generation accepted");
        bad=dead;++bad.source_heap_handle;check(!accepts(dead_status,bad,retirement,after),"foreign heap accepted");
        bad=dead;++bad.world_generation;check(!accepts(dead_status,bad,retirement,after),"foreign world accepted");
        auto changed=after;++changed.allocation_generation_watermark;check(!accepts(dead_status,dead,retirement,changed),"watermark change accepted");
        check(!accepts(MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT,dead,retirement,after),"allocation read failure accepted");
        MeleeWebSourceMemoryContext pure{};MeleeWebSourceMemoryAllocation pure_dead{};
        check(melee_web_source_memory_context_read(&pure)==after_status &&
                  pure.world_generation==after.world_generation && pure.source_heap_handle==after.source_heap_handle &&
                  pure.allocation_generation_watermark==after.allocation_generation_watermark &&
                  melee_web_source_memory_allocation_read(storage.payload,&pure_dead)==dead_status &&
                  pure_dead.live==dead.live && pure_dead.requested_bytes==dead.requested_bytes &&
                  pure_dead.allocation_generation==dead.allocation_generation && storage.devices_before.matches(),
              "pure phase/refusal controls mutated owner records");
        const auto retired=melee_web_gameplay_stats();
        check(retired.generation==baseline.generation && retired.ticks==baseline.ticks &&
                  retired.objects==baseline.objects && retired.processes==baseline.processes &&
                  source_stage_registry_empty() && source_stage_gobj_count()==0 && melee_web_ground_map_storage_available(),
              "phase reducer left source owners live");
        std::fprintf(stderr,"C3_PHASE_CONTROL_RESULT stale_refused=1 current_accepted=1 original_lease_preserved=1 negatives=1 pure=1 ticks=0 fullStage=0\n");
    } catch (const std::exception& failure) {
        std::fprintf(stderr,"C3_PHASE_CONTROL_RETAINED first_exception=%s\n",failure.what());std::fflush(stderr);
        std::_Exit(1); // Explicit retained partial owner, no raw cleanup on refusal.
    }
    check(melee_web_gameplay_shutdown(error,sizeof(error)),error);
    MeleeWebSourceMemoryContext inactive{};
    check(melee_web_source_memory_context_read(&inactive)==MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE,"phase owned world remains active");
}

void run_stadium_profile_controls()
{
    using namespace melee_web;
    check(melee_web_stadium_display_provenance_controls(),
          "Shared Stadium image assignment lost mixed preload/fallback provenance");
    check(melee_web_stadium_display_list_controls(),
          "Stadium SIS owner list controls changed a foreign text/context chain");
    check(melee_web_stadium_display_owner_retirement_controls(),
          "Stadium display owner refused mixed-buffer retirement or lost partial ownership");
    check(melee_web_stadium_map2_buffer_controls(),
          "Stadium map-2 0x7D5 origin journal lost the nested Ground owner or released a borrowed buffer");
    check(melee_web_stadium_source_journal_controls(),
          "Synthetic source-event journal controls lost ordering, pointers, or sticky refusal state");
    const auto* profile = melee_web_stage_stadium_profile_data();
    check(profile && melee_web_stage_profile(St_Kind_PStadium) == profile &&
              profile->diagnostic_only && profile->source != nullptr,
          "Guarded Stadium source profile is not resolved by its canonical owner");
    check(melee_web_stage_content(St_Kind_PStadium) == nullptr &&
              !melee_web_menu_stage_available(St_Kind_PStadium),
          "Diagnostic Stadium profile leaked into ordinary content admission");
    const auto* content = melee_web_stage_content_for_profile(St_Kind_PStadium);
    check(content && content->diagnostic_only &&
              content->ground_kind == Gr_Kind_PStadium &&
              std::string_view(content->archive) == "GrPs.usd" &&
              std::string_view(content->music) == "pstadium.hps" &&
              content->music_id == 64 &&
              std::string_view(content->audio_bank) == "pstadium.ssm",
          "Diagnostic Stadium content row differs from the prepared source identity");
    check(profile->required_map_count == 4 && profile->required_map_ids &&
              profile->required_map_ids[0] == 0 &&
              profile->required_map_ids[1] == 1 &&
              profile->required_map_ids[2] == 2 &&
              profile->required_map_ids[3] == 5,
          "Stadium profile omitted a source-ordered OnInit map owner");
    const auto* ownership = profile->map_ownership;
    check(profile->map_ownership_policy == MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED &&
              ownership && ownership->resident_entry_count == 4 &&
              ownership->external_reference_count == 26 &&
              ownership->animation_flag_entry_count == 10 &&
              ownership->flagged_object_count == 44 &&
              profile->entry_count == 10 && profile->animation_count_count == 10,
          "Stadium profile did not reuse the complete C0 authored map contract");
    check(profile->public_symbol_count == 2 && profile->public_symbols &&
              profile->public_symbols[0].kind == MELEE_WEB_STAGE_PUBLIC_IMAGE &&
              std::string_view(profile->public_symbols[0].name) ==
                  "GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc" &&
              profile->public_symbols[1].kind == MELEE_WEB_STAGE_PUBLIC_SIS &&
              std::string_view(profile->public_symbols[1].name) ==
                  "SIS_GrPStadiumData",
          "Stadium profile omitted its map-owned IMAGE or DatSis-backed SIS public symbol");

    char snapshot_error[160]{};
    MeleeWebStadiumC1StageInfoSnapshot* const stage_snapshot =
        melee_web_stadium_c1_stage_info_snapshot_begin(
            snapshot_error, sizeof(snapshot_error));
    check(stage_snapshot != nullptr, snapshot_error);
    char error[256]{};
    check(melee_web_stage_begin_kind(
              St_Kind_PStadium, nullptr, nullptr, 0, 0, error, sizeof(error)) == nullptr &&
              std::string_view(error) ==
                  "Diagnostic-only stage profile requires its explicit OnInit boundary",
          "Ordinary stage-begin API did not reject the diagnostic Stadium profile");
    MeleeWebStageLast* rejected_stage =
        reinterpret_cast<MeleeWebStageLast*>(uintptr_t{0x7008});
    check(melee_web_stage_begin_kind_on_init_diagnostic(
              St_Kind_Last, nullptr, nullptr, &rejected_stage, error, sizeof(error)) == nullptr &&
              rejected_stage == reinterpret_cast<MeleeWebStageLast*>(uintptr_t{0x7008}) &&
              std::string_view(error) ==
                  "OnInit-only retained-owner output slot must be empty",
          "Rejected OnInit begin erased the caller's retained-owner handle");
    rejected_stage = nullptr;
    check(melee_web_stage_begin_kind_on_init_diagnostic(
              St_Kind_Last, nullptr, nullptr, &rejected_stage, error, sizeof(error)) == nullptr &&
              rejected_stage == nullptr &&
              std::string_view(error) ==
                  "OnInit-only stage boundary is limited to the diagnostic Stadium profile",
          "OnInit-only boundary accepted a non-Stadium source profile");
    check(melee_web_stadium_c1_stage_info_snapshot_matches(stage_snapshot),
          "Rejected diagnostic profile controls changed source StageInfo bytes");
    check(melee_web_stadium_c1_stage_info_snapshot_release_unchanged(
              stage_snapshot, snapshot_error, sizeof(snapshot_error)),
          snapshot_error);
    std::cout << "Diagnostic Stadium profile/content gate and source-state rejection controls passed\n";
}

void run_stadium_started_map2_buffer_controls()
{
    char error[256]{};
    if (!melee_web_gameplay_startup(8U * 1024U * 1024U,
                                    error, sizeof(error))) {
        std::fprintf(stderr, "C1_STARTED_MAP2_CONTROL startup_refused=%s\n",
                     error);
        std::fflush(stderr);
        std::_Exit(1);
    }
    /* The C control restores the bootstrap-owned roots before returning. If it
     * refuses or leaves an uncertain partial fixture, retain the process. */
    if (!melee_web_stadium_map2_started_buffer_controls()) {
        std::fprintf(stderr,
                     "C1_STARTED_MAP2_CONTROL retained_partial_fixture=1\n");
        std::fflush(stderr);
        std::_Exit(1);
    }
    if (!melee_web_gameplay_shutdown(error, sizeof(error))) {
        std::fprintf(stderr, "C1_STARTED_MAP2_CONTROL shutdown_refused=%s\n",
                     error);
        std::fflush(stderr);
        std::_Exit(1);
    }
    MeleeWebSourceMemoryContext inactive{};
    if (melee_web_source_memory_context_read(&inactive) !=
        MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE ||
        melee_web_gameplay_world_exists() || melee_web_gameplay_session_active() ||
        melee_web_gameplay_allocation().identity || HSD_GObj_Entities != nullptr ||
        HSD_GetHeap() != -1) {
        std::fprintf(stderr,
                     "C1_STARTED_MAP2_CONTROL shutdown_context_still_active=1\n");
        std::fflush(stderr);
        std::_Exit(1);
    }
    std::cout << "C1 started map-2 graph/buffer lease controls passed; synthetic graph, real HSD fallback allocation, no Stadium OnInit/dispatch/ticks\n";
}

void run_stadium_effect_runtime_lifecycle_control()
{
    char error[256]{};
    check(melee_web_gameplay_startup(8U * 1024U * 1024U,
                                     error, sizeof(error)), error);
    check(melee_web_native_world_enable(error, sizeof(error)), error);
    HSD_GObj** const links = reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
    check(links != nullptr && !links[11] && !links[12] &&
              !melee_web_effect_runtime_prepared() &&
              !melee_web_effect_runtime_active(),
          "Asset-free effect lifecycle requires an unowned runtime and empty source links");
    const MeleeWebGameplayStats before = melee_web_gameplay_stats();
    const int scheduler_cycle_before = HSD_GObj_804D783C;
    const uint32_t gobj_pool_before = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    const uint32_t proc_pool_before = HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
    check(melee_web_effect_runtime_prepare(error, sizeof(error)), error);
    check(melee_web_effect_runtime_prepared() &&
              !melee_web_effect_runtime_active(),
          "Original effect reservation must remain inactive before efLib_Init");
    efLib_Init();
    check(links[11] && links[12],
          "Original efLib_Init did not create both source effect link owners");
    check(melee_web_effect_runtime_complete_source_init(error, sizeof(error)), error);
    check(melee_web_effect_runtime_active(),
          "Original effect runtime did not activate after source scheduler validation");
    check(melee_web_effect_runtime_end(error, sizeof(error)), error);
    check(!melee_web_effect_runtime_prepared() &&
              !melee_web_effect_runtime_active() && !links[11] && !links[12],
          "Original effect runtime teardown did not retire both source link owners");
    const MeleeWebGameplayStats after = melee_web_gameplay_stats();
    check(after.generation == before.generation && after.ticks == before.ticks &&
              HSD_GObj_804D783C == scheduler_cycle_before &&
              HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_pool_before &&
              HSD_ObjAllocGetUsing(&gobjproc_alloc_data) == proc_pool_before,
          "Asset-free effect lifecycle advanced the source cursor or retained scheduler owners");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), error);
    std::cout << "C1 asset-free original effect prepare/efLib_Init/complete/end passed; no stage callbacks, proc dispatch, or ticks\n";
}


constexpr size_t kC1HeapCensusRowCapacity = 4096;
constexpr size_t kC1HeapGraphCapacity = kC1HeapCensusRowCapacity;
constexpr size_t kC1HeapGraphRootCount = 10;
constexpr uint32_t kC1HeapGraphIslandRequestBytes = 0x2C;
static_assert(kC1HeapGraphCapacity == kC1HeapCensusRowCapacity);
static_assert(kC1HeapGraphRootCount <= 16);

struct C1GroundStartCallbackShape {
    // Mirrors Ground_801C10B8's local LIFO callback node; only `next` is read.
    void* next;
    HSD_GObj* gobj;
    HSD_GObjEvent callback;
};
static_assert(sizeof(C1GroundStartCallbackShape) <=
              std::numeric_limits<uint32_t>::max());
constexpr uint32_t kC1HeapGraphCallbackRequestBytes =
    static_cast<uint32_t>(sizeof(C1GroundStartCallbackShape));

enum class C1HeapGraphNodeKind : uint8_t {
    island_segment,
    ground_start_callback,
};

struct C1HeapCensusRow {
    uintptr_t payload{};
    uint32_t visitor_capacity{};
    uint32_t referent_capacity{};
    MeleeWebSourceMemoryAllocation lease{};
    MeleeWebSourceMemoryReadStatus lease_status =
        MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    uintptr_t graph_next{};
    uint16_t graph_root_mask{};
    uint16_t graph_cycle_mask{};
    uint8_t graph_kind{};
};
struct C1HeapCensus {
    C1HeapCensusRow rows[kC1HeapCensusRowCapacity]{};
    size_t count{};
    size_t visited{};
    bool overflow{};
    bool invalid{};
};
C1HeapCensus c1_heap_census;

enum class C1HeapGraphFailure : uint8_t {
    none,
    lease_query_refused,
    exact_lease_absent_or_not_live,
    lease_owner_mismatch,
    requested_size_mismatch,
    missing_census_cell,
    census_lease_mismatch,
    node_budget_exceeded,
    node_capacity_exceeded,
    invalid_node_kind,
    node_kind_alias_mismatch,
    same_path_cycle,
};

struct C1HeapGraphResolvedNode {
    uintptr_t next{};
};

using C1HeapGraphResolver = bool (*)(
    uintptr_t, C1HeapGraphNodeKind, C1HeapGraphResolvedNode*,
    C1HeapGraphFailure*, void*);

struct C1HeapGraphWalkState {
    C1HeapCensusRow* rows{};
    size_t row_count{};
    size_t budget{};
    size_t unique_nodes{};
    size_t aliases{};
    size_t cycles{};
    bool unavailable{};
};

struct C1HeapGraphRoot {
    const char* name{};
    uintptr_t payload{};
    C1HeapGraphNodeKind kind{};
};

struct C1HeapGraphRootResult {
    uint32_t visited{};
    uintptr_t failure_payload{};
    C1HeapGraphFailure failure = C1HeapGraphFailure::none;
    bool attempted{};
};

struct C1HeapGraphSavedIdentity {
    uintptr_t payload{};
    int32_t heap{};
    uint32_t requested_bytes{};
    uint64_t world_generation{};
    uint64_t allocation_generation{};
};

bool c1_heap_graph_is_new_generation_reuse(
    const C1HeapGraphSavedIdentity& prior,
    const MeleeWebSourceMemoryAllocation& current)
{
    return current.live == 1 &&
        current.allocation_generation > prior.allocation_generation;
}

struct C1HeapGraphSavedState {
    C1HeapGraphSavedIdentity identities[kC1HeapGraphCapacity]{};
    size_t count{};
    bool complete{};
};

C1HeapGraphWalkState c1_heap_graph_walk_state;
C1HeapGraphSavedState c1_heap_graph_saved_state;

void c1_heap_census_visitor(void* payload, u32 visitor_capacity)
{
    ++c1_heap_census.visited;
    if (c1_heap_census.count == kC1HeapCensusRowCapacity) {
        c1_heap_census.overflow = true;
        return;
    }
    auto& row = c1_heap_census.rows[c1_heap_census.count++];
    row = C1HeapCensusRow{};
    row.payload = reinterpret_cast<uintptr_t>(payload);
    row.visitor_capacity = visitor_capacity;
    row.referent_capacity = OSReferentSize(payload);
    row.lease_status = melee_web_source_memory_allocation_read(payload, &row.lease);
    if (!payload || !visitor_capacity ||
        row.referent_capacity != visitor_capacity ||
        row.lease_status != MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        (row.lease.live && (!row.lease.allocation_generation ||
                            row.lease.requested_bytes > visitor_capacity)) ||
        (!row.lease.live && (row.lease.requested_bytes ||
                             row.lease.allocation_generation)))
        c1_heap_census.invalid = true;
}

struct C1HeapGuard {
    MeleeWebGameplayStats stats{};
    MeleeWebSourceMemoryContext context{};
    uintptr_t arena_identity{};
    decltype(melee_web::test::stadium_screen::runtime_roots_snapshot()) roots;
    decltype(melee_web::test::stadium_screen::live_class_counts()) classes;
    decltype(melee_web::test::stadium_screen::live_pool_counts()) pools{};
    uint32_t gobj_used{};
    uint32_t proc_used{};
};

C1HeapGuard c1_heap_guard()
{
    namespace screen = melee_web::test::stadium_screen;
    const MeleeWebGameplayAllocation allocation = melee_web_gameplay_allocation();
    check(allocation.identity != 0 &&
              allocation.identity <= std::numeric_limits<uintptr_t>::max(),
          "C1 census cannot read the exact gameplay arena identity");
    const auto arena_identity = static_cast<uintptr_t>(allocation.identity);
    check(melee_web_gameplay_heap_owns(
              reinterpret_cast<const void*>(arena_identity)),
          "C1 census refuses traversal after gameplay heap ownership changes");
    MeleeWebSourceMemoryContext context{};
    check(melee_web_source_memory_context_read(&context) ==
              MELEE_WEB_SOURCE_MEMORY_READ_OK &&
              context.source_heap_handle >= 0 && context.world_generation != 0,
          "C1 census requires a healthy exact source-memory context");
    const MeleeWebGameplayStats stats = melee_web_gameplay_stats();
    check(stats.generation == context.world_generation &&
              stats.heap_free_bytes >= 0 && melee_web_source_memory_healthy(),
          "C1 census requires a healthy source heap and live gameplay world");
    return {stats, context, arena_identity, screen::runtime_roots_snapshot(),
            screen::live_class_counts(), screen::live_pool_counts(),
            HSD_ObjAllocGetUsing(&gobj_alloc_data),
            HSD_ObjAllocGetUsing(&gobjproc_alloc_data)};
}

bool c1_heap_guards_equal(const C1HeapGuard& a, const C1HeapGuard& b)
{
    return a.stats.ticks == b.stats.ticks &&
           a.stats.objects == b.stats.objects &&
           a.stats.processes == b.stats.processes &&
           a.stats.object_peak == b.stats.object_peak &&
           a.stats.process_peak == b.stats.process_peak &&
           a.stats.heap_free_bytes == b.stats.heap_free_bytes &&
           a.stats.generation == b.stats.generation &&
           a.context.source_heap_handle == b.context.source_heap_handle &&
           a.context.world_generation == b.context.world_generation &&
           a.context.allocation_generation_watermark ==
               b.context.allocation_generation_watermark &&
           a.arena_identity == b.arena_identity && a.roots == b.roots &&
           a.classes == b.classes && a.pools == b.pools &&
           a.gobj_used == b.gobj_used && a.proc_used == b.proc_used &&
           melee_web_source_memory_healthy();
}

void c1_heap_query_line(unsigned world, const char* consumer, unsigned cycle,
                        const char* phase, const char* kind, uintptr_t payload,
                        int status,
                        const MeleeWebSourceMemoryAllocation& lease,
                        uint64_t prior_generation = 0, int refused = 0,
                        const char* relation = nullptr,
                        const char* classification = nullptr)
{
    std::cerr << "C1_HEAP_QUERY world=" << world << " consumer=" << consumer
              << " cycle=" << cycle << " phase=" << phase << " kind=" << kind
              << " payload=0x" << std::hex << payload << std::dec
              << " status=" << status
              << " live=" << static_cast<unsigned>(lease.live)
              << " generation=" << lease.allocation_generation
              << " lease_world=" << lease.world_generation
              << " prior_generation=" << prior_generation
              << " refused=" << refused;
    if (relation) std::cerr << " relation=" << relation;
    if (classification) std::cerr << " classification=" << classification;
    std::cerr << '\n';
}

void c1_emit_heap_census(unsigned world, const char* consumer, unsigned cycle,
                         const char* phase, const C1HeapGuard& before,
                         C1HeapGuard* after_out)
{
    c1_heap_census.count = 0;
    c1_heap_census.visited = 0;
    c1_heap_census.overflow = false;
    c1_heap_census.invalid = false;
    OSVisitAllocated(c1_heap_census_visitor);
    check(!c1_heap_census.overflow &&
              c1_heap_census.visited == c1_heap_census.count,
          "C1 allocated-cell census overflowed its external 4096-row bound");
    check(!c1_heap_census.invalid,
          "C1 allocated-cell census found a missing lease or capacity mismatch");

    // OSDumpHeap is the pinned SDK source of real allocated/free cell spans.
    // The caller preserves the process logger policy; the host test parses its
    // ordinary INFO output and fails if the rows are filtered.
    std::cerr << "C1_HEAP_DUMP_BEGIN world=" << world << " consumer=" << consumer
              << " cycle=" << cycle << " phase=" << phase << '\n';
    std::cerr.flush();
    OSDumpHeap(before.context.source_heap_handle);
    std::cerr << "C1_HEAP_DUMP_END world=" << world << " consumer=" << consumer
              << " cycle=" << cycle << " phase=" << phase << '\n';
    std::cerr.flush();

    const auto first_live = std::find_if(
        c1_heap_census.rows, c1_heap_census.rows + c1_heap_census.count,
        [](const C1HeapCensusRow& row) { return row.lease.live != 0; });
    check(first_live != c1_heap_census.rows + c1_heap_census.count &&
              first_live->visitor_capacity > 1,
          "C1 census has no live exact payload for its interior-query control");
    int unknown_marker = 0;
    MeleeWebSourceMemoryAllocation unknown{};
    const auto unknown_status = melee_web_source_memory_allocation_read(
        &unknown_marker, &unknown);
    check(unknown_status == MELEE_WEB_SOURCE_MEMORY_READ_OK && !unknown.live,
          "C1 census unknown exact-payload query was not reported explicitly");
    c1_heap_query_line(world, consumer, cycle, phase, "unknown",
                       reinterpret_cast<uintptr_t>(&unknown_marker),
                       unknown_status, unknown);
    MeleeWebSourceMemoryAllocation interior{};
    const auto interior_status = melee_web_source_memory_allocation_read(
        reinterpret_cast<const void*>(first_live->payload + 1), &interior);
    check(interior_status == MELEE_WEB_SOURCE_MEMORY_READ_OK && !interior.live &&
              interior.allocation_generation == 0,
          "C1 census interior pointer was mistaken for an exact allocation lease");
    c1_heap_query_line(world, consumer, cycle, phase, "interior",
                       first_live->payload + 1, interior_status, interior);

    if (std::string_view(phase) == "cold") {
        MeleeWebSourceMemoryAllocation invalid{};
        const auto null_payload_status =
            melee_web_source_memory_allocation_read(nullptr, &invalid);
        const auto null_output_status = melee_web_source_memory_allocation_read(
            reinterpret_cast<const void*>(first_live->payload), nullptr);
        check(null_payload_status == MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT &&
                  null_output_status == MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT,
              "C1 census invalid query arguments were not refused explicitly");
        c1_heap_query_line(world, consumer, cycle, phase, "null_payload", 0,
                           null_payload_status, invalid, 0, 1);
        c1_heap_query_line(world, consumer, cycle, phase, "null_output",
                           first_live->payload, null_output_status, invalid, 0, 1);
        uintptr_t foreign_identity = before.arena_identity ^ uintptr_t{0x20};
        if (!foreign_identity || foreign_identity == before.arena_identity)
            ++foreign_identity;
        const int foreign_refused = !melee_web_gameplay_heap_owns(
            reinterpret_cast<const void*>(foreign_identity));
        check(foreign_refused,
              "C1 census did not refuse a foreign arena identity before traversal");
        c1_heap_query_line(world, consumer, cycle, phase, "foreign_owner",
                           foreign_identity,
                           -1, invalid, 0, 1);
    }

    C1HeapGuard after = c1_heap_guard();
    check(c1_heap_guards_equal(before, after),
          "C1 visitor/dump/query snapshot changed source owner, watermark, heap, roots, pools, classes, or ticks");
    std::cerr << "C1_HEAP_GUARD world=" << world << " consumer=" << consumer
              << " cycle=" << cycle << " phase=" << phase
              << " equal=1 source_healthy=1 world_equal=1 heap_owner=1 watermark=1"
              << " ticks=1 free_bytes=1 roots=1 classes=1 pools=1 gobj_used=1 proc_used=1\n";

    std::cerr << "C1_HEAP_SNAPSHOT world=" << world
              << " generation=" << before.context.world_generation
              << " consumer=" << consumer << " cycle=" << cycle
              << " phase=" << phase
              << " heap=" << before.context.source_heap_handle
              << " free=" << before.stats.heap_free_bytes
              << " watermark=" << before.context.allocation_generation_watermark
              << " rows=" << c1_heap_census.count << " overflow=0\n";
    for (size_t i = 0; i < c1_heap_census.count; ++i) {
        const auto& row = c1_heap_census.rows[i];
        std::cerr << "C1_HEAP_ALLOC world=" << world
                  << " generation=" << before.context.world_generation
                  << " consumer=" << consumer << " cycle=" << cycle
                  << " phase=" << phase << " payload=0x" << std::hex
                  << row.payload << std::dec
                  << " visitor_capacity=" << row.visitor_capacity
                  << " referent_capacity=" << row.referent_capacity
                  << " lease_status=" << static_cast<int>(row.lease_status)
                  << " live=" << static_cast<unsigned>(row.lease.live)
                  << " heap=" << row.lease.source_heap_handle
                  << " lease_world=" << row.lease.world_generation
                  << " requested=" << row.lease.requested_bytes
                  << " allocation_generation=" << row.lease.allocation_generation
                  << '\n';
    }
    std::cerr.flush();
    if (after_out) *after_out = std::move(after);
}

const char* c1_heap_owner_phase_name(uint8_t phase)
{
    switch (phase) {
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_BEFORE_LIGHT:
        return "before-light-preparation";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_LIGHT:
        return "after-light-preparation-before-e8";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_ONINIT:
        return "after-oninit";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_STAGE_LAST:
        return "after-stage-last-and-light-destroy";
    default:
        return "unknown";
    }
}

const char* c1_heap_owner_kind_name(uint8_t kind)
{
    switch (kind) {
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_OBJALLOC_POOL:
        return "objalloc_pool";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_DIRECTORY:
        return "class_directory";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_BUCKET:
        return "class_bucket_metadata";
    case MELEE_WEB_STADIUM_C1_HEAP_OWNER_CLASS_SLAB:
        return "class_slab";
    default:
        return "unknown";
    }
}

const char* c1_heap_owner_objalloc_label(uintptr_t owner)
{
    if (owner == reinterpret_cast<uintptr_t>(HSD_AObjGetAllocData())) return "AObj";
    if (owner == reinterpret_cast<uintptr_t>(HSD_RObjGetAllocData())) return "RObj";
    if (owner == reinterpret_cast<uintptr_t>(HSD_RvalueObjGetAllocData())) return "RvalueObj";
    if (owner == reinterpret_cast<uintptr_t>(HSD_VecGetAllocData())) return "Vec";
    if (owner == reinterpret_cast<uintptr_t>(HSD_MtxGetAllocData())) return "Mtx";
    if (owner == reinterpret_cast<uintptr_t>(HSD_RenderGetAllocData())) return "Render";
    if (owner == reinterpret_cast<uintptr_t>(HSD_TevRegGetAllocData())) return "TevReg";
    if (owner == reinterpret_cast<uintptr_t>(HSD_ChanGetAllocData())) return "Chan";
    if (owner == reinterpret_cast<uintptr_t>(&gobj_alloc_data)) return "GObj";
    if (owner == reinterpret_cast<uintptr_t>(&gobjproc_alloc_data)) return "GObjProc";
    return "other_objalloc";
}

void c1_publish_heap_owner_observations(const char* scope,
                                       bool require_v24_markers)
{
    const size_t count = melee_web_stadium_c1_heap_owner_count();
    const size_t markers = melee_web_stadium_c1_heap_owner_marker_count();
    const size_t pending = melee_web_stadium_c1_heap_owner_pending_count();
    const size_t invalid = melee_web_stadium_c1_heap_owner_invalid_count();
    const bool armed = melee_web_stadium_c1_heap_owner_armed() != 0;
    const bool overflow = melee_web_stadium_c1_heap_owner_overflowed() != 0;
    bool complete = armed && !overflow && pending == 0 && invalid == 0 &&
        (!require_v24_markers || markers == 4);
    uint8_t expected_phases[] = {
        MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_BEFORE_LIGHT,
        MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_LIGHT,
        MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_ONINIT,
        MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_STAGE_LAST,
    };
    uint32_t prior_marker_count = 0;
    for (size_t i = 0; i < markers; ++i) {
        MeleeWebStadiumC1HeapOwnerMarker marker{};
        if (!melee_web_stadium_c1_heap_owner_marker_read(i, &marker)) {
            complete = false;
            continue;
        }
        if (require_v24_markers &&
            (i >= 4 || marker.phase != expected_phases[i] ||
             marker.event_count < prior_marker_count || !marker.census_complete))
            complete = false;
        prior_marker_count = marker.event_count;
    }
    if (require_v24_markers && markers != 4) complete = false;
    if (armed && count == 0) complete = false;
    for (size_t i = 0; i < count; ++i) {
        MeleeWebStadiumC1HeapOwnerEvent event{};
        const bool readable = melee_web_stadium_c1_heap_owner_read(i, &event) != 0;
        const bool exact_live_lease = readable &&
            event.lease_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
            event.live == 1 && event.world_generation != 0 &&
            event.allocation_generation != 0 && event.source_heap_handle >= 0 &&
            event.hsd_requested_bytes == event.lease_requested_bytes;
        if (!exact_live_lease || event.reserved != 0 || !event.payload ||
            !event.owner_identity || !event.hsd_requested_bytes)
            complete = false;
    }
    const char* result_status = !armed && !require_v24_markers
        ? "disabled" : complete ? "complete" : "unavailable";
    std::cerr << "C1_HEAP_OWNER_META scope=" << scope
              << " status=" << result_status
              << " armed=" << armed
              << " rows=" << count
              << " row_bytes=" << melee_web_stadium_c1_heap_owner_row_bytes()
              << " capacity=" << MELEE_WEB_STADIUM_C1_HEAP_OWNER_CAPACITY
              << " buffer_bytes=" << melee_web_stadium_c1_heap_owner_buffer_bytes()
              << " overflow=" << overflow
              << " overflow_count=" << melee_web_stadium_c1_heap_owner_overflow_count()
              << " invalid_count=" << invalid
              << " pending_rows=" << pending
              << " markers=" << markers << '\n';
    prior_marker_count = 0;
    for (size_t i = 0; i < markers; ++i) {
        MeleeWebStadiumC1HeapOwnerMarker marker{};
        if (!melee_web_stadium_c1_heap_owner_marker_read(i, &marker)) continue;
        const uint32_t next_sequence = marker.event_count + 1;
        std::cerr << "C1_HEAP_OWNER_MARK scope=" << scope
                  << " phase=" << c1_heap_owner_phase_name(marker.phase)
                  << " first_sequence=" << prior_marker_count + 1
                  << " next_sequence=" << next_sequence
                  << " census_complete=" << static_cast<unsigned>(marker.census_complete)
                  << '\n';
        prior_marker_count = marker.event_count;
    }
    for (size_t i = 0; i < count; ++i) {
        MeleeWebStadiumC1HeapOwnerEvent event{};
        if (!melee_web_stadium_c1_heap_owner_read(i, &event)) {
            complete = false;
            continue;
        }
        const char* phase = require_v24_markers ? "before-light-preparation"
                                                 : "asset-free-control";
        if (require_v24_markers) {
            for (size_t marker_index = 0; marker_index < markers; ++marker_index) {
                MeleeWebStadiumC1HeapOwnerMarker marker{};
                if (melee_web_stadium_c1_heap_owner_marker_read(marker_index,
                                                                 &marker) &&
                    i + 1 <= marker.event_count) {
                    phase = c1_heap_owner_phase_name(marker.phase);
                    break;
                }
                phase = "after-stage-last-and-light-destroy";
            }
        }
        const char* owner_label = event.kind ==
                MELEE_WEB_STADIUM_C1_HEAP_OWNER_OBJALLOC_POOL
            ? c1_heap_owner_objalloc_label(event.owner_identity)
            : c1_heap_owner_kind_name(event.kind);
        std::cerr << "C1_HEAP_OWNER_EVENT scope=" << scope
                  << " sequence=" << i + 1 << " phase=" << phase
                  << " kind=" << c1_heap_owner_kind_name(event.kind)
                  << " owner_label=" << owner_label
                  << " owner=0x" << std::hex << event.owner_identity
                  << " payload=0x" << event.payload << std::dec
                  << " owner_size=" << event.owner_size
                  << " auxiliary=" << event.auxiliary
                  << " hsd_requested=" << event.hsd_requested_bytes
                  << " lease_status=" << static_cast<unsigned>(event.lease_status)
                  << " live=" << static_cast<unsigned>(event.live)
                  << " heap=" << event.source_heap_handle
                  << " lease_requested=" << event.lease_requested_bytes
                  << " world=" << event.world_generation
                  << " allocation_generation=" << event.allocation_generation
                  << '\n';
    }
    std::cerr << "C1_HEAP_OWNER_RESULT scope=" << scope
              << " status=" << result_status
              << " rows=" << count << " markers=" << markers
              << " overflow=" << overflow
              << " overflow_count=" << melee_web_stadium_c1_heap_owner_overflow_count()
              << " invalid_count=" << invalid
              << " pending_rows=" << pending << '\n';
    std::cerr.flush();
}

bool c1_gameplay_stats_equal(const MeleeWebGameplayStats& a,
                             const MeleeWebGameplayStats& b)
{
    return a.ticks == b.ticks && a.objects == b.objects &&
           a.processes == b.processes && a.object_peak == b.object_peak &&
           a.process_peak == b.process_peak &&
           a.heap_free_bytes == b.heap_free_bytes &&
           a.generation == b.generation;
}

const char* c1_heap_graph_failure_name(C1HeapGraphFailure failure)
{
    switch (failure) {
    case C1HeapGraphFailure::none: return "none";
    case C1HeapGraphFailure::lease_query_refused: return "lease_query_refused";
    case C1HeapGraphFailure::exact_lease_absent_or_not_live:
        return "exact_lease_absent_or_not_live";
    case C1HeapGraphFailure::lease_owner_mismatch: return "lease_owner_mismatch";
    case C1HeapGraphFailure::requested_size_mismatch: return "requested_size_mismatch";
    case C1HeapGraphFailure::missing_census_cell: return "missing_census_cell";
    case C1HeapGraphFailure::census_lease_mismatch: return "census_lease_mismatch";
    case C1HeapGraphFailure::node_budget_exceeded: return "node_budget_exceeded";
    case C1HeapGraphFailure::node_capacity_exceeded: return "node_capacity_exceeded";
    case C1HeapGraphFailure::invalid_node_kind: return "invalid_node_kind";
    case C1HeapGraphFailure::node_kind_alias_mismatch: return "node_kind_alias_mismatch";
    case C1HeapGraphFailure::same_path_cycle: return "same_path_cycle";
    }
    return "invalid_failure_code";
}

C1HeapCensusRow* c1_heap_graph_find_row(C1HeapGraphWalkState& state,
                                        uintptr_t payload)
{
    for (size_t i = 0; i < state.row_count; ++i)
        if (state.rows[i].payload == payload) return &state.rows[i];
    return nullptr;
}

bool c1_heap_graph_request_bytes(C1HeapGraphNodeKind kind, uint32_t* bytes)
{
    if (!bytes) return false;
    switch (kind) {
    case C1HeapGraphNodeKind::island_segment:
        *bytes = kC1HeapGraphIslandRequestBytes;
        return true;
    case C1HeapGraphNodeKind::ground_start_callback:
        *bytes = kC1HeapGraphCallbackRequestBytes;
        return true;
    }
    return false;
}

C1HeapGraphFailure c1_heap_graph_validate_live_lease(
    MeleeWebSourceMemoryReadStatus status,
    const MeleeWebSourceMemoryAllocation& lease,
    int32_t expected_heap, uint64_t expected_world,
    uint64_t generation_watermark, uint32_t expected_request_bytes)
{
    if (status != MELEE_WEB_SOURCE_MEMORY_READ_OK)
        return C1HeapGraphFailure::lease_query_refused;
    if (lease.live != 1 || !lease.allocation_generation ||
        lease.allocation_generation > generation_watermark)
        return C1HeapGraphFailure::exact_lease_absent_or_not_live;
    for (uint8_t reserved : lease.reserved)
        if (reserved) return C1HeapGraphFailure::census_lease_mismatch;
    if (lease.source_heap_handle != expected_heap ||
        lease.world_generation != expected_world)
        return C1HeapGraphFailure::lease_owner_mismatch;
    if (lease.requested_bytes != expected_request_bytes)
        return C1HeapGraphFailure::requested_size_mismatch;
    return C1HeapGraphFailure::none;
}

bool c1_heap_graph_walk_root(C1HeapGraphWalkState& state, size_t root_index,
                             const C1HeapGraphRoot& root,
                             C1HeapGraphResolver resolver, void* resolver_context,
                             C1HeapGraphRootResult* result)
{
    if (!result || !resolver || !state.rows ||
        root_index >= kC1HeapGraphRootCount) {
        if (result) {
            *result = {};
            result->failure = C1HeapGraphFailure::node_capacity_exceeded;
        }
        state.unavailable = true;
        return false;
    }
    *result = {};
    if (root.kind != C1HeapGraphNodeKind::island_segment &&
        root.kind != C1HeapGraphNodeKind::ground_start_callback) {
        result->failure = C1HeapGraphFailure::invalid_node_kind;
        result->failure_payload = root.payload;
        state.unavailable = true;
        return false;
    }
    result->attempted = true;
    uintptr_t payload = root.payload;
    if (!payload) return true;

    const uint16_t root_bit = static_cast<uint16_t>(uint16_t{1} << root_index);
    while (payload) {
        C1HeapCensusRow* row = c1_heap_graph_find_row(state, payload);
        if (!row) {
            result->failure = C1HeapGraphFailure::missing_census_cell;
            result->failure_payload = payload;
            state.unavailable = true;
            return false;
        }
        if (row->graph_root_mask == 0) {
            if (state.unique_nodes >= state.budget) {
                result->failure = C1HeapGraphFailure::node_budget_exceeded;
                result->failure_payload = payload;
                state.unavailable = true;
                return false;
            }
            if (state.unique_nodes >= kC1HeapGraphCapacity) {
                result->failure = C1HeapGraphFailure::node_capacity_exceeded;
                result->failure_payload = payload;
                state.unavailable = true;
                return false;
            }
            C1HeapGraphResolvedNode resolved{};
            C1HeapGraphFailure failure = C1HeapGraphFailure::none;
            if (!resolver(payload, root.kind, &resolved, &failure,
                          resolver_context)) {
                result->failure = failure == C1HeapGraphFailure::none
                    ? C1HeapGraphFailure::lease_query_refused : failure;
                result->failure_payload = payload;
                state.unavailable = true;
                return false;
            }
            row->graph_next = resolved.next;
            row->graph_kind = static_cast<uint8_t>(root.kind);
            row->graph_root_mask = root_bit;
            ++state.unique_nodes;
        } else {
            if (row->graph_kind != static_cast<uint8_t>(root.kind)) {
                result->failure = C1HeapGraphFailure::node_kind_alias_mismatch;
                result->failure_payload = payload;
                state.unavailable = true;
                return false;
            }
            if (row->graph_root_mask & root_bit) {
                row->graph_cycle_mask |= root_bit;
                ++state.cycles;
                result->failure = C1HeapGraphFailure::same_path_cycle;
                result->failure_payload = payload;
                state.unavailable = true;
                return false;
            }
            ++state.aliases;
            row->graph_root_mask |= root_bit;
        }
        if (result->visited == UINT32_MAX) {
            result->failure = C1HeapGraphFailure::node_budget_exceeded;
            result->failure_payload = payload;
            state.unavailable = true;
            return false;
        }
        ++result->visited;
        payload = row->graph_next;
    }
    return true;
}

std::array<C1HeapGraphRoot, kC1HeapGraphRootCount>
c1_heap_graph_current_roots()
{
    // Source keeps independent list heads, tail cursors and repartition roots.
    // B334 intentionally links some of those roots into next/x4, so the walk
    // keeps a membership bit per field and treats only same-path repeats as cycles.
    return {{
        {"island.next", reinterpret_cast<uintptr_t>(mpIsland_80458E88.next),
         C1HeapGraphNodeKind::island_segment},
        {"island.x4", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x4),
         C1HeapGraphNodeKind::island_segment},
        {"island.x8", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x8),
         C1HeapGraphNodeKind::island_segment},
        {"island.xC", reinterpret_cast<uintptr_t>(mpIsland_80458E88.xC),
         C1HeapGraphNodeKind::island_segment},
        {"island.x10", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x10),
         C1HeapGraphNodeKind::island_segment},
        {"island.x14", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x14),
         C1HeapGraphNodeKind::island_segment},
        {"island.x18", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x18),
         C1HeapGraphNodeKind::island_segment},
        {"island.x1C", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x1C),
         C1HeapGraphNodeKind::island_segment},
        {"island.x20", reinterpret_cast<uintptr_t>(mpIsland_80458E88.x20),
         C1HeapGraphNodeKind::island_segment},
        {"stage_info.x6A4", reinterpret_cast<uintptr_t>(
             melee_web_stadium_c1_stage_info_x6A4_root()),
         C1HeapGraphNodeKind::ground_start_callback},
    }};
}

bool c1_heap_graph_source_resolver(
    uintptr_t payload, C1HeapGraphNodeKind kind,
    C1HeapGraphResolvedNode* out, C1HeapGraphFailure* failure,
    void* context)
{
    auto refuse = [failure](C1HeapGraphFailure reason) {
        if (failure) *failure = reason;
        return false;
    };
    if (!payload || !out || !failure || !context)
        return refuse(C1HeapGraphFailure::lease_query_refused);

    const auto& guard = *static_cast<const C1HeapGuard*>(context);
    MeleeWebSourceMemoryAllocation lease{};
    const auto status = melee_web_source_memory_allocation_read(
        reinterpret_cast<const void*>(payload), &lease);
    uint32_t expected_bytes = 0;
    if (!c1_heap_graph_request_bytes(kind, &expected_bytes))
        return refuse(C1HeapGraphFailure::invalid_node_kind);
    const C1HeapGraphFailure lease_failure = c1_heap_graph_validate_live_lease(
        status, lease, guard.context.source_heap_handle,
        guard.context.world_generation,
        guard.context.allocation_generation_watermark, expected_bytes);
    if (lease_failure != C1HeapGraphFailure::none)
        return refuse(lease_failure);

    C1HeapCensusRow* row = nullptr;
    for (size_t i = 0; i < c1_heap_census.count; ++i) {
        if (c1_heap_census.rows[i].payload == payload) {
            if (row) return refuse(C1HeapGraphFailure::census_lease_mismatch);
            row = &c1_heap_census.rows[i];
        }
    }
    if (!row) return refuse(C1HeapGraphFailure::missing_census_cell);
    if (row->lease_status != MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        row->visitor_capacity != row->referent_capacity ||
        row->lease.live != 1 ||
        row->lease.source_heap_handle != lease.source_heap_handle ||
        row->lease.world_generation != lease.world_generation ||
        row->lease.allocation_generation != lease.allocation_generation ||
        row->lease.requested_bytes != lease.requested_bytes)
        return refuse(C1HeapGraphFailure::census_lease_mismatch);

    uintptr_t next = 0;
    if (kind == C1HeapGraphNodeKind::island_segment) {
        const auto* node = reinterpret_cast<const mp_UnkStruct0*>(payload);
        next = reinterpret_cast<uintptr_t>(node->next);
    } else {
        void* next_pointer = nullptr;
        std::memcpy(&next_pointer, reinterpret_cast<const void*>(payload),
                    sizeof(next_pointer));
        next = reinterpret_cast<uintptr_t>(next_pointer);
    }
    *out = {next};
    *failure = C1HeapGraphFailure::none;
    return true;
}

bool c1_heap_graph_root_snapshots_equal(
    const std::array<C1HeapGraphRoot, kC1HeapGraphRootCount>& a,
    const std::array<C1HeapGraphRoot, kC1HeapGraphRootCount>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].payload != b[i].payload || a[i].kind != b[i].kind)
            return false;
    return true;
}

void c1_heap_graph_emit_root_snapshots(
    const char* phase,
    const std::array<C1HeapGraphRoot, kC1HeapGraphRootCount>& before,
    const std::array<C1HeapGraphRoot, kC1HeapGraphRootCount>& after,
    bool valid,
    const std::array<C1HeapGraphRootResult, kC1HeapGraphRootCount>* walks)
{
    for (size_t i = 0; i < before.size(); ++i) {
        const C1HeapGraphRootResult walk = walks ? (*walks)[i]
                                                  : C1HeapGraphRootResult{};
        std::cerr << "C1_HEAP_GRAPH_ROOT phase=" << phase
                  << " index=" << i << " name=" << before[i].name
                  << " payload_before=0x" << std::hex << before[i].payload
                  << " payload_after=0x" << after[i].payload << std::dec
                  << " unchanged=" << (before[i].payload == after[i].payload)
                  << " snapshot_valid=" << valid
                  << " walk_attempted=" << walk.attempted
                  << " walk_status=" << c1_heap_graph_failure_name(walk.failure)
                  << " failure_payload=0x" << std::hex
                  << walk.failure_payload << std::dec
                  << " visited=" << walk.visited << '\n';
    }
}

bool c1_heap_graph_save_reached_identities(
    bool graph_complete, const char** reason)
{
    c1_heap_graph_saved_state.count = 0;
    c1_heap_graph_saved_state.complete = false;
    if (!reason) return false;
    *reason = "identity_census_unavailable";
    if (c1_heap_census.overflow || c1_heap_census.invalid ||
        c1_heap_census.count > kC1HeapGraphCapacity) {
        *reason = "census_incomplete_or_over_capacity";
        return false;
    }
    if (!graph_complete) {
        *reason = "root_graph_incomplete";
        return false;
    }
    for (size_t i = 0; i < c1_heap_census.count; ++i) {
        const auto& row = c1_heap_census.rows[i];
        if (!row.graph_root_mask) continue;
        if (!row.payload ||
            c1_heap_graph_saved_state.count == kC1HeapGraphCapacity) {
            *reason = "reached_identity_capacity_exceeded";
            return false;
        }
        c1_heap_graph_saved_state.identities[
            c1_heap_graph_saved_state.count++] = {
                row.payload, row.lease.source_heap_handle,
                row.lease.requested_bytes, row.lease.world_generation,
                row.lease.allocation_generation};
    }
    *reason = "none";
    return true;
}

void c1_heap_graph_emit_after_oninit(
    bool census_complete, const MeleeWebGameplayStats& boundary_stats)
{
    const char* reason = "unknown_observer_error";
    bool graph_complete = false;
    bool identities_complete = false;
    bool observer_failed = false;
    bool pure = false;
    C1HeapGuard before{};
    C1HeapGuard after{};
    bool have_before = false;
    std::array<C1HeapGraphRoot, kC1HeapGraphRootCount> roots_before =
        c1_heap_graph_current_roots();
    std::array<C1HeapGraphRoot, kC1HeapGraphRootCount> roots_after = roots_before;
    std::array<C1HeapGraphRootResult, kC1HeapGraphRootCount> results{};
    c1_heap_graph_walk_state = {};
    c1_heap_graph_saved_state.complete = false;
    c1_heap_graph_saved_state.count = 0;

    try {
        before = c1_heap_guard();
        have_before = true;
        roots_before = c1_heap_graph_current_roots();
        if (!c1_gameplay_stats_equal(before.stats, boundary_stats)) {
            reason = "boundary_stats_changed_before_observation";
        } else if (!census_complete) {
            reason = "after_oninit_census_unavailable";
        } else {
            c1_heap_graph_walk_state.rows = c1_heap_census.rows;
            c1_heap_graph_walk_state.row_count = c1_heap_census.count;
            for (size_t i = 0; i < c1_heap_census.count; ++i) {
                auto& row = c1_heap_census.rows[i];
                row.graph_next = 0;
                row.graph_root_mask = 0;
                row.graph_cycle_mask = 0;
                row.graph_kind = 0;
                if (row.lease_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                    row.lease.live == 1 &&
                    row.lease.source_heap_handle == before.context.source_heap_handle &&
                    row.lease.world_generation == before.context.world_generation)
                    ++c1_heap_graph_walk_state.budget;
            }
            bool roots_complete = true;
            for (size_t i = 0; i < roots_before.size(); ++i) {
                if (!c1_heap_graph_walk_root(
                        c1_heap_graph_walk_state, i, roots_before[i],
                        c1_heap_graph_source_resolver, &before, &results[i])) {
                    roots_complete = false;
                    if (std::strcmp(reason, "unknown_observer_error") == 0)
                        reason = c1_heap_graph_failure_name(results[i].failure);
                }
            }
            graph_complete = roots_complete &&
                !c1_heap_graph_walk_state.unavailable;
            const char* identity_reason = "none";
            identities_complete = c1_heap_graph_save_reached_identities(
                graph_complete, &identity_reason);
            if (!identities_complete && graph_complete)
                reason = identity_reason;
        }
        roots_after = c1_heap_graph_current_roots();
        after = c1_heap_guard();
        pure = c1_heap_guards_equal(before, after) &&
            c1_heap_graph_root_snapshots_equal(roots_before, roots_after);
        if (!pure) reason = "source_or_root_purity_guard_failed";
        else if (graph_complete && identities_complete) reason = "none";
    } catch (const std::exception&) {
        observer_failed = true;
        reason = "observer_exception";
    } catch (...) {
        observer_failed = true;
        reason = "unknown_observer_exception";
    }
    if (!have_before) pure = false;
    const bool complete = graph_complete && identities_complete && pure &&
        have_before && !observer_failed;
    c1_heap_graph_saved_state.complete = complete;
    std::cerr << "C1_HEAP_GRAPH_RESULT phase=after-oninit status="
              << (complete ? "complete" : "unavailable")
              << " reason=" << (complete ? "none" : reason)
              << " root_count=" << kC1HeapGraphRootCount
              << " unique_nodes=" << c1_heap_graph_walk_state.unique_nodes
              << " aliases=" << c1_heap_graph_walk_state.aliases
              << " cycles=" << c1_heap_graph_walk_state.cycles
              << " census_heap_row_budget=" << c1_heap_graph_walk_state.budget
              << " identity_capacity=" << kC1HeapGraphCapacity
              << " census_row_bytes=" << sizeof(C1HeapCensusRow)
              << " census_storage_bytes=" << sizeof(c1_heap_census.rows)
              << " census_state_bytes=" << sizeof(c1_heap_census)
              << " graph_state_bytes=" << sizeof(c1_heap_graph_walk_state)
              << " saved_identities=" << c1_heap_graph_saved_state.count
              << " saved_identity_row_bytes=" << sizeof(C1HeapGraphSavedIdentity)
              << " saved_identity_storage_bytes="
              << sizeof(c1_heap_graph_saved_state.identities)
              << " saved_state_bytes=" << sizeof(c1_heap_graph_saved_state)
              << " graph_auxiliary_state_bytes="
              << (sizeof(c1_heap_graph_walk_state) +
                  sizeof(c1_heap_graph_saved_state))
              << " census_and_graph_static_bytes="
              << (sizeof(c1_heap_census) +
                  sizeof(c1_heap_graph_walk_state) +
                  sizeof(c1_heap_graph_saved_state))
              << " identity_set_complete=" << complete
              << " pure=" << pure << '\n';
    c1_heap_graph_emit_root_snapshots(
        "after-oninit", roots_before, roots_after, pure, &results);
    for (size_t i = 0; i < c1_heap_census.count; ++i) {
        const auto& row = c1_heap_census.rows[i];
        if (!row.graph_root_mask) continue;
        std::cerr << "C1_HEAP_GRAPH_NODE phase=after-oninit payload=0x"
                  << std::hex << row.payload << std::dec
                  << " requested=" << row.lease.requested_bytes
                  << " heap=" << row.lease.source_heap_handle
                  << " world=" << row.lease.world_generation
                  << " allocation_generation="
                  << row.lease.allocation_generation
                  << " capacity=" << row.visitor_capacity
                  << " root_mask=0x" << std::hex << row.graph_root_mask
                  << " cycle_mask=0x" << row.graph_cycle_mask << std::dec
                  << " membership=" << (complete ? "complete" : "partial")
                  << '\n';
    }
    std::cerr.flush();
}

void c1_heap_graph_emit_after_stage_last(bool census_complete)
{
    const char* reason = "unknown_observer_error";
    bool pure = false;
    bool queries_complete = c1_heap_graph_saved_state.complete;
    bool roots_stable = false;
    C1HeapGuard before{};
    C1HeapGuard after{};
    std::array<C1HeapGraphRoot, kC1HeapGraphRootCount> roots_before =
        c1_heap_graph_current_roots();
    std::array<C1HeapGraphRoot, kC1HeapGraphRootCount> roots_after = roots_before;
    bool have_before = false;
    try {
        before = c1_heap_guard();
        have_before = true;
        roots_before = c1_heap_graph_current_roots();
        if (!c1_heap_graph_saved_state.complete) {
            reason = "after_oninit_identity_set_unavailable";
            queries_complete = false;
        } else {
            for (size_t i = 0; i < c1_heap_graph_saved_state.count; ++i) {
                const auto& prior = c1_heap_graph_saved_state.identities[i];
                MeleeWebSourceMemoryAllocation lease{};
                const auto status = melee_web_source_memory_allocation_read(
                    reinterpret_cast<const void*>(prior.payload), &lease);
                const char* classification = "unavailable";
                bool valid = status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                    lease.live <= 1 &&
                    lease.source_heap_handle == prior.heap &&
                    lease.world_generation == prior.world_generation &&
                    prior.heap == before.context.source_heap_handle &&
                    prior.world_generation == before.context.world_generation &&
                    (!lease.live || lease.allocation_generation <=
                        before.context.allocation_generation_watermark) &&
                    lease.reserved[0] == 0 && lease.reserved[1] == 0 &&
                    lease.reserved[2] == 0 && lease.reserved[3] == 0 &&
                    lease.reserved[4] == 0 && lease.reserved[5] == 0 &&
                    lease.reserved[6] == 0;
                if (valid && lease.live == 0 && !lease.requested_bytes &&
                    !lease.allocation_generation) {
                    classification = "absent_current_sdk_lease";
                } else if (valid && lease.live == 1 &&
                           lease.requested_bytes == prior.requested_bytes &&
                           lease.allocation_generation ==
                               prior.allocation_generation) {
                    classification = "unchanged_live_sdk_lease";
                } else if (valid &&
                           c1_heap_graph_is_new_generation_reuse(prior, lease)) {
                    classification = "new_generation_reuse";
                } else {
                    valid = false;
                    queries_complete = false;
                    reason = status == MELEE_WEB_SOURCE_MEMORY_READ_OK
                        ? "saved_identity_lease_mismatch" : "saved_identity_query_refused";
                }
                std::cerr << "C1_HEAP_GRAPH_LEASE phase=after-stage-last"
                          << " payload=0x" << std::hex << prior.payload << std::dec
                          << " prior_requested=" << prior.requested_bytes
                          << " prior_allocation_generation="
                          << prior.allocation_generation
                          << " status=" << static_cast<int>(status)
                          << " live=" << static_cast<unsigned>(lease.live)
                          << " requested=" << lease.requested_bytes
                          << " heap=" << lease.source_heap_handle
                          << " world=" << lease.world_generation
                          << " allocation_generation=" << lease.allocation_generation
                          << " classification=" << classification
                          << " valid=" << valid << '\n';
            }
        }
        roots_after = c1_heap_graph_current_roots();
        after = c1_heap_guard();
        roots_stable = c1_heap_graph_root_snapshots_equal(roots_before, roots_after);
        pure = c1_heap_guards_equal(before, after) && roots_stable;
        if (!pure) {
            queries_complete = false;
            reason = "source_or_root_purity_guard_failed";
        } else if (!census_complete) {
            queries_complete = false;
            reason = "after_stage_last_census_unavailable";
        } else if (queries_complete) {
            reason = "none";
        }
    } catch (const std::exception&) {
        queries_complete = false;
        reason = "observer_exception";
    } catch (...) {
        queries_complete = false;
        reason = "unknown_observer_exception";
    }
    if (!have_before) {
        pure = false;
        queries_complete = false;
    }
    const bool complete = queries_complete && pure && census_complete;
    std::cerr << "C1_HEAP_GRAPH_RESULT phase=after-stage-last status="
              << (complete ? "complete" : "unavailable")
              << " reason=" << (complete ? "none" : reason)
              << " saved_identities=" << c1_heap_graph_saved_state.count
              << " identity_set_complete=" << c1_heap_graph_saved_state.complete
              << " census_complete=" << census_complete
              << " roots_stable=" << roots_stable << " pure=" << pure << '\n';
    c1_heap_graph_emit_root_snapshots(
        "after-stage-last", roots_before, roots_after, roots_stable, nullptr);
    std::cerr.flush();
}

struct C1HeapGraphControlNode {
    uintptr_t payload{};
    uintptr_t next{};
    int32_t heap{};
    uint32_t requested_bytes{};
    uint64_t world_generation{};
    uint64_t allocation_generation{};
    bool live{};
};

struct C1HeapGraphControlContext {
    C1HeapGraphControlNode nodes[4]{};
    size_t count{};
    size_t queries{};
    size_t dereferences{};
    int32_t expected_heap{};
    uint64_t expected_world_generation{};
};

bool c1_heap_graph_control_resolver(
    uintptr_t payload, C1HeapGraphNodeKind kind,
    C1HeapGraphResolvedNode* out, C1HeapGraphFailure* failure,
    void* context)
{
    auto refuse = [failure](C1HeapGraphFailure reason) {
        if (failure) *failure = reason;
        return false;
    };
    if (!payload || !out || !failure || !context)
        return refuse(C1HeapGraphFailure::lease_query_refused);
    auto& state = *static_cast<C1HeapGraphControlContext*>(context);
    ++state.queries;
    C1HeapGraphControlNode* node = nullptr;
    for (size_t i = 0; i < state.count; ++i)
        if (state.nodes[i].payload == payload) {
            node = &state.nodes[i];
            break;
        }
    uint32_t expected_bytes = 0;
    if (!c1_heap_graph_request_bytes(kind, &expected_bytes))
        return refuse(C1HeapGraphFailure::invalid_node_kind);
    MeleeWebSourceMemoryAllocation lease{};
    if (node) {
        lease.source_heap_handle = node->heap;
        lease.requested_bytes = node->requested_bytes;
        lease.world_generation = node->world_generation;
        lease.allocation_generation = node->allocation_generation;
        lease.live = node->live ? 1 : 0;
    }
    const C1HeapGraphFailure lease_failure = c1_heap_graph_validate_live_lease(
        MELEE_WEB_SOURCE_MEMORY_READ_OK, lease, state.expected_heap,
        state.expected_world_generation, UINT64_MAX, expected_bytes);
    if (lease_failure != C1HeapGraphFailure::none)
        return refuse(lease_failure);
    ++state.dereferences;
    *out = {node->next};
    *failure = C1HeapGraphFailure::none;
    return true;
}

void c1_heap_graph_control_copy_census(
    std::array<C1HeapCensusRow, 4>& rows,
    const C1HeapGraphControlContext& context)
{
    for (size_t i = 0; i < context.count; ++i) {
        const auto& node = context.nodes[i];
        auto& row = rows[i];
        row = C1HeapCensusRow{};
        row.payload = node.payload;
        row.lease_status = MELEE_WEB_SOURCE_MEMORY_READ_OK;
        row.lease.source_heap_handle = node.heap;
        row.lease.requested_bytes = node.requested_bytes;
        row.lease.world_generation = node.world_generation;
        row.lease.allocation_generation = node.allocation_generation;
        row.lease.live = node.live ? 1 : 0;
    }
}

void c1_heap_graph_control_reset(
    C1HeapGraphWalkState& walk, std::array<C1HeapCensusRow, 4>& rows,
    size_t row_count, size_t budget)
{
    walk = {};
    walk.rows = rows.data();
    walk.row_count = row_count;
    walk.budget = budget;
    for (auto& row : rows) {
        row.graph_next = 0;
        row.graph_root_mask = 0;
        row.graph_cycle_mask = 0;
        row.graph_kind = 0;
    }
}

void run_stadium_owner_graph_controls()
{
    constexpr int32_t heap = 3;
    constexpr uint64_t world = 11;
    constexpr uintptr_t first = 0x1000;
    constexpr uintptr_t second = 0x2000;
    constexpr uintptr_t mismatch = 0x3000;

    std::array<C1HeapCensusRow, 4> rows{};
    C1HeapGraphWalkState walk{};
    C1HeapGraphControlContext fake{};
    fake.expected_heap = heap;
    fake.expected_world_generation = world;

    fake.count = 2;
    fake.nodes[0] = {first, second, heap, kC1HeapGraphIslandRequestBytes,
                     world, 1, true};
    fake.nodes[1] = {second, 0, heap, kC1HeapGraphIslandRequestBytes,
                     world, 2, true};
    c1_heap_graph_control_copy_census(rows, fake);
    c1_heap_graph_control_reset(walk, rows, fake.count, fake.count);
    C1HeapGraphRootResult first_result{}, alias_result{};
    check(c1_heap_graph_walk_root(
              walk, 0,
              {"control.head", first, C1HeapGraphNodeKind::island_segment},
              c1_heap_graph_control_resolver, &fake, &first_result) &&
              c1_heap_graph_walk_root(
                  walk, 1,
                  {"control.tail", second,
                   C1HeapGraphNodeKind::island_segment},
                  c1_heap_graph_control_resolver, &fake, &alias_result) &&
              walk.unique_nodes == 2 && walk.aliases == 1 && walk.cycles == 0 &&
              rows[1].graph_root_mask == 3 && fake.dereferences == 2,
          "Owner graph alias control counted a cross-root alias as a cycle");

    fake = {};
    fake.expected_heap = heap;
    fake.expected_world_generation = world;
    fake.count = 2;
    fake.nodes[0] = {first, second, heap, kC1HeapGraphIslandRequestBytes,
                     world, 1, true};
    fake.nodes[1] = {second, first, heap, kC1HeapGraphIslandRequestBytes,
                     world, 2, true};
    c1_heap_graph_control_copy_census(rows, fake);
    c1_heap_graph_control_reset(walk, rows, fake.count, fake.count);
    C1HeapGraphRootResult cycle_result{};
    check(!c1_heap_graph_walk_root(
              walk, 0,
              {"control.cycle", first,
               C1HeapGraphNodeKind::island_segment},
              c1_heap_graph_control_resolver, &fake, &cycle_result) &&
              cycle_result.failure == C1HeapGraphFailure::same_path_cycle &&
              walk.cycles == 1 && walk.unavailable &&
              rows[0].graph_cycle_mask == 1 && fake.dereferences == 2,
          "Owner graph same-path cycle was not rejected as unavailable");

    fake = {};
    fake.expected_heap = heap;
    fake.expected_world_generation = world;
    fake.count = 1;
    fake.nodes[0] = {mismatch, 0, heap, kC1HeapGraphIslandRequestBytes,
                     world + 1, 1, true};
    c1_heap_graph_control_copy_census(rows, fake);
    c1_heap_graph_control_reset(walk, rows, fake.count, fake.count);
    C1HeapGraphRootResult lease_mismatch_result{};
    check(!c1_heap_graph_walk_root(
              walk, 0,
              {"control.lease-mismatch", mismatch,
               C1HeapGraphNodeKind::island_segment},
              c1_heap_graph_control_resolver, &fake,
              &lease_mismatch_result) &&
              lease_mismatch_result.failure ==
                  C1HeapGraphFailure::lease_owner_mismatch &&
              fake.queries == 1 && fake.dereferences == 0,
          "Owner graph dereferenced a lease from a different world");

    fake = {};
    fake.expected_heap = heap;
    fake.expected_world_generation = world;
    fake.count = 1;
    fake.nodes[0] = {mismatch, 0, heap,
                     kC1HeapGraphIslandRequestBytes + 1, world, 1, true};
    c1_heap_graph_control_copy_census(rows, fake);
    c1_heap_graph_control_reset(walk, rows, fake.count, fake.count);
    C1HeapGraphRootResult size_mismatch_result{};
    check(!c1_heap_graph_walk_root(
              walk, 0,
              {"control.size-mismatch", mismatch,
               C1HeapGraphNodeKind::island_segment},
              c1_heap_graph_control_resolver, &fake,
              &size_mismatch_result) &&
              size_mismatch_result.failure ==
                  C1HeapGraphFailure::requested_size_mismatch &&
              fake.queries == 1 && fake.dereferences == 0,
          "Owner graph dereferenced a cell with a mismatched source request size");

    fake = {};
    fake.expected_heap = heap;
    fake.expected_world_generation = world;
    fake.count = 1;
    fake.nodes[0] = {mismatch, 0, heap, kC1HeapGraphIslandRequestBytes,
                     world, 1, true};
    c1_heap_graph_control_reset(walk, rows, 0, fake.count);
    C1HeapGraphRootResult absent_census_result{};
    check(!c1_heap_graph_walk_root(
              walk, 0,
              {"control.absent-census-cell", mismatch,
               C1HeapGraphNodeKind::island_segment},
              c1_heap_graph_control_resolver, &fake,
              &absent_census_result) &&
              absent_census_result.failure ==
                  C1HeapGraphFailure::missing_census_cell &&
              fake.queries == 0 && fake.dereferences == 0,
          "Owner graph queried or dereferenced a node absent from the allocation census");

    const C1HeapGraphSavedIdentity prior_reuse{
        mismatch, heap, kC1HeapGraphIslandRequestBytes, world, 1};
    const MeleeWebSourceMemoryAllocation reused_with_new_size{
        heap, kC1HeapGraphCallbackRequestBytes, world, 2, 1, {}};
    check(c1_heap_graph_is_new_generation_reuse(
              prior_reuse, reused_with_new_size),
          "Owner graph did not classify a higher-generation same-payload lease with a changed request size as reuse");

    std::cout << "C1 source-owner graph alias/cycle/lease-world/request-size/missing-cell/generation-reuse controls passed; no source world or fixture\n";
}

void c1_v23_census_unavailable(const char* phase, const char* reason) noexcept
{
    std::cerr << "C1_V23_CENSUS status=unavailable world=0 consumer=original-oninit"
              << " cycle=0 phase=" << (phase ? phase : "unknown") << " error=";
    const char* value = reason ? reason : "unknown_observer_error";
    for (size_t i = 0; value[i] && i < 128; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        const bool safe = (c >= 'a' && c <= 'z') ||
                          (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                          c == '.' || c == ':';
        std::cerr << (safe ? static_cast<char>(c) : '_');
    }
    std::cerr << '\n';
    std::cerr.flush();
}

void c1_try_emit_v23_heap_census(bool& prior_failure, const char* phase,
                                 const MeleeWebGameplayStats& boundary_stats) noexcept
{
    if (prior_failure) {
        c1_v23_census_unavailable(phase, "prior_phase_failed");
        return;
    }
    try {
        const C1HeapGuard before = c1_heap_guard();
        check(c1_gameplay_stats_equal(before.stats, boundary_stats),
              "C1 V23 census boundary stats changed before observation");
        C1HeapGuard after{};
        c1_emit_heap_census(0, "original-oninit", 0, phase, before, &after);
        std::cerr << "C1_V23_CENSUS status=complete world=0"
                  << " consumer=original-oninit cycle=0 phase=" << phase
                  << " rows=" << c1_heap_census.count << '\n';
        std::cerr.flush();
    } catch (const std::exception& failure) {
        prior_failure = true;
        c1_v23_census_unavailable(phase, failure.what());
    } catch (...) {
        prior_failure = true;
        c1_v23_census_unavailable(phase, "unknown_observer_error");
    }
}

void run_stadium_cache_live_control(bool capture_heap_owners)
{
    namespace screen = melee_web::test::stadium_screen;
    struct Baseline {
        MeleeWebGameplayStats stats{};
        decltype(screen::runtime_roots_snapshot()) roots;
        decltype(screen::live_class_counts()) classes;
        decltype(screen::live_pool_counts()) pools{};
        uint32_t gobj_used{}, proc_used{};
    };
    struct State {
        unsigned world{};
        Baseline baseline;
        std::string consumer;
        uintptr_t prior_object_payload{};
        uint64_t prior_object_generation{};
        bool prior_object_has_exact_lease{};
        bool has_prior_object_payload{};
        unsigned records{};
        unsigned completed{};
    };
    auto observer = +[](const char* consumer, const char* phase, unsigned cycle,
                        const void* owned, void* user) -> int {
        auto& state = *static_cast<State*>(user);
        try {
            const std::string_view current_phase(phase);
            if (current_phase == "cold") {
                // The reducer starts the next consumer with a cold observation;
                // never carry the prior consumer's object address into it.
                state.prior_object_payload = 0;
                state.prior_object_generation = 0;
                state.prior_object_has_exact_lease = false;
                state.has_prior_object_payload = false;
            }
            const C1HeapGuard census_before = c1_heap_guard();
            C1HeapGuard census_after{};
            c1_emit_heap_census(state.world, consumer, cycle, phase,
                                census_before, &census_after);
            Baseline now{census_after.stats, census_after.roots,
                census_after.classes, census_after.pools,
                census_after.gobj_used, census_after.proc_used};
            const bool current_is_live = current_phase == "live";
            const bool query_needed = state.has_prior_object_payload || current_is_live;
            C1HeapGuard query_before{};
            if (query_needed) query_before = c1_heap_guard();
            if (state.has_prior_object_payload) {
                MeleeWebSourceMemoryAllocation prior{};
                const auto prior_status = melee_web_source_memory_allocation_read(
                    reinterpret_cast<const void*>(state.prior_object_payload), &prior);
                check(prior_status == MELEE_WEB_SOURCE_MEMORY_READ_OK,
                      "C1 prior object payload query was refused");
                const char* classification = "unknown_no_exact_lease_baseline";
                if (state.prior_object_has_exact_lease) {
                    check(prior.world_generation == now.stats.generation,
                          "C1 prior exact payload moved to a different world generation");
                    if (!prior.live) {
                        classification = "freed_sdk_lease";
                    } else if (prior.allocation_generation ==
                               state.prior_object_generation) {
                        classification = "unchanged_live_sdk_lease";
                    } else {
                        check(prior.allocation_generation >
                                  state.prior_object_generation,
                              "C1 prior exact payload generation moved backwards");
                        classification = "new_generation_reuse";
                    }
                }
                c1_heap_query_line(
                    state.world, consumer, cycle, phase, "prior_object_payload",
                    state.prior_object_payload, prior_status, prior,
                    state.prior_object_generation, 0, "recheck", classification);
            }
            if (current_is_live) {
                check(owned != nullptr,
                      "C1 live cache phase omitted its owned object pointer");
                const uintptr_t object_payload = reinterpret_cast<uintptr_t>(owned);
                const auto object_row = std::find_if(
                    c1_heap_census.rows,
                    c1_heap_census.rows + c1_heap_census.count,
                    [object_payload](const C1HeapCensusRow& row) {
                        return row.payload == object_payload;
                    });
                MeleeWebSourceMemoryAllocation object_lease{};
                const auto object_status = melee_web_source_memory_allocation_read(
                    owned, &object_lease);
                check(object_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                          object_lease.world_generation == now.stats.generation,
                      "C1 object payload exact-lease query was refused or changed worlds");
                const char* classification = nullptr;
                if (object_row != c1_heap_census.rows + c1_heap_census.count) {
                    check(object_row->lease.live == object_lease.live &&
                              object_row->lease.allocation_generation ==
                                  object_lease.allocation_generation,
                          "C1 exact object payload query disagrees with its census row");
                    classification = object_lease.live
                        ? "captured_exact_live_sdk_lease"
                        : "captured_exact_payload_without_source_lease";
                } else {
                    check(!object_lease.live,
                          "C1 live object lease is missing from the allocated-cell census");
                    classification = "captured_unknown_nonexact_or_unmapped";
                }
                c1_heap_query_line(
                    state.world, consumer, cycle, phase, "prior_object_payload",
                    object_payload, object_status, object_lease,
                    object_lease.live ? object_lease.allocation_generation : 0,
                    0, "capture", classification);
                state.prior_object_payload = object_payload;
                state.prior_object_generation = object_lease.live
                    ? object_lease.allocation_generation : 0;
                state.prior_object_has_exact_lease = object_lease.live;
                state.has_prior_object_payload = true;
            }
            if (query_needed) {
                const C1HeapGuard query_after = c1_heap_guard();
                check(c1_heap_guards_equal(query_before, query_after),
                      "C1 prior object payload query changed source allocation state");
                std::cerr << "C1_HEAP_QUERY_GUARD world=" << state.world
                          << " consumer=" << consumer << " cycle=" << cycle
                          << " phase=" << phase
                          << " equal=1 source_healthy=1 world_equal=1 heap_owner=1 watermark=1"
                          << " ticks=1 free_bytes=1 roots=1 classes=1 pools=1"
                          << " gobj_used=1 proc_used=1\n";
            }
            std::cerr << "C1_CACHE_LIVE world=" << state.world
                      << " generation=" << now.stats.generation
                      << " consumer=" << consumer << " phase=" << phase
                      << " cycle=" << cycle << " owned=" << owned
                      << " heap_free_bytes=" << now.stats.heap_free_bytes
                      << " ticks=" << now.stats.ticks
                      << " gobj_used=" << now.gobj_used
                      << " gobj_free=" << HSD_ObjAllocGetFreed(&gobj_alloc_data)
                      << " gobj_size=" << gobj_alloc_data.size
                      << " proc_used=" << now.proc_used
                      << " proc_free=" << HSD_ObjAllocGetFreed(&gobjproc_alloc_data)
                      << " proc_size=" << gobjproc_alloc_data.size << " classes=";
            for (const auto& [identity, count] : now.classes)
                std::cerr << static_cast<const void*>(identity) << ':' << count << ',';
            std::cerr << " pools=";
            for (auto count : now.pools) std::cerr << count << ',';
            std::cerr << '\n'; std::cerr.flush();
            ++state.records;
            if (std::string_view(phase) == "cold") {
                check(cycle == 0 && owned == nullptr, "Invalid cold cache/live phase");
                state.baseline = std::move(now);
                state.consumer = consumer;
            } else {
                check(state.consumer == consumer && now.stats.generation == state.baseline.stats.generation &&
                      now.stats.ticks == state.baseline.stats.ticks,
                      "Cache/live control changed world or source ticks");
                if (std::string_view(phase) == "live") {
                    check(owned != nullptr && now.classes != state.baseline.classes,
                          "Original allocation did not expose live class ownership");
                } else {
                    check(owned == nullptr && (std::string_view(phase) == "removed" ||
                          std::string_view(phase) == "warm"), "Invalid cache/live phase");
                    check(now.roots == state.baseline.roots && now.classes == state.baseline.classes &&
                          now.pools == state.baseline.pools && now.gobj_used == state.baseline.gobj_used &&
                          now.proc_used == state.baseline.proc_used && melee_web_source_memory_healthy(),
                          "Original removal retained roots, live classes, used pools or unhealthy leases");
                    if (std::string_view(phase) == "removed") ++state.completed;
                }
            }
            return 1;
        } catch (const std::exception& failure) {
            std::cerr << "C1_CACHE_LIVE_REFUSAL " << failure.what() << '\n';
            std::cerr.flush(); return 0;
        }
    };
    uint64_t previous_generation = 0;
    for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
        char error[256]{};
        check(melee_web_gameplay_startup(8U * 1024U * 1024U, error, sizeof(error)), error);
        check(melee_web_native_world_enable(error, sizeof(error)), error);
        check(melee_web_gameplay_stats().generation != previous_generation,
              "Cache/live reducer reused a world generation");
        previous_generation = melee_web_gameplay_stats().generation;
        if (lifetime == 0 && capture_heap_owners) {
            MeleeWebSourceMemoryContext context{};
            check(melee_web_source_memory_context_read(&context) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      melee_web_source_memory_healthy(),
                  "Heap-owner control requires a healthy active source-memory context");
            melee_web_stadium_c1_heap_owner_arm();
        }
        State state{}; state.world = lifetime;
        check(melee_web_stadium_c1_cache_live_control(observer, &state, error, sizeof(error)), error);
        check(state.records == 12 && state.completed == 4,
              "Cache/live reducer skipped a cold/live/removed/warm observation");
        if (lifetime == 0) {
            c1_publish_heap_owner_observations("asset-free-control", false);
            melee_web_stadium_c1_heap_owner_disable();
        }
        check(melee_web_gameplay_shutdown(error, sizeof(error)), error);
        MeleeWebSourceMemoryContext inactive_context{};
        const auto inactive_status =
            melee_web_source_memory_context_read(&inactive_context);
        MeleeWebSourceMemoryAllocation inactive_allocation{};
        const auto inactive_allocation_status =
            melee_web_source_memory_allocation_read(
                reinterpret_cast<const void*>(state.prior_object_payload),
                &inactive_allocation);
        check(inactive_status == MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE &&
                  inactive_allocation_status ==
                      MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE,
              "C1 prior-object-payload query after shutdown was not refused as inactive");
        c1_heap_query_line(lifetime, state.consumer.c_str(), 1, "removed",
                           "prior_object_payload", state.prior_object_payload,
                           inactive_allocation_status, inactive_allocation,
                           state.prior_object_generation, 1, "after_shutdown",
                           "inactive_owner");
    }
    std::cout << "C1 asset-free cache/live reducer passed; two worlds, 24 phase records, bounded original SDK allocation/free census, exact lease queries and source-state purity; no original Stadium callback, camera or source ticks\n";
}

void run_stadium_map_light_adoption_control()
{
    char error[256]{};
    check(melee_web_gameplay_startup(8U * 1024U * 1024U, error, sizeof(error)), error);
    check(melee_web_native_world_enable(error, sizeof(error)), error);
    check(melee_web_stadium_c1_map_light_adoption_control(error, sizeof(error)), error);
    check(melee_web_gameplay_shutdown(error, sizeof(error)), error);
    std::cout << "C1 asset-free original Ground map-light creation/adoption, two retire-before-detach cycles and foreign/replaced/bound refusals passed; no camera, scheduled proc dispatch or source ticks\n";
}

void run_stadium_sis_allocator_lifecycle_control()
{
    char error[256]{};
    // Reproduce the source menu lifecycle before a new diagnostic SDK heap.
    check(melee_web_gameplay_startup(8U * 1024U * 1024U, error, sizeof(error)), error);
    check(melee_web_native_world_enable(error, sizeof(error)), error);
    HSD_SisLib_803A6048(MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES);
    HSD_GObj parent{}; // Non-null parent prevents original611C creating a camera.
    check(HSD_SisLib_803A611C(1, &parent, 9, 0xD, 0, 1, 0, 1) == 0,
          "Retired-menu SIS baseline lost its first context index");
    MeleeWebRetiredSisLease retired{};
    check(melee_web_diagnostic_sis_capture(&retired, error, sizeof(error)), error);
    HSD_SisLib_803A5FBC();
    check(melee_web_diagnostic_sis_verify_retired(&retired, error, sizeof(error)), error);
    // Actual original capture/drain/verify above produces immutable one-shot
    // evidence. A copied verified token must refuse without changing owners.
    const auto verified_token=retired;
    auto duplicate_token=retired;
    const auto verified_epoch=HSD_SisLib_HeapEpoch();
    const auto verified_heap=HSD_SisLib_HeapOwner();
    const auto verified_active=HSD_SisLib_HeapActive();
    const auto verified_used_head=used_head;
    const auto verified_text=HSD_SisLib_804D7978;
    const auto verified_context=HSD_SisLib_804D797C;
    const auto verified_fonts_empty=HSD_SisLib_AllFontSlotsEmpty();
    MeleeWebSourceMemoryAllocation allocation_before{},allocation_after{};
    const auto status_before=melee_web_source_memory_allocation_read(retired.prior.heap,&allocation_before);
    const auto duplicate_accepted=melee_web_diagnostic_sis_verify_retired(&duplicate_token,error,sizeof(error));
    const auto status_after=melee_web_source_memory_allocation_read(retired.prior.heap,&allocation_after);
    std::fprintf(stderr,"C1_SIS_VERIFY_REPEAT accepted=%d verified_before=%d verified_after=%d epoch_before=%llu epoch_after=%llu heap_before=%p heap_after=%p active_before=%d active_after=%d status_before=%d status_after=%d live_before=%u live_after=%u world_before=%llu world_after=%llu source_heap_before=%d source_heap_after=%d allocation_before=%llu allocation_after=%llu requested_before=%u requested_after=%u used_head_before=%p used_head_after=%p text_before=%p text_after=%p context_before=%p context_after=%p fonts_empty_before=%d fonts_empty_after=%d copied_token_equal=%d original_token_equal=%d\n",
        duplicate_accepted,verified_token.retirement_verified,duplicate_token.retirement_verified,
        (unsigned long long)verified_epoch,(unsigned long long)HSD_SisLib_HeapEpoch(),
        verified_heap,HSD_SisLib_HeapOwner(),verified_active,HSD_SisLib_HeapActive(),
        int(status_before),int(status_after),allocation_before.live,allocation_after.live,
        (unsigned long long)allocation_before.world_generation,(unsigned long long)allocation_after.world_generation,
        allocation_before.source_heap_handle,allocation_after.source_heap_handle,
        (unsigned long long)allocation_before.allocation_generation,(unsigned long long)allocation_after.allocation_generation,
        allocation_before.requested_bytes,allocation_after.requested_bytes,
        (void*)verified_used_head,(void*)used_head,
        (void*)verified_text,(void*)HSD_SisLib_804D7978,(void*)verified_context,(void*)HSD_SisLib_804D797C,
        verified_fonts_empty,HSD_SisLib_AllFontSlotsEmpty(),
        std::memcmp(&duplicate_token,&verified_token,sizeof(verified_token))==0,
        std::memcmp(&retired,&verified_token,sizeof(verified_token))==0);
    std::fflush(stderr);
    check(!duplicate_accepted && std::memcmp(&duplicate_token,&verified_token,sizeof(verified_token))==0 &&
          std::memcmp(&retired,&verified_token,sizeof(verified_token))==0 &&
          HSD_SisLib_HeapEpoch()==verified_epoch && HSD_SisLib_HeapOwner()==verified_heap &&
          HSD_SisLib_HeapActive()==verified_active && used_head==verified_used_head &&
          HSD_SisLib_804D7978==verified_text &&
          HSD_SisLib_804D797C==verified_context && HSD_SisLib_AllFontSlotsEmpty()==verified_fonts_empty &&
          status_before==MELEE_WEB_SOURCE_MEMORY_READ_OK && status_after==status_before &&
          std::memcmp(&allocation_before,&allocation_after,sizeof(allocation_before))==0,
          "Repeated SIS retirement verification changed the verified token/source owner or lease");
    check(HSD_SisLib_AllFontSlotsEmpty() && !HSD_SisLib_804D7978 &&
              !HSD_SisLib_804D797C,
          "Retired-menu SIS baseline retained live roots");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), error);

    check(melee_web_gameplay_startup(8U * 1024U * 1024U, error, sizeof(error)), error);
    check(melee_web_native_world_enable(error, sizeof(error)), error);
    const auto before = melee_web_gameplay_stats();
    const auto scheduler = HSD_GObj_804D783C;
    const auto gobj_count = HSD_ObjAllocGetUsing(&gobj_alloc_data);
    const auto proc_count = HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
    // One real current-world allocation at the old size adds address pressure.
    // Record reuse; never require a particular allocator address or fake a lease.
    void* const pressure = HSD_MemAlloc(MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES);
    MeleeWebSourceMemoryAllocation pressure_before{};
    check(pressure && melee_web_source_memory_allocation_read(
              pressure, &pressure_before) == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
              pressure_before.live, "SIS pressure allocation lacks its actual lease");
    const bool reused_retired_address = pressure == retired.prior.heap;
    // Synthetic same-address records use the same pure identity reducer as begin.
    // A live record with the prior generation remains the same owner; replacing
    // either generation is distinct only after verified original retirement.
    auto synthetic_same = pressure_before;
    synthetic_same.world_generation = retired.prior.world_generation;
    synthetic_same.allocation_generation = retired.prior.allocation_generation;
    check(!melee_web_diagnostic_sis_distinct_retired_lease(&retired, &synthetic_same),
          "Retirement reducer accepted the same live SIS lease");
    ++synthetic_same.allocation_generation;
    check(melee_web_diagnostic_sis_distinct_retired_lease(&retired, &synthetic_same),
          "Retirement reducer confused same-address replacement generation");
    synthetic_same.allocation_generation = retired.prior.allocation_generation;
    ++synthetic_same.world_generation;
    check(melee_web_diagnostic_sis_distinct_retired_lease(&retired, &synthetic_same),
          "Retirement reducer confused same-address replacement world");
    auto unverified = retired;
    unverified.retirement_verified = 0;
    check(!melee_web_diagnostic_sis_distinct_retired_lease(&unverified, &synthetic_same),
          "Unverified retirement authorized a replacement lease");

    MeleeWebDiagnosticSisOwner pressure_owner{};
    check(melee_web_diagnostic_sis_begin_retired(
              &pressure_owner, &retired, error, sizeof(error)), error);
    check(melee_web_diagnostic_sis_end(&pressure_owner, error, sizeof(error)), error);
    MeleeWebSourceMemoryAllocation pressure_after{};
    check(melee_web_source_memory_allocation_read(pressure, &pressure_after) ==
              MELEE_WEB_SOURCE_MEMORY_READ_OK && pressure_after.live &&
              pressure_after.world_generation == pressure_before.world_generation &&
              pressure_after.allocation_generation == pressure_before.allocation_generation,
          "Retired SIS handoff freed or replaced a current foreign allocation");
    HSD_Free(pressure);

    // A new original initializer is a genuine owner, even if its numeric address
    // matches an old token. Model equal address explicitly, retaining old epoch.
    HSD_SisLib_803A6048(MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES);
    auto stale_equal_address = retired;
    stale_equal_address.prior.heap = HSD_SisLib_HeapOwner();
    MeleeWebDiagnosticSisOwner refused{};
    check(!melee_web_diagnostic_sis_begin_retired(
              &refused, &stale_equal_address, error, sizeof(error)) && !refused.heap &&
              HSD_SisLib_HeapActive(),
          "Retired token overwrote a genuine newly initialized SIS owner");
    MeleeWebRetiredSisLease fresh_retired{};
    check(melee_web_diagnostic_sis_capture(&fresh_retired, error, sizeof(error)), error);
    HSD_SisLib_803A5FBC();
    check(melee_web_diagnostic_sis_verify_retired(&fresh_retired, error, sizeof(error)), error);
    check(!melee_web_diagnostic_sis_begin_retired(
              &refused, &stale_equal_address, error, sizeof(error)) && !refused.heap,
          "Old retirement token survived a later source startup/drain epoch");
    std::cout << "C1 actual post-restart retired SIS address reuse="
              << reused_retired_address << "; genuine-new-owner and later-epoch refusals passed\n";
    HSD_Text foreign_text{};
    sislib_UnkAlloc3 foreign_context{};
    SIS foreign_sis_data{};
    SIS* const foreign_sis = &foreign_sis_data;
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        MeleeWebDiagnosticSisOwner owner{};
        const auto text_head = HSD_SisLib_804D7978;
        const auto context_head = HSD_SisLib_804D797C;
        HSD_SisLib_804D7978 = &foreign_text;
        check(!melee_web_diagnostic_sis_begin(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D7978 == &foreign_text && !owner.heap,
              "Diagnostic SIS begin cleared a foreign text owner");
        HSD_SisLib_804D7978 = text_head;
        HSD_SisLib_804D797C = &foreign_context;
        check(!melee_web_diagnostic_sis_begin(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D797C == &foreign_context && !owner.heap,
              "Diagnostic SIS begin cleared a foreign context owner");
        HSD_SisLib_804D797C = context_head;
        HSD_SisLib_804D1124[1] = foreign_sis;
        check(!melee_web_diagnostic_sis_begin(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D1124[1] == foreign_sis && !owner.heap,
              "Diagnostic SIS begin cleared a foreign font slot");
        HSD_SisLib_804D1124[1] = nullptr;
        check(melee_web_diagnostic_sis_begin_retired(
                  &owner, cycle == 0 ? &fresh_retired : nullptr, error, sizeof(error)), error);
        MeleeWebDiagnosticSisOwner contender{};
        check(!melee_web_diagnostic_sis_begin(&contender, error, sizeof(error)) &&
                  !contender.heap && HSD_SisLib_HeapOwner() == owner.heap,
              "Diagnostic SIS accepted a second allocator owner");
        check(melee_web_stadium_text_topology_controls(&owner),
              "Original SIS append/remove topology control failed (partial owner retained)");
        HSD_SisLib_804D7978 = &foreign_text;
        check(!melee_web_diagnostic_sis_end(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D7978 == &foreign_text,
              "Diagnostic SIS end swept a foreign text owner");
        HSD_SisLib_804D7978 = text_head;
        HSD_SisLib_804D797C = &foreign_context;
        check(!melee_web_diagnostic_sis_end(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D797C == &foreign_context,
              "Diagnostic SIS end swept a foreign context owner");
        HSD_SisLib_804D797C = context_head;
        auto stale_world = owner;
        ++stale_world.world_generation;
        check(!melee_web_diagnostic_sis_end(&stale_world, error, sizeof(error)) &&
                  HSD_SisLib_HeapOwner() == owner.heap,
              "Diagnostic SIS end accepted a stale world token");
        auto replaced = owner;
        ++replaced.allocation_generation;
        check(!melee_web_diagnostic_sis_end(&replaced, error, sizeof(error)) &&
                  HSD_SisLib_HeapOwner() == owner.heap,
              "Diagnostic SIS drained an altered allocation lease");
        void* const raw_borrower = HSD_SisLib_Alloc(16);
        check(raw_borrower && used_head &&
                  !melee_web_diagnostic_sis_end(&owner, error, sizeof(error)),
              "Diagnostic SIS end swept an outstanding allocator borrower");
        HSD_SisLib_Free(raw_borrower);
        check(HSD_SisLib_803A611C(1, &parent, 9, 0xD, 0, 1, 0, 1) == 0,
              "Fresh diagnostic SIS allocation lost its first context index");
        const auto live_context = HSD_SisLib_804D797C;
        check(live_context && !live_context->x4 &&
                  !melee_web_diagnostic_sis_end(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D797C == live_context,
              "Diagnostic SIS end swept a live context");
        // Remove only this control's known611C context through original teardown.
        HSD_SisLib_803A5E70();
        check(melee_web_gameplay_vs_sis(MELEE_WEB_VS_SIS_VALIDATE_BORROW,
                                       1, foreign_sis, error, sizeof(error)), error);
        HSD_SisLib_804D1124[1] = foreign_sis;
        check(!melee_web_diagnostic_sis_end(&owner, error, sizeof(error)) &&
                  HSD_SisLib_804D1124[1] == foreign_sis,
              "Diagnostic SIS end erased a borrowed font slot before retirement");
        check(melee_web_gameplay_vs_sis(MELEE_WEB_VS_SIS_RETIRE_BORROW,
                                       1, foreign_sis, error, sizeof(error)), error);
        check(melee_web_diagnostic_sis_end(&owner, error, sizeof(error)), error);
        check(!owner.heap && !melee_web_diagnostic_sis_end(&owner, error, sizeof(error)),
              "Diagnostic SIS accepted a second drain of a retired owner");
    }
    const auto after = melee_web_gameplay_stats();
    check(after.generation == before.generation && after.ticks == before.ticks &&
              HSD_GObj_804D783C == scheduler &&
              HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_count &&
              HSD_ObjAllocGetUsing(&gobjproc_alloc_data) == proc_count,
          "SIS lifecycle control advanced source ticks or created scheduled/camera owners");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), error);
    std::cout << "C1 asset-free SIS retired-menu baseline, two owned allocator lifetimes, foreign-root/lease refusals and single drain passed; no camera, scheduled procs or source ticks\n";
}

void run_stadium_bind_refusal_control()
{
    char error[256]{};
    check(melee_web_gameplay_startup(8U * 1024U * 1024U,
                                     error, sizeof(error)), error);
    check(melee_web_native_world_enable(error, sizeof(error)), error);
    check(melee_web_stage_last_on_init_bind_refusal_controls(),
          "Asset-free bind-refusal reducer did not preserve the real cancellation refusal");
    std::cout << "C1 asset-free bind-refusal reporter preserved the initiating bind error and "
                 "real owner-cancel refusal; synthetic E8/bind adapter only, no archive lookup or "
                 "original Stage routine/OnInit call; "
                 "partial owner retained through process exit\n";
    std::cout.flush();
    std::_Exit(0);
}

uint32_t stadium_archive_symbol_offset(const melee_web::DatArchive& archive,
                                       const char* name)
{
    const auto found = std::find_if(
        archive.public_symbols().begin(), archive.public_symbols().end(),
        [name](const auto& symbol) { return symbol.name == name; });
    if (found == archive.public_symbols().end())
        throw melee_web::DatError(std::string("Missing Stadium source public root: ") +
                                  name);
    return found->data_offset;
}

const char* stadium_map2_buffer_origin_name(
    MeleeWebStadiumMap2BufferOrigin origin)
{
    switch (origin) {
    case MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_BORROWED_PRELOAD:
        return "borrowed_preload";
    case MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK:
        return "owned_fallback";
    case MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_RETIRED:
        return "retired";
    default:
        return "unknown";
    }
}

const char* stadium_source_event_kind_name(
    MeleeWebStadiumSourceEventKind kind)
{
    switch (kind) {
    case MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_E8:
        return "stage_e8";
    case MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_24C:
        return "stage_24c";
    case MELEE_WEB_STADIUM_SOURCE_EVENT_GROUND_0800:
        return "ground_0800";
    case MELEE_WEB_STADIUM_SOURCE_EVENT_ON_INIT:
        return "stadium_on_init";
    case MELEE_WEB_STADIUM_SOURCE_EVENT_MAP_GOBJ:
        return "map_gobj";
    default:
        return "unknown";
    }
}

struct StadiumSourceOnInitObservation {
    MeleeWebStadiumMap2BufferOwner map2_owner{};
    MeleeWebStadiumSourceJournal source_journal{};
    MeleeWebSourceMemoryContext memory_before_init{};
    MeleeWebSourceMemoryContext memory_before_end{};
    MeleeWebSourceMemoryContext memory_after_end{};
    MeleeWebSourceMemoryAllocation map2_before_end{};
    MeleeWebSourceMemoryAllocation map2_after_end{};
    MeleeWebSourceMemoryAllocation ground_storage_before_end{};
    MeleeWebSourceMemoryAllocation ground_storage_after_end{};
    MeleeWebGroundMapStorageView ground_storage_live{};
    MeleeWebGroundMapStorageView ground_storage_ended{};
    MeleeWebGameplayStats stats_before_init{};
    MeleeWebGameplayStats stats_after_light_preparation{};
    bool light_preparation_stats_captured = false;
    MeleeWebGameplayStats stats_after_on_init{};
    MeleeWebGameplayStats stats_after_end{};
    bool census_observer_failed = false;
    MeleeWebStadiumC1StageInfoView stage_info_before_init{};
    decltype(melee_web::test::stadium_screen::runtime_roots_snapshot()) roots_before{};
    decltype(melee_web::test::stadium_screen::live_class_counts()) class_counts_before{};
    decltype(melee_web::test::stadium_screen::live_pool_counts()) pool_counts_before{};
    MeleeWebStadiumC1FtDeviceSnapshot* device_snapshot = nullptr;
    int map2_allocation_status_before_end =
        MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    int map2_allocation_status_after_end =
        MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    int ground_storage_allocation_status_before_end =
        MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    int ground_storage_allocation_status_after_end =
        MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
    uint32_t gobj_pool_before = 0;
    uint32_t proc_pool_before = 0;
    uint32_t stage_gobj_count_before = 0;
    int scheduler_cycle_before = 0;
    uint32_t seed_after_on_init = 0;
    uint32_t seed_after_end = 0;
    bool cleanup_verified = false;
};


// Diagnostic reads only after the original first heap equality has failed.
// Its 21 checks still own acceptance, order and baselines unchanged.
void publish_stadium_heap_failure_observations(const StadiumSourceOnInitObservation& saved)
{
    auto stats = [](const char* phase, const MeleeWebGameplayStats& value) {
        std::cerr << "C1_HEAP_PHASE phase=" << phase << " provenance=copied"
                  << " generation=" << value.generation << " ticks=" << value.ticks
                  << " objects=" << value.objects << " processes=" << value.processes
                  << " heap_free_bytes=" << value.heap_free_bytes << '\n';
    };
    stats("before-light-preparation", saved.stats_before_init);
    if (saved.light_preparation_stats_captured)
        stats("after-light-preparation-before-e8", saved.stats_after_light_preparation);
    else
        std::cerr << "C1_HEAP_PHASE phase=after-light-preparation-before-e8 status=unavailable\n";
    stats("after-oninit", saved.stats_after_on_init);
    stats("after-stage-last-and-light-destroy", saved.stats_after_end);
    std::cerr.flush();
    MeleeWebSourceMemoryContext current{};
    const auto status = melee_web_source_memory_context_read(&current);
    const auto current_stats = melee_web_gameplay_stats();
    std::cerr << "C1_POST_HEAP_CONTEXT read_status=" << status
              << " heap=" << current.source_heap_handle << " world=" << current.world_generation
              << " watermark=" << current.allocation_generation_watermark
              << " current_gameplay_world=" << current_stats.generation
              << " expected_heap=" << saved.memory_before_init.source_heap_handle
              << " expected_world=" << saved.memory_before_init.world_generation
              << " before_end_watermark=" << saved.memory_before_end.allocation_generation_watermark
              << " captured_after_end_watermark=" << saved.memory_after_end.allocation_generation_watermark << '\n';
    if (status != MELEE_WEB_SOURCE_MEMORY_READ_OK ||
        current.source_heap_handle != saved.memory_before_init.source_heap_handle ||
        current.world_generation != saved.memory_before_init.world_generation ||
        !melee_web_gameplay_world_exists() ||
        current_stats.generation != current.world_generation) {
        std::cerr << "C1_POST_HEAP_OBSERVATION status=unavailable reason=current-owner-guard\n";
        std::cerr.flush(); return;
    }
    // A diagnostic read failure never replaces the original first error.
    auto observe = [](const char* field, auto read) {
        try { read(); }
        catch (const std::exception& failure) {
            std::cerr << "C1_POST_HEAP_OBSERVATION field=" << field
                      << " status=unavailable reason=" << failure.what() << '\n';
        } catch (...) {
            std::cerr << "C1_POST_HEAP_OBSERVATION field=" << field
                      << " status=unavailable reason=unknown-read-error\n";
        }
        std::cerr.flush();
    };
    auto scalar = [](const char* field, auto actual, auto baseline) {
        std::cerr << "C1_POST_HEAP_OBSERVATION field=" << field << " actual=" << actual
                  << " baseline=" << baseline << " acceptance=not-executed\n";
    };
    observe("source-registries", [&] {
        scalar("stage_registry_empty", source_stage_registry_empty(), true);
        scalar("stage_gobj_count", source_stage_gobj_count(), saved.stage_gobj_count_before);
        scalar("stage_markers_empty", source_stage_markers_empty(), true);
        scalar("stage_object_failures", melee_web_stadium_c1_stage_object_failures(), uint32_t{0});
    });
    observe("gobj-proc-pools", [&] {
        scalar("gobj_used", HSD_ObjAllocGetUsing(&gobj_alloc_data), saved.gobj_pool_before);
        scalar("proc_used", HSD_ObjAllocGetUsing(&gobjproc_alloc_data), saved.proc_pool_before);
        std::cerr << "C1_POST_HEAP_CAPACITY gobj_free=" << HSD_ObjAllocGetFreed(&gobj_alloc_data)
                  << " gobj_size=" << gobj_alloc_data.size
                  << " proc_free=" << HSD_ObjAllocGetFreed(&gobjproc_alloc_data)
                  << " proc_size=" << gobjproc_alloc_data.size
                  << " scope=public-pools-not-total-heap-accounting\n";
    });
    observe("live-classes", [&] {
        const auto now = melee_web::test::stadium_screen::live_class_counts();
        for (const auto& [identity, baseline] : saved.class_counts_before) {
            const auto found = now.find(identity);
            std::cerr << "C1_POST_HEAP_CLASS identity=" << static_cast<const void*>(identity)
                      << " actual=" << (found == now.end() ? 0 : found->second)
                      << " baseline=" << baseline << " acceptance=not-executed\n";
        }
        for (const auto& [identity, actual] : now)
            if (!saved.class_counts_before.contains(identity))
                std::cerr << "C1_POST_HEAP_CLASS identity=" << static_cast<const void*>(identity)
                          << " actual=" << actual << " baseline=0 acceptance=not-executed\n";
        std::cerr << "C1_POST_HEAP_OBSERVATION field=live_class_counts equal="
                  << (now == saved.class_counts_before) << " acceptance=not-executed\n";
    });
    observe("live-pools", [&] {
        const auto now = melee_web::test::stadium_screen::live_pool_counts();
        for (size_t i = 0; i < now.size(); ++i)
            std::cerr << "C1_POST_HEAP_POOL index=" << i << " actual=" << now[i]
                      << " baseline=" << saved.pool_counts_before[i] << " acceptance=not-executed\n";
    });
    observe("runtime-roots", [&] {
        const auto now = melee_web::test::stadium_screen::runtime_roots_snapshot();
        std::cerr << "C1_POST_HEAP_ROOTS actual_bytes=" << now.size()
                  << " baseline_bytes=" << saved.roots_before.size()
                  << " equal=" << (now == saved.roots_before) << " acceptance=not-executed";
        const auto common = std::min(now.size(), saved.roots_before.size());
        size_t first = 0;
        while (first < common && now[first] == saved.roots_before[first]) ++first;
        if (first < common)
            std::cerr << " first_offset=" << first << " actual=" << unsigned(now[first])
                      << " baseline=" << unsigned(saved.roots_before[first]);
        std::cerr << '\n';
    });
    observe("ft-device", [&] {
        if (saved.device_snapshot == nullptr)
            std::cerr << "C1_POST_HEAP_OBSERVATION field=ft_device status=unavailable reason=no-owned-copy\n";
        else
            scalar("ft_device_snapshot_matches",
                melee_web_stadium_c1_ft_device_snapshot_matches(saved.device_snapshot), 1);
    });
    observe("scheduler-health", [&] {
        scalar("scheduler_cycle", HSD_GObj_804D783C, saved.scheduler_cycle_before);
        scalar("source_memory_healthy", melee_web_source_memory_healthy(), 1);
        scalar("ground_dispatch_quiet", ground_dispatch_quiet(), true);
    });
}

void run_stadium_e8_request(
    const melee_web::RuntimeFiles& reopened_files,
    MeleeWebMenuHost* host,
    melee_web::GameplayMenuWorld* world,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before,
    bool perform_ground_map1_owner,
    bool perform_on_init,
    const MeleeWebRetiredSisLease* retired_sis,
    StadiumSelectionRngWitness& selection_rng,
    TransitionTrace& trace,
    MeleeWebMatchContext* stage_start_context = nullptr,
    MeleeWebRetiredSisLease* next_retired_sis = nullptr,
    const MeleeWebMenuMatchSelection* active_match_copied_selection = nullptr)
{
    using namespace melee_web;
    char error[256]{};
    check(host && world && reopened_files.contains("GrPs.usd"),
          "E8 request requires its retained source host and exact GrPs.usd entry");
    auto verify_selection_boundary = [&](const char* boundary, bool source_return) {
        if (!stage_start_context) {
            check_stadium_selection_preserved(host, selected, baseline,
                                              source_return ? &selection_rng : nullptr);
            return;
        }
        // These are active-match RNG + immutable copied-selection observations.
        // The host export requires its own seed and is deliberately not called.
        check(active_match_copied_selection &&
                  stadium_active_match_selection_witness_matches(
                      *active_match_copied_selection, selected, selection_rng),
              "Active-match RNG or immutable copied selection changed");
        check(melee_web_match_camera_available(stage_start_context, error, sizeof(error)), error);
        check(melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY &&
                  melee_web_menu_host_source_scene(host) == 0,
              "Active match changed its retained closed menu host phase");
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> observed_baseline{};
        check(melee_web_menu_host_snapshot_card_data(host, 1, observed_baseline.data(),
                  observed_baseline.size(), error, sizeof(error)), error);
        check(observed_baseline == baseline,
              "Active match changed its observed retained save-owner baseline");
        std::fprintf(stderr,
            "C3_ACTIVE_MATCH_SELECTION boundary=%s host_reexport=0 copied_selection_equal=1 active_match_rng_verified=1 save_baseline_observed_equal=1 match=%p rng_owner=%p initial=%u expected_live=%u live=%u\n",
            boundary, static_cast<void*>(stage_start_context), static_cast<const void*>(selection_rng.owner),
            selection_rng.initial, selection_rng.expected_live, *selection_rng.owner);
        std::fflush(stderr);
    };
    const auto& raw_bytes = reopened_files.at("GrPs.usd");
    const std::vector<std::uint8_t> raw_before = raw_bytes;
    auto archive = std::make_shared<const DatArchive>(
        raw_bytes, DatExternalPolicy::ResolveNull);
    const DatStage map_metadata(*archive);
    auto map_contract = test::stadium_contract_data(*archive, map_metadata);
    const std::uint32_t ground_root =
        stadium_archive_symbol_offset(*archive, "grGroundParam");
    const std::uint32_t itemdata_root =
        stadium_archive_symbol_offset(*archive, "itemdata");
    const std::uint32_t yaku_root =
        stadium_archive_symbol_offset(*archive, "ALDYakuAll");
    const std::uint32_t yakumono_root =
        stadium_archive_symbol_offset(*archive, "yakumono_param");
    (void)stadium_archive_symbol_offset(*archive, "map_plit");
    const std::uint32_t quake_root =
        stadium_archive_symbol_offset(*archive, "quake_model_set");
    const std::uint32_t image_root = stadium_archive_symbol_offset(
        *archive, "GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc");

    check(archive->be32(itemdata_root) == 0 &&
              !archive->has_relocation(itemdata_root) &&
              !archive->pointer(itemdata_root, 4),
          "C0 itemdata root is not the authored null word");
    const std::vector<std::uint8_t> archive_data_before(
        archive->data().begin(), archive->data().end());
    check(melee_web_stadium_c1_stage_object_failures() == 0,
          "E8 pre-request context contains stage objects or published item/light roots");

    MeleeWebStadiumC1StageInfoSnapshot* snapshot = nullptr;
    MeleeWebStageMap* stage_map = nullptr;
    MeleeWebStageLights* light_context = nullptr;
    void* previous_ground_param = nullptr;
    bool ground_param_published = false;
    bool effect_bank_attached = false;
    bool effect_runtime_owned = false;
    MeleeWebDiagnosticSisOwner sis_owner{};
    bool stage_selection_owned = false;
    bool observer_window_owned = false;
    bool cleanup_complete = false;
    std::unique_ptr<NativeDatArena> scalar_owner;
    std::unique_ptr<DatNativeMap> map_owner;
    std::unique_ptr<DatStageYaku> random_yaku;
    std::unique_ptr<DatEffectBanks> effects;
    std::unique_ptr<DatScene> quake;
    std::unique_ptr<DatStageItems> items;
    std::unique_ptr<DatSis> stadium_sis;
    MeleeWebStadiumE8CallObservation observed{};
    MeleeWebStadiumC1StageInfoView before_view{};
    MeleeWebStadiumC1StageInfoView after_view{};
    MeleeWebStageLast* retained_stage_owner = nullptr;
    MeleeWebStageLast* returned_stage_owner = nullptr;
    StadiumSourceOnInitObservation on_init_observation{};
    bool on_init_stage_end_succeeded = false;
    std::unique_ptr<GroundStorageLease> ground_storage;
    void* ground_data = nullptr;
    void* yakumono_data = nullptr;
    void* native_map_head = nullptr;
    void* native_collision = nullptr;
    void* native_ald_yaku = nullptr;
    uint32_t seed_before = 0;
    const uint32_t* seed_owner = seed_ptr;
    check(seed_owner != nullptr,
          "E8 request lost the source seed owner before preparation");
    seed_before = *seed_owner;
    check_stadium_rng_witness(selection_rng, selected, "e8-preparation");
    check(!selection_rng.source_return_captured,
          "E8 request cannot reuse a captured source-return RNG witness");
    const char* grps_resolved_name = lbFileGetFullName("/GrPs");
    check(grps_resolved_name &&
              std::strcmp(grps_resolved_name, "/GrPs.usd") == 0,
          "US source resolution did not preserve the authored /GrPs.usd path");

    auto cleanup = [&]() {
        if (observer_window_owned) {
            MeleeWebStadiumE8CallObservation discarded{};
            check(melee_web_stadium_e8_call_observer_end(&discarded),
                  "Could not close the E8 source-call observation window");
            observer_window_owned = false;
        }
        if (on_init_observation.device_snapshot) {
            check(melee_web_stadium_c1_ft_device_snapshot_release(
                      on_init_observation.device_snapshot),
                  "Could not release the source ftDevice observation snapshot");
            on_init_observation.device_snapshot = nullptr;
        }
        if (ground_storage) ground_storage->end();
        if (effect_runtime_owned) {
            check(melee_web_effect_runtime_end(error, sizeof(error)), error);
            effect_runtime_owned = false;
            HSD_GObj** const links =
                reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities);
            check(!melee_web_effect_runtime_prepared() &&
                      !melee_web_effect_runtime_active() && links &&
                      !links[11] && !links[12],
                  "OnInit cleanup did not retire the original effect runtime before map-bank detach");
        }
        if (sis_owner.heap) {
            if (next_retired_sis) {
                check(stage_start_context && !next_retired_sis->prior.heap &&
                          !next_retired_sis->retirement_verified,
                      "Full-world SIS continuation requires a fresh next-world retirement token");
                check(melee_web_diagnostic_sis_capture(next_retired_sis, error, sizeof(error)), error);
                trace.sis_lease("captured_before_stage_sis_drain", next_retired_sis);
            }
            check(melee_web_diagnostic_sis_end(&sis_owner, error, sizeof(error)), error);
            if (next_retired_sis) {
                check(melee_web_diagnostic_sis_verify_retired(next_retired_sis, error, sizeof(error)), error);
                trace.sis_lease("verified_stage_sis_retired_before_world_shutdown", next_retired_sis);
            }
        }
        if (effect_bank_attached) {
            check(melee_web_effect_bank_detach(effects->bank(), error,
                                                sizeof(error)), error);
            effect_bank_attached = false;
        }
        if (ground_param_published) {
            MeleeWebStadiumC1StageInfoView current_view{};
            check(melee_web_stadium_c1_stage_info_current_view(&current_view) &&
                      current_view.param == ground_data,
                  "E8 cleanup refused to overwrite a replaced GroundParam owner");
            void* const detached_ground_param =
                melee_web_ground_data_publish(previous_ground_param);
            ground_param_published = false;
            check(detached_ground_param == ground_data,
                  "Could not restore the prior source GroundParam owner");
        }
        if (light_context) {
            check(melee_web_stage_lights_destroy(light_context, error, sizeof(error)), error);
            light_context = nullptr;
        }
        if (stage_map) {
            grDatFiles_801C6288();
            check(melee_web_stage_map_close(stage_map, error, sizeof(error)),
                  error);
            stage_map = nullptr;
        }
        if (stage_selection_owned) {
            check(melee_web_stage_selection_end(),
                  "E8 request lost the source stage-selection scope");
            stage_selection_owned = false;
        }
        if (snapshot) {
            check(melee_web_stadium_c1_stage_info_snapshot_restore(
                      snapshot, error, sizeof(error)), error);
            check(melee_web_stadium_c1_stage_info_snapshot_release(
                      snapshot, error, sizeof(error)), error);
            snapshot = nullptr;
        }
        // The stage-map archive and light table borrow this owner's descriptors.
        stadium_sis.reset();
        items.reset();
        quake.reset();
        effects.reset();
        random_yaku.reset();
        map_owner.reset();
        scalar_owner.reset();
        archive.reset();
        cleanup_complete = true;
    };

    try {
        if (perform_ground_map1_owner) {
            ground_storage = std::make_unique<GroundStorageLease>();
            ground_storage->begin();
        }
        snapshot = melee_web_stadium_c1_stage_info_snapshot_begin(
            error, sizeof(error));
        check(snapshot != nullptr, error);
        check(melee_web_stadium_c1_stage_info_snapshot_view(
                  snapshot, &before_view),
              "Could not inspect the full source StageInfo snapshot");
        check(before_view.itemdata == nullptr && before_view.map_plit == nullptr,
              "E8 route requires the source-authored empty item/light roots");

        scalar_owner = std::make_unique<NativeDatArena>(archive);
        ground_data = melee_web_ground_data_decode(
            scalar_owner->reader(), ground_root);
        check(ground_data != nullptr,
              "C0 typed scalar owner did not decode GroundParam");
        yakumono_data = melee_web_stadium_yakumono_decode(
            scalar_owner->reader(), yakumono_root);
        map_owner = std::make_unique<DatNativeMap>(archive, map_contract.view());
        native_map_head = map_owner->map_head();
        native_collision = map_owner->collision();
        random_yaku = std::make_unique<DatStageYaku>(archive, yaku_root);
        native_ald_yaku = random_yaku->native_data();
        stadium_sis = std::make_unique<DatSis>(archive, "SIS_GrPStadiumData");
        check(stadium_sis->descriptor() != nullptr,
              "Stadium SIS typed owner returned a null descriptor");
        void* const native_image = map_owner->image_descriptor(image_root);
        check(yakumono_data != nullptr && native_map_head != nullptr &&
                  native_collision != nullptr && native_ald_yaku != nullptr &&
                  native_image != nullptr,
              "C0 typed map/scalar owners did not decode the selected roots");
        if (perform_on_init)
            check(!map_owner->light_overrides().empty(),
                  "Diagnostic map owner has no typed light identity table");
        effects = std::make_unique<DatEffectBanks>(
            archive, "map_ptcl", "map_texg", 0x40);
        check(effects->command_root() != nullptr &&
                  effects->texture_root() != nullptr,
              "C0 map_ptcl/map_texg typed owner is absent");
        quake = std::make_unique<DatScene>(
            archive, "quake_model_set", DatSceneRootKind::DynamicModel);
        check(quake->single_model() != nullptr && quake->model_count() == 1,
              "C0 quake_model_set typed owner is absent");
        items = std::make_unique<DatStageItems>(archive);
        check(items->items().empty(),
              "C0 authored-null itemdata unexpectedly decoded stage items");
        check(melee_web_stadium_c1_stage_object_failures() == 0,
              "E8 typed preparation published a stage object or item/light root");
        const auto* stadium_profile = melee_web_stage_profile(St_Kind_PStadium);
        const auto* stadium_content =
            melee_web_stage_content_for_profile(St_Kind_PStadium);
        check(stadium_profile && stadium_profile->diagnostic_only &&
                  stadium_profile->public_symbols && stadium_content &&
                  stadium_profile->public_symbol_count == 2 &&
                  stadium_content->archive &&
                  std::strcmp(stadium_content->archive, "GrPs.usd") == 0,
              "Diagnostic source catalog lost its checked profile/archive identity");
        std::vector<MeleeWebArchiveSymbol> symbols;
        const auto& source_symbols = archive->public_symbols();
        symbols.reserve(source_symbols.size());
        // Keep the source catalog's authored names/order. Any unhydrated source
        // name remains null and the archive-section lookup rejects its use.
        for (const auto& source_symbol : source_symbols) {
            void* native_data = nullptr;
            if (source_symbol.name == "map_head")
                native_data = map_owner->map_head();
            else if (source_symbol.name == "coll_data")
                native_data = native_collision;
            else if (source_symbol.name == "grGroundParam")
                native_data = ground_data;
            else if (source_symbol.name == "ALDYakuAll")
                native_data = native_ald_yaku;
            else if (source_symbol.name == "map_ptcl")
                native_data = effects->command_root();
            else if (source_symbol.name == "map_texg")
                native_data = effects->texture_root();
            else if (source_symbol.name == "yakumono_param")
                native_data = yakumono_data;
            else if (source_symbol.name == "quake_model_set")
                native_data = quake->single_model();
            for (size_t i = 0; i < stadium_profile->public_symbol_count; ++i) {
                const auto& request = stadium_profile->public_symbols[i];
                if (source_symbol.name != request.name) continue;
                if (request.kind == MELEE_WEB_STAGE_PUBLIC_IMAGE) {
                    check(source_symbol.data_offset == image_root,
                          "Diagnostic public IMAGE source identity changed");
                    native_data = map_owner->image_descriptor(source_symbol.data_offset);
                } else if (request.kind == MELEE_WEB_STAGE_PUBLIC_SIS) {
                    native_data = stadium_sis->descriptor();
                } else {
                    check(false, "Diagnostic public source descriptor kind is unsupported");
                }
            }
            symbols.push_back({stadium_content->archive,
                               source_symbol.name.c_str(), native_data});
        }
        check(symbols.size() == source_symbols.size(),
              "Diagnostic source catalog lost an authored public row");
        for (const auto& symbol : symbols)
            check(symbol.filename && symbol.symbol &&
                      std::strcmp(symbol.filename, stadium_content->archive) == 0,
                  "Diagnostic source catalog lost an authored name or filename");
        const auto find_owned_symbol = [&](const char* name) {
            const auto count = std::count_if(
                symbols.begin(), symbols.end(),
                [&](const auto& symbol) {
                    return std::strcmp(symbol.symbol, name) == 0;
                });
            check(count == 1,
                  "Diagnostic source catalog must expose one requested public name");
            const auto found = std::find_if(
                symbols.begin(), symbols.end(),
                [&](const auto& symbol) {
                    return std::strcmp(symbol.symbol, name) == 0;
                });
            return &*found;
        };
        check(find_owned_symbol("map_head")->native_data == map_owner->map_head() &&
                  find_owned_symbol("coll_data")->native_data == native_collision &&
                  find_owned_symbol("grGroundParam")->native_data == ground_data &&
                  find_owned_symbol("ALDYakuAll")->native_data == native_ald_yaku &&
                  find_owned_symbol("map_ptcl")->native_data == effects->command_root() &&
                  find_owned_symbol("map_texg")->native_data == effects->texture_root() &&
                  find_owned_symbol("yakumono_param")->native_data == yakumono_data &&
                  find_owned_symbol("quake_model_set")->native_data == quake->single_model() &&
                  find_owned_symbol("SIS_GrPStadiumData")->native_data == stadium_sis->descriptor() &&
                  find_owned_symbol("GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc")->native_data == native_image,
              "Diagnostic catalog identities differ from their typed source owners");
        if (perform_on_init) {
            check(!melee_web_effect_runtime_prepared() &&
                      !melee_web_effect_runtime_active(),
                  "Source-ordered OnInit requires an unowned original effect runtime");
            trace.sis_lease("before_begin", retired_sis);
            check(melee_web_diagnostic_sis_begin_retired(
                      &sis_owner, retired_sis, error, sizeof(error)), error);
            const int effect_begin_succeeded =
                melee_web_effect_runtime_begin(error, sizeof(error));
            effect_runtime_owned = melee_web_effect_runtime_prepared();
            check(effect_begin_succeeded, error);
            check(effect_runtime_owned && melee_web_effect_runtime_active(),
                  "Original effects were not initialized before stage-map and bank publication");
        }
        stage_map = melee_web_stage_map_publish(
            native_map_head, error, sizeof(error));
        check(stage_map != nullptr, error);
        check(melee_web_stage_map_set_public(
                  stage_map, symbols.data(), symbols.size(), error,
                  sizeof(error)), error);
        if (perform_on_init) {
            const auto& overrides = map_owner->light_overrides();
            check(melee_web_stage_map_set_overrides(
                      stage_map, overrides.data(), overrides.size(), error,
                      sizeof(error)), error);
        }
        previous_ground_param = melee_web_ground_data_publish(ground_data);
        ground_param_published = true;
        check(previous_ground_param == before_view.param,
              "GroundParam publication did not retain the prior StageInfo owner");
        check(melee_web_effect_bank_attach(
                  effects->bank(), error, sizeof(error)), error);
        effect_bank_attached = true;
        if (!perform_on_init) {
            check(melee_web_stage_selection_begin(St_Kind_PStadium),
                  "Could not scope original StageInfo selection for StKind 3");
            stage_selection_owned = true;
        }
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                  lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "E8 request lost its source VS and two-language scopes");
        verify_selection_boundary("before-e8", false);

        if (perform_on_init) {
            auto& on_init = on_init_observation;
            check(melee_web_source_memory_context_read(
                      &on_init.memory_before_init) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      melee_web_source_memory_healthy() &&
                      melee_web_gameplay_world_exists(),
                  "OnInit boundary lacks a healthy active source-memory/world owner");
            on_init.stats_before_init = melee_web_gameplay_stats();
            check(on_init.stats_before_init.generation ==
                      on_init.memory_before_init.world_generation,
                  "OnInit boundary source-memory/world generations disagree");
            melee_web_stadium_c1_heap_owner_arm();
            c1_try_emit_v23_heap_census(
                on_init.census_observer_failed, "before-light-preparation",
                on_init.stats_before_init);
            melee_web_stadium_c1_heap_owner_mark(
                MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_BEFORE_LIGHT,
                !on_init.census_observer_failed);
            check(melee_web_stadium_c1_stage_info_current_view(
                      &on_init.stage_info_before_init),
                  "OnInit boundary could not observe its typed pre-call StageInfo");
            check(melee_web_ground_map_storage_available(),
                  "OnInit boundary found pre-existing Ground storage ownership");
            check(melee_web_stadium_c1_stage_object_failures() == 0 &&
                      source_stage_registry_empty() &&
                      source_stage_gobj_count() == 0 &&
                      source_stage_markers_empty() && ground_dispatch_quiet(),
                  "OnInit boundary requires an empty stage/Ground baseline");
            on_init.roots_before = melee_web::test::stadium_screen::runtime_roots_snapshot();
            on_init.class_counts_before = melee_web::test::stadium_screen::live_class_counts();
            on_init.pool_counts_before = melee_web::test::stadium_screen::live_pool_counts();
            on_init.gobj_pool_before = HSD_ObjAllocGetUsing(&gobj_alloc_data);
            on_init.proc_pool_before = HSD_ObjAllocGetUsing(&gobjproc_alloc_data);
            on_init.stage_gobj_count_before = source_stage_gobj_count();
            on_init.scheduler_cycle_before = HSD_GObj_804D783C;
            on_init.device_snapshot =
                melee_web_stadium_c1_ft_device_snapshot_create();
            check(on_init.device_snapshot != nullptr,
                  "OnInit boundary could not snapshot typed ftDevice globals");
        }

        if (perform_on_init) {
            // Reuse normal gameplay's CPU light context without constructing a
            // strict full-stage owner. Both animation tables and authored entry
            // counts borrow the retained structural map's checked storage.
            const DatLights light_data(*archive, "map_plit", true);
            light_context = melee_web_stage_lights_create(
                light_data.lights.data(), light_data.lights.size(), error, sizeof(error));
            check(light_context != nullptr, error);
            for (uint32_t i = 0; i < light_data.lights.size(); ++i) {
                const auto flags = read_dat_light_override(
                    *archive, light_data.lights[i].source_offset);
                check(melee_web_stage_lights_set_override(light_context, i,
                          flags.has_value(), flags.value_or(0), error, sizeof(error)), error);
                if (light_data.animation_tables[i]) {
                    void* const table = map_owner->light_animation_table(
                        *light_data.animation_tables[i]);
                    check(table == map_owner->light_animation_table(
                              *light_data.animation_tables[i]),
                          "Borrowed map light animation table identity changed");
                    check(melee_web_stage_lights_set_animations(
                              light_context, i, table, error, sizeof(error)), error);
                }
            }
            check(melee_web_stage_lights_attach(light_context, error, sizeof(error)), error);
            const auto counts = map_owner->source_light_counts();
            check(melee_web_stage_lights_set_source_counts(light_context,
                      counts.data(), counts.size(), error, sizeof(error)), error);
        }
        if (perform_on_init) {
            on_init_observation.stats_after_light_preparation = melee_web_gameplay_stats();
            on_init_observation.light_preparation_stats_captured = true;
            c1_try_emit_v23_heap_census(
                on_init_observation.census_observer_failed,
                "after-light-preparation-before-e8",
                on_init_observation.stats_after_light_preparation);
            melee_web_stadium_c1_heap_owner_mark(
                MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_LIGHT,
                !on_init_observation.census_observer_failed);
        }
        check(melee_web_stadium_e8_call_observer_begin(),
              "Could not open the bounded E8 source-call window");
        observer_window_owned = true;
        check_stadium_rng_witness(selection_rng, selected, "before-original-e8-oninit");
        if (perform_on_init) {
            returned_stage_owner = melee_web_stage_begin_kind_on_init_diagnostic(
                    St_Kind_PStadium, yakumono_data, effects->bank(),
                    &retained_stage_owner, error, sizeof(error));
            if (returned_stage_owner != nullptr &&
                retained_stage_owner == nullptr)
                retained_stage_owner = returned_stage_owner;
            check(returned_stage_owner != nullptr &&
                      returned_stage_owner == retained_stage_owner,
                  error[0] ? error
                           : "Original Stadium OnInit did not return its retained owner");
            check(seed_ptr == seed_owner && seed_owner == selection_rng.owner,
                  "OnInit returned with a replaced retained menu RNG owner");
            selection_rng.expected_live = *seed_owner;
            selection_rng.source_return_captured = true;
            on_init_observation.seed_after_on_init = selection_rng.expected_live;
            check_stadium_rng_witness(selection_rng, selected, "on-init-return");
            check(melee_web_stage_last_stadium_map2_buffer_snapshot(
                      retained_stage_owner,
                      &on_init_observation.map2_owner),
                  "OnInit owner did not expose its captured map2 provenance record");
            check(melee_web_stage_last_stadium_source_journal_snapshot(
                      retained_stage_owner,
                      &on_init_observation.source_journal),
                  "OnInit owner did not expose its actual source-event journal");
        } else {
            Stage_802251E8(St_Kind_PStadium, NULL);
        }
        check(melee_web_stadium_e8_call_observer_end(&observed),
              "Could not close the bounded E8 source-call window");
        observer_window_owned = false;

        const char* const expected_size_name = "/GrPs.usd";
        check(observed.source_size_calls == 1 &&
                  observed.source_size_successes == 1 &&
                  observed.source_size_name_mismatches == 0 &&
                  observed.source_size_bytes == raw_bytes.size() &&
                  std::strcmp(observed.source_size_name,
                              expected_size_name) == 0,
              "E8 source-size call did not resolve exact /GrPs.usd bytes");
        check(observed.typed_open_calls == 1 &&
                  observed.typed_open_successes == 1 &&
                  observed.typed_open_name_mismatches == 0 &&
                  observed.typed_handle_mismatches == 0 &&
                  std::strcmp(observed.typed_open_name,
                              expected_size_name) == 0 &&
                  observed.typed_archive_handle != nullptr,
              "E8 typed open did not preserve and resolve exact /GrPs.usd identity");
        check(observed.map_head_calls == 1 &&
                  observed.map_head_archive == observed.typed_archive_handle &&
                  observed.map_head_value == native_map_head &&
                  observed.coll_data_calls == 1 &&
                  observed.coll_data_value == native_collision &&
                  observed.ground_param_calls == 0 &&
                  observed.itemdata_calls == 0 &&
                  observed.ald_yaku_all_calls == 1 &&
                  observed.ald_yaku_all_value == native_ald_yaku &&
                  observed.map_ptcl_calls == 1 &&
                  observed.map_ptcl_value == effects->command_root() &&
                  observed.map_texg_calls == 1 &&
                  observed.map_texg_value == effects->texture_root() &&
                  observed.yakumono_param_calls == 1 &&
                  observed.yakumono_param_value == yakumono_data &&
                  observed.map_plit_calls == 0 &&
                  observed.quake_model_set_calls == 1 &&
                  observed.quake_model_set_value == quake->single_model() &&
                  observed.other_public_calls == 0,
              "E8 source public lookup results differed from the typed owners");

        check(melee_web_stadium_c1_stage_info_current_view(&after_view),
              "Could not inspect StageInfo after the source E8 request");
        check(after_view.grkind == Gr_Kind_PStadium &&
                  after_view.param == ground_data &&
                  after_view.x6E4[0] == -1 &&
                  after_view.x6E4[1] == before_view.x6E4[1] &&
                  after_view.coll_data == native_collision &&
                  after_view.ald_yaku_all == native_ald_yaku &&
                  after_view.map_ptcl == effects->command_root() &&
                  after_view.map_texg == effects->texture_root() &&
                  after_view.yakumono_param == yakumono_data &&
                  after_view.quake_model_set == quake->single_model(),
              "Source StageInfo did not retain the checked typed owner pointers");
        check(after_view.itemdata == nullptr &&
                  after_view.itemdata == before_view.itemdata &&
                  after_view.map_plit == (perform_on_init
                      ? melee_web_stage_lights_descriptors(light_context)
                      : before_view.map_plit),
              "E8 request replaced its authored item root or checked light context");
        if (!perform_on_init) {
            check(melee_web_stadium_c1_stage_object_failures() == 0,
                  "E8 request entered stage objects, Ground, item, or light state");
        } else {
            auto& on_init = on_init_observation;
            const uint32_t active_failures =
                melee_web_stadium_c1_stage_object_failures();
            check((active_failures & MELEE_WEB_STADIUM_C1_STAGE_LIST_UNAVAILABLE) == 0 &&
                      (active_failures & MELEE_WEB_STADIUM_C1_STAGE_ITEMS) == 0 &&
                      (active_failures & MELEE_WEB_STADIUM_C1_STAGE_LIGHTS) != 0,
                  "OnInit did not retain a readable map graph and its checked published light context");
            const std::array<int, 4> map_ids{0, 1, 2, 5};
            const std::array<void*, 4> recorded_maps{
                on_init.map2_owner.map0_ground,
                on_init.map2_owner.display_ground,
                on_init.map2_owner.map2_ground,
                on_init.map2_owner.nested_map5_ground,
            };
            const std::array<MeleeWebStadiumSourceEventKind, 4> entry_events{
                MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_E8,
                MELEE_WEB_STADIUM_SOURCE_EVENT_STAGE_24C,
                MELEE_WEB_STADIUM_SOURCE_EVENT_GROUND_0800,
                MELEE_WEB_STADIUM_SOURCE_EVENT_ON_INIT,
            };
            check(on_init.source_journal.count ==
                          MELEE_WEB_STADIUM_SOURCE_EVENT_CAPACITY &&
                      !on_init.source_journal.failed &&
                      !on_init.source_journal.overflowed,
                  "OnInit source-event journal is incomplete, reordered, or overflowed");
            for (size_t i = 0; i < entry_events.size(); ++i) {
                const auto& event = on_init.source_journal.events[i];
                check(event.kind == entry_events[i] && event.map_id == -1 &&
                          event.gobj == nullptr,
                      "OnInit source-entry taps differed from E8→24C→Ground→OnInit");
            }
            check(on_init.map2_owner.captured == 1 &&
                      on_init.map2_owner.buffer != nullptr &&
                      (on_init.map2_owner.origin ==
                           MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_BORROWED_PRELOAD ||
                       on_init.map2_owner.origin ==
                           MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK),
                  "OnInit map2 journal lacks a captured exact source buffer origin");
            check(melee_web_stadium_c1_ground_map_slot_count() > 5,
                  "OnInit observer cannot address the authored map5 StageInfo slot");
            for (size_t i = 0; i < map_ids.size(); ++i) {
                check(recorded_maps[i] != nullptr &&
                          recorded_maps[i] ==
                              melee_web_stadium_c1_ground_map_slot(
                                  static_cast<size_t>(map_ids[i])),
                      "OnInit map2 owner record differs from an actual StageInfo map slot");
                const auto& event = on_init.source_journal.events[4 + i];
                check(event.kind == MELEE_WEB_STADIUM_SOURCE_EVENT_MAP_GOBJ &&
                          event.map_id == map_ids[i] &&
                          event.gobj == recorded_maps[i],
                      "OnInit actual map return journal differs from its owner/slot pointer");
            }
            on_init.stats_after_on_init = melee_web_gameplay_stats();
            c1_try_emit_v23_heap_census(
                on_init.census_observer_failed, "after-oninit",
                on_init.stats_after_on_init);
            melee_web_stadium_c1_heap_owner_mark(
                MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_ONINIT,
                !on_init.census_observer_failed);
            c1_heap_graph_emit_after_oninit(
                !on_init.census_observer_failed,
                on_init.stats_after_on_init);
            check_stadium_rng_witness(selection_rng, selected, "on-init-verified");
            check(on_init.stats_after_on_init.generation ==
                      on_init.stats_before_init.generation &&
                      on_init.stats_after_on_init.ticks ==
                          on_init.stats_before_init.ticks &&
                      HSD_GObj_804D783C == on_init.scheduler_cycle_before &&
                      ground_dispatch_quiet(),
                  "OnInit driver advanced a source tick or dispatched a scheduled callback");
            check(melee_web_source_memory_context_read(
                      &on_init.memory_before_end) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      on_init.memory_before_end.source_heap_handle ==
                          on_init.memory_before_init.source_heap_handle &&
                      on_init.memory_before_end.world_generation ==
                          on_init.memory_before_init.world_generation,
                  "OnInit changed the source-memory context before teardown");
            on_init.map2_allocation_status_before_end =
                melee_web_source_memory_allocation_read(
                    on_init.map2_owner.buffer, &on_init.map2_before_end);
            check(on_init.map2_allocation_status_before_end ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      on_init.map2_before_end.world_generation ==
                          on_init.memory_before_init.world_generation,
                  "Map2 buffer allocation observer lost the active source world");
            if (on_init.map2_owner.origin ==
                MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK) {
                check(on_init.map2_before_end.live == 1 &&
                          on_init.map2_before_end.requested_bytes == 0x50000 &&
                          on_init.map2_before_end.allocation_generation >
                              on_init.memory_before_init.allocation_generation_watermark &&
                          on_init.map2_before_end.source_heap_handle ==
                              on_init.memory_before_init.source_heap_handle,
                      "Authored map2 fallback is not the exact live 0x50000 source allocation");
            } else if (on_init.map2_before_end.live) {
                check(on_init.map2_before_end.allocation_generation != 0 &&
                          on_init.map2_before_end.source_heap_handle ==
                              on_init.memory_before_init.source_heap_handle,
                      "Tracked preloaded map2 buffer has an invalid source allocation identity");
            }
            check(melee_web_ground_map_storage_read(
                      &on_init.ground_storage_live) &&
                      on_init.ground_storage_live.payload != nullptr &&
                      on_init.ground_storage_live.requested_bytes == 64,
                  "OnInit did not retain its exact original 64-byte Ground storage owner");
            on_init.ground_storage_allocation_status_before_end =
                melee_web_source_memory_allocation_read(
                    on_init.ground_storage_live.payload,
                    &on_init.ground_storage_before_end);
            check(melee_web::test::stadium_buffer::exact_new_allocation_supported(
                      static_cast<MeleeWebSourceMemoryReadStatus>(
                          on_init.ground_storage_allocation_status_before_end),
                      on_init.ground_storage_before_end,
                      on_init.memory_before_init, 64),
                  "OnInit Ground storage did not retain its exact new 64-byte lease");
            verify_selection_boundary("after-oninit", true);
            check(seed_ptr == seed_owner &&
                      gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                      lbLang_GetLanguageSetting() == LANG_US &&
                      lbLang_GetSavedLanguage() == LANG_US,
                  "OnInit changed its retained source seed owner or scoped VS/language state");
            std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after_init{};
            check(melee_web_menu_host_snapshot_card_data(
                      host, 0, save_after_init.data(), save_after_init.size(),
                      error, sizeof(error)), error);
            check(save_after_init == save_before && raw_bytes == raw_before &&
                      std::equal(archive_data_before.begin(),
                                 archive_data_before.end(), archive->data().begin(),
                                 archive->data().end()),
                  "OnInit changed the selected save or immutable GrPs owner bytes");
            trace.event("stadium_source_oninit_returned", world->audio(),
                        "diagnostic-source-ordered-pstadium", &selected,
                        &on_init.seed_after_on_init);

            // Publish the already checked copied observations before retirement.
            // Pointer values below are historical identities only: never follow
            // them after StageLast has removed the corresponding source objects.
            std::fprintf(stderr,
                "C1_ONINIT_CAPTURE count=%u failed=%d overflowed=%d map2_origin=%d map2_buffer=%p map2_read=%d map2_live=%d map2_bytes=%u map2_world=%llu map2_allocation=%llu ground_buffer=%p ground_read=%d ground_bytes=%u ground_world=%llu ground_allocation=%llu\n",
                on_init.source_journal.count, on_init.source_journal.failed,
                on_init.source_journal.overflowed, static_cast<int>(on_init.map2_owner.origin),
                on_init.map2_owner.buffer, on_init.map2_allocation_status_before_end,
                on_init.map2_before_end.live, on_init.map2_before_end.requested_bytes,
                static_cast<unsigned long long>(on_init.map2_before_end.world_generation),
                static_cast<unsigned long long>(on_init.map2_before_end.allocation_generation),
                on_init.ground_storage_live.payload,
                on_init.ground_storage_allocation_status_before_end,
                on_init.ground_storage_before_end.requested_bytes,
                static_cast<unsigned long long>(on_init.ground_storage_before_end.world_generation),
                static_cast<unsigned long long>(on_init.ground_storage_before_end.allocation_generation));
            for (size_t i = 0; i < on_init.source_journal.count; ++i) {
                const auto& event = on_init.source_journal.events[i];
                std::fprintf(stderr, "C1_ONINIT_CAPTURE_TAP index=%zu kind=%s map_id=%d gobj=%p\n",
                    i, stadium_source_event_kind_name(event.kind), event.map_id,
                    static_cast<void*>(event.gobj));
            }
            std::fflush(stderr);

            const auto class_counts_before_end = on_init.class_counts_before;
            const auto pool_counts_before_end = on_init.pool_counts_before;
            const auto roots_before_end = on_init.roots_before;
            const uint32_t gobj_pool_before_end = on_init.gobj_pool_before;
            const uint32_t proc_pool_before_end = on_init.proc_pool_before;
            const uint32_t stage_gobj_count_before_end =
                on_init.stage_gobj_count_before;
            const int scheduler_cycle_before_end =
                on_init.scheduler_cycle_before;
            // OnInit-only assertions retain their original construction boundary.
            auto retirement_memory_before_end = on_init.memory_before_end;
            if (stage_start_context) {
                check(melee_web_stage_last_stadium_start(
                          retained_stage_owner, stage_start_context,
                          error, sizeof(error)), error);
                check(seed_ptr == seed_owner && *seed_owner == on_init.seed_after_on_init &&
                          melee_web_gameplay_stats().ticks == on_init.stats_before_init.ticks &&
                          HSD_GObj_804D783C == scheduler_cycle_before_end,
                      "Stadium source OnLoad/OnStart changed RNG or dispatched a scheduler tick");
                trace.event("stadium_source_onstart_returned", world->audio(),
                            "original-onload-onstart-and-owned-header-drain", &selected,
                            &on_init.seed_after_on_init);
                check(melee_web_source_memory_context_read(&retirement_memory_before_end) ==
                          MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                          retirement_memory_before_end.world_generation == on_init.memory_before_end.world_generation &&
                          retirement_memory_before_end.source_heap_handle == on_init.memory_before_end.source_heap_handle &&
                          melee_web_source_memory_healthy(),
                      "Started Stage construction changed its retirement memory owner");
                MeleeWebSourceMemoryAllocation ground_at_retirement{}, map2_at_retirement{};
                const auto ground_status = melee_web_source_memory_allocation_read(
                    on_init.ground_storage_live.payload, &ground_at_retirement);
                const auto map2_status = melee_web_source_memory_allocation_read(
                    on_init.map2_owner.buffer, &map2_at_retirement);
                std::fprintf(stderr,
                    "C3_RETIREMENT_BASELINE world=%llu heap=%d oninit_watermark=%llu retirement_watermark=%llu ground_status=%d ground_live=%d ground_requested=%u ground_generation=%llu map2_status=%d map2_live=%d map2_requested=%u map2_generation=%llu\n",
                    static_cast<unsigned long long>(retirement_memory_before_end.world_generation),
                    retirement_memory_before_end.source_heap_handle,
                    static_cast<unsigned long long>(on_init.memory_before_end.allocation_generation_watermark),
                    static_cast<unsigned long long>(retirement_memory_before_end.allocation_generation_watermark),
                    ground_status, ground_at_retirement.live, ground_at_retirement.requested_bytes,
                    static_cast<unsigned long long>(ground_at_retirement.allocation_generation),
                    map2_status, map2_at_retirement.live, map2_at_retirement.requested_bytes,
                    static_cast<unsigned long long>(map2_at_retirement.allocation_generation));
                std::fflush(stderr);
                check(melee_web::test::stadium_buffer::exact_live_allocation_matches_context(
                          ground_status, ground_at_retirement, retirement_memory_before_end, 64,
                          on_init.ground_storage_before_end.allocation_generation),
                      "Started construction changed the original Ground lease before retirement");
                if (on_init.map2_owner.origin == MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK)
                    check(melee_web::test::stadium_buffer::exact_live_allocation_matches_context(
                              map2_status, map2_at_retirement, retirement_memory_before_end,
                              on_init.map2_before_end.requested_bytes, on_init.map2_before_end.allocation_generation),
                          "Started construction changed the original owned map2 lease before retirement");
            }
            check(melee_web_stage_last_end(retained_stage_owner, error,
                                           sizeof(error)), error);
            on_init_stage_end_succeeded = true;
            check(melee_web_stage_lights_destroy(light_context, error, sizeof(error)), error);
            light_context = nullptr;
            retained_stage_owner = nullptr;
            returned_stage_owner = nullptr;
            on_init.stats_after_end = melee_web_gameplay_stats();
            c1_try_emit_v23_heap_census(
                on_init.census_observer_failed,
                "after-stage-last-and-light-destroy",
                on_init.stats_after_end);
            melee_web_stadium_c1_heap_owner_mark(
                MELEE_WEB_STADIUM_C1_HEAP_OWNER_PHASE_AFTER_STAGE_LAST,
                !on_init.census_observer_failed);
            c1_heap_graph_emit_after_stage_last(
                !on_init.census_observer_failed);
            check_stadium_rng_witness(selection_rng, selected, "stage-last-end");
            on_init.seed_after_end = *seed_owner;
            on_init.map2_allocation_status_after_end =
                melee_web_source_memory_allocation_read(
                    on_init.map2_owner.buffer, &on_init.map2_after_end);
            on_init.ground_storage_allocation_status_after_end =
                melee_web_source_memory_allocation_read(
                    on_init.ground_storage_live.payload,
                    &on_init.ground_storage_after_end);
            check(melee_web_source_memory_context_read(
                      &on_init.memory_after_end) ==
                      MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                      melee_web_ground_map_storage_read(
                          &on_init.ground_storage_ended) &&
                      on_init.ground_storage_ended.payload == nullptr &&
                      on_init.ground_storage_ended.requested_bytes == 64 &&
                      melee_web_ground_map_storage_available(),
                  "Original OnInit teardown did not release Ground storage ownership");
            std::fprintf(stderr,
                "C3_RETIRED_LEASES before_world=%llu after_world=%llu before_heap=%d after_heap=%d before_watermark=%llu after_watermark=%llu ground_status=%d ground_live=%d ground_requested=%u ground_generation=%llu ground_world=%llu ground_heap=%d map2_status=%d map2_live=%d map2_requested=%u map2_generation=%llu map2_world=%llu map2_heap=%d\n",
                static_cast<unsigned long long>(retirement_memory_before_end.world_generation),
                static_cast<unsigned long long>(on_init.memory_after_end.world_generation),
                retirement_memory_before_end.source_heap_handle, on_init.memory_after_end.source_heap_handle,
                static_cast<unsigned long long>(retirement_memory_before_end.allocation_generation_watermark),
                static_cast<unsigned long long>(on_init.memory_after_end.allocation_generation_watermark),
                on_init.ground_storage_allocation_status_after_end, on_init.ground_storage_after_end.live,
                on_init.ground_storage_after_end.requested_bytes,
                static_cast<unsigned long long>(on_init.ground_storage_after_end.allocation_generation),
                static_cast<unsigned long long>(on_init.ground_storage_after_end.world_generation),
                on_init.ground_storage_after_end.source_heap_handle,
                on_init.map2_allocation_status_after_end, on_init.map2_after_end.live,
                on_init.map2_after_end.requested_bytes,
                static_cast<unsigned long long>(on_init.map2_after_end.allocation_generation),
                static_cast<unsigned long long>(on_init.map2_after_end.world_generation),
                on_init.map2_after_end.source_heap_handle);
            std::fflush(stderr);
            check(melee_web::test::stadium_buffer::exact_retired_allocation_supported(
                      static_cast<MeleeWebSourceMemoryReadStatus>(
                          on_init.ground_storage_allocation_status_after_end),
                      on_init.ground_storage_after_end,
                      MELEE_WEB_SOURCE_MEMORY_READ_OK,
                      retirement_memory_before_end,
                      MELEE_WEB_SOURCE_MEMORY_READ_OK,
                      on_init.memory_after_end),
                  "Original OnInit teardown did not retire its exact Ground storage lease");
            if (on_init.map2_owner.origin ==
                MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK) {
                check(melee_web::test::stadium_buffer::exact_retired_allocation_supported(
                          static_cast<MeleeWebSourceMemoryReadStatus>(
                              on_init.map2_allocation_status_after_end),
                          on_init.map2_after_end,
                          static_cast<MeleeWebSourceMemoryReadStatus>(
                              on_init.map2_allocation_status_before_end),
                          retirement_memory_before_end,
                          MELEE_WEB_SOURCE_MEMORY_READ_OK,
                          on_init.memory_after_end),
                      "Original OnInit teardown did not retire the exact owned map2 fallback");
            } else {
                check(on_init.map2_allocation_status_after_end ==
                          on_init.map2_allocation_status_before_end &&
                          on_init.map2_after_end.source_heap_handle ==
                              on_init.map2_before_end.source_heap_handle &&
                          on_init.map2_after_end.requested_bytes ==
                              on_init.map2_before_end.requested_bytes &&
                          on_init.map2_after_end.world_generation ==
                              on_init.map2_before_end.world_generation &&
                          on_init.map2_after_end.allocation_generation ==
                              on_init.map2_before_end.allocation_generation &&
                          on_init.map2_after_end.live ==
                              on_init.map2_before_end.live,
                      "Original OnInit teardown changed a borrowed preload buffer lease");
            }
            if (stage_start_context) {
                std::fprintf(stderr,
                    "C3_STAGE_RETIRED before_world=%llu after_world=%llu before_ticks=%llu after_ticks=%llu before_heap=%d after_heap=%d before_objects=%u after_objects=%u before_processes=%u after_processes=%u before_gobj_used=%u after_gobj_used=%u before_proc_used=%u after_proc_used=%u before_cycle=%d after_cycle=%d\n",
                    static_cast<unsigned long long>(on_init.stats_before_init.generation),
                    static_cast<unsigned long long>(on_init.stats_after_end.generation),
                    static_cast<unsigned long long>(on_init.stats_before_init.ticks),
                    static_cast<unsigned long long>(on_init.stats_after_end.ticks),
                    on_init.stats_before_init.heap_free_bytes, on_init.stats_after_end.heap_free_bytes,
                    on_init.stats_before_init.objects, on_init.stats_after_end.objects,
                    on_init.stats_before_init.processes, on_init.stats_after_end.processes,
                    gobj_pool_before_end, HSD_ObjAllocGetUsing(&gobj_alloc_data),
                    proc_pool_before_end, HSD_ObjAllocGetUsing(&gobjproc_alloc_data),
                    scheduler_cycle_before_end, HSD_GObj_804D783C);
                std::fprintf(stderr,
                    "C3_STAGE_OWNERS registry_empty=%d markers_empty=%d stage_count_before=%u stage_count_after=%u stage_failures=%u device_matches=%d memory_healthy=%d quiet=%d gobj_free=%u gobj_freehead=%p proc_free=%u proc_freehead=%p\n",
                    source_stage_registry_empty(), source_stage_markers_empty(),
                    stage_gobj_count_before_end, source_stage_gobj_count(),
                    melee_web_stadium_c1_stage_object_failures(),
                    melee_web_stadium_c1_ft_device_snapshot_matches(on_init.device_snapshot),
                    melee_web_source_memory_healthy(), ground_dispatch_quiet(),
                    gobj_alloc_data.free, static_cast<void*>(gobj_alloc_data.freehead),
                    gobjproc_alloc_data.free, static_cast<void*>(gobjproc_alloc_data.freehead));
                std::fflush(stderr);
                const auto classes_retired = melee_web::test::stadium_screen::live_class_counts();
                const auto pools_retired = melee_web::test::stadium_screen::live_pool_counts();
                const auto roots_retired = melee_web::test::stadium_screen::runtime_roots_snapshot();
                for (const auto& [identity, expected] : class_counts_before_end) {
                    const auto found = classes_retired.find(identity);
                    std::cerr << "C3_CLASS identity=" << static_cast<void*>(identity)
                              << " before=" << expected << " after="
                              << (found == classes_retired.end() ? 0 : found->second) << '\n';
                }
                for (const auto& [identity, actual] : classes_retired)
                    if (!class_counts_before_end.contains(identity))
                        std::cerr << "C3_CLASS identity=" << static_cast<void*>(identity)
                                  << " before=0 after=" << actual << '\n';
                for (size_t i = 0; i < pools_retired.size(); ++i)
                    std::cerr << "C3_POOL index=" << i << " before=" << pool_counts_before_end[i]
                              << " after=" << pools_retired[i] << '\n';
                std::cerr << "C3_ROOTS before_bytes=" << roots_before_end.size()
                          << " after_bytes=" << roots_retired.size()
                          << " equal=" << (roots_retired == roots_before_end) << '\n';
                for (size_t i = 0; i < std::min(roots_retired.size(), roots_before_end.size()); ++i)
                    if (roots_retired[i] != roots_before_end[i])
                        std::cerr << "C3_ROOT_DIFFERENCE offset=" << i
                                  << " before=" << unsigned(roots_before_end[i])
                                  << " after=" << unsigned(roots_retired[i]) << '\n';
                std::cerr.flush();
                // Full-world scope reports component heap/cache retention without
                // substituting a threshold for the old same-world predicate.
                check(on_init.stats_after_end.generation == on_init.stats_before_init.generation &&
                          on_init.stats_after_end.ticks == on_init.stats_before_init.ticks &&
                          on_init.stats_after_end.objects == on_init.stats_before_init.objects &&
                          on_init.stats_after_end.processes == on_init.stats_before_init.processes,
                      "Started Stadium retirement changed world/ticks or left source objects/processes live");
                check(source_stage_registry_empty() && source_stage_markers_empty() &&
                          source_stage_gobj_count() == stage_gobj_count_before_end &&
                          melee_web_stadium_c1_stage_object_failures() == 0 &&
                          HSD_ObjAllocGetUsing(&gobj_alloc_data) == gobj_pool_before_end &&
                          HSD_ObjAllocGetUsing(&gobjproc_alloc_data) == proc_pool_before_end &&
                          HSD_GObj_804D783C == scheduler_cycle_before_end &&
                          classes_retired == class_counts_before_end &&
                          pools_retired == pool_counts_before_end &&
                          roots_retired == roots_before_end &&
                          melee_web_stadium_c1_ft_device_snapshot_matches(on_init.device_snapshot) == 1 &&
                          melee_web_source_memory_healthy() && ground_dispatch_quiet(),
                      "Started Stadium retirement left source roots, used pools, devices or callbacks live");
                std::cout << "{\"probe\":\"stadium-source-start-retired\",\"world\":"
                          << on_init.stats_after_end.generation
                          << ",\"heap_before_init\":" << on_init.stats_before_init.heap_free_bytes
                          << ",\"heap_after_stage_retirement\":" << on_init.stats_after_end.heap_free_bytes
                          << ",\"ticks\":0,\"onload_onstart_called\":true,\"checked_retirement\":true}\n";
            } else {
            // Keep the original left-to-right, fail-first conjunction order.
            // Every baseline and exact equality remains unchanged; do not read
            // later runtime graphs when an earlier scalar condition has failed.
            const char* const teardown_error =
                "Original OnInit teardown did not restore source lists, pools, ticks, leases, or typed devices";
            auto equal = [&](const char* field, auto actual, auto expected) {
                if (actual != expected) {
                    std::cerr << "C1_TEARDOWN_DIFFERENCE field=" << field
                              << " expected=" << expected << " observed=" << actual << '\n';
                    std::cerr.flush();
                }
                check(actual == expected, teardown_error);
            };
            equal("generation", on_init.stats_after_end.generation, on_init.stats_before_init.generation);
            equal("ticks", on_init.stats_after_end.ticks, on_init.stats_before_init.ticks);
            equal("objects", on_init.stats_after_end.objects, on_init.stats_before_init.objects);
            equal("processes", on_init.stats_after_end.processes, on_init.stats_before_init.processes);
            equal("heap_free_bytes", on_init.stats_after_end.heap_free_bytes, on_init.stats_before_init.heap_free_bytes);
            equal("allocation_generation_watermark", on_init.memory_after_end.allocation_generation_watermark,
                  on_init.memory_before_end.allocation_generation_watermark);
            equal("source_heap_handle", on_init.memory_after_end.source_heap_handle, on_init.memory_before_init.source_heap_handle);
            equal("world_generation", on_init.memory_after_end.world_generation, on_init.memory_before_init.world_generation);
            equal("stage_registry_empty", source_stage_registry_empty(), true);
            equal("stage_gobj_count", source_stage_gobj_count(), stage_gobj_count_before_end);
            equal("stage_markers_empty", source_stage_markers_empty(), true);
            equal("stage_object_failures", melee_web_stadium_c1_stage_object_failures(), uint32_t{0});
            equal("gobj_pool_used", HSD_ObjAllocGetUsing(&gobj_alloc_data), gobj_pool_before_end);
            equal("proc_pool_used", HSD_ObjAllocGetUsing(&gobjproc_alloc_data), proc_pool_before_end);
            equal("scheduler_cycle", HSD_GObj_804D783C, scheduler_cycle_before_end);
            const auto classes_after_end = melee_web::test::stadium_screen::live_class_counts();
            if (classes_after_end != class_counts_before_end) {
                // Compare copied map keys/counts without dereferencing class identities.
                for (const auto& [identity, expected] : class_counts_before_end) {
                    const auto found = classes_after_end.find(identity);
                    const auto actual = found == classes_after_end.end() ? 0 : found->second;
                    if (actual != expected)
                        std::cerr << "C1_TEARDOWN_DIFFERENCE field=live_class_counts identity="
                                  << static_cast<void*>(identity) << " expected=" << expected << " observed=" << actual << '\n';
                }
                for (const auto& [identity, actual] : classes_after_end)
                    if (!class_counts_before_end.contains(identity))
                        std::cerr << "C1_TEARDOWN_DIFFERENCE field=live_class_counts identity="
                                  << static_cast<void*>(identity) << " expected=0 observed=" << actual << '\n';
                std::cerr.flush();
            }
            check(classes_after_end == class_counts_before_end, teardown_error);
            const auto pools_after_end = melee_web::test::stadium_screen::live_pool_counts();
            for (size_t i = 0; i < pools_after_end.size(); ++i)
                if (pools_after_end[i] != pool_counts_before_end[i])
                    std::cerr << "C1_TEARDOWN_DIFFERENCE field=live_pool_counts index=" << i
                              << " expected=" << pool_counts_before_end[i] << " observed=" << pools_after_end[i] << '\n';
            std::cerr.flush();
            check(pools_after_end == pool_counts_before_end, teardown_error);
            const auto roots_after_end = melee_web::test::stadium_screen::runtime_roots_snapshot();
            if (roots_after_end != roots_before_end) {
                std::cerr << "C1_TEARDOWN_DIFFERENCE field=runtime_roots bytes_expected=" << roots_before_end.size()
                          << " bytes_observed=" << roots_after_end.size();
                for (size_t i = 0; i < std::min(roots_before_end.size(), roots_after_end.size()); ++i)
                    if (roots_before_end[i] != roots_after_end[i]) {
                        std::cerr << " first_byte_offset=" << i << " expected=" << unsigned(roots_before_end[i])
                                  << " observed=" << unsigned(roots_after_end[i]);
                        break;
                    }
                std::cerr << '\n';std::cerr.flush();
            }
            check(roots_after_end == roots_before_end, teardown_error);
            equal("ft_device_snapshot_matches", melee_web_stadium_c1_ft_device_snapshot_matches(on_init.device_snapshot), 1);
            equal("source_memory_healthy", melee_web_source_memory_healthy(), 1);
            equal("ground_dispatch_quiet", ground_dispatch_quiet(), true);
            }
            check(melee_web_stadium_c1_ft_device_snapshot_release(
                      on_init.device_snapshot),
                  "Could not release restored OnInit ftDevice snapshot");
            on_init.device_snapshot = nullptr;
            on_init.cleanup_verified = true;
            trace.event("stadium_source_oninit_cleaned", world->audio(),
                        "original-stage-last-end", &selected,
                        &on_init.seed_after_end);
        }
        verify_selection_boundary("after-stage-retirement", true);
        check(seed_ptr == seed_owner &&
                  *seed_ptr == selection_rng.expected_live,
              "E8 request changed the source seed owner or value");
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                  lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "E8 request changed source VS or language state");
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        check(melee_web_menu_host_snapshot_card_data(
                  host, 0, save_after.data(), save_after.size(), error,
                  sizeof(error)), error);
        check(save_after == save_before,
              "E8 request changed the live source save owner");
        check(raw_bytes == raw_before &&
                  std::equal(archive_data_before.begin(),
                             archive_data_before.end(), archive->data().begin(),
                             archive->data().end()),
              "E8 request changed the immutable source archive bytes");
        if (perform_ground_map1_owner) {
            check(ground_storage != nullptr,
                  "Ground map1 owner lost its pre-E8 storage lease");
            run_stadium_ground_map1_owner(archive, *map_owner,
                                          *ground_storage);
            check(melee_web_stadium_c1_stage_object_failures() == 0 &&
                      source_stage_registry_empty(),
                  "Ground map1 component did not restore its empty StageInfo owner");
        }
        if (!perform_on_init) {
            trace.event("stadium_e8_request_returned", world->audio(),
                        perform_ground_map1_owner
                            ? "typed-catalog-request-plus-map1-owner-component"
                            : "typed-catalog-request-only",
                        &selected, &seed_before);
        }
        cleanup();
        check(cleanup_complete && stage_map == nullptr && snapshot == nullptr &&
                  !melee_web_stage_map_archives(),
              "E8 typed teardown left map or StageInfo snapshot owners live");
        check(melee_web_stadium_c1_stage_state_failures() == 0,
              "E8 teardown did not restore the empty source stage context");
        check(melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 != nullptr &&
                  (Toy_804A284C[3] & 4) != 0,
              "E8 typed teardown changed the reopened MenuWorld or Toy owner");
        check(melee_web_menu_host_source_scene(host) == 0 &&
                  melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
              "E8 request entered an original source menu scene");
        check(seed_ptr == seed_owner &&
                  *seed_ptr == selection_rng.expected_live,
              "E8 teardown changed the retained source seed owner or value");
        if (perform_on_init && !stage_start_context) {
            const auto& on_init = on_init_observation;
            std::cout << "{\"probe\":\"stadium-source-oninit\","
                         "\"scope\":\"one original source-ordered OnInit and one StageLast teardown\","
                         "\"source_size_name\":\""
                      << observed.source_size_name
                      << "\",\"typed_open_name\":\""
                      << observed.typed_open_name
                      << "\",\"source_size_bytes\":" << observed.source_size_bytes
                      << ",\"source_calls\":1,\"map_head\":true,\"coll_data\":true,"
                         "\"typed_ground_param\":true,\"ald_yaku_all\":true,"
                         "\"map_ptcl\":true,\"map_texg\":true,"
                         "\"yakumono_param\":true,\"quake_model_set\":true,"
                         "\"itemdata_null\":true,\"map_plit_context_owned_during_oninit\":true,"
                         "\"map_plit_null_after_teardown\":true,"
                         "\"stage_info_xA0_observed_only\":"
                      << after_view.xA0
                      << ",\"authored_map_sequence\":[0,1,2,5],"
                         "\"runtime_map_call_order_observed\":true,"
                         "\"map_slots_match_owner_record\":true,"
                         "\"runtime_source_events\":[";
            for (size_t i = 0; i < on_init.source_journal.count; ++i) {
                const auto& event = on_init.source_journal.events[i];
                if (i != 0) std::cout << ",";
                std::cout << "{\"kind\":\""
                          << stadium_source_event_kind_name(event.kind)
                          << "\",\"map_id\":";
                if (event.kind == MELEE_WEB_STADIUM_SOURCE_EVENT_MAP_GOBJ)
                    std::cout << event.map_id;
                else
                    std::cout << "null";
                std::cout << ",\"gobj\":";
                if (event.gobj != nullptr)
                    std::cout << reinterpret_cast<uintptr_t>(event.gobj);
                else
                    std::cout << "null";
                std::cout << "}";
            }
            std::cout << "],\"map0_gobj\":"
                      << reinterpret_cast<uintptr_t>(on_init.map2_owner.map0_ground)
                      << ",\"display_gobj\":"
                      << reinterpret_cast<uintptr_t>(on_init.map2_owner.display_ground)
                      << ",\"map2_gobj\":"
                      << reinterpret_cast<uintptr_t>(on_init.map2_owner.map2_ground)
                      << ",\"nested_map5_gobj\":"
                      << reinterpret_cast<uintptr_t>(on_init.map2_owner.nested_map5_ground)
                      << ",\"map2_buffer_origin\":\""
                      << stadium_map2_buffer_origin_name(on_init.map2_owner.origin)
                      << "\",\"map2_buffer_pointer\":"
                      << reinterpret_cast<uintptr_t>(on_init.map2_owner.buffer)
                      << ",\"map2_allocation_tracked\":"
                      << (on_init.map2_before_end.live ? "true" : "false")
                      << ",\"map2_requested_bytes\":"
                      << on_init.map2_before_end.requested_bytes
                      << ",\"map2_allocation_generation\":"
                      << on_init.map2_before_end.allocation_generation
                      << ",\"map2_fallback_retired\":"
                      << (on_init.map2_owner.origin ==
                                  MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK
                              ? "true" : "false")
                      << ",\"borrowed_preload_preserved\":"
                      << (on_init.map2_owner.origin ==
                                  MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_BORROWED_PRELOAD
                              ? "true" : "false")
                      << ",\"ground_storage_requested_bytes\":64,"
                         "\"ground_storage_retired\":true,"
                         "\"ft_device_bytes_restored\":true,"
                         "\"source_tick_delta\":0,"
                         "\"map2_scheduled_proc_dispatch_absent\":true,"
                         "\"camera_called\":false,\"onstart_called\":false,"
                         "\"rendered\":false,\"ordinary_admission_closed\":true,"
                         "\"source_seed_before\":"
                      << seed_before << ",\"source_seed_after_oninit\":"
                      << on_init.seed_after_on_init
                      << ",\"source_seed_after_cleanup\":"
                      << on_init.seed_after_end
                      << ",\"save_owner_unchanged\":true,"
                         "\"checked_teardown\":true}\n";
        } else {
            std::cout << "{\"probe\":\"stadium-e8-request\","
                         "\"scope\":\""
                      << (perform_ground_map1_owner
                              ? "one original E8 request plus one Ground map1 lifetime"
                              : "one original E8 request and checked typed teardown only")
                      << "\","
                         "\"source_size_name\":\""
                      << observed.source_size_name
                      << "\",\"typed_open_name\":\""
                      << observed.typed_open_name
                      << "\",\"source_size_bytes\":" << observed.source_size_bytes
                      << ",\"map_head\":true,\"coll_data\":true,"
                         "\"grGroundParam\":true,\"ALDYakuAll\":true,"
                         "\"map_ptcl\":true,\"map_texg\":true,"
                         "\"yakumono_param\":true,\"quake_model_set\":true,"
                         "\"itemdata_public_calls\":0,\"map_plit_public_calls\":0,"
                         "\"stage_info_xA0_observed_only\":"
                      << after_view.xA0 << ",\"stage_info_x6E4\":["
                      << after_view.x6E4[0] << ',' << after_view.x6E4[1]
                      << "],\"source_seed_unchanged\":true,"
                         "\"save_owner_unchanged\":true,"
                         "\"stage_objects_started\":"
                      << (perform_ground_map1_owner ? "true" : "false")
                      << ",\"checked_teardown\":true}\n";
        }
    } catch (...) {
        bool original_heap_failure = false;
        try { throw; }
        catch (const std::exception& original) {
            const auto& observation = on_init_observation;
            original_heap_failure = perform_on_init && !stage_start_context && on_init_stage_end_succeeded &&
                retained_stage_owner == nullptr && returned_stage_owner == nullptr &&
                light_context == nullptr && !observation.cleanup_verified &&
                std::string_view(original.what()) ==
                    "Original OnInit teardown did not restore source lists, pools, ticks, leases, or typed devices" &&
                observation.stats_after_end.generation == observation.stats_before_init.generation &&
                observation.stats_after_end.ticks == observation.stats_before_init.ticks &&
                observation.stats_after_end.objects == observation.stats_before_init.objects &&
                observation.stats_after_end.processes == observation.stats_before_init.processes &&
                observation.stats_after_end.heap_free_bytes != observation.stats_before_init.heap_free_bytes;
        } catch (...) { /* Unknown original failure remains without new reads. */ }
        if (original_heap_failure) {
            try { publish_stadium_heap_failure_observations(on_init_observation); }
            catch (...) {
                std::cerr << "C1_POST_HEAP_OBSERVATION status=unavailable reason=diagnostic-publication-error\n";
                std::cerr.flush();
            }
            try {
                c1_publish_heap_owner_observations("original-oninit", true);
            } catch (...) {
                std::cerr << "C1_HEAP_OWNER_RESULT scope=original-oninit"
                             " status=unavailable reason=publication-error\n";
                std::cerr.flush();
            }
        }
        if (retained_stage_owner != nullptr || returned_stage_owner != nullptr ||
            (perform_on_init && on_init_stage_end_succeeded &&
             !on_init_observation.cleanup_verified)) {
            if (retained_stage_owner != nullptr &&
                !on_init_stage_end_succeeded) {
                MeleeWebStadiumSourceJournal failure_journal{};
                if (melee_web_stage_last_stadium_source_journal_snapshot(
                        retained_stage_owner, &failure_journal)) {
                    std::cerr << "failure_source_journal={count="
                              << failure_journal.count
                              << ",failed=" << failure_journal.failed
                              << ",overflowed=" << failure_journal.overflowed;
                    for (size_t i = 0; i < failure_journal.count; ++i) {
                        const auto& event = failure_journal.events[i];
                        std::cerr << ",event[" << i << "]={kind="
                                  << stadium_source_event_kind_name(event.kind)
                                  << ",map_id=" << event.map_id
                                  << ",gobj="
                                  << static_cast<const void*>(event.gobj) << '}';
                    }
                    std::cerr << "}\n";
                } else {
                    std::cerr << "failure_source_journal=snapshot_unavailable\n";
                }
            }
            if (stage_start_context) {
                try { throw; }
                catch (const std::exception& first) {
                    std::cerr << "C3_WORLD_RETAINED phase=stage-boundary first_exception=" << first.what()
                              << " stage_owner=" << retained_stage_owner
                              << " returned_owner=" << returned_stage_owner
                              << " error_buffer=" << error << '\n';
                } catch (...) { std::cerr << "C3_WORLD_RETAINED first_exception=unknown\n"; }
            }
            std::cerr << (stage_start_context
                ? "C3 Stadium source lifecycle refused while its source owner graph must be retained; stage_owner="
                : "C1 Stadium OnInit failed while its source owner graph must be retained; stage_owner=")
                      << static_cast<const void*>(retained_stage_owner)
                      << " returned_owner="
                      << static_cast<const void*>(returned_stage_owner)
                      << " diagnostic=" << (error[0] ? error : "post-OnInit verification failed")
                      << '\n';
            std::cerr.flush();
            std::cout.flush();
            // Do not run stack or fixture cleanup against a partially owned
            // source graph; retain it until process termination for diagnosis.
            std::_Exit(1);
        }
        if (!cleanup_complete) {
            try {
                cleanup();
            } catch (...) {
                std::abort();
            }
        }
        throw;
    }
}

MeleeWebStadiumC1ItemRuntimeGlobalsView stadium_item_runtime_globals()
{
    MeleeWebStadiumC1ItemRuntimeGlobalsView view{};
    check(melee_web_stadium_c1_item_runtime_globals_view(&view),
          "C1 item-state preflight cannot read source item globals");
    return view;
}

void check_stadium_item_runtime_globals(
    const MeleeWebStadiumC1ItemRuntimeGlobalsView& expected)
{
    const auto observed = stadium_item_runtime_globals();
    check(observed.public_data == expected.public_data &&
              observed.common_articles == expected.common_articles &&
              observed.common_data == expected.common_data &&
              observed.pokemon_articles == expected.pokemon_articles &&
              observed.character_articles == expected.character_articles &&
              observed.bounce_data == expected.bounce_data &&
              observed.color_rows == expected.color_rows,
          "C1 item-state preflight did not restore item globals");
}

struct StadiumItemRuntimeEndGuard {
    MeleeWebItemRuntime* runtime = nullptr;

    void end()
    {
        if (!runtime) return;
        char error[256]{};
        check(melee_web_item_runtime_end(runtime, error, sizeof(error)), error);
        runtime = nullptr;
    }

    ~StadiumItemRuntimeEndGuard()
    {
        if (!runtime) return;
        char error[256]{};
        if (!melee_web_item_runtime_end(runtime, error, sizeof(error))) {
            std::cerr << "C1 item-state preflight teardown: " << error << '\n';
            std::abort();
        }
    }
};

void run_stadium_c1_item_state_preflight(
    const melee_web::RuntimeFiles& reopened_files,
    MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before)
{
    using namespace melee_web;
    check(host != nullptr, "C1 item-state preflight requires a retained menu host");
    stadium_c1_item_owner::require_retained_inputs(reopened_files);

    const auto& itco_bytes = reopened_files.at("ItCo.usd");
    const auto& grps_bytes = reopened_files.at("GrPs.usd");
    const std::vector<std::uint8_t> itco_before = itco_bytes;
    const std::vector<std::uint8_t> grps_before = grps_bytes;
    const auto itco_archive = std::make_shared<const DatArchive>(
        itco_bytes, DatExternalPolicy::PreserveUnresolved);
    const auto grps_archive = std::make_shared<const DatArchive>(
        grps_bytes, DatExternalPolicy::ResolveNull);
    const auto yaku_root = stadium_archive_symbol_offset(
        *grps_archive, "ALDYakuAll");
    const DatStageYaku random_yaku(grps_archive, yaku_root);
    const auto& scripts = random_yaku.scripts();

    const auto* const seed_owner = seed_ptr;
    check(seed_owner != nullptr,
          "C1 item-state preflight lost the retained source RNG owner");
    const std::uint32_t seed_before = *seed_owner;
    const MeleeWebGameplayStats stats_before = melee_web_gameplay_stats();
    check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
          "C1 item-state preflight found a pre-existing item object");
    check_stadium_preflight_stage_empty();
    check_stadium_selection_preserved(host, selected, baseline);

    for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
        NativeDatArena public_data_arena(itco_archive);
        const DatItemRegistry source_registry(*itco_archive);
        const auto random_index = static_cast<std::size_t>(
            It_PKind_Random - It_Kind_Kuriboh);
        check(random_index < source_registry.articles.size(),
              "Original Random Pokémon Article index exceeds the source registry");

        DatItemRegistryNative registered_articles(itco_archive);
        void* const random_article = registered_articles.articles()[random_index];
        stadium_c1_item_owner::require_random_article(
            source_registry.articles[random_index], random_article);
        DatItemArticle random_article_owner(
            itco_archive, *source_registry.articles[random_index],
            It_PKind_Random, random_article);
        std::array<void*, 8> script_rows_before{};
        void* random_states = nullptr;
        stadium_c1_item_owner::require_state_capacity(
            scripts, random_article_owner.state_count());
        for (std::size_t row = 1; row < scripts.size(); ++row) {
            if (!scripts[row]) continue;
            check(row < random_article_owner.state_count(),
                  "ALDYakuAll consumer exceeds the authored Random Article state table");
            void* row_table = nullptr;
            check(melee_web_stadium_c1_random_article_state_row(
                      random_article, static_cast<std::uint32_t>(row), &row_table,
                      &script_rows_before[row]),
                  "Random Pokémon Article source state row is unavailable");
            if (!random_states) random_states = row_table;
            check(row_table == random_states,
                  "Random Pokémon Article state rows do not share one source table");
        }
        check(random_states != nullptr,
              "Stadium ALDYakuAll has no checked Random Article state consumers");

        const auto item_root = source_registry.root_offset;
        const auto color_root = itco_archive->pointer(item_root + 20, 8);
        if (!color_root)
            throw DatError("Original ItCo color-animation root is absent");
        const auto color_count = stadium_c1_item_owner::checked_color_row_count(
            *itco_archive, *color_root);
        DatColorAnimation color_owner(itco_archive, *color_root, color_count);
        void* const source_item = melee_web_item_public_data_decode(
            public_data_arena.reader(), item_root,
            registered_articles.articles(), MELEE_WEB_ITEM_REGISTRY_COUNT);
        MeleeWebStadiumC1ItemPublicDataView source_view{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_view) &&
                  source_view.common_data != nullptr &&
                  source_view.common_articles != nullptr &&
                  source_view.character_articles != nullptr &&
                  source_view.pokemon_articles != nullptr &&
                  source_view.bounce_data != nullptr,
              "Original ItCo public-data root did not retain all Article tables");
        check(source_view.character_articles ==
                  static_cast<const void*>(registered_articles.articles()),
              "ItCo public-data root did not borrow the checked character Article registry");

        const auto globals_before = stadium_item_runtime_globals();
        const auto source_color_before = source_view.color_rows;
        if (lifetime == 0) {
            const MeleeWebGameplayStats negative_stats_before =
                melee_web_gameplay_stats();
            const auto* const negative_seed_owner = seed_ptr;
            check(negative_seed_owner == seed_owner &&
                      *negative_seed_owner == seed_before,
                  "C1 item-state negative checks lost the retained RNG snapshot");
            check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
                  "C1 item-state negative checks found an item object");

            stadium_c1_item_owner::run_synthetic_negative_cases(scripts);

            check_stadium_item_runtime_globals(globals_before);
            MeleeWebStadiumC1ItemPublicDataView source_after_negatives{};
            check(melee_web_stadium_c1_item_public_data_view(
                      source_item, &source_after_negatives) &&
                      source_after_negatives.common_data == source_view.common_data &&
                      source_after_negatives.common_articles == source_view.common_articles &&
                      source_after_negatives.character_articles == source_view.character_articles &&
                      source_after_negatives.pokemon_articles == source_view.pokemon_articles &&
                      source_after_negatives.bounce_data == source_view.bounce_data &&
                      source_after_negatives.color_rows == source_color_before,
                  "Synthetic negatives changed the retained source color/public-data pointers");
            check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
                  "Synthetic negatives created an item object");
            for (std::size_t row = 1; row < scripts.size(); ++row) {
                if (!scripts[row]) continue;
                void* row_table = nullptr;
                void* script = nullptr;
                check(melee_web_stadium_c1_random_article_state_row(
                          random_article, static_cast<std::uint32_t>(row),
                          &row_table, &script) &&
                          row_table == random_states &&
                          script == script_rows_before[row],
                      "Synthetic negatives changed a retained Random Article state link");
            }
            const MeleeWebGameplayStats negative_stats_after =
                melee_web_gameplay_stats();
            check(negative_stats_after.generation == negative_stats_before.generation &&
                      negative_stats_after.ticks == negative_stats_before.ticks &&
                      negative_stats_after.generation == stats_before.generation &&
                      negative_stats_after.ticks == stats_before.ticks,
                  "Synthetic negatives advanced source ticks or changed generation");
            check(seed_ptr == negative_seed_owner &&
                      *negative_seed_owner == seed_before,
                  "Synthetic negatives changed retained source RNG ownership or value");
            check(itco_bytes == itco_before && grps_bytes == grps_before,
                  "Synthetic negatives changed immutable retained ItCo/GrPs bytes");
            std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>
                negative_save_after{};
            char negative_error[256]{};
            check(melee_web_menu_host_snapshot_card_data(
                      host, 0, negative_save_after.data(),
                      negative_save_after.size(), negative_error,
                      sizeof(negative_error)), negative_error);
            check(negative_save_after == save_before,
                  "Synthetic negatives changed the retained source save owner");
            check_stadium_selection_preserved(host, selected, baseline);
            check_stadium_preflight_stage_empty();
        }
        char error[256]{};
        StadiumItemRuntimeEndGuard runtime;
        runtime.runtime = melee_web_item_runtime_prepare_source(
            source_item, color_owner.table(), color_count,
            error, sizeof(error));
        check(runtime.runtime != nullptr, error);
        const auto globals_active = stadium_item_runtime_globals();
        MeleeWebStadiumC1ItemPublicDataView source_active{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_active) &&
                  globals_active.public_data == source_item &&
                  globals_active.common_articles == source_active.common_articles &&
                  globals_active.common_data == source_active.common_data &&
                  globals_active.pokemon_articles == source_active.pokemon_articles &&
                  globals_active.character_articles == source_active.character_articles &&
                  globals_active.character_articles ==
                      static_cast<const void*>(registered_articles.articles()) &&
                  ((void**) globals_active.character_articles)[random_index] ==
                      random_article &&
                  globals_active.bounce_data == source_active.bounce_data &&
                  globals_active.color_rows == source_active.color_rows &&
                  source_active.color_rows != source_color_before,
              "Prepared item-state globals do not reach the checked Random Article and color rows");
        for (std::size_t row = 1; row < scripts.size(); ++row) {
            if (!scripts[row]) continue;
            void* row_table = nullptr;
            void* script = nullptr;
            check(row < random_article_owner.state_count() &&
                      melee_web_stadium_c1_random_article_state_row(
                          random_article, static_cast<std::uint32_t>(row),
                          &row_table, &script) &&
                      row_table == random_states &&
                      script == script_rows_before[row],
                  "Item-state preflight attached a Stadium script before Ground_801C0800");
        }

        char competing_error[256]{};
        check(melee_web_item_runtime_prepare_source(
                  source_item, color_owner.table(), color_count,
                  competing_error, sizeof(competing_error)) == nullptr &&
                  std::string_view(competing_error).find(
                      "Item startup requires checked data") !=
                      std::string_view::npos,
              "A competing active item-state owner was not refused explicitly");
        const auto globals_competing = stadium_item_runtime_globals();
        check(globals_competing.character_articles ==
                  source_active.character_articles &&
                  ((void**) globals_competing.character_articles)[random_index] ==
                      random_article,
              "Competing item-state owner changed the active Random Article registry");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "C1 item-state preflight created an item object");

        runtime.end();
        check_stadium_item_runtime_globals(globals_before);
        MeleeWebStadiumC1ItemPublicDataView source_after{};
        check(melee_web_stadium_c1_item_public_data_view(source_item,
                                                         &source_after) &&
                  source_after.color_rows == source_color_before,
              "Item runtime teardown did not restore the source color pointer before owner destruction");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "Item runtime teardown left an item object");
    }

    const MeleeWebGameplayStats stats_after = melee_web_gameplay_stats();
    check(stats_after.generation == stats_before.generation &&
              stats_after.ticks == stats_before.ticks,
          "C1 item-state preflight advanced the source runtime or changed its generation");
    check(seed_ptr == seed_owner && *seed_owner == seed_before,
          "C1 item-state preflight changed retained source RNG ownership or value");
    check(grps_bytes == grps_before,
          "C1 item-state preflight changed immutable GrPs.usd input bytes");
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
    char error[256]{};
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, save_after.data(), save_after.size(), error,
              sizeof(error)), error);
    check(save_after == save_before,
          "C1 item-state preflight changed the retained source save owner");
    check_stadium_selection_preserved(host, selected, baseline);
    check_stadium_preflight_stage_empty();
}

void run_stadium_screen_roots_preflight(
    const melee_web::RuntimeFiles& files, MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before)
{
    using namespace melee_web;
    namespace screen = melee_web::test::stadium_screen;
    const auto& raw = files.at("GrPs.usd");
    const auto raw_before = raw;
    auto archive = std::make_shared<const DatArchive>(raw, DatExternalPolicy::ResolveNull);
    const DatStage map_metadata(*archive);
    auto map_contract = melee_web::test::stadium_contract_data(
        *archive, map_metadata);
    // ResolveNull clears validated external-link slots in the archive's owned
    // copy. Preserve that decoded baseline separately from immutable input.
    const std::vector<std::uint8_t> archive_before(archive->data().begin(), archive->data().end());
    const auto image_offset = screen::root(*archive, screen::image_name);
    const auto sis_offset = screen::root(*archive, screen::sis_name);
    check(image_offset == 35276 && archive->be16(image_offset+4) == 16 &&
              archive->be16(image_offset+6) == 16 && archive->be32(image_offset+8) == 0 &&
              sis_offset == 0x13ca80 &&
              archive->next_target_offset(sis_offset)-sis_offset == 88,
          "Screen roots differ from the retained C0 source contract");
    const auto globals_before = stadium_item_runtime_globals();
    const auto stats_before = melee_web_gameplay_stats();
    const auto* seed_owner = seed_ptr;
    check(seed_owner != nullptr, "Screen preflight lacks a retained RNG owner");
    const auto seed_before = *seed_owner;
    check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
          "Screen preflight found an existing item object");
    auto invariants = [&] {
        check_stadium_item_runtime_globals(globals_before);
        const auto stats = melee_web_gameplay_stats();
        check(stats.ticks == stats_before.ticks && stats.generation == stats_before.generation,
              "Screen descriptor checks changed source ticks/generation");
        check(seed_ptr == seed_owner && *seed_owner == seed_before,
              "Screen descriptor checks changed RNG owner/value");
        check(((HSD_GObj**)HSD_GObj_Entities)[9] == nullptr,
              "Screen descriptor checks created an item object");
        check(raw == raw_before,
              "Screen descriptor checks changed raw GrPs bytes");
        check(std::equal(archive->data().begin(), archive->data().end(), archive_before.begin()),
              "Screen descriptor checks changed the decoded GrPs owner");
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        char error[256]{};
        check(melee_web_menu_host_snapshot_card_data(host, 0, save_after.data(), save_after.size(), error, sizeof(error)), error);
        check(save_after == save_before, "Screen descriptor checks changed retained save");
        check_stadium_selection_preserved(host, selected, baseline);
        check_stadium_preflight_stage_empty();
    };
    invariants();
    melee_web::test::stadium_buffer::run_original_constructor_lifetimes(
        invariants);
    invariants();
    screen::synthetic_checks(invariants);
    invariants();
    for (unsigned lifetime=0; lifetime<2; ++lifetime) {
        {
            DatNativeMap map(archive, map_contract.view());
            DatSis sis(archive, screen::sis_name);
            check(sis.entry_count() == 22, "Screen SIS changed authored 22-slot count");
            {
                DatNativeMap foreign(archive, map_contract.view());
                screen::synthetic::rejects([&] {
                    screen::identity(map, 1, foreign.image_descriptor(image_offset));
                }, "unique map descriptor");
            }
            invariants();
            screen::catalog_checks(*archive, map, sis, 1, image_offset);
            invariants();
            auto* image = static_cast<HSD_ImageDesc*>(map.image_descriptor(image_offset));
            screen::identity(map, 1, image);
            const MeleeWebArchiveSymbol symbols[] = {
                {"GrPs.usd", screen::image_name, image},
                {"GrPs.usd", screen::sis_name, sis.descriptor()},
            };
            screen::Catalog catalog("GrPs.usd", symbols, 2);
            check(stadium_screen_source_public(catalog.handle, screen::image_name) == image &&
                      stadium_screen_source_public(catalog.handle, screen::sis_name) == sis.descriptor(),
                  "Live consumer catalog lost canonical IMAGE/SIS owners");
            DatNativeMap foreign(archive, map_contract.view());
            auto* foreign_image = static_cast<HSD_ImageDesc*>(foreign.image_descriptor(image_offset));
            auto* descriptor = static_cast<HSD_Joint*>(
                stadium_screen_map_entry_joint(map.map_head(), 1));
            screen::live_source_consumer_lifetime(descriptor, image, foreign_image, invariants);
            invariants();
        }
        invariants();
    }
    std::cout << "C1 screen-root preflight preserved canonical IMAGE, writable SIS and two owner/catalog lifetimes; no stage entry or ticks\n";
    std::cout << "C1 live Stadium IMAGE source hit/miss/remove passed twice; no stage entry or ticks\n";
    std::cout << "C1 original Stadium auxiliary IMAGE constructor/remove passed twice without callback dispatch or stage entry\n";
}

void run_stadium_c1_context_preflight(
    const melee_web::RuntimeFiles& menu_files,
    MeleeWebMenuHost*& host,
    std::unique_ptr<melee_web::GameplayMenuWorld>& world,
    const MeleeWebMenuMatchSelection& selected,
    const std::vector<std::string>& selected_names,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    bool perform_e8_request,
    bool perform_item_state_preflight,
    bool perform_screen_roots_preflight,
    bool perform_ground_map1_owner,
    bool perform_on_init,
    const MeleeWebRetiredSisLease* retired_sis,
    TransitionTrace& trace,
    bool full_world_lifecycle = false,
    bool ready_session = false,
    bool pad_leave_probe = false,
    bool source_text_lifetime_probe = false,
    bool ready_text_membership_probe = false)
{
    char error[256]{};
    const int previous_mode = gm_GetCurrentGameMode();
    const int previous_language = lbLang_GetLanguageSetting();
    const int previous_saved_language = lbLang_GetSavedLanguage();
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_before{};
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> baseline{};
    check(selected.start.rules.stkind == St_Kind_PStadium &&
              selected.save_profile_present,
          "C1 context preflight requires source StKind 3 and its retained save owner");
    check(melee_web_menu_host_snapshot_card_data(
              host, 1, baseline.data(), baseline.size(), error,
              sizeof(error)), error);
    check_stadium_selection_preserved(host, selected, baseline);
    StadiumSelectionRngWitness selection_rng{
        seed_ptr, selected.random_seed, selected.random_seed};
    check_stadium_rng_witness(selection_rng, selected, "reopened-context-start");
    check(melee_web_menu_host_snapshot_card_data(
              host, 0, save_before.data(), save_before.size(), error,
              sizeof(error)), error);
    check(melee_web_source_files_active(),
          "C1 source handoff lost its menu files");
    check(_Toy_sbss_804D6ED0 == nullptr,
          "C1 source handoff retained a Toy archive alias past MenuWorld close");
    check((Toy_804A284C[3] & 4) != 0,
          "C1 source handoff lost the retained Toy category baseline");
    check_stadium_preflight_stage_empty();
    const MeleeWebPadState* ready_input=nullptr;
    if(ready_session||pad_leave_probe||source_text_lifetime_probe||
       ready_text_membership_probe){
        ready_input=melee_web_menu_host_input(host);
        check(ready_input!=nullptr,
              "Closed Stadium SSS did not retain its decoded source PAD input");
    }
    world->verify_immutable_archives();
    world->close();
    world.reset();

    if(ready_session||pad_leave_probe||ready_text_membership_probe){
        const MeleeWebPadState* after_close_input=melee_web_menu_host_input(host);
        std::fprintf(stderr,"C1_PAD_CLOSE retained_equal=%d host_phase=%d source_scene=%d world_exists=%d input=%p\n",
            after_close_input==ready_input,melee_web_menu_host_phase(host),
            melee_web_menu_host_source_scene(host),melee_web_gameplay_world_exists(),
            static_cast<const void*>(after_close_input));
        std::fflush(stderr);
        check(after_close_input==ready_input,
              "MenuWorld close replaced the host-retained source PAD input");
    }

    if(pad_leave_probe){
        check(!melee_web_source_files_active()&&_Toy_sbss_804D6ED0==nullptr,
              "PAD leave probe did not close the selected SSS world");
        check(melee_web_menu_host_phase(host)==MELEE_WEB_MENU_READY&&
                  melee_web_menu_host_source_scene(host)==0&&
                  melee_web_menu_host_input(host)==ready_input,
              "PAD leave probe lost the closed host's retained source input");
        check(!melee_web_gameplay_world_exists(),
              "PAD leave probe unexpectedly started a match world");
        check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
        host=nullptr;
        std::cout<<"Stadium source PAD leave probe captured pre/post menu-leave wire state and retained host input; no MatchSession or Ready construction\n";
        return;
    }

    bool vs_mode_owned = false;
    bool language_scope_owned = false;
    bool cleanup_complete = false;
    melee_web::RuntimeFiles reopened_files;
    auto restore_context = [&]() {
        if (language_scope_owned) {
            lbLang_SetLanguageSetting(previous_language);
            lbLang_SetSavedLanguage(previous_saved_language);
            language_scope_owned = false;
        }
        if (vs_mode_owned) {
            check(melee_web_vs_mode_end(),
                  "C1 context preflight lost its source VS mode lease");
            vs_mode_owned = false;
        }
        check(gm_GetCurrentGameMode() == previous_mode,
              "C1 context preflight did not restore the source game mode");
        check(lbLang_GetLanguageSetting() == previous_language &&
                  lbLang_GetSavedLanguage() == previous_saved_language,
              "C1 context preflight did not restore both source language settings");
    };
    auto cleanup = [&]() {
        if (world) {
            world->close_prepared();
            world.reset();
        }
        check(!melee_web_source_files_active(),
              "C1 context preflight left a RuntimeFiles scope active");
        check(_Toy_sbss_804D6ED0 == nullptr,
              "C1 context preflight left Toy aliases past MenuWorld close");
        restore_context();
        if (host) {
            check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
            host = nullptr;
        }
        cleanup_complete = true;
    };

    try {
        check(!melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 == nullptr,
              "Closing the selected SSS world did not release its file and Toy owners");
        check((Toy_804A284C[3] & 4) != 0,
              "Closing MenuWorld erased the retained Toy category baseline");
        reopened_files = exact_stadium_runtime_union(
            menu_files, selected_names, menu_dir, game_dir);
        check(reopened_files.contains("GrPs.usd"),
              "Exact C1 RuntimeFiles union omitted GrPs.usd");

        if(source_text_lifetime_probe){
            check(perform_on_init && retired_sis,
                  "Stadium text lifetime probe requires original menu leave/SIS retirement evidence");
            check(retired_sis->retirement_verified && retired_sis->prior.heap &&
                  retired_sis->prior.world_generation && retired_sis->prior.allocation_generation &&
                  retired_sis->prior.source_epoch &&
                  retired_sis->requested_bytes==MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES,
                  "Stadium text lifetime probe requires already verified menu SIS retirement evidence");
            check(!melee_web_gameplay_world_exists(),
                  "Stadium text lifetime probe would double-own a source world");
            check(ready_input!=nullptr,
                  "Stadium text lifetime probe lost the closed host's retained source PAD input");
            const auto parent_seed_owner=seed_ptr;
            check(parent_seed_owner && *parent_seed_owner==selected.random_seed,
                  "Stadium text lifetime probe lost the actual host selected seed");
            melee_web::RuntimeArchiveCache cache(reopened_files);
            std::unique_ptr<melee_web::GameplayMatchSession> match;
            HSD_SisLib_C1TextProbeSet(1);
            std::fprintf(stderr,"STADIUM_SOURCE_TEXT_LIFETIME phase=probe-armed\n");
            std::fflush(stderr);
            try{
                match=std::make_unique<melee_web::GameplayMatchSession>(
                    reopened_files,selected,cache,melee_web::GameplayMatchConstruction::Deferred,
                    *ready_input,melee_web::GameplayMatchDiagnostic::StadiumReady);
                while(!match->advance_construction()){}
                const auto constructed=melee_web_gameplay_stats();
                check(constructed.ticks==0,
                      "Stadium text lifetime probe unexpectedly advanced a source match tick");
                std::fprintf(stderr,
                    "STADIUM_SOURCE_TEXT_LIFETIME phase=construction-complete world_exists=%d ticks=%llu match_retained=%d\n",
                    melee_web_gameplay_world_exists(),
                    static_cast<unsigned long long>(constructed.ticks),int(bool(match)));
                std::fflush(stderr);
                // Run normal Session.close at the source boundary, before any match tick.
                // A refusal remains a failed lifecycle, not treated as success.
                match->close();
                match.reset();
                HSD_SisLib_C1TextProbeSet(0);
                const auto closed=melee_web_gameplay_stats();
                std::fprintf(stderr,
                    "STADIUM_SOURCE_TEXT_LIFETIME phase=normal-close-returned world_exists=%d ticks=%llu\n",
                    melee_web_gameplay_world_exists(),
                    static_cast<unsigned long long>(closed.ticks));
                std::fflush(stderr);
                check(closed.ticks==0,
                      "Stadium text lifetime probe changed source tick count during normal close");
                check(seed_ptr==parent_seed_owner && *parent_seed_owner==selected.random_seed,
                      "Stadium text lifetime probe failed to restore exact parent host RNG owner/value");
                cleanup();
                std::cout<<"Stadium zero-tick source text-owner observation returned from normal Session.close; lifecycle acceptance remains unproven\n";
                return;
            }catch(const std::exception& first){
                const auto failed=melee_web_gameplay_stats();
                std::fprintf(stderr,
                    "STADIUM_SOURCE_TEXT_LIFETIME_FIRST_FAILURE diagnostic=%s world_exists=%d ticks=%llu match_retained=%d lifecycle=failed\n",
                    first.what(),melee_web_gameplay_world_exists(),
                    static_cast<unsigned long long>(failed.ticks),int(bool(match)));
                std::fflush(stderr);std::cout.flush();std::_Exit(1);
            }
        }

        if(ready_session||ready_text_membership_probe){
            check(perform_on_init && retired_sis,
                  "Stadium Ready requires the original menu leave/SIS retirement boundary");
            // Menu leave already verified this one-shot token while its source
            // allocation registry was live. MenuWorld.close has since retired
            // that world; consume the immutable evidence without re-verifying
            // or reading the old allocation through a replaced registry.
            std::fprintf(stderr,"C1_SIS_READY_EVIDENCE verified=%d prior_heap=%p prior_world=%llu prior_source_heap=%d prior_allocation=%llu prior_epoch=%llu requested=%zu expected_requested=%u world_exists=%d\n",
                retired_sis->retirement_verified,retired_sis->prior.heap,
                (unsigned long long)retired_sis->prior.world_generation,retired_sis->prior.source_heap_handle,
                (unsigned long long)retired_sis->prior.allocation_generation,
                (unsigned long long)retired_sis->prior.source_epoch,retired_sis->requested_bytes,
                MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES,melee_web_gameplay_world_exists());
            std::fflush(stderr);
            check(retired_sis->retirement_verified && retired_sis->prior.heap &&
                  retired_sis->prior.world_generation && retired_sis->prior.allocation_generation &&
                  retired_sis->prior.source_epoch &&
                  retired_sis->requested_bytes==MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES,
                  "Stadium Ready requires the already verified original menu SIS retirement evidence");
            check(!melee_web_gameplay_world_exists(),"Stadium Ready would double-own a source world");
            const auto parent_seed_owner=seed_ptr;
            check(parent_seed_owner && *parent_seed_owner==selected.random_seed,
                  "Stadium Ready lost the actual host selected seed");
            const auto session_before=melee_web_gameplay_allocation();
            auto observe=[](const char* phase){
                const auto stats=melee_web_gameplay_stats();
                const auto allocation=melee_web_gameplay_allocation();
                MeleeWebSourceMemoryContext memory{};
                const auto status=melee_web_source_memory_context_read(&memory);
                std::fprintf(stderr,"STADIUM_READY_SESSION phase=%s world_exists=%d world=%llu ticks=%llu heap=%d objects=%u processes=%u memory_status=%d memory_world=%llu memory_heap=%d watermark=%llu arena_identity=%llu arena_generation=%llu arena_bytes=%llu current_object=%p current_proc=%p gx=%p\n",
                    phase,melee_web_gameplay_world_exists(),(unsigned long long)stats.generation,
                    (unsigned long long)stats.ticks,stats.heap_free_bytes,stats.objects,stats.processes,
                    int(status),(unsigned long long)memory.world_generation,memory.source_heap_handle,
                    (unsigned long long)memory.allocation_generation_watermark,
                    (unsigned long long)allocation.identity,(unsigned long long)allocation.generation,
                    (unsigned long long)allocation.bytes,(void*)HSD_GObj_804D781C,
                    (void*)HSD_GObj_804D7838,(void*)HSD_GObj_804D7814);
                std::fflush(stderr);
            };
            check(ready_input!=nullptr,
                  "Stadium Ready lost the closed host's retained source PAD input");
            melee_web::RuntimeArchiveCache cache(reopened_files);
            std::unique_ptr<melee_web::GameplayMatchSession> match;
            try{
                observe("before-construction");
                if(ready_text_membership_probe){
                    HSD_SisLib_C1TextProbeSet(1);
                    std::fprintf(stderr,
                        "STADIUM_READY_TEXT_MEMBERSHIP phase=probe-armed tick_cap=600 draw=0\n");
                    std::fflush(stderr);
                }
                match=std::make_unique<melee_web::GameplayMatchSession>(
                    reopened_files,selected,cache,melee_web::GameplayMatchConstruction::Deferred,
                    *ready_input,melee_web::GameplayMatchDiagnostic::StadiumReady);
                while(!match->advance_construction()){}
                observe("construction-complete-before-source-ticks");
                if(ready_text_membership_probe){
                    MeleeWebRetiredSisLease text_snapshot{};
                    const auto construction=melee_web_gameplay_stats();
                    check(construction.ticks==0,
                          "Ready text membership snapshot was not taken at zero source ticks");
                    check(melee_web_diagnostic_sis_capture(
                              &text_snapshot,error,sizeof(error)),error);
                    check(text_snapshot.retirement_verified==0 &&
                              text_snapshot.requested_bytes==MELEE_WEB_DIAGNOSTIC_SIS_HEAP_BYTES &&
                              text_snapshot.prior.world_generation==construction.generation,
                          "Ready text membership snapshot lacks the current exact live SIS allocation");
                    check(melee_web_stadium_text_membership_snapshot_begin(
                              &text_snapshot.prior,error,sizeof(error)),error);
                    std::fprintf(stderr,
                        "STADIUM_READY_TEXT_MEMBERSHIP phase=snapshot-captured tick=0 heap=%p world=%llu source_heap=%d allocation=%llu epoch=%llu requested_bytes=%zu retirement_verified=%d\n",
                        text_snapshot.prior.heap,
                        static_cast<unsigned long long>(text_snapshot.prior.world_generation),
                        text_snapshot.prior.source_heap_handle,
                        static_cast<unsigned long long>(text_snapshot.prior.allocation_generation),
                        static_cast<unsigned long long>(text_snapshot.prior.source_epoch),
                        text_snapshot.requested_bytes,text_snapshot.retirement_verified);
                    std::fflush(stderr);
                }
                PADStatus pads[4]{};pads[2].err=pads[3].err=PAD_ERR_NO_CONTROLLER;
                float pcm[1068]{};unsigned audio_phase=0;
                for(unsigned tick=0;tick<600 && !match->ready();++tick){
                    match->tick(pads);
                    audio_phase+=32000;const unsigned samples=audio_phase/60;audio_phase%=60;
                    check(melee_web_audio_render(match->audio(),pcm,samples,error,sizeof(error)),error);
                }
                observe("first-ready-before-close");
                check(match->ready(),"Stadium did not reach original source Ready within 600 ticks");
                check(match->fighter_kind(0)==FTKIND_MARIO && match->fighter_kind(1)==FTKIND_MARIO,
                      "Stadium Ready lost actual selected Human Mario fighter identities");
                match->close();match.reset();
                if(ready_text_membership_probe){
                    HSD_SisLib_C1TextProbeSet(0);
                    melee_web_stadium_text_membership_snapshot_end();
                }
                observe("checked-session-close");
                const auto closed=melee_web_gameplay_stats();
                MeleeWebSourceMemoryContext closed_memory{};
                const auto closed_status=melee_web_source_memory_context_read(&closed_memory);
                const auto allocation=melee_web_gameplay_allocation();
                check(!melee_web_gameplay_world_exists() && !closed.generation && !closed.objects && !closed.processes &&
                      closed_status==MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE &&
                      HSD_GetHeap()==-1 && allocation.identity==session_before.identity &&
                      allocation.generation==session_before.generation && allocation.bytes==session_before.bytes,
                      "Stadium Ready did not retire its complete owned source world");
                check(seed_ptr==parent_seed_owner && *parent_seed_owner==selected.random_seed,
                      "Stadium Ready failed to restore the exact parent host RNG owner");
                check_stadium_selection_preserved(host,selected,baseline);
                cleanup();
                std::cout<<"Stadium original source-session Ready and checked owned-world retirement passed; one lifetime, no draw/post-GO idle/C3 claim\n";
                return;
            }catch(const std::exception& first){
                if(ready_text_membership_probe){
                    HSD_SisLib_C1TextProbeSet(0);
                    melee_web_stadium_text_membership_snapshot_end();
                }
                observe("first-failure-retained");
                std::fprintf(stderr,"STADIUM_READY_SESSION_FIRST_FAILURE diagnostic=%s match_retained=%d pre_OnStart_close_unsupported=1\n",first.what(),int(bool(match)));
                std::fflush(stderr);std::cout.flush();std::_Exit(1);
            }
        }

        check(melee_web_vs_mode_begin(),
              "C1 context preflight could not acquire the source VS mode lease");
        vs_mode_owned = true;
        language_scope_owned = true;
        lbLang_SetLanguageSetting(LANG_US);
        lbLang_SetSavedLanguage(LANG_US);
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode(),
              "Reopened C1 context is not source GM_VS non-1P mode");
        check(lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "Reopened C1 context did not set both source language scopes to US");
        check_stadium_selection_preserved(host, selected, baseline);

        if (full_world_lifecycle) {
            check(perform_on_init && retired_sis,
                  "Full-world Stadium continuation requires the existing real OnInit fixture");
            const auto session_before = melee_web_gameplay_allocation();
            const auto* const parent_seed_owner = seed_ptr;
            check(parent_seed_owner && *parent_seed_owner == selected.random_seed,
                  "Full-world continuation lost its original selected seed owner");
            const uint32_t parent_seed = *parent_seed_owner;
            MeleeWebGameplayStats fresh_baseline{};
            uint64_t previous_world = 0;
            MeleeWebRetiredSisLease previous_sis = *retired_sis;
            for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
                world = std::make_unique<melee_web::GameplayMenuWorld>(reopened_files);
                check(melee_web_native_world_enable(error, sizeof(error)), error);
                const auto fresh = melee_web_gameplay_stats();
                MeleeWebSourceMemoryContext fresh_memory{};
                const auto fresh_status = melee_web_source_memory_context_read(&fresh_memory);
                std::fprintf(stderr,
                    "C3_FRESH lifetime=%u world=%llu previous_world=%llu ticks=%llu heap=%d objects=%u processes=%u status=%d source_world=%llu source_heap=%d watermark=%llu baseline_heap=%d baseline_objects=%u baseline_processes=%u gobj_used=%u gobj_free=%u proc_used=%u proc_free=%u\n",
                    lifetime, static_cast<unsigned long long>(fresh.generation),
                    static_cast<unsigned long long>(previous_world), static_cast<unsigned long long>(fresh.ticks),
                    fresh.heap_free_bytes, fresh.objects, fresh.processes, int(fresh_status),
                    static_cast<unsigned long long>(fresh_memory.world_generation), fresh_memory.source_heap_handle,
                    static_cast<unsigned long long>(fresh_memory.allocation_generation_watermark),
                    fresh_baseline.heap_free_bytes, fresh_baseline.objects, fresh_baseline.processes,
                    gobj_alloc_data.used, gobj_alloc_data.free, gobjproc_alloc_data.used, gobjproc_alloc_data.free);
                std::fflush(stderr);
                check(fresh_status == MELEE_WEB_SOURCE_MEMORY_READ_OK &&
                          fresh.generation == fresh_memory.world_generation &&
                          fresh.generation > previous_world && fresh.ticks == 0 &&
                          melee_web_source_memory_healthy(),
                      "Fresh owned Stadium world did not reset source generation/ticks/heap");
                if (lifetime == 0) fresh_baseline = fresh;
                else check(fresh.heap_free_bytes == fresh_baseline.heap_free_bytes &&
                               fresh.objects == fresh_baseline.objects &&
                               fresh.processes == fresh_baseline.processes,
                           "Recreated Stadium world differs from its exact fresh heap/object/process baseline");
                check_stadium_preflight_stage_empty();
                check(HSD_ObjAllocGetUsing(&gobj_alloc_data) == 0 &&
                          HSD_ObjAllocGetUsing(&gobjproc_alloc_data) == 0,
                      "Fresh prepared world contains source objects/processes in used pools");
                const auto fresh_roots = melee_web::test::stadium_screen::runtime_roots_snapshot();
                const auto fresh_classes = melee_web::test::stadium_screen::live_class_counts();
                const auto fresh_pools = melee_web::test::stadium_screen::live_pool_counts();
                Toy_803124BC();
                check(_Toy_sbss_804D6ED0 && (Toy_804A284C[3] & 4) != 0,
                      "Full-world continuation lost original source Toy aliases");
                std::array<MeleeWebPlayerSettings, MELEE_WEB_MATCH_MAX_PLAYERS> players{};
                unsigned count = 0;
                for (unsigned slot = 0; slot < MELEE_WEB_MENU_MAX_PLAYERS; ++slot) {
                    const auto& source = selected.start.players[slot];
                    if (source.slot_type == Gm_PKind_NA) continue;
                    const auto& compatibility = selected.players[slot];
                    const auto* fighter = melee_web_fighter_content(source.ckind);
                    const unsigned port = source.slot ? source.slot - 1u : slot;
                    check(fighter && source.slot_type == Gm_PKind_Human && port == slot &&
                              compatibility.controller == port &&
                              compatibility.stocks == source.stocks &&
                              compatibility.costume == source.color &&
                              compatibility.sub_color == source.sub_color && count < players.size(),
                          "Stadium continuation differs from selected original human player identities");
                    players[count++] = {slot, port, compatibility.stocks, {0, 0, 0}, 1.0f,
                                        compatibility.costume, compatibility.sub_color, static_cast<uint32_t>(fighter->fighter_kind)};
                }
                check(count == selected.player_count,
                      "Stadium continuation changed original active source player count");
                const auto active_match_copied_selection = check_stadium_selection_preserved(
                    host, selected, baseline, &selection_rng);
                std::fprintf(stderr, "C3_HOST_SELECTION_EXPORTED boundary=before-match-begin lifetime=%u owner=%p seed=%u\n",
                    lifetime, static_cast<void*>(seed_ptr), *seed_ptr);
                std::fflush(stderr);
                MeleeWebMatchContext* match = melee_web_match_begin_players(
                    players.data(), count, 70, selected.random_seed, nullptr,
                    error, sizeof(error));
                check(match != nullptr, error);
                try {
                    StadiumSelectionRngWitness match_rng{
                        seed_ptr, selected.random_seed, selected.random_seed};
                    check_stadium_rng_witness(match_rng, selected, "owned-camera-before-stage");
                    check(melee_web_match_camera_available(match, error, sizeof(error)), error);
                    MeleeWebRetiredSisLease next_sis{};
                    run_stadium_e8_request(reopened_files, host, world.get(), selected,
                        baseline, save_before, false, true, &previous_sis, match_rng, trace, match, &next_sis,
                        &active_match_copied_selection);
                    check(next_sis.retirement_verified && next_sis.prior.world_generation == fresh.generation,
                          "Stadium world did not verify its own next SIS retirement lease before shutdown");
                    previous_sis = next_sis;
                    check(melee_web_match_end(match, error, sizeof(error)), error);
                    match = nullptr;
                    check(seed_ptr == parent_seed_owner && *seed_ptr == parent_seed,
                          "Stadium MatchContext did not restore the original parent seed owner/value");
                    check_stadium_selection_preserved(host, selected, baseline, &selection_rng);
                    std::fprintf(stderr, "C3_HOST_SELECTION_EXPORTED boundary=after-match-end lifetime=%u owner=%p seed=%u\n",
                        lifetime, static_cast<void*>(seed_ptr), *seed_ptr);
                    std::fflush(stderr);
                } catch (...) {
                    // Refusal retains this world's partial source owners. Do not
                    // allow a raw heap sweep to disguise failed checked retirement.
                    try { throw; }
                    catch (const std::exception& first) {
                        std::cerr << "C3_WORLD_RETAINED lifetime=" << lifetime
                                  << " world=" << fresh.generation << " match=" << match
                                  << " first_exception=" << first.what()
                                  << " error_buffer=" << error << '\n';
                    } catch (...) { std::cerr << "C3_WORLD_RETAINED first_exception=unknown\n"; }
                    std::cerr.flush(); std::cout.flush(); std::_Exit(1);
                }
                const auto retired = melee_web_gameplay_stats();
                std::cout << "{\"probe\":\"stadium-owned-world-before-close\",\"lifetime\":" << lifetime
                          << ",\"world\":" << fresh.generation << ",\"fresh_heap\":" << fresh.heap_free_bytes
                          << ",\"retired_heap\":" << retired.heap_free_bytes
                          << ",\"objects\":" << retired.objects << ",\"processes\":" << retired.processes
                          << ",\"ticks\":" << retired.ticks << "}\n";
                std::cout.flush();
                const auto retired_roots = melee_web::test::stadium_screen::runtime_roots_snapshot();
                const auto retired_classes = melee_web::test::stadium_screen::live_class_counts();
                const auto retired_pools = melee_web::test::stadium_screen::live_pool_counts();
                std::cerr << "C3_PREWORLD lifetime=" << lifetime << " gobj_used=" << gobj_alloc_data.used
                          << " proc_used=" << gobjproc_alloc_data.used
                          << " before_root_bytes=" << fresh_roots.size() << " after_root_bytes=" << retired_roots.size()
                          << " roots_equal=" << (retired_roots == fresh_roots) << '\n';
                for (size_t i = 0; i < std::min(fresh_roots.size(), retired_roots.size()); ++i)
                    if (fresh_roots[i] != retired_roots[i])
                        std::cerr << "C3_PREWORLD_ROOT offset=" << i << " before=" << unsigned(fresh_roots[i])
                                  << " after=" << unsigned(retired_roots[i]) << '\n';
                for (const auto& [identity, expected] : fresh_classes) {
                    const auto found = retired_classes.find(identity);
                    std::cerr << "C3_PREWORLD_CLASS identity=" << static_cast<void*>(identity)
                              << " before=" << expected << " after="
                              << (found == retired_classes.end() ? 0 : found->second) << '\n';
                }
                for (const auto& [identity, actual] : retired_classes)
                    if (!fresh_classes.contains(identity))
                        std::cerr << "C3_PREWORLD_CLASS identity=" << static_cast<void*>(identity)
                                  << " before=0 after=" << actual << '\n';
                for (size_t i = 0; i < retired_pools.size(); ++i)
                    std::cerr << "C3_PREWORLD_POOL index=" << i << " before=" << fresh_pools[i]
                              << " after=" << retired_pools[i] << '\n';
                std::cerr.flush();
                check(retired.generation == fresh.generation && retired.ticks == fresh.ticks &&
                          retired.objects == fresh.objects && retired.processes == fresh.processes &&
                          retired_roots == fresh_roots && retired_classes == fresh_classes &&
                          retired_pools == fresh_pools &&
                          melee_web_source_memory_healthy(),
                      "Stadium checked component retirement left source owners before whole-world close");
                world->verify_immutable_archives();
                world->close_prepared();
                world.reset();
                MeleeWebSourceMemoryContext inactive{};
                const auto session_after = melee_web_gameplay_allocation();
                const auto inactive_status = melee_web_source_memory_context_read(&inactive);
                const auto inactive_stats = melee_web_gameplay_stats();
                std::fprintf(stderr,
                    "C3_WORLD_CLOSED lifetime=%u retired_world=%llu world_exists=%d generation=%llu hsd_heap=%d registry=%p status=%d source_world=%llu source_heap=%d objects=%u processes=%u session_before=%llu session_after=%llu session_generation_before=%llu session_generation_after=%llu session_bytes_before=%llu session_bytes_after=%llu files_active=%d toy=%p\n",
                    lifetime, static_cast<unsigned long long>(fresh.generation), melee_web_gameplay_world_exists(),
                    static_cast<unsigned long long>(melee_web_gameplay_generation()), HSD_GetHeap(),
                    HSD_GObj_Entities, int(inactive_status), static_cast<unsigned long long>(inactive.world_generation),
                    inactive.source_heap_handle, inactive_stats.objects, inactive_stats.processes,
                    static_cast<unsigned long long>(session_before.identity), static_cast<unsigned long long>(session_after.identity),
                    static_cast<unsigned long long>(session_before.generation), static_cast<unsigned long long>(session_after.generation),
                    static_cast<unsigned long long>(session_before.bytes), static_cast<unsigned long long>(session_after.bytes),
                    melee_web_source_files_active(), static_cast<void*>(_Toy_sbss_804D6ED0));
                std::fflush(stderr);
                check(!melee_web_gameplay_world_exists() &&
                          melee_web_gameplay_generation() == 0 && HSD_GObj_Entities == nullptr &&
                          HSD_GetHeap() == -1 && !melee_web_gameplay_stats().objects &&
                          !melee_web_gameplay_stats().processes &&
                          inactive_status == MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE &&
                          !melee_web_source_files_active() && _Toy_sbss_804D6ED0 == nullptr &&
                          session_after.identity == session_before.identity &&
                          session_after.generation == session_before.generation &&
                          session_after.bytes == session_before.bytes,
                      "Owned MenuWorld close did not retire the complete SDK world and preserve its session arena");
                // HSD cache heads may contain numeric addresses into the retired
                // heap here. Do not traverse them; fresh startup must reset them.
                std::cout << "{\"probe\":\"stadium-owned-world-closed\",\"lifetime\":" << lifetime
                          << ",\"retired_world\":" << fresh.generation
                          << ",\"session_identity\":" << session_after.identity
                          << ",\"inactive\":true}\n";
                previous_world = fresh.generation;
            }
            restore_context();
            check_stadium_selection_preserved(host, selected, baseline, &selection_rng);
            std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> final_save{};
            check(melee_web_menu_host_snapshot_card_data(host, 0, final_save.data(),
                      final_save.size(), error, sizeof(error)), error);
            check(final_save == save_before, "Full-world Stadium continuation changed source save bytes");
            cleanup();
            check(cleanup_complete && !host && !world,
                  "Full-world Stadium continuation retained host or world ownership");
            return;
        }

        world = std::make_unique<melee_web::GameplayMenuWorld>(reopened_files);
        check(melee_web_source_files_active(),
              "Reopened MenuWorld did not activate its exact RuntimeFiles union");
        check(melee_web_menu_host_source_scene(host) == 0 &&
                  melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
              "Reopened MenuWorld entered a source menu scene");
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode(),
              "Reopened MenuWorld did not preserve the explicit source VS context");
        check(lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "Reopened MenuWorld changed one of the scoped US language settings");
        check(_Toy_sbss_804D6ED0 == nullptr,
              "Fresh MenuWorld unexpectedly retained Toy aliases from the closed world");
        check_stadium_preflight_stage_empty();

        const std::string grps_name = lbFileGetFullName("GrPs");
        check(grps_name == "GrPs.usd",
              "US C1 source resolution did not select the exact GrPs.usd filename");
        const auto grps = reopened_files.find(grps_name);
        check(grps != reopened_files.end() && !grps->second.empty(),
              "Exact C1 RuntimeFiles union has no GrPs.usd bytes");
        size_t resolved_size = 0;
        size_t root_path_size = 0;
        check(melee_web_source_file_size(grps_name.c_str(), &resolved_size) &&
                  resolved_size == grps->second.size() &&
                  lbFileGetSize("GrPs") == resolved_size,
              "Original lbFile GrPs resolution changed the exact RuntimeFiles size");
        check(melee_web_source_file_size("/GrPs.usd", &root_path_size) &&
                  root_path_size == resolved_size,
              "Retail root-path GrPs.usd resolution changed the exact RuntimeFiles size");
        const int grps_entry = melee_web_source_file_entry(grps_name.c_str());
        check(grps_entry > 0 && melee_web_source_file_entry_owned(grps_entry),
              "Reopened source file owner did not retain the GrPs.usd DVD entry");

        check((Toy_804A284C[3] & 4) != 0,
              "C1 preflight lost the menu host's existing Toy category baseline");
        Toy_803124BC();
        check(_Toy_sbss_804D6ED0 != nullptr && (Toy_804A284C[3] & 4) != 0,
              "Reopened source files did not restore Toy aliases over the retained baseline");
        const std::string toy_name = lbFileGetFullName("TyDatai");
        check(toy_name == "TyDatai.usd" &&
                  melee_web_source_file_size(toy_name.c_str(), &resolved_size) &&
                  resolved_size == reopened_files.at(toy_name).size(),
              "US Toy alias did not resolve through the reopened RuntimeFiles owner");
        check_stadium_selection_preserved(host, selected, baseline);
        if (perform_item_state_preflight) {
            run_stadium_c1_item_state_preflight(
                reopened_files, host, selected, baseline, save_before);
        }
        if (perform_screen_roots_preflight) {
            run_stadium_screen_roots_preflight(reopened_files, host, selected, baseline, save_before);
        }
        world->verify_immutable_archives();
        check_stadium_preflight_stage_empty();

        if (perform_e8_request || perform_ground_map1_owner || perform_on_init) {
            run_stadium_e8_request(reopened_files, host, world.get(), selected,
                                   baseline, save_before,
                                   perform_ground_map1_owner, perform_on_init,
                                   retired_sis, selection_rng, trace);
            check_stadium_preflight_stage_empty();
        }

        world->close_prepared();
        world.reset();
        check(!melee_web_source_files_active() &&
                  _Toy_sbss_804D6ED0 == nullptr,
              "Prepared MenuWorld teardown retained source-file or Toy aliases");
        restore_context();
        std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> save_after{};
        check(melee_web_menu_host_snapshot_card_data(
                  host, 0, save_after.data(), save_after.size(), error,
                  sizeof(error)), error);
        check(save_after == save_before,
              "C1 reopened-context preflight changed the live source save owner");
        check_stadium_selection_preserved(host, selected, baseline, &selection_rng);
        check((Toy_804A284C[3] & 4) != 0,
              "C1 context teardown changed the retained Toy category baseline");
        cleanup();
        check(host == nullptr && !melee_web_source_files_active(),
              "C1 context preflight did not release host and source-file owners");
        if (perform_ground_map1_owner) {
            std::cout << "C1 reopened-context lifecycle and one E8 typed request, "
                         "plus one isolated Ground map1 lifetime passed; "
                         "no rendered stage entry or source menu entry\n";
        } else if (perform_on_init && !full_world_lifecycle) {
            std::cout << "C1 reopened-context lifecycle and one source-ordered Stadium OnInit lifetime passed; "
                         "returned before camera/OnStart and rendered entry\n";
        } else if (perform_e8_request) {
            std::cout << "C1 reopened-context lifecycle preflight and one E8 typed request passed; "
                         "no stage object or source menu entry\n";
        } else if (perform_item_state_preflight) {
            std::cout << "C1 reopened-context lifecycle and item-state-owner preflight passed; "
                         "no E8 request, stage publication, or source menu entry\n";
        } else {
            std::cout << "C1 reopened-context lifecycle preflight passed; no E8 request, "
                         "stage publication, or source menu entry\n";
        }
    } catch (...) {
        if (full_world_lifecycle && !cleanup_complete) {
            try { throw; }
            catch (const std::exception& first) {
                std::cerr << "C3_WORLD_FIRST_FAILURE diagnostic=" << first.what()
                          << " world_retained=" << bool(world) << '\n';
            } catch (...) { std::cerr << "C3_WORLD_FIRST_FAILURE diagnostic=unknown\n"; }
            std::cerr.flush(); std::cout.flush(); std::_Exit(1);
        }
        if (!cleanup_complete) {
            try {
                cleanup();
            } catch (...) {
                std::abort();
            }
        }
        throw;
    }
}

void run_stadium_c1a_selection_smoke(
    const melee_web::RuntimeFiles& files,
    bool reopened_context_preflight,
    bool e8_request_trace,
    bool item_state_preflight,
    bool screen_roots_preflight,
    bool ground_map1_owner,
    bool source_on_init,
    bool full_world_lifecycle,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    TransitionTrace& trace,
    bool ready_session=false,
    bool pad_leave_probe=false,
    bool source_text_lifetime_probe=false,
    bool ready_text_membership_probe=false)
{
    char error[256]{};
    MeleeWebRetiredSisLease retired_sis{};
    MeleeWebMenuHost* host = melee_web_menu_host_create(error, sizeof(error));
    check(host != nullptr, error);
    check(melee_web_menu_host_enable_stadium_c1a(host, error, sizeof(error)), error);
    auto world = std::make_unique<melee_web::GameplayMenuWorld>(files);
    check(melee_web_menu_host_enter(host, world->audio(), error, sizeof(error)), error);

    PADStatus raw[4]{};
    melee_web_stage_input_neutral(raw);
    float pcm[1068]{};
    unsigned audio_phase = 0;
    auto tick = [&]() {
        const int result = melee_web_menu_host_tick(host, raw, error, sizeof(error));
        check(result == 1 || result == 3, error);
        audio_phase += 32000;
        const unsigned count = audio_phase / 60;
        audio_phase %= 60;
        check(melee_web_audio_render(world->audio(), pcm, count,
                                     error, sizeof(error)), error);
        return result;
    };
    auto transition = [&]() {
        const bool capture_sis = source_on_init &&
            melee_web_menu_host_phase(host) == MELEE_WEB_MENU_SSS;
        std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> pad_before_leave{};
        melee_web_stage_input_button(raw, PAD_BUTTON_START);
        int result = tick();
        melee_web_stage_input_neutral(raw);
        for (unsigned wait = 0; result != 3 && wait < 120; ++wait)
            result = tick();
        if (result != 3) {
            std::string detail = "Original menu input did not complete its C1a transition: phase=" +
                std::to_string(melee_web_menu_host_phase(host)) +
                " scene=" + std::to_string(melee_web_menu_host_source_scene(host));
            MeleeWebFighterInputObservation css{};
            if (melee_web_menu_host_phase(host) == MELEE_WEB_MENU_CSS &&
                melee_web_fighter_input_observe(CKIND_MARIO, &css)) {
                detail += " start_ready=" + std::to_string(css.source_start_ready) +
                    " start_cooldown=" + std::to_string(css.source_start_cooldown) +
                    " pending_scene=" + std::to_string(css.source_pending_scene) +
                    " last_start_trigger=" + std::to_string(css.source_last_start_trigger) +
                    " last_start_ready=" + std::to_string(css.source_last_start_ready) +
                    " last_start_pending=" + std::to_string(css.source_last_start_pending);
            }
            check(0, detail.c_str());
        }
        if (capture_sis) {
            check(melee_web_diagnostic_sis_capture(&retired_sis, error, sizeof(error)), error);
            trace.sis_lease("captured_before_menu_leave", &retired_sis);
        }
        if(capture_sis&&pad_leave_probe){
            melee_web_pad_state_capture(pad_before_leave.data());
            report_pad_snapshot("before-menu-leave",pad_before_leave.data());
        }
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
        if(capture_sis&&pad_leave_probe){
            std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> pad_after_leave{};
            melee_web_pad_state_capture(pad_after_leave.data());
            report_pad_snapshot("after-menu-leave",pad_after_leave.data(),pad_before_leave.data());
            const MeleeWebPadState* retained=melee_web_menu_host_input(host);
            std::fprintf(stderr,"C1_PAD_LEAVE_RETAINED host_input=%d host_phase=%d source_scene=%d source_world_exists=%d rng_owner=%p rng_value=%u\n",
                retained!=nullptr,melee_web_menu_host_phase(host),melee_web_menu_host_source_scene(host),
                melee_web_gameplay_world_exists(),static_cast<const void*>(seed_ptr),
                seed_ptr?*seed_ptr:0U);
            std::fflush(stderr);
            check(retained!=nullptr,
                  "Original SSS leave did not retain its decoded source PAD input");
        }
        if (capture_sis) {
            check(melee_web_diagnostic_sis_verify_retired(&retired_sis, error, sizeof(error)), error);
            trace.sis_lease("verified_retired_before_world_shutdown", &retired_sis);
        }
    };

    // The armed SSS still begins on an admitted stage. Its existing validation
    // remains live while the source cursor navigates toward Stadium.
    for (unsigned frame = 0; frame < 120; ++frame)
        check(tick() == 1, "C1a SSS navigation rejected its initial admitted stage");
    transition();
    check(melee_web_menu_host_phase(host) == MELEE_WEB_MENU_SSS_READY,
          "C1a CSS did not complete the original SSS transition");
    world->rebuild_scene(melee_web::GameplayMenuScene::Stages);
    check(melee_web_menu_host_enter(host, world->audio(), error, sizeof(error)), error);
    melee_web_stage_input_neutral(raw);
    for (unsigned frame = 0; frame < 120; ++frame)
        check(tick() == 1, "C1a rejected the ordinary initial SSS tile");

    bool at_stadium = false;
    for (unsigned frame = 0; frame < 180; ++frame) {
        MeleeWebStageInputObservation observed{};
        check(melee_web_stage_input_observe(St_Kind_PStadium, &observed),
              "Original SSS Stadium cursor observation is unavailable");
        const int state = melee_web_stage_input_drive(
            raw, &observed, St_Kind_PStadium);
        check(state != MELEE_WEB_STAGE_INPUT_INVALID,
              "Original SSS Stadium cursor target is invalid");
        if (state == MELEE_WEB_STAGE_INPUT_AT_TARGET) {
            check(observed.selected_stage_kind == St_Kind_PStadium,
                  "Raw PAD reached the Stadium target without source tile selection");
            at_stadium = true;
            break;
        }
        check(tick() == 1, "Original SSS transitioned during raw Stadium navigation");
    }
    check(at_stadium, "Raw PAD did not select the original Stadium SSS tile");
    check(melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium) &&
          !melee_web_menu_stage_available(St_Kind_PStadium),
          "C1a explicit confirmation did not remain separate from admission/random availability");
    transition();
    check(melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY,
          "Original SSS did not commit the C1a selection");

    check(!melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium),
          "C1a explicit-confirm permission survived source SSS exit");
    StartMeleeData raw_selection{};
    check(melee_web_menu_host_stadium_c1a_raw_selection(
              host, &raw_selection, error, sizeof(error)), error);
    check(raw_selection.rules.stkind == St_Kind_PStadium,
          "Source SSS raw payload did not retain StKind 3");
    SSSData unarmed_validation{};
    unarmed_validation.force_stage_id = -1;
    unarmed_validation.start_game = true;
    unarmed_validation.vs.start = raw_selection;
    check(!melee_web_menu_sss_selection_valid(&unarmed_validation),
          "The ordinary SSS validator admitted Stadium without its session gate");

    MeleeWebMenuMatchSelection ordinary{};
    check(!melee_web_menu_host_selection(host, &ordinary, error, sizeof(error)),
          "Ordinary match admission accepted Stadium through the diagnostic gate");
    MeleeWebMenuMatchSelection selected{};
    check(melee_web_menu_host_stadium_c1a_selection(
              host, &selected, error, sizeof(error)), error);
    check(selected.start.rules.stkind == St_Kind_PStadium,
          "Prepared C1a payload did not retain source StKind 3");
    const auto names = melee_web::stadium_c1a_asset_names(selected);
    for (const char* required : {
             "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat", "GrPs4.dat",
             "pstadium.hps", "pokesta.hps", "pstadium.ssm"})
        check(std::find(names.begin(), names.end(), required) != names.end(),
              "C1a source manifest omitted a Stadium dependency");
    check(!melee_web_stage_content(St_Kind_PStadium),
          "C1a diagnostic unexpectedly registered Stadium as playable");

    if (reopened_context_preflight) {
        run_stadium_c1_context_preflight(
            files, host, world, selected, names, menu_dir, game_dir,
            e8_request_trace, item_state_preflight, screen_roots_preflight,
            ground_map1_owner, source_on_init,
            source_on_init ? &retired_sis : nullptr, trace, full_world_lifecycle, ready_session,
            pad_leave_probe, source_text_lifetime_probe,
            ready_text_membership_probe);
    } else {
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
    }
    check(!melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium),
          "C1a explicit-confirm permission survived unload");
    if(ready_session||pad_leave_probe||source_text_lifetime_probe||
       ready_text_membership_probe)return;
    if (full_world_lifecycle) {
        std::cout << "Stadium original OnInit/OnLoad/OnStart two owned-world lifetimes passed; Ready/GO and ticks remain unrun\n";
    } else if (source_on_init) {
        std::cout << "C1a raw PAD CSS->SSS selection and one source-ordered Stadium OnInit lifetime passed; "
                     "admission remains closed\n";
    } else if (ground_map1_owner) {
        std::cout << "C1a raw PAD CSS->SSS selection, one E8 typed request, and one "
                     "Ground map1 constructor/removal passed; admission remains closed\n";
    } else if (e8_request_trace) {
        std::cout << "C1a raw PAD CSS->SSS selection and one E8 typed request passed; "
                     "ordinary match admission and gameplay entry remain closed\n";
    } else {
        std::cout << "C1a raw PAD CSS->SSS Stadium selection and exact preparation manifest passed; "
                     "ordinary admission and source/stage construction remain closed\n";
    }
}
#endif
}
int main(int argc,char** argv){try{
 if(argc==2&&std::string_view(argv[1])=="--returned-menu-fixture-manifest"){
  emit_sd_resolution_fixture_manifest(false);return 0;
 }
 if(argc==2&&std::string_view(argv[1])=="--sd-resolution-fixture-manifest"){
  emit_sd_resolution_fixture_manifest();return 0;
 }
 if(argc==2&&std::string_view(argv[1])=="--vs-sudden-death-source-control"){
  run_vs_sudden_death_source_control();return 0;
 }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
  if(argc==2&&std::string_view(argv[1])=="--stadium-gobj-proc-pool-controls"){
   check(melee_web_stadium_c1_gobj_proc_pool_control(), "Stadium GObj/proc pool control failed");
   return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-generator-lifetime-controls"){
   check(melee_web_stadium_c1_generator_lifetime_control(), "Stadium generator lifetime control failed");
   return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-retirement-phase-controls"){
   run_stadium_retirement_phase_controls();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-manager-mapset-controls"){
   check(melee_web_stadium_c1_manager_mapset_control(), "Stadium manager mapset control failed");
   return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--ground-pending-callback-controls"){
   check(melee_web_stadium_c1_pending_queue_loss_control(), "Ground queue root-loss control failed");
   std::cout.flush();std::cerr.flush();
   std::_Exit(0); // Retained expected partial owner; no raw source-world shutdown.
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-profile-controls"){
   run_stadium_profile_controls();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-started-map2-buffer-controls"){
   run_stadium_profile_controls();
   run_stadium_started_map2_buffer_controls();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-on-init-controls"){
   run_stadium_profile_controls();
   run_stadium_effect_runtime_lifecycle_control();
   run_stadium_selection_rng_controls();
   std::cout<<"C1 source OnInit refusal and synthetic event-journal controls passed; no Stadium stage initialization invoked\n";
   return 0;
  }
  if (argc == 2 &&
      (std::string_view(argv[1]) == "--stadium-cache-live-controls" ||
       std::string_view(argv[1]) == "--stadium-cache-live-controls=0")) {
   run_stadium_cache_live_control(false);return 0;
  }
  if (argc == 2 &&
      (std::string_view(argv[1]) == "--stadium-cache-live-controls-owner" ||
       std::string_view(argv[1]) == "--stadium-cache-live-controls=1")) {
   run_stadium_cache_live_control(true);return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-map-light-adoption-controls"){
   run_stadium_map_light_adoption_control();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-sis-allocator-controls"){
   run_stadium_sis_allocator_lifecycle_control();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-sis-text-event-controls"){
   HSD_SisLib_C1TextProbeSet(1);
   run_stadium_sis_allocator_lifecycle_control();
   HSD_SisLib_C1TextProbeSet(0);
   return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-bind-refusal-controls"){
   run_stadium_bind_refusal_control();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-owner-graph-controls"){
   run_stadium_owner_graph_controls();return 0;
  }
  if(argc==2&&std::string_view(argv[1])=="--stadium-yakumono-exchange"){
  run_stadium_yakumono_exchange_control();return 0;
 }
#endif
 if(argc<3||argc>8)throw std::runtime_error("Expected menu/audio directories, optional stage kind, transition trace path, source revision and input recipe");
 const int stage_kind=argc>=4?std::stoi(argv[3]):St_Kind_Last;
 const char* trace_path=argc>=5?argv[4]:nullptr;
 const char* source_revision=argc>=6?argv[5]:nullptr;
 const char* input_recipe=argc>=7?argv[6]:nullptr;
 const char* replay_recipe_path=argc==8?argv[7]:nullptr;
 const bool retail_fd_recipe=input_recipe&&std::string(input_recipe)=="retail-stock-fd-v1";
 const bool results_mario_recipe=input_recipe&&std::string(input_recipe)=="results-mario-v1";
 const bool link_css_unload_recipe=input_recipe&&std::string(input_recipe)=="link-css-unload-v1";
 const bool title_main_abort_recipe=input_recipe&&std::string(input_recipe)=="title-main-abort-v1";
 const bool opening_movie_preload_recipe=input_recipe&&std::string(input_recipe)=="opening-movie-preload-v1";
 const bool trophy_baseline_recipe=input_recipe&&std::string(input_recipe)=="trophy-baseline-v1";
 const bool sound_settings_recipe=input_recipe&&std::string(input_recipe)=="main-settings-sound-v1";
 const bool sd_menu_setup_recipe=input_recipe&&
     std::string(input_recipe)=="sudden-death-menu-setup-control-v1";
 const bool css_observer_recipe=input_recipe&&
     std::string(input_recipe)=="sparse-css-observer-preflight-v1";
 const bool sparse_css_recipe=input_recipe&&
     std::string(input_recipe)=="sparse-actual-css-sss-control-v1";
 const bool sparse_pad_recipe=input_recipe&&
     std::string(input_recipe)=="sparse-source-pad-control-v1";
 const bool ordinary_timeout_recipe=input_recipe&&
     std::string(input_recipe)=="ordinary-nontied-timeout-control-v1";
 const bool timeout_results_recipe=input_recipe&&
     std::string(input_recipe)=="returned-menu-two-human-timeout-results-control-v1";
 const bool two_human_results_recipe=timeout_results_recipe||(input_recipe&&
     std::string(input_recipe)=="returned-menu-two-human-results-input-control-v1");
 const bool returned_menu_recipe=two_human_results_recipe||(input_recipe&&
     std::string(input_recipe)=="returned-menu-results-control-v1");
 const bool resolve_sd_recipe=input_recipe&&
     std::string(input_recipe)=="sudden-death-natural-resolution-control-v1";
 const bool natural_sd_recipe=resolve_sd_recipe||(input_recipe&&
     std::string(input_recipe)=="sudden-death-natural-timeout-control-v1");
 const bool sudden_death_world_recipe=natural_sd_recipe||(input_recipe&&
     std::string(input_recipe)=="sudden-death-world-control-v1");
 const bool sudden_death_host_recipe=input_recipe&&
     std::string(input_recipe)=="sudden-death-host-control-v1";
 const bool v10_css_replay_start_recipe=input_recipe&&
     std::string(input_recipe)=="whole-session-css-replay-start-v10-v1";
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 const bool stadium_c1a_recipe=input_recipe&&std::string(input_recipe)=="stadium-c1a-v1";
 const bool stadium_c1_context_preflight_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-c1-context-preflight-v1";
 const bool stadium_c1_item_state_preflight_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-c1-item-state-preflight-v1";
 const bool stadium_screen_roots_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-screen-roots-preflight-v1";
 const bool stadium_e8_request_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-e8-request-v1";
 const bool stadium_ground_map1_owner_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-ground-map1-owner-v1";
 const bool stadium_source_on_init_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-source-oninit-v1";
 const bool stadium_ready_session_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-source-ready-session-v1";
 const bool stadium_source_world_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-source-world-lifecycle-v1";
 const bool stadium_pad_leave_probe_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-pad-leave-probe-v1";
 const bool stadium_source_text_lifetime_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-source-text-lifetime-v1";
 const bool stadium_ready_text_membership_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-source-ready-text-membership-v1";
#else
 const bool stadium_c1a_recipe=false;
 const bool stadium_c1_context_preflight_recipe=false;
 const bool stadium_c1_item_state_preflight_recipe=false;
 const bool stadium_screen_roots_recipe=false;
 const bool stadium_e8_request_recipe=false;
 const bool stadium_ground_map1_owner_recipe=false;
 const bool stadium_source_on_init_recipe=false;
 const bool stadium_source_world_recipe=false;
 const bool stadium_ready_session_recipe=false;
 const bool stadium_pad_leave_probe_recipe=false;
 const bool stadium_source_text_lifetime_recipe=false;
 const bool stadium_ready_text_membership_recipe=false;
#endif
 if(input_recipe&&!css_observer_recipe&&!sparse_css_recipe&&!sparse_pad_recipe&&!ordinary_timeout_recipe&&!retail_fd_recipe&&!results_mario_recipe&&!link_css_unload_recipe&&
    !title_main_abort_recipe&&!opening_movie_preload_recipe&&!trophy_baseline_recipe&&
    !sound_settings_recipe&&!sudden_death_host_recipe&&!sudden_death_world_recipe&&
    !sd_menu_setup_recipe&&!returned_menu_recipe&&
    !stadium_c1a_recipe&&
    !stadium_c1_context_preflight_recipe&&
    !stadium_c1_item_state_preflight_recipe&&!stadium_screen_roots_recipe&&
    !stadium_e8_request_recipe&&!stadium_ground_map1_owner_recipe&&
    !stadium_source_on_init_recipe&&!stadium_source_world_recipe&&!stadium_ready_session_recipe&&
    !stadium_pad_leave_probe_recipe&&!stadium_source_text_lifetime_recipe&&
    !stadium_ready_text_membership_recipe&&
    !v10_css_replay_start_recipe)
    throw std::runtime_error("Unknown transition input recipe");
 if(v10_css_replay_start_recipe&&
    (argc!=8||!replay_recipe_path||!trace_path||!source_revision))
   throw std::runtime_error("MWRC v10 CSS replay-start reducer requires trace, source revision and exact recipe path");
 if(!v10_css_replay_start_recipe&&argc==8)
   throw std::runtime_error("Only the MWRC v10 CSS replay-start reducer accepts an exact recipe path");
 if((css_observer_recipe||sparse_css_recipe||sparse_pad_recipe||ordinary_timeout_recipe||retail_fd_recipe||results_mario_recipe||sudden_death_host_recipe||
     sudden_death_world_recipe||sd_menu_setup_recipe||returned_menu_recipe||
     v10_css_replay_start_recipe)&&
    stage_kind!=St_Kind_Last)
   throw std::runtime_error("Explicit FD recipes require Final Destination");
 if((stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
     stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
     stadium_e8_request_recipe||stadium_ground_map1_owner_recipe||
     stadium_source_on_init_recipe||stadium_source_world_recipe||stadium_ready_session_recipe||
     stadium_pad_leave_probe_recipe||stadium_source_text_lifetime_recipe||
     stadium_ready_text_membership_recipe)&&
    stage_kind!=St_Kind_PStadium)
   throw std::runtime_error("C1a recipes require source StKind 3");
 TransitionTrace trace(trace_path,source_revision,input_recipe);
 melee_web::RuntimeFiles files;
 std::vector<std::string> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","GrNBa.dat","GrSt.dat","hyaku.hps","hyaku2.hps","sp_zako.hps","ystory.hps","ItCo.usd","EfMrData.dat","EfFxData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","fox.ssm","falco.ssm","mars.ssm","drmario.ssm","emblem.ssm","pupupu.ssm","dsp_coef.bin","sislib_font.bin"};
 if(css_observer_recipe||sparse_css_recipe||sparse_pad_recipe||ordinary_timeout_recipe||sudden_death_host_recipe||sudden_death_world_recipe||sd_menu_setup_recipe||returned_menu_recipe||stadium_c1a_recipe||
    stadium_c1_context_preflight_recipe||
    stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
    stadium_e8_request_recipe||stadium_ground_map1_owner_recipe||
    stadium_source_on_init_recipe||stadium_source_world_recipe||stadium_ready_session_recipe||
    stadium_pad_leave_probe_recipe||stadium_source_text_lifetime_recipe||
    stadium_ready_text_membership_recipe||
    v10_css_replay_start_recipe||title_main_abort_recipe||opening_movie_preload_recipe||
    trophy_baseline_recipe||sound_settings_recipe)
  keys=melee_web::menu_asset_names();
 for(const auto& key:melee_web::menu_asset_names())
  if(std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);
 for(const auto& key:keys){
  const auto root=std::filesystem::exists(std::filesystem::path(argv[1])/key)?argv[1]:argv[2];
  std::ifstream input(std::filesystem::path(root)/key,std::ios::binary);if(!input)throw std::runtime_error("Missing owned menu host fixture: "+key);
  files[key]={(std::istreambuf_iterator<char>(input)),{}};
 }
 if(results_mario_recipe){
  for(const char* key:{"GmRst.usd","SdRst.usd","GmRstMMr.dat","ff_mario.hps","TyDatai.usd","IfPrize.usd","SdPrize.usd","s_info1.hps","s_info2.hps","s_info3.hps"}){
   std::ifstream input(std::filesystem::path(argv[2])/key,std::ios::binary);
   if(!input)throw std::runtime_error("Missing owned Results fixture");
   files[key]={(std::istreambuf_iterator<char>(input)),{}};
  }
 }
 char session_error[256]{};
 check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
 const auto session_allocation=melee_web_gameplay_allocation();
 if(v10_css_replay_start_recipe){
  run_v10_css_replay_start_prefix(files,replay_recipe_path,trace);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"MWRC v10 original CSS replay-start prefix returned after one recorded source tick; "
              "no draw or full-session comparison\n";
  return 0;
 }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 if(stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
    stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
    stadium_e8_request_recipe||stadium_ground_map1_owner_recipe||
    stadium_source_on_init_recipe||stadium_source_world_recipe||stadium_ready_session_recipe||
    stadium_pad_leave_probe_recipe||stadium_source_text_lifetime_recipe||
    stadium_ready_text_membership_recipe){
  run_stadium_c1a_selection_smoke(
      files, stadium_c1_context_preflight_recipe||
          stadium_c1_item_state_preflight_recipe||stadium_screen_roots_recipe||
          stadium_e8_request_recipe||stadium_ground_map1_owner_recipe||
          stadium_source_on_init_recipe||stadium_source_world_recipe||stadium_ready_session_recipe||
          stadium_pad_leave_probe_recipe||stadium_source_text_lifetime_recipe||
              stadium_ready_text_membership_recipe,
      stadium_e8_request_recipe||stadium_ground_map1_owner_recipe,
      stadium_c1_item_state_preflight_recipe, stadium_screen_roots_recipe,
          stadium_ground_map1_owner_recipe, stadium_source_on_init_recipe||stadium_source_world_recipe||
              stadium_ready_session_recipe||stadium_pad_leave_probe_recipe||
                  stadium_source_text_lifetime_recipe||
                  stadium_ready_text_membership_recipe,
      stadium_source_world_recipe,
      argv[1], argv[2], trace,stadium_ready_session_recipe,
      stadium_pad_leave_probe_recipe,stadium_source_text_lifetime_recipe,
      stadium_ready_text_membership_recipe);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  if(stadium_source_world_recipe||stadium_ready_session_recipe||
     stadium_pad_leave_probe_recipe||stadium_source_text_lifetime_recipe||
     stadium_ready_text_membership_recipe){
   const auto released=melee_web_gameplay_allocation();
   std::fprintf(stderr,"C3_SESSION_CLOSED identity=%llu generation=%llu bytes=%llu world_exists=%d\n",
       static_cast<unsigned long long>(released.identity),static_cast<unsigned long long>(released.generation),
       static_cast<unsigned long long>(released.bytes),melee_web_gameplay_world_exists());
   std::fflush(stderr);
   check(!released.identity&&!released.generation&&!released.bytes&&!melee_web_gameplay_world_exists(),
         "Stadium full-world continuation did not retire its complete owned session arena");
  }
  return 0;
 }
#endif
 if(title_main_abort_recipe){
  run_title_main_abort_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Title/Main checked abort and CSS re-entry smoke passed; no browser or retail-route claim\n";
  return 0;
 }
 if(opening_movie_preload_recipe){
  run_opening_movie_preload_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Opening movie preload ownership probe passed; no movie decode or retail-route claim\n";
  return 0;
 }
 if(trophy_baseline_recipe){
  run_trophy_baseline_smoke(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native trophy baseline initialization smoke passed; no browser or retail-route claim\n";
  return 0;
 }
 if(sound_settings_recipe){
  run_main_sound_mix_route(files);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native Main Settings Sound source route passed; no browser or retail-route claim\n";
  return 0;
 }
 const unsigned cycle_count=(css_observer_recipe||sparse_css_recipe||sparse_pad_recipe||ordinary_timeout_recipe||results_mario_recipe||sudden_death_host_recipe||sudden_death_world_recipe||sd_menu_setup_recipe||returned_menu_recipe)?1:2;
 for(unsigned cycle=0;cycle<cycle_count;cycle++){
  trace.begin_run(cycle);
  if((retail_fd_recipe||results_mario_recipe)&&cycle==0)*seed_ptr=1840631306u;
  const GameRules pre_native_rules=*gmMainLib_GetGameRules();
  char error[256]{};auto* host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
  if(ordinary_timeout_recipe||natural_sd_recipe||returned_menu_recipe){
   GameRules rules=gmMainLib_803D4A48;rules.mode=1;rules.stock_count=4;rules.stock_time_limit=1;
   emit_native_bytes("stock_timer_fixture_initial_GameRules",&rules,sizeof(rules));
   check(melee_web_menu_host_apply_initial_native_rules(host,&rules,error,sizeof(error)),error);
  }
  auto world=std::make_unique<melee_web::GameplayMenuWorld>(files);
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  if(ordinary_timeout_recipe||natural_sd_recipe||returned_menu_recipe){
   check(gmMainLib_GetGameRules()->stock_time_limit==1,
         "Initial CSS lost authored persistent one-minute fixture");
   GameRules invalid=*gmMainLib_GetGameRules();
   check(!melee_web_menu_host_apply_initial_native_rules(host,&invalid,error,sizeof(error)),
         "Entered host accepted setup fixture mutation");
  }
  trace.event("capture_begin",world->audio());
  PADStatus raw[4]{};raw[2].err=raw[3].err=-1;float pcm[1068];unsigned audio_phase=0;
  auto tick=[&](){
   int result=melee_web_menu_host_tick(host,raw,error,sizeof(error));
   check(result==1||result==3,error);
   audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(world->audio(),pcm,count,error,sizeof(error)),error);
   return result;
  };
  auto transition=[&](u16 button=PAD_BUTTON_START){
   raw[0].button=button;
   int result=tick();raw[0].button=0;
   for(unsigned wait=0;result!=3&&wait<120;wait++)result=tick();
   check(result==3,"Original menu input did not complete its transition");
   check(melee_web_menu_host_leave(host,0,error,sizeof(error)),error);
   world->verify_immutable_archives();
  };
  auto rebuild_menu_scene=[&](){
   MeleeWebAudio* retained=world->audio();
   const uint64_t generation=melee_web_audio_generation(retained);
   uint32_t completed=0,revisited=0,after_completed=0,after_revisited=0;
   check(generation!=0,"Original menu audio lifetime is unavailable");
   check(melee_web_audio_stream_progress(retained,&completed,&revisited),
         "Original menu HPS progress is unavailable before scene rebuild");
   const auto scene=melee_web_menu_host_phase(host)==2?
       melee_web::GameplayMenuScene::Stages:melee_web::GameplayMenuScene::Characters;
   world->rebuild_scene(scene);
   check(world->audio()==retained&&melee_web_audio_generation(world->audio())==generation,
         "CSS/SSS scene rebuild replaced the original menu audio lifetime");
   check(melee_web_audio_stream_progress(world->audio(),&after_completed,&after_revisited)&&
         after_completed==completed&&after_revisited==revisited,
         "CSS/SSS scene rebuild reset or advanced menu music outside an audio tick");
   check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  };
  auto select_stage=[&](){
  bool at_target=false;
  for(unsigned t=0;t<120;t++){
   MeleeWebStageInputObservation observed{};
   check(melee_web_stage_input_observe(stage_kind,&observed),"Original SSS cursor observation unavailable");
   const int state=melee_web_stage_input_drive(raw,&observed,stage_kind);
   check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"Original SSS target is invalid");
   if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET){
    check(observed.selected_stage_kind==stage_kind,"Cursor target and source selected tile differ");
    at_target=true;break;
   }
   if(sparse_css_recipe){raw[1].err=-1;raw[2].err=0;raw[3].err=-1;}
   check(tick()==1,"SSS cursor input unexpectedly transitioned");
  }
  check(at_target,"Original SSS cursor did not reach requested stage");
  };
  const unsigned first_css_neutral=retail_fd_recipe&&cycle==0?187:120;
  if(!retail_fd_recipe){
   // Owned LbRb row zero ends after four motor-on samples and one hard stop.
   // Exercise the menu host's normal raw-sample boundary, without adding
   // simulation steps or claiming physical actuator behavior.
   lb_80014574(0,0x4d57,0,0);
   check(HSD_Rumble_804C22E0[0].nb_list==1,
         "Menu startup did not publish an available source rumble pool");
  }
  for(unsigned t=0;t<first_css_neutral;t++){
   check(tick()==1,"Unexpected CSS transition");
   if(!retail_fd_recipe&&t==5)
    check(HSD_Rumble_804C22E0[0].nb_list==0,
          "Menu raw samples did not advance and release the source rumble program");
  }
  uint32_t initial_music_completed=0,initial_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&initial_music_completed,
                                         &initial_music_revisited)&&
        initial_music_completed>0,
        "Original CSS music did not load an HPS payload");
  if(css_observer_recipe){
   // Same startup and single raw neutral sample as the failed first observer.
   for(auto& pad:raw)pad={};raw[1].err=raw[3].err=-1;
   check(tick()==1,"CSS observer reducer left active CSS");
   int cursors[4][4]{},doors[4][10]{};float geometry[4][12]{};
   css_observer_failure_count=0;
   melee_web_css_observe_setup_diagnostic_once(css_observer_failure_diagnostic);
   const int observed=melee_web_css_observe_setup(cursors,doors,geometry);
   std::cout<<"{\"record\":\"css_observer_reducer_observation\",\"observed\":"<<observed
       <<",\"failure_callbacks\":"<<css_observer_failure_count<<"}\n";std::cout.flush();
   check((observed==0&&css_observer_failure_count==1)||
         (observed==1&&css_observer_failure_count==0),"CSS observer reducer lost one-shot diagnostics");
   check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
   world->verify_immutable_archives();world->close();world.reset();
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin()&&melee_web_vs_mode_end(),"CSS observer reducer leaked VS lease");
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists()&&
         std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "CSS observer reducer cleanup lost world/arena/rules");
   continue; // No cursor movement, door taps, SSS or match.
  }
  if(sparse_css_recipe)drive_sparse_source_css(raw,tick);
  if(cycle==1||link_css_unload_recipe){
   const int target_kind=link_css_unload_recipe?(cycle==0?CKIND_LINK:CKIND_CLINK):CKIND_FALCO;
   bool target_selected=false;
   for(unsigned t=0;t<180;t++){
    MeleeWebFighterInputObservation observed{};
    check(melee_web_fighter_input_observe(target_kind,&observed),
          "Original CSS fighter observation unavailable");
    const int state=melee_web_fighter_input_drive(raw,&observed,target_kind);
    check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,
          "Original CSS fighter target is invalid");
    if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED){
     target_selected=true;break;
    }
    if(state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||
       state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY)
      melee_web_fighter_input_button(raw,PAD_BUTTON_A);
    check(tick()==1,"CSS cursor input unexpectedly transitioned");
    melee_web_fighter_input_neutral(raw);
    check(tick()==1,"CSS button release unexpectedly transitioned");
   }
   check(target_selected,"Original CSS did not commit requested fighter through raw PAD input");
   // The source keeps the door/model confirmation animation active briefly
   // after the drop.  Give that original process time to reach its ordinary
   // Start-accepting state before requesting the scene transition.
   melee_web_fighter_input_neutral(raw);
   for(unsigned settle=0;settle<30;++settle)
    check(tick()==1,"CSS transitioned during fighter confirmation settle");
  }
  if(link_css_unload_recipe){
   check(melee_web_menu_host_phase(host)==1,"CSS unload recipe left the original CSS phase");
   check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
   world->verify_immutable_archives();
   world->close();world.reset();
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
   host=nullptr;
   std::cout<<"Original CSS "<<(cycle==0?"Link":"Young Link")<<" audio registry entered, aborted and unloaded\n";
   continue;
  }
  transition();check(melee_web_menu_host_phase(host)==2,"CSS did not choose original SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected SSS transition");
  // Exercise the real B cancellation before committing the match, with new
  // owned worlds for both directions and no direct source selection writes.
  transition(PAD_BUTTON_B);check(melee_web_menu_host_phase(host)==4,"Original SSS B did not return toward CSS");
  trace.event("sss_exit_complete",world->audio(),"css");
  rebuild_menu_scene();
  trace.event("css_enter_complete",world->audio());
  const unsigned second_css_neutral=retail_fd_recipe&&cycle==0?138:120;
  for(unsigned t=0;t<second_css_neutral;t++)check(tick()==1,"Unexpected cancelled CSS transition");
  transition();check(melee_web_menu_host_phase(host)==2,"Returned CSS did not choose SSS");
  trace.event("css_exit_complete",world->audio());
  rebuild_menu_scene();
  trace.event("sss_enter_complete",world->audio());
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected second SSS transition");
  // Move the original SSS cursor with raw PAD input. Random is deliberately
  // not used: adding an available stage must not change the FD regression.
  select_stage();
  uint32_t continued_music_completed=0,continued_music_revisited=0;
  check(melee_web_audio_stream_progress(world->audio(),&continued_music_completed,
                                         &continued_music_revisited)&&
        continued_music_completed>initial_music_completed,
        "Original menu music did not continue loading across CSS/SSS scenes");
  if(sparse_css_recipe){raw[1].err=-1;raw[2].err=0;raw[3].err=-1;}
  transition();check(melee_web_menu_host_phase(host)==5,"SSS did not complete original selection");
  StartMeleeData raw_start{};
  check(melee_web_menu_host_raw_selection(host,&raw_start,error,sizeof(error)),error);
  MeleeWebMenuMatchSelection selection{};check(melee_web_menu_host_selection(host,&selection,error,sizeof(error)),error);
  check(selection.start.rules.stkind==stage_kind,"Source SSS committed another stage");
  check(selection.start.players[0].ckind==(cycle==1?CKIND_FALCO:CKIND_MARIO),
        "Source CSS committed another P1 character");
  const uint32_t selection_rng=selection.random_seed;
  MeleeWebMenuMatchSelection raw_selection{};raw_selection.start=raw_start;
  raw_selection.random_seed=selection_rng;
  trace.event("sss_exit_complete",world->audio(),"match",&raw_selection,&selection_rng);
  world->close();world.reset();audio_phase=0;
  if(sparse_css_recipe){
   emit_native_bytes("actual_sparse_sss_raw_start",&raw_start,sizeof(raw_start));
   emit_native_bytes("actual_sparse_sss_normalized_start",&selection.start,sizeof(selection.start));
   check(selection.player_count==2,"Actual sparse SSS lost two-player count");
   for(unsigned port=0;port<4;++port){
    const int expected=(port==0||port==2)?Gm_PKind_Human:Gm_PKind_NA;
    check(raw_start.players[port].slot_type==expected&&selection.start.players[port].slot_type==expected&&
          raw_start.players[port].slot==0&&selection.start.players[port].slot==0,
          "Closed source SSS renumbered sparse player identities");
    if(expected==Gm_PKind_Human)check(raw_start.players[port].ckind==CKIND_MARIO&&
        selection.start.players[port].ckind==CKIND_MARIO&&selection.players[port].controller==port,
        "Closed source SSS lost sparse Mario/controller identity");
   }
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin()&&melee_web_vs_mode_end(),"Actual sparse CSS/SSS leaked VS lease");
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists()&&
         std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "Actual sparse CSS/SSS cleanup lost world/arena/rules");
   continue; // No match construction or post-SSS payload mutation.
  }
  if(sparse_pad_recipe){
   // Preserve the observed closed source payload except the explicitly authored
   // Human membership/row relocation. Do not promote this to a CSS joining claim.
   check(selection.player_count==2&&selection.start.players[2].slot_type==Gm_PKind_NA&&
         selection.start.players[3].slot_type==Gm_PKind_NA,
         "Sparse fixture requires the existing dense two-player source selection");
   selection.start.players[0].slot_type=Gm_PKind_Human;
   selection.start.players[2]=selection.start.players[1];
   selection.start.players[2].slot_type=Gm_PKind_Human;
   selection.players[2]=selection.players[1];selection.players[2].controller=2;
   selection.start.players[1]={};selection.start.players[1].slot_type=Gm_PKind_NA;
   selection.players[1]={};
   emit_native_bytes("authored_sparse_source_start",&selection.start,sizeof(selection.start));
   for(const auto& name:melee_web::match_asset_names(selection)){
    if(files.contains(name))continue;
    auto path=std::filesystem::path(argv[1])/name;
    if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing sparse source match fixture: "+name);
    files[name]={(std::istreambuf_iterator<char>(input)),{}};
   }
   try{run_sparse_source_pad(files,host,selection,error,sizeof(error));}
   catch(...){const auto primary=std::current_exception();
    if(!melee_web_menu_host_destroy(host,error,sizeof(error)))std::cerr<<"Secondary sparse host destroy: "<<error<<'\n';
    std::rethrow_exception(primary);}
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin()&&melee_web_vs_mode_end(),"Sparse component leaked VS lease");
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists()&&
         std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "Sparse component lost retained arena or persistent rules restoration");
   continue;
  }
  if(ordinary_timeout_recipe){
   const auto* prepared=melee_web_menu_host_post_vs_mode(host);
   check(prepared!=nullptr,"Ordinary timeout has no retained post-VS mode");
   melee_web_pad_state_apply(melee_web_menu_host_input(host));
   uint8_t pad[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(pad);
   trace.menu_selection(raw_start,selection,*prepared,pad);
   for(const auto& name:sd_resolution_asset_names(selection)){
    if(files.contains(name))continue;
    auto path=std::filesystem::path(argv[1])/name;
    if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing ordinary timeout fixture: "+name);
    files[name]={(std::istreambuf_iterator<char>(input)),{}};
   }
   try{run_ordinary_nontied_timeout(files,host,selection,error,sizeof(error));}
   catch(...){const auto primary=std::current_exception();
    if(!melee_web_menu_host_destroy(host,error,sizeof(error)))std::cerr<<"Secondary ordinary host destroy: "<<error<<'\n';
    std::rethrow_exception(primary);}
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin()&&melee_web_vs_mode_end(),"Ordinary timeout leaked VS mode lease");
   check(!melee_web_gameplay_world_exists()&&std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "Ordinary timeout cleanup lost world or persistent rules restoration");
   continue;
  }
  if(returned_menu_recipe){
   check(selection.player_count==2&&selection.start.rules.time_limit==60,
         "Reduced returned menu control lost fixture-controlled stock timer");
   const auto* prepared=melee_web_menu_host_post_vs_mode(host);
   check(prepared!=nullptr,"Reduced control has no checked post-VS mode");
   melee_web_pad_state_apply(melee_web_menu_host_input(host));
   uint8_t pad[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(pad);
   trace.menu_selection(raw_start,selection,*prepared,pad);
   for(const auto& name:sd_resolution_asset_names(selection,false)){
    if(files.contains(name))continue;
    auto path=std::filesystem::path(argv[1])/name;
    if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Missing constructed Results fixture: "+name);
    files[name]={(std::istreambuf_iterator<char>(input)),{}};
   }
   MatchExitInfo constructed{};
   constructed.match_end.outcome=timeout_results_recipe?OUTCOME_TIMEOUT:OUTCOME_ELIMINATION;
   constructed.match_end.match_kind=selection.start.rules.match_kind;
   constructed.match_end.n_winners=1;constructed.match_end.winners[0]=1;
   for(auto& standing:constructed.match_end.player_standings)standing.slot_type=Gm_PKind_NA;
   for(unsigned slot=0;slot<selection.player_count;++slot){
    const auto& player=selection.start.players[slot];
    auto& standing=constructed.match_end.player_standings[slot];
    standing.slot_type=player.slot_type;standing.ckind=player.ckind;
    standing.x3=player.color;standing.x4=player.nametag;
    standing.stocks=slot==1?4:(timeout_results_recipe?3:0);standing.is_big_loser=slot==0;
   }
   emit_native_bytes("constructed_non_tied_terminal_no_gameplay",&constructed,sizeof(constructed));
   auto result_seed=selection.random_seed;
   try{
    run_results_source_smoke(files,host,constructed,result_seed,pad,two_human_results_recipe?(timeout_results_recipe?&kTimeoutResults:&kEliminationResults):nullptr);
    run_returned_menu_selection(files,host,error,sizeof(error));
   }catch(...){
    const auto primary=std::current_exception();
    if(!melee_web_menu_host_destroy(host,error,sizeof(error)))
     std::cerr<<"Secondary reduced host destroy failure: "<<error<<'\n';
    std::rethrow_exception(primary);
   }
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin(),"Reduced menu control leaked VS lease");
   check(melee_web_vs_mode_end(),"Reduced menu VS lease reacquisition did not close");
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&
         retained.generation==session_allocation.generation&&retained.bytes==session_allocation.bytes&&
         !melee_web_gameplay_world_exists()&&
         std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "Reduced menu control leaked world or initial rules");
   std::cout<<"Constructed non-tied outcome through original Results and returned CSS/SSS passed; no gameplay world or natural outcome claim\n";
   continue;
  }
  if(sd_menu_setup_recipe){
   check(melee_web_menu_host_input(host)!=nullptr,
         "Closed SSS has no retained PAD bank");
   melee_web_pad_state_apply(melee_web_menu_host_input(host));
   uint8_t pad[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(pad);
   const auto* prepared=melee_web_menu_host_post_vs_mode(host);
   check(prepared!=nullptr,"Menu diagnostic has no retained VS entry state");
   trace.menu_selection(raw_start,selection,*prepared,pad);
   check(!melee_web_gameplay_generation()&&!melee_web_gameplay_world_exists(),
         "Menu-only setup retained a world after closed SSS");
   check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);host=nullptr;
   check(melee_web_vs_mode_begin(),"Menu-only host cleanup leaked VS lease");
   check(melee_web_vs_mode_end(),"Menu-only reacquired VS lease did not close");
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&
         retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes,
         "Menu-only teardown changed application arena identity");
   trace.event("menu_setup_host_cleanup_complete",nullptr);
   continue;
  }
  if(sudden_death_host_recipe||sudden_death_world_recipe){
   if(sudden_death_world_recipe){
    check(selection.player_count==2&&selection.start.rules.stkind==St_Kind_Last&&
          selection.start.players[0].ckind==CKIND_MARIO&&
          selection.start.players[1].ckind==CKIND_MARIO&&
          selection.start.players[0].color==1&&selection.start.players[1].color==0&&
          selection.players[0].controller==0&&selection.players[1].controller==1&&
          selection.start.rules.match_kind==MatchKind_Stock&&
          selection.start.rules.is_stock&&selection.start.rules.is_vs&&
          selection.start.players[0].stocks==4&&selection.start.players[1].stocks==4,
          "SD lifecycle fixture contract requires observed four-stock Mario/Mario colors 1/0 ports 0/1 on FD");
    for(const auto& name:resolve_sd_recipe?sd_resolution_asset_names(selection):
                           melee_web::match_asset_names(selection)){
     if(files.contains(name))continue;
     auto path=std::filesystem::path(argv[1])/name;
     if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
     std::ifstream input(path,std::ios::binary);
     if(!input)throw std::runtime_error("Missing source SD fixture: "+name);
     files[name]={(std::istreambuf_iterator<char>(input)),{}};
    }
   }
   if(sudden_death_world_recipe){
    const auto* prepared=melee_web_menu_host_post_vs_mode(host);
    check(prepared!=nullptr,"SD lifecycle has no retained original VS mode");
    melee_web_pad_state_apply(melee_web_menu_host_input(host));
    uint8_t pad[MELEE_WEB_PAD_STATE_BYTES];melee_web_pad_state_capture(pad);
    trace.menu_selection(raw_start,selection,*prepared,pad);
   }
   run_sudden_death_host_control(host,selection,error,sizeof(error),
                                sudden_death_world_recipe?&files:nullptr,natural_sd_recipe,resolve_sd_recipe);
   const auto retained=melee_web_gameplay_allocation();
   check(retained.identity==session_allocation.identity&&
         retained.generation==session_allocation.generation&&
         retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists(),
         "Typed Sudden Death host control changed the retained application arena");
   if(natural_sd_recipe)check(std::memcmp(gmMainLib_GetGameRules(),&pre_native_rules,sizeof(pre_native_rules))==0,
         "Natural SD fixture failed to restore initial persistent rules");
   continue;
  }
  for(const auto& name:melee_web::match_asset_names(selection)){
   if(files.find(name)!=files.end())continue;
   auto path=std::filesystem::path(argv[1])/name;
   if(!std::filesystem::is_regular_file(path))path=std::filesystem::path(argv[2])/name;
   std::ifstream input(path,std::ios::binary);
   if(!input)throw std::runtime_error("Missing source match fixture: "+name);
   files[name]={(std::istreambuf_iterator<char>(input)),{}};
  }
  const MeleeWebPadState* menu_input=melee_web_menu_host_input(host);
  check(menu_input!=nullptr,"Original SSS did not retain PAD history for match entry");
  bool match_entry_recorded=false;
  if(cycle==0)for(unsigned stop:{0u,60u,100u}){
   // Unload both before and after Ready's stage-start callback, then rebuild
   // the full SDK world from the same immutable native selection.
   melee_web::GameplayMatchSession interrupted(files,selection,*menu_input);unsigned phase=0;
   if(!match_entry_recorded){const uint32_t rng=interrupted.random_seed();trace.event("match_enter_complete",interrupted.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   for(unsigned t=0;t<stop;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;interrupted.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(interrupted.audio(),pcm,count,error,sizeof(error)),error);
   }
   interrupted.close();interrupted.close();
  }
  if(cycle==0){
   // Exercise the source-owned pause/no-contest path from the same committed
   // menu payload before the ordinary stock run.  Every source tick still
   // drains the resident audio stream.
   melee_web::GameplayMatchSession no_contest(files,selection,*menu_input);unsigned phase=0;
   for(unsigned t=0;t<600&&!no_contest.ready();++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;no_contest.tick(pads);
    phase+=32000;unsigned count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.ready(),"Original Ready/Go did not complete for No Contest test");
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   no_contest.tick(pause);phase+=32000;unsigned count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!no_contest.paused();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(no_contest.paused(),"Original P1 Start did not pause the match");
   for(unsigned t=0;t<12;t++){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   const uint32_t no_contest_lras=PAD_TRIGGER_L|PAD_TRIGGER_R|PAD_BUTTON_A|PAD_BUTTON_START;
   pause[0].button=no_contest_lras;
   no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
   check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   /* Keep the exact LRAS+A+Start sample held while the source ending drains;
    * the final HSD history handed back to CSS must be this real state. */
   for(unsigned t=0;t<500&&!no_contest.complete();++t){
    no_contest.tick(pause);phase+=32000;count=phase/60;phase%=60;
    check(melee_web_audio_render(no_contest.audio(),pcm,count,error,sizeof(error)),error);
   }
   int no_contest_winner=-1;
   check(no_contest.complete(),"Original No Contest did not complete its source ending");
   check(no_contest.outcome(no_contest_winner)==OUTCOME_NO_CONTEST&&no_contest_winner==-1,
         "Original No Contest outcome or winner was incorrect");
   uint8_t held_lras_input[MELEE_WEB_PAD_STATE_BYTES];
   melee_web_pad_state_capture(held_lras_input);
   uint32_t no_contest_seed=no_contest.random_seed();
   no_contest.close();
   MatchExitInfo canceled{};
   check(melee_web_match_rules_terminal_data(&canceled),"No Contest Results payload absent");
   std::cout<<"No Contest Results: outcome="<<unsigned(canceled.match_end.outcome)
            <<" winners="<<unsigned(canceled.match_end.n_winners)
            <<" kind="<<unsigned(canceled.match_end.match_kind)
            <<" selected-kind="<<unsigned(selection.start.rules.match_kind)<<'\n';
   check(
         canceled.match_end.outcome==OUTCOME_NO_CONTEST&&
         // Source ranking retains both equal-stock players even though the
         // canceled outcome has no gameplay winner. Do not normalize it.
         canceled.match_end.n_winners==selection.player_count&&
         canceled.match_end.match_kind==selection.start.rules.match_kind,
         "No Contest lost its complete original Results payload");
   if(results_mario_recipe){
    run_results_source_smoke(files,host,canceled,no_contest_seed,held_lras_input);
   }else check(melee_web_menu_host_match_finished(host,no_contest_seed,held_lras_input,error,sizeof(error)),error);

   /* Return immediately to CSS. The explicit Results recipe has handed the
    * source Results PAD back to the host, so let CSS settle on a neutral
    * sample before ordinary Start input selects the next match. The retail
    * recipe keeps its held-LRAS regression exactly as exercised above. */
   world=std::make_unique<melee_web::GameplayMenuWorld>(files);audio_phase=0;
   check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
   if(results_mario_recipe){
    raw[0].button=0;
    for(unsigned t=0;t<120;t++)check(tick()==1,"Results PAD history left CSS route unexpectedly");
   }else{
    raw[0].button=no_contest_lras;
    check(tick()==1,"Held LRAS unexpectedly transitioned CSS after No Contest");
    check(melee_web_menu_host_phase(host)==1,"Held LRAS did not remain in original CSS");
    raw[0].button=0;
    for(unsigned t=0;t<120;t++)check(tick()==1,"CSS release unexpectedly transitioned");
   }
   check(melee_web_menu_host_phase(host)==1,"CSS release left the original CSS route");
   transition();check(melee_web_menu_host_phase(host)==2,"CSS did not choose SSS after No Contest");
   rebuild_menu_scene();
   for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected post-No-Contest SSS transition");
   // Re-entering SSS can initialize a different source hover. Select the
   // requested tile through ordinary raw PAD input for every recipe.
   select_stage();
   transition();check(melee_web_menu_host_phase(host)==5,"SSS did not select the next match after No Contest");
   MeleeWebMenuMatchSelection next_selection{};
   check(melee_web_menu_host_selection(host,&next_selection,error,sizeof(error)),error);
   check(next_selection.start.rules.stkind==selection.start.rules.stkind&&
         next_selection.start.players[0].ckind==selection.start.players[0].ckind,
         "Ordinary CSS/SSS input changed the committed No Contest match selection");
   selection=next_selection;
   world->close();world.reset();audio_phase=0;
  }
  {
  const MeleeWebPadState* input=melee_web_menu_host_input(host);
   check(input!=nullptr,"Original SSS did not retain PAD history for ordinary match entry");
   melee_web::GameplayMatchSession match(files,selection,*input);audio_phase=0;
   MatchExitInfo fresh_result{};
   check(!melee_web_match_rules_terminal_data(&fresh_result),
         "Fresh source match retained a prior Results payload");
   if(!match_entry_recorded){const uint32_t rng=match.random_seed();trace.event("match_enter_complete",match.audio(),nullptr,&selection,&rng);match_entry_recorded=true;}
   check(!match.ready(),"Original match intro was bypassed");
   const auto entry_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++)check(entry_stats.eyes[eye].image_is_base&&
      entry_stats.eyes[eye].image_index==UINT32_MAX&&entry_stats.eyes[eye].palette_is_base&&
      entry_stats.eyes[eye].palette_index==UINT32_MAX,
      "Original Entry eye telemetry did not retain owned base image/palette");
   const float ready_start_x=match.player_stats(0).position[0];
   unsigned intro_ticks=0;
   for(;intro_ticks<600&&!match.ready();++intro_ticks){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(intro_ticks<60)pads[0].stickX=80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    if(intro_ticks<60)check(match.player_stats(0).position[0]==ready_start_x,
                           "Fighter accepted movement before original Ready completion");
   }
   check(match.ready(),"Original Ready/Go did not reach gameplay");
   check(!match.paused(),"Original match entered gameplay already paused");
   const auto active_frame=match.source_frames();
   for(unsigned t=0;t<3;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;match.tick(pads);
    audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.source_frames()>active_frame,"Original active source frame did not advance");
   const auto wait_stats=match.player_stats(0);
   for(unsigned eye=0;eye<2;eye++){
    check(wait_stats.eyes[eye].image_count>0,
          "Original Wait eye telemetry lost its owned animation bounds");
    check((wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].image_is_base&&wait_stats.eyes[eye].image_index<wait_stats.eyes[eye].image_count),
          "Original Wait eye image state is outside its owned base/table representation");
    check((wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index==UINT32_MAX)||
          (!wait_stats.eyes[eye].palette_is_base&&wait_stats.eyes[eye].palette_index<wait_stats.eyes[eye].palette_count),
          "Original Wait eye palette state is outside its owned base/table representation");
   }

   // Start pauses through the original pauser path.  The source scheduler and
   // audio continue to tick, but fighter actions/animation/positions and the
   // match frame must remain held while the pause is debounced.
   PADStatus pause[4]{};pause[2].err=pause[3].err=-1;pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   for(unsigned t=0;t<20&&!match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.paused(),"Original P1 Start did not enter pause");
   const auto paused_frame=match.source_frames();
   const auto paused_player0=match.player_stats(0);
   const auto paused_player1=match.player_stats(1);
   for(unsigned t=0;t<20;t++){
    pause[1].button=t==15?PAD_BUTTON_START:0;
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    pause[1].button=0;
    const auto held0=match.player_stats(0);
    const auto held1=match.player_stats(1);
    check(match.paused()&&match.source_frames()==paused_frame&&
          held0.motion_id==paused_player0.motion_id&&
          held0.animation_frame==paused_player0.animation_frame&&
          held0.position[0]==paused_player0.position[0]&&
          held0.position[1]==paused_player0.position[1]&&held0.stocks==paused_player0.stocks&&
          held1.motion_id==paused_player1.motion_id&&
          held1.animation_frame==paused_player1.animation_frame&&
          held1.position[0]==paused_player1.position[0]&&
          held1.position[1]==paused_player1.position[1]&&held1.stocks==paused_player1.stocks,
          "Wrong-port Start or paused source tick changed the match");
   }
   pause[0].button=PAD_BUTTON_START;
   match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
   check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   pause[0].button=0;
   auto resumed_frame=match.source_frames();
   for(unsigned t=0;t<30&&match.paused();++t){
    match.tick(pause);audio_phase+=32000;count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    resumed_frame=match.source_frames();
   }
   check(!match.paused()&&resumed_frame>paused_frame,
         "Original P1 Start did not resume after pause debounce");
   std::cout<<"Original Ready/Go completed at "<<intro_ticks<<" ticks\n";
   check(match.hud_damage(0)==0&&match.hud_damage(1)==0,"Original player damage HUD did not initialize");
   // Battlefield's authored spawn puts P2 on the upper platform. Use source
   // input to reach the main floor before testing a horizontal projectile.
   for(unsigned t=0;t<180&&
       std::abs(match.player_stats(1).position[1]-
                match.player_stats(0).position[1])>=5.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;pads[1].stickY=-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[1]-
                  match.player_stats(0).position[1])<5.0f,
         "Original input did not bring both fighters to the same stage level");
   // Approach with source input so the opponent lies inside the projectile's
   // actual lifetime and range.
   for(unsigned t=0;t<120&&
       std::abs(match.player_stats(1).position[0]-
                match.player_stats(0).position[0])>=35.0f;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    pads[0].stickX=match.player_stats(1).position[0]>
                           match.player_stats(0).position[0]?80:-80;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(std::abs(match.player_stats(1).position[0]-
                  match.player_stats(0).position[0])<35.0f,
         "Original movement did not reach projectile test range");
   // Let the actual selected fighter projectile hit the opponent, then
   // observe the original HUD consumer catching up to source player damage.
   const float projectile_start_damage=match.player_stats(1).damage_percent;
   for(unsigned t=0;t<240&&
       match.player_stats(1).damage_percent==projectile_start_damage;++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t%8==0)pads[0].button=PAD_BUTTON_B;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   for(unsigned t=0;t<30&&
       match.hud_damage(1)!=int(match.player_stats(1).damage_percent);++t){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
   }
   check(match.player_stats(1).damage_percent>projectile_start_damage&&
         match.hud_damage(1)==int(match.player_stats(1).damage_percent),
         "Original HUD did not display actual projectile damage");
   bool lost=false,jump=false;int stocks=4,respawns=0,winner=-1;unsigned t=0;
   unsigned ending_ticks=0;MeleeWebMatchStats held_players[2]{};
   for(;t<4000;t++){
    PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
    if(t>=20&&!lost)pads[0].stickX=80;if(jump)pads[0].button=PAD_BUTTON_X;
    match.tick(pads);audio_phase+=32000;unsigned count=audio_phase/60;audio_phase%=60;
    check(melee_web_audio_render(match.audio(),pcm,count,error,sizeof(error)),error);
    auto player=match.player_stats(0);check(match.player_stats(1).stocks==4,"Stationary match opponent lost a stock");
    jump=!lost&&stocks<4&&player.ground_or_air==0&&player.position[0]>65;
    if(player.stocks<stocks){lost=true;stocks=player.stocks;}
    if(lost&&player.motion_id==14&&player.ground_or_air==0){lost=false;++respawns;}
    const int outcome=match.outcome(winner);
    if(match.ending()){
     check(outcome!=0,"Original ending started before the source outcome");
     for(unsigned slot=0;slot<2;++slot){
      const auto current=match.player_stats(slot);
      if(ending_ticks){
       check(current.motion_id==held_players[slot].motion_id&&
             current.animation_frame==held_players[slot].animation_frame&&
             current.position[0]==held_players[slot].position[0]&&
             current.position[1]==held_players[slot].position[1]&&
             current.stocks==held_players[slot].stocks,
             "Fighter processes advanced during original GAME freeze");
      }else held_players[slot]=current;
     }
     ++ending_ticks;
    }
    if(match.complete()){check(outcome!=0&&ending_ticks>0,"Source exit skipped the original ending");break;}
   }
   check(t<4000&&stocks==0&&respawns==3,"Native menu match did not complete original four-stock outcome");
   std::cout<<"Original GAME ending and source transition completed across "<<ending_ticks<<" frozen ticks\n";
   uint8_t final_input[MELEE_WEB_PAD_STATE_BYTES];
   melee_web_pad_state_capture(final_input);
   int timer_before_close=0;
   GetMatchTimer(&timer_before_close);
   MatchExitInfo published_once{},published_twice{};
   check(melee_web_match_rules_publish_result(),
         "Completed source match did not publish its Results payload");
   check(melee_web_match_rules_terminal_data(&published_once),
         "Completed source match did not retain its first Results payload");
   check(melee_web_match_rules_publish_result(),
         "Repeated source Results publication was not idempotent");
   check(melee_web_match_rules_terminal_data(&published_twice)&&
         std::memcmp(&published_once,&published_twice,sizeof(published_once))==0,
         "Repeated source Results publication changed the retained payload");
   uint32_t seed=match.random_seed();match.close();
   int terminal_outcome=0,winner_count=0,winners[6]{};
   check(melee_web_match_rules_terminal_result(&terminal_outcome,&winner_count,winners)&&
         terminal_outcome==OUTCOME_ELIMINATION&&winner_count==1&&winners[0]==1,
         "Native menu match did not publish the original elimination winner at close");
   MatchExitInfo result{};
   check(melee_web_match_rules_terminal_data(&result)&&
         result.x0==selection.start.rules.x18&&result.x4==uint32_t(timer_before_close)&&
         result.match_end.outcome==terminal_outcome&&
         result.match_end.match_kind==selection.start.rules.match_kind&&
         result.match_end.frame_count>0&&result.match_end.n_winners==1&&
         result.match_end.winners[0]==1,
         "Original VS OnExit did not retain complete Results metadata");
   for(unsigned slot=0;slot<selection.player_count;++slot){
    const auto& player=result.match_end.player_standings[slot];
    check(player.ckind==selection.start.players[slot].ckind&&
          player.slot_type==selection.start.players[slot].slot_type&&
          player.x3==selection.start.players[slot].color&&
          player.stocks==(slot==0?0:4),
          "Original Results player identity or final stocks were lost at teardown");
   }
   if(results_mario_recipe){
    run_results_source_smoke(files,host,result,seed,final_input);
   }else check(melee_web_menu_host_match_finished(host,seed,final_input,error,sizeof(error)),error);
  }
  world=std::make_unique<melee_web::GameplayMenuWorld>(files);audio_phase=0;
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
  for(unsigned t=0;t<120;t++)check(tick()==1,"Unexpected return CSS transition");
  check(melee_web_menu_host_leave(host,1,error,sizeof(error)),error);
  world->verify_immutable_archives();world->close();world->close();world.reset();
  check(melee_web_menu_host_destroy(host,error,sizeof(error)),error);
  const auto retained=melee_web_gameplay_allocation();
  check(retained.identity==session_allocation.identity&&
        retained.generation==session_allocation.generation&&
        retained.bytes==session_allocation.bytes&&!melee_web_gameplay_world_exists(),
        "Menu/match teardown replaced the application's retained source arena");
 }
 check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
 if(sd_menu_setup_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  trace.event("menu_setup_application_cleanup_complete",nullptr);
  std::cout<<"Native closed CSS/SSS raw and normalized selection diagnostic with host/VS/application cleanup passed; no tie or SD world\n";
 }else if(css_observer_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native single CSS observer reducer and checked application cleanup completed; no route claim\n";
 }else if(sparse_css_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native actual source CSS P1/P3 join and closed SSS identity/cleanup passed; no match/browser/original comparison claim\n";
 }else if(sparse_pad_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native authored sparse source PAD/checked owner cleanup passed; no CSS joining, Results, browser or original comparison claim\n";
 }else if(ordinary_timeout_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<"Native natural non-tied ordinary timeout/typed Results/cache/cleanup passed; no browser or original comparison\n";
 }else if(sudden_death_world_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  std::cout<<(resolve_sd_recipe?"Native natural SD resolution and application session reacquisition passed; ":"Native actual SD lifecycle prefix and application session reacquisition passed; ")
           <<(natural_sd_recipe?"fixture-controlled natural VS timeout only, ":"constructed tie only, ")
             <<(resolve_sd_recipe?"native functional Results/CSS only, no original comparison or browser acceptance\n":"no Results or gameplay acceptance claim\n");
 }else if(link_css_unload_recipe)
  std::cout<<"Native Link/Young Link CSS audio registry and unload smoke passed; no match/rendered claim\n";
 else if(sudden_death_host_recipe)
  std::cout<<"Native menu-host Sudden Death callback handoff passed through source CSS/SSS; "
              "constructed callback control only, no GameplayMatchSession or Results-scene claim\n";
 else if(returned_menu_recipe){
  check(melee_web_gameplay_session_begin(32U*1024U*1024U,session_error,sizeof(session_error)),session_error);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
  trace.event("returned_menu_application_cleanup_complete",nullptr);
  std::cout<<"Reduced native original Results/returned menu ownership control complete; explicitly constructed terminal, no gameplay\n";
 }
 else if(results_mario_recipe)
  std::cout<<"Native source Mario Results smoke (No Contest and elimination) returned to CSS; no retail/rendered claim\n";
 else
  std::cout<<"Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice; no browser or equivalence claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
