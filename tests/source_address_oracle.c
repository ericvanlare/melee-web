/*
 * Synthetic source-address oracle.  OSAlloc.c remains an untouched pinned
 * dependency and is compiled into this translation unit by source inclusion.
 * This executable reports offsets into its own fixed wasm32 arena, never host
 * addresses, so a replacement allocator can consume the same event stream.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdint.h>

/* Pin the original release layout; debug descriptor fields are absent. */
#undef DEBUG
#undef ENABLE_HEAPDESC
#include "../.deps/melee/extern/dolphin/src/dolphin/os/OSAlloc.c"

/* HSD_GetHeap is the selected-heap boundary. Skip unrelated video/initialization
 * declarations, while including the real HSD memory/object allocator bodies. */
#define _initialize_h_
static int HSD_GetHeap(void) { return __OSCurrHeap; }
#include "../.deps/melee/src/sysdolphin/baselib/memory.c"
#include "../.deps/melee/src/sysdolphin/baselib/objalloc.c"

_Static_assert(sizeof(void *) == 4, "source oracle requires the wasm32 ABI");
_Static_assert(sizeof(long) == 4, "source oracle requires 32-bit long");
_Static_assert(sizeof(struct Cell) == 12,
               "release source Cell layout changed");
_Static_assert(sizeof(struct HeapDesc) == 12,
               "release source HeapDesc layout changed");

#ifndef SOURCE_OSALLOC_SHA256
#define SOURCE_OSALLOC_SHA256 "unbound"
#endif

#define ARENA_BYTES 65536u
#define MAX_ALLOCS 128u
#define MAX_POOLS 8u

struct Allocation {
    void *address;
    unsigned requested;
    unsigned size;
    int active;
    int pool;
};

static unsigned char arena[ARENA_BYTES] __attribute__((aligned(32)));
static struct Allocation allocations[MAX_ALLOCS];
static int heap_id = -1;
static HSD_ObjAllocData pools[MAX_POOLS];
static int pool_ready[MAX_POOLS];

static unsigned arena_offset(const void *address) {
    return (unsigned)((const unsigned char *)address - arena);
}

static void print_json_string(const char *value) {
    /* All strings emitted by this program are controlled constants. */
    putchar('"');
    fputs(value, stdout);
    putchar('"');
}

static void emit_active(void) {
    unsigned id;
    int first = 1;

    fputs(",\"active\":[", stdout);
    for (id = 0; id < MAX_ALLOCS; ++id) {
        const struct Allocation *allocation = &allocations[id];
        if (!allocation->active) {
            continue;
        }
        if (!first) {
            putchar(',');
        }
        first = 0;
        printf("{\"id\":%u,\"offset\":%u,\"requested\":%u,\"size\":%u,\"pool\":%d}",
               id, arena_offset(allocation->address), allocation->requested,
               allocation->size, allocation->pool);
    }
    putchar(']');
}

static void emit_cells(const char *name, const struct Cell *cell) {
    int first = 1;

    fputs(",\"", stdout);
    fputs(name, stdout);
    fputs("\":[", stdout);
    for (; cell != NULL; cell = cell->next) {
        if (!first) {
            putchar(',');
        }
        first = 0;
        printf("{\"offset\":%u,\"bytes\":%u}", arena_offset(cell),
               (unsigned)cell->size);
    }
    putchar(']');
}

static void emit_pools(void) {
    unsigned i;
    int first = 1;
    fputs(",\"pools\":[", stdout);
    for (i = 0; i < MAX_POOLS; ++i) {
        HSD_ObjAllocLink *link;
        if (!pool_ready[i]) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"id\":%u,\"size\":%u,\"align_mask\":%u,\"used\":%u,"
               "\"free\":%u,\"peak\":%u,\"free_chain\":[", i,
               pools[i].size, pools[i].align, pools[i].used, pools[i].free, pools[i].peak);
        for (link = pools[i].freehead; link; link = link->next) {
            if (link != pools[i].freehead) putchar(',');
            printf("%u", arena_offset(link));
        }
        fputs("]}", stdout);
    }
    putchar(']');
}

static void emit_state(const char *operation, int id, const char *status,
                       unsigned result_offset, unsigned result_size) {
    long free_bytes = OSCheckHeap(heap_id);

    fputs("{\"record\":\"event\",\"op\":", stdout);
    print_json_string(operation);
    printf(",\"id\":%d,\"status\":", id);
    print_json_string(status);
    if (strcmp(status, "ok") == 0 && (operation[0] == 'a' || operation[0] == 'o')) {
        printf(",\"offset\":%u,\"size\":%u", result_offset,
               result_size);
    }
    if (free_bytes >= 0) {
        printf(",\"free_bytes\":%ld", free_bytes);
    } else {
        fputs(",\"free_bytes\":null,\"heap_check\":\"failed\"", stdout);
    }
    emit_active();
    emit_cells("allocated_cells", HeapArray[heap_id].allocated);
    emit_cells("free_cells", HeapArray[heap_id].free);
    emit_pools();
    puts("}");
    fflush(stdout);
}

static int parse_id(long value) {
    return value >= 0 && value < (long)MAX_ALLOCS ? (int)value : -1;
}

static void handle_allocate(int id, unsigned requested) {
    void *address;

    if (id < 0 || allocations[id].active || requested == 0 || requested > 0x7fffffc0U) {
        emit_state("a", id, "invalid", 0, 0);
        return;
    }
    address = OSAllocFromHeap(heap_id, requested);
    if (address == NULL) {
        emit_state("a", id, "oom", 0, 0);
        return;
    }
    allocations[id].address = address;
    allocations[id].requested = requested;
    allocations[id].size = (unsigned)OSReferentSize(address);
    allocations[id].active = 1;
    allocations[id].pool = -1;
    emit_state("a", id, "ok", arena_offset(address), allocations[id].size);
}

static void handle_free(int id) {
    if (id < 0 || !allocations[id].active || allocations[id].pool != -1) {
        emit_state("f", id, "invalid", 0, 0);
        return;
    }
    OSFreeToHeap(heap_id, allocations[id].address);
    memset(&allocations[id], 0, sizeof allocations[id]);
    emit_state("f", id, "ok", 0, 0);
}

static void handle_pool(char operation, int id, unsigned value, unsigned align) {
    char op[2] = {operation, 0};
    unsigned pool = (operation == 'p' || operation == 'r') ? (unsigned)id : value;
    if (pool >= MAX_POOLS || (operation != 'p' && !pool_ready[pool])) {
        emit_state(op, id, "invalid", 0, 0);
        return;
    }
    if (operation == 'p') {
        if (pool_ready[pool] || value < 4 || value > 1024 ||
            !align || align > 256 || (align & (align - 1))) {
            emit_state(op, id, "invalid", 0, 0);
            return;
        }
        HSD_ObjAllocInit(&pools[pool], value, align);
        pool_ready[pool] = 1;
    } else if (operation == 'r') {
        if (!value || value > 128) {
            emit_state(op, id, "invalid", 0, 0);
            return;
        }
        HSD_ObjAllocAddFree(&pools[pool], value);
    } else if (operation == 'o') {
        void *address;
        if (id < 0 || allocations[id].active) {
            emit_state(op, id, "invalid", 0, 0);
            return;
        }
        address = HSD_ObjAlloc(&pools[pool]);
        if (!address) { emit_state(op, id, "oom", 0, 0); return; }
        allocations[id] = (struct Allocation){address, pools[pool].size,
                                               pools[pool].size, 1, (int)pool};
        emit_state(op, id, "ok", arena_offset(address), pools[pool].size);
        return;
    } else {
        if (id < 0 || !allocations[id].active || allocations[id].pool != (int)pool) {
            emit_state(op, id, "invalid", 0, 0);
            return;
        }
        HSD_ObjFree(&pools[pool], allocations[id].address);
        memset(&allocations[id], 0, sizeof allocations[id]);
    }
    emit_state(op, id, "ok", 0, 0);
}

int main(void) {
    void *heap_start;
    char line[160];

    memset(allocations, 0, sizeof allocations);
    heap_start = OSInitAlloc(arena, arena + ARENA_BYTES, 1);
    if (heap_start == NULL) {
        fputs("{\"record\":\"fatal\",\"error\":\"init_alloc_failed\"}\n",
              stdout);
        return 2;
    }
    heap_id = OSCreateHeap(heap_start, arena + ARENA_BYTES);
    if (heap_id < 0) {
        fputs("{\"record\":\"fatal\",\"error\":\"create_heap_failed\"}\n",
              stdout);
        return 2;
    }
    OSSetCurrentHeap(heap_id);

    printf("{\"record\":\"header\",\"schema\":\"melee-source-address-oracle\","
           "\"version\":1,\"arena_bytes\":%u,\"heap\":%d,"
           "\"heap_start_offset\":%u,\"heap_end_offset\":%u,"
           "\"source_osalloc_sha256\":",
           ARENA_BYTES, heap_id, arena_offset(heap_start), ARENA_BYTES);
    print_json_string(SOURCE_OSALLOC_SHA256);
    puts(",\"object_allocator\":\"original_ordinary_os_backed\",\"synthetic\":true}");
    fflush(stdout);

    while (fgets(line, sizeof line, stdin) != NULL) {
        char operation;
        long raw_id;
        unsigned long raw_size, raw_align;
        int matched;

        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        matched = sscanf(line, " %c %ld %lu %lu", &operation, &raw_id, &raw_size, &raw_align);
        if (matched < 1) {
            continue;
        }
        if ((operation == 'p' && matched == 4) ||
            ((operation == 'o' || operation == 'q' || operation == 'r') && matched == 3)) {
            handle_pool(operation, parse_id(raw_id), (unsigned)raw_size,
                        operation == 'p' ? (unsigned)raw_align : 0);
        } else if (operation == 'a' && matched == 3 && raw_size <= 0xffffffffUL) {
            handle_allocate(parse_id(raw_id), (unsigned)raw_size);
        } else if (operation == 'f' && matched >= 2) {
            handle_free(parse_id(raw_id));
        } else {
            emit_state("?", -1, "invalid", 0, 0);
        }
    }
    return 0;
}
