#include "gameplay_stadium_start.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_bootstrap.h"
#include "gameplay_hud.h"
#include "gameplay_source_memory_runtime.h"
#include <melee/gr/grzakogenerator.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/memory.h>
#include <stdlib.h>
#include <stdio.h>

struct MeleeWebStadiumGenerator {
    void* saved;
    size_t saved_size;
    uint64_t generation;
    MeleeWebSourceMemoryContext context;
    MeleeWebSourceMemoryAllocation lease;
    grZakoGenerator_Data* data;
    HSD_GObj* scheduler;
    HSD_GObjProc* proc;
};
static MeleeWebStadiumGenerator* owner;
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
/* Exact classifier/link/proc priority authored by grZakoGenerator_801CAE04. */
enum { GENERATOR_CLASS = 2, GENERATOR_LINK = 4, GENERATOR_PROC_LINK = 0 };
static HSD_GObj* source_scheduler(int* count)
{
    HSD_GObj* found=NULL;*count=0;
    if(!HSD_GObj_Entities)return NULL;
    for(HSD_GObj* g=((HSD_GObj**)HSD_GObj_Entities)[GENERATOR_LINK];g;g=g->next)
        for(HSD_GObjProc* p=g->proc;p;p=p->child)
            if(p->on_invoke==fn_801CADBC){found=g;++*count;}
    return found;
}
static int live(MeleeWebStadiumGenerator* h,int ready,char* e,size_t n)
{
    MeleeWebSourceMemoryContext context;
    if(!h||h!=owner||h->generation!=melee_web_gameplay_generation()||
       melee_web_source_memory_context_read(&context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       context.world_generation!=h->context.world_generation||
       context.source_heap_handle!=h->context.source_heap_handle)
        return fail(e,n,"Stadium generator lost its source world/heap owner");
    if(ready ? !melee_web_hud_stadium_ready_context(NULL,NULL) :
       (HSD_GObj_804D781C||HSD_GObj_804D7814))
        return fail(e,n,"Stadium generator ownership requires idle source callbacks");
    return 1;
}
MeleeWebStadiumGenerator* melee_web_stadium_generator_prepare(char* e,size_t n)
{
    void *descs,*data;int count;
    if(owner||!melee_web_stadium_zako_view(&descs,&data)||data||
       (source_scheduler(&count),count)!=0)
        {fail(e,n,"Stadium OnStart requires an unowned original generator");return NULL;}
    MeleeWebStadiumGenerator* h=calloc(1,sizeof(*h));
    if(!h){fail(e,n,"Cannot allocate Stadium generator owner");return NULL;}
    h->generation=melee_web_gameplay_generation();
    h->saved_size=melee_web_stadium_zako_snapshot_size();
    h->saved=malloc(h->saved_size);
    if(!h->generation||!h->saved||
       melee_web_source_memory_context_read(&h->context)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       !melee_web_stadium_zako_snapshot_read(h->saved,h->saved_size)){
        free(h->saved);free(h);fail(e,n,"Stadium generator source snapshot unavailable");return NULL;
    }
    owner=h;
    if(!live(h,0,e,n)){owner=NULL;free(h->saved);free(h);return NULL;}
    ok(e,n);return h;
}
static int generator_preflight(MeleeWebStadiumGenerator*,int,char*,size_t);
static int generator_capture(MeleeWebStadiumGenerator* h,int ready,char* e,size_t n)
{
    void *descs,*data;int count;
    if(!live(h,ready,e,n))return 0;
    if(h->data||!melee_web_stadium_zako_view(&descs,&data)||descs||!data)
        return fail(e,n,"Original Stadium OnStart did not publish its NULL-desc generator");
    MeleeWebSourceMemoryAllocation lease;
    if(melee_web_source_memory_allocation_read(data,&lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       !lease.live||lease.requested_bytes!=sizeof(grZakoGenerator_Data)||
       lease.world_generation!=h->context.world_generation||
       lease.source_heap_handle!=h->context.source_heap_handle||
       lease.allocation_generation<=h->context.allocation_generation_watermark)
        return fail(e,n,"Original generator data is not this exact new SDK lease");
    HSD_GObj* g=source_scheduler(&count);
    if(count!=1||!g||g->classifier!=GENERATOR_CLASS||g->p_link!=GENERATOR_LINK||
       g->hsd_obj||g->user_data||g->user_data_remove_func||!g->proc||
       g->proc->child||g->proc->gobj!=g||g->proc->s_link!=GENERATOR_PROC_LINK)
        return fail(e,n,"Original generator scheduler has a foreign or missing owner");
    h->data=data;h->lease=lease;h->scheduler=g;h->proc=g->proc;
    return generator_preflight(h,ready,e,n);
}
static int generator_preflight(MeleeWebStadiumGenerator* h,int ready,char* e,size_t n)
{
    void *descs,*data;int count;MeleeWebSourceMemoryAllocation lease;
    if(!live(h,ready,e,n))return 0;
    if(!h->data||!melee_web_stadium_zako_view(&descs,&data)||descs||data!=h->data||
       melee_web_source_memory_allocation_read(data,&lease)!=MELEE_WEB_SOURCE_MEMORY_READ_OK||
       !lease.live||lease.requested_bytes!=h->lease.requested_bytes||
       lease.world_generation!=h->lease.world_generation||
       lease.source_heap_handle!=h->lease.source_heap_handle||
       lease.allocation_generation!=h->lease.allocation_generation)
        return fail(e,n,"Stadium generator exact data lease/root changed");
    HSD_GObj* g=source_scheduler(&count);
    if(count!=1||g!=h->scheduler||g->classifier!=GENERATOR_CLASS||
       g->p_link!=GENERATOR_LINK||g->hsd_obj||g->user_data||g->user_data_remove_func||
       g->proc!=h->proc||g->proc->child||g->proc->gobj!=g||
       g->proc->on_invoke!=fn_801CADBC||g->proc->s_link!=GENERATOR_PROC_LINK)
        return fail(e,n,"Stadium generator exact scheduler/proc changed");
    /* The source owns the authored data array; item pointers are borrowers.
     * The item owner must retire them first. Never broaden this into a drain. */
    for(size_t i=0;i<ARRAY_SIZE(h->data->entries);++i)
        if(h->data->entries[i].x4)
            return fail(e,n,"Retire generator item borrowers before Stadium teardown");
    return ok(e,n);
}
int melee_web_stadium_generator_capture(MeleeWebStadiumGenerator* h,char* e,size_t n)
{return generator_capture(h,0,e,n);}
int melee_web_stadium_generator_capture_ready(MeleeWebStadiumGenerator* h,char* e,size_t n)
{return generator_capture(h,1,e,n);}
int melee_web_stadium_generator_preflight(MeleeWebStadiumGenerator* h,char* e,size_t n)
{return generator_preflight(h,0,e,n);}
int melee_web_stadium_generator_end(MeleeWebStadiumGenerator* h,char* e,size_t n)
{
    if(!h)return ok(e,n);
    if(!melee_web_stadium_generator_preflight(h,e,n))return 0;
    HSD_GObjPLink_80390228(h->scheduler);h->scheduler=NULL;h->proc=NULL;
    /* Private root restoration is checked against the still-live exact data
     * before freeing it. All object/data/root predicates precede destruction. */
    if(!melee_web_stadium_zako_restore(h->saved,h->saved_size,h->data))
        return fail(e,n,"Stadium generator private root changed during scheduler retirement");
    HSD_Free(h->data);h->data=NULL;
    owner=NULL;free(h->saved);free(h);return ok(e,n);
}
#endif
