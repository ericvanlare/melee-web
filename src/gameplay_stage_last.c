#include "gameplay_stage_last.h"
#include "gameplay_stage_map.h"
#include "gameplay_effect_runtime.h"
#include "gameplay_bootstrap.h"
#include <melee/gr/ground.h>
#include <melee/gr/grlast.h>
#include <melee/gr/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
extern int melee_web_ground_map_storage_begin(void);
extern int melee_web_ground_map_storage_end(void);
extern void* melee_web_grlast_exchange_yakumono(void*);
struct MeleeWebStageLast {
    StageInfo saved;
    struct ftDeviceUnk3 device1[1],device3[1];
    struct ftDeviceUnk5 device2[2];
    struct ftDeviceUnk4 device4;
    int count1,count2;
    void* yaku;
    HSD_GObj* manager;
    uint64_t generation;
};
static MeleeWebStageLast* active;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
MeleeWebStageLast* melee_web_stage_last_begin(void* yaku,MeleeWebEffectBank* map_bank,char* e,size_t n){
 MeleeWebEffectBankStats bank;
 if(!melee_web_effect_bank_stats(map_bank,&bank,e,n)||bank.bank!=64||!bank.particle_bank_ready){fail(e,n,"FD requires its actual registered stage particle bank64");return NULL;}
 if(active||!yaku||!melee_web_effect_runtime_active()||!melee_web_stage_map_archives()||!stage_info.param||stage_info.grkind!=Gr_Kind_Last){fail(e,n,"Full FD requires original effects and published native map/numeric stage contexts");return NULL;}
 for(unsigned i=0;i<sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]);i++)if(stage_info.map_gobjs[i]){fail(e,n,"Full FD requires an empty source stage object registry");return NULL;}
 MeleeWebStageLast* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate FD ownership scope");return NULL;}
 h->saved=stage_info;h->generation=melee_web_gameplay_stats().generation;
 memcpy(h->device1,ft_80459A68,sizeof(h->device1));memcpy(h->device2,ftDevice_BuryThings,sizeof(h->device2));memcpy(h->device3,ft_80459A8C,sizeof(h->device3));h->device4=ft_804D6578;h->count1=ft_804D6570;h->count2=ftDevice_BuryThingCount;
 if(!melee_web_ground_map_storage_begin()){free(h);fail(e,n,"Original Ground collision-state storage is already owned");return NULL;}
 h->yaku=melee_web_grlast_exchange_yakumono(yaku);stage_info.yakumono_param=yaku;active=h;
 grNLa_StageData.on_init();
 for(int i=0;i<9;i++)if(!stage_info.map_gobjs[i]){melee_web_stage_last_end(h,NULL,0);fail(e,n,"Original FD initializer did not create every initial stage object");return NULL;}
 StageIdPair pair={Gr_Kind_Last,St_Kind_Last};
 Ground_801C0FB8(&pair);
 /* Ground startup creates one source scheduler owner on link5. */
 for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next){
  if(obj->classifier==HSD_GOBJ_CLASS_STAGE&&!obj->user_data){
   if(h->manager){fail(e,n,"Ambiguous original Ground scheduler ownership");return NULL;}
   h->manager=obj;
  }
 }
 if(!h->manager){fail(e,n,"Original Ground startup did not create its scheduler");return NULL;}
 ok(e,n);return h;
}
int melee_web_stage_last_end(MeleeWebStageLast* h,char* e,size_t n){
 if(!h)return ok(e,n);
 if(h!=active||h->generation!=melee_web_gameplay_stats().generation)return fail(e,n,"FD scope lost its original world ownership");
 if(HSD_GObj_804D781C||HSD_GObj_804D7814)return fail(e,n,"FD teardown must run outside source object callbacks");
 if(h->manager){HSD_GObjPLink_80390228(h->manager);h->manager=NULL;}
 for(int i=(int)(sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]))-1;i>=0;i--)if(stage_info.map_gobjs[i])Ground_801C4A08(stage_info.map_gobjs[i]);
 if(!melee_web_ground_map_storage_end())return fail(e,n,"Original stage objects remain during collision-state release");
 melee_web_grlast_exchange_yakumono(h->yaku);
 memcpy(ft_80459A68,h->device1,sizeof(h->device1));memcpy(ftDevice_BuryThings,h->device2,sizeof(h->device2));memcpy(ft_80459A8C,h->device3,sizeof(h->device3));ft_804D6578=h->device4;ft_804D6570=h->count1;ftDevice_BuryThingCount=h->count2;
 stage_info=h->saved;active=NULL;free(h);return ok(e,n);
}
