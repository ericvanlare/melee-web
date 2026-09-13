#include "gameplay_compat.h"
#include "gameplay_world.hpp"
#include "gameplay_match_context.h"
#include "gameplay_match_rules.h"
#include "gameplay_render.h"
#include "gameplay_edge_state.h"
#include "dat_archive.hpp"
#include <filesystem>
#include <fstream>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <cstring>
using namespace melee_web;
extern "C" int mpColl_804D64AC;
static void check(int ok,const char* why){if(!ok){std::cerr<<why<<'\n';throw DatError(why);}}
static std::vector<uint8_t> bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);check(bool(f),"Open owned edge-test asset");return {std::istreambuf_iterator<char>(f),{}};}
static void dump_state(unsigned tick, const char* phase,
                       const MeleeWebEdgeSnapshot& snap,
                       const MeleeWebMatchStats& stats)
{
    std::cerr<<phase<<" tick"<<tick<<" action"<<stats.motion_id
             <<" ga"<<stats.ground_or_air<<" x"<<snap.x
             <<" prev"<<snap.prev_x<<" dx"<<snap.delta_x
             <<" self"<<snap.self_x<<","<<snap.self_y<<" gr"<<snap.gr_vel
             <<" ecb b"<<snap.ecb_bottom_x<<","<<snap.ecb_bottom_y
             <<" l"<<snap.ecb_left_x<<","<<snap.ecb_left_y
             <<" r"<<snap.ecb_right_x<<","<<snap.ecb_right_y
             <<" t"<<snap.ecb_top_x<<","<<snap.ecb_top_y
             <<" prev_ecb b"<<snap.prev_bottom_x<<","<<snap.prev_bottom_y
             <<" l"<<snap.prev_left_x<<","<<snap.prev_left_y
             <<" r"<<snap.prev_right_x<<","<<snap.prev_right_y
             <<" t"<<snap.prev_top_x<<","<<snap.prev_top_y
             <<" desired_ecb b"<<snap.desired_bottom_x<<","<<snap.desired_bottom_y
             <<" l"<<snap.desired_left_x<<","<<snap.desired_left_y
             <<" r"<<snap.desired_right_x<<","<<snap.desired_right_y
             <<" t"<<snap.desired_top_x<<","<<snap.desired_top_y
             <<" x64_ecb b"<<snap.x64_bottom_x<<","<<snap.x64_bottom_y
             <<" l"<<snap.x64_left_x<<","<<snap.x64_left_y
             <<" r"<<snap.x64_right_x<<","<<snap.x64_right_y
             <<" t"<<snap.x64_top_x<<","<<snap.x64_top_y
             <<" xe4_ecb b"<<snap.xe4_bottom_x<<","<<snap.xe4_bottom_y
             <<" l"<<snap.xe4_left_x<<","<<snap.xe4_left_y
             <<" r"<<snap.xe4_right_x<<","<<snap.xe4_right_y
             <<" t"<<snap.xe4_top_x<<","<<snap.xe4_top_y
             <<" floor"<<snap.floor_index<<" skip"<<snap.floor_skip
             <<" jskip"<<snap.joint_id_skip<<" jonly"<<snap.joint_id_only
             <<" x34"<<std::hex<<snap.x34_flags<<" x35"<<snap.x35_flags
             <<" x130"<<snap.x130_flags<<" lock"<<snap.x130_locked
             <<" clear"<<snap.x130_clear<<std::dec<<" x38"<<snap.x38
             <<" collgen"<<mpColl_804D64AC
             <<" face"<<snap.facing_dir<<" fighter_face"<<snap.fighter_facing_dir
             <<" cooldown"<<snap.ledge_cooldown<<" x2224b2"<<snap.fighter_x2224_b2
             <<" x2219b1"<<snap.fighter_x2219_b1<<" x2223b4"<<snap.fighter_x2223_b4
             <<" x221db7"<<snap.fighter_x221d_b7
             <<" nudge"<<snap.nudge_x<<","<<snap.nudge_y
             <<" ledges"<<snap.ledge_id_left<<","<<snap.ledge_id_right
             <<" ff0x"<<std::hex<<snap.floor_flags<<std::dec
             <<" env"<<snap.env_flags<<" prev_env"<<snap.prev_env_flags
             <<" cpos"<<snap.coll_x<<","<<snap.coll_y
             <<" cprev"<<snap.coll_prev_x<<","<<snap.coll_prev_y
             <<" clast"<<snap.coll_last_x<<","<<snap.coll_last_y
             <<" c28"<<snap.coll_x28_x<<","<<snap.coll_x28_y
             <<" clstick"<<snap.coll_lstick_x<<" cx13c"<<snap.coll_x13c
             <<" contact"<<snap.contact_x<<","<<snap.contact_y
             <<" normal"<<snap.floor_normal_x<<","<<snap.floor_normal_y
             <<" stick"<<snap.stick_x<<","<<snap.stick_y
             <<" held0x"<<std::hex<<snap.held_buttons<<std::dec<<"\n";
    if(snap.floor_index>=0){
        std::cerr<<phase<<" floor_line"<<snap.floor_index<<" v0"<<snap.floor_v0_x<<","<<snap.floor_v0_y
                 <<" v1"<<snap.floor_v1_x<<","<<snap.floor_v1_y<<" next"<<snap.floor_next
                 <<" prev"<<snap.floor_prev<<"\n";
    }
}
int main(int argc,char** argv){try{
 bool phase_only=argc>=3&&std::strcmp(argv[2],"phase")==0;
 check(argc==2||(phase_only&&(argc==3||argc==4)),"Expected local asset directory or 'phase [finite initial_x]' variant");RuntimeFiles files;
 for(const char* name:{"PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","sislib_font.bin"})files[name]=bytes(std::filesystem::path(argv[1])/name);
 char error[256];
 GameplayWorld world(files);
 float initial_x=phase_only?15.4f:-20.0f;
 if(phase_only&&argc==4){char* end=nullptr;errno=0;const float parsed=std::strtof(argv[3],&end);check(end!=argv[3]&&*end=='\0'&&errno!=ERANGE&&std::isfinite(parsed),"Phase initial_x must be a finite number");initial_x=parsed;}
 float initial_facing=phase_only?-1.0f:1.0f;
 MeleeWebPlayerSettings players[2]={{0,0,4,{initial_x,world.floor_height(initial_x)+1,0},initial_facing},{1,1,4,{20,world.floor_height(20)+1,0},-1}};
 auto* match=melee_web_match_begin_players(players,2,70,1,world.collision(),error,sizeof(error));check(match!=nullptr,error);
 check(melee_web_match_create_fighters(match,error,sizeof(error)),error);
 MeleeWebRenderSettings render_settings{640,480,{0,35,190},{0,5,0},45,1,2000,(UINT64_C(1)<<3)|(UINT64_C(1)<<5)};
 auto* camera=melee_web_render_begin_match(&render_settings,error,sizeof(error));check(camera!=nullptr,error);
 world.enable_full_stage();
 bool lost=false,second=false,first_edge=false,first_cross=false;int stock=4,first_post_frames=0,second_frame=0,phase_frames=0,phase_regrabs=0;
 for(unsigned tick=0;tick<1200;tick++){
   PADStatus pads[4]={{0}};
   // First run leaves the platform. Release after the source stock loss, then
   // resume the same raw stick input after the original grounded respawn.
   if(tick>=20&&!lost)pads[0].stickX=80;
   if(second)pads[0].stickX=80;
   check(melee_web_match_step_raw(match,pads,error,sizeof(error)),error);
   MeleeWebMatchStats stats[2];for(unsigned i=0;i<2;i++)check(melee_web_match_player_stats(match,i,&stats[i],error,sizeof(error)),error);
   check(stats[1].stocks==4,"Stationary opponent lost a stock");
   MeleeWebEdgeSnapshot snap;check(melee_web_edge_snapshot(0,&snap),"Source fighter disappeared during edge trace");
   if(phase_only&&tick<45)dump_state(tick,"phase",snap,stats[0]);
   if(!first_edge&&stats[0].stocks==4&&stats[0].position[0]>80){dump_state(tick,"first",snap,stats[0]);first_edge=true;}
   if(!first_cross&&stats[0].stocks==4&&stats[0].position[0]>85.5f){dump_state(tick,"first_cross",snap,stats[0]);first_cross=true;}
   if(phase_only&&first_cross&&!lost&&stats[0].motion_id==14&&stats[0].ground_or_air==0)++phase_regrabs;
   if(first_cross&&!lost&&first_post_frames<8)dump_state(first_post_frames++,"first_post",snap,stats[0]);
   if(stats[0].stocks<stock){lost=true;stock=stats[0].stocks;std::cerr<<"first_loss tick"<<tick<<"\n";}
   if(!second&&lost&&stats[0].stocks==3&&stats[0].motion_id==14&&stats[0].ground_or_air==0){
       second=true;lost=false;second_frame=0;std::cerr<<"second_run_start tick"<<tick<<" x"<<stats[0].position[0]<<"\n";
   }
   if(second){dump_state(second_frame++,"second",snap,stats[0]);if(second_frame>=100)break;}
   if(phase_only&&first_cross){
       ++phase_frames;
       if(phase_frames>=100)break;
   }
  }
 check(first_edge&&first_cross&&(phase_only||second),"Edge trace did not reach first edge and grounded second run");
 if(phase_only){
   check(phase_regrabs==0,"Full-stage edge phase regrabbed the floor after entering Fall");
   check(lost&&stock==3,"Full-stage edge phase did not reach the expected first stock loss");
 }
 world.end_stage();
 check(melee_web_render_end(camera,error,sizeof(error)),error);
 check(melee_web_match_end(match,error,sizeof(error)),error);world.close();
 std::cout<<(phase_only?"Original initial-spawn raw-stick phase trace captured\n":"Original post-respawn raw-stick edge trace captured\n");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
