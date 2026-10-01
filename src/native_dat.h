#ifndef MELEE_WEB_NATIVE_DAT_H
#define MELEE_WEB_NATIVE_DAT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Checked immutable archive reader and owner-backed, zeroed native allocation.
 * Decoder C is compiled with exceptions: read/allocation failures unwind to the
 * C++ owner, which releases every allocation before publishing any descriptor.
 * UINT32_MAX denotes a null pointer; relocated offset zero remains valid. */
typedef struct MeleeWebNativeDat {
    void* context;
    uint32_t (*word)(void*, uint32_t);
    uint16_t (*half)(void*, uint32_t);
    uint8_t (*byte)(void*, uint32_t);
    uint32_t (*pointer)(void*, uint32_t, size_t);
    const void* (*region)(void*, uint32_t, size_t);
    /* Resolve a checked byte range in another source archive's retail address
     * window. NULL means no owned source archive maps the entire range. */
    const void* (*source_region)(void*, uint32_t, size_t);
    void* (*allocate)(void*, size_t, size_t);
    void (*reject)(void*, const char*);
    /* Bytes to the next authored reference target, for variable-length tables. */
    uint32_t (*extent)(void*, uint32_t);
} MeleeWebNativeDat;
#ifdef __cplusplus
}
#endif
#endif
