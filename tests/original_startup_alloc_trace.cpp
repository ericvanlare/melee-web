#include <aurora/aurora.h>
#include <dolphin/gx/GXManage.h>
#include <dolphin/gx/GXStruct.h>
#include <dolphin/os.h>
extern "C" {
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/synth.h>
#include <sysdolphin/baselib/tev.h>
}

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Aurora's public headless initializer opens a window.  This fixture uses its
// checked Wasm32 OS provider directly, with only the two memory sizes supplied
// by the independently decoded boot context.  It is a test-only configuration
// bridge, not a source gameplay input or a replacement service.
namespace aurora {
extern AuroraConfig g_config;
}
extern void* MEM1Start;
extern void* MEM1End;
extern uintptr_t OSBaseAddress;
extern "C" HSD_ObjAllocData zlist_alloc_data;

namespace {

struct Args {
    u32 mem1 = 0, aram = 0, arena_hi = 0, arena_lo = 0;
    u32 crash_size = 0, crash_align = 0, crash_base = 0, after_crash = 0;
    u32 xfb_count = 0, xfb_size = 0, fb_width = 0, fb_height = 0;
    u32 xfb_begin = 0, after_xfb = 0;
    u32 fifo_size = 0, init_lo = 0, heap_max = 0, audio_size = 0;
    u32 aram_base = 0;
};

static_assert(sizeof(void*) == 4, "allocation fixture must be checked Wasm32");
// This is the private Aurora OSAlloc.cpp HeapDesc layout.  Keeping the
// source ABI assertion here avoids using the native-64 descriptor width.
struct SourceHeapDesc {
    s32 size;
    void* free_list;
    void* allocated;
};
static_assert(sizeof(SourceHeapDesc) == 12, "release Wasm32 HeapDesc changed");

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "original-startup-fixture: %s\n", message);
    std::exit(2);
}

u32 number(const char* text, const char* name) {
    errno = 0;
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 0);
    if (text[0] == '\0' || text[0] == '-' || errno == ERANGE || *end != '\0' || value > 0xffffffffUL) {
        std::fprintf(stderr, "original-startup-fixture: invalid %s\n", name);
        std::exit(2);
    }
    return static_cast<u32>(value);
}

u32 require(const Args& args, const char* key, u32 value) {
    if (value == 0 && std::strcmp(key, "crash_size") != 0) {
        std::fprintf(stderr, "original-startup-fixture: missing --%s\n", key);
        std::exit(2);
    }
    return value;
}

u32 round_up(uint64_t value, u32 alignment) {
    if (alignment == 0) fail("source alignment is zero");
    const uint64_t rounded = (value + alignment - 1) & ~(static_cast<uint64_t>(alignment) - 1);
    if (rounded > 0xffffffffU) fail("source alignment overflows uint32");
    return static_cast<u32>(rounded);
}

Args parse(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc || argv[i][0] != '-' || argv[i][1] != '-') {
            fail("arguments must be --name value pairs");
        }
        const char* key = argv[i] + 2;
        u32 value = number(argv[i + 1], key);
#define READ_ARG(name) else if (std::strcmp(key, #name) == 0) args.name = value
        if (false) {}
        READ_ARG(mem1);
        READ_ARG(aram);
        READ_ARG(arena_hi);
        READ_ARG(arena_lo);
        READ_ARG(crash_size);
        READ_ARG(crash_align);
        READ_ARG(crash_base);
        READ_ARG(after_crash);
        READ_ARG(xfb_count);
        READ_ARG(xfb_size);
        READ_ARG(fb_width);
        READ_ARG(fb_height);
        READ_ARG(xfb_begin);
        READ_ARG(after_xfb);
        READ_ARG(fifo_size);
        READ_ARG(init_lo);
        READ_ARG(heap_max);
        READ_ARG(audio_size);
        READ_ARG(aram_base);
        else fail("unknown argument");
#undef READ_ARG
    }
#define REQUIRE(name) require(args, #name, args.name)
    REQUIRE(mem1); REQUIRE(aram); REQUIRE(arena_hi); REQUIRE(arena_lo);
    REQUIRE(crash_align); REQUIRE(crash_base); REQUIRE(after_crash);
    REQUIRE(xfb_count); REQUIRE(xfb_size); REQUIRE(fb_width); REQUIRE(fb_height);
    REQUIRE(xfb_begin); REQUIRE(after_xfb);
    REQUIRE(fifo_size); REQUIRE(init_lo); REQUIRE(heap_max); REQUIRE(audio_size);
    REQUIRE(aram_base);
#undef REQUIRE
    return args;
}

uintptr_t source_address(const void* pointer) {
    if (pointer == nullptr) return 0;
    const uintptr_t host = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t mem1_start = reinterpret_cast<uintptr_t>(MEM1Start);
    const uintptr_t mem1_end = reinterpret_cast<uintptr_t>(MEM1End);
    if (MEM1Start == nullptr || MEM1End == nullptr || host < mem1_start || host >= mem1_end) {
        fail("source address is outside the provider-owned host MEM1 span");
    }
    return 0x80000000U + static_cast<uintptr_t>(OSCachedToPhysical(const_cast<void*>(pointer)));
}

void* cached_source(u32 source, u32 mem1) {
    if (source < 0x80000000U || source - 0x80000000U > mem1) {
        fail("source address is outside the fresh MEM1 mapping");
    }
    return OSPhysicalToCached(source - 0x80000000U);
}

void expect_source(const void* pointer, u32 expected, const char* label) {
    if (pointer == nullptr || source_address(pointer) != expected) {
        std::fprintf(stderr, "original-startup-fixture: %s source address mismatch (got %08lx expected %08x)\n",
                     label, static_cast<unsigned long>(source_address(pointer)), expected);
        std::exit(3);
    }
}

void expect_range(const void* pointer, u32 span, u32 begin, u32 end, const char* label) {
    u32 address = static_cast<u32>(source_address(pointer));
    if (pointer == nullptr || address != begin || end < begin || span > end - begin) {
        std::fprintf(stderr, "original-startup-fixture: %s span mismatch (got %08x+%x expected %08x..%08x)\n",
                     label, address, span, begin, end);
        std::exit(3);
    }
}

struct PoolView {
    const char* name;
    HSD_ObjAllocData* (*get)();
};

void print_pool_json(const PoolView& pool, bool* first) {
    const HSD_ObjAllocData* data = pool.get();
    if (!*first) std::printf(",");
    *first = false;
    std::printf("{\"name\":\"%s\",\"size\":%lu,\"align\":%lu,\"used\":%lu,\"free\":%lu,\"peak\":%lu}",
                pool.name, static_cast<unsigned long>(data->size),
                static_cast<unsigned long>(data->align + 1),
                static_cast<unsigned long>(data->used), static_cast<unsigned long>(data->free),
                static_cast<unsigned long>(data->peak));
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parse(argc, argv);
    if (args.arena_hi <= args.arena_lo || args.xfb_count == 0 || args.xfb_count > 3 ||
        args.xfb_size == 0 || args.fb_width == 0 || args.fb_height == 0 ||
        args.fb_width > 0xffff || args.fb_height > 0xffff || args.heap_max < 2 ||
        args.audio_size < 64 || args.crash_size == 0 || args.crash_align == 0 ||
        (args.crash_align & (args.crash_align - 1)) != 0 ||
        args.heap_max > 0xffffffffU / sizeof(SourceHeapDesc) || args.aram_base >= args.aram ||
        (args.aram_base & 31U) != 0) {
        fail("invalid source-derived bounds");
    }
    const uint64_t mem1_end = static_cast<uint64_t>(0x80000000U) + args.mem1;
    const uint64_t arena_lo = args.arena_lo;
    const uint64_t arena_hi = args.arena_hi;
    const uint64_t xfb_end_unrounded = static_cast<uint64_t>(args.xfb_begin) +
                                       static_cast<uint64_t>(args.xfb_count) * args.xfb_size;
    const u32 expected_after_xfb = round_up(xfb_end_unrounded, 32);
    const uint64_t expected_init_lo_wide = static_cast<uint64_t>(expected_after_xfb) + args.fifo_size;
    if (expected_init_lo_wide > 0xffffffffU) fail("source FIFO end overflows uint32");
    const u32 expected_init_lo = static_cast<u32>(expected_init_lo_wide);
    const u32 descriptor_bytes = args.heap_max * sizeof(SourceHeapDesc);
    const u32 descriptor_end = round_up(static_cast<uint64_t>(args.init_lo) + descriptor_bytes, 32);
    const uint64_t audio_end_wide = static_cast<uint64_t>(descriptor_end) + args.audio_size;
    const uint64_t rounded_arena_hi = static_cast<uint64_t>(args.arena_hi) & ~31U;
    const uint64_t audio_end_rounded = audio_end_wide & ~31U;
    const uint64_t main_begin_rounded = (audio_end_wide + 31U) & ~31U;
    if (descriptor_end <= args.init_lo || audio_end_wide > rounded_arena_hi ||
        audio_end_rounded < static_cast<uint64_t>(descriptor_end) + 64U ||
        main_begin_rounded + 64U > rounded_arena_hi) {
        fail("source descriptor/audio reservation leaves no valid main heap");
    }
    if (args.arena_lo < 0x80000000U || args.arena_hi > mem1_end ||
        arena_lo >= arena_hi || args.crash_base < args.arena_lo ||
        static_cast<uint64_t>(args.crash_base) + args.crash_size > arena_hi ||
        args.crash_base != round_up(args.arena_lo, args.crash_align) ||
        args.after_crash != round_up(static_cast<uint64_t>(args.crash_base) + args.crash_size, args.crash_align) ||
        args.after_crash > args.arena_hi ||
        args.xfb_begin != round_up(args.after_crash, 32) ||
        xfb_end_unrounded > arena_hi ||
        args.after_xfb != expected_after_xfb ||
        args.init_lo != expected_init_lo ||
        static_cast<uint64_t>(args.init_lo) + args.heap_max * sizeof(SourceHeapDesc) > arena_hi) {
        fail("source-derived allocation span is outside fresh MEM1");
    }

    aurora::g_config.mem1Size = args.mem1;
    aurora::g_config.mem2Size = args.aram;
    // original_startup_osmemory.cpp includes Aurora's pinned OSMemory.cpp and
    // changes only its calloc-backed MEM1 provider to return a zeroed,
    // 32-byte-aligned span.  OSInit therefore owns this mapping from the
    // beginning; no post-init global replacement can hide provider state.
    OSInit();
    const uintptr_t provider_start = reinterpret_cast<uintptr_t>(MEM1Start);
    const uintptr_t provider_end = reinterpret_cast<uintptr_t>(MEM1End);
    if (MEM1Start == nullptr || MEM1End == nullptr || provider_end < provider_start ||
        (provider_start & 31U) != 0 || provider_end - provider_start != args.mem1 ||
        OSBaseAddress != provider_start) {
        fail("Aurora source MEM1 provider did not publish an aligned owned span");
    }
    if (OSGetPhysicalMemSize() != args.mem1) {
        fail("Aurora MEM1 size differs from source boot context");
    }
    OSSetArenaLo(cached_source(args.arena_lo, args.mem1));
    OSSetArenaHi(cached_source(args.arena_hi, args.mem1));

    GXRenderModeObj render{};
    // The context already derived the NTSC framebuffer span from the pinned
    // DOL.  Keep the fixture independent of a linked GX global while still
    // exercising HSD_AllocateXFB's authored size/alignment calculation.
    render.fbWidth = static_cast<u16>(args.fb_width);
    render.xfbHeight = static_cast<u16>(args.fb_height);
    const uint64_t render_size_wide =
        static_cast<uint64_t>((static_cast<u32>(render.fbWidth) + 0xF) & 0xFFF0U) *
        render.xfbHeight * 2;
    if (render_size_wide > 0xffffffffU || static_cast<u32>(render_size_wide) != args.xfb_size) {
        fail("source render geometry differs from XFB span");
    }
    // HSD_SetInitParameter's source implementation returns its `ok` flag for
    // the numeric cases but leaves it false for HSD_INIT_RENDER_MODE_OBJ even
    // after installing the pointer.  Keep the source call, but do not treat
    // that intentional return-value asymmetry as a rejected render mode.
    if (!HSD_SetInitParameter(HSD_INIT_XFB_MAX_NUM, args.xfb_count) ||
        !HSD_SetInitParameter(HSD_INIT_FIFO_SIZE, args.fifo_size) ||
        !HSD_SetInitParameter(HSD_INIT_HEAP_MAX_NUM, args.heap_max) ||
        !HSD_SetInitParameter(HSD_INIT_AUDIO_HEAP_SIZE, args.audio_size)) {
        fail("source HSD init parameters were rejected");
    }
    HSD_SetInitParameter(HSD_INIT_RENDER_MODE_OBJ, &render);

    void* crash = OSAllocFromArenaLo(args.crash_size, args.crash_align);
    expect_source(crash, args.crash_base, "crash allocation");
    expect_source(OSGetArenaLo(), args.after_crash, "crash allocation end");

    void** xfb = HSD_AllocateXFB(static_cast<s32>(args.xfb_count), &render);
    if (xfb == nullptr) fail("HSD_AllocateXFB returned no buffer table");
    expect_range(xfb[0], args.xfb_size, args.xfb_begin, args.after_xfb, "XFB");
    if (args.xfb_count > 1) {
        expect_source(xfb[1], args.xfb_begin + args.xfb_size, "second XFB");
    }
    expect_source(OSGetArenaLo(), args.after_xfb, "XFB allocation end");

    GXFifoObj* fifo = HSD_AllocateFifo(args.fifo_size);
    expect_source(fifo, args.after_xfb, "FIFO allocation");
    expect_source(OSGetArenaLo(), args.init_lo, "FIFO allocation end");
    // The authored caller passes the raw allocation through GXInit before
    // publishing it to HSD_GXSetFifoObj. Preserve that real boundary instead
    // of binding uninitialized storage as if it were a GXFifoObj.
    GXFifoObj* initialized_fifo = GXInit(fifo, args.fifo_size);
    if (initialized_fifo == nullptr) fail("Aurora GXInit rejected the source FIFO");
    HSD_GXSetFifoObj(initialized_fifo);

    // The wrapper deliberately shares initialize.c's private HSD state and
    // stops before HSD_InitComponent's VI/GX/DVD/retrace work.  This is the
    // source HSD_OSInit/IDSetup/ObjInit boundary, not a full game startup.
    HSD_AllocationInit();
    void* next_lo = nullptr;
    void* next_hi = nullptr;
    HSD_GetNextArena(&next_lo, &next_hi);
    if (source_address(next_lo) <= args.init_lo || source_address(next_hi) != args.arena_hi) {
        fail("HSD arena roots differ from source boot bounds");
    }
    if (source_address(next_lo) != descriptor_end + args.audio_size) {
        fail("checked-Wasm32 HSD descriptor/audio reservation differs from source");
    }

    // ARInit/ARQInit alone are not source-equivalent.  The retail audio
    // path performs ARAlloc(0x500), ARAlloc(bank_size), and ARAlloc(0x30000)
    // in HSD_SynthInit before lbMemory_8001564C; AI/AX and bank metadata are
    // also required.  Since those authored services are outside this narrow
    // target, stop here rather than claiming lbMemory/lbHeap roots.
    const PoolView pools[] = {
        {"slist", HSD_SListGetAllocData}, {"dlist", HSD_DListGetAllocData},
        {"aobj", HSD_AObjGetAllocData}, {"fobj", HSD_FObjGetAllocData},
        {"id", HSD_IDGetAllocData}, {"vec", HSD_VecGetAllocData},
        {"mtx", HSD_MtxGetAllocData}, {"robj", HSD_RObjGetAllocData},
        {"rvalue", HSD_RvalueObjGetAllocData}, {"render", HSD_RenderGetAllocData},
        {"tevreg", HSD_TevRegGetAllocData}, {"chan", HSD_ChanGetAllocData},
        {"shadow", HSD_ShadowGetAllocData}, {"zlist", []() { return &zlist_alloc_data; }},
    };
    const OSHeapHandle audio_heap = HSD_Synth_804D6018;
    const OSHeapHandle main_heap = HSD_GetHeap();
    if (audio_heap < 0 || main_heap < 0) fail("source HSD heap creation returned an invalid handle");
    const s32 main_heap_free_signed = OSCheckHeap(main_heap);
    if (main_heap_free_signed < 0) fail("source HSD main heap failed its integrity check");
    const u32 main_heap_free = static_cast<u32>(main_heap_free_signed);
    const u32 hsd_lo = static_cast<u32>(source_address(next_lo));
    const u32 hsd_hi = static_cast<u32>(source_address(next_hi));
    if (hsd_lo < args.audio_size) fail("measured HSD audio heap end underflows source span");
    const u32 measured_audio_begin = hsd_lo - args.audio_size;
    const u32 expected_main_begin = measured_audio_begin + args.audio_size;
    if (measured_audio_begin != descriptor_end || hsd_lo != expected_main_begin) {
        fail("measured HSD audio/main heap bounds differ from source-derived span");
    }
    std::printf("{\"schema\":\"melee-web-original-startup-fixture\",\"version\":1,\"status\":\"boundary_reached\",\"boundary\":\"hsd_os_id_obj\",\"next_source_boundary\":\"lbAudioAx_8002838C\",\"arena\":{\"crash\":%lu,\"after_crash\":%lu,\"xfb_begin\":%lu,\"after_xfb\":%lu,\"fifo_begin\":%lu,\"after_fifo\":%lu,\"fifo_bound\":true,\"hsd_next_lo\":%lu,\"hsd_next_hi\":%lu},\"heap\":{\"descriptor_bytes\":%lu,\"audio_bounds\":\"derived_from_measured_hsd_next_lo\",\"audio_handle\":%d,\"audio_begin\":%lu,\"audio_end\":%lu,\"main_handle\":%d,\"main_begin\":%lu,\"main_end\":%lu,\"main_free\":%lu},\"object_pools\":[",
                static_cast<unsigned long>(args.crash_base), static_cast<unsigned long>(args.after_crash),
                static_cast<unsigned long>(args.xfb_begin), static_cast<unsigned long>(args.after_xfb),
                static_cast<unsigned long>(args.after_xfb), static_cast<unsigned long>(args.init_lo),
                static_cast<unsigned long>(hsd_lo), static_cast<unsigned long>(hsd_hi),
                static_cast<unsigned long>(descriptor_bytes), audio_heap,
                static_cast<unsigned long>(measured_audio_begin), static_cast<unsigned long>(hsd_lo),
                main_heap, static_cast<unsigned long>(hsd_lo),
                static_cast<unsigned long>(hsd_hi), static_cast<unsigned long>(main_heap_free));
    bool first = true;
    for (const PoolView& pool : pools) print_pool_json(pool, &first);
    std::printf("],\"source_aram\":{\"base\":%lu,\"size\":%lu,\"initialized\":false},\"omitted_services\":[\"VI\",\"HSD_GXInit\",\"DVD\",\"retrace\",\"AI\",\"AX\",\"ARInit\",\"lbMemory\",\"lbHeap\"]}\n",
                static_cast<unsigned long>(args.aram_base), static_cast<unsigned long>(args.aram));
    return 0;
}
