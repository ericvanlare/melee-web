#include "hsd_probe_compat.h"
#include "hsd_host_support.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/debug.h>
#include <stdio.h>
#include <stdlib.h>

/* HSD's piece allocator groups sizes in 32-byte buckets. The host adapter
 * preserves that alignment/size contract with real owned allocations rather
 * than retaining GameCube-specific heap addresses or emulating a free list. */
typedef struct PieceHeader {
    size_t size;
    unsigned int magic;
    unsigned char padding[32 - sizeof(size_t) - sizeof(unsigned int)];
} PieceHeader;
_Static_assert(sizeof(PieceHeader) == 32, "HSD piece header alignment");
static size_t allocated_bytes;
static const size_t allocation_budget = 64U * 1024U * 1024U;
static const unsigned int piece_magic = 0x48534450;

size_t melee_web_hsd_allocation_bytes(void) { return allocated_bytes; }

void* hsdAllocMemPiece(s32 size)
{
    if (size <= 0) return NULL;
    const size_t rounded = ((size_t) size + 31U) & ~(size_t) 31U;
    const size_t total = rounded + sizeof(PieceHeader);
    if (total > allocation_budget - allocated_bytes) return NULL;
    PieceHeader* header = aligned_alloc(32, total);
    if (header == NULL) return NULL;
    header->size = rounded;
    header->magic = piece_magic;
    allocated_bytes += total;
    return header + 1;
}

void hsdFreeMemPiece(void* memory, s32 size)
{
    if (memory == NULL) return;
    PieceHeader* header = (PieceHeader*) memory - 1;
    if (size <= 0 || header->magic != piece_magic ||
        header->size != (((size_t) size + 31U) & ~(size_t) 31U)) {
        fputs("HSD host allocator: invalid allocation or size bucket\n", stderr);
        abort();
    }
    allocated_bytes -= header->size + sizeof(PieceHeader);
    header->magic = 0;
    free(header);
}

/* Keep the existing inspection/test translation-unit interface. */
#include "hsd_debug.c"
