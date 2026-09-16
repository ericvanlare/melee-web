#include "gameplay_compat.h"
#include "gameplay_fighter_assets.hpp"
#include "gameplay_fighter_data.h"
#include "dat_item_article.hpp"
#include "dat_native_animation.hpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#include <array>
#include <cstdio>
#include <cstdlib>
namespace melee_web {
namespace {
uint32_t root(const DatArchive& archive,std::string_view name)
{for(const auto& s:archive.public_symbols())if(s.name==name)return s.data_offset;throw DatError("Fighter asset root is missing: "+std::string(name));}
bool canonical_costume(const FighterCostume& candidate) noexcept
{
    for(const auto& value:fighter_costumes()) {
        if(value.fighter_kind!=candidate.fighter_kind || value.costume_index!=candidate.costume_index ||
           value.motion_count!=candidate.motion_count || value.kind_name!=candidate.kind_name ||
           value.fighter_filename!=candidate.fighter_filename || value.fighter_symbol!=candidate.fighter_symbol ||
           value.animation_filename!=candidate.animation_filename || value.model_filename!=candidate.model_filename ||
           value.model_symbol!=candidate.model_symbol ||
           value.material_animation_symbol!=candidate.material_animation_symbol) continue;
        return true;
    }
    return false;
}
void destroy_joint(MeleeWebNativeJoint* joint)
{char error[128];if(!melee_web_native_joint_destroy(joint,error,sizeof(error))){std::fprintf(stderr,"%s\n",error);std::abort();}}
struct NativePartAnimationGroup {
    uint16_t start_part=0;
    uint16_t part_count=0;
    uint8_t* parts=nullptr;
    void** animations=nullptr;
};
static_assert(sizeof(NativePartAnimationGroup)==12);
struct OwnedPartAnimationGroup {
    NativePartAnimationGroup native;
    std::vector<uint8_t> parts;
    std::vector<std::unique_ptr<DatNativeAnimation>> owners;
    std::vector<void*> animations;
};
}
struct OwnedAdditionalCostume {
    FighterCostume identity;
    DatNativeJoint model;
    DatMaterialAnimation material;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> native{nullptr,destroy_joint};
    OwnedAdditionalCostume(std::shared_ptr<const DatArchive> archive,const FighterCostume& id,
                           const MeleeWebNativeGraph& base)
        :identity(id),model(archive,root(*archive,id.model_symbol)),
         material(archive,root(*archive,id.material_animation_symbol),model.graph())
    {
        const auto& graph=model.graph();
        if(graph.joint_count!=base.joint_count)throw DatError("Additional costume joint count differs from shared metal/guard topology");
        for(uint32_t j=0;j<graph.joint_count;j++)
            if(graph.joints[j].child!=base.joints[j].child||graph.joints[j].next!=base.joints[j].next)
                throw DatError("Additional costume differs from shared metal/guard topology");
        char error[256];native.reset(melee_web_native_joint_hydrate(&graph,error,sizeof(error)));
        if(!native)throw DatError(error);
    }
};
struct GameplayFighterAssets::Storage {
    std::shared_ptr<const DatArchive> fighter;
    FighterCostume identity;
    std::vector<uint8_t> animation;
    GameplayActionStore prototype;
    NativeDatArena arena;
    std::array<std::unique_ptr<DatItemArticle>,4> articles;
    DatNativeJoint model;
    DatMaterialAnimation material;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> native;
    std::unique_ptr<DatNativeJoint> metal_model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> metal_native;
    std::unique_ptr<DatNativeJoint> guard_model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> guard_native{nullptr,destroy_joint};
    std::vector<std::unique_ptr<OwnedPartAnimationGroup>> part_groups;
    std::vector<void*> part_group_table;
    std::array<std::unique_ptr<OwnedAdditionalCostume>,16> additional;
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
        const uint32_t fighter_root=root(*fighter,id.fighter_symbol);
        void* data=melee_web_fighter_data_decode(arena.reader(),fighter_root,id.fighter_kind,
            costume_count,static_cast<uint32_t>(prototype.runtime().actions().size()),
            prototype.action_rows(),prototype.blend_rows(),prototype.wait_choices(),&unresolved);
        const auto item_table=fighter->pointer(fighter_root+0x48,16);
        struct ItemIdentity { uint32_t index,kind; };
        std::array<ItemIdentity,3> item_identities{};
        size_t item_count=0;
        if(id.fighter_kind==0) {
            if(!item_table)throw DatError("Mario item Article table is missing");
            item_identities[item_count++]={0,static_cast<uint32_t>(It_Kind_Mario_Fire)};
            item_identities[item_count++]={2,static_cast<uint32_t>(It_Kind_Mario_Cape)};
        } else if(id.fighter_kind==21) {
            if(!item_table)throw DatError("Dr. Mario item Article table is missing");
            const auto& attributes=prototype.runtime().mario_attributes();
            if(!attributes)throw DatError("Dr. Mario ftData extension is not hydrated");
            item_identities[item_count++]={1,static_cast<uint32_t>(It_Kind_DrMario_Vitamin)};
            item_identities[item_count++]={3,static_cast<uint32_t>(attributes->specials_cape_kind)};
        } else if(id.fighter_kind==1 || id.fighter_kind==22) {
            if(!item_table)throw DatError("Fox-family item Article table is missing");
            const auto& attributes=prototype.runtime().fox_attributes();
            if(!attributes)throw DatError("Fox family ftData extension is not hydrated");
            item_identities[item_count++]={0,attributes->blaster_shot_item_kind};
            item_identities[item_count++]={1,attributes->blaster_gun_item_kind};
            item_identities[item_count++]={id.fighter_kind==1?2U:3U,
                static_cast<uint32_t>(id.fighter_kind==1?It_Kind_Fox_Illusion:It_Kind_Falco_Phantasm)};
        } else if(id.fighter_kind!=18 && id.fighter_kind!=26) {
            throw DatError("Fighter item Article schema is unavailable");
        }
        for(size_t n=0;n<item_count;++n) {
            const auto item=item_identities[n];
            const auto article_root=fighter->pointer(*item_table+item.index*4,24);
            if(!article_root)throw DatError("Fighter item Article root is missing");
            auto* registered=melee_web_fighter_data_article(data,item.index);
            if(!registered)throw DatError("Fighter item Article registration identity is missing");
            articles[item.index]=std::make_unique<DatItemArticle>(fighter,*article_root,item.kind,registered);
        }
        const auto metal_root=fighter->pointer(root(*fighter,id.fighter_symbol)+0x5c,64);
        if(!metal_root)throw DatError("Fighter constructor requires its original metal graph");
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
        const auto guard_data=fighter->pointer(root(*fighter,id.fighter_symbol)+0x20,4);
        if(!guard_data)throw DatError("Fighter requires its original guard pose");
        const auto guard_root=fighter->pointer(*guard_data,64);
        if(!guard_root)throw DatError("Fighter guard pose descriptor is missing");
        guard_model=std::make_unique<DatNativeJoint>(fighter,*guard_root);
        const auto& guard=guard_model->graph();
        if(guard.joint_count!=costume_graph.joint_count)throw DatError("Guard pose does not match costume joint count");
        for(uint32_t j=0;j<guard.joint_count;j++)
            if(guard.joints[j].child!=costume_graph.joints[j].child||guard.joints[j].next!=costume_graph.joints[j].next)
                throw DatError("Guard pose does not match costume joint topology");
        guard_native.reset(melee_web_native_joint_hydrate(&guard,error,sizeof(error)));
        if(!guard_native)throw DatError(error);
        melee_web_fighter_data_set_guard(arena.reader(),root(*fighter,id.fighter_symbol),data,
            melee_web_native_joint_descriptor(guard_native.get(),error,sizeof(error)),&unresolved);
        const auto part_table=fighter->pointer(fighter_root+0x1c,4);
        if(!part_table)throw DatError("Fighter part animation table is missing");
        const auto part_table_end=fighter->next_target_offset(*part_table);
        if(part_table_end<=*part_table||(part_table_end-*part_table)%4)
            throw DatError("Fighter part animation table extent is invalid");
        const auto part_group_count=(part_table_end-*part_table)/4;
        if(!part_group_count||part_group_count>5)
            throw DatError("Fighter part animation group count exceeds source capacity");
        part_groups.reserve(part_group_count);part_group_table.reserve(part_group_count);
        for(uint32_t group_index=0;group_index<part_group_count;++group_index) {
            const auto descriptor=fighter->pointer(*part_table+group_index*4,12);
            if(!descriptor)throw DatError("Fighter part animation descriptor is missing");
            auto group=std::make_unique<OwnedPartAnimationGroup>();
            group->native.start_part=fighter->be16(*descriptor);
            group->native.part_count=fighter->be16(*descriptor+2);
            if(group->native.start_part>=model.graph().joint_count||!group->native.part_count||
               group->native.part_count>model.graph().joint_count)
                throw DatError("Fighter part animation range exceeds the model graph");
            const auto part_indices=fighter->pointer(*descriptor+4,group->native.part_count);
            if(!part_indices)throw DatError("Fighter part animation index list is missing");
            const auto part_bytes=fighter->range(*part_indices,group->native.part_count);
            for(const auto part:part_bytes)if(part>=model.graph().joint_count)
                throw DatError("Fighter part animation index exceeds the model graph");
            group->parts.assign(part_bytes.begin(),part_bytes.end());
            const auto animation_table=fighter->pointer(*descriptor+8,4);
            if(!animation_table)throw DatError("Fighter part animation root table is missing");
            const auto animation_end=fighter->next_target_offset(*animation_table);
            if(animation_end<=*animation_table||(animation_end-*animation_table)%4)
                throw DatError("Fighter part animation root table extent is invalid");
            const auto animation_count=(animation_end-*animation_table)/4;
            if(!animation_count||animation_count>32)
                throw DatError("Fighter part animation variant count exceeds source capacity");
            group->owners.reserve(animation_count);group->animations.reserve(animation_count);
            auto partial_graph=model.graph();partial_graph.root=group->native.start_part;
            for(uint32_t animation_index=0;animation_index<animation_count;++animation_index) {
                const auto animation_root=fighter->pointer(*animation_table+animation_index*4,20);
                if(!animation_root)throw DatError("Fighter part animation root is missing");
                auto animation=std::make_unique<DatNativeAnimation>(fighter,*animation_root,partial_graph);
                group->animations.push_back(animation->descriptor());
                group->owners.push_back(std::move(animation));
            }
            group->native.parts=group->parts.data();group->native.animations=group->animations.data();
            part_group_table.push_back(&group->native);part_groups.push_back(std::move(group));
        }
        if(!melee_web_fighter_data_set_part_animations(data,part_group_table.data(),part_group_count,
            &unresolved,error,sizeof(error)))throw DatError(error);
        // Demo clips remain explicitly unavailable.
        // All other ftData roots are required; Article spawning has its own gate.
        constexpr uint32_t neutral_unreached=(1U<<5)|(1U<<6);
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
void GameplayFighterAssets::add_costume(std::shared_ptr<const DatArchive> archive,const FighterCostume& id)
{
    if(!storage_||!archive||!canonical_costume(id)||live_fighters()||id.fighter_kind!=storage_->identity.fighter_kind||
       id.costume_index>=storage_->additional.size()||id.costume_index==storage_->identity.costume_index||
       storage_->additional[id.costume_index])throw DatError("Additional costume identity is invalid or already owned");
    auto value=std::make_unique<OwnedAdditionalCostume>(std::move(archive),id,storage_->model.graph());
    char error[256];
    if(!melee_web_fighter_assets_add_costume(storage_->scope,id.costume_index,
       melee_web_native_joint_descriptor(value->native.get(),error,sizeof(error)),value->material.descriptor(),error,sizeof(error)))
        throw DatError(error);
    storage_->additional[id.costume_index]=std::move(value);
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
