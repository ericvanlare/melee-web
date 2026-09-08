#include "gameplay_bootstrap.h"
#include "gameplay_heap.h"
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/objalloc.h>
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
static OSHeapHandle heap = -1;
static uint64_t ticks, disabled_links, generation;
static int stepping, shutting_down, tables_live;

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
            if (object->obj_kind != HSD_GOBJ_OBJ_NONE) return 1;
    return 0;
}

int melee_web_gameplay_startup(size_t bytes, char* error, size_t error_size)
{
    if (arena || HSD_GObj_Entities || !melee_web_gameplay_heap_available() || HSD_GetHeap() != -1)
        return fail(error, error_size, "An HSD object world or SDK heap already exists");
    if (bytes < 65536 || bytes > 64U * 1024U * 1024U)
        return fail(error, error_size, "Gameplay bootstrap heap must be between 64 KiB and 64 MiB");
    arena = malloc(bytes);
    if (!arena) return fail(error, error_size, "Unable to allocate gameplay bootstrap arena");
    void* start = melee_web_gameplay_heap_initialize(arena, bytes, error, error_size);
    heap = start ? OSCreateHeap(start, (unsigned char*) arena + bytes) : -1;
    if (heap < 0) {
        if (start && !melee_web_gameplay_heap_release(arena, error, error_size)) return 0;
        free(arena); arena = NULL;
        return fail(error, error_size, "SDK heap initialization failed");
    }
    OSSetCurrentHeap(heap);
    HSD_SetHeap(heap);
    HSD_ObjSetHeap((u32) bytes, NULL);

    /* The table ownership mirrors gobjinit.c. Registering its default graphics
     * destructors would pull in uninitialized HSD render-object lifetimes. This
     * scoped world has no such object kinds; original GObj/proc allocation,
     * insertion, mutation, deletion and invocation execute unchanged below.
     * Source defaults are p/gx link max63; gm_1A45.c sets process max0x18. */
    HSD_GObjLibInitData = (HSD_GObjLibInitDataType) {63, 63, 0x18, NULL, &disabled_links};
    HSD_GObj_Entities = zero_table(64, sizeof(HSD_GObj*));
    plinklow_gobjs = zero_table(64, sizeof(HSD_GObj*));
    HSD_GObjGXLinkHead = zero_table(65, sizeof(HSD_GObj*));
    HSD_GObj_804D7820 = zero_table(65, sizeof(HSD_GObj*));
    HSD_GObj_804D7840 = zero_table(25, sizeof(HSD_GObjProc*));
    HSD_GObj_804D7844 = zero_table(25 * 64, sizeof(HSD_GObjProc*));
    HSD_GObj_804D7810 = NULL;
    HSD_ObjAllocInit(&gobj_alloc_data, sizeof(HSD_GObj), 4);
    HSD_ObjAllocInit(&gobjproc_alloc_data, sizeof(HSD_GObjProc), 4);
    HSD_GObj_804D783C = 0;
    HSD_GObj_804D7834 = 0;
    HSD_GObj_804D7830 = HSD_GObj_804D7838 = NULL;
    HSD_GObj_804D7814 = HSD_GObj_804D7818 = HSD_GObj_804D781C = NULL;
    HSD_GObj_804CE3E4.flags = 0;
    ticks = disabled_links = 0;
    ++generation;
    stepping = shutting_down = 0;
    tables_live = 1;
    return success(error, error_size);
}

int melee_web_gameplay_step(char* error, size_t error_size)
{
    if (!tables_live) return fail(error, error_size, "Gameplay bootstrap is not initialized");
    if (!melee_web_gameplay_heap_owns(arena))
        return fail(error, error_size, "Gameplay SDK heap ownership was replaced");
    if (shutting_down) return fail(error, error_size, "Gameplay world is being destroyed");
    if (stepping) return fail(error, error_size, "Gameplay process execution cannot be reentered");
    if (has_render_objects())
        return fail(error, error_size, "HSD render-object lifetimes are not initialized in this bootstrap");
    stepping = 1;
    HSD_GObj_80390CFC();
    stepping = 0;
    ++ticks;
    return success(error, error_size);
}

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
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        while (((HSD_GObj**) HSD_GObj_Entities)[link])
            HSD_GObjPLink_80390228(((HSD_GObj**) HSD_GObj_Entities)[link]);
    HSD_Free(HSD_GObj_Entities); HSD_Free(plinklow_gobjs);
    HSD_Free(HSD_GObjGXLinkHead); HSD_Free(HSD_GObj_804D7820);
    HSD_Free(HSD_GObj_804D7840); HSD_Free(HSD_GObj_804D7844);
    HSD_GObj_Entities = NULL; plinklow_gobjs = NULL;
    HSD_GObjGXLinkHead = HSD_GObj_804D7820 = NULL;
    HSD_GObj_804D7840 = HSD_GObj_804D7844 = NULL;
    HSD_GObj_804D7830 = HSD_GObj_804D7838 = NULL;
    _HSD_ObjAllocForgetMemory(NULL, NULL);
    HSD_ObjSetHeap(0, NULL);
    HSD_SetHeap(-1);
    OSDestroyHeap(heap);
    heap = -1;
    tables_live = 0;
    if (!melee_web_gameplay_heap_release(arena, error, error_size)) {
        shutting_down = 0;
        return 0;
    }
    free(arena); arena = NULL;
    shutting_down = 0;
    return success(error, error_size);
}
