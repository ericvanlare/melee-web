#include "gameplay_compat.h"
#include "gameplay_fighter_assets.hpp"
#include "gameplay_fighter_data.h"
#include "gameplay_archive_sections.h"
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
std::unique_ptr<DatMaterialAnimation> costume_material(
    std::shared_ptr<const DatArchive> archive,const FighterCostume& id,
    const MeleeWebNativeGraph& graph)
{
    // ftData_80085820 publishes null only when the source costume string is
    // null. A required symbol still passes through the exact-root lookup.
    if(!canonical_costume(id))throw DatError("Fighter costume identity is not authored by the source registry");
    if(id.material_animation_symbol.empty())return nullptr;
    return std::make_unique<DatMaterialAnimation>(archive,root(*archive,id.material_animation_symbol),graph);
}
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
// The source Purin OnLoad resolves a costume hat from HSD_Archive, then
// retains the descriptor in its kind cache. Keep both the typed archive and
// native graph alive until the asset scope restores that cache after teardown.
struct OwnedCostumePart {
    DatNativeJoint model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> native{nullptr,destroy_joint};
    MeleeWebArchiveSections* sections=nullptr;
    void* archive=nullptr;
    uint32_t dobj_count=0;
    OwnedCostumePart(std::shared_ptr<const DatArchive> source,const FighterCostume& id,
                     const char* symbol):model(source,root(*source,symbol))
    {
        const auto& graph=model.graph();
        for(uint32_t j=0;j<graph.joint_count;++j)
            for(uint32_t d=graph.joints[j].dobj;d!=UINT32_MAX;d=graph.dobjs[d].next)
                if(++dobj_count>32)throw DatError("Costume part exceeds source 32-DObj storage");
        char error[256];
        native.reset(melee_web_native_joint_hydrate(&model.graph(),error,sizeof(error)));
        if(!native)throw DatError(error);
        void* descriptor=melee_web_native_joint_descriptor(native.get(),error,sizeof(error));
        if(!descriptor)throw DatError(error);
        const std::string filename(id.model_filename);
        const MeleeWebArchiveSymbol entry{filename.c_str(),symbol,descriptor};
        sections=melee_web_archive_sections_register(&entry,1,error,sizeof(error));
        if(!sections)throw DatError(error);
        archive=melee_web_archive_sections_open(filename.c_str());
    }
    ~OwnedCostumePart()
    {
        char error[256];
        if(archive)melee_web_archive_sections_release(archive);
        if(!melee_web_archive_sections_close(sections,error,sizeof(error))) {
            std::fprintf(stderr,"%s\n",error);std::abort();
        }
    }
};
std::unique_ptr<OwnedCostumePart> costume_part(
    std::shared_ptr<const DatArchive> archive,const FighterCostume& id)
{
    const char* symbol=melee_web_fighter_costume_part_symbol(id.fighter_kind,id.costume_index);
    return symbol?std::make_unique<OwnedCostumePart>(std::move(archive),id,symbol):nullptr;
}
void validate_costume_part_dynamics(const DatFighterRuntime& runtime,
    const FighterCostume& id,const OwnedCostumePart* part)
{
    if(id.fighter_kind!=FTKIND_PURIN || (id.costume_index!=2 && id.costume_index!=3))return;
    if(!part)throw DatError("Purin costume dynamics require its hat graph");
    const auto& graph=part->model.graph();
    std::vector<uint32_t> preorder,pending{graph.root};
    while(!pending.empty()) {
        const auto joint=pending.back();pending.pop_back();
        if(joint>=graph.joint_count || preorder.size()>=graph.joint_count)
            throw DatError("Costume dynamics graph traversal exceeds its checked bounds");
        preorder.push_back(joint);
        if(graph.joints[joint].next!=UINT32_MAX)pending.push_back(graph.joints[joint].next);
        if(graph.joints[joint].child!=UINT32_MAX)pending.push_back(graph.joints[joint].child);
    }
    const uint32_t first=(id.costume_index==2)?1:3;
    const auto& bones=runtime.dynamics().bones;
    if(bones.size()<first+2)throw DatError("Purin costume dynamics descriptors are missing");
    for(uint32_t row=first;row<first+2;++row) {
        const auto& bone=bones[row];
        if(bone.bone_index>=preorder.size())throw DatError("Purin costume dynamics bone exceeds its hat graph");
        auto joint=preorder[bone.bone_index];
        for(size_t link=0;link<bone.parameters.size();++link) {
            if(joint==UINT32_MAX)throw DatError("Purin costume dynamics chain exceeds its hat children");
            joint=graph.joints[joint].child;
        }
    }
}
struct OwnedAdditionalCostume {
    FighterCostume identity;
    std::unique_ptr<OwnedCostumePart> part;
    DatNativeJoint model;
    std::unique_ptr<DatMaterialAnimation> material;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> native{nullptr,destroy_joint};
    OwnedAdditionalCostume(std::shared_ptr<const DatArchive> archive,const FighterCostume& id,
                           const MeleeWebNativeGraph& base)
        :identity(id),part(costume_part(archive,id)),model(archive,root(*archive,id.model_symbol)),
         material(costume_material(archive,id,model.graph()))
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
    std::unique_ptr<OwnedCostumePart> part;
    std::vector<uint8_t> animation;
    GameplayActionStore prototype;
    NativeDatArena arena;
    /* Ness's authored x48 table has eleven Article slots; every other
     * admitted family stays within the original seven-slot array. */
    std::array<std::unique_ptr<DatItemArticle>,11> articles;
    std::unique_ptr<DatNativeJoint> link_part_model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy_joint)> link_part_native{nullptr,destroy_joint};
    DatNativeJoint model;
    std::unique_ptr<DatMaterialAnimation> material;
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
    void* data=nullptr;
    MeleeWebFighterAssetScope* scope=nullptr;
    Storage(std::shared_ptr<const DatArchive> ft,std::shared_ptr<const DatArchive> costume,
            std::span<const uint8_t> aj,const FighterCostume& id)
      :fighter(std::move(ft)),identity(id),part(costume_part(costume,id)),animation(aj.begin(),aj.end()),prototype(fighter,id,aj),arena(fighter),
       model(costume,root(*costume,id.model_symbol)),
       material(costume_material(costume,id,model.graph())),native(nullptr,destroy_joint),metal_native(nullptr,destroy_joint)
    {
        char error[256];
        native.reset(melee_web_native_joint_hydrate(&model.graph(),error,sizeof(error)));
        if(!native)throw DatError(error);
        uint32_t costume_count=0;
        for(const auto& value:fighter_costumes())if(value.fighter_kind==id.fighter_kind)++costume_count;
        const uint32_t fighter_root=root(*fighter,id.fighter_symbol);
        data=melee_web_fighter_data_decode(arena.reader(),fighter_root,id.fighter_kind,
            costume_count,static_cast<uint32_t>(prototype.runtime().actions().size()),
            prototype.action_rows(),prototype.blend_rows(),prototype.wait_choices(),&unresolved);
        const bool link=id.fighter_kind==FTKIND_LINK||id.fighter_kind==FTKIND_CLINK;
        const uint32_t item_table_bytes=link?28:
            (id.fighter_kind==FTKIND_LUIGI||id.fighter_kind==FTKIND_KOOPA)?4:
            id.fighter_kind==FTKIND_PURIN?8:
            (id.fighter_kind==FTKIND_PIKACHU||id.fighter_kind==FTKIND_PICHU)?12:
            id.fighter_kind==FTKIND_NESS?44:
            id.fighter_kind==FTKIND_PEACH?20:16;
        const auto item_table=fighter->pointer(fighter_root+0x48,item_table_bytes);
        struct ItemIdentity { uint32_t index,kind; };
        std::array<ItemIdentity,11> item_identities{};
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
        } else if(link) {
            if(!item_table)throw DatError("Link family item Article table is missing");
            const auto& attributes=prototype.runtime().link_attributes();
            if(!attributes)throw DatError("Link family ftData extension is not hydrated");
            item_identities[item_count++]={0,static_cast<uint32_t>(attributes->x48)};
            item_identities[item_count++]={1,static_cast<uint32_t>(attributes->x2C)};
            item_identities[item_count++]={2,static_cast<uint32_t>(attributes->xBC)};
            item_identities[item_count++]={3,static_cast<uint32_t>(attributes->xC)};
            item_identities[item_count++]={4,static_cast<uint32_t>(attributes->x10)};
            if(id.fighter_kind==FTKIND_CLINK)
                item_identities[item_count++]={5,static_cast<uint32_t>(It_Kind_CLink_Milk)};
        } else if(id.fighter_kind==FTKIND_LUIGI) {
            if(!item_table)throw DatError("Luigi item Article table is missing");
            item_identities[item_count++]={0,static_cast<uint32_t>(It_Kind_Luigi_Fire)};
        } else if(id.fighter_kind==FTKIND_KOOPA) {
            if(!item_table)throw DatError("Koopa Flame Article table is missing");
            item_identities[item_count++]={0,static_cast<uint32_t>(It_Kind_Koopa_Flame)};
        } else if(id.fighter_kind==FTKIND_NESS) {
            if(!item_table)throw DatError("Ness item Article table is missing");
            /* All eleven slots are required by ftNs_Init_OnLoad, in this
             * original registration order. */
            item_identities[item_count++]={0,static_cast<uint32_t>(It_Kind_Ness_PKFire)};
            item_identities[item_count++]={1,static_cast<uint32_t>(It_Kind_Ness_PKFire_Flame)};
            item_identities[item_count++]={2,static_cast<uint32_t>(It_Kind_Ness_PKFlush)};
            item_identities[item_count++]={3,static_cast<uint32_t>(It_Kind_Ness_PKThunder)};
            item_identities[item_count++]={4,static_cast<uint32_t>(It_Kind_Ness_PKThunder1)};
            item_identities[item_count++]={5,static_cast<uint32_t>(It_Kind_Ness_PKThunder2)};
            item_identities[item_count++]={6,static_cast<uint32_t>(It_Kind_Ness_PKThunder3)};
            item_identities[item_count++]={7,static_cast<uint32_t>(It_Kind_Ness_PKThunder4)};
            item_identities[item_count++]={8,static_cast<uint32_t>(It_Kind_Ness_PKFlush_Explode)};
            item_identities[item_count++]={9,static_cast<uint32_t>(It_Kind_Ness_Bat)};
            item_identities[item_count++]={10,static_cast<uint32_t>(It_Kind_Ness_Yoyo)};
        } else if(id.fighter_kind==FTKIND_PEACH) {
            if(!item_table)throw DatError("Peach item Article table is missing");
            /* All five slots are required by ftPe_Init_OnLoad, in this
             * original registration order. */
            item_identities[item_count++]={0,static_cast<uint32_t>(It_Kind_Peach_Explode)};
            item_identities[item_count++]={1,static_cast<uint32_t>(It_Kind_Peach_Turnip)};
            item_identities[item_count++]={2,static_cast<uint32_t>(It_Kind_Peach_Parasol)};
            item_identities[item_count++]={3,static_cast<uint32_t>(It_Kind_Peach_Toad)};
            item_identities[item_count++]={4,static_cast<uint32_t>(It_Kind_Peach_ToadSpore)};
        } else if(id.fighter_kind==FTKIND_PIKACHU || id.fighter_kind==FTKIND_PICHU) {
            if(!item_table)throw DatError("Pikachu-family item Article table is missing");
            const auto& attributes=prototype.runtime().pikachu_attributes();
            if(!attributes)throw DatError("Pikachu-family ftData extension is not hydrated");
            item_identities[item_count++]={0,static_cast<uint32_t>(attributes->xDC)};
            item_identities[item_count++]={1,static_cast<uint32_t>(attributes->specialn_itkind)};
            item_identities[item_count++]={2,static_cast<uint32_t>(attributes->specialairn_itkind)};
        } else if(id.fighter_kind==FTKIND_PURIN) {
            if(!item_table)throw DatError("Purin custom part table is missing");
            // x48 contains a custom FtPartsDesc wrapper, never an Article.
        } else if(id.fighter_kind==FTKIND_CAPTAIN || id.fighter_kind==FTKIND_GANON) {
            if(item_table)throw DatError("Captain-family source Article table must be null");
        } else if(id.fighter_kind==FTKIND_DONKEY) {
            if(item_table)throw DatError("Donkey source Article table must be null");
        } else if(id.fighter_kind!=18 && id.fighter_kind!=26) {
            throw DatError("Fighter item Article schema is unavailable");
        }
        for(size_t n=0;n<item_count;++n) {
            const auto item=item_identities[n];
            const auto article_root=fighter->pointer(*item_table+item.index*4,24);
            if(!article_root)throw DatError("Fighter item Article root is missing");
            auto* registered=melee_web_fighter_data_article(data,id.fighter_kind,item.index);
            if(!registered)throw DatError("Fighter item Article registration identity is missing");
            articles[item.index]=std::make_unique<DatItemArticle>(fighter,*article_root,item.kind,registered);
        }
        if(link) {
            const auto part_root=fighter->pointer(*item_table+6*4,64);
            if(!part_root)throw DatError("Link family parts joint is missing");
            link_part_model=std::make_unique<DatNativeJoint>(fighter,*part_root);
            link_part_native.reset(melee_web_native_joint_hydrate(&link_part_model->graph(),error,sizeof(error)));
            if(!link_part_native)throw DatError(error);
            if(!melee_web_fighter_data_set_link_part(data,
                melee_web_native_joint_descriptor(link_part_native.get(),error,sizeof(error)),
                &unresolved,error,sizeof(error)))throw DatError(error);
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
        if(!data || (unresolved&~neutral_unreached))throw DatError("Creation-reachable fighter data is not hydrated: kind="+
            std::to_string(id.fighter_kind)+" unresolved="+std::to_string(unresolved&~neutral_unreached));
        validate_costume_part_dynamics(prototype.runtime(),id,part.get());
        if(part && !melee_web_fighter_data_check_purin_part(data,id.costume_index,
            part->dobj_count,error,sizeof(error)))throw DatError(error);
        scope=melee_web_fighter_assets_begin(id.fighter_kind,id.costume_index,data,
            melee_web_native_joint_descriptor(native.get(),error,sizeof(error)),material?material->descriptor():nullptr,
            part?part->archive:nullptr,uint32_t(prototype.runtime().actions().size()),this,bind,unbind,error,sizeof(error));
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
    validate_costume_part_dynamics(storage_->prototype.runtime(),id,value->part.get());
    if(value->part && !melee_web_fighter_data_check_purin_part(storage_->data,id.costume_index,
        value->part->dobj_count,error,sizeof(error)))throw DatError(error);
    if(!melee_web_fighter_assets_add_costume(storage_->scope,id.costume_index,
       melee_web_native_joint_descriptor(value->native.get(),error,sizeof(error)),value->material?value->material->descriptor():nullptr,
       value->part?value->part->archive:nullptr,error,sizeof(error)))
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
