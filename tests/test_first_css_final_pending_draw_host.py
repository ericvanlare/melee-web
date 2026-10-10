"""Compile the exact result-3 host functions against a small ownership fixture.

This is an asset-free control for the diagnostic host authorization.  It
extracts the production C function bodies rather than copying their logic.
"""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import subprocess
import unittest
from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
HOST_SOURCE = ROOT / "src" / "gameplay_menu_host.c"


def _function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"missing production function: {signature}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for production function: {signature}")

    depth = 0
    state = "code"
    i = opening
    while i < len(source):
        char = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "code":
            if char == '"':
                state = "string"
            elif char == "'":
                state = "char"
            elif char == "/" and nxt == "*":
                state = "comment"
                i += 1
            elif char == "/" and nxt == "/":
                state = "line_comment"
                i += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    return source[start : i + 1]
        elif state == "string":
            if char == "\\":
                i += 1
            elif char == '"':
                state = "code"
        elif state == "char":
            if char == "\\":
                i += 1
            elif char == "'":
                state = "code"
        elif state == "comment":
            if char == "*" and nxt == "/":
                state = "code"
                i += 1
        elif state == "line_comment":
            if char == "\n":
                state = "code"
        i += 1
    raise AssertionError(f"unterminated production function: {signature}")


PRELUDE = r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define TARGET_PC 1
#define MELEE_WEB_SAVE_PROFILE_CARD_BYTES 8
#define MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED 3
#define MELEE_WEB_MENU_CSS 1
#define GM_VS 7
#define GS_CSS 8
#define GM_MAX_PLAYERS 6
#define MELEE_WEB_PAD_STATE_BYTES 822
#define HSD_RP_SCREEN 1
#define HSD_STATE_ALL 0xffffffffu
#define GX_MAX_Z24 0xffffffu

typedef struct { uint16_t button; int8_t stickX,stickY,substickX,substickY;
  uint8_t triggerLeft,triggerRight,analogA,analogB; int8_t err;
  uint16_t extButton; uint8_t abi_padding[2]; } PADStatus;
typedef struct { PADStatus stat[4]; } HSD_PadData;
typedef struct { HSD_PadData* queue; int qcount,qread,qwrite; } PadLibData;
typedef struct { int pending_scene_change; void* ko_counts; } CSSData;
typedef struct { int scene_kind; void* enter_data; void* exit_data; } GameSceneInfo;
typedef struct { CSSData css; int phase,result,request; } MeleeWebMenuSession;
typedef struct { int active; } MeleeWebAudio;
typedef struct { int unused; } MeleeWebSaveProfileOwner;
typedef struct { uint64_t generation; unsigned ticks; } GameplayStats;
typedef struct { int opaque; } GXRenderModeObj;
typedef struct { int opaque; } GXColor;
typedef struct MeleeWebMenuHost MeleeWebMenuHost;
struct MeleeWebMenuHost {
  MeleeWebMenuSession* session; MeleeWebSaveProfileOwner* profile;
  MeleeWebAudio* audio; uint64_t generation,audio_generation;
  uint32_t seed,*saved_seed; HSD_PadData queue;
  GameSceneInfo source_scene_info; int source_scene,source_mode_kind,vs_mode_owned;
  int entered,drawing,transition;
  int first_css_return_state;
  int final_pending_css_draw_witness,final_pending_css_draw_state;
  int final_pending_css_draw_tick_result,final_pending_css_draw_request;
  int final_pending_css_draw_pending_scene_change;
  unsigned final_pending_css_draw_input_ordinal,final_pending_css_draw_pad_sequence;
  PADStatus final_pending_css_draw_raw[4]; uint64_t final_pending_css_draw_generation;
  MeleeWebMenuSession* final_pending_css_draw_session;
  CSSData* final_pending_css_draw_css; GameSceneInfo* final_pending_css_draw_scene_info;
  uint32_t* final_pending_css_draw_seed_owner; uint32_t final_pending_css_draw_seed;
};
static MeleeWebMenuHost* owner;
static uint32_t* seed_ptr;
static PadLibData HSD_PadLibData;
static MeleeWebMenuHost* active_host;
static GameSceneInfo* active_scene_info;
static GameplayStats stats={7,0};
static GXRenderModeObj render_mode, GXNtsc480IntDf;
static void* HSD_GObj_804D781C,*HSD_GObj_804D7838,*HSD_GObj_804D7830;
static void* HSD_GObj_804D7814,*HSD_GObj_804D7818;
static int gobj_draws, clock_presents, configured_result=3, configured_request=19;
static int clock_present_ok=1;
enum { MELEE_WEB_HOST_SCENE_CSS=1, MELEE_WEB_HOST_SCENE_TITLE=3,
       MELEE_WEB_HOST_SCENE_MAIN=4 };

static int fail(char*,size_t,const char*);
static int ok(char*,size_t);
static int live(MeleeWebMenuHost*,char*,size_t);
static GameplayStats melee_web_gameplay_stats(void);
static int melee_web_audio_is_active(MeleeWebAudio*);
static int melee_web_audio_bank_transport_active(void);
static int melee_web_save_profile_owner_live(MeleeWebSaveProfileOwner*,char*,size_t);
static int melee_web_menu_phase(MeleeWebMenuSession*);
static CSSData* melee_web_menu_css(MeleeWebMenuSession*);
static int gm_GetCurrentGameMode(void);
static GameSceneInfo* melee_web_current_scene_info(void);
static void HSD_PadRumbleInterpret(void);
static void HSD_PadRenewMasterStatus(void);
static void HSD_PadRenewCopyStatus(void);
static void HSD_PadRenewGameStatus(void);
static void gm_EvaluateAllControllerInputs(void);
static int source_scene_tick(MeleeWebMenuHost*,char*,size_t);
static int melee_web_menu_tick(MeleeWebMenuSession*,char*,size_t);
static GXRenderModeObj* HSD_VIGetRenderMode(void);
static void GXInvalidateVtxCache(void);
static void GXInvalidateTexAll(void);
static void HSD_StartRender(int);
static void HSD_StateInvalidate(unsigned);
static void HSD_GObj_80390FC0(void);
static void HSD_Init_803755A8(void);
static int melee_web_menu_clock_present(void);
"""


STUBS_AND_TESTS = r"""
static GameplayStats melee_web_gameplay_stats(void) { return stats; }
static int melee_web_audio_is_active(MeleeWebAudio* a) { return a != NULL && a->active; }
static int melee_web_audio_bank_transport_active(void) { return 1; }
static int melee_web_save_profile_owner_live(MeleeWebSaveProfileOwner* p,char* e,size_t n)
{ (void)e;(void)n; return p != NULL; }
static int melee_web_menu_phase(MeleeWebMenuSession* s) { return s->phase; }
static CSSData* melee_web_menu_css(MeleeWebMenuSession* s) { return &s->css; }
static int gm_GetCurrentGameMode(void) { return GM_VS; }
static GameSceneInfo* melee_web_current_scene_info(void) { return active_scene_info; }
static void HSD_PadRumbleInterpret(void) { }
static void HSD_PadRenewMasterStatus(void) { }
static void HSD_PadRenewCopyStatus(void) { }
static void HSD_PadRenewGameStatus(void) { HSD_PadLibData.qcount=0; }
static void gm_EvaluateAllControllerInputs(void) { }
static int source_scene_tick(MeleeWebMenuHost* h,char* e,size_t n)
{ (void)h;(void)e;(void)n; return 0; }
static int melee_web_menu_tick(MeleeWebMenuSession* s,char* e,size_t n)
{ (void)e;(void)n; active_host->transition=s->request; return configured_result; }
static GXRenderModeObj* HSD_VIGetRenderMode(void) { return &render_mode; }
static void GXInvalidateVtxCache(void) { }
static void GXInvalidateTexAll(void) { }
static void HSD_StartRender(int x) { (void)x; }
static void HSD_StateInvalidate(unsigned x) { (void)x; }
static void HSD_GObj_80390FC0(void) { ++gobj_draws; }
static void HSD_Init_803755A8(void) { }
static int melee_web_menu_clock_present(void) { ++clock_presents; return clock_present_ok; }

static void setup(MeleeWebMenuHost* h,MeleeWebMenuSession* s,
                  MeleeWebAudio* a,MeleeWebSaveProfileOwner* p)
{
  memset(h,0,sizeof(*h)); memset(s,0,sizeof(*s)); memset(a,0,sizeof(*a));
  memset(p,0,sizeof(*p));
  a->active=1; s->phase=MELEE_WEB_MENU_CSS; s->request=configured_request;
  h->session=s; h->profile=p; h->audio=a; h->generation=stats.generation;
  h->seed=0x35a455b9u; h->source_scene=MELEE_WEB_HOST_SCENE_CSS;
  h->source_mode_kind=GM_VS; h->vs_mode_owned=1; h->entered=1;
  h->source_scene_info.scene_kind=GS_CSS;
  h->source_scene_info.enter_data=&s->css; h->source_scene_info.exit_data=&s->css;
  owner=h; active_host=h; active_scene_info=&h->source_scene_info; seed_ptr=&h->seed;
  HSD_PadLibData.queue=&h->queue; HSD_PadLibData.qcount=0;
  HSD_PadLibData.qread=HSD_PadLibData.qwrite=0;
  HSD_GObj_804D781C=HSD_GObj_804D7838=HSD_GObj_804D7830=NULL;
  HSD_GObj_804D7814=HSD_GObj_804D7818=NULL;
  gobj_draws=clock_presents=0; clock_present_ok=1;
  configured_result=3; configured_request=19;
}
static void make_raw(PADStatus raw[4])
{
  unsigned i; memset(raw,0,sizeof(PADStatus)*4);
  for(i=0;i<4;i++){raw[i].button=(uint16_t)(0x10+i);raw[i].stickX=(int8_t)(i+1);
    raw[i].stickY=(int8_t)(-((int)i+1));raw[i].triggerLeft=(uint8_t)(3+i);
    raw[i].err=(int8_t)i;raw[i].extButton=(uint16_t)(0x80+i);
    raw[i].abi_padding[0]=(uint8_t)(0xa0+i);}
}
static int produce_transition(MeleeWebMenuHost* h,PADStatus raw[4])
{ return melee_web_menu_host_tick(h,raw,NULL,0); }

static void test_one_use_success_and_normal_pending_noop(void)
{
  MeleeWebMenuHost h; MeleeWebMenuSession s; MeleeWebAudio a;
  MeleeWebSaveProfileOwner p; PADStatus raw[4]; char error[160]={0};
  setup(&h,&s,&a,&p); make_raw(raw);
  assert(produce_transition(&h,raw)==MELEE_WEB_MENU_RESULT_TRANSITION_REQUESTED);
  assert(h.final_pending_css_draw_witness==1&&h.final_pending_css_draw_request==19);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==1);
  assert(h.final_pending_css_draw_state==2&&gobj_draws==1&&clock_presents==1);
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==1&&clock_presents==1);
  /* The ordinary public draw stays a successful no-op on a pending transition. */
  assert(melee_web_menu_host_draw(&h,error,sizeof(error))==1);
  assert(gobj_draws==1&&clock_presents==1&&h.final_pending_css_draw_witness==0);
}

static void test_wrong_input_and_semantic_pad_fail_closed(void)
{
  MeleeWebMenuHost h; MeleeWebMenuSession s; MeleeWebAudio a;
  MeleeWebSaveProfileOwner p; PADStatus raw[4], changed[4]; char error[160]={0};
  setup(&h,&s,&a,&p); make_raw(raw);
  assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,147,1574,raw,error,sizeof(error))==0);
  assert(h.final_pending_css_draw_state==2&&gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw); make_raw(changed);
  assert(produce_transition(&h,raw)==3);
  changed[2].stickY++;
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,changed,error,sizeof(error))==0);
  assert(h.final_pending_css_draw_state==2&&gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw);
  assert(produce_transition(&h,raw)==3);
  raw[0].abi_padding[0]^=0xff; /* Padding is not part of PAD authority. */
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  h.queue.stat[0].extButton^=1;
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0&&clock_presents==0);
}

static void test_live_owner_changes_and_later_tick_invalidate(void)
{
  MeleeWebMenuHost h; MeleeWebMenuSession s; MeleeWebAudio a;
  MeleeWebSaveProfileOwner p; PADStatus raw[4]; char error[160]={0};
  uint32_t other_seed=0x35a455b9u; GameSceneInfo foreign_scene={GS_CSS,0,0};

  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  h.transition++; assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  seed_ptr=&other_seed; assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  active_scene_info=&foreign_scene;
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  configured_result=1; s.request=0;
  assert(melee_web_menu_host_tick(&h,raw,error,sizeof(error))==1);
  assert(h.final_pending_css_draw_witness==0&&h.final_pending_css_draw_state==2);
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0);
}

static void test_preexit_pending_scalar_is_observed_and_bound(void)
{
  for(int initial=0;initial<=1;initial++) {
    MeleeWebMenuHost h;MeleeWebMenuSession s;MeleeWebAudio a;
    MeleeWebSaveProfileOwner p;PADStatus raw[4];char error[160]={0};
    setup(&h,&s,&a,&p);make_raw(raw);s.css.pending_scene_change=initial;
    assert(produce_transition(&h,raw)==3);
    assert(s.css.pending_scene_change==initial);
    assert(h.final_pending_css_draw_pending_scene_change==initial);
    assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
    assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==1);
    assert(s.css.pending_scene_change==initial&&h.transition==19&&gobj_draws==1);
    setup(&h,&s,&a,&p);make_raw(raw);s.css.pending_scene_change=initial;
    assert(produce_transition(&h,raw)==3);
    s.css.pending_scene_change=initial^1;
    assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==0);
    assert(gobj_draws==0);
    setup(&h,&s,&a,&p);make_raw(raw);s.css.pending_scene_change=initial;
    assert(produce_transition(&h,raw)==3);
    assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
    s.css.pending_scene_change=initial^1;
    assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
    assert(gobj_draws==0);
  }
}

static void test_failed_clock_consumes_authorization(void)
{
  MeleeWebMenuHost h; MeleeWebMenuSession s; MeleeWebAudio a;
  MeleeWebSaveProfileOwner p; PADStatus raw[4]; char error[160]={0};
  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  clock_present_ok=0;
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(h.final_pending_css_draw_state==2&&gobj_draws==1&&clock_presents==1);
  clock_present_ok=1;
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==1&&clock_presents==1);
}

static void test_active_source_callback_refuses_arm_and_draw(void)
{
  MeleeWebMenuHost h; MeleeWebMenuSession s; MeleeWebAudio a;
  MeleeWebSaveProfileOwner p; PADStatus raw[4]; char error[160]={0};
  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  HSD_GObj_804D781C=(void*)1;
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==0);
  assert(h.final_pending_css_draw_state==2&&gobj_draws==0);

  setup(&h,&s,&a,&p); make_raw(raw); assert(produce_transition(&h,raw)==3);
  assert(melee_web_menu_host_arm_final_pending_css_draw(&h,148,1574,raw,error,sizeof(error))==1);
  HSD_GObj_804D7838=(void*)1;
  assert(melee_web_menu_host_draw_final_pending_css(&h,error,sizeof(error))==0);
  assert(gobj_draws==0&&clock_presents==0);
}

int main(void)
{
  test_one_use_success_and_normal_pending_noop();
  test_wrong_input_and_semantic_pad_fail_closed();
  test_live_owner_changes_and_later_tick_invalidate();
  test_preexit_pending_scalar_is_observed_and_bound();
  test_failed_clock_consumes_authorization();
  test_active_source_callback_refuses_arm_and_draw();
  puts("actual-source final pending CSS host controls passed");
  return 0;
}
"""


CPP_CAPTURE_PRELUDE = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
struct GameModeState { struct GameSceneInfo { int scene_kind; }; };
struct FirstCssDrawSample {
 std::array<uint8_t,822> pad{}; uint32_t seed=0,scene_frame=0;
 uint64_t world_generation=0; int source_scene=0,menu_phase=0,scene_kind=0;
 unsigned current_mode=0,previous_mode=0,current_scene_index=0,previous_scene_index=0;
 bool seed_owner_stable=false,scene_owner_stable=false,world_generation_stable=false;
};
struct FirstCssBrowserDrawState {
 uint64_t world_generation=0; const uint32_t* seed_owner=nullptr;
 const GameModeState::GameSceneInfo* scene_owner=nullptr;
};
static FirstCssBrowserDrawState first_css_browser_draw;
static void* host; static void* world; static bool host_entered;
static const uint32_t* seed_ptr;
static const GameModeState::GameSceneInfo* scene_ptr;
static uint64_t generation_value; static int source_scene_value,phase_value,frame_value;
static int current_mode_value,previous_mode_value,current_index_value,previous_index_value;
static std::array<uint8_t,822> captured_pad;
static void check(bool condition,const char* message)
{ if(!condition)throw std::runtime_error(message); }
static bool melee_web_gameplay_world_exists(void){return true;}
static bool melee_web_source_memory_healthy(void){return true;}
static uint64_t melee_web_gameplay_generation(void){return generation_value;}
static const GameModeState::GameSceneInfo* melee_web_current_scene_info(void){return scene_ptr;}
static int melee_web_menu_host_source_scene(void*){return source_scene_value;}
static int melee_web_menu_host_phase(void*){return phase_value;}
static void melee_web_pad_state_capture(uint8_t* out)
{ std::copy(captured_pad.begin(),captured_pad.end(),out); }
static int gm_801A4BA8(void){return frame_value;}
static int gm_GetCurrentGameMode(void){return current_mode_value;}
static int gm_GetPreviousGameMode(void){return previous_mode_value;}
static int gm_GetCurrentSceneIndex(void){return current_index_value;}
static int gm_GetPreviousSceneIndex(void){return previous_index_value;}
"""


CPP_CAPTURE_TEST = r"""
int main(void){
 uint32_t seed=0x12345678;
 GameModeState::GameSceneInfo scene{8};
 host=(void*)1;world=(void*)2;host_entered=true;seed_ptr=&seed;
 first_css_browser_draw.seed_owner=&seed;first_css_browser_draw.scene_owner=&scene;
 first_css_browser_draw.world_generation=44;
 generation_value=44;scene_ptr=&scene;source_scene_value=1;phase_value=1;
 frame_value=149;seed=0x87654321;current_mode_value=7;previous_mode_value=6;
 current_index_value=4;previous_index_value=3;captured_pad.fill(0x5a);
 scene.scene_kind=9;source_scene_value=2;phase_value=2;
 auto sample=first_css_final_pending_draw_capture("same-owner mutation control");
 assert(sample.scene_kind==9&&sample.source_scene==2&&sample.menu_phase==2);
 assert(sample.scene_frame==149&&sample.seed==0x87654321&&sample.pad[0]==0x5a);
 assert(sample.world_generation_stable&&sample.seed_owner_stable&&sample.scene_owner_stable);

 scene_ptr=reinterpret_cast<const GameModeState::GameSceneInfo*>(uintptr_t(1));
 bool refused=false;
 try{(void)first_css_final_pending_draw_capture("foreign scene pointer control");}
 catch(const std::runtime_error&){refused=true;}
 assert(refused);
 scene_ptr=&scene;uint32_t foreign_seed=seed;seed_ptr=&foreign_seed;refused=false;
 try{(void)first_css_final_pending_draw_capture("foreign seed pointer control");}
 catch(const std::runtime_error&){refused=true;}
 assert(refused);
 seed_ptr=&seed;generation_value++ ;refused=false;
 try{(void)first_css_final_pending_draw_capture("foreign generation control");}
 catch(const std::runtime_error&){refused=true;}
 assert(refused);
 return 0;
}
"""


class FinalPendingCssDrawHostTests(OwnedWorkspaceTests):
    def test_actual_host_ownership_and_draw_functions(self) -> None:
        source = HOST_SOURCE.read_text(encoding="utf-8")
        functions = [
            _function(source, "static int fail("),
            _function(source, "static int ok("),
            _function(source, "static int live("),
            _function(source, "static void final_pending_css_draw_invalidate("),
            _function(source, "static int final_pending_css_pad_equal("),
            _function(source, "static void final_pending_css_pad_copy("),
            _function(source, "static int final_pending_css_draw_owner_live("),
            _function(source, "int melee_web_menu_host_tick("),
            _function(source, "static int host_draw_render("),
            _function(source, "int melee_web_menu_host_draw("),
            _function(source, "int melee_web_menu_host_arm_final_pending_css_draw("),
            _function(source, "int melee_web_menu_host_draw_final_pending_css("),
        ]
        c_source = PRELUDE + "\n".join(functions) + STUBS_AND_TESTS
        cc = shlex.split(os.environ.get("CC", "cc"))
        workspace = self.new_workspace(ROOT, "first-css-final-host-")
        c_path = workspace / "host_control.c"
        binary = workspace / "host_control"
        c_path.write_text(c_source, encoding="utf-8")
        compile_result = subprocess.run(
            cc + ["-std=c11", "-Wall", "-Wextra", "-Werror", str(c_path), "-o", str(binary)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout)
        run_result = subprocess.run(
            [str(binary)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(run_result.returncode, 0, run_result.stdout)
        self.assertIn("actual-source final pending CSS host controls passed", run_result.stdout)

    def test_actual_final_capture_retains_same_owner_scalar_changes(self) -> None:
        source = (ROOT / "src" / "gameplay_menu_browser.cpp").read_text(encoding="utf-8")
        function = _function(source,
            "FirstCssDrawSample first_css_final_pending_draw_capture(")
        cxx = shlex.split(os.environ.get("CXX", "c++"))
        workspace = self.new_workspace(ROOT, "first-css-final-capture-")
        cpp_path = workspace / "capture_control.cpp"
        binary = workspace / "capture_control"
        cpp_path.write_text(CPP_CAPTURE_PRELUDE + function + CPP_CAPTURE_TEST,
                            encoding="utf-8")
        compile_result = subprocess.run(
            cxx + ["-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp_path), "-o", str(binary)],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout)
        run_result = subprocess.run([str(binary)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        self.assertEqual(run_result.returncode, 0, run_result.stdout)


if __name__ == "__main__":
    unittest.main()
