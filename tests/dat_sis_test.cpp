#include "dat_sis.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
using Bytes = std::vector<uint8_t>;
static void check(bool value) { if (!value) throw std::runtime_error("SIS check failed"); }
static void word(Bytes& b, size_t p, uint32_t n) {
    for (unsigned i=0;i<4;i++) b.at(p+i)=uint8_t(n>>(24-8*i));
}
static Bytes fixture() {
    Bytes b(32+576+16+8+4);
    word(b,0,b.size());word(b,4,576);word(b,8,4);word(b,12,1);
    word(b,32,64);word(b,36,32);word(b,40,16);word(b,44,16);
    const uint8_t stream[]={14,2,0,1,0,0x40,0,15,0};
    std::copy(std::begin(stream),std::end(stream),b.begin()+48);
    b[64]=1;b[65]=2;
    for(unsigned i=0;i<4;i++)word(b,608+4*i,4*i);
    b[632]='s';return b;
}
int main() { try {
    auto bytes=fixture(), original=bytes;
    auto archive=std::make_shared<melee_web::DatArchive>(bytes);
    melee_web::DatSis sis(archive,"s");
    auto** table=static_cast<uint8_t**>(sis.descriptor());
    check(sis.entry_count()==4 && table[2]==table[3]);
    check((uintptr_t(table[0])&31)==0 && table[2][1]==2 && table[2][5]==0x40);
    table[2][1]=3;
    check(archive->range(16,2)[1]==2 && bytes==original);
    auto rejects=[](const Bytes& b){bool caught=false;try {
        melee_web::DatSis bad(std::make_shared<melee_web::DatArchive>(b),"s");
    }catch(const melee_web::DatError&){caught=true;}check(caught);};
    auto bad=fixture();bad[54]=1;rejects(bad); // custom glyph 1, atlas contains only 0
    bad=fixture();bad[48]=8;rejects(bad); // unsupported branch relocation
    bad=fixture();std::fill(bad.begin()+48,bad.begin()+64,1);rejects(bad); // no terminator
    bad=fixture();bad[62]=14;std::fill(bad.begin()+48,bad.begin()+62,1);rejects(bad); // truncated operand
    std::cout<<"owned SIS bytecode/font bounds and alias checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
