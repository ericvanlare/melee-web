// Headless source-state candidate. GPU submission/presentation is excluded;
// this executable cannot produce performance or rendering acceptance evidence.
#include "gameplay_match_session.hpp"
#include "gameplay_audio.h"
#include <array>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

extern "C" int melee_web_retail_setup(const uint8_t*,uint32_t,
    MeleeWebMenuMatchSelection*,char*,size_t);
extern "C" void melee_web_retail_state(void);

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Reader {
    std::vector<uint8_t> bytes;
    size_t cursor=0;
    explicit Reader(const char* path){
        const auto size=std::filesystem::file_size(path);
        check(size>=16+0x138&&size<=16+0x138+36000*44,
              "Reference input size is outside its bounds");
        std::ifstream f(path,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(f),{});
        check(bytes.size()==size,"Reference input read was incomplete");
    }
    uint8_t u8(){check(cursor<bytes.size(),"Reference input is truncated");return bytes[cursor++];}
    uint16_t u16(){const auto hi=u8();const auto lo=u8();return uint16_t(hi)<<8|lo;}
    uint32_t u32(){const auto hi=u16();const auto lo=u16();return uint32_t(hi)<<16|lo;}
};
void hex(const uint8_t* data,size_t count){
    static constexpr char digits[]="0123456789abcdef";
    for(size_t i=0;i<count;i++)std::cout<<digits[data[i]>>4]<<digits[data[i]&15];
}
}

int main(int argc,char** argv){try{
    check(argc==4,"Expected owned menu/game directories and MWRC reference input");
    Reader input(argv[3]);
    check(input.u32()==0x4d575243&&input.u32()==1,"Unsupported reference input format");
    const auto seed=input.u32();const auto frames=input.u32();
    check(frames&&frames<=36000&&input.bytes.size()==16+0x138+size_t(frames)*44,
          "Reference input frame count disagrees with its size");
    std::array<uint8_t,0x138> setup{};for(auto& byte:setup)byte=input.u8();
    char error[256]{};MeleeWebMenuMatchSelection selection{};
    check(melee_web_retail_setup(setup.data(),seed,&selection,error,sizeof(error)),error);
    melee_web::RuntimeFiles files;
    for(const auto* root:{argv[1],argv[2]}){
        for(const auto& entry:std::filesystem::directory_iterator(root)){
            if(!entry.is_regular_file()||entry.file_size()>64*1024*1024)continue;
            std::ifstream stream(entry.path(),std::ios::binary);
            files[entry.path().filename().string()]={std::istreambuf_iterator<char>(stream),{}};
        }
    }
    melee_web::GameplayMatchSession match(files,selection);
    std::cout<<"{\"record\":\"header\",\"schema\":\"melee-web-port-replay-candidate\",\"version\":1,\"frames_requested\":"<<frames
        <<",\"phase\":\"after_source_tick_before_audio_transport\",\"rendering\":\"excluded\",\"comparison\":\"not_run\"}\n";
    std::cout<<"{\"record\":\"match_enter\",\"rng\":"<<seed<<",\"start_melee_hex\":\"";hex(setup.data(),setup.size());std::cout<<"\"}\n";
    std::cout<<"{\"record\":\"match_enter_complete\",";melee_web_retail_state();std::cout<<"}\n";
    unsigned audio_phase=0;float pcm[1068];
    for(uint32_t index=0;index<frames;index++){
        check(!match.paused()&&!match.complete(),"Reference workload reached an unsupported pause/exit");
        const auto offset=input.cursor;PADStatus pads[4]{};
        for(auto& pad:pads){
            pad.button=input.u16();pad.stickX=std::bit_cast<int8_t>(input.u8());pad.stickY=std::bit_cast<int8_t>(input.u8());
            pad.substickX=std::bit_cast<int8_t>(input.u8());pad.substickY=std::bit_cast<int8_t>(input.u8());
            pad.triggerLeft=input.u8();pad.triggerRight=input.u8();pad.analogA=input.u8();pad.analogB=input.u8();pad.err=std::bit_cast<int8_t>(input.u8());
        }
        match.tick(pads);
        std::cout<<"{\"record\":\"frame\",\"index\":"<<index<<",\"supplied_inputs\":[";
        for(unsigned port=0;port<4;port++){if(port)std::cout<<",";std::cout<<"\"";hex(input.bytes.data()+offset+port*11,11);std::cout<<"\"";}
        std::cout<<"],";melee_web_retail_state();std::cout<<"}\n";
        audio_phase+=32000;const auto samples=audio_phase/60;audio_phase%=60;
        check(melee_web_audio_render(match.audio(),pcm,samples,error,sizeof(error)),error);
    }
    match.close();
    std::cout<<"{\"record\":\"end\",\"frames\":"<<frames<<",\"status\":\"captured\"}\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
