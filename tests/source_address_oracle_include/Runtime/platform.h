#ifndef MELEE_SOURCE_ADDRESS_ORACLE_PLATFORM_H
#define MELEE_SOURCE_ADDRESS_ORACLE_PLATFORM_H
#include <dolphin.h>
#include <sys/types.h>
#include <stdlib.h>
#define ASSERT_SIZE(type, size) _Static_assert(sizeof(type) == (size), "source type layout")
/* HSD assertions remain fatal. Only unrelated reporting/header dependencies
 * are adapted; the included allocator and memory routine bodies are untouched. */
#define SYSDOLPHIN_BASELIB_DEBUG_H
#define HSD_ASSERT(line, condition) do { if (!(condition)) abort(); } while (0)
#endif
