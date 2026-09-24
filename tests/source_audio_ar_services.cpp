#include <dolphin.h>
#include "source_audio_ar_services.h"

#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <emscripten/stack.h>

static_assert(sizeof(void*) == 4 && sizeof(long) == 4,
              "source audio AR fixture requires Wasm32");
static_assert(sizeof(OSContext) == 0x2C8,
              "fixture must use the pinned SDK OSContext layout");
static_assert(sizeof(BOOL) == 4, "Dolphin BOOL ABI changed");
static_assert(sizeof(ARQRequest) == 0x20, "ARQRequest ABI changed");

namespace {
struct PlatformState {
    uint16_t regs[64]{};
    bool interrupts_enabled = true;
    MeleeWebSourceAudioArInterruptHandler handler = nullptr;
    bool interrupt_pending = false;
    OSContext context{};
    uint32_t size_cell = 0;
    unsigned dispatch_count = 0;
    bool callback_saw_masked = false;
    uint32_t unmask_mask = 0;
    OSContext* current_context = nullptr;
    unsigned context_clears = 0;
    unsigned context_sets = 0;
    unsigned cache_flushes = 0;
    unsigned cache_invalidates = 0;
    unsigned register_operations = 0;
    OSContext* fpu_context = nullptr;
    bool callback_saw_exception_context = false;
    OSInterruptMask global_mask = OS_INTERRUPTMASK_MEM | OS_INTERRUPTMASK_DSP |
                                  OS_INTERRUPTMASK_AI | OS_INTERRUPTMASK_EXI |
                                  OS_INTERRUPTMASK_PI;
    OSInterruptMask local_mask = 0;
    OSInterruptMask unmask_previous = 0;
};

PlatformState state;
MeleeWebSourceAudioArService service;
alignas(32) unsigned char aram[0x01000000];
alignas(32) unsigned char source[32];
ARQRequest* callback_request = nullptr;
unsigned callback_count = 0;
uintptr_t probe_stack_top = 0;
bool probe_active = false;
uint32_t stale_probe_address = 0;

struct CacheSpan {
    uint32_t address = 0;
    uint32_t length = 0;
    bool valid = false;
    bool published = false;
    unsigned char visible[32]{};
};
/* ARChecksize has exactly three temporary aligned source buffers. Their
 * registrations are deliberately short-lived and are cleared after ARInit.
 * The later ARQ request uses this separate borrowed source span. */
CacheSpan source_stack_spans[3];
CacheSpan borrowed_source_span;

constexpr uint32_t kMramToAram = ARAM_DIR_MRAM_TO_ARAM;
constexpr uint32_t kAramToMram = ARAM_DIR_ARAM_TO_MRAM;
constexpr unsigned kRegisterOperationBudget = 512;
constexpr uint16_t kDspArStatus = 0x0020;
constexpr uint16_t kDspArMask = 0x0040;
constexpr uint16_t kDspBusy = 0x0200;
constexpr uint16_t kDspStatusW1c = kDspArStatus;
constexpr uint16_t kDspKnownStatus = kDspArStatus | kDspArMask | kDspBusy;

void require(bool condition, const char* message);

bool contains(uintptr_t base, size_t size, uintptr_t address, size_t length)
{
    return address >= base && length <= size && address - base <= size - length;
}

CacheSpan* find_span(uintptr_t address, uint32_t length)
{
    for (CacheSpan& span : source_stack_spans)
        if (span.valid && span.address == static_cast<uint32_t>(address) &&
            span.length == length)
            return &span;
    if (borrowed_source_span.valid &&
        borrowed_source_span.address == static_cast<uint32_t>(address) &&
        borrowed_source_span.length == length)
        return &borrowed_source_span;
    return nullptr;
}

bool is_live_source_stack_span(uintptr_t address, uint32_t length)
{
    if (!probe_active || address > probe_stack_top)
        return false;
    const uintptr_t current = emscripten_stack_get_current();
    return address >= current &&
           static_cast<uintptr_t>(length) <= probe_stack_top - address;
}

CacheSpan* allocate_probe_span(uintptr_t address, uint32_t length)
{
    if (CacheSpan* existing = find_span(address, length))
        return existing;
    for (CacheSpan& span : source_stack_spans) {
        if (!span.valid) {
            span = {};
            span.address = static_cast<uint32_t>(address);
            span.length = length;
            span.valid = true;
            return &span;
        }
    }
    return nullptr;
}

void reject_overlapping_span(uintptr_t address, uint32_t length,
                             const CacheSpan* except)
{
    auto overlaps = [address, length](const CacheSpan& span) {
        if (!span.valid) return false;
        const uintptr_t end = address + length;
        const uintptr_t span_end = static_cast<uintptr_t>(span.address) + span.length;
        return address < span_end && static_cast<uintptr_t>(span.address) < end;
    };
    for (const CacheSpan& span : source_stack_spans)
        require(&span == except || !overlaps(span),
                "overlapping source cache spans");
    require(&borrowed_source_span == except || !overlaps(borrowed_source_span),
            "overlapping source cache spans");
}

void clear_probe_spans()
{
    for (CacheSpan& span : source_stack_spans)
        span = {};
}

[[noreturn]] void die(const char* message)
{
    std::fprintf(stderr, "source audio AR service: %s\n", message);
    std::abort();
}

void require(bool condition, const char* message)
{
    if (!condition) die(message);
}

uint16_t dsp_read(void* user, unsigned index)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(++platform->register_operations <= kRegisterOperationBudget,
            "DSP register operation budget exhausted");
    require(index == 5 || index == 9 || index == 11 || index == 13 ||
                (index >= 16 && index <= 21),
            "DSP read outside the declared AR register set");
    return platform->regs[index];
}

void dsp_write(void* user, unsigned index, uint16_t value)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(++platform->register_operations <= kRegisterOperationBudget,
            "DSP register operation budget exhausted");
    require(index == 5 || index == 9 || index == 13 ||
                (index >= 16 && index <= 21),
            "DSP write outside the declared AR register set");
    if (index != 5) {
        platform->regs[index] = value;
        return;
    }
    require((value & static_cast<uint16_t>(~kDspKnownStatus)) == 0,
            "DSP status write contains unsupported AR bits");
    const uint16_t old = platform->regs[5];
    /* The AR completion status bit is write-one-to-clear. Control and busy
     * bits retain the value written by the source handler. */
    platform->regs[5] = static_cast<uint16_t>(
        (old & kDspStatusW1c & static_cast<uint16_t>(~value)) |
        (value & static_cast<uint16_t>(~kDspStatusW1c)));
}

int disable_interrupts(void* user)
{
    auto* platform = static_cast<PlatformState*>(user);
    const int previous = platform->interrupts_enabled ? 1 : 0;
    platform->interrupts_enabled = false;
    return previous;
}

void restore_interrupts(void* user, int enabled)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(enabled == 0 || enabled == 1, "invalid interrupt state");
    platform->interrupts_enabled = enabled != 0;
}

void set_dma_busy(void* user, int busy)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(busy == 0 || busy == 1, "invalid AR DMA busy state");
    if (busy) platform->regs[5] |= kDspBusy;
    else platform->regs[5] &= static_cast<uint16_t>(~kDspBusy);
}

unsigned char* resolve_mainmem(void*, uint32_t address, uint32_t length,
                               MeleeWebSourceAudioArTransferType type)
{
    require(type == kMramToAram || type == kAramToMram,
            "AR DMA direction is outside the authored source contract");
    require(address && length == sizeof(CacheSpan::visible) &&
                !(address & 31u),
            "main-memory span is not an owned aligned source span");
    const uintptr_t numeric = static_cast<uintptr_t>(address);
    CacheSpan* span = find_span(numeric, length);
    require(span, "main-memory span is outside the owned cache spans");
    if (type == kMramToAram)
        require(span->published, "DMA consumed an unpublished source cache span");
    return span->visible;
}

void set_interrupt_handler(void* user, int exception,
                           MeleeWebSourceAudioArInterruptHandler handler)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(exception == 6 && handler, "unexpected AR interrupt handler");
    platform->handler = handler;
}

void raise_interrupt(void* user, int exception)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(exception == 6 && platform->handler,
            "AR interrupt was raised without an installed handler");
    require(platform->unmask_mask == OS_INTERRUPTMASK_DSP_ARAM,
            "AR interrupt was raised while exception 6 was masked");
    require(!(platform->global_mask & OS_INTERRUPTMASK_DSP_ARAM) &&
                !(platform->local_mask & OS_INTERRUPTMASK_DSP_ARAM),
            "AR interrupt was raised while its source mask was set");
    require((platform->regs[5] & kDspArMask) != 0,
            "AR interrupt was raised while the DSP AR bit was disabled");
    require(!platform->interrupt_pending,
            "AR interrupt was raised while a prior event was pending");
    require((platform->regs[5] & kDspArStatus) == 0,
            "AR interrupt status was still pending before a new event");
    platform->regs[5] |= kDspArStatus;
    platform->interrupt_pending = true;
}

void dispatch_interrupt(void* user, int exception)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(exception == 6 && platform->handler, "AR interrupt was not installed");
    require(platform->interrupt_pending,
            "AR interrupt was dispatched without a raised event");
    require(!platform->interrupts_enabled,
            "AR interrupt was dispatched outside its masked owner boundary");
    require((platform->regs[5] & kDspArStatus) != 0,
            "AR interrupt was dispatched without a DSP completion status");
    require((platform->regs[5] & kDspArMask) != 0,
            "AR interrupt was dispatched while the DSP AR bit was disabled");
    require(!(platform->global_mask & OS_INTERRUPTMASK_DSP_ARAM) &&
                !(platform->local_mask & OS_INTERRUPTMASK_DSP_ARAM),
            "AR interrupt was dispatched while its source mask was set");
    ++platform->dispatch_count;
    platform->current_context = &platform->context;
    platform->handler((short)exception, &platform->context);
    require(platform->current_context == &platform->context,
            "AR ISR did not restore the incoming OS context");
    require((platform->regs[5] & kDspArStatus) == 0,
            "AR ISR did not acknowledge DSP completion status");
    platform->interrupt_pending = false;
}

void clear_context(void* user, OSContext* context)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(context, "AR ISR cleared a null context");
    /* Pinned OSContext.c clears only mode/state and releases the FPU owner. */
    context->mode = 0;
    context->state = 0;
    if (platform->fpu_context == context)
        platform->fpu_context = nullptr;
    ++platform->context_clears;
}

void set_current_context(void* user, OSContext* context)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(context, "AR ISR installed a null context");
    platform->current_context = context;
    ++platform->context_sets;
}

void flush_range(void* user, void* address, uint32_t length)
{
    auto* platform = static_cast<PlatformState*>(user);
    const uintptr_t numeric = reinterpret_cast<uintptr_t>(address);
    require(length == sizeof(CacheSpan::visible) && !(numeric & 31u),
            "AR cache flush has an unsupported span");
    require(numeric <= UINTPTR_MAX - length,
            "AR cache flush span wraps the host address space");
    const bool borrowed = contains(reinterpret_cast<uintptr_t>(source),
                                   sizeof(source), numeric, length);
    require(borrowed || is_live_source_stack_span(numeric, length),
            "AR cache flush is outside the owned source span");
    CacheSpan* span = borrowed ? &borrowed_source_span
                               : allocate_probe_span(numeric, length);
    require(span, "AR cache flush span table is exhausted");
    reject_overlapping_span(numeric, length, span);
    span->address = static_cast<uint32_t>(numeric);
    span->length = length;
    span->valid = true;
    if (!borrowed)
        stale_probe_address = static_cast<uint32_t>(numeric);
    std::memcpy(span->visible, address, length);
    span->published = true;
    ++platform->cache_flushes;
}

void invalidate_range(void* user, void* address, uint32_t length)
{
    auto* platform = static_cast<PlatformState*>(user);
    CacheSpan* span = find_span(reinterpret_cast<uintptr_t>(address), length);
    require(span, "AR cache invalidate has no owned owner span");
    std::memcpy(address, span->visible, length);
    /* An inbound DMA makes the shadow current; a later outbound DMA still
     * requires a fresh source publication. */
    span->published = false;
    ++platform->cache_invalidates;
}

void* physical_to_uncached(void* user, uint32_t address)
{
    auto* platform = static_cast<PlatformState*>(user);
    require(address == 0xD0, "AR source requested an unsupported physical cell");
    return &platform->size_cell;
}

void abort_platform(void*, const char* message)
{
    die(message);
}

void callback(ARQRequest* request)
{
    callback_request = request;
    ++callback_count;
    state.callback_saw_masked = !state.interrupts_enabled;
    state.callback_saw_exception_context =
        state.current_context != &state.context && state.current_context &&
        state.current_context->mode == 0 && state.current_context->state == 0;
}
}

MeleeWebAudioArDsp melee_web_audio_ar_dsp;

extern "C" int melee_web_audio_ar_disable_interrupts(void)
{
    return disable_interrupts(&state);
}
extern "C" void melee_web_audio_ar_restore_interrupts(int enabled)
{
    restore_interrupts(&state, enabled);
}
extern "C" void melee_web_audio_ar_set_handler(
    int exception, void (*handler)(short, OSContext*))
{
    set_interrupt_handler(&state, exception, handler);
}
extern "C" void melee_web_audio_ar_clear_context(void* context)
{
    clear_context(&state, static_cast<OSContext*>(context));
}
extern "C" void melee_web_audio_ar_set_context(void* context)
{
    set_current_context(&state, static_cast<OSContext*>(context));
}
extern "C" void melee_web_audio_ar_flush(void* address, u32 length)
{
    flush_range(&state, address, length);
}
extern "C" void melee_web_audio_ar_invalidate(void* address, u32 length)
{
    invalidate_range(&state, address, length);
}
extern "C" void* melee_web_audio_ar_physical(u32 address)
{
    return physical_to_uncached(&state, address);
}
extern "C" void melee_web_audio_ar_abort(const char* message)
{
    abort_platform(&state, message);
}
extern "C" void melee_web_audio_ar_report(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

extern "C" void* melee_web_audio_ar_physical_cached(u32 address)
{
    auto* platform = &state;
    if (address == 0xC4u) return &platform->global_mask;
    if (address == 0xC8u) return &platform->local_mask;
    die("source OS interrupt code accessed an unsupported physical cell");
}

extern "C" u32 melee_web_audio_ar_cntlzw(u32 value)
{
    return value == 0 ? 32u : static_cast<u32>(__builtin_clz(value));
}

struct UnsupportedRegisterCell {
    operator u16() const
    {
        die("source AR interrupt code accessed an unsupported register");
    }
    UnsupportedRegisterCell& operator=(u16)
    {
        die("source AR interrupt code wrote an unsupported register");
    }
};

struct UnsupportedRegisterBank {
    UnsupportedRegisterCell operator[](unsigned) const { return {}; }
};

UnsupportedRegisterBank melee_web_audio_ar_mem_regs;
UnsupportedRegisterBank melee_web_audio_ar_ai_regs;
UnsupportedRegisterBank melee_web_audio_ar_exi_regs;
UnsupportedRegisterBank melee_web_audio_ar_pi_regs;

/* The test runner replaces this marker with the pinned OSInterrupt.c
 * SetInterruptMask/__OSUnmaskInterrupts bodies after verifying its hash. */
#undef __OSUnmaskInterrupts
#undef OSPhysicalToCached
#undef __cntlzw
#define __OSUnmaskInterrupts melee_web_source_os_unmask
#define OSPhysicalToCached(address) melee_web_audio_ar_physical_cached(address)
#define __cntlzw(value) melee_web_audio_ar_cntlzw(value)
#define __MEMRegs melee_web_audio_ar_mem_regs
#define __AIRegs melee_web_audio_ar_ai_regs
#define __EXIRegs melee_web_audio_ar_exi_regs
#define __PIRegs melee_web_audio_ar_pi_regs
extern "C" {
/* MELEE_WEB_PINNED_OS_INTERRUPT */
}
#undef __MEMRegs
#undef __AIRegs
#undef __EXIRegs
#undef __PIRegs
#undef OSPhysicalToCached
#undef __cntlzw
#undef __OSUnmaskInterrupts

extern "C" OSInterruptMask melee_web_source_os_unmask(OSInterruptMask);
extern "C" OSInterruptMask melee_web_audio_ar_unmask(OSInterruptMask mask)
{
    const OSInterruptMask previous = melee_web_source_os_unmask(mask);
    state.unmask_previous = previous;
    state.unmask_mask |= mask;
    return previous;
}

#define __OSUnmaskInterrupts melee_web_audio_ar_unmask

MeleeWebAudioArDspCell::operator u16() const
{
    return melee_web_source_audio_ar_dsp_read(&service, index);
}

MeleeWebAudioArDspCell& MeleeWebAudioArDspCell::operator=(u16 value)
{
    melee_web_source_audio_ar_dsp_write(&service, index, value);
    return *this;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbitwise-op-parentheses"
#pragma clang diagnostic ignored "-Wunused-parameter"
#define ARRegisterDMACallback melee_web_source_audio_ar_original_register
#define ARQCallback ARDMACallback
#ifndef MELEE_WEB_SOURCE_AR_PATH
#define MELEE_WEB_SOURCE_AR_PATH "../.deps/melee/extern/dolphin/src/dolphin/ar/ar.c"
#endif
#include MELEE_WEB_SOURCE_AR_PATH
#undef ARQCallback
#undef ARRegisterDMACallback

/* Keep ARRegisterDMACallback owned by the original ar.c translation unit. The
 * service phase is explicit and is changed only after source ARQInit. */
ARDMACallback ARRegisterDMACallback(ARDMACallback callback_value)
{
    return melee_web_source_audio_ar_original_register(callback_value);
}

#ifndef MELEE_WEB_SOURCE_ARQ_PATH
#define MELEE_WEB_SOURCE_ARQ_PATH "../.deps/melee/extern/dolphin/src/dolphin/ar/arq.c"
#endif
#include MELEE_WEB_SOURCE_ARQ_PATH
#pragma clang diagnostic pop

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "valid";
    require(argc <= 2, "source audio AR fixture accepts at most one mode");
    const bool known_mode = std::strcmp(mode, "valid") == 0 ||
        std::strcmp(mode, "no-inline") == 0 ||
        std::strcmp(mode, "missing-publication") == 0 ||
        std::strcmp(mode, "stale-probe-span") == 0 ||
        std::strcmp(mode, "cpu-masked-pump") == 0 ||
        std::strcmp(mode, "irq-masked-pump") == 0 ||
        std::strcmp(mode, "bad-span") == 0 ||
        std::strcmp(mode, "bad-mode") == 0;
    require(known_mode, "unknown source audio AR fixture mode");
    state = {};
    service = {};
    callback_request = nullptr;
    callback_count = 0;
    probe_stack_top = 0;
    probe_active = false;
    stale_probe_address = 0;
    MeleeWebSourceAudioArPlatform platform{};
    platform.user = &state;
    platform.dsp_read = dsp_read;
    platform.dsp_write = dsp_write;
    platform.disable_interrupts = disable_interrupts;
    platform.restore_interrupts = restore_interrupts;
    platform.set_dma_busy = set_dma_busy;
    platform.resolve_mainmem = resolve_mainmem;
    platform.set_interrupt_handler = set_interrupt_handler;
    platform.dispatch_interrupt = dispatch_interrupt;
    platform.raise_interrupt = raise_interrupt;
    platform.clear_context = clear_context;
    platform.set_current_context = set_current_context;
    platform.flush_range = flush_range;
    platform.invalidate_range = invalidate_range;
    platform.physical_to_uncached = physical_to_uncached;
    platform.abort = abort_platform;
    u32 stack[16] __attribute__((aligned(32))) = {};
    ARQRequest request = {};
    const uint32_t destination = 0x4000;

    state.regs[11] = 1;
    std::memset(aram, 0, sizeof(aram));
    clear_probe_spans();
    borrowed_source_span = {};
    for (unsigned index = 0; index < sizeof(source); ++index)
        source[index] = (unsigned char)(0x80 + index);
    require(melee_web_source_audio_ar_initialize(
                &service, &platform, aram, sizeof(aram), 0x04000000) == 0,
            "AR service initialization failed");
    probe_stack_top = emscripten_stack_get_current();
    probe_active = true;
    require(ARInit(stack, 16) == destination, "ARInit returned the wrong base");
    probe_active = false;
    clear_probe_spans();
    require(ARCheckInit() == 1 && ARGetSize() == sizeof(aram),
            "ARInit did not establish the declared 16 MiB profile");
    if (std::strcmp(mode, "bad-span") == 0) {
        (void)resolve_mainmem(&state,
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source)) + 1,
                              sizeof(source), MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM);
        die("misaligned source span was accepted");
    }
    if (std::strcmp(mode, "bad-mode") == 0) {
        (void)melee_web_source_audio_ar_set_phase(
            &service, static_cast<MeleeWebSourceAudioArPhase>(99));
        die("unsupported AR phase was accepted");
    }
    ARQInit();
    require(melee_web_source_audio_ar_set_phase(
                &service, MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED) == 0,
            "AR provider did not enter the explicit deferred ARQ phase");
    if (std::strcmp(mode, "stale-probe-span") == 0) {
        require(stale_probe_address != 0, "ARInit did not publish a probe span");
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, stale_probe_address, 0x4000,
                   sizeof(source));
        die("stale probe span was accepted after ARInit");
    }
    ARQSetChunkSize(32);
    state.context.mode = 0x1357;
    state.context.state = 0x2468;
    state.current_context = &state.context;
    if (std::strcmp(mode, "missing-publication") != 0)
        flush_range(&state, source, sizeof(source));
    ARQPostRequest(&request, 7, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)source, destination, sizeof(source), callback);
    require(callback_count == 0, "ARQ callback ran inline during submission");
    require(melee_web_source_audio_ar_pending(&service),
            "AR provider did not retain the deferred request");
    require((melee_web_source_audio_ar_dsp_read(&service, 5) & 0x200u) != 0,
            "deferred source ARQ request lost its DSP busy status");
    require(std::memcmp(aram + destination, source, sizeof(source)) != 0,
            "deferred ARQ transfer copied bytes inline during submission");
    if (std::strcmp(mode, "irq-masked-pump") == 0)
        state.global_mask |= OS_INTERRUPTMASK_DSP_ARAM;
    if (std::strcmp(mode, "cpu-masked-pump") == 0) {
        state.interrupts_enabled = false;
        require(melee_web_source_audio_ar_pump(&service) == -8,
                "CPU-masked AR pump was not rejected at its ownership boundary");
        state.interrupts_enabled = true;
    }
    require(melee_web_source_audio_ar_pump(&service) == 0,
            "AR provider completion pump failed");
    require(callback_count == 1 && callback_request == &request,
            "original ARQ callback did not run once");
    require(state.callback_saw_masked && state.dispatch_count == 1,
            "AR completion did not dispatch through the masked original handler");
    require(state.callback_saw_exception_context,
            "ARQ callback did not run under the cleared exception context");
    require(state.context_clears == 2 && state.context_sets == 2 &&
                state.unmask_mask == OS_INTERRUPTMASK_DSP_ARAM &&
                    state.current_context == &state.context,
            "AR completion did not use the owned SDK OSContext boundary");
    require(state.unmask_previous ==
                (OS_INTERRUPTMASK_MEM | OS_INTERRUPTMASK_DSP |
                 OS_INTERRUPTMASK_AI | OS_INTERRUPTMASK_EXI |
                 OS_INTERRUPTMASK_PI),
            "AR source did not preserve the previous interrupt mask");
    require((state.global_mask & OS_INTERRUPTMASK_DSP_ARAM) == 0 &&
                (state.local_mask & OS_INTERRUPTMASK_DSP_ARAM) == 0,
            "AR source left its interrupt source masked");
    require((state.regs[5] & kDspArStatus) == 0,
            "AR source left completion status pending after dispatch");
    require(state.context.mode == 0x1357 && state.context.state == 0x2468,
            "AR completion did not preserve the incoming OS context");
    require(state.cache_flushes >= 4 && state.cache_invalidates >= 6,
            "AR source cache ownership was not exercised");
    require(std::memcmp(aram + destination, source, sizeof(source)) == 0,
            "deferred ARQ transfer copied the wrong bytes");
    require(melee_web_source_audio_ar_submitted_count(&service) > 1,
            "ARInit probes were not submitted through the shared provider");
    require(!melee_web_source_audio_ar_pending(&service),
            "AR provider remained pending after completion");
    melee_web_source_audio_ar_shutdown(&service);
    std::printf("source audio AR service: passed\n");
    return 0;
}
