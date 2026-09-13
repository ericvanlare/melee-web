#include "gameplay_compat.h"
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <melee/gm/gm_1601.h>
#include <melee/it/it_26B1.h>
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
int melee_web_test_content_player(unsigned slot,int kind,unsigned costume){
    HSD_GObj* entity=Player_GetEntity(slot);
    if(!entity)return 0;
    Fighter* fighter=entity->user_data;
    const int ckind=kind==FTKIND_FOX?CKIND_FOX:kind==FTKIND_FALCO?CKIND_FALCO:
        kind==FTKIND_MARS?CKIND_MARS:CKIND_MARIO;
    const float icon=(kind==FTKIND_FOX?2:kind==FTKIND_FALCO?19:
        kind==FTKIND_MARS?9:8)+30*costume;
    return fighter&&fighter->kind==kind&&Player_GetPlayerCharacter(slot)==ckind&&
        Player_GetCostumeId(slot)==costume&&gm_80168BF8(slot)==icon;
}
int melee_web_test_item_count(int kind){return it_8026B3C0((ItemKind)kind);}
