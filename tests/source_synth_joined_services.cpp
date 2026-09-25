/* Shared checked hardware owner for the original AX, AR/ARQ, AI/DSP, and
 * Synth/DevCom startup boundary.  Source bodies are injected at the markers;
 * this file contains no host-side audio success path. */
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>
#include <emscripten/stack.h>

#include <dolphin/types.h>
extern "C" {
#include <dolphin/os.h>
#include <dolphin/ai.h>
#include <dolphin/dsp.h>
#include <dolphin/ax.h>
#include <dolphin/axfx.h>
#include <dolphin/ar.h>
#include <dolphin/dvd.h>
#include <dolphin/hw_regs.h>
}
#include "source_ax_startup_accessors.h"
#include "source_synth_joined_services.h"
#include "source_audio_ar_services.h"

extern "C" unsigned melee_web_source_devcom_spans(
    u32* addresses, u32* lengths, unsigned capacity);
extern "C" void HSD_SynthInit(int dsp_size, int voices, int stream_size,
                               int bank_size);
extern "C" int HSD_Synth_804D6018;
#if defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
#include "source_lbaudio_startup_accessors.h"
extern "C" void lbAudioAx_8002838C(void);
#endif
/* The authored MSL bool is a four-byte int, unlike C++ bool. */
extern "C" int HSD_DevComIsBusy(int);
#if defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
extern "C" void melee_web_audio_program_check(const u32*)
{
    std::fputs("source lbAudio startup cannot execute an SFX command stream\n", stderr);
    std::abort();
}
#endif
extern "C" unsigned melee_web_source_synth_sram_reads(void);
extern "C" void melee_web_source_synth_sram_initialize(
    const unsigned char* settings, unsigned length);

static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4,
              "Dolphin integer ABI changed");
static_assert(sizeof(BOOL) == 4 && sizeof(OSTime) == 8,
              "Dolphin control ABI changed");
static_assert(sizeof(void*) == 4, "source Synth fixture requires Wasm32");
static_assert(sizeof(DSPTaskInfo) == 0x50,
              "Dolphin DSPTaskInfo ABI changed");

extern "C" void __DSP_debug_printf(const char* fmt, ...) { va_list a; va_start(a, fmt); std::vfprintf(stderr, fmt, a); va_end(a); }

namespace melee_web_source_synth_joined {
constexpr u32 kMmioLimit = 5000000;
constexpr u32 kPollLimit = 256;
constexpr u32 kDspImageHash = 0x4E8A8B21u;
constexpr u32 kDspImageBytes = 6624u;
constexpr u16 kDspStatusMask = 0x0100u;
constexpr u16 kDspStatusInterrupt = 0x0080u;
constexpr u16 kDspStatusW1C = 0x00A8u;
constexpr u16 kArDmaStatus = 0x0020u;
constexpr u16 kArDmaEnable = 0x0040u;
constexpr u16 kArDmaBusy = 0x0200u;
constexpr u32 kArInterruptMask = OS_INTERRUPTMASK_DSP_ARAM;
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
    std::fprintf(stderr, "source Synth joined startup failure: %s\n", message);
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

void set_dsp_status(u16 value);
void cache_transfer(void* pointer, u32 bytes, bool invalidate);
void* ar_physical_to_uncached(void*, uint32_t address);
bool is_ar_register(unsigned index)
{
    return index == 9 || index == 11 || index == 13 ||
           (index >= 16 && index <= 21);
}

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
bool g_prepared = false;
bool g_synth_started = false;
u64 g_sample_remainder = 0;
OSTime g_last_sample_time = 0;
u32 g_sample_reads = 0;
u32 g_sample_advances = 0;
OSContext g_caller_context{};
OSContext* g_current_context = &g_caller_context;

/* AR owns a separate physical backing but shares the DSP register, interrupt,
 * context, and cache owners above.  The source AR module is the only code
 * that decides when a DMA is submitted or completed. */
MeleeWebSourceAudioArService g_ar_service{};
MeleeWebSourceAudioArPlatform g_ar_platform{};
alignas(32) unsigned char g_aram[0x01000000];
u32 g_ar_size_cell = 0;
bool g_ar_probe_active = false;
u32 g_ar_probe_stack_top = 0;
bool g_ar_pending = false;
struct ArSpan {
    u32 address = 0;
    u32 length = 0;
    u64 generation = 0;
    bool valid = false;
    bool published = false;
    std::vector<u8> visible;
};
ArSpan g_ar_probe_spans[3];
ArSpan g_devcom_spans[2];
u64 g_next_span_generation = 1;
alignas(32) u32 g_fixture_ar_block_lengths[16]{};
u32* g_ar_block_lengths = g_fixture_ar_block_lengths;
u32 g_ar_submitted_before_synth = 0;
long g_audio_heap_before = -1, g_audio_heap_after = -1;

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

ArSpan* find_ar_span(u32 address, u32 bytes)
{
    for (ArSpan& span : g_devcom_spans) {
        if (span.valid && span_contains(span.address, span.length, address, bytes))
            return &span;
    }
    for (ArSpan& span : g_ar_probe_spans) {
        if (span.valid && span.address == address && span.length == bytes)
            return &span;
    }
    return nullptr;
}

bool ar_live_probe(u32 address, u32 bytes)
{
    if (!g_ar_probe_active || !address || address > g_ar_probe_stack_top)
        return false;
    const u32 current = static_cast<u32>(emscripten_stack_get_current());
    return address >= current && bytes <= g_ar_probe_stack_top - address;
}

ArSpan* allocate_ar_probe(u32 address, u32 bytes)
{
    if (ArSpan* existing = find_ar_span(address, bytes)) return existing;
    for (ArSpan& span : g_ar_probe_spans) {
        if (!span.valid) {
            span = {};
            span.address = address;
            span.length = bytes;
            span.generation = g_next_span_generation++;
            require(span.generation != 0, "AR cache span generation wrapped");
            span.valid = true;
            span.visible.resize(bytes);
            return &span;
        }
    }
    return nullptr;
}

bool ar_owned_static(u32 address, u32 bytes, ArSpan** result)
{
    for (ArSpan& span : g_devcom_spans) {
        if (span.valid && span_contains(span.address, span.length, address, bytes)) {
            if (result) *result = &span;
            return true;
        }
    }
    return false;
}

void collect_devcom_spans()
{
    const unsigned count = melee_web_source_devcom_spans(nullptr, nullptr, 0);
    require(count == 2, "source DevCom relay count changed");
    u32 addresses[2]{};
    u32 lengths[2]{};
    require(melee_web_source_devcom_spans(addresses, lengths, count) == count,
            "source DevCom relay descriptor query failed");
    for (unsigned i = 0; i < count; ++i) {
        require(addresses[i] != 0 && lengths[i] == 0x4000u &&
                    !(addresses[i] & 31u),
                "source DevCom relay span is not an authored aligned buffer");
        g_devcom_spans[i] = {};
        g_devcom_spans[i].address = addresses[i];
        g_devcom_spans[i].length = lengths[i];
        g_devcom_spans[i].generation = g_next_span_generation++;
        require(g_devcom_spans[i].generation != 0,
                "DevCom relay span generation wrapped");
        g_devcom_spans[i].visible.resize(lengths[i]);
        g_devcom_spans[i].valid = true;
        g_devcom_spans[i].published = false;
    }
}

void expire_ar_probe_spans()
{
    for (ArSpan& span : g_ar_probe_spans) {
        span.valid = false;
        span.published = false;
        span.visible.clear();
        span.generation = g_next_span_generation++;
    }
    g_ar_probe_active = false;
}

ArSpan* cache_span_for(void* pointer, u32 bytes)
{
    const u32 address = static_cast<u32>(reinterpret_cast<uintptr_t>(pointer));
    ArSpan* span = nullptr;
    if (ar_owned_static(address, bytes, &span)) return span;
    for (ArSpan& candidate : g_ar_probe_spans) {
        if (candidate.valid && span_contains(candidate.address, candidate.length,
                                             address, bytes)) {
            require(candidate.address == address && candidate.length == bytes,
                    "partial AR probe cache spans are unsupported");
            return &candidate;
        }
    }
    return nullptr;
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
    g_ar_service = {};
    g_ar_platform = {};
    g_ar_size_cell = 0;
    g_ar_probe_active = false;
    g_ar_probe_stack_top = 0;
    g_ar_pending = false;
    g_prepared = false;
    g_synth_started = false;
    std::memset(g_fixture_ar_block_lengths, 0, sizeof(g_fixture_ar_block_lengths));
    g_ar_block_lengths = g_fixture_ar_block_lengths;
    std::memset(g_aram, 0xA5, sizeof(g_aram));
    for (ArSpan& span : g_ar_probe_spans) span = {};
    for (ArSpan& span : g_devcom_spans) span = {};
    g_next_span_generation = 1;
    g_ar_submitted_before_synth = 0;
    g_ai_regs.values[0] = 0x42u;
    for (u32 i = 0; i < 32; ++i) { g_published[i].clear(); g_published_valid[i] = false; }
    std::memset(&g_caller_context, 0, sizeof(g_caller_context)); g_current_context = &g_caller_context;
}

} // namespace melee_web_source_synth_joined

#undef __AIRegs
#undef __DSPRegs
#undef __MEMRegs
#undef __EXIRegs
#undef __PIRegs
#define __AIRegs melee_web_source_synth_joined::g_ai_regs
#define __DSPRegs melee_web_source_synth_joined::g_dsp_regs
#define __MEMRegs melee_web_source_synth_joined::g_mem_registers
#define __EXIRegs melee_web_source_synth_joined::g_exi_registers
#define __PIRegs melee_web_source_synth_joined::g_pi_registers
#undef __OSBusClock
#define __OSBusClock (162000000u)
#undef OSPhysicalToCached
#define OSPhysicalToCached(address) melee_web_source_synth_joined::physical_cached(address)
#define __cntlzw(value) melee_web_source_synth_joined::cntlzw(value)

/* The exact source mask routines are inserted here. */
#define __OSUnmaskInterrupts melee_web_source_os_unmask
extern "C" {
/* MELEE_WEB_PINNED_OS_INTERRUPT */
}
#undef __OSUnmaskInterrupts

/* Original AR and ARQ share this translation unit so they observe the same
 * checked DSP register, interrupt, cache, and context owners.  The typed
 * header separates ar.c's no-argument DMA callback from arq.c's request
 * callback; the source bodies themselves remain unchanged. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitwise-op-parentheses"
#define ARRegisterDMACallback melee_web_source_synth_joined_original_register
#define ARInit melee_web_source_original_ar_init
#define ARQCallback ARDMACallback
#define OSPhysicalToUncached(address) melee_web_source_synth_joined::ar_physical_to_uncached(nullptr, address)
extern "C" {
/* MELEE_WEB_PINNED_AR_SOURCE */
}
#undef ARInit
#undef OSPhysicalToUncached
#undef ARQCallback
#undef ARRegisterDMACallback
extern "C" u32 ARInit(u32* block_lengths, u32 block_count)
{
    using namespace melee_web_source_synth_joined;
    require(block_lengths != nullptr && block_count == 16,
            "source ARInit requires its authored allocation stack");
    require(!g_ar_probe_active && ARCheckInit() == 0,
            "source ARInit may execute only once");
    g_ar_block_lengths = block_lengths;
    g_ar_probe_stack_top = static_cast<u32>(emscripten_stack_get_current());
    g_ar_probe_active = true;
    const u32 base = melee_web_source_original_ar_init(block_lengths, block_count);
    require(base == 0x4000u && ARCheckInit() != 0 && ARGetSize() == sizeof(g_aram),
            "source ARInit did not establish the declared ARAM profile");
    expire_ar_probe_spans();
    require(melee_web_source_audio_ar_set_phase(
                &g_ar_service, MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED) == 0,
            "source AR service did not enter deferred ARQ phase");
    g_ar_submitted_before_synth =
        melee_web_source_audio_ar_submitted_count(&g_ar_service);
    return base;
}

extern "C" ARDMACallback ARRegisterDMACallback(ARDMACallback callback)
{
    return melee_web_source_synth_joined_original_register(callback);
}
extern "C" {
/* MELEE_WEB_PINNED_ARQ_SOURCE */
}
#pragma clang diagnostic pop

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

namespace melee_web_source_synth_joined {

RegisterWord RegisterBank::operator[](unsigned index) {
    require(index == 0 || index == 1 || index == 2 || index == 3 || index == 5 ||
                is_ar_register(index) || index == 24 || index == 25 ||
                index == 27 || index == 29,
            "unsupported DSP register index"); return RegisterWord{this, index};
}
RegisterWord::operator u16() const {
    require(bank != nullptr, "null DSP register");
    if (g_ar_service.initialized && (index == 5 || is_ar_register(index))) {
        require(g_ar_service.initialized, "AR register used before its provider was initialized");
        return melee_web_source_audio_ar_dsp_read(&g_ar_service, index);
    }
    account();
    if (index == 0) return static_cast<u16>((bank->values[0] & 0x7FFFu) | (g_to_pending ? 0x8000u : 0));
    if (index == 2 && g_from_pending) return static_cast<u16>(g_from_mail >> 16);
    if (index == 3 && g_from_pending) { g_from_pending = false; return static_cast<u16>(g_from_mail); }
    return bank->values[index];
}
void set_dsp_status(u16 value) {
    require((value & ~static_cast<u16>(0x09F8u)) == 0, "unsupported DSP status bit");
    const u16 w1c = static_cast<u16>(kDspStatusW1C | kArDmaStatus);
    const u16 cleared = g_dsp_regs.values[5] & value & w1c;
    g_dsp_regs.values[5] = static_cast<u16>((g_dsp_regs.values[5] & w1c & ~value) |
                                             (value & ~w1c));
    if ((cleared & kDspStatusInterrupt) != 0) g_dsp_pending = false;
    if ((cleared & kArDmaStatus) != 0) g_ar_pending = false;
}

u16 ar_dsp_read(void*, unsigned index)
{
    require(index == 5 || is_ar_register(index),
            "AR service read escaped its authored DSP register set");
    account();
    return g_dsp_regs.values[index];
}

void ar_dsp_write(void*, unsigned index, u16 value)
{
    require(index == 5 || is_ar_register(index),
            "AR service write escaped its authored DSP register set");
    account();
    if (index == 5) set_dsp_status(value);
    else g_dsp_regs.values[index] = value;
}

int ar_disable_interrupts(void*)
{
    const int previous = g_interrupts_enabled ? 1 : 0;
    g_interrupts_enabled = FALSE;
    return previous;
}

void ar_restore_interrupts(void*, int enabled)
{
    require(enabled == 0 || enabled == 1, "invalid AR interrupt restore level");
    g_interrupts_enabled = enabled ? TRUE : FALSE;
}

void ar_set_dma_busy(void*, int busy)
{
    require(busy == 0 || busy == 1, "invalid AR DMA busy value");
    if (busy) g_dsp_regs.values[5] |= kArDmaBusy;
    else g_dsp_regs.values[5] &= static_cast<u16>(~kArDmaBusy);
}

unsigned char* ar_resolve_mainmem(void*, uint32_t address, uint32_t bytes,
                                  MeleeWebSourceAudioArTransferType type,
                                  uint64_t* generation)
{
    require(type == MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM ||
                type == MELEE_WEB_SOURCE_AUDIO_AR_ARAM_TO_MRAM,
            "AR DMA direction is outside the source contract");
    ArSpan* span = find_ar_span(address, bytes);
    require(span && span->valid && span->generation != 0,
            "AR DMA span is outside the owned source spans");
    require(generation != nullptr, "AR DMA span omitted its lifetime generation");
    if (type == MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM)
        require(span->published, "AR DMA consumed an unpublished source span");
    *generation = span->generation;
    return span->visible.data() + (address - span->address);
}

void ar_set_interrupt_handler(void*, int exception,
                              MeleeWebSourceAudioArInterruptHandler handler)
{
    require(exception == 6 && handler, "invalid AR interrupt handler");
    g_handlers[exception] = handler;
}

void ar_raise_interrupt(void*, int exception)
{
    require(exception == 6 && g_handlers[exception],
            "AR interrupt raised without a source handler");
    require(!g_interrupts_enabled, "AR interrupt raised outside masked owner");
    require(g_global_mask == (g_global_mask & ~kArInterruptMask) &&
                g_local_mask == (g_local_mask & ~kArInterruptMask),
            "AR interrupt raised while its source mask remained set");
    require((g_dsp_regs.values[5] & kArDmaEnable) != 0,
            "AR interrupt raised while DSP AR enable was clear");
    require((g_dsp_regs.values[5] & kArDmaStatus) == 0 && !g_ar_pending,
            "AR interrupt was raised while a prior event remained pending");
    g_dsp_regs.values[5] |= kArDmaStatus;
    g_ar_pending = true;
}

void ar_dispatch_interrupt(void*, int exception)
{
    require(exception == 6 && g_handlers[exception],
            "AR interrupt dispatched without a source handler");
    require(!g_interrupts_enabled && g_ar_pending,
            "AR interrupt dispatched outside its masked owner");
    require((g_dsp_regs.values[5] & (kArDmaStatus | kArDmaEnable)) ==
                (kArDmaStatus | kArDmaEnable),
            "AR interrupt missing status or enable bit");
    require((g_global_mask & kArInterruptMask) == 0 &&
                (g_local_mask & kArInterruptMask) == 0,
            "AR interrupt source remained masked at dispatch");
    OSContext* saved = g_current_context;
    g_current_context = &g_caller_context;
    g_handlers[exception](static_cast<short>(exception), g_current_context);
    require(g_current_context == &g_caller_context && saved == &g_caller_context,
            "AR handler did not preserve its incoming context");
    require((g_dsp_regs.values[5] & kArDmaStatus) == 0,
            "AR handler did not acknowledge completion status");
    g_current_context = saved;
    g_ar_pending = false;
}

void ar_clear_context(void*, OSContext* context)
{
    require(context != nullptr, "AR handler cleared a null context");
    context->mode = 0;
    context->state = 0;
}

void ar_set_current_context(void*, OSContext* context)
{
    require(context != nullptr, "AR handler installed a null context");
    g_current_context = context;
}

void* ar_physical_to_uncached(void*, uint32_t address)
{
    require(address == 0xD0u, "AR source requested an unsupported physical cell");
    return &g_ar_size_cell;
}

void ar_flush(void*, void* pointer, uint32_t bytes) { cache_transfer(pointer, bytes, false); }
void ar_invalidate(void*, void* pointer, uint32_t bytes) { cache_transfer(pointer, bytes, true); }
void ar_abort(void*, const char* message) { fail(message); }
RegisterWord& RegisterWord::operator=(u16 value) {
    require(bank != nullptr, "null DSP register");
    if (g_ar_service.initialized && (index == 5 || is_ar_register(index))) {
        require(g_ar_service.initialized, "AR register used before its provider was initialized");
        melee_web_source_audio_ar_dsp_write(&g_ar_service, index, value);
        return *this;
    }
    account();
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
    require(mask == kArInterruptMask || mask == OS_INTERRUPTMASK_DSP_DSP ||
            mask == OS_INTERRUPTMASK_DSP_AI ||
            mask == OS_INTERRUPTMASK_AI_AI || mask == (OS_INTERRUPTMASK_DSP_AI | OS_INTERRUPTMASK_AI_AI),
            "unsupported interrupt mask");
    auto old = melee_web_source_os_unmask(mask);
    require((g_global_mask & mask) == 0 && (g_local_mask & mask) == 0,
            "source interrupt unmask left global or local mask set");
    if ((mask & OS_INTERRUPTMASK_DSP_DSP) != 0)
        require((g_dsp_regs.values[5] & kDspStatusMask) != 0,
                "DSP interrupt unmask did not publish DSP control enable");
    if ((mask & kArInterruptMask) != 0)
        require((g_dsp_regs.values[5] & kArDmaEnable) != 0,
                "AR interrupt unmask did not publish DSP AR enable");
    if ((mask & OS_INTERRUPTMASK_DSP_AI) != 0)
        require((g_dsp_regs.values[5] & 0x0010u) != 0,
                "DSP AI interrupt unmask did not publish DSP control enable");
    if ((mask & OS_INTERRUPTMASK_AI_AI) != 0)
        require((g_ai_regs.values[0] & 0x0004u) != 0,
                "AI interrupt unmask did not publish AI control enable");
    return old;
}
/* Aurora's startup archive also defines OSGetTime.  The joined link routes
 * source calls through -Wl,--wrap=OSGetTime so this checked clock remains the
 * single owner without silently colliding with that archive symbol. */
extern "C" OSTime __wrap_OSGetTime(void) {
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
    ArSpan* span = cache_span_for(pointer, bytes);
    if (!span && !invalidate && ar_live_probe(address, bytes))
        span = allocate_ar_probe(address, bytes);
    require(span, invalidate ? "DCInvalidateRange used an unknown source span"
                             : "DCFlushRange used an unknown source span");
    const u32 offset = address - span->address;
    require(span->visible.size() == span->length &&
                span_contains(span->address, span->length, address, bytes),
            "source cache shadow has an invalid extent");
    if (invalidate) {
        std::memcpy(pointer, span->visible.data() + offset, bytes);
        span->published = false;
    } else {
        std::memcpy(span->visible.data() + offset, pointer, bytes);
        span->published = true;
    }
}
extern "C" void DCFlushRange(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, false); }
extern "C" void DCFlushRangeNoSync(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, false); }
extern "C" void DCStoreRange(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, false); }
extern "C" void DCInvalidateRange(void* ptr, u32 bytes) { cache_transfer(ptr, bytes, true); }
extern "C" void OSReport(char* fmt, ...) { va_list a; va_start(a,fmt); std::vfprintf(stderr,fmt,a); va_end(a); }
extern "C" void OSPanic(char* file,int line,char* fmt,...) { (void)file;(void)line; va_list a;va_start(a,fmt);std::vfprintf(stderr,fmt,a);va_end(a);fail("original source assertion"); }
extern "C" BOOL DVDFastOpen(s32, DVDFileInfo*) { fail("DVD service is unreachable in the Synth startup boundary"); }
extern "C" BOOL DVDReadAsyncPrio(DVDFileInfo*, void*, s32, s32, DVDCallback, s32) { fail("DVD service is unreachable in the Synth startup boundary"); }
extern "C" OSContext* OSGetCurrentContext(void) { return g_current_context; }
extern "C" void OSClearContext(OSContext* c) { if (!c) fail("null OS context"); c->mode=0;c->state=0; }
extern "C" void OSSetCurrentContext(OSContext* c) { g_current_context = c; }

int initialize_ar_service()
{
    g_ar_platform = {};
    g_ar_platform.user = nullptr;
    g_ar_platform.dsp_read = ar_dsp_read;
    g_ar_platform.dsp_write = ar_dsp_write;
    g_ar_platform.disable_interrupts = ar_disable_interrupts;
    g_ar_platform.restore_interrupts = ar_restore_interrupts;
    g_ar_platform.set_dma_busy = ar_set_dma_busy;
    g_ar_platform.resolve_mainmem = ar_resolve_mainmem;
    g_ar_platform.set_interrupt_handler = ar_set_interrupt_handler;
    g_ar_platform.dispatch_interrupt = ar_dispatch_interrupt;
    g_ar_platform.raise_interrupt = ar_raise_interrupt;
    g_ar_platform.clear_context = ar_clear_context;
    g_ar_platform.set_current_context = ar_set_current_context;
    g_ar_platform.flush_range = ar_flush;
    g_ar_platform.invalidate_range = ar_invalidate;
    g_ar_platform.physical_to_uncached = ar_physical_to_uncached;
    g_ar_platform.abort = ar_abort;
    return melee_web_source_audio_ar_initialize(
        &g_ar_service, &g_ar_platform, g_aram, sizeof(g_aram),
        MELEE_WEB_SOURCE_AUDIO_AR_DECLARED_ARAM_BUS_LENGTH);
}

extern "C" void DSPReset(void); extern "C" BOOL DSPCheckInit(void);
extern "C" void DSPInit(void); extern "C" DSPTaskInfo* DSPAddTask(DSPTaskInfo*);
extern "C" void AIInit(u8*); extern "C" void AXInit(void);

int prepare()
{
    require(!g_prepared, "joined source startup was prepared twice");
    reset();
    collect_descriptors();
    collect_devcom_spans();
    require(initialize_ar_service() == 0,
            "source AR service initialization failed");

    /* __ARChecksize waits for the authored SDRAM-ready bit before issuing its
     * five synchronous probes.  The AR service owns all later register I/O. */
    g_dsp_regs.values[11] = 1;
#if !defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
    ARInit(g_fixture_ar_block_lengths, 16);
    ARQInit();
#endif
    g_prepared = true;
    return 0;
}

int begin_synth(int dsp_size, int voices, int stream_size, int bank_size,
                const unsigned char* sram_settings, unsigned sram_length)
{
    require(g_prepared, "Synth entered before joined source preparation");
    require(!g_synth_started, "source Synth was entered twice");
    require(dsp_size >= 0 && voices >= 0 && stream_size >= 0 && bank_size >= 0,
            "source Synth driver arguments must be nonnegative");
    require(sram_settings != nullptr && sram_length == 64,
            "source Synth requires exactly 64 owned SRAM settings bytes");
    melee_web_source_synth_sram_initialize(sram_settings, sram_length);

    /* HSD_SynthInit is the source entry.  It calls AXInit itself, so this
     * boundary intentionally does not perform a separate AXInit first. */
#if !defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
    AIInit(nullptr);
#endif
    require(HSD_Synth_804D6018 >= 0, "source audio heap was not initialized");
    g_audio_heap_before = OSCheckHeap(HSD_Synth_804D6018);
    require(g_audio_heap_before > 0, "source audio heap failed its pre-Synth check");
#if defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
    lbAudioAx_8002838C();
    MeleeWebSourceLBAudioSnapshot audio{};
    require(melee_web_source_lbaudio_snapshot(&audio) == 1,
            "original lbAudio snapshot unavailable");
    require(audio.ar_stack_address == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_ar_block_lengths)) &&
                audio.ar_stack_entries == 16,
            "original lbAudio AR stack ownership mismatch");
    require(audio.driver_call_count == 1 &&
                audio.driver_voices == static_cast<uint32_t>(dsp_size) &&
                audio.driver_priority == static_cast<uint32_t>(voices) &&
                audio.driver_sample_rate == static_cast<uint32_t>(stream_size) &&
                audio.driver_aram_size == static_cast<uint32_t>(bank_size),
            "original lbAudio driver call differs from independently derived expected values");
    require(audio.fx_count == 2 && audio.fx_success[0] == 1 && audio.fx_success[1] == 1 &&
                audio.fx_sizes[0] == 53u * 1024u &&
                audio.fx_sizes[1] == 71u * 1024u &&
                audio.fx_addresses[0] != 0 && audio.fx_addresses[1] != 0 &&
                audio.fx_addresses[0] != audio.fx_addresses[1],
            "original lbAudio auxiliary buffer ownership mismatch");
    require(audio.bank_call_count == 3 && audio.bank_total_size == bank_size,
            "original lbAudio SFX bank partition mismatch");
    uint64_t bank_sum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        require(audio.bank_descriptor_sizes[i] > 0 &&
                    audio.bank_call_sizes[i] == static_cast<uint32_t>(audio.bank_descriptor_sizes[i]),
                "original lbAudio bank call differs from authored descriptor");
        bank_sum += audio.bank_call_sizes[i];
    }
    require(bank_sum == static_cast<uint32_t>(bank_size),
            "original lbAudio bank sizes do not cover the derived reservation");
    require(audio.sfx_state_counters[0] == -1 && audio.sfx_state_counters[1] == 0 &&
                audio.sfx_state_counters[2] == 0 && audio.sfx_state_counters[3] == 0,
            "original lbAudio SFX counters were not initialized");
    for (unsigned table = 0; table < 4; ++table)
        for (unsigned i = 0; i < 0x38; ++i)
            require(audio.bookkeeping[table][i] == -1,
                    "original lbAudio SFX bookkeeping was not initialized");
    std::printf("{\"kind\":\"source_lbaudio_startup\",\"driver_calls\":%u,\"fx_bytes\":[%u,%u],\"bank_sizes\":[%u,%u,%u],\"bookkeeping_entries\":224,\"runtime_claim\":false}\n",
                audio.driver_call_count, audio.fx_sizes[0], audio.fx_sizes[1],
                audio.bank_call_sizes[0], audio.bank_call_sizes[1], audio.bank_call_sizes[2]);
#else
    HSD_SynthInit(dsp_size, voices, stream_size, bank_size);
#endif
    require(AICheckInit() != FALSE, "source AIInit did not complete");
    g_audio_heap_after = OSCheckHeap(HSD_Synth_804D6018);
    require(g_audio_heap_after >= 0 && g_audio_heap_after < g_audio_heap_before,
            "source DevCom did not allocate from the original audio heap");
    require(__AR_FreeBlocks == 13 && g_ar_block_lengths[0] == 0x500 &&
                g_ar_block_lengths[1] == static_cast<u32>(bank_size) &&
                g_ar_block_lengths[2] == 0x30000 &&
                __AR_StackPointer == 0x4000u + 0x500u + static_cast<u32>(bank_size) + 0x30000u,
            "source Synth AR reservation sequence disagrees with authored allocations");
    require(HSD_DevComIsBusy(3) != 0 && melee_web_source_synth_sram_reads() == 1,
            "source Synth request or sound-mode read did not reach its boundary");
    for (u32 i = 0; i < g_ar_block_lengths[0]; ++i)
        require(g_aram[0x4000u + i] == 0xA5, "source Synth DMA copied before completion pump");
    g_synth_started = true;
    require(melee_web_source_audio_ar_pending(&g_ar_service) != 0,
            "source Synth did not submit its deferred DevCom AR request");
    return 0;
}

int pump()
{
    require(g_synth_started, "ARQ pump entered before source Synth");
    require(melee_web_source_audio_ar_pending(&g_ar_service) != 0,
            "source ARQ pump had no deferred request");
    return melee_web_source_audio_ar_pump(&g_ar_service);
}

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

int run(int dsp_size, int voices, int stream_size, int bank_size,
        const unsigned char* sram_settings, unsigned sram_length,
        const char* mode)
{
    const char* selected = mode ? mode : "valid";
#if defined(MELEE_WEB_SOURCE_LBAUDIO_STARTUP)
    if (std::strcmp(selected, "unsupported-chorus-init") == 0) AXFXChorusInit(nullptr);
    if (std::strcmp(selected, "unsupported-chorus-shutdown") == 0) AXFXChorusShutdown(nullptr);
    if (std::strcmp(selected, "unsupported-chorus-callback") == 0) AXFXChorusCallback(nullptr, nullptr);
    if (std::strcmp(selected, "unsupported-reverb-hi-init") == 0) AXFXReverbHiInit(nullptr);
    if (std::strcmp(selected, "unsupported-reverb-hi-shutdown") == 0) AXFXReverbHiShutdown(nullptr);
    if (std::strcmp(selected, "unsupported-reverb-hi-callback") == 0) AXFXReverbHiCallback(nullptr, nullptr);
    if (std::strcmp(selected, "unexpected-sfx-command") == 0) melee_web_audio_program_check(nullptr);
#endif
    require(std::strcmp(selected, "") == 0 ||
                std::strcmp(selected, "valid") == 0 ||
                std::strcmp(selected, "bad-dma") == 0 ||
                std::strcmp(selected, "bad-image") == 0 ||
                std::strcmp(selected, "bad-mail") == 0 ||
                std::strcmp(selected, "masked-pump") == 0 ||
                std::strcmp(selected, "missing-cache") == 0 ||
                std::strcmp(selected, "bad-task-callback") == 0 ||
                std::strcmp(selected, "bad-task-span") == 0 ||
                std::strcmp(selected, "global-masked-pump") == 0 ||
                std::strcmp(selected, "local-masked-pump") == 0 ||
                std::strcmp(selected, "status-masked-pump") == 0,
            "unknown joined Synth startup mode");
    g_bad_dma = std::strcmp(selected, "bad-dma") == 0;
    g_bad_image = std::strcmp(selected, "bad-image") == 0;
    g_bad_mail = std::strcmp(selected, "bad-mail") == 0;
    g_masked_pump = std::strcmp(selected, "masked-pump") == 0;
    g_skip_cache_publication = std::strcmp(selected, "missing-cache") == 0;
    g_bad_task_callback = std::strcmp(selected, "bad-task-callback") == 0;
    g_bad_task_span = std::strcmp(selected, "bad-task-span") == 0;
    g_global_masked_pump = std::strcmp(selected, "global-masked-pump") == 0;
    g_local_masked_pump = std::strcmp(selected, "local-masked-pump") == 0;
    g_status_masked_pump = std::strcmp(selected, "status-masked-pump") == 0;

    prepare();
    require(image_hash() == kDspImageHash, "AX DSP image hash mismatch");
    if (g_bad_image) {
        const auto* image = find_desc(MELEE_WEB_AX_TAG_DSP_SLAVE);
        require(image != nullptr && image->bytes >= 2,
                "missing AX DSP image for bad-image case");
        reinterpret_cast<u16*>(static_cast<uintptr_t>(image->address))[0] ^= 1;
        require(image_hash() == kDspImageHash,
                "AX DSP image mutation did not reach the source validator");
    }

    begin_synth(dsp_size, voices, stream_size, bank_size,
                sram_settings, sram_length);
    unsigned pumps = 0;
    while (melee_web_source_audio_ar_pending(&g_ar_service) != 0) {
        require(pumps < 16, "source Synth ARQ completion exceeded the fixture budget");
        require(pump() == 0, "source Synth ARQ completion failed");
        ++pumps;
    }
    require(g_ar_pending == false && HSD_DevComIsBusy(3) == 0,
            "source AR or DevCom completion remained pending");
    for (u32 i = 0; i < g_ar_block_lengths[0]; ++i)
        require(g_aram[0x4000u + i] == 0, "source Synth DevCom clear did not reach ARAM");
    require(g_aram[0x3FFF] == 0xA5 && g_aram[0x4000u + g_ar_block_lengths[0]] == 0xA5,
            "source Synth DMA wrote outside its reservation");
    require(OSCheckHeap(HSD_Synth_804D6018) == g_audio_heap_after,
            "source completion changed the allocated DevCom node block");
    std::printf("{\"kind\":\"source_synth_allocations\",\"aram_blocks\":[%u,%u,%u],\"aram_next\":%u,\"aram_free_blocks\":%u,\"audio_heap_before\":%ld,\"audio_heap_after\":%ld,\"sram_reads\":%u,\"deferred_bytes_verified\":%u}\n",
                static_cast<unsigned>(g_ar_block_lengths[0]), static_cast<unsigned>(g_ar_block_lengths[1]), static_cast<unsigned>(g_ar_block_lengths[2]),
                static_cast<unsigned>(__AR_StackPointer), static_cast<unsigned>(__AR_FreeBlocks), g_audio_heap_before, g_audio_heap_after,
                melee_web_source_synth_sram_reads(), static_cast<unsigned>(g_ar_block_lengths[0]));
    const auto* output = find_desc(MELEE_WEB_AX_TAG_OUT_BUFFER);
    require(output != nullptr && g_ai_dma_enabled &&
                g_ai_dma_length == kAiDmaBytes,
            "source AX path did not enable its authored AI DMA");
    MeleeWebAxState output_state{};
    require(melee_web_ax_out_describe(nullptr, 0, &output_state) == 9 &&
                output_state.dsp_init_flag == 1 && output_state.out_dsp_ready == 1 &&
                output_state.dsp_task_state == 1 && output_state.dsp_task_flags == 1,
            "source AX output state did not publish the initialized DSP task");
    require(g_dsp_mail_count == 10 && g_dsp_mail_step == 10,
            "source AX DSP startup callback incomplete");
    std::printf(
        "{\"kind\":\"source_synth_joined\",\"boundary\":\"synth_devcom_arq_complete\","
        "\"ai_init\":true,\"ax_init_owned_by_synth\":true,\"ai_dma_enabled\":true,"
        "\"ai_dma_bytes\":%u,\"dsp_boot_mails\":%u,\"dsp_init_callback\":%u,"
        "\"dsp_image_bytes\":%u,\"dsp_image_hash\":%u,\"ar_init_submissions\":%u,"
        "\"synth_arq_submissions\":%u,\"arq_pumps\":%u,\"runtime_claim\":false}\n",
        static_cast<unsigned>(g_ai_dma_length),
        static_cast<unsigned>(g_dsp_mail_count),
        static_cast<unsigned>(output_state.dsp_init_flag),
        static_cast<unsigned>(kDspImageBytes),
        static_cast<unsigned>(kDspImageHash),
        static_cast<unsigned>(g_ar_submitted_before_synth),
        static_cast<unsigned>(melee_web_source_audio_ar_submitted_count(&g_ar_service) -
                              g_ar_submitted_before_synth),
        pumps);
    return 0;
}

} // namespace melee_web_source_synth_joined

extern "C" int melee_web_source_synth_joined_run(
    int dsp_size, int voices, int stream_size, int bank_size,
    const unsigned char* sram_settings, unsigned sram_length,
    const char* mode)
{
    return melee_web_source_synth_joined::run(
        dsp_size, voices, stream_size, bank_size,
        sram_settings, sram_length, mode);
}

extern "C" int melee_web_source_synth_joined_prepare(void)
{
    return melee_web_source_synth_joined::prepare();
}

extern "C" int melee_web_source_synth_joined_begin_synth(
    int dsp_size, int voices, int stream_size, int bank_size,
    const unsigned char* sram_settings, unsigned sram_length)
{
    return melee_web_source_synth_joined::begin_synth(
        dsp_size, voices, stream_size, bank_size,
        sram_settings, sram_length);
}

extern "C" int melee_web_source_synth_joined_pump(void)
{
    return melee_web_source_synth_joined::pump();
}

extern "C" void melee_web_source_synth_aram_state(unsigned* stack, unsigned* free_blocks)
{
    using namespace melee_web_source_synth_joined;
    require(g_synth_started && stack != nullptr && free_blocks != nullptr,
            "AR state read requires the completed source startup owner");
    *stack = static_cast<unsigned>(__AR_StackPointer);
    *free_blocks = static_cast<unsigned>(__AR_FreeBlocks);
}
