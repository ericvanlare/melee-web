/*
 * Differential driver for the untouched original lbmemory.c. The source body
 * is included directly; this file only supplies test shims for ARAM, alarms,
 * device-copy declarations and reporting. Source identities are normalized to
 * offsets from the original allocator global and ARAM roots before emission.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t oracle_aram_lo;
static uint32_t oracle_aram_hi;

#include "../.deps/melee/src/melee/lb/lbmemory.c"

uint32_t ARAlloc(uint32_t length)
{
    (void)length;
    return oracle_aram_lo;
}

uint32_t ARFree(uint32_t* length)
{
    if (length) *length = 0;
    return 0;
}

uint32_t ARGetSize(void) { return oracle_aram_hi; }
static void unexpected_async_source_path(void)
{
    fputs("unexpected lbMemory asynchronous path\n", stderr);
    abort();
}
void OSCreateAlarm(OSAlarm* alarm)
{
    (void)alarm;
    unexpected_async_source_path();
}
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void)alarm;
    (void)tick;
    (void)handler;
    unexpected_async_source_path();
}
int OSDisableInterrupts(void)
{
    unexpected_async_source_path();
    return 0;
}
void OSRestoreInterrupts(int enabled)
{
    (void)enabled;
    unexpected_async_source_path();
}
int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size,
                      int type, int priority, HSD_DevComCallback callback, void* args)
{
    (void)file;
    (void)src;
    (void)dest;
    (void)size;
    (void)type;
    (void)priority;
    (void)callback;
    (void)args;
    unexpected_async_source_path();
    return 0;
}

struct Label {
    char name[64];
    Handle* handle;
    void* payload;
};

static struct Label labels[256];
static unsigned label_count;
static Handle* all_handles[256];
static unsigned all_handle_count;

static uint32_t global_base(void) { return (uint32_t)(uintptr_t)&lbMemory_804318B0; }
static uint32_t handle_offset(const Handle* handle)
{
    return handle ? (uint32_t)(uintptr_t)handle - global_base() : 0;
}
static uint32_t arena_offset(const void* address)
{
    return address ? (uint32_t)(uintptr_t)address - oracle_aram_lo : 0;
}
static int is_heap_handle(const Handle* handle)
{
    const uintptr_t first = (uintptr_t)&lbMemory_804318B0.x638_heap[0];
    const uintptr_t value = (uintptr_t)handle;
    return value >= first && value < first + 6 * sizeof(Handle) &&
           ((value - first) % sizeof(Handle)) == 0;
}
static struct Label* label(const char* name)
{
    unsigned i;
    for (i = 0; i < label_count; ++i) {
        if (strcmp(labels[i].name, name) == 0) return &labels[i];
    }
    if (label_count >= sizeof(labels) / sizeof(labels[0])) return NULL;
    strncpy(labels[label_count].name, name, sizeof(labels[0].name) - 1);
    labels[label_count].name[sizeof(labels[0].name) - 1] = '\0';
    labels[label_count].handle = NULL;
    labels[label_count].payload = NULL;
    return &labels[label_count++];
}
static void remember_handle(Handle* handle)
{
    unsigned i;
    for (i = 0; i < all_handle_count; ++i) {
        if (all_handles[i] == handle) return;
    }
    if (all_handle_count < sizeof(all_handles) / sizeof(all_handles[0]))
        all_handles[all_handle_count++] = handle;
}
static int in_free_mem(const Handle* wanted)
{
    const Handle* iter = lbMemory_804318B0.free_mem;
    unsigned guard = 0;
    while (iter && guard++ < 0x83) {
        if (iter == wanted) return 1;
        iter = iter->x0_next;
    }
    return 0;
}
static int in_free_heap(const Handle* wanted)
{
    const Handle* iter = lbMemory_804318B0.free_heap;
    unsigned guard = 0;
    while (iter && guard++ < 6) {
        if (iter == wanted) return 1;
        iter = iter->x0_next;
    }
    return 0;
}
static int active_handle(const Handle* handle)
{
    return handle && !in_free_mem(handle) && !in_free_heap(handle);
}
static int compare_handles(const void* lhs, const void* rhs)
{
    const Handle* const* a = (const Handle* const*)lhs;
    const Handle* const* b = (const Handle* const*)rhs;
    const uint32_t left = handle_offset(*a), right = handle_offset(*b);
    return left < right ? -1 : left != right;
}
static void emit_chain(const char* key, Handle* head, unsigned limit)
{
    unsigned i = 0;
    printf(",\"%s\":[", key);
    while (head && i++ < limit) {
        printf("%s%u", i == 1 ? "" : ",", handle_offset(head));
        head = head->x0_next;
    }
    putchar(']');
}
static void emit_state(const char* op, const char* status,
                       Handle* result, void* payload, uint32_t size)
{
    Handle* active[256];
    unsigned active_count = 0, i;
    printf("{\"op\":\"%s\",\"status\":\"%s\"", op, status);
    if (!strcmp(status, "ok") && result) {
        printf(",\"handle\":%u,\"payload\":%u,\"size\":%u",
               handle_offset(result), arena_offset(payload), size);
    }
    for (i = 0; i < all_handle_count; ++i) {
        if (active_handle(all_handles[i])) active[active_count++] = all_handles[i];
    }
    qsort(active, active_count, sizeof(active[0]), compare_handles);
    printf(",\"initialized\":%s,\"current\":%u,\"allocations\":%d,\"max_allocations\":%d",
           lbMemory_804318B0.free_heap || lbMemory_804318B0.free_mem ? "true" : "false",
           handle_offset(lbMemory_804318B0.x69C), lbMemory_804318B0.x630_num_allocs,
           lbMemory_804318B0.x634_max_num_allocs);
    emit_chain("free_mem", lbMemory_804318B0.free_mem, 0x83);
    emit_chain("free_heap", lbMemory_804318B0.free_heap, 6);
    printf(",\"active\":[");
    for (i = 0; i < active_count; ++i) {
        Handle* handle = active[i];
        const int heap = is_heap_handle(handle);
        printf("%s{\"identity\":%u,\"next\":%u,\"lo\":%u,\"hi\":%u,\"heap\":%s",
               i ? "," : "", handle_offset(handle), handle_offset(handle->x0_next),
               arena_offset(handle->x4_lo),
               heap ? arena_offset(handle->x8_hi) : (uint32_t)(uintptr_t)handle->x8_hi,
               heap ? "true" : "false");
        if (heap) printf(",\"prev\":%u", handle_offset(handle->xC_prev));
        putchar('}');
    }
    puts("]}");
    fflush(stdout);
}
static int contiguous(Handle* handle)
{
    uintptr_t expected;
    Handle* iter;
    if (!handle) return 0;
    expected = (uintptr_t)handle->x4_lo;
    for (iter = handle->xC_prev; iter; iter = iter->x0_next) {
        if ((uintptr_t)iter->x4_lo != expected) return 0;
        expected += (uintptr_t)iter->x8_hi;
    }
    return 1;
}
static unsigned parse_u32(const char* text)
{
    char* end = NULL;
    const unsigned long long value = strtoull(text, &end, 0);
    if (!end || *end || value > 0xffffffffULL) abort();
    return (unsigned)value;
}
static void clear_labels(void)
{
    label_count = 0;
    all_handle_count = 0;
    memset(labels, 0, sizeof(labels));
    memset(all_handles, 0, sizeof(all_handles));
}

int main(int argc, char** argv)
{
    char line[256];
    if (argc != 3) return 2;
    oracle_aram_lo = parse_u32(argv[1]);
    oracle_aram_hi = parse_u32(argv[2]);
    clear_labels();
    while (fgets(line, sizeof line, stdin)) {
        char op = 0, first[64] = {}, second[64] = {}, third[64] = {};
        const int count = sscanf(line, " %c %63s %63s %63s", &op, first, second, third);
        struct Label* owner;
        struct Label* child;
        Handle* result;
        if (count < 1 || op == '#') continue;
        if (op == 'b') {
            lbMemory_8001564C();
            clear_labels();
            label("current")->handle = lbMemory_804318B0.x69C;
            remember_handle(lbMemory_804318B0.x69C);
            emit_state("boot", "ok", NULL, NULL, 0);
        } else if (op == 'n' || op == 'c') {
            if (count != 4) return 2;
            result = op == 'n'
                ? lbMemory_80014E24((void*)(uintptr_t)parse_u32(second),
                                    (void*)(uintptr_t)parse_u32(third))
                : lbMemory_800154D4((void*)(uintptr_t)parse_u32(second),
                                     (void*)(uintptr_t)parse_u32(third));
            owner = label(first);
            if (!owner) return 2;
            owner->handle = result;
            remember_handle(result);
            if (op == 'c') label("current")->handle = result;
            emit_state(op == 'n' ? "new" : "current_new", "ok", result, NULL, 0);
        } else if (op == 'a') {
            if (count != 4) return 2;
            owner = label(first);
            if (!owner || !owner->handle) return 2;
            result = lbMemory_80014FC8(owner->handle, (size_t)parse_u32(third));
            child = label(second);
            if (!child) return 2;
            child->handle = result;
            child->payload = result->x4_lo;
            remember_handle(result);
            emit_state("alloc", "ok", result, result->x4_lo, (uint32_t)(uintptr_t)result->x8_hi);
        } else if (op == 'f') {
            if (count != 3) return 2;
            owner = label(first);
            child = label(second);
            if (!owner || !child || !owner->handle || !child->payload) return 2;
            lbMemFreeToHeap(owner->handle, child->payload);
            emit_state("free", "ok", NULL, NULL, 0);
        } else if (op == 'd') {
            owner = label(first);
            if (!owner || !owner->handle) return 2;
            lbMemory_80014EEC(owner->handle);
            emit_state("destroy", "ok", NULL, NULL, 0);
        } else if (op == 's') {
            lbMemory_800155A4();
            label("current")->handle = NULL;
            emit_state("destroy_current", "ok", NULL, NULL, 0);
        } else if (op == 'm' || op == 'k') {
            owner = op == 'k' ? label("current") : label(first);
            if (!owner || !owner->handle) return 2;
            if (!contiguous(owner->handle)) {
                emit_state(op == 'k' ? "compact_current" : "compact",
                           "async_move_required", NULL, NULL, 0);
                continue;
            }
            const uint32_t moved = lbMemory_8001529C(owner->handle, NULL, 0);
            emit_state(op == 'k' ? "compact_current" : "compact",
                       moved ? "async_move_required" : "ok", NULL, NULL, 0);
        } else {
            return 2;
        }
    }
    return 0;
}
