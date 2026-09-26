#ifndef MELEE_WEB_GAMEPLAY_SOURCE_FILES_H
#define MELEE_WEB_GAMEPLAY_SOURCE_FILES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebSourceFileInput {
    const char* name;
    const uint8_t* bytes;
    size_t size;
} MeleeWebSourceFileInput;

typedef struct MeleeWebSourceFileScope MeleeWebSourceFileScope;

/* Copy the file-name index and borrow immutable RuntimeFiles byte vectors.
 * There can be one active source file scope; it must outlive every source
 * lbFile/lbArchive consumer in that world. */
MeleeWebSourceFileScope* melee_web_source_files_begin(
    const MeleeWebSourceFileInput* files, size_t count, char* error,
    size_t error_size);
int melee_web_source_files_end(MeleeWebSourceFileScope* scope, char* error,
                              size_t error_size);

/* Exact FST-name lookup. Missing files and an inactive scope return failure;
 * callers must surface that failure rather than fabricate a success value. */
int melee_web_source_file_size(const char* name, size_t* size);
int melee_web_source_file_copy(const char* name, void* destination,
                               size_t* size);
/* lbArchive consumes the exact source filename associated with the most recent
 * synchronous copy of this buffer. The returned name remains valid until the
 * source file scope closes. */
const char* melee_web_source_file_take_name(const void* copied_buffer);

#ifdef __cplusplus
}
#endif
#endif
