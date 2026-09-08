#include "dat_item_article.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "dat_item_commands.hpp"
#include "native_dat.hpp"
#include "gameplay_article_data.h"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#include <cmath>
#include <cstring>
namespace melee_web {
namespace {
void require(bool c,const char* message){if(!c)throw DatError(message);}
void destroy(MeleeWebNativeJoint* p){if(p&&!melee_web_native_joint_destroy(p,nullptr,0))std::terminate();}
}
struct DatItemArticle::Storage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    std::unique_ptr<DatNativeJoint> model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy)> native{nullptr,destroy};
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    DatItemCommands commands;
    MeleeWebItemStateDesc states[8]{};
    uint32_t count=0;
    explicit Storage(std::shared_ptr<const DatArchive> a):archive(a),arena(std::move(a)){}
};
DatItemArticle::DatItemArticle(std::shared_ptr<const DatArchive> archive,uint32_t root,uint32_t kind,void* article)
    :storage_(std::make_unique<Storage>(archive))
{
    require(bool(archive)&&article,"Item article requires owned source and registered identity");
    require(kind==It_Kind_Mario_Fire||kind==It_Kind_Mario_Cape,"Item kind has no checked native article schema");
    auto& s=*storage_;const auto& a=*archive;char error[256];
    auto record=[&](uint32_t at,size_t size){require(!(at&3)&&size<=a.next_target_offset(at)-at,"Item descriptor crosses source region");(void)a.range(at,size);};
    auto pointer=[&](uint32_t at,size_t size){auto p=a.pointer(at,size);require(bool(p),"Required item descriptor missing");record(*p,size);return *p;};
    record(root,24);const uint32_t special_size=kind==It_Kind_Mario_Fire?20:4;
    uint32_t at=pointer(root+4,special_size);const auto* reader=s.arena.reader();
    void* special=reader->allocate(reader->context,special_size/4,4);
    for(uint32_t i=0;i<special_size;i+=4){
        uint32_t value=a.be32(at+i);require(!a.has_relocation(at+i),"Item special scalar is relocated");
        if(kind==It_Kind_Mario_Fire)require(std::isfinite(a.f32(at+i)),"Item special scalar is nonfinite");
        std::memcpy(static_cast<uint8_t*>(special)+i,&value,4);
    }
    at=pointer(root+16,16);const uint32_t model_root=pointer(at,64);
    s.model=std::make_unique<DatNativeJoint>(archive,model_root);const auto& graph=s.model->graph();
    const uint32_t bones=a.be32(at+4);const int32_t attach=int32_t(a.be32(at+8));
    const uint8_t flags=a.range(at+12,1)[0];
    require(!a.has_relocation(at+4)&&!a.has_relocation(at+8)&&!a.has_relocation(at+12),"Item model scalar is relocated");
    require((bones==0||bones==graph.joint_count)&&graph.joint_count<=140,
            "Item model bone count does not match native topology");
    require(attach>=0&&uint32_t(attach)<graph.joint_count,"Item attachment bone is outside native model");
    s.native.reset(melee_web_native_joint_hydrate(&graph,error,sizeof(error)));require(bool(s.native),error);
    void* joint=melee_web_native_joint_descriptor(s.native.get(),error,sizeof(error));require(joint,error);
    std::vector<void*> descriptors;
    for(uint32_t j=0;j<graph.joint_count;j++){
        void* d=melee_web_native_joint_descriptor_at(s.native.get(),j,graph.joints[j].source_offset,error,sizeof(error));require(d,error);descriptors.push_back(d);
    }
    s.count=kind==It_Kind_Mario_Fire?1:2;at=pointer(root+12,s.count*16);
    for(uint32_t i=0;i<s.count;i++){
        const uint32_t row=at+16*i;auto& state=s.states[i];
        if(auto p=a.pointer(row,20)){
            // Item animation containers use the original particle callback
            // channel for source effects (Mario's fireball and cape each carry
            // one). Preserve those packed event tracks so the source HSD
            // updater can dispatch them after the matching effect bank lives.
            auto animation=std::make_unique<DatNativeAnimation>(archive,*p,graph,DatNativeAnimationPolicy::ParticleDescriptors,descriptors);
            state.animation=animation->descriptor();s.animations.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+4,12)){
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*p,graph);
            state.material=animation->descriptor();s.materials.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+8,12)){
            auto animation=std::make_unique<DatShapeAnimation>(archive,*p,graph);
            state.shape=animation->descriptor();s.shapes.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+12,4))state.commands=s.commands.decode(a,*p);
    }
    require(melee_web_article_publish(reader,article,special,s.states,s.count,joint,bones,attach,flags,error,sizeof(error)),error);
}
DatItemArticle::~DatItemArticle()=default;
uint32_t DatItemArticle::state_count()const noexcept{return storage_->count;}
}
