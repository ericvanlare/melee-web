#ifndef MELEE_WEB_GAMEPLAY_COMPAT_H
#define MELEE_WEB_GAMEPLAY_COMPAT_H

#define MELEE_WEB_GAMEPLAY 1

/* Compile original gameplay sources against Aurora's SDK headers. Keep this
 * separate from the existing graphics compatibility boundary. These vector
 * declarations match Melee's pinned extern/dolphin/include/dolphin/mtx.h;
 * Aurora provides Vec but does not declare these gameplay vector types. */
#include "hsd_probe_compat.h"

typedef struct { f32 x, y; } Vec2;
typedef struct { int x, y; } IntVec2;
typedef struct { s32 x, y; } S32Vec2;
typedef struct { s32 x, y, z; } S32Vec3;
typedef struct { s8 x, y, z; } S8Vec3;
typedef Quaternion Vec4;

/* Original MSL/math.h defines this double constant. Strict C11 libc headers
 * do not expose it; preserve the original expression and precision. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI / 2)
#endif

_Static_assert(sizeof(Vec2) == 8 && _Alignof(Vec2) == 4, "GameCube Vec2 layout");
_Static_assert(sizeof(IntVec2) == 8 && sizeof(S32Vec2) == 8, "GameCube integer vector layout");
_Static_assert(sizeof(S32Vec3) == 12 && sizeof(Vec4) == 16, "GameCube vector layout");
_Static_assert(sizeof(S8Vec3) == 3, "GameCube signed-byte vector layout");

#endif
