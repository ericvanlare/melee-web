"""Actual host/session finite prefix composition; services and owner IDs are fixtures."""
import os
from pathlib import Path
import shlex
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
from test_first_sss_consumed_tick_host import build_first_sss_tick_control
ROOT=Path(__file__).resolve().parents[1]

class FirstSssPrefixHostTests(OwnedWorkspaceTests):
 def test_actual_finite_permits_draw_seams_and_refusals(self):
  hs=(ROOT/'src/gameplay_menu_host.c').read_text();ms=(ROOT/'src/gameplay_menu.c').read_text()
  control=build_first_sss_tick_control(ROOT)
  control=control.replace(_function(control,'int main(void)'), '')
  control=control.replace('unsigned first_sss_prefix_remaining; uint64_t ticks;', 'unsigned first_sss_prefix_remaining; void (*first_sss_prefix_exit_note)(void*,struct MeleeWebMenuSession*,const SSSData*,uint64_t,int); uint64_t ticks;')
  # The old fixture uses an anonymous session typedef, so avoid a separate tag.
  # Keep the typed callback prototype; the session now has its actual tag.
  control=control.replace('typedef int MeleeWebMenuScene;', '#define MELEE_WEB_MENU_SSS_PREFIX_EXIT 3\ntypedef void (*MeleeWebMenuFirstSssPairNote)();\ntypedef int MeleeWebMenuScene;')
  old=_function(control,'static int observe_transition(')
  control=control.replace(old,old.replace('*r=0;', '*r=callbacks==125?1:0;active_host->transition=*r;'))
  old=_function(control,'static int run_scheduler(MeleeWebMenuSession*s,')
  control=control.replace(old,old.replace('(void)s;++steps;', '(void)s;++steps;if(active_host->first_sss_sequence_operation==2){sss_sequence_capture(active_host,1,MELEE_WEB_SSS_SAMPLE_TICK,&active_host->first_sss_prefix.scheduler_end);++active_host->first_sss_prefix.consumed_inputs;return melee_web_menu_clock_tick();}'))
  control=control.replace('typedef struct { CSSData css; SSSData sss;', 'typedef struct MeleeWebMenuSession { CSSData css; SSSData sss;')
  control=control.replace('typedef struct {Vs vs;} SSSData;', 'typedef struct {Vs vs;int start_game;} SSSData;')
  control=control.replace('unsigned first_sss_prefix_remaining;', 'int post_vs_mode_valid,stadium_c1a_enabled,stadium_c1a_ready;Vs match_vs,post_vs_mode;struct {void* user;int (*scene_exit)(void*,int,char*,size_t);}runtime;unsigned first_sss_prefix_remaining;')
  # Forward declarations keep actual production bodies and original source order.
  marker='static int run_scheduler(MeleeWebMenuSession*s,'
  proto='static int sss_sequence_capture(MeleeWebMenuHost*,int,int,MeleeWebMenuFirstSssTickSnapshot*);\n'
  control=control.replace(marker,proto+marker,1)
  old=_function(control,'static int host_draw_render(MeleeWebMenuHost*h,')
  bodies='\n'.join(_function(ms,x) for x in ['int melee_web_menu_arm_first_sss_prefix(', 'int melee_web_menu_authorize_first_sss_prefix_tick('])
  bodies+='\n'+ '\n'.join(_function(hs,x) for x in ['static int sss_sequence_owner(', 'static int sss_sequence_capture(', 'static void first_sss_prefix_exit_note(', 'int melee_web_menu_host_arm_first_sss_draw(', 'int melee_web_menu_host_arm_first_sss_prefix(', 'static int host_draw_render(', 'int melee_web_menu_host_draw_first_sss(', 'int melee_web_menu_host_tick_first_sss_prefix(', 'int melee_web_menu_host_draw_first_sss_prefix('])
  # Execute the actual public-entry guard prefix without needing unrelated
  # host preference/heap services. The success tail is a retirement counter,
  # explicitly a fixture rather than a claim about full ordinary leave.
  leave=_function(hs,'int melee_web_menu_host_leave(')
  guard=leave[:leave.index('    if (abort_scene)\n')]
  guard=guard.replace('int melee_web_menu_host_leave(', 'static int leave_guard(')
  destroy=_function(hs,'int melee_web_menu_host_destroy(')
  destroy=destroy[:destroy.index('    final_pending_css_draw_invalidate(h);')]
  destroy=destroy.replace('int melee_web_menu_host_destroy(', 'static int destroy_guard(')
  bodies+='\nstatic int retirement_attempts;\n'+guard+'++retirement_attempts;return 1;}\n'
  bodies+='\n'+destroy+'#endif\n++retirement_attempts;return 1;}\n'
  bodies+=r"""
#define MELEE_WEB_MENU_CLOSED 0
#define MELEE_WEB_MENU_FD_ST_KIND 32
#define MELEE_WEB_MENU_READY 4
#define MELEE_WEB_MENU_CSS_READY 2
#define St_Kind_PStadium 3
static int exit_events;
static void mnStageSel_Scene_OnExit(void*p){(void)p;assert(exit_events==0);exit_events=1;active_host->session->sss.start_game=1;}
static int melee_web_menu_gobj_teardown(MeleeWebMenuSession*s,char*e,size_t n){(void)s;(void)e;(void)n;assert(exit_events==2);exit_events=3;return 1;}
static int melee_web_menu_stage_available(int k){(void)k;return 1;}
static int match_selection_valid(StartMeleeData*s){(void)s;return 1;}
static int melee_web_vs_prepare_start_source(StartMeleeData*s,Vs*vs,Vs*out){assert(exit_events==3);exit_events=4;*s=vs->start;*out=*vs;return 1;}
static int mode_exit_fixture(void*user,int scene,char*e,size_t n){(void)e;(void)n;MeleeWebMenuHost*h=user;assert(scene==2&&exit_events==1&&h->first_sss_prefix.exit_captured);exit_events=2;h->first_sss_prefix.selected_stage_captured=1;h->first_sss_prefix.selected_stage_index=18;h->first_sss_prefix.selected_stage_kind=3;return 1;}
"""
  bodies+='\n'+_function(ms,'int melee_web_menu_leave_sss(')
  bodies+='\nstatic int melee_web_menu_host_leave(MeleeWebMenuHost*h,int abort_scene,char*e,size_t n){assert(!abort_scene);exit_events=0;h->session->runtime.user=h;h->session->runtime.scene_exit=mode_exit_fixture;const int result=melee_web_menu_leave_sss(h->session,e,n);assert(exit_events==4);h->entered=0;h->source_scene=0;h->transition=0;return result;}\n'

  bodies+='\n'+_function(hs,'int melee_web_menu_host_leave_first_sss_prefix(')
  bodies='static int melee_web_sss_selected_stage(int*i,int*k){*i=18;*k=3;return 1;}\n'+bodies
  control=control.replace(old,bodies)
  # setup() remains the original ownership fixture, not a replacement for the
  # production predicates or a claim about actual acquired world/audio IDs.
  control+=r'''
static int reject_phase, approvals[6],reenter;
static int compare(void* data,int phase,const MeleeWebMenuFirstSssTickSnapshot* sample,const MeleeWebMenuFirstSssPairNoteSnapshot* exit_note){
 (void)data;char e[160];HSD_PadData before=h.queue;int old_callbacks=callbacks;
 if(reenter==1){PADStatus foreign[4]={{0}};foreign[0].button=123;assert(!melee_web_menu_host_tick(&h,foreign,e,sizeof e));assert(!memcmp(&before,&h.queue,sizeof before)&&callbacks==old_callbacks);}
 if(reenter==2)assert(!leave_guard(&h,1,e,sizeof e));
 if(reenter==3)assert(!destroy_guard(&h,e,sizeof e));
 if(reenter==4)assert(!melee_web_menu_host_leave_first_sss_prefix(&h,e,sizeof e));
 if(phase==4){assert(exit_note&&exit_note->captured&&exit_note->phase==3&&exit_note->session_ticks==125);++approvals[phase];return 1;}
 if(phase==5){assert(h.first_sss_prefix.selected_stage_captured);++approvals[phase];return 1;}
 assert(sample&&sample->captured);assert(sample->scene_kind==9);for(int i=0;i<8;i++)assert(sample->owners[i]);++approvals[phase];return phase!=reject_phase;
}
static void ready(void){char e[160];setup();reject_phase=reenter=retirement_attempts=0;memset(approvals,0,sizeof approvals);assert(melee_web_menu_host_arm_first_sss_tick(&h,raw,e,sizeof e));assert(melee_web_menu_host_tick(&h,raw,e,sizeof e)==1);assert(h.first_sss_tick_state==3);}
int main(void){char e[160];ready();MeleeWebMenuFirstSssTickSnapshot original=h.first_sss_tick_scheduler_end;
 assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));assert(melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(approvals[2]==1&&approvals[3]==1&&h.first_sss_draw.state==2);
 assert(melee_web_menu_host_arm_first_sss_prefix(&h,compare,NULL,e,sizeof e));
 for(unsigned i=0;i<124;i++){assert(melee_web_menu_host_tick_first_sss_prefix(&h,raw,i,e,sizeof e)==(i==123?3:1));assert(h.first_sss_prefix.scheduler_end.scene_frame==i+1);assert(melee_web_menu_host_draw_first_sss_prefix(&h,i,e,sizeof e));assert(h.first_sss_prefix.draw_enter.scene_frame==i+2&&h.first_sss_prefix.draw_return.scene_frame==i+2);}
 assert(callbacks==125&&steps==125&&s.ticks==125&&frame==125&&h.transition==1);assert(s.first_sss_prefix_remaining==0&&s.first_sss_prefix_token==2);assert(h.first_sss_prefix.input_index==124&&h.first_sss_prefix.consumed_inputs==124&&h.first_sss_prefix.matched_ticks==124&&h.first_sss_prefix.matched_draw_returns==124);assert(!memcmp(&original,&h.first_sss_tick_scheduler_end,sizeof original)&&h.first_sss_tick_host_tick_calls==1&&!h.first_sss_tick_host_draw_calls&&h.first_sss_pair_state==6&&!h.first_sss_pair_host_tick_calls&&!h.first_sss_pair_host_draw_calls);
 // Actual public guard refuses ordinary leave before source teardown and
 // preserves successful old pair/tick snapshots. Abort is allowed only idle.
 int old_pair=h.first_sss_pair_state;assert(!leave_guard(&h,0,e,sizeof e));assert(!retirement_attempts&&h.first_sss_pair_state==old_pair&&!memcmp(&original,&h.first_sss_tick_scheduler_end,sizeof original));
 // Reset only this fixture's failure latch to exercise the distinct exit API.
 h.first_sss_prefix.state=1;h.first_sss_prefix.error[0]=0;
 assert(melee_web_menu_host_leave_first_sss_prefix(&h,e,sizeof e));assert(h.first_sss_prefix.state==2&&approvals[4]==1&&approvals[5]==1&&!h.entered&&!h.source_scene&&h.first_sss_prefix.exit_note.host_entered&&h.first_sss_prefix.exit_note.session_phase==3&&!memcmp(&original,&h.first_sss_tick_scheduler_end,sizeof original));
 MeleeWebMenuFirstSssTickSnapshot final=h.first_sss_prefix.draw_return;assert(!melee_web_menu_host_tick_first_sss_prefix(&h,raw,124,e,sizeof e));assert(callbacks==125&&!memcmp(&final,&h.first_sss_prefix.draw_return,sizeof final));
 ready();assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));reject_phase=2;int before=gobj_draws;assert(!melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(gobj_draws==before&&h.first_sss_draw.draw_enter.captured&&!h.first_sss_draw.draw_return.captured&&!h.drawing);assert(h.first_sss_tick_state==3&&h.first_sss_pair_state==6);
 ready();assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));reject_phase=3;assert(!melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(h.first_sss_draw.draw_return.captured&&!h.drawing);
 ready();assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));assert(melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(melee_web_menu_host_arm_first_sss_prefix(&h,compare,NULL,e,sizeof e));audio.generation++;before=callbacks;HSD_PadData saved=h.queue;assert(!melee_web_menu_host_tick_first_sss_prefix(&h,raw,0,e,sizeof e));assert(callbacks==before&&!memcmp(&saved,&h.queue,sizeof saved)&&h.first_sss_tick_state==3&&h.first_sss_pair_state==6);
 ready();assert(!melee_web_menu_host_arm_first_sss_prefix(&h,compare,NULL,e,sizeof e));assert(!callbacks||callbacks==1);assert(h.first_sss_tick_state==3);
 for(int action=1;action<=3;++action){ready();assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));assert(melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(melee_web_menu_host_arm_first_sss_prefix(&h,compare,NULL,e,sizeof e));reenter=action;before=callbacks;assert(!melee_web_menu_host_tick_first_sss_prefix(&h,raw,0,e,sizeof e));assert(callbacks==before+1&&h.first_sss_prefix.state==3&&h.first_sss_prefix.scheduler_end.captured&&!h.first_sss_prefix.matched_ticks&&!retirement_attempts&&h.first_sss_pair_state==6&&h.first_sss_tick_state==3);}
 ready();assert(melee_web_menu_host_arm_first_sss_draw(&h,compare,NULL,e,sizeof e));assert(melee_web_menu_host_draw_first_sss(&h,e,sizeof e));assert(melee_web_menu_host_arm_first_sss_prefix(&h,compare,NULL,e,sizeof e));for(unsigned i=0;i<124;++i){assert(melee_web_menu_host_tick_first_sss_prefix(&h,raw,i,e,sizeof e));assert(melee_web_menu_host_draw_first_sss_prefix(&h,i,e,sizeof e));}reenter=4;assert(!melee_web_menu_host_leave_first_sss_prefix(&h,e,sizeof e));assert(h.first_sss_prefix.state==3&&h.first_sss_prefix.exit_captured&&h.first_sss_prefix.selected_stage_captured&&approvals[4]==1&&!approvals[5]&&!h.first_sss_sequence_operation&&!retirement_attempts);
 puts("PASS actual host/session finite124: first-draw gate, scheduler-before-clock, final pending draw, preserved old snapshots and refused extra/foreign/mismatch work");return 0;}
'''
  work=self.new_workspace(ROOT,'sss-prefix-host-');c=work/'control.c';binary=work/'control';c.write_text(control)
  argv=shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',str(c),'-o',str(binary)]
  result=subprocess.run(argv,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(work/'compile.log').write_text(result.stdout);self.assertEqual(result.returncode,0,result.stdout)
  result=subprocess.run([str(binary)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(work/'run.log').write_text(result.stdout);self.assertEqual(result.returncode,0,result.stdout);self.assertIn('PASS actual host/session finite124',result.stdout)
if __name__=='__main__':unittest.main()
