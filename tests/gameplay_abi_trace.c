#include "gameplay_abi.h"
#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/gr/types.h>
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
    union ColorOverlay_x8_t overlay={0};
    uint32_t canonical=0xa7fff123;memcpy(&overlay,&canonical,4);
    CHECK(overlay.unk.unk==41&&overlay.unk.timer==0x3fff123);
    CHECK(overlay.light_rot2.x==-1&&overlay.light_rot2.yz==0x123&&overlay.light_rot2.light_enable==1);
    const unsigned char rgba[4]={1,19,97,255};memcpy(&overlay,rgba,4);
    CHECK(overlay.light_color.r==1&&overlay.light_color.g==19&&overlay.light_color.b==97&&overlay.light_color.a==255);
    UnkFlagStruct byte_flags={0};
#define BYTE_WRITE(bit) do { \
    byte_flags.u8=0;byte_flags.b##bit=1;CHECK(byte_flags.u8==(1U<<(7-bit))); \
    byte_flags.u8=255;byte_flags.b##bit=0;CHECK(byte_flags.u8==(255U^(1U<<(7-bit)))); \
    byte_flags.u8=1U<<(7-bit);CHECK(byte_flags.b##bit==1); \
    byte_flags.u8=255U^(1U<<(7-bit));CHECK(byte_flags.b##bit==0); \
} while(0)
    BYTE_WRITE(0);BYTE_WRITE(1);BYTE_WRITE(2);BYTE_WRITE(3);
    BYTE_WRITE(4);BYTE_WRITE(5);BYTE_WRITE(6);BYTE_WRITE(7);
#undef BYTE_WRITE
    CHECK(sizeof(UnkFlagStruct)==1);
    Fighter fp = {0};
    fp.x21FC_flag.u8=1;CHECK(fp.x21FC_flag.b7&&!fp.x21FC_flag.b0);
    fp.x21FC_flag.u8=0x80;CHECK(!fp.x21FC_flag.b7&&fp.x21FC_flag.b0);
    char error[128];
    CHECK(melee_web_gameplay_check_fighter_flags(error, sizeof(error)));
    CHECK(melee_web_gameplay_check_stage_flags(error, sizeof(error)));
    CHECK(melee_web_gameplay_check_motion_flags(error, sizeof(error)));
    MotionState state = {0};
#define MOTION_WRITE(field, maximum, mask) do { \
    state._ = 0; state.field = maximum; CHECK(state._ == UINT32_C(mask)); \
    state._ = UINT32_MAX; state.field = 0; CHECK(state._ == ~UINT32_C(mask)); \
} while (0)
    MOTION_WRITE(move_id, 255, 0xff000000);
    MOTION_WRITE(x9_b0, 1, 0x00800000); MOTION_WRITE(x9_b1, 1, 0x00400000);
    MOTION_WRITE(x9_b2, 1, 0x00200000); MOTION_WRITE(x9_b3, 1, 0x00100000);
    MOTION_WRITE(x9_b4, 1, 0x00080000); MOTION_WRITE(x9_b5, 1, 0x00040000);
    MOTION_WRITE(x9_b6, 1, 0x00020000); MOTION_WRITE(x9_b7, 1, 0x00010000);
    MOTION_WRITE(xA, 255, 0x0000ff00); MOTION_WRITE(xB, 255, 0x000000ff);
#undef MOTION_WRITE
    StageCallbacks callbacks = {0};
#define STAGE_WRITE(bit, mask) do { \
    callbacks.flags = 0; callbacks.flags_b##bit = 1; CHECK(callbacks.flags == UINT32_C(mask)); \
    callbacks.flags = UINT32_MAX; callbacks.flags_b##bit = 0; CHECK(callbacks.flags == ~UINT32_C(mask)); \
} while (0)
    STAGE_WRITE(0, 0x80000000); STAGE_WRITE(1, 0x40000000);
    STAGE_WRITE(2, 0x20000000); STAGE_WRITE(3, 0x10000000);
    STAGE_WRITE(4, 0x08000000); STAGE_WRITE(5, 0x04000000);
    STAGE_WRITE(6, 0x02000000); STAGE_WRITE(7, 0x01000000);
#undef STAGE_WRITE
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
