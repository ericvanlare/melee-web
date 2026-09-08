#include "native_dat.hpp"
#include "gameplay_fighter_data.h"
#include "gameplay_article_data.h"
#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace fighter_runtime_test;
extern "C" void melee_web_test_fighter_data(void*,int);
namespace {
Bytes read_file(const char* path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);
    auto n=f.tellg(); check(n>0 && n<64*1024*1024,"Invalid fighter input size");
    f.seekg(0); Bytes bytes((std::istreambuf_iterator<char>(f)),{}); check(bytes.size()==size_t(n),"Truncated input"); return bytes;
}
FighterFixture native_fixture() {
    FighterFixture f;
    put32(f.data,f.command_a,0); f.unlink(f.command_a+4);put32(f.data,f.command_b,0);
    auto append=[&](size_t n) { uint32_t at=uint32_t((f.data.size()+3)&~size_t(3));f.data.resize(at+n);return at; };
    auto model=append(24), vis=append(80), textures=append(20);
    f.link(8,model);f.link(model+4,vis);f.link(model+12,textures);
    for(auto [slot,size]:std::initializer_list<std::pair<uint32_t,size_t>>{
        {0x34,8},{0x38,40},{0x3c,24},{0x44,28},{0x4c,56},{0x54,8},{0x58,28}})f.link(slot,append(size));
    auto items=append(16);f.link(0x48,items);
    for(auto i:{0U,2U}){auto article=append(24),attr=append(0x84);f.link(items+i*4,article);f.link(article,attr);}
    return f;
}
void verify(std::shared_ptr<const DatArchive> archive,const Bytes& container,int actual) {
    uint32_t root=UINT32_MAX;for(const auto&s:archive->public_symbols())if(s.name=="ftDataMario")root=s.data_offset;
    check(root!=UINT32_MAX,"Missing ftDataMario");
    GameplayActionStore actions(archive,mario(),container);
    NativeDatArena owner(archive);uint32_t unresolved;
    void* data=melee_web_fighter_data_decode(owner.reader(),root,0,5,actions.action_rows(),actions.blend_rows(),actions.wait_choices(),&unresolved);
    archive.reset(); // Reader and action store retain the backing archive independently.
    melee_web_test_fighter_data(data,actual);
    check((unresolved&((1U<<3)|(1U<<4)|(1U<<9)|(1U<<18)|(1U<<22)))==0,"Reached fields remain unresolved");
    if(actual)check(unresolved==0x8001e0,"Unexpected unhydrated fields changed");
    std::cout<<"Native fighter data unresolved mask: "<<std::hex<<unresolved<<std::dec<<'\n';
}
}
int main(int argc,char**argv) {
    try {
        auto fixture=native_fixture();
        verify(std::make_shared<const DatArchive>(fixture.file()),fixture.container,0);
        // Missing required pointers and malformed typed values fail in the C decoder
        // and unwind across its frames into the arena owner without publication.
        for(auto slot:{0U,4U,8U,0x30U,0x48U}) {
            auto bad=fixture;bad.unlink(slot);
            rejects([&]{NativeDatArena owner(std::make_shared<const DatArchive>(bad.file()));uint32_t mask;
                (void)melee_web_fighter_data_decode(owner.reader(),0,0,5,nullptr,nullptr,nullptr,&mask);});
        }
        for(auto at:{FighterFixture::co+0x5c,fixture.hurt_rows+12}) {
            auto bad=fixture;put32(bad.data,at,0x7f800000);
            rejects([&]{NativeDatArena owner(std::make_shared<const DatArchive>(bad.file()));uint32_t mask;
                (void)melee_web_fighter_data_decode(owner.reader(),0,0,5,nullptr,nullptr,nullptr,&mask);});
        }
        NativeDatArena owner(std::make_shared<const DatArchive>(fixture.file()));auto r=owner.reader();
        rejects([&]{(void)r->word(r->context,1);});rejects([&]{(void)r->half(r->context,1);});
        rejects([&]{(void)r->word(r->context,0);});
        rejects([&]{(void)r->allocate(r->context,SIZE_MAX,4);});
        rejects([&]{(void)r->region(r->context,0,FighterFixture::co+4);});
        std::cout<<"Owned native ftData synthetic and rejection checks: passed\n";
        if(argc==3)verify(std::make_shared<const DatArchive>(read_file(argv[1])),read_file(argv[2]),1);
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
