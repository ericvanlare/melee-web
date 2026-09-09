#include "gameplay_fighter_assets.h"
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
