#include "dat_native_joint.hpp"
#include "dat_stage.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/mobj.h>
#pragma GCC diagnostic pop
#include <fstream>
#include <iostream>
#include <stdexcept>
static void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
    check(argc==2,"stage asset path required");std::ifstream f(argv[1],std::ios::binary);
    check(bool(f),"stage asset unavailable");std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
    auto a=std::make_shared<melee_web::DatArchive>(bytes);melee_web::DatStage stage(*a);
    check(stage.entries.size()==10,"actual FD has ten entries");
    for(unsigned i=1;i<10;i++){
        const auto& e=stage.entries[i];check(bool(e.joint_offset),"actual visual entry has model");
        melee_web::DatNativeJoint model(a,*e.joint_offset);const auto& g=model.graph();
        check(g.dobj_count&&g.pobj_count,"complete visual geometry present");
        if(i==3){
            check(g.joint_count==5&&g.dobj_count==26&&g.pobj_count==26&&g.material_count==26,"complete main platform graph");
            check(bool(e.joint_animation_table)&&bool(e.material_animation_table),"actual platform animation tables present");
            melee_web::DatNativeAnimation j(a,*a->pointer(*e.joint_animation_table,20),g);
            melee_web::DatMaterialAnimation m(a,*a->pointer(*e.material_animation_table,12),g);
            check(j.descriptor()&&m.descriptor(),"actual platform joint and alpha descriptors ready");
        }
        if(i==4||i==9){
            const auto root=*a->pointer(*e.joint_animation_table,20);
            bool rejected=false;try{melee_web::DatNativeAnimation unsafe(a,root,g);}catch(const melee_web::DatError&){rejected=true;}
            check(rejected,"particle animations require explicit descriptor-only policy");
            melee_web::DatNativeAnimation events(a,root,g,melee_web::DatNativeAnimationPolicy::ParticleDescriptors);
            check(!events.particle_events().empty(),"particle descriptor readiness reports actual events");
            for(const auto& event:events.particle_events())check(event.bank==30&&event.command>=30000&&event.command<30010,"exact FD packed particle event references");
        }
        for(unsigned clip=0;clip<((i>=4&&i<=8)?11u:1u);clip++){
            if(e.joint_animation_table)if(auto p=a->pointer(*e.joint_animation_table+clip*4,20)){
                melee_web::DatNativeAnimation animation(a,*p,g,melee_web::DatNativeAnimationPolicy::ParticleDescriptors);
                auto* indexed=static_cast<HSD_AnimJoint*>(animation.indexed_descriptor());
                for(uint32_t k=0;k<g.joint_count;k++){
                    check(indexed[k].child==(g.joints[k].child==UINT32_MAX?nullptr:&indexed[g.joints[k].child]),"original direct bone indexing preserves joint animation child identity");
                    const auto source=a->pointer(*p+k*20+8,16);
                    check(bool(indexed[k].aobjdesc)==source.has_value(),"indexed animation payload corresponds to exact original array slot");
                    if(source)check(indexed[k].aobjdesc->end_frame==a->f32(*source+4),"direct indexed AObj range matches source");
                }
            }
            if(e.material_animation_table)if(auto p=a->pointer(*e.material_animation_table+clip*4,12)){
                melee_web::DatMaterialAnimation animation(a,*p,g);auto* indexed=static_cast<HSD_MatAnimJoint*>(animation.indexed_descriptor());
                for(uint32_t k=0;k<g.joint_count;k++)check(indexed[k].child==(g.joints[k].child==UINT32_MAX?nullptr:&indexed[g.joints[k].child]),"original direct bone indexing preserves material animation child identity");
            }
        }
        if(i>=4){
            bool rgba6=false;for(uint32_t p=0;p<g.pobj_count;p++)for(uint32_t v=0;v<g.pobjs[p].geometry.attribute_count;v++){
                const auto& attr=g.pobjs[p].geometry.attributes[v];rgba6|=attr.attr==11&&attr.attr_type==1&&attr.comp_type==4;
            }
            check(rgba6,"background retains actual direct RGBA6");
            bool rejected=false;try{melee_web::RigidModel viewer(a,*e.joint_offset,"background");}catch(const melee_web::DatError&){rejected=true;}
            check(rejected,"inspection policy still rejects native RGBA6 behavior");
        }
    }
    std::cout<<"FD complete nine visual graphs and main platform joint/alpha animation descriptors passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
