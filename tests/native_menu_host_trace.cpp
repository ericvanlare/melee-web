#include "gameplay_menu_world.hpp"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_content.h"
#include <algorithm>
#include "gameplay_menu_host.h"
#include "gameplay_save_profile.h"
#include "gameplay_match_session.hpp"
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "dat_archive.hpp"
#include "dat_effect_banks.hpp"
#include "dat_native_stage.hpp"
#include "dat_scene.hpp"
#include "dat_stage.hpp"
#include "dat_stage_items.hpp"
#include "dat_stage_yaku.hpp"
#include "gameplay_effect_banks.h"
#include "gameplay_ground_data.h"
#include "gameplay_stage_map.h"
#include "native_dat.hpp"
#include "stadium_c0_native_map_contract.hpp"
#include "stadium_c1_e8_call_observer.h"
#endif
#include <melee/ft/forward.h>
#include <melee/gm/forward.h>
extern "C" {
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gm_1A3F.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/gmresultplayer.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lbfile.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnmain.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include <melee/gr/grdatfiles.h>
#include <melee/gr/stage.h>
#endif
#include <melee/ty/forward.h>
#include <melee/ty/toy.h>
#include <melee/ty/types.h>
extern HSD_Archive* _Toy_sbss_804D6ED0;
}
#include <melee/gr/forward.h>
#include <sysdolphin/baselib/random.h>
extern "C" {
#include <melee/lb/lb_013B.h>
#include <sysdolphin/baselib/rumble.h>
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
}
#include <bit>
#include <cmath>
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
#include <vector>
extern "C" int melee_web_vs_mode_begin(void);
extern "C" int melee_web_vs_mode_end(void);
extern "C" int melee_web_vs_mode_select_state(int);
extern "C" int melee_web_vs_mode_set_route(int current_mode, int previous_mode);
extern "C" void* melee_web_current_scene_info(void);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern "C" int melee_web_stage_selection_begin(int stage_kind);
extern "C" int melee_web_stage_selection_end(void);
#endif
static void check(int value,const char* error){if(!value){std::cerr<<"Check failed before teardown: "<<error<<"\n";throw std::runtime_error(error);}}

namespace {
std::string hex32(uint32_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(8)<<value;return out.str();}
std::string hex64(uint64_t value){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return out.str();}
std::string stream_name(MeleeWebAudio* audio){
 const char* path=melee_web_audio_stream_path(audio);if(!path)return {};
 std::string result(path);const auto slash=result.find_last_of("/\\");return slash==std::string::npos?result:result.substr(slash+1);
}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
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

void check_stadium_selection_preserved(
    MeleeWebMenuHost* host,
    const MeleeWebMenuMatchSelection& expected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>&
        expected_baseline)
{
    check(host != nullptr &&
              melee_web_menu_host_phase(host) == MELEE_WEB_MENU_READY &&
              melee_web_menu_host_source_scene(host) == 0,
          "C1 context preflight changed the closed source SSS selection");
    MeleeWebMenuMatchSelection observed{};
    char error[256]{};
    check(melee_web_menu_host_stadium_c1a_selection(
              host, &observed, error, sizeof(error)), error);
    check(observed.start.rules.stkind == St_Kind_PStadium &&
              std::memcmp(&observed.start, &expected.start,
                          sizeof(expected.start)) == 0,
          "C1 context preflight changed the source-selected StKind 3 payload");
    check(std::memcmp(observed.players, expected.players,
                      sizeof(expected.players)) == 0 &&
              observed.player_count == expected.player_count &&
              observed.random_seed == expected.random_seed &&
              observed.hud_layout == expected.hud_layout &&
              observed.unlocked_characters == expected.unlocked_characters &&
              observed.unlocked_stages == expected.unlocked_stages &&
              observed.save_profile_present == expected.save_profile_present &&
              observed.opening_demo == expected.opening_demo,
          "C1 context preflight changed retained menu save or RNG provenance");
    std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES> baseline{};
    check(melee_web_menu_host_snapshot_card_data(
              host, 1, baseline.data(), baseline.size(), error,
              sizeof(error)), error);
    check(baseline == expected_baseline,
          "C1 context preflight changed the retained save-owner baseline");
}
#endif
void run_results_source_smoke(const melee_web::RuntimeFiles& files,
                              MeleeWebMenuHost* host,
                              const MatchExitInfo& exit_info,
                              uint32_t& seed,
                              uint8_t input_bytes[MELEE_WEB_PAD_STATE_BYTES])
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
    std::unique_ptr<MeleeWebPadState,decltype(&melee_web_pad_state_free)> decoded(
        melee_web_pad_state_decode(input_bytes,MELEE_WEB_PAD_STATE_BYTES,error,sizeof(error)),
        melee_web_pad_state_free);
    check(decoded!=nullptr,error);
    melee_web::GameplayResultsSession session(files,result,seed,*decoded);
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
    for(unsigned t=0;t<600&&!session.requested();t++){
        PADStatus pads[4]{};pads[2].err=pads[3].err=-1;
        // The original Results confirmation is Start; A changes stats pages.
        if(t%90==0)pads[0].button=pads[1].button=PAD_BUTTON_START;
        tick(pads);
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
 void begin_run(unsigned run){run_=run;index_=0;epochs.clear();}
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

void run_stadium_e8_request(
    const melee_web::RuntimeFiles& reopened_files,
    MeleeWebMenuHost* host,
    melee_web::GameplayMenuWorld* world,
    const MeleeWebMenuMatchSelection& selected,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& baseline,
    const std::array<std::uint8_t, MELEE_WEB_SAVE_PROFILE_CARD_BYTES>& save_before,
    TransitionTrace& trace)
{
    using namespace melee_web;
    char error[256]{};
    check(host && world && reopened_files.contains("GrPs.usd"),
          "E8 request requires its retained source host and exact GrPs.usd entry");
    const auto& raw_bytes = reopened_files.at("GrPs.usd");
    const std::vector<std::uint8_t> raw_before = raw_bytes;
    auto archive = std::make_shared<const DatArchive>(
        raw_bytes, DatExternalPolicy::ResolveNull);
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
    void* previous_ground_param = nullptr;
    bool ground_param_published = false;
    bool effect_bank_attached = false;
    bool stage_selection_owned = false;
    bool observer_window_owned = false;
    bool cleanup_complete = false;
    std::unique_ptr<NativeDatArena> scalar_owner;
    std::unique_ptr<DatNativeMap> map_owner;
    std::unique_ptr<DatStageYaku> random_yaku;
    std::unique_ptr<DatEffectBanks> effects;
    std::unique_ptr<DatScene> quake;
    std::unique_ptr<DatStageItems> items;
    MeleeWebStadiumE8CallObservation observed{};
    MeleeWebStadiumC1StageInfoView before_view{};
    MeleeWebStadiumC1StageInfoView after_view{};
    void* ground_data = nullptr;
    void* yakumono_data = nullptr;
    uint32_t seed_before = 0;
    const uint32_t* seed_owner = seed_ptr;
    check(seed_owner != nullptr,
          "E8 request lost the source seed owner before preparation");
    seed_before = *seed_owner;
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
        yakumono_data = melee_web_stadium_yakumono_decode(
            scalar_owner->reader(), yakumono_root);
        check(ground_data != nullptr && yakumono_data != nullptr,
              "C0 typed scalar owners did not decode the Stadium roots");
        map_owner = std::make_unique<DatNativeMap>(
            archive, test::stadium_contract);
        check(map_owner->map_head() != nullptr && map_owner->collision() != nullptr,
              "C0 typed map owner did not decode map_head/coll_data");
        random_yaku = std::make_unique<DatStageYaku>(archive, yaku_root);
        check(random_yaku->native_data() != nullptr,
              "C0 ALDYakuAll typed owner is absent");
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

        const std::vector<MeleeWebArchiveSymbol> symbols{
            {"GrPs.usd", "map_head", map_owner->map_head()},
            {"GrPs.usd", "coll_data", map_owner->collision()},
            {"GrPs.usd", "grGroundParam", ground_data},
            {"GrPs.usd", "ALDYakuAll", random_yaku->native_data()},
            {"GrPs.usd", "map_ptcl", effects->command_root()},
            {"GrPs.usd", "map_texg", effects->texture_root()},
            {"GrPs.usd", "yakumono_param", yakumono_data},
            {"GrPs.usd", "quake_model_set", quake->single_model()},
        };
        stage_map = melee_web_stage_map_publish(
            map_owner->map_head(), error, sizeof(error));
        check(stage_map != nullptr, error);
        check(melee_web_stage_map_set_public(
                  stage_map, symbols.data(), symbols.size(), error,
                  sizeof(error)), error);
        previous_ground_param = melee_web_ground_data_publish(ground_data);
        ground_param_published = true;
        check(previous_ground_param == before_view.param,
              "GroundParam publication did not retain the prior StageInfo owner");
        check(melee_web_effect_bank_attach(
                  effects->bank(), error, sizeof(error)), error);
        effect_bank_attached = true;
        check(melee_web_stage_selection_begin(St_Kind_PStadium),
              "Could not scope original StageInfo selection for StKind 3");
        stage_selection_owned = true;
        check(gm_GetCurrentGameMode() == GM_VS && !gm_IsCurrently1PMode() &&
                  lbLang_GetLanguageSetting() == LANG_US &&
                  lbLang_GetSavedLanguage() == LANG_US,
              "E8 request lost its source VS and two-language scopes");
        check_stadium_selection_preserved(host, selected, baseline);

        check(melee_web_stadium_e8_call_observer_begin(),
              "Could not open the bounded E8 source-call window");
        observer_window_owned = true;
        Stage_802251E8(St_Kind_PStadium, NULL);
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
                  observed.map_head_value == map_owner->map_head() &&
                  observed.coll_data_calls == 1 &&
                  observed.coll_data_value == map_owner->collision() &&
                  observed.ground_param_calls == 0 &&
                  observed.itemdata_calls == 0 &&
                  observed.ald_yaku_all_calls == 1 &&
                  observed.ald_yaku_all_value == random_yaku->native_data() &&
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
                  after_view.coll_data == map_owner->collision() &&
                  after_view.ald_yaku_all == random_yaku->native_data() &&
                  after_view.map_ptcl == effects->command_root() &&
                  after_view.map_texg == effects->texture_root() &&
                  after_view.yakumono_param == yakumono_data &&
                  after_view.quake_model_set == quake->single_model(),
              "Source StageInfo did not retain the checked typed owner pointers");
        check(after_view.itemdata == before_view.itemdata &&
                  after_view.map_plit == before_view.map_plit &&
                  after_view.itemdata == nullptr && after_view.map_plit == nullptr,
              "E8 request changed the source-authored empty item/light roots");
        check(melee_web_stadium_c1_stage_object_failures() == 0,
              "E8 request entered stage objects, Ground, item, or light state");
        check_stadium_selection_preserved(host, selected, baseline);
        check(seed_ptr == seed_owner && *seed_ptr == seed_before,
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
        trace.event("stadium_e8_request_returned", world->audio(),
                    "typed-catalog-request-only", &selected, &seed_before);
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
        check(seed_ptr == seed_owner && *seed_ptr == seed_before,
              "E8 teardown changed the retained source seed owner or value");
        std::cout << "{\"probe\":\"stadium-e8-request\","
                     "\"scope\":\"one original E8 request and checked typed teardown only\","
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
                     "\"stage_objects_started\":false,"
                     "\"checked_teardown\":true}\n";
    } catch (...) {
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

void run_stadium_c1_context_preflight(
    const melee_web::RuntimeFiles& menu_files,
    MeleeWebMenuHost*& host,
    std::unique_ptr<melee_web::GameplayMenuWorld>& world,
    const MeleeWebMenuMatchSelection& selected,
    const std::vector<std::string>& selected_names,
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    bool perform_e8_request,
    TransitionTrace& trace)
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
    world->verify_immutable_archives();
    world->close();
    world.reset();

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
        world->verify_immutable_archives();
        check_stadium_preflight_stage_empty();

        if (perform_e8_request) {
            run_stadium_e8_request(reopened_files, host, world.get(), selected,
                                   baseline, save_before, trace);
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
        check_stadium_selection_preserved(host, selected, baseline);
        check((Toy_804A284C[3] & 4) != 0,
              "C1 context teardown changed the retained Toy category baseline");
        cleanup();
        check(host == nullptr && !melee_web_source_files_active(),
              "C1 context preflight did not release host and source-file owners");
        if (perform_e8_request) {
            std::cout << "C1 reopened-context lifecycle preflight and one E8 typed request passed; "
                         "no stage object or source menu entry\n";
        } else {
            std::cout << "C1 reopened-context lifecycle preflight passed; no E8 request, "
                         "stage publication, or source menu entry\n";
        }
    } catch (...) {
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
    const std::filesystem::path& menu_dir,
    const std::filesystem::path& game_dir,
    TransitionTrace& trace)
{
    char error[256]{};
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
        check(melee_web_menu_host_leave(host, 0, error, sizeof(error)), error);
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
            e8_request_trace, trace);
    } else {
        world->verify_immutable_archives();
        world->close();
        world.reset();
        check(melee_web_menu_host_destroy(host, error, sizeof(error)), error);
        host = nullptr;
    }
    check(!melee_web_menu_stage_explicit_confirm_available(St_Kind_PStadium),
          "C1a explicit-confirm permission survived unload");
    if (e8_request_trace) {
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
 const bool v10_css_replay_start_recipe=input_recipe&&
     std::string(input_recipe)=="whole-session-css-replay-start-v10-v1";
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 const bool stadium_c1a_recipe=input_recipe&&std::string(input_recipe)=="stadium-c1a-v1";
 const bool stadium_c1_context_preflight_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-c1-context-preflight-v1";
 const bool stadium_e8_request_recipe=input_recipe&&
     std::string(input_recipe)=="stadium-e8-request-v1";
#else
 const bool stadium_c1a_recipe=false;
 const bool stadium_c1_context_preflight_recipe=false;
 const bool stadium_e8_request_recipe=false;
#endif
 if(input_recipe&&!retail_fd_recipe&&!results_mario_recipe&&!link_css_unload_recipe&&
    !title_main_abort_recipe&&!opening_movie_preload_recipe&&!trophy_baseline_recipe&&
    !sound_settings_recipe&&!stadium_c1a_recipe&&
    !stadium_c1_context_preflight_recipe&&!stadium_e8_request_recipe&&
    !v10_css_replay_start_recipe)
    throw std::runtime_error("Unknown transition input recipe");
 if(v10_css_replay_start_recipe&&
    (argc!=8||!replay_recipe_path||!trace_path||!source_revision))
   throw std::runtime_error("MWRC v10 CSS replay-start reducer requires trace, source revision and exact recipe path");
 if(!v10_css_replay_start_recipe&&argc==8)
   throw std::runtime_error("Only the MWRC v10 CSS replay-start reducer accepts an exact recipe path");
 if((retail_fd_recipe||results_mario_recipe||v10_css_replay_start_recipe)&&
    stage_kind!=St_Kind_Last)
   throw std::runtime_error("Explicit FD recipes require Final Destination");
 if((stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
     stadium_e8_request_recipe)&&
    stage_kind!=St_Kind_PStadium)
   throw std::runtime_error("C1a recipes require source StKind 3");
 TransitionTrace trace(trace_path,source_revision,input_recipe);
 melee_web::RuntimeFiles files;
 std::vector<std::string> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","GrNBa.dat","GrSt.dat","hyaku.hps","hyaku2.hps","sp_zako.hps","ystory.hps","ItCo.usd","EfMrData.dat","EfFxData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","fox.ssm","falco.ssm","mars.ssm","drmario.ssm","emblem.ssm","pupupu.ssm","dsp_coef.bin","sislib_font.bin"};
 if(stadium_c1a_recipe||stadium_c1_context_preflight_recipe||
    stadium_e8_request_recipe||
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
    stadium_e8_request_recipe){
  run_stadium_c1a_selection_smoke(
      files, stadium_c1_context_preflight_recipe||stadium_e8_request_recipe,
      stadium_e8_request_recipe, argv[1], argv[2], trace);
  check(melee_web_gameplay_session_end(session_error,sizeof(session_error)),session_error);
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
 const unsigned cycle_count=results_mario_recipe?1:2;
 for(unsigned cycle=0;cycle<cycle_count;cycle++){
  trace.begin_run(cycle);
  if((retail_fd_recipe||results_mario_recipe)&&cycle==0)*seed_ptr=1840631306u;
  char error[256]{};auto* host=melee_web_menu_host_create(error,sizeof(error));check(host!=nullptr,error);
  auto world=std::make_unique<melee_web::GameplayMenuWorld>(files);
  check(melee_web_menu_host_enter(host,world->audio(),error,sizeof(error)),error);
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
 if(link_css_unload_recipe)
  std::cout<<"Native Link/Young Link CSS audio registry and unload smoke passed; no match/rendered claim\n";
 else if(results_mario_recipe)
  std::cout<<"Native source Mario Results smoke (No Contest and elimination) returned to CSS; no retail/rendered claim\n";
 else
  std::cout<<"Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice; no browser or equivalence claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
