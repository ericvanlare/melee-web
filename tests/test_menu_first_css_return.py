"""Exact extracted diagnostic note/arm/read and CSS order; synthetic owners.

This control proves ownership, note timing and failure completion, not SDK
execution, source ABI agreement or original/native CSS state equivalence.
"""
from pathlib import Path
import re
import shutil
import subprocess
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class FirstCssReturnTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "first-css-return-")

    def test_extracted_return_order_and_owner_refusals(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("Native C compiler required")
        menu = (ROOT / "src/gameplay_menu.c").read_text()
        host = (ROOT / "src/gameplay_menu_host.c").read_text()
        header = (ROOT / "src/gameplay_menu_host.h").read_text()
        # Ordinary source sequence is unchanged, with the diagnostic note
        # specifically between the callback return and phase updates.
        enter = function(menu, "static int enter_css(")
        self.assertLess(enter.index("mnCharSel_Scene_OnEnter(&session->css);"),
                        enter.index("session->first_css_return_note("))
        self.assertLess(enter.index("session->first_css_return_note("),
                        enter.index("session->css_open = 1;"))
        for signature in ["int melee_web_menu_tick(", "int melee_web_menu_leave_css(",
                          "int melee_web_menu_abort("]:
            body = function(menu, signature)
            self.assertIn("session->first_css_return_armed = 3;", body)
            self.assertLess(body.index("session->first_css_return_armed = 3;"),
                            body.index("#endif"))
        for signature in ["int melee_web_menu_host_tick(",
                          "int melee_web_menu_host_leave("]:
            body = function(host, signature)
            self.assertLess(body.index("h->first_css_return_state = 3;"),
                            body.index("if(!live(h,e,n)"))
        snapshot = re.search(r"typedef struct MeleeWebMenuCssReturnSnapshot \{.*?\} MeleeWebMenuCssReturnSnapshot;", header, re.S).group()
        menu_parts = [function(menu, s) for s in [
            "static int session_live(", "int melee_web_menu_arm_first_css_return(",
            "int melee_web_menu_first_css_return_live(", "static int enter_css("]]
        menu_code = re.sub(r"\bowner\b", "session_owner", "\n".join(menu_parts))
        host_code = "\n".join(function(host,s) for s in [
            "static void first_css_return_note(",
            "int melee_web_menu_host_arm_first_css_return(",
            "int melee_web_menu_host_first_css_return("])
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define GM_MAX_PLAYERS 6
#define MELEE_WEB_PAD_STATE_BYTES 822
#define MELEE_WEB_MENU_CREATED 0
#define MELEE_WEB_MENU_CSS 1
#define MELEE_WEB_MENU_CSS_READY 4
#define MELEE_WEB_MENU_READY 5
#define MELEE_WEB_MENU_SCENE_CSS 0
#define MELEE_WEB_HOST_SCENE_NONE 0
#define MELEE_WEB_HOST_SCENE_CSS 1
#define GM_VS 2
#define GS_CSS 8
#define VS_MELEE 0
#define TRAINING_MODE 1
typedef int MeleeWebMenuScene;
typedef struct CSSData { int match_type,pending_scene_change; uint8_t* ko_counts; int field; } CSSData;
typedef struct MeleeWebMenuSession MeleeWebMenuSession;
typedef void (*MeleeWebMenuFirstCssReturnNote)(void*,MeleeWebMenuSession*,const CSSData*,const uint8_t*);
typedef struct { void* user; int (*scene_enter)(void*,int,char*,size_t); } Runtime;
struct MeleeWebMenuSession {
 Runtime runtime; CSSData css; uint8_t css_ko_counts[6]; uint64_t ticks;
 int phase,css_open,sss_open,transition_requested,css_parent_route_requested;
 int selection_rejected,transition_failed,training_mode_scene;
 int first_css_return_armed; MeleeWebMenuFirstCssReturnNote first_css_return_note;
};
''' + snapshot + r'''
typedef struct { int scene_kind; const void* enter_data; const void* exit_data; } Scene;
typedef struct MeleeWebMenuHost {
 MeleeWebMenuSession* session; void* profile; void* audio;
 int entered,drawing,initial_replay_context,source_scene,source_mode_kind,vs_mode_owned;
 uint32_t seed; uint64_t generation,first_css_return_generation;
 int first_css_return_state; char first_css_return_error[160];
 MeleeWebMenuCssReturnSnapshot first_css_return; Scene source_scene_info;
} MeleeWebMenuHost;
static MeleeWebMenuHost* owner;
static MeleeWebMenuSession* session_owner;
static uint32_t* seed_ptr;
static Scene* current_scene;
static void *HSD_GObj_804D781C,*HSD_GObj_804D7838,*HSD_GObj_804D7830,*HSD_GObj_804D7814,*HSD_GObj_804D7818;
static int healthy=1,profile_live=1,poison_ko=0,source_calls=0;
static struct Stats { uint64_t ticks; } stats;
static struct Stats melee_web_gameplay_stats(void){return stats;}
static int fail(char* e,size_t n,const char* text){if(e&&n)snprintf(e,n,"%s",text);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static int live(MeleeWebMenuHost* h,char* e,size_t n){return healthy && h==owner && h->audio && h->generation ? 1:fail(e,n,"world lost");}
static int melee_web_save_profile_owner_live(void* p,char* e,size_t n){return p&&profile_live?1:fail(e,n,"profile lost");}
static const CSSData* melee_web_menu_css(const MeleeWebMenuSession* s){return s&&s==session_owner?&s->css:NULL;}
static int melee_web_menu_phase(const MeleeWebMenuSession* s){return s&&s==session_owner?s->phase:-1;}
static Scene* melee_web_current_scene_info(void){return current_scene;}
static void melee_web_pad_state_capture(uint8_t out[822]){memset(out,0x37,822);}
static int check_runtime(MeleeWebMenuSession* s,int scene,char* e,size_t n){(void)s;(void)scene;return live(owner,e,n);}
static int observe_transition(MeleeWebMenuSession* s,int scene,int* r,char* e,size_t n){(void)s;(void)scene;(void)e;(void)n;*r=0;return 1;}
static int melee_web_menu_gobj_snapshot(MeleeWebMenuSession* s,char* e,size_t n){(void)s;(void)e;(void)n;return 1;}
static void melee_web_menu_gobj_snapshot_clear(MeleeWebMenuSession* s){(void)s;}
static int mode_enter(void* p,int scene,char* e,size_t n){
 MeleeWebMenuHost* h=p;(void)scene;(void)e;(void)n;
 h->source_scene=1;h->source_mode_kind=GM_VS;h->vs_mode_owned=1;
 h->source_scene_info=(Scene){GS_CSS,&h->session->css,&h->session->css};
 current_scene=&h->source_scene_info;h->seed=11;h->session->css.field=21;return 1;
}
static void mnCharSel_Scene_OnEnter(CSSData* css){
 assert(css==&session_owner->css);assert(!session_owner->css_open);
 assert(session_owner->phase==MELEE_WEB_MENU_CREATED || session_owner->phase==MELEE_WEB_MENU_CSS_READY);
 source_calls++;owner->seed=12;css->field=22;
 if(poison_ko)css->ko_counts=NULL;
}
''' + menu_code + '\n' + host_code + r'''
static MeleeWebMenuHost h;
static MeleeWebMenuSession s;
static char error[200];
static void reset(void){
 memset(&h,0,sizeof(h));memset(&s,0,sizeof(s));owner=&h;session_owner=&s;
 h.session=&s;h.profile=&s;h.initial_replay_context=1;h.seed=10;seed_ptr=&h.seed;
 s.runtime=(Runtime){&h,mode_enter};s.css.ko_counts=s.css_ko_counts;
 for(int i=0;i<6;i++)s.css_ko_counts[i]=(uint8_t)(i+1);
 healthy=profile_live=1;poison_ko=0;stats.ticks=0;current_scene=NULL;error[0]=0;
}
static void enter(void){h.audio=&s;h.generation=7;assert(enter_css(&s,0,error,sizeof(error)));h.entered=1;}
static int read_snapshot(void){MeleeWebMenuCssReturnSnapshot out;return melee_web_menu_host_first_css_return(&h,&out,error,sizeof(error));}
int main(void){
 MeleeWebMenuCssReturnSnapshot out;
 reset();assert(!read_snapshot());assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));
 assert(!melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));
 enter();h.seed=99;s.css.field=99; /* Later host state must not replace callback snapshot. */
 assert(melee_web_menu_host_first_css_return(&h,&out,error,sizeof(error)));
 assert(out.random_seed==12&&out.css.field==22&&out.css.ko_counts==NULL);
 assert(out.source_scene==1&&out.source_scene_kind==GS_CSS);
 for(int i=0;i<6;i++)assert(out.ko_counts[i]==i+1);
 for(int i=0;i<822;i++)assert(out.pad_state[i]==0x37);
 s.ticks=1;assert(!read_snapshot());s.ticks=0;
 stats.ticks=1;assert(!read_snapshot());stats.ticks=0;
 s.phase=MELEE_WEB_MENU_READY;assert(!read_snapshot());s.phase=MELEE_WEB_MENU_CSS;
 h.generation=8;assert(!read_snapshot());h.generation=7;
 seed_ptr=NULL;assert(!read_snapshot());seed_ptr=&h.seed;
 current_scene=NULL;assert(!read_snapshot());current_scene=&h.source_scene_info;
 h.entered=0;assert(!read_snapshot());h.entered=1;
 void** active[]={&HSD_GObj_804D781C,&HSD_GObj_804D7838,&HSD_GObj_804D7830,&HSD_GObj_804D7814,&HSD_GObj_804D7818};
 for(int i=0;i<5;i++){*active[i]=&h;assert(!read_snapshot());*active[i]=NULL;}
 healthy=0;assert(!read_snapshot());healthy=1;
 assert(!melee_web_menu_host_first_css_return((MeleeWebMenuHost*)(uintptr_t)1,&out,error,sizeof(error)));
 assert(!melee_web_menu_host_first_css_return(&h,NULL,error,sizeof(error)));
 reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));poison_ko=1;enter();
 assert(s.css_open&&s.phase==MELEE_WEB_MENU_CSS);assert(!read_snapshot());assert(strstr(error,"KO/RNG"));
 reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));enter();
 s.css_open=0;s.phase=MELEE_WEB_MENU_CSS_READY;h.entered=0;
 assert(enter_css(&s,0,error,sizeof(error)));h.entered=1;assert(!read_snapshot());
 reset();enter();assert(!read_snapshot());assert(s.css_open&&s.phase==MELEE_WEB_MENU_CSS); /* Unarmed unchanged. */
 reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));h.audio=&s;h.generation=7;
 mode_enter(&h,0,error,sizeof(error));
 first_css_return_note(&h,NULL,NULL,NULL);assert(h.first_css_return_state==3);
 reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));h.audio=&s;h.generation=7;
 mode_enter(&h,0,error,sizeof(error));
 CSSData foreign={0};first_css_return_note(&h,&s,&foreign,s.css_ko_counts);assert(h.first_css_return_state==3);
 reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));enter();
 first_css_return_note(&h,&s,&s.css,s.css_ko_counts);assert(!read_snapshot()); /* Duplicate note refuses. */
 reset();profile_live=0;assert(!melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));
 reset();h.initial_replay_context=0;assert(!melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));
 /* Exact note rejects each changed owner operand without output mutation. */
 for(int i=0;i<13;i++){
  reset();assert(melee_web_menu_host_arm_first_css_return(&h,error,sizeof(error)));
  h.audio=&s;h.generation=7;mode_enter(&h,0,error,sizeof(error));
  const uint8_t* ko=s.css_ko_counts;
  switch(i){
   case 0: seed_ptr=NULL;break;
   case 1: h.entered=1;break;
   case 2: h.drawing=1;break;
   case 3: s.phase=MELEE_WEB_MENU_READY;break;
   case 4: h.source_scene=0;break;
   case 5: h.source_mode_kind=99;break;
   case 6: h.vs_mode_owned=0;break;
   case 7: current_scene=NULL;break;
   case 8: h.source_scene_info.scene_kind=9;break;
   case 9: h.source_scene_info.enter_data=NULL;break;
   case 10: h.source_scene_info.exit_data=NULL;break;
   case 11: ko=NULL;break;
   case 12: healthy=0;break;
  }
  first_css_return_note(&h,&s,&s.css,ko);
  assert(h.first_css_return_state==3&&h.first_css_return_generation==0);
 }
 puts("PASS exact extracted callback order, live PAD copy, typed KO ownership and refusal controls");return 0;
}
'''
        scratch = self.scratch
        c = scratch / "control.c"
        c.write_text(harness)
        compile_result = subprocess.run([compiler,"-std=c11","-Wall","-Wextra","-Werror",str(c),"-o",str(scratch/"control")],capture_output=True,text=True)
        (scratch/"compile.log").write_text(compile_result.stdout+compile_result.stderr)
        self.assertEqual(compile_result.returncode,0,compile_result.stderr)
        run = subprocess.run([str(scratch/"control")],capture_output=True,text=True)
        (scratch/"run.log").write_text(run.stdout+run.stderr)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
