#ifndef MELEE_WEB_SOURCE_AR_INIT_PROFILE_AR_H
#define MELEE_WEB_SOURCE_AR_INIT_PROFILE_AR_H

#include <stdint.h>
#include <string.h>

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;
typedef int BOOL;

#ifndef NULL
#define NULL ((void*)0)
#endif
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

struct ARQRequest;
/* ar.c's callback slot is a void(void) handler; source ar.h's C declaration
 * accepts the SDK's broader ARQ type because C permits the assignment. Keep
 * this oracle ABI exact for the untouched ar.c body when compiling as C++. */
typedef void (*ARQCallback)(void);

struct ARQRequest {
    struct ARQRequest* next;
    u32 owner;
    u32 type;
    u32 priority;
    u32 source;
    u32 dest;
    u32 length;
    ARQCallback callback;
};

#define ARAM_DIR_MRAM_TO_ARAM 0
#define ARAM_DIR_ARAM_TO_MRAM 1
#define ARQ_DMA_ALIGNMENT 32

typedef struct ARQRequest ARQRequest;

ARQCallback ARRegisterDMACallback(ARQCallback callback);
u32 ARGetDMAStatus(void);
void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length);
u32 ARAlloc(u32 length);
u32 ARFree(u32* length);
int ARCheckInit(void);
u32 ARInit(u32* stack_index_addr, u32 num_entries);
void ARReset(void);
void ARSetSize(void);
u32 ARGetBaseAddress(void);
u32 ARGetSize(void);

#endif
