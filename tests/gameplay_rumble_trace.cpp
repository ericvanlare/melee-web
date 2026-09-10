#include "gameplay_rumble.h"
#include "native_dat.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
extern "C" int melee_web_test_rumble_sequence(void);
using namespace melee_web;
static void check(bool b){if(!b)throw std::runtime_error("Rumble boundary check failed");}
int main(int argc,char** argv){try{
    check(argc==2);
    std::ifstream f(argv[1],std::ios::binary);check(bool(f));
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
    auto archive=std::make_shared<DatArchive>(bytes);
    uint32_t root=UINT32_MAX;
    for(const auto& symbol:archive->public_symbols())if(symbol.name=="lbRumbleData")root=symbol.data_offset;
    check(root!=UINT32_MAX&&archive->next_target_offset(root)-root==40*8);
    NativeDatArena arena(archive);
    auto* decoded=melee_web_rumble_decode(arena.reader(),root,40);
    char error[256]{};
    for(unsigned cycle=0;cycle<2;cycle++){
        check(melee_web_rumble_begin(decoded,error,sizeof(error)));
        check(!melee_web_rumble_begin(decoded,error,sizeof(error)));
        check(melee_web_test_rumble_sequence());
        check(melee_web_rumble_end(decoded,error,sizeof(error)));
        check(!melee_web_rumble_end(decoded,error,sizeof(error)));
    }
    const auto program=*archive->pointer(root,2);
    unsigned rejected=0;
    for(uint16_t word:{uint16_t(0xc000),uint16_t(0xa000),uint16_t(0)}){
        auto bad=bytes;bad[32+program]=word>>8;bad[33+program]=word;
        try{NativeDatArena corrupt(std::make_shared<DatArchive>(bad));
            melee_web_rumble_decode(corrupt.reader(),root,40);
        }catch(const DatError&){rejected++;}
    }
    check(rejected==3);
    std::cout<<"40 owned rumble programs decoded; source motor sequence, restart and malformed commands passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
