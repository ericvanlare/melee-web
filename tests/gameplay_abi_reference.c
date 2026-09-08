/* Compile these original declarations for PowerPC and Wasm at -O2. Constant
 * return values expose compiler bitfield layout independently of our checks. */
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdint.h>
#include <string.h>

#define REFERENCE(name, field, value) \
uint32_t name(void) { Fighter fp = {0}; fp.field = value; return (uint32_t)fp.x594_s32; }
REFERENCE(flag_b0, x594_b0, 1)
REFERENCE(flag_loop, x594_b1_loop, 1)
REFERENCE(flag_b2, x594_b2, 1)
REFERENCE(flag_b3, x594_b3, 1)
REFERENCE(flag_b4, x594_b4, 1)
REFERENCE(flag_b5, x594_b5, 1)
REFERENCE(flag_b6, x594_b6, 1)
REFERENCE(flag_b7, x594_b7, 1)
REFERENCE(flag_pad, x594_pad, 1023)
REFERENCE(flag_mask, x594_bits, 8191)
REFERENCE(flag_pad2, x594_pad2, 7)
REFERENCE(flag_kind, x597_bits, 63)
REFERENCE(flag_nested0, x596_bits.x0, 127)
REFERENCE(flag_nested7, x596_bits.x7, 7)
uint32_t fighter_size(void) { return sizeof(Fighter); }
uint32_t command_size(void) { return sizeof(CommandInfo); }
uint32_t command_tail_offset(void) { return offsetof(CommandInfo, loop_count_dup); }
uint32_t gobj_size(void) { return sizeof(HSD_GObj); }
uint32_t nested_offset(void) { return offsetof(Fighter, x596_bits); }
uint32_t throw_flag_word(void) { Fighter fp = {0}; fp.throw_flags_b0 = 1; return fp.throw_flags; }
uint32_t command_word(void) {
    union { union CmdUnion command; uint32_t word; } value = {0};
    value.command.Command_00.code = 3; value.command.Command_00.value = 7;
    return value.word;
}
#define STAGE_REFERENCE(bit) \
uint32_t stage_flag_##bit(void) { StageCallbacks value = {0}; value.flags_b##bit = 1; return value.flags; }
STAGE_REFERENCE(0) STAGE_REFERENCE(1) STAGE_REFERENCE(2) STAGE_REFERENCE(3)
STAGE_REFERENCE(4) STAGE_REFERENCE(5) STAGE_REFERENCE(6) STAGE_REFERENCE(7)
uint32_t stage_callbacks_size(void) { return sizeof(StageCallbacks); }
uint32_t stage_flags_offset(void) { return offsetof(StageCallbacks, flags); }

#define MOTION_REFERENCE(name, field, maximum) \
uint32_t motion_##name(void) { MotionState value = {0}; value.field = maximum; return value._; }
MOTION_REFERENCE(move_id, move_id, 255)
MOTION_REFERENCE(b0, x9_b0, 1) MOTION_REFERENCE(b1, x9_b1, 1)
MOTION_REFERENCE(b2, x9_b2, 1) MOTION_REFERENCE(b3, x9_b3, 1)
MOTION_REFERENCE(b4, x9_b4, 1) MOTION_REFERENCE(b5, x9_b5, 1)
MOTION_REFERENCE(b6, x9_b6, 1) MOTION_REFERENCE(b7, x9_b7, 1)
MOTION_REFERENCE(xa, xA, 255) MOTION_REFERENCE(xb, xB, 255)
uint32_t motion_size(void) { return sizeof(MotionState); }
uint32_t motion_word_offset(void) { return offsetof(MotionState, _); }
uint32_t motion_callback_offset(void) { return offsetof(MotionState, anim_cb); }
/* Test harness extracts this initializer directly from original ftmotionstates.c. */
#include "motion_wait_fixture.h"
uint32_t motion_wait_word(void) { return motion_wait._; }
uint32_t motion_wait_move(void) { return motion_wait.move_id; }
uint32_t motion_wait_default(void) { return FtMoveId_Default; }
uint32_t motion_wait_b0(void) { return motion_wait.x9_b0; }
uint32_t motion_wait_b1(void) { return motion_wait.x9_b1; }

#define BYTE_REFERENCE(bit) \
uint32_t byte_flag_##bit(void) { UnkFlagStruct v={0};v.b##bit=1;return v.u8; } \
uint32_t byte_clear_##bit(void) { UnkFlagStruct v;v.u8=255;v.b##bit=0;return v.u8; }
BYTE_REFERENCE(0) BYTE_REFERENCE(1) BYTE_REFERENCE(2) BYTE_REFERENCE(3)
BYTE_REFERENCE(4) BYTE_REFERENCE(5) BYTE_REFERENCE(6) BYTE_REFERENCE(7)
uint32_t byte_flag_size(void) { return sizeof(UnkFlagStruct); }
uint32_t fighter_visible_on(void) { Fighter fp={0};fp.x21FC_flag.u8=1;return fp.x21FC_flag.b7; }
uint32_t fighter_visible_off(void) { Fighter fp={0};fp.x21FC_flag.u8=0x80;return fp.x21FC_flag.b7; }

#define COLOR_REFERENCE(name,field,value) \
uint32_t name(void) { union { union ColorOverlay_x8_t c; uint32_t word; } v={0}; v.c.field=value; return v.word; }
COLOR_REFERENCE(color_opcode,unk.unk,63)
COLOR_REFERENCE(color_timer,unk.timer,0x3ffffff)
COLOR_REFERENCE(color_rot1_x,light_rot1.x,-1)
COLOR_REFERENCE(color_rot1_yz,light_rot1.yz,-1)
COLOR_REFERENCE(color_rot2_x,light_rot2.x,-1)
COLOR_REFERENCE(color_rot2_yz,light_rot2.yz,-1)
COLOR_REFERENCE(color_enable,light_rot2.light_enable,1)
