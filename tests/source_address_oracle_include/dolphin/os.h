#ifndef MELEE_SOURCE_ADDRESS_ORACLE_DOLPHIN_OS_H
#define MELEE_SOURCE_ADDRESS_ORACLE_DOLPHIN_OS_H

#include <dolphin.h>
#include <stdarg.h>

/* The allocator source uses these only in DEBUG diagnostics. */
#define ASSERTLINE(line, expression) ((void)0)
#define ASSERTMSGLINE(line, expression, message) ((void)0)
#define ASSERTMSG(expression, message) ((void)0)

#define OFFSET(value, alignment) \
    ((u32)((uintptr_t)(value)) & ((u32)(alignment) - 1u))

/* OSDumpHeap is retained by the source but is outside this oracle's API. */
static inline void OSReport(char *format, ...) {
    (void)format;
}

#endif
