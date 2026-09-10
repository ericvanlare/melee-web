#include "dat_archive.hpp"
#include "dat_collision.hpp"
#include "dat_lights.hpp"
#include "dat_stage.hpp"
#include "gameplay_stage_dream_land.h"
#include "native_dat.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace melee_web;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint32_t root(const DatArchive& archive,const char* name){
    for(const auto& symbol:archive.public_symbols())if(symbol.name==name)return symbol.data_offset;
    throw std::runtime_error(std::string("missing symbol: ")+name);
}
static bool near(float a,float b){return std::isfinite(a)&&std::fabs(a-b)<0.0001f;}
int main(int argc,char** argv){try{
    check(argc==2,"expected owned GrOp.dat");std::ifstream file(argv[1],std::ios::binary);
    check(bool(file),"cannot open GrOp.dat");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
    auto archive=std::make_shared<DatArchive>(bytes);DatStage stage(*archive);
    check(stage.entries.size()==8,"Dream Land map entry count changed");
    constexpr bool joint_anim[8]={false,true,true,false,true,false,true,true};
    constexpr bool material_anim[8]={false,false,true,false,true,false,false,true};
    for(unsigned i=0;i<8;++i)check(stage.entries[i].joint_offset.has_value()&&
        stage.entries[i].joint_animation_table.has_value()==joint_anim[i]&&
        stage.entries[i].material_animation_table.has_value()==material_anim[i]&&
        !stage.entries[i].shape_animation_table,"Dream Land map animation services changed");
    check(stage.joint_reference_table.count==1&&stage.spline_table.count==0&&
        stage.light_override_table.count==38&&stage.shadow_table.count==10&&
        stage.flagged_object_table.count==8,"Dream Land stage service table counts changed");
    DatCollision collision(*archive);DatLights lights(*archive);
    check(near(read_dat_stage_scale(*archive),1)&&collision.vertices.size()==14&&
        collision.lines.size()==11&&collision.joints.size()==4&&lights.lights.size()==3,
        "Dream Land scale, collision or light identity changed");
    check(read_dat_collision_bindings(*archive,stage,collision).empty(),
        "Dream Land unexpectedly gained moving collision bindings");
    NativeDatArena arena(archive);auto* yaku=static_cast<MeleeWebDreamLandYakumono*>(
        melee_web_dream_land_yakumono_decode(arena.reader(),root(*archive,"yakumono_param")));
    check(yaku&&yaku->bird_timer_min==3000&&yaku->bird_timer_max==4000&&
        yaku->bird_height==30&&yaku->tree_timer_min==1200&&yaku->tree_timer_max==600&&
        near(yaku->wind_speed,0.2f)&&near(yaku->wind_left_inner,-17)&&near(yaku->wind_right_inner,76)&&
        near(yaku->wind_left_outer,-18)&&near(yaku->wind_right_outer,-74)&&near(yaku->wind_top,40)&&
        near(yaku->wind_bottom,-10)&&near(yaku->blink_timer_max,180)&&near(yaku->blink_timer_min,360),
        "Dream Land bird, tree, wind or blink source parameters changed");
    std::cout<<"Dream Land exact eight maps, animation services, collision, lights, shadows and scheduler parameters passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
