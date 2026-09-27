/* Compile the production packed types, flag producer and Cape consumer. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <melee/ft/types.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftCommon/ftCo_DamageSong.h>
#include <sysdolphin/baselib/gobj.h>
#include "legacy_motion_word.h"

static unsigned cape_calls;
static u16 attack_serial;
u16 plAttack_80037B08(void) { return ++attack_serial; }
melee_source_bool it_8026B6C8(Item_GObj* gobj) { return gobj != NULL; }
void ftCo_800C3598(Fighter_GObj* gobj) { assert(gobj); ++cape_calls; }

static void check_word(u32 word)
{
    union Struct2070 flags;
    UnkPlBonusBits bonus;
    u32 roundtrip;
    flags.x2070_int = (s32) word;
    assert(sizeof(flags) == 4 && sizeof(bonus) == 4);
    assert((u8) flags.x2070 == (word >> 24));
    assert(flags.x2071_b0_3 == ((word >> 20) & 15));
    assert(flags.x2071_b4 == ((word >> 19) & 1));
    assert(flags.x2071_b5 == ((word >> 18) & 1));
    assert(flags.x2071_b6 == ((word >> 17) & 1));
    assert(flags.x2071_b7 == ((word >> 16) & 1));
    assert(flags.x2072_b0 == ((word >> 15) & 1));
    assert(flags.x2072_b1 == ((word >> 14) & 1));
    assert(flags.x2072_b2 == ((word >> 13) & 1));
    assert(flags.count_x1A4 == ((word >> 12) & 1));
    assert(flags.count_thrown_items == ((word >> 11) & 1));
    assert(flags.count_aerials == ((word >> 10) & 1));
    assert(flags.count_x1A0 == ((word >> 9) & 1));
    assert(flags.count_specials == ((word >> 8) & 1));
    assert(flags.x2073 == (word & 255));
    memcpy(&bonus, &flags, 4);
    assert(bonus.x0 == (word >> 24));
    assert(bonus.x1 == ((word >> 16) & 255));
    assert(bonus.x2_b0 == ((word >> 15) & 1));
    assert(bonus.x2_b1 == ((word >> 14) & 1));
    assert(bonus.x2_b2 == ((word >> 13) & 1));
    assert(bonus.x2_b3 == ((word >> 12) & 1));
    assert(bonus.x2_b4 == ((word >> 11) & 1));
    assert(bonus.x2_b5 == ((word >> 10) & 1));
    assert(bonus.x2_b6 == ((word >> 9) & 1));
    assert(bonus.x2_b7 == ((word >> 8) & 1));
    assert(bonus.x3 == (word & 255));
    memcpy(&roundtrip, &bonus, 4);
    assert(roundtrip == word);
    /* Named field writes must retain every other source mask. */
    flags.count_thrown_items ^= 1;
    assert((u32) flags.x2070_int == (word ^ (1u << 11)));
    flags.x2071_b0_3 ^= 15;
    assert((u32) flags.x2070_int == (word ^ (1u << 11) ^ (15u << 20)));
}

int main(void)
{
    Fighter fighter = {0};
    Fighter_GObj gobj = {0};
    gobj.user_data = &fighter;
    for (unsigned bit = 0; bit < 32; ++bit) {
        check_word(1u << bit);
        check_word(~(1u << bit));
    }
    assert(ftCo_MF_Catch == 0x00a00033);
    check_word(ftCo_MF_Catch);
    check_word(0x87654321);
    union LegacyStruct2070 legacy;
    legacy.x2070_int = ftCo_MF_Catch;
    assert(legacy.x2071_b0_3 == 0); /* Prior layout takes the wrong Cape route. */
    for (unsigned category = 0; category < 16; ++category) {
        unsigned before = cape_calls;
        u32 word = (category << 20) | 0x33;
        ft_800895E0(&fighter, word);
        assert((u32) fighter.x2070.x2070_int == word);
        assert(fighter.x2070.x2071_b0_3 == category);
        int expected = category < 9 || category > 11;
        assert(ftCo_800C3538(&gobj) == expected);
        assert(cape_calls == before + expected);
        fighter.x2222_b2 = 1;
        assert(ftCo_800C3538(&gobj) == 0);
        assert(cape_calls == before + expected);
        fighter.x2222_b2 = 0;
    }
    /* The producer also reads the low-byte attack id before storing the word. */
    fighter.kind = FTKIND_LUIGI;
    ft_800895E0(&fighter, 0x71);
    assert(fighter.x2070.x2070_int == 0x240063);
    fighter.kind = FTKIND_MARIO;
    ft_800895E0(&fighter, 0x62);
    assert(fighter.x2070.x2070_int == 0x62);
    fighter.item_gobj = &gobj;
    ft_800895E0(&fighter, 0x62);
    assert(fighter.x2070.x2070_int == 0x44003d);
    puts("Source motion word masks, bonus aliases and all Cape categories passed");
}
