/*
 * Synthetic differential fixture for the untouched original lbheap.c.  The
 * source is included directly; only the unrelated OS/HSD/ARAM services are
 * small explicit recorders.  Returned handles and heap ids are normalized in
 * the JSON stream, so the fixture does not expose host or captured addresses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../.deps/melee/src/melee/lb/lbheap.c"

enum { MAX_CALLS = 128, MAX_HANDLES = 64 };

struct Call {
    const char* kind;
    unsigned index;
    int id;
    Handle* handle;
    uintptr_t lo;
    uintptr_t hi;
};

static uintptr_t fixture_arena_lo;
static uintptr_t fixture_arena_hi;
static uintptr_t fixture_aram_lo;
static uintptr_t fixture_aram_hi;
static struct Call calls[MAX_CALLS];
static unsigned call_count;
static int next_heap_id;
static Handle handle_storage[MAX_HANDLES];
static unsigned handle_count;

/* These paths are outside the lifecycle fixture.  They are explicit aborting
 * stubs so an accidental call cannot become silent oracle success. */
int HSD_GetHeap(void) { abort(); }
void HSD_SetHeap(int heap) { (void)heap; abort(); }
void* HSD_MemAlloc(size_t size) { (void)size; abort(); }
void HSD_Free(void* address) { (void)address; abort(); }
int OSDisableInterrupts(void) { abort(); }
void OSRestoreInterrupts(int state) { (void)state; abort(); }
int OSCheckHeap(int heap) { (void)heap; abort(); }
u32 lbMemory_80014F7C(Handle* handle) { (void)handle; abort(); }
Handle* lbMemory_80014FC8(Handle* handle, size_t size)
{
    (void)handle;
    (void)size;
    abort();
}
void lbMemFreeToHeap(Handle* handle, void* address)
{
    (void)handle;
    (void)address;
    abort();
}
u32 lbMemory_8001529C(Handle* handle, void (*callback)(u32), u32 heap)
{
    (void)handle;
    (void)callback;
    (void)heap;
    abort();
}

static void record_call(const char* kind, unsigned index, int id, Handle* handle,
                        uintptr_t lo, uintptr_t hi)
{
    if (call_count >= MAX_CALLS) abort();
    calls[call_count++] = (struct Call){kind, index, id, handle, lo, hi};
}

static unsigned heap_index_for_span(uintptr_t lo, uintptr_t hi)
{
    unsigned index;
    for (index = 0; index < 6; ++index) {
        const struct Heap* heap = &lbHeap_80431FA0.heap_array[index];
        if ((uintptr_t)heap->start == lo &&
            lo + heap->size == hi)
            return index;
    }
    return 0;
}

static unsigned heap_index_for_handle(Handle* handle)
{
    unsigned index;
    for (index = 0; index < 6; ++index)
        if (lbHeap_80431FA0.heap_array[index].handle == handle)
            return index;
    return 0;
}

void HSD_GetNextArena(void** lo, void** hi)
{
    *lo = (void*)fixture_arena_lo;
    *hi = (void*)fixture_arena_hi;
}

void lbMemory_800154BC(uintptr_t* lo, uintptr_t* hi)
{
    *lo = fixture_aram_lo;
    *hi = fixture_aram_hi;
}

int HSD_CreateMainHeap(void* lo, void* hi)
{
    record_call("replace_hsd_main", 0, -1, NULL,
                (uintptr_t)lo, (uintptr_t)hi);
    return next_heap_id++;
}

void OSDestroyHeap(int id)
{
    unsigned index;
    for (index = 0; index < 6; ++index)
        if (lbHeap_80431FA0.heap_array[index].id == id) break;
    record_call("destroy_os", index, id, NULL, 0, 0);
}

int OSCreateHeap(void* lo, void* hi)
{
    record_call("new_os", heap_index_for_span((uintptr_t)lo, (uintptr_t)hi),
                -1, NULL, (uintptr_t)lo, (uintptr_t)hi);
    return next_heap_id++;
}

static Handle* new_handle(const char* kind, uintptr_t lo, uintptr_t hi)
{
    Handle* handle;
    if (handle_count >= MAX_HANDLES) abort();
    handle = &handle_storage[handle_count++];
    memset(handle, 0, sizeof(*handle));
    record_call(kind, 0, -1, handle, lo, hi);
    return handle;
}

Handle* lbMemory_80014E24(void* lo, void* hi)
{
    Handle* handle = new_handle("new_handle", (uintptr_t)lo, (uintptr_t)hi);
    calls[call_count - 1].index = heap_index_for_span((uintptr_t)lo, (uintptr_t)hi);
    return handle;
}

void lbMemory_80014EEC(Handle* handle)
{
    record_call("destroy_handle", heap_index_for_handle(handle), -1, handle, 0, 0);
}

Handle* lbMemory_800154D4(void* lo, void* hi)
{
    Handle* handle = new_handle("new_current_handle", (uintptr_t)lo, (uintptr_t)hi);
    calls[call_count - 1].index = 1;
    return handle;
}

void lbMemory_800155A4(void)
{
    record_call("destroy_current_handle", 1, -1, NULL, 0, 0);
}

static unsigned parse_u32(const char* text)
{
    char* end = NULL;
    const unsigned long long value = strtoull(text, &end, 0);
    if (!end || *end || value > 0xffffffffULL) abort();
    return (unsigned)value;
}

static int handle_token(const Handle* handle)
{
    uintptr_t raw;
    if (!handle || (uintptr_t)handle == UINTPTR_MAX) return -1;
    raw = (uintptr_t)handle;
    return (int)((raw - (uintptr_t)handle_storage) / sizeof(Handle));
}

static unsigned address_offset(uintptr_t address, int aram)
{
    if (!address) return 0;
    return (unsigned)(address - (aram ? fixture_aram_lo : fixture_arena_lo));
}

static void emit_call(const struct Call* call)
{
    const int aram = !strcmp(call->kind, "new_current_handle") ||
                     (call->kind[0] == 'n' && call->index == 5);
    printf("{\"kind\":\"%s\",\"index\":%u", call->kind, call->index);
    if (!strcmp(call->kind, "destroy_os"))
        printf(",\"id\":%d", call->id);
    if (!strcmp(call->kind, "destroy_handle"))
        printf(",\"handle\":%d", handle_token(call->handle));
    if (!strcmp(call->kind, "replace_hsd_main") ||
        !strcmp(call->kind, "new_os") ||
        !strcmp(call->kind, "new_handle") ||
        !strcmp(call->kind, "new_current_handle")) {
        printf(",\"lo\":%u,\"hi\":%u", address_offset(call->lo, aram),
               address_offset(call->hi, aram));
    }
    if (!strcmp(call->kind, "new_current_handle") ||
        !strcmp(call->kind, "new_handle"))
        printf(",\"handle\":%d", handle_token(call->handle));
    fputs("}", stdout);
}

static void emit_heap(unsigned index, const struct Heap* heap)
{
    const int aram = index == 1 || heap->type == 4;
    printf("{\"index\":%u,\"id\":%d,\"handle\":%d,\"start\":%u,"
           "\"size\":%u,\"type\":%u,\"transient\":%d,\"status\":%d}",
           index, heap->id, handle_token(heap->handle),
           address_offset((uintptr_t)heap->start, aram), heap->size,
           (unsigned)heap->type, heap->transient, (int)heap->status);
}

static void emit_state(const char* op, const char* status)
{
    unsigned i;
    printf("{\"op\":\"%s\",\"status\":\"%s\",\"calls\":[", op,
           status);
    for (i = 0; i < call_count; ++i) {
        if (i) putchar(',');
        emit_call(&calls[i]);
    }
    printf("],\"heaps\":[");
    for (i = 0; i < 6; ++i) {
        if (i) putchar(',');
        emit_heap(i, &lbHeap_80431FA0.heap_array[i]);
    }
    printf("]}\n");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    char line[128];
    if (argc != 5) return 2;
    fixture_arena_lo = parse_u32(argv[1]);
    fixture_arena_hi = parse_u32(argv[2]);
    fixture_aram_lo = parse_u32(argv[3]);
    fixture_aram_hi = parse_u32(argv[4]);
    memset(&lbHeap_80431FA0, 0, sizeof(lbHeap_80431FA0));
    next_heap_id = 100;
    handle_count = 0;
    while (fgets(line, sizeof(line), stdin)) {
        char op = 0;
        unsigned index = 0;
        int value = 0;
        const int fields = sscanf(line, " %c %u %d", &op, &index, &value);
        call_count = 0;
        if (fields < 1 || op == '#') continue;
        if (op == 'b') {
            lbHeap_80015F3C();
            emit_state("boot", "ok");
        } else if (op == 't' && fields == 3) {
            if (index >= 6) emit_state("transient", "invalid_request");
            else {
                lbHeap_800158D0((int)index, value);
                emit_state("transient", "ok");
            }
        } else if (op == 'r' || op == 'q') {
            lbHeap_80015900();
            emit_state("rebuild", "ok");
        } else {
            emit_state("invalid", "invalid_request");
        }
    }
    return 0;
}
