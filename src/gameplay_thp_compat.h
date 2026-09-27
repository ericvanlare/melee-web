#ifndef MELEE_WEB_GAMEPLAY_THP_COMPAT_H
#define MELEE_WEB_GAMEPLAY_THP_COMPAT_H

#include <dolphin/types.h>
#include <stdint.h>
#include <string.h>

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

#endif
