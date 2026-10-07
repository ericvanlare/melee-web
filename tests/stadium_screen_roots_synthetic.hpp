#pragma once
#include "stadium_screen_roots_checks.hpp"
#include "gameplay_bootstrap.h"
#include <array>
#include <vector>
#include <functional>
namespace melee_web::test::stadium_screen::synthetic {
inline void word(std::vector<uint8_t>& b, size_t p, uint32_t n) {
    for (unsigned i=0;i<4;++i) b.at(p+i)=uint8_t(n>>(24-8*i));
}
inline std::vector<uint8_t> fixture(bool image_public=true, bool sis_public=true,
                             uint32_t public_image=0x3a0, bool malformed_sis=false) {
    std::vector<uint8_t> data(0x700);
    std::vector<uint32_t> relocations;
    auto link=[&](uint32_t p,uint32_t n){word(data,p,n);relocations.push_back(p);};
    link(0x100,0x140);word(data,0x104,1);link(0x108,0x160);word(data,0x10c,3);
    link(0x140,0x200);link(0x144,0xe0);word(data,0x148,1);
    link(0x160,0x200);link(0x194,0x240);link(0x1c8,0x240);
    for(auto joint:{0x200U,0x240U})
        for(unsigned i=0;i<3;++i)word(data,joint+32+4*i,0x3f800000);
    link(0x250,0x280);link(0x288,0x300);
    word(data,0x304,0x14);link(0x308,0x340);link(0x30c,0x320);
    word(data,0x32c,0x3f800000);word(data,0x330,0x42000000);
    word(data,0x34c,4);
    for(unsigned i=0;i<3;++i)word(data,0x35c+4*i,0x3f800000);
    data[0x37c]=data[0x37d]=1;word(data,0x380,0x40011);
    word(data,0x384,0x3f800000);word(data,0x388,1);link(0x38c,0x3a0);
    link(0x3a0,0x400);word(data,0x3a4,0x00080008);
    link(0x508,0x520);link(0x50c,0x520);
    data[0x520]=malformed_sis?8:1;data[0x521]=0;
    std::vector<std::pair<uint32_t,std::string>> publics={{0x100,"map_head"}};
    if(image_public)publics.emplace_back(public_image,image_name);
    if(sis_public)publics.emplace_back(0x500,sis_name);
    std::vector<uint8_t> names;
    std::vector<uint32_t> name_offsets;
    for(const auto& [offset,name]:publics){
        (void)offset;name_offsets.push_back(names.size());
        names.insert(names.end(),name.begin(),name.end());names.push_back(0);
    }
    const size_t table=32+data.size(), pubs=table+4*relocations.size();
    std::vector<uint8_t> bytes(pubs+8*publics.size()+names.size());
    word(bytes,0,bytes.size());word(bytes,4,data.size());
    word(bytes,8,relocations.size());word(bytes,12,publics.size());
    std::copy(data.begin(),data.end(),bytes.begin()+32);
    for(size_t i=0;i<relocations.size();++i)word(bytes,table+4*i,relocations[i]);
    for(size_t i=0;i<publics.size();++i){word(bytes,pubs+8*i,publics[i].first);word(bytes,pubs+8*i+4,name_offsets[i]);}
    std::copy(names.begin(),names.end(),bytes.begin()+pubs+8*publics.size());
    return bytes;
}
template<class F>void rejects(F&& f,const char* message){
    bool rejected=false;
    try{f();}catch(const DatError& e){rejected=std::string_view(e.what()).find(message)!=std::string_view::npos;}
    require(rejected,"Screen negative failed at its declared boundary");
}
inline const DatNativeMapContract& contract(){
    static const std::array<uint8_t,3> counts={1,1,1};
    static const std::array<uint32_t,3> residents={0,1,2};
    static const DatNativeMapContract value{3,counts,residents,{},{},{}};
    return value;
}
}

namespace melee_web::test::stadium_screen {
inline void synthetic_checks(const std::function<void()>& verify = [] {}){
    using namespace synthetic;
    const auto bytes=fixture();
    const auto archive=std::make_shared<const DatArchive>(bytes);
    const auto before=melee_web_gameplay_stats();
    verify();
    for(unsigned lifetime=0;lifetime<2;++lifetime){
        DatNativeMap map(archive,contract());
        DatSis sis(archive,sis_name);
        require(root(*archive,image_name)==0x3a0 && sis.entry_count()==8,
                "Synthetic screen authored roots/table changed");
        {
            DatNativeMap foreign(archive,contract());
            rejects([&]{identity(map,1,foreign.image_descriptor(0x3a0));},"unique map descriptor");
            verify();
        }
        rejects([&]{map.image_descriptor(0x3c0);},"absent from native map");
        verify();
        const auto first=identity(map,1,map.image_descriptor(0x3a0));
        const auto alias=identity(map,2,map.image_descriptor(0x3a0));
        require(first.image==alias.image && first.texture!=alias.texture,
                "Source IMAGE aliases were not canonicalized across map graphs");
        catalog_checks(*archive,map,sis,1,root(*archive,image_name));
        auto** table=static_cast<uint8_t**>(sis.descriptor());
        require(table[2]==table[3] && table[2][0]==1,"SIS alias/restoration failed");
        verify();
    }
    for(const auto* missing:{image_name,sis_name}){
        auto absent=std::make_shared<const DatArchive>(fixture(missing!=image_name,missing!=sis_name));
        rejects([&]{root(*absent,missing);},"Missing required screen public root");
        verify();
    }
    const auto outside=std::make_shared<const DatArchive>(fixture(true,true,0x3c0));
    {
        DatNativeMap map(outside,contract());
        rejects([&]{map.image_descriptor(root(*outside,image_name));},"absent from native map");
        verify();
    }
    rejects([&]{DatSis bad(std::make_shared<const DatArchive>(fixture(true,true,0x3a0,true)),sis_name);},"branch relocation");
    verify();
    const auto after=melee_web_gameplay_stats();
    require(before.generation==after.generation && before.ticks==after.ticks,
            "Synthetic descriptor checks changed generation/ticks");
    require(std::equal(archive->data().begin(),archive->data().end(),bytes.begin()+32),
            "Screen checks mutated raw synthetic archive");
}
}
