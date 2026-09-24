#ifndef MELEE_WEB_SOURCE_ARQ_AR_H
#define MELEE_WEB_SOURCE_ARQ_AR_H

#include <dolphin/types.h>

struct ARQRequest;
typedef struct ARQRequest ARQRequest;
typedef void (*ARQCallback)(struct ARQRequest*);
/* __ar.h declares the source DMA interrupt as void(void); keep it distinct
 * from request callbacks so the checked-Wasm32 function table is type-safe. */
typedef void (*ARQDMACallback)(void);

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

#define ARQ_DMA_ALIGNMENT 32
#define ARAM_DIR_MRAM_TO_ARAM 0
#define ARAM_DIR_ARAM_TO_MRAM 1
#define ARQ_TYPE_MRAM_TO_ARAM ARAM_DIR_MRAM_TO_ARAM
#define ARQ_TYPE_ARAM_TO_MRAM ARAM_DIR_ARAM_TO_MRAM
#define ARQ_PRIORITY_LOW 0
#define ARQ_PRIORITY_HIGH 1

ARQDMACallback ARRegisterDMACallback(ARQDMACallback callback);
u32 ARGetDMAStatus(void);
u32 ARAlloc(u32 length);
void ARStartDMA(u32 type, u32 mainmem_addr, u32 aram_addr, u32 length);
void ARQInit(void);
void ARQReset(void);
void ARQPostRequest(struct ARQRequest* request, u32 owner, u32 type,
                   u32 priority, u32 source, u32 dest, u32 length,
                   ARQCallback callback);
void ARQRemoveRequest(struct ARQRequest* request);
void ARQRemoveOwnerRequest(u32 owner);
void ARQFlushQueue(void);
void ARQSetChunkSize(u32 size);
u32 ARQGetChunkSize(void);

#endif
