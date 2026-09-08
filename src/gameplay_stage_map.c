#include "gameplay_stage_map.h"
#include "gameplay_stage_map_build.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <melee/gr/ground.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <stdio.h>
#include <stdlib.h>
struct MeleeWebStageMap {UnkArchiveStruct archives[4];uint64_t generation;const MeleeWebMapLightOverride* overrides;size_t override_count;};
static MeleeWebStageMap* owner;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
MeleeWebStageMap* melee_web_stage_map_publish(void* input,char* e,size_t n){
    UnkStageDat* map=input;
    if(owner||!map||map->unkC<=0||map->unkC>256||!map->unk8){
        fail(e,n,"Native map publication requires an unowned checked entry table");return NULL;
    }
    if(!melee_web_native_world_enable(e,n))return NULL;
    MeleeWebGameplayStats stats=melee_web_gameplay_stats();
    MeleeWebStageMap* h=calloc(1,sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate native map publication");return NULL;}
    h->generation=stats.generation;h->archives[0].unk4=map;owner=h;ok(e,n);return h;
}
void* melee_web_stage_map_archives(void){return owner?owner->archives:NULL;}
void* melee_web_stage_map_lookup(int id){
    if(!owner||id<0)return NULL;
    UnkStageDat* m=owner->archives[0].unk4;
    return id<m->unkC&&m->unk8[id].unk0?&owner->archives[0]:NULL;
}
int melee_web_stage_map_close(MeleeWebStageMap* h,char* e,size_t n){
    if(!h)return ok(e,n);
    if(h!=owner||h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Native map publication lost its source world ownership");
    for(unsigned i=0;i<sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]);i++)
        if(stage_info.map_gobjs[i])return fail(e,n,"Remove original stage GObjs before closing native map descriptors");
    owner=NULL;free(h);return ok(e,n);
}

void* melee_web_stage_map_light_list(const MeleeWebNativeDat* r,HSD_LightDesc* light,HSD_LightAnim** anims){
    LightList* d=r->allocate(r->context,1,sizeof(*d));
    d->desc=light;d->anims=anims;return d;
}
void* melee_web_stage_map_build(const MeleeWebNativeDat* r,const MeleeWebMapInput* input){
    UnkStageDat* d=r->allocate(r->context,1,sizeof(*d));
    d->unk0=input->unk0;d->unk4=input->unk4;d->unkC=input->unkC;
    d->unk8=r->allocate(r->context,d->unkC,sizeof(*d->unk8));
    for(int i=0;i<d->unkC;i++){
        const MeleeWebMapEntryInput* a=&input->unk8[i];struct UnkStageDat_x8_t* b=&d->unk8[i];
        b->unk0=a->unk0;b->unk4=a->unk4;b->unk8=a->unk8;b->unkC=a->unkC;
        b->x10=a->x10;b->x14=a->x14;b->x18=(LightList**)a->x18;b->x1C=a->x1C;
        b->unk24=a->unk24;
        if(b->unk24){
            b->unk20=r->allocate(r->context,b->unk24,sizeof(*b->unk20));
            for(int j=0;j<b->unk24;j++){
                b->unk20[j].x=a->unk20[3*j];b->unk20[j].y=a->unk20[3*j+1];b->unk20[j].z=a->unk20[3*j+2];
            }
        }
        b->x28=a->x28;b->x2C=a->x2C;b->x30=a->x30;
    }
    d->unk10=input->unk10;d->unk14=input->unk14;d->unk18=input->unk18;d->unk1C=input->unk1C;
    d->unk24=input->unk24;
    d->unk20=r->allocate(r->context,d->unk24,sizeof(*d->unk20));
    for(int i=0;i<d->unk24;i++){d->unk20[i].unk0=input->unk20[i].unk0;d->unk20[i].flag=input->unk20[i].flag!=0;}
    d->unk28=(UnkStageDatInternal**)input->unk28;d->unk2C=input->unk2C;return d;
}

int melee_web_stage_map_set_overrides(MeleeWebStageMap* h,const MeleeWebMapLightOverride* entries,size_t count,char* e,size_t n){
    if(!h||h!=owner||h->overrides||!entries||!count||count>256)
        return fail(e,n,"Native map light identity table missing or already published");
    for(size_t i=0;i<count;i++){
        if(!entries[i].descriptor||(entries[i].found!=0&&entries[i].found!=1)||(entries[i].flags&~0xe0)||(!entries[i].found&&entries[i].flags))
            return fail(e,n,"Native map light override is invalid");
        for(size_t j=0;j<i;j++)if(entries[i].descriptor==entries[j].descriptor)return fail(e,n,"Native map light identity duplicated");
    }
    h->overrides=entries;h->override_count=count;return ok(e,n);
}
int melee_web_stage_map_lookup_override(void* descriptor,int* found,uint8_t* flags){
    if(!owner)return 0;
    for(size_t i=0;i<owner->override_count;i++)if(owner->overrides[i].descriptor==descriptor){
        if(!found||!flags)abort();*found=owner->overrides[i].found;*flags=owner->overrides[i].flags;return 1;
    }
    fputs("Native stage light query names an unowned descriptor\n",stderr);abort();
}
