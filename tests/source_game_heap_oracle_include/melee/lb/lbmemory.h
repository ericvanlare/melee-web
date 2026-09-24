#ifndef MELEE_LB_MEMORY_H
#define MELEE_LB_MEMORY_H

#include <Runtime/platform.h>

typedef struct Handle {
    struct Handle* x0_next;
    void* x4_lo;
    void* x8_hi;
    struct Handle* xC_prev;
} Handle;

Handle* lbMemory_80014E24(void* lo, void* hi);
void lbMemory_80014EEC(Handle* handle);
u32 lbMemory_80014F7C(Handle* handle);
Handle* lbMemory_80014FC8(Handle* handle, size_t size);
void lbMemFreeToHeap(Handle* handle, void* address);
u32 lbMemory_8001529C(Handle* handle, void (*callback)(u32), u32 heap);
void lbMemory_800154BC(uintptr_t* lo, uintptr_t* hi);
Handle* lbMemory_800154D4(void* lo, void* hi);
void lbMemory_800155A4(void);

#endif
