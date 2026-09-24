/*
 * Executes the pinned SDK ar.c body against a declared 16 MiB ARAM hardware
 * profile. The test harness supplies a temporary C++ compatibility translation
 * that changes only the three legacy C void-pointer result casts; the source
 * operations and control flow remain from ar.c. The DSP register proxy is the
 * narrow boundary needed to observe __ARWriteDMA/__ARReadDMA without changing
 * the source MMIO expressions. This is a checked-Wasm32 oracle, not production
 * audio.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/stack.h>

static_assert(sizeof(void*) == 4 && sizeof(long) == 4, "AR oracle requires wasm32");

#include <dolphin.h>

MeleeWebSourceArInitDsp melee_web_source_ar_init_dsp;

extern "C" uint32_t melee_web_source_ar_init_source_stack_pointer(void);
extern "C" uint32_t melee_web_source_ar_init_source_free_blocks(void);
extern "C" uintptr_t melee_web_source_ar_init_source_block_length(void);
extern "C" int melee_web_source_ar_init_source_init_flag(void);

namespace {
constexpr uint32_t kAramSize = 0x01000000u;
constexpr uint32_t kInitialBase = 0x00004000u;
constexpr unsigned kStackEntries = 0x10;
constexpr unsigned kRegisterCount = 64;
constexpr uint32_t kBusClock = 162000000u;

struct FixtureState {
    uint16_t regs[kRegisterCount]{};
    bool interrupts_enabled = true;
    bool handler_installed = false;
    bool interrupt_unmasked = false;
    unsigned unmask_mask = 0;
    void (*handler)(short, ::OSContext*) = nullptr;
    uint64_t dma_count = 0;
    uint64_t dma_reads = 0;
    uint64_t dma_writes = 0;
    uint64_t dma_out_of_range_reads = 0;
    uint64_t dma_out_of_range_writes = 0;
    uint64_t alias_writes = 0;
    uint64_t flush_count = 0;
    uint64_t invalidate_count = 0;
};

FixtureState g_state;
bool g_probe_active = false;
uintptr_t g_probe_stack_top = 0;
unsigned g_register_operations = 0;
constexpr unsigned kRegisterOperationBudget = 512;
uint32_t g_declared_stack_source = 0;
uint32_t g_declared_bus_clock = kBusClock;
alignas(32) uint8_t g_aram[kAramSize];
alignas(32) uint32_t g_stack[kStackEntries];
alignas(32) uint32_t g_uncached_size;

struct MainSpan {
    uint32_t address = 0;
    uint32_t length = 0;
    bool valid = false;
    bool published = false;
    uint8_t visible[32]{};
};

MainSpan g_main_spans[3];

[[noreturn]] void fail(const char* message)
{
    fprintf(stderr, "source-ar-init-profile: %s\n", message);
    abort();
}

uint32_t decode_register_address(unsigned high, unsigned low)
{
    return (static_cast<uint32_t>(g_state.regs[high]) << 16) |
           static_cast<uint32_t>(g_state.regs[low]);
}

uint32_t decode_length()
{
    return ((static_cast<uint32_t>(g_state.regs[20]) & 0x03ffu) << 16) |
           static_cast<uint32_t>(g_state.regs[21]);
}

MainSpan& checked_main_span(uint32_t address, uint32_t length)
{
    if (address == 0 || static_cast<uint64_t>(address) + length > 0x100000000ull)
        fail("DMA main-memory span is not a checked Wasm32 span");
    if ((address & 31u) != 0 || (length & 31u) != 0)
        fail("DMA main-memory span is not 32-byte aligned");
    for (MainSpan& span : g_main_spans) {
        if (span.valid && span.address == address && length <= span.length)
            return span;
    }
    fail("DMA main-memory span is outside the owned cache spans");
}

void apply_dma()
{
    const uint32_t main_address = decode_register_address(16, 17);
    const uint32_t aram_address = decode_register_address(18, 19) & 0x03ffffffu;
    const uint32_t length = decode_length();
    const bool aram_to_main = (g_state.regs[20] & 0x8000u) != 0;
    if (length == 0 || (length & 31u) != 0)
        fail("DMA length is not a nonzero 32-byte span");
    if (aram_address < kAramSize && static_cast<uint64_t>(aram_address) + length > kAramSize)
        fail("DMA crossing the ARAM/expansion boundary is outside this profile");
    MainSpan& span = checked_main_span(main_address, length);
    ++g_state.dma_count;
    if (aram_to_main) {
        ++g_state.dma_reads;
        for (uint32_t i = 0; i < length; ++i) {
            const uint64_t address = static_cast<uint64_t>(aram_address) + i;
            if (address < kAramSize) span.visible[i] = g_aram[address];
            else ++g_state.dma_out_of_range_reads; // Declared null expansion leaves bytes unchanged.
        }
        span.published = false;
    } else {
        ++g_state.dma_writes;
        if (!span.published)
            fail("DMA consumed an unflushed main-memory span");
        const unsigned mode = g_state.regs[9] & 0x0fu;
        if (mode != 3 && mode != 4)
            fail("declared 16 MiB profile reached unsupported ARAM mode");
        for (uint32_t i = 0; i < length; ++i) {
            const uint64_t address = static_cast<uint64_t>(aram_address) + i;
            if (address >= kAramSize) {
                ++g_state.dma_out_of_range_writes;
                continue;
            }
            g_aram[address] = span.visible[i];
            if (mode == 4 && address < 0x00400000u) {
                g_aram[address + 0x00400000u] = span.visible[i];
                ++g_state.alias_writes;
            }
        }
    }
}

void reset()
{
    g_state = {};
    /* The declared retail DSP profile reports ARAM ready before __ARChecksize. */
    g_state.regs[11] = 1;
    memset(g_aram, 0, sizeof(g_aram));
    memset(g_stack, 0, sizeof(g_stack));
    g_uncached_size = 0;
    memset(g_main_spans, 0, sizeof(g_main_spans));
}

void emit_json(uint32_t first_return, uint32_t second_return,
               int init_check, uint32_t init_size, uint32_t init_cell,
               uint32_t init_source_stack_pointer,
               uint32_t init_source_free_blocks,
               uintptr_t init_source_block_length,
               int init_source_init_flag,
               const uint32_t* init_stack)
{
    const uint32_t first = ARAlloc(0x500);
    const uint32_t second = ARAlloc(0x2000);
    const uint32_t third = ARAlloc(0x30000);
    uint32_t released = 0;
    const uint32_t after_third = ARFree(&released);
    const uint32_t after_second = ARFree(&released);
    const uint32_t after_first = ARFree(&released);
    printf("{\"schema\":\"melee-web-source-ar-init-profile\",\"version\":1,"
           "\"profile\":{\"name\":\"declared-retail-aram-16m\",\"initial_base\":%u,"
           "\"hardware_size\":%u,\"stack_entries\":%u,\"stack_source\":%u,"
           "\"bus_clock\":%u},"
           "\"init\":{\"return\":%u,\"second_return\":%u,\"check_init\":%d,"
           "\"size\":%u,\"size_cell\":%u,\"source_stack_pointer\":%u,"
           "\"source_free_blocks\":%u,\"source_block_length_offset\":%u,"
           "\"source_init_flag\":%d},"
           "\"stack\":{\"owner\":\"fixture_static_ar_stack\","
           "\"source_symbol_validated\":false,\"words_after_init\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]},"
           "\"dsp\":{\"handler_installed\":%s,\"unmask_mask\":%u,"
           "\"mode_after_probe\":%u,\"refresh\":%u,\"busy\":%u},"
           "\"dma\":{\"count\":%llu,\"reads\":%llu,\"writes\":%llu,"
           "\"out_of_range_reads\":%llu,\"out_of_range_writes\":%llu,"
           "\"alias_writes\":%llu,\"cache_flush\":%llu,\"cache_invalidate\":%llu},"
           "\"allocations\":[%u,%u,%u],\"frees\":[%u,%u,%u],"
           "\"limitations\":[\"checked-Wasm32-oracle\",\"no-AI-or-AX\","
           "\"source-stack-symbol-not-yet-bound\"]}\n",
           kInitialBase, kAramSize, kStackEntries, g_declared_stack_source,
           g_declared_bus_clock,
           first_return, second_return,
           init_check, init_size, init_cell, init_source_stack_pointer,
           init_source_free_blocks,
           static_cast<unsigned>(init_source_block_length - reinterpret_cast<uintptr_t>(&g_stack[0])),
           init_source_init_flag,
           init_stack[0], init_stack[1], init_stack[2], init_stack[3],
           init_stack[4], init_stack[5], init_stack[6], init_stack[7],
           init_stack[8], init_stack[9], init_stack[10], init_stack[11],
           init_stack[12], init_stack[13], init_stack[14], init_stack[15],
           g_state.handler_installed ? "true" : "false", g_state.unmask_mask,
           static_cast<unsigned>(g_state.regs[9] & 0x0fu), g_state.regs[13],
           static_cast<unsigned>(g_state.regs[5] & 0x200u),
           static_cast<unsigned long long>(g_state.dma_count),
           static_cast<unsigned long long>(g_state.dma_reads),
           static_cast<unsigned long long>(g_state.dma_writes),
           static_cast<unsigned long long>(g_state.dma_out_of_range_reads),
           static_cast<unsigned long long>(g_state.dma_out_of_range_writes),
           static_cast<unsigned long long>(g_state.alias_writes),
           static_cast<unsigned long long>(g_state.flush_count),
           static_cast<unsigned long long>(g_state.invalidate_count),
           first, second, third, after_third, after_second, after_first);
}
} // namespace

extern "C" {
uint16_t melee_web_source_ar_init_dsp_read(unsigned index)
{
    if (++g_register_operations > kRegisterOperationBudget) fail("DSP register operation budget exhausted");
    if (index != 5 && index != 9 && index != 11 && index != 13 &&
        (index < 16 || index > 21)) fail("unsupported DSP register read");
    return g_state.regs[index];
}

void melee_web_source_ar_init_dsp_write(unsigned index, uint16_t value)
{
    if (++g_register_operations > kRegisterOperationBudget) fail("DSP register operation budget exhausted");
    if (index != 9 && index != 13 && (index < 16 || index > 21))
        fail("unsupported DSP register write");
    g_state.regs[index] = value;
    if (index == 21) apply_dma();
}

int melee_web_source_ar_init_disable_interrupts(void)
{
    const int previous = g_state.interrupts_enabled ? 1 : 0;
    g_state.interrupts_enabled = false;
    return previous;
}

void melee_web_source_ar_init_restore_interrupts(int enabled)
{
    if (enabled != 0 && enabled != 1) fail("invalid interrupt state");
    g_state.interrupts_enabled = enabled != 0;
}

void melee_web_source_ar_init_set_handler(int exception, void (*handler)(short, struct OSContext*))
{
    if (exception != 6 || handler == nullptr) fail("unexpected AR interrupt handler");
    g_state.handler_installed = true;
    g_state.handler = handler;
}

void melee_web_source_ar_init_unmask(uint32_t mask)
{
    if (mask != 0x02000000u) fail("unexpected AR interrupt mask");
    g_state.interrupt_unmasked = true;
    g_state.unmask_mask = mask;
}

void melee_web_source_ar_init_clear_context(void* context)
{
    (void)context;
    fail("unexpected AR interrupt context clear during synchronous ARInit");
}

void melee_web_source_ar_init_set_context(void* context)
{
    (void)context;
    fail("unexpected AR interrupt context switch during synchronous ARInit");
}

void melee_web_source_ar_init_flush(void* address, uint32_t length)
{
    if (address == nullptr || length != 0x20 ||
        (reinterpret_cast<uintptr_t>(address) & 31u) != 0)
        fail("invalid checked cache flush span");
    const uint32_t numeric = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
    if (!g_probe_active || numeric < emscripten_stack_get_current() ||
        static_cast<uint64_t>(numeric) + length > g_probe_stack_top)
        fail("cache publication is outside the live source probe stack");
    MainSpan* free_span = nullptr;
    for (MainSpan& span : g_main_spans) {
        if (span.valid && span.address == numeric && span.length == length) {
            free_span = &span;
            break;
        }
        if (span.valid && numeric < span.address + span.length &&
            span.address < numeric + length) fail("overlapping source cache spans");
        if (!span.valid && free_span == nullptr) free_span = &span;
    }
    if (free_span == nullptr) fail("checked cache span table exhausted");
    free_span->address = numeric;
    free_span->length = length;
    free_span->valid = true;
    memcpy(free_span->visible, address, length);
    free_span->published = true;
    ++g_state.flush_count;
}

void melee_web_source_ar_init_invalidate(void* address, uint32_t length)
{
    if (!g_probe_active || address == nullptr || length != 0x20 ||
        (reinterpret_cast<uintptr_t>(address) & 31u) != 0)
        fail("invalid checked cache invalidate span");
    const uint32_t numeric = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
    for (MainSpan& span : g_main_spans) {
        if (span.valid && span.address == numeric && span.length == length) {
            memcpy(address, span.visible, length);
            ++g_state.invalidate_count;
            return;
        }
    }
    fail("cache invalidate span is outside the owned cache spans");
}

void* melee_web_source_ar_init_physical(uint32_t address)
{
    if (address != 0xd0u) fail("unexpected physical uncached address");
    return &g_uncached_size;
}

void melee_web_source_ar_init_abort(const char* message)
{
    fail(message);
}
}

uint32_t parse_u32(const char* value)
{
    char* end = nullptr;
    const unsigned long long parsed = strtoull(value, &end, 0);
    if (end == value || *end != '\0' || parsed > 0xffffffffull)
        fail("invalid profile integer");
    return static_cast<uint32_t>(parsed);
}

void parse_profile(int argc, char** argv)
{
    if (argc != 9) fail("profile requires --aram-size, --stack-source, --stack-entries, --bus-clock");
    unsigned seen = 0;
    for (int i = 1; i < argc; i += 2) {
        unsigned bit = 0;
        if (strcmp(argv[i], "--aram-size") == 0) {
            bit = 1;
            if (parse_u32(argv[i + 1]) != kAramSize)
                fail("only the declared 16 MiB ARAM profile is supported");
        } else if (strcmp(argv[i], "--stack-source") == 0) {
            bit = 2;
            g_declared_stack_source = parse_u32(argv[i + 1]);
            if (g_declared_stack_source < 0x80000000u ||
                static_cast<uint64_t>(g_declared_stack_source) + sizeof(g_stack) > 0x81800000ull ||
                (g_declared_stack_source & 3u) != 0)
                fail("stack source identity is outside declared MEM1");
        } else if (strcmp(argv[i], "--stack-entries") == 0) {
            bit = 4;
            if (parse_u32(argv[i + 1]) != kStackEntries)
                fail("only the authored 16-entry stack is supported");
        } else if (strcmp(argv[i], "--bus-clock") == 0) {
            bit = 8;
            g_declared_bus_clock = parse_u32(argv[i + 1]);
            if (g_declared_bus_clock != kBusClock)
                fail("only the declared GameCube bus clock is supported");
        } else {
            fail("unknown ARInit profile option");
        }
        if (seen & bit) fail("duplicate ARInit profile option");
        seen |= bit;
    }
    if (seen != 15) fail("incomplete ARInit profile");
}

// Independent synthetic service check; its local span has its own bounded
// lifetime. It cannot register memory for the subsequent source startup run.
__attribute__((noinline)) void check_absent_expansion()
{
    alignas(32) unsigned char buffer[32];
    memset(buffer, 0xA5, sizeof(buffer));
    melee_web_source_ar_init_flush(buffer, sizeof(buffer));
    ARStartDMA(1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer)),
               kAramSize, sizeof(buffer));
    melee_web_source_ar_init_invalidate(buffer, sizeof(buffer));
    for (unsigned char value : buffer)
        if (value != 0xA5) fail("absent expansion changed destination bytes");
}

int main(int argc, char** argv)
{
    const char* fault = nullptr;
    if (argc == 11 && strcmp(argv[9], "--control") == 0) {
        fault = argv[10]; argc = 9;
    }
    parse_profile(argc, argv);
    reset();
    if (fault != nullptr) {
        if (strcmp(fault, "absent-expansion") == 0) {
            g_probe_stack_top = emscripten_stack_get_current();
            g_probe_active = true;
            check_absent_expansion();
            g_probe_active = false;
            puts("{\"synthetic_absent_expansion_preserves_bytes\":true}");
            return 0;
        }
        if (strcmp(fault, "unowned-dma") == 0)
            ARStartDMA(0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_stack)), 0x4000, 32);
        else if (strcmp(fault, "unowned-cache") == 0)
            melee_web_source_ar_init_flush(g_stack, 32);
        else if (strcmp(fault, "stalled-ready") == 0) g_state.regs[11] = 0;
        else fail("unknown fault control");
    }
    g_probe_stack_top = emscripten_stack_get_current();
    g_probe_active = true;
    const uint32_t first_return = ARInit(g_stack, kStackEntries);
    g_probe_active = false;
    for (MainSpan& span : g_main_spans) span.valid = false;
    const uint32_t init_size = ARGetSize();
    const uint32_t init_cell = g_uncached_size;
    const int init_check = ARCheckInit();
    const uint32_t init_source_stack_pointer =
        melee_web_source_ar_init_source_stack_pointer();
    const uint32_t init_source_free_blocks =
        melee_web_source_ar_init_source_free_blocks();
    const uintptr_t init_source_block_length =
        melee_web_source_ar_init_source_block_length();
    const int init_source_init_flag = melee_web_source_ar_init_source_init_flag();
    uint32_t init_stack[kStackEntries];
    memcpy(init_stack, g_stack, sizeof(init_stack));
    if (first_return != kInitialBase ||
        ARGetBaseAddress() != kInitialBase || init_size != kAramSize ||
        init_cell != kAramSize || init_source_stack_pointer != kInitialBase ||
        init_source_free_blocks != kStackEntries ||
        init_source_block_length != reinterpret_cast<uintptr_t>(&g_stack[0]) ||
        init_source_init_flag != 1 || !init_check ||
        !g_state.handler_installed || !g_state.interrupt_unmasked)
        fail("declared ARInit profile did not reach the source state");
    const uint64_t dma_before_second = g_state.dma_count;
    const uint32_t second_return = ARInit(nullptr, 0);
    if (g_state.dma_count != dma_before_second ||
        melee_web_source_ar_init_source_block_length() != init_source_block_length ||
        melee_web_source_ar_init_source_free_blocks() != init_source_free_blocks ||
        !g_state.interrupts_enabled || second_return != kInitialBase)
        fail("source repeated ARInit return changed");
    emit_json(first_return, second_return, init_check, init_size, init_cell,
              init_source_stack_pointer, init_source_free_blocks,
              init_source_block_length, init_source_init_flag,
              init_stack);
    return 0;
}
