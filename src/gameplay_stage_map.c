#include "gameplay_stage_map.h"
#include "gameplay_stage_map_build.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include <melee/gr/ground.h>
#include <melee/gr/granime.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebStageMap {
    UnkArchiveStruct archives[4];
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    UnkArchiveStruct grdatfiles_rows[4];
    uint64_t grdatfiles_identities[4];
    uint64_t grdatfiles_native_identity;
    int grdatfiles_tracking,grdatfiles_begin_seen,grdatfiles_captured,grdatfiles_retired;
#endif
    uint64_t generation;
    const MeleeWebMapLightOverride* overrides;
    size_t override_count;
    MeleeWebArchiveSections* sections;
};
static MeleeWebStageMap* owner;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
extern UnkArchiveStruct* melee_web_grdatfiles_ordinary_table(void);
extern int melee_web_grdatfiles_retire_captured(const void*,const uint64_t[4]);
#endif
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
static int grdatfiles_row_zero(const UnkArchiveStruct* row){return row&&!row->unk0&&!row->unk4&&!row->unk8;}
static int grdatfiles_table_zero(const UnkArchiveStruct* rows){if(!rows)return 0;for(unsigned i=0;i<4;i++)if(!grdatfiles_row_zero(&rows[i]))return 0;return 1;}
static int grdatfiles_same_row(const UnkArchiveStruct* a,const UnkArchiveStruct* b){return a->unk0==b->unk0&&a->unk4==b->unk4&&a->unk8==b->unk8;}
static int grdatfiles_source_map_matches(MeleeWebStageMap* h,const UnkArchiveStruct* row,uint64_t expected,uint64_t* actual,uint64_t expected_native,uint64_t* actual_native){
    return h&&h==owner&&h->sections&&h->generation&&row&&
        row->unk4==h->archives[0].unk4&&
        melee_web_archive_sections_preloaded_stage_map_matches(
            h->sections,h->generation,row->unk0,h->archives[0].unk0,
            h->archives[0].unk4,expected,actual,expected_native,actual_native);
}
static int grdatfiles_preflight(MeleeWebStageMap* h,const UnkArchiveStruct* rows,char* e,size_t n){
    uint64_t identities[4]={0,0,0,0},native_identity=0;unsigned populated=0;
    if(!h||h!=owner||!rows||!h->grdatfiles_begin_seen||!h->sections||!h->grdatfiles_native_identity||
       h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Original grDatFiles retirement lost its StageMap owner");
    for(unsigned i=0;i<4;i++){
        if(!grdatfiles_same_row(&rows[i],&h->grdatfiles_rows[i]))
            return fail(e,n,"Original grDatFiles row changed after E8 capture");
        if(grdatfiles_row_zero(&rows[i])){
            if(h->grdatfiles_identities[i])return fail(e,n,"Captured empty grDatFiles row has an archive identity");
            continue;
        }
        if(!rows[i].unk0||!rows[i].unk4||rows[i].unk8!=0||
           (uintptr_t)rows[i].unk0==UINTPTR_MAX)
            return fail(e,n,"Original grDatFiles row is not an ordinary preload");
        if(!grdatfiles_source_map_matches(h,&rows[i],h->grdatfiles_identities[i],&identities[i],
            h->grdatfiles_native_identity,&native_identity)||
           (!identities[i]||!native_identity))
            return fail(e,n,"Original grDatFiles preload ownership or incarnation changed");
        for(unsigned j=0;j<i;j++)if(identities[j]&&identities[j]==identities[i])
            return fail(e,n,"Original grDatFiles rows alias one preload handle");
        populated++;
    }
    if(!populated)return fail(e,n,"Original E8 capture contains no owned grDatFiles preload");
    /* The later close_owned call has its own open-handle refusal. Check that
     * same condition before releasing any captured preload so a foreign open
     * handle cannot leave this four-slot table half-retired. */
    if(!melee_web_archive_sections_close_owned_preflight(
           h->sections,h->archives[0].unk0))
        return fail(e,n,"Original StageMap archive scope has another open handle");
    return 1;
}
int melee_web_stage_map_grdatfiles_begin(void){
    MeleeWebStageMap* h=owner;
    UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    if(!h||!h->grdatfiles_tracking)return 1;
    if(h->generation!=melee_web_gameplay_stats().generation||!h->sections||
       h->grdatfiles_begin_seen||h->grdatfiles_captured||!grdatfiles_table_zero(rows))return 0;
    h->grdatfiles_begin_seen=1;
    return 1;
}
int melee_web_stage_map_grdatfiles_capture(void){
    MeleeWebStageMap* h=owner;
    UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    uint64_t identities[4]={0,0,0,0},native_identity=0;unsigned populated=0;
    if(!h||!h->grdatfiles_tracking)return 1;
    if(h->generation!=melee_web_gameplay_stats().generation||!h->sections||
       !h->grdatfiles_begin_seen||h->grdatfiles_captured||!rows)return 0;
    for(unsigned i=0;i<4;i++){
        if(grdatfiles_row_zero(&rows[i]))continue;
        if(!rows[i].unk0||!rows[i].unk4||rows[i].unk8!=0||
           (uintptr_t)rows[i].unk0==UINTPTR_MAX||
           !grdatfiles_source_map_matches(h,&rows[i],0,&identities[i],native_identity,&native_identity)||
           !identities[i]||!native_identity)return 0;
        for(unsigned j=0;j<i;j++)if(identities[j]&&identities[j]==identities[i])return 0;
        populated++;
    }
    if(!populated)return 0;
    for(unsigned i=0;i<4;i++){
        h->grdatfiles_rows[i]=rows[i];
        h->grdatfiles_identities[i]=identities[i];
    }
    h->grdatfiles_native_identity=native_identity;
    h->grdatfiles_captured=1;
    return 1;
}
static int grdatfiles_retire(MeleeWebStageMap* h,char* e,size_t n){
    if(!h->grdatfiles_tracking)return 1;
    UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    if(!rows)return fail(e,n,"Original grDatFiles table is unavailable");
    if(!h->grdatfiles_begin_seen){
        if(!grdatfiles_table_zero(rows))return fail(e,n,"Original grDatFiles rows appeared without an E8 owner capture");
        return 1;
    }
    if(!h->grdatfiles_captured)return fail(e,n,"Original E8 grDatFiles owner was not captured");
    if(h->grdatfiles_retired)
        return grdatfiles_table_zero(rows)?1:fail(e,n,"Retired original grDatFiles rows were repopulated");
    if(!grdatfiles_preflight(h,rows,e,n))return 0;
    /* Registry provenance for all populated rows was checked above; the
     * original-C helper repeats the complete tuple preflight before release. */
    if(!melee_web_grdatfiles_retire_captured(h->grdatfiles_rows,h->grdatfiles_identities))
        return fail(e,n,"Original grDatFiles row retirement refused its captured table");
    if(!grdatfiles_table_zero(rows))return fail(e,n,"Original grDatFiles release did not clear all four rows");
    h->grdatfiles_retired=1;
    return 1;
}
int melee_web_stage_map_grdatfiles_arm(MeleeWebStageMap* h,char* e,size_t n){
    UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    if(!h||h!=owner||h->grdatfiles_tracking||h->grdatfiles_begin_seen||
       h->grdatfiles_captured||!h->sections||!h->generation||
       !melee_web_gameplay_world_exists()||
       h->generation!=melee_web_gameplay_stats().generation||!rows||
       !grdatfiles_table_zero(rows))
        return fail(e,n,"C1 grDatFiles retirement requires an exact empty live StageMap owner");
    h->grdatfiles_tracking=1;
    return ok(e,n);
}
#endif
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
int melee_web_stage_map_set_public(MeleeWebStageMap* h,const MeleeWebArchiveSymbol* entries,size_t count,char* e,size_t n){
    if(!h||h!=owner||h->sections||!entries||!count)
        return fail(e,n,"Native stage public catalog missing or already published");
    for(size_t i=0;i<count;i++)if(!entries[i].filename||!entries[0].filename||strcmp(entries[i].filename,entries[0].filename))
        return fail(e,n,"Native stage public catalog must belong to one archive");
    h->sections=melee_web_archive_sections_register(entries,count,e,n);
    if(!h->sections)return 0;
    h->archives[0].unk0=melee_web_archive_sections_open(entries[0].filename);
    return ok(e,n);
}
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
    for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next)
        if(obj->classifier==HSD_GOBJ_CLASS_STAGE)
            return fail(e,n,"Remove all original stage instances before closing native map descriptors");
    if(Ground_801C498C())
        return fail(e,n,"Remove original map lights before closing native map descriptors");
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(!grdatfiles_retire(h,e,n))return 0;
#endif
    if(h->sections){
        if(!melee_web_archive_sections_close_owned(h->sections,h->archives[0].unk0,e,n))return 0;
        h->sections=NULL;h->archives[0].unk0=NULL;
    }
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
