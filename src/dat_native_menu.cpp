#include "dat_native_menu.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/fog.h>
#include <melee/sc/types.h>
#pragma GCC diagnostic pop
#include <cmath>
#include <cstring>
#include <map>
#include <set>
namespace melee_web {
namespace { void require(bool value,const char* message){if(!value)throw DatError(message);} }
struct DatNativeMenu::Storage {
    // CSSSceneModels + CSSAnimSet[] and the SSS table share this source prefix.
    struct Scene {
        HSD_CObjDesc* camera;
        HSD_LightDesc* light0;
        HSD_LightDesc* light1;
        HSD_FogDesc* fog;
        StaticModelDesc models[12];
    } scene{};
#ifdef __wasm__
    static_assert(offsetof(Scene,models)==0x10);
    static_assert(sizeof(StaticModelDesc)==0x10);
#endif
    std::shared_ptr<const DatArchive> archive;
    std::vector<std::shared_ptr<void>> memory;
    std::map<uint32_t,HSD_CObjDesc*> cameras;
    std::map<uint32_t,HSD_LightDesc*> lights;
    std::set<uint32_t> active;
    std::vector<std::unique_ptr<DatNativeJoint>> graphs;
    std::vector<MeleeWebNativeJoint*> natives;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    unsigned count=0;
    ~Storage(){for(auto* native:natives)if(!melee_web_native_joint_destroy(native,nullptr,0))std::terminate();}
    template<class T>T* make(size_t n=1){require(n<=65536,"Native menu allocation budget");auto p=std::shared_ptr<T[]>(new T[n]{});auto* out=p.get();memory.emplace_back(p,out);return out;}
    void record(uint32_t o,size_t n){require(!(o&3),"Native menu record alignment");(void)archive->range(o,n);require(n<=archive->next_target_offset(o)-o,"Native menu record crosses referenced region");}
    uint32_t pointer(uint32_t slot,size_t n){auto p=archive->pointer(slot,n);require(bool(p),"Native menu required pointer missing");return *p;}
    float number(uint32_t o){float f=archive->f32(o);require(std::isfinite(f),"Nonfinite native menu scalar");return f;}
    Vec3* vector(uint32_t o){record(o,12);auto* v=make<Vec3>();v->x=number(o);v->y=number(o+4);v->z=number(o+8);return v;}
    HSD_WObjDesc* world(std::optional<uint32_t> o){if(!o)return nullptr;record(*o,20);require(!archive->pointer(*o)&&!archive->pointer(*o+16),"Native menu WObj class/constraints unsupported");auto* w=make<HSD_WObjDesc>();w->pos=*vector(*o+4);return w;}
    HSD_CObjDesc* camera(uint32_t o){
        if(cameras.contains(o))return cameras.at(o);require(cameras.size()<32,"Native menu camera count exceeds budget");record(o,48);require(!archive->pointer(o),"Custom menu camera class unsupported");auto* d=make<HSD_CObjDesc>();auto& c=d->common;
        c.flags=archive->be16(o+4);c.projection_type=archive->be16(o+6);require(c.projection_type>=1&&c.projection_type<=3,"Invalid menu camera projection");
        c.viewport={int16_t(archive->be16(o+8)),int16_t(archive->be16(o+10)),int16_t(archive->be16(o+12)),int16_t(archive->be16(o+14))};
        c.scissor={archive->be16(o+16),archive->be16(o+18),archive->be16(o+20),archive->be16(o+22)};
        c.eyepos=world(archive->pointer(o+24,20));c.interest=world(archive->pointer(o+28,20));c.roll=number(o+32);if(auto p=archive->pointer(o+36,12))c.up_vector=vector(*p);
        c.nnear=number(o+40);c.ffar=number(o+44);require(c.nnear>0&&c.ffar>c.nnear,"Invalid menu camera clip range");
        if(c.projection_type==1){record(o,56);d->perspective.fov=number(o+48);d->perspective.aspect=number(o+52);require(d->perspective.fov>0&&d->perspective.fov<180&&d->perspective.aspect>0,"Invalid menu perspective");}
        else{record(o,64);d->frustum.top=number(o+48);d->frustum.bottom=number(o+52);d->frustum.left=number(o+56);d->frustum.right=number(o+60);require(d->frustum.top!=d->frustum.bottom&&d->frustum.left!=d->frustum.right,"Degenerate menu projection");}
        cameras[o]=d;return d;
    }
    HSD_LightDesc* light(uint32_t o){
        if(lights.contains(o))return lights.at(o);require(lights.size()+active.size()<256,"Native menu light count exceeds budget");record(o,28);require(active.insert(o).second,"Native light descriptor cycle");require(!archive->pointer(o),"Custom native light class unsupported");auto* d=make<HSD_LightDesc>();d->flags=archive->be16(o+8);d->attnflags=archive->be16(o+10);
        require((d->flags&3)<=1,"Menu point/spot descriptor not implemented");std::memcpy(&d->color,archive->range(o+12,4).data(),4);d->position=world(archive->pointer(o+16,20));d->interest=world(archive->pointer(o+20,20));if(auto p=archive->pointer(o+24,4)){d->u.shininess=make<float>();*d->u.shininess=number(*p);}
        if(auto p=archive->pointer(o+4,28))d->next=light(*p);active.erase(o);lights[o]=d;return d;
    }
    HSD_FogDesc* fog(uint32_t o){record(o,20);auto* d=make<HSD_FogDesc>();d->type=archive->be32(o);require(d->type<=15,"Invalid source fog type");d->start=number(o+8);d->end=number(o+12);std::memcpy(&d->color,archive->range(o+16,4).data(),4);
        if(auto p=archive->pointer(o+4,68)){record(*p,68);auto* f=make<HSD_FogAdjDesc>();f->center=archive->be16(*p);f->width=archive->be16(*p+2);for(unsigned i=0;i<16;i++)f->mtx[i/4][i%4]=number(*p+4+4*i);d->fogadjdesc=f;}return d;}
};
DatNativeMenu::DatNativeMenu(std::shared_ptr<const DatArchive> archive,NativeMenuKind kind)
    :storage_(std::make_unique<Storage>()){
    auto& s=*storage_;require(bool(archive),"Native menu archive missing");s.archive=archive;
    const auto name=kind==NativeMenuKind::Characters?"MnSelectChrDataTable":"MnSelectStageDataTable";
    std::optional<uint32_t> root;
    for(const auto& symbol:archive->public_symbols())if(symbol.name==name)root=symbol.data_offset;
    require(root.has_value(),"Native menu public scene table missing");
    s.count=kind==NativeMenuKind::Characters?9:12;
    s.record(*root,16+16*s.count);
    s.scene.camera=s.camera(s.pointer(*root,48));
    s.scene.light0=s.light(s.pointer(*root+4,28));
    s.scene.light1=s.light(s.pointer(*root+8,28));
    s.scene.fog=s.fog(s.pointer(*root+12,20));
    for(unsigned i=0;i<s.count;i++){
        const uint32_t row=*root+16+16*i;auto& out=s.scene.models[i];
        auto graph=std::make_unique<DatNativeJoint>(archive,s.pointer(row,64));
        char error[256];auto* native=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native,error);s.natives.push_back(native);
        out.joint=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native,error,sizeof(error)));require(out.joint,error);
        if(auto p=archive->pointer(row+4,20)){
            auto animation=std::make_unique<DatNativeAnimation>(archive,*p,graph->graph());
            out.animjoint=static_cast<HSD_AnimJoint*>(animation->descriptor());s.animations.push_back(std::move(animation));
        }
        if(auto p=archive->pointer(row+8,12)){
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*p,graph->graph(),TextureIndexValidation::DispatchedValues);
            out.matanim_joint=static_cast<HSD_MatAnimJoint*>(animation->descriptor());s.materials.push_back(std::move(animation));
        }
        if(auto p=archive->pointer(row+12,12)){
            auto animation=std::make_unique<DatShapeAnimation>(archive,*p,graph->graph());
            out.shapeanim_joint=animation->descriptor();s.shapes.push_back(std::move(animation));
        }
        s.graphs.push_back(std::move(graph));
    }
}
DatNativeMenu::~DatNativeMenu()=default;
void* DatNativeMenu::descriptor()const noexcept{return &storage_->scene;}
unsigned DatNativeMenu::model_count()const noexcept{return storage_->count;}
}
