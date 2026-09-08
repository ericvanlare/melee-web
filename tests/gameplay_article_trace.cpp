#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "gameplay_article_item_state.h"
#include "dat_archive.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace melee_web;

static void check(int ok,const char* why)
{
    if(!ok){std::cerr<<why<<'\n';throw DatError(why);}
}
static std::vector<uint8_t> bytes(const std::filesystem::path& p)
{
    std::ifstream f(p);check(bool(f),"Open owned article-test asset");
    return {std::istreambuf_iterator<char>(f),{}};
}

static void step(MeleeWebMatchContext* match,const PADStatus& pad,char* error)
{
    PADStatus pads[4]={{0}};pads[0]=pad;
    check(melee_web_match_step_raw(match,pads,error,256),error);
}

static MeleeWebArticleItemSnapshot item_snapshot()
{
    MeleeWebArticleItemSnapshot result{};
    check(melee_web_article_item_snapshot(&result),
        "Original item list was not available while the match was live");
    return result;
}

int main(int argc,char** argv)
{
    try {
        check(argc==2,"Expected local asset directory");
        RuntimeFiles files;
        for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat",
                              "GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat",
                              "PdPm.dat","sislib_font.bin"})
            files[name]=bytes(std::filesystem::path(argv[1])/name);
        char error[256];
        for(unsigned cycle=0;cycle<2;cycle++) {
            GameplayWorld world(files);
            MeleeWebMatchContext* match=nullptr;
            try {
              MeleeWebPlayerSettings players[2]={
                {0,0,4,{-20,world.floor_height(-20)+1,0},1},
                {1,1,4,{20,world.floor_height(20)+1,0},-1}};
            match=melee_web_match_begin_players(players,2,2,1,world.collision(),error,sizeof(error));
            check(match!=nullptr,error);
            check(melee_web_match_create_fighters(match,error,sizeof(error)),error);

            PADStatus neutral={0};
            MeleeWebMatchStats stats[2];
            for(unsigned tick=0;tick<30;tick++) {
                step(match,neutral,error);
                check(melee_web_match_player_stats(match,0,&stats[0],error,sizeof(error)),error);
                check(melee_web_match_player_stats(match,1,&stats[1],error,sizeof(error)),error);
            }
            check(stats[0].ground_or_air==0,"Mario did not settle onto the source ground");
            check(item_snapshot().fireball_count==0,
                "A Mario fireball survived into the new match");

            PADStatus ground_fire={0};ground_fire.button=PAD_BUTTON_B;
            step(match,ground_fire,error);
            check(melee_web_match_player_stats(match,0,&stats[0],error,sizeof(error)),error);
            check(melee_web_match_player_stats(match,1,&stats[1],error,sizeof(error)),error);
            check((stats[0].held_buttons&PAD_BUTTON_B)!=0&&stats[1].held_buttons==0,
                "Ground raw PAD_BUTTON_B did not reach only the selected fighter");
            const float p2_damage_before=stats[1].damage_percent;
            bool ground_created=false,ground_expired=false,ground_hit=false,settled=false;
            float first_fire_life=0.0f;
            float last_fire_x=0.0f,last_fire_y=0.0f,last_fire_vx=0.0f,last_fire_vy=0.0f;
            float last_fire_life=0.0f,last_fire_hit_damage=0.0f;
            float last_special[5]={0},last_attr[7]={0};
            unsigned last_fire_hit_state=0,last_fire_hit_flags=0;
            int last_fire_damage=0;
            for(unsigned tick=0;tick<360;tick++) {
                step(match,neutral,error);
                check(melee_web_match_player_stats(match,0,&stats[0],error,sizeof(error)),error);
                check(melee_web_match_player_stats(match,1,&stats[1],error,sizeof(error)),error);
                check(stats[1].held_buttons==0,
                    "Stationary P2 unexpectedly received raw input during ground fireball");
                MeleeWebArticleItemSnapshot items=item_snapshot();
                if(!ground_created&&items.fireball_count!=0){
                    ground_created=true;
                    first_fire_life=items.fireball_life_max;
                    std::cerr<<"ground fire first pos "<<items.fireball_pos_x<<","<<items.fireball_pos_y
                             <<" vel "<<items.fireball_vel_x<<","<<items.fireball_vel_y
                             <<" life "<<items.fireball_life_min<<" hit state "<<items.fireball_hit_state
                             <<" special0 "<<items.fireball_special_0<<" attr x58 "
                             <<items.fireball_attr_x58<<'\n';
                }
                if(items.fireball_count!=0){
                    last_fire_x=items.fireball_pos_x;
                    last_fire_y=items.fireball_pos_y;
                    last_fire_vx=items.fireball_vel_x;
                    last_fire_vy=items.fireball_vel_y;
                    last_fire_life=items.fireball_life_min;
                    last_special[0]=items.fireball_special_0;
                    last_special[1]=items.fireball_special_4;
                    last_special[2]=items.fireball_special_8;
                    last_special[3]=items.fireball_special_c;
                    last_special[4]=items.fireball_special_10;
                    last_attr[0]=items.fireball_attr_fall_speed;
                    last_attr[1]=items.fireball_attr_fall_speed_max;
                    last_attr[2]=items.fireball_attr_x50;
                    last_attr[3]=items.fireball_attr_x54;
                    last_attr[4]=items.fireball_attr_x58;
                    last_attr[5]=items.fireball_attr_x5c;
                    last_attr[6]=items.fireball_attr_x60;
                    last_fire_hit_state=items.fireball_hit_state;
                    last_fire_hit_damage=items.fireball_hit_damage;
                    last_fire_hit_flags=items.fireball_hit_flags;
                    last_fire_damage=items.fireball_damage_max;
                }
                if(ground_created&&items.fireball_count==0)ground_expired=true;
                if(stats[1].damage_percent>p2_damage_before+0.001f)ground_hit=true;
                /* ftCo_MS_Wait is the original common motion id 14. Keep this
                 * trace's C++ boundary independent of the source forward.h,
                 * which declares platform ssize_t types before gameplay_compat. */
                settled=stats[0].motion_id==14&&stats[0].ground_or_air==0;
                if(ground_created&&ground_expired&&ground_hit&&settled)break;
            }
            if(!ground_hit){
                std::cerr<<"ground fire trace: last pos "<<last_fire_x<<","<<last_fire_y
                         <<" vel "<<last_fire_vx<<","<<last_fire_vy
                         <<" life "<<last_fire_life<<" hit state "<<last_fire_hit_state
                         <<" hit damage "<<last_fire_hit_damage<<" hit flags 0x"
                         <<std::hex<<last_fire_hit_flags<<std::dec
                         <<" item damage "<<last_fire_damage
                         <<" special ["<<last_special[0]<<","<<last_special[1]<<","<<last_special[2]
                         <<","<<last_special[3]<<","<<last_special[4]<<"] attr ["
                         <<last_attr[0]<<","<<last_attr[1]<<","<<last_attr[2]<<","<<last_attr[3]
                         <<","<<last_attr[4]<<","<<last_attr[5]<<","<<last_attr[6]<<"]"
                         <<" P2 damage "<<stats[1].damage_percent<<" P2 pos "
                         <<stats[1].position[0]<<","<<stats[1].position[1]<<'\n';
            }
            check(ground_created,"Ground source Mario B did not create an original fireball item");
            check(first_fire_life>0.0f,
                "Ground source Mario fireball had no positive original lifetime");
            check(ground_expired,"Ground source Mario fireball was not destroyed after creation");
            check(ground_hit,"Ground source Mario fireball did not damage stationary P2");
            check(settled,"Mario did not settle after the source ground fireball");

            PADStatus jump={0};jump.button=PAD_BUTTON_X;
            step(match,jump,error);
            bool airborne=false;
            for(unsigned tick=0;tick<8&&!airborne;tick++) {
                step(match,neutral,error);
                check(melee_web_match_player_stats(match,0,&stats[0],error,sizeof(error)),error);
                airborne=stats[0].ground_or_air!=0;
            }
            check(airborne,"Mario did not enter the source airborne state");
            PADStatus air_fire={0};air_fire.button=PAD_BUTTON_B;
            step(match,air_fire,error);
            check(melee_web_match_player_stats(match,0,&stats[0],error,sizeof(error)),error);
            check(melee_web_match_player_stats(match,1,&stats[1],error,sizeof(error)),error);
            check((stats[0].held_buttons&PAD_BUTTON_B)!=0&&stats[1].held_buttons==0,
                "Air raw PAD_BUTTON_B did not reach only the selected fighter");
            bool air_created=false;
            for(unsigned tick=0;tick<120&&!air_created;tick++) {
                step(match,neutral,error);
                MeleeWebArticleItemSnapshot items=item_snapshot();
                air_created=items.fireball_count!=0;
            }
            check(air_created,"Air source Mario B did not create an original fireball item");
            for(unsigned tick=0;tick<12;tick++)step(match,neutral,error);

            check(melee_web_match_end(match,error,sizeof(error)),error);
            match=nullptr;
            check(item_snapshot().item_count==0,
                "Original item teardown left an article alive after match end");
            world.close();
            } catch(...) {
                if(match!=nullptr){
                    char cleanup_error[256];
                    if(!melee_web_match_end(match,cleanup_error,sizeof(cleanup_error)))
                        std::cerr<<"Article trace cleanup failed: "<<cleanup_error<<'\n';
                    match=nullptr;
                }
                world.close();
                throw;
            }
        }
        std::cout<<"Original Mario ground and air PAD_BUTTON_B Article paths passed in two worlds\n";
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';return 1;
    }
}
