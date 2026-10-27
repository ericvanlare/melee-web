#include "gameplay_rng_draw_owner.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "stadium_registration_observer.h"
#include <sysdolphin/baselib/objalloc.h>
extern HSD_ObjAllocData gobj_alloc_data, gobjproc_alloc_data;
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#endif
#include <melee/ef/eflib.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gr/grpstadium.h>
#include <sysdolphin/baselib/gobj.h>
/* Only the full fighter source closure links these identities and tables. */
static int fighter_membership(const HSD_GObj* gobj, unsigned* slot,
                              unsigned* entity, unsigned* kind)
{
 const Fighter* fighter = gobj->user_data;
 if (!fighter || fighter->gobj != gobj || fighter->player_id >= 6) return 0;
 const StaticPlayer* player = Player_GetPtrForSlot(fighter->player_id);
 if (!player) return 0;
 _Static_assert(sizeof(player->player_entity) / sizeof(player->player_entity[0]) == 2,
                "Source player entity extent changed");
 unsigned matches = 0, ordinal = 0;
 for (unsigned i = 0; i < sizeof(player->player_entity) / sizeof(player->player_entity[0]); ++i) {
  if (player->player_entity[i] == gobj) { ++matches; ordinal = i; }
 }
 if (matches != 1) return 0;
 *slot = fighter->player_id; *entity = ordinal; *kind = fighter->kind;
 return 1;
}
void melee_web_rng_draw_probe_install_source_owners(void)
{
 static const MeleeWebRngDrawOwnerBindings bindings = {
  grStadium_801D1390, efLib_particles_proc_main, Fighter_8006A360, fighter_membership
 };
 melee_web_rng_draw_probe_bind_source_owners(&bindings);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
 melee_web_stadium_registration_bind(&bindings,&gobj_alloc_data,&gobjproc_alloc_data);
#endif
}

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int melee_web_stadium_registration_arm(void)
{
 melee_web_rng_draw_probe_install_source_owners();
 return melee_web_stadium_registration_arm_checked();
}
#endif
