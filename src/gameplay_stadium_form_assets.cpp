#include "gameplay_stadium_form_assets.hpp"
#include "stadium_form_archive_contract.hpp"
#include "dat_stage.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace melee_web {
namespace {
constexpr std::array<const char*,4> filenames={"GrPs1.dat","GrPs2.dat","GrPs3.dat","GrPs4.dat"};
void require(bool ok,const char* error){if(!ok)throw DatError(error);}
const DatExternalSymbol* external(const DatArchive& a,uint32_t slot){
    const DatExternalSymbol* result=nullptr;
    for(const auto& e:a.external_symbols())if(std::find(e.slots.begin(),e.slots.end(),slot)!=e.slots.end()){
        require(!result,"Stadium form external slot is multiply owned");result=&e;
    }
    return result;
}
void append_override(std::vector<MeleeWebMapLightOverride>& out,const MeleeWebMapLightOverride& row){
    require(row.descriptor&&(row.found==0||row.found==1)&&!(row.flags&~0xe0)&&
            (row.found||!row.flags),"Stadium form light override is malformed");
    auto old=std::find_if(out.begin(),out.end(),[&](const auto& value){return value.descriptor==row.descriptor;});
    if(old!=out.end()){
        require(old->found==row.found&&old->flags==row.flags,"Stadium form light identity has contradictory overrides");
    }else out.push_back(row);
    require(out.size()<=256,"Stadium form light overrides exceed existing StageMap bound");
}
}

DatNativeStageMapContractData stadium_form_map_contract(const DatArchive& archive,unsigned ordinal){
    using namespace stadium_form_contract;
    require(ordinal>=1&&ordinal<=4,"Stadium form ordinal must name GrPs1..4");
    constexpr std::array<size_t,4> public_counts={31,25,39,28};
    constexpr std::array<size_t,4> external_counts={91,88,71,91};
    constexpr std::array<uint32_t,12> head={0x50,1,0x88,10,0,0,0x290,0x30,0,0,0x350,44};
    DatStage metadata(archive);
    require(archive.public_symbols().size()==public_counts[ordinal-1]&&
            archive.external_symbols().size()==external_counts[ordinal-1]&&
            metadata.root_offset==0x400&&metadata.entries.size()==10,
            "Stadium form archive differs from retained C0 layout");
    for(size_t i=0;i<head.size();i++)require(archive.be32(0x400+4*i)==head[i],
            "Stadium form map_head differs from retained C0 table");
    for(const auto& e:archive.external_symbols()){
        require(!e.name.empty()&&!e.slots.empty(),"Stadium form external lacks an identity");
        for(auto slot:e.slots)require(slot<0x400,"Stadium form external escaped authored header tables");
    }
    const auto* canonical=melee_web_stage_stadium_profile_data();
    require(canonical&&canonical->entry_count==10&&canonical->animation_counts&&
            canonical->animation_count_count==10&&canonical->map_ownership,
            "Canonical Stadium map contract is unavailable");
    DatNativeStageMapContractData result;
    result.entry_count=10;
    result.animation_consumer_counts.assign(canonical->animation_counts,canonical->animation_counts+10);
    for(const auto& entry:metadata.entries){
        if(joint_owners[entry.index]==-2||joint_owners[entry.index]==static_cast<int>(ordinal))
            result.resident_entry_ids.push_back(entry.index);
        for(uint32_t field:{0u,4u,8u,12u}){
            const auto expected=animation_expectation(entry.index,field);
            const uint32_t slot=entry.descriptor_offset+field;
            const auto* imported=external(archive,slot);
            if(expected.owner==-1){
                require(!imported&&!archive.has_relocation(slot)&&!archive.be32(slot),
                        "Stadium form authored absent animation slot changed");
            }else if(expected.owner==-2||expected.owner==static_cast<int>(ordinal)){
                require(!imported&&archive.has_relocation(slot),"Stadium form local animation provider changed");
                const auto target=archive.pointer(slot,expected.target_bytes);
                require(target.has_value(),"Stadium form local animation target escaped source bytes");
                for(const auto& pub:archive.public_symbols())require(pub.data_offset!=*target,
                        "Stadium form local animation row lost its anonymous C0 identity");
                if(expected.pointer_list){
                    require(archive.pointer(*target).has_value()&&!archive.pointer(*target+4),
                            "Stadium form animation table differs from one authored consumer");
                }
            }else{
                require(imported&&expected.external_identity&&imported->name==expected.external_identity&&
                        !archive.has_relocation(slot)&&archive.be32(slot)==0,
                        "Stadium form imported animation slot or source identity changed");
            }
        }
        // The same bounded source map pointer fields used by DatNativeMap.
        // Their exact import identities come from this immutable DAT; no
        // imported graph is resolved or authorized by this enumeration.
        for(uint32_t field:{0u,4u,8u,12u,16u,20u,24u,28u,32u,40u,44u})
            if(const auto* e=external(archive,entry.descriptor_offset+field))
                result.external_references.push_back({entry.index,field,e->name});
    }
    const auto* ownership=canonical->map_ownership;
    result.animation_flag_entry_ids.assign(ownership->animation_flag_entry_ids,
        ownership->animation_flag_entry_ids+ownership->animation_flag_entry_count);
    require(metadata.flagged_object_table.count==44&&metadata.flagged_object_table.data_offset,
            "Stadium form flagged-object table changed");
    for(uint32_t i=0;i<44;i++){
        const uint32_t slot=*metadata.flagged_object_table.data_offset+4*i;
        if(const auto* e=external(archive,slot)){
            require(!archive.has_relocation(slot)&&!archive.be32(slot),"Stadium form imported material was resolved");
            result.flagged_objects.push_back({i,DatNativeMapFlagKind::ExternalNull,0,e->name});
        }else if(const auto target=archive.pointer(slot,8)){
            // DatNativeMap subsequently requires this exact target to belong
            // to a decoded local material in this form's resident graph.
            result.flagged_objects.push_back({i,DatNativeMapFlagKind::LocalMaterial,*target,{}});
        }else result.flagged_objects.push_back({i,DatNativeMapFlagKind::Null,0,{}});
    }
    return result;
}

StadiumFormAssets::StadiumFormAssets(Archives archives,
    std::span<const MeleeWebMapLightOverride> base_overrides,std::span<const uint32_t> base_counts)
    :archives_(std::move(archives)){
    require(base_counts.size()==10&&!base_overrides.empty(),"Stadium form owners require the checked base map services");
    std::copy(base_counts.begin(),base_counts.end(),light_counts_.begin());
    std::array<bool,10> supplied{};
    const auto* profile=melee_web_stage_stadium_profile_data();
    require(profile&&profile->map_ownership,"Canonical Stadium ownership is absent");
    for(size_t i=0;i<profile->map_ownership->resident_entry_count;i++)
        supplied.at(profile->map_ownership->resident_entry_ids[i])=true;
    for(const auto& row:base_overrides)append_override(overrides_,row);
    for(unsigned i=0;i<4;i++){
        require(archives_[i]!=nullptr,"Stadium form archive is absent");
        auto contract=stadium_form_map_contract(*archives_[i],i+1);
        maps_[i]=std::make_unique<DatNativeMap>(archives_[i],contract.view());
        const auto counts=maps_[i]->source_light_counts();
        require(counts.size()==10,"Stadium form source light-count inventory changed");
        for(auto id:contract.resident_entry_ids){
            if(supplied[id])require(light_counts_[id]==counts[id],"Shared Stadium map light-count providers disagree");
            else {light_counts_[id]=counts[id];supplied[id]=true;}
        }
        for(const auto& row:maps_[i]->light_overrides())append_override(overrides_,row);
        bool found=false;
        std::set<std::string> names;
        for(const auto& pub:archives_[i]->public_symbols()){
            require(names.insert(pub.name).second,"Stadium form public symbol is duplicated");
            void* value=nullptr;
            if(pub.name=="map_head"){
                require(pub.data_offset==0x400&&!found,"Stadium form map_head identity changed");
                value=maps_[i]->map_head();found=true;
            }
            symbols_.push_back({filenames[i],pub.name.c_str(),value});
        }
        require(found,"Stadium form public map_head is absent");
    }
    require(std::all_of(supplied.begin(),supplied.end(),[](bool x){return x;}),
            "Stadium map light-count provider is missing");
    require(symbols_.size()==123,"Stadium form public catalogs differ from retained C0 inventory");
}
StadiumFormAssets::~StadiumFormAssets(){
    if(scope_){std::fputs("Close Stadium form source archive handles before descriptor destruction\n",stderr);std::abort();}
}
const std::vector<MeleeWebArchiveSymbol>& StadiumFormAssets::public_symbols()const noexcept{return symbols_;}
const std::vector<MeleeWebMapLightOverride>& StadiumFormAssets::light_overrides()const noexcept{return overrides_;}
std::span<const uint32_t> StadiumFormAssets::source_light_counts()const noexcept{return light_counts_;}
const DatNativeMap& StadiumFormAssets::map(unsigned ordinal)const{
    require(ordinal>=1&&ordinal<=4,"Stadium form ordinal is outside authored archives");return *maps_[ordinal-1];
}
void StadiumFormAssets::publish(){
    require(!scope_,"Stadium form archive scope is already published");
    char error[256]{};
    scope_=melee_web_archive_sections_register(symbols_.data(),symbols_.size(),error,sizeof(error));
    if(!scope_)throw DatError(error);
}
void StadiumFormAssets::close(){
    if(!scope_)return;
    char error[256]{};
    if(!melee_web_archive_sections_close(scope_,error,sizeof(error)))throw DatError(error);
    scope_=nullptr;
}
}
