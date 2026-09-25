/*
 * Async differential oracle for the untouched original lbmemory.c.
 *
 * The DevCom shim is only a queued completion service. It does not replace
 * source behavior: lbMemory_80015320 still publishes handle placement and
 * chooses HSD_DevComRequest from the original branch. The test explicitly
 * invokes the queued callback to compare the next source boundary.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../.deps/melee/src/melee/lb/lbmemory.c"

static uint32_t oracle_aram_lo;
static uint32_t oracle_aram_hi;
static HSD_DevComCallback oracle_pending_callback;
static void* oracle_pending_args;
static uintptr_t oracle_pending_src;
static uintptr_t oracle_pending_dest;
static size_t oracle_pending_size;
static int oracle_pending_type;
static int oracle_pending_request;
static unsigned oracle_completion_count;

uint32_t ARAlloc(uint32_t length)
{
    (void) length;
    return oracle_aram_lo;
}

uint32_t ARFree(uint32_t* length)
{
    if (length) *length = 0;
    return 0;
}

uint32_t ARGetSize(void) { return oracle_aram_hi; }

static void unsupported_async_path(void)
{
    fputs("unsupported RAM-alarm/interrupt path in low-DevCom oracle\n", stderr);
    abort();
}

void OSCreateAlarm(OSAlarm* alarm)
{
    (void) alarm;
    unsupported_async_path();
}

void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler)
{
    (void) alarm;
    (void) tick;
    (void) handler;
    unsupported_async_path();
}

int OSDisableInterrupts(void)
{
    unsupported_async_path();
    return 0;
}

void OSRestoreInterrupts(int enabled)
{
    (void) enabled;
    unsupported_async_path();
}

int HSD_DevComRequest(int file, uintptr_t src, uintptr_t dest, size_t size,
                      int type, int priority, HSD_DevComCallback callback,
                      void* args)
{
    (void) file;
    (void) src;
    (void) dest;
    (void) size;
    (void) type;
    (void) priority;
    if (oracle_pending_callback != NULL) {
        fputs("nested DevCom request before completion\n", stderr);
        abort();
    }
    oracle_pending_callback = callback;
    oracle_pending_args = args;
    oracle_pending_src = src;
    oracle_pending_dest = dest;
    oracle_pending_size = size;
    oracle_pending_type = type;
    oracle_pending_request += 4;
    return oracle_pending_request;
}

struct Label {
    char name[64];
    Handle* handle;
    void* payload;
};

static struct Label labels[64];
static unsigned label_count;
static Handle* all_handles[256];
static unsigned all_handle_count;

static uint32_t global_base(void)
{
    return (uint32_t) (uintptr_t) &lbMemory_804318B0;
}

static uint32_t handle_offset(const Handle* handle)
{
    return handle ? (uint32_t) (uintptr_t) handle - global_base() : 0;
}

static uint32_t arena_offset(const void* address)
{
    return address ? (uint32_t) (uintptr_t) address - oracle_aram_lo : 0;
}

static int is_heap_handle(const Handle* handle)
{
    const uintptr_t first = (uintptr_t) &lbMemory_804318B0.x638_heap[0];
    const uintptr_t value = (uintptr_t) handle;
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

static int in_free(const Handle* wanted, const Handle* head, unsigned bound)
{
    unsigned count = 0;
    while (head && count++ < bound) {
        if (head == wanted) return 1;
        head = head->x0_next;
    }
    return 0;
}

static int active_handle(const Handle* handle)
{
    return handle &&
           !in_free(handle, lbMemory_804318B0.free_mem, 0x83) &&
           !in_free(handle, lbMemory_804318B0.free_heap, 6);
}

static int compare_handles(const void* lhs, const void* rhs)
{
    const Handle* const* a = (const Handle* const*) lhs;
    const Handle* const* b = (const Handle* const*) rhs;
    const uint32_t left = handle_offset(*a), right = handle_offset(*b);
    return left < right ? -1 : left != right;
}

static void emit_chain(const char* key, Handle* head, unsigned limit)
{
    unsigned count = 0;
    printf(",\"%s\":[", key);
    while (head && count++ < limit) {
        printf("%s%u", count == 1 ? "" : ",", handle_offset(head));
        head = head->x0_next;
    }
    putchar(']');
}

static void emit_state(const char* op, const char* status)
{
    Handle* active[256];
    unsigned active_count = 0;
    unsigned i;

    printf("{\"op\":\"%s\",\"status\":\"%s\",\"current\":%u",
           op, status, handle_offset(lbMemory_804318B0.x69C));
    printf(",\"allocations\":%d,\"max_allocations\":%d",
           lbMemory_804318B0.x630_num_allocs,
           lbMemory_804318B0.x634_max_num_allocs);
    emit_chain("free_mem", lbMemory_804318B0.free_mem, 0x83);
    emit_chain("free_heap", lbMemory_804318B0.free_heap, 6);
    for (i = 0; i < all_handle_count; ++i) {
        if (active_handle(all_handles[i])) active[active_count++] = all_handles[i];
    }
    qsort(active, active_count, sizeof(active[0]), compare_handles);
    printf(",\"active\":[");
    for (i = 0; i < active_count; ++i) {
        Handle* handle = active[i];
        const int heap = is_heap_handle(handle);
        printf("%s{\"identity\":%u,\"next\":%u,\"lo\":%u,\"hi\":%u,\"heap\":%s",
               i ? "," : "", handle_offset(handle),
               handle_offset(handle->x0_next), arena_offset(handle->x4_lo),
               heap ? arena_offset(handle->x8_hi) : (uint32_t) (uintptr_t) handle->x8_hi,
               heap ? "true" : "false");
        if (heap) printf(",\"prev\":%u", handle_offset(handle->xC_prev));
        putchar('}');
    }
    printf("]");
    printf(",\"cursor\":%u,\"pending\":",
           arena_offset(lbMemory_804318B0.x6E4));
    if (oracle_pending_callback) {
        printf("{\"kind\":\"devcom_1b\",\"source\":%u,\"destination\":%u,\"size\":%u,\"type\":%d}",
               arena_offset((void*) oracle_pending_src),
               arena_offset((void*) oracle_pending_dest),
               (unsigned) oracle_pending_size, oracle_pending_type);
    } else {
        fputs("null", stdout);
    }
    printf(",\"completion_count\":%u}\n", oracle_completion_count);
    fflush(stdout);
}

static unsigned parse_u32(const char* text)
{
    char* end = NULL;
    const unsigned long long value = strtoull(text, &end, 0);
    if (!end || *end || value > 0xffffffffULL) abort();
    return (unsigned) value;
}

static void oracle_complete(u32 unused)
{
    (void) unused;
    ++oracle_completion_count;
}

static void reset_labels(void)
{
    label_count = 0;
    all_handle_count = 0;
    memset(labels, 0, sizeof(labels));
    memset(all_handles, 0, sizeof(all_handles));
    oracle_pending_callback = NULL;
    oracle_pending_args = NULL;
    oracle_pending_src = 0;
    oracle_pending_dest = 0;
    oracle_pending_size = 0;
    oracle_pending_type = 0;
    oracle_pending_request = 0;
    oracle_completion_count = 0;
}

int main(int argc, char** argv)
{
    char line[256];
    if (argc != 3) return 2;
    oracle_aram_lo = parse_u32(argv[1]);
    oracle_aram_hi = parse_u32(argv[2]);
    reset_labels();
    while (fgets(line, sizeof(line), stdin)) {
        char op = 0, first[64] = {}, second[64] = {}, third[64] = {};
        const int count = sscanf(line, " %c %63s %63s %63s",
                                 &op, first, second, third);
        struct Label* owner;
        struct Label* child;
        Handle* result;
        if (count < 1 || op == '#') continue;
        if (op == 'b') {
            lbMemory_8001564C();
            reset_labels();
            label("current")->handle = lbMemory_804318B0.x69C;
            remember_handle(lbMemory_804318B0.x69C);
            emit_state("boot", "ok");
        } else if (op == 'n') {
            if (count != 4) return 2;
            result = lbMemory_80014E24((void*) (uintptr_t) parse_u32(second),
                                       (void*) (uintptr_t) parse_u32(third));
            owner = label(first);
            if (!owner) return 2;
            owner->handle = result;
            remember_handle(result);
            emit_state("new", "ok");
        } else if (op == 'a') {
            if (count != 4) return 2;
            owner = label(first);
            child = label(second);
            if (!owner || !child || !owner->handle) return 2;
            result = lbMemory_80014FC8(owner->handle, parse_u32(third));
            child->handle = result;
            child->payload = result->x4_lo;
            remember_handle(result);
            emit_state("alloc", "ok");
        } else if (op == 'f') {
            if (count != 3) return 2;
            owner = label(first);
            child = label(second);
            if (!owner || !child || !owner->handle || !child->payload) return 2;
            lbMemFreeToHeap(owner->handle, child->payload);
            emit_state("free", "ok");
        } else if (op == 'm') {
            if (count != 2) return 2;
            owner = label(first);
            if (!owner || !owner->handle) return 2;
            const uint32_t moved = lbMemory_8001529C(owner->handle,
                                                       oracle_complete, 4);
            emit_state("compact", moved ? "started" : "ok");
        } else if (op == 'c') {
            if (!oracle_pending_callback) {
                emit_state("complete", "invalid_transition");
                continue;
            }
            HSD_DevComCallback callback = oracle_pending_callback;
            void* args = oracle_pending_args;
            const int request = oracle_pending_request;
            oracle_pending_callback = NULL;
            oracle_pending_args = NULL;
            oracle_pending_src = 0;
            oracle_pending_dest = 0;
            oracle_pending_size = 0;
            oracle_pending_type = 0;
            callback(request, (int) (uintptr_t) args, NULL, false);
            emit_state("complete", "ok");
        } else if (op == 's') {
            emit_state("state", "ok");
        } else {
            return 2;
        }
    }
    return 0;
}
