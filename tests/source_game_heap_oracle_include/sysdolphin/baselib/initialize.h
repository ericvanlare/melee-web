#ifndef MELEE_WEB_SOURCE_GAME_HEAP_ORACLE_INITIALIZE_H
#define MELEE_WEB_SOURCE_GAME_HEAP_ORACLE_INITIALIZE_H

#include <Runtime/platform.h>

void HSD_GetNextArena(void** lo, void** hi);
int HSD_CreateMainHeap(void* lo, void* hi);
int OSCreateHeap(void* lo, void* hi);
void OSDestroyHeap(int id);
int HSD_GetHeap(void);
void HSD_SetHeap(int heap);
void* HSD_MemAlloc(size_t size);
void HSD_Free(void* address);
int OSDisableInterrupts(void);
void OSRestoreInterrupts(int state);
int OSCheckHeap(int heap);

#endif
