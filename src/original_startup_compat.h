#ifndef MELEE_WEB_ORIGINAL_STARTUP_COMPAT_H
#define MELEE_WEB_ORIGINAL_STARTUP_COMPAT_H

/* Runtime/platform.h spells out the retail 32-bit ssize_t ABI.  Emscripten's
 * libc has already declared its host spelling by the time C++ SDK headers are
 * read, so load the source header under a private name and retain libc's
 * public typedef. */
#define ssize_t melee_original_startup_ssize_t
#include <Runtime/platform.h>
#undef ssize_t

#include <dolphin/mtx.h>
typedef Vec Vec3;
typedef enum {
    GX_TC_LINEAR = 0,
    GX_TC_GE = 1,
    GX_TC_EQ = 2,
    GX_TC_LE = 3,
} GXTevClampMode;

#endif
