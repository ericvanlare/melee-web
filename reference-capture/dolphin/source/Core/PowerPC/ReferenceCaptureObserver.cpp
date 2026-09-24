// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/ReferenceCaptureObserver.h"
#include "Core/PowerPC/ReferenceInputStream.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
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
constexpr size_t RING_SIZE = 128;
constexpr size_t RING_PAYLOAD = 256 * 1024;
constexpr size_t MAX_SLICES = 64;
constexpr size_t MAX_RAW = 192 * 1024;
constexpr u32 PAD_READ_HSD_CALLER = 0x80376A28;
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
};

struct SliceRef
{
  SliceTag tag;
  u16 flags;
  u32 address;
  u32 size;
  u32 offset;
};

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

constexpr bool IsGuestRange(u32 address, size_t size)
{
  if (size == 0 || size > 0x100000)
    return false;
  const u64 end = static_cast<u64>(address) + size;
  return (address >= 0x80000000U && end <= 0x81800000U) ||
         (address >= 0x90000000U && end <= 0x94000000U);
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

bool ActivationRequested()
{
  return Env("MWRC_ENABLE") == "1" && !Env("MWRC_OUTPUT").empty() &&
         Env("MWRC_DOL_SHA256") == EXPECTED_DOL_SHA256 && Env("MWRC_CPU") == "JITARM64" &&
         Env("MWRC_SOURCE_REV") == "GALE01r2";
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
  return result >= WHOLE_SESSION_MIN_MATCHES ? result : 0;
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

}  // namespace

struct Observer::Impl
{
  Impl() = default;

  ~Impl()
  {
    Stop();
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
    capture_id = Env("MWRC_CAPTURE_ID");
    sequence_id = Env("MWRC_SEQUENCE_ID");
    // Dolphin builds with exceptions disabled.  std::thread reports an
    // unavailable worker by terminating; there is no catchable error path.
    writer = std::thread([this] { WriterMain(); });
    if (!Env("MWRC_WHOLE_SESSION_MATCHES").empty() && whole_session_matches == 0)
    {
      SetInvalid("MWRC_WHOLE_SESSION_MATCHES must be a decimal count from 3 through 64");
      return false;
    }
    if (whole_session_enabled() && (!ValidIdentity(capture_id) || !ValidIdentity(sequence_id)))
    {
      SetInvalid("whole-session capture and sequence IDs must be safe non-empty strings");
      return false;
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

  bool ReadU32(Core::System* system, u32 address, u32* value) const
  {
    std::array<u8, 4> bytes{};
    if (!ReadBytes(system, address, bytes.size(), bytes.data()))
      return false;
    *value = ReadBE32(bytes.data());
    return true;
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

  bool AddFighterSlices(Core::System* system, u32 slot, u32 pointer)
  {
    if (!AddSlice(system, SliceTag::FighterHead, pointer, 0x100, static_cast<u16>(slot)) ||
        !AddSlice(system, SliceTag::FighterInputAnim, pointer + 0x620, 0x280,
                  static_cast<u16>(slot)) ||
        !AddSlice(system, SliceTag::FighterDamageShield, pointer + 0x1830, 0x16c,
                  static_cast<u16>(slot)) ||
        !AddSlice(system, SliceTag::FighterStocks, 0x80453080 + slot * 0xe90 + 0x8e, 1,
                  static_cast<u16>(slot)))
      return false;
    u32 subject = 0;
    if (!ReadU32(system, pointer + 0x890, &subject))
      return false;
    if (subject && !AddSlice(system, SliceTag::FighterSubject, subject, 0x28,
                             static_cast<u16>(slot)))
      return false;
    if (cpu_slots[slot] &&
        !AddSlice(system, SliceTag::CpuState, pointer + 0x1a88, 0x57c,
                  static_cast<u16>(slot)))
      return false;
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
      if (fighter_present[slot] && !AddFighterSlices(system, slot, fighter_pointers[slot]))
        return false;
      if (fighter_present[slot] &&
          (!AddSlice(system, SliceTag::Hud, 0x804a10c8 + slot * 0x64, 0x11,
                     static_cast<u16>(slot)) ||
           !AddSlice(system, SliceTag::Magnifier, 0x804a1de0 + 0x14 + slot * 0x10 + 0xc, 1,
                     static_cast<u16>(slot))))
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
    // Steering evidence for the ordinary menu route: the highlighted stage
    // index, the authored stage kind it points at, and each CSS cursor. Every
    // part is optional so a boundary outside those menus stays valid.
    u8 stage_index = 0;
    if (ReadBytes(system, STAGE_SELECT_INDEX, 1, &stage_index))
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
    case 0x8025bb5c:
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
      return word == 0x4e800020;
    case 0x8016e9c8:
      return word == 0x7c0802a6;
    case 0x8026688c:
    case 0x80266d70:
    case 0x8025a998:
    case 0x8025bb5c:
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

  void Observe(Core::System* system, u32 pc, PowerPC::PowerPCState* state)
  {
    if (!Start() || invalid.load() || finish_requested.load())
      return;
    if (whole_session_enabled() && pc == MENU_AUDIO_STREAM_START)
    {
      ++audio_owner_epoch;
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
    raw_size = 0;
    slice_count = 0;
    if (boundary == Boundary::CssEnter || boundary == Boundary::CssCancelEnter ||
        boundary == Boundary::CssExit ||
        boundary == Boundary::SssEnter || boundary == Boundary::SssExit ||
        boundary == Boundary::ReturnCss)
    {
      if (boundary == Boundary::ReturnCss)
      {
        if (whole_phase != 6 || (!pending_next_match && !awaiting_final_css))
          return SetInvalid("whole-session return CSS was missing or out of order"), void();
        whole_phase = awaiting_final_css ? 7 : 1;
      }
      else if (boundary == Boundary::CssEnter)
      {
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
    }
    else if (boundary == Boundary::Entry || boundary == Boundary::Setup)
    {
      if (boundary == Boundary::Entry)
      {
        u8 current_mode = 0;
        if (whole_session_enabled() &&
            !ReadBytes(system, 0x80479d30, 1, &current_mode))
          return SetInvalid("VS entry did not expose source mode routing"), void();
        // Opening movie attract demos reuse the VS constructor and can run
        // while the outer routing record still names GM_OPENING_MV. They are
        // pre-CSS source coverage, not the supported SSS-to-match route.
        if (whole_session_enabled() && current_mode == 0x18 && whole_phase == 0)
          return;
        if (whole_session_enabled() && (current_mode != 0x02 || whole_phase != 4))
          return SetInvalid("whole-session VS entry was missing its SSS route"), void();
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
        cpu_slots.fill(false);
        draw_ordinal = 0;
        const auto* setup = system->GetMemory().GetPointerForRange(setup_pointer, 0x138);
        if (!setup)
          return SetInvalid("source setup pointer is invalid"), void();
        // The same entry routine is also used by title-screen attract demos.
        // Their setup can carry the ordinary VS bit, so the source mode is
        // part of the guard: the whole-session contract arms only the
        // ordinary GM_VS route reached from SSS. Keep the legacy observer's
        // setup-only behavior when whole-session capture is disabled.
        match_active = (setup[4] & 0x40) != 0 &&
                      (!whole_session_enabled() || current_mode == 0x02);
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
                         [](bool present) { return present; }) ||
            !AddMatchSlices(system))
          return SetInvalid("match setup completed before all bounded fighter slices were ready"),
                 void();
        setup_ready = true;
      }
    }
    else if (boundary == Boundary::FighterCreate)
    {
      if (!match_active)
        return;
      u32 pointer = 0;
      u8 slot = 0xff;
      if (!AddSlice(system, SliceTag::FighterCreateContext, state->gpr[3], 0x30) ||
          !ReadU32(system, state->gpr[3] + 0x2c, &pointer) || !pointer ||
          !ReadBytes(system, pointer + 0xc, 1, &slot) || slot >= active_slot_count ||
          fighter_present[slot])
        return SetInvalid("fighter creation exposed an invalid source slot"), void();
      fighter_pointers[slot] = pointer;
      fighter_present[slot] = true;
      if (!AddSlice(system, SliceTag::FighterHead, pointer, 0x100,
                    static_cast<u16>(slot)))
        return SetInvalid("fighter creation slices escaped the pinned ranges"), void();
    }
    else if (boundary == Boundary::SourceTick || boundary == Boundary::DrawEnter ||
             boundary == Boundary::DrawReturn)
    {
      if (!match_active || !setup_ready ||
          !std::all_of(fighter_present.begin(), fighter_present.begin() + active_slot_count,
                       [](bool present) { return present; }))
        return;
      if (!AddMatchSlices(system))
        return SetInvalid("match semantic slice escaped the pinned ranges"), void();
    }
    else if (boundary == Boundary::ResultEnter || boundary == Boundary::ResultReturn)
    {
      if (!match_active)
        return;
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
        if (!whole_session_enabled() || match_active ||
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
        if (!whole_session_enabled() || !match_active || !setup_ready)
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
          if (!AddSessionSlices(system))
            return SetInvalid("VS exit return did not expose PAD/RNG state"), void();
          vs_exit_return_seen = true;
          result_seen = true;
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
        // Retail performs an early scene reset while booting the original
        // title, before the first CSS callback has established the whole
        // session route.  It is not a completed match teardown.  Ignore only
        // this pre-route reset; once a route is active, keep the strict
        // VS/Results ordering below.
        if (!match_active && whole_phase == 0)
          return;
        if (!match_active || !result_seen || !vs_exit_seen || !vs_exit_return_seen ||
            !vs_mode_exit_seen || !results_enter_seen || !results_exit_seen ||
            !results_gobj_seen || !results_mode_exit_seen)
          return SetInvalid("whole-session scene reset is missing an ordered VS/Results hook"),
                 void();
      }
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
    Publish(slot);
    if (boundary == Boundary::DrawReturn)
      ++draw_ordinal;
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

  Slot* Reserve(Event event, u32 pc, u32 source_tick, u32 draw_count)
  {
    if (invalid.load() || finish_requested.load())
      return nullptr;
    const u64 head = head_index.load(std::memory_order_relaxed);
    Slot& slot = ring[head % RING_SIZE];
    if (slot.ready.load(std::memory_order_acquire))
    {
      SetInvalid("observer ring overflow; no record was dropped");
      return nullptr;
    }
    slot.event = event;
    slot.sequence = next_sequence++;
    slot.timestamp_ns = static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
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

  void WriterMain()
  {
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
        if (output.IsOpen())
        {
          if (!WriteFrame(output, slot))
          {
            SetInvalid("observer stream write failed");
            output.Close();
          }
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
      const bool finished = finish_requested.load() && tail >= head_index.load();
      if (finished)
      {
        if (!InputStream::WaitComplete())
          SetInvalid("input stream did not complete successfully");
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
          const std::string json = complete
                                       ? "{\"status\":\"completed\",\"natural\":true}"
                                       : "{\"status\":\"interrupted\",\"natural\":false}";
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
    const std::string json =
        "{\"state\":\"" + std::string(state) + "\",\"event_count\":" +
        std::to_string(event_count) + ",\"last_seq\":" + std::to_string(last_seq) +
        ",\"source_tick\":" + std::to_string(source_tick) +
        ",\"draw_ordinal\":" + std::to_string(draw) + ",\"completed\":" +
        (completed ? "true" : "false") + ",\"invalid\":" +
        (invalid.load() ? "true" : "false") + ",\"error\":" +
        (error_text.empty() ? "null" : "\"" + error_text + "\"") + "}\n";
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
  std::array<bool, 4> cpu_slots{};
  u32 setup_pointer = 0;
  u32 active_slot_count = 0;
  bool match_active = false;
  bool setup_ready = false;
  bool result_seen = false;
  u32 result_pointer = 0;
  u32 draw_ordinal = 0;
  u32 whole_session_matches = 0;
  u32 audio_owner_epoch = 0;
  u32 match_index = 0;
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
  return Common::SHA1::CalculateDigest(dol) == EXPECTED_DOL_SHA1_BYTES &&
         InputStream::Initialize();
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

bool Observer::IsBoundary(u32 guest_pc)
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
  case 0x80266D70:
  case 0x8025A998:
  case 0x8025BB5C:
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
    return false;
  }
}

void Observer::OnBoundary(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  if (!IsEnabled() || !system || !state)
    return;
  Instance().Observe(system, guest_pc, state);
}

void Observer::Observe(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  m_impl->Observe(system, guest_pc, state);
}

}  // namespace ReferenceCapture
