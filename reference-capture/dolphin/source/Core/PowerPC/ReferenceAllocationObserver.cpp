// Copyright 2026 Melee Web contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/ReferenceAllocationObserver.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <mbedtls/sha256.h>
#include <zlib.h>

#include "Common/DirectIOFile.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

namespace ReferenceAllocation
{
namespace
{
constexpr char kExpectedDolSha1[] = "08e0bf20134dfcb260699671004527b2d6bb1a45";
constexpr char kExpectedSourceRevision[] = "b43912cc78606f96c9569f5d6229bc9d7e265ea5";
constexpr size_t kFunctionCount = 52;
constexpr size_t kGlobalCount = 23;
constexpr size_t kMaxThreads = 16;
constexpr size_t kMaxDepth = 64;
constexpr size_t kRingSize = 8192;
constexpr size_t kMaxEvents = 2'000'000;
constexpr size_t kMaxConfiguredEvents = 100'000'000;
constexpr size_t kMaxReads = 256;
constexpr size_t kMaxDescriptors = 32;
constexpr size_t kMaxPayload = 64 * 1024;
constexpr size_t kMaxQueuedBytes = 8 * 1024 * 1024;
// The writer owns this bounded buffer.  Records are copied out of released
// ring slots and written as one stream chunk, keeping the callback producer
// independent of filesystem syscall frequency.
constexpr size_t kWriterBatchBytes = 256 * 1024;
constexpr size_t kMaxProfileHashReads = kFunctionCount * (0x100000 / 256);
constexpr u32 kThreadGlobal = 0x800000e4;
constexpr u32 kSceneFrameGlobal = 0x80479d58;
constexpr u32 kRetraceGlobal = 0x804d7420;
constexpr u32 kBootMemorySize = 0x80000028;
constexpr u32 kBootArenaLo = 0x80000030;
constexpr u32 kBootArenaHi = 0x80000034;
constexpr u32 kBootBi2 = 0x800000f4;
constexpr u32 kBlr = 0x4e800020;

constexpr bool IsMem1Range(u32 address, size_t size)
{
  if (size == 0 || size > 0x100000)
    return false;
  const u64 end = static_cast<u64>(address) + size;
  return address >= 0x80000000U && end <= 0x81800000U;
}

constexpr bool IsMem1Read(u32 address, size_t size)
{
  return size > 0 && size <= kMaxReads && IsMem1Range(address, size);
}

bool IsHex256(std::string_view value)
{
  if (value.size() != 64)
    return false;
  return std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  });
}

std::string HexDigest(const std::array<u8, 32>& digest)
{
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (u8 byte : digest)
  {
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0xf]);
  }
  return result;
}

bool IsSafePath(std::string_view value)
{
  if (value.empty() || value.size() > 4096)
    return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= 0x20; });
}

std::string JsonEscape(std::string_view value)
{
  std::string result;
  result.reserve(value.size() + 2);
  constexpr char hex[] = "0123456789abcdef";
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

bool Append(std::string* output, std::string_view value)
{
  if (output->size() > kMaxPayload || value.size() > kMaxPayload - output->size())
    return false;
  output->append(value);
  return true;
}

bool AppendNumber(std::string* output, u64 value)
{
  return Append(output, std::to_string(value));
}

struct Machine
{
  std::array<u32, 32> gpr{};
  u32 pc = 0;
  u32 lr = 0;
  u32 ctr = 0;
  u32 cr = 0;
  u32 scene_frame = 0;
  u32 retrace_count = 0;
};

struct CallFrame
{
  u32 function_index = 0;
  u32 call = 0;
  u32 thread = 0;
  u32 sp = 0;
  u32 lr = 0;
  std::array<u32, 8> args{};
  u32 argc = 0;
  bool has_parent = false;
  u32 parent = 0;
};

struct ThreadStack
{
  bool used = false;
  u32 thread = 0;
  size_t depth = 0;
  std::array<CallFrame, kMaxDepth> frames{};
};

struct Slot
{
  std::atomic<bool> ready{false};
  size_t queued_size = 0;
  std::string payload;
};

struct LastStop
{
  bool valid = false;
  u64 sequence = 0;
  u32 function_index = 0;
  std::string key;
  std::string state;
  u32 repeats = 0;
};

struct State
{
  std::atomic<bool> armed{false};
  std::atomic<bool> started{false};
  std::atomic<bool> initialized{false};
  std::atomic<bool> invalid{false};
  std::atomic<bool> finish_requested{false};
  std::atomic<bool> natural{false};
  BoundProfile profile{};
  std::string output_path;
  std::string error;
  mutable std::mutex error_mutex;
  mutable std::mutex lifecycle_mutex;
  std::condition_variable lifecycle_cv;
  size_t callbacks_in_flight = 0;
  bool finishing = false;
  std::thread writer;
  std::array<Slot, kRingSize> ring{};
  std::atomic<u64> head{0};
  std::atomic<u64> tail{0};
  std::atomic<size_t> queued_bytes{0};
  std::atomic<u64> next_sequence{0};
  u32 call_count = 0;
  u32 repeated_stops = 0;
  bool boundary_complete = false;
  u32 vs_target = 1;
  u32 vs_entries = 0;
  u32 vs_exits = 0;
  bool stop_at_exit = false;
  bool target_complete = false;
  bool active_owner = false;
  bool ownership_complete = false;
  u64 max_events = kMaxEvents;
  bool gzip_output = false;
  std::array<ThreadStack, kMaxThreads> threads{};
  LastStop last_stop;
};

class CallbackScope final
{
public:
  explicit CallbackScope(State& state) : state_(state)
  {
    std::lock_guard lock(state_.lifecycle_mutex);
    if (state_.armed.load(std::memory_order_acquire) &&
        !state_.invalid.load(std::memory_order_acquire) && !state_.finishing &&
        !state_.finish_requested.load(std::memory_order_acquire))
    {
      ++state_.callbacks_in_flight;
      active_ = true;
    }
  }

  ~CallbackScope()
  {
    if (!active_)
      return;
    std::lock_guard lock(state_.lifecycle_mutex);
    --state_.callbacks_in_flight;
    if (state_.callbacks_in_flight == 0)
      state_.lifecycle_cv.notify_all();
  }

  bool Active() const { return active_; }

private:
  State& state_;
  bool active_ = false;
};

State& GetState()
{
  static State state;
  return state;
}

const FunctionIdentity* FunctionAt(const BoundProfile& profile, u32 index)
{
  return index < profile.function_count ? &profile.functions[index] : nullptr;
}

const GlobalIdentity* FindGlobal(const BoundProfile& profile, std::string_view name)
{
  for (u32 index = 0; index < profile.global_count; ++index)
  {
    const GlobalIdentity& global = profile.globals[index];
    if (global.name && name == global.name)
      return &global;
  }
  return nullptr;
}

const FunctionIdentity* FindEntry(const BoundProfile& profile, u32 pc, u32* index)
{
  for (u32 candidate = 0; candidate < profile.function_count; ++candidate)
  {
    if (profile.functions[candidate].address == pc)
    {
      *index = candidate;
      return &profile.functions[candidate];
    }
  }
  return nullptr;
}

const FunctionIdentity* FindReturn(const BoundProfile& profile, u32 pc, u32* index)
{
  for (u32 candidate = 0; candidate < profile.function_count; ++candidate)
  {
    const FunctionIdentity& function = profile.functions[candidate];
    for (u32 return_index = 0; return_index < function.return_count; ++return_index)
    {
      if (function.returns[return_index] == pc)
      {
        *index = candidate;
        return &function;
      }
    }
  }
  return nullptr;
}

bool AppendMachineJson(std::string* output, const Machine& machine)
{
  if (!Append(output, "{\"r0\":"))
    return false;
  for (size_t index = 0; index < machine.gpr.size(); ++index)
  {
    if (index != 0 && !Append(output, ",\"r" + std::to_string(index) + "\":"))
      return false;
    if (index == 0 && !AppendNumber(output, machine.gpr[index]))
      return false;
    else if (index != 0 && !AppendNumber(output, machine.gpr[index]))
      return false;
  }
  return Append(output, ",\"pc\":") && AppendNumber(output, machine.pc) &&
         Append(output, ",\"lr\":") && AppendNumber(output, machine.lr) &&
         Append(output, ",\"ctr\":") && AppendNumber(output, machine.ctr) &&
         Append(output, ",\"cr\":") && AppendNumber(output, machine.cr) &&
         Append(output, ",\"scene_frame\":") && AppendNumber(output, machine.scene_frame) &&
         Append(output, ",\"retrace_count\":") && AppendNumber(output, machine.retrace_count) &&
         Append(output, "}");
}

class OutputStream final
{
public:
  OutputStream(const std::string& path, bool gzip)
      : output_(path, File::AccessMode::Write, File::OpenMode::Create), gzip_(gzip)
  {
    if (!gzip_)
      return;
    if (!output_.IsOpen())
    {
      failed_ = true;
      return;
    }
    if (deflateInit2(&stream_, Z_BEST_SPEED, Z_DEFLATED, 15 + 16, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK)
    {
      failed_ = true;
      return;
    }
    initialized_ = true;
  }

  ~OutputStream()
  {
    if (initialized_)
      deflateEnd(&stream_);
  }

  bool IsOpen() const
  {
    return output_.IsOpen() && !failed_ && (!gzip_ || initialized_);
  }

  bool Write(const u8* data, size_t size)
  {
    if (!IsOpen())
      return false;
    if (!gzip_)
      return output_.Write(data, size);

    while (size != 0)
    {
      const uInt chunk = static_cast<uInt>(std::min<size_t>(size, std::numeric_limits<uInt>::max()));
      stream_.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
      stream_.avail_in = chunk;
      while (stream_.avail_in != 0)
      {
        stream_.next_out = compressed_.data();
        stream_.avail_out = static_cast<uInt>(compressed_.size());
        const int result = deflate(&stream_, Z_NO_FLUSH);
        if (result != Z_OK || !WriteCompressed())
        {
          failed_ = true;
          return false;
        }
      }
      data += chunk;
      size -= chunk;
    }
    return true;
  }

  bool Flush()
  {
    if (!IsOpen())
      return false;
    if (!gzip_)
      return output_.Flush();

    int result = Z_OK;
    do
    {
      stream_.next_in = nullptr;
      stream_.avail_in = 0;
      stream_.next_out = compressed_.data();
      stream_.avail_out = static_cast<uInt>(compressed_.size());
      result = deflate(&stream_, Z_SYNC_FLUSH);
      if ((result != Z_OK && result != Z_BUF_ERROR) || !WriteCompressed())
      {
        failed_ = true;
        return false;
      }
    } while (stream_.avail_out == 0);
    return output_.Flush();
  }

  bool Finish()
  {
    if (!IsOpen())
      return false;
    if (!gzip_)
      return output_.Flush() && output_.Close();

    int result = Z_OK;
    do
    {
      stream_.next_in = nullptr;
      stream_.avail_in = 0;
      stream_.next_out = compressed_.data();
      stream_.avail_out = static_cast<uInt>(compressed_.size());
      result = deflate(&stream_, Z_FINISH);
      if ((result != Z_OK && result != Z_STREAM_END) || !WriteCompressed())
      {
        failed_ = true;
        return false;
      }
    } while (result != Z_STREAM_END);
    const int end_result = deflateEnd(&stream_);
    initialized_ = false;
    const bool flushed = output_.Flush();
    const bool closed = output_.Close();
    if (end_result != Z_OK)
      failed_ = true;
    return end_result == Z_OK && flushed && closed;
  }

  bool Close()
  {
    if (initialized_)
    {
      deflateEnd(&stream_);
      initialized_ = false;
    }
    return output_.Close();
  }

private:
  bool WriteCompressed()
  {
    const size_t produced = compressed_.size() - stream_.avail_out;
    return produced == 0 || output_.Write(compressed_.data(), produced);
  }

  File::DirectIOFile output_;
  bool gzip_ = false;
  bool initialized_ = false;
  bool failed_ = false;
  z_stream stream_{};
  std::array<Bytef, 64 * 1024> compressed_{};
};

class Backend
{
public:
  ~Backend()
  {
    if (state_.writer.joinable())
    {
      // Process teardown is an ordinary (non-natural) finish. Preserve the
      // requested stop boundary, rather than treating the first
      // VS entry as complete for an opt-in repeated-ownership capture.
      state_.natural.store(state_.target_complete);
      state_.finish_requested.store(true);
      state_.writer.join();
    }
  }

  static Backend& Instance()
  {
    static Backend backend;
    return backend;
  }

  bool Arm(const BoundProfile& profile, std::string_view output_path)
  {
    std::lock_guard lifecycle_lock(state_.lifecycle_mutex);
    if (state_.armed.load() || state_.started.load() || state_.invalid.load())
      return false;
    state_.profile = profile;
    state_.output_path = std::string(output_path);
    state_.gzip_output = state_.output_path.size() >= 3 &&
                         state_.output_path.ends_with(".gz");
    if (!ConfigureCaptureScope())
      return false;
    if (!ValidateProfile() || !IsSafePath(output_path))
    {
      Fail("allocation observer arm identity or output validation failed");
      return false;
    }
    state_.armed.store(true);
    return true;
  }

  bool Start(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
  {
    {
      std::lock_guard lifecycle_lock(state_.lifecycle_mutex);
      if (!state_.armed.load() || state_.started.load() || state_.invalid.load() ||
          state_.finishing || !system || !state || guest_pc != state_.profile.entry)
      {
        Fail("allocation observer start requires the verified original DOL entry");
        return false;
      }
    }
    if (!system || !state)
    {
      Fail("allocation observer start requires the verified original DOL entry");
      return false;
    }
    u32 entry_word = 0;
    if (!ReadWord(system, guest_pc, &entry_word) || entry_word != state_.profile.entry_word)
    {
      Fail("allocation observer start entry instruction differs from the generated profile");
      return false;
    }
    if (!ValidateBodyHashes(system))
    {
      Fail("allocation observer loaded function body differs from the generated profile");
      return false;
    }
    std::array<u32, 4> boot{};
    if (!ReadWord(system, kBootMemorySize, &boot[0]) || !ReadWord(system, kBootArenaLo, &boot[1]) ||
        !ReadWord(system, kBootArenaHi, &boot[2]) || !ReadWord(system, kBootBi2, &boot[3]))
    {
      Fail("allocation observer boot context is outside bounded MEM1");
      return false;
    }
    std::string header =
        "{\"record\":\"header\",\"schema\":\"melee-web-original-allocation-history\","
        "\"version\":1,\"start\":\"original_dol_entry\",\"scope\":\"vs_ownership\","
        "\"vs_target\":";
    if (!AppendNumber(&header, state_.vs_target) || !Append(&header, ",\"stop_at\":\"") ||
        !Append(&header, state_.stop_at_exit ? "exit" : "entry") ||
        !Append(&header, "\",\"max_events\":") ||
        !AppendNumber(&header, state_.max_events) ||
        !Append(&header, ",\"compression\":\"") ||
        !Append(&header, state_.gzip_output ? "gzip" : "none") ||
        !Append(&header, "\",\"writes_game_state\":false,\"profile_sha256\":\"") ||
        !Append(&header, state_.profile.profile_sha256) || !Append(&header, "\",\"initial_pc\":"))
    {
      Fail("allocation observer header exceeded its bound");
      return false;
    }
    if (!AppendNumber(&header, guest_pc) || !Append(&header, ",\"initial_sp\":") ||
        !AppendNumber(&header, state->gpr[1]) ||
        !Append(&header, ",\"observed_boot_context\":{\"arena_lo\":"))
    {
      Fail("allocation observer header exceeded its bound");
      return false;
    }
    if (!AppendNumber(&header, boot[1]) || !Append(&header, ",\"arena_hi\":") ||
        !AppendNumber(&header, boot[2]) || !Append(&header, ",\"memory_size\":") ||
        !AppendNumber(&header, boot[0]) || !Append(&header, ",\"bi2\":") ||
        !AppendNumber(&header, boot[3]) || !Append(&header, "}}"))
    {
      Fail("allocation observer header exceeded its bound");
      return false;
    }
    std::lock_guard lifecycle_lock(state_.lifecycle_mutex);
    if (state_.finishing || state_.started.load(std::memory_order_acquire) ||
        state_.invalid.load(std::memory_order_acquire))
    {
      Fail("allocation observer start raced with finish");
      return false;
    }
    // Dolphin is built with exceptions disabled. Match the existing observer
    // convention: an unavailable worker is a process-level startup failure,
    // while all stream errors after startup remain explicit in Error()/end.
    state_.writer = std::thread([this] { WriterMain(); });
    if (!Emit(std::move(header)))
      return false;
    // Publish the writer before started so Finish can never observe a started
    // session with an unassigned thread.
    state_.initialized.store(true, std::memory_order_release);
    state_.started.store(true, std::memory_order_release);
    return true;
  }

  bool Start(Core::System* system, PowerPC::PowerPCState* state)
  {
    return Start(system, state ? state->pc : 0, state);
  }

  bool Initialize(const BoundProfile& profile, Core::System* system,
                  PowerPC::PowerPCState* state, std::string_view output_path)
  {
    return Arm(profile, output_path) && Start(system, state ? state->pc : 0, state);
  }

  bool IsBoundary(u32 pc) const
  {
    if (!state_.armed.load() || state_.invalid.load() || !state_.profile.functions)
      return false;
    u32 index = 0;
    return pc == state_.profile.entry || FindEntry(state_.profile, pc, &index) ||
           FindReturn(state_.profile, pc, &index);
  }

  void Observe(Core::System* system, u32 pc, PowerPC::PowerPCState* state)
  {
    if (!state)
    {
      if (state_.armed.load(std::memory_order_acquire))
        Fail("allocation observer callback received a null CPU state");
      return;
    }
    CallbackScope callback(state_);
    if (!callback.Active())
      return;
    if (!state_.started.load())
    {
      if (pc != state_.profile.entry || !Start(system, pc, state))
        return;
    }
    if (pc == state_.profile.entry)
      return;
    u32 function_index = 0;
    const FunctionIdentity* function = FindEntry(state_.profile, pc, &function_index);
    const bool entering = function != nullptr;
    if (!function)
      function = FindReturn(state_.profile, pc, &function_index);
    if (!function)
      return;
    u32 instruction = 0;
    if (!ReadWord(system, pc, &instruction) ||
        instruction != (entering ? function->entry_word : kBlr))
      return Fail("allocation observer boundary word differs from the generated profile");

    const u32 thread = ReadWordOrFail(system, kThreadGlobal);
    if (state_.invalid.load())
      return;
    const u32 sp = state->gpr[1];
    const u32 lr = state->spr[8];
    Machine machine{};
    if (!CaptureMachine(system, pc, state, &machine))
      return;
    ThreadStack* stack = FindThread(thread, true);
    if (!stack)
      return Fail("allocation observer thread or depth bound exceeded");
    std::array<u32, 8> args{};
    u32 argc = 0;
    u32 call = 0;
    u32 parent = 0;
    bool has_parent = false;
    if (entering)
    {
      if (function->name && std::string_view(function->name) == "gm_Scene_Vs_OnEnter" &&
          (state_.active_owner || HasFunctionFrame(function_index)))
        return Fail("allocation observer VS entry is reentrant for an active source owner");
      if (function->name && std::string_view(function->name) == "gm_Scene_Vs_OnExit" &&
          !state_.active_owner)
        return Fail("allocation observer VS exit has no active source owner");
      if (function->name && std::string_view(function->name) == "gm_Scene_Vs_OnExit" &&
          HasFunctionFrame(function_index))
        return Fail("allocation observer VS exit is reentrant for an active source owner");
      argc = function->argc;
      for (u32 index = 0; index < argc; ++index)
        args[index] = state->gpr[3 + index];
      call = state_.call_count;
      if (stack->depth)
      {
        has_parent = true;
        parent = stack->frames[stack->depth - 1].call;
      }
      if (stack->depth && stack->frames[stack->depth - 1].function_index == function_index &&
          stack->frames[stack->depth - 1].sp == sp && stack->frames[stack->depth - 1].lr == lr)
        return Fail("allocation observer repeated entry lacks a source return");
    }
    else
    {
      if (!stack->depth)
        return Fail("allocation observer return has no source frame");
      const CallFrame& frame = stack->frames[stack->depth - 1];
      if (frame.function_index != function_index || frame.sp != sp || frame.lr != lr)
        return Fail("allocation observer return does not match function, sp, and lr");
      argc = frame.argc;
      args = frame.args;
      call = frame.call;
    }

    std::string observed;
    if (!BuildObserved(system, function->name, args, argc, entering ? nullptr : &state->gpr[3], state,
                        &observed))
      return Fail("allocation observer metadata escaped a bounded source identity");
    std::string machine_json;
    if (!AppendMachineJson(&machine_json, machine))
      return Fail("allocation observer machine record exceeded its bound");
    const std::string key = std::to_string(entering ? 1 : 2) + ":" +
                            std::to_string(function_index) + ":" + std::to_string(thread) + ":" +
                            std::to_string(sp) + ":" + std::to_string(lr);
    const std::string stop_state = machine_json + observed;
    if (state_.last_stop.valid && state_.last_stop.function_index == function_index &&
        state_.last_stop.key == key && state_.last_stop.state == stop_state)
    {
      if (++state_.last_stop.repeats > 32)
        return Fail("allocation observer repeated boundary did not make progress");
      ++state_.repeated_stops;
      std::string repeated = "{\"record\":\"repeated_stop\",\"original_sequence\":";
      if (!AppendNumber(&repeated, state_.last_stop.sequence) || Append(&repeated, ",\"function\":\"") == false ||
          !Append(&repeated, JsonEscape(function->name)) || Append(&repeated, "\",\"phase\":\"") == false ||
          !Append(&repeated, entering ? "enter" : "return") || Append(&repeated, "\",\"machine\":") == false ||
          !Append(&repeated, machine_json) || Append(&repeated, ",\"observed\":") == false ||
          !Append(&repeated, observed) || !Append(&repeated, "}"))
        return Fail("allocation observer repeated-stop record exceeded its bound");
      Emit(std::move(repeated));
      return;
    }

    std::string row = entering ? "{\"record\":\"enter\",\"call\":"
                               : "{\"record\":\"return\",\"call\":";
    if (!AppendNumber(&row, call) || !Append(&row, ",\"function\":\"") ||
        !Append(&row, JsonEscape(function->name)) || !Append(&row, "\",\"thread\":") ||
        !AppendNumber(&row, thread))
      return Fail("allocation observer event exceeded its bound");
    if (entering)
    {
      if (!Append(&row, ",\"parent\":") || (has_parent ? !AppendNumber(&row, parent) : !Append(&row, "null")) ||
          !Append(&row, ",\"sp\":") || !AppendNumber(&row, sp) || !Append(&row, ",\"lr\":") ||
          !AppendNumber(&row, lr) || !Append(&row, ",\"args\":["))
        return Fail("allocation observer enter record exceeded its bound");
      for (u32 index = 0; index < argc; ++index)
      {
        if (index && !Append(&row, ","))
          return Fail("allocation observer argument record exceeded its bound");
        if (!AppendNumber(&row, args[index]))
          return Fail("allocation observer argument record exceeded its bound");
      }
      if (!Append(&row, "]"))
        return Fail("allocation observer enter record exceeded its bound");
    }
    if (!Append(&row, ",\"machine\":") || !Append(&row, machine_json) ||
        !Append(&row, ",\"observed\":") || !Append(&row, observed))
      return Fail("allocation observer event exceeded its bound");
    if (!entering)
    {
      if (!Append(&row, ",\"result\":") || !AppendNumber(&row, state->gpr[3]))
        return Fail("allocation observer return record exceeded its bound");
    }
    if (!Append(&row, "}"))
      return Fail("allocation observer event exceeded its bound");
    const u64 sequence = state_.next_sequence.load(std::memory_order_relaxed);
    if (!Emit(std::move(row)))
      return;
    state_.last_stop = {true, sequence, function_index, key, stop_state, 0};
    if (entering)
    {
      if (stack->depth == kMaxDepth)
        return Fail("allocation observer call depth exceeded");
      stack->frames[stack->depth++] = {function_index, call, thread, sp, lr, args, argc,
                                       has_parent, parent};
      ++state_.call_count;
    }
    else
    {
      --stack->depth;
      if (function->name)
      {
        const std::string_view name(function->name);
        if ((name == "gm_Scene_Vs_OnEnter" || name == "gm_Scene_Vs_OnExit") &&
            !StacksEmpty())
          return Fail("allocation observer VS boundary returned with other source calls active");
        if (!StacksEmpty())
          return;
        if (name == "gm_Scene_Vs_OnEnter")
        {
          state_.boundary_complete = true;
          ++state_.vs_entries;
          state_.active_owner = true;
          if (!state_.stop_at_exit && state_.vs_entries >= state_.vs_target)
          {
            state_.target_complete = true;
            state_.natural.store(true);
            state_.finish_requested.store(true);
          }
        }
        else if (name == "gm_Scene_Vs_OnExit")
        {
          if (!state_.active_owner || state_.vs_exits >= state_.vs_entries)
            return Fail("allocation observer VS exit has no active source owner");
          ++state_.vs_exits;
          state_.active_owner = false;
          if (state_.stop_at_exit && state_.vs_exits >= state_.vs_target)
          {
            state_.ownership_complete = true;
            state_.target_complete = true;
            state_.natural.store(true);
            state_.finish_requested.store(true);
          }
        }
      }
    }
  }

  bool Finish(bool natural)
  {
    bool started = false;
    {
      std::unique_lock lifecycle_lock(state_.lifecycle_mutex);
      state_.finishing = true;
      state_.finish_requested.store(true, std::memory_order_release);
      state_.lifecycle_cv.wait(lifecycle_lock,
                               [this] { return state_.callbacks_in_flight == 0; });
      started = state_.started.load(std::memory_order_acquire);
      if (natural && !state_.target_complete)
        Fail("allocation observer natural finish precedes the requested VS ownership boundary");
      // A caller may perform ordinary teardown after a completed requested
      // boundary. Do not promote a partial repeated-ownership stream.
      state_.natural.store(state_.target_complete);
    }
    if (state_.writer.joinable())
      state_.writer.join();
    return started && !state_.invalid.load();
  }

  bool Initialized() const { return state_.started.load(); }

  std::string Error() const
  {
    std::lock_guard lock(state_.error_mutex);
    return state_.error;
  }

private:
  bool ConfigureCaptureScope()
  {
    state_.vs_target = 1;
    state_.stop_at_exit = false;
    state_.max_events = kMaxEvents;
    const char* target = std::getenv("MWRC_ALLOCATION_VS_TARGET");
    if (target && *target)
    {
      u32 value = 0;
      for (const unsigned char character : std::string_view(target))
      {
        if (character < '0' || character > '9' || value > 16)
        {
          Fail("MWRC_ALLOCATION_VS_TARGET must be a decimal count from 1 through 16");
          return false;
        }
        value = value * 10 + static_cast<u32>(character - '0');
      }
      if (value == 0 || value > 16)
      {
        Fail("MWRC_ALLOCATION_VS_TARGET must be a decimal count from 1 through 16");
        return false;
      }
      state_.vs_target = value;
    }
    const char* stop_at = std::getenv("MWRC_ALLOCATION_STOP_AT");
    if (stop_at && *stop_at)
    {
      if (std::string_view(stop_at) == "exit")
        state_.stop_at_exit = true;
      else if (std::string_view(stop_at) != "entry")
      {
        Fail("MWRC_ALLOCATION_STOP_AT must be entry or exit");
        return false;
      }
    }
    const char* max_events = std::getenv("MWRC_ALLOCATION_MAX_EVENTS");
    if (max_events && *max_events)
    {
      u64 value = 0;
      for (const unsigned char character : std::string_view(max_events))
      {
        if (character < '0' || character > '9' ||
            value > (kMaxConfiguredEvents - static_cast<u64>(character - '0')) / 10)
        {
          Fail("MWRC_ALLOCATION_MAX_EVENTS must be a decimal count from 1 through 100000000");
          return false;
        }
        value = value * 10 + static_cast<u64>(character - '0');
      }
      if (value == 0 || value > kMaxConfiguredEvents)
      {
        Fail("MWRC_ALLOCATION_MAX_EVENTS must be a decimal count from 1 through 100000000");
        return false;
      }
      state_.max_events = value;
    }
    return true;
  }

  bool ValidateProfile() const
  {
    const BoundProfile& profile = state_.profile;
    if (!profile.generated_and_verified || profile.function_count != kFunctionCount ||
        profile.global_count != kGlobalCount || !profile.functions || !profile.globals ||
        std::string_view(profile.dol_sha1 ? profile.dol_sha1 : "") != kExpectedDolSha1 ||
        std::string_view(profile.source_revision ? profile.source_revision : "") !=
            kExpectedSourceRevision ||
        !IsHex256(profile.profile_sha256 ? profile.profile_sha256 : "") ||
        !profile.entry || profile.entry % 4 || !IsMem1Range(profile.entry, 4) ||
        !profile.entry_word)
      return false;
    for (u32 index = 0; index < profile.function_count; ++index)
    {
      const FunctionIdentity& function = profile.functions[index];
      if (!function.name || !function.body_sha256 || !IsHex256(function.body_sha256) ||
          !function.address || function.address % 4 || !function.size || function.size % 4 ||
          !IsMem1Range(function.address, function.size) ||
          function.return_count == 0 || function.return_count > 256 || !function.returns ||
          function.argc > 8)
        return false;
      for (u32 return_index = 0; return_index < function.return_count; ++return_index)
      {
        const u32 address = function.returns[return_index];
        if (!address || address % 4 || address < function.address ||
            static_cast<u64>(address) + 4 > static_cast<u64>(function.address) + function.size)
          return false;
      }
      for (u32 other = index + 1; other < profile.function_count; ++other)
      {
        if (std::string_view(function.name) == profile.functions[other].name ||
            function.address == profile.functions[other].address)
          return false;
      }
    }
    for (u32 index = 0; index < profile.global_count; ++index)
    {
      const GlobalIdentity& global = profile.globals[index];
      if (!global.name || !global.address || !global.size || !IsMem1Range(global.address, global.size))
        return false;
      for (u32 other = index + 1; other < profile.global_count; ++other)
      {
        if (std::string_view(global.name) == profile.globals[other].name)
          return false;
      }
    }
    return true;
  }

  bool ValidateBodyHashes(Core::System* system)
  {
    size_t reads = 0;
    std::array<u8, 256> chunk{};
    for (u32 index = 0; index < state_.profile.function_count; ++index)
    {
      const FunctionIdentity& function = state_.profile.functions[index];
      mbedtls_sha256_context digest_context;
      mbedtls_sha256_init(&digest_context);
      const auto cleanup = [&] { mbedtls_sha256_free(&digest_context); };
      if (mbedtls_sha256_starts_ret(&digest_context, 0) != 0)
      {
        cleanup();
        return false;
      }
      for (u32 offset = 0; offset < function.size;)
      {
        const size_t count = std::min<size_t>(chunk.size(), function.size - offset);
        if (++reads > kMaxProfileHashReads || !ReadBytes(system, function.address + offset, count,
                                                         chunk.data()) ||
            mbedtls_sha256_update_ret(&digest_context, chunk.data(), count) != 0)
        {
          cleanup();
          return false;
        }
        offset += static_cast<u32>(count);
      }
      std::array<u8, 32> digest{};
      const bool finished = mbedtls_sha256_finish_ret(&digest_context, digest.data()) == 0;
      cleanup();
      if (!finished || std::string_view(function.body_sha256) != HexDigest(digest))
        return false;
    }
    return true;
  }

  bool ReadBytes(Core::System* system, u32 address, size_t size, u8* destination) const
  {
    if (!system || !IsMem1Read(address, size))
      return false;
    const auto* source = system->GetMemory().GetPointerForRange(address, size);
    if (!source)
      return false;
    std::memcpy(destination, source, size);
    return true;
  }

  bool ReadWord(Core::System* system, u32 address, u32* value) const
  {
    std::array<u8, 4> bytes{};
    if (!ReadBytes(system, address, bytes.size(), bytes.data()))
      return false;
    *value = (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
             (static_cast<u32>(bytes[2]) << 8) | bytes[3];
    return true;
  }

  u32 ReadWordOrFail(Core::System* system, u32 address)
  {
    u32 result = 0;
    if (!ReadWord(system, address, &result))
      Fail("allocation observer metadata read escaped bounded MEM1");
    return result;
  }

  bool CaptureMachine(Core::System* system, u32 pc, PowerPC::PowerPCState* state,
                      Machine* machine)
  {
    machine->pc = pc;
    machine->lr = state->spr[8];
    machine->ctr = state->spr[9];
    machine->cr = state->cr.Get();
    machine->gpr = {};
    for (size_t index = 0; index < machine->gpr.size(); ++index)
      machine->gpr[index] = state->gpr[index];
    if (!ReadWord(system, kSceneFrameGlobal, &machine->scene_frame) ||
        !ReadWord(system, kRetraceGlobal, &machine->retrace_count))
    {
      Fail("allocation observer machine metadata escaped bounded MEM1");
      return false;
    }
    return true;
  }

  ThreadStack* FindThread(u32 thread, bool create)
  {
    for (ThreadStack& stack : state_.threads)
    {
      if (stack.used && stack.thread == thread)
        return &stack;
    }
    if (!create)
      return nullptr;
    for (ThreadStack& stack : state_.threads)
    {
      if (!stack.used)
      {
        stack.used = true;
        stack.thread = thread;
        return &stack;
      }
    }
    return nullptr;
  }

  bool StacksEmpty() const
  {
    return std::all_of(state_.threads.begin(), state_.threads.end(),
                       [](const ThreadStack& stack) { return stack.depth == 0; });
  }

  bool HasFunctionFrame(u32 function_index) const
  {
    return std::any_of(state_.threads.begin(), state_.threads.end(),
                       [function_index](const ThreadStack& stack) {
                         return std::any_of(stack.frames.begin(), stack.frames.begin() + stack.depth,
                                            [function_index](const CallFrame& frame) {
                                              return frame.function_index == function_index;
                                            });
                       });
  }

  bool ReadGlobalWord(Core::System* system, std::string_view name, u32* value) const
  {
    const GlobalIdentity* global = FindGlobal(state_.profile, name);
    return global && global->size >= 4 && ReadWord(system, global->address, value);
  }

  bool ReadGlobalOffset(Core::System* system, std::string_view name, u32 offset, u32* value) const
  {
    const GlobalIdentity* global = FindGlobal(state_.profile, name);
    if (!global || offset > global->size || global->size - offset < sizeof(u32) ||
        static_cast<u64>(global->address) + offset > std::numeric_limits<u32>::max())
      return false;
    return ReadWord(system, global->address + offset, value);
  }

  bool AppendHandleMetadata(Core::System* system, u32 handle, bool include_prev,
                            std::string* output)
  {
    if (handle == 0)
      return Append(output, "null");
    std::array<u8, 16> bytes{};
    const size_t size = include_prev ? 16 : 12;
    if (!IsMem1Read(handle, size) || !ReadBytes(system, handle, size, bytes.data()))
      return false;
    const auto word = [&](size_t index) {
      return (static_cast<u32>(bytes[index * 4]) << 24) |
             (static_cast<u32>(bytes[index * 4 + 1]) << 16) |
             (static_cast<u32>(bytes[index * 4 + 2]) << 8) | bytes[index * 4 + 3];
    };
    if (!Append(output, "{\"pointer\":") || !AppendNumber(output, handle) ||
        !Append(output, ",\"x0_next\":") || !AppendNumber(output, word(0)) ||
        !Append(output, ",\"x4_lo\":") || !AppendNumber(output, word(1)) ||
        !Append(output, ",\"x8_hi\":") || !AppendNumber(output, word(2)))
      return false;
    if (include_prev &&
        (!Append(output, ",\"xC_prev\":") || !AppendNumber(output, word(3))))
      return false;
    return Append(output, "}");
  }

  bool AppendCompactionManager(Core::System* system, std::string* output)
  {
    constexpr std::array<std::pair<std::string_view, u32>, 9> fields = {{
        {"src", 0x6c8}, {"dst", 0x6cc}, {"size", 0x6d0}, {"offset", 0x6d4},
        {"callback_arg", 0x6d8}, {"callback", 0x6dc}, {"x6E0", 0x6e0},
        {"x6E4", 0x6e4}, {"x6E8", 0x6e8},
    }};
    std::array<u32, fields.size()> values{};
    for (size_t index = 0; index < fields.size(); ++index)
    {
      if (!ReadGlobalOffset(system, "lbMemory_804318B0", fields[index].second, &values[index]))
        return false;
    }
    const u32 size = values[2];
    const u32 offset = values[3];
    if (size != 0 && offset > size)
      return false;
    const u32 remaining = size == 0 ? 0 : size - offset;
    const u32 chunk = std::min<u32>(remaining, 0x19000);
    if (!Append(output, "{\"src\":") || !AppendNumber(output, values[0]) ||
        !Append(output, ",\"dst\":") || !AppendNumber(output, values[1]) ||
        !Append(output, ",\"size\":") || !AppendNumber(output, size) ||
        !Append(output, ",\"offset\":") || !AppendNumber(output, offset) ||
        !Append(output, ",\"remaining\":") || !AppendNumber(output, remaining) ||
        !Append(output, ",\"chunk\":") || !AppendNumber(output, chunk) ||
        !Append(output, ",\"callback_arg\":") || !AppendNumber(output, values[4]) ||
        !Append(output, ",\"callback\":") || !AppendNumber(output, values[5]) ||
        !Append(output, ",\"x6E0\":") || !AppendNumber(output, values[6]) ||
        !Append(output, ",\"x6E4\":") || !AppendNumber(output, values[7]) ||
        !Append(output, ",\"x6E8\":") || !AppendNumber(output, values[8]))
      return false;
    return Append(output, "}");
  }

  bool AppendCompactionObserved(Core::System* system, std::string_view function,
                                const std::array<u32, 8>& args, u32 argc,
                                const u32* return_value, std::string* output)
  {
    const bool is_memory = function == "lbMemory_8001529C" || function == "lbMemory_80015320";
    const bool is_alarm = function == "fn_80015184";
    const bool is_preload = function == "lbDvd_80017A80";
    if (!is_memory && !is_alarm && !is_preload)
      return false;
    const u32 expected_argc = function == "lbMemory_8001529C" ? 3 :
                              function == "lbMemory_80015320" ? 4 :
                              function == "fn_80015184" ? 2 : 1;
    if (argc != expected_argc || !Append(output, "{\"phase\":\""))
      return false;
    if (!Append(output, return_value ? "return" : "entry") || !Append(output, "\""))
      return false;
    if (return_value)
    {
      if (!Append(output, ",\"return\":") || !AppendNumber(output, *return_value))
        return false;
    }
    if (is_memory)
    {
      if (!Append(output, ",\"manager\":") || !AppendCompactionManager(system, output))
        return false;
      const u32 handle = function == "lbMemory_8001529C" ? args[0] : args[1];
      if (!Append(output, ",\"handle\":") ||
          !AppendHandleMetadata(system, handle, function == "lbMemory_8001529C", output))
        return false;
      if (function == "lbMemory_8001529C")
      {
        if (!Append(output, ",\"callback\":") || !AppendNumber(output, args[1]) ||
            !Append(output, ",\"callback_arg\":") || !AppendNumber(output, args[2]))
          return false;
      }
      else
      {
        if (args[3] > 1)
          return false;
        if (!Append(output, ",\"callback_arg\":") || !AppendNumber(output, args[2]) ||
            !Append(output, ",\"cancel\":") || !Append(output, args[3] ? "true" : "false"))
          return false;
      }
    }
    else if (is_alarm)
    {
      if (!Append(output, ",\"manager\":") || !AppendCompactionManager(system, output) ||
          !Append(output, ",\"alarm\":") || !AppendNumber(output, args[0]) ||
          !Append(output, ",\"context\":") || !AppendNumber(output, args[1]))
        return false;
    }
    else if (!Append(output, ",\"callback_arg\":") || !AppendNumber(output, args[0]))
    {
      return false;
    }
    return Append(output, "}");
  }

  bool AppendDevComObserved(std::string_view function, const std::array<u32, 8>& args, u32 argc,
                            const u32* return_value, std::string* output)
  {
    const bool request = function == "HSD_DevComRequest";
    const bool callback = function == "HSD_DevComARAMCallback";
    if (!request && !callback)
      return false;
    if (argc != (request ? 8 : 1) || !Append(output, "{\"phase\":\""))
      return false;
    if (!Append(output, return_value ? "return" : "entry") || !Append(output, "\""))
      return false;
    if (return_value && (!Append(output, ",\"return\":") || !AppendNumber(output, *return_value)))
      return false;
    if (request)
    {
      constexpr std::array<std::string_view, 8> names = {
          "file", "src", "dest", "size", "type", "priority", "callback", "callback_arg"};
      for (size_t index = 0; index < names.size(); ++index)
      {
        if (!Append(output, ",\"") || !Append(output, names[index]) || !Append(output, "\":") ||
            !AppendNumber(output, args[index]))
          return false;
      }
    }
    else if (!Append(output, ",\"request\":") || !AppendNumber(output, args[0]))
    {
      return false;
    }
    return Append(output, "}");
  }

  bool AppendObjectField(std::string* output, bool* first, std::string_view name)
  {
    if (!*first && !Append(output, ","))
      return false;
    *first = false;
    return Append(output, "\"") && Append(output, name) && Append(output, "\":");
  }

  bool AppendWordObject(Core::System* system, const std::array<std::string_view, 5>& keys,
                        std::string* output)
  {
    if (!Append(output, "{"))
      return false;
    bool object_first = true;
    for (std::string_view key : keys)
    {
      u32 value = 0;
      if (!ReadGlobalWord(system, key, &value) || !AppendObjectField(output, &object_first, key) ||
          !AppendNumber(output, value))
        return false;
    }
    return Append(output, "}");
  }

  bool AppendWords(std::string* output, const u8* bytes, size_t count)
  {
    if (!Append(output, "["))
      return false;
    for (size_t index = 0; index < count; ++index)
    {
      if (index && !Append(output, ","))
        return false;
      const u32 value = (static_cast<u32>(bytes[index * 4]) << 24) |
                        (static_cast<u32>(bytes[index * 4 + 1]) << 16) |
                        (static_cast<u32>(bytes[index * 4 + 2]) << 8) | bytes[index * 4 + 3];
      if (!AppendNumber(output, value))
        return false;
    }
    return Append(output, "]");
  }

  bool BuildObserved(Core::System* system, const char* name, const std::array<u32, 8>& args,
                     u32 argc, const u32* return_value, PowerPC::PowerPCState* state,
                     std::string* output)
  {
    *output = "{";
    bool first = true;
    const auto add = [&](std::string_view field, auto&& body) {
      if (!AppendObjectField(output, &first, field))
        return false;
      return body();
    };
    const std::string_view function(name ? name : "");
    if (function == "lbMemory_8001529C" || function == "lbMemory_80015320" ||
        function == "fn_80015184" || function == "lbDvd_80017A80")
    {
      if (!add("compaction", [&] {
            return AppendCompactionObserved(system, function, args, argc, return_value, output);
          }))
        return false;
    }
    if (function == "HSD_DevComARAMCallback" || function == "HSD_DevComRequest")
    {
      if (!add("devcom", [&] {
            return AppendDevComObserved(function, args, argc, return_value, output);
          }))
        return false;
    }
    if (function == "ARInit" || function == "ARAlloc" || function == "ARFree" ||
        function == "ARGetSize")
    {
      if (!add("aram", [&] {
            return AppendWordObject(system,
                                     {"__AR_Size", "__AR_StackPointer", "__AR_FreeBlocks",
                                      "__AR_BlockLength", "__AR_init_flag"},
                                     output);
          }))
        return false;
    }
    if (function == "HSD_ObjAllocInit" || function == "HSD_ObjAllocAddFree" ||
        function == "HSD_ObjAlloc" || function == "HSD_ObjFree")
    {
      if (argc == 0)
        return false;
      std::array<u8, 44> pool{};
      if (!ReadBytes(system, args[0], pool.size(), pool.data()) ||
          !add("pool_words", [&] { return AppendWords(output, pool.data(), 11); }))
        return false;
    }
    if (function == "HSD_ObjSetHeap" || function == "HSD_ObjAllocAddFree" ||
        function == "HSD_ObjAlloc")
    {
      std::array<u8, 16> object_heap{};
      const GlobalIdentity* global = FindGlobal(state_.profile, "obj_heap");
      if (!global || !ReadBytes(system, global->address, object_heap.size(), object_heap.data()) ||
          !add("object_heap_words", [&] { return AppendWords(output, object_heap.data(), 4); }))
        return false;
    }
    if (return_value && (function == "OSInitAlloc" || function == "OSCreateHeap" ||
                         function == "OSDestroyHeap" || function == "OSAllocFromHeap" ||
                         function == "OSFreeToHeap"))
    {
      constexpr std::array<std::string_view, 8> heap_keys = {
          "HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd", "__OSCurrHeap",
          "current_heap", "__OSArenaLo", "__OSArenaHi"};
      std::array<u32, heap_keys.size()> heap_values{};
      for (size_t index = 0; index < heap_keys.size(); ++index)
      {
        if (!ReadGlobalWord(system, heap_keys[index], &heap_values[index]))
          return false;
      }
      u32 heap_array = 0;
      u32 heap_count = 0;
      heap_array = heap_values[0];
      heap_count = heap_values[1];
      if (heap_count > kMaxDescriptors ||
          !IsMem1Range(heap_array, heap_count * 12))
        return false;
      if (!add("heaps", [&] {
            if (!Append(output, "{"))
              return false;
            for (size_t index = 0; index < heap_keys.size(); ++index)
            {
              if (index && !Append(output, ","))
                return false;
              if (!Append(output, "\"") || !Append(output, heap_keys[index]) ||
                  !Append(output, "\":") || !AppendNumber(output, heap_values[index]))
                return false;
            }
            if (!Append(output, ",\"descriptors\":["))
              return false;
            for (u32 index = 0; index < heap_count; ++index)
            {
              std::array<u8, 12> descriptor{};
              if (!ReadBytes(system, heap_array + index * 12, descriptor.size(), descriptor.data()))
                return false;
              if (index && !Append(output, ","))
                return false;
              if (!AppendWords(output, descriptor.data(), 3))
                return false;
            }
            return Append(output, "]}");
          }))
        return false;
    }
    if (return_value && function.size() >= 7 && function.substr(0, 7) == "lbHeap_")
    {
      std::array<u8, 184> heap_words{};
      const GlobalIdentity* global = FindGlobal(state_.profile, "lbHeap_80431FA0");
      if (!global || !ReadBytes(system, global->address, heap_words.size(), heap_words.data()) ||
          !add("game_heap_words", [&] { return AppendWords(output, heap_words.data(), 46); }))
        return false;
    }
    if (return_value && (function == "lbMemory_80014E24" || function == "lbMemory_80014FC8" ||
                         function == "lbMemory_800154D4") && *return_value)
    {
      const size_t count = function == "lbMemory_80014FC8" ? 3 : 4;
      std::array<u8, 16> handle_words{};
      if (!ReadBytes(system, *return_value, count * 4, handle_words.data()) ||
          !add("handle_words", [&] { return AppendWords(output, handle_words.data(), count); }))
        return false;
    }
    if (return_value && (function == "Fighter_Create" || function == "ftDemo_CreateFighter") &&
        *return_value)
    {
      u32 fighter = 0;
      u8 slot = 0;
      u32 kind = 0;
      if (!IsMem1Range(*return_value, 0x30) ||
          !ReadWord(system, *return_value + 0x2c, &fighter) || !IsMem1Range(fighter, 0xd) ||
          !ReadBytes(system, fighter + 0xc, 1, &slot) || !ReadWord(system, fighter + 4, &kind) ||
          !add("fighter", [&] {
            return Append(output, "{\"gobj\":") && AppendNumber(output, *return_value) &&
                   Append(output, ",\"address\":") && AppendNumber(output, fighter) &&
                   Append(output, ",\"slot\":") && AppendNumber(output, slot) &&
                   Append(output, ",\"kind\":") && AppendNumber(output, kind) && Append(output, "}");
          }))
        return false;
    }
    if (return_value && function == "gm_Scene_Vs_OnEnter")
    {
      if (!add("globals", [&] {
            constexpr std::array<std::string_view, 5> keys = {"seed_ptr", "ArenaStart", "ArenaEnd",
                                                               "HeapArray", "NumHeaps"};
            if (!Append(output, "{"))
              return false;
            bool object_first = true;
            for (const std::string_view key : keys)
            {
              u32 value = 0;
              if (!ReadGlobalWord(system, key, &value) ||
                  !AppendObjectField(output, &object_first, key) || !AppendNumber(output, value))
                return false;
            }
            return Append(output, "}");
          }) ||
          !add("r2", [&] { return AppendNumber(output, state->gpr[2]); }) ||
          !add("r13", [&] { return AppendNumber(output, state->gpr[13]); }))
        return false;
    }
    return Append(output, "}");
  }

  bool Emit(std::string row)
  {
    if (row.size() > kMaxPayload)
    {
      Fail("allocation observer event budget or payload bound exceeded");
      return false;
    }
    const u64 head = state_.head.load(std::memory_order_relaxed);
    if (state_.head.load(std::memory_order_relaxed) - state_.tail.load(std::memory_order_acquire) >=
        kRingSize)
    {
      Fail("allocation observer ring overflow");
      return false;
    }
    Slot& slot = state_.ring[head % kRingSize];
    if (slot.ready.load(std::memory_order_acquire))
    {
      Fail("allocation observer ring slot was not released");
      return false;
    }
    const size_t brace = row.find('{');
    if (brace == std::string::npos)
    {
      Fail("allocation observer record has no bounded JSON object");
      return false;
    }
    while (true)
    {
      const u64 sequence = state_.next_sequence.load(std::memory_order_relaxed);
      if (sequence >= state_.max_events)
      {
        Fail("allocation observer event budget or payload bound exceeded");
        return false;
      }
      const std::string marker = "\"sequence\":" + std::to_string(sequence) + ",";
      if (marker.size() > kMaxPayload - row.size())
      {
        Fail("allocation observer record has no bounded JSON object");
        return false;
      }
      // Insert before reserving so the queue bound accounts for the string's
      // retained allocation, not only its JSON length. The writer later
      // swaps the string with an empty one before releasing the slot.
      row.insert(brace + 1, marker);
      const size_t queued_size = row.capacity() + 1;  // include the newline
      if (queued_size > kMaxQueuedBytes)
      {
        row.erase(brace + 1, marker.size());
        Fail("allocation observer queued byte bound exceeded");
        return false;
      }
      size_t queued = state_.queued_bytes.load(std::memory_order_relaxed);
      while (true)
      {
        if (queued > kMaxQueuedBytes - queued_size)
        {
          Fail("allocation observer queued byte bound exceeded");
          return false;
        }
        if (state_.queued_bytes.compare_exchange_weak(queued, queued + queued_size,
                                                      std::memory_order_acq_rel,
                                                      std::memory_order_relaxed))
          break;
      }
      u64 expected_sequence = sequence;
      if (!state_.next_sequence.compare_exchange_strong(expected_sequence, sequence + 1,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_relaxed))
      {
        state_.queued_bytes.fetch_sub(queued_size, std::memory_order_release);
        row.erase(brace + 1, marker.size());
        continue;
      }
      slot.payload = std::move(row);
      slot.queued_size = queued_size;
      slot.ready.store(true, std::memory_order_release);
      state_.head.fetch_add(1, std::memory_order_release);
      return true;
    }
  }

  bool EmitWriter(std::string row, OutputStream& output)
  {
    if (row.size() > kMaxPayload)
      return false;
    const u64 sequence = state_.next_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string marker = "\"sequence\":" + std::to_string(sequence) + ",";
    const size_t brace = row.find('{');
    if (brace == std::string::npos)
      return false;
    row.insert(brace + 1, marker);
    row.push_back('\n');
    return output.Write(reinterpret_cast<const u8*>(row.data()), row.size());
  }

  std::string PendingJson() const
  {
    std::string result = "[";
    bool first = true;
    for (const ThreadStack& stack : state_.threads)
    {
      for (size_t index = 0; index < stack.depth; ++index)
      {
        if (!first)
          result += ",";
        first = false;
        const CallFrame& frame = stack.frames[index];
        result += "{\"call\":" + std::to_string(frame.call) + ",\"function\":\"" +
                  JsonEscape(FunctionAt(state_.profile, frame.function_index)->name) +
                  "\",\"thread\":" + std::to_string(frame.thread) + ",\"sp\":" +
                  std::to_string(frame.sp) + ",\"lr\":" + std::to_string(frame.lr) +
                  ",\"parent\":" +
                  (frame.has_parent ? std::to_string(frame.parent) : "null") + ",\"args\":[";
        for (u32 arg = 0; arg < frame.argc; ++arg)
        {
          if (arg)
            result += ",";
          result += std::to_string(frame.args[arg]);
        }
        result += "]}";
      }
    }
    result += "]";
    return result;
  }

  std::string ErrorText() const
  {
    std::lock_guard lock(state_.error_mutex);
    return state_.error;
  }

  void Fail(std::string_view reason)
  {
    bool expected = false;
    if (state_.invalid.compare_exchange_strong(expected, true))
    {
      std::lock_guard lock(state_.error_mutex);
      state_.error = std::string(reason);
    }
    state_.finish_requested.store(true);
  }

  void WriterMain()
  {
    OutputStream output(state_.output_path, state_.gzip_output);
    if (!output.IsOpen())
      Fail("allocation observer output could not be created exclusively");
    std::array<char, kWriterBatchBytes> batch{};
    size_t batch_size = 0;
    const auto flush_batch = [&]() {
      if (batch_size == 0)
        return true;
      if (!output.IsOpen())
      {
        batch_size = 0;
        return false;
      }
      const bool written = output.Write(reinterpret_cast<const u8*>(batch.data()), batch_size);
      batch_size = 0;
      if (!written)
      {
        Fail("allocation observer output write failed");
        if (!output.Close())
          Fail("allocation observer output close failed after a write failure");
      }
      else if (!output.Flush())
      {
        Fail("allocation observer output flush failed");
        if (!output.Close())
          Fail("allocation observer output close failed after a flush failure");
      }
      return written;
    };
    const auto append_batch = [&](const char* data, size_t size) {
      if (size > kWriterBatchBytes)
        return false;
      if (batch_size > kWriterBatchBytes - size && !flush_batch())
        return false;
      if (!output.IsOpen())
        return false;
      std::memcpy(batch.data() + batch_size, data, size);
      batch_size += size;
      return true;
    };
    bool error_written = false;
    while (true)
    {
      bool drained = false;
      u64 tail = state_.tail.load(std::memory_order_relaxed);
      while (tail < state_.head.load(std::memory_order_acquire))
      {
        Slot& slot = state_.ring[tail % kRingSize];
        if (!slot.ready.load(std::memory_order_acquire))
          break;
        if (output.IsOpen())
        {
          if (slot.payload.size() > kWriterBatchBytes - 1 ||
              !append_batch(slot.payload.data(), slot.payload.size()) ||
              !append_batch("\n", 1))
          {
            Fail("allocation observer output batch exceeded its bound or could not be written");
            if (output.IsOpen() && !output.Close())
              Fail("allocation observer output close failed after a write failure");
          }
        }
        const size_t queued_size = slot.queued_size;
        std::string{}.swap(slot.payload);
        slot.queued_size = 0;
        state_.queued_bytes.fetch_sub(queued_size, std::memory_order_release);
        slot.ready.store(false, std::memory_order_release);
        ++tail;
        state_.tail.store(tail, std::memory_order_release);
        drained = true;
      }
      bool callbacks_idle = false;
      {
        std::lock_guard lifecycle_lock(state_.lifecycle_mutex);
        callbacks_idle = state_.callbacks_in_flight == 0;
      }
      const bool finished = state_.finish_requested.load(std::memory_order_acquire) &&
                            tail >= state_.head.load(std::memory_order_acquire) && callbacks_idle;
      if (finished)
      {
        const bool pending = std::any_of(state_.threads.begin(), state_.threads.end(),
                                         [](const ThreadStack& stack) { return stack.depth != 0; });
        if (pending)
          Fail("allocation observer finished with pending source calls");
        if (output.IsOpen())
        {
          if (!flush_batch())
            Fail("allocation observer output batch flush failed before final record");
        }
        if (state_.invalid.load() && !error_written && output.IsOpen())
        {
          std::string error = "{\"record\":\"error\",\"error\":\"" +
                              JsonEscape(ErrorText()) + "\"}";
          error_written = EmitWriter(std::move(error), output);
          if (!error_written)
            Fail("allocation observer error record could not be written");
        }
        const bool complete = state_.natural.load() && state_.target_complete &&
                              !state_.invalid.load() && !pending;
        if (output.IsOpen())
        {
          if (!output.Flush())
            Fail("allocation observer output flush failed before final record");
          std::string end = "{\"record\":\"end\",\"status\":\"";
          const bool final_complete = complete && !state_.invalid.load();
          const bool incomplete = state_.invalid.load() || !state_.boundary_complete ||
                                  !state_.target_complete;
          end += final_complete ? "captured" : (incomplete ? "incomplete" : "error");
          end += "\",\"error\":";
          const std::string error = ErrorText();
          if (error.empty())
            end += "null";
          else
            end += "\"" + JsonEscape(error) + "\"";
          end += ",\"scope\":\"vs_ownership\",\"vs_target\":" +
                 std::to_string(state_.vs_target) + ",\"stop_at\":\"" +
                 (state_.stop_at_exit ? "exit" : "entry") + "\",\"max_events\":" +
                 std::to_string(state_.max_events) + ",\"compression\":\"" +
                 (state_.gzip_output ? "gzip" : "none") + "\",\"target_complete\":" +
                 (state_.target_complete ? "true" : "false") + ",\"vs_entries\":" +
                 std::to_string(state_.vs_entries) + ",\"vs_exits\":" +
                 std::to_string(state_.vs_exits) + ",\"active_owner\":" +
                 (state_.active_owner ? "true" : "false") + ",\"pending_calls\":" + PendingJson() +
                 ",\"calls\":" +
                 std::to_string(state_.call_count) + ",\"repeated_stops\":" +
                 std::to_string(state_.repeated_stops) + ",\"boundary_complete\":" +
                 (state_.boundary_complete ? "true" : "false") +
                 ",\"ownership_complete\":" +
                 (state_.ownership_complete ? "true" : "false") + "}";
          if (!EmitWriter(std::move(end), output))
            Fail("allocation observer final record could not be written");
          if (!output.Finish())
            Fail("allocation observer output finalization failed");
        }
        return;
      }
      if (!drained)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  State& state_ = GetState();
};

}  // namespace

bool Observer::Arm(const BoundProfile& profile, std::string_view output_path)
{
  return Backend::Instance().Arm(profile, output_path);
}

bool Observer::Start(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  return Backend::Instance().Start(system, guest_pc, state);
}

bool Observer::Start(Core::System* system, PowerPC::PowerPCState* state)
{
  return Backend::Instance().Start(system, state);
}

bool Observer::Initialize(const BoundProfile& profile, Core::System* system,
                          PowerPC::PowerPCState* state, std::string_view output_path)
{
  return Backend::Instance().Initialize(profile, system, state, output_path);
}

bool Observer::IsBoundary(u32 guest_pc)
{
  return Backend::Instance().IsBoundary(guest_pc);
}

void Observer::Observe(Core::System* system, u32 guest_pc, PowerPC::PowerPCState* state)
{
  Backend::Instance().Observe(system, guest_pc, state);
}

bool Observer::Finish(bool natural)
{
  return Backend::Instance().Finish(natural);
}

bool Observer::IsInitialized()
{
  return Backend::Instance().Initialized();
}

std::string Observer::Error()
{
  return Backend::Instance().Error();
}

}  // namespace ReferenceAllocation
