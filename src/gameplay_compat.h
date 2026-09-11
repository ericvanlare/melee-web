#ifndef MELEE_WEB_GAMEPLAY_COMPAT_H
#define MELEE_WEB_GAMEPLAY_COMPAT_H

#define MELEE_WEB_GAMEPLAY 1

/* Compile original gameplay sources against Aurora's SDK headers. Keep this
 * separate from the existing graphics compatibility boundary. These vector
 * declarations match Melee's pinned extern/dolphin/include/dolphin/mtx.h;
 * Aurora provides Vec but does not declare these gameplay vector types. */
#include "hsd_probe_compat.h"
#include <dolphin/gx.h>
#include <dolphin/card.h>
#include "gameplay_ps_math.h"

/* Retail uses the paired-single SDK path. Aurora's portable C matrix
 * fallback has different arithmetic rounding; keep the original PS operation
 * order in gameplay and HSD joint evaluation. Explicit C_MTXConcat callers
 * retain their separate C SDK contract. */
#undef MTXConcat
#define MTXConcat melee_web_ps_mtx_concat
#undef PSMTXConcat
#define PSMTXConcat melee_web_ps_mtx_concat
#undef MTXQuat
#define MTXQuat melee_web_ps_mtx_quat
#undef PSMTXQuat
#define PSMTXQuat melee_web_ps_mtx_quat

typedef struct { f32 x, y; } Vec2;
typedef struct { int x, y; } IntVec2;
typedef struct { s32 x, y; } S32Vec2;
typedef struct { s32 x, y, z; } S32Vec3;
typedef struct { s8 x, y, z; } S8Vec3;
typedef Quaternion Vec4;
typedef struct { u8 x, y, z, w; } U8Vec4;

/* Source SDK declarations missing from Aurora; unsupported hardware call keeps
 * the original assertion behavior in gameplay_sdk.c. */
void GXSetTevClampMode(int stage, int mode);
void GXWaitDrawDone(void);
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 projection[4][4]);
void PADSetSamplingRate(unsigned long msec);
s32 CARDFormatAsync(s32 channel, CARDCallback callback);
#ifndef VIPadFrameBufferWidth
#define VIPadFrameBufferWidth(width) ((u16) (((u16) (width) + 15) & ~15))
#endif


/* Exact extended source PAD masks used by original HSD input processing. */
#ifndef PAD_STICK_UP
#define PAD_STICK_UP (1 << 16)       // 0x10000
#define PAD_STICK_DOWN (1 << 17)     // 0x20000
#define PAD_STICK_LEFT (1 << 18)     // 0x40000
#define PAD_STICK_RIGHT (1 << 19)    // 0x80000
#define PAD_SUBSTICK_UP (1 << 20)    // 0x100000
#define PAD_SUBSTICK_DOWN (1 << 21)  // 0x200000
#define PAD_SUBSTICK_LEFT (1 << 22)  // 0x400000
#define PAD_SUBSTICK_RIGHT (1 << 23) // 0x800000
#define PAD_TRIGGER_LR (1 << 31)     // 0x80000000
#define PAD_CONFIRM (1ULL << 32)        // 0x100000000
#define PAD_CANCEL (1ULL << 33)         // 0x200000000
#define PAD_LR_START (1ULL << 34)       // 0x400000000
#define PAD_LRA_START (1ULL << 35)      // 0x800000000
#define PAD_ANY_UP (1ULL << 36)         // 0x1000000000
#define PAD_ANY_DOWN (1ULL << 37)       // 0x2000000000
#define PAD_ANY_LEFT (1ULL << 38)       // 0x4000000000
#define PAD_ANY_RIGHT (1ULL << 39)      // 0x8000000000

#endif

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
