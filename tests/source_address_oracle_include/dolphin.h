#ifndef MELEE_SOURCE_ADDRESS_ORACLE_DOLPHIN_H
#define MELEE_SOURCE_ADDRESS_ORACLE_DOLPHIN_H

/*
 * Deliberately small test adapter for the untouched Dolphin OS allocator.
 * The oracle is compiled as wasm32, so these aliases retain the source's
 * 32-bit ABI without depending on the full Dolphin SDK include graph.
 */
#include <stddef.h>
#include <stdint.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int BOOL;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
