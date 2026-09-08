#include "gameplay_abi.h"
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr,"ABI check failed at %d: %s\n",__LINE__,#value); return 1; } } while (0)
/* Each named view must set AND clear its own original numeric mask without
 * damaging any other bits. This tests aliases, not an implementation copy. */
#define WRITE_VIEW(field, maximum, mask) do { \
    fp.x594_s32 = 0; fp.field = maximum; CHECK((uint32_t)fp.x594_s32 == UINT32_C(mask)); \
    fp.x594_s32 = -1; fp.field = 0; CHECK((uint32_t)fp.x594_s32 == ~UINT32_C(mask)); \
} while (0)

int main(void)
{
    Fighter fp = {0};
    char error[128];
    CHECK(melee_web_gameplay_check_fighter_flags(error, sizeof(error)));
    WRITE_VIEW(x594_b0, 1, 0x80000000); WRITE_VIEW(x594_b1_loop, 1, 0x40000000);
    WRITE_VIEW(x594_b2, 1, 0x20000000); WRITE_VIEW(x594_b3, 1, 0x10000000);
    WRITE_VIEW(x594_b4, 1, 0x08000000); WRITE_VIEW(x594_b5, 1, 0x04000000);
    WRITE_VIEW(x594_b6, 1, 0x02000000); WRITE_VIEW(x594_b7, 1, 0x01000000);
    WRITE_VIEW(x594_pad, 0x3ff, 0xffc00000); WRITE_VIEW(x594_bits, 0x1fff, 0x003ffe00);
    WRITE_VIEW(x594_pad2, 7, 0x000001c0); WRITE_VIEW(x597_bits, 63, 0x0000003f);
    WRITE_VIEW(x596_bits.x0, 127, 0x0000fe00); WRITE_VIEW(x596_bits.x7, 7, 0x000001c0);
    fp.x594_s32 = 0x400002c1;
    CHECK(fp.x594_b1_loop == 1 && fp.x594_bits == 1 && fp.x597_bits == 1);
    CHECK(fp.x596_bits.x0 == 1 && fp.x596_bits.x7 == 3 && fp.x594_pad2 == 3);
    const uint8_t* bytes = (const uint8_t*)&fp.x594_s32;
    CHECK(bytes[0] == 0xc1 && bytes[1] == 2 && bytes[2] == 0 && bytes[3] == 0x40);
    CHECK(offsetof(Fighter, x596_bits) == 0x594); // Intentional nested host offset.

    /* Byte-only flags use host representation safely when read/written by
     * their names. Whole-word zero clears remain valid; numeric imports do not.
     * Do not conflate that with a proven serialized command decoder. */
    fp.throw_flags = 0; fp.throw_flags_b0 = 1;
    CHECK(fp.throw_flags == 1); // Original PPC's same write produces 0x80000000.
    fp.throw_flags = 0;
    CHECK(!fp.throw_flags_b0 && !fp.throw_flags_b7);
    CHECK(!melee_web_gameplay_native_command_overlay_compatible());
    union CmdUnion command = {0};
    uint32_t encoded = 0x0c000007; // Canonical command code 3, value 7.
    memcpy(&command, &encoded, sizeof(encoded));
    CHECK(command.Command_00.code != 3 || command.Command_00.value != 7);
    puts("Wasm gameplay layout and canonical animation flag aliases: passed; native command overlays unsupported");
    return 0;
}
