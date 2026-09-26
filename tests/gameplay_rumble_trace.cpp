#include "gameplay_rumble.h"
#include "native_dat.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
extern "C" void lb_80014534(void);
extern "C" void* melee_web_rumble_exchange(void*);
extern "C" int melee_web_test_rumble_sequence(void);
extern "C" int melee_web_test_rumble_queue(void);
extern "C" int melee_web_test_rumble_clear(void);
using namespace melee_web;
static void check(bool b){if(!b)throw std::runtime_error("Rumble boundary check failed");}
static uint32_t fake_pointer(void*,uint32_t at,size_t){
    if(at<0x200||(at-0x200)%8)throw std::runtime_error("fake rumble table pointer is out of range");
    return 0x20+((at-0x200)/8)*4;
}
static const void* fake_region(void*,uint32_t at,size_t){
    if(at!=0x200)throw std::runtime_error("fake rumble table region differs");
    return reinterpret_cast<const void*>(uintptr_t(1));
}
static uint16_t fake_half(void*,uint32_t at){return (at&2)?0:0x2001;}
static uint8_t fake_byte(void*,uint32_t){return 0;}
static void* fake_allocate(void*,size_t count,size_t width){return std::calloc(count,width);}
static void fake_reject(void*,const char* why){throw std::runtime_error(why);}
static void check_owner_route(MeleeWebRumble* decoded,char* error,size_t size){
    check(melee_web_rumble_publish_source(decoded,error,size));
    check(melee_web_rumble_source_ready());
    /* The original VS callback must reuse this exact published owner; the
     * fallback loader is intentionally unavailable in this focused test. */
    lb_80014534();
    void* const rows=melee_web_rumble_source_rows(decoded);
    void* const mismatch=reinterpret_cast<void*>(uintptr_t(1));
    check(melee_web_rumble_exchange(mismatch)==rows);
    check(!melee_web_rumble_source_ready());
    check(melee_web_rumble_exchange(rows)==mismatch);
    check(melee_web_rumble_source_ready());
    lb_80014534();
    check(melee_web_rumble_begin(decoded,error,size));
    check(melee_web_rumble_source_ready());
    check(melee_web_rumble_end(decoded,error,size));
    check(melee_web_rumble_source_ready());
    check(melee_web_rumble_clear_source(decoded,error,size));
    check(!melee_web_rumble_source_ready());
}
static int owner_only(){
    MeleeWebNativeDat reader{};
    reader.pointer=fake_pointer;reader.region=fake_region;reader.half=fake_half;
    reader.byte=fake_byte;reader.allocate=fake_allocate;reader.reject=fake_reject;
    auto* decoded=melee_web_rumble_decode(&reader,0x200,40);
    char error[256]{};
    check(decoded&&!melee_web_rumble_source_ready());
    check_owner_route(decoded,error,sizeof(error));
    std::cout<<"source rumble owner gate: passed\n";
    return 0;
}
int main(int argc,char** argv){try{
    if(argc==1)return owner_only();
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
    check(!melee_web_rumble_source_ready());
    for(unsigned cycle=0;cycle<2;cycle++){
        check_owner_route(decoded,error,sizeof(error));
        check(melee_web_rumble_begin(decoded,error,sizeof(error)));
        check(!melee_web_rumble_begin(decoded,error,sizeof(error)));
        check(melee_web_test_rumble_sequence());
        check(melee_web_test_rumble_queue());
        check(melee_web_rumble_end(decoded,error,sizeof(error)));
        check(melee_web_test_rumble_clear());
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
