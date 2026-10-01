#ifndef MELEE_WEB_GAMEPLAY_THP_COMPAT_H
#define MELEE_WEB_GAMEPLAY_THP_COMPAT_H

#include <dolphin/types.h>
#include <stdint.h>
#include <string.h>
#include "gameplay_platform.h"
#include "gameplay_thp_cpu.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Translate the retail decoder's cache-line zero instruction to its exact
 * 32-byte memory effect on coherent Wasm memory. */
static inline void melee_web_thp_zero_cache_line(void* address, u32 offset)
{
    uintptr_t line = ((uintptr_t) address + offset) & ~(uintptr_t) 31;
    memset((void*) line, 0, 32);
}

/* PPC cntlzw returns 32 for zero; the C builtin is undefined for zero. */
static inline u32 melee_web_thp_count_leading_zeros(u32 value)
{
    return value == 0 ? 32 : (u32) __builtin_clz(value);
}

#define __dcbz(address, offset) melee_web_thp_zero_cache_line((address), (offset))
#define __cntlzw(value) melee_web_thp_count_leading_zeros((u32) (value))

#if defined(TARGET_PC)
static inline void* melee_web_thp_cpu_cache_base(void)
{
    void* base = melee_web_thp_cpu_scratch();
    if (!base)
        melee_web_platform_unavailable("unowned THP CPU cache initialization");
    return base;
}
#define MELEE_WEB_THP_CPU_CACHE_BASE() ((u8*) melee_web_thp_cpu_cache_base())
#define DCZeroRange(destination, size) \
    melee_web_thp_cpu_zero((destination), (u32) (size))
#define LCStoreData(destination, source, size) \
    melee_web_thp_cpu_store((destination), (source), (u32) (size))
#define LCQueueWait(length) melee_web_thp_cpu_wait((u32) (length))
#endif

#endif
