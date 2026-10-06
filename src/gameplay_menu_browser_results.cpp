// Results exports: exact source-frame PAD and pause schedules.
#include "gameplay_menu_browser_state.hpp"
using namespace melee_web_menu_browser;
extern "C" {
int melee_web_native_menu_results_pad_schedule(unsigned source_frame,unsigned port,
                                                unsigned buttons,unsigned duration){try{
 const int phase=host&&host_entered?melee_web_menu_host_phase(host):-1;
 if(replay||faulted||preparation.busy()||pending||!running||!host_entered||match||
    results||prize||(phase!=MELEE_WEB_MENU_CSS&&phase!=MELEE_WEB_MENU_CSS_READY&&
                     phase!=MELEE_WEB_MENU_SSS&&phase!=MELEE_WEB_MENU_SSS_READY)||
    diagnostic_pad_remaining||diagnostic_start_ticks||stock_check==-1)
  throw std::runtime_error("Results PAD schedule requires an idle original CSS/SSS before Match construction");
 if(port!=0||buttons!=PAD_BUTTON_START||duration<1||duration>120||source_frame>8191)
  throw std::runtime_error("Results PAD schedule is outside the P1 Start source-tick bounds");
 if(scheduled_results_pad.started()||scheduled_results_pad.full())
  throw std::runtime_error("Results PAD schedule is already running or full");
 if(!scheduled_results_pad.enqueue({source_frame,port,buttons,duration}))
  throw std::runtime_error("Results PAD source tick is invalid, duplicated, or unordered");
 message="Results P1 Start scheduled at an exact future source tick.";
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_results_pause_schedule(unsigned source_frame){try{
 const int phase=host&&host_entered?melee_web_menu_host_phase(host):-1;
 if(replay||faulted||preparation.busy()||pending||!running||!host_entered||match||
    results||prize||(phase!=MELEE_WEB_MENU_CSS&&phase!=MELEE_WEB_MENU_CSS_READY&&
                     phase!=MELEE_WEB_MENU_SSS&&phase!=MELEE_WEB_MENU_SSS_READY)||
    diagnostic_pad_remaining||diagnostic_start_ticks||stock_check==-1)
  throw std::runtime_error("Results source-frame pause schedule requires idle original CSS/SSS before Match construction");
 if(source_frame>8191)
  throw std::runtime_error("Results source-frame pause is outside the supported source cursor bounds");
 if(scheduled_results_pauses.started()||scheduled_results_pauses.full())
  throw std::runtime_error("Results source-frame pause schedule is already running or full");
 if(!scheduled_results_pauses.enqueue(source_frame))
  throw std::runtime_error("Results source-frame pause is invalid, duplicated, or unordered");
 message="Results source-frame pause scheduled at an exact future cursor.";
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
}
