/*
 * Narrow source DevCom startup boundary.  The pinned arq.c and devcom.c are
 * included unchanged; this TU supplies only a checked host memory/ARAM
 * completion provider and the fixture's explicit HSD OS setup.
 */
#include <aurora/aurora.h>
#include <dolphin/ar.h>
#include <dolphin/dvd.h>
#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/os.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/synth.h>
#include <source_arq_context.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void* MEM1Start;
extern void* MEM1End;
extern uintptr_t OSBaseAddress;

_Static_assert(sizeof(void*) == 4, "DevCom fixture requires Wasm32");
static MeleeWebSourceArqContext g_arq;
static ARQDMACallback g_dma_callback;
static unsigned published_mask;

static _Noreturn void fixture_fail(const char* message)
{
    fprintf(stderr, "source-devcom-fixture: %s\n", message);
    abort();
}

u32 ARAlloc(u32 length)
{
    (void)length;
    fixture_fail("source ARAlloc is outside this direct DevCom boundary");
}

/* Host completion invokes this void(void) DMA interrupt callback after the
 * transfer has completed.  The original ISR, not this provider, selects the
 * pending ARQRequest and invokes its request callback. */
static void fixture_arq_completion(void)
{
    if (g_dma_callback == NULL)
        fixture_fail("ARQ completion arrived without source DMA callback");
    g_dma_callback();
}

// A controlled single-thread completion boundary, independent of Aurora's
// production interrupt provider. No hardware interrupt timing is claimed.
static int fixture_interrupts_enabled = 1;
static BOOL fixture_disable_interrupts(void)
{
    const int previous = fixture_interrupts_enabled;
    fixture_interrupts_enabled = 0;
    return previous;
}
static BOOL fixture_restore_interrupts(BOOL enabled)
{
    const int previous = fixture_interrupts_enabled;
    if (enabled != 0 && enabled != 1) fixture_fail("invalid interrupt state");
    fixture_interrupts_enabled = enabled;
    return previous;
}
#define OSDisableInterrupts fixture_disable_interrupts
#define OSRestoreInterrupts fixture_restore_interrupts

/* The original ISR retains ownership of queue selection and callbacks. */
#include "../.deps/melee/extern/dolphin/src/dolphin/ar/arq.c"

ARQDMACallback ARRegisterDMACallback(ARQDMACallback callback)
{
    ARQDMACallback previous = g_dma_callback;
    if (melee_web_source_arq_register_callback(&g_arq, fixture_arq_completion) !=
        MELEE_WEB_SOURCE_ARQ_OK)
        fixture_fail("source ARQ callback registration failed");
    g_dma_callback = callback;
    return previous;
}

u32 ARGetDMAStatus(void)
{
    return (u32) melee_web_source_arq_pending(&g_arq);
}

void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length)
{
    unsigned published = 0;
    for (size_t i = 0; i < g_arq.span_count; ++i) {
        const MeleeWebSourceArqSpan* span = &g_arq.spans[i];
        if (mainmem_addr >= span->address &&
            (uint64_t)mainmem_addr + length <= (uint64_t)span->address + span->length)
            published = (published_mask & (1U << i)) != 0;
    }
    if (!published) fixture_fail("DMA requested an unpublished relay span");
    if (melee_web_source_arq_start(&g_arq, type, mainmem_addr, aram_addr,
                                   length) != MELEE_WEB_SOURCE_ARQ_OK)
        fixture_fail("source ARQ rejected the source DMA span");
}

/* DVD is unreachable for the type-3 lane.  Rename those ordinary source
 * calls in this fixture so an accidental DVD request fails explicitly rather
 * than becoming an Aurora success path. */
BOOL melee_web_source_devcom_unreachable_dvd_open(s32, DVDFileInfo*);
BOOL melee_web_source_devcom_unreachable_dvd_read(
    DVDFileInfo*, void*, s32, s32, DVDCallback, s32);
#define DVDFastOpen melee_web_source_devcom_unreachable_dvd_open
#define DVDReadAsyncPrio melee_web_source_devcom_unreachable_dvd_read
#include "../.deps/melee/src/sysdolphin/baselib/devcom.static.h"
// Enforce the SDK DMA contract on the same source object; preserve its
// authored type and bounds rather than relying on linker placement.
_Alignas(ARQ_DMA_ALIGNMENT) static __typeof__(HSD_DevCom_804C6330_bufs)
    HSD_DevCom_804C6330_bufs;
#include "../.deps/melee/src/sysdolphin/baselib/devcom.c"
#undef DVDReadAsyncPrio
#undef DVDFastOpen
_Static_assert(sizeof(HSD_DevCom) == 0x24, "original DevCom layout changed");
// DMA sees only explicitly published relay bytes. This models the source
// cache-store ownership boundary without pretending to execute a PPC cache.
static unsigned char published_relay[2][DEVCOM_BUF_SIZE] __attribute__((aligned(32)));
static unsigned cache_publish_count;

void DCStoreRange(void* address, u32 length)
{
    if (!g_arq.initialized) fixture_fail("cache store has no bound DMA owner");
    for (unsigned i = 0; i < 2; ++i) {
        if (address == HSD_DevCom_804C6330_bufs[i] && length == DEVCOM_BUF_SIZE) {
            if (g_arq.pending && (uintptr_t)g_arq.pending_mainmem >= (uintptr_t)published_relay[i] &&
                (uintptr_t)g_arq.pending_mainmem < (uintptr_t)published_relay[i] + DEVCOM_BUF_SIZE)
                fixture_fail("relay published while its DMA is pending");
            memcpy(published_relay[i], address, length);
            published_mask |= 1U << i;
            ++cache_publish_count;
            return;
        }
    }
    fixture_fail("cache store is outside the owned source relay span");
}

void DCInvalidateRange(void* address, u32 length)
{
    (void)address; (void)length;
    fixture_fail("cache invalidation is outside the type-3 boundary");
}


BOOL melee_web_source_devcom_unreachable_dvd_open(s32 entrynum,
                                                  DVDFileInfo* fileinfo)
{
    (void) entrynum;
    (void) fileinfo;
    fixture_fail("source DevCom reached an unsupported DVD request");
}

BOOL melee_web_source_devcom_unreachable_dvd_read(DVDFileInfo* fileinfo,
                                                  void* address, s32 length,
                                                  s32 offset, DVDCallback callback,
                                                  s32 priority)
{
    (void) fileinfo;
    (void) address;
    (void) length;
    (void) offset;
    (void) callback;
    (void) priority;
    fixture_fail("source DevCom reached an unsupported DVD read");
}

#include "source_devcom_context.h"

static uint32_t source_address(const void* pointer)
{
    const uintptr_t host = (uintptr_t) pointer;
    const uintptr_t lo = (uintptr_t) MEM1Start;
    const uintptr_t hi = (uintptr_t) MEM1End;
    if (pointer == NULL || MEM1Start == NULL || MEM1End == NULL ||
        host < lo || host >= hi)
        fixture_fail("source pointer is outside provider-owned MEM1");
    return (uint32_t) (UINT32_C(0x80000000) +
                       OSCachedToPhysical((void*) pointer));
}

static void require_source_span(const void* pointer, const char* label)
{
    (void) label;
    (void) source_address(pointer);
}

static void completion_callback(int request, int args, void* buffer,
                                melee_source_bool cancelled)
{
    int* count = (int*) (uintptr_t) args;
    if (count == NULL || buffer != NULL || cancelled || request <= 0 ||
        fixture_interrupts_enabled)
        fixture_fail("type-3 callback metadata was not source-valid");
    *count += 1;
}

static int fixture_pump(void)
{
    if (!fixture_interrupts_enabled) return MELEE_WEB_SOURCE_ARQ_BUSY;
    const int previous = OSDisableInterrupts();
    const int result = melee_web_source_arq_pump(&g_arq);
    OSRestoreInterrupts(previous);
    return result;
}

int melee_web_source_devcom_run(uint32_t aram_size, uint32_t aram_base)
{
    unsigned char* aram_storage;
    unsigned char* aram;
    MeleeWebSourceArqSpan spans[2];
    uint32_t* relay0;
    uint32_t* relay1;
    OSHeapHandle audio_heap;
    s32 heap_before;
    s32 heap_after;
    int callback_count = 0;
    int request;
    int second_request;
    HSD_DevCom* first_node;
    HSD_DevCom* second_node;
    unsigned i;

    if (aram_size == 0 || (aram_size & 31U) != 0 || aram_base % 32 != 0 ||
        (uint64_t) aram_base + 0xA20 > aram_size ||
        (uint64_t) aram_size + 31U > SIZE_MAX)
        fixture_fail("invalid source-derived ARAM bounds");

    aram_storage = (unsigned char*) malloc((size_t) aram_size + 31U);
    if (aram_storage == NULL) fixture_fail("ARAM provider allocation failed");
    aram = (unsigned char*) (((uintptr_t) aram_storage + 31U) & ~((uintptr_t) 31U));
    relay0 = (uint32_t*) HSD_DevCom_804C6330_bufs[0];
    relay1 = (uint32_t*) HSD_DevCom_804C6330_bufs[1];
    spans[0] = (MeleeWebSourceArqSpan){
        (uint32_t) (uintptr_t) relay0, published_relay[0], DEVCOM_BUF_SIZE};
    spans[1] = (MeleeWebSourceArqSpan){
        (uint32_t) (uintptr_t) relay1, published_relay[1], DEVCOM_BUF_SIZE};
    if (melee_web_source_arq_bind(&g_arq, spans, 2, aram, aram_size) !=
        MELEE_WEB_SOURCE_ARQ_OK)
        fixture_fail("source ARQ relay mapping was rejected");
    memset(aram, 0xA5, aram_size);
    memset(relay0, 0xCC, DEVCOM_BUF_SIZE);
    memset(relay1, 0xCC, DEVCOM_BUF_SIZE);
    ARQInit();
    audio_heap = HSD_Synth_804D6018;
    if (audio_heap < 0)
        fixture_fail("source HSD audio heap was not created");
    heap_before = OSCheckHeap(audio_heap);

    request = HSD_DevComRequest(0, 0, aram_base, 0x500, 3, 0, NULL, NULL);
    if (request != 7 || !HSD_DevComIsBusy(3) ||
        !melee_web_source_arq_pending(&g_arq))
        fixture_fail("first source type-3 request did not remain pending");
    if (memcmp(aram + aram_base, "\xA5\xA5\xA5\xA5", 4) != 0)
        fixture_fail("source ARQ copied before its completion boundary");
    for (i = 0; i < DEVCOM_BUF_SIZE / sizeof(*relay0); ++i) {
        if (relay0[i] != 0) fixture_fail("source relay buffer was not zeroed");
    }
    first_node = devComStatus[3];
    if (first_node == NULL || HSD_DevCom_804D77F0 == NULL)
        fixture_fail("source DevCom node/free list was not linked");
    // The first request owns dc[0]; validate the complete source-authored
    // free chain before reporting its block payload identity.
    require_source_span(first_node, "DevCom block");
    if ((uintptr_t)MEM1End - (uintptr_t)first_node <
        sizeof(HSD_DevCom) * INIT_N_DEVCOMS)
        fixture_fail("DevCom block crosses MEM1 end");
    HSD_DevCom* free_node = HSD_DevCom_804D77F0;
    for (i = 1; i < INIT_N_DEVCOMS; ++i) {
        if (free_node != first_node + i)
            fixture_fail("source DevCom free chain differs from authored block");
        free_node = free_node->next;
    }
    if (free_node != NULL) fixture_fail("source DevCom free chain is not bounded");
    heap_after = OSCheckHeap(audio_heap);
    if (heap_before < 0 || heap_after < 0 || heap_before <= heap_after)
        fixture_fail("source DevCom node did not consume the HSD audio heap");

    const int previous_mask = OSDisableInterrupts();
    if (fixture_pump() != MELEE_WEB_SOURCE_ARQ_BUSY || !aramstate ||
        !melee_web_source_arq_pending(&g_arq))
        fixture_fail("masked completion did not remain deferred");
    for (i = 0; i < 0x500; ++i) {
        if (aram[aram_base + i] != 0xA5)
            fixture_fail("ARAM changed before unmasked completion");
    }
    OSRestoreInterrupts(previous_mask);
    if (fixture_pump() != MELEE_WEB_SOURCE_ARQ_OK ||
        HSD_DevComIsBusy(3) || melee_web_source_arq_pending(&g_arq))
        fixture_fail("source type-3 completion did not drain its lane");
    for (i = 0; i < 0x500; ++i) {
        if (aram[aram_base + i] != 0)
            fixture_fail("source relay bytes did not reach ARAM");
    }
    if (devComStatus[3] != NULL || HSD_DevCom_804C6330[3] != NULL)
        fixture_fail("source completion left its lane linked");
    if (HSD_DevCom_804D77F0 != first_node)
        fixture_fail("source completion did not recycle the DevCom node");

    second_request = HSD_DevComRequest(
        0, 0, aram_base + 0x520, 0x500, 3, 0, completion_callback,
        (void*) (uintptr_t) &callback_count);
    second_node = devComStatus[3];
    if (second_request != 11 || second_node != first_node ||
        !melee_web_source_arq_pending(&g_arq))
        fixture_fail("source DevCom free-list reuse or serial changed");
    if (fixture_pump() != MELEE_WEB_SOURCE_ARQ_OK ||
        callback_count != 1 || HSD_DevComIsBusy(3))
        fixture_fail("source type-3 callback/recycle boundary was incorrect");

    if (fixture_pump() != MELEE_WEB_SOURCE_ARQ_NO_PENDING ||
        callback_count != 1 || aramstate || devComRelayBufFlag[0] ||
        devComRelayBufFlag[1] || !fixture_interrupts_enabled ||
        OSCheckHeap(audio_heap) != heap_after || cache_publish_count != 2)
        fixture_fail("completion leaked state or repeated a callback/allocation");
    for (i = 0; i < 0x500; ++i) {
        if (aram[aram_base + 0x520 + i] != 0)
            fixture_fail("second source transfer did not copy full bytes");
    }
    require_source_span(first_node, "HSD audio heap DevCom node");
    printf("{\"schema\":\"melee-web-source-devcom\",\"version\":1,\"status\":\"boundary_reached\",\"request\":{\"first\":%d,\"second\":%d,\"lane\":3,\"type\":3,\"size\":1280,\"entry_dest\":%u,\"callback_after_first\":0,\"callback_after_second\":%d},\"audio_heap\":{\"handle\":%d,\"free_before\":%d,\"free_after_node\":%d,\"node_size\":%lu,\"node_count\":16,\"node_source\":%u,\"block_payload_source\":%u},\"relay\":{\"size\":16384,\"buffers\":2,\"first_zeroed\":true,\"aram_bytes_zeroed\":true,\"deferred\":true,\"cache_publish_count\":2,\"recycled\":true,\"masked_delivery_rejected\":true,\"duplicate_completion_rejected\":true},\"aram\":{\"base\":%u,\"size\":%u,\"provider\":\"checked_source_arq\"},\"omitted_services\":[\"AI\",\"AX\",\"DSP\",\"DVD\",\"lbAudioAx\",\"lbMemory\",\"lbHeap\"]}\n",
            request, second_request, aram_base, callback_count, audio_heap,
            heap_before, heap_after, (unsigned long) sizeof(HSD_DevCom),
            source_address(first_node), source_address(first_node), aram_base, aram_size);
    if (melee_web_source_arq_shutdown(&g_arq) != MELEE_WEB_SOURCE_ARQ_OK)
        fixture_fail("completed ARQ owner did not shut down");
    ARQReset();
    free(aram_storage);
    return 0;
}
