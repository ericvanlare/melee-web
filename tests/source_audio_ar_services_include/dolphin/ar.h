#ifndef MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_AR_H
#define MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_AR_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ARQRequest;

/* ar.c calls its DMA callback with no argument.  Keep that ABI separate from
 * the request callback used by arq.c; the upstream header conflates them. */
typedef void (*ARDMACallback)(void);
typedef void (*ARQCallback)(struct ARQRequest*);

struct ARQRequest
{
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
#define ARAM_DIR_MRAM_TO_ARAM 0x00
#define ARAM_DIR_ARAM_TO_MRAM 0x01
#define ARQ_TYPE_MRAM_TO_ARAM ARAM_DIR_MRAM_TO_ARAM
#define ARQ_TYPE_ARAM_TO_MRAM ARAM_DIR_ARAM_TO_MRAM
#define ARQ_PRIORITY_LOW 0
#define ARQ_PRIORITY_HIGH 1

ARDMACallback ARRegisterDMACallback(ARDMACallback callback);
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

#ifdef __cplusplus
}
#endif

#endif
