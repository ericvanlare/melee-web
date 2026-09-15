#ifndef MELEE_WEB_GAMEPLAY_HUD_H
#define MELEE_WEB_GAMEPLAY_HUD_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MeleeWebHud MeleeWebHud;
/* Requires the actual match, camera, stage and hydrated interface archives.
 * Runs original ifAll startup, Ready/Go transitions and player HUD creation. */
MeleeWebHud* melee_web_hud_begin(unsigned layout, char*, size_t);
/* The complete match prepares/starts original stage music after ifAll/flash
 * construction and before Ready/Go, as in fn_8016E730 + VS OnEnter. */
MeleeWebHud* melee_web_hud_begin_with_music(unsigned layout,
    int (*prepare_music)(void*, char*, size_t), void*, char*, size_t);
/* Source teardown must precede fighters, cameras and the SDK world. */
int melee_web_hud_end(MeleeWebHud*, char*, size_t);
int melee_web_hud_ready(const MeleeWebHud*);
/* Observation of the original damage HUD, not a separately computed overlay. */
int melee_web_hud_damage(const MeleeWebHud*, unsigned player);
#ifdef __cplusplus
}
#endif
#endif
