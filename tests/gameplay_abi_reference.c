/* Compile these original declarations for PowerPC and Wasm at -O2. Constant
 * return values expose compiler bitfield layout independently of our checks. */
#include "gameplay_compat.h"
#include <melee/ft/types.h>
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
