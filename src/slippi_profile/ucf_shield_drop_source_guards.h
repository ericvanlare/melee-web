/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_UCF_SHIELD_DROP_SOURCE_GUARDS_H
#define MELEE_WEB_SLIPPI_UCF_SHIELD_DROP_SOURCE_GUARDS_H

/* Included only after the original Fighter/common/surface declarations in
 * the composed source. Scalar helpers deliberately have no SDK dependency. */
#include <stddef.h>
#include <stdint.h>

#define SHIELD_DROP_TYPE(member, type) \
    _Static_assert(_Generic((member), type: 1, default: 0), \
                   "Shield Drop source field type changed: " #member)
SHIELD_DROP_TYPE(((Fighter *) 0)->input.cstick[0].y, float);
SHIELD_DROP_TYPE(((Fighter *) 0)->input.lstick[0].x, float);
SHIELD_DROP_TYPE(((Fighter *) 0)->input.lstick[0].y, float);
SHIELD_DROP_TYPE(((Fighter *) 0)->x670_timer_lstick_tilt_x, u8);
SHIELD_DROP_TYPE(((Fighter *) 0)->coll_data.floor.index, int);
SHIELD_DROP_TYPE(((Fighter *) 0)->coll_data.floor.flags, u32);
SHIELD_DROP_TYPE(((ftCommonData *) 0)->x314, float);
SHIELD_DROP_TYPE(((ftCommonData *) 0)->x320, int);
#undef SHIELD_DROP_TYPE
_Static_assert(LINE_FLAG_PLATFORM == 0x100, "platform source mask changed");

_Static_assert(sizeof(((Fighter *) 0)->input.cstick[0].y) == sizeof(float),
               "Shield Drop C-stick field must remain binary32");
_Static_assert(sizeof(((Fighter *) 0)->input.lstick[0].y) == sizeof(float),
               "Shield Drop main-stick field must remain binary32");
_Static_assert(sizeof(((Fighter *) 0)->x670_timer_lstick_tilt_x) == 1,
               "Shield Drop reads the unsigned X-tilt timer byte");
_Static_assert(sizeof(((Fighter *) 0)->coll_data.floor.index) == sizeof(int),
               "Shield Drop floor index must remain signed int");
_Static_assert(sizeof(((Fighter *) 0)->coll_data.floor.flags) == sizeof(u32),
               "Shield Drop floor flags must remain u32");

/* The Melee Fighter offsets are the pinned 32-bit source ABI. The Wasm32
 * source probe validates these against actual prepared source types. Native
 * scalar/caller fixtures use synthetic storage and do not impose offsets. */
#if defined(__wasm32__) && UINTPTR_MAX == UINT32_MAX
_Static_assert(offsetof(Fighter, input.cstick[0].y) == 0x63C,
               "Shield Drop C-stick Y source offset changed");
_Static_assert(offsetof(Fighter, input.lstick[0].x) == 0x620,
               "Shield Drop main-stick X source offset changed");
_Static_assert(offsetof(Fighter, input.lstick[0].y) == 0x624,
               "Shield Drop main-stick Y source offset changed");
_Static_assert(offsetof(Fighter, x670_timer_lstick_tilt_x) == 0x670,
               "Shield Drop X-tilt timer source offset changed");
_Static_assert(offsetof(Fighter, coll_data.floor.index) == 0x83C,
               "Shield Drop floor index source offset changed");
_Static_assert(offsetof(Fighter, coll_data.floor.flags) == 0x840,
               "Shield Drop floor flags source offset changed");
_Static_assert(offsetof(ftCommonData, x314) == 0x314,
               "Shield Drop C-stick threshold source offset changed");
_Static_assert(offsetof(ftCommonData, x320) == 0x320,
               "Shield Drop X-tilt window source offset changed");
_Static_assert(sizeof(u32) == sizeof(uint32_t),
               "Shield Drop source word must be 32-bit on the game target");
#endif

#endif
