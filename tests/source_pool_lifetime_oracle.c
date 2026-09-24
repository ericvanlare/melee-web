/* Original OSAlloc/HSD object-pool lifecycle oracle. The included source
 * files are pinned and untouched; all emitted pointers are arena offsets. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

#undef DEBUG
#undef ENABLE_HEAPDESC
#include "../.deps/melee/extern/dolphin/src/dolphin/os/OSAlloc.c"

#define _initialize_h_
static int HSD_GetHeap(void) { return __OSCurrHeap; }
#include "../.deps/melee/src/sysdolphin/baselib/memory.c"
#include "../.deps/melee/src/sysdolphin/baselib/objalloc.c"

#define ARENA_BYTES 0x10000u
static unsigned char arena[ARENA_BYTES] __attribute__((aligned(32)));
static HSD_ObjAllocData pool;
static int heaps[2];
static unsigned char *base;
static int current;
static unsigned offset(const void *address) { return (int)((const unsigned char *)address - base); }

static const char *status(int ok) { return ok ? "ok" : "oom"; }
static void emit(char op, const char *state, const void *result)
{
    printf("{\"op\":\"%c\",\"status\":\"%s\"", op, state);
    if (result) printf(",\"offset\":%u", (unsigned)offset(result));
    printf(",\"used\":%u,\"free\":%u,\"peak\":%u,\"free_chain\":[",
           pool.used, pool.free, pool.peak);
    HSD_ObjAllocLink *link = pool.freehead;
    int first = 1;
    while (link) {
        printf("%s%u", first ? "" : ",", (unsigned)offset(link));
        first = 0;
        link = link->next;
    }
    printf("],\"heap_free\":%ld}\n", OSCheckHeap(heaps[current]));
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    const unsigned supplied = (unsigned)strtoul(argv[1], NULL, 0);
    if (supplied > 0xffff0000u) return 2;
    base = arena;
    void *start = OSInitAlloc(arena, arena + ARENA_BYTES, 2);
    if (!start) return 2;
    heaps[0] = OSCreateHeap(start, arena + 0x5000);
    heaps[1] = OSCreateHeap(arena + 0x5000, arena + ARENA_BYTES);
    if (heaps[0] < 0 || heaps[1] < 0) return 2;
    current = 0;
    OSSetCurrentHeap(heaps[current]);
    printf("{\"op\":\"header\",\"base\":%u}\n", supplied);
    char op;
    unsigned value = 0, align = 0;
    while (scanf(" %c %u %u", &op, &value, &align) >= 1) {
        if (op == 's') {
            if (value >= 2) { emit(op, "invalid_context", NULL); continue; }
            current = (int)value;
            OSSetCurrentHeap(heaps[current]);
            emit(op, "ok", NULL);
        } else if (op == 'p' || op == 'R') {
            HSD_ObjAllocInit(&pool, value, align);
            emit(op, "ok", NULL);
        } else if (op == 'r') {
            const int count = HSD_ObjAllocAddFree(&pool, value);
            emit(op, status(count != 0), NULL);
        } else if (op == 'q') {
            const int count = HSD_ObjAllocAddFree(&pool, value);
            emit(op, status(count != 0), NULL);
        } else if (op == 'o') {
            void *result = HSD_ObjAlloc(&pool);
            emit(op, result ? "ok" : "oom", result);
        } else if (op == 'e') {
            void *result = pool.free ? HSD_ObjAlloc(&pool) : NULL;
            emit(op, result ? "ok" : "oom", result);
        } else if (op == 'c') {
            if (value >= 2) {
                emit(op, "invalid_context", NULL);
                continue;
            }
            OSDestroyHeap(heaps[value]);
            const void *lo = value ? arena + 0x5000 : arena + 0x20;
            const void *hi = value ? arena + 0x10000 : arena + 0x5000;
            heaps[value] = OSCreateHeap((void *)lo, (void *)hi);
            if (current == (int)value) OSSetCurrentHeap(heaps[current]);
            emit(op, "ok", NULL);
        } else {
            emit(op, "invalid", NULL);
        }
    }
    return 0;
}
