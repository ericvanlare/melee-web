#include "gameplay_compat.h"
#include <melee/ft/types.h>
#include <melee/ft/inlines.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>

/* No game services or assets are reached. These two omitted pre-callback
 * services record consumer order; they make no gameplay success claim. */
static Fighter fighter;
static HSD_GObj object;
static unsigned calls[8], count;
static void check(int condition) { if (!condition) abort(); }
static void record(unsigned call) { check(count < 8); calls[count++] = call; }
void ft_80088770(Fighter* fp) { check(fp == &fighter); record(1); }
void ft_800887CC(Fighter* fp) { check(fp == &fighter); record(2); }
static void death1(HSD_GObj* gobj) { check(gobj == &object); record(4); }
#include "koopa_callback.inc"

int main(void)
{
    object.user_data = &fighter;
    fighter.death1_cb = death1;
    ftKp_SpecialHi_Enter_inline(&object);
    check(fighter.take_dmg_cb != NULL && fighter.death2_cb != NULL);
    ftCommon_8007DB58(&object);
    check(count == 4 && calls[0] == 1 && calls[1] == 2 &&
          calls[2] == 3 && calls[3] == 4);
    fighter.death2_cb(&object);
    check(count == 5 && calls[4] == 3);
    puts("Koopa callback ABI and order passed");
    return 0;
}
