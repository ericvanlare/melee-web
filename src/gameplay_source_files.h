#ifndef MELEE_WEB_GAMEPLAY_SOURCE_FILES_H
#define MELEE_WEB_GAMEPLAY_SOURCE_FILES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebSourceFileInput {
    const char* name;
    const uint8_t* bytes;
    size_t size;
} MeleeWebSourceFileInput;

typedef struct MeleeWebSourceFileScope MeleeWebSourceFileScope;

/* DVD/DevCom support for original source assets that need byte reads at run
 * time (for example an authored THP movie). Entries are scoped to the same
 * immutable RuntimeFiles owner as archive loading. */
#define MELEE_WEB_SOURCE_FILE_ENTRY_BASE 0x40000000
typedef void (*MeleeWebSourceFileCallback)(int request, int args,
                                           void* buffer, int cancelled);

/* Copy the file-name index and borrow immutable RuntimeFiles byte vectors.
 * There can be one active source file scope; it must outlive every source
 * lbFile/lbArchive consumer in that world. */
MeleeWebSourceFileScope* melee_web_source_files_begin(
    const MeleeWebSourceFileInput* files, size_t count, char* error,
    size_t error_size);
int melee_web_source_files_end(MeleeWebSourceFileScope* scope, char* error,
                              size_t error_size);

/* Exact RuntimeFiles-name lookup, also accepting one retail DVD root-path
 * slash (for example /GrNLa.dat -> GrNLa.dat). Missing files and an inactive
 * scope return failure; callers must surface that failure rather than
 * fabricate a success value. */
int melee_web_source_file_size(const char* name, size_t* size);
int melee_web_source_file_copy(const char* name, void* destination,
                               size_t* size);
/* lbArchive consumes the exact source filename associated with the most recent
 * synchronous copy of this buffer. The returned name remains valid until the
 * source file scope closes. */
const char* melee_web_source_file_take_name(const void* copied_buffer);

/* Return a stable DVD entry only while the exact file is present in the
 * active RuntimeFiles scope. Entry numbers occupy a private positive range. */
int melee_web_source_file_entry(const char* name);
int melee_web_source_file_entry_owned(int entry);
int melee_web_source_files_active(void);
int melee_web_source_file_drive_busy(void);

/* Files present on the validated disc but intentionally omitted from the
 * bounded RuntimeFiles import (for example authored THP movies). The catalog
 * owns immutable names and sizes across source scene scopes; clear it when the
 * selected disc session is replaced. */
int melee_web_source_files_external_set(const char* name, size_t size,
                                        char* error, size_t error_size);
int melee_web_source_files_external_clear(char* error, size_t error_size);

/* Only bounded aligned ranges may be streamed from external files. */
#define MELEE_WEB_SOURCE_FILE_STREAM_MAX (16u * 1024u * 1024u)

/* Source DMA destinations must be registered by the source object that owns
 * them. Transfer requests accept only aligned, in-bounds DVD reads and are
 * completed by pump(), never inline. */
int melee_web_source_file_region_register(void* base, size_t size,
                                          char* error, size_t error_size);
int melee_web_source_file_region_unregister(void* base, size_t size,
                                            char* error, size_t error_size);
int melee_web_source_file_request(int entry, uintptr_t offset,
                                  uintptr_t destination, size_t size,
                                  int type, int priority,
                                  MeleeWebSourceFileCallback callback,
                                  void* args, int* request,
                                  char* error, size_t error_size);
int melee_web_source_file_supply(int request, const void* bytes, size_t size,
                                 char* error, size_t error_size);
int melee_web_source_file_cancel(int request, uint32_t flags,
                                 MeleeWebSourceFileCallback callback,
                                 void* args);
int melee_web_source_files_pump(char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
