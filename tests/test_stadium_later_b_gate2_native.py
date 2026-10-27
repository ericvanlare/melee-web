"""Focused controls for the actual private Later-B MWB2 native boundary.

The C++ fixture extracts production parser, owner-identity, GO-validator,
status-serializer, first-error selection, and abort-predicate bodies. Its stopped match/host services
are deliberately modeled leaves; it never runs a source action, native close,
or browser callback and cannot establish runtime admission or gameplay success.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import unittest


SOURCE_ROOT = Path(os.environ.get(
    "MELEE_LATER_B_SOURCE_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
sys.path.insert(0, str(SOURCE_ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


CPP_PATH_ENV = "MELEE_LATER_B_GATE2_NATIVE_CPP"
PROJECTION_ENV = "MELEE_LATER_B_GATE2_PROJECTION"
LONG_PROJECTION_ENV = "MELEE_LATER_B_LONG_PROJECTION"
RUN_ROOT_ENV = "MELEE_LATER_B_GATE2_RUN_ROOT"
CLANGXX_ENV = "MELEE_LATER_B_GATE2_CLANGXX"


def _function(source: str, definition: str) -> str:
    """Return one actual C++ definition, balancing braces and comments."""
    if source.count(definition) != 1:
        raise AssertionError(f"expected one production definition: {definition}")
    start = source.index(definition)
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body: {definition}")
    depth = 0
    state = "code"
    index = opening
    while index < len(source):
        char = source[index]
        nxt = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and nxt == "*":
                state = "comment"
                index += 1
            elif char == "/" and nxt == "/":
                state = "line_comment"
                index += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[start:index + 1]
        elif state == "string":
            if char == "\\":
                index += 1
            elif char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                index += 1
            elif char == "'":
                state = "code"
        elif state == "comment":
            if char == "*" and nxt == "/":
                state = "code"
                index += 1
        elif state == "line_comment" and char == "\n":
            state = "code"
        index += 1
    raise AssertionError(f"unterminated production definition: {definition}")


PRELUDE = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#define MELEE_WEB_STADIUM_SETUP_CONSTRUCTOR_PHASE_CAPACITY 5U

#include "stadium_later_reference.hpp"

static void check(bool condition, const char* error) {
  if (!condition) throw std::runtime_error(error ? error : "native check refused");
}

namespace melee_web::stadium_first_css_diagnostic {
static std::string hex32(uint32_t value) {
  static const char digits[] = "0123456789abcdef";
  std::string out(8, '0');
  for (int i = 7; i >= 0; --i) { out[static_cast<size_t>(i)] = digits[value & 15U]; value >>= 4; }
  return out;
}
}

enum {
  MELEE_WEB_STADIUM_SETUP_PHASE_ENTRY=1,
  MELEE_WEB_STADIUM_SETUP_PHASE_PARAMETERS=2,
  MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL=3,
  MELEE_WEB_STADIUM_SETUP_PHASE_UP_RETURN=4,
  MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_CALL=5,
  MELEE_WEB_STADIUM_SETUP_PHASE_DOWN_RETURN=6,
  MELEE_WEB_STADIUM_SETUP_PHASE_COMMIT=7,
};
struct MeleeWebStadiumSetupConstructorPhaseEvent {
  uint32_t ordinal=0, phase=0, parameter_values_valid=0;
  int32_t parameter_x0=0, parameter_x4=0;
  uint32_t scalar_value_valid=0;
  int32_t scalar_value=0;
  uint32_t source_seed_valid=0, source_seed=0;
};
struct MeleeWebStadiumSetupSnapshot {
  uint64_t world_generation = 0, world_ticks = 0;
  uint32_t source_frame = 0;
  int32_t map_id = 2, initial_countdown = 0, current_countdown = 0;
  unsigned constructor_callback_count = 0;
  unsigned constructor_randi_call_count = 0;
  int parameter_x0 = 0, parameter_x4 = 0;
  unsigned constructor_random_bound_valid = 0;
  int32_t constructor_random_bound = 0;
  unsigned constructor_random_result_valid = 0;
  int32_t constructor_random_result = 0;
  uint32_t source_seed_before = 0, source_seed_after = 0;
  uint32_t constructor_phase_count = 0;
  MeleeWebStadiumSetupConstructorPhaseEvent constructor_phases[5]{};
};
struct MeleeWebStadiumGoAlignmentEvent {
  uint32_t sequence = 0;
  uint64_t generation = 0;
  unsigned world_ticks = 0;
  unsigned source_branch = 0, source_seed_valid = 0;
};
struct MeleeWebStadiumGoAlignmentSnapshot {
  unsigned armed = 0, failure = 0, expected_branch = 0;
  uint64_t generation = 0;
  MeleeWebStadiumGoAlignmentEvent go_after;
  unsigned next_sequence = 0;
  struct { unsigned sequence = 0; } stage_before, stage_after, hud_after;
};

namespace melee_web {
struct RetailReplayInitialCssContext {};
struct RetailReplayFrame { std::array<uint8_t, 44> bytes{}; };
struct RetailReplaySpan { unsigned scene = 0, first_frame = 0, last_frame = 0; };
struct RetailReplayRecipe {
  unsigned version = 8;
  bool whole_session_value = true, diagnostic_entity_prefix = false;
  std::unique_ptr<RetailReplayInitialCssContext> initial_css;
  std::vector<RetailReplayFrame> frames;
  std::vector<RetailReplaySpan> spans;
  bool whole_session() const { return whole_session_value; }
};
struct MenuPreparationState {
  enum class Phase { Idle, Busy };
  bool busy_value = false;
  bool busy() const { return busy_value; }
  Phase phase() const { return busy_value ? Phase::Busy : Phase::Idle; }
};
}

enum class FirstSssVsSetupOrigin { LegacyEntry124, CurrentPreparedLater_943, CurrentPreparedLater_951 };
enum class CurrentSssExitProfile { Other, Stock4None };
enum class StadiumCfObservationScope { Common15, Fresh132Typed22, LaterBTyped22 };
enum class AssetDestination { None, Other };
struct MockMatch {
  uint32_t seed = 0;
  uint64_t frames = 0;
  bool built = true;
  uint32_t random_seed() const { return seed; }
  bool construction_complete() const { return built; }
  uint64_t source_frames() const { return frames; }
};
struct MockCache {};
struct MeleeWebMenuHost {};
struct SetupSnapshotHolder { MeleeWebStadiumSetupSnapshot setup_snapshot; };
struct FirstSssVsSetupState {
  bool attempted = false, complete = false, failed = false, construction_complete = false;
  FirstSssVsSetupOrigin origin = FirstSssVsSetupOrigin::LegacyEntry124;
  uint64_t match_world_generation = 0;
  unsigned committed_generation = 0;
  MeleeWebStadiumSetupSnapshot setup_snapshot;
};
struct CurrentSssExitState {
  bool armed = false, failed = false, terminal = false;
  CurrentSssExitProfile profile = CurrentSssExitProfile::Other;
  bool prepared_setup_attempted = false, prepared_setup_approved = false;
  bool prepared_setup_consumed = false, notification_received = false;
  const melee_web::RetailReplayRecipe* recipe = nullptr;
  const melee_web::RetailReplayInitialCssContext* context = nullptr;
  uint64_t notification_order = 0, order = 0;
  uint64_t notification_cursor = 0, terminal_cursor = 0;
  unsigned notification_steps = 0, notification_draws = 0;
  unsigned terminal_steps = 0, terminal_draws = 0;
  const MeleeWebMenuHost* host_owner = nullptr;
};
struct LaterSssState { const melee_web::stadium_later_reference::Context* reference = nullptr; };
struct RecipeSeal {
  const melee_web::RetailReplayRecipe* owner = nullptr;
  unsigned bytes = 0;
  std::array<uint8_t, 32> sha256{};
};
struct ReplayCounters {
  bool is_pending = false;
  unsigned step_count = 0, draw_count = 0;
  bool pending() const { return is_pending; }
  unsigned steps() const { return step_count; }
  unsigned draws() const { return draw_count; }
};
struct MatchedSetupCfState {
  enum class ManifestActionKind { Unused };
  struct ManifestAction {
    uint8_t kind = 0;
    uint32_t ordinal = 0, bundle = UINT32_MAX, draws_before = 0;
    std::array<uint8_t, 44> pad{};
  };
  StadiumCfObservationScope observation_scope = StadiumCfObservationScope::Common15;
  bool opted_in = false, armed = false, queued = false, awaiting_approval = false;
  bool failed = false, complete = false, last_action_complete = false;
  bool explicit_step_mode = false, explicit_step_inflight = false;
  bool later_b_gate2_attempted = false, later_b_gate2_parent_archived = false;
  bool later_b_gate2_mode = false, later_b_gate2_long_mode = false, later_b_gate2_go_observed = false;
  bool later_b_rng_action_enabled = false;
  unsigned later_b_rng_action_first = 0, later_b_rng_action_last = 0;
  bool later_b_gate2_abort_consumed = false;
  bool later_b_action_started = false, later_b_safe_refusal_boundary = false;
  std::unique_ptr<melee_web::RetailReplayRecipe> later_b_archived_replay;
  const melee_web::RetailReplayRecipe* later_b_recipe_owner = nullptr;
  const melee_web::RetailReplayInitialCssContext* later_b_context_owner = nullptr;
  uint64_t later_b_notification_order = 0, later_b_setup_world_generation = 0;
  uint32_t later_b_setup_seed = 0, later_b_reference_go_tick = 0;
  uint32_t later_b_actual_go_tick = 0, later_b_post_go_typed22_count = 0;
  uint32_t later_b_prefix_cursor = 0, later_b_stop_tick = 0;
  std::string later_b_transfer_token_id;
  bool version2 = false, full_schedule = false, common_fields = false;
  bool fresh132_full_a = false;
  uint32_t action_count = 171, cursor = 0, last_schedule_index = 0;
  uint32_t source_ticks = 0, source_draws = 0, last_source_frame_after = 0;
  unsigned queued_draws_before = 0;
  const MockMatch* owner = nullptr;
  const MockCache* cache_owner = nullptr;
  const MeleeWebMenuHost* host_owner = nullptr;
  uint64_t world_generation = 0;
  unsigned asset_generation = 0;
  unsigned last_bundle = 0;
  uint64_t audio_calls_before = 0, audio_calls_after = 0;
  uint64_t audio_frames_before = 0, audio_frames_after = 0;
  unsigned audio_phase_before = 0, audio_phase_after = 0;
  unsigned aurora_begin_calls = 0, aurora_end_calls = 0;
  std::vector<ManifestAction> version2_actions;
  std::string error, last_action_json;
};
struct MatchedSetupCfRetirement {
  bool native_retired = false, later_b_unarmed_abort = false;
  bool later_b_attempt_consumed = false, later_b_was_armed = false;
  unsigned later_b_source_ticks = 0, later_b_source_draws = 0, later_b_action_cursor = 0;
  int phase_after_close = -1, running_after_close = -1;
  std::string error, later_b_transfer_receipt;
};
struct AuroraStats { unsigned queuedPipelines = 0; };
struct AuroraSubmission {
  unsigned pendingFramePackets = 0, pendingStagingBuffers = 0, workerBusy = 0;
};
struct GameplayStats { unsigned ticks = 0; };

static bool host_entered = false, running = false, pending = false, faulted = false;
static bool first_use_draw_pending = false, menu_scene_rebuild_pending = false;
static bool source_session_owned = true, scoped_assets = true, scoped_disc_import = true;
static bool native_idle = true, net_active = false, source_file_busy = false;
static bool world_live = true, first_css_browser_draw_armed = false;
static unsigned asset_generation = 5;
static uint64_t world_generation = 7;
static AssetDestination asset_destination = AssetDestination::None;
static LaterSssState later_sss;
static RecipeSeal later_sss_reference_seal;
static CurrentSssExitState current_sss_exit;
static FirstSssVsSetupState first_sss_vs_setup;
static MatchedSetupCfState matched_setup_cf;
static MatchedSetupCfRetirement matched_setup_cf_retirement;
static std::unique_ptr<melee_web::RetailReplayRecipe> replay;
static std::unique_ptr<MockCache> archive_cache;
static std::unique_ptr<MockMatch> match;
static MeleeWebMenuHost host_object;
static MeleeWebMenuHost* host = &host_object;
static void* world = &world_generation;
static size_t replay_cursor = 943;
static ReplayCounters replay_source_frames;
static melee_web::MenuPreparationState preparation;
static AuroraStats aurora_stats;
static AuroraSubmission aurora_submission;
static GameplayStats gameplay_stats;
static bool arm_leaf_accept = true;
static unsigned arm_leaf_calls = 0;
struct FixtureClock { void reset() {} };
static FixtureClock menu_clock, audio_clock;
static std::string message;
static void first_css_browser_draw_refresh_observation() {}

// `world` is the outgoing menu-world owner; `world_live` models the separate
// gameplay-world service that survives when completed Setup owns `match`.
static bool melee_web_gameplay_world_exists() { return world_live; }
static uint64_t melee_web_gameplay_generation() { return world_generation; }
static GameplayStats melee_web_gameplay_stats() { return gameplay_stats; }
static bool melee_web_net_active() { return net_active; }
static bool melee_web_source_file_drive_busy() { return source_file_busy; }
static AuroraSubmission aurora_browser_submission_status() { return aurora_submission; }
static const AuroraStats* aurora_get_stats() { return &aurora_stats; }
static int test_native_idle() { return native_idle ? 1 : 0; }
#define EM_ASM_INT(...) test_native_idle()

static void reset_fixture();
static std::vector<uint8_t> make_manifest();
static int matched_setup_cf_arm_internal(bool, unsigned, unsigned, unsigned,
    unsigned, unsigned, bool, bool, bool);
static std::string matched_setup_cf_retirement_json();
"""


FIXTURE_BODY = r"""
static std::array<uint8_t, 11> hex_pad(const char* hex) {
  assert(hex && std::char_traits<char>::length(hex)==22);
  auto digit=[](char c)->unsigned {
    assert((c>='0'&&c<='9')||(c>='a'&&c<='f'));
    return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);
  };
  std::array<uint8_t,11> out{};
  for (size_t i=0;i<out.size();++i) out[i]=static_cast<uint8_t>((digit(hex[2*i])<<4)|digit(hex[2*i+1]));
  return out;
}
static const char* const actual_tick_pads[180][4] = {
__PADS__
};
static const char* const actual_long_tick_pads[3585][4] = {
__LONG_PADS__
};

static void reset_fixture() {
  matched_setup_cf = MatchedSetupCfState{};
  matched_setup_cf_retirement = MatchedSetupCfRetirement{};
  current_sss_exit = CurrentSssExitState{};
  first_sss_vs_setup = FirstSssVsSetupState{};
  later_sss = LaterSssState{};
  later_sss.reference = &melee_web::stadium_later_reference::observed_b;
  replay = std::make_unique<melee_web::RetailReplayRecipe>();
  replay->initial_css = std::make_unique<melee_web::RetailReplayInitialCssContext>();
  replay->frames.resize(2013); replay->spans.resize(12);
  static const unsigned scenes[12]={1,2,3,4,1,2,3,4,1,2,3,4};
  for (size_t i=0;i<12;++i) replay->spans[i]={scenes[i],later_sss.reference->first[i],later_sss.reference->last[i]};
  for (unsigned tick=0;tick<180;++tick) {
    std::array<uint8_t,44> row{};
    for (unsigned port=0;port<4;++port) {
      const auto bytes=hex_pad(actual_tick_pads[tick][port]);
      assert(bytes.size()==11 && port*11+bytes.size()<=row.size());
      std::copy(bytes.begin(),bytes.end(),row.begin()+port*11);
    }
    assert(row.size()==44);
    replay->frames[943+tick].bytes=row;
  }
  archive_cache=std::make_unique<MockCache>();
  match=std::make_unique<MockMatch>(); match->seed=0x4b25c79bU; match->frames=0; match->built=true;
  // Match the observed completed Setup boundary: the old menu-world owner has
  // been closed/reset, while the source match world remains live at generation 7.
  world=nullptr; world_live=true; world_generation=7;
  gameplay_stats=GameplayStats{};
  host=&host_object; host_entered=false; running=false; pending=false; faulted=false;
  first_use_draw_pending=false; menu_scene_rebuild_pending=false;
  source_session_owned=true; scoped_assets=true; scoped_disc_import=true;
  native_idle=true; net_active=false; source_file_busy=false;
  asset_generation=5; asset_destination=AssetDestination::None;
  replay_cursor=943; replay_source_frames=ReplayCounters{};
  preparation=melee_web::MenuPreparationState{};
  aurora_stats=AuroraStats{}; aurora_submission=AuroraSubmission{};
  arm_leaf_accept=true; arm_leaf_calls=0;
  later_sss_reference_seal.owner=replay.get();
  later_sss_reference_seal.bytes=112230;
  later_sss_reference_seal.sha256=later_sss.reference->recipe_sha256;
  current_sss_exit.armed=true; current_sss_exit.profile=CurrentSssExitProfile::Stock4None;
  current_sss_exit.terminal=true; current_sss_exit.prepared_setup_attempted=true;
  current_sss_exit.prepared_setup_approved=true; current_sss_exit.prepared_setup_consumed=true;
  current_sss_exit.recipe=replay.get(); current_sss_exit.context=replay->initial_css.get();
  current_sss_exit.notification_order=4330; current_sss_exit.order=4331;
  current_sss_exit.notification_received=true; current_sss_exit.notification_cursor=943;
  current_sss_exit.terminal_cursor=943; current_sss_exit.host_owner=host;
  first_sss_vs_setup.attempted=true; first_sss_vs_setup.complete=true;
  first_sss_vs_setup.construction_complete=true;
  first_sss_vs_setup.origin=FirstSssVsSetupOrigin::CurrentPreparedLater_943;
  first_sss_vs_setup.match_world_generation=7; first_sss_vs_setup.committed_generation=5;
  auto& snap=first_sss_vs_setup.setup_snapshot;
  snap.world_generation=7; snap.world_ticks=0; snap.source_frame=0; snap.map_id=2;
  snap.constructor_callback_count=1; snap.constructor_phase_count=5;
  snap.constructor_randi_call_count=1; snap.parameter_x0=3600; snap.parameter_x4=3800;
  snap.initial_countdown=3742; snap.current_countdown=3742;
  snap.constructor_random_bound_valid=1; snap.constructor_random_bound=200;
  snap.constructor_random_result_valid=1; snap.constructor_random_result=142;
  snap.source_seed_before=0xba5dac05U; snap.source_seed_after=0xb6cdeeb4U;
  auto& p=snap.constructor_phases;
  p[0]={1,MELEE_WEB_STADIUM_SETUP_PHASE_ENTRY,0,0,0,0,0,1,0xba5dac05U};
  p[1]={2,MELEE_WEB_STADIUM_SETUP_PHASE_PARAMETERS,1,3600,3800,0,0,1,0xba5dac05U};
  p[2]={3,MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL,1,3600,3800,1,200,1,0xba5dac05U};
  p[3]={4,MELEE_WEB_STADIUM_SETUP_PHASE_UP_RETURN,1,3600,3800,1,142,1,0xb6cdeeb4U};
  p[4]={5,MELEE_WEB_STADIUM_SETUP_PHASE_COMMIT,1,3600,3800,1,3742,1,0xb6cdeeb4U};
}

static void put32(std::vector<uint8_t>& out,uint32_t value) {
  for (unsigned i=0;i<4;++i) out.push_back(static_cast<uint8_t>(value>>(8*i)));
}
static std::vector<uint8_t> make_manifest() {
  std::vector<uint8_t> out={'M','W','B','2'};
  put32(out,360); put32(out,943); put32(out,84); put32(out,180); put32(out,36);
  const char token[]="123e4567-e89b-12d3-a456-426614174000";
  out.insert(out.end(),token,token+36);
  assert(out.size()==60);
  for (uint32_t tick=0;tick<180;++tick) {
    const auto tick_start=out.size();
    out.push_back(1); put32(out,tick); put32(out,tick); put32(out,tick);
    for (unsigned port=0;port<4;++port) {
      const auto pad=hex_pad(actual_tick_pads[tick][port]);
      assert(pad.size()==11);
      out.insert(out.end(),pad.begin(),pad.end());
    }
    assert(out.size()-tick_start==57);
    const auto draw_start=out.size();
    out.push_back(2); put32(out,tick); put32(out,UINT32_MAX); put32(out,tick);
    out.insert(out.end(),44,0);
    assert(out.size()-draw_start==57);
  }
  assert(out.size()==60+360*57 && out.size()==20580);
  std::cerr << "MWB2 fixture geometry: per-port=11 four-port=44 rows=360 bytes=" << out.size() << '\n';
  return out;
}

int matched_setup_cf_arm_internal(bool version2,unsigned action_count,
    unsigned c_action_index,unsigned f_action_index,unsigned c_source_tick,
    unsigned f_source_tick,bool full_schedule,bool common_fields,
    bool fresh132_full_a) {
  assert(version2 && c_action_index==0 && f_action_index==0);
  assert((action_count==360 || action_count==7168) && c_source_tick==84 &&
         f_source_tick==85 && !full_schedule && common_fields && !fresh132_full_a);
  ++arm_leaf_calls;
  auto& state=matched_setup_cf;
  // Exact assignment from actual matched_setup_cf_arm_internal; this remains a modeled leaf.
  state.action_count=version2?action_count:171U;
  // This explicitly modeled boundary stands for the source arm's one-way
  // unique_ptr move; it performs no source work and reports no runtime result.
  state.later_b_archived_replay=std::move(replay);
  state.later_b_gate2_parent_archived=true;
  if (!arm_leaf_accept) { state.failed=true; return 0; }
  return 1;
}

__EXTRACTED_FUNCTIONS__

static bool parser_refuses(std::vector<uint8_t> manifest) {
  bool refused=false;
  try { (void)later_b_gate2_manifest_arm(manifest.data(),manifest.size()); }
  catch (const std::exception&) { refused=true; }
  return refused;
}

static void parser_accept_case() {
  reset_fixture(); auto manifest=make_manifest();
  // This is the captured profile-3 completed Setup topology: no old menu-world
  // owner, but the zero-tick match/cache/host and source world still exist.
  assert(world==nullptr && world_live && melee_web_gameplay_world_exists());
  assert(match && archive_cache && host && melee_web_gameplay_generation()==7);
  std::cerr << "Actual parser accept begins after checked fixture geometry" << '\n';
  assert(later_b_gate2_manifest_arm(manifest.data(),manifest.size())==1);
  std::cerr << "Actual parser accepted; retained owner/count assertions follow" << '\n';
  assert(arm_leaf_calls==1 && matched_setup_cf.later_b_gate2_attempted);
  assert(matched_setup_cf.later_b_gate2_parent_archived && !replay);
  assert(matched_setup_cf.later_b_archived_replay.get()==matched_setup_cf.later_b_recipe_owner);
  assert(matched_setup_cf.later_b_setup_seed==0x4b25c79bU);
  assert(first_sss_vs_setup.setup_snapshot.source_seed_after==0xb6cdeeb4U);
  assert(matched_setup_cf.later_b_reference_go_tick==84 && !matched_setup_cf.later_b_gate2_go_observed);
  assert(matched_setup_cf.action_count==360 && matched_setup_cf.version2_actions.size()==360);
  assert(matched_setup_cf.source_ticks==0 && matched_setup_cf.source_draws==0 && matched_setup_cf.cursor==0);
  assert(matched_setup_cf.version2_actions[0].kind==1 && matched_setup_cf.version2_actions[1].kind==2);
  assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  auto old_match=std::move(match); match=std::make_unique<MockMatch>();
  match->seed=0x4b25c79bU; match->frames=0; match->built=true;
  assert(!later_b_gate2_parent_identity_valid(matched_setup_cf));
  match=std::move(old_match); assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  auto original_context=std::move(matched_setup_cf.later_b_archived_replay->initial_css);
  matched_setup_cf.later_b_archived_replay->initial_css=std::make_unique<melee_web::RetailReplayInitialCssContext>();
  assert(!later_b_gate2_parent_identity_valid(matched_setup_cf));
  matched_setup_cf.later_b_archived_replay->initial_css=std::move(original_context);
  assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
}

static void parser_refusal_controls() {
  auto valid=make_manifest();
  auto reject=[&](std::vector<uint8_t> bytes) {
    reset_fixture();
    assert(parser_refuses(std::move(bytes)));
    assert(matched_setup_cf.later_b_gate2_attempted && arm_leaf_calls==0 && replay);
  };
  auto bytes=valid; bytes.pop_back(); reject(bytes);
  bytes=valid; bytes.push_back(0); reject(bytes);
  bytes=valid; bytes[73]^=1; reject(bytes); // First tick's actual 44-byte PAD span.
  bytes=valid; bytes[60]=2; reject(bytes); // First row kind cannot become a draw.
  bytes=valid; bytes[24]='z'; reject(bytes); // Malformed UUID refuses before owner move.
  bytes=valid; bytes[12]=180; reject(bytes); // Header coordinate cannot exceed stop.
  bytes=valid;
  std::vector<uint8_t> wrong_cursor; put32(wrong_cursor,942);
  assert(wrong_cursor.size()==4);
  std::copy(wrong_cursor.begin(),wrong_cursor.end(),bytes.begin()+8);
  reject(bytes); // Exact LEu32 cursor 942; sealed input begins at global cursor 943.
  bytes=valid; bytes[60+57+1]=1; reject(bytes); // Draw row local ordinal is immutable.
  // A pre-Setup menu-world owner must not masquerade as the completed Setup owner.
  reset_fixture(); world=&world_generation;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); match.reset();
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); world_generation=8;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); later_sss.reference=&melee_web::stadium_later_reference::legacy;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); later_sss_reference_seal.owner=nullptr;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); replay->version=7;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); replay->spans[0].scene=4;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); match->seed=0xb6cdeeb4U;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); first_sss_vs_setup.setup_snapshot.source_seed_after=0x4b25c79bU;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); first_sss_vs_setup.setup_snapshot.constructor_phases[2].scalar_value=201;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); first_sss_vs_setup.setup_snapshot.constructor_phases[3].phase=MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL;
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); replay->frames.resize(1122);
  assert(parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_fixture(); arm_leaf_accept=false;
  assert(parser_refuses(valid));
  assert(matched_setup_cf.later_b_gate2_attempted && matched_setup_cf.failed);
  assert(matched_setup_cf.later_b_gate2_parent_archived && !replay);
  assert(matched_setup_cf.later_b_archived_replay.get()==matched_setup_cf.later_b_recipe_owner);
  assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  assert(parser_refuses(valid)); // The failed attempt cannot be reset or rebound.
  assert(arm_leaf_calls==1 && matched_setup_cf.later_b_gate2_parent_archived);
}

static void go_snapshot_controls() {
  MeleeWebStadiumGoAlignmentSnapshot s{};
  s.armed=1; s.failure=0; s.expected_branch=0; s.generation=7;
  s.go_after.sequence=3; s.go_after.generation=7; s.go_after.world_ticks=84;
  s.go_after.source_branch=0; s.go_after.source_seed_valid=1;
  s.next_sequence=3; s.stage_before.sequence=1; s.stage_after.sequence=2; s.hud_after.sequence=0;
  assert(later_b_gate2_first_go_snapshot_valid(s,84,7));
  auto bad=s; bad.go_after.sequence=0; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.go_after.sequence=4; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.go_after.world_ticks=83; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.generation=8; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.go_after.generation=8; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.go_after.source_seed_valid=0; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
  bad=s; bad.stage_after.sequence=1; assert(!later_b_gate2_first_go_snapshot_valid(bad,84,7));
}

static void lifecycle_and_status_controls() {
  reset_fixture(); auto manifest=make_manifest();
  std::cerr << "Actual parser accept begins after checked fixture geometry" << '\n';
  assert(later_b_gate2_manifest_arm(manifest.data(),manifest.size())==1);
  std::cerr << "Actual parser accepted; retained owner/count assertions follow" << '\n';
  // This is the actual transfer serializer against the real Context header's
  // std::array<uint8_t,32>, not a string substitute. Native leaves stay modeled.
  std::cout << "TRANSFER " << later_b_gate2_transfer_proof_json(matched_setup_cf,false) << '\n';
  running=false; assert(actual_abort_running_read_control()==0);
  running=true; assert(actual_abort_running_read_control()==1); running=false;
  auto& state=matched_setup_cf; state.failed=true; state.armed=false;
  assert(world==nullptr && world_live && melee_web_gameplay_world_exists());
  assert(match && archive_cache && host && melee_web_gameplay_generation()==7);
  assert(later_b_gate2_unarmed_abort_ready(state));
  world=&world_generation; assert(!later_b_gate2_unarmed_abort_ready(state));
  world=nullptr; assert(later_b_gate2_unarmed_abort_ready(state));
  state.later_b_action_started=true; assert(!later_b_gate2_unarmed_abort_ready(state));
  state.later_b_action_started=false; state.explicit_step_inflight=true;
  assert(!later_b_gate2_unarmed_abort_ready(state)); state.explicit_step_inflight=false;
  state.source_ticks=1; assert(!later_b_gate2_unarmed_abort_ready(state)); state.source_ticks=0;
  state.source_draws=1; assert(!later_b_gate2_unarmed_abort_ready(state)); state.source_draws=0;
  state.queued=true; assert(!later_b_gate2_unarmed_abort_ready(state)); state.queued=false;
  state.awaiting_approval=true; assert(!later_b_gate2_unarmed_abort_ready(state)); state.awaiting_approval=false;
  state.later_b_safe_refusal_boundary=true; assert(!later_b_gate2_unarmed_abort_ready(state)); state.later_b_safe_refusal_boundary=false;
  match->frames=1; assert(!later_b_gate2_unarmed_abort_ready(state)); match->frames=0;
  pending=true; assert(!later_b_gate2_unarmed_abort_ready(state)); pending=false;
  replay_source_frames.is_pending=true; assert(!later_b_gate2_unarmed_abort_ready(state)); replay_source_frames.is_pending=false;
  native_idle=false; assert(!later_b_gate2_unarmed_abort_ready(state)); native_idle=true;
  aurora_stats.queuedPipelines=1; assert(!later_b_gate2_unarmed_abort_ready(state)); aurora_stats.queuedPipelines=0;
  assert(later_b_gate2_unarmed_abort_ready(state));

  // Gate1 leaves the legacy CSS draw owner unarmed. The actual existing getter
  // still dispatches the Gate2 retained-state serializer without a CSS refresh.
  first_css_browser_draw_armed=false;
  assert(!first_css_browser_draw_armed);
  state.failed=false; state.armed=true; state.later_b_gate2_attempted=true;
  state.later_b_gate2_mode=true; state.observation_scope=StadiumCfObservationScope::LaterBTyped22;
  state.action_count=360; state.later_b_prefix_cursor=943;
  state.later_b_reference_go_tick=84; state.later_b_stop_tick=180;
  state.later_b_gate2_go_observed=false; state.later_b_post_go_typed22_count=0;
  state.last_bundle=1;
  std::cout << "ACTION_TICK " << actual_action_json_control(true) << '\n';
  std::cout << "ACTION_DRAW " << actual_action_json_control(false) << '\n';
  state.later_b_gate2_long_mode=true; state.later_b_prefix_cursor=951;
  state.queued_draws_before=84; state.last_bundle=85;
  std::cout << "LONG_ACTION_TICK " << actual_action_json_control(true,168,84) << '\n';
  std::cout << "LONG_ACTION_DRAW " << actual_action_json_control(false,169,84) << '\n';
  state.later_b_gate2_long_mode=false;
  state.later_b_gate2_mode=false;
  std::cout << "LEGACY_TICK " << actual_action_json_control(true) << '\n';
  std::cout << "LEGACY_DRAW " << actual_action_json_control(false) << '\n';
  state.later_b_gate2_mode=true; state.last_action_json.clear();
  std::cout << "LIVE " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
  state.later_b_gate2_go_observed=true; state.later_b_actual_go_tick=84;
  state.later_b_post_go_typed22_count=96;
  state.last_action_json="{\"source_coordinates\":{\"source_tick\":84,\"draw_ordinal\":null,\"recipe_input_index\":1027},\"actual_go_event\":{\"observed\":true,\"sequence\":3},\"post_go_typed22_state\":{}}";
  std::cout << "OBSERVED " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
  matched_setup_cf_retirement.native_retired=true;
  std::cout << "RETIRED " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
  matched_setup_cf_retirement.later_b_unarmed_abort=true;
  std::cout << "ABORT " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
  matched_setup_cf_retirement.native_retired=false;
  matched_setup_cf_retirement.error="abort \"failed\"\nretained";
  std::cout << "ABORT_ERROR " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
  matched_setup_cf_retirement.later_b_unarmed_abort=false;
  state.later_b_gate2_attempted=false;
  std::cout << "LEGACY " << melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status() << '\n';
}

static void step_error_controls() {
  reset_fixture();
  auto& state=matched_setup_cf;
  state.later_b_gate2_mode=true;
  state.armed=true;
  matched_setup_cf_fail("first native service error");
  matched_setup_cf_fail("generic outer catch");
  assert(state.error=="first native service error");
  assert(message=="first native service error");
  std::cout << "STEP_ERROR " << matched_setup_cf_json() << '\n';
}

__LONG_TESTS__

int main(int argc,char** argv) {
  assert(argc==2); const std::string mode=argv[1];
  if(mode=="parser-accept") parser_accept_case();
  else if(mode=="parser-controls") parser_refusal_controls();
  else if(mode=="go") go_snapshot_controls();
  else if(mode=="lifecycle-status") lifecycle_and_status_controls();
  else if(mode=="step-error") step_error_controls();
  else if(mode=="long-parser-accept") long_parser_accept_case();
  else if(mode=="long-parser-controls") long_parser_refusal_controls();
  else return 2;
  return 0;
}
"""


LONG_TESTS = r"""
static void reset_long_fixture() {
  reset_fixture();
  const auto& reference=melee_web::stadium_later_reference::observed_b_long;
  later_sss.reference=&reference;
  replay->frames.resize(reference.frames);
  static const unsigned scenes[12]={1,2,3,4,1,2,3,4,1,2,3,4};
  for(size_t i=0;i<12;++i)
    replay->spans[i]={scenes[i],reference.first[i],reference.last[i]};
  for(unsigned tick=0;tick<3585;++tick) {
    std::array<uint8_t,44> row{};
    for(unsigned port=0;port<4;++port) {
      const auto bytes=hex_pad(actual_long_tick_pads[tick][port]);
      assert(bytes.size()==11);
      std::copy(bytes.begin(),bytes.end(),row.begin()+port*11);
    }
    replay->frames[951+tick].bytes=row;
  }
  match->seed=0x25d557a7U;
  replay_cursor=951;
  later_sss_reference_seal.owner=replay.get();
  later_sss_reference_seal.bytes=262226;
  later_sss_reference_seal.sha256=reference.recipe_sha256;
  current_sss_exit.recipe=replay.get(); current_sss_exit.context=replay->initial_css.get();
  current_sss_exit.notification_cursor=951; current_sss_exit.terminal_cursor=951;
  // The actual trace order is dynamic and is not fixed by the reference descriptor.
  // This integer is a deliberately modeled nonzero order for the extracted-owner leaf.
  current_sss_exit.notification_order=9000; current_sss_exit.order=9001;
  first_sss_vs_setup.origin=FirstSssVsSetupOrigin::CurrentPreparedLater_951;
  auto& snap=first_sss_vs_setup.setup_snapshot;
  snap.constructor_callback_count=1; snap.constructor_phase_count=5;
  snap.constructor_randi_call_count=1; snap.parameter_x0=3600; snap.parameter_x4=3800;
  snap.initial_countdown=3672; snap.current_countdown=3672;
  snap.constructor_random_bound_valid=1; snap.constructor_random_bound=200;
  snap.constructor_random_result_valid=1; snap.constructor_random_result=72;
  snap.source_seed_before=0x7fc17cf1U; snap.source_seed_after=0x5ce52bf0U;
  auto& phases=snap.constructor_phases;
  phases[0]={1,MELEE_WEB_STADIUM_SETUP_PHASE_ENTRY,0,0,0,0,0,1,0x7fc17cf1U};
  phases[1]={2,MELEE_WEB_STADIUM_SETUP_PHASE_PARAMETERS,1,3600,3800,0,0,1,0x7fc17cf1U};
  phases[2]={3,MELEE_WEB_STADIUM_SETUP_PHASE_UP_CALL,1,3600,3800,1,200,1,0x7fc17cf1U};
  phases[3]={4,MELEE_WEB_STADIUM_SETUP_PHASE_UP_RETURN,1,3600,3800,1,72,1,0x5ce52bf0U};
  phases[4]={5,MELEE_WEB_STADIUM_SETUP_PHASE_COMMIT,1,3600,3800,1,3672,1,0x5ce52bf0U};
}
static const char* const actual_long_manifest_hex="__LONG_MANIFEST_HEX__";
static std::vector<uint8_t> make_long_manifest() {
  const std::string hex=actual_long_manifest_hex;
  assert(hex.size()==2*(24+36+7168*57));
  auto digit=[](char c)->unsigned {
    assert((c>='0'&&c<='9')||(c>='a'&&c<='f'));
    return c<='9'?unsigned(c-'0'):unsigned(c-'a'+10);
  };
  std::vector<uint8_t> out; out.reserve(hex.size()/2);
  for(size_t i=0;i<hex.size();i+=2)
    out.push_back(static_cast<uint8_t>((digit(hex[i])<<4)|digit(hex[i+1])));
  return out;
}
static bool long_parser_refuses(std::vector<uint8_t> bytes) {
  bool refused=false;
  try { (void)later_b_gate2_long_manifest_arm(bytes.data(),bytes.size()); }
  catch(const std::exception&) { refused=true; }
  return refused;
}
static void long_parser_accept_case() {
  reset_long_fixture(); auto manifest=make_long_manifest();
  assert(world==nullptr && world_live && match && archive_cache && host);
  assert(replay->frames.size()==5422 && replay_cursor==951);
  assert(later_b_gate2_long_manifest_arm(manifest.data(),manifest.size())==1);
  assert(matched_setup_cf.later_b_gate2_attempted && matched_setup_cf.later_b_gate2_mode &&
         matched_setup_cf.later_b_gate2_long_mode && matched_setup_cf.later_b_gate2_parent_archived);
  assert(matched_setup_cf.action_count==7168 && matched_setup_cf.version2_actions.size()==7168);
  assert(matched_setup_cf.later_b_prefix_cursor==951 && matched_setup_cf.later_b_stop_tick==3585);
  assert(matched_setup_cf.later_b_setup_seed==0x25d557a7U);
  assert(first_sss_vs_setup.setup_snapshot.source_seed_after==0x5ce52bf0U);
  assert(matched_setup_cf.later_b_reference_go_tick==84 && !matched_setup_cf.later_b_gate2_go_observed);
  assert(matched_setup_cf.version2_actions[168].kind==1 &&
         matched_setup_cf.version2_actions[168].ordinal==84 &&
         matched_setup_cf.version2_actions[169].kind==2 &&
         matched_setup_cf.version2_actions[169].ordinal==84 &&
         matched_setup_cf.version2_actions[169].bundle==85 &&
         matched_setup_cf.version2_actions[170].kind==1 &&
         matched_setup_cf.version2_actions[170].ordinal==85);
  assert(matched_setup_cf.version2_actions.back().kind==2 &&
         matched_setup_cf.version2_actions.back().ordinal==3582 &&
         matched_setup_cf.version2_actions.back().bundle==3585);
  assert(matched_setup_cf.source_ticks==0 && matched_setup_cf.source_draws==0 &&
         matched_setup_cf.cursor==0);
  assert(matched_setup_cf.later_b_archived_replay.get()==matched_setup_cf.later_b_recipe_owner);
  assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  const uint32_t setup_seed=match->seed;
  match->seed=0x5ce52bf0U; // Constructor endpoint differs from completed Setup RNG state.
  assert(!later_b_gate2_parent_identity_valid(matched_setup_cf));
  match->seed=setup_seed; assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  std::cout << "MWB3_STATUS " << matched_setup_cf_json() << '\n';
  std::cout << "MWB3_ACCEPT actions=7168 ticks=3585 draws=3583 prefix=951 GO=84 actual_go_observed=false\n";
}
static void long_parser_refusal_controls() {
  const auto valid=make_long_manifest();
  auto reject=[&](std::vector<uint8_t> bytes) {
    reset_long_fixture();
    assert(long_parser_refuses(std::move(bytes)));
    assert(matched_setup_cf.later_b_gate2_attempted && arm_leaf_calls==0 && replay);
  };
  auto bytes=valid; bytes.pop_back(); reject(bytes);
  bytes=valid; bytes[8]=0xB8; reject(bytes); // Refuse prefix 952 before any move.
  bytes=valid; bytes[24]='x'; reject(bytes); // UUID is validated before ownership transfer.
  bytes=valid; bytes[60+13]^=1; reject(bytes); // First literal PAD no longer matches retained recipe.
  bytes=valid; bytes[60+169*57+5]=84; reject(bytes); // Draw bundle must be ticks_before=85.
  bytes=valid; bytes[60+170*57]=2; reject(bytes); // Action 170 must be source tick 85.
  reset_long_fixture(); replay->frames[951+84].bytes[0]^=1;
  assert(long_parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_long_fixture(); later_sss.reference=&melee_web::stadium_later_reference::observed_b;
  assert(long_parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_long_fixture(); match->seed=0x5ce52bf0U; // Constructor endpoint is not completed Setup seed.
  assert(long_parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_long_fixture(); first_sss_vs_setup.origin=FirstSssVsSetupOrigin::CurrentPreparedLater_943;
  assert(long_parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_long_fixture(); current_sss_exit.order=current_sss_exit.notification_order;
  assert(long_parser_refuses(valid)); assert(arm_leaf_calls==0 && replay);
  reset_long_fixture(); arm_leaf_accept=false;
  assert(long_parser_refuses(valid));
  assert(matched_setup_cf.failed && matched_setup_cf.later_b_gate2_parent_archived && !replay);
  assert(matched_setup_cf.later_b_archived_replay.get()==matched_setup_cf.later_b_recipe_owner);
  assert(later_b_gate2_parent_identity_valid(matched_setup_cf));
  assert(long_parser_refuses(valid)); // Failed one-use parent is not rebound.
  assert(arm_leaf_calls==1 && matched_setup_cf.later_b_gate2_parent_archived);
  std::cout << "MWB3_CONTROLS passed=geometry,token,pad,order,context,seed,setup-order,one-use\n";
}
"""


class LaterBGate2NativeControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        required = (CPP_PATH_ENV, PROJECTION_ENV)
        present = [name in os.environ and bool(os.environ[name]) for name in required]
        if not any(present):
            raise unittest.SkipTest("optional retained Later-B Gate2 source fixtures are absent")
        if not all(present):
            raise ValueError("partial Later-B Gate2 fixture configuration refuses")

        cpp_supplied = Path(os.environ[CPP_PATH_ENV]).absolute()
        projection_supplied = Path(os.environ[PROJECTION_ENV]).absolute()
        if (cpp_supplied.is_symlink() or cpp_supplied.resolve(strict=True) != cpp_supplied or
                not cpp_supplied.is_file()):
            raise ValueError("MELEE_LATER_B_GATE2_NATIVE_CPP must name the regular candidate source file")
        if (projection_supplied.is_symlink() or
                projection_supplied.resolve(strict=True) != projection_supplied or
                not projection_supplied.is_file()):
            raise ValueError("retained Gate2 projection must not be a symlink")
        cls.cpp_path = cpp_supplied
        cls.projection_path = projection_supplied
        if cls.cpp_path.name != "gameplay_menu_browser.cpp":
            raise ValueError("MELEE_LATER_B_GATE2_NATIVE_CPP must name the candidate source file")
        cls.cpp_source = cls.cpp_path.read_text()
        cls.hex_source = (SOURCE_ROOT / "src/stadium_first_css_diagnostic_input.hpp").read_text()
        cls.projection_bytes = cls.projection_path.read_bytes()
        if hashlib.sha256(cls.projection_bytes).hexdigest() != (
                "c7f44077f44533c4c3904b94538f654492d58f162cd579b1f267db02ac89c8b4"):
            raise ValueError("retained Gate2 projection bytes differ from the reviewed export")
        cls.projection = json.loads(cls.projection_bytes)
        cls._validate_projection(cls.projection)
        cls.long_projection = None
        cls.long_projection_path = None
        if os.environ.get(LONG_PROJECTION_ENV):
            long_supplied = Path(os.environ[LONG_PROJECTION_ENV]).absolute()
            if (long_supplied.is_symlink() or long_supplied.resolve(strict=True) != long_supplied or
                    not long_supplied.is_file()):
                raise ValueError("retained long-B projection must be a regular non-symlink file")
            cls.long_projection_path = long_supplied
            cls.long_projection_bytes = long_supplied.read_bytes()
            if hashlib.sha256(cls.long_projection_bytes).hexdigest() != (
                    "dead7b678716df87432febba4c8dfedf43acdb47f8d1254bec7b6970f00d3ba1"):
                raise ValueError("retained long-B projection bytes differ from the reviewed export")
            cls.long_projection = json.loads(cls.long_projection_bytes)
            cls._validate_long_projection(cls.long_projection)
            cls.long_manifest = cls._encode_long_manifest(cls.long_projection)

        run_root = Path(os.environ.get(RUN_ROOT_ENV, cls.cpp_path.parents[1] / "run-v1")).absolute()
        if run_root.is_symlink() or run_root.resolve() != run_root:
            raise ValueError("Gate2 run root must not be a symlink")
        if run_root.exists():
            raise ValueError("Gate2 run root must be absent before the focused run")
        run_root.mkdir(parents=True, exist_ok=False)
        cls.workspace = cls.new_workspace(run_root, "stadium-later-b-gate2-native-")
        cls.binary = cls.workspace / "native-boundary-controls"
        fixture = cls.workspace / "native-boundary-controls.cpp"
        fixture.write_text(cls._build_fixture())
        compiler = shutil.which(os.environ.get(CLANGXX_ENV, "clang++"))
        if not compiler:
            raise RuntimeError("clang++ is required by the pinned focused native recipe")
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(cls.cpp_path.parent), "-I", str(SOURCE_ROOT / "src"),
                   str(fixture), "-o", str(cls.binary)]
        # Redirect before execution so even compiler errors/timeouts retain raw diagnostics.
        compile_stdout = cls.workspace / "compile.stdout"
        compile_stderr = cls.workspace / "compile.stderr"
        (cls.workspace / "compile-command.json").write_text(json.dumps(command, indent=2) + "\n")
        try:
            with compile_stdout.open("w") as stdout, compile_stderr.open("w") as stderr:
                compiled = subprocess.run(command, check=False, timeout=60,
                                          stdout=stdout, stderr=stderr, text=True)
        except subprocess.TimeoutExpired as error:
            raise RuntimeError(
                f"Native component compiler timed out; retained {compile_stdout} and {compile_stderr}"
            ) from error
        if compiled.returncode != 0:
            raise RuntimeError(
                f"Native component compiler exited {compiled.returncode}; retained "
                f"{compile_stdout} and {compile_stderr}\n{compile_stderr.read_text()}"
            )

    @staticmethod
    def _validate_projection(value):
        if value.get("schema") != "melee-web-stadium-later-b-gate2-reference-projection-v1":
            raise ValueError("retained Gate2 projection schema differs")
        if value.get("reference_id") != "original-b-2013-943-v1":
            raise ValueError("retained Gate2 projection reference identity differs")
        if value.get("setup_global_cursor") != 943 or len(value.get("actions", [])) != 360:
            raise ValueError("retained projection does not contain the fixed 943/360 boundary")
        if len(value.get("states", [])) != 96:
            raise ValueError("retained projection does not contain the bounded 96-state interval")
        if value.get("runtime_admission") is not False or value.get("C3") is not False:
            raise ValueError("retained reference-only projection must not claim native admission")
        if value["setup"].get("seed") != "4b25c79b":
            raise ValueError("retained post-Setup match seed differs")
        if value["constructor_shared_projection"].get("seed_after") != "b6cdeeb4":
            raise ValueError("retained constructor endpoint seed differs")
        shared = value["constructor_shared_projection"]
        if (shared.get("entry_count") != 1 or shared.get("min_parameter") != 3600 or
                shared.get("max_parameter") != 3800 or shared.get("random_bound") != 200 or
                shared.get("random_result") != 142 or shared.get("seed_before") != "ba5dac05" or
                shared.get("parameter_roles") != {
                    "min_parameter": "parameter_x0", "max_parameter": "parameter_x4"}):
            raise ValueError("retained seven-value constructor projection or role mapping differs")
        witnesses = value["constructor_original_witnesses"].get("coordinates", [])
        if ([row.get("sequence") for row in witnesses] != [4303, 4304, 4305, 4306, 4307] or
                value["constructor_original_witnesses"].get("scope") !=
                "original-only order; no browser phase-dispatch pairing"):
            raise ValueError("retained five constructor witnesses are not original-only ordered evidence")
        if value["go"]["coordinate"].get("source_tick") != 84:
            raise ValueError("retained GO coordinate differs")
        for tick in range(180):
            tick_row, draw_row = value["actions"][2 * tick:2 * tick + 2]
            if tick_row.get("kind") != "tick" or tick_row.get("source_tick") != tick:
                raise ValueError(f"retained tick action {tick} is out of order")
            if draw_row.get("kind") != "draw" or draw_row.get("source_tick") != tick + 1:
                raise ValueError(f"retained draw action {tick} lost its source-frame coordinate")
            pads = tick_row.get("pads")
            if not isinstance(pads, list) or len(pads) != 4 or any(
                    not isinstance(pad, str) or len(pad) != 22 for pad in pads):
                raise ValueError(f"retained tick {tick} does not have four literal PAD records")
            for pad in pads:
                try:
                    bytes.fromhex(pad)
                except ValueError as error:
                    raise ValueError(f"retained tick {tick} PAD record is not literal hex") from error

    @staticmethod
    def _encode_long_manifest(value):
        out = bytearray(b"MWB3" + struct.pack("<5I", 7168, 951, 84, 3585, 36))
        out.extend(b"123e4567-e89b-12d3-a456-426614174000")
        for row in value["actions"]:
            if row["kind"] == "tick":
                encoded = (struct.pack("<BIII", 1, row["bundle"], row["bundle"],
                                       row["draws_before"]) +
                           b"".join(bytes.fromhex(pad) for pad in row["pads"]))
            else:
                encoded = (struct.pack("<BIII", 2, row["draw"], row["ticks_before"],
                                       row["draw"]) + bytes(44))
            if len(encoded) != 57:
                raise ValueError("long MWB3 action does not have the fixed 57-byte wire extent")
            out.extend(encoded)
        if len(out) != 24 + 36 + 7168 * 57:
            raise ValueError("long MWB3 fixture extent differs")
        return bytes(out)

    @staticmethod
    def _validate_long_projection(value):
        if value.get("schema") != "melee-web-stadium-later-b-long-reference-projection-v1":
            raise ValueError("retained long-B projection schema differs")
        if value.get("reference_id") != "original-b-5422-951-long-v1":
            raise ValueError("retained long-B reference identity differs")
        if (value.get("setup_global_cursor") != 951 or value.get("recipe_frames") != 5422 or
                value.get("recipe_bytes") != 262226 or value.get("selected_match") != 1):
            raise ValueError("retained long-B projection dimensions differ")
        recipe = bytes.fromhex(value["recipe_hex"])
        if (len(recipe) != 262226 or hashlib.sha256(recipe).hexdigest() !=
                "e669f831538b90e39241a569b6c38884300bfe246b252cacda994cbc45c10bbf"):
            raise ValueError("retained long-B exact recipe bytes differ")
        descriptor = value["reference_descriptor"]
        if (descriptor.get("id") != value["reference_id"] or descriptor.get("profile") != 4 or
                descriptor.get("recipe_sha256") != hashlib.sha256(recipe).hexdigest() or
                descriptor.get("recipe_bytes") != 262226 or descriptor.get("frames") != 5422 or
                descriptor.get("pause_timer_zero_cursor") != 468 or
                descriptor.get("notification_order") is not None):
            raise ValueError("retained long-B descriptor or dynamic-order scope differs")
        if (value["entry"].get("rng", {}).get("value_hex") != "7fc17cf1" or
                value["setup"].get("rng", {}).get("value_hex") != "25d557a7" or
                value["go"]["coordinate"].get("source_tick") != 84):
            raise ValueError("retained Entry/Setup/actual-GO seed or coordinate differs")
        shared = value["constructor_shared_projection"]
        if (shared.get("entry_count") != 1 or shared.get("min_parameter") != 3600 or
                shared.get("max_parameter") != 3800 or shared.get("random_bound") != 200 or
                shared.get("random_result") != 72 or shared.get("seed_before") != "7fc17cf1" or
                shared.get("seed_after") != "5ce52bf0" or
                shared.get("parameter_roles") != {
                    "min_parameter": "parameter_x0", "max_parameter": "parameter_x4"}):
            raise ValueError("retained long-B shared constructor aggregate differs")
        actions = value.get("actions", [])
        if len(actions) != 7168 or len(value.get("states", [])) != 3501:
            raise ValueError("retained long-B action or typed-state count differs")
        ticks = draws = 0
        tick_pads = []
        for index, action in enumerate(actions):
            if action.get("action_index") != index:
                raise ValueError("retained long-B action indices are not contiguous")
            source = action.get("source", {})
            if action.get("kind") == "tick":
                pads = action.get("pads")
                if (action.get("bundle") != ticks or action.get("draws_before") != draws or
                        source.get("source_tick") != ticks or source.get("draw_ordinal") != draws or
                        action.get("consume", {}).get("source_tick") != ticks or
                        action.get("consume", {}).get("draw_ordinal") != draws or
                        not isinstance(pads, list) or len(pads) != 4):
                    raise ValueError(f"retained long-B tick action {index} lost source/PAD coordinates")
                for pad in pads:
                    if not isinstance(pad, str) or len(pad) != 22:
                        raise ValueError(f"retained long-B tick action {index} lacks literal four-port PAD")
                    bytes.fromhex(pad)
                tick_pads.append(pads)
                ticks += 1
            elif action.get("kind") == "draw":
                draw_enter = action.get("draw_enter", {})
                if (action.get("draw") != draws or action.get("ticks_before") != ticks or
                        source.get("source_tick") != ticks or source.get("draw_ordinal") != draws or
                        draw_enter.get("source_tick") != ticks or
                        draw_enter.get("draw_ordinal") != draws):
                    raise ValueError(f"retained long-B draw action {index} lost distinct source/draw coordinates")
                draws += 1
            else:
                raise ValueError(f"retained long-B action {index} has an unknown kind")
        if (ticks != 3585 or draws != 3583 or len(tick_pads) != 3585 or
                actions[168]["kind"] != "tick" or actions[168]["bundle"] != 84 or
                actions[169]["kind"] != "draw" or actions[169]["draw"] != 84 or
                actions[169]["ticks_before"] != 85 or
                actions[170]["kind"] != "tick" or actions[170]["bundle"] != 85 or
                actions[-1]["kind"] != "draw" or actions[-1]["draw"] != 3582 or
                actions[-1]["ticks_before"] != 3585):
            raise ValueError("retained long-B independent action schedule boundary differs")
        terminal = value["natural_terminal"]
        if (terminal.get("target_source_tick") != 3584 or
                terminal.get("first_later_natural_draw_return", {}).get("source_tick") != 3585 or
                terminal.get("first_later_natural_draw_return", {}).get("draw_ordinal") != 3582):
            raise ValueError("retained long-B natural terminal coordinates differ")
        selector = value["action_rng"]["selector"]
        if (selector != {"schema": "melee-web-later-b-long-action-rng-v1",
                         "reference_id": "original-b-5422-951-long-v1", "prefix_cursor": 951,
                         "reference_go_tick": 84, "first_action": 168, "last_action": 7167} or
                len(value["action_rng"].get("rows", [])) != 7000):
            raise ValueError("retained long-B diagnostic action-index selector differs")
        if any(value.get(flag) is not False for flag in
               ("runtime_admission", "browser_parity", "per_owner_rng_parity", "C3")):
            raise ValueError("reference-only long-B projection claims native/runtime acceptance")

    @classmethod
    def _build_fixture(cls):
        running_declaration = 'extern "C" int melee_web_native_menu_running();'
        if cls.cpp_source.count(running_declaration) != 1:
            raise AssertionError("actual running getter forward declaration must be unique")
        abort_body = _function(cls.cpp_source, "int later_b_gate2_unarmed_abort(){")
        running_read = ' retirement.running_after_close=melee_web_native_menu_running();'
        if abort_body.count(running_read) != 1:
            raise AssertionError("actual abort running read must be unique")
        if cls.cpp_source.index(running_declaration) >= cls.cpp_source.index("int later_b_gate2_unarmed_abort(){"):
            raise AssertionError("running getter declaration must precede actual abort read")
        extracted = [
            "namespace melee_web::stadium_first_css_diagnostic {\n" +
            _function(cls.hex_source, "inline std::string hex(const uint8_t* bytes, size_t size) {") + "\n}",
            running_declaration + "\nstatic int actual_abort_running_read_control() {\n" +
            " auto& retirement=matched_setup_cf_retirement;\n" + running_read +
            "\n return retirement.running_after_close;\n}\nextern \"C\" {\n" +
            _function(cls.cpp_source, "int melee_web_native_menu_running(){") + "\n}",
            _function(cls.cpp_source, "bool later_b_gate2_uuid_valid(const std::string& value){"),
            _function(cls.cpp_source, "bool later_b_gate2_constructor_phase_trace_valid(const MeleeWebStadiumSetupSnapshot& setup){"),
            _function(cls.cpp_source, "bool later_b_gate2_constructor_snapshot_valid(const MeleeWebStadiumSetupSnapshot& setup,\n                                               bool long_reference=false){"),
            _function(cls.cpp_source, "bool later_b_gate2_first_go_snapshot_valid(\n    const MeleeWebStadiumGoAlignmentSnapshot& observed,unsigned source_tick,\n    uint64_t world_generation){"),
            _function(cls.cpp_source, "bool later_b_gate2_parent_identity_valid(const MatchedSetupCfState& state){"),
            _function(cls.cpp_source, "std::string later_b_gate2_constructor_snapshot_json(const MeleeWebStadiumSetupSnapshot& setup){"),
            _function(cls.cpp_source, "std::string first_css_browser_draw_json_quote(const std::string& value){"),
            _function(cls.cpp_source, "std::string later_b_gate2_transfer_proof_json(const MatchedSetupCfState& state,bool parent_close_checked){"),
            _function(cls.cpp_source, "static int later_b_gate2_manifest_arm(const uint8_t* manifest,size_t manifest_bytes){"),
            _function(cls.cpp_source, "static int later_b_gate2_long_manifest_arm(const uint8_t* manifest,size_t manifest_bytes){"),
            _function(cls.cpp_source, "bool later_b_gate2_unarmed_abort_ready(const MatchedSetupCfState& state){"),
            _function(cls.cpp_source, "void matched_setup_cf_fail(const char* error){"),
            _function(cls.cpp_source, "std::string matched_setup_cf_json(){"),
            _function(cls.cpp_source, "const char* melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status(){"),
        ]
        # Extract the entire actual last_action assignment, not a duplicate generation predicate.
        action_start = '  state.last_action_json="{\\"schedule_index\\":'
        action_end = '\n  if(state.later_b_gate2_mode&&index+1==state.action_count)'
        if cls.cpp_source.count(action_start) != 1 or cls.cpp_source.count(action_end) != 1:
            raise AssertionError("actual last_action assignment endpoints must be unique")
        assignment = cls.cpp_source[cls.cpp_source.index(action_start):cls.cpp_source.index(action_end)]
        extracted.append("""
static std::string actual_action_json_control(bool tick_action,unsigned index=0,unsigned ordinal=0) {
  auto& state=matched_setup_cf;
  const std::string v2_counter_json, go2_state_observation_json="null",
      pad_queue_observation_json="null", common_sample_json="null",
      later_b_actual_go_event_json="null", later_b_post_go_typed22_state_json="null";
  std::ostringstream actual; actual << "null";
""" + assignment + "\n return state.last_action_json;\n}\n")
        retirement = _function(cls.cpp_source, "std::string matched_setup_cf_retirement_json(){")
        ordinary = '\n out<<"{\\"schema\\":\\"melee-web-stadium-matched-setup-cf-retirement-v1\\""'
        if retirement.count(ordinary) != 1:
            raise AssertionError("actual new-only abort/ordinary serializer boundary must be unique")
        # Preserve actual abort branch. Only the unrelated legacy dispatch remains modeled.
        extracted.append(retirement[:retirement.index(ordinary)] +
                         '\n return "{\\"schema\\":\\"modeled-retirement-dispatch\\"}";\n}')

        # Execute only the actual post-action owner-refresh and awaiting-owner
        # comparison branches around the retained tick-84/draw-84 boundary.
        # Snapshot values not present in the retained typed22 row are explicit
        # modeled leaves; this reducer does not recreate a native match.
        draw84_refresh = _function(
            cls.cpp_source,
            "  if(state.version2&&(!state.later_b_gate2_mode||!tick_action)&&state.source_ticks>c_tick&&")
        draw84_owner = _function(
            cls.cpp_source,
            " if(state.awaiting_approval&&((state.version2&&state.last_ticks_after>state.c_source_tick)||")
        draw84_probe = r"""
namespace draw84_source_probe {
struct MeleeWebStadiumIdleSnapshot {
  uint32_t version=0; uint64_t world_generation=0, world_ticks=0; uint32_t source_frame=0;
  int32_t map2_gate=0, map2_countdown=0; int16_t map2_phase=0, map2_form=0;
  int16_t map2_history_e0=0, map2_history_e2=0, display_mode=0, display_e6=0, display_e8=0;
  unsigned char display_timer_native_bytes[sizeof(int32_t)]{};
  uint32_t display_timer_initialization_observed=0;
  int16_t display_previous_mode=0, display_selected_player=0, display_f2=0;
  uint32_t display_flags=0, sis_font_slot=0, sis_static_text_role=0, sis_dynamic_text_role=0;
  uint32_t sis_context_present=0, sis_title_entry_seen=0; int32_t sis_title_entry_index=0;
  uint32_t title_bytes_captured=0; int32_t sis_source_heap_handle=0;
  uint64_t sis_world_generation=0, sis_allocation_generation=0, sis_source_epoch=0;
  uint32_t sis_requested_bytes=0; int32_t map2_source_heap_handle=0;
  uint64_t map2_world_generation=0, map2_allocation_generation=0;
  uint32_t map2_requested_bytes=0, map2_buffer_origin=0;
  uint32_t typed_relations_version=0, map2_gate_mask_40=0, map5_root_relation=0;
  uint32_t sis_font1_present=0, title_child_present=0, title_wrapper_present=0;
  uint32_t title_static_present=0, title_dynamic_present=0;
};
struct MeleeWebStadiumGoAlignmentEvent {
  uint64_t generation=0, world_ticks=0; uint32_t source_frame=0, sequence=0;
  uintptr_t object_identity=0, proc_identity=0; int32_t callback_index=0, source_branch=0;
  int32_t map2_gate=0, display_mode=0, remap_branch=0, callback_identity=0, hud_enabled=0;
  uint32_t source_seed=0, source_seed_valid=0;
};
struct MeleeWebStadiumGoAlignmentSnapshot {
  uint64_t generation=0, armed_world_ticks=0; uint32_t armed_source_frame=0, armed=0;
  uint32_t failure=0, expected_branch=0, next_sequence=0;
  MeleeWebStadiumGoAlignmentEvent stage_before, stage_after, go_after, hud_after;
};
static void check(bool ok, const char* error) {
  if (!ok) throw std::runtime_error(error ? error : "modeled check refused");
}
struct ProbeState {
  bool version2=true, later_b_gate2_mode=true, awaiting_approval=true;
  uint32_t source_ticks=85, last_ticks_after=85, c_source_tick=84;
  int last_action_kind=1; uint32_t last_ordinal=84;
  MeleeWebStadiumIdleSnapshot idle{}; MeleeWebStadiumGoAlignmentSnapshot go{};
};
struct SnapshotService {
  MeleeWebStadiumIdleSnapshot live_idle{}; MeleeWebStadiumGoAlignmentSnapshot live_go{};
  unsigned idle_reads=0, go_reads=0;
  bool diagnostic_stadium_idle_snapshot(MeleeWebStadiumIdleSnapshot* out,char*,size_t) {
    ++idle_reads; *out=live_idle; return true;
  }
  bool diagnostic_stadium_go_alignment_snapshot(MeleeWebStadiumGoAlignmentSnapshot* out,char*,size_t) {
    ++go_reads; *out=live_go; return true;
  }
};
static ProbeState state;
static SnapshotService service;
static SnapshotService* match=&service;
__GO_EVENT_EQUAL__
__GO_EQUAL__
__IDLE_EQUAL__
static void actual_after_action_refresh(bool tick_action,unsigned ordinal){
 auto& state=draw84_source_probe::state; const unsigned c_tick=84U,f_tick=85U;
 __REFRESH__
}
static void actual_awaiting_approval_check(){
 auto& state=draw84_source_probe::state; auto* match=draw84_source_probe::match; char error[256]{};
 MeleeWebStadiumGoAlignmentSnapshot go{};
 check(match->diagnostic_stadium_go_alignment_snapshot(&go,error,sizeof(error)),
       error[0]?error:"modeled GO snapshot refused");
 __OWNER__
}
static void load_retained_tick84_leaf() {
  state=ProbeState{}; service=SnapshotService{};
  // These are the observed action-168 typed22 values and actual GO event from
  // the retained failed capture. Unreported identity/resource fields stay
  // zero-valued modeled leaves, not claimed observations.
  state.idle.version=2; state.idle.world_generation=7; state.idle.world_ticks=85;
  state.idle.source_frame=0; state.idle.map2_countdown=3741; state.idle.map2_form=5;
  state.idle.map2_history_e0=-1; state.idle.map2_history_e2=-1;
  state.idle.display_e6=64; state.idle.display_e8=64; state.idle.display_mode=11;
  state.idle.display_previous_mode=10; state.idle.display_selected_player=99;
  state.idle.typed_relations_version=1; state.idle.map2_gate_mask_40=0;
  state.idle.map5_root_relation=1; state.idle.sis_font1_present=1;
  state.idle.title_child_present=1; state.idle.title_wrapper_present=1;
  state.idle.title_static_present=1; state.idle.title_dynamic_present=1;
  state.go.generation=7; state.go.armed=1; state.go.failure=0;
  state.go.expected_branch=0; state.go.next_sequence=3;
  state.go.go_after.generation=7; state.go.go_after.world_ticks=84;
  state.go.go_after.source_frame=0; state.go.go_after.sequence=3;
  state.go.go_after.callback_index=3; state.go.go_after.source_branch=0;
  state.go.go_after.map2_gate=0; state.go.go_after.display_mode=11;
  state.go.go_after.remap_branch=0; state.go.go_after.callback_identity=1;
  state.go.go_after.hud_enabled=-1; state.go.go_after.source_seed=0x1bc52546U;
  state.go.go_after.source_seed_valid=1;
  service.live_idle=state.idle; service.live_go=state.go;
}
static void draw84_refresh_and_owner_controls() {
  load_retained_tick84_leaf();
  actual_after_action_refresh(true,84); // The retained tick already stored this snapshot.
  assert(service.idle_reads==0 && service.go_reads==0);
  actual_after_action_refresh(false,84); // Actual Gate2 draw now captures the completed owner state.
  assert(service.idle_reads==1 && service.go_reads==1);
  actual_awaiting_approval_check();
  assert(service.idle_reads==2 && service.go_reads==2);
  std::cout << "GATE2_UNCHANGED pass retained_action=168 next_action=169 draw_refresh=1 owner_compare=1\n";

  load_retained_tick84_leaf();
  service.live_idle.display_e8=65; // Explicit modeled post-draw mutation, not a captured value.
  actual_after_action_refresh(false,84);
  assert(service.idle_reads==1 && service.go_reads==1);
  assert(state.idle.display_e8==65);
  actual_awaiting_approval_check();
  assert(service.idle_reads==2 && service.go_reads==2);
  std::cout << "GATE2_DRAW_REFRESH pass modeled_display_e8=65 refreshed_before_compare=1\n";
  service.live_idle.display_e8=66; // Between-action mutation must still fail closed.
  bool refused=false;
  try { actual_awaiting_approval_check(); }
  catch (const std::exception& e) {
    refused=std::string(e.what())=="Matched Setup C/F native owner state changed while awaiting host approval";
  }
  assert(refused);
  std::cout << "BETWEEN_ACTION_CHANGE refused after_refresh=1\n";

  load_retained_tick84_leaf();
  state.later_b_gate2_mode=false; // Existing version-2 refresh control.
  service.live_idle.display_e8=65;
  actual_after_action_refresh(false,84);
  assert(service.idle_reads==1 && service.go_reads==1);
  actual_awaiting_approval_check();
  assert(service.idle_reads==2 && service.go_reads==2);
  std::cout << "VERSION2_REFRESH_CONTROL pass refreshed_before_compare=1\n";
}
} // namespace draw84_source_probe
"""
        draw84_probe = (draw84_probe
                        .replace("__GO_EVENT_EQUAL__", _function(
                            cls.cpp_source,
                            "bool matched_setup_cf_go_event_equal(const MeleeWebStadiumGoAlignmentEvent& a,"))
                        .replace("__GO_EQUAL__", _function(
                            cls.cpp_source,
                            "bool matched_setup_cf_go_equal(const MeleeWebStadiumGoAlignmentSnapshot& a,"))
                        .replace("__IDLE_EQUAL__", _function(
                            cls.cpp_source,
                            "bool matched_setup_cf_idle_equal(const MeleeWebStadiumIdleSnapshot& a,"))
                        .replace("__REFRESH__", draw84_refresh)
                        .replace("__OWNER__", draw84_owner))
        extracted.append(draw84_probe)
        pad_rows = []
        for action in cls.projection["actions"]:
            if action["kind"] == "tick":
                pad_rows.append("{" + ",".join(json.dumps(pad) for pad in action["pads"]) + "}")
        if len(pad_rows) != 180:
            raise ValueError("actual projection did not yield 180 PAD rows")
        pads = ",\n".join(pad_rows)
        long_pad_rows = []
        if cls.long_projection is not None:
            for action in cls.long_projection["actions"]:
                if action["kind"] == "tick":
                    long_pad_rows.append("{" + ",".join(json.dumps(pad) for pad in action["pads"]) + "}")
            if len(long_pad_rows) != 3585:
                raise ValueError("long projection did not yield 3585 actual PAD rows")
        else:
            long_pad_rows = ['{"' + '0000000000000000000000' + '","' +
                             '0000000000000000000000' + '","' +
                             '0000000000000000000000' + '","' +
                             '0000000000000000000000' + '"}']
        long_pads = ",\n".join(long_pad_rows)
        fixture_body = FIXTURE_BODY.replace(
            'else if(mode=="step-error") step_error_controls();',
            'else if(mode=="step-error") step_error_controls();\n  else if(mode=="draw84") draw84_source_probe::draw84_refresh_and_owner_controls();')
        return (PRELUDE + fixture_body
                .replace("__PADS__", pads)
                .replace("__LONG_PADS__", long_pads)
                .replace("__LONG_TESTS__", LONG_TESTS)
                .replace("__LONG_MANIFEST_HEX__", cls.long_manifest.hex() if cls.long_projection is not None else "")
                .replace("__EXTRACTED_FUNCTIONS__", "\n\n".join(extracted)))

    def test_actual_mwb3_parser_accepts_exact_retained_long_rows_and_keeps_go_unobserved(self):
        if self.long_projection is None:
            self.skipTest("optional retained long-B projection fixture is absent")
        output = self._run_fixture("long-parser-accept")
        self.assertIn("MWB3_ACCEPT actions=7168 ticks=3585 draws=3583 prefix=951 GO=84 actual_go_observed=false", output)
        statuses = [line.removeprefix("MWB3_STATUS ") for line in output.splitlines()
                    if line.startswith("MWB3_STATUS ")]
        self.assertEqual(len(statuses), 1)
        status = json.loads(statuses[0])
        self.assertEqual(status["schema"], "melee-web-stadium-later-b-long-native-v1")
        # The modeled arm leaf does not set the runtime's armed state.
        self.assertEqual(status["state"], "idle")
        self.assertEqual(status["cursor"], 0)
        self.assertEqual(status["action_count"], 7168)
        self.assertEqual(status["actual_go"], {"observed": False, "tick": None})

    def test_actual_mwb3_parser_refuses_geometry_literal_pad_and_owner_controls(self):
        if self.long_projection is None:
            self.skipTest("optional retained long-B projection fixture is absent")
        output = self._run_fixture("long-parser-controls")
        self.assertIn("MWB3_CONTROLS passed=geometry,token,pad,order,context,seed,setup-order,one-use", output)

    def _run_fixture(self, mode):
        result = subprocess.run([str(self.binary), mode], check=False, timeout=20,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_actual_mwb2_parser_accepts_exact_retained_rows_and_keeps_go_unobserved(self):
        self._run_fixture("parser-accept")

    def test_actual_parser_refuses_geometry_pad_topology_and_mixed_seed_controls(self):
        self._run_fixture("parser-controls")

    def test_actual_go_alignment_validator_requires_the_native_event_order(self):
        self._run_fixture("go")

    def test_actual_unarmed_predicate_and_gate1_unarmed_status_dispatch(self):
        output = self._run_fixture("lifecycle-status")
        records = {}
        def no_duplicate_keys(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise AssertionError(f"duplicate native JSON key: {key}")
                result[key] = value
            return result
        with self.assertRaisesRegex(AssertionError, "duplicate native JSON key: cursor"):
            json.loads('{"cursor":0,"cursor":1}', object_pairs_hook=no_duplicate_keys)
        for line in output.splitlines():
            label, payload = line.split(" ", 1)
            records[label] = json.loads(payload, object_pairs_hook=no_duplicate_keys)
        proof = records["TRANSFER"]
        self.assertEqual(proof["schema"], "melee-web-stadium-later-b-parent-transfer-v1")
        self.assertEqual(proof["recipe_sha256"], "5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755")
        self.assertEqual(len(proof["recipe_sha256"]), 64)
        self.assertEqual(proof["reference_id"], "original-b-2013-943-v1")
        self.assertEqual(proof["cursor"], 943)
        self.assertEqual(proof["notification_order"], 4330)
        self.assertEqual(proof["world_generation"], 7)
        self.assertEqual(proof["setup_seed_hex"], "4b25c79b")
        self.assertEqual((proof["source_ticks"], proof["source_draws"]), (0, 0))
        self.assertTrue(proof["parent_identity_checked"])
        self.assertTrue(proof["parent_archived"])
        self.assertEqual(proof["stage"], "after-parent-move")
        self.assertEqual(records["LIVE"]["schema"], "melee-web-stadium-later-b-gate2-native-v1")
        self.assertEqual(records["LIVE"]["actual_go"], {"observed": False, "tick": None})
        self.assertEqual(records["LIVE"]["constructor_shared_projection"], {
            "scope": "shared-constructor-aggregate-only", "entry_count": 1,
            "parameter_roles": {"min_parameter": "parameter_x0", "max_parameter": "parameter_x4"},
            "min_parameter": 3600, "max_parameter": 3800, "random_bound": 200,
            "random_result": 142, "seed_before": "ba5dac05", "seed_after": "b6cdeeb4",
            "randi_call_count": 1,
        })
        self.assertEqual(records["OBSERVED"]["actual_go"], {"observed": True, "tick": 84})
        self.assertEqual(records["OBSERVED"]["post_go_typed22_state_count"], 96)
        expected_coordinates = {
            "ACTION_TICK": {"source_tick": 0, "draw_ordinal": None, "recipe_input_index": 943},
            "ACTION_DRAW": {"source_tick": 1, "draw_ordinal": 0, "recipe_input_index": None},
            "LONG_ACTION_TICK": {"source_tick": 84, "draw_ordinal": None,
                                 "recipe_input_index": 1035},
            "LONG_ACTION_DRAW": {"source_tick": 85, "draw_ordinal": 84,
                                 "recipe_input_index": None},
        }
        coordinate_keys = {"source_tick", "draw_ordinal", "recipe_input_index"}
        def accept_action_receipt(label, receipt):
            coordinates = receipt.get("source_coordinates")
            if (not isinstance(coordinates, dict) or set(coordinates) != coordinate_keys or
                    coordinates != expected_coordinates[label] or
                    "global_recipe_input_index" in receipt):
                raise ValueError("native action source_coordinates contract differs")
            return coordinates
        for label, expected in expected_coordinates.items():
            self.assertEqual(accept_action_receipt(label, records[label]), expected)
        bad_receipt = dict(records["LONG_ACTION_TICK"])
        bad_receipt["source_coordinates"] = dict(bad_receipt["source_coordinates"])
        bad_receipt["global_recipe_input_index"] = bad_receipt[
            "source_coordinates"].pop("recipe_input_index")
        with self.assertRaisesRegex(ValueError, "source_coordinates contract"):
            accept_action_receipt("LONG_ACTION_TICK", bad_receipt)
        bad_receipt = dict(records["LONG_ACTION_DRAW"])
        bad_receipt["source_coordinates"] = dict(bad_receipt["source_coordinates"])
        bad_receipt["source_coordinates"]["recipe_input_index"] = 1035
        with self.assertRaisesRegex(ValueError, "source_coordinates contract"):
            accept_action_receipt("LONG_ACTION_DRAW", bad_receipt)
        self.assertEqual(records["LIVE"]["state"], "armed")
        self.assertEqual(records["LIVE"]["cursor"], 0)
        for label in ("ACTION_TICK", "ACTION_DRAW", "LEGACY_TICK"):
            self.assertEqual(records[label]["world_generation"], 7)
        self.assertNotIn("world_generation", records["LEGACY_DRAW"])
        for label in ("RETIRED", "LEGACY"):
            self.assertEqual(records[label]["schema"], "modeled-retirement-dispatch")
        self.assertEqual(records["ABORT"]["schema"], "melee-web-stadium-later-b-unarmed-abort-v1")
        self.assertTrue(records["ABORT"]["native_closed"])
        self.assertIsNone(records["ABORT"]["error"])
        self.assertFalse(records["ABORT_ERROR"]["native_closed"])
        self.assertEqual(records["ABORT_ERROR"]["error"], 'abort "failed"\nretained')

    def test_actual_outer_step_catch_preserves_first_service_error_in_native_status(self):
        step = _function(self.cpp_source,
                         "int melee_web_native_menu_stadium_first_sss_vs_setup_cf_step_once(){")
        self.assertIn("matched_setup_cf_fail(error.what())", step)
        fail = _function(self.cpp_source, "void matched_setup_cf_fail(const char* error){")
        self.assertIn("if(state.error.empty())state.error=error?error:", fail)
        output = self._run_fixture("step-error")
        label, payload = output.strip().split(" ", 1)
        self.assertEqual(label, "STEP_ERROR")
        status = json.loads(payload)
        self.assertTrue(status["failed"])
        self.assertEqual(status["error"], "first native service error")

    def test_actual_tick84_draw84_owner_refresh_branches_with_modeled_draw_control(self):
        output = self._run_fixture("draw84")
        self.assertIn("GATE2_UNCHANGED pass retained_action=168 next_action=169 draw_refresh=1 owner_compare=1", output)
        self.assertIn("GATE2_DRAW_REFRESH pass modeled_display_e8=65 refreshed_before_compare=1", output)
        self.assertIn("BETWEEN_ACTION_CHANGE refused after_refresh=1", output)
        self.assertIn("VERSION2_REFRESH_CONTROL pass refreshed_before_compare=1", output)


if __name__ == "__main__":
    unittest.main()
