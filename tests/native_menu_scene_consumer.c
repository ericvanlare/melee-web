#include "gameplay_compat.h"
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/pobj.h>
#include "hsd_native_arrays.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef struct Scene {
    HSD_CObjDesc* camera;
    HSD_LightDesc* light0;
    HSD_LightDesc* light1;
    HSD_FogDesc* fog;
    StaticModelDesc models[12];
} Scene;
/* Compare the actual source CPU morph inputs with the canonical GX array,
 * bit for bit. A finite/nonzero-value check would not catch wrong byte order. */
static int shape_array_matches(const HSD_VtxDescList* host,
                               const HSD_VtxDescList* gpu, const HSD_ShapeSetDesc* shape,
                               int normal)
{
    uint32_t bytes;
    if(!host || !gpu || host->stride!=gpu->stride ||
       host->comp_type!=gpu->comp_type || host->frac!=gpu->frac ||
       host->comp_cnt!=gpu->comp_cnt || host->attr_type!=gpu->attr_type ||
       !melee_web_native_array_bound(gpu->attr,gpu->vertex,gpu->stride,&bytes))
        return 0;
    const uint32_t scalar=gpu->comp_type==GX_F32 ? 4 :
        gpu->comp_type==GX_U16 || gpu->comp_type==GX_S16 ? 2 : 1;
    const uint32_t width=3*scalar;
    if(bytes<width || gpu->stride<width)return 0;
    /* Array registrations can share a source address but have different
     * extents. Compare precisely the indices this ShapeSet reads, rather than
     * the union of all registered GPU array bounds. */
    const unsigned lists=shape->nb_shape+((shape->flags&SHAPESET_ADDITIVE)?1:0);
    const unsigned count=normal?shape->nb_normal_index:shape->nb_vertex_index;
    u8** indices=normal?shape->normal_idx_list:shape->vertex_idx_list;
    const unsigned index_bytes=gpu->attr_type==GX_INDEX16?2:1;
    for(unsigned list=0;list<lists;list++)for(unsigned item=0;item<count;item++){
        const u8* index=indices[list]+item*index_bytes;
        const unsigned value=index_bytes==2?((unsigned)index[0]<<8)|index[1]:index[0];
        const uint32_t at=value*gpu->stride;
        if(at>bytes-width)return 0;
        for(uint32_t axis=0;axis<3;axis++){
            const uint8_t* raw=(const uint8_t*)gpu->vertex+at+axis*scalar;
            const uint8_t* converted=(const uint8_t*)host->vertex+at+axis*scalar;
            uint32_t expected=0,actual=0;
            for(uint32_t b=0;b<scalar;b++)expected=(expected<<8)|raw[b];
            if(scalar==4)memcpy(&actual,converted,4);
            else if(scalar==2){uint16_t half;memcpy(&half,converted,2);actual=half;}
            else actual=*converted;
            if(actual!=expected){fprintf(stderr,"Shape scalar attr=%u at=%u axis=%u actual=%x expected=%x\n",gpu->attr,at,axis,actual,expected);return 0;}
        }
    }
    return 1;
}
static int check_shape_inputs(HSD_Joint* joint, unsigned* shape_count)
{
    for(;joint;joint=joint->next){
        if(!(joint->flags&JOBJ_SPLINE)){
            for(HSD_DObjDesc* d=joint->u.dobjdesc;d;d=d->next){
                for(HSD_PObjDesc* p=d->pobjdesc;p;p=p->next){
                    if((p->flags&0x3000)!=POBJ_SHAPEANIM)continue;
                    HSD_ShapeSetDesc* shape=p->u.shape_set;
                    for(unsigned normal=0;normal<2;normal++){
                        HSD_VtxDescList* host=normal?shape->normal_desc:shape->vertex_desc;
                        if(normal && !shape->nb_normal_index)continue;
                        HSD_VtxDescList* gpu=p->verts;
                        while(gpu->attr!=GX_VA_NULL && gpu->attr!=host->attr)gpu++;
                        if(gpu->attr==GX_VA_NULL || !shape_array_matches(host,gpu,shape,normal))return 0;
                    }
                    (*shape_count)++;
                }
            }
        }
        if(!check_shape_inputs(joint->child,shape_count))return 0;
    }
    return 1;
}
int melee_web_test_menu_scene_consume(void* descriptor, unsigned count)
{
    Scene* scene=descriptor;
    unsigned shape_count=0;
    HSD_GObj* object=GObj_Create(2,3,0x80);
    HSD_CObj* camera=HSD_CObjLoadDesc(scene->camera);
    if(!object||!camera)return 0;
    HSD_GObjObject_80390A70(object,HSD_GObj_CameraKind,camera);
    object=GObj_Create(3,4,0x80);
    HSD_LObj* light0=HSD_LObjLoadDesc(scene->light0);
    HSD_LObj* light1=HSD_LObjLoadDesc(scene->light1);
    if(!object||!light0||!light1)return 0;
    HSD_LObjSetNext(light0,light1);
    HSD_GObjObject_80390A70(object,HSD_GObj_LightKind,light0);
    object=GObj_Create(14,15,0);
    HSD_Fog* fog=HSD_FogLoadDesc(scene->fog);
    if(!object||!fog)return 0;
    HSD_GObjObject_80390A70(object,HSD_GObj_FogKind,fog);
    for(unsigned i=0;i<count;i++){
        StaticModelDesc* model=&scene->models[i];
        if(!check_shape_inputs(model->joint,&shape_count)){fprintf(stderr,"Shape input mismatch in model %u\n",i);return 0;}
        object=GObj_Create(4,5,0x80);
        HSD_JObj* joint=HSD_JObjLoadJoint(model->joint);
        if(!object||!joint)return 0;
        HSD_GObjObject_80390A70(object,HSD_GObj_JObjKind,joint);
        HSD_JObjAddAnimAll(joint,model->animjoint,model->matanim_joint,model->shapeanim_joint);
        for(unsigned frame=0;frame<3;frame++){
            HSD_JObjReqAnimAll(joint,(float)frame);
            HSD_JObjAnimAll(joint);
        }
    }
    if(count==12 && shape_count!=2){fprintf(stderr,"Shape count %u\n",shape_count);return 0;}
    if(shape_count)printf("Native shape CPU inputs match original GX scalar bits: %u PObjs\n",shape_count);
    return 1;
}


/* Drive the original FObj/TObj update path, including the fatal guard mode.
 * A two-entry table deliberately retains an authored, unselected index two. */
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include "hsd_texture_bounds.h"
#include <math.h>
int melee_web_test_texture_bounds(unsigned bad_channel)
{
    uint8_t stream[]={0x21,0,1,1,1,2};
    HSD_FObjDesc track={0};
    track.length=sizeof(stream);track.type=bad_channel?bad_channel:1;
    track.frac_value=0x80;track.ad=stream;
    HSD_AObjDesc animation={0};animation.end_frame=10;animation.fobjdesc=&track;
    HSD_ImageDesc images[2]={{0},{0}};
    HSD_ImageDesc* image_table[]={&images[0],&images[1]};
    HSD_TlutDesc palettes[2]={{0},{0}};
    HSD_TlutDesc* palette_table[]={&palettes[0],&palettes[1]};
    HSD_TexAnim texture={0};texture.aobjdesc=&animation;
    texture.imagetbl=image_table;texture.n_imagetbl=2;
    texture.tluttbl=palette_table;texture.n_tluttbl=2;
    HSD_TObj* object=HSD_TObjAlloc();
    if(!object)return 0;
    HSD_TObjAddAnim(object,&texture);
    if(melee_web_texture_bounds_live()!=1 ||
       !melee_web_texture_index_valid(object,1,1) ||
       !melee_web_texture_index_valid(object,10,1) ||
       melee_web_texture_index_valid(object,1,2) ||
       melee_web_texture_index_valid(object,10,2) ||
       melee_web_texture_index_valid(object,1,NAN) ||
       melee_web_texture_index_valid(object,10,INFINITY) ||
       melee_web_texture_index_valid(object,1,-1) ||
       melee_web_texture_index_valid(object,2,0))return 0;
    for(unsigned frame=0;frame<2;frame++){
        HSD_TObjReqAnim(object,(float)frame);HSD_TObjAnim(object);
        if(object->imagedesc!=image_table[frame] && !bad_channel)return 0;
    }
    if(bad_channel){
        HSD_TObjReqAnim(object,2);HSD_TObjAnim(object);
        return 0; /* The explicit index guard must terminate before lookup. */
    }
    texture.n_imagetbl=1;HSD_TObjAddAnim(object,&texture);
    if(melee_web_texture_bounds_live()!=1 ||
       melee_web_texture_index_valid(object,1,1))return 0;
    HSD_TObjRemove(object);
    return melee_web_texture_bounds_live()==0 &&
        !melee_web_texture_index_valid(object,1,0);
}


#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include "gameplay_bootstrap.h"
int melee_web_test_card_scene_consume(void)
{
    lbLang_SetLanguageSetting(1);lbLang_SetSavedLanguage(1);
    lbCardNew_AllocWorkArea();
    lbCardGame_LoadArchive(0);
    unsigned before=melee_web_gameplay_stats().objects;
    lb_8001CF18();
    return melee_web_gameplay_stats().objects==before+2;
}
void melee_web_test_card_scene_forget(void)
{
    /* The original generic scene teardown clears these pointers after its
     * heap/object lifetime ends. Neither routine fabricates a card operation. */
    lb_8001D1F4();lb_8001C5A4();
}
