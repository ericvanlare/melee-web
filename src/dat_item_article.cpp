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
struct ArticleSchema { uint32_t special_bytes, state_count; bool special_required; };
ArticleSchema schema(uint32_t kind)
{
    switch(kind) {
    case It_Kind_Mario_Fire:return {20,1,true};
    // Luigi's fireball uses the shared five-float source type selectively:
    // its callback consumes x0, x4 and xC, while x8/x10 belong to Mario's
    // distinct fireball record. The authored Luigi region is four floats.
    case It_Kind_Luigi_Fire:return {16,1,true};
    // Koopa's Flame uses six source floats, one serialized state row, and
    // the source-valid null-joint ItemModelDesc form.
    case It_Kind_Koopa_Flame:return {24,1,true};
    // Ness's authored scalar extents come from the PlNs.dat article regions.
    // PK Fire's own record is two floats; the pillar record adds the scale.
    // PK Flush carries the eleven-float itFlashAttributes record with three
    // serialized animation rows, PK Thunder the five-float
    // itPKThunderAttributes record. The four Thunder trails share one
    // single-float record, the bat keeps one authored scalar, and the Yo-Yo's
    // 0x5C itYoyoAttributes record ends at its three source joint/material
    // pointers with no serialized state table.
    case It_Kind_Ness_PKFire:return {8,1,true};
    case It_Kind_Ness_PKFire_Flame:return {12,1,true};
    case It_Kind_Ness_PKFlush:return {0x2C,3,true};
    case It_Kind_Ness_PKThunder:return {0x14,1,true};
    case It_Kind_Ness_PKThunder1:
    case It_Kind_Ness_PKThunder2:
    case It_Kind_Ness_PKThunder3:
    case It_Kind_Ness_PKThunder4:return {4,1,true};
    case It_Kind_Ness_PKFlush_Explode:return {0x14,1,true};
    case It_Kind_Ness_Bat:return {4,1,true};
    case It_Kind_Ness_Yoyo:return {0x5C,0,true};
    // Peach's authored scalar extents come from the PlPe.dat article regions.
    // The Bomber explosion has no special record, two command-only state rows
    // and the source-valid null-joint ItemModelDesc form. The vegetable's
    // itPeachTurnipAttributes record is its lifetime float, the authored
    // turnip-type count and eight {odds, damage} pairs; the parasol and Toad
    // keep one authored unread scalar word each; the spore record is the
    // four-float itPeachToadSporeAttributes with one command-only state row.
    case It_Kind_Peach_Explode:return {0,2,false};
    case It_Kind_Peach_Turnip:return {0x48,3,true};
    case It_Kind_Peach_Parasol:return {4,2,true};
    case It_Kind_Peach_Toad:return {4,2,true};
    case It_Kind_Peach_ToadSpore:return {0x10,1,true};
    // Seven original pill motion states select six serialized animation rows,
    // including the throw/catch sequences used by Dr. Mario's taunt.
    case It_Kind_DrMario_Vitamin:return {20,6,true};
    // Thunder has three source callbacks, but the callback state IDs are
    // [-1, 0, 0] and the DAT carries one serialized animation descriptor.
    case It_Kind_Pikachu_Thunder:
    case It_Kind_Pichu_Thunder:return {12,1,true};
    case It_Kind_Pikachu_TJolt_Ground:
    case It_Kind_Pichu_TJolt_Ground:return {16,2,true};
    case It_Kind_Pikachu_TJolt_Air:
    case It_Kind_Pichu_TJolt_Air:return {4,1,true};
    case It_Kind_Mario_Cape:
    case It_Kind_DrMario_Sheet:return {4,2,true};
    case It_Kind_Fox_Laser:
    case It_Kind_Falco_Laser:return {40,2,true};
    case It_Kind_Fox_Blaster:
    case It_Kind_Falco_Blaster:return {40,9,true};
    case It_Kind_Fox_Illusion:
    case It_Kind_Falco_Phantasm:return {8,3,true};
    case It_Kind_Heiho:return {24,3,true};
    // Serialized animation descriptors, not ItemStateTable callback counts.
    // Bomb also selects row 3 directly for its fuse; Hookshot uses only -1
    // animation IDs. Link and Young Link retain distinct ItemKinds.
    case It_Kind_Link_Bomb:
    case It_Kind_CLink_Bomb:return {0x34,4,true};
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return {0x64,3,true};
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return {0x60,0,true};
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return {0x2c,1,true};
    case It_Kind_Link_Bow:
    case It_Kind_CLink_Bow:return {8,6,false};
    case It_Kind_CLink_Milk:return {4,2,false};
    default:throw DatError("Item kind has no checked native article schema");
    }
}
bool pointer_field(uint32_t kind,uint32_t offset)
{
    switch(kind) {
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return offset==0x54||offset==0x58||offset==0x5c;
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return offset==0x44||offset==0x48||
        (offset>=0x4c&&offset<0x64&&((offset-0x4c)%4==0));
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return offset==0x24||offset==0x28;
    // The Yo-Yo's serialized record ends with the original HSD_Joint string,
    // HSD_Joint yoyo and HSD_MatAnimJoint material pointers. The asset owner
    // hydrates them into native descriptors below.
    case It_Kind_Ness_Yoyo:return offset==0x50||offset==0x54||offset==0x58;
    default:return false;
    }
}
bool float_field(uint32_t kind,uint32_t offset)
{
    if(pointer_field(kind,offset))return false;
    switch(kind) {
    case It_Kind_Link_Bomb:
    case It_Kind_CLink_Bomb:return offset!=0x0&&offset!=0xc&&offset!=0x10;
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return offset>=0xc&&offset<0x44;
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return offset!=0xc&&offset!=0x2c;
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return true;
    // The Yo-Yo record keeps its original integer words (string segment
    // counts at 0x00..0x08, rotation frames at 0x40..0x4C) beside the float
    // scalars at 0x0C..0x3C.
    case It_Kind_Ness_Yoyo:return offset>=0xC&&offset<=0x3C;
    // The vegetable record's lifetime is its only float; the odds/damage
    // pairs and the authored turnip-type count are integers. The parasol and
    // Toad records keep one unread integer word each, while the spore record
    // is four serialized floats.
    case It_Kind_Peach_Turnip:return offset==0x0;
    case It_Kind_Peach_Parasol:
    case It_Kind_Peach_Toad:return false;
    default:return true;
    }
}
void write_native_pointer(void* destination,std::size_t offset,void* value)
{
    require(sizeof(void*)==4,"Native item descriptor requires the 32-bit gameplay target");
    const auto address=reinterpret_cast<std::uintptr_t>(value);
    require(address<=UINT32_MAX,"Native item descriptor pointer exceeds source width");
    const auto encoded=static_cast<uint32_t>(address);
    std::memcpy(static_cast<uint8_t*>(destination)+offset,&encoded,sizeof(encoded));
}
}
struct DatItemArticle::Storage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    std::unique_ptr<DatNativeJoint> model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy)> native{nullptr,destroy};
    std::vector<std::unique_ptr<DatNativeJoint>> special_models;
    using SpecialNative = std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy)>;
    std::vector<SpecialNative> special_native;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    DatItemCommands commands;
    std::unique_ptr<MeleeWebItemStateDesc[]> states;
    uint32_t count=0;
    explicit Storage(std::shared_ptr<const DatArchive> a):archive(a),arena(std::move(a)){}
};
DatItemArticle::DatItemArticle(std::shared_ptr<const DatArchive> archive,uint32_t root,uint32_t kind,void* article)
    :storage_(std::make_unique<Storage>(archive))
{
    require(bool(archive)&&article,"Item article requires owned source and registered identity");
    const auto article_schema=schema(kind);
    auto& s=*storage_;const auto& a=*archive;char error[256];
    auto record=[&](uint32_t at,size_t size){
        if((at&3)||size>a.next_target_offset(at)-at)
            throw DatError("Item kind "+std::to_string(kind)+" descriptor at "+std::to_string(at)+
                           " crosses source region (requested "+std::to_string(size)+" bytes)");
        (void)a.range(at,size);
    };
    auto pointer=[&](uint32_t at,size_t size){
        auto p=a.pointer(at,size);
        if(!p) {
            throw DatError("Required item descriptor missing at DAT slot " +
                           std::to_string(at) + " (" + std::to_string(size) +
                           " bytes, item kind " + std::to_string(kind) + ")");
        }
        record(*p,size);return *p;
    };
    record(root,24);const uint32_t special_size=article_schema.special_bytes;
    std::optional<uint32_t> special_at;
    if(special_size) {
        special_at=a.pointer(root+4,special_size);
        require(special_at||!article_schema.special_required,"Required item special attributes missing");
        if(special_at)record(*special_at,special_size);
    }
    const auto* reader=s.arena.reader();
    void* special=special_size&&special_at?
        reader->allocate(reader->context,special_size/4,4):nullptr;
    uint32_t first_special_word=0;
    if(kind==It_Kind_Heiho&&special_at){
        require(sizeof(void*)==4,"Heiho Article hydration requires the 32-bit gameplay target");
        const uint32_t scalar_at=pointer(*special_at,4);
        require(!a.has_relocation(scalar_at),"Heiho threshold scalar is relocated");
        auto* scalar=static_cast<int32_t*>(reader->allocate(reader->context,1,sizeof(int32_t)));
        const uint32_t value=a.be32(scalar_at);std::memcpy(scalar,&value,4);
        require(*scalar>=0&&*scalar<=10000,"Heiho threshold scalar is outside source bounds");
        std::memcpy(special,&scalar,sizeof(scalar));
        first_special_word=4;
    }
    if(special_at) for(uint32_t i=first_special_word;i<special_size;i+=4){
        if(pointer_field(kind,i))continue;
        const auto scalar_at=*special_at+i;
        require(!a.has_relocation(scalar_at),"Item special scalar is relocated");
        uint32_t value=a.be32(scalar_at);
        if(float_field(kind,i))require(std::isfinite(a.f32(scalar_at)),"Item special scalar is nonfinite");
        std::memcpy(static_cast<uint8_t*>(special)+i,&value,4);
    }
    uint32_t at=pointer(root+16,16);
    const auto model_root=a.pointer(at,64);
    const bool null_model=!model_root;
    const uint32_t bones=a.be32(at+4);const int32_t attach=int32_t(a.be32(at+8));
    const uint8_t flags=a.range(at+12,1)[0];
    require(!a.has_relocation(at+4)&&!a.has_relocation(at+8)&&!a.has_relocation(at+12),"Item model scalar is relocated");
    MeleeWebNativeGraph empty_graph{};
    const MeleeWebNativeGraph* graph_ptr=&empty_graph;
    void* joint=nullptr;
    std::vector<void*> descriptors;
    if(null_model) {
        /* item.c copies this authored null to xC8_joint; Item_802680CC then
         * creates the source identity JObj. Keep the model descriptor and
         * its scalar fields while requiring the zero-bone form below. */
        require(bones==0&&attach==0,
                "Null item model has nonzero bone or attachment fields");
    } else {
        record(*model_root,64);
        s.model=std::make_unique<DatNativeJoint>(archive,*model_root);
        graph_ptr=&s.model->graph();
        const auto& graph=*graph_ptr;
        require((bones==0||bones==graph.joint_count)&&graph.joint_count<=140,
                "Item model bone count does not match native topology");
        require(attach>=0&&uint32_t(attach)<graph.joint_count,
                "Item attachment bone is outside native model");
        s.native.reset(melee_web_native_joint_hydrate(&graph,error,sizeof(error)));
        require(bool(s.native),error);
        joint=melee_web_native_joint_descriptor(s.native.get(),error,sizeof(error));
        require(joint,error);
        for(uint32_t j=0;j<graph.joint_count;j++){
            void* d=melee_web_native_joint_descriptor_at(s.native.get(),j,graph.joints[j].source_offset,error,sizeof(error));require(d,error);descriptors.push_back(d);
        }
    }
    const auto& graph=*graph_ptr;

    auto special_joint=[&](uint32_t offset)->DatNativeJoint* {
        require(bool(special_at),"Item special joint has no special attributes");
        const auto target=pointer(*special_at+offset,64);
        auto model=std::make_unique<DatNativeJoint>(archive,target);
        Storage::SpecialNative owner(nullptr,destroy);owner.reset(melee_web_native_joint_hydrate(
            &model->graph(),error,sizeof(error)));require(bool(owner),error);
        void* descriptor=melee_web_native_joint_descriptor(owner.get(),error,sizeof(error));require(descriptor,error);
        write_native_pointer(special,offset,descriptor);
        DatNativeJoint* result=model.get();s.special_models.push_back(std::move(model));
        s.special_native.push_back(std::move(owner));return result;
    };
    auto hydrate_joint_descriptors=[&](const MeleeWebNativeGraph& value,MeleeWebNativeJoint* owner) {
        std::vector<void*> result;result.reserve(value.joint_count);
        for(uint32_t j=0;j<value.joint_count;j++) {
            void* descriptor=melee_web_native_joint_descriptor_at(owner,j,value.joints[j].source_offset,error,sizeof(error));
            require(descriptor,error);result.push_back(descriptor);
        }
        return result;
    };
    auto special_animation=[&](uint32_t slot,const MeleeWebNativeGraph& value,
                               MeleeWebNativeJoint* owner) {
        if(auto target=a.pointer(*special_at+slot,20)) {
            auto descriptors=hydrate_joint_descriptors(value,owner);
            auto animation=std::make_unique<DatNativeAnimation>(archive,*target,value,
                DatNativeAnimationPolicy::Transforms,descriptors);
            void* descriptor=animation->descriptor();require(descriptor,"Item animation descriptor is null");
            write_native_pointer(special,slot,descriptor);s.animations.push_back(std::move(animation));
        }
    };
    auto special_material=[&](uint32_t slot,const MeleeWebNativeGraph& value) {
        if(auto target=a.pointer(*special_at+slot,12)) {
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*target,value);
            void* descriptor=animation->descriptor();require(descriptor,"Item material descriptor is null");
            write_native_pointer(special,slot,descriptor);s.materials.push_back(std::move(animation));
        }
    };
    auto special_shape=[&](uint32_t slot,const MeleeWebNativeGraph& value) {
        if(auto target=a.pointer(*special_at+slot,12)) {
            auto animation=std::make_unique<DatShapeAnimation>(archive,*target,value);
            void* descriptor=animation->descriptor();require(descriptor,"Item shape descriptor is null");
            write_native_pointer(special,slot,descriptor);s.shapes.push_back(std::move(animation));
        }
    };
    if(kind==It_Kind_Link_HShot||kind==It_Kind_CLink_HShot) {
        for(const auto offset:{0x54U,0x58U,0x5cU}) (void)special_joint(offset);
    } else if(kind==It_Kind_Link_Arrow||kind==It_Kind_CLink_Arrow) {
        for(const auto offset:{0x24U,0x28U}) (void)special_joint(offset);
    } else if(kind==It_Kind_Link_Boomerang||kind==It_Kind_CLink_Boomerang) {
        auto first=special_joint(0x44),second=special_joint(0x48);
        MeleeWebNativeJoint* first_native=nullptr;MeleeWebNativeJoint* second_native=nullptr;
        /* Find the matching native owners by the stable model graph pointer. */
        for(size_t i=0;i<s.special_models.size();++i) {
            if(s.special_models[i].get()==first)first_native=s.special_native[i].get();
            if(s.special_models[i].get()==second)second_native=s.special_native[i].get();
        }
        require(first_native&&second_native,"Boomerang special joint owner is missing");
        special_animation(0x4c,first->graph(),first_native);
        special_material(0x50,first->graph());special_shape(0x54,first->graph());
        special_animation(0x58,second->graph(),second_native);
        special_material(0x5c,second->graph());special_shape(0x60,second->graph());
    } else if(kind==It_Kind_Ness_Yoyo) {
        /* it_802BE65C loads the string and yoyo joints from the special
         * record and it_802BE5D8 attaches the material animation to the
         * loaded yoyo joint, so the material binds that joint's graph. */
        auto string_joint=special_joint(0x50);
        auto yoyo_joint=special_joint(0x54);
        (void)string_joint;
        special_material(0x58,yoyo_joint->graph());
    }
    s.count=article_schema.state_count;
    if(s.count){at=pointer(root+12,s.count*16);s.states=std::make_unique<MeleeWebItemStateDesc[]>(s.count);}
    else require(!a.pointer(root+12),"Animation-free item has an unexpected state descriptor table");
    for(uint32_t i=0;i<s.count;i++){
        const uint32_t row=at+16*i;auto& state=s.states[i];
        if(auto p=a.pointer(row,20)){
            require(!null_model,"Null item model has an animation descriptor");
            // Item animation containers use the original particle callback
            // channel for source effects (Mario's fireball and cape each carry
            // one). Preserve those packed event tracks so the source HSD
            // updater can dispatch them after the matching effect bank lives.
            auto animation=std::make_unique<DatNativeAnimation>(archive,*p,graph,DatNativeAnimationPolicy::ParticleDescriptors,descriptors);
            state.animation=animation->descriptor();s.animations.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+4,12)){
            require(!null_model,"Null item model has a material descriptor");
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*p,graph);
            state.material=animation->descriptor();s.materials.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+8,12)){
            require(!null_model,"Null item model has a shape descriptor");
            auto animation=std::make_unique<DatShapeAnimation>(archive,*p,graph);
            state.shape=animation->descriptor();s.shapes.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+12,4))state.commands=s.commands.decode(a,*p);
    }
    require(melee_web_article_publish(reader,article,special,s.states.get(),s.count,joint,bones,attach,flags,
                                      null_model?1:0,error,sizeof(error)),error);
}
DatItemArticle::~DatItemArticle()=default;
uint32_t DatItemArticle::state_count()const noexcept{return storage_->count;}
}
