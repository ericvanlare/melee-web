#ifndef MELEE_WEB_SOURCE_PPC_PROFILE_GALE01R2_H
#define MELEE_WEB_SOURCE_PPC_PROFILE_GALE01R2_H

/* GALE01 revision-2 profile derived from the owned DOL and its pinned symbol
 * map. Values are checked by test_source_stack_profile.py. The callback entry
 * is __init_registers' _stack_addr after __start/main/gm_801A4510/
 * runGameMode/gm_801A4014/gm_801A4D34 have executed their source prologues. */
#define MELEE_WEB_GALE01R2_SOURCE_STACK_TOP 0x804EEC00u
#define MELEE_WEB_GALE01R2_GM_CALLBACK_SP   0x804EEAF8u
#define MELEE_WEB_GALE01R2_SEED_WORD        0x804D5F90u
#define MELEE_WEB_GALE01R2_FLOOR_LOCAL_Y0   0x44u

#define MELEE_WEB_GALE01R2_FRAME_GOBJ_DISPATCH       0x30u
#define MELEE_WEB_GALE01R2_FRAME_FIGHTER_CPU_CALLBACK 0x18u
#define MELEE_WEB_GALE01R2_FRAME_CPU_CALLBACK        0x20u
#define MELEE_WEB_GALE01R2_FRAME_CPU_STATE_DISPATCH  0xC8u
#define MELEE_WEB_GALE01R2_FRAME_CPU_STATE_18        0x48u
#define MELEE_WEB_GALE01R2_FRAME_CPU_FLOOR_QUERY     0x90u
#define MELEE_WEB_GALE01R2_FRAME_MP_CHECK_FLOOR      0xF0u

#endif
