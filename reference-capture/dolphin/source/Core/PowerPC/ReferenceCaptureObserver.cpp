// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/ReferenceCaptureObserver.h"
#include "Core/PowerPC/ReferenceAllocationObserver.h"
#include "Core/PowerPC/ReferenceAllocationProfile.h"
#include "Core/PowerPC/ReferenceInputStream.h"
#include "Core/PowerPC/ReferenceSdInitState.h"
#include "Core/PowerPC/ReferenceOrdinaryTimeoutState.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Common/DirectIOFile.h"
#include "Common/Crypto/SHA1.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "DiscIO/DiscUtils.h"
#include "DiscIO/VolumeDisc.h"

#include <mbedtls/sha256.h>

namespace ReferenceCapture
{
namespace
{
constexpr char EXPECTED_DOL_SHA256[] =
    "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646";
constexpr char EXPECTED_DOL[] =
    "08e0bf20134dfcb260699671004527b2d6bb1a45";
constexpr char EXPECTED_COMMIT[] = "c77bbaa0f372c3f72281602a8b087206706542cb";
constexpr u16 SCHEMA = 1;
constexpr u32 MAGIC = 0x4f52574d; // little-endian "MWRO"
// Headless Dolphin can advance much faster than the host while source draw
// callbacks emit a burst of records for a frame. Keep enough bounded
// headroom for those bursts; overflow remains a hard capture failure.
constexpr size_t RING_SIZE = 1024;
constexpr size_t RING_PAYLOAD = 256 * 1024;
constexpr size_t MAX_SLICES = 64;
constexpr size_t MAX_RAW = 192 * 1024;
constexpr u32 PAD_READ_HSD_CALLER = 0x80376A28;
constexpr u32 CSS_ENTER_RETURN = 0x802669F0;
constexpr u32 CSS_ENTER = 0x8026688C;
constexpr u32 SSS_ENTER = 0x8025A998;
constexpr u32 SSS_ENTER_RETURN = 0x8025B84C;
// Menu steering sources, as the retail menu owners read them:
// mnStageSel_803F06D0 is the 30-entry authored stage list (stride 0x1C, stage
// kind at +0xB), mnStageSel_804D6CAE is the highlighted index, and
// mnCharSel_804A0BC0 holds one CSS cursor pointer per port.
constexpr u32 STAGE_SELECT_TABLE = 0x803F06D0;
constexpr size_t STAGE_SELECT_STRIDE = 0x1C;
constexpr size_t STAGE_SELECT_COUNT = 30;
constexpr size_t STAGE_SELECT_KIND_OFFSET = 0xB;
constexpr u32 STAGE_SELECT_INDEX = 0x804D6CAE;
constexpr u32 CSS_CURSOR_POINTERS = 0x804A0BC0;
// mnCharSel_803F0DFC is the authored CSS door state (0x90 bytes: four 0x24
// entries carrying each port's selected icon and door coordinates).
constexpr u32 CSS_DOORS_STATE = 0x803F0DFC;
constexpr size_t CSS_DOORS_BYTES = 0x90;
constexpr size_t CSS_CURSOR_BYTES = 0x14;
constexpr size_t CSS_CURSOR_PORTS = 4;
constexpr u32 MENU_AUDIO_STREAM_START = 0x8038E8EC;
// Authored gmm_x0 layout behind gmMainLib_804D3EE0. gmMainLib_GetSaveData
// returns &gmm_x0.thing, whose block the retail accessors read at +0x1868:
// GameRules is the asserted 0x18-byte rules block at +0x1850, so the save
// block starts at +0x1868 and its asserted 0x55E8 bytes end at the +0x6E50
// trailing pad. (The decomp's own /* 0x1898 */ comment on that member
// contradicts both ASSERT_SIZE(struct GameRules, 0x18) and the 0x6E50 pad, so
// the observer keeps the arithmetic-consistent offset and reads the block the
// accessors actually use.) The observer copies these ranges as authored; field
// and packed-bit interpretation belongs to the offline decoder.
constexpr u32 PROFILE_ROOT_GLOBAL = 0x804D3EE0;
constexpr u32 PROFILE_GAME_RULES_OFFSET = 0x1850;
constexpr size_t PROFILE_GAME_RULES_SIZE = 0x18;
constexpr u32 PROFILE_SAVE_DATA_OFFSET = 0x1868;
constexpr size_t PROFILE_SAVE_DATA_SIZE = 0x55E8;
constexpr u32 PROFILE_LAST_BYTE_OFFSET =
    PROFILE_SAVE_DATA_OFFSET + PROFILE_SAVE_DATA_SIZE - 1;
constexpr u16 WHOLE_SESSION_FLAG = 1;
constexpr u32 WHOLE_SESSION_MIN_MATCHES = 3;
constexpr u32 WHOLE_SESSION_MAX_MATCHES = 64;
constexpr size_t CPU_PROBE_TICK_WINDOW_MAX = 64;
constexpr size_t CPU_PROBE_MAX_RECORDS = 4096;
constexpr size_t CPU_PROBE_MAX_JSON_BYTES = 64 * 1024 * 1024;
constexpr size_t ITEM_PROBE_MAX_RECORDS = 2048;
constexpr size_t ITEM_PROBE_MAX_JSON_BYTES = 4 * 1024 * 1024;
constexpr size_t ITEM_PROBE_MAX_PROCESSES = 16;
constexpr u32 ITEM_PROBE_ARROW_GOBJ = 0x80e29760;
constexpr u32 ITEM_PROBE_ARROW_ITEM = 0x80e1b920;
constexpr u32 ITEM_PROBE_ARROW_KIND = 65;
constexpr u32 CPU_PROBE_STACK_SIZE = 0x100;
constexpr u32 CPU_PROBE_FIGHTER_HEAD_SIZE = 0x100;
constexpr u32 CPU_PROBE_FIGHTER_CPU_SIZE = 0x57c;
constexpr u32 CPU_PROBE_FIGHTER_FLAGS_OFFSET = 0x2218;
constexpr u32 CPU_PROBE_RANDOM_ADDRESS = 0x804d5f90;

constexpr std::array<u8, 32> EXPECTED_DOL_SHA256_BYTES = {
    0xdc, 0x21, 0x50, 0x45, 0x13, 0x42, 0x43, 0x50, 0xbd, 0xa1, 0x7a,
    0x7c, 0x65, 0xe8, 0x23, 0x71, 0xb4, 0x51, 0x12, 0xa5, 0xdf, 0xc1,
    0xe9, 0xf2, 0x74, 0x9a, 0x8b, 0x7a, 0xb0, 0xef, 0xf6, 0x46};
constexpr std::array<u8, 20> EXPECTED_DOL_SHA1_BYTES = {
    0x08, 0xe0, 0xbf, 0x20, 0x13, 0x4d, 0xfc, 0xb2, 0x60, 0x69,
    0x96, 0x71, 0x00, 0x45, 0x27, 0xb2, 0xd6, 0xbb, 0x1a, 0x45};

enum class Event : u16
{
  Handshake = 1,
  Start = 2,
  Boundary = 3,
  Progress = 4,
  Error = 5,
  End = 6,
};

constexpr bool ShouldWriteObserverEvent(Event event, bool item_probe_summary_stream)
{
  return !item_probe_summary_stream || event == Event::Handshake || event == Event::Start;
}

enum class Boundary : u16
{
  PadPoll = 1,
  PadConsume = 2,
  FighterCreate = 3,
  Entry = 4,
  Setup = 5,
  SourceTick = 6,
  DrawEnter = 7,
  DrawReturn = 8,
  ResultEnter = 9,
  ResultReturn = 10,
  SceneTeardown = 11,
  SceneExit = 12,
  CssEnter = 13,
  CssExit = 14,
  SssEnter = 15,
  SssExit = 16,
  VsExit = 17,
  VsExitReturn = 18,
  VsModeExit = 19,
  ResultsEnter = 20,
  ResultsExit = 21,
  ResultsModeExit = 22,
  ResultsGObjProcess = 23,
  ReturnCss = 24,
  CssCancelEnter = 25,
  PrizeModeEnter = 26,
  PrizeSceneEnter = 27,
  PrizeSceneExit = 28,
  PrizeModeExit = 29,
  StartupPrizeModeExit = 30,
};

// gmMain calls HSD_PadInit(5,...). gm_1A45 drains the entire snapshot of
// that queue before one DrawReturn; never interrupt its authored batch.
struct EntityPrefixBoundaryProgress
{
  static constexpr u32 comparison_ticks = 60;
  static constexpr u32 queue_capacity = 5;
  static constexpr u32 maximum_ticks = comparison_ticks + queue_capacity - 1;
  u32 observations = 0, batch_ticks = 0;
  bool drawing = false, qualified = false;
  // -1 rejects, 0 retains, 1 is the first qualifying checked draw.
  int Observe(Boundary boundary, u32 tick, u8 qnum)
  {
    if (qualified || qnum != queue_capacity) return -1;
    if (boundary == Boundary::SourceTick)
    {
      if (drawing || tick != observations || observations >= maximum_ticks ||
          batch_ticks >= queue_capacity) return -1;
      ++observations;
      ++batch_ticks;
    }
    else if (boundary == Boundary::DrawEnter)
    {
      if (drawing || tick != observations) return -1;
      drawing = true;
    }
    else if (boundary == Boundary::DrawReturn)
    {
      if (!drawing || tick != observations) return -1;
      drawing = false;
      batch_ticks = 0;
      if (observations >= comparison_ticks) { qualified = true; return 1; }
    }
    else return -1;
    return 0;
  }
};

enum class SliceTag : u16
{
  PadStatusAll4 = 1,
  PadQueue = 2,
  PadSlot = 3,
  MatchSetup = 4,
  FighterHead = 5,
  FighterInputAnim = 6,
  FighterDamageShield = 7,
  FighterStocks = 8,
  CpuState = 9,
  Camera = 10,
  CameraProjection = 11,
  Hud = 12,
  Magnifier = 13,
  MatchClock = 14,
  Result = 15,
  SceneEntityHeads = 16,
  SceneRouting = 17,
  FighterSubject = 18,
  RngPointer = 19,
  RngValue = 20,
  PadSnapshot = 21,
  RetraceCount = 22,
  SourceVICount = 23,
  FighterCreateContext = 24,
  SceneEntityCount = 25,
  SceneEntityHeadsPointer = 26,
  PadPollCaller = 27,
  CameraObjectPointer = 28,
  SceneRequest = 29,
  SceneFrame = 30,
  MenuCssState = 31,
  MenuSssState = 32,
  MenuAudio = 33,
  MenuAudioVoice = 34,
  MenuSssRoute = 35,
  ProfileCharacters = 36,
  ProfileStages = 37,
  ProfileGameRules = 38,
  ProfileSaveData = 39,
  // 40 takes the next free number after the typed profile tags so no shipped
  // tag is ever renumbered.
  SceneKind = 40,
  StageSelectIndex = 41,
  StageSelectKind = 42,
  MenuCssCursor = 43,
  MenuCssDoors = 44,
  MenuMainFlow = 45,
  MenuMainInput = 46,
  MenuCssModel = 47,
  MenuCssLiveState = 48,
  MenuCssSlider = 49,
  MenuCssContext = 50,
  MenuCssKoCounts = 51,
  PlayerEntities = 52,
  PlayerEntityUserData = 53,
  SdRumblePorts = 54,  // Opt-in Progress JSON only; never a full save-data slice.
  SdStageCooldown = 55,  // Recipe-five menu route only; original acceptance gate.
  SdItemsLock = 56,  // Reduced Items owner only; original u8 animation lock.
  PlayerIdentity = 57,  // Opt-in entity profile: authored StaticPlayer header.
  PlayerTransformed = 58,  // Opt-in Zelda/Sheik active-entity indexes.
};

struct SliceRef
{
  SliceTag tag;
  u16 flags;
  u32 address;
  u32 size;
  u32 offset;
};

const u8* SparsePadSlotBytes(const u8* raw, size_t raw_size, const SliceRef* slices,
                             size_t slice_count)
{
  if (!raw || !slices || !slice_count)
    return nullptr;
  const SliceRef& slot = slices[slice_count - 1];
  if (slot.tag != SliceTag::PadSlot || slot.size != 0x30 || slot.offset > raw_size ||
      slot.size > raw_size - slot.offset)
    return nullptr;
  return raw + slot.offset;
}

struct Slot
{
  std::atomic<bool> ready{false};
  Event event = Event::Progress;
  u64 sequence = 0;
  u64 timestamp_ns = 0;
  u32 guest_pc = 0;
  u32 source_tick = 0;
  u32 draw_ordinal = 0;
  u32 payload_size = 0;
  u32 checksum = 0;
  std::array<u8, RING_PAYLOAD> payload{};
};

struct CpuProbeFighterRecord
{
  u32 pointer = 0;
  std::array<u8, CPU_PROBE_FIGHTER_HEAD_SIZE> head{};
  std::array<u8, CPU_PROBE_FIGHTER_CPU_SIZE> cpu{};
  std::array<u8, 8> flags{};
};

struct CpuProbeRecord
{
  u32 pc = 0;
  u32 expected_word = 0;
  u32 source_tick = 0;
  u32 match = 0;
  u32 lr = 0;
  std::array<u32, 32> gpr{};
  std::array<u64, 7> fpr{};
  std::array<u8, CPU_PROBE_STACK_SIZE> stack{};
  std::array<u8, 8> random_seed_and_pointer{};
  bool effect_group_present = false;
  u32 effect_bank_base = 0;
  u32 effect_group_address = 0;
  u32 effect_palette_offset = 0;
  u32 effect_palette_address = 0;
  bool effect_palette_readable = false;
  std::array<u8, 512> effect_palette{};
  bool effect_literal_palette_readable = false;
  std::array<u8, 512> effect_literal_palette{};
  bool samus_effect_loaded_group_present = false;
  u32 samus_effect_loaded_bank = 0;
  u32 samus_effect_loaded_texture_base = 0;
  u32 samus_effect_loaded_group_address = 0;
  u32 samus_effect_loaded_image_address = 0;
  u32 samus_effect_loaded_palette_address = 0;
  bool samus_effect_gx_tlut_call = false;
  u32 samus_effect_gx_tlut_address = 0;
  bool samus_effect_particle_spawn = false;
  u32 samus_effect_particle_bank = 0;
  u32 samus_effect_particle_kind = 0;
  u32 samus_effect_particle_group = 0;
  std::array<bool, 4> fighter_present{};
  std::array<CpuProbeFighterRecord, 4> fighters{};
};

enum class ItemProbeEvent : u8
{
  Boundary,
  CreationReturn,
  CallbackEntry,
  CallbackReturn,
  Call,
  CallReturn,
  CpuEntry,
  CpuReturn,
  ArrowShieldOverlapCallSite,
  ArrowShieldOverlapCallReturn,
  ArrowItemCallEntry,
  ArrowItemCallReturn,
  ArrowTargetCheckCallSite,
  ArrowTargetCheckCallReturn,
};

struct ItemProbePoint
{
  const char* label;
  u32 address;
  u32 expected_word;
  ItemProbeEvent event;
  u8 pair;
};

// A bounded diagnostic only.  These PCs are instruction-word checked against
// the pinned Rev 2 DOL at every hit.  Pair IDs associate wrapper entries and
// returns without trusting caller-owned or renderer timing.
constexpr std::array<ItemProbePoint, 33> ITEM_PROBE_POINTS = {{
    {"item_physics_wrapper_entry", 0x802697d4, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 1},
    {"item_physics_wrapper_return", 0x80269974, 0x4e800020, ItemProbeEvent::CallbackReturn, 1},
    {"item_collision_wrapper_entry", 0x80269978, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 2},
    {"item_collision_wrapper_return", 0x80269a98, 0x4e800020, ItemProbeEvent::CallbackReturn, 2},
    {"item_hitbox_wrapper_entry", 0x80269b60, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 3},
    {"item_hitbox_updater_call", 0x80269bc0, 0x480077bd, ItemProbeEvent::Call, 4},
    {"item_hitbox_updater_return", 0x80269bc4, 0x7fc3f378, ItemProbeEvent::CallReturn, 4},
    {"item_hitbox_wrapper_return", 0x80269be0, 0x4e800020, ItemProbeEvent::CallbackReturn, 3},
    {"cpu_item_decision_entry", 0x800bb9b4, 0x7c0802a6, ItemProbeEvent::CpuEntry, 5},
    {"cpu_item_decision_return", 0x800bbb88, 0x4e800020, ItemProbeEvent::CpuReturn, 5},
    {"young_link_arrow_anim_entry", 0x802a8cc8, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 6},
    {"young_link_arrow_anim_return", 0x802a90ec, 0x4e800020, ItemProbeEvent::CallbackReturn, 6},
    {"young_link_arrow_physics_entry", 0x802a90f0, 0x80a3002c, ItemProbeEvent::CallbackEntry, 7},
    {"young_link_arrow_physics_return", 0x802a9134, 0x4e800020, ItemProbeEvent::CallbackReturn, 7},
    {"young_link_arrow_collision_entry", 0x802a9138, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 8},
    {"young_link_arrow_collision_return", 0x802a9348, 0x4e800020, ItemProbeEvent::CallbackReturn, 8},
    {"arrow_hit_shield_state_call", 0x802a9c08, 0x4bfbf255, ItemProbeEvent::Call, 9},
    {"arrow_hit_shield_state_return", 0x802a9c0c, 0x7f63db78, ItemProbeEvent::CallReturn, 9},
    {"young_link_arrow_spawn_return", 0x802a8508, 0x4e800020, ItemProbeEvent::CreationReturn, 0},
    {"young_link_arrow_launch_entry", 0x802a850c, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 10},
    {"young_link_arrow_launch_return", 0x802a8980, 0x4e800020, ItemProbeEvent::CallbackReturn, 10},
    {"arrow_shield_overlap_call", 0x80079810, 0x4bf8e3bd, ItemProbeEvent::ArrowShieldOverlapCallSite, 11},
    {"arrow_shield_overlap_return", 0x80079814, 0x2c030000, ItemProbeEvent::ArrowShieldOverlapCallReturn, 11},
    {"arrow_ftcoll_shield_assignment_entry", 0x80077688, 0x7c0802a6, ItemProbeEvent::ArrowItemCallEntry, 12},
    {"arrow_ftcoll_shield_assignment_return", 0x8007796c, 0x4e800020, ItemProbeEvent::ArrowItemCallReturn, 12},
    {"arrow_item_damage_dispatch_entry", 0x80269dc8, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 13},
    {"arrow_item_damage_dispatch_return", 0x80269f10, 0x4e800020, ItemProbeEvent::CallbackReturn, 13},
    {"arrow_hit_shield_callback_entry", 0x802a9b08, 0x7c0802a6, ItemProbeEvent::CallbackEntry, 14},
    {"arrow_hit_shield_callback_return", 0x802a9cdc, 0x4e800020, ItemProbeEvent::CallbackReturn, 14},
    {"arrow_hit_shield_target_test_call", 0x802a9b30, 0x4bfc9211, ItemProbeEvent::ArrowTargetCheckCallSite, 15},
    {"arrow_hit_shield_target_test_return", 0x802a9b34, 0x2c030000, ItemProbeEvent::ArrowTargetCheckCallReturn, 15},
    {"source_tick_return", 0x80390eb4, 0x4e800020, ItemProbeEvent::Boundary, 0},
    {"source_draw_return", 0x80391040, 0x4e800020, ItemProbeEvent::Boundary, 0},
}};

const ItemProbePoint* FindItemProbePoint(u32 address)
{
  for (const ItemProbePoint& point : ITEM_PROBE_POINTS)
  {
    if (point.address == address)
      return &point;
  }
  return nullptr;
}

struct ItemProbeRecord
{
  u32 ordinal = 0;
  u32 pc = 0;
  u32 expected_word = 0;
  u32 source_tick = 0;
  u32 match = 0;
  u32 lr = 0;
  u32 gpr3 = 0;
  u32 gpr4 = 0;
  u32 gpr5 = 0;
  u32 gpr6 = 0;
  std::array<u64, 3> shield_fpr{};
  bool shield_inputs_present = false;
  bool shield_transform_present = false;
  std::array<u8, 0x64> shield_capsule{};
  std::array<u8, 0x24> shield_result{};
  std::array<u8, 0x30> shield_transform{};
  bool arrow_ftcoll_inputs_present = false;
  u64 arrow_ftcoll_angle = 0;
  std::array<u8, 0x0c> arrow_ftcoll_position{};
  std::array<u8, 0x24> arrow_shield_hit{};
  std::array<u8, 0x30> arrow_shield_bone_matrix{};
  u32 scheduler_priority = 0;
  u32 fighter_slot0_pointer = 0;
  u32 fighter_slot0_gobj = 0;
  u32 fighter_slot2_pointer = 0;
  u32 fighter_slot2_gobj = 0;
  u32 decision_item = 0;
  u32 decision_item_status = 0;
  bool arrow_present = false;
  u32 arrow_gobj = 0;
  u32 arrow_item = 0;
  u32 arrow_owner_gobj = 0;
  u32 arrow_kind = 0;
  u32 arrow_list_order = 0;
  u32 arrow_p_link = 0;
  u32 arrow_process_count = 0;
  std::array<u32, ITEM_PROBE_MAX_PROCESSES> arrow_process_priorities{};
  u32 arrow_anim_id = 0;
  u32 arrow_damage_dealt = 0;
  u32 arrow_pending_shield_damage = 0;
  u32 arrow_shield_target_gobj = 0;
  u8 arrow_damage_flags = 0;
  u32 arrow_ground_or_air = 0;
  u32 arrow_shield_angle_bits = 0;
  u32 arrow_common_shield_degrees_bits = 0;
  bool arrow_shield_bounced_present = false;
  bool arrow_hit_shield_present = false;
  std::array<u32, 3> velocity_bits{};
  std::array<u32, 3> position_bits{};
  u32 hitbox0_state = 0;
  std::array<u32, 3> hitbox0_previous_endpoint_bits{};
  std::array<u32, 3> hitbox0_current_endpoint_bits{};
};

constexpr bool IsGuestRange(u32 address, size_t size)
{
  if (size == 0 || size > 0x100000)
    return false;
  const u64 end = static_cast<u64>(address) + size;
  return (address >= 0x80000000U && end <= 0x81800000U) ||
         (address >= 0x90000000U && end <= 0x94000000U);
}

constexpr bool IsMem1Range(u32 address, size_t size)
{
  if (size == 0 || size > 0x100000)
    return false;
  const u64 end = static_cast<u64>(address) + size;
  return address >= 0x80000000U && end <= 0x81800000U;
}

constexpr u32 ReadBE32(const u8* bytes)
{
  return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
         (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
}

void PutU16(u8*& out, u16 value)
{
  *out++ = static_cast<u8>(value);
  *out++ = static_cast<u8>(value >> 8);
}

void PutU32(u8*& out, u32 value)
{
  for (int shift = 0; shift < 32; shift += 8)
    *out++ = static_cast<u8>(value >> shift);
}

void PutU64(u8*& out, u64 value)
{
  for (int shift = 0; shift < 64; shift += 8)
    *out++ = static_cast<u8>(value >> shift);
}

std::string JsonEscape(std::string_view value)
{
  std::string result;
  result.reserve(value.size() + 2);
  for (const unsigned char c : value)
  {
    if (c == '"' || c == '\\')
    {
      result.push_back('\\');
      result.push_back(static_cast<char>(c));
    }
    else if (c < 0x20)
    {
      result += "\\u00";
      constexpr char hex[] = "0123456789abcdef";
      result.push_back(hex[c >> 4]);
      result.push_back(hex[c & 15]);
    }
    else
    {
      result.push_back(static_cast<char>(c));
    }
  }
  return result;
}

std::string Env(const char* name)
{
  const char* value = std::getenv(name);
  return value ? std::string(value) : std::string();
}

bool SdInitRequested()
{
  static const bool requested = !Env("MWRC_SD_INIT").empty();
  return requested;
}

bool OrdinaryTimeoutRequested()
{
  return Env("MWRC_SD_MENU_PROBE") == "ordinary_timeout";
}

bool SparsePairRequested()
{
  return Env("MWRC_SD_MENU_PROBE") == "sparse_pair";
}

bool SparsePadErrorsValid(const u8* pad)
{
  return pad[10] == 0 && pad[22] == 0xff && pad[34] == 0 && pad[46] == 0xff;
}

constexpr u32 SPARSE_SOURCE_SAMPLE_CAP = 8;
constexpr u32 SPARSE_PREPRESS_NEUTRAL_CAP = 6;

bool SparsePadStatusMatches(const u8* pad, bool pressed)
{
  for (u32 port = 0; port < 4; ++port)
  {
    // PADStatus defines bytes 0x00..0x0A; byte 0x0B is trailing ABI padding.
    // Retain all 48 raw bytes in the event, but compare only semantic fields.
    for (u32 byte = 0; byte < 11; ++byte)
    {
      u8 expected = 0;
      if ((port == 1 || port == 3) && byte == 10)
        expected = 0xff;
      if (pressed && port == 0 && byte == 0)
        expected = 0x01;  // A button, high byte of the original PADStatus buttons.
      if (pressed && port == 0 && byte == 2)
        expected = 35;  // Distinct P1 MAIN X.
      if (pressed && port == 2 && byte == 0)
        expected = 0x02;  // B button on original source port 2.
      if (pressed && port == 2 && byte == 3)
        expected = static_cast<u8>(-35);  // Distinct P3 MAIN Y.
      if (pad[port * 12 + byte] != expected)
        return false;
    }
  }
  return true;
}

bool ActivationRequested()
{
  return Env("MWRC_ENABLE") == "1" && !Env("MWRC_OUTPUT").empty() &&
         Env("MWRC_DOL_SHA256") == EXPECTED_DOL_SHA256 && Env("MWRC_CPU") == "JITARM64" &&
         Env("MWRC_SOURCE_REV") == "GALE01r2";
}

bool TransformPrefixPadStatusMatches(const u8* pad, bool down_b)
{
  if (!pad)
    return false;
  // Each PADStatus occupies 12 ABI bytes, but only bytes 0..10 are fields.
  // Validate all four source ports and consistently ignore each trailing pad byte.
  if (down_b)
  {
    if (ReadBE16(pad) != 0x0200 || pad[2] != 0 || static_cast<s8>(pad[3]) >= 0 ||
        !std::all_of(pad + 4, pad + 11, [](u8 value) { return value == 0; }))
      return false;
  }
  else if (!std::all_of(pad, pad + 11, [](u8 value) { return value == 0; }))
  {
    return false;
  }
  if (!std::all_of(pad + 12, pad + 23, [](u8 value) { return value == 0; }))
    return false;
  for (u32 port = 2; port < 4; ++port)
  {
    const u8* status = pad + port * 12;
    if (!std::all_of(status, status + 10, [](u8 value) { return value == 0; }) ||
        status[10] != 0xff)
      return false;
  }
  return true;
}

enum class TransformPrefixEntryDisposition
{
  IgnoreOpeningAttract,
  AcceptVsEntry,
  Reject,
};

TransformPrefixEntryDisposition ClassifyTransformPrefixEntry(u8 mode, bool vs_entry_seen)
{
  if (mode == 0x18 && !vs_entry_seen)
    return TransformPrefixEntryDisposition::IgnoreOpeningAttract;
  if (mode == 0x02 && !vs_entry_seen)
    return TransformPrefixEntryDisposition::AcceptVsEntry;
  return TransformPrefixEntryDisposition::Reject;
}

bool TransformPrefixSourceTickIsNext(bool first_seen, u32 previous, u32 current)
{
  if (!first_seen)
    return current == 0;
  return previous != std::numeric_limits<u32>::max() && current == previous + 1;
}

bool TransformPrefixReadinessSequencesValid(uint64_t neutral_pad, uint64_t grounded_tick,
                                            uint64_t down_b_consume)
{
  return neutral_pad < grounded_tick && grounded_tick < down_b_consume;
}

bool TransformPrefixCssOwnerReady(bool source_return_seen, bool mode_two,
                                  bool live_state_seen, bool doors_seen)
{
  return source_return_seen && mode_two && live_state_seen && doors_seen;
}

bool TransformPrefixSssOwnerReady(bool source_return_seen, bool css_owner_seen,
                                  bool mode_two, bool stage_index_seen,
                                  bool stage_kind_seen)
{
  return source_return_seen && css_owner_seen && mode_two && stage_index_seen &&
         stage_kind_seen;
}

bool TransformPrefixMenuOwnersReady(bool css_owner_seen, bool sss_owner_seen)
{
  return css_owner_seen && sss_owner_seen;
}

bool TransformPrefixTeardownArmed(bool transform_prefix_enabled, bool match_active)
{
  return transform_prefix_enabled && match_active;
}

u32 WholeSessionMatchCount()
{
  const std::string value = Env("MWRC_WHOLE_SESSION_MATCHES");
  if (value.empty())
    return 0;
  u32 result = 0;
  for (const char digit : value)
  {
    if (digit < '0' || digit > '9' || result > WHOLE_SESSION_MAX_MATCHES / 10)
      return 0;
    result = result * 10 + static_cast<u32>(digit - '0');
    if (result > WHOLE_SESSION_MAX_MATCHES)
      return 0;
  }
  return result >= WHOLE_SESSION_MIN_MATCHES ||
         (result == 1 && Env("MWRC_ENTITY_PROFILE") == "jiggly-ice-mario-fox-v1") ? result : 0;
}

bool ValidIdentity(std::string_view value)
{
  if (value.empty() || value.size() > 128)
    return false;
  for (const unsigned char character : value)
  {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '-' || character == '_' ||
          character == '.'))
      return false;
  }
  return true;
}

bool AllocationConfigured()
{
  static const bool configured = !Env("MWRC_ALLOCATION_OUTPUT").empty();
  return configured;
}

struct CpuProbePoint
{
  const char* label;
  u32 address;
  u32 expected_word;
};

// These are the fixed addresses and instruction words from
// tools/cpu-register-gale01r2.json.  They intentionally remain compiled
// constants: a probe cannot turn an unverified guest PC into an observer
// boundary by supplying a different file at runtime.
constexpr std::array<CpuProbePoint, 37> CPU_PROBE_POINTS = {{
    {"after_kind_dispatch", 0x800b3924, 0x7fe3fb78},
    {"state_dispatch_entry", 0x800b2790, 0x7c0802a6},
    {"before_hitlag_sticks", 0x800b2aa8, 0x4bff9af9},
    {"after_hitlag_sticks", 0x800b2aac, 0x48000028},
    {"hitlag_sticks_entry", 0x800ac5a0, 0x7c0802a6},
    {"near_zero_predicate", 0x800ac6cc, 0x2c000000},
    {"near_zero_branch", 0x800ac6d0, 0x40820074},
    {"stick_setup", 0x800ac744, 0x387d0000},
    {"write_stick_x", 0x800ac74c, 0x48007f6d},
    {"after_stick_x", 0x800ac750, 0x387d0000},
    {"write_stick_y", 0x800ac75c, 0x48007f5d},
    {"after_stick_y", 0x800ac760, 0x48000024},
    {"hitlag_sticks_exit", 0x800ac7b8, 0x80010034},
    {"kind4_command_carry", 0x800b24e4, 0x7c651b79},
    {"target_common_call", 0x800b25dc, 0x4bffb86d},
    {"target_common_return", 0x800b25e0, 0x48000190},
    {"no_target_common_call", 0x800b276c, 0x4bffb6dd},
    {"no_target_common_return", 0x800b2770, 0x8001004c},
    {"common_entry", 0x800ade48, 0x7c0802a6},
    {"after_floor_output_setup", 0x800ade74, 0xdbc10080},
    {"floor_call", 0x800adebc, 0x4bfa114d},
    {"floor_return", 0x800adec0, 0x7c7c1b79},
    {"floor_exit", 0x8004f3c4, 0x7f63db78},
    {"state18_test", 0x800ae1b8, 0x801e0018},
    {"state18_return", 0x800ae290, 0x38600001},
    {"command_writer", 0x800b46b8, 0x7c0802a6},
    {"reset_cpu_commands_entry", 0x800b4a78, 0x38831a88},
    {"after_behavior_change", 0x800ae280, 0x38000012},
    {"reset_cpu_commands_return", 0x800b4aac, 0x4e800020},
    {"hitlag_random_call", 0x800ae21c, 0x482d230d},
    {"hitlag_random_return", 0x800ae220, 0x807b0010},
    {"randf_return", 0x8038057c, 0x4e800020},
    // Exact GALE01r2 entry instruction. This bounded diagnostic snapshots
    // only the unusual C8 group-0 palette referenced by Kirby's Samus bank.
    {"samus_effect_bank_locate", 0x80398614, 0xa0c30000},
    // Exact GALE01r2 entries from the DOL. These observe the already-located
    // group at registration and the renderer's consumed TLUT argument.
    {"samus_effect_bank_load", 0x803984f4, 0x7c0802a6},
    {"gx_init_tlut", 0x8033f024, 0x38000000},
    {"samus_effect_particle_spawn", 0x80398c04, 0x7c0802a6},
    {"rand_return", 0x80380524, 0x4e800020},
}};

const CpuProbePoint* FindCpuProbePoint(u32 address)
{
  for (const CpuProbePoint& point : CPU_PROBE_POINTS)
  {
    if (point.address == address)
      return &point;
  }
  return nullptr;
}

bool ParseBoundedDecimal(std::string_view value, u32 maximum, u32* result)
{
  if (value.empty())
    return false;
  u32 parsed = 0;
  for (const char digit : value)
  {
    if (digit < '0' || digit > '9')
      return false;
    const u32 number = static_cast<u32>(digit - '0');
    if (number > maximum || parsed > (maximum - number) / 10)
      return false;
    parsed = parsed * 10 + number;
  }
  *result = parsed;
  return true;
}

struct CpuProbeSettings
{
  bool present = false;
  bool valid = false;
  std::string output_path;
  std::string error;
  u32 match = 0;
  u32 first_tick = 0;
  u32 last_tick = 0;
  u32 rng_return_pc = 0;
  std::string rng_return_site;
};

const CpuProbeSettings& CpuProbeEnvironment()
{
  static const CpuProbeSettings settings = [] {
    CpuProbeSettings result;
    const std::string output = Env("MWRC_CPU_PROBE_OUTPUT");
    const std::string match = Env("MWRC_CPU_PROBE_MATCH");
    const std::string first_tick = Env("MWRC_CPU_PROBE_FIRST_TICK");
    const std::string last_tick = Env("MWRC_CPU_PROBE_LAST_TICK");
    const std::string rng_return_site = Env("MWRC_CPU_PROBE_RNG_RETURN_SITE");
    result.present = !output.empty() || !match.empty() || !first_tick.empty() ||
                     !last_tick.empty() || !rng_return_site.empty();
    if (!result.present)
      return result;
    if (output.empty() || output.size() > 4096)
    {
      result.error = "MWRC_CPU_PROBE_OUTPUT must be a non-empty path of at most 4096 bytes";
      return result;
    }
    result.output_path = output;
    if (!rng_return_site.empty())
    {
      if (rng_return_site != "rand_return" && rng_return_site != "randf_return")
      {
        result.error = "MWRC_CPU_PROBE_RNG_RETURN_SITE must be rand_return or randf_return";
        return result;
      }
      result.rng_return_site = rng_return_site;
      result.rng_return_pc = rng_return_site == "rand_return" ? 0x80380524 : 0x8038057c;
    }
    if (!ParseBoundedDecimal(match, WHOLE_SESSION_MAX_MATCHES - 1, &result.match) ||
        !ParseBoundedDecimal(first_tick, std::numeric_limits<u32>::max(),
                             &result.first_tick) ||
        !ParseBoundedDecimal(last_tick, std::numeric_limits<u32>::max(), &result.last_tick))
    {
      result.error =
          "MWRC_CPU_PROBE_MATCH, FIRST_TICK, and LAST_TICK must be unsigned decimal values";
      return result;
    }
    if (result.first_tick > result.last_tick ||
        static_cast<u64>(result.last_tick) - result.first_tick >= CPU_PROBE_TICK_WINDOW_MAX)
    {
      result.error = "CPU probe tick window must contain at most 64 source ticks";
      return result;
    }
    result.valid = true;
    return result;
  }();
  return settings;
}

bool CpuProbeEnabled()
{
  const CpuProbeSettings& settings = CpuProbeEnvironment();
  return ActivationRequested() && settings.present && settings.valid;
}

struct ItemProbeSettings
{
  bool present = false;
  bool valid = false;
  std::string output_path;
  std::string error;
  u32 match = 0;
  u32 first_tick = 0;
  u32 last_tick = 0;
  bool trigger_on_arrow_creation = false;
  bool trigger_on_arrow_launch = false;
  u32 capture_ticks = 0;
};

const ItemProbeSettings& ItemProbeEnvironment()
{
  static const ItemProbeSettings settings = [] {
    ItemProbeSettings result;
    const std::string output = Env("MWRC_ITEM_PROBE_OUTPUT");
    const std::string match = Env("MWRC_ITEM_PROBE_MATCH");
    const std::string first_tick = Env("MWRC_ITEM_PROBE_FIRST_TICK");
    const std::string last_tick = Env("MWRC_ITEM_PROBE_LAST_TICK");
    const std::string trigger = Env("MWRC_ITEM_PROBE_TRIGGER");
    const std::string capture_ticks = Env("MWRC_ITEM_PROBE_CAPTURE_TICKS");
    result.present = !output.empty() || !match.empty() || !first_tick.empty() ||
                     !last_tick.empty() || !trigger.empty() || !capture_ticks.empty();
    if (!result.present)
      return result;
    if (output.empty() || output.size() > 4096)
    {
      result.error = "MWRC_ITEM_PROBE_OUTPUT must be a non-empty path of at most 4096 bytes";
      return result;
    }
    result.output_path = output;
    if (!ParseBoundedDecimal(match, WHOLE_SESSION_MAX_MATCHES - 1, &result.match))
    {
      result.error = "MWRC_ITEM_PROBE_MATCH must be an unsigned decimal match index";
      return result;
    }
    if (!trigger.empty())
    {
      if ((trigger != "young_link_arrow_creation" &&
           trigger != "young_link_arrow_launch") ||
          !first_tick.empty() || !last_tick.empty() ||
          !ParseBoundedDecimal(capture_ticks, 16, &result.capture_ticks) ||
          result.capture_ticks == 0)
      {
        result.error =
            "triggered item probe requires an Arrow creation/launch trigger and "
            "CAPTURE_TICKS from 1 through 16, with no fixed tick bounds";
        return result;
      }
      result.trigger_on_arrow_creation = trigger == "young_link_arrow_creation";
      result.trigger_on_arrow_launch = trigger == "young_link_arrow_launch";
    }
    else
    {
      if (!capture_ticks.empty() ||
          !ParseBoundedDecimal(first_tick, std::numeric_limits<u32>::max(),
                               &result.first_tick) ||
          !ParseBoundedDecimal(last_tick, std::numeric_limits<u32>::max(), &result.last_tick))
      {
        result.error =
            "MWRC_ITEM_PROBE_FIRST_TICK and LAST_TICK must be unsigned decimal values";
        return result;
      }
      if (result.first_tick > result.last_tick ||
          static_cast<u64>(result.last_tick) - result.first_tick >= CPU_PROBE_TICK_WINDOW_MAX)
      {
        result.error = "item probe tick window must contain at most 64 source ticks";
        return result;
      }
    }
    result.valid = true;
    return result;
  }();
  return settings;
}

bool ItemProbeEnabled()
{
  const ItemProbeSettings& settings = ItemProbeEnvironment();
  return ActivationRequested() && settings.present && settings.valid;
}

bool AppendBounded(std::string* output, std::string_view value)
{
  if (output->size() > CPU_PROBE_MAX_JSON_BYTES ||
      value.size() > CPU_PROBE_MAX_JSON_BYTES - output->size())
    return false;
  output->append(value);
  return true;
}

bool AppendHex(std::string* output, u64 value, size_t digits)
{
  constexpr char hex[] = "0123456789abcdef";
  std::array<char, 16> buffer{};
  if (digits > buffer.size())
    return false;
  for (size_t index = 0; index < digits; ++index)
    buffer[digits - index - 1] = hex[(value >> (index * 4)) & 0xf];
  return AppendBounded(output, std::string_view(buffer.data(), digits));
}

bool AppendHexBytes(std::string* output, const u8* bytes, size_t size)
{
  constexpr char hex[] = "0123456789abcdef";
  if (output->size() > CPU_PROBE_MAX_JSON_BYTES ||
      size > (CPU_PROBE_MAX_JSON_BYTES - output->size()) / 2)
    return false;
  for (size_t index = 0; index < size; ++index)
  {
    output->push_back(hex[bytes[index] >> 4]);
    output->push_back(hex[bytes[index] & 0xf]);
  }
  return true;
}

}  // namespace

struct Observer::Impl
{
  Impl() = default;

  ~Impl()
  {
    Stop();
    if (AllocationConfigured() && ReferenceAllocation::Observer::IsInitialized())
      ReferenceAllocation::Observer::Finish(false);
  }

  bool Start()
  {
    if (started.exchange(true))
      return !invalid.load();
    output_path = Env("MWRC_OUTPUT");
    status_path = Env("MWRC_STATUS");
    if (status_path.empty())
      status_path = output_path + ".status.json";
    if (output_path.empty())
    {
      SetInvalid("MWRC_OUTPUT must name a fresh stream");
      return false;
    }
    whole_session_matches = WholeSessionMatchCount();
    const std::string entity_profile = Env("MWRC_ENTITY_PROFILE");
    checked_entity_profile = entity_profile == "jiggly-ice-mario-fox-v1";
    if ((!entity_profile.empty() && !checked_entity_profile) ||
        (checked_entity_profile && (whole_session_matches != 1 || SdInitRequested() ||
          !Env("MWRC_CPU_PROBE_OUTPUT").empty() || !Env("MWRC_ITEM_PROBE_OUTPUT").empty() ||
          !Env("MWRC_ALLOCATION_OUTPUT").empty())))
      return SetInvalid("Entity profile requires its exact exclusive diagnostic scope"), false;
    capture_id = Env("MWRC_CAPTURE_ID");
    sequence_id = Env("MWRC_SEQUENCE_ID");
    const std::string transform_prefix_setting = Env("MWRC_TRANSFORM_PREFIX");
    if (!transform_prefix_setting.empty() && transform_prefix_setting != "1")
      SetInvalid("MWRC_TRANSFORM_PREFIX must be 1 when set");
    transform_prefix_enabled = transform_prefix_setting == "1";
    const CpuProbeSettings& cpu_probe = CpuProbeEnvironment();
    cpu_probe_configured = cpu_probe.present;
    cpu_probe_valid = cpu_probe.valid;
    if (cpu_probe.valid)
    {
      cpu_probe_output_path = cpu_probe.output_path;
      cpu_probe_match = cpu_probe.match;
      cpu_probe_first_tick = cpu_probe.first_tick;
      cpu_probe_last_tick = cpu_probe.last_tick;
      cpu_probe_rng_return_pc = cpu_probe.rng_return_pc;
      cpu_probe_rng_return_site = cpu_probe.rng_return_site;
      cpu_probe_records.reset(new (std::nothrow) CpuProbeRecord[CPU_PROBE_MAX_RECORDS]);
      if (!cpu_probe_records)
      {
        cpu_probe_valid = false;
        cpu_probe_error = "CPU probe record buffer allocation failed";
      }
    }
    const ItemProbeSettings& item_probe = ItemProbeEnvironment();
    item_probe_configured = item_probe.present;
    item_probe_valid = item_probe.valid;
    if (item_probe.valid)
    {
      item_probe_output_path = item_probe.output_path;
      item_probe_match = item_probe.match;
      item_probe_first_tick = item_probe.first_tick;
      item_probe_last_tick = item_probe.last_tick;
      item_probe_trigger_on_arrow_creation = item_probe.trigger_on_arrow_creation;
      item_probe_trigger_on_arrow_launch = item_probe.trigger_on_arrow_launch;
      item_probe_capture_ticks = item_probe.capture_ticks;
      item_probe_records.reset(new (std::nothrow) ItemProbeRecord[ITEM_PROBE_MAX_RECORDS]);
      if (!item_probe_records)
      {
        item_probe_valid = false;
        item_probe_error = "item probe record buffer allocation failed";
      }
    }
    const std::string item_probe_summary_stream_setting =
        Env("MWRC_ITEM_PROBE_SUMMARY_STREAM");
    if (!item_probe_summary_stream_setting.empty() &&
        item_probe_summary_stream_setting != "1")
      SetInvalid("MWRC_ITEM_PROBE_SUMMARY_STREAM must be 1 when set");
    item_probe_summary_stream = item_probe_summary_stream_setting == "1";
    if (item_probe_summary_stream && !item_probe_configured)
      SetInvalid("MWRC_ITEM_PROBE_SUMMARY_STREAM requires a configured item probe");
    // Dolphin builds with exceptions disabled.  std::thread reports an
    // unavailable worker by terminating; there is no catchable error path.
    writer = std::thread([this] { WriterMain(); });
    if (!Env("MWRC_SD_MENU_PROBE").empty() && !SdInitRequested())
    {
      SetInvalid("SD menu probe requires the opt-in SD diagnostic owner");
      return false;
    }
    if (!Env("MWRC_ORDINARY_POLICY_SHA256").empty() && !SdInitRequested())
      return SetInvalid("Ordinary policy requires its diagnostic owner"), false;
    if (!Env("MWRC_SD_PROFILE_GCI_SHA256").empty() &&
        (!SdInitRequested() || (Env("MWRC_SD_MENU_PROBE") != "rules_ready" &&
                              Env("MWRC_SD_MENU_PROBE") != "sd_prefix" &&
                              Env("MWRC_SD_MENU_PROBE") != "competitive_entry" &&
                              Env("MWRC_SD_MENU_PROBE") != "ordinary_timeout" &&
                              Env("MWRC_SD_MENU_PROBE") != "items_row" &&
                              !SparsePairRequested()) ||
         Env("MWRC_SD_PROFILE_GCI_SHA256") !=
             "5184f7f9bfcbd35ea7cc07904cbed557b8a7fc9e624a05aa02c8d1d308d4d729"))
    {
      SetInvalid("SD loaded-profile diagnostic identity/scope differs");
      return false;
    }
    if (SdInitRequested())
    {
      const std::string hash = Env("MWRC_SD_RECIPE_SHA256");
      if (Env("MWRC_SD_INIT") != "1" || hash.size() != 64 ||
          hash.find_first_not_of("0123456789abcdef") != std::string::npos ||
          hash == std::string(64, '0') || !InputStream::IsRecording() ||
          !Env("MWRC_WHOLE_SESSION_MATCHES").empty() ||
          !Env("MWRC_CPU_PROBE_OUTPUT").empty() || !Env("MWRC_ITEM_PROBE_OUTPUT").empty() ||
          !Env("MWRC_ALLOCATION_OUTPUT").empty() ||
          (!Env("MWRC_SD_MENU_PROBE").empty() && Env("MWRC_SD_MENU_PROBE") != "rules_ready" &&
           Env("MWRC_SD_MENU_PROBE") != "sd_prefix" && Env("MWRC_SD_MENU_PROBE") != "items_row" &&
           Env("MWRC_SD_MENU_PROBE") != "competitive_entry" && !OrdinaryTimeoutRequested() &&
           !SparsePairRequested()) ||
          ((Env("MWRC_SD_MENU_PROBE") == "sd_prefix" || Env("MWRC_SD_MENU_PROBE") == "items_row" ||
            Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() ||
            SparsePairRequested()) && Env("MWRC_SD_PROFILE_GCI_SHA256").empty()))
      {
        SetInvalid("SD prefix requires a recipe hash, native input recording and exclusive scope");
        return false;
      }
      if ((OrdinaryTimeoutRequested() && (Env("MWRC_ORDINARY_POLICY_SHA256") != OrdinaryTimeoutState::policy_hash ||
           hash != "6cfb538e58ef9692a3ac6c8dac0dcff10129af7e6e14d851dec95583255d5326")) ||
          (!OrdinaryTimeoutRequested() && !Env("MWRC_ORDINARY_POLICY_SHA256").empty()))
        return SetInvalid("Ordinary timeout policy requires its exclusive exact identity"), false;
    }
    if (!Env("MWRC_WHOLE_SESSION_MATCHES").empty() && whole_session_matches == 0)
    {
      SetInvalid("MWRC_WHOLE_SESSION_MATCHES must be a decimal count from 3 through 64");
      return false;
    }
    if (transform_prefix_enabled &&
        (whole_session_enabled() || !Env("MWRC_WHOLE_SESSION_MATCHES").empty() ||
         SdInitRequested() || !Env("MWRC_SD_MENU_PROBE").empty() ||
         !Env("MWRC_ORDINARY_POLICY_SHA256").empty() || cpu_probe_configured ||
         item_probe_configured || AllocationConfigured() || !InputStream::IsRecording()))
    {
      SetInvalid("Sheik transform prefix requires exclusive original input recording scope");
      return false;
    }
    if (whole_session_enabled() && (!ValidIdentity(capture_id) || !ValidIdentity(sequence_id)))
    {
      SetInvalid("whole-session capture and sequence IDs must be safe non-empty strings");
      return false;
    }
    if (cpu_probe_configured)
    {
      if (!cpu_probe_valid)
      {
        SetInvalid("invalid CPU probe configuration: " +
                   (cpu_probe_error.empty() ? cpu_probe.error : cpu_probe_error));
        return false;
      }
      if (cpu_probe_output_path == output_path)
      {
        SetInvalid("CPU probe output must be separate from MWRC_OUTPUT");
        return false;
      }
      if ((whole_session_enabled() && cpu_probe_match >= whole_session_matches) ||
          (!whole_session_enabled() && cpu_probe_match != 0))
      {
        SetInvalid("CPU probe match is outside the configured whole-session matches");
        return false;
      }
    }
    if (item_probe_configured)
    {
      if (!item_probe_valid)
      {
        SetInvalid("invalid item probe configuration: " +
                   (item_probe_error.empty() ? item_probe.error : item_probe_error));
        return false;
      }
      if (item_probe_output_path == output_path ||
          (cpu_probe_configured && item_probe_output_path == cpu_probe_output_path))
      {
        SetInvalid("item probe output must be separate from other capture outputs");
        return false;
      }
      if ((whole_session_enabled() && item_probe_match >= whole_session_matches) ||
          (!whole_session_enabled() && item_probe_match != 0))
      {
        SetInvalid("item probe match is outside the configured whole-session matches");
        return false;
      }
    }
    std::string handshake =
        std::string("{\"schema\":\"melee-web-passive-dolphin-observer\",\"version\":1,") +
        "\"dolphin_commit\":\"" + EXPECTED_COMMIT + "\",\"dol_sha1\":\"" + EXPECTED_DOL +
        "\",\"dol_sha256\":\"" + EXPECTED_DOL_SHA256 +
        "\",\"cpu\":\"JITARM64\",\"writes_guest_memory\":false,\"ring_capacity\":" +
        std::to_string(RING_SIZE);
    if (whole_session_enabled())
      handshake += ",\"whole_session\":true,\"match_count\":" +
                   std::to_string(whole_session_matches) + ",\"capture_id\":\"" +
                   JsonEscape(capture_id) + "\",\"sequence_id\":\"" +
                   JsonEscape(sequence_id) + "\"";
    if (transform_prefix_enabled)
      handshake += ",\"diagnostic\":\"sheik_transform_prefix\",\"max_active_source_ticks\":600"
                   ",\"completion_boundary\":\"active_sheik_grounded_neutral_source_tick_after_owner_change\"";
    if (SdInitRequested())
      handshake += ",\"diagnostic\":\"sd_initialization_prefix\",\"recipe_sha256\":\"" +
                   Env("MWRC_SD_RECIPE_SHA256") + "\",\"menu_probe\":\"" +
                   Env("MWRC_SD_MENU_PROBE") + "\"";
    if (!Env("MWRC_SD_PROFILE_GCI_SHA256").empty())
      handshake += ",\"profile_gci_sha256\":\"" + Env("MWRC_SD_PROFILE_GCI_SHA256") + "\"";
    if (OrdinaryTimeoutRequested())
      handshake += ",\"ordinary_policy_sha256\":\"" + Env("MWRC_ORDINARY_POLICY_SHA256") + "\"";
    if (checked_entity_profile)
      handshake += ",\"entity_profile\":\"jiggly-ice-mario-fox-v1\"";
    handshake += "}";
    PushJson(Event::Handshake, handshake);
    std::string start =
        "{\"status\":\"recording\",\"source_revision\":\"GALE01r2\",\"boundaries\":\""
        "typed-existing-retail-harness";
    if (whole_session_enabled())
      start += "\",\"whole_session\":true,\"match_count\":" +
               std::to_string(whole_session_matches) + ",\"capture_id\":\"" +
               JsonEscape(capture_id) + "\",\"sequence_id\":\"" +
               JsonEscape(sequence_id) + "\"";
    else
      start += "\"";
    if (checked_entity_profile)
      start += ",\"entity_profile\":\"jiggly-ice-mario-fox-v1\"";
    if (transform_prefix_enabled)
      start += ",\"diagnostic\":\"sheik_transform_prefix\",\"max_active_source_ticks\":600"
               ",\"completion_boundary\":\"active_sheik_grounded_neutral_source_tick_after_owner_change\"";
    start += "}";
    PushJson(Event::Start, start);
    return !invalid.load();
  }

  void Stop()
  {
    if (!started.load())
      return;
    if (!finish_requested.exchange(true))
      natural_completion.store(false);
    if (writer.joinable())
      writer.join();
  }

  void RequestComplete()
  {
    InputStream::RequestFinish(true);
    natural_completion.store(true);
    finish_requested.store(true);
  }

  bool ReadBytes(Core::System* system, u32 address, size_t size, u8* destination) const
  {
    if (!IsGuestRange(address, size))
      return false;
    const auto* pointer = system->GetMemory().GetPointerForRange(address, size);
    if (!pointer)
      return false;
    std::memcpy(destination, pointer, size);
    return true;
  }

  bool ReadFighterSourceSlot(Core::System* system, u32 fighter, u8* slot) const
  {
    // This boundary is Fighter_Create's return: r3 is its GObj, and the
    // GObj's +0x2c user_data is a Fighter whose source player_id is at +0xc.
    return IsMem1Range(fighter, 0xD) && ReadBytes(system, fighter + 0xC, 1, slot);
  }

  static u16 FighterEntitySliceFlags(u32 slot, u32 entity_index)
  {
    // Keep the source slot in the low byte and distinguish the paired fighter
    // in the high byte. Entity zero retains the legacy flags.
    return static_cast<u16>(slot | (entity_index << 8));
  }

  bool RegisterFighterEntity(u32 slot, u32 fighter, u32 kind, u32* entity_index)
  {
    if (slot >= fighter_entity_pointers.size() || !fighter || !entity_index)
      return false;
    const u32 count = fighter_entity_count[slot];
    if (count >= fighter_entity_pointers[slot].size())
      return false;
    for (u32 index = 0; index < count; ++index)
    {
      if (fighter_entity_pointers[slot][index] == fighter ||
          fighter_entity_kinds[slot][index] == kind)
        return false;
    }
    fighter_entity_pointers[slot][count] = fighter;
    fighter_entity_kinds[slot][count] = kind;
    fighter_entity_count[slot] = static_cast<u8>(count + 1);
    if (count == 0)
    {
      fighter_pointers[slot] = fighter;
      fighter_present[slot] = true;
    }
    *entity_index = count;
    return true;
  }

  bool ReadU32(Core::System* system, u32 address, u32* value) const
  {
    std::array<u8, 4> bytes{};
    if (!ReadBytes(system, address, bytes.size(), bytes.data()))
      return false;
    *value = ReadBE32(bytes.data());
    return true;
  }

  bool TransformPrefixRosterValid(Core::System* system) const
  {
    std::array<u8, 0x138> setup{};
    if (!setup_pointer || active_slot_count != 2 || cpu_slots[0] || cpu_slots[1] ||
        fighter_entity_count[0] != 2 || fighter_entity_count[1] != 1 ||
        fighter_entity_count[2] != 0 || fighter_entity_count[3] != 0 ||
        fighter_entity_kinds[0][0] != 19 || fighter_entity_kinds[0][1] != 7 ||
        fighter_entity_kinds[1][0] != 0 ||
        !ReadBytes(system, setup_pointer, setup.size(), setup.data()))
      return false;
    // StartMeleeData stores character kind/player type at +0x60/+0x61 and
    // +0x84/+0x85. This opt-in probe is only the original human Zelda-versus-
    // Mario setup; a CSS Sheik or an added port is a different experiment.
    return (setup[4] & 0x40) != 0 && setup[0x60] == 18 && setup[0x61] == 0 &&
           setup[0x84] == 8 && setup[0x85] == 0 && ReadBE16(setup.data() + 0x0e) == 32 &&
           (setup[2] & 0x80) != 0 && setup[0x62] == 4 && setup[0x86] == 4;
  }

  bool AddTransformPrefixOwnerSlices(Core::System* system, u32* active_entity_index,
                                     u32* active_kind, u32* active_fighter)
  {
    if (!active_entity_index || !active_kind || !active_fighter)
      return false;
    u8 resolved_kind = 0;
    u32 resolved_fighter = 0;
    for (u32 slot = 0; slot < 2; ++slot)
    {
      const u32 player = 0x80453080 + slot * 0xe90;
      const u32 entities = player + 0xb0;
      std::array<u8, 8> entity_bytes{};
      const u32 expected_count = slot == 0 ? 2 : 1;
      if (fighter_entity_count[slot] != expected_count ||
          !ReadBytes(system, entities, entity_bytes.size(), entity_bytes.data()))
        return false;
      const u32 first_gobj = ReadBE32(entity_bytes.data());
      const u32 second_gobj = ReadBE32(entity_bytes.data() + 4);
      if (!first_gobj || (slot == 0 ? (!second_gobj || second_gobj == first_gobj) :
                                      second_gobj != 0))
        return false;
      if (slot == 0)
      {
        std::array<u8, 2> transformed{};
        if (!ReadBytes(system, player + 0x0c, transformed.size(), transformed.data()) ||
            transformed[0] > 1 || transformed[1] > 1 ||
            transformed[0] == transformed[1] ||
            !AddSlice(system, SliceTag::PlayerEntities, entities, entity_bytes.size(), 0) ||
            !AddSlice(system, SliceTag::PlayerTransformed, player + 0x0c,
                      transformed.size(), 0))
          return false;
        *active_entity_index = transformed[0];
      }
      else if (!AddSlice(system, SliceTag::PlayerEntities, entities,
                         entity_bytes.size(), static_cast<u16>(slot)))
      {
        return false;
      }

      for (u32 entity_index = 0; entity_index < expected_count; ++entity_index)
      {
        const u32 gobj = entity_index == 0 ? first_gobj : second_gobj;
        const u32 fighter = fighter_entity_pointers[slot][entity_index];
        const u32 kind_expected = fighter_entity_kinds[slot][entity_index];
        const u16 flags = FighterEntitySliceFlags(slot, entity_index);
        u32 user_data = 0, backlink = 0, kind = 0;
        u8 source_slot = 0xff;
        if (!IsMem1Range(gobj, 0x30) || !IsMem1Range(fighter, 0x100) ||
            !ReadU32(system, gobj + 0x2c, &user_data) || user_data != fighter ||
            !ReadU32(system, fighter, &backlink) || backlink != gobj ||
            !ReadFighterSourceSlot(system, fighter, &source_slot) || source_slot != slot ||
            !ReadU32(system, fighter + 4, &kind) || kind != kind_expected ||
            !AddSlice(system, SliceTag::PlayerEntityUserData, gobj + 0x2c, 4, flags) ||
            !AddSlice(system, SliceTag::FighterHead, fighter, 0x100, flags))
          return false;
        if (slot == 0 && entity_index == *active_entity_index)
        {
          resolved_kind = static_cast<u8>(kind);
          resolved_fighter = fighter;
        }
      }
    }
    if (!resolved_fighter)
      return false;
    *active_kind = resolved_kind;
    *active_fighter = resolved_fighter;
    return true;
  }

  bool ReadMem1(Core::System* system, u32 address, size_t size, u8* destination) const
  {
    if (!IsMem1Range(address, size))
      return false;
    const auto* pointer = system->GetMemory().GetPointerForRange(address, size);
    if (!pointer)
      return false;
    std::memcpy(destination, pointer, size);
    return true;
  }

  void RecordSelectedRngCallback(Core::System* system, u32 pc)
  {
    if (!cpu_probe_configured || !cpu_probe_valid || cpu_probe_closed ||
        cpu_probe_rng_return_pc == 0 || pc != cpu_probe_rng_return_pc)
      return;
    if (cpu_probe_rng_return_callback_count != std::numeric_limits<u64>::max())
      ++cpu_probe_rng_return_callback_count;
    cpu_probe_rng_return_callback_pc = pc;
    cpu_probe_rng_return_callback_match = match_index;
    cpu_probe_rng_return_callback_tick_valid =
        ReadU32(system, 0x80479d58, &cpu_probe_rng_return_callback_tick);
  }

  void CloseCpuProbe(u32 pc, u32 source_tick)
  {
    if (!cpu_probe_configured || cpu_probe_closed)
      return;
    cpu_probe_close_pc = pc;
    cpu_probe_close_tick = source_tick;
    cpu_probe_closed = true;
    // The callback is the sole writer.  Once this release is visible, the
    // writer owns an immutable snapshot and can serialize it without taking a
    // lock or touching guest memory from its thread.
    cpu_probe_published.store(true, std::memory_order_release);
  }

  void CloseCpuProbeAtSourceTick(u32 pc, u32 source_tick)
  {
    if (!cpu_probe_configured || !cpu_probe_valid || cpu_probe_closed ||
        source_tick <= cpu_probe_last_tick || match_index != cpu_probe_match)
      return;
    const bool rng_match_scope = cpu_probe_rng_return_pc != 0 && whole_session_enabled() &&
        (whole_phase == 4 || (whole_phase == 5 && match_active));
    const bool active_match_scope = match_active && setup_ready;
    if (rng_match_scope || active_match_scope)
      CloseCpuProbe(pc, source_tick);
  }

  std::string CpuProbeCloseStatusJson() const
  {
    if (!cpu_probe_published.load(std::memory_order_acquire) ||
        cpu_probe_rng_return_pc == 0)
      return {};
    std::string rng_return_pc;
    std::string close_pc;
    std::string callback_pc;
    if (!AppendHex(&rng_return_pc, cpu_probe_rng_return_pc, 8) ||
        !AppendHex(&close_pc, cpu_probe_close_pc, 8) ||
        (cpu_probe_rng_return_callback_count != 0 &&
         !AppendHex(&callback_pc, cpu_probe_rng_return_callback_pc, 8)))
      return {};
    const std::string last_callback =
        cpu_probe_rng_return_callback_count == 0 ? "null" :
        "{\"pc\":\"0x" + callback_pc + "\",\"source_tick\":" +
            (cpu_probe_rng_return_callback_tick_valid ?
                 std::to_string(cpu_probe_rng_return_callback_tick) : "null") +
            ",\"match\":" +
            std::to_string(cpu_probe_rng_return_callback_match) + "}";
    return ",\"cpu_probe_close\":{\"reason\":\"source_tick_after_window\",\"rng_return_pc\":\"0x" +
           rng_return_pc + "\",\"close_pc\":\"0x" + close_pc +
           "\",\"configured_match\":" + std::to_string(cpu_probe_match) +
           ",\"first_tick\":" +
           std::to_string(cpu_probe_first_tick) + ",\"last_tick\":" +
           std::to_string(cpu_probe_last_tick) + ",\"selected_return_callback_count\":" +
           std::to_string(cpu_probe_rng_return_callback_count) +
           ",\"selected_return_record_count\":" +
           std::to_string(cpu_probe_record_count) +
           ",\"selected_return_last_callback\":" + last_callback +
           ",\"source_tick\":" + std::to_string(cpu_probe_close_tick) + "}";
  }

  void RecordCpuProbe(Core::System* system, u32 pc, const CpuProbePoint& point,
                      PowerPC::PowerPCState* state, u32 source_tick)
  {
    if (cpu_probe_rng_return_pc != 0 && pc != cpu_probe_rng_return_pc)
      return;
    const bool rng_match_construction_scope = cpu_probe_rng_return_pc != 0 &&
        whole_session_enabled() && match_index == cpu_probe_match &&
        (whole_phase == 4 || (whole_phase == 5 && match_active && !setup_ready));
    const bool samus_effect_bank_probe = pc == 0x80398614;
    const bool samus_effect_bank_load_probe = pc == 0x803984f4;
    const bool samus_effect_gx_tlut_probe = pc == 0x8033f024;
    const bool samus_effect_particle_probe = pc == 0x80398c04;
    const bool samus_effect_load_scope =
        samus_effect_bank_probe || samus_effect_bank_load_probe;
    if (!cpu_probe_configured || !cpu_probe_valid || cpu_probe_closed ||
        (!samus_effect_load_scope &&
         ((!match_active || !setup_ready || match_index != cpu_probe_match) &&
          !rng_match_construction_scope)) ||
        (samus_effect_bank_probe && cpu_probe_effect_group_found))
      return;
    u32 instruction = 0;
    if (!ReadU32(system, pc, &instruction) || instruction != point.expected_word)
    {
      SetInvalid("CPU probe instruction differs from the verified GALE01r2 word at pc=" +
                 std::to_string(pc) + " expected=" +
                 std::to_string(point.expected_word) + " actual=" +
                 std::to_string(instruction));
      return;
    }
    if (!samus_effect_load_scope && source_tick > cpu_probe_last_tick)
    {
      CloseCpuProbe(pc, source_tick);
      return;
    }
    if (!samus_effect_load_scope &&
        (source_tick < cpu_probe_first_tick || match_index != cpu_probe_match))
      return;
    if (cpu_probe_record_count >= CPU_PROBE_MAX_RECORDS)
    {
      SetInvalid("CPU probe record bound exceeded");
      return;
    }

    CpuProbeRecord& record = cpu_probe_records[cpu_probe_record_count];
    record.pc = pc;
    record.expected_word = point.expected_word;
    record.source_tick = source_tick;
    record.match = match_index;
    record.lr = state->spr[8];
    for (size_t index = 0; index < record.gpr.size(); ++index)
      record.gpr[index] = state->gpr[index];
    for (size_t index = 0; index < record.fpr.size(); ++index)
      record.fpr[index] = state->ps[index].PS0AsU64();
    if (!ReadMem1(system, state->gpr[1], record.stack.size(), record.stack.data()) ||
        !ReadMem1(system, CPU_PROBE_RANDOM_ADDRESS, record.random_seed_and_pointer.size(),
                  record.random_seed_and_pointer.data()))
    {
      SetInvalid("CPU probe stack or random-seed range is outside MEM1");
      return;
    }
    if (samus_effect_bank_load_probe)
    {
      // psInitDataBankLoad receives the bank number in r3 and the already
      // relocated texture-group table in r5. Retain only the exact C8/64x64
      // group-zero signature from Kirby's Samus bank; unrelated registrations
      // remain outside this diagnostic.
      const u32 bank = state->gpr[3];
      const u32 texture_base = state->gpr[5];
      u32 group_count = 0;
      u32 group_address = 0;
      u32 image_count = 0;
      u32 format = 0;
      u32 width = 0;
      u32 height = 0;
      u32 palette_counts = 0;
      u32 image_address = 0;
      u32 palette_address = 0;
      if (bank == 34 && ReadU32(system, texture_base, &group_count) &&
          group_count >= 1 && group_count <= 256 &&
          ReadU32(system, texture_base + 4, &group_address) &&
          IsMem1Range(group_address, 0x20) &&
          ReadU32(system, group_address, &image_count) &&
          ReadU32(system, group_address + 4, &format) &&
          ReadU32(system, group_address + 12, &width) &&
          ReadU32(system, group_address + 16, &height) &&
          ReadU32(system, group_address + 20, &palette_counts) &&
          ReadU32(system, group_address + 24, &image_address) &&
          ReadU32(system, group_address + 28, &palette_address) &&
          image_count == 1 && format == 9 && width == 64 && height == 64 &&
          palette_counts == 0)
      {
        record.samus_effect_loaded_group_present = true;
        record.samus_effect_loaded_bank = bank;
        record.samus_effect_loaded_texture_base = texture_base;
        record.samus_effect_loaded_group_address = group_address;
        record.samus_effect_loaded_image_address = image_address;
        record.samus_effect_loaded_palette_address = palette_address;
        cpu_probe_samus_effect_palette_address = palette_address;
      }
    }
    if (samus_effect_gx_tlut_probe && cpu_probe_samus_effect_palette_address != 0 &&
        state->gpr[4] == cpu_probe_samus_effect_palette_address)
    {
      record.samus_effect_gx_tlut_call = true;
      record.samus_effect_gx_tlut_address = state->gpr[4];
    }
    if (samus_effect_particle_probe && state->gpr[5] == 34)
    {
      // psGenerateParticle0's source ABI carries bank, command kind and
      // texture-group identity in r5-r7. This observes simulation-side use
      // without depending on a video backend or sampling graphics output.
      record.samus_effect_particle_spawn = true;
      record.samus_effect_particle_bank = state->gpr[5];
      record.samus_effect_particle_kind = state->gpr[6];
      record.samus_effect_particle_group = state->gpr[7] & 0xffff;
    }
    if (pc == 0x80398614)
    {
      // psInitDataBankLocate receives the texture bank root in r4. Its source
      // group table is still relative at entry; compute the address the
      // original routine will publish for a C8 palette without writing guest
      // memory. The signature is diagnostic-only and does not affect the
      // capture or source execution.
      u32 group_count = 0;
      u32 group_offset = 0;
      u32 image_count = 0;
      u32 format = 0;
      u32 width = 0;
      u32 height = 0;
      u32 palette_counts = 0;
      u32 image_offset = 0;
      u32 palette_offset = 0;
      const u32 bank_base = state->gpr[4];
      if (ReadU32(system, bank_base, &group_count) && group_count >= 1 &&
          group_count <= 256 && ReadU32(system, bank_base + 4, &group_offset) &&
          group_offset != 0 && group_offset <= 0x100000 &&
          bank_base + group_offset >= bank_base &&
          ReadU32(system, bank_base + group_offset, &image_count) &&
          ReadU32(system, bank_base + group_offset + 4, &format) &&
          ReadU32(system, bank_base + group_offset + 12, &width) &&
          ReadU32(system, bank_base + group_offset + 16, &height) &&
          ReadU32(system, bank_base + group_offset + 20, &palette_counts) &&
          ReadU32(system, bank_base + group_offset + 24, &image_offset) &&
          ReadU32(system, bank_base + group_offset + 28, &palette_offset) &&
          image_count == 1 && format == 9 && width == 64 && height == 64 &&
          palette_counts == 0 && image_offset == 0x40 &&
          palette_offset == 0x80a8812a)
      {
        record.effect_group_present = true;
        record.effect_bank_base = bank_base;
        record.effect_group_address = bank_base + group_offset;
        record.effect_palette_offset = palette_offset;
        const u32 target = bank_base + palette_offset;
        record.effect_palette_address = target;
        record.effect_literal_palette_readable =
            IsMem1Range(palette_offset, record.effect_literal_palette.size()) &&
            ReadMem1(system, palette_offset, record.effect_literal_palette.size(),
                     record.effect_literal_palette.data());
        u32 readable_target = target;
        if (readable_target < 0x01800000)
          readable_target += 0x80000000;
        record.effect_palette_readable =
            IsMem1Range(readable_target, record.effect_palette.size()) &&
            ReadMem1(system, readable_target, record.effect_palette.size(),
                     record.effect_palette.data());
      }
    }
    if (samus_effect_bank_probe && !record.effect_group_present)
      return;
    if (samus_effect_bank_load_probe && !record.samus_effect_loaded_group_present)
      return;
    if (samus_effect_gx_tlut_probe && !record.samus_effect_gx_tlut_call)
      return;
    if (samus_effect_particle_probe && !record.samus_effect_particle_spawn)
      return;
    if (record.effect_group_present)
      cpu_probe_effect_group_found = true;
    for (u32 slot = 0; slot < record.fighters.size(); ++slot)
    {
      if (!fighter_present[slot])
        continue;
      const u32 pointer = fighter_pointers[slot];
      if (!IsMem1Range(pointer, CPU_PROBE_FIGHTER_FLAGS_OFFSET + 8))
      {
        SetInvalid("CPU probe fighter pointer is outside MEM1");
        return;
      }
      CpuProbeFighterRecord& fighter = record.fighters[slot];
      record.fighter_present[slot] = true;
      fighter.pointer = pointer;
      if (!ReadMem1(system, pointer, fighter.head.size(), fighter.head.data()) ||
          !ReadMem1(system, pointer + 0x1a88, fighter.cpu.size(), fighter.cpu.data()) ||
          !ReadMem1(system, pointer + CPU_PROBE_FIGHTER_FLAGS_OFFSET, fighter.flags.size(),
                    fighter.flags.data()))
      {
        SetInvalid("CPU probe fighter snapshot could not be read from MEM1");
        return;
      }
    }
    ++cpu_probe_record_count;
  }

  void CloseItemProbe()
  {
    if (!item_probe_configured || item_probe_closed)
      return;
    item_probe_closed = true;
    item_probe_published.store(true, std::memory_order_release);
  }

  bool ReadTrackedArrow(Core::System* system, ItemProbeRecord* record)
  {
    if (!fighter_present[2] || !fighter_present[3] || !fighter_pointers[2] ||
        !fighter_pointers[3])
    {
      SetInvalid("item probe cannot bind the Link Arrow without CPU slots 2 and 3");
      return false;
    }
    u32 owner_gobj = 0;
    if (!IsMem1Range(fighter_pointers[3], 4) ||
        !ReadU32(system, fighter_pointers[3], &owner_gobj) || !IsMem1Range(owner_gobj, 4))
    {
      SetInvalid("item probe slot-3 owner GObj is outside MEM1");
      return false;
    }
    u32 entities = 0;
    if (!ReadU32(system, 0x804d782c, &entities) || !IsMem1Range(entities, 0x28))
    {
      SetInvalid("item probe HSD_GObj_Entities pointer is outside MEM1");
      return false;
    }
    u32 gobj = 0;
    if (!ReadU32(system, entities + 0x24, &gobj))
    {
      SetInvalid("item probe item-list head could not be read from MEM1");
      return false;
    }
    u32 list_order = 0;
    bool found = false;
    while (gobj != 0 && list_order < 64)
    {
      std::array<u8, 0x30> gobj_bytes{};
      if (!IsMem1Range(gobj, gobj_bytes.size()) ||
          !ReadMem1(system, gobj, gobj_bytes.size(), gobj_bytes.data()))
      {
        SetInvalid("item probe item GObj list escaped MEM1");
        return false;
      }
      const u16 classifier = static_cast<u16>((static_cast<u16>(gobj_bytes[0]) << 8) |
                                               gobj_bytes[1]);
      const u32 item = ReadBE32(gobj_bytes.data() + 0x2c);
      if (gobj == ITEM_PROBE_ARROW_GOBJ || item == ITEM_PROBE_ARROW_ITEM)
      {
        if (found || classifier != 0x6 || gobj != ITEM_PROBE_ARROW_GOBJ ||
            item != ITEM_PROBE_ARROW_ITEM)
        {
          SetInvalid("item probe tracked Link Arrow GObj was reused or duplicated");
          return false;
        }
        found = true;
        record->arrow_gobj = gobj;
        record->arrow_item = item;
        record->arrow_list_order = list_order;
      }
      gobj = ReadBE32(gobj_bytes.data() + 0x8);
      ++list_order;
    }
    if (gobj != 0)
    {
      SetInvalid("item probe item list exceeds its explicit 64-GObj bound");
      return false;
    }
    if (!found)
    {
      SetInvalid("item probe tracked Link Arrow is missing from the item list");
      return false;
    }
    if (!item_probe_trigger_on_arrow_creation && !item_probe_trigger_on_arrow_launch &&
        record->arrow_list_order != 1)
    {
      SetInvalid("item probe tracked Link Arrow item-list order changed");
      return false;
    }
    if (!IsMem1Range(record->arrow_item, 0xdd0))
    {
      SetInvalid("item probe tracked Link Arrow damage-state extent is outside MEM1");
      return false;
    }
    std::array<u8, 0x30> tracked_gobj{};
    if (!ReadMem1(system, record->arrow_gobj, tracked_gobj.size(), tracked_gobj.data()))
    {
      SetInvalid("item probe tracked Link Arrow GObj could not be read from MEM1");
      return false;
    }
    record->arrow_p_link = tracked_gobj[2];
    if (record->arrow_p_link != 9 ||
        !ReadU32(system, 0x804d7834, &record->scheduler_priority) ||
        record->scheduler_priority > 0x18)
    {
      SetInvalid("item probe tracked Arrow scheduler or item-list priority is invalid");
      return false;
    }
    u32 process = ReadBE32(tracked_gobj.data() + 0x18);
    while (process != 0)
    {
      if (record->arrow_process_count >= ITEM_PROBE_MAX_PROCESSES)
      {
        SetInvalid("item probe tracked Arrow exceeds its explicit process-chain bound");
        return false;
      }
      std::array<u8, 0x18> process_bytes{};
      if (!IsMem1Range(process, process_bytes.size()) ||
          !ReadMem1(system, process, process_bytes.size(), process_bytes.data()))
      {
        SetInvalid("item probe tracked Arrow process chain escaped MEM1");
        return false;
      }
      const u32 process_owner = ReadBE32(process_bytes.data() + 0x10);
      const u32 process_priority = process_bytes[0x0c];
      if (process_owner != record->arrow_gobj || process_priority > 0x18)
      {
        SetInvalid("item probe tracked Arrow process owner or priority is invalid");
        return false;
      }
      record->arrow_process_priorities[record->arrow_process_count++] = process_priority;
      process = ReadBE32(process_bytes.data());
    }
    if (record->arrow_process_count == 0)
    {
      SetInvalid("item probe tracked Arrow has no registered scheduler processes");
      return false;
    }
    u32 owner = 0;
    if (!ReadU32(system, record->arrow_item + 0x10, &record->arrow_kind) ||
        !ReadU32(system, record->arrow_item + 0x518, &owner) ||
        record->arrow_kind != ITEM_PROBE_ARROW_KIND || owner != owner_gobj)
    {
      SetInvalid("item probe tracked Link Arrow kind or slot-3 owner changed");
      return false;
    }
    record->arrow_owner_gobj = owner;
    if (!IsMem1Range(fighter_pointers[2], 0x1a88 + 0x100))
    {
      SetInvalid("item probe slot-2 CPU state is outside MEM1");
      return false;
    }
    record->fighter_slot2_pointer = fighter_pointers[2];
    if (!ReadU32(system, fighter_pointers[2], &record->fighter_slot2_gobj))
    {
      SetInvalid("item probe slot-2 Fighter GObj could not be read");
      return false;
    }
    const u32 cpu_state = fighter_pointers[2] + 0x1a88;
    if (!IsMem1Range(cpu_state + 0xf4, 8) ||
        !ReadU32(system, cpu_state + 0xf4, &record->decision_item) ||
        !ReadU32(system, cpu_state + 0xf8, &record->decision_item_status))
    {
      SetInvalid("item probe slot-2 CPU item-selection fields are outside MEM1");
      return false;
    }
    u32 item_logic_table = 0;
    u32 common_data = 0;
    u32 shield_bounced = 0;
    u32 hit_shield = 0;
    if (!ReadU32(system, record->arrow_item + 0x28, &record->arrow_anim_id) ||
        !ReadU32(system, record->arrow_item + 0xc34, &record->arrow_damage_dealt) ||
        !ReadU32(system, record->arrow_item + 0xc50,
                 &record->arrow_pending_shield_damage) ||
        !ReadU32(system, record->arrow_item + 0xcf4,
                 &record->arrow_shield_target_gobj) ||
        !ReadMem1(system, record->arrow_item + 0xdce, 1, &record->arrow_damage_flags) ||
        !ReadU32(system, record->arrow_item + 0xc0, &record->arrow_ground_or_air) ||
        !ReadU32(system, record->arrow_item + 0xc54, &record->arrow_shield_angle_bits) ||
        !ReadU32(system, record->arrow_item + 0xb8, &item_logic_table) ||
        !IsMem1Range(item_logic_table, 0x38) ||
        !ReadU32(system, item_logic_table + 0x30, &shield_bounced) ||
        !ReadU32(system, item_logic_table + 0x34, &hit_shield) ||
        !ReadU32(system, 0x804d6d28, &common_data) ||
        !IsMem1Range(common_data, 0xe4) ||
        !ReadU32(system, common_data + 0xe0, &record->arrow_common_shield_degrees_bits))
    {
      SetInvalid("item probe tracked Link Arrow shield-dispatch fields could not be read");
      return false;
    }
    record->arrow_shield_bounced_present = shield_bounced != 0;
    record->arrow_hit_shield_present = hit_shield != 0;
    for (u32 axis = 0; axis < 3; ++axis)
    {
      if (!ReadU32(system, record->arrow_item + 0x40 + axis * 4,
                   &record->velocity_bits[axis]) ||
          !ReadU32(system, record->arrow_item + 0x4c + axis * 4,
                   &record->position_bits[axis]) ||
          !ReadU32(system, record->arrow_item + 0x62c + axis * 4,
                   &record->hitbox0_previous_endpoint_bits[axis]) ||
          !ReadU32(system, record->arrow_item + 0x620 + axis * 4,
                   &record->hitbox0_current_endpoint_bits[axis]))
      {
        SetInvalid("item probe tracked Link Arrow position or hitbox endpoint is outside MEM1");
        return false;
      }
    }
    if (!ReadU32(system, record->arrow_item + 0x5d4, &record->hitbox0_state))
    {
      SetInvalid("item probe tracked Link Arrow hitbox state could not be read");
      return false;
    }
    record->arrow_present = true;
    return true;
  }

  void RecordItemProbe(Core::System* system, u32 pc, const ItemProbePoint& point,
                       PowerPC::PowerPCState* state, u32 source_tick)
  {
    if (!item_probe_configured || !item_probe_valid || item_probe_closed || !match_active ||
        !setup_ready || match_index != item_probe_match)
      return;
    u32 instruction = 0;
    if (!ReadU32(system, pc, &instruction) || instruction != point.expected_word)
    {
      SetInvalid("item probe instruction differs from the verified GALE01r2 word");
      return;
    }
    const bool dynamic_window = item_probe_trigger_on_arrow_creation ||
                                item_probe_trigger_on_arrow_launch;
    if (dynamic_window)
    {
      if (!item_probe_window_triggered)
      {
        const bool creation_trigger = item_probe_trigger_on_arrow_creation &&
                                      point.event == ItemProbeEvent::CreationReturn &&
                                      state->gpr[3] == ITEM_PROBE_ARROW_GOBJ;
        const bool launch_trigger = item_probe_trigger_on_arrow_launch &&
                                    point.event == ItemProbeEvent::CallbackEntry &&
                                    point.pair == 10 &&
                                    state->gpr[3] == ITEM_PROBE_ARROW_GOBJ;
        if (!creation_trigger && !launch_trigger)
          return;
        if (source_tick > std::numeric_limits<u32>::max() - item_probe_capture_ticks)
        {
          SetInvalid("item probe trigger window tick range overflowed");
          return;
        }
        item_probe_window_triggered = true;
        item_probe_first_tick = source_tick;
        item_probe_last_tick = source_tick + item_probe_capture_ticks - 1;
      }
      else
      {
        if (source_tick > item_probe_last_tick)
        {
          CloseItemProbe();
          return;
        }
        if ((point.event == ItemProbeEvent::CreationReturn ||
             (point.event == ItemProbeEvent::CallbackEntry && point.pair == 10)) &&
            state->gpr[3] == ITEM_PROBE_ARROW_GOBJ)
        {
          SetInvalid("tracked Link Arrow creation or launch repeated in its probe window");
          return;
        }
        if (source_tick < item_probe_first_tick)
          return;
      }
    }
    else
    {
      // Pair tracking must be scoped to the selected interval. Otherwise an
      // unrelated earlier call can be validated against a later return.
      if (source_tick > item_probe_last_tick)
      {
        CloseItemProbe();
        return;
      }
      if (source_tick < item_probe_first_tick)
        return;
    }
    if (point.event == ItemProbeEvent::CallbackEntry || point.event == ItemProbeEvent::Call)
    {
      const u8 pair = point.pair;
      item_probe_pair_active[pair] = state->gpr[3] == ITEM_PROBE_ARROW_GOBJ;
      item_probe_pair_lr[pair] = state->spr[8];
    }
    else if (point.event == ItemProbeEvent::CallbackReturn ||
             point.event == ItemProbeEvent::CallReturn)
    {
      if (!item_probe_pair_active[point.pair])
        return;
      if (point.event == ItemProbeEvent::CallbackReturn &&
          item_probe_pair_lr[point.pair] != state->spr[8])
      {
        SetInvalid("item probe callback return did not match its tracked entry LR");
        return;
      }
      item_probe_pair_active[point.pair] = false;
    }
    else if (point.event == ItemProbeEvent::CpuEntry)
    {
      item_probe_pair_active[point.pair] =
          fighter_present[2] && state->gpr[3] == fighter_pointers[2];
      item_probe_pair_lr[point.pair] = state->spr[8];
    }
    else if (point.event == ItemProbeEvent::CpuReturn)
    {
      if (!item_probe_pair_active[point.pair])
        return;
      if (item_probe_pair_lr[point.pair] != state->spr[8])
      {
        SetInvalid("item probe CPU decision return did not match its tracked entry LR");
        return;
      }
      item_probe_pair_active[point.pair] = false;
    }
    else if (point.event == ItemProbeEvent::ArrowShieldOverlapCallSite)
    {
      constexpr u32 ITEM_HITBOX_OFFSET = 0x5d4;
      constexpr u32 ITEM_HITBOX_STRIDE = 0x13c;
      const u32 arrow_hitbox_begin = ITEM_PROBE_ARROW_ITEM + ITEM_HITBOX_OFFSET;
      bool tracked_arrow_hitbox = false;
      for (u32 hitbox = 0; hitbox < 4; ++hitbox)
      {
        if (state->gpr[3] == arrow_hitbox_begin + hitbox * ITEM_HITBOX_STRIDE)
          tracked_arrow_hitbox = true;
      }
      item_probe_pair_active[point.pair] = tracked_arrow_hitbox;
      item_probe_pair_lr[point.pair] = state->spr[8];
    }
    else if (point.event == ItemProbeEvent::ArrowShieldOverlapCallReturn)
    {
      if (!item_probe_pair_active[point.pair])
        return;
      // This is a call-site pair: the branch-and-link writes the return PC to
      // LR, so the value at this point is expected to differ from call entry.
      item_probe_pair_active[point.pair] = false;
    }
    else if (point.event == ItemProbeEvent::ArrowItemCallEntry)
    {
      item_probe_pair_active[point.pair] = state->gpr[3] == ITEM_PROBE_ARROW_ITEM;
      item_probe_pair_lr[point.pair] = state->spr[8];
    }
    else if (point.event == ItemProbeEvent::ArrowItemCallReturn)
    {
      if (!item_probe_pair_active[point.pair])
        return;
      if (item_probe_pair_lr[point.pair] != state->spr[8])
      {
        SetInvalid("item probe Arrow damage-assignment return did not match its entry LR");
        return;
      }
      item_probe_pair_active[point.pair] = false;
    }
    else if (point.event == ItemProbeEvent::ArrowTargetCheckCallSite)
    {
      item_probe_pair_active[point.pair] = item_probe_pair_active[14];
      item_probe_pair_lr[point.pair] = state->spr[8];
    }
    else if (point.event == ItemProbeEvent::ArrowTargetCheckCallReturn)
    {
      if (!item_probe_pair_active[point.pair])
        return;
      // The call-site's `bl` replaces LR with its return PC. At this
      // breakpoint the callee has returned to the instruction after the call,
      // so validate that exact PC rather than the enclosing callback's LR.
      if (state->spr[8] != point.address)
      {
        SetInvalid("item probe Arrow target-check returned to an unexpected address");
        return;
      }
      item_probe_pair_active[point.pair] = false;
    }
    bool selected = point.event == ItemProbeEvent::Boundary;
    if (point.event == ItemProbeEvent::CreationReturn)
      selected = state->gpr[3] == ITEM_PROBE_ARROW_GOBJ;
    else if (point.event == ItemProbeEvent::CallbackEntry || point.event == ItemProbeEvent::Call ||
             point.event == ItemProbeEvent::CpuEntry ||
             point.event == ItemProbeEvent::ArrowShieldOverlapCallSite ||
             point.event == ItemProbeEvent::ArrowItemCallEntry ||
             point.event == ItemProbeEvent::ArrowTargetCheckCallSite)
      selected = item_probe_pair_active[point.pair];
    else if (point.event == ItemProbeEvent::CallbackReturn ||
             point.event == ItemProbeEvent::CallReturn ||
             point.event == ItemProbeEvent::CpuReturn ||
             point.event == ItemProbeEvent::ArrowShieldOverlapCallReturn ||
             point.event == ItemProbeEvent::ArrowItemCallReturn ||
             point.event == ItemProbeEvent::ArrowTargetCheckCallReturn)
      selected = true;  // The paired target invocation was checked above.
    if (!selected)
      return;
    if (item_probe_record_count >= ITEM_PROBE_MAX_RECORDS)
    {
      SetInvalid("item probe record bound exceeded");
      return;
    }

    ItemProbeRecord& record = item_probe_records[item_probe_record_count];
    record.ordinal = static_cast<u32>(item_probe_record_count);
    record.pc = pc;
    record.expected_word = point.expected_word;
    record.source_tick = source_tick;
    record.match = match_index;
    record.lr = state->spr[8];
    record.gpr3 = state->gpr[3];
    record.gpr4 = state->gpr[4];
    record.gpr5 = state->gpr[5];
    record.gpr6 = state->gpr[6];
    if (fighter_present[0])
    {
      std::array<u8, 4> fighter_gobj{};
      record.fighter_slot0_pointer = fighter_pointers[0];
      if (!ReadMem1(system, fighter_pointers[0], fighter_gobj.size(), fighter_gobj.data()))
      {
        SetInvalid("item probe slot-0 Fighter pointer escaped MEM1");
        return;
      }
      record.fighter_slot0_gobj = ReadBE32(fighter_gobj.data());
    }
    else if (point.event == ItemProbeEvent::ArrowShieldOverlapCallSite)
    {
      SetInvalid("Arrow shield-overlap call has no checked slot-0 Fighter identity");
      return;
    }
    if (point.event == ItemProbeEvent::ArrowShieldOverlapCallSite)
    {
      record.shield_inputs_present = true;
      for (size_t index = 0; index < record.shield_fpr.size(); ++index)
        record.shield_fpr[index] = state->ps[index].PS0AsU64();
      if (!ReadMem1(system, state->gpr[3], record.shield_capsule.size(),
                    record.shield_capsule.data()) ||
          !ReadMem1(system, state->gpr[4], record.shield_result.size(),
                    record.shield_result.data()))
      {
        SetInvalid("Arrow shield-overlap capsule or HitResult escaped MEM1");
        return;
      }
      if (state->gpr[5] != 0)
      {
        record.shield_transform_present = true;
        if (!ReadBytes(system, state->gpr[5], record.shield_transform.size(),
                       record.shield_transform.data()))
        {
          SetInvalid("Arrow shield-overlap transform escaped guest memory");
          return;
        }
      }
    }
    else if (point.event == ItemProbeEvent::ArrowItemCallEntry)
    {
      constexpr u32 FIGHTER_SHIELD_HIT_OFFSET = 0x19c0;
      constexpr u32 JOBJ_MATRIX_OFFSET = 0x44;
      if (state->gpr[5] != fighter_pointers[0] || state->gpr[6] == 0 ||
          !ReadMem1(system, state->gpr[6], record.arrow_ftcoll_position.size(),
                    record.arrow_ftcoll_position.data()) ||
          !ReadMem1(system, state->gpr[5] + FIGHTER_SHIELD_HIT_OFFSET,
                    record.arrow_shield_hit.size(), record.arrow_shield_hit.data()))
      {
        SetInvalid("Arrow ftColl assignment inputs escaped the checked Fighter or MEM1 range");
        return;
      }
      const u32 shield_bone = ReadBE32(record.arrow_shield_hit.data());
      if (!IsMem1Range(shield_bone + JOBJ_MATRIX_OFFSET,
                       record.arrow_shield_bone_matrix.size()) ||
          !ReadBytes(system, shield_bone + JOBJ_MATRIX_OFFSET,
                     record.arrow_shield_bone_matrix.size(),
                     record.arrow_shield_bone_matrix.data()))
      {
        SetInvalid("Arrow ftColl shield bone matrix escaped the checked guest range");
        return;
      }
      record.arrow_ftcoll_angle = state->ps[1].PS0AsU64();
      record.arrow_ftcoll_inputs_present = true;
    }
    record.scheduler_priority = 0;
    if (!ReadTrackedArrow(system, &record))
      return;
    ++item_probe_record_count;
  }

  bool AddSlice(Core::System* system, SliceTag tag, u32 address, size_t size, u16 flags = 0)
  {
    if (slice_count >= MAX_SLICES || size > MAX_RAW - raw_size || size > UINT32_MAX)
      return false;
    if (!ReadBytes(system, address, size, raw.data() + raw_size))
      return false;
    slices[slice_count++] = {tag, flags, address, static_cast<u32>(size),
                             static_cast<u32>(raw_size)};
    raw_size += size;
    return true;
  }

  bool HasSingleSlice(SliceTag tag) const
  {
    size_t matches = 0;
    for (size_t index = 0; index < slice_count; ++index)
      matches += slices[index].tag == tag;
    return matches == 1;
  }

  bool AddFighterSlices(Core::System* system, u32 slot, u32 entity_index, u32 pointer)
  {
    const u16 flags = FighterEntitySliceFlags(slot, entity_index);
    if (!AddSlice(system, SliceTag::FighterHead, pointer, 0x100, flags) ||
        !AddSlice(system, SliceTag::FighterInputAnim, pointer + 0x620, 0x280,
                  flags) ||
        !AddSlice(system, SliceTag::FighterDamageShield, pointer + 0x1830, 0x16c,
                  flags))
      return false;
    u32 subject = 0;
    if (!ReadU32(system, pointer + 0x890, &subject))
      return false;
    if (subject && !AddSlice(system, SliceTag::FighterSubject, subject, 0x28, flags))
      return false;
    if (cpu_slots[slot] &&
        !AddSlice(system, SliceTag::CpuState, pointer + 0x1a88, 0x57c, flags))
      return false;
    return true;
  }

  // Only the explicit diagnostic profile admits a follower. These source
  // identities come from player.h/ft/types.h and ftMapping_list in player.c.
  // No guest pointer is accepted merely because it was seen by Fighter_Create.
  bool AddCheckedPlayerEntitySlices(Core::System* system, u32 requested_slot)
  {
    if (requested_slot >= 4 || active_slot_count != 4)
      return SetInvalid("Entity profile requires four declared source slots"), false;
    constexpr std::array<u32, 4> characters{15, 14, 8, 2};
    constexpr std::array<std::array<u32, 2>, 4> kinds{{{15, 0}, {10, 11}, {0, 0}, {1, 0}}};
    std::array<u32, 5> seen_gobjs{}, seen_fighters{};
    size_t seen_count = 0;
    std::array<std::array<u32, 2>, 4> gobjs{};
    for (u32 slot = 0; slot < 4; ++slot)
    {
      const u32 player = 0x80453080 + slot * 0xe90;
      std::array<u8, 0x10> identity{};
      std::array<u8, 8> entities{};
      if (!ReadBytes(system, player, identity.size(), identity.data()) ||
          !ReadBytes(system, player + 0xb0, entities.size(), entities.data()))
        return SetInvalid("Entity profile player ownership escaped its source range"), false;
      const u32 type = ReadBE32(identity.data() + 8);
      const u32 count = slot == 1 ? 2 : 1;
      if (ReadBE32(identity.data() + 4) != characters[slot] || type != 1 ||
          identity[0xc] != 0 || identity[0xd] != 1 ||
          !fighter_present[slot] || fighter_entity_count[slot] != count ||
          fighter_pointers[slot] != fighter_entity_pointers[slot][0])
        return SetInvalid("Entity profile character/type/form/ordinal inventory differs"), false;
      for (u32 ordinal = 0; ordinal < 2; ++ordinal)
      {
        const u32 gobj = ReadBE32(entities.data() + ordinal * 4);
        gobjs[slot][ordinal] = gobj;
        if (ordinal >= count)
        {
          if (gobj || fighter_entity_pointers[slot][ordinal])
            return SetInvalid("Entity profile has an undeclared follower"), false;
          continue;
        }
        u32 fighter = 0;
        std::array<u8, 0x10> head{};
        if (!gobj || !IsMem1Range(gobj, 0x30) ||
            !ReadU32(system, gobj + 0x2c, &fighter) ||
            !fighter || !IsMem1Range(fighter, 0x100) ||
            !ReadBytes(system, fighter, head.size(), head.data()) ||
            fighter == gobj || fighter != fighter_entity_pointers[slot][ordinal] ||
            ReadBE32(head.data()) != gobj || head[0xc] != slot ||
            ReadBE32(head.data() + 4) != kinds[slot][ordinal] ||
            fighter_entity_kinds[slot][ordinal] != kinds[slot][ordinal])
          return SetInvalid("Entity profile GObj/Fighter/registered owner differs"), false;
        for (size_t earlier = 0; earlier < seen_count; ++earlier)
          if (seen_gobjs[earlier] == gobj || seen_fighters[earlier] == fighter ||
              seen_gobjs[earlier] == fighter || seen_fighters[earlier] == gobj)
            return SetInvalid("Entity profile aliases another source entity owner"), false;
        seen_gobjs[seen_count] = gobj;
        seen_fighters[seen_count++] = fighter;
      }
    }
    for (u32 slot = 4; slot < 6; ++slot)
    {
      u32 type = 0;
      std::array<u8, 8> entities{};
      const u32 player = 0x80453080 + slot * 0xe90;
      if (!ReadU32(system, player + 8, &type) || type != 3 ||
          !ReadBytes(system, player + 0xb0, entities.size(), entities.data()) ||
          ReadBE32(entities.data()) || ReadBE32(entities.data() + 4))
        return SetInvalid("Entity profile has an undeclared active source slot"), false;
    }
    const u32 player = 0x80453080 + requested_slot * 0xe90;
    if (!AddSlice(system, SliceTag::PlayerIdentity, player, 0x10,
                  static_cast<u16>(requested_slot)) ||
        !AddSlice(system, SliceTag::PlayerEntities, player + 0xb0, 8,
                  static_cast<u16>(requested_slot)))
      return SetInvalid("Entity profile relationship slices escaped source ranges"), false;
    for (u32 ordinal = 0; ordinal < fighter_entity_count[requested_slot]; ++ordinal)
      if (!AddSlice(system, SliceTag::PlayerEntityUserData,
                    gobjs[requested_slot][ordinal] + 0x2c, 4,
                    FighterEntitySliceFlags(requested_slot, ordinal)))
        return SetInvalid("Entity profile user-data slice escaped source ranges"), false;
    return true;
  }

  bool AddPlayerEntitySlices(Core::System* system, u32 slot)
  {
    if (checked_entity_profile)
      return AddCheckedPlayerEntitySlices(system, slot);
    // StaticPlayer is the pinned GALE01r2 source table at 0x80453080 with
    // 0xe90-byte records. Its two HSD_GObj* player_entity fields begin at
    // +0xb0 (melee/pl/player.h). HSD_GObj::user_data is at +0x2c
    // (sysdolphin/baselib/gobj.h). Record both the entity pair and the
    // primary GObj's Fighter link; do not infer entity coverage from heads.
    const u32 entities = 0x80453080 + slot * 0xe90 + 0xb0;
    std::array<u8, 8> entity_bytes{};
    if (!ReadBytes(system, entities, entity_bytes.size(), entity_bytes.data()))
      return SetInvalid("StaticPlayer entity pair escaped its pinned source range"), false;
    const u32 primary = ReadBE32(entity_bytes.data());
    const u32 secondary = ReadBE32(entity_bytes.data() + 4);
    if (!primary || secondary)
      return SetInvalid("whole-session roster requires one primary entity and no secondary entity"),
             false;
    if (!IsMem1Range(primary, 0x30))
      return SetInvalid("primary player GObj is outside source MEM1"), false;
    u32 user_data = 0;
    if (!ReadU32(system, primary + 0x2c, &user_data) || user_data != fighter_pointers[slot])
      return SetInvalid("primary player GObj user_data does not name its observed Fighter"), false;
    if (!AddSlice(system, SliceTag::PlayerEntities, entities, entity_bytes.size(),
                  static_cast<u16>(slot)) ||
        !AddSlice(system, SliceTag::PlayerEntityUserData, primary + 0x2c, 4,
                  static_cast<u16>(slot)))
      return SetInvalid("player entity relationship escaped the pinned source ranges"), false;
    return true;
  }

  bool AddMatchSlices(Core::System* system)
  {
    if (!AddSlice(system, SliceTag::MatchClock, 0x8046b6a0, 0x2e) ||
        !AddSlice(system, SliceTag::PadSnapshot, 0x804c1f84, 0x358) ||
        !AddSlice(system, SliceTag::SceneFrame, 0x80479d58, 4))
      return false;
    u32 rng_pointer = 0;
    if (!AddSlice(system, SliceTag::RngPointer, 0x804d5f94, 4) ||
        !ReadU32(system, 0x804d5f94, &rng_pointer) || !rng_pointer ||
        !AddSlice(system, SliceTag::RngValue, rng_pointer, 4))
      return false;
    for (u32 slot = 0; slot < 4; ++slot)
    {
      if (!fighter_present[slot])
        continue;
      if (!AddPlayerEntitySlices(system, slot))
        return false;
      for (u32 entity_index = 0; entity_index < fighter_entity_count[slot]; ++entity_index)
      {
        if (!AddFighterSlices(system, slot, entity_index,
                              fighter_entity_pointers[slot][entity_index]))
          return false;
      }
      if (!AddSlice(system, SliceTag::FighterStocks, 0x80453080 + slot * 0xe90 + 0x8e, 1,
                    static_cast<u16>(slot)) ||
          !AddSlice(system, SliceTag::Hud, 0x804a10c8 + slot * 0x64, 0x11,
                    static_cast<u16>(slot)) ||
          !AddSlice(system, SliceTag::Magnifier, 0x804a1de0 + 0x14 + slot * 0x10 + 0xc, 1,
                    static_cast<u16>(slot)))
        return false;
    }
    if (!AddSlice(system, SliceTag::Camera, 0x80452c68, 0x4c))
      return false;
    u32 camera_object = 0;
    u32 camera = 0;
    if (!ReadU32(system, 0x80452c68, &camera) || !camera ||
        !ReadU32(system, camera + 0x28, &camera_object) || !camera_object ||
        !AddSlice(system, SliceTag::CameraObjectPointer, camera + 0x28, 4) ||
        !AddSlice(system, SliceTag::CameraProjection, camera_object, 0x51))
      return false;
    return true;
  }

  bool ReadProfileRoot(Core::System* system, u32* main_data) const
  {
    // gmMainLib_804D3EE0 points at the source-owned gmm_x0. The retail
    // gmMainLib_GetSaveData/15ED8C/15EDA4 instruction bodies load the save
    // block at +0x1868 and its first two u16 masks at +0/+2.
    return ReadU32(system, PROFILE_ROOT_GLOBAL, main_data) && *main_data != 0;
  }

  bool AddProfileSlices(Core::System* system)
  {
    u32 main_data = 0;
    if (!ReadProfileRoot(system, &main_data) || main_data > UINT32_MAX - 0x186a ||
        !AddSlice(system, SliceTag::ProfileCharacters, main_data + 0x1868, 2) ||
        !AddSlice(system, SliceTag::ProfileStages, main_data + 0x186a, 2))
      return false;
    return true;
  }

  bool AddSceneKindSlice(Core::System* system)
  {
    // 0x804D6720 holds the source scene object; its first byte is the scene
    // kind the source routing and menu owners branch on. Capture it beside the
    // routing bytes so a capture can be steered without guest memory access.
    // The scene object does not exist before the first scene is created, so an
    // absent pointer omits the slice instead of invalidating the boundary.
    u32 scene_pointer = 0;
    if (!ReadU32(system, 0x804d6720, &scene_pointer) || !scene_pointer)
      return true;
    return AddSlice(system, SliceTag::SceneKind, scene_pointer, 1);
  }

  bool ReadCssJoint(Core::System* system, u32 root, u8 index, u32* result) const
  {
    // Read-only equivalent of the traversal order in lb_80011E24. The
    // authored CSSDoor joint index is a u8, so at most 256 visited nodes
    // can precede the requested node. HSD_JObj INSTANCE nodes skip children.
    std::array<u32, 256> visited{};
    u32 node = root;
    for (u32 ordinal = 0; node && ordinal <= index; ++ordinal)
    {
      for (u32 previous = 0; previous < ordinal; ++previous)
        if (visited[previous] == node)
          return false;
      visited[ordinal] = node;
      if (ordinal == index)
      {
        *result = node;
        return true;
      }
      u32 flags = 0, child = 0, next = 0;
      if (!ReadU32(system, node + 0x14, &flags) ||
          !ReadU32(system, node + 0x10, &child) ||
          !ReadU32(system, node + 8, &next))
        return false;
      if (!(flags & (1u << 12)) && child)
        node = child;
      else if (next)
        node = next;
      else
      {
        // Every ancestor must be one of the nodes already traversed.
        u32 parent = node;
        node = 0;
        for (u32 ascent = 0; ascent <= ordinal; ++ascent)
        {
          if (!ReadU32(system, parent + 0xc, &parent))
            return false;
          if (!parent)
            break;
          bool seen = false;
          for (u32 previous = 0; previous <= ordinal; ++previous)
            seen |= visited[previous] == parent;
          if (!seen || !ReadU32(system, parent + 8, &next))
            return false;
          if (next)
          {
            node = next;
            break;
          }
          if (ascent == ordinal)
            return false;
        }
      }
    }
    return false;
  }

  bool AddCssCpuSteeringSlices(Core::System* system)
  {
    u32 css = 0, root = 0;
    if (!ReadU32(system, 0x804d6cb0, &css) ||
        !ReadU32(system, 0x804d6cc0, &root))
      return false;
    if (!css || !root)
      return true; // CSS OnEnter has not published its owners yet.
    u8 match_type = 0;
    if (!ReadBytes(system, css + 2, 1, &match_type))
      return false;
    if (match_type != 0)
      return true; // These controls belong to ordinary VS_MELEE.
    // CSSData: 8-byte menu header, 8-byte VsModeData header, then the
    // complete 0x138-byte StartMeleeData (including all six source slots).
    if (!AddSlice(system, SliceTag::MenuCssLiveState, css, 0x148))
      return false;
    for (u32 port = 0; port < 4; ++port)
    {
      u32 model = 0;
      if (!ReadU32(system, 0x804a0bd0 + port * 4, &model))
        return false;
      if (!model)
        continue;
      // CSSCharModel stores authored token positions at +8/+c; no guest
      // function is called and no controller or gameplay state is changed.
      if (!AddSlice(system, SliceTag::MenuCssModel, model, 0x18,
                    static_cast<u16>(port)))
        return false;
      for (u32 variant = 0; variant < 2; ++variant)
      {
        u8 index = 0;
        u32 joint = 0;
        if (!ReadBytes(system, CSS_DOORS_STATE + port * 0x24 + 7 + variant,
                       1, &index) ||
            !ReadCssJoint(system, root, index, &joint) ||
            !AddSlice(system, SliceTag::MenuCssSlider, joint, 0x74,
                      static_cast<u16>(port * 2 + variant)))
          return false;
      }
    }
    return true;
  }

  bool AddMenuSteeringSlices(Core::System* system)
  {
    // mnmain.h: MenuFlow (0x18) and MenuInputState (8), at the
    // GALE01r2 symbols mn_804A04F0 and mn_804D6BC8. The original main
    // menu rejects input during its source cooldown; observing that boundary
    // avoids treating a wall-clock delay as proof that an input was accepted.
    u32 scene_pointer = 0;
    u8 scene_kind = 0;
    if (!ReadU32(system, 0x804d6720, &scene_pointer) || !scene_pointer)
      return true;
    if (!ReadBytes(system, scene_pointer, 1, &scene_kind))
      return false;
    if (scene_kind == 1 &&
        (!AddSlice(system, SliceTag::MenuMainFlow, 0x804a04f0, 0x18) ||
         !AddSlice(system, SliceTag::MenuMainInput, 0x804d6bc8, 8)))
      return false;
    if (scene_kind == 1 && (Env("MWRC_SD_MENU_PROBE") == "items_row" ||
                            Env("MWRC_SD_MENU_PROBE") == "sd_prefix" ||
                            Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()))
    {
      u8 menu_kind = 0;
      if (!ReadBytes(system, 0x804a04f0, 1, &menu_kind))
        return false;
      if (menu_kind == 16)
      {
        // fn_80233E10: lbz r0,-0x4ab4(r13), cmpwi, bne.
        // Verified original r13=0x804db6a0 derives u8 0x804d6bec.
        const std::array<u32, 3> words = {0x880db54c, 0x28000000, 0x40820180};
        for (size_t i = 0; i < words.size(); ++i)
        {
          u32 word = 0;
          if (!ReadU32(system, 0x80233ec0 + static_cast<u32>(i * 4), &word) || word != words[i])
            return false;
        }
        if (!AddSlice(system, SliceTag::SdItemsLock, 0x804d6bec, 1))
          return false;
      }
    }
    // Source menu globals survive arena teardown. PAD interrupts can run
    // inside OnEnter while those globals still point into the old arena.
    // Publish CSS steering only after the verified OnEnter return.
    if (scene_kind == 8 && (whole_session_enabled() || SdInitRequested()) && !css_steering_ready)
      return true;
    if (scene_kind == 8 && transform_prefix_enabled && !transform_prefix_css_ready)
      return true;
    if (scene_kind == 8 && !AddCssCpuSteeringSlices(system))
      return false;
    // Menu globals retain pointers after their scene arena is reclaimed.
    // Observe each steering owner only in its live source menu scene.
    u8 stage_index = 0;
    if (scene_kind == 9 && transform_prefix_enabled && !transform_prefix_sss_ready)
      return true;
    if (scene_kind == 9 && (Env("MWRC_SD_MENU_PROBE") == "sd_prefix" || Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()))
    {
      if (!sd_sss_ready)
        return true;
      // mnStageSel_80259C28 tests lwz r0,-0x49fc(r13), cmpwi, bne
      // before accepting confirmation. Original GALE01r2 r13 is 0x804db6a0.
      const std::array<u32, 3> words = {0x800db604, 0x28000000, 0x40820128};
      for (size_t i = 0; i < words.size(); ++i)
      {
        u32 word = 0;
        if (!ReadU32(system, 0x80259c3c + static_cast<u32>(i * 4), &word) || word != words[i])
          return false;
      }
      if (!AddSlice(system, SliceTag::SdStageCooldown, 0x804d6ca4, 4))
        return false;
    }
    if (scene_kind == 9 && ReadBytes(system, STAGE_SELECT_INDEX, 1, &stage_index))
    {
      if (!AddSlice(system, SliceTag::StageSelectIndex, STAGE_SELECT_INDEX, 1))
        return false;
      if (stage_index < STAGE_SELECT_COUNT)
      {
        const u32 kind_address = STAGE_SELECT_TABLE +
            static_cast<u32>(stage_index) * STAGE_SELECT_STRIDE + STAGE_SELECT_KIND_OFFSET;
        if (!AddSlice(system, SliceTag::StageSelectKind, kind_address, 1))
          return false;
      }
    }
    if (scene_kind == 9 && transform_prefix_enabled)
    {
      u8 source_mode = 0;
      if (!ReadBytes(system, 0x80479d30, 1, &source_mode))
        return false;
      transform_prefix_sss_live_owner_seen |= TransformPrefixSssOwnerReady(
          transform_prefix_sss_ready, transform_prefix_css_live_owner_seen,
          source_mode == 0x02, HasSingleSlice(SliceTag::StageSelectIndex),
          HasSingleSlice(SliceTag::StageSelectKind));
    }
    if (scene_kind != 8)
      return true;
    if (!AddSlice(system, SliceTag::MenuCssDoors, CSS_DOORS_STATE, CSS_DOORS_BYTES))
      return false;
    for (u32 port = 0; port < CSS_CURSOR_PORTS; ++port)
    {
      u32 cursor = 0;
      if (!ReadU32(system, CSS_CURSOR_POINTERS + port * 4, &cursor))
        return false;
      if (!cursor)
        continue;
      if (!AddSlice(system, SliceTag::MenuCssCursor, cursor, CSS_CURSOR_BYTES,
                    static_cast<u16>(port)))
        return false;
    }
    if (transform_prefix_enabled)
    {
      u8 source_mode = 0;
      if (!ReadBytes(system, 0x80479d30, 1, &source_mode))
        return false;
      transform_prefix_css_live_owner_seen |= TransformPrefixCssOwnerReady(
          transform_prefix_css_ready, source_mode == 0x02,
          HasSingleSlice(SliceTag::MenuCssLiveState),
          HasSingleSlice(SliceTag::MenuCssDoors));
    }
    return true;
  }

  bool AddProfileContextSlices(Core::System* system)
  {
    // Typed first-CSS context. Every CSS entry publishes the authored rules
    // and save block once, so the offline comparison can bind the profile the
    // first CSS ran with instead of only the two unlock masks. The save block
    // contains the persistent fighter records and name banks, so they are not
    // captured a second time.
    u32 main_data = 0;
    if (!ReadProfileRoot(system, &main_data) ||
        main_data > UINT32_MAX - PROFILE_LAST_BYTE_OFFSET ||
        !AddSlice(system, SliceTag::ProfileGameRules,
                  main_data + PROFILE_GAME_RULES_OFFSET, PROFILE_GAME_RULES_SIZE) ||
        !AddSlice(system, SliceTag::ProfileSaveData,
                  main_data + PROFILE_SAVE_DATA_OFFSET, PROFILE_SAVE_DATA_SIZE))
      return false;
    return true;
  }

  bool AddSessionSlices(Core::System* system)
  {
    u32 rng_pointer = 0;
    return AddProfileSlices(system) &&
           AddSlice(system, SliceTag::PadSnapshot, 0x804c1f84, 0x358) &&
           AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6) &&
           AddSlice(system, SliceTag::SceneFrame, 0x80479d58, 4) &&
           AddSceneKindSlice(system) &&
           AddSlice(system, SliceTag::RngPointer, 0x804d5f94, 4) &&
           ReadU32(system, 0x804d5f94, &rng_pointer) && rng_pointer &&
           AddSlice(system, SliceTag::RngValue, rng_pointer, 4);
  }

  bool AddMenuSlices(Core::System* system, Boundary boundary, u32 entry_argument)
  {
    const bool css = boundary == Boundary::CssEnter || boundary == Boundary::CssCancelEnter ||
                     boundary == Boundary::CssExit ||
                     boundary == Boundary::ReturnCss;
    const u32 pointer_address = css ? 0x804d6cb0 : 0x804d6c90;
    const bool entering = boundary == Boundary::CssEnter ||
                          boundary == Boundary::CssCancelEnter ||
                          boundary == Boundary::SssEnter ||
                          boundary == Boundary::ReturnCss;
    // These raw hooks run at function entry, before OnEnter assigns the
    // scene's static pointer. Use its actual argument for entry records;
    // exit records use the pointer published by that original initializer.
    // Completed menu callbacks still require the transition-trace join.
    u32 state_pointer = entry_argument;
    if ((!entering && !ReadU32(system, pointer_address, &state_pointer)) || !state_pointer ||
        state_pointer > UINT32_MAX - 0x10 ||
        !AddSlice(system, css ? SliceTag::MenuCssState : SliceTag::MenuSssState,
                  state_pointer + 0x10, 0xf0) ||
        (!css && (state_pointer > UINT32_MAX - 4 ||
                  !AddSlice(system, SliceTag::MenuSssRoute, state_pointer + 4, 1))) ||
        !AddSlice(system, SliceTag::MenuAudio, 0x803bb300, 0x40) ||
        !AddSlice(system, SliceTag::MenuAudioVoice, 0x804d6038, 4) ||
        (css && entering && !AddProfileContextSlices(system)) ||
        !AddSessionSlices(system))
      return false;
    if (css)
    {
      u32 ko_counts = 0;
      if (!AddSlice(system, SliceTag::MenuCssContext, state_pointer, 0x148) ||
          !ReadU32(system, state_pointer + 4, &ko_counts) || !ko_counts ||
          // gmvsmelee.c owns ko_counts[GM_MAX_PLAYERS], including the two
          // non-CSS source slots. Preserve the complete authored array.
          !AddSlice(system, SliceTag::MenuCssKoCounts, ko_counts, 6))
        return false;
    }
    return true;
  }

  static bool BoundaryForPC(u32 pc, bool whole_session, Boundary* boundary)
  {
    switch (pc)
    {
    case 0x8034dd8c:
      *boundary = Boundary::PadPoll;
      return true;
    case 0x80377584:
      *boundary = Boundary::PadConsume;
      return true;
    case 0x800693a8:
      *boundary = Boundary::FighterCreate;
      return true;
    case 0x8016e934:
      *boundary = Boundary::Entry;
      return true;
    case 0x8016e9c4:
      *boundary = Boundary::Setup;
      return true;
    case 0x80390eb4:
      *boundary = Boundary::SourceTick;
      return true;
    case 0x80390fc0:
      *boundary = Boundary::DrawEnter;
      return true;
    case 0x80391040:
      *boundary = Boundary::DrawReturn;
      return true;
    case 0x8016e9c8:
      *boundary = whole_session ? Boundary::VsExit : Boundary::ResultEnter;
      return true;
    case 0x8016ebbc:
      *boundary = whole_session ? Boundary::VsExitReturn : Boundary::ResultReturn;
      return true;
    case 0x8039157c:
      *boundary = Boundary::SceneTeardown;
      return true;
    case 0x801a4b70:
      *boundary = Boundary::SceneExit;
      return true;
    case 0x8026688c:
      *boundary = Boundary::CssEnter;
      return whole_session;
    case 0x80266d70:
      *boundary = Boundary::CssExit;
      return whole_session;
    case 0x8025a998:
      *boundary = Boundary::SssEnter;
      return whole_session;
    case 0x8025bbd0:
      // The original OnExit writes start_game before returning. Its entry
      // still holds the old route and would classify A-confirm as cancel.
      *boundary = Boundary::SssExit;
      return whole_session;
    case 0x801a5af0:
      *boundary = Boundary::VsModeExit;
      return whole_session;
    case 0x80177368:
      *boundary = Boundary::ResultsEnter;
      return whole_session;
    case 0x80177704:
      *boundary = Boundary::ResultsExit;
      return whole_session;
    case 0x801a5f64:
      *boundary = Boundary::ResultsModeExit;
      return whole_session;
    case 0x80179350:
      *boundary = Boundary::ResultsGObjProcess;
      return whole_session;
    case 0x801bfcfc:
      *boundary = Boundary::PrizeModeEnter;
      return whole_session;
    case 0x802febe0:
      *boundary = Boundary::PrizeSceneEnter;
      return whole_session;
    case 0x802fed10:
      *boundary = Boundary::PrizeSceneExit;
      return whole_session;
    case 0x801a6308:
      *boundary = Boundary::PrizeModeExit;
      return whole_session;
    case 0x801bff7c:
      *boundary = Boundary::StartupPrizeModeExit;
      return whole_session;
    default:
      return false;
    }
  }

  bool BoundaryInstructionMatches(Core::System* system, u32 pc) const
  {
    u32 word = 0;
    if (!ReadU32(system, pc, &word))
      return false;
    switch (pc)
    {
    case 0x8034dd8c:
      {
        u32 restore = 0;
        return word == 0x7ec3b378 && ReadU32(system, pc + 4, &restore) &&
               restore == 0x4bff95fd;
      }
    case 0x80377584:
      return word == 0x3b7e0028;
    case 0x8016e9c4:
    case 0x80391040:
    case 0x8016ebbc:
    case 0x8039157c:
    case 0x801a4b70:
    case 0x8025bbd0:
      return word == 0x4e800020;
    case 0x8016e9c8:
      return word == 0x7c0802a6;
    case 0x8026688c:
    case 0x80266d70:
    case 0x8025a998:
    case 0x801a5af0:
    case 0x80177368:
    case 0x80177704:
    case 0x801a5f64:
    case 0x80179350:
    case 0x801bfcfc:
    case 0x802febe0:
    case 0x801a6308:
    case 0x801bff7c:
      // These source function entries are pinned to the owned DOL's
      // prologue word.  The digest check establishes the remaining bytes.
      return word == 0x7c0802a6;
    case 0x802fed10:
      // The retail Prize scene has an empty OnExit callback (blr at entry).
      return word == 0x4e800020;
    default:
      // The pinned DOL digest has already established the exact source for
      // boundary PCs whose neighboring words are not part of the harness's
      // published guards.
      return word != 0;
    }
  }

  bool AddOrdinaryLive(Core::System* system, std::array<u8, 2>* stocks,
                       std::array<u32, 2>* positions)
  {
    // gm_Scene_Vs_OnExit publishes MatchEnd without destroying fighters.
    // Nevertheless revalidate each StaticPlayer->GObj->Fighter link at every
    // scheduled/terminal observation; recorded creation pointers alone do not
    // attest current ownership. Arena retirement clears them before completion.
    if (sd_init.phase != SdInitState::Phase::VsActive && sd_init.phase != SdInitState::Phase::VsExited)
      return false;
    u32 scene = 0;
    u8 kind = 0, mode = 0;
    if (!ReadU32(system, 0x804d6720, &scene) || !scene ||
        !ReadBytes(system, scene, 1, &kind) || kind != 2 ||
        !ReadBytes(system, 0x80479d30, 1, &mode) || mode != 2 ||
        !AddSceneKindSlice(system) || !AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6))
      return false;
    for (u32 slot = 0; slot < 2; ++slot)
    {
      std::array<u8, 0x100> head{};
      u32 damage = 1;
      if (!fighter_present[slot] || !AddPlayerEntitySlices(system, slot) ||
          !ReadBytes(system, fighter_pointers[slot], head.size(), head.data()) ||
          head[12] != slot || ReadBE32(head.data() + 4) != 0 ||
          !ReadU32(system, fighter_pointers[slot] + 0x1830, &damage) || damage != 0 ||
          !ReadBytes(system, 0x80453080 + slot * 0xe90 + 0x8e, 1, &(*stocks)[slot]) ||
          !AddSlice(system, SliceTag::FighterHead, fighter_pointers[slot], 0x100, slot) ||
          !AddSlice(system, SliceTag::FighterDamageShield, fighter_pointers[slot] + 0x1830, 4, slot) ||
          !AddSlice(system, SliceTag::FighterStocks, 0x80453080 + slot * 0xe90 + 0x8e, 1, slot))
        return false;
      (*positions)[slot] = ReadBE32(head.data() + 0xb0);
    }
    return true;
  }

  void SdEvent(const char* name, u32 pc, u32 tick, Event event = Event::Progress)
  {
    std::string json = "{\"diagnostic\":\"sd_initialization_prefix\",\"name\":\"" +
                       std::string(name) + "\",\"consumed\":" +
                       std::to_string(sd_init.consumed) + ",\"menu_consumed\":" +
                       std::to_string(sd_menu_consumed) + ",\"pc\":" + std::to_string(pc) + ",\"slices\":[";
    for (size_t index = 0; index < slice_count; ++index)
    {
      const auto& slice = slices[index];
      if (index) json += ",";
      json += "{\"tag\":" + std::to_string(static_cast<u16>(slice.tag)) +
              ",\"flags\":" + std::to_string(slice.flags) + ",\"address\":" +
              std::to_string(slice.address) + ",\"hex\":\"";
      if (!AppendHexBytes(&json, raw.data() + slice.offset, slice.size))
        return SetInvalid("SD prefix hex payload exceeds its bound"), void();
      json += "\"}";
    }
    json += "]}";
    if (OrdinaryTimeoutRequested())
    {
      const std::string event_name(name);
      const size_t cap = event_name == "menu" ? 6144 :
                         (event_name == "input" || event_name == "menu_input" || event_name == "vs_retired") ? 512 :
                         event_name == "tick" ? 2048 : event_name == "vs_setup" ? 4096 :
                         (event_name == "vs_exit" || event_name == "terminal_rejected") ? 8192 : 65536;
      if (json.size() > cap)
        return SetInvalid("Ordinary timeout event exceeds its serialized ceiling"), void();
    }
    PushJson(event, json, pc, tick);
  }

  void OrdinaryTerminalFailure(const char* reason, u32 pc, u32 tick)
  {
    // Preserve the actual slices already read at this failed publication.
    // This is an error record, never a successful vs_exit or input completion.
    SdEvent("terminal_rejected", pc, tick, Event::Error);
    SetInvalid(reason);
  }

  void SparseInputFailure(const char* reason, u32 pc, u32 tick)
  {
    // Preserve the queue descriptor and consumed full four-port slot already
    // read at this boundary. Error-only: never a successful input or finish.
    SdEvent("input_rejected", pc, tick, Event::Error);
    SetInvalid(reason);
  }

  void ObserveSdInit(Core::System* system, u32 pc, PowerPC::PowerPCState* state)
  {
    u32 tick = 0;
    if (!ReadU32(system, 0x80479d58, &tick))
      return SetInvalid("SD prefix source counter is invalid"), void();
    raw_size = 0;
    slice_count = 0;
    if ((Env("MWRC_SD_MENU_PROBE") == "sd_prefix" || Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()) &&
        (pc == 0x8025a998 || pc == 0x8025b84c))
    {
      u32 word = 0;
      if (!ReadU32(system, pc, &word) || word != (pc == 0x8025a998 ? 0x7c0802a6 : 0x4e800020))
        return SetInvalid("SD prefix SSS readiness instruction differs"), void();
      sd_sss_ready = pc == 0x8025b84c;
      return;
    }
    if (pc == CSS_ENTER_RETURN)
    {
      u32 word = 0;
      if (!ReadU32(system, pc, &word) || word != 0x4e800020)
        return SetInvalid("SD prefix CSS readiness instruction differs"), void();
      css_steering_ready = true;
      return;
    }
    if (pc == 0x8026688c) css_steering_ready = false;
    if (pc == 0x8034dd8c)
    {
      u32 caller = 0;
      if (!ReadU32(system, state->gpr[1] + 0x54, &caller) || caller != PAD_READ_HSD_CALLER)
        return;
      if (!BoundaryInstructionMatches(system, pc) || !AddSceneKindSlice(system) ||
          !AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6) ||
          !AddMenuSteeringSlices(system))
        return SetInvalid("SD prefix menu polling owner is invalid"), void();
      if (sd_init.phase == SdInitState::Phase::Menu)
      {
        if (++sd_menu_polls > 7200)
          return SetInvalid("SD prefix menu polling cap exhausted"), void();
        SdEvent("menu", pc, tick);
        if (Env("MWRC_SD_MENU_PROBE") == "items_row" && sd_rules_observed && sd_menu_neutral)
        {
          std::array<u8, 0x18> flow{};
          std::array<u8, 8> input{};
          u8 lock = 1;
          if (raw[0] == 1 && ReadBytes(system, 0x804a04f0, flow.size(), flow.data()) &&
              ReadBytes(system, 0x804d6bc8, input.size(), input.data()) &&
              ReadBytes(system, 0x804d6bec, 1, &lock) && flow[0] == 16 &&
              flow[2] == 0 && flow[3] == 31 && flow[4] == 3 && flow[0x11] == 1 &&
              input[0] == 0 && input[1] == 0 && lock == 0)
          {
            SdEvent("items_ready", pc, tick);
            InputStream::RequestFinish(true);
            natural_completion.store(false);
            finish_requested.store(true);
          }
        }
        if (!sd_rules_observed && !Env("MWRC_SD_MENU_PROBE").empty() && sd_menu_consumed && sd_menu_neutral)
        {
          std::array<u8, 0x18> flow{};
          std::array<u8, 8> input{};
          u32 profile = 0;
          const u8* scene = raw.data();
          if (scene[0] == 1 && ReadBytes(system, 0x804a04f0, flow.size(), flow.data()) &&
              ReadBytes(system, 0x804d6bc8, input.size(), input.data()) &&
              flow[0] == 13 && flow[2] == 0 && flow[3] == 0 && flow[4] == 0 &&
              // Original Confirm preserves forward-entry direction after
              // cooldown; only the exact GCI campaign takes this route.
              flow[0x11] == (Env("MWRC_SD_PROFILE_GCI_SHA256").empty() ? 0 : 1) &&
              input[0] == 0 && input[1] == 0)
          {
            if (!ReadProfileRoot(system, &profile) ||
                !AddSlice(system, SliceTag::SdRumblePorts, profile + 0x1cc0, 4))
              return SetInvalid("SD Rules probe profile is invalid"), void();
            if (!Env("MWRC_SD_PROFILE_GCI_SHA256").empty())
            {
              // Existing typed profile ranges, now observed at this reduced
              // gate. Actual getter words bind both roots before reading them.
              const std::array<u32, 6> words = {0x806d8840, 0x38631868, 0x4e800020,
                                               0x806d8840, 0x38632ff8, 0x4e800020};
              for (size_t i = 0; i < words.size(); ++i)
              {
                u32 word = 0;
                if (!ReadU32(system, 0x8015cc40 + static_cast<u32>(i * 4), &word) || word != words[i])
                  return SetInvalid("SD loaded-profile getter identity differs"), void();
              }
              if (!AddProfileSlices(system) || !AddProfileContextSlices(system))
                return SetInvalid("SD loaded-profile context is missing"), void();
            }
            SdEvent("rules_ready", pc, tick);
            sd_rules_observed = true;
            if (Env("MWRC_SD_MENU_PROBE") == "rules_ready")
            {
              InputStream::RequestFinish(true);
              natural_completion.store(false);
              finish_requested.store(true);
            }
          }
        }
      }
      return;
    }
    if (pc == 0x80390eb4 && sd_init.phase != SdInitState::Phase::Menu)
    {
      if (SparsePairRequested())
        return;  // This milestone stops at consumed input, before gameplay ticks.
      if (!BoundaryInstructionMatches(system, pc) ||
          !AddSlice(system, SliceTag::MatchClock, 0x8046b6a0, 0x2e))
        return SetInvalid("SD prefix scheduler return instruction differs"), void();
      if (OrdinaryTimeoutRequested())
      {
        std::array<u8, 2> stocks{};
        std::array<u32, 2> positions{};
        if (sd_init.phase != SdInitState::Phase::VsActive || !AddOrdinaryLive(system, &stocks, &positions) ||
            !ordinary_timeout.Tick(tick, ReadBE32(raw.data() + 0x24), ReadBE32(raw.data() + 0x28),
                                   (raw[0x2c] << 8) | raw[0x2d], stocks[0], stocks[1], positions[0], positions[1]))
          return SetInvalid("Ordinary timeout source clock/live ownership/policy differs"), void();
      }
      SdEvent("tick", pc, tick);
      return;
    }
    if (pc == 0x8016e934 || pc == 0x8016ebc0)
    {
      const bool sudden = pc == 0x8016ebc0;
      if (sudden && (Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested()))
        return SetInvalid("Competitive profile entry cannot admit SD"), void();
      u32 word = 0, root = 0, scene = 0;
      std::array<u8, 6> route{};
      if (!ReadU32(system, pc, &word) || word != 0x7c0802a6 ||
          !ReadU32(system, 0x804d6720, &scene) || !scene ||
          !ReadBytes(system, 0x80479d30, route.size(), route.data()))
        return SetInvalid("SD prefix entry instruction/routing is invalid"), void();
      const auto* kind = system->GetMemory().GetPointerForRange(scene, 1);
      // Opening demos also use the VS initializer. They are outside this recipe.
      if (!sudden && sd_init.phase == SdInitState::Phase::Menu && route[0] == 0x18) return;
      if (Env("MWRC_SD_MENU_PROBE") == "rules_ready" || Env("MWRC_SD_MENU_PROBE") == "items_row")
        return SetInvalid("SD Rules probe entered undeclared gameplay"), void();
      if (!kind || *kind != (sudden ? 3 : 2) || route[0] != 2 ||
          !sd_init.Entry(state->gpr[3], sudden))
        return SetInvalid("SD prefix entry is missing its declared phase"), void();
      // Actual getter words load gmMainLib_804D3EE0 and add +0x590.
      const std::array<u32, 3> getter = {0x806d8840, 0x38630590, 0x4e800020};
      for (size_t i = 0; i < getter.size(); ++i)
        if (!ReadU32(system, 0x801a5244 + static_cast<u32>(i * 4), &word) || word != getter[i])
          return SetInvalid("SD prefix VS payload getter differs from verified DOL"), void();
      if (!ReadProfileRoot(system, &root) || root > UINT32_MAX - 0x1868 ||
          !AddSlice(system, SliceTag::MatchSetup, sd_init.setup_pointer, 0x138) ||
          !AddSlice(system, SliceTag::MatchSetup, root + 0x598, 0x138, 1) ||
          !AddSlice(system, SliceTag::ProfileGameRules, root + 0x1850, 0x18) ||
          !AddSlice(system, SliceTag::SdRumblePorts, root + 0x1cc0, 4) ||
          !AddSlice(system, SliceTag::PadSnapshot, 0x804c1f84, 0x358) || !AddProfileSlices(system) ||
          !AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6) || !AddSceneKindSlice(system))
        return SetInvalid("SD prefix setup/persistent VS payload is invalid"), void();
      if ((Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()) &&
          !AddSlice(system, SliceTag::ProfileSaveData, root + PROFILE_SAVE_DATA_OFFSET, PROFILE_SAVE_DATA_SIZE))
        return SetInvalid("Original committed profile/save data is missing"), void();
      u32 rng = 0;
      if (!ReadU32(system, 0x804d5f94, &rng) || !rng ||
          !AddSlice(system, SliceTag::RngPointer, 0x804d5f94, 4) ||
          !AddSlice(system, SliceTag::RngValue, rng, 4))
        return SetInvalid("SD prefix RNG context is invalid"), void();
      fighter_present.fill(false);
      fighter_pointers.fill(0);
      sparse_setup_seen = false;
      sparse_setup_consumed = 0;
      sparse_source_samples = 0;
      sparse_prepress_neutral_samples = 0;
      sparse_witness_phase = 0;
      SdEvent(sudden ? "sd_entry" : "vs_entry", pc, tick);
      return;
    }
    if (pc == 0x800693a8 &&
        (sd_init.phase == SdInitState::Phase::VsSetup || sd_init.phase == SdInitState::Phase::SdSetup))
    {
      u32 pointer = 0;
      u8 slot = 0xff;
      if (!BoundaryInstructionMatches(system, pc) ||
          !ReadU32(system, state->gpr[3] + 0x2c, &pointer) ||
          !ReadFighterSourceSlot(system, pointer, &slot) ||
          (SparsePairRequested() ? (slot != 0 && slot != 2) : slot > 1) || fighter_present[slot])
        return SetInvalid("SD prefix fighter creation is missing, repeated or outside its declared source pair"), void();
      fighter_present[slot] = true;
      fighter_pointers[slot] = pointer;
      return;
    }
    if (pc == 0x80377584)
    {
      std::array<u8, 0xc> queue{};
      const bool menu = sd_init.phase == SdInitState::Phase::Menu;
      const bool sparse_active = SparsePairRequested() && sd_init.phase == SdInitState::Phase::VsActive;
      const u32 sparse_cap = sparse_setup_consumed + SPARSE_SOURCE_SAMPLE_CAP;
      if (!BoundaryInstructionMatches(system, pc) ||
          (!menu && !sd_init.Consume(sparse_active ? sparse_cap :
                                     OrdinaryTimeoutRequested() ? 29523 : SdInitState::sample_cap)) ||
          !ReadBytes(system, 0x804c1f78, queue.size(), queue.data()) || !queue[0] ||
          state->gpr[6] >= queue[0] || ReadBE32(queue.data() + 8) + state->gpr[6] * 0x30 != state->gpr[25] ||
          (SparsePairRequested() && !AddSlice(system, SliceTag::PadQueue, 0x804c1f78, queue.size())) ||
          !AddSlice(system, SliceTag::PadSlot, state->gpr[25], 0x30))
        return SetInvalid("SD prefix input queue or declared sample cap is invalid"), void();
      const u8* pad = raw.data();
      if (menu)
      {
        if (SparsePairRequested())
        {
          pad = SparsePadSlotBytes(raw.data(), raw_size, slices.data(), slice_count);
          if (!pad)
            return SetInvalid("Sparse menu input lacks its observed full PadSlot slice"), void();
        }
        if (++sd_menu_consumed > 7200)
          return SetInvalid("SD menu consumed input cap exhausted"), void();
        sd_menu_neutral = true;
        for (u32 port = 0; port < 4; ++port)
          for (u32 byte = 0; byte < 11; ++byte)
          {
            const bool inactive = SparsePairRequested() ? (port == 1 || port == 3) : port >= 2;
            sd_menu_neutral &= pad[port * 12 + byte] == (inactive && byte == 10 ? 0xff : 0);
          }
        SdEvent("menu_input", pc, tick);
        return;
      }
      if (SparsePairRequested())
      {
        const u8* pad = SparsePadSlotBytes(raw.data(), raw_size, slices.data(), slice_count);
        if (!pad)
          return SparseInputFailure("Sparse source input lacks its observed full PadSlot slice", pc, tick), void();
        if (sd_init.phase == SdInitState::Phase::VsSetup)
        {
          if (!SparsePadStatusMatches(pad, false))
            return SparseInputFailure("Sparse original VS setup consumed non-neutral PAD0/PAD2 or active PAD1/PAD3", pc, tick), void();
          SdEvent("input", pc, tick);
          return;
        }
        if (!sparse_active || !sparse_setup_seen ||
            sparse_source_samples >= SPARSE_SOURCE_SAMPLE_CAP ||
            !SparsePadErrorsValid(pad))
          return SparseInputFailure("Sparse original consumed PAD lacks its declared four-port source identity", pc, tick), void();

        ++sparse_source_samples;
        if (sparse_witness_phase == 0)
        {
          if (SparsePadStatusMatches(pad, false))
          {
            if (++sparse_prepress_neutral_samples > SPARSE_PREPRESS_NEUTRAL_CAP)
              return SparseInputFailure("Sparse source press exceeded its original neutral-sample cap", pc, tick), void();
          }
          else if (SparsePadStatusMatches(pad, true))
          {
            sparse_witness_phase = 1;
          }
          else
          {
            return SparseInputFailure("Sparse original PAD press differs from distinct source0/source2 intent", pc, tick), void();
          }
        }
        else if (sparse_witness_phase == 1)
        {
          // Pipe release cannot retract an already queued identical press.
          // Retain every sample within the existing total cap until neutral.
          if (SparsePadStatusMatches(pad, false))
            sparse_witness_phase = 2;
          else if (!SparsePadStatusMatches(pad, true))
            return SparseInputFailure("Sparse original PAD release differs from held press or neutral", pc, tick), void();
        }
        else
        {
          return SparseInputFailure("Sparse original PAD continued after its exact release witness", pc, tick), void();
        }
        SdEvent("input", pc, tick);
        if (sparse_witness_phase == 2)
        {
          InputStream::RequestFinish(true);
          natural_completion.store(false);
          finish_requested.store(true);
        }
        return;
      }
      if (OrdinaryTimeoutRequested())
      {
        bool neutral = true, direction = true;
        for (u32 port = 0; port < 4; ++port)
          for (u32 byte = 0; byte < 11; ++byte)
          {
            const u8 expected = port >= 2 && byte == 10 ? 0xff : 0;
            neutral &= pad[port * 12 + byte] == expected;
            direction &= pad[port * 12 + byte] == (port == 0 && byte == 2 ? 0xb0 : expected);
          }
        if (sd_init.phase == SdInitState::Phase::VsSetup)
        {
          if (!neutral || sd_init.consumed > 123)
            return SetInvalid("Ordinary constructor input/cap differs"), void();
        }
        else if (sd_init.phase != SdInitState::Phase::VsActive || !ordinary_timeout.Input(neutral, direction))
          return SetInvalid("Ordinary actual PAD policy/cap differs"), void();
        SdEvent("input", pc, tick);
        return;
      }
      for (u32 port = 0; port < 4; ++port)
        for (u32 byte = 0; byte < 11; ++byte)
          if (pad[port * 12 + byte] != (port >= 2 && byte == 10 ? 0xff : 0))
            return SetInvalid("SD prefix consumed undeclared controller input"), void();
      SdEvent("input", pc, tick);
      return;
    }
    if (pc == 0x8016e9c4 || pc == 0x8016ec24)
    {
      const bool sudden = pc == 0x8016ec24;
      if (!sudden && sd_init.phase == SdInitState::Phase::Menu) return;
      u32 word = 0;
      if (!ReadU32(system, pc, &word) || word != 0x4e800020 ||
          (SparsePairRequested() ? (!fighter_present[0] || !fighter_present[2] ||
                                    fighter_present[1] || fighter_present[3]) :
                                   (!fighter_present[0] || !fighter_present[1])) ||
          !sd_init.Ready(sudden, Env("MWRC_SD_MENU_PROBE") == "competitive_entry") ||
          !AddSlice(system, SliceTag::MatchSetup, sd_init.setup_pointer, 0x138))
        return SetInvalid("SD prefix setup return lacks its retained entry owner"), void();
      for (u32 index = 0; index < 2; ++index)
      {
        const u32 slot = SparsePairRequested() ? (index == 0 ? 0 : 2) : index;
        if (!AddSlice(system, SliceTag::FighterHead, fighter_pointers[slot], 0x100, slot) ||
            !AddSlice(system, SliceTag::FighterDamageShield, fighter_pointers[slot] + 0x1830, 4, slot) ||
            !AddSlice(system, SliceTag::FighterStocks, 0x80453080 + slot * 0xe90 + 0x8e, 1, slot))
          return SetInvalid("SD prefix initialized fighter snapshot is invalid"), void();
      }
      SdEvent(sudden ? "sd_setup" : "vs_setup", pc, tick);
      if (SparsePairRequested())
      {
        sparse_setup_seen = true;
        sparse_setup_consumed = sd_init.consumed;
      }
      if (OrdinaryTimeoutRequested()) ordinary_timeout.setup_samples = sd_init.consumed;
      if (sudden || Env("MWRC_SD_MENU_PROBE") == "competitive_entry")
      {
        // The native input footer completes THIS declared prefix. The primary
        // observer remains interrupted, never a successful match/scene teardown.
        InputStream::RequestFinish(true);
        natural_completion.store(false);
        finish_requested.store(true);
      }
      return;
    }
    if (pc == 0x8016ebbc && sd_init.phase == SdInitState::Phase::VsActive)
    {
      if (SparsePairRequested())
        return SetInvalid("Sparse setup/input milestone reached a VS terminal before its declared stop"), void();
      if (!BoundaryInstructionMatches(system, pc) || !sd_init.Exit() ||
          !AddSlice(system, SliceTag::Result, 0x80479d98 + 0xc, 0x448) ||
          !AddSlice(system, SliceTag::MatchClock, 0x8046b6a0, 0x2e))
        return SetInvalid("SD prefix normal timeout exit is invalid"), void();
      if (OrdinaryTimeoutRequested())
      {
        std::array<u8, 2> stocks{};
        std::array<u32, 2> positions{};
        const u8* clock = raw.data() + 0x448;
        if (raw[4] != 1 || raw[5] != 1 || raw[6] != 0 || ReadBE32(raw.data()+8) != 28800 ||
            raw[13] != 1 || raw[16] != 1 || !AddOrdinaryLive(system, &stocks, &positions) ||
            !ordinary_timeout.Exit(tick, ReadBE32(clock + 0x24), ReadBE32(clock + 0x28),
                                   (clock[0x2c] << 8) | clock[0x2d], stocks[0], stocks[1]))
          return OrdinaryTerminalFailure("Ordinary canonical timeout/live terminal differs", pc, tick);
        for (u32 slot = 0; slot < 2; ++slot)
        {
          const size_t base = 0x58 + slot * 0xa8;
          // gm_80166378/fn_80165AC0 ranks stock scores3:4 as1:0.
          // MatchPlayerData+5 is is_big_loser, not a setup/team field.
          if (raw[base] != 0 || raw[base+1] != 8 || (raw[base+3] >> 2) != (slot == 0 ? 1 : 0) ||
              raw[base+5] != (slot == 0 ? 1 : 0) || raw[base+8] != (slot == 0 ? 3 : 4) || raw[base+12] || raw[base+13])
            return OrdinaryTerminalFailure("Ordinary MatchEnd participant/stock/damage differs", pc, tick);
        }
        for (u32 slot = 2; slot < 6; ++slot)
          if (raw[0x58 + slot * 0xa8] != 3)
            return OrdinaryTerminalFailure("Ordinary MatchEnd inactive roster differs", pc, tick);
        SdEvent("vs_exit", pc, tick);
        return;
      }
      // Original outcome and participant decision remain outputs, never inputs.
      if (raw[4] != 1 || raw[5] != 1 || raw[6] != 0 || raw[0xd] != 2 ||
          raw[0x58] != 0 || raw[0x100] != 0 ||
          raw[0x60] != 4 || raw[0x108] != 4)
        return SetInvalid("SD prefix normal match did not produce its declared tied timeout"), void();
      SdEvent("vs_exit", pc, tick);
      return;
    }
    if (pc == 0x8039157c && sd_init.phase != SdInitState::Phase::Menu)
    {
      if (!BoundaryInstructionMatches(system, pc) || !sd_init.Retire())
        return SetInvalid("SD prefix arena retirement is out of order"), void();
      fighter_present.fill(false);
      fighter_pointers.fill(0);
      SdEvent("vs_retired", pc, tick);
      if (OrdinaryTimeoutRequested())
      {
        if (tick != ordinary_timeout.ticks || !ordinary_timeout.Retire())
          return SetInvalid("Ordinary retirement precedes terminal publication"), void();
        sd_init.phase = SdInitState::Phase::Complete;
        InputStream::RequestFinish(true);
        natural_completion.store(false);
        finish_requested.store(true);
      }
    }
  }

  enum class SceneResetAction { Ignore, BeginResults, FinishResults, Invalid };

  SceneResetAction ClassifyWholeSceneReset() const
  {
    // gm_801A4014 initializes a fresh GObj arena for EVERY scene, after the
    // mode's on_enter and before the scene's on_enter. Menu transitions do
    // not complete a match; the first reset after VS releases its fighters.
    if (!match_active)
    {
      if (whole_phase == 0 || whole_phase == 2 || whole_phase == 4 || whole_phase == 8)
        return SceneResetAction::Ignore;
      if (whole_phase == 6 && prize_scene_exit_seen && prize_mode_exit_seen)
        return SceneResetAction::Ignore;
      return SceneResetAction::Invalid;
    }
    if (whole_phase != 5 || !result_seen || !vs_exit_seen || !vs_exit_return_seen ||
        !vs_mode_exit_seen)
      return SceneResetAction::Invalid;
    if (!results_enter_seen && !results_gobj_seen && !results_exit_seen &&
        !results_mode_exit_seen)
      return SceneResetAction::BeginResults;
    if (results_enter_seen && results_gobj_seen && results_exit_seen && results_mode_exit_seen)
      return SceneResetAction::FinishResults;
    return SceneResetAction::Invalid;
  }

  void Observe(Core::System* system, u32 pc, PowerPC::PowerPCState* state)
  {
    if (!Start() || invalid.load() || finish_requested.load())
      return;
    if (SdInitRequested())
    {
      ObserveSdInit(system, pc, state);
      return;
    }
    if (transform_prefix_enabled && pc == CSS_ENTER)
    {
      if (transform_prefix_css_enter_seen || transform_prefix_vs_entry_seen)
        return SetInvalid("Sheik transform prefix observed a duplicate or late CSS entry"), void();
      transform_prefix_css_enter_seen = true;
      transform_prefix_css_ready = false;
      transform_prefix_css_live_owner_seen = false;
      transform_prefix_sss_enter_seen = false;
      transform_prefix_sss_ready = false;
      transform_prefix_sss_live_owner_seen = false;
      return;
    }
    if (transform_prefix_enabled && pc == CSS_ENTER_RETURN)
    {
      u32 word = 0;
      if (!transform_prefix_css_enter_seen || transform_prefix_css_ready ||
          !ReadU32(system, pc, &word) || word != 0x4e800020)
        return SetInvalid("Sheik transform prefix CSS readiness lacks its verified source return"),
               void();
      transform_prefix_css_ready = true;
      return;
    }
    if (transform_prefix_enabled && pc == SSS_ENTER)
    {
      u32 word = 0;
      if (!transform_prefix_css_live_owner_seen ||
          transform_prefix_sss_enter_seen || transform_prefix_vs_entry_seen ||
          !ReadU32(system, pc, &word) || word != 0x7c0802a6)
        return SetInvalid("Sheik transform prefix SSS entry preceded its live CSS owner"), void();
      transform_prefix_sss_enter_seen = true;
      transform_prefix_sss_ready = false;
      transform_prefix_sss_live_owner_seen = false;
      return;
    }
    if (transform_prefix_enabled && pc == SSS_ENTER_RETURN)
    {
      u32 word = 0;
      if (!transform_prefix_sss_enter_seen || transform_prefix_sss_ready ||
          !transform_prefix_css_live_owner_seen || !ReadU32(system, pc, &word) ||
          word != 0x4e800020)
        return SetInvalid("Sheik transform prefix SSS readiness lacks its ordered source return"),
               void();
      transform_prefix_sss_ready = true;
      return;
    }
    if (pc == cpu_probe_rng_return_pc && cpu_probe_rng_return_pc != 0)
      RecordSelectedRngCallback(system, pc);
    if (const ItemProbePoint* item_probe = FindItemProbePoint(pc))
    {
      u32 source_tick = 0;
      if (!ReadU32(system, 0x80479d58, &source_tick))
      {
        SetInvalid("item probe source counter is outside the pinned RAM range");
        return;
      }
      RecordItemProbe(system, pc, *item_probe, state, source_tick);
      // Tick/draw returns are ordinary capture boundaries too.  Every other
      // item diagnostic PC exists only to feed the compact companion trace.
      if (pc != 0x80390eb4 && pc != 0x80391040)
        return;
    }
    if (whole_session_enabled() && pc == CSS_ENTER_RETURN)
    {
      u32 word = 0;
      if (!ReadU32(system, pc, &word) || word != 0x4e800020 || whole_phase != 1)
        return SetInvalid("CSS steering readiness lacks its verified source return"), void();
      css_steering_ready = true;
      return;
    }
    if (whole_session_enabled() && pc == MENU_AUDIO_STREAM_START)
    {
      ++audio_owner_epoch;
      return;
    }
    if (const CpuProbePoint* probe = FindCpuProbePoint(pc))
    {
      u32 source_tick = 0;
      if (!ReadU32(system, 0x80479d58, &source_tick))
      {
        SetInvalid("CPU probe source counter is outside the pinned RAM range");
        return;
      }
      RecordCpuProbe(system, pc, *probe, state, source_tick);
      return;
    }
    Boundary boundary;
    if (!BoundaryForPC(pc, whole_session_enabled(), &boundary))
      return;
    if (whole_session_enabled() && boundary == Boundary::CssEnter && whole_phase == 6)
      boundary = Boundary::ReturnCss;
    else if (whole_session_enabled() && boundary == Boundary::CssEnter && whole_phase == 8)
      boundary = Boundary::CssCancelEnter;
    if (!BoundaryInstructionMatches(system, pc))
    {
      SetInvalid("observer boundary instruction is not resident in the pinned DOL");
      return;
    }
    u32 source_tick = 0;
    if (!ReadU32(system, 0x80479d58, &source_tick))
    {
      SetInvalid("source scene counter is outside the pinned RAM range");
      return;
    }
    bool transform_prefix_complete_after_publish = false;
    bool transform_prefix_cap_after_publish = false;
    bool transform_prefix_neutral_pad_after_publish = false;
    bool transform_prefix_grounded_tick_after_publish = false;
    bool transform_prefix_down_b_after_publish = false;
    if (boundary == Boundary::SourceTick)
      CloseCpuProbeAtSourceTick(pc, source_tick);
    raw_size = 0;
    slice_count = 0;
    if (boundary == Boundary::CssEnter || boundary == Boundary::CssCancelEnter ||
        boundary == Boundary::CssExit ||
        boundary == Boundary::SssEnter || boundary == Boundary::SssExit ||
        boundary == Boundary::ReturnCss)
    {
      if (boundary == Boundary::CssEnter || boundary == Boundary::CssCancelEnter ||
          boundary == Boundary::ReturnCss || boundary == Boundary::CssExit)
        css_steering_ready = false;
      if (boundary == Boundary::ReturnCss)
      {
        if (whole_phase != 6 || (!pending_next_match && !awaiting_final_css))
          return SetInvalid("whole-session return CSS was missing or out of order"), void();
        whole_phase = awaiting_final_css ? 7 : 1;
      }
      else if (boundary == Boundary::CssEnter)
      {
        u8 current_mode = 0;
        if (!ReadBytes(system, 0x80479d30, 1, &current_mode) || current_mode != 0x02)
          return SetInvalid("whole-session first CSS is not ordinary GM_VS"), void();
        if (whole_phase != 0)
        {
          return SetInvalid("whole-session CSS enter was missing or out of order"), void();
        }
        if (startup_prize_pending)
          return SetInvalid("whole-session CSS enter preceded startup Prize mode exit"), void();
        whole_phase = 1;
      }
      else if (boundary == Boundary::CssCancelEnter)
      {
        if (whole_phase != 8)
        {
          return SetInvalid("whole-session canceled CSS enter was missing or out of order"),
                 void();
        }
        whole_phase = 1;
      }
      else if (boundary == Boundary::CssExit)
      {
        if (whole_phase != 1)
          return SetInvalid("whole-session CSS exit was missing or out of order"), void();
        whole_phase = 2;
      }
      else if (boundary == Boundary::SssEnter)
      {
        if (whole_phase != 2)
          return SetInvalid("whole-session SSS enter was missing or out of order"), void();
        whole_phase = 3;
      }
      else
      {
        if (whole_phase != 3)
          return SetInvalid("whole-session SSS exit was missing or out of order"), void();
        u32 sss_pointer = 0;
        u8 route = 0;
        if (!ReadU32(system, 0x804d6c90, &sss_pointer) || !sss_pointer ||
            sss_pointer > UINT32_MAX - 4 || !ReadBytes(system, sss_pointer + 4, 1, &route))
          return SetInvalid("whole-session SSS exit did not expose its source route"), void();
        whole_phase = route ? 4 : 8;
      }
      if (!AddMenuSlices(system, boundary, state->gpr[3]))
        return SetInvalid("menu boundary did not expose its pinned state/audio slices"), void();
    }
    else if (boundary == Boundary::PadPoll)
    {
      u32 caller = 0;
      if (state->gpr[1] > UINT32_MAX - 0x54 ||
          !ReadU32(system, state->gpr[1] + 0x54, &caller) || caller != PAD_READ_HSD_CALLER)
        return;
      if (state->gpr[31] < 0x30 ||
          !AddSlice(system, SliceTag::PadStatusAll4, state->gpr[31] - 0x30, 0x30) ||
          !AddSlice(system, SliceTag::PadPollCaller, state->gpr[1] + 0x54, 4) ||
          !AddSlice(system, SliceTag::PadQueue, 0x804c1f78, 0xc) ||
          !AddSlice(system, SliceTag::PadSnapshot, 0x804c1f84, 0x358) ||
          !AddSlice(system, SliceTag::RetraceCount, 0x804d7420, 4) ||
          !AddSlice(system, SliceTag::SourceVICount, 0x804a7f98, 4) ||
          !AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6) ||
          !AddSceneKindSlice(system) ||
          !AddMenuSteeringSlices(system))
        return SetInvalid("PAD poll did not expose its bounded four-port slices"), void();
    }
    else if (boundary == Boundary::PadConsume)
    {
      if (!AddSlice(system, SliceTag::PadQueue, 0x804c1f78, 0xc) ||
          !AddSlice(system, SliceTag::PadSlot, state->gpr[25], 0x30))
        return SetInvalid("PAD consume did not expose its bounded queue/slot slices"), void();
      std::array<u8, 0xc> queue{};
      const u8 qread = static_cast<u8>(state->gpr[6]);
      if (!ReadBytes(system, 0x804c1f78, queue.size(), queue.data()) || !queue[0] ||
          qread >= queue[0] || ReadBE32(queue.data() + 8) + qread * 0x30 != state->gpr[25])
        return SetInvalid("PAD consume registers escaped the pinned queue"), void();
      if (transform_prefix_enabled && setup_ready)
      {
        std::array<u8, 0x30> consumed{};
        if (!ReadBytes(system, state->gpr[25], consumed.size(), consumed.data()))
          return SetInvalid("Sheik transform PAD consumption escaped its checked queue slot"), void();
        const bool p1_down_b = ReadBE16(consumed.data()) == 0x0200 && consumed[2] == 0 &&
                               static_cast<s8>(consumed[3]) < 0;
        if (!TransformPrefixPadStatusMatches(consumed.data(), p1_down_b))
          return SetInvalid("Sheik transform requires exact four-port source PAD statuses"), void();
        if (p1_down_b && !transform_prefix_previous_down_b)
        {
          if (transform_prefix_down_b_seen)
            return SetInvalid("Sheik transform input contains more than one down+B episode"), void();
          if (!transform_prefix_grounded_neutral_seen ||
              !transform_prefix_neutral_pad_seen ||
              !TransformPrefixReadinessSequencesValid(
                  transform_prefix_neutral_pad_sequence,
                  transform_prefix_grounded_neutral_source_sequence, next_sequence))
            return SetInvalid("Sheik transform down+B preceded a consumed neutral PAD record and later grounded neutral Zelda tick"), void();
          transform_prefix_down_b_seen = true;
          transform_prefix_down_b_after_publish = true;
        }
        else if (!p1_down_b && !transform_prefix_previous_down_b &&
                 !transform_prefix_neutral_pad_seen &&
                 !transform_prefix_grounded_neutral_seen && !transform_prefix_down_b_seen)
          transform_prefix_neutral_pad_after_publish = true;
        if (!p1_down_b && transform_prefix_previous_down_b)
          transform_prefix_release_seen = true;
        transform_prefix_previous_down_b = p1_down_b;
      }
    }
    else if (boundary == Boundary::Entry || boundary == Boundary::Setup)
    {
      if (boundary == Boundary::Entry)
      {
        u8 current_mode = 0;
        if ((whole_session_enabled() || transform_prefix_enabled) &&
            !ReadBytes(system, 0x80479d30, 1, &current_mode))
          return SetInvalid("VS entry did not expose source mode routing"), void();
        // Opening movie attract demos reuse the VS constructor and can run
        // while the outer routing record still names GM_OPENING_MV. They are
        // pre-CSS source coverage, not the supported SSS-to-match route.
        if (whole_session_enabled() && current_mode == 0x18 && whole_phase == 0)
          return;
        // Match the same pre-CSS attract handling for the opt-in prefix, but
        // only before its one accepted original VS entry. Later Entry callbacks
        // are errors and cannot erase the in-progress ownership/input witness.
        if (transform_prefix_enabled)
        {
          const TransformPrefixEntryDisposition disposition =
              ClassifyTransformPrefixEntry(current_mode, transform_prefix_vs_entry_seen);
          if (disposition == TransformPrefixEntryDisposition::IgnoreOpeningAttract)
            return;
          if (disposition == TransformPrefixEntryDisposition::Reject)
          {
            if (current_mode != 0x02)
              return SetInvalid("Sheik transform prefix requires the original SSS-to-VS route"),
                     void();
            return SetInvalid(
                       "Sheik transform prefix encountered a later VS entry before its bounded completion"),
                   void();
          }
        }
        if (whole_session_enabled() && (current_mode != 0x02 || whole_phase != 4))
          return SetInvalid("whole-session VS entry was missing its SSS route"), void();
        if (transform_prefix_enabled &&
            !TransformPrefixMenuOwnersReady(transform_prefix_css_live_owner_seen,
                                            transform_prefix_sss_live_owner_seen))
          return SetInvalid("Sheik transform prefix VS entry preceded ordered live CSS/SSS owners"),
                 void();
        if (transform_prefix_enabled)
          transform_prefix_vs_entry_seen = true;
        setup_pointer = state->gpr[3];
        if (!setup_pointer || !AddSlice(system, SliceTag::MatchSetup, setup_pointer, 0x138))
          return SetInvalid("VS entry did not expose its source setup"), void();
        u32 rng_pointer = 0;
        if (!AddSlice(system, SliceTag::RngPointer, 0x804d5f94, 4) ||
            !ReadU32(system, 0x804d5f94, &rng_pointer) || !rng_pointer ||
            !AddSlice(system, SliceTag::RngValue, rng_pointer, 4) ||
            !AddSlice(system, SliceTag::PadSnapshot, 0x804c1f84, 0x358))
          return SetInvalid("VS entry did not expose its bounded initial state"), void();
        if (!AddProfileSlices(system))
          return SetInvalid("VS entry did not expose its loaded profile masks"), void();
        setup_ready = false;
        transform_prefix_active_ticks = 0;
        transform_prefix_down_b_seen = false;
        transform_prefix_previous_down_b = false;
        transform_prefix_release_seen = false;
        transform_prefix_action_seen = false;
        transform_prefix_swap_seen = false;
        transform_prefix_neutral_pad_seen = false;
        transform_prefix_neutral_pad_sequence = 0;
        transform_prefix_grounded_neutral_seen = false;
        transform_prefix_grounded_neutral_source_sequence = 0;
        transform_prefix_down_b_source_sequence = 0;
        transform_prefix_sheik_neutral_seen = false;
        transform_prefix_first_source_tick_seen = false;
        transform_prefix_last_source_tick = 0;
        result_seen = false;
        result_pointer = 0;
        vs_exit_seen = false;
        vs_exit_return_seen = false;
        vs_mode_exit_seen = false;
        results_enter_seen = false;
        results_gobj_seen = false;
        results_exit_seen = false;
        results_mode_exit_seen = false;
        completed_match_pending_prize = false;
        startup_prize_pending = false;
        prize_mode_enter_seen = false;
        prize_scene_enter_seen = false;
        prize_scene_exit_seen = false;
        prize_mode_exit_seen = false;
        fighter_present.fill(false);
        fighter_pointers.fill(0);
        for (auto& entities : fighter_entity_pointers)
          entities.fill(0);
        for (auto& kinds : fighter_entity_kinds)
          kinds.fill(0);
        fighter_entity_count.fill(0);
        cpu_slots.fill(false);
        draw_ordinal = 0;
        const auto* setup = system->GetMemory().GetPointerForRange(setup_pointer, 0x138);
        if (!setup)
          return SetInvalid("source setup pointer is invalid"), void();
        // The same entry routine is also used by title-screen attract demos.
        // Their setup can carry the ordinary VS bit, so the source mode is
        // part of the guard for both the whole-session and Sheik-prefix probes.
        match_active = (setup[4] & 0x40) != 0 &&
                      (!(whole_session_enabled() || transform_prefix_enabled) ||
                       current_mode == 0x02);
        if (transform_prefix_enabled && !match_active)
          return SetInvalid("Sheik transform prefix requires an original VS setup"), void();
        active_slot_count = 0;
        if (match_active)
        {
          while (active_slot_count < 6 &&
                 (setup[0x61 + active_slot_count * 0x24] == 0 ||
                  setup[0x61 + active_slot_count * 0x24] == 1))
            ++active_slot_count;
          if (active_slot_count < 2 || active_slot_count > 4 ||
              (active_slot_count < 6 && setup[0x61 + active_slot_count * 0x24] != 3))
            return SetInvalid("VS setup has a non-contiguous active port layout"), void();
          for (u32 slot = 0; slot < 4; ++slot)
          {
            const u8 type = setup[0x61 + slot * 0x24];
            cpu_slots[slot] = slot < active_slot_count && type == 1;
            if (slot >= active_slot_count && type != 3)
              return SetInvalid("VS setup has an unexpected trailing port"), void();
          }
        }
        if (whole_session_enabled())
          whole_phase = 5;
      }
      else
      {
        if (!match_active || !setup_pointer ||
            !AddSlice(system, SliceTag::MatchSetup, setup_pointer, 0x138))
          return;
        if (!std::all_of(fighter_present.begin(), fighter_present.begin() + active_slot_count,
                         [](bool present) { return present; }))
          return SetInvalid("match setup completed before all bounded fighter slices were ready"),
                 void();
        if (transform_prefix_enabled)
        {
          u32 active_entity_index = 0, active_kind = 0, active_fighter = 0;
          if (!TransformPrefixMenuOwnersReady(transform_prefix_css_live_owner_seen,
                                              transform_prefix_sss_live_owner_seen) ||
              !TransformPrefixRosterValid(system) ||
              !AddTransformPrefixOwnerSlices(system, &active_entity_index, &active_kind,
                                             &active_fighter) ||
              active_entity_index != 0 || active_kind != 19)
            return SetInvalid("Sheik transform setup lacks the authored Zelda/Sheik ownership pair"),
                 void();
        }
        else if (!AddMatchSlices(system))
        {
          return SetInvalid("match setup completed before all bounded fighter slices were ready"),
                 void();
        }
        setup_ready = true;
      }
    }
    else if (boundary == Boundary::FighterCreate)
    {
      if (!match_active)
        return;
      const u32 gobj = state->gpr[3];
      u32 fighter = 0;
      u32 kind = 0;
      u8 slot = 0xff;
      u32 entity_index = 0;
      if (!IsMem1Range(gobj, 0x30) || !ReadU32(system, gobj + 0x2c, &fighter) || !fighter ||
          !ReadFighterSourceSlot(system, fighter, &slot) ||
          !ReadU32(system, fighter + 0x4, &kind))
        return SetInvalid("fighter creation returned an unreadable GObj/Fighter identity"), void();
      if (slot >= active_slot_count)
        return SetInvalid("fighter creation exposed an out-of-range source slot"), void();
      if (!RegisterFighterEntity(slot, fighter, kind, &entity_index))
        return SetInvalid("fighter creation repeated or exceeded source-slot entity identity"),
               void();
      const u16 flags = FighterEntitySliceFlags(slot, entity_index);
      if (!AddSlice(system, SliceTag::FighterCreateContext, gobj, 0x30, flags) ||
          !AddSlice(system, SliceTag::FighterHead, fighter, 0x100, flags))
        return SetInvalid("fighter creation slices escaped the pinned ranges"), void();
    }
    else if (boundary == Boundary::SourceTick || boundary == Boundary::DrawEnter ||
             boundary == Boundary::DrawReturn)
    {
      if (!match_active || !setup_ready ||
          !std::all_of(fighter_present.begin(), fighter_present.begin() + active_slot_count,
                       [](bool present) { return present; }))
        return;
      if (transform_prefix_enabled)
      {
        if (boundary != Boundary::SourceTick)
          return;
        if (!TransformPrefixSourceTickIsNext(transform_prefix_first_source_tick_seen,
                                             transform_prefix_last_source_tick, source_tick))
          return SetInvalid(
                     "Sheik transform source-tick stream is missing its initial tick or contains a gap"),
                 void();
        transform_prefix_first_source_tick_seen = true;
        transform_prefix_last_source_tick = source_tick;
        u32 active_entity_index = 0, active_kind = 0, active_fighter = 0, motion = 0, ground_air = 0;
        if (!AddTransformPrefixOwnerSlices(system, &active_entity_index, &active_kind,
                                           &active_fighter) ||
            !ReadU32(system, active_fighter + 0x10, &motion) ||
            !ReadU32(system, active_fighter + 0xe0, &ground_air))
          return SetInvalid("Sheik transform source tick lacks checked entity ownership"), void();
        ++transform_prefix_active_ticks;
        if (active_kind == 19)
        {
          if (active_entity_index != 0 || transform_prefix_swap_seen)
            return SetInvalid("Zelda regained active ownership after the Sheik transform transition"), void();
          if (!transform_prefix_down_b_seen && !transform_prefix_previous_down_b &&
              transform_prefix_neutral_pad_seen &&
              transform_prefix_neutral_pad_sequence < next_sequence && motion == 14 &&
              ground_air == 0 && !transform_prefix_grounded_neutral_seen)
            transform_prefix_grounded_tick_after_publish = true;
          if (motion == 355)
          {
            if (!transform_prefix_down_b_seen)
              return SetInvalid("Zelda down-B motion began without consumed P1 down+B"), void();
            transform_prefix_action_seen = true;
          }
        }
        else if (active_kind == 7)
        {
          if (!transform_prefix_action_seen || !transform_prefix_release_seen || active_entity_index != 1)
            return SetInvalid("Sheik became active without one consumed down+B, neutral release, and Zelda action"), void();
          if (!transform_prefix_swap_seen)
          {
            transform_prefix_swap_seen = true;
          }
          else if (motion == 14 && ground_air == 0)
          {
            transform_prefix_sheik_neutral_seen = true;
            transform_prefix_complete_after_publish = true;
          }
        }
        else
        {
          return SetInvalid("Sheik transform active owner has an unexpected fighter kind"), void();
        }
        if (!transform_prefix_sheik_neutral_seen && transform_prefix_active_ticks >= 600)
          transform_prefix_cap_after_publish = true;
      }
      else if (!AddMatchSlices(system))
      {
        return SetInvalid("match semantic slice escaped the pinned ranges"), void();
      }
      if (checked_entity_profile)
      {
        u8 qnum = 0;
        if (!ReadBytes(system, 0x804c1f78, 1, &qnum))
          return SetInvalid("entity prefix queue capacity is unreadable"), void();
        const int admission = entity_prefix_progress.Observe(boundary, source_tick, qnum);
        if (admission < 0)
          return SetInvalid("entity prefix source/draw batch boundary differs"), void();
        entity_prefix_final_draw = admission == 1;
      }
    }
    else if (boundary == Boundary::ResultEnter || boundary == Boundary::ResultReturn)
    {
      if (!match_active)
        return;
      if (transform_prefix_enabled && !transform_prefix_sheik_neutral_seen)
        return SetInvalid("original match ended before the completed active Sheik neutral prefix"), void();
      if (boundary == Boundary::ResultEnter)
        result_pointer = state->gpr[3];
      if (!result_pointer || result_pointer != 0x80479d98 ||
          !AddSlice(system, SliceTag::Result, result_pointer + 0xc, 0x28))
        return SetInvalid("VS result pointer differs from the pinned source context"), void();
      if (boundary == Boundary::ResultReturn)
        result_seen = true;
    }
    else if (boundary == Boundary::VsExit || boundary == Boundary::VsExitReturn ||
             boundary == Boundary::VsModeExit || boundary == Boundary::ResultsEnter ||
             boundary == Boundary::ResultsExit || boundary == Boundary::ResultsModeExit ||
             boundary == Boundary::ResultsGObjProcess || boundary == Boundary::PrizeModeEnter ||
             boundary == Boundary::PrizeSceneEnter || boundary == Boundary::PrizeSceneExit ||
             boundary == Boundary::PrizeModeExit || boundary == Boundary::StartupPrizeModeExit)
    {
      if (boundary == Boundary::PrizeModeEnter || boundary == Boundary::PrizeSceneEnter ||
          boundary == Boundary::PrizeSceneExit || boundary == Boundary::PrizeModeExit ||
          boundary == Boundary::StartupPrizeModeExit)
      {
        // The mode's Prize on_enter precedes the arena reset into its scene.
        // Arm that handoff from the already ordered Results exit hooks; the
        // following reset still publishes the final Results teardown.
        const bool entering_prize_from_results = boundary == Boundary::PrizeModeEnter &&
            match_active && whole_phase == 5 && results_gobj_seen && results_exit_seen &&
            results_mode_exit_seen;
        if (entering_prize_from_results)
          completed_match_pending_prize = true;
        if (!whole_session_enabled() || (match_active && !entering_prize_from_results) ||
            (!completed_match_pending_prize && !startup_prize_pending && whole_phase != 0))
          return SetInvalid("whole-session Prize hook occurred outside a completed match"), void();
        if (boundary == Boundary::PrizeModeEnter)
        {
          if (prize_mode_enter_seen || prize_scene_enter_seen || prize_scene_exit_seen ||
              (completed_match_pending_prize && startup_prize_pending) ||
              !AddSessionSlices(system))
            return SetInvalid("Prize mode enter is missing its completed Results handoff"), void();
          if (!completed_match_pending_prize)
            startup_prize_pending = true;
          prize_mode_enter_seen = true;
        }
        else if (boundary == Boundary::PrizeSceneEnter)
        {
          if (!prize_mode_enter_seen || prize_scene_enter_seen || !AddSessionSlices(system))
            return SetInvalid("Prize scene enter is missing its ordered mode hook"), void();
          prize_scene_enter_seen = true;
        }
        else if (boundary == Boundary::PrizeSceneExit)
        {
          if (!prize_scene_enter_seen || prize_scene_exit_seen || !AddSessionSlices(system))
            return SetInvalid("Prize scene exit is missing its ordered scene hook"), void();
          prize_scene_exit_seen = true;
        }
        else if (boundary == Boundary::StartupPrizeModeExit)
        {
          if (!startup_prize_pending || !prize_scene_exit_seen || prize_mode_exit_seen ||
              !AddSessionSlices(system))
            return SetInvalid("startup Prize mode exit is missing its ordered scene hook"), void();
          prize_mode_exit_seen = true;
          startup_prize_pending = false;
        }
        else
        {
          if (startup_prize_pending || !completed_match_pending_prize ||
              !prize_scene_exit_seen || prize_mode_exit_seen || !AddSessionSlices(system))
            return SetInvalid("Prize mode exit is missing its ordered scene hook"), void();
          prize_mode_exit_seen = true;
          completed_match_pending_prize = false;
          startup_prize_pending = false;
        }
      }
      else
      {
        u8 current_mode = 0;
        if (whole_session_enabled() && !match_active && whole_phase == 0 &&
            ReadBytes(system, 0x80479d30, 1, &current_mode) && current_mode == 0x18)
          return;
        // Only VS exit still owns playable fighter state. Its return retires
        // those pointers; the ordered mode/Results hooks remain part of this
        // match until Results teardown and must not require them to be live.
        if (!whole_session_enabled() || !match_active ||
            (boundary == Boundary::VsExit && !setup_ready))
          return SetInvalid("whole-session source hook occurred outside an active VS match"), void();
        if (boundary == Boundary::VsExit)
        {
          if (vs_exit_seen)
            return SetInvalid("duplicate gm_Scene_Vs_OnExit hook"), void();
          result_pointer = 0x80479d98;
          if (!AddSlice(system, SliceTag::Result, result_pointer + 0xc, 0x28) ||
              !AddSessionSlices(system))
            return SetInvalid("VS exit did not expose its pinned result/session slices"), void();
          vs_exit_seen = true;
        }
        else if (boundary == Boundary::VsExitReturn)
        {
          if (!vs_exit_seen || vs_exit_return_seen)
            return SetInvalid("VS exit return is missing or duplicated"), void();
          // gm_Scene_Vs_OnExit populates EndMeleeData during the callback.
          // Keep its entry snapshot diagnostic, and publish the completed
          // result only at the verified return instruction.
          if (!AddSessionSlices(system) ||
              !AddSlice(system, SliceTag::Result, result_pointer + 0xc, 0x28))
            return SetInvalid("VS exit return did not expose result/PAD/RNG state"), void();
          vs_exit_return_seen = true;
          result_seen = true;
          // Gameplay observations end at the original VS exit. Results has
          // its own source hooks and must never reuse these fighter pointers.
          setup_ready = false;
        }
        else if (boundary == Boundary::VsModeExit)
        {
          if (!vs_exit_return_seen || vs_mode_exit_seen || !AddSessionSlices(system))
            return SetInvalid("VS mode exit is missing its ordered source hook"), void();
          vs_mode_exit_seen = true;
        }
        else if (boundary == Boundary::ResultsEnter)
        {
          if (!vs_mode_exit_seen || results_enter_seen || !AddSessionSlices(system))
            return SetInvalid("Results enter is missing its ordered source hook"), void();
          results_enter_seen = true;
        }
        else if (boundary == Boundary::ResultsExit)
        {
          if (!results_enter_seen || !results_gobj_seen || results_exit_seen ||
              !AddSessionSlices(system))
            return SetInvalid("Results exit is missing its ordered source hook"), void();
          results_exit_seen = true;
        }
        else if (boundary == Boundary::ResultsModeExit)
        {
          if (!results_exit_seen || !results_gobj_seen || results_mode_exit_seen ||
              !AddSessionSlices(system))
            return SetInvalid("Results mode exit is missing its ordered source hook"), void();
          results_mode_exit_seen = true;
        }
        else
        {
          if (!results_enter_seen || results_exit_seen || !AddSessionSlices(system) ||
              !AddSlice(system, SliceTag::Result, 0x80479d98 + 0xc, 0x28))
            return SetInvalid("Results GObj process did not expose input/RNG/result state"), void();
          results_gobj_seen = true;
        }
      }
    }
    else if (boundary == Boundary::SceneTeardown)
    {
      if (whole_session_enabled())
      {
        const SceneResetAction reset = ClassifyWholeSceneReset();
        if (reset == SceneResetAction::Ignore)
          return;
        if (reset == SceneResetAction::BeginResults)
        {
          setup_ready = false;
          fighter_present.fill(false);
          fighter_pointers.fill(0);
          for (auto& entities : fighter_entity_pointers)
            entities.fill(0);
          for (auto& kinds : fighter_entity_kinds)
            kinds.fill(0);
          fighter_entity_count.fill(0);
          return;
        }
        if (reset == SceneResetAction::Invalid)
          return SetInvalid("whole-session scene reset is missing an ordered VS/Results hook"),
                 void();
      }
      else if (TransformPrefixTeardownArmed(transform_prefix_enabled, match_active) &&
               !transform_prefix_sheik_neutral_seen)
        return SetInvalid("original match ended before the completed active Sheik neutral prefix"), void();
      else if (!match_active || !result_seen)
        return;
      const u8* count_ptr = system->GetMemory().GetPointerForRange(0x804ce380, 1);
      u32 list_heads = 0;
      if (!count_ptr || !ReadU32(system, 0x804d782c, &list_heads) || !list_heads ||
          count_ptr[0] >= 64 ||
          !AddSlice(system, SliceTag::SceneEntityCount, 0x804ce380, 1) ||
          !AddSlice(system, SliceTag::SceneEntityHeadsPointer, 0x804d782c, 4) ||
          !AddSlice(system, SliceTag::SceneEntityHeads, list_heads,
                    (static_cast<size_t>(count_ptr[0]) + 1) * 4))
        return SetInvalid("scene teardown entity lists are invalid"), void();
      if (!AddSlice(system, SliceTag::SceneRouting, 0x80479d30, 6))
        return SetInvalid("scene teardown routing bytes are invalid"), void();
      if (whole_session_enabled() && !AddSessionSlices(system))
        return SetInvalid("whole-session scene reset did not expose PAD/RNG state"), void();
      if (whole_session_enabled())
        whole_phase = 6;
      if (whole_session_enabled())
        completed_match_pending_prize = true;
    }
    else if (boundary == Boundary::SceneExit)
    {
      if (!match_active)
        return;
      if (!AddSlice(system, SliceTag::MatchClock, 0x8046b6a0, 0x30))
        return SetInvalid("scene exit match state is invalid"), void();
      if (!AddSlice(system, SliceTag::SceneRequest, 0x80479d64, 4))
        return SetInvalid("scene exit request is invalid"), void();
    }

    Slot* slot = Reserve(Event::Boundary, pc, source_tick, draw_ordinal);
    if (!slot)
      return;
    u8* out = slot->payload.data();
    PutU16(out, static_cast<u16>(boundary));
    PutU16(out, whole_session_enabled() ? WHOLE_SESSION_FLAG : 0);
    PutU32(out, state->spr[8]);
    PutU32(out, 32);
    PutU32(out, static_cast<u32>(slice_count));
    PutU32(out, 0);
    for (u32 value : state->gpr)
      PutU32(out, value);
    const size_t descriptor_size = 16 * slice_count;
    u8* descriptor = out;
    out += descriptor_size;
    u32 raw_offset = static_cast<u32>(out - slot->payload.data());
    for (size_t index = 0; index < slice_count; ++index)
    {
      const SliceRef& slice = slices[index];
      PutU16(descriptor, static_cast<u16>(slice.tag));
      PutU16(descriptor, slice.flags);
      PutU32(descriptor, slice.address);
      PutU32(descriptor, slice.size);
      PutU32(descriptor, raw_offset);
      raw_offset += slice.size;
    }
    const size_t metadata_size = whole_session_enabled() ? 12 : 0;
    if (static_cast<size_t>(out - slot->payload.data()) + raw_size + metadata_size >
        slot->payload.size())
      return SetInvalid("observer boundary payload exceeds its bounded slot"), void();
    std::memcpy(out, raw.data(), raw_size);
    out += raw_size;
    if (whole_session_enabled())
    {
      PutU16(out, static_cast<u16>(match_index));
      PutU16(out, static_cast<u16>(boundary));
      PutU32(out, audio_owner_epoch);
      PutU32(out, 0);
    }
    slot->payload_size = static_cast<u32>(out - slot->payload.data());
    slot->checksum = CRC32(slot->payload.data(), slot->payload_size);
    if (transform_prefix_neutral_pad_after_publish)
    {
      transform_prefix_neutral_pad_seen = true;
      transform_prefix_neutral_pad_sequence = slot->sequence;
    }
    if (transform_prefix_grounded_tick_after_publish)
    {
      transform_prefix_grounded_neutral_seen = true;
      transform_prefix_grounded_neutral_source_sequence = slot->sequence;
    }
    if (transform_prefix_down_b_after_publish)
      transform_prefix_down_b_source_sequence = slot->sequence;
    Publish(slot);
    if (transform_prefix_complete_after_publish)
      RequestComplete();
    else if (transform_prefix_cap_after_publish)
      SetInvalid(
          "active Sheik grounded-neutral source tick did not follow the owner change within 600 source ticks");
    if (boundary == Boundary::DrawReturn)
      ++draw_ordinal;
    if (checked_entity_profile && entity_prefix_final_draw)
    {
      // Publish every tick/PAD/draw row first. This only closes the diagnostic
      // stream at a passive hook; it never breaks the guest loop or creates Results.
      RequestComplete();
      return;
    }
    if (boundary == Boundary::SceneTeardown && result_seen)
    {
      if (!whole_session_enabled())
      {
        RequestComplete();
      }
      else if (match_index + 1 >= whole_session_matches)
      {
        // The declared sequence is complete only after the source has
        // re-entered CSS following the final Results teardown.
        awaiting_final_css = true;
        match_active = false;
      }
      else
      {
        // Keep the completed match index on the return-to-CSS boundary.  The
        // next CSS enter advances it, so menu rows and source rows retain the
        // same index at each observed boundary.
        pending_next_match = true;
        match_active = false;
      }
    }
    if (boundary == Boundary::ReturnCss)
    {
      if (awaiting_final_css)
      {
        awaiting_final_css = false;
        RequestComplete();
      }
      else if (pending_next_match)
      {
        ++match_index;
        pending_next_match = false;
      }
    }
  }

  void SetInvalid(std::string reason)
  {
    bool expected = false;
    if (invalid.compare_exchange_strong(expected, true))
    {
      std::lock_guard lock(error_mutex);
      error = std::move(reason);
    }
    finish_requested.store(true);
  }

  static u64 SteadyNowNs()
  {
    return static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
  }

  static void UpdateMaximum(std::atomic<u64>& maximum, u64 value)
  {
    u64 current = maximum.load(std::memory_order_relaxed);
    while (current < value &&
           !maximum.compare_exchange_weak(current, value, std::memory_order_relaxed))
    {
    }
  }

  Slot* Reserve(Event event, u32 pc, u32 source_tick, u32 draw_count)
  {
    if (invalid.load() || finish_requested.load())
      return nullptr;
    const u64 head = head_index.load(std::memory_order_relaxed);
    const u64 tail = tail_index.load(std::memory_order_acquire);
    const u64 occupancy = head - tail;
    const u64 timestamp_ns = SteadyNowNs();
    UpdateMaximum(max_ring_occupancy, occupancy);
    Slot& slot = ring[head % RING_SIZE];
    if (slot.ready.load(std::memory_order_acquire))
    {
      const u64 last_dequeue_ns = writer_last_dequeue_ns.load(std::memory_order_acquire);
      const u64 writer_gap_ns = last_dequeue_ns != 0 && timestamp_ns > last_dequeue_ns
                                    ? timestamp_ns - last_dequeue_ns
                                    : 0;
      const u64 max_writer_gap_ns = max_writer_dequeue_gap_ns.load(std::memory_order_relaxed);
      const u64 max_frame_write_ns = max_write_frame_ns.load(std::memory_order_relaxed);
      SetInvalid("observer ring overflow; no record was dropped; ring_size=" +
                 std::to_string(RING_SIZE) + "; event=" +
                 std::to_string(static_cast<u16>(event)) + "; pc_dec=" +
                 std::to_string(pc) + "; source_tick=" + std::to_string(source_tick) +
                 "; attempted_timestamp_ns=" + std::to_string(timestamp_ns) +
                 "; head=" + std::to_string(head) + "; tail=" + std::to_string(tail) +
                 "; occupancy=" + std::to_string(occupancy) +
                 "; max_occupancy=" + std::to_string(max_ring_occupancy.load(
                     std::memory_order_relaxed)) +
                 "; writer_gap_at_overflow_ns=" + std::to_string(writer_gap_ns) +
                 "; max_writer_dequeue_gap_ns=" + std::to_string(max_writer_gap_ns) +
                 "; max_frame_write_ns=" + std::to_string(max_frame_write_ns));
      return nullptr;
    }
    slot.event = event;
    slot.sequence = next_sequence++;
    slot.timestamp_ns = timestamp_ns;
    slot.guest_pc = pc;
    slot.source_tick = source_tick;
    slot.draw_ordinal = draw_count;
    slot.payload_size = 0;
    slot.checksum = 0;
    return &slot;
  }

  void Publish(Slot* slot)
  {
    slot->ready.store(true, std::memory_order_release);
    head_index.fetch_add(1, std::memory_order_release);
  }

  void PushJson(Event event, const std::string& json, u32 pc = 0, u32 tick = 0,
                u32 draw = 0)
  {
    if (OrdinaryTimeoutRequested())
    {
      // All JSON records, including handshake/start, are charged. Writer error
      // and End records reserve two extra 4096+44-byte records in the proof.
      if (ordinary_records >= 73498 || json.size() > 65536 ||
          ordinary_bytes + 44 + json.size() > 128ULL * 1024 * 1024 - 8280)
        return SetInvalid("Ordinary observer aggregate record/byte cap"), void();
      ++ordinary_records;
      ordinary_bytes += 44 + json.size();
    }
    Slot* slot = Reserve(event, pc, tick, draw);
    if (!slot)
      return;
    if (json.size() > slot->payload.size())
    {
      SetInvalid("observer JSON event exceeds payload bound");
      return;
    }
    std::memcpy(slot->payload.data(), json.data(), json.size());
    slot->payload_size = static_cast<u32>(json.size());
    slot->checksum = CRC32(slot->payload.data(), slot->payload_size);
    Publish(slot);
  }

  static u32 CRC32(const u8* bytes, size_t size)
  {
    u32 crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i)
    {
      crc ^= bytes[i];
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc ^ 0xffffffffU;
  }

  bool BuildCpuProbeJson(std::string* json) const
  {
    if (!cpu_probe_records || !cpu_probe_published.load(std::memory_order_acquire) ||
        cpu_probe_record_count > CPU_PROBE_MAX_RECORDS)
      return false;
    json->clear();
    json->reserve(std::min(CPU_PROBE_MAX_JSON_BYTES, size_t(1024)));
    const auto append = [&](std::string_view value) { return AppendBounded(json, value); };
    const auto append_number = [&](u64 value) { return append(std::to_string(value)); };
    const auto append_hex32 = [&](u32 value) { return AppendHex(json, value, 8); };
    const auto append_hex64 = [&](u64 value) { return AppendHex(json, value, 16); };
    if (!append(cpu_probe_rng_return_pc == 0 ?
                "{\"schema\":\"melee-web-cpu-register-probe\",\"version\":1," :
                "{\"schema\":\"melee-web-cpu-register-probe\",\"version\":3,") ||
        !append("\"diagnostic_only\":true,\"window_complete\":true,"
                "\"source_revision\":\"GALE01r2\",\"dol_sha256\":\""))
      return false;
    if (!append(EXPECTED_DOL_SHA256) || !append("\",\"capture_id\":\"") ||
        !append(JsonEscape(capture_id)) || !append("\",\"sequence_id\":\"") ||
        !append(JsonEscape(sequence_id)) || !append("\",\"match\":"))
      return false;
    if (!append_number(cpu_probe_match) || !append(",\"first_tick\":") ||
        !append_number(cpu_probe_first_tick) || !append(",\"last_tick\":") ||
        !append_number(cpu_probe_last_tick))
      return false;
    if (cpu_probe_rng_return_pc != 0 &&
        (!append(",\"rng_return_pc\":\"0x") ||
         !append_hex32(cpu_probe_rng_return_pc) || !append("\"")))
      return false;
    if (!append(",\"record_count\":") ||
        !append_number(cpu_probe_record_count) || !append(",\"records\":["))
      return false;
    for (size_t record_index = 0; record_index < cpu_probe_record_count; ++record_index)
    {
      const CpuProbeRecord& record = cpu_probe_records[record_index];
      const CpuProbePoint* point = FindCpuProbePoint(record.pc);
      if (!point)
        return false;
      if (record_index != 0 && !append(","))
        return false;
      if (!append("{\"pc\":\"0x") || !append_hex32(record.pc) ||
          !append("\",\"label\":\"") || !append(JsonEscape(point->label)) ||
          !append("\",\"expected_word\":\"0x") || !append_hex32(record.expected_word) ||
          !append("\",\"source_tick\":") || !append_number(record.source_tick) ||
          !append(",\"match\":") || !append_number(record.match) ||
          !append(",\"lr\":\"0x") || !append_hex32(record.lr) || !append("\",\"gpr\":["))
        return false;
      for (size_t index = 0; index < record.gpr.size(); ++index)
      {
        if (index != 0 && !append(","))
          return false;
        if (!append("\"0x") || !append_hex32(record.gpr[index]) || !append("\""))
          return false;
      }
      if (!append("],\"fpr\":["))
        return false;
      for (size_t index = 0; index < record.fpr.size(); ++index)
      {
        if (index != 0 && !append(","))
          return false;
        if (!append("\"0x") || !append_hex64(record.fpr[index]) || !append("\""))
          return false;
      }
      if (!append("],\"stack_0x100\":\"0x") ||
          !AppendHexBytes(json, record.stack.data(), record.stack.size()) ||
          !append("\",\"random_seed_and_pointer\":\"0x") ||
          !AppendHexBytes(json, record.random_seed_and_pointer.data(),
                          record.random_seed_and_pointer.size()) ||
          !append("\",\"fighters\":["))
        return false;
      bool first_fighter = true;
      for (size_t slot = 0; slot < record.fighter_present.size(); ++slot)
      {
        if (!record.fighter_present[slot])
          continue;
        if (!first_fighter && !append(","))
          return false;
        first_fighter = false;
        const CpuProbeFighterRecord& fighter = record.fighters[slot];
        if (!append("{\"slot\":") || !append_number(slot) ||
            !append(",\"pointer\":\"0x") || !append_hex32(fighter.pointer) ||
            !append("\",\"head_0x100\":\"0x") ||
            !AppendHexBytes(json, fighter.head.data(), fighter.head.size()) ||
            !append("\",\"cpu_0x57c\":\"0x") ||
            !AppendHexBytes(json, fighter.cpu.data(), fighter.cpu.size()) ||
            !append("\",\"flags_0x2218\":\"0x") ||
            !AppendHexBytes(json, fighter.flags.data(), fighter.flags.size()) || !append("\"}"))
          return false;
      }
      if (!append("]"))
        return false;
      if (record.effect_group_present)
      {
        if (!append(",\"samus_effect_group\":{\"bank_base\":\"0x") ||
            !append_hex32(record.effect_bank_base) ||
            !append("\",\"group_address\":\"0x") ||
            !append_hex32(record.effect_group_address) ||
            !append("\",\"palette_offset\":\"0x") ||
            !append_hex32(record.effect_palette_offset) ||
            !append("\",\"palette_address\":\"0x") ||
            !append_hex32(record.effect_palette_address) ||
            !append("\",\"palette_readable\":") ||
            !append(record.effect_palette_readable ? "true" : "false"))
          return false;
        if (!append(",\"literal_palette_address\":\"0x") ||
            !append_hex32(record.effect_palette_offset) ||
            !append("\",\"literal_palette_readable\":") ||
            !append(record.effect_literal_palette_readable ? "true" : "false"))
          return false;
        if (record.effect_literal_palette_readable &&
            (!append(",\"literal_palette_512\":\"0x") ||
             !AppendHexBytes(json, record.effect_literal_palette.data(),
                             record.effect_literal_palette.size()) ||
             !append("\"")))
          return false;
        if (record.effect_palette_readable &&
            (!append(",\"palette_512\":\"0x") ||
             !AppendHexBytes(json, record.effect_palette.data(),
                             record.effect_palette.size()) ||
             !append("\"")))
          return false;
        if (!append("}"))
          return false;
      }
      if (record.samus_effect_loaded_group_present)
      {
        if (!append(",\"samus_effect_loaded_group\":{\"bank\":") ||
            !append_number(record.samus_effect_loaded_bank) ||
            !append(",\"texture_base\":\"0x") ||
            !append_hex32(record.samus_effect_loaded_texture_base) ||
            !append("\",\"group_address\":\"0x") ||
            !append_hex32(record.samus_effect_loaded_group_address) ||
            !append("\",\"image_address\":\"0x") ||
            !append_hex32(record.samus_effect_loaded_image_address) ||
            !append("\",\"palette_address\":\"0x") ||
            !append_hex32(record.samus_effect_loaded_palette_address) || !append("\"}"))
          return false;
      }
      if (record.samus_effect_gx_tlut_call &&
          (!append(",\"samus_effect_gx_tlut\":{\"palette_address\":\"0x") ||
           !append_hex32(record.samus_effect_gx_tlut_address) || !append("\"}")))
        return false;
      if (record.samus_effect_particle_spawn &&
          (!append(",\"samus_effect_particle_spawn\":{\"bank\":") ||
           !append_number(record.samus_effect_particle_bank) ||
           !append(",\"kind\":") ||
           !append_number(record.samus_effect_particle_kind) ||
           !append(",\"texture_group\":") ||
           !append_number(record.samus_effect_particle_group) || !append("}")))
        return false;
      if (!append("}"))
        return false;
    }
    return append("]}\n");
  }

  bool WriteCpuProbe()
  {
    if (!cpu_probe_published.load(std::memory_order_acquire))
    {
      SetInvalid("CPU probe window did not close before capture completion");
      return false;
    }
    if (cpu_probe_record_count == 0)
    {
      SetInvalid("CPU probe window closed without any matching records");
      return false;
    }
    std::string json;
    if (!BuildCpuProbeJson(&json))
    {
      SetInvalid("CPU probe JSON exceeded its bounded output or contained an unknown PC");
      return false;
    }
    File::DirectIOFile output(cpu_probe_output_path, File::AccessMode::Write,
                              File::OpenMode::Create);
    if (!output.IsOpen() || !output.Write(reinterpret_cast<const u8*>(json.data()), json.size()) ||
        !output.Flush() || !output.Close())
    {
      SetInvalid("CPU probe companion file could not be written");
      return false;
    }
    return true;
  }

  bool BuildItemProbeJson(std::string* json) const
  {
    if (!item_probe_records || !item_probe_published.load(std::memory_order_acquire) ||
        item_probe_record_count == 0 || item_probe_record_count > ITEM_PROBE_MAX_RECORDS)
      return false;
    json->clear();
    json->reserve(std::min(ITEM_PROBE_MAX_JSON_BYTES, size_t(1024)));
    const auto append = [&](std::string_view value) { return AppendBounded(json, value); };
    const auto append_number = [&](u64 value) { return append(std::to_string(value)); };
    const auto append_hex32 = [&](u32 value) { return AppendHex(json, value, 8); };
    const auto append_hex64 = [&](u64 value) { return AppendHex(json, value, 16); };
    const auto append_hex_value = [&](u32 value) {
      return append("\"0x") && append_hex32(value) && append("\"");
    };
    if (!append("{\"schema\":\"melee-web-cpu-item-boundary-probe\",\"version\":5,"
                "\"diagnostic_only\":true,\"window_complete\":true,"
                "\"source_revision\":\"GALE01r2\",\"dol_sha256\":\""))
      return false;
    if (!append(EXPECTED_DOL_SHA256) || !append("\",\"capture_id\":\"") ||
        !append(JsonEscape(capture_id)) || !append("\",\"sequence_id\":\"") ||
        !append(JsonEscape(sequence_id)) || !append("\",\"match\":"))
      return false;
    if (!append_number(item_probe_match) || !append(",\"first_tick\":") ||
        !append_number(item_probe_first_tick) || !append(",\"last_tick\":") ||
        !append_number(item_probe_last_tick) || !append(",\"trigger\":\"") ||
        !append(item_probe_trigger_on_arrow_creation ? "young_link_arrow_creation" :
                    (item_probe_trigger_on_arrow_launch ? "young_link_arrow_launch" :
                                                         "fixed_tick_window")) ||
        !append("\",\"capture_ticks\":") ||
        !append_number((item_probe_trigger_on_arrow_creation ||
                        item_probe_trigger_on_arrow_launch) ? item_probe_capture_ticks :
                                                              item_probe_last_tick -
                                                                  item_probe_first_tick + 1) ||
        !append(",\"target\":{\"gobj\":\"0x") ||
        !append_hex32(ITEM_PROBE_ARROW_GOBJ) || !append("\",\"item\":\"0x") ||
        !append_hex32(ITEM_PROBE_ARROW_ITEM) || !append("\",\"kind\":") ||
        !append_number(ITEM_PROBE_ARROW_KIND) || !append(",\"owner_slot\":3},\"record_count\":") ||
        !append_number(item_probe_record_count) || !append(",\"records\":["))
      return false;
    for (size_t record_index = 0; record_index < item_probe_record_count; ++record_index)
    {
      const ItemProbeRecord& record = item_probe_records[record_index];
      const ItemProbePoint* point = FindItemProbePoint(record.pc);
      if (!point || !record.arrow_present || record.arrow_gobj != ITEM_PROBE_ARROW_GOBJ ||
          record.arrow_item != ITEM_PROBE_ARROW_ITEM || record.arrow_kind != ITEM_PROBE_ARROW_KIND ||
          record.arrow_list_order >= 64 ||
          (!item_probe_trigger_on_arrow_creation && !item_probe_trigger_on_arrow_launch &&
           record.arrow_list_order != 1))
        return false;
      if (record_index != 0 && !append(","))
        return false;
      if (!append("{\"ordinal\":") || !append_number(record.ordinal) ||
          !append(",\"pc\":\"0x") || !append_hex32(record.pc) ||
          !append("\",\"label\":\"") || !append(point->label) ||
          !append("\",\"expected_word\":\"0x") || !append_hex32(record.expected_word) ||
          !append("\",\"source_tick\":") || !append_number(record.source_tick) ||
          !append(",\"match\":") || !append_number(record.match) ||
          !append(",\"lr\":\"0x") || !append_hex32(record.lr) ||
          !append("\",\"gpr3\":\"0x") || !append_hex32(record.gpr3) ||
          !append("\",\"gpr4\":\"0x") || !append_hex32(record.gpr4) ||
          !append("\",\"gpr5\":\"0x") || !append_hex32(record.gpr5) ||
          !append("\",\"gpr6\":\"0x") || !append_hex32(record.gpr6) ||
          !append("\",\"scheduler_priority\":") ||
          !append_number(record.scheduler_priority) || !append(",\"arrow_p_link\":") ||
          !append_number(record.arrow_p_link) || !append(",\"arrow_process_priorities\":["))
        return false;
      for (u32 process_index = 0; process_index < record.arrow_process_count; ++process_index)
      {
        if ((process_index != 0 && !append(",")) ||
            !append_number(record.arrow_process_priorities[process_index]))
          return false;
      }
      if (!append("],\"fighter_slot0_pointer\":\"0x") ||
          !append_hex32(record.fighter_slot0_pointer) ||
          !append("\",\"fighter_slot0_gobj\":\"0x") ||
          !append_hex32(record.fighter_slot0_gobj) ||
          !append("\",\"fighter_slot2_pointer\":\"0x") ||
          !append_hex32(record.fighter_slot2_pointer) ||
          !append("\",\"fighter_slot2_gobj\":\"0x") ||
          !append_hex32(record.fighter_slot2_gobj) ||
          !append("\",\"decision_item\":\"0x") ||
          !append_hex32(record.decision_item) ||
          !append("\",\"decision_item_status\":\"0x") ||
          !append_hex32(record.decision_item_status) ||
          !append("\",\"arrow_present\":true,\"arrow_gobj\":\"0x") ||
          !append_hex32(record.arrow_gobj) || !append("\",\"arrow_item\":\"0x") ||
          !append_hex32(record.arrow_item) || !append("\",\"arrow_owner_gobj\":\"0x") ||
          !append_hex32(record.arrow_owner_gobj) || !append("\",\"arrow_kind\":") ||
          !append_number(record.arrow_kind) || !append(",\"arrow_list_order\":") ||
          !append_number(record.arrow_list_order) || !append(",\"arrow_anim_id\":\"0x") ||
          !append_hex32(record.arrow_anim_id) || !append("\",\"velocity_bits\":["))
        return false;
      for (size_t axis = 0; axis < record.velocity_bits.size(); ++axis)
      {
        if ((axis != 0 && !append(",")) || !append_hex_value(record.velocity_bits[axis]))
          return false;
      }
      if (!append("],\"position_bits\":["))
        return false;
      for (size_t axis = 0; axis < record.position_bits.size(); ++axis)
      {
        if ((axis != 0 && !append(",")) || !append_hex_value(record.position_bits[axis]))
          return false;
      }
      if (!append("],\"hitbox0_state\":") || !append_number(record.hitbox0_state) ||
          !append(",\"hitbox0_previous_endpoint_bits\":["))
        return false;
      for (size_t axis = 0; axis < record.hitbox0_previous_endpoint_bits.size(); ++axis)
      {
        if ((axis != 0 && !append(",")) ||
            !append_hex_value(record.hitbox0_previous_endpoint_bits[axis]))
          return false;
      }
      if (!append("],\"hitbox0_current_endpoint_bits\":["))
        return false;
      for (size_t axis = 0; axis < record.hitbox0_current_endpoint_bits.size(); ++axis)
      {
        if ((axis != 0 && !append(",")) ||
            !append_hex_value(record.hitbox0_current_endpoint_bits[axis]))
          return false;
      }
      if (!append("],\"arrow_damage_dealt\":\"0x") ||
          !append_hex32(record.arrow_damage_dealt) ||
          !append("\",\"arrow_pending_shield_damage\":\"0x") ||
          !append_hex32(record.arrow_pending_shield_damage) ||
          !append("\",\"arrow_shield_target_gobj\":\"0x") ||
          !append_hex32(record.arrow_shield_target_gobj) ||
          !append("\",\"arrow_damage_flags\":") ||
          !append_number(record.arrow_damage_flags) ||
          !append(",\"arrow_dispatch_state\":{\"ground_or_air\":") ||
          !append_number(record.arrow_ground_or_air) ||
          !append(",\"xDCE_raw\":") ||
          !append_number(record.arrow_damage_flags) ||
          !append(",\"xC54_angle_bits\":\"0x") ||
          !append_hex32(record.arrow_shield_angle_bits) ||
          !append("\",\"unk_degrees_bits\":\"0x") ||
          !append_hex32(record.arrow_common_shield_degrees_bits) ||
          !append("\",\"xDCE_b4\":") ||
          !append((record.arrow_damage_flags & 0x08) ? "true" : "false") ||
          !append(",\"xDCE_b5\":") ||
          !append((record.arrow_damage_flags & 0x04) ? "true" : "false") ||
          !append(",\"shield_bounced_present\":") ||
          !append(record.arrow_shield_bounced_present ? "true" : "false") ||
          !append(",\"hit_shield_present\":") ||
          !append(record.arrow_hit_shield_present ? "true}" : "false}"))
        return false;
      if (!append(",\"shield_overlap_inputs\":"))
        return false;
      if (!record.shield_inputs_present)
      {
        if (!append("null"))
          return false;
      }
      else
      {
        if (!append("{\"fpr1\":\"0x") || !append_hex64(record.shield_fpr[0]) ||
            !append("\",\"fpr2\":\"0x") || !append_hex64(record.shield_fpr[1]) ||
            !append("\",\"fpr3\":\"0x") || !append_hex64(record.shield_fpr[2]) ||
            !append("\",\"capsule_bytes\":\"0x") ||
            !AppendHexBytes(json, record.shield_capsule.data(), record.shield_capsule.size()) ||
            !append("\",\"hit_result_bytes\":\"0x") ||
            !AppendHexBytes(json, record.shield_result.data(), record.shield_result.size()) ||
            !append("\",\"transform_bytes\":"))
          return false;
        if (record.shield_transform_present)
        {
          if (!append("\"0x") ||
              !AppendHexBytes(json, record.shield_transform.data(), record.shield_transform.size()) ||
              !append("\"}"))
            return false;
        }
        else if (!append("null}"))
          return false;
      }
      if (!append(",\"arrow_ftcoll_inputs\":"))
        return false;
      if (!record.arrow_ftcoll_inputs_present)
      {
        if (!append("null"))
          return false;
      }
      else if (!append("{\"angle_fpr1\":\"0x") ||
               !append_hex64(record.arrow_ftcoll_angle) ||
               !append("\",\"collision_position_bytes\":\"0x") ||
               !AppendHexBytes(json, record.arrow_ftcoll_position.data(),
                               record.arrow_ftcoll_position.size()) ||
               !append("\",\"shield_hit_bytes\":\"0x") ||
               !AppendHexBytes(json, record.arrow_shield_hit.data(),
                               record.arrow_shield_hit.size()) ||
               !append("\",\"shield_bone_matrix_bytes\":\"0x") ||
               !AppendHexBytes(json, record.arrow_shield_bone_matrix.data(),
                               record.arrow_shield_bone_matrix.size()) ||
               !append("\"}"))
        return false;
      if (!append("}"))
        return false;
    }
    if (!append("]}\n") || json->size() > ITEM_PROBE_MAX_JSON_BYTES)
      return false;
    return true;
  }

  bool WriteItemProbe()
  {
    if (!item_probe_published.load(std::memory_order_acquire))
    {
      SetInvalid("item probe window did not close before capture completion");
      return false;
    }
    std::string json;
    if (!BuildItemProbeJson(&json))
    {
      SetInvalid("item probe JSON exceeded its bound or contained an invalid Arrow identity");
      return false;
    }
    File::DirectIOFile output(item_probe_output_path, File::AccessMode::Write,
                              File::OpenMode::Create);
    if (!output.IsOpen() || !output.Write(reinterpret_cast<const u8*>(json.data()), json.size()) ||
        !output.Flush() || !output.Close())
    {
      SetInvalid("item probe companion file could not be written");
      return false;
    }
    return true;
  }

  void WriterMain()
  {
    writer_last_dequeue_ns.store(SteadyNowNs(), std::memory_order_release);
    File::DirectIOFile output(output_path, File::AccessMode::Write, File::OpenMode::Create);
    if (!output.IsOpen())
      SetInvalid("observer stream could not be opened");
    if (!WriteStatus("starting", -1, 0, 0, false, true))
      SetInvalid("observer status could not be written");
    u64 tail = 0;
    u64 last_seq = static_cast<u64>(-1);
    bool error_written = false;
    while (true)
    {
      bool drained = false;
      while (tail < head_index.load(std::memory_order_acquire))
      {
        Slot& slot = ring[tail % RING_SIZE];
        if (!slot.ready.load(std::memory_order_acquire))
          break;
        const u64 dequeue_ns = SteadyNowNs();
        const u64 previous_dequeue_ns =
            writer_last_dequeue_ns.exchange(dequeue_ns, std::memory_order_acq_rel);
        if (previous_dequeue_ns != 0 && dequeue_ns > previous_dequeue_ns)
          UpdateMaximum(max_writer_dequeue_gap_ns, dequeue_ns - previous_dequeue_ns);
        if (output.IsOpen() && ShouldWriteObserverEvent(slot.event, item_probe_summary_stream))
        {
          const u64 write_start_ns = SteadyNowNs();
          if (!WriteFrame(output, slot))
          {
            SetInvalid("observer stream write failed");
            output.Close();
          }
          const u64 write_end_ns = SteadyNowNs();
          if (write_end_ns > write_start_ns)
            UpdateMaximum(max_write_frame_ns, write_end_ns - write_start_ns);
        }
        last_seq = slot.sequence;
        last_tick = slot.source_tick;
        last_draw = slot.draw_ordinal;
        ++event_count;
        slot.ready.store(false, std::memory_order_release);
        ++tail;
        tail_index.store(tail, std::memory_order_release);
        drained = true;
        if (!WriteStatus(invalid.load() ? "invalid" : "recording", static_cast<s64>(last_seq),
                         last_tick, last_draw, false))
          SetInvalid("observer status could not be written");
      }
      if (invalid.load() && !error_written && output.IsOpen())
      {
        const std::string reason = Error();
        Slot error_slot;
        error_slot.event = Event::Error;
        error_slot.sequence = last_seq + 1;
        error_slot.timestamp_ns = static_cast<u64>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        error_slot.payload_size = static_cast<u32>(reason.size());
        const std::string json = "{\"error\":\"" + JsonEscape(reason) + "\"}";
        error_slot.payload_size = static_cast<u32>(json.size());
        std::memcpy(error_slot.payload.data(), json.data(), json.size());
        error_slot.checksum = CRC32(error_slot.payload.data(), error_slot.payload_size);
        error_slot.guest_pc = 0;
        error_slot.source_tick = last_tick;
        error_slot.draw_ordinal = last_draw;
        if (WriteFrame(output, error_slot))
        {
          last_seq = error_slot.sequence;
          ++event_count;
          error_written = true;
        }
      }
      if (cpu_probe_configured && cpu_probe_valid && !cpu_probe_written &&
          cpu_probe_published.load(std::memory_order_acquire))
      {
        WriteCpuProbe();
        cpu_probe_written = true;
      }
      if (item_probe_configured && item_probe_valid && !item_probe_written &&
          item_probe_published.load(std::memory_order_acquire))
      {
        WriteItemProbe();
        item_probe_written = true;
      }
      const bool finished = finish_requested.load() && tail >= head_index.load();
      if (finished)
      {
        if (!InputStream::WaitComplete())
          SetInvalid("input stream did not complete successfully");
        if (AllocationConfigured() &&
            !ReferenceAllocation::Observer::Finish(natural_completion.load()))
          SetInvalid("allocation diagnostic did not complete: " +
                     ReferenceAllocation::Observer::Error());
        if (cpu_probe_configured && cpu_probe_valid && !cpu_probe_written)
        {
          WriteCpuProbe();
          cpu_probe_written = true;
        }
        if (item_probe_configured && item_probe_valid && !item_probe_written)
        {
          WriteItemProbe();
          item_probe_written = true;
        }
        if (output.IsOpen())
        {
          Slot end_slot;
          end_slot.event = Event::End;
          end_slot.sequence = last_seq + 1;
          end_slot.timestamp_ns = static_cast<u64>(
              std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count());
          const bool complete = natural_completion.load() && !invalid.load();
          std::string json = complete
                                       ? "{\"status\":\"completed\",\"natural\":true}"
                                       : "{\"status\":\"interrupted\",\"natural\":false}";
          if (checked_entity_profile)
            json = "{\"status\":\"" + std::string(complete ? "completed" : "interrupted") +
                   "\",\"natural\":false,\"diagnostic_prefix_complete\":" +
                   (complete ? "true" : "false") + ",\"whole_session_equivalent\":false," +
                   "\"comparison_source_ticks\":60,\"observed_source_ticks\":" +
                   std::to_string(entity_prefix_progress.observations) + "}";
          if (complete && transform_prefix_enabled)
          {
            json = "{\"status\":\"completed\",\"natural\":true,"
                   "\"completion_boundary\":\"active_sheik_grounded_neutral_source_tick_after_owner_change\","
                   "\"match_complete\":false,\"readiness_source_sequence\":{\"neutral_pad_consume\":" +
                   std::to_string(transform_prefix_neutral_pad_sequence) +
                   ",\"grounded_neutral_source_tick\":" +
                   std::to_string(transform_prefix_grounded_neutral_source_sequence) +
                   ",\"down_b_consume\":" +
                   std::to_string(transform_prefix_down_b_source_sequence) + "}}";
          }
          end_slot.payload_size = static_cast<u32>(json.size());
          std::memcpy(end_slot.payload.data(), json.data(), json.size());
          end_slot.checksum = CRC32(end_slot.payload.data(), end_slot.payload_size);
          end_slot.source_tick = last_tick;
          end_slot.draw_ordinal = last_draw;
          if (WriteFrame(output, end_slot))
          {
            last_seq = end_slot.sequence;
            ++event_count;
          }
          if (!output.Flush())
            SetInvalid("observer stream flush failed");
        }
        const bool complete = natural_completion.load() && !invalid.load();
        if (!WriteStatus(complete ? "completed" : (invalid.load() ? "invalid" : "interrupted"),
                         static_cast<s64>(last_seq), last_tick, last_draw, complete, true))
          SetInvalid("observer final status could not be written");
        return;
      }
      if (!drained)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  bool WriteFrame(File::DirectIOFile& output, const Slot& slot)
  {
    std::array<u8, 44> header{};
    u8* out = header.data();
    PutU32(out, MAGIC);
    PutU16(out, SCHEMA);
    PutU16(out, static_cast<u16>(slot.event));
    PutU64(out, slot.sequence);
    PutU64(out, slot.timestamp_ns);
    PutU32(out, slot.guest_pc);
    PutU32(out, slot.source_tick);
    PutU32(out, slot.draw_ordinal);
    PutU32(out, slot.payload_size);
    PutU32(out, slot.checksum);
    return output.Write(header.data(), header.size()) &&
           output.Write(slot.payload.data(), slot.payload_size);
  }

  bool WriteStatus(std::string_view state, s64 last_seq, u32 source_tick, u32 draw,
                   bool completed, bool force = false)
  {
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < next_status_write)
      return true;
    const std::string temporary = status_path + ".tmp." +
                                  std::to_string(reinterpret_cast<uintptr_t>(this));
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    File::DirectIOFile status(temporary, File::AccessMode::Write, File::OpenMode::Create);
    if (!status.IsOpen())
      return false;
    const std::string error_text = JsonEscape(Error());
    std::string json =
        "{\"state\":\"" + std::string(state) + "\",\"event_count\":" +
        std::to_string(event_count) + ",\"last_seq\":" + std::to_string(last_seq) +
        ",\"source_tick\":" + std::to_string(source_tick) +
        ",\"draw_ordinal\":" + std::to_string(draw) + ",\"completed\":" +
        (completed ? "true" : "false") + ",\"invalid\":" +
        (invalid.load() ? "true" : "false") + ",\"error\":" +
        (error_text.empty() ? "null" : "\"" + error_text + "\"");
    if (checked_entity_profile && force && state != "starting")
      json += ",\"diagnostic_prefix_complete\":" + std::string(completed ? "true" : "false") +
              ",\"whole_session_equivalent\":false,\"comparison_source_ticks\":60," +
              "\"observed_source_ticks\":" + std::to_string(entity_prefix_progress.observations);
    json += CpuProbeCloseStatusJson();
    json += "}\n";
    if (!status.Write(reinterpret_cast<const u8*>(json.data()), json.size()) || !status.Flush() ||
        !status.Close())
      return false;
    std::error_code error_code;
    std::filesystem::rename(temporary, status_path, error_code);
    if (error_code)
    {
      std::filesystem::remove(temporary, error_code);
      return false;
    }
    next_status_write = now + std::chrono::milliseconds(250);
    return true;
  }

  std::string Error() const
  {
    std::lock_guard lock(error_mutex);
    return error;
  }

  std::atomic<bool> started{false};
  std::atomic<bool> invalid{false};
  std::atomic<bool> finish_requested{false};
  std::atomic<bool> natural_completion{false};
  std::array<Slot, RING_SIZE> ring{};
  std::atomic<u64> head_index{0};
  std::atomic<u64> tail_index{0};
  std::atomic<u64> max_ring_occupancy{0};
  std::atomic<u64> writer_last_dequeue_ns{0};
  std::atomic<u64> max_writer_dequeue_gap_ns{0};
  std::atomic<u64> max_write_frame_ns{0};
  u64 next_sequence = 0;
  std::thread writer;
  std::string output_path;
  std::string status_path;
  mutable std::mutex error_mutex;
  std::string error;
  u64 event_count = 0;
  u32 last_tick = 0;
  u32 last_draw = 0;
  std::chrono::steady_clock::time_point next_status_write{};
  std::array<SliceRef, MAX_SLICES> slices{};
  std::array<u8, MAX_RAW> raw{};
  size_t slice_count = 0;
  size_t raw_size = 0;
  std::array<u32, 4> fighter_pointers{};
  std::array<bool, 4> fighter_present{};
  std::array<std::array<u32, 2>, 4> fighter_entity_pointers{};
  std::array<std::array<u32, 2>, 4> fighter_entity_kinds{};
  std::array<u8, 4> fighter_entity_count{};
  u32 cpu_probe_samus_effect_palette_address = 0;
  std::array<bool, 4> cpu_slots{};
  SdInitState sd_init;
  OrdinaryTimeoutState ordinary_timeout;
  u64 ordinary_records = 0, ordinary_bytes = 0;
  bool sd_sss_ready = false;
  bool sd_rules_observed = false;
  u32 sd_menu_polls = 0;
  u32 sd_menu_consumed = 0;
  bool sd_menu_neutral = false;
  bool sparse_setup_seen = false;
  u32 sparse_setup_consumed = 0;
  u32 sparse_source_samples = 0;
  u32 sparse_prepress_neutral_samples = 0;
  u8 sparse_witness_phase = 0;
  u32 setup_pointer = 0;
  u32 active_slot_count = 0;
  bool match_active = false;
  bool setup_ready = false;
  bool result_seen = false;
  u32 result_pointer = 0;
  u32 draw_ordinal = 0;
  bool cpu_probe_configured = false;
  bool cpu_probe_valid = false;
  bool cpu_probe_closed = false;
  bool cpu_probe_written = false;
  bool cpu_probe_effect_group_found = false;
  std::atomic<bool> cpu_probe_published{false};
  std::string cpu_probe_output_path;
  u32 cpu_probe_match = 0;
  u32 cpu_probe_first_tick = 0;
  u32 cpu_probe_last_tick = 0;
  u32 cpu_probe_rng_return_pc = 0;
  std::string cpu_probe_rng_return_site;
  u64 cpu_probe_rng_return_callback_count = 0;
  u32 cpu_probe_rng_return_callback_pc = 0;
  u32 cpu_probe_rng_return_callback_tick = 0;
  u32 cpu_probe_rng_return_callback_match = 0;
  bool cpu_probe_rng_return_callback_tick_valid = false;
  u32 cpu_probe_close_pc = 0;
  u32 cpu_probe_close_tick = 0;
  size_t cpu_probe_record_count = 0;
  std::unique_ptr<CpuProbeRecord[]> cpu_probe_records;
  std::string cpu_probe_error;
  bool item_probe_configured = false;
  bool item_probe_valid = false;
  bool item_probe_summary_stream = false;
  bool item_probe_closed = false;
  bool item_probe_written = false;
  std::atomic<bool> item_probe_published{false};
  std::string item_probe_output_path;
  u32 item_probe_match = 0;
  u32 item_probe_first_tick = 0;
  u32 item_probe_last_tick = 0;
  bool item_probe_trigger_on_arrow_creation = false;
  bool item_probe_trigger_on_arrow_launch = false;
  bool item_probe_window_triggered = false;
  u32 item_probe_capture_ticks = 0;
  size_t item_probe_record_count = 0;
  std::unique_ptr<ItemProbeRecord[]> item_probe_records;
  std::string item_probe_error;
  std::array<bool, 16> item_probe_pair_active{};
  std::array<u32, 16> item_probe_pair_lr{};
  bool checked_entity_profile = false;
  EntityPrefixBoundaryProgress entity_prefix_progress;
  bool entity_prefix_final_draw = false;
  u32 whole_session_matches = 0;
  u32 audio_owner_epoch = 0;
  u32 match_index = 0;
  bool css_steering_ready = false;
  u32 whole_phase = 0;  // CSS, SSS, VS, completed match, or return CSS.
  bool pending_next_match = false;
  bool awaiting_final_css = false;
  bool vs_exit_seen = false;
  bool vs_exit_return_seen = false;
  bool vs_mode_exit_seen = false;
  bool results_enter_seen = false;
  bool results_gobj_seen = false;
  bool results_exit_seen = false;
  bool results_mode_exit_seen = false;
  bool completed_match_pending_prize = false;
  bool startup_prize_pending = false;
  bool prize_mode_enter_seen = false;
  bool prize_scene_enter_seen = false;
  bool prize_scene_exit_seen = false;
  bool prize_mode_exit_seen = false;
  std::string capture_id;
  std::string sequence_id;
  bool transform_prefix_enabled = false;
  bool transform_prefix_vs_entry_seen = false;
  bool transform_prefix_css_enter_seen = false;
  bool transform_prefix_css_ready = false;
  bool transform_prefix_css_live_owner_seen = false;
  bool transform_prefix_sss_enter_seen = false;
  bool transform_prefix_sss_ready = false;
  bool transform_prefix_sss_live_owner_seen = false;
  u32 transform_prefix_active_ticks = 0;
  bool transform_prefix_first_source_tick_seen = false;
  u32 transform_prefix_last_source_tick = 0;
  bool transform_prefix_down_b_seen = false;
  bool transform_prefix_previous_down_b = false;
  bool transform_prefix_neutral_pad_seen = false;
  u64 transform_prefix_neutral_pad_sequence = 0;
  bool transform_prefix_release_seen = false;
  bool transform_prefix_action_seen = false;
  bool transform_prefix_swap_seen = false;
  bool transform_prefix_grounded_neutral_seen = false;
  u64 transform_prefix_grounded_neutral_source_sequence = 0;
  u64 transform_prefix_down_b_source_sequence = 0;
  bool transform_prefix_sheik_neutral_seen = false;

  bool whole_session_enabled() const { return whole_session_matches != 0; }
};

Observer::Observer() : m_impl(new Impl) {}
Observer::~Observer()
{
  delete m_impl;
}

Observer& Observer::Instance()
{
  static Observer observer;
  return observer;
}

bool Observer::ValidateDiscDOL(const DiscIO::VolumeDisc& volume)
{
  const DiscIO::Partition partition = volume.GetGamePartition();
  if (volume.GetGameID(partition) != "GALE01" || volume.GetRevision(partition) != 2)
    return false;

  const auto dol_offset = DiscIO::GetBootDOLOffset(volume, partition);
  if (!dol_offset)
    return false;
  const auto dol_size = DiscIO::GetBootDOLSize(volume, partition, *dol_offset);
  if (!dol_size || *dol_size == 0 || *dol_size > 64 * 1024 * 1024)
    return false;

  std::vector<u8> dol(*dol_size);
  if (!volume.Read(*dol_offset, dol.size(), dol.data(), partition))
    return false;

  std::array<u8, 32> sha256{};
  if (mbedtls_sha256_ret(dol.data(), dol.size(), sha256.data(), 0) != 0 ||
      sha256 != EXPECTED_DOL_SHA256_BYTES)
    return false;
  if (Common::SHA1::CalculateDigest(dol) != EXPECTED_DOL_SHA1_BYTES ||
      !InputStream::Initialize())
    return false;
  const std::string allocation_output = Env("MWRC_ALLOCATION_OUTPUT");
  if (allocation_output.empty())
    return true;
  for (const char* other : {"MWRC_OUTPUT", "MWRC_STATUS", "MWRC_CPU_PROBE_OUTPUT",
                            "MWRC_INPUT_RECORD", "MWRC_INPUT_REPLAY"})
  {
    if (allocation_output == Env(other))
      return false;
  }
  // Arm while boot still owns the DOL, before any of its blocks are compiled.
  // The module reads guest memory only at the verified DOL-entry callback.
  return ReferenceAllocation::Observer::Arm(ReferenceAllocation::Generated::kProfile,
                                            allocation_output);
}

void Observer::Fail(const char* reason)
{
  Instance().m_impl->SetInvalid(reason);
}

bool Observer::IsEnabled()
{
  static const bool enabled = ActivationRequested();
  return enabled;
}

static bool IsCaptureBoundary(u32 guest_pc)
{
  switch (guest_pc)
  {
  case 0x8034DD8C:
  case 0x80377584:
  case 0x800693A8:
  case 0x8016E934:
  case 0x8016E9C4:
  case 0x80390EB4:
  case 0x80390FC0:
  case 0x80391040:
  case 0x8016E9C8:
  case 0x8016EBBC:
  case 0x8039157C:
  case 0x801A4B70:
  case 0x8026688C:
  case 0x802669F0:
  case 0x80266D70:
  case 0x8025A998:
  case 0x8025BBD0:
  case 0x801A5AF0:
  case 0x80177368:
  case 0x80177704:
  case 0x801A5F64:
  case 0x80179350:
  case 0x801BFCFC:
  case 0x802FEBE0:
  case 0x802FED10:
  case 0x801A6308:
  case 0x801BFF7C:
  case 0x8038E8EC:
    return true;
  default:
    // Diagnostic CPU PCs are JIT boundaries only for the fully validated,
    // opt-in companion configuration.  The normal observer boundary set and
    // its disabled path remain unchanged.
    return (Env("MWRC_TRANSFORM_PREFIX") == "1" && guest_pc == SSS_ENTER_RETURN) ||
           (SdInitRequested() && (guest_pc == 0x8016ebc0 || guest_pc == 0x8016ec24 ||
            ((Env("MWRC_SD_MENU_PROBE") == "sd_prefix" || Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()) && guest_pc == 0x8025b84c))) ||
           (CpuProbeEnabled() && FindCpuProbePoint(guest_pc) != nullptr &&
            (CpuProbeEnvironment().rng_return_pc == 0 ||
             CpuProbeEnvironment().rng_return_pc == guest_pc)) ||
           (ItemProbeEnabled() && FindItemProbePoint(guest_pc) != nullptr);
  }
}

bool Observer::IsRngReturnBoundary(u32 guest_pc)
{
  return CpuProbeEnabled() && CpuProbeEnvironment().rng_return_pc == guest_pc &&
         FindCpuProbePoint(guest_pc) != nullptr;
}

bool Observer::IsBoundary(u32 guest_pc)
{
  return IsCaptureBoundary(guest_pc) ||
         (AllocationConfigured() && ReferenceAllocation::Observer::IsBoundary(guest_pc));
}

void Observer::OnBoundary(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  if (!IsEnabled() || !system || !state)
    return;
  if (AllocationConfigured())
  {
    ReferenceAllocation::Observer::Observe(system, guest_pc, state);
    const std::string allocation_error = ReferenceAllocation::Observer::Error();
    if (!allocation_error.empty())
      Instance().m_impl->SetInvalid("allocation diagnostic failed: " + allocation_error);
  }
  // Allocation-only callbacks must not start or extend the primary stream.
  if (IsCaptureBoundary(guest_pc))
    Instance().Observe(system, guest_pc, state);
}

void Observer::Observe(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  m_impl->Observe(system, guest_pc, state);
}

}  // namespace ReferenceCapture
