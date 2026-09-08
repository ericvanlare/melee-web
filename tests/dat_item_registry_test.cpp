#include "dat_item_registry.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace melee_web;using Bytes=std::vector<uint8_t>;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
void put(Bytes& b,uint32_t p,uint32_t v){for(int i=0;i<4;i++)b.at(p+i)=uint8_t(v>>(24-8*i));}
struct Fixture {
    Bytes data=Bytes(544);std::vector<uint32_t> relocations{8,32};
    Fixture(){put(data,8,32);put(data,32,504);}
    DatArchive archive(){std::string name="itPublicData";uint32_t p=32+data.size()+4*relocations.size();Bytes b(p+8+name.size()+1);put(b,0,b.size());put(b,4,data.size());put(b,8,relocations.size());put(b,12,1);std::copy(data.begin(),data.end(),b.begin()+32);for(uint32_t i=0;i<relocations.size();i++)put(b,32+data.size()+i*4,relocations[i]);std::copy(name.begin(),name.end(),b.begin()+p+8);return DatArchive(b);}
};
template<class F>void rejects(F f){try{f();}catch(const DatError&){return;}throw std::runtime_error("Expected rejection");}
int main(int argc,char** argv){try{
 if(argc==2){std::ifstream f(argv[1],std::ios::binary);check(bool(f),"Open item archive");Bytes b((std::istreambuf_iterator<char>(f)),{});DatItemRegistry r{DatArchive(b,DatExternalPolicy::PreserveUnresolved)};uint32_t present=0;for(auto p:r.articles)present+=p.has_value();check(present==8,"US source registry has8 preloaded Articles");check(!r.articles[5],"Mario Fire registry slot source null");std::cout<<"US item registry:118 exact slots,8 initial Article refs\n";return 0;}
 Fixture f;DatItemRegistry r(f.archive());check(r.articles.size()==118&&r.articles[0]==504&&!r.articles[5],"Exact source table extent and nulls");
 f=Fixture();put(f.data,32,0);check(DatItemRegistry(f.archive()).articles[0]==0,"Relocated offset0 stays nonnull");
 f=Fixture();put(f.data,36,504);rejects([&]{DatItemRegistry bad(f.archive());});
 f=Fixture();put(f.data,32,505);rejects([&]{DatItemRegistry bad(f.archive());});
 f=Fixture();f.data.resize(480);rejects([&]{DatItemRegistry bad(f.archive());});
 f=Fixture();f.relocations={32};put(f.data,8,0);rejects([&]{DatItemRegistry bad(f.archive());});
 std::cout<<"Item registry exact extent and malformed reference tests passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
