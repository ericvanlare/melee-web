#include "gameplay_fighter_assets.hpp"
#include "gameplay_fighter_data.h"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace melee_web {
namespace {
uint32_t root(const DatArchive& archive,std::string_view name)
{for(const auto& s:archive.public_symbols())if(s.name==name)return s.data_offset;throw DatError("Fighter asset root is missing: "+std::string(name));}
void destroy_joint(MeleeWebNativeJoint* joint)
{char error[128];if(!melee_web_native_joint_destroy(joint,error,sizeof(error))){std::fprintf(stderr,"%s\n",error);std::abort();}}
}
struct GameplayFighterAssets::Storage {
    std::shared_ptr<const DatArchive> fighter;
    FighterCostume identity;
    std::vector<uint8_t> animation;
    GameplayActionStore prototype;
    NativeDatArena arena;
    DatNativeJoint model;
    DatMaterialAnimation material;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> native;
    std::unique_ptr<DatNativeJoint> metal_model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> metal_native;
    std::array<std::unique_ptr<GameplayActionStore>,6> bindings;
    std::array<Fighter*,6> fighters{};
    uint32_t unresolved=0;
    MeleeWebFighterAssetScope* scope=nullptr;
    Storage(std::shared_ptr<const DatArchive> ft,std::shared_ptr<const DatArchive> costume,
            std::span<const uint8_t> aj,const FighterCostume& id)
      :fighter(std::move(ft)),identity(id),animation(aj.begin(),aj.end()),prototype(fighter,id,aj),arena(fighter),
       model(costume,root(*costume,id.model_symbol)),
       material(costume,root(*costume,id.material_animation_symbol),model.graph()),native(nullptr,destroy_joint),metal_native(nullptr,destroy_joint)
    {
        char error[256];
        native.reset(melee_web_native_joint_hydrate(&model.graph(),error,sizeof(error)));
        if(!native)throw DatError(error);
        uint32_t costume_count=0;
        for(const auto& value:fighter_costumes())if(value.fighter_kind==id.fighter_kind)++costume_count;
        void* data=melee_web_fighter_data_decode(arena.reader(),root(*fighter,id.fighter_symbol),id.fighter_kind,
            costume_count,prototype.action_rows(),prototype.blend_rows(),prototype.wait_choices(),&unresolved);
        const auto metal_root=fighter->pointer(root(*fighter,id.fighter_symbol)+0x5c,64);
        if(!metal_root)throw DatError("Mario constructor requires its original metal graph");
        metal_model=std::make_unique<DatNativeJoint>(fighter,*metal_root);
        const auto& metal=metal_model->graph();
        // The original consumer assigns metal descriptors to the costume's
        // preorder joints before resolving their envelope references.
        const auto& costume_graph=model.graph();
        if(metal.joint_count!=costume_graph.joint_count)throw DatError("Metal graph does not match costume joint count");
        uint32_t metal_dobjs=0;
        for(uint32_t j=0;j<metal.joint_count;j++) {
            if(metal.joints[j].child!=costume_graph.joints[j].child||metal.joints[j].next!=costume_graph.joints[j].next)
                throw DatError("Metal graph does not match costume joint topology");
            for(uint32_t d=metal.joints[j].dobj;d!=UINT32_MAX;d=metal.dobjs[d].next)
                if(++metal_dobjs>32)throw DatError("Metal DObj occurrences exceed source capacity");
        }
        metal_native.reset(melee_web_native_joint_hydrate(&metal,error,sizeof(error)));
        if(!metal_native)throw DatError(error);
        if(!melee_web_fighter_data_set_metal(data,
            melee_web_native_joint_descriptor(metal_native.get(),error,sizeof(error)),
            costume_count,metal_dobjs,&unresolved,error,sizeof(error)))throw DatError(error);
        // Neutral Wait may not enter demo clips, Guard or part animation.
        // All other ftData roots are required; Article spawning has its own gate.
        constexpr uint32_t neutral_unreached=(1U<<5)|(1U<<6)|(1U<<7)|(1U<<8);
        if(!data || (unresolved&~neutral_unreached))throw DatError("Creation-reachable fighter data is not hydrated");
        scope=melee_web_fighter_assets_begin(id.fighter_kind,id.costume_index,data,
            melee_web_native_joint_descriptor(native.get(),error,sizeof(error)),material.descriptor(),
            uint32_t(prototype.runtime().actions().size()),this,bind,unbind,error,sizeof(error));
        if(!scope)throw DatError(error);
    }
    static int bind(void* context,Fighter* fp,void** rows,void** blends,char* error,size_t size)
    {
        try {
            auto& s=*static_cast<Storage*>(context);
            for(size_t i=0;i<s.fighters.size();++i)if(!s.fighters[i]) {
                auto actions=std::make_unique<GameplayActionStore>(s.fighter,s.identity,s.animation);
                actions->bind(fp);*rows=actions->action_rows();*blends=actions->blend_rows();
                s.bindings[i]=std::move(actions);s.fighters[i]=fp;return 1;
            }
            throw DatError("Per-Fighter asset retention capacity exceeded");
        }catch(const std::exception& e){if(error&&size)std::snprintf(error,size,"%s",e.what());return 0;}
    }
    static void unbind(void* context,Fighter* fp)
    {
        auto& s=*static_cast<Storage*>(context);
        for(size_t i=0;i<s.fighters.size();++i)if(s.fighters[i]==fp){s.bindings[i].reset();s.fighters[i]=nullptr;return;}
    }
};
GameplayFighterAssets::GameplayFighterAssets(std::shared_ptr<const DatArchive> fighter,
    std::shared_ptr<const DatArchive> costume,std::span<const uint8_t> animation,const FighterCostume& identity)
{
    if(!fighter || !costume)throw DatError("Fighter asset archives are missing");
    storage_=std::make_unique<Storage>(std::move(fighter),std::move(costume),animation,identity);
}
GameplayFighterAssets::~GameplayFighterAssets()
{
    try{close();}catch(const std::exception& e){std::fprintf(stderr,"Fighter assets outlived required teardown: %s\n",e.what());std::abort();}
}
void GameplayFighterAssets::close()
{
    if(!storage_)return;
    char error[256];if(!melee_web_fighter_assets_end(storage_->scope,error,sizeof(error)))throw DatError(error);
    storage_->scope=nullptr;storage_.reset();
}
uint32_t GameplayFighterAssets::live_fighters()const noexcept
{return storage_?melee_web_fighter_assets_live(storage_->scope):0;}
uint32_t GameplayFighterAssets::unresolved_fields()const noexcept
{return storage_?storage_->unresolved:0;}
}
