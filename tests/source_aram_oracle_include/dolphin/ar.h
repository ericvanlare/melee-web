#ifndef MELEE_WEB_SOURCE_ARAM_AR_H
#define MELEE_WEB_SOURCE_ARAM_AR_H

#include <dolphin/types.h>

struct ARQRequest;
typedef void (*ARQCallback)(struct ARQRequest*);

u32 ARAlloc(u32 length);
u32 ARFree(u32* length);
int ARCheckInit(void);
u32 ARInit(u32* stack_index_addr, u32 num_entries);
void ARReset(void);
u32 ARGetSize(void);

#endif
