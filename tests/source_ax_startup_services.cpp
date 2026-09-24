/* The generated AX startup fixture includes the pinned AI, DSP, and OS source
 * bodies at the marked locations.  This file owns only checked hardware and
 * interrupt services needed by that source boundary. */
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include <dolphin/types.h>
extern "C" {
#include <dolphin/os.h>
#include <dolphin/ai.h>
#include <dolphin/dsp.h>
#include <dolphin/ax.h>
#include <dolphin/hw_regs.h>
}
#include "source_ax_startup_accessors.h"
#include "source_ax_startup_services.h"

static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4,
              "Dolphin integer ABI changed");
static_assert(sizeof(BOOL) == 4 && sizeof(OSTime) == 8,
              "Dolphin control ABI changed");
static_assert(sizeof(void*) == 4, "source AX fixture requires Wasm32");
static_assert(sizeof(DSPTaskInfo) == 0x50,
              "Dolphin DSPTaskInfo ABI changed");

extern "C" void __DSP_debug_printf(const char* fmt, ...) { va_list a; va_start(a, fmt); std::vfprintf(stderr, fmt, a); va_end(a); }

namespace melee_web_source_ax {
constexpr u32 kMmioLimit = 5000000;
constexpr u32 kPollLimit = 256;
constexpr u32 kDspImageHash = 0x4E8A8B21u;
constexpr u32 kDspImageBytes = 6624u;
constexpr u16 kDspStatusMask = 0x0100u;
constexpr u16 kDspStatusInterrupt = 0x0080u;
constexpr u16 kDspStatusW1C = 0x00A8u;
constexpr u32 kAiDmaBytes = 0x280u;
constexpr u32 kDspDramLength = 0x2000u;
constexpr u16 kDspInitVector = 0x10u;
constexpr u16 kDspResumeVector = 0x30u;
constexpr u32 kDspTaskPriority = 0u;
constexpr u32 kBusClockHz = 162000000u;
constexpr u32 kOsTimerHz = kBusClockHz / 4u;
constexpr u32 kFixedSampleRateDividend = 54000000u * 2u;
constexpr u32 kGc48KhzDivisor = 2248u;
constexpr u32 kGc32KhzDivisor = kGc48KhzDivisor * 3u / 2u;
constexpr OSTime kServiceTickQuantum = 64;
constexpr u32 kTimeReadLimit = 100000u;

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "source AX startup failure: %s\n", message);
    std::abort();
}
void require(bool ok, const char* message) { if (!ok) fail(message); }

struct RegisterWord;
struct RegisterBank {
    u16 values[64]{};
    RegisterWord operator[](unsigned index);
};
struct RegisterWord {
    RegisterBank* bank;
    unsigned index;
    operator u16() const;
    RegisterWord& operator=(u16 value);
    RegisterWord& operator|=(u16 value) { return *this = static_cast<u16>(*this) | value; }
};

struct AiWord;
struct AiBank {
    u32 values[4]{};
    AiWord operator[](unsigned index);
};
struct AiWord {
    AiBank* bank;
    unsigned index;
    operator u32() const;
    AiWord& operator=(u32 value);
    AiWord& operator|=(u32 value) { return *this = static_cast<u32>(*this) | value; }
};

struct UnsupportedWord { operator u32() const { fail("unsupported MMIO read"); }
    UnsupportedWord& operator=(u32) { fail("unsupported MMIO write"); return *this; } };
struct UnsupportedBank { UnsupportedWord operator[](unsigned) { fail("unsupported MMIO index"); } };

RegisterBank g_dsp_regs;
AiBank g_ai_regs;
UnsupportedBank g_mem_registers, g_exi_registers, g_pi_registers;
OSInterruptMask g_global_mask;
OSInterruptMask g_local_mask;
BOOL g_interrupts_enabled = TRUE;
__OSInterruptHandler g_handlers[32]{};
u32 g_mmio_count = 0;
u32 g_dsp_mail_step = 0;
u32 g_dsp_mail_count = 0;
u32 g_restore_count = 0;
u32 g_ai_dma_start = 0;
OSTime g_time = 0;
u32 g_time_reads = 0;
u32 g_ai_dma_length = 0;
bool g_ai_dma_enabled = false;
bool g_dsp_pending = false;
bool g_from_pending = false;
u32 g_from_mail = 0x8071FEEDu;
bool g_to_pending = false;
bool g_defer_pump = false;
bool g_bad_dma = false;
bool g_bad_image = false;
bool g_bad_mail = false;
bool g_masked_pump = false;
bool g_skip_cache_publication = false;
bool g_bad_task_callback = false;
bool g_bad_task_span = false;
bool g_global_masked_pump = false;
bool g_local_masked_pump = false;
bool g_status_masked_pump = false;
bool g_dispatch_masked = false;
u64 g_sample_remainder = 0;
OSTime g_last_sample_time = 0;
u32 g_sample_reads = 0;
u32 g_sample_advances = 0;
OSContext g_caller_context{};
OSContext* g_current_context = &g_caller_context;

void account() { if (++g_mmio_count > kMmioLimit) fail("MMIO budget exceeded"); }
u32 cntlzw(u32 value) { return value == 0 ? 32 : static_cast<u32>(__builtin_clz(value)); }
void* physical_cached(u32 address) {
    if (address == 0xC4) return &g_global_mask;
    if (address == 0xC8) return &g_local_mask;
    fail("unsupported OS interrupt physical cell");
}

bool span_contains(u32 address, u32 bytes, u32 wanted, u32 wanted_bytes) {
    const u64 a = address, b = wanted;
    return b >= a && wanted_bytes <= bytes && b - a <= bytes - wanted_bytes;
}
void require_owned_span(u32 address, u32 bytes, u32 alignment, u32 wanted, u32 wanted_bytes) {
    require((address & (alignment - 1u)) == 0, "AX static span lost required alignment");
    require(span_contains(address, bytes, wanted, wanted_bytes), "unknown or out-of-span AX pointer");
}

u32 descriptor_count(void) { return 32; }
MeleeWebAxDescriptor g_desc[32]{};
u32 g_desc_count = 0;
std::vector<u8> g_published[32];
bool g_published_valid[32]{};
bool g_output_flushed = false;
void collect_descriptors() {
    g_desc_count = 0;
    const auto add = [](u32 (*fn)(MeleeWebAxDescriptor*, u32, MeleeWebAxState*)) {
        const u32 n = fn(nullptr, 0, nullptr);
        require(g_desc_count + n <= descriptor_count(), "AX descriptor table overflow");
        g_desc_count += fn(&g_desc[g_desc_count], n, nullptr);
    };
    add(melee_web_ax_alloc_describe); add(melee_web_ax_vpb_describe);
    add(melee_web_ax_spb_describe); add(melee_web_ax_aux_describe);
    add(melee_web_ax_cl_describe); add(melee_web_ax_out_describe);
    add(melee_web_ax_dsp_code_describe);
    for (u32 i = 0; i < g_desc_count; ++i) {
        g_published[i].resize(g_desc[i].bytes);
        if (g_desc[i].kind == MELEE_WEB_AX_SPAN && g_desc[i].bytes != 0) {
            std::memcpy(g_published[i].data(),
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(g_desc[i].address)),
                        g_desc[i].bytes);
        }
        g_published_valid[i] = g_desc[i].kind == MELEE_WEB_AX_SPAN;
    }
}
const MeleeWebAxDescriptor* find_desc(u32 tag) {
    for (u32 i = 0; i < g_desc_count; ++i) if (g_desc[i].tag == tag) return &g_desc[i];
    return nullptr;
}
u32 callback_address(DSPCallback callback) {
    return static_cast<u32>(reinterpret_cast<uintptr_t>(callback));
}
void validate_span(u32 address, u32 bytes, u32 tag, u32 alignment = 1) {
    const auto* d = find_desc(tag);
    require(d && d->kind == MELEE_WEB_AX_SPAN, "missing AX source-owned span");
    require_owned_span(d->address, d->bytes, alignment, address, bytes);
}

u32 image_hash() {
    const auto* d = find_desc(MELEE_WEB_AX_TAG_DSP_SLAVE);
    require(d && d->bytes == kDspImageBytes, "unexpected AX DSP image span");
    u16* image = reinterpret_cast<u16*>(static_cast<uintptr_t>(d->address));
    u32 hash = 0;
    for (u32 i = 0; i < d->bytes / 2; ++i) {
        u16 word = image[i];
        hash ^= static_cast<u8>(word >> 8); hash = (hash << 3) | (hash >> 29);
        hash ^= static_cast<u8>(word); hash = (hash << 3) | (hash >> 29);
    }
    return hash;
}

void maybe_pump();
void on_dsp_mail(u32 mail);
void reset() {
    g_dsp_regs = RegisterBank{}; g_ai_regs = AiBank{};
    g_global_mask = 0xFFFFFFFFu; g_local_mask = 0;
    g_interrupts_enabled = TRUE; std::memset(g_handlers, 0, sizeof(g_handlers));
    g_mmio_count = g_dsp_mail_step = g_dsp_mail_count = 0;
    g_restore_count = 0; g_ai_dma_start = g_ai_dma_length = 0;
    g_ai_dma_enabled = false; g_dsp_pending = false; g_from_pending = true; g_from_mail = 0x8071FEEDu; g_to_pending = false; g_defer_pump = false; g_dispatch_masked = false;
    g_time = g_time_reads = 0; g_sample_remainder = 0; g_last_sample_time = 0; g_sample_reads = g_sample_advances = 0;
    g_output_flushed = false;
    g_ai_regs.values[0] = 0x42u;
    for (u32 i = 0; i < 32; ++i) { g_published[i].clear(); g_published_valid[i] = false; }
    std::memset(&g_caller_context, 0, sizeof(g_caller_context)); g_current_context = &g_caller_context;
}

} // namespace melee_web_source_ax

#undef __AIRegs
#undef __DSPRegs
#undef __MEMRegs
#undef __EXIRegs
#undef __PIRegs
#define __AIRegs melee_web_source_ax::g_ai_regs
#define __DSPRegs melee_web_source_ax::g_dsp_regs
#define __MEMRegs melee_web_source_ax::g_mem_registers
#define __EXIRegs melee_web_source_ax::g_exi_registers
#define __PIRegs melee_web_source_ax::g_pi_registers
#undef __OSBusClock
#define __OSBusClock (162000000u)
#undef OSPhysicalToCached
#define OSPhysicalToCached(address) melee_web_source_ax::physical_cached(address)
#define __cntlzw(value) melee_web_source_ax::cntlzw(value)

/* The exact source mask routines are inserted here. */
#define __OSUnmaskInterrupts melee_web_source_os_unmask
extern "C" {
/* MELEE_WEB_PINNED_OS_INTERRUPT */
}
#undef __OSUnmaskInterrupts

/* AI and DSP source bodies are inserted after the checked MMIO proxies. */
extern "C" {
/* MELEE_WEB_PINNED_AI_SOURCE */
}
#define DSPAddTask melee_web_source_dsp_add_task
extern "C" {
/* MELEE_WEB_PINNED_DSP_TASK_SOURCE */
/* MELEE_WEB_PINNED_DSP_SOURCE */
}
#undef DSPAddTask

namespace melee_web_source_ax {

RegisterWord RegisterBank::operator[](unsigned index) {
    require(index == 0 || index == 1 || index == 2 || index == 3 || index == 5 || index == 24 || index == 25 || index == 27 || index == 29,
            "unsupported DSP register index"); return RegisterWord{this, index};
}
RegisterWord::operator u16() const {
    require(bank != nullptr, "null DSP register"); account();
    if (index == 0) return static_cast<u16>((bank->values[0] & 0x7FFFu) | (g_to_pending ? 0x8000u : 0));
    if (index == 2 && g_from_pending) return static_cast<u16>(g_from_mail >> 16);
    if (index == 3 && g_from_pending) { g_from_pending = false; return static_cast<u16>(g_from_mail); }
    return bank->values[index];
}
void set_dsp_status(u16 value) {
    require((value & ~static_cast<u16>(0x09F8u)) == 0, "unsupported DSP status bit");
    const u16 cleared = g_dsp_regs.values[5] & value & kDspStatusW1C;
    g_dsp_regs.values[5] = static_cast<u16>((g_dsp_regs.values[5] & kDspStatusW1C & ~value) |
                                             (value & ~kDspStatusW1C));
    if ((cleared & kDspStatusInterrupt) != 0) g_dsp_pending = false;
}
RegisterWord& RegisterWord::operator=(u16 value) {
    require(bank != nullptr, "null DSP register"); account();
    if (index == 0) { bank->values[0] = value; g_to_pending = true; return *this; }
    if (index == 1) {
        if (g_bad_mail && g_dsp_mail_step == 0) value ^= 1u;
        bank->values[1] = value;
        on_dsp_mail((static_cast<u32>(bank->values[0]) << 16) | bank->values[1]);
        g_to_pending = false;
        return *this;
    }
    if (index == 24 || index == 25 || index == 27) {
        // Mutate the actual source register field for the negative control;
        // the DMA address below must always be derived from published MMIO.
        if (index == 25 && g_bad_dma) value ^= 0x0020u;
        bank->values[index] = value;
        if (index == 27) {
            g_ai_dma_start = ((static_cast<u32>(bank->values[24]) << 16) & 0x03FF0000u) |
                             (static_cast<u32>(bank->values[25]) & 0xFFE0u);
            g_ai_dma_length = (static_cast<u32>(value) & 0x7FFFu) << 5;
            g_ai_dma_enabled = (value & 0x8000u) != 0;
            if (g_ai_dma_enabled) {
                const auto* d = find_desc(MELEE_WEB_AX_TAG_OUT_BUFFER);
                require(g_output_flushed, "AX output buffer was not published before DMA");
                MeleeWebAxState output_state{};
                require(d != nullptr &&
                            melee_web_ax_out_describe(nullptr, 0, &output_state) == 9 &&
                            output_state.out_frame < 2,
                        "AX output frame index is outside the authored double buffer");
                const u64 expected_start = static_cast<u64>(d->address) +
                                           static_cast<u64>(output_state.out_frame) * kAiDmaBytes;
                require(expected_start <= std::numeric_limits<u32>::max() &&
                            g_ai_dma_length == kAiDmaBytes &&
                            g_ai_dma_start == static_cast<u32>(expected_start) &&
                            span_contains(d->address, d->bytes, g_ai_dma_start, g_ai_dma_length),
                        "AI DMA points outside AX output buffer");
            }
        }
        return *this;
    }
    require(index == 5, "write to read-only DSP register"); set_dsp_status(value); return *this;
}

u32 sample_divisor() {
    return ((g_ai_regs.values[0] >> 1) & 1u) != 0 ? kGc48KhzDivisor : kGc32KhzDivisor;
}
void settle_sample_counter() {
    const OSTime now = g_time;
    if ((g_ai_regs.values[0] & 1u) == 0) {
        g_last_sample_time = now;
        g_sample_remainder = 0;
        return;
    }
    require(now >= g_last_sample_time, "AI virtual clock moved backwards");
    const u64 elapsed = static_cast<u64>(now - g_last_sample_time);
    const u64 denominator = static_cast<u64>(kOsTimerHz) * sample_divisor();
    const u64 numerator = elapsed * kFixedSampleRateDividend + g_sample_remainder;
    const u64 samples = numerator / denominator;
    g_sample_remainder = numerator % denominator;
    g_ai_regs.values[2] += static_cast<u32>(samples);
    g_sample_advances += static_cast<u32>(samples);
    g_last_sample_time = now;
}
AiWord AiBank::operator[](unsigned index) { require(index < 4, "unsupported AI register index"); return AiWord{this,index}; }
AiWord::operator u32() const {
    require(bank != nullptr, "null AI register"); account();
    if (index == 2) { ++g_sample_reads; settle_sample_counter(); }
    return bank->values[index];
}
AiWord& AiWord::operator=(u32 value) {
    require(bank != nullptr, "null AI register"); account();
    require(index != 2, "AI sample counter write is unsupported");
    require(index != 0 || (value & ~0x67u) == 0, "unsupported AI control bit");
    if (index != 0) { bank->values[index] = value; return *this; }
    settle_sample_counter();
    const u32 old = bank->values[0];
    const bool state_or_rate_changed = ((old ^ value) & 0x3u) != 0;
    const bool sample_reset = (value & 0x20u) != 0;
    bank->values[0] = value & ~0x20u;
    if (sample_reset) { bank->values[2] = 0; g_sample_remainder = 0; }
    if (state_or_rate_changed || sample_reset) { g_last_sample_time = g_time; g_sample_remainder = 0; }
    return *this;
}

extern "C" BOOL OSDisableInterrupts(void) {
    BOOL old = g_interrupts_enabled; g_interrupts_enabled = FALSE; return old;
}
extern "C" BOOL OSRestoreInterrupts(BOOL level) {
    require(level == TRUE || level == FALSE, "invalid interrupt level");
    BOOL old = g_interrupts_enabled; g_interrupts_enabled = level; ++g_restore_count;
    if (g_interrupts_enabled && old == FALSE && !g_defer_pump && g_dsp_pending) {
        g_dispatch_masked = true;
        g_interrupts_enabled = FALSE;
        maybe_pump();
        g_interrupts_enabled = TRUE;
        g_dispatch_masked = false;
    }
    return old;
}
extern "C" __OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt n, __OSInterruptHandler h) {
    require(n >= 0 && n < 32 && h != nullptr, "invalid interrupt handler");
    auto old = g_handlers[n]; g_handlers[n] = h; return old;
}
extern "C" OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) {
    require(mask == OS_INTERRUPTMASK_DSP_DSP || mask == OS_INTERRUPTMASK_DSP_AI ||
            mask == OS_INTERRUPTMASK_AI_AI || mask == (OS_INTERRUPTMASK_DSP_AI | OS_INTERRUPTMASK_AI_AI),
            "unsupported interrupt mask");
    auto old = melee_web_source_os_unmask(mask);
    require((g_global_mask & mask) == 0 && (g_local_mask & mask) == 0,
            "source interrupt unmask left global or local mask set");
    if ((mask & OS_INTERRUPTMASK_DSP_DSP) != 0)
        require((g_dsp_regs.values[5] & kDspStatusMask) != 0,
                "DSP interrupt unmask did not publish DSP control enable");
    if ((mask & OS_INTERRUPTMASK_DSP_AI) != 0)
        require((g_dsp_regs.values[5] & 0x0010u) != 0,
                "DSP AI interrupt unmask did not publish DSP control enable");
    if ((mask & OS_INTERRUPTMASK_AI_AI) != 0)
        require((g_ai_regs.values[0] & 0x0004u) != 0,
                "AI interrupt unmask did not publish AI control enable");
    return old;
}
extern "C" OSTime OSGetTime(void) {
    if (++g_time_reads > kTimeReadLimit) fail("OSGetTime read budget exceeded");
    if (g_time > std::numeric_limits<OSTime>::max() - kServiceTickQuantum)
        fail("OSGetTime virtual clock overflow");
    g_time += kServiceTickQuantum;
    return g_time;
}
void cache_transfer(void* pointer, u32 bytes, bool invalidate) {
    const u32 address = static_cast<u32>(reinterpret_cast<uintptr_t>(pointer));
    for (u32 i = 0; i < g_desc_count; ++i) {
        const auto& descriptor = g_desc[i];
        if (!span_contains(descriptor.address, descriptor.bytes, address, bytes)) continue;
        const u32 offset = address - descriptor.address;
        require(g_published[i].size() == descriptor.bytes, "AX cache shadow size mismatch");
        require(g_published_valid[i], "AX cache shadow was not initialized");
        if (invalidate) {
            std::memcpy(pointer, g_published[i].data() + offset, bytes);
        } else if (descriptor.tag == MELEE_WEB_AX_TAG_OUT_BUFFER &&
                   offset == 0 && bytes == descriptor.bytes &&
                   g_skip_cache_publication) {
            // Negative control: source reaches DMA with no publication of its
            // CPU output buffer, which must be rejected by the provider.
            return;
        } else {
            std::memcpy(g_published[i].data() + offset, pointer, bytes);
            if (descriptor.tag == MELEE_WEB_AX_TAG_OUT_BUFFER && offset == 0 &&
                bytes == descriptor.bytes)
                g_output_flushed = true;
        }
        return;
    }
    fail(invalidate ? "DCInvalidateRange used an unknown AX span" : "DCFlushRange used an unknown AX span");
}
extern "C" void DCFlushRange(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, false); }
extern "C" void DCFlushRangeNoSync(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, false); }
extern "C" void DCInvalidateRange(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, true); }
extern "C" void OSReport(char* fmt, ...) { va_list a; va_start(a,fmt); std::vfprintf(stderr,fmt,a); va_end(a); }
extern "C" void OSPanic(char* file,int line,char* fmt,...) { (void)file;(void)line; va_list a;va_start(a,fmt);std::vfprintf(stderr,fmt,a);va_end(a);fail("original source assertion"); }
extern "C" OSContext* OSGetCurrentContext(void) { return g_current_context; }
extern "C" void OSClearContext(OSContext* c) { if (!c) fail("null OS context"); c->mode=0;c->state=0; }
extern "C" void OSSetCurrentContext(OSContext* c) { g_current_context = c; }

extern "C" void DSPReset(void); extern "C" BOOL DSPCheckInit(void);
extern "C" void DSPInit(void); extern "C" DSPTaskInfo* DSPAddTask(DSPTaskInfo*);
extern "C" void AIInit(u8*); extern "C" void AXInit(void);

void on_dsp_mail(u32 mail) {
    ++g_dsp_mail_count;
    static const u32 expected[] = {0x80F3A001u,0,0x80F3C002u,0,0x80F3A002u,
                                   0,0x80F3B002u,0,0x80F3D001u,0x10u};
    require(g_dsp_mail_step < sizeof(expected)/sizeof(expected[0]), "unexpected DSP mail");
    // The source sends the image pointer and image length at positions 1 and 5.
    if (g_dsp_mail_step == 1) require(mail == find_desc(MELEE_WEB_AX_TAG_DSP_SLAVE)->address, "DSP image pointer mismatch");
    else if (g_dsp_mail_step == 5) require(mail == kDspImageBytes, "DSP image length mismatch");
    else require(mail == expected[g_dsp_mail_step], "DSP boot mail mismatch");
    ++g_dsp_mail_step;
    if (g_dsp_mail_step == 10) {
        require((g_global_mask & OS_INTERRUPTMASK_DSP_DSP) == 0 &&
                    (g_local_mask & OS_INTERRUPTMASK_DSP_DSP) == 0 &&
                    (g_dsp_regs.values[5] & kDspStatusMask) != 0,
                "DSP completion arrived while DSP interrupt remained masked");
        g_dsp_pending = true; g_from_pending = true; g_from_mail = 0xDCD10000u;
        g_dsp_regs.values[5] |= kDspStatusInterrupt;
    }
}
void maybe_pump() {
    if (!g_dsp_pending) return;
    require(g_handlers[7] != nullptr, "DSP interrupt handler missing");
    require(g_dispatch_masked && !g_interrupts_enabled,
            "DSP interrupt dispatched without masked interrupt owner");
    require((g_global_mask & OS_INTERRUPTMASK_DSP_DSP) == 0 &&
                (g_local_mask & OS_INTERRUPTMASK_DSP_DSP) == 0,
            "DSP interrupt dispatched while globally or locally masked");
    require((g_dsp_regs.values[5] & (kDspStatusMask | kDspStatusInterrupt)) ==
                (kDspStatusMask | kDspStatusInterrupt),
            "DSP interrupt pending without enabled DSP status bits");
    // Source DSP handler consumes its own mailbox and calls AX's original init callback.
    g_dsp_pending = false;
    OSContext* saved_context = g_current_context;
    g_current_context = &g_caller_context;
    g_handlers[7](7, g_current_context);
    OSContext* handler_context = g_current_context;
    require(handler_context == &g_caller_context,
            "DSP handler did not restore its caller context");
    g_current_context = saved_context;
    require(g_current_context == saved_context && !g_dsp_pending &&
                (g_dsp_regs.values[5] & kDspStatusInterrupt) == 0,
            "DSP handler did not restore context or acknowledge interrupt");
}
extern "C" DSPTaskInfo* DSPAddTask(DSPTaskInfo* task) {
    require(task != nullptr, "DSPAddTask received a null task");
    g_defer_pump = true;
    DSPTaskInfo* result = melee_web_source_dsp_add_task(task);
    g_defer_pump = false;
    require(result == task, "source DSPAddTask returned a different task");
    require(task->state == 0 && task->flags == 1,
            "source DSPAddTask did not publish the expected queued task state");
    const auto* image = find_desc(MELEE_WEB_AX_TAG_DSP_SLAVE);
    const auto* dram = find_desc(MELEE_WEB_AX_TAG_DRAM);
    const auto* task_descriptor = find_desc(MELEE_WEB_AX_TAG_DSP_TASK);
    const auto* init_callback = find_desc(MELEE_WEB_AX_TAG_DSP_INIT_CALLBACK);
    const auto* resume_callback = find_desc(MELEE_WEB_AX_TAG_DSP_RESUME_CALLBACK);
    const auto* done_callback = find_desc(MELEE_WEB_AX_TAG_DSP_DONE_CALLBACK);
    if (g_bad_task_callback) task->init_cb = nullptr;
    if (g_bad_task_span) task->iram_mmem_addr = reinterpret_cast<u16*>(
        static_cast<uintptr_t>(image != nullptr ? image->address + 2u : 2u));
    require(task_descriptor != nullptr &&
                static_cast<u32>(reinterpret_cast<uintptr_t>(task)) == task_descriptor->address,
            "DSP task pointer does not match source task descriptor");
    require(image != nullptr && task->iram_mmem_addr ==
                reinterpret_cast<u16*>(static_cast<uintptr_t>(image->address)) &&
                task->iram_length == kDspImageBytes && task->iram_addr == 0,
            "DSP task IRAM span does not match source image");
    require(dram != nullptr && task->dram_mmem_addr ==
                reinterpret_cast<u16*>(static_cast<uintptr_t>(dram->address)) &&
                task->dram_length == kDspDramLength && task->dram_addr == 0 &&
                span_contains(dram->address, dram->bytes, dram->address,
                              task->dram_length),
            "DSP task DRAM span does not match source image");
    require(task->priority == kDspTaskPriority &&
                task->dsp_init_vector == kDspInitVector &&
                task->dsp_resume_vector == kDspResumeVector,
            "DSP task authored fields do not match AXOut source");
    require(init_callback != nullptr && resume_callback != nullptr &&
                done_callback != nullptr && task->init_cb != nullptr &&
                task->res_cb != nullptr && task->done_cb != nullptr &&
                task->req_cb == nullptr &&
                callback_address(task->init_cb) == init_callback->address &&
                callback_address(task->res_cb) == resume_callback->address &&
                callback_address(task->done_cb) == done_callback->address,
            "DSP task callback identity does not match AXOut source");
    if (g_dsp_pending && g_masked_pump) {
        g_interrupts_enabled = FALSE;
        maybe_pump();
    }
    if (g_global_masked_pump) g_global_mask |= OS_INTERRUPTMASK_DSP_DSP;
    if (g_local_masked_pump) g_local_mask |= OS_INTERRUPTMASK_DSP_DSP;
    if (g_status_masked_pump) g_dsp_regs.values[5] &= ~kDspStatusMask;
    if (g_dsp_pending && g_interrupts_enabled) {
        g_dispatch_masked = true;
        g_interrupts_enabled = FALSE;
        maybe_pump();
        g_interrupts_enabled = TRUE;
        g_dispatch_masked = false;
    }
    require(task->state == 1 && task->flags == 1,
            "DSP init handler did not publish the expected task state");
    return result;
}

int run(const char* mode) {
    require(mode == nullptr || std::strcmp(mode, "") == 0 || std::strcmp(mode,"bad-dma") == 0 || std::strcmp(mode,"bad-image") == 0 || std::strcmp(mode,"bad-mail") == 0 || std::strcmp(mode,"masked-pump") == 0 || std::strcmp(mode,"missing-cache") == 0 || std::strcmp(mode,"bad-task-callback") == 0 || std::strcmp(mode,"bad-task-span") == 0 || std::strcmp(mode,"global-masked-pump") == 0 || std::strcmp(mode,"local-masked-pump") == 0 || std::strcmp(mode,"status-masked-pump") == 0, "unknown AX startup mode");
    g_bad_dma = mode && std::strcmp(mode,"bad-dma")==0; g_bad_image = mode && std::strcmp(mode,"bad-image")==0; g_bad_mail = mode && std::strcmp(mode,"bad-mail")==0; g_masked_pump = mode && (std::strcmp(mode,"masked-pump")==0); g_skip_cache_publication = mode && std::strcmp(mode,"missing-cache")==0;
    g_bad_task_callback = mode && std::strcmp(mode,"bad-task-callback")==0;
    g_bad_task_span = mode && std::strcmp(mode,"bad-task-span")==0;
    g_global_masked_pump = mode && std::strcmp(mode,"global-masked-pump")==0;
    g_local_masked_pump = mode && std::strcmp(mode,"local-masked-pump")==0;
    g_status_masked_pump = mode && std::strcmp(mode,"status-masked-pump")==0;
    reset(); collect_descriptors();
    require(image_hash() == kDspImageHash, "AX DSP image hash mismatch");
    if (g_bad_image) {
        auto* d = find_desc(MELEE_WEB_AX_TAG_DSP_SLAVE);
        require(d != nullptr && d->bytes >= 2, "missing AX DSP image for bad-image case");
        reinterpret_cast<u16*>(static_cast<uintptr_t>(d->address))[0] ^= 1;
        require(image_hash() == kDspImageHash, "AX DSP image hash mismatch");
    }
    AIInit(nullptr); require(AICheckInit()!=FALSE, "source AIInit did not complete");
    AXInit();
    const auto* out = find_desc(MELEE_WEB_AX_TAG_OUT_BUFFER); require(out != nullptr, "missing AX output descriptor");
    require(g_ai_dma_enabled && g_ai_dma_length == kAiDmaBytes, "AXInit did not enable AI DMA");
    MeleeWebAxState out_state{};
    require(melee_web_ax_out_describe(nullptr, 0, &out_state) == 9 &&
            out_state.dsp_init_flag == 1 && out_state.out_dsp_ready == 1 &&
            out_state.dsp_task_state == 1 && out_state.dsp_task_flags == 1,
            "AX output state did not publish the initialized DSP task");
    require(g_dsp_mail_count == 10 && g_dsp_mail_step == 10, "AX DSP startup callback incomplete");
    std::printf("{\"kind\":\"source_ax_startup\",\"boundary\":\"first_ai_dma_enabled\",\"ai_init\":true,\"ax_init\":true,\"ai_dma_enabled\":true,\"ai_dma_bytes\":%u,\"dsp_boot_mails\":%u,\"dsp_init_callback\":%u,\"dsp_image_bytes\":%u,\"dsp_image_hash\":%u,\"runtime_claim\":false}\n", static_cast<unsigned>(g_ai_dma_length),static_cast<unsigned>(g_dsp_mail_count),static_cast<unsigned>(out_state.dsp_init_flag),static_cast<unsigned>(kDspImageBytes),static_cast<unsigned>(kDspImageHash));
    return 0;
}
} // namespace melee_web_source_ax

extern "C" int melee_web_source_ax_startup_run(const char* mode) {
    return melee_web_source_ax::run(mode);
}

int main(int argc, char** argv) { if (argc > 2) melee_web_source_ax::fail("unexpected AX fixture arguments"); return melee_web_source_ax_startup_run(argc == 2 ? argv[1] : ""); }
