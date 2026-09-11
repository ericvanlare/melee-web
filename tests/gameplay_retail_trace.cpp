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
#include <memory>
#include "gameplay_retail_recipe.hpp"


namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

}

int main(int argc,char** argv){try{
    check(argc==4,"Expected owned menu/game directories and MWRC reference input");
    const auto size=std::filesystem::file_size(argv[3]);
    check(size<=melee_web::kRetailReplayMaxBytes,"Reference input exceeds size limit");
    std::ifstream stream(argv[3],std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream),{}};
    check(bytes.size()==size,"Reference input read was incomplete");
    auto recipe=melee_web::read_retail_replay(bytes);
    melee_web::RuntimeFiles files;
    for(const auto* root:{argv[1],argv[2]}){
        for(const auto& entry:std::filesystem::directory_iterator(root)){
            if(!entry.is_regular_file()||entry.file_size()>64*1024*1024)continue;
            std::ifstream stream(entry.path(),std::ios::binary);
            files[entry.path().filename().string()]={std::istreambuf_iterator<char>(stream),{}};
        }
    }
    auto owned_match=recipe.initial_input?
        std::make_unique<melee_web::GameplayMatchSession>(files,recipe.selection,*recipe.initial_input):
        std::make_unique<melee_web::GameplayMatchSession>(files,recipe.selection);
    auto& match=*owned_match;
    melee_web::retail_replay_initial(recipe,false);
    unsigned audio_phase=0;float pcm[1068];char error[256]{};
    for(uint32_t index=0;index<recipe.frames.size();index++){
        check(!match.paused()&&!match.complete(),"Reference workload reached an unsupported pause/exit");
        match.tick(recipe.frames[index].pads.data());
        melee_web::retail_replay_frame(recipe,index);
        audio_phase+=32000;const auto samples=audio_phase/60;audio_phase%=60;
        check(melee_web_audio_render(match.audio(),pcm,samples,error,sizeof(error)),error);
    }
    match.close();
    melee_web::retail_replay_end(recipe.frames.size());
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
