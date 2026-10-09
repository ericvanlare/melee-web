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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include <melee/if/if_2F72.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#endif
#include <stdio.h>
#include <stdlib.h>

extern int melee_web_pause_screen_begin(void);
extern int melee_web_pause_screen_end(void);
extern int melee_web_bg_flash_begin(void);
extern int melee_web_bg_flash_end(void);
struct MeleeWebHud {
    uint64_t generation;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    HSD_GObj* ready_object;
    HSD_GObjProc* ready_proc;
#endif
    HSD_Archive* previous_archive;
    int previous_language, previous_saved_language, pause_owned, flash_owned;
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
int melee_web_hud_stadium_ready_context(void* object,void* proc)
{
    const Element_803F9628* row=&ifStatus_803F9628[3];
    return owner && owner->generation==melee_web_gameplay_stats().generation &&
        owner->ready_object && owner->ready_proc && !HSD_GObj_804D7814 &&
        row->x0==owner->ready_object && row->x8==if_802F73C4 &&
        row->x1C==intro_finished && HSD_GObj_804D781C==owner->ready_object &&
        HSD_GObj_804D7838==owner->ready_proc &&
        owner->ready_object->proc==owner->ready_proc && !owner->ready_proc->child &&
        owner->ready_proc->gobj==owner->ready_object &&
        owner->ready_proc->on_invoke==if_802F73C4 &&
        (!object || object==owner->ready_object) && (!proc || proc==owner->ready_proc);
}
int melee_web_hud_stadium_ready_snapshot(void** object,void** proc)
{
    if(!object || !proc || !melee_web_hud_stadium_ready_context(NULL,NULL))return 0;
    *object=owner->ready_object;*proc=owner->ready_proc;return 1;
}
#endif
static MeleeWebHud* melee_web_hud_begin_source_status(unsigned,
    int (*)(void*, char*, size_t), void*, int, int, char*, size_t);
MeleeWebHud* melee_web_hud_begin(unsigned layout, char* error, size_t size)
{
    return melee_web_hud_begin_with_music(layout, NULL, NULL, error, size);
}
MeleeWebHud* melee_web_hud_begin_with_music(unsigned layout,
    int (*prepare_music)(void*, char*, size_t), void* context,
    char* error, size_t size)
{
    return melee_web_hud_begin_source_status(layout,prepare_music,context,3,
                                              1,error,size);
}
MeleeWebHud* melee_web_hud_begin_sudden_death_with_music(unsigned layout,
    int (*prepare_music)(void*, char*, size_t), void* context,
    char* error, size_t size)
{
    return melee_web_hud_begin_source_status(layout,prepare_music,context,1,
                                              1,error,size);
}
static MeleeWebHud* melee_web_hud_begin_source_status(unsigned layout,
    int (*prepare_music)(void*, char*, size_t), void* context,
    int source_status, int sparse_source_players,
    char* error, size_t size)
{
    const uint64_t generation = melee_web_gameplay_stats().generation;
    unsigned active_players=0;
    if(sparse_source_players){
        for(unsigned slot=0;slot<4;++slot)
            if(Player_GetEntity(slot))++active_players;
    }
    if (owner || !generation || layout < 1 || layout > 6 ||
        (sparse_source_players?active_players<2:
         (!Player_GetEntity(0)||!Player_GetEntity(1)))) {
        fail(error, size, "Original HUD requires the owned source participants");
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
    ifAll_802F390C();
    /* fn_8016E730 creates the authored screen-flash system after ifAll. */
    if (!melee_web_bg_flash_begin()) {
        fail(error, size, "Original screen-flash ownership is unavailable");
        melee_web_hud_end(hud, NULL, 0);
        return NULL;
    }
    hud->flash_owned = 1;
    if (prepare_music && !prepare_music(context, error, size)) {
        melee_web_hud_end(hud, NULL, 0);
        return NULL;
    }
    ifStatus_802F6EA4(source_status, -1, -1, 0, (Event) fn_8016B7B4,
                    (Event) intro_finished);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(source_status==3){
        hud->ready_object=ifStatus_803F9628[3].x0;
        hud->ready_proc=hud->ready_object?hud->ready_object->proc:NULL;
    }
#endif
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
    if (!hud || hud != owner || player >= 4 ||
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
    if (hud->flash_owned && !melee_web_bg_flash_end())
        return fail(error, size, "Original screen-flash ownership changed");
    hud->flash_owned = 0;
    ifAll_802F3A64();
    *ifAll_GetArchive() = hud->previous_archive;
    lbLang_SetLanguageSetting(hud->previous_language);
    lbLang_SetSavedLanguage(hud->previous_saved_language);
    owner = NULL;
    free(hud);
    if (error && size) *error = 0;
    return 1;
}
