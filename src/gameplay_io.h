#ifndef MELEE_WEB_GAMEPLAY_IO_H
#define MELEE_WEB_GAMEPLAY_IO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebIo MeleeWebIo;
typedef enum MeleeWebIoKind {
    MELEE_WEB_IO_FILE=1, MELEE_WEB_IO_MEMORY=2, MELEE_WEB_IO_AUXILIARY=3
} MeleeWebIoKind;
typedef struct MeleeWebIoEndpoint { MeleeWebIoKind kind; uint64_t handle; size_t offset; } MeleeWebIoEndpoint;
typedef enum MeleeWebIoResult { MELEE_WEB_IO_COMPLETE=0, MELEE_WEB_IO_CANCELLED=1 } MeleeWebIoResult;
typedef void (*MeleeWebIoCallback)(uint64_t request,MeleeWebIoResult result,size_t transferred,void* user);
/* Single-threaded owned transport. Files are immutable copies of supplied local
 * bytes. Memory/auxiliary endpoints are explicit buffer identities, not CPU or
 * emulated addresses. No filesystem/network access or renderer/audio claims. */
MeleeWebIo* melee_web_io_create(char*,size_t);
int melee_web_io_add_file(MeleeWebIo*,const char* name,const void* bytes,size_t length,uint64_t* handle,char*,size_t);
int melee_web_io_find_file(MeleeWebIo*,const char* name,uint64_t* handle,size_t* length,char*,size_t);
/* Buffers are zero-initialized,32-byte aligned and owned until explicit removal.
 * Exposed bytes stay valid across registry additions and queued transfers. */
int melee_web_io_add_buffer(MeleeWebIo*,MeleeWebIoKind,size_t length,uint64_t* handle,char*,size_t);
int melee_web_io_buffer(MeleeWebIo*,uint64_t handle,void** bytes,size_t* length,char*,size_t);
/* Rejects removal while a queued request references the file/buffer. */
int melee_web_io_remove(MeleeWebIo*,uint64_t handle,char*,size_t);
/* Validates source/destination and complete ranges before queuing. Exact bytes
 * only: no guessed EOF padding or classification by pointer numerical value.
 * Completion runs after the real memmove on a later pump, never inline. */
int melee_web_io_submit(MeleeWebIo*,MeleeWebIoEndpoint source,MeleeWebIoEndpoint dest,size_t length,
                       MeleeWebIoCallback,void* user,uint64_t* request,char*,size_t);
int melee_web_io_cancel(MeleeWebIo*,uint64_t request,char*,size_t);
/* FIFO; at most max_requests from the queue present at pump entry. Callback
 * submissions wait for the next pump. Reentrant pumping/destruction rejects. */
int melee_web_io_pump(MeleeWebIo*,uint32_t max_requests,uint32_t* completed,char*,size_t);
size_t melee_web_io_pending(const MeleeWebIo*);
/* Refuses pending transfers; callers must pump completion/cancellation first. */
int melee_web_io_destroy(MeleeWebIo*,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
