#include "dat_fighter_runtime.hpp"
#include "fighter_binding.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using namespace melee_web;
static std::vector<uint8_t> read_file(const char* path){
    std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error(std::string("cannot open ")+path);
    return {std::istreambuf_iterator<char>(input),{}};
}
static uint32_t root(const DatArchive& archive,const char* name){
    for(const auto& symbol:archive.public_symbols())if(symbol.name==name)return symbol.data_offset;
    throw std::runtime_error(std::string("missing public symbol: ")+name);
}
int main(int argc,char** argv){try{
    if(argc!=10)throw std::runtime_error("expected Marth fighter, animation, effect, audio and five costume assets");
    const FighterCostume* identity=nullptr;
    for(const auto& costume:fighter_costumes())if(costume.fighter_kind==18&&costume.costume_index==0)identity=&costume;
    if(!identity||identity->motion_count!=327||identity->fighter_symbol!="ftDataMars")
        throw std::runtime_error("generated Marth identity differs from source metadata");
    auto archive=std::make_shared<const DatArchive>(read_file(argv[1]));
    auto runtime=std::make_shared<const DatFighterRuntime>(archive,*identity);
    if(runtime->actions().size()!=327||!runtime->mars_attributes())
        throw std::runtime_error("Marth actions or exact MarsAttributes are incomplete");
    const auto fighter_root=root(*archive,"ftDataMars");
    if(archive->pointer(fighter_root+0x48,1))
        throw std::runtime_error("Marth source Article table must be null");
    const auto& mars=*runtime->mars_attributes();
    if(mars.absorb_bone<0||mars.absorb_size<=0||mars.sword_x14<0)
        throw std::runtime_error("Marth Counter or sword attributes are invalid");
    DatFighterAnimationStore animations(runtime,read_file(argv[2]));
    for(const unsigned motion:{44,295,296,297,303,304,310,321,323,324,326}){
        const auto selected=animations.select(motion);
        if(!selected.action.archive_bytes||!selected.animation)
            throw std::runtime_error("Marth action animation is missing: "+std::to_string(motion));
        if(selected.action.command_offset&&!selected.commands)
            throw std::runtime_error("Marth action command stream is missing: "+std::to_string(motion));
    }
    const DatArchive effects(read_file(argv[3]));const auto effect_root=root(effects,"effMarsDataTable");
    if(effects.next_target_offset(effect_root)-effect_root<8+2*20||
        effects.next_target_offset(effect_root)-effect_root>=8+3*20)
        throw std::runtime_error("Marth effect table does not contain exactly two entries");
    if(read_file(argv[4]).empty())throw std::runtime_error("Marth audio bank is empty");
    constexpr std::array<const char*,5> models={"PlyMars5K_Share_joint","PlyMars5KRe_Share_joint",
        "PlyMars5KGr_Share_joint","PlyMars5KBk_Share_joint","PlyMars5KWh_Share_joint"};
    for(size_t i=0;i<models.size();++i){const DatArchive model(read_file(argv[5+i]));(void)root(model,models[i]);}
    std::cout<<"Marth exact metadata, 327 actions, five costumes, MarsAttributes, specials, effect bank 16/count 2 and audio passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
