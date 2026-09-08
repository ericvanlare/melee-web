/* Shared fail-fast diagnostics; native classes use the original class allocator. */
#include "hsd_probe_compat.h"
#include <sysdolphin/baselib/debug.h>
#include <stdio.h>
#include <stdlib.h>

void HSD_Panic(char* file, u32 line, char* reason)
{
    fprintf(stderr, "HSD panic at %s:%u: %s\n", file, line, reason);
    abort();
}

void __assert(char* file, u32 line, char* condition)
{
    fprintf(stderr, "HSD assertion at %s:%u: %s\n", file, line, condition);
    abort();
}
