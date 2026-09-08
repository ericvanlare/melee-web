#include "common_tables.h"
#include "dat_common.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <memory>
#include <vector>

struct FighterPartsTable;
extern "C" FighterPartsTable** ftPartsTable;
extern "C" unsigned common_tables_check_native_layout(const MeleeWebCommonNative*,const MeleeWebCommonTables*);
static char error[256];
static void check(bool condition,const char* message)
{ if(!condition){std::fprintf(stderr,"%s: %s\n",message,error);std::exit(1);} }
using Owner=std::unique_ptr<MeleeWebCommonNative,decltype(&melee_web_common_tables_destroy)>;
static Owner create(const MeleeWebCommonTables& tables)
{
    Owner owner(melee_web_common_tables_create(&tables,error,sizeof(error)),melee_web_common_tables_destroy);
    check(bool(owner),"native table graph construction");return owner;
}
static std::uint32_t lookup(MeleeWebCommonNative* owner,unsigned kind,unsigned name)
{
    std::uint32_t result=0;
    check(melee_web_common_parts_lookup(owner,kind,name,&result,error,sizeof(error)),"original named part lookup");return result;
}
static std::uint32_t remap(MeleeWebCommonNative* owner,unsigned to,unsigned from,unsigned joint)
{
    std::uint32_t result=0;
    check(melee_web_common_parts_remap(owner,to,from,joint,&result,error,sizeof(error)),"original cross-fighter part remap");return result;
}
int main(int argc,char** argv)
{
    auto input=std::make_unique<MeleeWebCommonTables>();
    input->ready_mask=MELEE_WEB_COMMON_STATIC_ROOT_MASK;
    input->item_throw[25].heavy_mul=1.5f;input->swing[5][4]=2.5f;input->stale[8]=.125f;
    for(auto& shake:input->damage_shake){shake.count=1;shake.samples[0]={1.25f,-2.5f};}
    input->grab_shake=input->smash_shake=input->damage_shake[0];
    input->scale_modifiers[38]=3.5f;input->bunny_modifiers[14]=4.5f;
    input->metal_modifiers[8]=5.5f;input->gravity_weight[1]=6.5f;
    input->primary_colors[4]={12,34,56,78};input->secondary_colors[4]={90,87,65,43};
    input->crowd.angle_min=-2.5f;input->crowd.x1C=-3;input->crowd.blastzone_y_offset=125.25f;
    auto initialize_map=[](MeleeWebCommonParts& map) {
        map.part_count=3;
        std::fill_n(map.joint_to_part,MELEE_WEB_COMMON_MAX_PARTS,255);
        std::fill_n(map.part_to_joint,MELEE_WEB_COMMON_PART_NAMES,255);
        map.joint_to_part[0]=2;map.joint_to_part[2]=53;
        map.part_to_joint[2]=0;map.part_to_joint[53]=2;
    };
    for(auto& map:input->parts) initialize_map(map);
    initialize_map(input->none_parts);
    input->alternates[4].has_descriptor=1;input->alternates[4].count=1;
    input->alternates[4].entries[0]={2,0,3,255};
    auto first=create(*input);
    check(common_tables_check_native_layout(first.get(),input.get())==0,
          "authored pointers, mixed count words and native layouts read through original C descriptors");
    input->parts[1].part_to_joint[53]=1;
    auto second=create(*input);
    input.reset();
    check(lookup(first.get(),32,53)==2&&lookup(second.get(),1,53)==1,"independent graphs own copied named maps");
    check(remap(first.get(),32,0,2)==2&&remap(second.get(),1,0,2)==1,
          "original remap follows names across owned fighter maps");
    check(remap(first.get(),32,MELEE_WEB_COMMON_FIGHTERS,2)==2,
          "original typed remap accepts the source-only FTKIND_NONE part table");
    check(remap(first.get(),32,0,1)==255&&remap(first.get(),32,0,3)==255&&
          remap(first.get(),32,0,0xffffffff)==255,"original unnamed and out-of-skeleton remaps retain invalid sentinel");
    check(ftPartsTable==nullptr,"native graph construction and reads leave no published global");
    ftPartsTable=static_cast<FighterPartsTable**>(const_cast<void*>(melee_web_common_tables_root(first.get(),4)));
    const auto prior=ftPartsTable;
    check(lookup(second.get(),1,53)==1&&ftPartsTable==prior,"source consumer restores an existing real native graph");
    ftPartsTable=nullptr;
    std::uint32_t output=123;
    check(!melee_web_common_parts_lookup(first.get(),33,0,&output,error,sizeof(error))&&output==123,
          "invalid fighter does not enter original unchecked lookup");
    check(!melee_web_common_parts_lookup(first.get(),0,54,&output,error,sizeof(error))&&output==123,
          "alignment padding and raw joint enums are not named map entries");
    check(!melee_web_common_tables_root(first.get(),0)&&!melee_web_common_tables_root(first.get(),20)&&
          !melee_web_common_tables_root(first.get(),23),"only actually ready static roots are exposed");
    first.reset();check(lookup(second.get(),1,53)==1,"destroying one graph leaves another independent owner live");
    auto invalid=std::make_unique<MeleeWebCommonTables>();
    invalid->ready_mask=1U<<17;
    check(!melee_web_common_tables_create(invalid.get(),error,sizeof(error)),"unknown root cannot be asserted ready");
    invalid->ready_mask=1U<<4;
    check(!melee_web_common_tables_create(invalid.get(),error,sizeof(error)),"empty part graphs reject before native allocation");
    invalid->ready_mask=1U<<5;
    check(!melee_web_common_tables_create(invalid.get(),error,sizeof(error)),"alternates require the corresponding named graph");
    invalid->ready_mask=1U<<12;invalid->scale_modifiers[38]=std::numeric_limits<float>::infinity();
    check(!melee_web_common_tables_create(invalid.get(),error,sizeof(error)),"nonfinite source modifier input rejects");
    invalid.reset();

    if(argc==2) {
        std::ifstream file(argv[1],std::ios::binary|std::ios::ate);const auto size=file.tellg();
        check(file&&size>0&&size<=melee_web::DatArchive::max_archive_bytes,"bounded local common input");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));file.seekg(0);
        check(bool(file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))),"read local common input");
        auto decoded=std::make_unique<melee_web::DatCommon>(melee_web::DatArchive(bytes));
        check(decoded->tables.ready_mask==MELEE_WEB_COMMON_STATIC_ROOT_MASK,"local PlCo has all 15 decoded static roots");
        auto native=create(decoded->tables);
        check(common_tables_check_native_layout(native.get(),&decoded->tables)==0,
              "actual local graphs read through original C descriptor types");
        decoded.reset();bytes.clear();
        check(lookup(native.get(),0,1)==1&&lookup(native.get(),0,53)==60&&lookup(native.get(),1,53)==72,
              "actual Mario and Fox named maps reach original lookup");
        check(remap(native.get(),1,0,47)==5&&remap(native.get(),1,0,21)==255,
              "actual Mario-to-Fox remap reaches original source function");
        check(remap(native.get(),0,MELEE_WEB_COMMON_FIGHTERS,0)<MELEE_WEB_COMMON_MAX_PARTS,
              "actual FTKIND_NONE row reaches the original typed remap function");
        for(unsigned i=0;i<23;++i)
            check(bool(melee_web_common_tables_root(native.get(),i))==bool(MELEE_WEB_COMMON_STATIC_ROOT_MASK&(1U<<i)),
                  "native local root publication is limited to decoded static graph ownership");
        check(ftPartsTable==nullptr,"actual local graph consumption restores original global");
        std::puts("Local PlCo 15 static roots and original Mario/Fox part consumers: passed");
    }
    std::puts("Original common table ownership and part lookup trace: passed");
}
