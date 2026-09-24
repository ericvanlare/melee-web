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
extern void melee_web_purin_exchange_hat_cache(void**);

static void purin_cache_set(void* const values[6])
{
    void* swapped[6];
    memcpy(swapped,values,sizeof(swapped));
    melee_web_purin_exchange_hat_cache(swapped);
}
static int purin_cache_is(void* const expected[6])
{
    void* observed[6]={0};
    melee_web_purin_exchange_hat_cache(observed);
    const int equal=!memcmp(observed,expected,sizeof(observed));
    melee_web_purin_exchange_hat_cache(observed);
    return equal;
}
static void purin_cache_capture(void* output[6])
{
    void* observed[6]={0};
    melee_web_purin_exchange_hat_cache(observed);
    memcpy(output,observed,sizeof(observed));
    melee_web_purin_exchange_hat_cache(observed);
}

int assets_test_purin_archive_contract(void* rows,void* blends,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind)
{
    static unsigned char cache_tokens[6];
    static unsigned char archive_token;
    ftData data={0};data.xC=rows;data.x10=blends;data.x5C=&test_joint;
    char error[160];
    ftData* saved_data=gFtDataList[FTKIND_PURIN];
    UnkCostumeStruct saved[5];
    memcpy(saved,CostumeListsForeachCharacter[FTKIND_PURIN].costume_list,sizeof(saved));
    void* original_cache[6]={0};
    purin_cache_capture(original_cache);
    void* prior_cache[6];
    for(unsigned i=0;i<6;++i)prior_cache[i]=&cache_tokens[i];
    purin_cache_set(prior_cache);
    void* archive=&archive_token;
    /* The neutral costume has no source hat archive; every hat costume has
     * one. The token is publication-only in this fixture: HSD lookup is
     * covered by the real Purin match loader. */
    if(melee_web_fighter_assets_begin(FTKIND_PURIN,0,&data,&test_joint,&test_material,
        archive,327,context,bind,unbind,error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_begin(FTKIND_PURIN,1,&data,&test_joint,&test_material,
        NULL,327,context,bind,unbind,error,sizeof(error)))return 0;
    MeleeWebFighterAssetScope* scope=melee_web_fighter_assets_begin(FTKIND_PURIN,0,
        &data,&test_joint,&test_material,NULL,327,context,bind,unbind,error,sizeof(error));
    if(!scope || gFtDataList[FTKIND_PURIN]!=&data || !purin_cache_is((void* const[6]){0}))return 0;
    if(!melee_web_fighter_assets_check_owned("purin-initial",error,sizeof(error)))return 0;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[0].x14_archive=archive;
    if(melee_web_fighter_assets_check_owned("purin-archive-mutated",error,sizeof(error)))return 0;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[0].x14_archive=NULL;
    for(unsigned costume=1;costume<5;++costume)
        if(!melee_web_fighter_assets_add_costume(scope,costume,&test_joint,&test_material,archive,error,sizeof(error)))return 0;
    if(!melee_web_fighter_assets_check_owned("purin-hats",error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_add_costume(scope,1,&test_joint,&test_material,archive,error,sizeof(error)))return 0;
    /* A duplicate scope is rejected before the Purin cache exchange and must
     * leave the cleared in-scope cache untouched. */
    if(melee_web_fighter_assets_begin(FTKIND_PURIN,0,&data,&test_joint,&test_material,
        NULL,327,context,bind,unbind,error,sizeof(error)))return 0;
    if(!purin_cache_is((void* const[6]){0}))return 0;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[2].x14_archive=NULL;
    if(melee_web_fighter_assets_check_owned("purin-hat-mutated",error,sizeof(error)))return 0;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[2].x14_archive=archive;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[3].x14_archive=NULL;
    if(melee_web_fighter_assets_end(scope,error,sizeof(error)))return 0;
    CostumeListsForeachCharacter[FTKIND_PURIN].costume_list[3].x14_archive=archive;
    if(!melee_web_fighter_assets_end(scope,error,sizeof(error)) || !purin_cache_is(prior_cache))return 0;
    if(gFtDataList[FTKIND_PURIN]!=saved_data ||
       memcmp(saved,CostumeListsForeachCharacter[FTKIND_PURIN].costume_list,sizeof(saved)))return 0;
    /* A second lifetime exercises cache clearing and exact six-pointer
     * restoration again, then returns the pre-test cache to the caller. */
    purin_cache_set(prior_cache);
    scope=melee_web_fighter_assets_begin(FTKIND_PURIN,0,&data,&test_joint,&test_material,
        NULL,327,context,bind,unbind,error,sizeof(error));
    if(!scope || !purin_cache_is((void* const[6]){0}) ||
       !melee_web_fighter_assets_end(scope,error,sizeof(error)) || !purin_cache_is(prior_cache))return 0;
    purin_cache_set(original_cache);
    return gFtDataList[FTKIND_PURIN]==saved_data &&
        !memcmp(saved,CostumeListsForeachCharacter[FTKIND_PURIN].costume_list,sizeof(saved));
}
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
    if(melee_web_fighter_assets_begin(FTKIND_MARIO,0,&data,&test_joint,NULL,NULL,303,context,bind,unbind,error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_begin(FTKIND_GANON,0,&data,&test_joint,&test_material,NULL,318,context,bind,unbind,error,sizeof(error)))return 0;
    MeleeWebFighterAssetScope* scope=melee_web_fighter_assets_begin(FTKIND_GANON,0,&data,&test_joint,NULL,NULL,318,context,bind,unbind,error,sizeof(error));
    if(!scope)return 0;
    if(melee_web_fighter_assets_add_costume(scope,1,&test_joint,&test_material,NULL,error,sizeof(error)))return 0;
    for(unsigned i=1;i<5;i++)if(!melee_web_fighter_assets_add_costume(scope,i,&test_joint,NULL,NULL,error,sizeof(error)))return 0;
    if(melee_web_fighter_assets_add_costume(scope,5,&test_joint,NULL,NULL,error,sizeof(error)))return 0;
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
    /* Item subroutine/goto commands store their relocated target in the
     * following command union. The target is not payload in the dispatch
     * word itself. */
    const uint32_t flow[]={(5U<<26),3,4U<<26,0};
    commands=melee_web_item_commands_create(flow,4);
    if(!commands || commands[0].Command_00.code!=5 ||
       commands[1].Command_05.ptr!=&commands[3]){
        if(commands)melee_web_item_commands_destroy(commands);
        return 0;
    }
    melee_web_item_commands_destroy(commands);
    /* it_8027978C advances over two operand unions even for an ignored
     * high sub-opcode. A following invalid opcode therefore remains after
     * the complete three-word command. */
    const uint32_t texture[]={(16U<<26)|(10U<<18),0x12345678,63U<<26,0};
    commands=melee_web_item_commands_create(texture,4);
    if(!commands || commands[3].Command_00.code!=0){
        if(commands)melee_web_item_commands_destroy(commands);
        return 0;
    }
    melee_web_item_commands_destroy(commands);
    return ok;
}
MeleeWebFighterAssetScope* assets_test_begin(void* rows,void* blends,void* waits,void* context,
    MeleeWebFighterAssetBind bind,MeleeWebFighterAssetUnbind unbind)
{
    test_data=(ftData){0};test_data.xC=rows;test_data.x10=blends;test_data.x24=waits;
    previous_data=gFtDataList[0];previous_costume=CostumeListsForeachCharacter[0].costume_list[0];
    if(melee_web_fighter_assets_begin(0,0,&test_data,&test_joint,&test_material,NULL,303,context,bind,unbind,NULL,0))abort();
    if(gFtDataList[0]!=previous_data)abort();
    test_data.x5C=&test_joint;
    return melee_web_fighter_assets_begin(0,0,&test_data,&test_joint,&test_material,NULL,303,context,bind,unbind,NULL,0);
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
