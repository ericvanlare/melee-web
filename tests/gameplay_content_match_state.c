#include "gameplay_compat.h"
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <melee/gm/gm_1601.h>
#include <melee/it/it_26B1.h>
#include <melee/mn/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include "gameplay_bootstrap.h"
#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <math.h>
extern void* melee_web_camera_state(void);
int melee_web_test_quake_start(int variant){
    const unsigned before=melee_web_gameplay_stats().objects;
    Camera_80030E44(variant,NULL);
    return melee_web_gameplay_stats().objects>before ||
        (variant==1 && ((Camera*)melee_web_camera_state())->xA0!=NULL);
}
int melee_web_test_quake_translated(void){
    Camera* camera=melee_web_camera_state();
    return isfinite(camera->translation.x)&&isfinite(camera->translation.y)&&
        (camera->translation.x!=0.0f||camera->translation.y!=0.0f);
}
/* Keep this table in source terms.  In particular, Dr. Mario and Roy do not
 * reuse Mario/Marth's CharacterKind just because their fighter routines share
 * an ABI.  ICONHUD_* is the source stock-icon base used by gm_80168B34. */
typedef struct MeleeWebSourceIdentity {
    CharacterKind character;
    FighterKind fighter;
    int stock_icon;
} MeleeWebSourceIdentity;

static const MeleeWebSourceIdentity* melee_web_source_identity(int ckind){
    static const MeleeWebSourceIdentity rows[] = {
        {CKIND_MARIO, FTKIND_MARIO, ICONHUD_MARIO},
        {CKIND_FOX, FTKIND_FOX, ICONHUD_FOX},
        {CKIND_FALCO, FTKIND_FALCO, ICONHUD_FALCO},
        {CKIND_MARS, FTKIND_MARS, ICONHUD_MARS},
        {CKIND_DRMARIO, FTKIND_DRMARIO, ICONHUD_DRMARIO},
        {CKIND_EMBLEM, FTKIND_EMBLEM, ICONHUD_EMBLEM},
        {CKIND_LINK, FTKIND_LINK, ICONHUD_LINK},
        {CKIND_CLINK, FTKIND_CLINK, ICONHUD_CLINK},
        {CKIND_CAPTAIN, FTKIND_CAPTAIN, ICONHUD_CAPTAIN},
        {CKIND_GANON, FTKIND_GANON, ICONHUD_GANON},
        {CKIND_LUIGI, FTKIND_LUIGI, ICONHUD_LUIGI},
    };
    for (unsigned i=0;i<sizeof(rows)/sizeof(rows[0]);++i)
        if (rows[i].character==(CharacterKind)ckind) return &rows[i];
    return NULL;
}

int melee_web_test_content_player(unsigned slot,int ckind,int kind,unsigned costume){
    HSD_GObj* entity=Player_GetEntity(slot);
    if(!entity)return 0;
    Fighter* fighter=entity->user_data;
    const MeleeWebSourceIdentity* identity=melee_web_source_identity(ckind);
    if(!identity || identity->fighter!=(FighterKind)kind)return 0;
    const float icon=identity->stock_icon+30*costume;
    return fighter&&fighter->kind==identity->fighter&&Player_GetPlayerCharacter(slot)==identity->character&&
        Player_GetCostumeId(slot)==costume&&gm_80168BF8(slot)==icon;
}
int melee_web_test_item_count(int kind){return it_8026B3C0((ItemKind)kind);}
