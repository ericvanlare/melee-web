// Bounded source-AI oracle prototype. The generated ai_source_generated.c is
// the pinned ai.c with only unreachable callback-stack assembly replaced by a
// fail-closed C stub. This host proxy owns no DSP, PCM, or audio-device
// behavior and makes no hardware timing claim.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dolphin/types.h>
#include <dolphin/os.h>
#include <dolphin/hw_regs.h>
#include <dolphin/ai.h>

static_assert(sizeof(u8) == 1, "source u8 ABI must be one byte");
static_assert(sizeof(u16) == 2, "source u16 ABI must be two bytes");
static_assert(sizeof(u32) == 4, "source u32 ABI must be four bytes");
static_assert(sizeof(BOOL) == 4, "source BOOL ABI must be four bytes");
static_assert(sizeof(OSTime) == 8, "source OSTime ABI must be eight bytes");
static_assert(sizeof(void*) == 4, "Wasm32 pointer ABI is required");
static_assert(sizeof(AIDCallback) == sizeof(void*),
              "source callback pointer ABI must be Wasm32");

namespace melee_web_ai_probe {

constexpr u32 kBusClockHz = 162000000u;
constexpr u32 kCpuClockHz = 486000000u;
constexpr u32 kOsTimerHz = kBusClockHz / 4u;
// Reference Dolphin's fixed-point source-rate conversion uses the CPU sample
// rate numerator (54 MHz * stereo channels), with GameCube AI divisors below.
// This virtual clock is a deterministic service model, not browser timing.
constexpr u32 kFixedSampleRateDividend = 54000000u * 2u;
constexpr u32 kGc48KhzDivisor = 2248u;
constexpr u32 kGc32KhzDivisor = kGc48KhzDivisor * 3u / 2u;
constexpr OSTime kServiceTickQuantum = 64;
constexpr OSTime kVirtualTimeLimit = 200000;
constexpr u32 kTimeReadLimit = 100000;
constexpr u32 kMmioAccessLimit = 250000;
constexpr u32 kDmaBytes = 0x280u;
constexpr u32 kDefaultAiControl = 0x42u;  // AISFR48k + AIDFR32k.
constexpr u32 kAiControlSupportedMask = 0x67u;  // PSTAT/AISFR/AIINT/SCRESET/AIDFR.
constexpr u32 kStalledClockReadLimit = 64u;

[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr, "AI probe contract failure: %s\n", reason);
  std::abort();
}

static u32 g_mmio_accesses;
static bool g_calibration_mode;
static bool g_stalled_clock;
static bool g_unowned_dma;
static u32 g_stalled_clock_reads;

void account_mmio(const char* operation) {
  if (++g_mmio_accesses > kMmioAccessLimit) fail(operation);
}

struct AiRegisterBank;

struct AiWord {
  AiRegisterBank* bank;
  unsigned index;

  operator u32() const;
  AiWord& operator=(u32 value);
  AiWord& operator|=(u32 value);
};

struct AiRegisterBank {
  u32 values[4]{};
  u32 sample_reads = 0;
  u32 sample_advances = 0;
  OSTime last_sample_time = 0;
  u64 sample_remainder = 0;

  AiWord operator[](unsigned index) {
    if (index >= 4) fail("unknown AI register index");
    return AiWord{this, index};
  }
};

struct DspRegisterBank;

struct DspWord {
  DspRegisterBank* bank;
  unsigned index;

  operator u32() const;
  DspWord& operator=(u32 value);
  DspWord& operator|=(u32 value);
};

struct DspRegisterBank {
  u16 values[64]{};

  DspWord operator[](unsigned index) {
    if (index != 5 && index != 24 && index != 25 && index != 27 && index != 29)
      fail("unknown DSP register index");
    return DspWord{this, index};
  }

  u16 read(unsigned index) {
    if (index != 5 && index != 24 && index != 25 && index != 27 && index != 29)
      fail("unknown DSP register read index");
    account_mmio("DSP register access budget exceeded");
    return values[index];
  }
};

static AiRegisterBank g_ai;
static DspRegisterBank g_dsp;
static u32 g_bus_clock = kBusClockHz;
static OSTime g_time;
static u32 g_time_reads;
static BOOL g_interrupts_enabled = TRUE;
static u32 g_interrupt_restore_calls;
static __OSInterruptHandler g_handlers[32]{};
// Standalone source service profile: initialize the source physical interrupt
// cells to the documented all-disabled fixture state. This is a service
// precondition, not a retail global-mask capture or a runtime input derived
// from one.
static OSInterruptMask g_physical_global_mask;
static OSInterruptMask g_physical_local_mask;
static OSInterruptMask g_last_unmask_previous;
alignas(32) static u8 g_dma_buffer[kDmaBytes];

struct UnsupportedRegisterWord {
  operator u32() const {
    fail("unsupported MEM/EXI/PI register read");
  }
  UnsupportedRegisterWord& operator=(u32) {
    fail("unsupported MEM/EXI/PI register write");
    return *this;
  }
};

struct UnsupportedRegisterBank {
  UnsupportedRegisterWord operator[](unsigned) {
    fail("unsupported MEM/EXI/PI register index");
  }
};

static UnsupportedRegisterBank g_mem_registers;
static UnsupportedRegisterBank g_exi_registers;
static UnsupportedRegisterBank g_pi_registers;

u32 source_cntlzw(u32 value) {
  return value == 0 ? 32u : static_cast<u32>(__builtin_clz(value));
}

void* source_physical_to_cached(u32 address) {
  if (address == 0x00C4u) {
    return &g_physical_global_mask;
  }
  if (address == 0x00C8u) return &g_physical_local_mask;
  fail("unsupported OS physical register");
}

u32 sample_divisor() {
  return ((g_ai.values[0] >> 1) & 1u) != 0 ? kGc48KhzDivisor
                                            : kGc32KhzDivisor;
}

void service_tick() {
  if (g_stalled_clock) {
    if (++g_stalled_clock_reads > kStalledClockReadLimit)
      fail("synthetic source clock stalled during sample calibration");
    return;
  }
  if (g_time > kVirtualTimeLimit - kServiceTickQuantum)
    fail("virtual clock budget exceeded");
  g_time += kServiceTickQuantum;
}

void settle_sample_counter() {
  const OSTime now = g_time;
  if ((g_ai.values[0] & 1u) == 0) {
    g_ai.last_sample_time = now;
    g_ai.sample_remainder = 0;
    return;
  }
  if (now < g_ai.last_sample_time) fail("virtual clock moved backwards");
  const u64 elapsed = static_cast<u64>(now - g_ai.last_sample_time);
  const u64 denominator = static_cast<u64>(kOsTimerHz) * sample_divisor();
  const u64 numerator = elapsed * kFixedSampleRateDividend +
                        g_ai.sample_remainder;
  const u64 samples = numerator / denominator;
  g_ai.sample_remainder = numerator % denominator;
  g_ai.values[2] += static_cast<u32>(samples);
  g_ai.sample_advances += static_cast<u32>(samples);
  g_ai.last_sample_time = now;
}

u32 read_ai(unsigned index) {
  if (index >= 4) fail("unknown AI register read index");
  account_mmio("AI register access budget exceeded");
  service_tick();
  if (index == 2) {
    ++g_ai.sample_reads;
    settle_sample_counter();
  }
  return g_ai.values[index];
}

void write_ai(AiRegisterBank* bank, unsigned index, u32 value) {
  if (bank == nullptr || index >= 4) fail("unknown AI register write");
  account_mmio("AI register access budget exceeded");
  if (index == 2) fail("direct AI sample counter writes are unsupported");
  if (index == 0 && (value & ~kAiControlSupportedMask) != 0)
    fail("unsupported AI control bits");
  if (index != 0) {
    bank->values[index] = value;
    return;
  }

  // Settle under the old state before authored rate/play/SCRESET writes.
  settle_sample_counter();
  const u32 old = bank->values[0];
  const bool state_or_rate_changed = ((old ^ value) & 0x3u) != 0;
  const bool sample_reset = (value & 0x20u) != 0;
  bank->values[0] = value & ~0x20u;
  if (sample_reset) {
    bank->values[2] = 0;
    bank->sample_remainder = 0;
  }
  if (state_or_rate_changed || sample_reset) {
    bank->last_sample_time = g_time;
    bank->sample_remainder = 0;
  }
}

AiWord::operator u32() const {
  if (bank == nullptr) fail("null AI register proxy");
  return read_ai(index);
}

AiWord& AiWord::operator=(u32 value) {
  write_ai(bank, index, value);
  return *this;
}

AiWord& AiWord::operator|=(u32 value) {
  return *this = static_cast<u32>(*this) | value;
}

DspWord::operator u32() const {
  if (bank == nullptr || (index != 5 && index != 24 && index != 25 &&
                          index != 27 && index != 29))
    fail("unknown DSP register proxy");
  account_mmio("DSP register access budget exceeded");
  return bank->values[index];
}

DspWord& DspWord::operator=(u32 value) {
  if (bank == nullptr || (index != 5 && index != 24 && index != 25 &&
                          index != 27 && index != 29))
    fail("unknown DSP register write");
  account_mmio("DSP register access budget exceeded");
  if (index == 5 && (value & ~0x0150u) != 0)
    fail("unsupported DSP interrupt control bits");
  if (index == 27 && (value & 0x8000u) != 0) {
    const u32 registered = ((static_cast<u32>(bank->values[24]) << 16) &
                            0x03FF0000u) |
                           (static_cast<u32>(bank->values[25]) & 0xFFE0u);
    const u32 length = (value & 0x7FFFu) << 5;
    const u32 owned = static_cast<u32>(reinterpret_cast<uintptr_t>(g_dma_buffer));
    if ((owned & 31u) != 0 || registered != (owned & 0x03FFFFE0u) ||
        length != kDmaBytes || registered + length < registered ||
        registered + length > (owned & 0x03FFFFE0u) + kDmaBytes)
      fail("DMA enable is not bound to the owned buffer span");
  }
  bank->values[index] = static_cast<u16>(value);
  return *this;
}

DspWord& DspWord::operator|=(u32 value) {
  return *this = static_cast<u32>(*this) | value;
}

void reset() {
  g_ai = AiRegisterBank{};
  g_ai.values[0] = g_calibration_mode ? 0u : kDefaultAiControl;
  g_dsp = DspRegisterBank{};
  g_time = 0;
  g_time_reads = 0;
  g_mmio_accesses = 0;
  g_stalled_clock_reads = 0;
  g_interrupts_enabled = TRUE;
  g_interrupt_restore_calls = 0;
  g_physical_global_mask = 0xFFFFFFFFu;
  g_physical_local_mask = 0;
  g_last_unmask_previous = 0;
  for (auto& handler : g_handlers) handler = nullptr;
  std::memset(g_dma_buffer, 0, sizeof(g_dma_buffer));
}

bool init_with_stack(u8* stack) {
  if (stack != nullptr) return false;
  AIInit(stack);
  return AICheckInit() == TRUE;
}

bool init_null_stack() { return init_with_stack(nullptr); }

bool initialize_dma() {
  const uintptr_t address = reinterpret_cast<uintptr_t>(g_dma_buffer);
  if (address > 0x03FFFFE0u || (address & 31u) != 0)
    fail("owned DMA buffer is outside the checked 32-byte address range");
  const u32 start = static_cast<u32>(address) + (g_unowned_dma ? 0x400u : 0u);
  AIInitDMA(start, sizeof(g_dma_buffer));
  AIStartDMA();
  const u32 registered = AIGetDMAStartAddr();
  const u32 expected = start & 0x03FFFFE0u;
  if (g_unowned_dma || registered != expected ||
      registered + sizeof(g_dma_buffer) < registered ||
      registered + sizeof(g_dma_buffer) > expected + sizeof(g_dma_buffer))
    fail("AI DMA register does not identify the owned buffer span");
  return AIGetDMAEnableFlag() == TRUE && AIGetDMALength() == sizeof(g_dma_buffer);
}

bool set_rate_and_volume() {
  AISetStreamVolLeft(0x12u);
  AISetStreamVolRight(0x34u);
  AISetStreamTrigger(0x12345678u);
  return AIGetStreamVolLeft() == 0x12u && AIGetStreamVolRight() == 0x34u &&
         AIGetStreamTrigger() == 0x12345678u &&
         AIGetStreamSampleRate() == AI_SAMPLERATE_48KHZ &&
         AIGetDSPSampleRate() == AI_SAMPLERATE_32KHZ;
}

u32 sample_reads() { return g_ai.sample_reads; }
u32 sample_advances() { return g_ai.sample_advances; }
OSTime time_ticks() { return g_time; }
u32 time_reads() { return g_time_reads; }
u32 unmask_requested() {
  return 0xFFFFFFFFu & ~g_physical_global_mask;
}
u32 last_unmask_previous() { return g_last_unmask_previous; }
bool handler_present(unsigned index) {
  if (index >= 32) fail("interrupt handler query out of range");
  return g_handlers[index] != nullptr;
}
u32 interrupt_restore_calls() { return g_interrupt_restore_calls; }
u32 ai_control() { return g_ai.values[0]; }
u32 ai_volume() { return g_ai.values[1]; }
u32 dsp_dma_control() { return g_dsp.read(27); }
u32 dsp_control() { return g_dsp.values[5]; }
u32 dma_start() { return AIGetDMAStartAddr(); }
u32 dma_buffer_start() {
  return static_cast<u32>(reinterpret_cast<uintptr_t>(g_dma_buffer));
}
u32 bus_clock() { return g_bus_clock; }
u32 cpu_clock() { return kCpuClockHz; }
u32 sample_divisor_32() { return kGc32KhzDivisor; }
u32 sample_divisor_48() { return kGc48KhzDivisor; }
bool calibration_mode() { return g_calibration_mode; }

}  // namespace melee_web_ai_probe

// The original SDK headers use fixed-address MMIO macros. Define the proxy
// arrays after those headers and before including the source body.
#undef __AIRegs
#undef __DSPRegs
#undef __MEMRegs
#undef __EXIRegs
#undef __PIRegs
#define __AIRegs melee_web_ai_probe::g_ai
#define __DSPRegs melee_web_ai_probe::g_dsp
#define __MEMRegs melee_web_ai_probe::g_mem_registers
#define __EXIRegs melee_web_ai_probe::g_exi_registers
#define __PIRegs melee_web_ai_probe::g_pi_registers
#undef __OSBusClock
#define __OSBusClock (melee_web_ai_probe::g_bus_clock)
#undef OSPhysicalToCached
#define OSPhysicalToCached(address) \
  melee_web_ai_probe::source_physical_to_cached(address)
#define __cntlzw(value) melee_web_ai_probe::source_cntlzw(value)

#define __OSUnmaskInterrupts source_os_unmask
extern "C" {
#include "os_interrupt_source_generated.c"
}
#undef __OSUnmaskInterrupts

extern "C" OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask global);

extern "C" {
#include "ai_source_generated.c"
}

extern "C" OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask global) {
  constexpr OSInterruptMask kSupported =
      OS_INTERRUPTMASK_DSP_AI | OS_INTERRUPTMASK_AI_AI;
  if ((global & ~kSupported) != 0)
    melee_web_ai_probe::fail("unsupported interrupt mask");
  const OSInterruptMask previous = source_os_unmask(global);
  melee_web_ai_probe::g_last_unmask_previous = previous;
  return previous;
}

extern "C" BOOL OSDisableInterrupts(void) {
  const BOOL old = melee_web_ai_probe::g_interrupts_enabled;
  melee_web_ai_probe::g_interrupts_enabled = FALSE;
  return old;
}

extern "C" BOOL OSRestoreInterrupts(BOOL level) {
  if (level != TRUE && level != FALSE)
    melee_web_ai_probe::fail("invalid interrupt state restoration");
  const BOOL old = melee_web_ai_probe::g_interrupts_enabled;
  melee_web_ai_probe::g_interrupts_enabled = level;
  ++melee_web_ai_probe::g_interrupt_restore_calls;
  return old;
}

extern "C" OSTime OSGetTime(void) {
  if (++melee_web_ai_probe::g_time_reads >
      melee_web_ai_probe::kTimeReadLimit)
    melee_web_ai_probe::fail("OSGetTime read budget exceeded");
  melee_web_ai_probe::service_tick();
  return melee_web_ai_probe::g_time;
}

extern "C" __OSInterruptHandler __OSSetInterruptHandler(
    __OSInterrupt interrupt, __OSInterruptHandler handler) {
  if (interrupt < 0 || interrupt >= 32)
    melee_web_ai_probe::fail("interrupt handler index out of range");
  if (handler == nullptr)
    melee_web_ai_probe::fail("null interrupt handler");
  auto& slot = melee_web_ai_probe::g_handlers[interrupt];
  const auto old = slot;
  slot = handler;
  return old;
}

extern "C" void OSClearContext(OSContext*) {
  melee_web_ai_probe::fail("OSClearContext is outside the AIInit oracle");
}

extern "C" void OSSetCurrentContext(OSContext*) {
  melee_web_ai_probe::fail("OSSetCurrentContext is outside the AIInit oracle");
}

extern "C" void OSReport(char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}

extern "C" void OSPanic(char* file, int line, char* format, ...) {
  std::fprintf(stderr, "OSPanic %s:%d: ", file, line);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fputc('\n', stderr);
  std::abort();
}

void print_ai_init_snapshot() {
  using namespace melee_web_ai_probe;
  // Emit immediately after AIInit returns, before synthetic DMA/volume/
  // trigger exercise. These private fields belong to the included source;
  // this is an observation, not an assignment of expected values.
  std::printf(
      "{\"kind\":\"ai_init_return\",\"ai_init_flag\":%s,"
      "\"ai_control\":%u,\"ai_volume\":%u,\"ai_sample_count\":%u,"
      "\"ai_trigger\":%u,\"bound_32khz\":%lld,"
      "\"bound_48khz\":%lld,\"min_wait\":%lld,\"max_wait\":%lld,"
      "\"buffer\":%lld,\"stream_callback_null\":%s,"
      "\"dma_callback_null\":%s,\"callback_stack_null\":%s,"
      "\"unmask_requested\":%u,\"unmask_previous\":%u,"
      "\"dsp_control\":%u,"
      "\"handler_5_is_AIDHandler\":%s,"
      "\"handler_8_is_AISHandler\":%s,\"sample_reads\":%u,"
      "\"sample_advances\":%u,\"time_reads\":%u,"
      "\"interrupt_restore_calls\":%u,\"interrupts_enabled\":%s}\n",
      __AI_init_flag ? "true" : "false",
      static_cast<unsigned>(g_ai.values[0]),
      static_cast<unsigned>(g_ai.values[1]),
      static_cast<unsigned>(g_ai.values[2]),
      static_cast<unsigned>(g_ai.values[3]),
      static_cast<long long>(bound_32KHz),
      static_cast<long long>(bound_48KHz), static_cast<long long>(min_wait),
      static_cast<long long>(max_wait), static_cast<long long>(buffer),
      __AIS_Callback == nullptr ? "true" : "false",
      __AID_Callback == nullptr ? "true" : "false",
      __CallbackStack == nullptr ? "true" : "false",
      static_cast<unsigned>(unmask_requested()),
      static_cast<unsigned>(last_unmask_previous()),
      static_cast<unsigned>(dsp_control()),
      g_handlers[5] == __AIDHandler ? "true" : "false",
      g_handlers[8] == __AISHandler ? "true" : "false",
      static_cast<unsigned>(sample_reads()),
      static_cast<unsigned>(sample_advances()),
      static_cast<unsigned>(time_reads()),
      static_cast<unsigned>(interrupt_restore_calls()),
      g_interrupts_enabled ? "true" : "false");
}

int main(int argc, char** argv) {
  using namespace melee_web_ai_probe;
  bool invalid_handler = false;
  bool unknown_register = false;
  bool unknown_dsp_register = false;
  bool unknown_interrupt_mask = false;
  bool unknown_control = false;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--control") == 0 && i + 1 < argc &&
        std::strcmp(argv[i + 1], "calibration") == 0) {
      g_calibration_mode = true;
      ++i;
    } else if (std::strcmp(arg, "--clock-stall") == 0) {
      g_stalled_clock = true;
    } else if (std::strcmp(arg, "--dma-unowned") == 0) {
      g_unowned_dma = true;
    } else if (std::strcmp(arg, "--invalid-handler") == 0) {
      invalid_handler = true;
    } else if (std::strcmp(arg, "--unknown-register") == 0) {
      unknown_register = true;
    } else if (std::strcmp(arg, "--unknown-dsp-register") == 0) {
      unknown_dsp_register = true;
    } else if (std::strcmp(arg, "--unknown-interrupt-mask") == 0) {
      unknown_interrupt_mask = true;
    } else if (std::strcmp(arg, "--unknown-control") == 0) {
      unknown_control = true;
    } else {
      fail("unknown probe option or missing option value");
    }
  }
  if (g_stalled_clock && !g_calibration_mode)
    fail("--clock-stall requires --control calibration");
  reset();
  if (invalid_handler) {
    __OSSetInterruptHandler(5, nullptr);
    return 1;
  }
  if (unknown_register) {
    volatile u32 invalid_probe = static_cast<u32>(g_ai[4]);
    (void)invalid_probe;
    return 1;
  }
  if (unknown_dsp_register) {
    volatile u32 invalid_probe = static_cast<u32>(g_dsp[26]);
    (void)invalid_probe;
    return 1;
  }
  if (unknown_interrupt_mask) {
    __OSUnmaskInterrupts(OS_INTERRUPTMASK_MEM_0);
    return 1;
  }
  if (unknown_control) {
    g_ai[0] = 0x08u;
    return 1;
  }
  u8 unsupported_stack[8]{};
  if (init_with_stack(unsupported_stack) || !init_null_stack()) {
    fail("AIInit startup path failed");
  }
  print_ai_init_snapshot();
  if (!initialize_dma() || !set_rate_and_volume() ||
      unmask_requested() != 0x04800000u || !handler_present(5) ||
      !handler_present(8) || (dsp_control() & 0x0150u) != 0x10u ||
      interrupt_restore_calls() == 0 ||
      !g_interrupts_enabled) {
    std::fprintf(stderr,
                 "AI probe validation failed dma=%u/%u/%u vol=%u/%u trig=%u "
                 "rates=%u/%u mask=%u handlers=%u/%u restores=%u\n",
                 static_cast<unsigned>(AIGetDMAEnableFlag()),
                 static_cast<unsigned>(dma_start()),
                 static_cast<unsigned>(AIGetDMALength()),
                 static_cast<unsigned>(AIGetStreamVolLeft()),
                 static_cast<unsigned>(AIGetStreamVolRight()),
                 static_cast<unsigned>(AIGetStreamTrigger()),
                 static_cast<unsigned>(AIGetStreamSampleRate()),
                 static_cast<unsigned>(AIGetDSPSampleRate()),
                 static_cast<unsigned>(unmask_requested()), handler_present(5),
                 handler_present(8),
                 static_cast<unsigned>(interrupt_restore_calls()));
    return 1;
  }
  std::printf(
      "{\"kind\":\"synthetic_validation\",\"ai_init\":true,"
      "\"control_mode\":\"%s\","
      "\"clock_hz\":%u,\"cpu_clock_hz\":%u,"
      "\"service_tick_quantum\":%lld,\"source_divisor_32\":%u,"
      "\"source_divisor_48\":%u,\"sample_reads\":%u,"
      "\"sample_advances\":%u,\"time_ticks\":%lld,\"time_reads\":%u,"
      "\"unmask_requested\":%u,\"unmask_previous\":%u,"
      "\"dsp_control\":%u,"
      "\"handler_5_is_AIDHandler\":%s,"
      "\"handler_8_is_AISHandler\":%s,\"interrupt_restore_calls\":%u,"
      "\"ai_control\":%u,\"ai_volume\":%u,\"dsp_dma_control\":%u,"
      "\"dma_buffer_start\":%u,\"dma_length\":%u,\"dma_start\":%u,"
      "\"dma_programming_checked\":true,"
      "\"nonnull_stack_adapter_rejected\":true,"
      "\"hardware_audio_verified\":false}\n",
      calibration_mode() ? "calibration" : "audio_interface_manager",
      static_cast<unsigned>(bus_clock()), static_cast<unsigned>(cpu_clock()),
      static_cast<long long>(kServiceTickQuantum),
      static_cast<unsigned>(sample_divisor_32()),
      static_cast<unsigned>(sample_divisor_48()),
      static_cast<unsigned>(sample_reads()),
      static_cast<unsigned>(sample_advances()),
      static_cast<long long>(time_ticks()), static_cast<unsigned>(time_reads()),
      static_cast<unsigned>(unmask_requested()),
      static_cast<unsigned>(last_unmask_previous()),
      static_cast<unsigned>(dsp_control()),
      g_handlers[5] == __AIDHandler ? "true" : "false",
      g_handlers[8] == __AISHandler ? "true" : "false",
      static_cast<unsigned>(interrupt_restore_calls()),
      static_cast<unsigned>(ai_control()), static_cast<unsigned>(ai_volume()),
      static_cast<unsigned>(dsp_dma_control()),
      static_cast<unsigned>(dma_buffer_start()),
      static_cast<unsigned>(AIGetDMALength()), static_cast<unsigned>(dma_start()));
  return 0;
}
