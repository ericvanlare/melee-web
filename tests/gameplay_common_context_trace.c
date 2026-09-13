#include "gameplay_common_context.h"
#include "gameplay_bootstrap.h"
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0C8C.h>
#include <melee/ft/ftCo_800C7CA0.h>
#include <melee/sfx/crowdsfx.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"common context check line%d: %s (%s)\n",__LINE__,#c,error);abort();}}while(0)
static unsigned cpu_destroyed;
static void cpu_destroy(MeleeWebCommonCpuData* data)
{
    (void)data;
    ++cpu_destroyed;
}
int main(void)
{
    char error[256];
    void* saved[23];
#define SAVE(index,name) saved[index]=(void*)name;
    MELEE_WEB_COMMON_ROOTS(SAVE)
#undef SAVE
    HSD_MObj* old_materials[]={ft_804D6580,ft_804D6588};
    for(unsigned pass=0;pass<2;++pass){
        MeleeWebCommonScalars scalars={0};scalars.walk_stick_threshold=0.18f;
        scalars.x7D8=(MeleeWebCommonColor){7,11,13,17};
        MeleeWebCommonTables tables={0};tables.ready_mask=1u<<4;
        unsigned cpu_storage=0xC0DEC0DE;
        MeleeWebCommonCpuData cpu_data={&cpu_storage,&cpu_storage,1,cpu_destroy};
        tables.cpu_data=&cpu_data;
        for(unsigned i=0;i<MELEE_WEB_COMMON_FIGHTERS;++i)tables.parts[i].part_count=1;
        tables.none_parts.part_count=1;
        MeleeWebNativeJointDesc joint={0};joint.child=joint.next=UINT32_MAX;
        joint.scale[0]=joint.scale[1]=joint.scale[2]=1;
        MeleeWebNativeDObjDesc dobj={0};dobj.next=dobj.pobj=UINT32_MAX;
        uint8_t pixels[32];memset(pixels,0x73,sizeof(pixels));
        MeleeWebNativeTextureDesc texture={0};texture.texture.source=4;texture.texture.flags=0x40010;
        texture.texture.scale[0]=texture.texture.scale[1]=texture.texture.scale[2]=1;
        texture.texture.image_data=pixels;texture.texture.image_bytes=32;
        texture.texture.width=texture.texture.height=8;
        MeleeWebNativeMaterialDesc material={0};material.material.rendermode=4;material.material.alpha=1;
        material.material.diffuse[0]=123;material.material.texture_count=1;material.textures=&texture;
        MeleeWebNativeGraph graph={&joint,&dobj,NULL,&material,1,1,0,1,0};
        MeleeWebCommonContext* context=melee_web_common_context_create(&scalars,&tables,&graph,error,sizeof(error));
        CHECK(context);memset(pixels,0,sizeof(pixels));scalars.walk_stick_threshold=0;
        MeleeWebNativeJoint* root16=melee_web_native_joint_hydrate(&graph,error,sizeof(error));
        CHECK(root16);
        void* root16_descriptor=melee_web_native_joint_descriptor(root16,error,sizeof(error));
        CHECK(root16_descriptor);
        CHECK(melee_web_common_context_set_root16(context,root16_descriptor,error,sizeof(error)));
        CHECK(!melee_web_common_context_set_root16(context,
            melee_web_native_joint_descriptor(root16,error,sizeof(error)),error,sizeof(error)));
        CHECK(melee_web_gameplay_startup(4U*1024U*1024U,error,sizeof(error)));
        CHECK(melee_web_common_context_attach(context,error,sizeof(error)));
        CHECK(!melee_web_common_context_attach(context,error,sizeof(error)));
        CHECK(melee_web_common_context_require(context,1u|(1u<<4)|(1u<<16)|(1u<<20),error,sizeof(error)));
        CHECK(melee_web_common_context_require(context,1u<<22,error,sizeof(error)));
        CHECK(!melee_web_common_context_require(context,1u<<6,error,sizeof(error)));
        CHECK(strstr(error,"missing0x000040"));
        void** roots=melee_web_common_context_source_roots();
        CHECK(p_ftCommonData->walk_stick_threshold==0.18f&&ftPartsTable[0]->parts_num==1);
        for(unsigned i=0;i<23;++i)CHECK((roots[i]!=NULL)==(i==0||i==4||i==16||i==20||i==22));
        CHECK(roots[16]==root16_descriptor);
        CHECK((void*)Fighter_804D64FC==&cpu_storage);
        HSD_Joint* copied=roots[20];
        CHECK(copied->u.dobjdesc->mobjdesc->texdesc->imagedesc->image_ptr!=pixels);
        CHECK(((uint8_t*)copied->u.dobjdesc->mobjdesc->texdesc->imagedesc->image_ptr)[0]==0x73);
        CHECK(melee_web_common_context_initialize_materials(context,error,sizeof(error)));
        CHECK(!melee_web_common_context_initialize_materials(context,error,sizeof(error)));
        CHECK(ft_804D6580&&ft_804D6588&&ft_804D6580!=ft_804D6588);
        CHECK(ft_804D6580->mat->diffuse.r==123);
        CHECK(ft_804D6588->mat->diffuse.r==7&&ft_804D6588->mat->diffuse.a==17);
        HSD_GObj* fighter=GObj_Create(HSD_GOBJ_CLASS_FIGHTER,8,0);CHECK(fighter);
        CHECK(!melee_web_common_context_destroy(context,error,sizeof(error)));
        HSD_GObjPLink_80390228(fighter);
        if(pass)CHECK(melee_web_gameplay_shutdown(error,sizeof(error)));
        CHECK(melee_web_common_context_destroy(context,error,sizeof(error)));
        CHECK(melee_web_native_joint_destroy(root16,error,sizeof(error)));
#define RESTORED(index,name) CHECK((void*)name==saved[index]);
        MELEE_WEB_COMMON_ROOTS(RESTORED)
#undef RESTORED
        CHECK(ft_804D6580==old_materials[0]&&ft_804D6588==old_materials[1]);
        CHECK(cpu_data.refs==1);
        melee_web_common_cpu_release(&cpu_data);
        CHECK(cpu_data.refs==0&&cpu_destroyed==pass+1);
        if(!pass)CHECK(melee_web_gameplay_shutdown(error,sizeof(error)));
    }
    puts("Original common8064/8F6C persistent ownership/publication/restoration/restart passed");
}
