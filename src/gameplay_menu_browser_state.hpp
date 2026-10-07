// Internal to the gameplay_menu_browser*.cpp translation units; not an API.
// The browser runtime has one owner of its source sessions. Its state is
// defined once, in its original order, in gameplay_menu_browser.cpp and is
// shared here with the per-scene export surfaces and the JSON observers.
// Every unit keeps the original include set so name lookup is unchanged.
#pragma once
#include "gameplay_menu_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_save_profile.h"
#include "gameplay_match_session.hpp"
#include "gameplay_results_session.hpp"
#include "results_source_pad_schedule.hpp"
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
#include "gameplay_results_entry_packet.hpp"
#endif
#include "gameplay_prize_session.hpp"
extern "C" const char* melee_web_native_menu_match_observe();
extern "C" const char* melee_web_native_menu_source_observe();
extern "C" const char* melee_web_native_menu_memory();
extern "C" int melee_web_native_menu_phase();
extern "C" void melee_web_cpu_observation_set_event_cursor(size_t index);
extern "C" void melee_web_cpu_observation_scheduler_return(void);
extern "C" {
#include <melee/gm/types.h>
#include <melee/mn/mnitemsw.h>
#include <melee/ty/toy.h>
#include <sysdolphin/baselib/controller.h>
extern ResultsData lbl_8046DBE8;
}
#include "gameplay_match_rules.h"
#include "gameplay_bootstrap.h"
#include "gameplay_retail_recipe.hpp"
#include "gameplay_net_input.h"
#include "gameplay_replay_completion_policy.hpp"
#include "../tests/native_menu_fighter_input.h"
#include "../tests/native_menu_stage_input.h"
#include "gameplay_audio_stream.h"
#include "runtime_archive_cache.hpp"
#include "runtime_asset_scope.hpp"
#include "gameplay_asset_manifest.hpp"
#include "gameplay_source_files.h"
#include "gameplay_content.h"
#include "menu_preparation_state.hpp"
#include "browser_input.h"
#include "animation_clock.hpp"
#include "source_frame_sequence.hpp"
#include "gameplay_pipeline_preparation.hpp"
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
#include "melee_pipeline_seed_identity.h"
#endif
#include <aurora/aurora.h>
#include <aurora/event.h>
#include <aurora/main.h>
#include <aurora/gfx.h>
#include <aurora/pipeline_prepare.h>
#include <dolphin/gx.h>
#include <dolphin/vi.h>
#include <emscripten.h>
#include <emscripten/heap.h>
#include <malloc.h>
#include <SDL3/SDL_hints.h>
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance_runtime.h"
#endif
// Defined in gameplay_menu_browser_diagnostics.cpp. The CSS/SSS drivers queue
// their samples through the same checked raw-PAD export.
extern "C" int melee_web_native_menu_pad_sample_full(unsigned port,unsigned buttons,int stick_x,int stick_y,
                                                     int cstick_x,int cstick_y,unsigned trigger_l,
                                                     unsigned trigger_r,unsigned duration);
// Formerly an anonymous namespace. Each unit adds a using-directive at file
// scope, so unqualified lookup from the C exports is the same as before.
namespace melee_web_menu_browser {
// Source asset scope and owners.
extern melee_web::RuntimeFiles files;
extern melee_web::RuntimeAssetScope asset_scope;
enum class AssetDestination {
 None, InitialMenu, Match, ReturnMenu, Replay, Results, Prize,
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 StadiumC1a,
#endif
 OpeningScene, OpeningMatch, TitleReturn
};
extern AssetDestination asset_destination;
extern bool scoped_disc_import;
extern bool scoped_assets,asset_committed;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern bool stadium_c1a_armed;
extern std::string stadium_c1a_observation;
extern bool asset_selection_valid;
extern MeleeWebMenuMatchSelection asset_selection;
#endif
extern uint32_t asset_generation;
extern std::vector<std::string> requested_assets;
extern std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
extern std::unique_ptr<melee_web::GameplayMenuWorld> world;
extern bool source_session_owned;
extern std::unique_ptr<melee_web::GameplayMatchSession> match;
extern std::unique_ptr<melee_web::GameplayResultsSession> results;
extern std::unique_ptr<melee_web::GameplayPrizeSession> prize;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
extern melee_web::ResultsEntryPacket results_entry_packet;
#endif
extern std::string terminal_match_observation;
extern std::string match_observer_error;
extern MeleeWebFighterInputObservation last_css_fighter_observation;
extern int last_css_fighter_target,last_css_fighter_drive_state;
extern bool last_css_fighter_observation_valid;
extern int css_fighter_release_port;
// Reference replay.
extern std::unique_ptr<melee_web::RetailReplayRecipe> replay;
extern size_t replay_cursor;
extern melee_web::SourceFrameSequence replay_source_frames;
extern melee_web::ReplayCompletionState replay_completion;
extern bool replay_trace,replay_pending,replay_final_draw;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
// Development Results PAD trace, retained across teardown.
struct ResultsPadTraceRow {
 uint32_t source_frame;
 bool tick_returned;
 bool results_state_sampled;
 uint32_t results_state_frame;
 uint8_t results_phase;
 uint8_t results_stats_phase;
 uint8_t results_num_pages;
 uint8_t results_player_pages[4];
 uint8_t results_player_confirmed[4];
 PADStatus pads[4];
 HSD_PadStatus source_consumed_pads[4];
};
constexpr size_t kResultsPadTraceCapacity=8192;
extern std::array<ResultsPadTraceRow,kResultsPadTraceCapacity> results_pad_trace;
extern size_t results_pad_trace_count;
extern uint64_t results_pad_trace_attempts;
extern bool results_pad_trace_overflow;
extern MeleeWebResultsCameraEntrySnapshot results_camera_entry_snapshot;
#endif
extern bool reference_heap_used;
extern unsigned reference_menu_preparations;
extern MeleeWebMenuHost* host;
extern int configured_save_mode;
extern std::vector<uint8_t> configured_save_profile;
// Scheduling, status and diagnostics.
extern melee_web::FixedTickClock menu_clock;
extern std::string message;
extern std::string match_message;
extern bool running,pending,host_entered,world_exposed,faulted;
extern melee_web::MenuPreparationState preparation;
extern unsigned diagnostic_start_ticks;
extern int stock_check,stock_count,stock_respawns;
extern unsigned stock_tick,completed_matches;
extern bool stock_lost,stock_jump;
extern bool menu_scene_rebuild_pending;
extern unsigned render_frame;
extern PADStatus diagnostic_pad;
extern unsigned diagnostic_pad_port,diagnostic_pad_remaining;
extern melee_web::ResultsSourcePadSchedule scheduled_results_pad;
extern melee_web::ResultsSourceFramePauseSchedule scheduled_results_pauses;
inline void check(int value,const char* error){if(!value)throw std::runtime_error(error);}
void begin_source_session();
void close();
void request_assets(AssetDestination destination,
                    const MeleeWebMenuMatchSelection* selection=nullptr,
                    const MeleeWebOpeningPreview* opening_preview=nullptr,
                    int opening_state=-1);
void enter_world();
void diagnostic_incident(int reason,double value=0,double threshold=0,int clock_owner=0);
AuroraStats aurora_stats_snapshot();
int32_t stat_delta(uint64_t after,uint64_t before);
void report_construction(const char* kind,double started,double constructed,double entered,
                         const AuroraStats& before,const AuroraStats& after);
std::string selected_match_message(const MeleeWebMenuMatchSelection& selection);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
MeleeWebPipelineSourceContext pipeline_context(uint32_t phase_override=0,uint32_t scene_override=0);
#endif
// report() is a JSON observer in gameplay_menu_browser_observers.cpp.
struct PreparationProfile {
 double requested_at=0,audio_ready_at=0,constructed_at=0;
 double render_cpu_ms=0,max_callback_ms=0,max_draw_ms=0,max_end_ms=0;
 double submission_wait_started=0,submission_ready_at=0;
 unsigned submission_wait_callbacks=0,pending_staging_at_settle=0,pending_staging_at_first_arm=0;
 bool submission_polled=false;
 uint64_t texture_upload_bytes=0;
 unsigned callbacks=0,source_draws=0,max_draw_calls=0,max_queued=0;
 int32_t queued_delta=0,created_delta=0;
 bool source_transition=false;
 void begin(bool transition,double now) noexcept {
  *this={};requested_at=now;source_transition=transition;
 }
 void construction_started(double now) noexcept {audio_ready_at=now;}
 void construction_finished(double now) noexcept {constructed_at=now;}
 void observe(double callback_ms,double draw_ms,double end_ms,bool source_draw,
              const AuroraStats& before,const AuroraStats& after) noexcept {
  ++callbacks;source_draws+=source_draw?1U:0U;
  render_cpu_ms+=callback_ms;max_callback_ms=std::max(max_callback_ms,callback_ms);
  max_draw_ms=std::max(max_draw_ms,draw_ms);max_end_ms=std::max(max_end_ms,end_ms);
  texture_upload_bytes+=after.lastTextureUploadSize;
  max_draw_calls=std::max(max_draw_calls,after.drawCallCount);
  max_queued=std::max(max_queued,after.queuedPipelines);
  queued_delta+=stat_delta(after.queuedPipelines,before.queuedPipelines);
  created_delta+=stat_delta(after.createdPipelines,before.createdPipelines);
 }
 void report(double settled_at) const;
};
extern PreparationProfile preparation_profile;
#if !defined(MELEE_WEB_PUBLIC_RUNTIME)
// One development callback for window.menuRuntimeTiming. tick() samples every
// value; publish_runtime_timing() in the observers unit only formats it.
struct RuntimeTimingSample {
 double started;
 int timing_valid;
 int first_use;
 double input_done;
 double simulation_cpu_ms;
 double preparation_ms;
 const AuroraStats& callback_begin_stats;
 double render_begin_ms;
 double render_draw_ms;
 double render_end_ms;
 double finished;
 const AuroraStats& callback_end_stats;
 int began;
 int drawn;
 const AuroraStats& stats_before;
 const AuroraStats& stats_after;
 uint32_t callback_draw_calls;
 uint32_t callback_texture_upload;
 uint32_t callback_staging_used;
 int suppress_draw;
 const melee_web::SourceFrameSequence& source_frames;
};
void publish_runtime_timing(RuntimeTimingSample s);
#endif
}
