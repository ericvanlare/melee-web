#ifndef MELEE_WEB_HSD_PROBE_COMPAT_H
#define MELEE_WEB_HSD_PROBE_COMPAT_H

/* Header differences observed between the pinned Melee and Aurora revisions.
 * Apply this only when compiling the HSD probe, never to Aurora itself.
 */
#include <stdarg.h>
#include <sys/types.h>

/* The decomp declares ssize_t as signed int, while Emscripten's libc uses
 * signed long. Both are 32-bit in wasm32, but C rejects the duplicate typedef.
 * Load the original header once with its local alias renamed, then restore
 * the system name before any SDK headers are parsed. No functions in this
 * probe use the decomp's ssize_t alias; libc keeps its own type unchanged.
 */
#define ssize_t melee_probe_decomp_ssize_t
#include <Runtime/platform.h>
#undef ssize_t

#include <dolphin/mtx.h>

/* Melee names the SDK's three-float vector Vec3; Aurora names it Vec. */
typedef Vec Vec3;

/* Used in an HSD texture-expression descriptor, absent from Aurora's enum
 * header. Values match Melee's extern/dolphin/include/dolphin/gx/GXEnum.h.
 * The probe does not execute texture-expression code.
 */
typedef enum {
    GX_TC_LINEAR = 0,
    GX_TC_GE = 1,
    GX_TC_EQ = 2,
    GX_TC_LE = 3
} GXTevClampMode;

_Static_assert(sizeof(Vec3) == 12, "HSD requires three 32-bit float components");

#endif
