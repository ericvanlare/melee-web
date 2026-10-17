"""Actual proposed function extraction; endpoint doubles, not native owner proof.
Endpoint mechanism controls only. No browser/game service success is established here.
"""
from pathlib import Path
import os,subprocess,unittest,json,shutil,time
from owned_test_workspace import OwnedWorkspaceTests
ROOT=Path(__file__).resolve().parents[1]

def body(path,start,end):
 s=(ROOT/path).read_text();assert s.count(start)==s.count(end)==1
 return s[s.index(start):s.index(end)]

def run(source,workspace,language="c++"):
 p=Path(workspace)
 (p/'fixture.cpp').write_text(source)
 def command(name,argv,limit):
  with (p/(name+'.stdout')).open('wb') as out,(p/(name+'.stderr')).open('wb') as err:
   proc=subprocess.Popen(argv,stdout=out,stderr=err)
   (p/(name+'.command.json')).write_text(json.dumps({'argv':argv,'pid':proc.pid,'pgid':os.getpgid(proc.pid),'timeout_seconds':limit,'started_ns':time.monotonic_ns()},indent=2)+'\n')
   try:code=proc.wait(timeout=limit)
   finally:
    if proc.poll() is None:
     proc.kill();proc.wait(timeout=2)
   if code:
    print((p/(name+'.stderr')).read_text(),flush=True)
    raise subprocess.CalledProcessError(code,argv)
 try:
  command('compile',[shutil.which('clang++') or 'clang++','-x',language,'-std='+('c11' if language=='c' else 'c++20'),'-Wall','-Wextra','-Werror',str(p/'fixture.cpp'),'-o',str(p/'fixture')],60)
  command('run',[str(p/'fixture')],20)
 except BaseException:
  print('Retained failed endpoint-mechanism fixture: '+str(p),flush=True)
  raise
 else:pass # Existing OwnedWorkspaceTests removes successful owned scratch.

class CurrentSssExitActualFunctions(OwnedWorkspaceTests):
 def test_actual_note_refuses_audio_and_mode_owner_mutations(self):
  fn=body('src/gameplay_menu_host.c','static void current_sss_exit_note(', 'int melee_web_menu_host_arm_current_sss_exit(')
  fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum {MELEE_WEB_MENU_SSS_PREFIX_EXIT=3,MELEE_WEB_HOST_SCENE_SSS=2,MELEE_WEB_MENU_SSS=3,GS_SSS=9,GM_VS=2,gmVsMode_State_Sss=1,St_Kind_PStadium=3};
typedef struct MeleeWebMenuSession {int unused;} MeleeWebMenuSession;
typedef struct SSSData {int start_game,force_stage_id;struct {struct {struct {int stkind;}rules;}start;}vs;} SSSData;
typedef struct MeleeWebMenuFirstSssPairNoteSnapshot {int phase,host_entered,session_phase,source_scene,scene_kind;unsigned scene_frame,random_seed;uint64_t session_ticks,world_generation,audio_generation;SSSData sss;unsigned char pad_state[4];int scene_routing_getters[4],owners[8],captured;} MeleeWebMenuFirstSssPairNoteSnapshot;
typedef struct MeleeWebMenuCurrentSssExitObservation{int state;char error[160];MeleeWebMenuFirstSssPairNoteSnapshot note;} MeleeWebMenuCurrentSssExitObservation;
typedef struct MeleeWebMenuHost{MeleeWebMenuCurrentSssExitObservation current_sss_exit;const void* current_sss_exit_context;void(*current_sss_exit_notify)(void*,const MeleeWebMenuCurrentSssExitObservation*);void* current_sss_exit_notify_data;MeleeWebMenuSession* session;int entered,drawing,vs_mode_owned,source_scene,source_mode_kind;unsigned seed;void* audio;uint64_t generation,audio_generation;struct {int scene_kind;const void* enter_data;const void* exit_data;}source_scene_info;} MeleeWebMenuHost;
MeleeWebMenuHost* owner;unsigned* seed_ptr;SSSData payload;int mode,scene;uint64_t audio_gen;int service_live=1,notifications;
int live(MeleeWebMenuHost* h,char* e,size_t n){(void)h;(void)e;(void)n;return service_live;}
const SSSData* melee_web_menu_sss(MeleeWebMenuSession* s){(void)s;return &payload;}
int melee_web_menu_phase(MeleeWebMenuSession* s){(void)s;return 3;}
uint64_t melee_web_audio_generation(void* a){(void)a;return audio_gen;}
int gm_GetCurrentGameMode(void){return mode;}int gm_GetPreviousGameMode(void){return 0;}int gm_GetCurrentSceneIndex(void){return scene;}int gm_GetPreviousSceneIndex(void){return 0;}
const void* melee_web_current_scene_info(void){return &owner->source_scene_info;}
unsigned gm_801A4BA8(void){return 123;}
void melee_web_pad_state_capture(unsigned char* b){b[0]=19;}
void notify(void* d,const MeleeWebMenuCurrentSssExitObservation* o){(void)d;(void)o;++notifications;}
'''
  main=r'''
void reset(MeleeWebMenuHost* h){memset(&h->current_sss_exit,0,sizeof(h->current_sss_exit));h->current_sss_exit.state=1;notifications=0;audio_gen=7;h->audio_generation=7;h->source_mode_kind=2;mode=2;scene=1;}
int main(void){MeleeWebMenuSession session={0};MeleeWebMenuHost h={0};owner=&h;h.session=&session;h.entered=1;h.vs_mode_owned=1;h.source_scene=2;h.source_scene_info.scene_kind=9;h.generation=3;h.seed=42;payload.start_game=1;payload.force_stage_id=-1;payload.vs.start.rules.stkind=3;h.current_sss_exit_context=&session;h.current_sss_exit_notify=notify;h.current_sss_exit_notify_data=&h;seed_ptr=&h.seed;h.source_scene_info.enter_data=h.source_scene_info.exit_data=&payload;
 reset(&h);current_sss_exit_note(&h,&session,&payload,456,3);assert(notifications==1&&h.current_sss_exit.state==2&&h.current_sss_exit.note.captured&&h.current_sss_exit.note.audio_generation==7);for(unsigned i=0;i<8;++i)assert(h.current_sss_exit.note.owners[i]==1);
 for(int fault=0;fault<5;++fault){reset(&h);if(fault==0)audio_gen=8;if(fault==1)h.audio_generation=0;if(fault==2)h.source_mode_kind=0;if(fault==3)mode=0;if(fault==4)scene=0;current_sss_exit_note(&h,&session,&payload,456,3);assert(notifications==1&&h.current_sss_exit.state==3&&!h.current_sss_exit.note.captured&&h.current_sss_exit.error[0]);}
}
'''
  run(fixture+fn+main,self.new_workspace(ROOT,'current-sss-note-'),language='c')
 def test_actual_notify_retains_current_coordinates_without_claiming_tick_return(self):
  fn=body('src/gameplay_menu_browser.cpp','void current_sss_exit_notify(', 'void current_sss_exit_tick_begin(')
  fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
struct Note{bool captured=true;uint64_t world_generation=6;};struct MeleeWebMenuCurrentSssExitObservation{int state=2;Note note;const char* error="";};
struct Context{};struct Recipe{std::unique_ptr<Context> initial_css{new Context};};struct Host{};
std::unique_ptr<Recipe> replay{new Recipe};Host host_object;Host* host=&host_object;size_t replay_cursor=100;
struct Counter{size_t s=2,d=1;size_t steps(){return s;}size_t draws(){return d;}}replay_source_frames;
struct State {bool armed=true,notified=false,failed=false,notification_received=false,host_tick_in_flight=false,last_host_tick_returned=true,notification_in_host_tick=false,notification_last_tick_returned=false;size_t notification_cursor=0,notification_steps=0,notification_draws=0;uint64_t notification_order=0,order=9;const Recipe* recipe=nullptr;const Context* context=nullptr;Host* host_owner=nullptr;MeleeWebMenuCurrentSssExitObservation observation;std::string error;};State current_sss_exit;
uint64_t melee_web_gameplay_generation(){return 6;}
'''
  main=r'''
int main(){auto& s=current_sss_exit;auto reset=[&]{s=State{};s.recipe=replay.get();s.context=replay->initial_css.get();s.host_owner=host;};MeleeWebMenuCurrentSssExitObservation o;
 reset();current_sss_exit_notify(&s,&o);assert(s.notified&&s.notification_received&&s.notification_cursor==100&&s.notification_steps==2&&s.notification_draws==1&&s.notification_order==9&&!s.notification_in_host_tick&&s.notification_last_tick_returned);
 replay_cursor=101;replay_source_frames.s=0;replay_source_frames.d=0;current_sss_exit_notify(&s,&o);assert(s.failed&&s.notification_cursor==100&&s.notification_steps==2&&s.notification_order==9);
 reset();s.host_tick_in_flight=true;s.last_host_tick_returned=false;current_sss_exit_notify(&s,&o);assert(s.notification_in_host_tick&&!s.notification_last_tick_returned);
 reset();o.state=3;o.note.captured=false;o.error="actual owner refusal";current_sss_exit_notify(&s,&o);assert(s.notified&&s.failed&&s.observation.state==3&&s.error=="actual owner refusal"&&s.notification_received);
 reset();o.state=2;o.note.captured=true;o.note.world_generation=5;current_sss_exit_notify(&s,&o);assert(s.failed&&!s.notified&&s.notification_received);
}
'''
  run(fixture+fn+main,self.new_workspace(ROOT,'current-sss-notify-'))

if __name__=='__main__':unittest.main()
