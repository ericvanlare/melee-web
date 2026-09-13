#include "gameplay_common_context.h"
#include "gameplay_bootstrap.h"
#include <melee/ft/fighter.h>
#include <melee/lb/types.h>
#include <melee/ft/ft_0C8C.h>
#include <melee/ft/ftCo_800C7CA0.h>
#include <melee/gr/types.h>
#include <melee/gr/ground.h>
#include <melee/sfx/crowdsfx.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MELEE_WEB_COMMON_ASSERT_LAYOUT(ftCommonData);
int melee_web_native_common_capture_begin(void*, void (*)(void*, void*), void*);
int melee_web_native_common_capture_end(void*);
typedef struct Payload { struct Payload* next; void* bytes; } Payload;
typedef struct CommonObject { struct MeleeWebCommonContext* context; HSD_GObj* owner; } CommonObject;
struct MeleeWebCommonContext {
    ftCommonData scalars;
    MeleeWebCommonNative* tables;
    MeleeWebCommonCpuData* cpu_data;
    MeleeWebNativeJoint* joint;
    Payload* payloads;
    size_t payload_bytes;
    void* roots[23];
    void* saved[23];
    HSD_MObj* saved_materials[2];
    HSD_MObj* owned_materials[2];
    CommonObject objects[2];
    uint32_t ready_mask, captured;
    uint64_t generation;
    unsigned attached, initialized;
};
static MeleeWebCommonContext* published;
static int fail(char* error,size_t size,const char* why) { if(error&&size)snprintf(error,size,"%s",why);return 0; }
static int success(char* error,size_t size) { if(error&&size)error[0]=0;return 1; }
static void* copy_payload(MeleeWebCommonContext* h,const void* input,size_t size)
{
    if(!size)return NULL;
    if(!input||size>64U*1024U*1024U-h->payload_bytes)return NULL;
    Payload* p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->bytes=malloc(size);if(!p->bytes){free(p);return NULL;}
    memcpy(p->bytes,input,size);h->payload_bytes+=size;p->next=h->payloads;h->payloads=p;return p->bytes;
}
static void release(MeleeWebCommonContext* h)
{
    if(!h)return;
    if(h->joint)melee_web_native_joint_destroy(h->joint,NULL,0);
    melee_web_common_tables_destroy(h->tables);
    melee_web_common_cpu_release(h->cpu_data);
    while(h->payloads){Payload* p=h->payloads;h->payloads=p->next;free(p->bytes);free(p);}
    free(h);
}
MeleeWebCommonContext* melee_web_common_context_create(const MeleeWebCommonScalars* input,
    const MeleeWebCommonTables* tables,const MeleeWebNativeGraph* graph,char* error,size_t size)
{
    if(!input||!tables||!graph||!graph->joints||graph->root>=graph->joint_count||
       graph->joints[graph->root].dobj==UINT32_MAX)
        {fail(error,size,"Common context requires actual scalar/static/root20 inputs");return NULL;}
    MeleeWebCommonContext* h=calloc(1,sizeof(*h));if(!h){fail(error,size,"Cannot allocate common context");return NULL;}
#define VALID_F32(v) isfinite(v)
#define VALID_I32(v) 1
#define VALID_U32(v) 1
#define VALID_OPAQUE32(v) 1
#define VALID_BYTE(v) 1
#define COPY_FIELD(offset,kind,name) \
    if(!VALID_##kind(input->name)){fail(error,size,"Nonfinite common scalar " #name);release(h);return NULL;} \
    memcpy((char*)&h->scalars+offset,(const char*)input+offset,sizeof(input->name));
    MELEE_WEB_COMMON_FIELDS(COPY_FIELD)
#undef COPY_FIELD
    /* DatCommon owns the decoded PlCo graph.  The context takes a reference
     * before publishing Fighter_804D64FC, so the archive owner may be
     * destroyed while original fighters still use the graph. */
    if(tables->cpu_data&&!melee_web_common_cpu_retain(tables->cpu_data)){
        fail(error,size,"Common CPU data handle cannot be retained");release(h);return NULL;
    }
    h->cpu_data=tables->cpu_data;
    h->tables=melee_web_common_tables_create(tables,error,size);if(!h->tables){release(h);return NULL;}
    // Validate the complete graph before copying bounded borrowed byte spans.
    MeleeWebNativeJoint* checked=melee_web_native_joint_hydrate(graph,error,size);
    if(!checked){release(h);return NULL;}
    melee_web_native_joint_destroy(checked,NULL,0);
    MeleeWebNativeGraph owned=*graph;
    MeleeWebNativePObjDesc* polygons=copy_payload(h,graph->pobjs,graph->pobj_count*sizeof(*polygons));
    MeleeWebNativeMaterialDesc* materials=copy_payload(h,graph->materials,graph->material_count*sizeof(*materials));
    if((graph->pobj_count&&!polygons)||(graph->material_count&&!materials))goto oom;
    owned.pobjs=polygons;owned.materials=materials;
    for(uint32_t p=0;p<graph->pobj_count;++p){
        MeleeWebPObjView* v=&polygons[p].geometry;
        MeleeWebPObjAttribute* attrs=copy_payload(h,v->attributes,v->attribute_count*sizeof(*attrs));
        if(!attrs)goto oom;v->attributes=attrs;
        v->display=copy_payload(h,v->display,v->display_byte_size);if(!v->display)goto oom;
        for(uint32_t a=0;a<v->attribute_count;++a){
            attrs[a].data=copy_payload(h,attrs[a].data,attrs[a].byte_size);
            if(attrs[a].byte_size&&!attrs[a].data)goto oom;
        }
    }
    for(uint32_t m=0;m<graph->material_count;++m){
        MeleeWebNativeTextureDesc* textures=copy_payload(h,materials[m].textures,materials[m].material.texture_count*sizeof(*textures));
        if(materials[m].material.texture_count&&!textures)goto oom;materials[m].textures=textures;
        for(uint32_t t=0;t<materials[m].material.texture_count;++t){
            MeleeWebHsdTextureDesc* d=&textures[t].texture;
            d->image_data=copy_payload(h,d->image_data,d->image_bytes);if(!d->image_data)goto oom;
            d->palette_data=copy_payload(h,d->palette_data,d->palette_bytes);
            if(d->palette_bytes&&!d->palette_data)goto oom;
        }
    }
    h->joint=melee_web_native_joint_hydrate(&owned,error,size);if(!h->joint){release(h);return NULL;}
    h->ready_mask=tables->ready_mask|1u|(1u<<20);
    if(h->cpu_data)h->ready_mask|=1u<<22;
    h->roots[0]=&h->scalars;
    for(uint32_t r=1;r<23;++r)h->roots[r]=(void*)melee_web_common_tables_root(h->tables,r);
    h->roots[22]=h->cpu_data?h->cpu_data->root:NULL;
    h->roots[20]=melee_web_native_joint_descriptor(h->joint,error,size);
    success(error,size);return h;
oom:
    fail(error,size,"Cannot copy common root20 payload storage");release(h);return NULL;
}
int melee_web_common_context_set_color_tables(MeleeWebCommonContext* h,const MeleeWebColorRow* common,const MeleeWebColorRow* extra,char* error,size_t size)
{
    if(!h||h->attached||h->initialized||!common||!extra)return fail(error,size,"Color tables require an unpublished common owner");
    struct Fighter_804D653C_t rows[123];
    const MeleeWebColorRow* inputs[2]={common,extra};
    for(unsigned table=0;table<2;table++){
        unsigned count=table?6:123;
        memset(rows,0,sizeof(rows));
        for(unsigned i=0;i<count;i++){
            rows[i].unk=inputs[table][i].program;rows[i].unk4=inputs[table][i].priority;rows[i].unk5=inputs[table][i].layer;
        }
        h->roots[6+table]=copy_payload(h,rows,count*sizeof(rows[0]));
        if(!h->roots[6+table])return fail(error,size,"Cannot own native common color rows");
    }
    h->ready_mask|=(1u<<6)|(1u<<7);
    return success(error,size);
}
int melee_web_common_context_set_root16(MeleeWebCommonContext* h,void* joint,char* error,size_t size)
{
    if(!h||h->attached||h->initialized||h->roots[16]||!joint)
        return fail(error,size,"Common root16 requires an unpublished nonnull descriptor");
    if(((uintptr_t)joint&3U)!=0)
        return fail(error,size,"Common root16 descriptor is not four-byte aligned");
    h->roots[16]=joint;
    h->ready_mask|=1u<<16;
    return success(error,size);
}
int melee_web_common_context_set_respawn(MeleeWebCommonContext* h,void* joint,void* animation,char* error,size_t size)
{
    if(!h||h->attached||h->initialized||!joint||!animation)return fail(error,size,"Respawn descriptors require an unpublished common owner");
    void* entries[2]={joint,animation};
    h->roots[8]=copy_payload(h,entries,sizeof(entries));
    if(!h->roots[8])return fail(error,size,"Cannot own respawn descriptor table");
    h->ready_mask|=1u<<8;return success(error,size);
}
static void removed(void* data) { ((CommonObject*)data)->owner=NULL; }
static void captured(void* data,void* root)
{
    MeleeWebCommonContext* h=data;
    if(!h->attached||h!=published||h->generation!=melee_web_gameplay_stats().generation||h->captured>=2||!root)
        HSD_Panic(__FILE__,__LINE__,"Unexpected original common material constructor");
    h->owned_materials[h->captured]=HSD_JObjGetDObj(root)->mobj;
    CommonObject* o=&h->objects[h->captured++];o->context=h;
    o->owner=GObj_Create(HSD_GOBJ_CLASS_UI,0,0);
    if(!o->owner)HSD_Panic(__FILE__,__LINE__,"Cannot retain common material joint owner");
    HSD_GObjObject_80390A70(o->owner,HSD_GObj_JObjKind,root);
    GObj_InitUserData(o->owner,0,removed,o);
}
int melee_web_common_context_require(MeleeWebCommonContext* h,uint32_t mask,char* error,size_t size)
{
    if(!h)return fail(error,size,"Common context is absent");
    if(mask&~h->ready_mask){
        if(error&&size)snprintf(error,size,"Common roots unavailable: requested0x%06x ready0x%06x missing0x%06x",mask,h->ready_mask,mask&~h->ready_mask);
        return 0;
    }
    return success(error,size);
}
int melee_web_common_context_attach(MeleeWebCommonContext* h,char* error,size_t size)
{
    if(!h||h->attached||published)return fail(error,size,"Common publication is absent or already owned");
    if(!melee_web_native_world_enable(error,size))return 0;
    if(!melee_web_native_common_capture_begin(h->roots[20],captured,h))return fail(error,size,"Common material capture is already owned");
#define SAVE_ROOT(index,name) h->saved[index]=(void*)name; name=h->roots[index];
    MELEE_WEB_COMMON_ROOTS(SAVE_ROOT)
#undef SAVE_ROOT
    h->saved_materials[0]=ft_804D6580;h->saved_materials[1]=ft_804D6588;
    ft_804D6580=ft_804D6588=NULL;
    h->attached=1;h->generation=melee_web_gameplay_stats().generation;published=h;
    return success(error,size);
}
void** melee_web_common_context_source_roots(void)
{
    if(!published||!published->attached||published->generation!=melee_web_gameplay_stats().generation)
        HSD_Panic(__FILE__,__LINE__,"Fighter_LoadCommonData requires attached checked common roots");
    return published->roots;
}
int melee_web_common_context_initialize_materials(MeleeWebCommonContext* h,char* error,size_t size)
{
    if(!h||h!=published||h->generation!=melee_web_gameplay_stats().generation||h->captured||h->initialized)
        return fail(error,size,"Common material initialization requires a fresh attached live context");
    ftCo_800C8064();ftCo_800C8F6C();h->initialized=1;
    return success(error,size);
}
int melee_web_common_context_initialize_fighters(MeleeWebCommonContext* h,char* error,size_t size)
{
    if(!h||h!=published||h->generation!=melee_web_gameplay_stats().generation||h->captured||h->initialized)
        return fail(error,size,"Fighter initialization requires a fresh attached live common context");
    if(!stage_info.map_plit||!stage_info.param)return fail(error,size,"Original fighter initialization requires installed stage/light parameters");
    if(!melee_web_common_context_require(h,MELEE_WEB_COMMON_RUNTIME_MASK,error,size))return 0;
    Fighter_FirstInitialize_80067A84();h->initialized=2;
    return success(error,size);
}
int melee_web_common_context_destroy(MeleeWebCommonContext* h,char* error,size_t size)
{
    if(!h)return success(error,size);
    if(h->attached){
        if(h!=published)return fail(error,size,"Common context publication ownership was lost");
#define CHECK_ROOT(index,name) if((void*)name!=h->roots[index])return fail(error,size,"Common global was replaced: " #name);
        MELEE_WEB_COMMON_ROOTS(CHECK_ROOT)
#undef CHECK_ROOT
        if(ft_804D6580!=h->owned_materials[0]||ft_804D6588!=h->owned_materials[1])
            return fail(error,size,"Common material publication was replaced by another owner");
        if((h->objects[0].owner||h->objects[1].owner)&&h->generation!=melee_web_gameplay_stats().generation)
            return fail(error,size,"Common teardown lost its original heap ownership");
        if(h->generation==melee_web_gameplay_stats().generation){
            for(unsigned link=0;link<=HSD_GObjLibInitData.p_link_max;++link)
                for(HSD_GObj* o=((HSD_GObj**)HSD_GObj_Entities)[link];o;o=o->next)
                    if(o->classifier==HSD_GOBJ_CLASS_FIGHTER)return fail(error,size,"Remove original fighters before their common context");
            if((h->objects[0].owner&&h->objects[0].owner==HSD_GObj_804D781C)||
               (h->objects[1].owner&&h->objects[1].owner==HSD_GObj_804D781C))
                return fail(error,size,"Common context cannot be destroyed inside its current callback");
        }
        if(!melee_web_native_common_capture_end(h))return fail(error,size,"Common material capture ownership was lost");
#define RESTORE_ROOT(index,name) name=h->saved[index];
        MELEE_WEB_COMMON_ROOTS(RESTORE_ROOT)
#undef RESTORE_ROOT
        ft_804D6580=h->saved_materials[0];ft_804D6588=h->saved_materials[1];
        published=NULL;h->attached=0;
        for(unsigned i=0;i<2;++i)if(h->objects[i].owner)HSD_GObjPLink_80390228(h->objects[i].owner);
    }
    release(h);return success(error,size);
}
