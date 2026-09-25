#ifndef MELEE_WEB_SOURCE_HANDLE_ORACLE_PLATFORM_H
#define MELEE_WEB_SOURCE_HANDLE_ORACLE_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef int BOOL;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define ASSERT_SIZE(type, size) _Static_assert(sizeof(type) == (size), "source layout")
#define SYSDOLPHIN_BASELIB_DEBUG_H
#define HSD_ASSERT(line, condition) do { if (!(condition)) abort(); } while (0)

#endif
