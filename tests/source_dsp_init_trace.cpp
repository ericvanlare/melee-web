
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <dolphin.h>
#include <dolphin/dsp.h>
#include <dolphin/hw_regs.h>

extern "C" {
u16 melee_web_dsp_read(unsigned index);
void melee_web_dsp_write(unsigned index, u16 value);
}

/* The pinned source reads status registers directly.  The fixture-only
 * include path supplies a typed checked proxy for __DSPRegs, so the original
 * mailbox functions and wait loops remain unchanged in the generated TU. */
MeleeWebDspRegisterBank melee_web_dsp_regs;
/* MELEE_WEB_PINNED_DSP_SOURCES */


void __DSP_debug_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

extern DSPTaskInfo* __DSP_curr_task;
void __DSPHandler(__OSInterrupt, OSContext*);

namespace {
using InterruptHandler = __OSInterruptHandler;

static_assert(sizeof(void*) == 4, "DSP source fixture requires Wasm32");
static_assert(sizeof(u16) == 2 && sizeof(u32) == 4,
              "Dolphin integer widths changed");
static_assert(sizeof(BOOL) == 4 && sizeof(OSTime) == 8,
              "Dolphin BOOL/OSTime ABI changed");
static_assert(sizeof(DSPTaskInfo) == 0x50 &&
                  offsetof(DSPTaskInfo, init_cb) == 0x28 &&
                  offsetof(DSPTaskInfo, next) == 0x38 &&
                  offsetof(DSPTaskInfo, prev) == 0x3C,
              "Dolphin DSPTaskInfo ABI changed");

constexpr u32 kBootMail = 0x8071FEEDu;
constexpr u32 kDspInit = 0xDCD10000u;
constexpr u32 kDspInterruptMask = OS_INTERRUPTMASK_DSP_DSP;
constexpr u32 kDspInitVector = 0x10u;
constexpr u32 kDspIramAddress = 0u;
constexpr u32 kDspDramAddress = 0u;
constexpr u32 kDspDramLength = 0x2000u;
constexpr u32 kMailMarkerIramSource = 0x80F3A001u;
constexpr u32 kMailMarkerIramAddress = 0x80F3C002u;
constexpr u32 kMailMarkerIramLength = 0x80F3A002u;
constexpr u32 kMailMarkerDramLength = 0x80F3B002u;
constexpr u32 kMailMarkerStart = 0x80F3D001u;
constexpr u32 kMaxMailboxPolls = 128u;
constexpr u16 kDspStatusInterrupt = 0x0080u;
constexpr u16 kDspStatusDspMask = 0x0100u;
constexpr u16 kDspStatusControlMask = 0x01F8u;
constexpr u16 kDspStatusW1C = 0x00A8u;
constexpr u16 kDspStatusKnown = kDspStatusControlMask | 0x0800u;
constexpr OSInterruptMask kInitialDisabledMask =
    OS_INTERRUPTMASK_MEM | OS_INTERRUPTMASK_DSP | OS_INTERRUPTMASK_AI |
    OS_INTERRUPTMASK_EXI | OS_INTERRUPTMASK_PI;

struct FixtureState {
    bool interrupts_enabled = true;
    bool handler_installed = false;
    bool dsp_unmasked = false;
    bool callback_seen = false;
    bool callback_saw_masked = false;
    bool from_pending = true;
    bool from_high_read = false;
    u32 from_mail = kBootMail;
    u16 to_high = 0;
    u16 to_low = 0;
    bool to_pending = false;
    bool to_high_written = false;
    u16 dsp_status = 0;
    u32 dsp_status_reads = 0;
    u32 dsp_status_writes = 0;
    u16 dsp_status_w1c_cleared = 0;
    bool dsp_mask_hardware_enabled = false;
    OSInterruptMask global_mask = kInitialDisabledMask;
    OSInterruptMask local_mask = 0;
    OSInterruptMask unmask_previous = 0;
    bool pending_interrupt = false;
    bool stall_to = false;
    u32 expected_step = 0;
    u32 sent_count = 0;
    u32 poll_count = 0;
    u32 callback_count = 0;
    u32 clear_count = 0;
    u32 set_count = 0;
    bool bad_image_pointer = false;
    bool bad_image_word = false;
    bool bad_span = false;
    bool masked_pump = false;
    bool repeat_pump = false;
    OSContext* current_context = nullptr;
    OSContext* original_context = nullptr;
    OSContext* exception_context = nullptr;
    InterruptHandler handler = nullptr;
    std::string failure;
} state;

[[noreturn]] void fail(const char* message) {
    if (state.failure.empty()) state.failure = message;
    std::fprintf(stderr, "DSP fixture failure: %s\n", message);
    std::abort();
}

u32 melee_web_source_cntlzw(u32 value) {
    return value == 0 ? 32u : static_cast<u32>(__builtin_clz(value));
}

void* melee_web_source_physical_to_cached(u32 address) {
    if (address == 0x00C4u)
        return &state.global_mask;
    if (address == 0x00C8u)
        return &state.local_mask;
    fail("source OS interrupt code accessed an unsupported physical cell");
}

void require(bool condition, const char* message) {
    if (!condition) fail(message);
}

void queue_from_mail(u32 mail) {
    require(!state.from_pending, "mailbox overwrite before source read");
    state.from_mail = mail;
    state.from_pending = true;
    state.from_high_read = false;
}

u32 image_address() {
    return static_cast<u32>(reinterpret_cast<uintptr_t>(axDspSlave));
}

u16 read_register(unsigned index) {
    require(index == 0 || index == 1 || index == 2 || index == 3 || index == 5,
            "DSP source accessed an unsupported MMIO register");
    switch (index) {
    case 0: return state.to_high | (state.to_pending ? 0x8000u : 0u);
    case 1: return state.to_low;
    case 2:
        if (state.from_pending)
            state.from_high_read = true;
        return state.from_pending ? static_cast<u16>((state.from_mail >> 16) | 0x8000u)
                                  : 0;
    case 3:
        require(state.from_pending && state.from_high_read,
                "DSP read source mailbox low word out of order");
        state.from_pending = false;
        state.from_high_read = false;
        return static_cast<u16>(state.from_mail);
    case 5:
        require(state.dsp_status_reads < 4,
                "DSP control register was read outside the init profile");
        if (state.dsp_status_reads == 0)
            require(state.dsp_status == 0,
                    "DSP control/status did not start cleared");
        else if (state.dsp_status_reads == 1)
            require(state.dsp_status == kDspStatusDspMask,
                    "DSP interrupt mask was not published by OSUnmask");
        else if (state.dsp_status_reads == 2)
            require(state.dsp_status == (kDspStatusDspMask | 0x0800u),
                    "DSP control/status changed outside the init profile");
        else
            require(state.dsp_status == (kDspStatusDspMask | 0x0800u |
                                         kDspStatusInterrupt),
                    "DSP interrupt status was not published before dispatch");
        ++state.dsp_status_reads;
        return state.dsp_status;
    default: fail("unreachable DSP register"); return 0;
    }
}

void process_host_mail(u32 mail) {
    ++state.sent_count;
    switch (state.expected_step++) {
    case 0: require(mail == kMailMarkerIramSource, "wrong IRAM source marker"); break;
    case 1:
        require(mail == image_address(), "AX image pointer is not the owned image");
        break;
    case 2: require(mail == kMailMarkerIramAddress, "wrong IRAM address marker"); break;
    case 3: require(mail == kDspIramAddress, "wrong IRAM address"); break;
    case 4: require(mail == kMailMarkerIramLength, "wrong IRAM length marker"); break;
    case 5: require(mail == axDspSlaveLength, "wrong AX image length"); break;
    case 6: require(mail == kMailMarkerDramLength, "wrong DRAM length marker"); break;
    case 7: require(mail == 0, "unexpected DRAM source length"); break;
    case 8: require(mail == kMailMarkerStart, "wrong DSP start marker"); break;
    case 9:
        require(mail == kDspInitVector, "wrong DSP init vector");
        require(state.handler_installed && state.dsp_unmasked,
                "DSP callback before handler/mask setup");
        queue_from_mail(kDspInit);
        require(state.dsp_mask_hardware_enabled,
                "DSP init interrupt was queued before hardware unmask");
        state.dsp_status |= kDspStatusInterrupt;
        require(state.handler != nullptr, "DSP handler was not installed");
        state.pending_interrupt = true;
        break;
    default: fail("unexpected extra DSP host mail");
    }
}

u16 melee_web_dsp_read_impl(unsigned index) {
    require(++state.poll_count <= kMaxMailboxPolls,
            "DSP mailbox poll bound exceeded");
    if (index == 0 && !state.to_pending)
        return static_cast<u16>(state.to_high & ~0x8000u);
    return read_register(index);
}

void write_dsp_status(u16 value) {
    require(state.dsp_status_writes < 4,
            "DSP control register was written outside the init profile");
    require((value & ~kDspStatusKnown) == 0,
            "DSP control/status wrote an unsupported hardware bit");
    const u16 cleared = state.dsp_status & value & kDspStatusW1C;
    state.dsp_status_w1c_cleared |= cleared;
    state.dsp_status = static_cast<u16>(
        (state.dsp_status & kDspStatusW1C & ~value) |
        (value & static_cast<u16>(~kDspStatusW1C)));
    ++state.dsp_status_writes;
    state.dsp_mask_hardware_enabled =
        (state.dsp_status & kDspStatusDspMask) != 0;
}

void melee_web_dsp_write_impl(unsigned index, u16 value) {
    require(index == 0 || index == 1 || index == 2 || index == 3 || index == 5,
            "DSP source wrote an unsupported MMIO register");
    switch (index) {
    case 0:
        require(!state.to_high_written, "DSP host mailbox high word was overwritten");
        state.to_high = value;
        state.to_high_written = true;
        break;
    case 1:
        require(state.to_high_written, "DSP host mailbox low word arrived first");
        state.to_low = value;
        state.to_high_written = false;
        state.to_pending = true;
        process_host_mail((static_cast<u32>(state.to_high) << 16) | state.to_low);
        if (!state.stall_to)
            state.to_pending = false;
        break;
    case 2:
    case 3: fail("DSP source wrote a read-only DSP mailbox");
    case 5:
        require(state.dsp_status_writes < 4,
                "DSP control register was written outside the init profile");
        write_dsp_status(value);
        break;
    default: fail("unreachable DSP register");
    }
}

void pump_interrupt() {
    /* A hardware DSP interrupt is delivered after DSPAddTask has returned and
     * the source has restored its interrupt level. */
    require(state.pending_interrupt, "DSP init interrupt was not queued");
    require(state.interrupts_enabled, "DSP interrupt dispatched while masked");
    require(state.handler != nullptr, "DSP interrupt handler was not installed");
    require(state.handler == &__DSPHandler,
            "DSP interrupt handler was not the original __DSPHandler");
    require(__DSP_curr_task != nullptr && __DSP_curr_task->state == 0 &&
                __DSP_curr_task->flags == 1,
            "DSP init interrupt arrived before task publication");
    state.pending_interrupt = false;
    const BOOL old = state.interrupts_enabled ? TRUE : FALSE;
    state.interrupts_enabled = false;
    state.handler(7, state.original_context);
    state.interrupts_enabled = old != FALSE;
}

extern "C" BOOL OSDisableInterrupts(void) {
    const BOOL old = state.interrupts_enabled ? TRUE : FALSE;
    state.interrupts_enabled = false;
    return old;
}

extern "C" BOOL OSRestoreInterrupts(BOOL enabled) {
    const BOOL old = state.interrupts_enabled ? TRUE : FALSE;
    state.interrupts_enabled = enabled != FALSE;
    return old;
}

extern "C" __OSInterruptHandler __OSSetInterruptHandler(
    __OSInterrupt interrupt, __OSInterruptHandler handler) {
    require(interrupt == 7, "DSPInit installed a non-DSP interrupt handler");
    InterruptHandler previous = state.handler;
    state.handler = handler;
    state.handler_installed = handler != nullptr;
    return previous;
}

#define __OSUnmaskInterrupts melee_web_source_os_unmask
#define OSPhysicalToCached(address) melee_web_source_physical_to_cached(address)
#define __cntlzw(value) melee_web_source_cntlzw(value)
extern "C" {
/* MELEE_WEB_PINNED_OS_INTERRUPT */
}
#undef __OSUnmaskInterrupts
#undef OSPhysicalToCached
#undef __cntlzw

extern "C" OSInterruptMask melee_web_source_os_unmask(OSInterruptMask);

extern "C" OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) {
    require(mask == kDspInterruptMask, "DSPInit changed the DSP interrupt mask");
    const OSInterruptMask previous =
        melee_web_source_os_unmask(mask);
    state.unmask_previous = previous;
    state.dsp_unmasked = true;
    return previous;
}

extern "C" OSContext* OSGetCurrentContext(void) { return state.current_context; }

extern "C" void OSClearContext(OSContext* context) {
    require(context != nullptr, "DSP handler cleared a null context");
    ++state.clear_count;
    require(context != state.original_context,
            "DSP handler cleared the owned caller context");
    if (state.exception_context == nullptr)
        state.exception_context = context;
    require(context == state.exception_context,
            "DSP handler changed its temporary exception context");
    context->mode = 0;
    context->state = 0;
}

extern "C" void OSSetCurrentContext(OSContext* context) {
    require(context != nullptr, "DSP handler installed a null context");
    ++state.set_count;
    if (state.exception_context == nullptr && context != state.original_context) {
        state.exception_context = context;
    }
    if (context != state.original_context)
        require(context == state.exception_context,
                "DSP handler installed a different temporary context");
    state.current_context = context;
}

extern "C" void OSReport(char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

extern "C" void OSPanic(char* file, int line, char* message, ...) {
    (void)file;
    (void)line;
    va_list args;
    va_start(args, message);
    std::vfprintf(stderr, message, args);
    va_end(args);
    fail("original Dolphin assertion");
}

extern "C" void melee_web_init_callback(void* task) {
    require(task == __DSP_curr_task,
            "DSP init callback received the wrong task identity");
    require(__DSP_curr_task->state == 1 && __DSP_curr_task->flags == 1,
            "DSP init callback observed the wrong published task state");
    require(state.current_context == state.exception_context,
            "DSP init callback did not run on the temporary exception context");
    require(!state.interrupts_enabled,
            "DSP init callback ran with the DSP interrupt still enabled");
    ++state.callback_count;
    state.callback_seen = true;
    state.callback_saw_masked = true;
}

void run_fixture() {
    const uintptr_t image = reinterpret_cast<uintptr_t>(axDspSlave);
    require(image != 0 && image <= UINT32_MAX &&
                image <= UINT32_MAX - static_cast<u32>(axDspSlaveLength),
            "AX image span is outside checked Wasm32 memory");
    require((image & 31u) == 0, "AX image is not 32-byte aligned");
    require(static_cast<u32>(axDspSlaveLength) == 6624u,
            "unexpected original AX image length");
    if (state.bad_image_word)
        axDspSlave[0] ^= 1;
    u32 ector_hash = 0;
    for (u32 index = 0; index < axDspSlaveLength / 2; ++index) {
        const u16 word = axDspSlave[index];
        for (u8 byte : {static_cast<u8>(word >> 8), static_cast<u8>(word)}) {
            ector_hash ^= byte;
            ector_hash = (ector_hash << 3) | (ector_hash >> 29);
        }
    }
    require(ector_hash == 0x4E8A8B21u,
            "AX image does not match the pinned Dolphin HLE profile");

    alignas(32) static u16 dram[0x1000] = {};
    const uintptr_t dram_address = reinterpret_cast<uintptr_t>(dram);
    require(dram_address != 0 && dram_address <= UINT32_MAX &&
                dram_address <= UINT32_MAX - sizeof(dram),
            "owned DRAM span is outside checked Wasm32 memory");
    require((dram_address & 31u) == 0,
            "owned DRAM image is not 32-byte aligned");
    require(kDspDramLength == sizeof(dram), "owned DRAM span changed");

    OSContext caller = {};
    state.current_context = &caller;
    state.original_context = &caller;
    DSPTaskInfo task = {};
    task.iram_mmem_addr = state.bad_image_pointer ? axDspSlave + 1 : axDspSlave;
    task.iram_length = state.bad_span ? axDspSlaveLength + 2 : axDspSlaveLength;
    task.iram_addr = kDspIramAddress;
    task.dram_mmem_addr = dram;
    task.dram_length = kDspDramLength;
    task.dram_addr = kDspDramAddress;
    task.dsp_init_vector = kDspInitVector;
    task.dsp_resume_vector = 0x30;
    task.init_cb = melee_web_init_callback;
    task.priority = 0;

    DSPInit();
    require(DSPCheckInit() != FALSE, "original DSPInit did not publish initialized state");
    DSPAddTask(&task);
    require(!state.callback_seen && state.pending_interrupt,
            "DSP init callback ran inline before task submission returned");
    require(task.state == 0 && task.flags == 1,
            "original DSP task was not left published before interrupt delivery");
    if (state.masked_pump) {
        OSDisableInterrupts();
        pump_interrupt();
    }
    pump_interrupt();
    require(state.callback_seen && state.callback_count == 1,
            "original DSP init callback did not run exactly once");
    require(state.callback_saw_masked,
            "DSP init callback did not run inside masked interrupt dispatch");
    if (state.repeat_pump)
        pump_interrupt();
    require(task.state == 1 && task.flags == 1 && __DSP_curr_task == &task,
            "original DSP handler did not publish the initialized current task");
    require(state.expected_step == 10 && state.sent_count == 10,
            "original DSP boot mail sequence was incomplete");
    require(state.clear_count == 2 && state.set_count == 2,
            "original DSP handler did not save/restore context exactly once");
    std::fprintf(stderr, "DSP status trace: reads=%u writes=%u status=%04x w1c=%04x\n",
                 static_cast<unsigned>(state.dsp_status_reads),
                 static_cast<unsigned>(state.dsp_status_writes),
                 static_cast<unsigned>(state.dsp_status),
                 static_cast<unsigned>(state.dsp_status_w1c_cleared));
    require(state.dsp_status_reads == 4 && state.dsp_status_writes == 4,
            "DSP control/status sequence was incomplete");
    require(state.current_context == &caller,
            "original DSP handler did not restore caller context");
    require(state.global_mask == (kInitialDisabledMask & ~kDspInterruptMask) &&
                state.unmask_previous == kInitialDisabledMask,
            "DSP interrupt mask did not preserve the previous disabled mask");
    require(state.dsp_mask_hardware_enabled &&
                state.dsp_status == (kDspStatusDspMask | 0x0800u) &&
                state.dsp_status_w1c_cleared == kDspStatusInterrupt,
            "DSP interrupt status was not acknowledged with source W1C semantics");
    require(state.interrupts_enabled, "DSPInit left interrupts disabled");
    std::printf("source DSP init profile: passed image_bytes=%u ector_hash=%08x mails=%u callback=%u\n",
                static_cast<unsigned>(axDspSlaveLength),
                static_cast<unsigned>(ector_hash),
                static_cast<unsigned>(state.sent_count),
                static_cast<unsigned>(state.callback_count));
}
}  // namespace

extern "C" u16 melee_web_dsp_read(unsigned index) {
    return melee_web_dsp_read_impl(index);
}

extern "C" void melee_web_dsp_write(unsigned index, u16 value) {
    melee_web_dsp_write_impl(index, value);
}

MeleeWebDspRegister::operator u16() const {
    return melee_web_dsp_read(index_);
}

MeleeWebDspRegister& MeleeWebDspRegister::operator=(u16 value) {
    melee_web_dsp_write(index_, value);
    return *this;
}

int main(int argc, char** argv) {
    require(argc <= 2, "unexpected DSP fixture arguments");
    const char* mode = argc == 2 ? argv[1] : "";
    require(std::strcmp(mode, "") == 0 ||
                std::strcmp(mode, "bad-image-pointer") == 0 ||
                std::strcmp(mode, "bad-image-word") == 0 ||
                std::strcmp(mode, "bad-order") == 0 ||
                std::strcmp(mode, "bad-mail") == 0 ||
                std::strcmp(mode, "bad-register") == 0 ||
                std::strcmp(mode, "bad-status") == 0 ||
                std::strcmp(mode, "bad-unmask") == 0 ||
                std::strcmp(mode, "bad-span") == 0 ||
                std::strcmp(mode, "masked-pump") == 0 ||
                std::strcmp(mode, "repeat-pump") == 0 ||
                std::strcmp(mode, "stall-from") == 0 ||
                std::strcmp(mode, "stall-to") == 0,
            "unknown DSP fixture mode");
    if (std::strcmp(mode, "bad-image-pointer") == 0)
        state.bad_image_pointer = true;
    if (std::strcmp(mode, "bad-image-word") == 0)
        state.bad_image_word = true;
    if (std::strcmp(mode, "bad-order") == 0)
        melee_web_dsp_write(1, 0);
    if (std::strcmp(mode, "bad-span") == 0)
        state.bad_span = true;
    if (std::strcmp(mode, "bad-mail") == 0)
        state.from_mail = 0xDEADBEEFu;
    if (std::strcmp(mode, "bad-register") == 0)
        (void)melee_web_dsp_read(4);
    if (std::strcmp(mode, "bad-status") == 0)
        melee_web_dsp_write(5, 1);
    if (std::strcmp(mode, "bad-unmask") == 0)
        (void)__OSUnmaskInterrupts(OS_INTERRUPTMASK_AI_AI);
    if (std::strcmp(mode, "masked-pump") == 0)
        state.masked_pump = true;
    if (std::strcmp(mode, "repeat-pump") == 0)
        state.repeat_pump = true;
    if (std::strcmp(mode, "stall-from") == 0)
        state.from_pending = false;
    if (std::strcmp(mode, "stall-to") == 0)
        state.stall_to = true;
    run_fixture();
    return 0;
}
