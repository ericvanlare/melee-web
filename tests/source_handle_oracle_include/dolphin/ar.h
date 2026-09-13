#ifndef MELEE_WEB_SOURCE_HANDLE_ORACLE_AR_H
#define MELEE_WEB_SOURCE_HANDLE_ORACLE_AR_H

#include <stdint.h>

typedef uint32_t u32;
u32 ARAlloc(u32 length);
u32 ARFree(u32* length);
u32 ARGetSize(void);

#endif
