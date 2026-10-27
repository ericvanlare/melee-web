#include "gameplay_stage_map.h"
#include "gameplay_stage_map_build.h"
#include "gameplay_bootstrap.h"
#include "hsd_native_joint.h"
#include "gameplay_source_files.h"
#include "gameplay_source_memory_runtime.h"
#include <sysdolphin/baselib/archive.h>
#include <melee/gr/ground.h>
#include <melee/gr/granime.h>
#include <melee/gr/types.h>
#include <melee/sc/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
typedef struct MeleeWebStageMapSourceSlot {
    UnkArchiveStruct row;
    const void* buffer;size_t length;unsigned form,state,raw_clear_pending;
    uint64_t identity;
    MeleeWebSourceFileScope* file_scope;
    MeleeWebSourceMemoryAllocation header,buffer_lease;
} MeleeWebStageMapSourceSlot;
#endif
struct MeleeWebStageMap {
    UnkArchiveStruct archives[4];
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    void* form_handles[4];
    UnkStageDat* form_maps[4];
    MeleeWebStageMapSourceSlot source_slots[4];
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
static const char* const form_files[4]={"GrPs1.dat","GrPs2.dat","GrPs3.dat","GrPs4.dat"};
static int source_slot_index(const void* slot){
    const UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    if(!rows)return -1;
    for(unsigned i=0;i<4;i++)if(slot==&rows[i])return (int)i;
    return -1;
}
static int source_slot_live(MeleeWebStageMap* h,unsigned index){
    MeleeWebStageMapSourceSlot* saved=&h->source_slots[index];
    const UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    uint64_t identity=0;
    MeleeWebSourceMemoryAllocation header={0},buffer={0};
    MeleeWebSourceMemoryContext context={0};
    if(!rows||saved->state!=1||saved->form>=4||
       !grdatfiles_same_row(&rows[index],&saved->row)||
       h->generation!=melee_web_gameplay_stats().generation||
       !melee_web_archive_sections_source_map_matches(h->generation,
           saved->row.unk0,h->form_handles[saved->form],h->form_maps[saved->form],
           saved->buffer,saved->length,saved->identity,&identity)||
       identity!=saved->identity||
       melee_web_source_files_current_scope()!=saved->file_scope||
       !melee_web_source_files_quiescent_for(saved->file_scope,NULL,0)||
       melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       context.world_generation!=h->generation||context.source_heap_handle!=saved->header.source_heap_handle||
       melee_web_source_memory_allocation_read(saved->row.unk0,&header)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       melee_web_source_memory_allocation_read(saved->buffer,&buffer)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       !header.live||!buffer.live||!header.allocation_generation||!buffer.allocation_generation||header.requested_bytes!=sizeof(HSD_Archive)||
       buffer.requested_bytes!=0x50000||
       header.world_generation!=h->generation||buffer.world_generation!=h->generation||
       header.source_heap_handle!=context.source_heap_handle||buffer.source_heap_handle!=context.source_heap_handle||
       header.allocation_generation!=saved->header.allocation_generation||
       buffer.allocation_generation!=saved->buffer_lease.allocation_generation)return 0;
    /* Registry identity and exact live source allocation permit this read. */
    const HSD_Archive* archive=saved->row.unk0;
    if(saved->length<sizeof(HSD_ArchiveHeader)||archive->top_ptr!=saved->buffer||
       archive->data!=(const unsigned char*)saved->buffer+sizeof(HSD_ArchiveHeader)||
       !(archive->flags&1))return 0;
    return 1;
}
int melee_web_stage_map_set_forms(MeleeWebStageMap* h,void* const maps[4],char* e,size_t n){
    if(!h||h!=owner||!h->sections||!h->grdatfiles_captured||h->grdatfiles_retired||h->form_handles[0]||!maps||
       !melee_web_gameplay_world_exists()||h->generation!=melee_web_gameplay_stats().generation)
        return fail(e,n,"Stadium form catalogs lack an exact live StageMap owner");
    for(unsigned i=0;i<4;i++){
        if(!melee_web_archive_sections_catalog_map_matches(form_files[i],maps[i]))
            return fail(e,n,"Stadium form map lacks its exact registered public catalog");
        for(unsigned j=0;j<i;j++)if(maps[i]==maps[j])
            return fail(e,n,"Stadium form maps alias one descriptor owner");
    }
    for(unsigned i=0;i<4;i++){
        h->form_handles[i]=melee_web_archive_sections_open(form_files[i]);
        h->form_maps[i]=maps[i];
    }
    return ok(e,n);
}
int melee_web_stage_map_source_slot_bind(void* candidate,const void* data,size_t length){
    MeleeWebStageMap* h=owner;
    if(!h)return 1; /* No native StageMap: preserve the existing source route. */
    int index=source_slot_index(candidate);
    if(!h->form_handles[0]||index<0||!data||!length||length>0x50000||
       !h->grdatfiles_captured||h->grdatfiles_retired||h->source_slots[index].state||
       !grdatfiles_row_zero(&h->grdatfiles_rows[index]))return 0;
    const UnkArchiveStruct* row=candidate;
    if(!row->unk0||!row->unk4||row->unk8!=1)return 0;
    unsigned form=4;uint64_t identity=0;
    for(unsigned i=0;i<4;i++)if(row->unk4==h->form_maps[i])form=i;
    if(form==4||!melee_web_archive_sections_source_map_matches(h->generation,
          row->unk0,h->form_handles[form],row->unk4,data,length,0,&identity))return 0;
    for(unsigned i=0;i<4;i++)if(h->source_slots[i].state&&
        (h->source_slots[i].buffer==data||h->source_slots[i].row.unk0==row->unk0))return 0;
    MeleeWebStageMapSourceSlot saved={0};
    saved.row=*row;saved.form=form;saved.identity=identity;saved.buffer=data;saved.length=length;
    saved.file_scope=melee_web_source_files_current_scope();saved.state=1;
    if(melee_web_source_memory_allocation_read(row->unk0,&saved.header)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       melee_web_source_memory_allocation_read(data,&saved.buffer_lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK)return 0;
    h->source_slots[index]=saved;
    if(!source_slot_live(h,(unsigned)index)){
        memset(&h->source_slots[index],0,sizeof(h->source_slots[index]));return 0;
    }
    return 1;
}
int melee_web_stage_map_source_slot_release(void* candidate){
    MeleeWebStageMap* h=owner;
    if(!h||!h->form_handles[0])return 1; /* Default legacy path unchanged. */
    int index=source_slot_index(candidate);
    if(index<0){
        /* Authored phase6 also passes the exact retained raw map2 buffer.
         * Its non-0/non-1 third word selects no free; preserve the subsequent
         * original sizeof(slot) clear, never convert xCC into a slot. */
        for(unsigned i=0;i<4;i++)if(h->source_slots[i].state==1&&
              h->source_slots[i].buffer==candidate){
            MeleeWebStageMapSourceSlot* saved=&h->source_slots[i];uint32_t word=0;
            if(saved->raw_clear_pending||saved->length<sizeof(UnkArchiveStruct)||
               !source_slot_live(h,i))return 0;
            memcpy(&word,(const unsigned char*)candidate+offsetof(UnkArchiveStruct,unk8),sizeof(word));
            if(word==0||word==1)return 0; /* Those branches would free a raw word. */
            saved->raw_clear_pending=1;return 1;
        }
        return 0; /* Unknown pointer refused before any candidate dereference. */
    }
    const UnkArchiveStruct* row=candidate;
    if(row->unk8==0){
        uint64_t identity=0,native_identity=0;
        return grdatfiles_same_row(row,&h->grdatfiles_rows[index])&&
            grdatfiles_source_map_matches(h,row,h->grdatfiles_identities[index],&identity,
                h->grdatfiles_native_identity,&native_identity);
    }
    if(h->source_slots[index].raw_clear_pending||!source_slot_live(h,(unsigned)index))return 0;
    MeleeWebStageMapSourceSlot* saved=&h->source_slots[index];
    const UnkStageDat* map=h->form_maps[saved->form];
    if(map->unkC<0||map->unkC>(int)(sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]))||
       !map->unk8)return 0;
    for(int id=0;id<map->unkC;id++)if(map->unk8[id].unk0&&stage_info.map_gobjs[id])return 0;
    /* The stage array is not permission to ignore a still-linked source GObj.
     * Check allocation ownership before reading any Ground userdata. */
    for(HSD_GObj* obj=((HSD_GObj**)HSD_GObj_Entities)[5];obj;obj=obj->next){
        if(obj->classifier!=HSD_GOBJ_CLASS_STAGE||!obj->user_data)continue;
        MeleeWebSourceMemoryAllocation lease={0};
        if(melee_web_source_memory_allocation_read(obj->user_data,&lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
           !lease.live||lease.world_generation!=h->generation||lease.requested_bytes!=sizeof(Ground))return 0;
        const Ground* ground=obj->user_data;
        if((unsigned)ground->map_id<(unsigned)map->unkC&&map->unk8[ground->map_id].unk0)return 0;
    }
    /* This is an adjunct detach, never lbArchive_80016EFC/top_ptr release.
     * Typed descriptor arenas/catalog handles remain retained by StageMap. */
    melee_web_archive_sections_release(saved->row.unk0);saved->state=2;
    return 1;
}
int melee_web_stage_map_source_slot_cleared(void* candidate){
    MeleeWebStageMap* h=owner;
    if(!h||!h->form_handles[0])return 1;
    int index=source_slot_index(candidate);
    if(index<0){
        for(unsigned i=0;i<4;i++)if(h->source_slots[i].state==1&&
              h->source_slots[i].buffer==candidate&&h->source_slots[i].raw_clear_pending){
            if(!source_slot_live(h,i))return 0;
            const unsigned char* bytes=candidate;
            for(size_t j=0;j<sizeof(UnkArchiveStruct);j++)if(bytes[j])return 0;
            h->source_slots[i].raw_clear_pending=0;return 1;
        }
        return 0;
    }
    if(!grdatfiles_row_zero(candidate))return 0;
    if(h->source_slots[index].state==2){memset(&h->source_slots[index],0,sizeof(h->source_slots[index]));return 1;}
    return h->source_slots[index].state==0;
}

static UnkArchiveStruct* source_slot_table(MeleeWebStageMap* h){
    UnkArchiveStruct* rows=melee_web_grdatfiles_ordinary_table();
    if(!rows||h!=owner||h->generation!=melee_web_gameplay_stats().generation)abort();
    for(unsigned i=0;i<4;i++){
        if(grdatfiles_row_zero(&rows[i])){
            if(h->source_slots[i].state)abort();
            continue;
        }
        if(rows[i].unk8==0){
            uint64_t identity=0,native_identity=0;
            if(!grdatfiles_same_row(&rows[i],&h->grdatfiles_rows[i])||
               !grdatfiles_source_map_matches(h,&rows[i],h->grdatfiles_identities[i],
                   &identity,h->grdatfiles_native_identity,&native_identity))abort();
        }else if(!source_slot_live(h,i))abort();
    }
    return rows;
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
void* melee_web_stage_map_archives(void){
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(owner&&owner->form_handles[0])return source_slot_table(owner);
#endif
    return owner?owner->archives:NULL;
}
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
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    if(owner->form_handles[0]){
        UnkArchiveStruct* rows=source_slot_table(owner);
        for(unsigned i=0;i<4;i++){
            if(grdatfiles_row_zero(&rows[i]))continue;
            UnkStageDat* map=rows[i].unk4;
            if(id<map->unkC&&map->unk8[id].unk0)return &rows[i];
        }
        return NULL;
    }
#endif
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
    for(unsigned i=0;i<4;i++)if(h->source_slots[i].state)
        return fail(e,n,"Source Stadium form slot must finish its header retirement before map close");
    if(!grdatfiles_retire(h,e,n))return 0;
#endif
    if(h->sections){
        if(!melee_web_archive_sections_close_owned(h->sections,h->archives[0].unk0,e,n))return 0;
        h->sections=NULL;h->archives[0].unk0=NULL;
    }
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    for(unsigned i=0;i<4;i++)if(h->form_handles[i])
        melee_web_archive_sections_release(h->form_handles[i]);
#endif
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
