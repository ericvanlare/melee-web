#include "gameplay_bootstrap.h"
#include "gameplay_source_context.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_heap.h"
#include <melee/lb/lb_00F9.h>
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(void*) == 4, "Original runtime requires Wasm32 pointers");
_Static_assert(sizeof(HSD_GObj) == 0x38 && offsetof(HSD_GObj, proc) == 0x18 &&
               offsetof(HSD_GObj, user_data) == 0x2c, "Original HSD GObj layout");
_Static_assert(sizeof(HSD_GObjProc) == 0x18 && offsetof(HSD_GObjProc, gobj) == 0x10,
               "Original HSD process layout");
_Static_assert(sizeof(HSD_ObjAllocData) == 0x2c, "Original object allocator layout");

extern HSD_ObjAllocData gobj_alloc_data, gobjproc_alloc_data;

static void* arena;
static size_t arena_bytes;
static void* session_arena;
static size_t session_bytes;
static OSHeapHandle heap = -1;
static uint64_t ticks, disabled_links, generation, allocation_generation;
static int stepping, shutting_down, tables_live;
static int vs_startup_pending, startup_in_progress, vs_sis_live;
static int vs_dynamics_ready, vs_manager_ready;
static MeleeWebGameplayVSStartup vs_startup_callback;
static MeleeWebGameplayVSShutdown vs_shutdown_callback;
static unsigned object_kind_count;
static void (*finish_hsd_objects)(void);

static int fail(char* error, size_t size, const char* text)
{
    if (error && size) snprintf(error, size, "%s", text);
    return 0;
}
static int success(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
    return 1;
}
static void* zero_table(size_t count, size_t element_size)
{
    void* result = HSD_MemAlloc((ssize_t) (count * element_size));
    /* HSD_MemAlloc asserts on allocation failure, matching original behavior. */
    memset(result, 0, count * element_size);
    return result;
}
static int has_render_objects(void)
{
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link]; object; object = object->next)
            if (object->obj_kind != HSD_GOBJ_OBJ_NONE &&
                object->obj_kind >= object_kind_count) return 1;
    return 0;
}

static uint64_t next_allocation_generation(void)
{
    ++allocation_generation;
    if (allocation_generation == 0) ++allocation_generation;
    return allocation_generation;
}

static void* allocation_arena(void)
{
    return arena ? arena : session_arena;
}

int melee_web_gameplay_session_active(void)
{
    return session_arena != NULL;
}

MeleeWebGameplayAllocation melee_web_gameplay_allocation(void)
{
    MeleeWebGameplayAllocation result = {0};
    void* candidate = allocation_arena();
    if (!candidate) return result;
    /* A live world or an already initialized session must still own the
     * original SDK roots. Before the first world, heap_available() proves
     * that no foreign allocator has claimed the raw session allocation. */
    if ((arena && !melee_web_gameplay_heap_owns(candidate)) ||
        (!arena && !melee_web_gameplay_heap_owns(candidate) &&
         !melee_web_gameplay_heap_available()))
        return result;
    result.identity = (uint64_t) (uintptr_t) candidate;
    result.generation = allocation_generation;
    result.bytes = (uint64_t) (arena ? arena_bytes : session_bytes);
    return result;
}

int melee_web_gameplay_session_begin(size_t bytes, char* error, size_t error_size)
{
    if (session_arena || arena || HSD_GObj_Entities || HSD_GetHeap() != -1 ||
        !melee_web_gameplay_heap_available())
        return fail(error, error_size, "An idle process with no SDK allocator is required for a gameplay session");
    if (bytes < 65536 || bytes > 64U * 1024U * 1024U)
        return fail(error, error_size, "Gameplay session arena must be between 64 KiB and 64 MiB");
    void* candidate = malloc(bytes);
    if (!candidate) return fail(error, error_size, "Unable to allocate gameplay session arena");
    session_arena = candidate;
    session_bytes = bytes;
    next_allocation_generation();
    return success(error, error_size);
}

int melee_web_gameplay_session_end(char* error, size_t error_size)
{
    if (!session_arena) return success(error, error_size);
    if (arena || tables_live || HSD_GetHeap() != -1)
        return fail(error, error_size, "Gameplay session cannot end while a world is live");
    if (melee_web_gameplay_heap_owns(session_arena)) {
        if (!melee_web_gameplay_heap_release(session_arena, error, error_size)) return 0;
    } else if (!melee_web_gameplay_heap_available()) {
        return fail(error, error_size, "Gameplay session SDK arena ownership changed");
    }
    free(session_arena);
    session_arena = NULL;
    session_bytes = 0;
    return success(error, error_size);
}

int melee_web_gameplay_startup(size_t bytes, char* error, size_t error_size)
{
    if (arena || HSD_GObj_Entities || HSD_GetHeap() != -1)
        return fail(error, error_size, "An HSD object world or SDK heap already exists");
    if (bytes < 65536 || bytes > 64U * 1024U * 1024U)
        return fail(error, error_size, "Gameplay bootstrap heap must be between 64 KiB and 64 MiB");
    const int vs_startup = vs_startup_pending;
    vs_startup_pending = 0;
    const int retained = session_arena != NULL;
    if (retained && bytes != session_bytes)
        return fail(error, error_size, "Gameplay world heap size differs from its session arena");
    void* candidate = retained ? session_arena : malloc(bytes);
    if (!candidate) return fail(error, error_size, "Unable to allocate gameplay bootstrap arena");
    void* start = NULL;
    if (retained && melee_web_gameplay_heap_owns(candidate)) {
        if (!melee_web_gameplay_heap_recreate(candidate, &heap, error, error_size))
            return 0;
    } else {
        if (retained && !melee_web_gameplay_heap_available()) {
            return fail(error, error_size, "Gameplay session SDK arena ownership changed");
        }
        start = melee_web_gameplay_heap_initialize(candidate, bytes, error, error_size);
        heap = start ? OSCreateHeap(start, (unsigned char*) candidate + bytes) : -1;
    }
    if (heap < 0) {
        if (start && !melee_web_gameplay_heap_release(candidate, error, error_size)) return 0;
        if (!retained) free(candidate);
        return fail(error, error_size, "SDK heap initialization failed");
    }
    const uint64_t previous_world_generation = generation;
    uint64_t next_world_generation = generation + 1;
    if (next_world_generation == 0) next_world_generation = 1;
    if (!melee_web_source_memory_begin(next_world_generation, heap)) {
        OSDestroyHeap(heap);
        heap = -1;
        OSSetCurrentHeap(-1);
        HSD_SetHeap(-1);
        if (!retained) {
            if (!melee_web_gameplay_heap_release(candidate, error, error_size)) return 0;
            free(candidate);
        }
        return fail(error, error_size, "Source main-heap address context could not begin");
    }
    arena = candidate;
    arena_bytes = bytes;
    if (!retained) next_allocation_generation();
    OSSetCurrentHeap(heap);
    HSD_SetHeap(heap);
    HSD_ObjSetHeap((u32) bytes, NULL);
    generation = next_world_generation;
    startup_in_progress = vs_startup;
    vs_sis_live = 0;
    vs_dynamics_ready = 0;
    vs_manager_ready = 0;
    object_kind_count = 0;
    finish_hsd_objects = NULL;
    HSD_GObj_804D7810 = NULL;
    HSD_GObj_804D783C = 0;
    HSD_GObj_804D7834 = 0;
    HSD_GObj_804D7830 = HSD_GObj_804D7838 = NULL;
    HSD_GObj_804D7814 = HSD_GObj_804D7818 = HSD_GObj_804D781C = NULL;
    HSD_GObj_804CE3E4.flags = 0;
    ticks = disabled_links = 0;
    stepping = shutting_down = 0;
    if (vs_startup) {
        /* The source manager owns SObj registration and every GObj table. The
         * world callback prepares native HSD component pools and SIS, then
         * enters the original gm_801A4BD4 implementation. */
        if (!vs_startup_callback(error, error_size)) {
            startup_in_progress = 0;
            HSD_ObjSetHeap(0, NULL);
            HSD_SetHeap(-1);
            const int source_memory_ok = melee_web_source_memory_end(generation);
            OSDestroyHeap(heap);
            heap = -1;
            OSSetCurrentHeap(-1);
            if (!melee_web_source_context_reset_world(generation))
                return fail(error, error_size,
                            "Source PPC context could not reset after VS manager preparation failed");
            if (!retained) {
                if (!melee_web_gameplay_heap_release(candidate, error, error_size)) return 0;
                free(candidate);
            }
            arena = NULL;
            arena_bytes = 0;
            generation = previous_world_generation;
            if (!source_memory_ok)
                return fail(error, error_size,
                            "Source main-heap allocation mirror failed while rejecting VS startup");
            if (error && error_size && !error[0])
                snprintf(error, error_size, "Original VS scene-manager preparation failed");
            return 0;
        }
        if (!HSD_GObj_Entities || !plinklow_gobjs || !HSD_GObjGXLinkHead ||
            !HSD_GObj_804D7820 || !HSD_GObj_804D7840 || !HSD_GObj_804D7844 ||
            !HSD_GObj_804D7810 || !HSD_GObjLibInitData.funcs) {
            startup_in_progress = 0;
            return fail(error, error_size,
                        "Original VS scene manager returned without publishing its GObj tables and kind registry");
        }
        for (GObjFuncs* p = HSD_GObjLibInitData.funcs; p; p = p->next)
            object_kind_count += p->size;
        if (object_kind_count != 5 || HSD_SObjLib_804D7960 != 0) {
            startup_in_progress = 0;
            return fail(error, error_size,
                        "Original VS scene manager did not publish the five source GObj kinds in order");
        }
        vs_sis_live = 1;
        vs_manager_ready = 1;
        vs_dynamics_ready = 0;
        startup_in_progress = 0;
    } else {
        vs_sis_live = 0;
        vs_dynamics_ready = 1;
        /* Fixture worlds retain the narrow GObj scheduler. The actual VS path
         * below takes its tables directly from HSD_GObj_80391304. */
        HSD_GObjLibInitData = (HSD_GObjLibInitDataType) {63, 63, 0x18, NULL, &disabled_links};
        HSD_GObj_Entities = zero_table(64, sizeof(HSD_GObj*));
        plinklow_gobjs = zero_table(64, sizeof(HSD_GObj*));
        HSD_GObjGXLinkHead = zero_table(65, sizeof(HSD_GObj*));
        HSD_GObj_804D7820 = zero_table(65, sizeof(HSD_GObj*));
        HSD_GObj_804D7840 = zero_table(25, sizeof(HSD_GObjProc*));
        HSD_GObj_804D7844 = zero_table(25 * 64, sizeof(HSD_GObjProc*));
        HSD_ObjAllocInit(&gobj_alloc_data, sizeof(HSD_GObj), 4);
        HSD_ObjAllocInit(&gobjproc_alloc_data, sizeof(HSD_GObjProc), 4);
        lb_8000FCDC();
    }
    /* VS dynamics belong to the later Fighter_FirstInitialize boundary. */
    tables_live = 1;
    return success(error, error_size);
}

int melee_web_gameplay_prepare_vs_startup(MeleeWebGameplayVSStartup startup,
                                          MeleeWebGameplayVSShutdown shutdown,
                                          char* error, size_t error_size)
{
    if (arena || tables_live || HSD_GObj_Entities ||
        HSD_GetHeap() != -1 || vs_startup_pending || vs_sis_live ||
        !startup || !shutdown)
        return fail(error, error_size,
                    "VS preload requires an idle process before gameplay startup");
    vs_startup_callback = startup;
    vs_shutdown_callback = shutdown;
    vs_startup_pending = 1;
    return success(error, error_size);
}

int melee_web_gameplay_vs_startup_active(void)
{
    return vs_sis_live && vs_manager_ready && tables_live && !shutting_down &&
           melee_web_gameplay_heap_owns(arena);
}

int melee_web_gameplay_vs_manager_preparing(void)
{
    return startup_in_progress && arena && !tables_live && !shutting_down &&
           melee_web_gameplay_heap_owns(arena);
}

int melee_web_gameplay_initialize_vs_dynamics(char* error, size_t error_size)
{
    if (!arena || !tables_live || shutting_down || stepping ||
        !vs_sis_live || vs_dynamics_ready ||
        !melee_web_gameplay_heap_owns(arena))
        return fail(error, error_size,
                    "VS dynamics pool requires the live source-preloaded world");
    lb_8000FCDC();
    vs_dynamics_ready = 1;
    return success(error, error_size);
}

int melee_web_gameplay_enable_hsd_objects(void (*after_objects)(void),
                                         char* error, size_t error_size)
{
    if (!tables_live || shutting_down || stepping ||
        !melee_web_gameplay_heap_owns(arena))
        return fail(error, error_size, "An idle owned gameplay world is required for HSD lifetimes");
    if (!after_objects)
        return fail(error, error_size, "Native HSD lifetimes require post-object class cleanup");
    if (finish_hsd_objects)
        return finish_hsd_objects == after_objects ? success(error, error_size) :
            fail(error, error_size, "Native HSD lifetime ownership is already registered");
    if (vs_manager_ready) {
        unsigned count = 0;
        for (GObjFuncs* p = HSD_GObjLibInitData.funcs; p; p = p->next)
            count += p->size;
        if (count != object_kind_count || count != 5 || !HSD_GObj_804D7810 ||
            !HSD_GObj_Entities)
            return fail(error, error_size,
                        "Original VS scene-manager GObj registry lost source ownership");
        finish_hsd_objects = after_objects;
        return success(error, error_size);
    }
    if (HSD_GObjLibInitData.funcs || HSD_GObj_804D7810 || has_render_objects())
        return fail(error, error_size, "Cannot replace an existing HSD object-kind registry");
    /* Exact registry setup and flattening used by original gobjinit.c. */
    HSD_GObj_80391260(&HSD_GObjLibInitData);
    for (GObjFuncs* p = HSD_GObjLibInitData.funcs; p; p = p->next)
        object_kind_count += p->size;
    HSD_GObj_804D7810 = zero_table(object_kind_count, sizeof(GObjFunc));
    unsigned offset = 0;
    for (GObjFuncs* p = HSD_GObjLibInitData.funcs; p; p = p->next)
        for (unsigned i = 0; i < p->size; ++i)
            HSD_GObj_804D7810[offset++] = p->funcs[i];
    finish_hsd_objects = after_objects;
    return success(error, error_size);
}

int melee_web_gameplay_step(char* error, size_t error_size)
{
    if (!tables_live) return fail(error, error_size, "Gameplay bootstrap is not initialized");
    if (!melee_web_gameplay_heap_owns(arena))
        return fail(error, error_size, "Gameplay SDK heap ownership was replaced");
    if (!melee_web_source_memory_healthy())
        return fail(error, error_size, "Source main-heap allocation mirror is invalid");
    if (shutting_down) return fail(error, error_size, "Gameplay world is being destroyed");
    if (stepping) return fail(error, error_size, "Gameplay process execution cannot be reentered");
    if (has_render_objects())
        return fail(error, error_size, "HSD render-object lifetimes are not initialized in this bootstrap");
    if (!melee_web_source_context_begin_tick(generation))
        return fail(error, error_size, "Source PPC callback context is unavailable for this scheduler tick");
    stepping = 1;
    HSD_GObj_80390CFC();
    stepping = 0;
    if (!melee_web_source_context_end_tick())
        return fail(error, error_size, "Source PPC callback frames did not balance at scheduler return");
    if (!melee_web_source_memory_healthy())
        return fail(error, error_size, "Source main-heap allocation mirror rejected a scheduler allocation");
    ++ticks;
    return success(error, error_size);
}

int melee_web_gameplay_world_exists(void) { return arena != NULL; }

MeleeWebGameplayStats melee_web_gameplay_stats(void)
{
    MeleeWebGameplayStats result = {0};
    if (!tables_live || shutting_down || !melee_web_gameplay_heap_owns(arena)) return result;
    result.ticks = ticks;
    result.objects = gobj_alloc_data.used;
    result.processes = gobjproc_alloc_data.used;
    result.object_peak = gobj_alloc_data.peak;
    result.process_peak = gobjproc_alloc_data.peak;
    result.heap_free_bytes = OSCheckHeap(heap);
    result.generation = generation;
    return result;
}

uint64_t melee_web_gameplay_generation(void)
{
    if ((!tables_live && !startup_in_progress) || shutting_down ||
        !melee_web_gameplay_heap_owns(arena)) return 0;
    return generation;
}
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
uint64_t melee_web_gameplay_provenance_tick(void)
{
    return melee_web_gameplay_generation() ? ticks : 0;
}
#endif

int melee_web_gameplay_shutdown(char* error, size_t error_size)
{
    if (!arena) return success(error, error_size);
    if (shutting_down) return fail(error, error_size, "Gameplay world destruction cannot be reentered");
    if (stepping) return fail(error, error_size, "Cannot destroy the gameplay world during a process callback");
    if (!melee_web_gameplay_heap_owns(arena))
        return fail(error, error_size, "Gameplay SDK heap ownership was replaced; arena retained");
    if (!tables_live) {
        if (!melee_web_gameplay_heap_release(arena, error, error_size)) return 0;
        free(arena); arena = NULL;
        return success(error, error_size);
    }
    if (has_render_objects())
        return fail(error, error_size, "Cannot destroy uninitialized HSD render-object lifetimes");
    shutting_down = 1;
    if (vs_sis_live) {
        /* Source SIS teardown owns its text GObjs and must run before the
         * generic object sweep and before destroying the source heap. */
        vs_shutdown_callback();
        vs_sis_live = 0;
    }
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        while (((HSD_GObj**) HSD_GObj_Entities)[link])
            HSD_GObjPLink_80390228(((HSD_GObj**) HSD_GObj_Entities)[link]);
    if (finish_hsd_objects) finish_hsd_objects();
    finish_hsd_objects = NULL;
    vs_manager_ready = 0;
    if (HSD_GObj_804D7810) HSD_Free(HSD_GObj_804D7810);
    HSD_GObj_804D7810 = NULL;
    HSD_GObjLibInitData.funcs = NULL;
    object_kind_count = 0;
    HSD_Free(HSD_GObj_Entities); HSD_Free(plinklow_gobjs);
    HSD_Free(HSD_GObjGXLinkHead); HSD_Free(HSD_GObj_804D7820);
    HSD_Free(HSD_GObj_804D7840); HSD_Free(HSD_GObj_804D7844);
    HSD_GObj_Entities = NULL; plinklow_gobjs = NULL;
    HSD_GObjGXLinkHead = HSD_GObj_804D7820 = NULL;
    HSD_GObj_804D7840 = HSD_GObj_804D7844 = NULL;
    HSD_GObj_804D7830 = HSD_GObj_804D7838 = NULL;
    /* HSD_CreateMainHeap forgets the base class library before replacing its
     * heap. The scoped browser bootstrap keeps one OS arena across scene
     * generations, so perform that original amnesia while the current HSD
     * heap is still live; otherwise GetMemoryEntry reuses old scene indices. */
    hsdForgetClassLibrary("sysdolphin_base_library");
    _HSD_ObjAllocForgetMemory(NULL, NULL);
    HSD_ObjSetHeap(0, NULL);
    HSD_SetHeap(-1);
    const int source_memory_ok = melee_web_source_memory_end(generation);
    OSDestroyHeap(heap);
    heap = -1;
    tables_live = 0;
    vs_dynamics_ready = 0;
    startup_in_progress = 0;
    vs_startup_callback = NULL;
    vs_shutdown_callback = NULL;
    if (session_arena) {
        /* Keep the claimed descriptor array and its inactive heap metadata so
         * the next world can call heap_recreate over the same bounds. This is
         * the isolated equivalent of retail HSD_CreateMainHeap: the object
         * tables above are gone, the old OS heap is destroyed, and no payload
         * bytes are cleared between scenes. */
        if (!melee_web_source_context_reset_world(generation)) {
            shutting_down = 0;
            return fail(error, error_size, "Source PPC context still has a live scheduler owner");
        }
        arena = NULL;
        arena_bytes = 0;
        shutting_down = 0;
        if (!source_memory_ok)
            return fail(error, error_size, "Source main-heap allocation mirror failed before teardown");
        return success(error, error_size);
    }
    if (!melee_web_gameplay_heap_release(arena, error, error_size)) {
        shutting_down = 0;
        return 0;
    }
    free(arena); arena = NULL; arena_bytes = 0;
    if (!melee_web_source_context_reset_world(generation)) {
        shutting_down = 0;
        return fail(error, error_size, "Source PPC context still has a live scheduler owner");
    }
    shutting_down = 0;
    if (!source_memory_ok)
        return fail(error, error_size, "Source main-heap allocation mirror failed before teardown");
    return success(error, error_size);
}
