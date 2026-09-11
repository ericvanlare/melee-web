#include "gameplay_stage_numeric.h"
#include "gameplay_stage_profile.h"
#include "gameplay_stage_map.h"
#include "hsd_native_joint.h"
#include <melee/gr/ground.h>
#include <melee/gr/grlast.h>
#include <melee/gr/types.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct MeleeWebStageMarkers {HSD_Joint* root;uint32_t offsets[261],joint_count,pair_count;uint16_t pairs[261][2];};
struct MeleeWebStageNumeric {StageInfo saved;HSD_GObj* owner;};
static MeleeWebStageNumeric* active;
/* Original numeric stage-row consumer, private in ground.h. */
extern void melee_web_ground_stage_parameters(int);
#define REQUIRE(c,m) do{if(!(c))r->reject(r->context,m);}while(0)
static HSD_Joint* joint(const MeleeWebNativeDat* r,MeleeWebStageMarkers* m,uint32_t at,unsigned depth){
    REQUIRE(depth<261&&m->joint_count<261,"Marker joint tree exceeds budget");
    for(unsigned i=0;i<m->joint_count;i++)REQUIRE(m->offsets[i]!=at,"Marker joint cycle or shared subtree");
    m->offsets[m->joint_count++]=at;r->region(r->context,at,64);
    REQUIRE(r->pointer(r->context,at,1)==UINT32_MAX,"Custom marker joint class");
    const uint32_t flags=r->word(r->context,at+4);
    REQUIRE(!(flags&~JOBJ_CLASSICAL_SCALE),"Unsupported marker joint flags");
    REQUIRE(r->pointer(r->context,at+16,1)==UINT32_MAX&&r->pointer(r->context,at+56,1)==UINT32_MAX&&r->pointer(r->context,at+60,1)==UINT32_MAX,"Marker joint has unsupported payload");
    HSD_Joint* j=r->allocate(r->context,1,sizeof(*j));j->flags=flags;
    float values[9];for(unsigned i=0;i<9;i++){uint32_t bits=r->word(r->context,at+20+4*i);memcpy(&values[i],&bits,4);REQUIRE(isfinite(values[i]),"Nonfinite marker transform");}
    for(unsigned i=3;i<6;i++)REQUIRE(values[i]!=0,"Singular marker scale");
    memcpy(&j->rotation,values,12);memcpy(&j->scale,values+3,12);memcpy(&j->position,values+6,12);
    uint32_t child=r->pointer(r->context,at+8,64),next=r->pointer(r->context,at+12,64);
    if(child!=UINT32_MAX)j->child=joint(r,m,child,depth+1);
    if(next!=UINT32_MAX)j->next=joint(r,m,next,depth+1);
    return j;
}
MeleeWebStageMarkers* melee_web_stage_markers_decode(const MeleeWebNativeDat* r,uint32_t head){
    if(!r)return NULL;r->region(r->context,head,48);
    const uint32_t reference_count=r->word(r->context,head+4);
    REQUIRE(reference_count>0&&reference_count<=64,
            "Source marker context has an invalid joint-reference count");
    uint32_t entry=r->pointer(r->context,head,12);REQUIRE(entry!=UINT32_MAX,"Missing marker table");r->region(r->context,entry,12);
    uint32_t root=r->pointer(r->context,entry,64),pairs=r->pointer(r->context,entry+4,4),count=r->word(r->context,entry+8);
    REQUIRE(root!=UINT32_MAX&&pairs!=UINT32_MAX&&count>0&&count<=261,"Invalid marker tree or pair count");
    REQUIRE(r->pointer(r->context,root+12,64)==UINT32_MAX,"Marker root has a sibling");
    r->region(r->context,pairs,count*4);
    MeleeWebStageMarkers* m=r->allocate(r->context,1,sizeof(*m));m->root=joint(r,m,root,0);m->pair_count=count;
    unsigned char seen[261]={0};
    for(unsigned i=0;i<count;i++){
        uint16_t index=r->half(r->context,pairs+i*4),id=r->half(r->context,pairs+i*4+2);
        REQUIRE(index<m->joint_count&&id<261&&!seen[id],"Invalid or duplicate marker binding");
        seen[id]=1;m->pairs[i][0]=index;m->pairs[i][1]=id;
    }
    /* All versus stages require four authored player starts. Additional marker
     * IDs are stage-specific; Dream Land ends at ID 4 while modern stages may
     * carry alternate/team starts through ID 7. */
    for(unsigned i=0;i<4;i++)REQUIRE(seen[i],"Missing source player marker");
    for(unsigned i=148;i<=152;i++)REQUIRE(seen[i],"Missing source camera or blast marker");
    return m;
}
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static void removed(void* p){((MeleeWebStageNumeric*)p)->owner=NULL;}
static void collect(HSD_JObj* j,HSD_JObj** list,unsigned* count){for(;j;j=j->next){if(*count>=261)abort();list[(*count)++]=j;if(j->child)collect(j->child,list,count);}}
MeleeWebStageNumeric* melee_web_stage_numeric_begin_kind(MeleeWebStageMarkers* m,int stage_kind,char* e,size_t n){
    const MeleeWebStageProfile* profile=melee_web_stage_profile(stage_kind);
    if(!profile||!m||active||!stage_info.param){fail(e,n,"Missing supported stage marker/ground data or active stage scope");return NULL;}
    if(melee_web_stage_map_archives()){fail(e,n,"Initialize source ground state before publishing map archives");return NULL;}
    for(unsigned i=0;i<sizeof(stage_info.map_gobjs)/sizeof(stage_info.map_gobjs[0]);i++)
        if(stage_info.map_gobjs[i]){fail(e,n,"Initialize source ground state before creating stage objects");return NULL;}
    if(!isfinite(stage_info.param->y)||stage_info.param->y<=0){fail(e,n,"Stage marker context requires a finite positive source scale");return NULL;}
    int has_row=0;
    for(int i=0;i<stage_info.param->stage_param_count;i++)if(stage_info.param->stage_params[i].stkind==profile->stage_kind)has_row=1;
    if(!has_row){fail(e,n,"Ground parameters have no selected stage row");return NULL;}
    if(!melee_web_native_world_enable(e,n))return NULL;
    MeleeWebStageNumeric* h=calloc(1,sizeof(*h));if(!h){fail(e,n,"Cannot allocate stage numeric scope");return NULL;}
    h->saved=stage_info;h->owner=GObj_Create(3,3,0);if(!h->owner){free(h);fail(e,n,"Cannot allocate marker owner");return NULL;}
    HSD_JObj* root=HSD_JObjLoadJoint(m->root);if(!root){HSD_GObjPLink_80390228(h->owner);free(h);fail(e,n,"Original marker joint load failed");return NULL;}
    /* Ground_GetStageGObj loads every map root below a synthetic source
     * joint whose uniform scale is GroundParam::y. Keep that wrapper in the
     * numeric owner too: Ground_801C2D24 reads world transforms, so omitting
     * it would leave Battlefield's 0.8 marker/camera coordinates unscaled. */
    HSD_Joint scale_desc={0};
    scale_desc.scale.x=stage_info.param->y;
    scale_desc.scale.y=stage_info.param->y;
    scale_desc.scale.z=stage_info.param->y;
    HSD_JObj* scaled=HSD_JObjLoadJoint(&scale_desc);
    if(!scaled){HSD_GObjObject_80390A70(h->owner,HSD_GObj_JObjKind,root);HSD_GObjPLink_80390228(h->owner);free(h);fail(e,n,"Original marker scale joint load failed");return NULL;}
    HSD_JObjAddNext(root,scaled);
    HSD_GObjObject_80390A70(h->owner,HSD_GObj_JObjKind,scaled);GObj_InitUserData(h->owner,0,removed,h);
    HSD_JObj* joints[261];unsigned count=0;collect(root,joints,&count);
    if(count!=m->joint_count){HSD_GObjPLink_80390228(h->owner);free(h);fail(e,n,"Original marker tree count differs");return NULL;}
    /* Ground_801C0754 resets mutable ground state before publishing markers
     * and stage data. In particular its -10000 floor sentinel permits the
     * original camera to follow fighters below the stage. Archive publication
     * and stage objects are absent here; saved StageInfo owns the full restore. */
    Ground_801BFFB0();
    for(unsigned i=0;i<m->pair_count;i++)Ground_801C2D0C(m->pairs[i][1],joints[m->pairs[i][0]]);
    stage_info.grkind=profile->ground_kind;stage_info.on_touch_line=profile->source->on_touch_line;stage_info.on_check_shadow_render=profile->source->on_check_shadow_render;
    stage_info.unk8C.b4=1;stage_info.unk8C.b5=1;
    melee_web_ground_stage_parameters(profile->stage_kind);
    GroundParam* p=stage_info.param;
    Ground_801C38D0(p->x8,p->x14,p->x1C,p->x18);Ground_801C38EC(p->x10,p->xC);Ground_801C3970(p->x28);
    Ground_801C3900(p->x2E,p->x30,p->x34,p->x38,p->x3C,p->x40,p->x44,p->x48);
    Ground_801C392C(p->x50,p->x54,p->x58,p->x5C,p->x60,p->x64);Ground_801C3960(p->x20);Ground_801C3950(p->x24);
    active=h;
    for(unsigned i=0;i<m->pair_count;i++) {
        Vec3 pos;
        if(!Ground_801C2D24(m->pairs[i][1],&pos)||!isfinite(pos.x)||!isfinite(pos.y)||!isfinite(pos.z)) {
            melee_web_stage_numeric_end(h,NULL,0);fail(e,n,"Invalid original marker world position");return NULL;
        }
    }
    Ground_801C39C0();Ground_801C3BB4();
    StageBlastZone* camera=&stage_info.cam_info.cam_bounds;StageBlastZone* blast=&stage_info.blast_zone;
    float ranges[8];memcpy(ranges,camera,16);memcpy(ranges+4,blast,16);int finite=1;
    for(unsigned i=0;i<8;i++)if(!isfinite(ranges[i]))finite=0;
    if(!finite||!(camera->left<camera->right&&camera->bottom<camera->top&&blast->left<blast->right&&blast->bottom<blast->top)) {
        melee_web_stage_numeric_end(h,NULL,0);fail(e,n,"Invalid source camera or blast range");return NULL;
    }
    if(e&&n)*e=0;return h;
}
MeleeWebStageNumeric* melee_web_stage_numeric_begin(MeleeWebStageMarkers* m,char* e,size_t n){
    return melee_web_stage_numeric_begin_kind(m,St_Kind_Last,e,n);
}
int melee_web_stage_numeric_bounds(MeleeWebStageNumeric* h,float camera[4],float blast[4],float offset[2],char* e,size_t n){
    if(!h||active!=h||!h->owner||!camera||!blast||!offset)return fail(e,n,"Stage numeric context is not live");
    memcpy(camera,&stage_info.cam_info.cam_bounds,16);memcpy(blast,&stage_info.blast_zone,16);offset[0]=stage_info.cam_info.cam_x_offset;offset[1]=stage_info.cam_info.cam_y_offset;if(e&&n)*e=0;return 1;
}
int melee_web_stage_numeric_spawn(MeleeWebStageNumeric* h,uint32_t slot,float position[3],char* e,size_t n){
    if(!h||active!=h||!h->owner)return fail(e,n,"Stage numeric context is not live");
    if(slot>3)return fail(e,n,"Player spawn marker slot must be 0..3");
    if(!position)return fail(e,n,"Player spawn output is required");
    Vec3 source;
    if(!Ground_801C2D24((enum_t)slot,&source))return fail(e,n,"Original player spawn marker is unavailable");
    if(!isfinite(source.x)||!isfinite(source.y)||!isfinite(source.z))return fail(e,n,"Original player spawn marker is nonfinite");
    position[0]=source.x;position[1]=source.y;position[2]=source.z;
    if(e&&n)*e=0;return 1;
}
int melee_web_stage_numeric_end(MeleeWebStageNumeric* h,char* e,size_t n){
    if(!h)return 1;if(active!=h)return fail(e,n,"Stage numeric scope is not active");
    stage_info=h->saved;active=NULL;if(h->owner)HSD_GObjPLink_80390228(h->owner);free(h);if(e&&n)*e=0;return 1;
}

void* melee_web_stage_markers_descriptor(MeleeWebStageMarkers* m){return m?m->root:NULL;}
