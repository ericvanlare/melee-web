#include "gameplay_hud.h"
#include "gameplay_bootstrap.h"
#include <melee/gm/gm_16AE.h>
#include <melee/gm/types.h>
#include <melee/if/ifall.h>
#include <melee/if/if_2F6E.h>
#include <melee/if/ifstatus.h>
#include <melee/if/iftime.h>
#include <melee/pl/player.h>
#include <melee/lb/lblanguage.h>
#include <sysdolphin/baselib/sislib.h>
#include <stdio.h>
#include <stdlib.h>

extern int melee_web_pause_screen_begin(void);
extern int melee_web_pause_screen_end(void);
struct MeleeWebHud {
    uint64_t generation;
    HSD_Archive* previous_archive;
    int previous_language, previous_saved_language, pause_owned;
};
static MeleeWebHud* owner;
static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}
/* The original interface invokes both callbacks with an int. Keep this ABI
 * explicit even when the recovered game function ignores that argument. */
static void intro_finished(int unused)
{
    (void) unused;
    fn_8016B7F8();
}
MeleeWebHud* melee_web_hud_begin(unsigned layout, char* error, size_t size)
{
    const uint64_t generation = melee_web_gameplay_stats().generation;
    if (owner || !generation || layout < 1 || layout > 6 || !Player_GetEntity(0) || !Player_GetEntity(1)) {
        fail(error, size, "Original HUD requires the owned two-player match");
        return NULL;
    }
    MeleeWebHud* hud = calloc(1, sizeof(*hud));
    if (!hud) {
        fail(error, size, "Cannot allocate original HUD lifetime");
        return NULL;
    }
    hud->generation = generation;
    hud->previous_archive = *ifAll_GetArchive();
    hud->previous_language = lbLang_GetLanguageSetting();
    hud->previous_saved_language = lbLang_GetSavedLanguage();
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    owner = hud;
    /* gm_1A3F's original default gameplay scene preparation size. */
    HSD_SisLib_803A6048(0x4800);
    ifAll_802F390C();
    ifStatus_802F6EA4(3, -1, -1, 0, (Event) fn_8016B7B4,
                    (Event) intro_finished);
    ifTime_CreateTimers();
    if (!melee_web_pause_screen_begin()) {
        fail(error, size, "Original pause-screen ownership is unavailable");
        melee_web_hud_end(hud, NULL, 0);
        return NULL;
    }
    hud->pause_owned = 1;
    /* x0_3 is the original HUD layout choice. It is not inferred from the
     * number of active players; ordinary VS defaults to the four-slot layout. */
    ifStatus_802F665C(layout);
    if (error && size) *error = 0;
    return hud;
}
int melee_web_hud_ready(const MeleeWebHud* hud)
{
    return hud && hud == owner &&
        hud->generation == melee_web_gameplay_stats().generation &&
        gm_16AE_GetUnkData_0()->hud_enabled;
}
int melee_web_hud_damage(const MeleeWebHud* hud, unsigned player)
{
    if (!hud || hud != owner || player >= 2 ||
        hud->generation != melee_web_gameplay_stats().generation) return -1;
    const IfDamageState* state = &ifStatus_GetHUDInfo()->players[player];
    return state->HUD_parent_entity ? state->damage_percent : -1;
}
int melee_web_hud_end(MeleeWebHud* hud, char* error, size_t size)
{
    if (!hud) return 1;
    if (owner != hud || hud->generation != melee_web_gameplay_stats().generation)
        return fail(error, size, "Original HUD ownership changed");
    if (hud->pause_owned && !melee_web_pause_screen_end())
        return fail(error, size, "Original pause-screen ownership changed");
    hud->pause_owned = 0;
    ifAll_802F3A64();
    HSD_SisLib_803A5FBC();
    *ifAll_GetArchive() = hud->previous_archive;
    lbLang_SetLanguageSetting(hud->previous_language);
    lbLang_SetSavedLanguage(hud->previous_saved_language);
    owner = NULL;
    free(hud);
    if (error && size) *error = 0;
    return 1;
}
