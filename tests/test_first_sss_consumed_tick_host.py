"""Exact host/session one-use SSS tick composition; services and owners are fixtures."""
from pathlib import Path
import os,shlex,subprocess,unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function,PRELUDE,STUBS_AND_TESTS
ROOT=Path(__file__).resolve().parents[1]
class FirstSssConsumedTickHostTests(OwnedWorkspaceTests):
 def test_actual_host_session_one_tick_and_refusals(self):
  hs=(ROOT/'src/gameplay_menu_host.c').read_text(); ms=(ROOT/'src/gameplay_menu.c').read_text(); header=(ROOT/'src/gameplay_menu_host.h').read_text()
  pre=PRELUDE
  if '/* SSS_TICK_FIXTURE_BEGIN */' in pre:
   a=pre.index('  /* SSS_TICK_FIXTURE_BEGIN */');b=pre.index('  /* SSS_TICK_FIXTURE_END */',a)+len('  /* SSS_TICK_FIXTURE_END */');pre=pre[:a]+pre[b:]
  for sig in ['static void first_sss_tick_fail(', 'static int first_sss_tick_owner_idle(', 'static int melee_web_menu_arm_first_sss_tick(']:
   if sig in pre:pre=pre.replace(_function(pre,sig),'')
  pre=pre.replace(_function(pre,'static int gm_801A4BA8('),'')
  pre=pre.replace('#define GS_CSS 8', '#define GS_CSS 8\n#define GS_SSS 9\n#define MELEE_WEB_MENU_RESULT_SELECTION_REJECTED 2\n#define MELEE_WEB_MENU_SCENE_CSS 1\n#define MELEE_WEB_MENU_SCENE_SSS 2\n#define gmVsMode_State_Sss 1')
  types=r"""
typedef int MeleeWebMenuScene;
typedef struct {int ckind,slot_type;} Player;
typedef struct {int stkind,match_kind,is_stock,is_vs,is_teams,timer_enabled,xB;} Rules;
typedef struct {Rules rules;Player players[6];} StartMeleeData;
typedef struct {StartMeleeData start;} Vs;
typedef struct {Vs vs;} SSSData;
typedef struct {uint32_t counter_0,counter_4,counter_8;} MeleeWebMenuClockCounters;
"""
  pre=pre.replace('typedef struct { int pending_scene_change; void* ko_counts; } CSSData;',types+'typedef struct { int pending_scene_change; void* ko_counts; Vs vs; } CSSData;')
  pre=pre.replace('typedef struct { CSSData css; int phase,result,request; } MeleeWebMenuSession;', 'typedef struct { CSSData css; SSSData sss; int phase,result,request,first_sss_pair_state,first_sss_tick_token,first_css_return_armed,sss_open,css_open,transition_failed,selection_rejected,transition_requested,training_mode_scene; uint64_t ticks; } MeleeWebMenuSession;')
  pre=pre.replace('typedef struct { int active; } MeleeWebAudio;', 'typedef struct { int active; uint64_t generation; } MeleeWebAudio;')
  a=header.index('typedef struct MeleeWebMenuFirstSssPairNoteSnapshot');b=header.index('int melee_web_menu_host_arm_first_sss_tick(',a)
  pre=pre.replace('typedef struct MeleeWebMenuHost MeleeWebMenuHost;', header[a:b]+'typedef struct MeleeWebMenuHost MeleeWebMenuHost;')
  fields=hs[hs.index('    int first_sss_tick_state;'):hs.index('    char first_sss_tick_error[160];')+len('    char first_sss_tick_error[160];')]
  pre=pre.replace('  int first_sss_pair_state;',fields+'\n  MeleeWebMenuFirstSssPairNoteSnapshot first_sss_pair_entry,first_sss_pair_returned;\n  MeleeWebAudio* first_sss_pair_audio_owner; uint64_t first_sss_pair_audio_generation,first_sss_pair_world_generation;\n  int first_sss_pair_state;')
  stubs=STUBS_AND_TESTS[:STUBS_AND_TESTS.index('static void setup(')]
  old=_function(stubs,'static int melee_web_menu_tick(');stubs=stubs.replace(old,'')
  extra=r"""
static uint32_t frame;static int clock_menu=1,callbacks,steps,foreign_at_scheduler;static uint32_t foreign_scheduler_seed;
static const SSSData* melee_web_menu_sss(MeleeWebMenuSession*s){return &s->sss;}
static int gm_GetPreviousGameMode(void){return 1;}
static int gm_GetCurrentSceneIndex(void){return 1;}
static int gm_GetPreviousSceneIndex(void){return 0;}
static int melee_web_vs_mode_pending_mode(void){return -1;}
static int melee_web_vs_mode_next_state(void){return -1;}
static uint64_t melee_web_audio_generation(MeleeWebAudio*a){return a->generation;}
static uint32_t gm_801A4BA8(void){return frame;}
static int melee_web_menu_clock_capture_counters(MeleeWebMenuClockCounters*c){*c=(MeleeWebMenuClockCounters){frame,0,0};return clock_menu&&!HSD_GObj_804D781C;}
static int melee_web_menu_clock_tick(void){++frame;return 1;}
static void melee_web_pad_state_capture(uint8_t*p){memset(p,0xa5,822);}
static int session_live(MeleeWebMenuSession*s,char*e,size_t n){(void)e;(void)n;return active_host&&s==active_host->session;}
static int check_runtime(MeleeWebMenuSession*s,MeleeWebMenuScene c,char*e,size_t n){(void)s;(void)c;(void)e;(void)n;return 1;}
static void mnCharSel_Scene_OnFrame(void){assert(0);}
static void mnStageSel_Scene_OnFrame(void){++callbacks;}
static int observe_transition(MeleeWebMenuSession*s,MeleeWebMenuScene c,int*r,char*e,size_t n){(void)s;(void)c;(void)e;(void)n;*r=0;return 1;}
static int css_progress_valid(const CSSData*c){(void)c;return 1;}
static int training_sss_selection_valid_internal(const SSSData*s,int i){(void)s;(void)i;return 1;}
static int sss_selection_valid_for_session(MeleeWebMenuSession*s){(void)s;return 1;}
static int run_scheduler(MeleeWebMenuSession*,char*,size_t);
"""
  bodies='\n'.join(_function(hs,x) for x in ['static int fail(', 'static int ok(', 'static int live(', 'static int final_pending_css_pad_equal(', 'static void final_pending_css_pad_copy(', 'static void first_sss_pair_fail(', 'static void final_pending_css_draw_invalidate(', 'static void first_sss_tick_fail(', 'static int first_sss_tick_owner_idle('])
  bodies+='\n'+_function(ms,'int melee_web_menu_arm_first_sss_tick(')+'\n'+_function(hs,'int melee_web_menu_host_arm_first_sss_tick(')+'\n'+_function(hs,'static int first_sss_tick_capture_scheduler_end(')+'\n'+_function(hs,'static int first_sss_tick_scheduler_post(')
  bodies+=r"""
static int run_scheduler(MeleeWebMenuSession*s,char*e,size_t n){(void)s;++steps;if(foreign_at_scheduler)seed_ptr=&foreign_scheduler_seed;int c=first_sss_tick_capture_scheduler_end(active_host);return first_sss_tick_scheduler_post(active_host,c,e,n);}
"""
  bodies+='\n'+_function(ms,'int melee_web_menu_tick(')+'\n'+_function(hs,'int melee_web_menu_host_tick(')+'\n'+_function(hs,'int melee_web_menu_host_draw(')
  # Drawing is forbidden; its unreachable renderer is a refusal fixture.
  pre+='\nstatic int host_draw_render(MeleeWebMenuHost*,char*,size_t);\n'
  main=r"""
static int host_draw_render(MeleeWebMenuHost*h,char*e,size_t n){(void)h;return fail(e,n,"unreachable draw");}
static MeleeWebMenuHost h;static MeleeWebMenuSession s;static MeleeWebAudio audio;static MeleeWebSaveProfileOwner profile;
static PADStatus raw[4];
static void setup(void){memset(&h,0,sizeof h);memset(&s,0,sizeof s);memset(raw,0,sizeof raw);audio=(MeleeWebAudio){1,4};h.session=&s;h.profile=&profile;h.audio=h.first_sss_pair_audio_owner=&audio;h.audio_generation=h.first_sss_pair_audio_generation=4;h.generation=h.first_sss_pair_world_generation=7;h.seed=123;h.entered=1;h.source_scene=2;h.source_mode_kind=GM_VS;h.vs_mode_owned=1;h.first_sss_pair_state=6;s.phase=3;s.sss_open=1;s.first_sss_pair_state=4;h.source_scene_info=(GameSceneInfo){9,&s.sss,&s.sss};owner=active_host=&h;seed_ptr=&h.seed;active_scene_info=&h.source_scene_info;HSD_PadLibData=(PadLibData){&h.queue,0,0,0};stats.generation=7;frame=callbacks=steps=foreign_at_scheduler=0;clock_menu=1;HSD_GObj_804D781C=NULL;h.first_sss_pair_entry.captured=h.first_sss_pair_returned.captured=1;for(int i=0;i<8;i++)h.first_sss_pair_entry.owners[i]=h.first_sss_pair_returned.owners[i]=1;int route[4]={7,1,1,0};memcpy(h.first_sss_pair_returned.scene_routing_getters,route,sizeof route);}
int main(void){char error[160];setup();assert(!melee_web_menu_tick(&s,error,sizeof error));assert(!callbacks&&!steps&&s.first_sss_pair_state==4);setup();assert(melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));raw[0].abi_padding[0]=9;assert(melee_web_menu_host_tick(&h,raw,error,sizeof error)==1);assert(callbacks==1&&steps==1&&s.ticks==1&&frame==1&&s.first_sss_tick_token==2);assert(h.first_sss_tick_state==3&&h.first_sss_tick_scheduler_end.captured&&h.first_sss_tick_scheduler_end.scene_frame==0&&h.first_sss_tick_post_host_frame==1);MeleeWebMenuFirstSssTickSnapshot saved=h.first_sss_tick_scheduler_end;assert(!melee_web_menu_host_tick(&h,raw,error,sizeof error));assert(h.first_sss_tick_host_tick_calls==2&&h.first_sss_pair_state==6&&!h.first_sss_pair_host_tick_calls&&!h.first_sss_pair_host_draw_calls);assert(!memcmp(&saved,&h.first_sss_tick_scheduler_end,sizeof saved));assert(!melee_web_menu_host_draw(&h,error,sizeof error));assert(h.first_sss_tick_host_draw_calls==1&&h.first_sss_pair_state==6);for(int i=0;i<6;i++){setup();assert(melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));uint32_t foreign=3;switch(i){case 0:clock_menu=0;break;case 1:seed_ptr=&foreign;break;case 2:audio.generation++;break;case 3:stats.generation++;break;case 4:raw[0].button=1;break;case 5:HSD_GObj_804D781C=&h;break;}HSD_PadData before=h.queue;assert(!melee_web_menu_host_tick(&h,raw,error,sizeof error));assert(!callbacks&&!steps&&!memcmp(&before,&h.queue,sizeof before)&&h.first_sss_tick_state==4&&h.first_sss_pair_state==6);assert(!melee_web_menu_host_tick(&h,raw,error,sizeof error));assert(!callbacks&&!steps);}setup();assert(melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));assert(!melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));assert(!melee_web_menu_host_tick(&h,raw,error,sizeof error)&&!callbacks);setup();clock_menu=0;assert(!melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error)&&h.first_sss_tick_state==4);clock_menu=1;assert(!melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));assert(!melee_web_menu_host_tick(&h,raw,error,sizeof error)&&!callbacks);setup();assert(melee_web_menu_host_arm_first_sss_tick(&h,raw,error,sizeof error));foreign_at_scheduler=1;assert(melee_web_menu_host_tick(&h,raw,error,sizeof error)==1);assert(h.first_sss_tick_state==4&&h.first_sss_tick_scheduler_end.captured&&!h.first_sss_tick_scheduler_end.owners[7]&&h.first_sss_tick_clock_post_succeeded&&h.first_sss_tick_post_host_frame_captured&&h.first_sss_tick_post_host_frame==1&&s.ticks==1);saved=h.first_sss_tick_scheduler_end;assert(!first_sss_tick_capture_scheduler_end(&h));assert(!memcmp(&saved,&h.first_sss_tick_scheduler_end,sizeof saved));puts("PASS exact composed host/session: one tick, scheduler-before-clock, semantic PAD, six prewrite refusals, duplicate tick/draw retention");return 0;}
"""
  work=self.new_workspace(ROOT,'first-sss-tick-host-');c=work/'control.c';binary=work/'control';c.write_text(pre+stubs+extra+bodies+main)
  command=shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',str(c),'-o',str(binary)]
  result=subprocess.run(command,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(work/'compile.log').write_text(result.stdout);self.assertEqual(result.returncode,0,result.stdout)
  result=subprocess.run([str(binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(work/'run.log').write_text(result.stdout);self.assertEqual(result.returncode,0,result.stdout);self.assertIn('PASS exact composed host/session',result.stdout)
if __name__=='__main__':unittest.main()
