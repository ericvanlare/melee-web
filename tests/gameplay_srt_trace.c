/* Original Roy subject call 65 (source tick 64). Captured matrix operands and
 * outputs are documented in ROY_DR_MARIO_PORT_NOTES.md. No gameplay state is
 * supplied to the runtime: this executable tests the shared math boundary. */
#include "sysdolphin/baselib/mtx.h"
#include "gameplay_trig.h"
#include "gameplay_ps_math.h"
#include <stdio.h>

static void print_matrix(const Mtx matrix)
{
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column)
            printf("%08x%c", melee_web_trig_bits(matrix[row][column]),
                   row == 2 && column == 3 ? '\n' : ' ');
}

int main(void)
{
    const uint32_t grandparent_bits[12] = {
        0xb381146f, 0x00000000, 0x3f8a3d71, 0xc271ca76,
        0x00000000, 0x3f8a3d71, 0x00000000, 0x418aa377,
        0xbf8a3d71, 0x80000000, 0xb381146f, 0x32d60a62,
    };
    Mtx grandparent, parent_local, parent_world, child_local, child_world;
    memcpy(grandparent, grandparent_bits, sizeof(grandparent));
    Vec3 scale = { 1, 1, 1 };
    Vec3 parent_rotation = {
        melee_web_trig_from_bits(0xbcbe0000),
        melee_web_trig_from_bits(0xbef66000),
        melee_web_trig_from_bits(0xbca4ff30),
    };
    Vec3 parent_translation = {
        0, melee_web_trig_from_bits(0xbde00000),
        melee_web_trig_from_bits(0x3c000000),
    };
    Vec3 child_rotation = {
        melee_web_trig_from_bits(0x3ea1bc00),
        melee_web_trig_from_bits(0xbe637000),
        melee_web_trig_from_bits(0xbe1ab000),
    };
    Vec3 child_translation = {
        0, melee_web_trig_from_bits(0x3fccccd5),
        melee_web_trig_from_bits(0x3d4ccccd),
    };
    HSD_MtxSRT(parent_local, &scale, &parent_rotation, &parent_translation, NULL);
    melee_web_ps_mtx_concat(grandparent, parent_local, parent_world);
    HSD_MtxSRT(child_local, &scale, &child_rotation, &child_translation, NULL);
    melee_web_ps_mtx_concat(parent_world, child_local, child_world);
    print_matrix(parent_world);
    print_matrix(child_world);
    return 0;
}
