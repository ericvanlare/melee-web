// Match exports: reference replay, source player state and the stock check.
#include "gameplay_menu_browser_state.hpp"
using namespace melee_web_menu_browser;
extern "C" {
int melee_web_native_menu_replay(const uint8_t* data,unsigned size,int observe){try{
 replay_source_frames=melee_web::SourceFrameSequence{};
 check(data&&size<=melee_web::kRetailReplayMaxBytes,"Invalid reference replay bytes");
 check(observe==0||observe==1,"Invalid replay observation mode");
 auto candidate=std::make_unique<melee_web::RetailReplayRecipe>(melee_web::read_retail_replay({data,size}));
 check(candidate->version!=6||observe==1,"Recorded input-queue replay requires state-capture mode; live timing is not admitted");
 check(candidate->version>=2&&candidate->initial_input,"Browser reference playback requires a PAD history recipe (v2 or v3)");
 // Both replay forms start in a fresh application. A whole-session recipe
 // retains the canonical unentered CSS preparation and its scoped assets;
 // close() would release them before launch can enter the original scene.
 check(!reference_heap_used,"Reference replay requires a fresh application. Use Reload application state, import the disc, then play the recipe before entering menus.");
 if(candidate->whole_session())
  check(world&&host&&!host_entered&&!world_exposed&&!match&&!results&&!prize&&
        melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED,
        "Whole-session replay requires the fresh prepared character-select owner");
 reference_heap_used=true;
 if(!candidate->whole_session()){
  close();
  // Modern disc import releases its active CSS assets at unload. Reuse the
  // existing bounded Replay scope rather than the eager-file legacy path.
  scoped_assets=scoped_disc_import;
 }
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 replay=std::move(candidate);replay_trace=observe;replay_pending=!replay->whole_session();
 replay_completion={};replay_completion.whole_session=replay->whole_session();
 match_message="Reference replay: "+selected_match_message(replay->selection);
 if(replay->whole_session()){
  // A whole-session timeline starts at CSS, so its entry is the ordinary menu
  // launch the caller already uses: the caller prepares the menu resources and
  // enters the world, and the timeline then drives CSS, SSS, the match,
  // Results and the return through one cursor.
  message="Whole-session reference replay ready; launch to enter character select.";
  return 1;
 }
 check(preparation.request(),"Replay preparation is already active");
 preparation_profile.begin(true,emscripten_get_now());
 VISetFrameBufferScale(1);
 message="Preparing reference replay...";
 EM_ASM({window.menuPreparation?.(UTF8ToString($0));},message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();running=false;return 0;}}
unsigned melee_web_native_menu_replay_cursor(){return static_cast<unsigned>(replay_cursor);}
int melee_web_native_menu_replay_whole_session(){return replay&&replay->whole_session()?1:0;}
int melee_web_native_menu_player_state(unsigned player,int* fighter_kind,int* motion_id,
                                       int* ground_or_air,unsigned* source_frame,
                                       float* position_x,float* position_y){try{
 if(!fighter_kind||!motion_id||!ground_or_air||!source_frame||!position_x||!position_y||
    player>=4||!match||!match->ready())
  throw std::runtime_error("Player state requires a ready source match and valid output storage");
 const auto stats=match->player_stats(player);*fighter_kind=stats.fighter_kind;
 *motion_id=stats.motion_id;*ground_or_air=stats.ground_or_air;*source_frame=match->source_frames();
 *position_x=stats.position[0];*position_y=stats.position[1];return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_stock_check_ready(){
 return !replay&&match&&!faulted&&running&&match->ready()&&!match->paused()&&
        !match->ending()&&stock_check!=-1&&diagnostic_start_ticks==0&&diagnostic_pad_remaining==0;
}
int melee_web_native_menu_stock_check(){
 if(!melee_web_native_menu_stock_check_ready()||
    match->player_stats(0).stocks!=4||match->player_stats(1).stocks!=4)return 0;
 stock_check=-1;stock_count=4;stock_respawns=0;stock_tick=0;stock_lost=stock_jump=false;
 running=true;menu_clock.reset();return 1;
}
}
