#include "gameplay_fighter_assets.h"
#include "dat_item_commands.h"
#include <melee/ft/ftdata.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <stdlib.h>
#include <string.h>
static ftData test_data;
static HSD_Joint test_joint;
static HSD_MatAnimJoint test_material;
static ftData* previous_data;
static UnkCostumeStruct previous_costume;
int assets_test_nullable_material(void* rows,void* blends,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind)
{
    ftData data={0};data.xC=rows;data.x10=blends;data.x5C=&test_joint;
    char error[160];
    ftData* saved_data=gFtDataList[FTKIND_GANON];
    UnkCostumeStruct saved[5];
    memcpy(saved,CostumeListsForeachCharacter[FTKIND_GANON].costume_list,sizeof(saved));
    /* Required Mario material cannot be omitted; authored Ganon null cannot
     * be replaced by a fabricated descriptor. No gameplay is run here. */
    if(melee_web_fighter_assets_begin(FTKIND_MARIO,0,&data,&test_joint,NULL,303,context,bind,unbind,error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_begin(FTKIND_GANON,0,&data,&test_joint,&test_material,318,context,bind,unbind,error,sizeof(error)))return 0;
    MeleeWebFighterAssetScope* scope=melee_web_fighter_assets_begin(FTKIND_GANON,0,&data,&test_joint,NULL,318,context,bind,unbind,error,sizeof(error));
    if(!scope)return 0;
    if(melee_web_fighter_assets_add_costume(scope,1,&test_joint,&test_material,error,sizeof(error)))return 0;
    for(unsigned i=1;i<5;i++)if(!melee_web_fighter_assets_add_costume(scope,i,&test_joint,NULL,error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_add_costume(scope,5,&test_joint,NULL,error,sizeof(error)))return 0;
    for(unsigned i=0;i<5;i++)if(CostumeListsForeachCharacter[FTKIND_GANON].costume_list[i].x4!=NULL)return 0;
    return melee_web_fighter_assets_end(scope,error,sizeof(error)) &&
        gFtDataList[FTKIND_GANON]==saved_data &&
        !memcmp(saved,CostumeListsForeachCharacter[FTKIND_GANON].costume_list,sizeof(saved));
}
int assets_test_item_commands(void)
{
    const uint32_t words[]={ (3U<<26)|5, (13U<<26)|(2U<<23)|384, (1U<<26)|2, 4U<<26, 0 };
    union CmdUnion* commands=melee_web_item_commands_create(words,5);
    if(!commands)return 0;
    const int ok=commands[0].Command_03.value==5 &&
        commands[1].set_hitbox_scale.opcode==13 && commands[1].set_hitbox_scale.idx==2 &&
        commands[1].set_hitbox_scale.value==384 && commands[2].Command_00.value==2 &&
        commands[3].Command_00.code==4;
    melee_web_item_commands_destroy(commands);
    const uint32_t invalid[]={(13U<<26)|(4U<<23),0};
    commands=melee_web_item_commands_create(invalid,2);
    if(commands){melee_web_item_commands_destroy(commands);return 0;}
    return ok;
}
MeleeWebFighterAssetScope* assets_test_begin(void* rows,void* blends,void* waits,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind)
{
    test_data=(ftData){0};test_data.xC=rows;test_data.x10=blends;test_data.x24=waits;
    previous_data=gFtDataList[0];previous_costume=CostumeListsForeachCharacter[0].costume_list[0];
    if(melee_web_fighter_assets_begin(0,0,&test_data,&test_joint,&test_material,303,context,bind,unbind,NULL,0))abort();
    if(gFtDataList[0]!=previous_data)abort();
    test_data.x5C=&test_joint;
    return melee_web_fighter_assets_begin(0,0,&test_data,&test_joint,&test_material,303,context,bind,unbind,NULL,0);
}
int assets_test_restored(void)
{
    return gFtDataList[0]==previous_data &&
        memcmp(&CostumeListsForeachCharacter[0].costume_list[0],&previous_costume,sizeof(previous_costume))==0;
}
Fighter* assets_test_construct_storage(void)
{
    Fighter* fp=calloc(1,sizeof(*fp));if(!fp)abort();
    fp->kind=0;fp->ft_data=gFtDataList[0];fp->x619_costume_id=0;
    fp->x24=fp->ft_data->xC;fp->x28=fp->ft_data->x10;
    ftData_80085B10(fp);
    if(fp->x58C!=303 || fp->x59C || fp->x5A0)abort();
    return fp;
}
int assets_test_load(Fighter* fp,int id)
{
    ftData_80085CD8(fp,fp,id);ftData_80085E50(fp,6);
    return fp->x590 && fp->x590==fp->x598 && fp->x5A4==fp->x5A8 &&
        fp->x5A4==(void*)(uintptr_t)fp->x24[2].x14;
}
void assets_test_destroy_storage(Fighter* fp)
{
    melee_web_fighter_assets_unbind_destroying(fp);
    if(fp->x590 || fp->x598 || fp->x24 || fp->x28)abort();
    free(fp);
}
