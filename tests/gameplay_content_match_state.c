#include "gameplay_compat.h"
#include <melee/pl/player.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdata.h>
#include <melee/gm/gm_1601.h>
#include <melee/it/it_26B1.h>
#include <sysdolphin/baselib/gobj.h>
int melee_web_test_content_player(unsigned slot,int kind,unsigned costume){
    HSD_GObj* entity=Player_GetEntity(slot);
    if(!entity)return 0;
    Fighter* fighter=entity->user_data;
    const int ckind=kind==FTKIND_FOX?CKIND_FOX:kind==FTKIND_FALCO?CKIND_FALCO:CKIND_MARIO;
    const float icon=(kind==FTKIND_FOX?2:kind==FTKIND_FALCO?19:8)+30*costume;
    return fighter&&fighter->kind==kind&&Player_GetPlayerCharacter(slot)==ckind&&
        Player_GetCostumeId(slot)==costume&&gm_80168BF8(slot)==icon;
}
int melee_web_test_item_count(int kind){return it_8026B3C0((ItemKind)kind);}
