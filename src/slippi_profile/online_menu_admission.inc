/* SPDX-License-Identifier: GPL-3.0-or-later
 * Initial rollback-staging source-menu slice. Included only by gameplay_menu.c
 * when MELEE_WEB_ROLLBACK_STAGING is enabled. These predicates constrain the
 * original menu's existing admission callbacks; they do not write selection
 * state or synthesize input. */
#ifndef MELEE_WEB_SLIPPI_ONLINE_MENU_ADMISSION_INC
#define MELEE_WEB_SLIPPI_ONLINE_MENU_ADMISSION_INC

static int melee_web_online_menu_admission_character(int ckind)
{
    return ckind == CKIND_MARIO;
}

static int melee_web_online_menu_admission_stage(int stkind)
{
    return stkind == St_Kind_Last;
}

#endif
