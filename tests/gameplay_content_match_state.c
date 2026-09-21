#include "gameplay_compat.h"
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftDonkey/forward.h>
#include <melee/ft/kinds/ftKoopa/forward.h>
#include <melee/ft/kinds/ftCommon/forward.h>
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
        {CKIND_PIKACHU, FTKIND_PIKACHU, ICONHUD_PIKACHU},
        {CKIND_PICHU, FTKIND_PICHU, ICONHUD_PICHU},
        {CKIND_PURIN, FTKIND_PURIN, ICONHUD_PURIN},
        {CKIND_DONKEY, FTKIND_DONKEY, ICONHUD_DONKEY},
        {CKIND_KOOPA, FTKIND_KOOPA, ICONHUD_KOOPA},
        {CKIND_NESS, FTKIND_NESS, ICONHUD_NESS},
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
    if(fighter && kind==FTKIND_PURIN) {
        if((fighter->u.pr.x223C!=NULL)!=(costume!=0))return 0;
        if(costume && (!fighter->u.pr.x2240.data || fighter->u.pr.x2248.model_num!=1))return 0;
    }
    const float icon=identity->stock_icon+30*costume;
    return fighter&&fighter->kind==identity->fighter&&Player_GetPlayerCharacter(slot)==identity->character&&
        Player_GetCostumeId(slot)==costume&&gm_80168BF8(slot)==icon;
}
int melee_web_test_item_count(int kind){return it_8026B3C0((ItemKind)kind);}

int melee_web_test_purin_anim_id(int expected)
{
    HSD_GObj* entity=Player_GetEntity(0);
    if(!entity || !entity->user_data)return 0;
    Fighter* fighter=entity->user_data;
    return fighter->kind==FTKIND_PURIN && fighter->anim_id==expected;
}


/* Donkey cargo observation. The native fixture uses this
 * narrow source-owned relation check instead of writing a victim/state. */
int melee_web_test_donkey_cargo(unsigned phase, int expected_fighter_kind)
{
    HSD_GObj* entity = Player_GetEntity(0);
    if (!entity || !entity->user_data) return 0;
    Fighter* fighter = entity->user_data;
    if (fighter->kind != FTKIND_DONKEY || !fighter->victim_gobj ||
        !fighter->victim_gobj->user_data)
        return 0;
    Fighter* victim = fighter->victim_gobj->user_data;
    const int motion = fighter->motion_id;
    const int catch_wait = motion == ftCo_MS_CatchWait;
    const int cargo_wait = motion >= ftDk_MS_ThrowFWait0 &&
                           motion <= ftDk_MS_ThrowFWait2;
    const int cargo_walk = motion >= ftDk_MS_ThrowFWalkSlow &&
                           motion <= ftDk_MS_ThrowFWalkFast;
    const int cargo_throw = motion >= ftDk_MS_ThrowFF &&
                            motion <= ftDk_MS_ThrowAirFLw;
    if (victim->kind != (FighterKind) expected_fighter_kind) return 0;
    return phase == 0 ? cargo_wait : phase == 1 ? cargo_throw :
           phase == 2 ? cargo_walk : phase == 4 ? catch_wait :
           cargo_wait || cargo_throw || cargo_walk || catch_wait;
}

/* Koopa's side special keeps the victim in Fighter::victim_gobj while the
 * source common capture/throw rows run.  The observer only reads that link
 * and both source motion IDs; it never selects an action or repairs a stale
 * link. Modes are: 0 ground capture (278..280), 1 ground throw (281..282),
 * 2 air capture (283..285), and 3 air throw (286..287). */
int melee_web_test_koopa_capture(unsigned mode, int expected_fighter_kind)
{
    HSD_GObj* entity = Player_GetEntity(0);
    if (!entity || !entity->user_data) return 0;
    Fighter* fighter = entity->user_data;
    if (fighter->kind != FTKIND_KOOPA || !fighter->victim_gobj ||
        !fighter->victim_gobj->user_data)
        return 0;
    Fighter* victim = fighter->victim_gobj->user_data;
    if (victim->kind != (FighterKind) expected_fighter_kind) return 0;
    const int thrower_motion = fighter->motion_id;
    const int victim_motion = victim->motion_id;
    const int ground_capture =
        (thrower_motion >= ftKp_MS_SpecialSStart &&
         thrower_motion <= ftKp_MS_SpecialSHit0_1) &&
        (victim_motion == ftCo_MS_CaptureKoopa ||
         victim_motion == ftCo_MS_CaptureDamageKoopa ||
         victim_motion == ftCo_MS_CaptureWaitKoopa);
    const int ground_throw =
        (thrower_motion == ftKp_MS_SpecialSEndF ||
         thrower_motion == ftKp_MS_SpecialSEndB) &&
        (victim_motion == ftCo_MS_ThrownKoopaF ||
         victim_motion == ftCo_MS_ThrownKoopaB);
    const int air_capture =
        (thrower_motion >= ftKp_MS_SpecialAirSStart &&
         thrower_motion <= ftKp_MS_SpecialAirSHit0_1) &&
        (victim_motion == ftCo_MS_CaptureKoopaAir ||
         victim_motion == ftCo_MS_CaptureDamageKoopaAir ||
         victim_motion == ftCo_MS_CaptureWaitKoopaAir);
    const int air_throw =
        (thrower_motion == ftKp_MS_SpecialAirSEndF ||
         thrower_motion == ftKp_MS_SpecialAirSEndB) &&
        (victim_motion == ftCo_MS_ThrownKoopaAirF ||
         victim_motion == ftCo_MS_ThrownKoopaAirB);
    switch (mode) {
    case 0: return ground_capture;
    case 1: return ground_throw;
    case 2: return air_capture;
    case 3: return air_throw;
    default: return 0;
    }
}
