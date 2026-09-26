#include "dat_effect_entries.hpp"
#include "dat_native_joint.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "gameplay_compat.h"
#include "gameplay_archive_sections.h"
#include "gameplay_effect_runtime.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/ef/types.h>
#include <melee/ef/efasync.h>
#include <melee/ef/efdata.h>
extern EF_DAT_Entry efAsync_DatEntries[51];
}
#pragma GCC diagnostic pop
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
namespace melee_web {
namespace {
struct Table {void* commands;void* textures;};
bool fail(char* error,size_t size,const char* message){if(error&&size)std::snprintf(error,size,"%s",message);return false;}
struct Entry {
    std::unique_ptr<DatNativeJoint> model;
    std::unique_ptr<DatNativeAnimation> animation;
    std::unique_ptr<DatMaterialAnimation> material_animation;
    std::unique_ptr<DatShapeAnimation> shape_animation;
    MeleeWebNativeJoint* native=nullptr;
    ~Entry(){if(native&&!melee_web_native_joint_destroy(native,nullptr,0))std::terminate();}
};
}
struct DatEffectEntries::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::unique_ptr<DatEffectBanks> bank;
    std::vector<std::unique_ptr<Entry>> entries;
    Table* table=nullptr;
    uint32_t bank_index=0;
    std::string symbol;
    MeleeWebArchiveSections* registration=nullptr;
    void* previous_data=nullptr;
    bool ready=false,needs_particles=false;
    ~Storage(){std::free(table);}
};
DatEffectEntries::DatEffectEntries(std::shared_ptr<const DatArchive> archive,std::string_view symbol,
    uint32_t bank,uint32_t count,bool particles,std::vector<NativeDatSourceRegion> source_regions)
    :storage_(std::make_unique<Storage>())
{
    static_assert(sizeof(void*)==4&&sizeof(EF_EffectDesc)==20&&sizeof(Table)==8,"Original EF table layout");
    if(!archive||!count||count>64)throw DatError("Effect table requires an archive and bounded source entry count");
    auto& s=*storage_;s.archive=std::move(archive);s.bank_index=bank;s.symbol=symbol;
    std::optional<uint32_t> root;
    for(const auto& entry:s.archive->public_symbols())if(entry.name==symbol)root=entry.data_offset;
    if(!root)throw DatError("Exact effect entry symbol is absent");
    const auto& a=*s.archive;
    const auto source_region=a.next_target_offset(*root)-*root;
    if(8+size_t(count)*20>source_region)throw DatError("Effect table bank "+std::to_string(bank)+" symbol "+std::string(symbol)+" declares "+std::to_string(count)+" entries but its source-referenced region is "+std::to_string(source_region)+" bytes");
    const auto commands=a.pointer(*root,8),textures=a.pointer(*root+4,4);
    if(bool(commands)!=bool(textures))throw DatError("Effect table has an incomplete particle bank pair");
    // efAsync_LoadSync permits model-only effect tables: both particle roots
    // are null (for example Link's authored sword effects).
    if(commands) {
        try {
            s.bank=std::make_unique<DatEffectBanks>(s.archive,symbol,bank,
                                                     std::move(source_regions));
        } catch(const DatError& error) {
            throw DatError("Effect particle bank "+std::to_string(bank)+" symbol "+
                           std::string(symbol)+": "+error.what());
        }
    }
    s.table=static_cast<Table*>(std::calloc(1,8+size_t(count)*20));
    if(!s.table)throw DatError("Cannot allocate native effect entries");
    if(s.bank){
        s.table->commands=melee_web_effect_bank_commands(s.bank->bank());
        s.table->textures=melee_web_effect_bank_textures(s.bank->bank());
    }
    for(uint32_t i=0;i<count;++i){
      try {
        auto owner=std::make_unique<Entry>();auto& e=*owner;const uint32_t at=*root+8+20*i;
        const float lifetime=a.f32(at);
        if(!std::isfinite(lifetime)||lifetime<0||lifetime>65535)throw DatError("Effect lifetime exceeds native source range");
        auto& out=reinterpret_cast<EF_EffectDesc*>(s.table+1)[i];out.lifetime=lifetime;
        const auto model=a.pointer(at+4,64),animation=a.pointer(at+8,20),material=a.pointer(at+12,12);
        const auto shape=a.pointer(at+16,12);
        if(!model){
            // Source tables can retain an empty model row beside particle
            // commands with the same numeric suffix (Pikachu's row 7005).
            // Keep its index and exact zero descriptor; no model is invented.
            if(lifetime!=0||animation||material||shape)
                throw DatError("Effect null model row has a lifetime or animation descriptor");
            s.entries.push_back(std::move(owner));
            continue;
        }
        e.model=std::make_unique<DatNativeJoint>(s.archive,*model);char error[256];
        e.native=melee_web_native_joint_hydrate(&e.model->graph(),error,sizeof(error));
        if(!e.native)throw DatError(error);
        if(animation){
            std::vector<void*> descriptors;
            for(uint32_t j=0;j<e.model->graph().joint_count;j++){
                void* descriptor=melee_web_native_joint_descriptor_at(e.native,j,e.model->graph().joints[j].source_offset,error,sizeof(error));
                if(!descriptor)throw DatError(error);
                descriptors.push_back(descriptor);
            }
            e.animation=std::make_unique<DatNativeAnimation>(s.archive,*animation,e.model->graph(),
                particles?DatNativeAnimationPolicy::ParticleDescriptors:DatNativeAnimationPolicy::Transforms,descriptors);
            s.needs_particles|=!e.animation->particle_events().empty();
        }
        if(material)e.material_animation=std::make_unique<DatMaterialAnimation>(s.archive,*material,e.model->graph());
        if(shape)e.shape_animation=std::make_unique<DatShapeAnimation>(s.archive,*shape,e.model->graph());
        out.model_desc.joint=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(e.native,error,sizeof(error)));
        out.model_desc.animjoint=e.animation?static_cast<HSD_AnimJoint*>(e.animation->descriptor()):nullptr;
        out.model_desc.matanim_joint=e.material_animation?static_cast<HSD_MatAnimJoint*>(e.material_animation->descriptor()):nullptr;
        out.model_desc.shapeanim_joint=e.shape_animation?e.shape_animation->descriptor():nullptr;
        s.entries.push_back(std::move(owner));
      } catch(const DatError& error) {
        throw DatError("Effect bank "+std::to_string(bank)+" entry "+std::to_string(i)+": "+error.what());
      }
    }
}
DatEffectEntries::~DatEffectEntries(){if(!detach(nullptr,0))std::terminate();}
bool DatEffectEntries::publish_for_source(char* error,size_t size){
    auto& s=*storage_;
    if(s.needs_particles&&!melee_web_effect_runtime_prepared())return fail(error,size,"Effect particle descriptors require an owned callback runtime");
    if(s.registration)return fail(error,size,"Effect entries are already published");
    if(efLib_EffectCount)return fail(error,size,"Live source effects prevent descriptor replacement");
    if(s.bank_index>=50)return fail(error,size,"Effect source bank index is invalid");
    auto& lookup=efAsync_DatEntries[s.bank_index];
    if(!lookup.ef_DAT_file||!lookup.effDataTable_name||s.symbol!=lookup.effDataTable_name)
        return fail(error,size,"Effect table does not match original source lookup");
    if(s.bank&&!melee_web_effect_bank_attach(s.bank->bank(),error,size))return false;
    MeleeWebArchiveSymbol entry{lookup.ef_DAT_file,lookup.effDataTable_name,s.table};
    s.registration=melee_web_archive_sections_register(&entry,1,error,size);
    if(!s.registration){if(s.bank)melee_web_effect_bank_detach(s.bank->bank(),nullptr,0);return false;}
    s.previous_data=lookup.data;lookup.data=nullptr;
    if(error&&size)*error=0;return true;
}
bool DatEffectEntries::verify_source_load(char* error,size_t size){
    auto& s=*storage_;
    if(!s.registration)return fail(error,size,"Effect entries are not published");
    const auto& lookup=efAsync_DatEntries[s.bank_index];
    s.ready=lookup.data==s.table+1;
    if(!s.ready)return fail(error,size,"Original effect loader did not publish owned entries");
    if(error&&size)*error=0;return true;
}
bool DatEffectEntries::load(char* error,size_t size){
    if(storage_->needs_particles&&!melee_web_effect_runtime_active())
        return fail(error,size,"Effect particle descriptors require the initialized original callback runtime");
    if(!publish_for_source(error,size))return false;
    efAsync_LoadSync(int(storage_->bank_index));
    return verify_source_load(error,size);
}
bool DatEffectEntries::detach(char* error,size_t size){
    auto& s=*storage_;
    if(!s.registration)return true;
    if(efLib_EffectCount)return fail(error,size,"Live source effects prevent descriptor release");
    auto& lookup=efAsync_DatEntries[s.bank_index];
    if(lookup.data!=s.table+1&&(s.ready||lookup.data))return fail(error,size,"Effect lookup ownership changed");
    if(s.bank&&!melee_web_effect_bank_detach(s.bank->bank(),error,size))return false;
    lookup.data=s.previous_data;
    if(!melee_web_archive_sections_close(s.registration,error,size))std::terminate();
    s.registration=nullptr;s.previous_data=nullptr;s.ready=false;
    if(error&&size)*error=0;return true;
}
bool DatEffectEntries::entries_ready()const noexcept{return storage_->ready;}

void* DatEffectEntries::table()const noexcept{return storage_->table;}
uint32_t DatEffectEntries::entry_count()const noexcept{return uint32_t(storage_->entries.size());}
MeleeWebEffectBank* DatEffectEntries::bank()const noexcept{return storage_->bank?storage_->bank->bank():nullptr;}
}
