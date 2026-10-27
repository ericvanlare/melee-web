#include "gameplay_rng_draw_owner.h"
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
}
