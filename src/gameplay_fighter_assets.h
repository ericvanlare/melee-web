#ifndef MELEE_WEB_GAMEPLAY_FIGHTER_ASSETS_H
#define MELEE_WEB_GAMEPLAY_FIGHTER_ASSETS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct Fighter;
typedef struct MeleeWebFighterAssetScope MeleeWebFighterAssetScope;
typedef int (*MeleeWebFighterAssetBind)(void*,struct Fighter*,void** actions,void** blends,char*,size_t);
typedef void (*MeleeWebFighterAssetUnbind)(void*,struct Fighter*);
/* Publish one decoded Mario costume and ftData after common initialization.
 * Source globals are restored only after every bound Fighter has unloaded. */
MeleeWebFighterAssetScope* melee_web_fighter_assets_begin(uint32_t kind,uint32_t costume,
    void* data,void* joint,void* material_animation,uint32_t motion_count,void* context,
    MeleeWebFighterAssetBind,MeleeWebFighterAssetUnbind,char*,size_t);
/* Add another costume of the same decoded kind before creating Fighters.
 * Shares ftData and action ownership while retaining distinct native models. */
int melee_web_fighter_assets_add_costume(MeleeWebFighterAssetScope*,uint32_t costume,
    void* joint,void* material_animation,char*,size_t);
int melee_web_fighter_assets_end(MeleeWebFighterAssetScope*,char*,size_t);
uint32_t melee_web_fighter_assets_live(const MeleeWebFighterAssetScope*);
/* Storage hooks for the original constructor/loader/unload call sites. */
void melee_web_fighter_assets_require_kind(uint32_t kind);
void melee_web_fighter_assets_require_costume(uint32_t kind,int costume);
void melee_web_fighter_assets_bind_created(struct Fighter*);
void melee_web_fighter_assets_unbind_destroying(struct Fighter*);
#ifdef __cplusplus
}
#endif
#endif
