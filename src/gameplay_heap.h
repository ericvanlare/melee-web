#ifndef MELEE_WEB_GAMEPLAY_HEAP_H
#define MELEE_WEB_GAMEPLAY_HEAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True only when the SDK allocator has no arena metadata, selected heap or
 * wrapper owner. An initialized allocator with no selected heap is occupied. */
int melee_web_gameplay_heap_available(void);

/* Check the claimed arena identity without accessing heap descriptors. Call
 * before operating on stored SDK heap handles; another arena may reuse them. */
int melee_web_gameplay_heap_owns(const void* expected_arena);

/* Claim caller-owned memory for the isolated runtime and call original
 * OSInitAlloc with one heap slot. Returns the usable start for OSCreateHeap;
 * accepts 128 bytes through 64 MiB and does not select/create a heap or transfer
 * malloc/free ownership. On failure
 * the caller still owns its memory and no claim was established. */
void* melee_web_gameplay_heap_initialize(void* arena, size_t bytes,
                                        char* error, size_t error_size);

/* Release only the exact claimed arena, after every heap was destroyed.
 * Clears SDK metadata before the caller frees arena. Failure retains ownership
 * and the caller must retain the memory. Direct SDK reinitialization violates
 * exclusive ownership; changed arena identity is detected before release. */
int melee_web_gameplay_heap_release(void* expected_arena,
                                   char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
