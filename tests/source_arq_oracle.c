/*
 * Deferred ARQ backend for the untouched original arq.c.
 *
 * The source queue, priority handling, chunking, cancellation and interrupt
 * re-entry all come from the pinned SDK. This fixture supplies only the host
 * ARAM/main-memory backend: ARStartDMA validates and queues an owned
 * transfer; pump_once copies bytes and invokes the source-registered DMA
 * interrupt callback after the source call has unwound.
 */
#include <dolphin.h>
#include "source_arq_context.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char oracle_aram[0x1000] __attribute__((aligned(32)));
static unsigned char oracle_main_a[0x100] __attribute__((aligned(32)));
static unsigned char oracle_main_b[0x100] __attribute__((aligned(32)));
static unsigned char oracle_main_c[0x100] __attribute__((aligned(32)));
static MeleeWebSourceArqContext oracle_backend;
static ARQDMACallback oracle_dma_callback;

static char oracle_events[32];
static unsigned oracle_event_count;
static ARQRequest oracle_reentry_request;
static int oracle_reentry_enabled;
static int oracle_nested_pump_status;
static int oracle_callback_saw_masked;

#include "../.deps/melee/extern/dolphin/src/dolphin/ar/arq.c"

static _Noreturn void fail(const char* message)
{
    fprintf(stderr, "source ARQ fixture: %s\n", message);
    abort();
}

static void require_condition(int condition, const char* message)
{
    if (!condition) fail(message);
}

ARQDMACallback ARRegisterDMACallback(ARQDMACallback callback)
{
    ARQDMACallback previous = oracle_dma_callback;
    const int previous_enabled = OSDisableInterrupts();
    require_condition(melee_web_source_arq_register_callback(&oracle_backend, callback) ==
                          MELEE_WEB_SOURCE_ARQ_OK,
                      "DMA callback registration was rejected");
    oracle_dma_callback = callback;
    OSRestoreInterrupts(previous_enabled);
    return previous;
}

u32 ARGetDMAStatus(void)
{
    const int previous_enabled = OSDisableInterrupts();
    const u32 pending = (u32)melee_web_source_arq_pending(&oracle_backend);
    OSRestoreInterrupts(previous_enabled);
    return pending;
}

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length)
{
    const int previous_enabled = OSDisableInterrupts();
    const int result = melee_web_source_arq_start(
        &oracle_backend, type, mainmem_addr, aram_addr, length);
    require_condition(result == MELEE_WEB_SOURCE_ARQ_OK,
                      "DMA submission was rejected");
    OSRestoreInterrupts(previous_enabled);
}

static int pump_once(void)
{
    int previous_enabled;
    int result;
    if (melee_web_source_arq_interrupts_masked()) return 0;
    previous_enabled = OSDisableInterrupts();
    result = melee_web_source_arq_pump(&oracle_backend);
    OSRestoreInterrupts(previous_enabled);
    require_condition(result == MELEE_WEB_SOURCE_ARQ_OK,
                      "DMA completion pump was rejected");
    return 1;
}

static void pump_until_idle(void)
{
    unsigned count = 0;
    while (melee_web_source_arq_pending(&oracle_backend)) {
        if (++count > 64) fail("ARQ completion pump exceeded its bound");
        require_condition(pump_once(),
                          "DMA completion remained masked at its pump boundary");
    }
}

static void reset_fixture(void)
{
    require_condition(!melee_web_source_arq_interrupts_masked(),
                      "fixture reset began with interrupts masked");
    memset(oracle_aram, 0, sizeof(oracle_aram));
    memset(oracle_main_a, 0, sizeof(oracle_main_a));
    memset(oracle_main_b, 0, sizeof(oracle_main_b));
    memset(oracle_main_c, 0, sizeof(oracle_main_c));
    if (oracle_backend.initialized)
        require_condition(melee_web_source_arq_shutdown(&oracle_backend) ==
                              MELEE_WEB_SOURCE_ARQ_OK,
                          "cannot reset a busy ARQ backend");
    {
        const MeleeWebSourceArqSpan spans[] = {
            {(u32)(uintptr_t)oracle_main_a, oracle_main_a, sizeof(oracle_main_a)},
            {(u32)(uintptr_t)oracle_main_b, oracle_main_b, sizeof(oracle_main_b)},
            {(u32)(uintptr_t)oracle_main_c, oracle_main_c, sizeof(oracle_main_c)},
        };
        require_condition(melee_web_source_arq_bind(
                              &oracle_backend, spans,
                              sizeof(spans) / sizeof(spans[0]),
                              oracle_aram, sizeof(oracle_aram)) ==
                              MELEE_WEB_SOURCE_ARQ_OK,
                          "ARQ backend binding failed");
    }
    oracle_dma_callback = NULL;
    memset(oracle_events, 0, sizeof(oracle_events));
    oracle_event_count = 0;
    oracle_reentry_enabled = 0;
    oracle_nested_pump_status = MELEE_WEB_SOURCE_ARQ_NO_PENDING;
    oracle_callback_saw_masked = 0;
    memset(&oracle_reentry_request, 0, sizeof(oracle_reentry_request));
    ARQReset();
    ARQInit();
    ARQSetChunkSize(0x1000);
}

static void event(char value)
{
    require_condition(oracle_event_count < sizeof(oracle_events) - 1,
                      "callback event log overflow");
    oracle_events[oracle_event_count++] = value;
    oracle_events[oracle_event_count] = '\0';
}

static void callback_low(struct ARQRequest* request)
{
    oracle_callback_saw_masked = melee_web_source_arq_interrupts_masked();
    event(request == &oracle_reentry_request ? 'c' : 'l');
}

static void callback_high(struct ARQRequest* request)
{
    oracle_callback_saw_masked = melee_web_source_arq_interrupts_masked();
    (void)request;
    event('h');
}

static void callback_reentry(struct ARQRequest* request)
{
    oracle_callback_saw_masked = melee_web_source_arq_interrupts_masked();
    event('a');
    if (oracle_reentry_enabled) {
        oracle_reentry_enabled = 0;
        ARQPostRequest(&oracle_reentry_request, 0, ARAM_DIR_MRAM_TO_ARAM,
                       ARQ_PRIORITY_LOW, (u32)(uintptr_t)oracle_main_c,
                       0x200, 32, callback_low);
    }
    (void)request;
}

static void callback_nested_pump(struct ARQRequest* request)
{
    (void)request;
    oracle_main_b[0] = 0x44;
    ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (u32)(uintptr_t)oracle_main_b,
               0x300, 32);
    oracle_nested_pump_status = melee_web_source_arq_pump(&oracle_backend);
}

static void test_deferred_priority_and_chunking(void)
{
    ARQRequest low = {0};
    ARQRequest high = {0};
    unsigned i;

    reset_fixture();
    for (i = 0; i < 96; ++i) oracle_main_a[i] = (unsigned char)(i + 1);
    for (i = 0; i < 32; ++i) oracle_main_b[i] = (unsigned char)(0xA0 + i);
    ARQSetChunkSize(32);

    ARQPostRequest(&low, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0, 96, callback_low);
    require_condition(oracle_event_count == 0,
                      "ARQ callback ran inline during low request submission");
    require_condition(ARGetDMAStatus() != 0,
                      "ARStartDMA did not expose a pending transfer");
    ARQPostRequest(&high, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_HIGH,
                   (u32)(uintptr_t)oracle_main_b, 0x100, 32, callback_high);
    pump_until_idle();
    require_condition(strcmp(oracle_events, "hl") == 0,
                      "high-priority completion did not precede low completion");
    require_condition(memcmp(oracle_aram, oracle_main_a, 96) == 0,
                      "chunked low-priority copy is incomplete");
    require_condition(memcmp(oracle_aram + 0x100, oracle_main_b, 32) == 0,
                      "high-priority copy is incorrect");
    require_condition(melee_web_source_arq_submitted_count(&oracle_backend) == 4,
                      "priority/chunking did not issue the expected four DMA chunks");
    require_condition(ARGetDMAStatus() == 0,
                      "DMA remained pending after the completion pump");
}

static void test_cancellation(void)
{
    ARQRequest first = {0};
    ARQRequest removed = {0};

    reset_fixture();
    oracle_main_a[0] = 0x11;
    oracle_main_b[0] = 0x22;
    ARQPostRequest(&first, 7, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0, 32, callback_low);
    /* A completed-size active request already owns its callback; source
     * ARQRemoveRequest leaves it in flight while removing only queued work. */
    ARQRemoveRequest(&first);
    ARQPostRequest(&removed, 7, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_b, 0x20, 32, callback_low);
    ARQRemoveRequest(&removed);
    pump_until_idle();
    require_condition(strcmp(oracle_events, "l") == 0,
                      "cancelled queued request still invoked its callback");
    require_condition(oracle_aram[0] == 0x11 && oracle_aram[0x20] == 0,
                      "cancelled request changed owned ARAM bytes");
}

static void test_deferred_roundtrip(void)
{
    ARQRequest write = {0};
    ARQRequest read = {0};
    unsigned i;

    reset_fixture();
    for (i = 0; i < 32; ++i) oracle_main_a[i] = (unsigned char)(0x50 + i);
    ARQPostRequest(&write, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0x280, 32, callback_low);
    require_condition(oracle_aram[0x280] == 0,
                      "MRAM-to-ARAM bytes became visible before completion");
    pump_until_idle();
    require_condition(memcmp(oracle_aram + 0x280, oracle_main_a, 32) == 0,
                      "MRAM-to-ARAM completion did not copy the full extent");

    memset(oracle_main_a, 0, 32);
    ARQPostRequest(&read, 0, ARAM_DIR_ARAM_TO_MRAM, ARQ_PRIORITY_LOW,
                   0x280, (u32)(uintptr_t)oracle_main_a, 32, callback_low);
    require_condition(oracle_main_a[0] == 0,
                      "ARAM-to-MRAM bytes became visible before completion");
    pump_until_idle();
    require_condition(memcmp(oracle_main_a, oracle_aram + 0x280, 32) == 0,
                      "ARAM-to-MRAM completion did not copy the full extent");
}

static void test_callback_reentry(void)
{
    ARQRequest first = {0};

    reset_fixture();
    oracle_reentry_enabled = 1;
    oracle_main_a[0] = 0x31;
    oracle_main_c[0] = 0x33;
    ARQPostRequest(&first, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0x180, 32,
                   callback_reentry);
    require_condition(oracle_event_count == 0,
                      "reentry callback ran inline during request submission");
    pump_until_idle();
    require_condition(strcmp(oracle_events, "ac") == 0,
                      "callback reentry did not preserve source queue order");
    require_condition(oracle_aram[0x180] == 0x31 && oracle_aram[0x200] == 0x33,
                      "callback reentry did not complete both copies");
}

static void test_backend_lifetime_and_nested_pump_guards(void)
{
    ARQRequest first = {0};
    MeleeWebSourceArqSpan spans[3];

    reset_fixture();
    spans[0] = (MeleeWebSourceArqSpan){
        (u32)(uintptr_t)oracle_main_a, oracle_main_a, sizeof(oracle_main_a)};
    spans[1] = (MeleeWebSourceArqSpan){
        (u32)(uintptr_t)oracle_main_b, oracle_main_b, sizeof(oracle_main_b)};
    spans[2] = (MeleeWebSourceArqSpan){
        (u32)(uintptr_t)oracle_main_c, oracle_main_c, sizeof(oracle_main_c)};
    ARQPostRequest(&first, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0x280, 32,
                   callback_nested_pump);
    require_condition(melee_web_source_arq_bind(
                          &oracle_backend, spans, 3, oracle_aram,
                          sizeof(oracle_aram)) == MELEE_WEB_SOURCE_ARQ_BUSY,
                      "backend rebind was accepted while DMA was pending");
    require_condition(melee_web_source_arq_register_callback(
                          &oracle_backend, NULL) == MELEE_WEB_SOURCE_ARQ_BUSY,
                      "callback unregister was accepted while DMA was pending");
    require_condition(melee_web_source_arq_shutdown(&oracle_backend) ==
                          MELEE_WEB_SOURCE_ARQ_BUSY,
                      "backend shutdown was accepted while DMA was pending");
    pump_until_idle();
    require_condition(oracle_nested_pump_status ==
                          MELEE_WEB_SOURCE_ARQ_REENTRANT_PUMP,
                      "nested completion pump was not rejected");
    require_condition(oracle_aram[0x300] == 0x44,
                      "post-callback transfer did not complete after unwind");
}

static void test_interrupt_mask_boundary(void)
{
    ARQRequest request = {0};
    int outer;
    int inner;

    reset_fixture();
    oracle_main_a[0] = 0x55;
    outer = OSDisableInterrupts();
    inner = OSDisableInterrupts();
    require_condition(outer == 1 && inner == 0 &&
                          melee_web_source_arq_interrupts_masked(),
                      "nested interrupt masking did not preserve prior state");
    OSRestoreInterrupts(inner);
    require_condition(melee_web_source_arq_interrupts_masked(),
                      "inner interrupt restore unmasked the outer critical section");
    ARQPostRequest(&request, 0, ARAM_DIR_MRAM_TO_ARAM, ARQ_PRIORITY_LOW,
                   (u32)(uintptr_t)oracle_main_a, 0, 32, callback_low);
    require_condition(!pump_once() && oracle_event_count == 0 && oracle_aram[0] == 0,
                      "masked completion delivered data or callback");
    OSRestoreInterrupts(outer);
    require_condition(!melee_web_source_arq_interrupts_masked(),
                      "outer interrupt restore left the fixture masked");
    require_condition(pump_once() && oracle_event_count == 1 && oracle_aram[0] == 0x55 &&
                          oracle_callback_saw_masked,
                      "unmasked completion did not deliver data and callback");
}

static int invalid_case(const char* name)
{
    ARQRequest request = {0};
    reset_fixture();
    if (strcmp(name, "missing-span") == 0) {
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, 0x1200, 0, 32);
    } else if (strcmp(name, "bad-alignment") == 0) {
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM,
                   (u32)(uintptr_t)oracle_main_a + 1, 0, 32);
    } else if (strcmp(name, "busy") == 0) {
        oracle_main_a[0] = 0x41;
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (u32)(uintptr_t)oracle_main_a, 0, 32);
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (u32)(uintptr_t)oracle_main_a, 0x20, 32);
    } else if (strcmp(name, "unknown-direction") == 0) {
        ARStartDMA(2, (u32)(uintptr_t)oracle_main_a, 0, 32);
    } else if (strcmp(name, "bad-length") == 0) {
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (u32)(uintptr_t)oracle_main_a, 0, 31);
    } else if (strcmp(name, "missing-callback") == 0) {
        require_condition(melee_web_source_arq_register_callback(
                              &oracle_backend, NULL) == MELEE_WEB_SOURCE_ARQ_OK,
                          "idle callback unregister was rejected");
        ARStartDMA(ARAM_DIR_MRAM_TO_ARAM, (u32)(uintptr_t)oracle_main_a, 0, 32);
    } else if (strcmp(name, "host-alias-aram") == 0) {
        const MeleeWebSourceArqSpan span = {
            (u32)(uintptr_t)oracle_main_a, oracle_main_a, sizeof(oracle_main_a)};
        return melee_web_source_arq_bind(
                   &oracle_backend, &span, 1, oracle_main_a,
                   sizeof(oracle_aram)) == MELEE_WEB_SOURCE_ARQ_OK ? 0 : 3;
    } else if (strcmp(name, "host-overlap-spans") == 0) {
        const MeleeWebSourceArqSpan spans[] = {
            {(u32)(uintptr_t)oracle_main_a, oracle_main_a, 64},
            {(u32)(uintptr_t)oracle_main_b, oracle_main_a + 32, 64},
        };
        return melee_web_source_arq_bind(
                   &oracle_backend, spans, 2, oracle_aram,
                   sizeof(oracle_aram)) == MELEE_WEB_SOURCE_ARQ_OK ? 0 : 3;
    } else if (strcmp(name, "source-queue-unknown") == 0) {
        ARQPostRequest(&request, 0, 2, ARQ_PRIORITY_LOW,
                       (u32)(uintptr_t)oracle_main_a, 0, 32, callback_low);
    } else {
        return 2;
    }
    return 3;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "run") == 0) {
        test_deferred_priority_and_chunking();
        test_cancellation();
        test_deferred_roundtrip();
        test_callback_reentry();
        test_backend_lifetime_and_nested_pump_guards();
        test_interrupt_mask_boundary();
        printf("{\"schema\":\"melee-source-arq-completion\",\"version\":1,\"scenarios\":[\"priority_chunking\",\"cancellation\",\"roundtrip\",\"reentry\",\"lifetime_guards\",\"nested_pump\",\"interrupt_mask\"],\"deferred\":true}\n");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "invalid") == 0)
        return invalid_case(argv[2]);
    return 2;
}
