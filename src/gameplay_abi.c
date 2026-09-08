#include "gameplay_abi.h"
#include "gameplay_compat.h"

#include <melee/ft/types.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Original source field annotations, checked independently of ASSERT_SIZE:
 * upstream disables that macro outside MUST_MATCH/LINT. CommandInfo's trailing
 * comments predate event_return[3]; actual source/PPC layout is 0x24 bytes. */
#define OFFSET(type, field, value) \
    _Static_assert(offsetof(type, field) == value, #type "." #field " layout changed")
_Static_assert(sizeof(void*) == 4 && sizeof(int) == 4 && sizeof(float) == 4,
               "Gameplay boundary requires Wasm32-width pointers and scalars");
_Static_assert(sizeof(Fighter) == 0x23ec && _Alignof(Fighter) == 4, "Fighter layout changed");
OFFSET(Fighter, gobj, 0); OFFSET(Fighter, motion_id, 0x10);
OFFSET(Fighter, facing_dir, 0x2c); OFFSET(Fighter, self_vel, 0x80);
OFFSET(Fighter, cur_pos, 0xb0); OFFSET(Fighter, ground_or_air, 0xe0);
OFFSET(Fighter, ft_data, 0x10c); OFFSET(Fighter, co_attrs, 0x110);
OFFSET(Fighter, x3E4_fighterCmdScript, 0x3e4);
OFFSET(Fighter, x590, 0x590); OFFSET(Fighter, x594_s32, 0x594);
OFFSET(Fighter, x598, 0x598); OFFSET(Fighter, parts, 0x5e8);
OFFSET(Fighter, coll_data, 0x6f0); OFFSET(Fighter, anim_cb, 0x21a0);
OFFSET(Fighter, phys_cb, 0x21a4); OFFSET(Fighter, coll_cb, 0x21a8);
OFFSET(Fighter, cmd_vars, 0x2200); OFFSET(Fighter, throw_flags, 0x2210);
OFFSET(Fighter, cmd_timer, 0x2214); OFFSET(Fighter, u, 0x222c);
OFFSET(Fighter, mv, 0x2340);
_Static_assert(sizeof(CommandInfo) == 0x24 && _Alignof(CommandInfo) == 4, "CommandInfo layout changed");
OFFSET(CommandInfo, timer, 0); OFFSET(CommandInfo, frame_count, 4);
OFFSET(CommandInfo, u, 8); OFFSET(CommandInfo, ptr, 8);
OFFSET(CommandInfo, loop_count, 0xc); OFFSET(CommandInfo, event_return, 0x10);
OFFSET(CommandInfo, loop_count_dup, 0x1c); OFFSET(CommandInfo, unk_x18, 0x20);
_Static_assert(sizeof(HSD_GObj) == 0x38 && _Alignof(HSD_GObj) == 8, "HSD_GObj layout changed");
OFFSET(HSD_GObj, next, 8); OFFSET(HSD_GObj, proc, 0x18);
OFFSET(HSD_GObj, gxlink_prios, 0x20); OFFSET(HSD_GObj, hsd_obj, 0x28);
OFFSET(HSD_GObj, user_data, 0x2c); OFFSET(HSD_GObj, user_data_remove_func, 0x30);
_Static_assert(sizeof(HSD_GObjProc) == 0x18, "HSD_GObjProc layout changed");
OFFSET(HSD_GObjProc, gobj, 0x10); OFFSET(HSD_GObjProc, on_invoke, 0x14);
_Static_assert(sizeof(StageCallbacks) == 20, "StageCallbacks layout changed");
OFFSET(StageCallbacks, flags, 0x10);
_Static_assert(sizeof(MotionState) == 0x20, "MotionState layout changed");
OFFSET(MotionState, _, 8); OFFSET(MotionState, anim_cb, 0xc);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
OFFSET(Fighter, x596_bits, 0x594);
#else
OFFSET(Fighter, x596_bits, 0x596);
#endif
#undef OFFSET

static int aliases_match(const Fighter* fp, uint32_t word)
{
    return fp->x594_b0 == ((word >> 31) & 1) &&
           fp->x594_b1_loop == ((word >> 30) & 1) &&
           fp->x594_b2 == ((word >> 29) & 1) &&
           fp->x594_b3 == ((word >> 28) & 1) &&
           fp->x594_b4 == ((word >> 27) & 1) &&
           fp->x594_b5 == ((word >> 26) & 1) &&
           fp->x594_b6 == ((word >> 25) & 1) &&
           fp->x594_b7 == ((word >> 24) & 1) &&
           fp->x594_pad == (word >> 22) &&
           fp->x594_bits == ((word >> 9) & 0x1fff) &&
           fp->x594_pad2 == ((word >> 6) & 7) &&
           fp->x597_bits == (word & 63) &&
           fp->x596_bits.x0 == ((word >> 9) & 127) &&
           fp->x596_bits.x7 == ((word >> 6) & 7);
}

int melee_web_gameplay_check_fighter_flags(char* error, size_t error_size)
{
    Fighter fp = {0};
    for (unsigned bit = 0; bit < 32; ++bit) {
        const uint32_t one = UINT32_C(1) << bit;
        for (unsigned complement = 0; complement < 2; ++complement) {
            const uint32_t word = complement ? ~one : one;
            memcpy(&fp.x594_s32, &word, sizeof(word));
            if (!aliases_match(&fp, word)) {
                if (error && error_size) snprintf(error, error_size,
                    "Fighter animation flag overlay differs at bit %u", bit);
                return 0;
            }
        }
    }
    if (error && error_size) error[0] = 0;
    return 1;
}

int melee_web_gameplay_native_command_overlay_compatible(void)
{
    union CmdUnion command = {0};
    uint32_t word;
    command.Command_00.code = 0x2d;
    command.Command_00.value = 0x123456;
    memcpy(&word, &command, sizeof(word));
    return word == ((UINT32_C(0x2d) << 26) | UINT32_C(0x123456));
}

int melee_web_gameplay_check_stage_flags(char* error, size_t error_size)
{
    StageCallbacks callbacks = {0};
    for (unsigned bit = 0; bit < 32; ++bit) {
        for (unsigned complement = 0; complement < 2; ++complement) {
            const uint32_t one = UINT32_C(1) << bit;
            callbacks.flags = complement ? ~one : one;
            const uint32_t actual = ((uint32_t)callbacks.flags_b0 << 31) |
                ((uint32_t)callbacks.flags_b1 << 30) | ((uint32_t)callbacks.flags_b2 << 29) |
                ((uint32_t)callbacks.flags_b3 << 28) | ((uint32_t)callbacks.flags_b4 << 27) |
                ((uint32_t)callbacks.flags_b5 << 26) | ((uint32_t)callbacks.flags_b6 << 25) |
                ((uint32_t)callbacks.flags_b7 << 24);
            if (actual != (callbacks.flags & UINT32_C(0xff000000))) {
                if (error && error_size) snprintf(error, error_size,
                    "Stage callback flag overlay differs at bit %u", bit);
                return 0;
            }
        }
    }
    if (error && error_size) error[0] = 0;
    return 1;
}

int melee_web_gameplay_check_motion_flags(char* error, size_t error_size)
{
    MotionState state = {0};
    for (unsigned bit = 0; bit < 32; ++bit) {
        for (unsigned complement = 0; complement < 2; ++complement) {
            const uint32_t one = UINT32_C(1) << bit;
            state._ = complement ? ~one : one;
            const uint32_t actual = ((uint32_t)state.move_id << 24) |
                ((uint32_t)state.x9_b0 << 23) | ((uint32_t)state.x9_b1 << 22) |
                ((uint32_t)state.x9_b2 << 21) | ((uint32_t)state.x9_b3 << 20) |
                ((uint32_t)state.x9_b4 << 19) | ((uint32_t)state.x9_b5 << 18) |
                ((uint32_t)state.x9_b6 << 17) | ((uint32_t)state.x9_b7 << 16) |
                ((uint32_t)state.xA << 8) | state.xB;
            if (actual != state._) {
                if (error && error_size) snprintf(error, error_size,
                    "MotionState flag overlay differs at bit %u", bit);
                return 0;
            }
        }
    }
    if (error && error_size) error[0] = 0;
    return 1;
}
