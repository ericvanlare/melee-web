"""Actual patched read-only observer guards; synthetic fields, no CSS assets."""
from pathlib import Path
import re
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


class CssObserverFailureTests(OwnedWorkspaceTests):
    def test_actual_observer_guard_order_and_private_one_shot(self):
        patch = (ROOT / "patches/melee-gameplay.patch").read_text()
        start = patch.index("+/* Internal one-shot test binding")
        end = patch.index("+#endif", start)
        actual = "\n".join(line[1:] for line in patch[start:end].splitlines()
                           if line.startswith("+"))
        jobj = (ROOT / ".deps/melee/src/sysdolphin/baselib/jobj.h").read_text()
        flags = "\n".join(re.search(r"^#define " + name + r" .*", jobj, re.M).group()
                          for name in ("JOBJ_MTX_DIRTY", "JOBJ_USER_DEF_MTX"))
        # Only unavailable asset-owned structs/services are synthetic. Compile
        # the exact patched function, callback binding and authored flag values.
        declarations = r'''
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef struct {unsigned flags; float mtx[3][4]; struct {float x;} translate;} HSD_JObj;
struct CSSCursorData {int x4,x5,x6,x7;float xC,x10;};
struct CSSCharModel {float x8,xC;};
typedef struct {int p_kind,p_kind_prev,sel_icon,cpuslider_joint,cpuslider2_joint;
 float togglebtn_left,togglebtn_right,teambtn_left,teambtn_right;} CSSDoor;
typedef struct {struct {struct {struct {int ckind,slot_type,cpu_level,slot;} players[6];} start;} vs;} CSSData;
static CSSData data_object,*mnCharSel_804D6CB0;
static HSD_JObj root_object,*mnCharSel_804D6CC0,sliders[4];
static struct CSSCursorData cursor_objects[4],*mnCharSel_804A0BC0[4];
static struct CSSCharModel model_objects[4],*mnCharSel_804A0BD0[4];
static struct {CSSDoor doors[4];} mnCharSel_803F0DFC;
static int mnCharSel_804D6CF5,missing_slider=-1;
static void lb_80011E24(HSD_JObj* root,HSD_JObj** out,int joint,int end){
 (void)root;(void)end;*out=joint==missing_slider?NULL:&sliders[joint];
}
'''
        controls = r'''
static int cursors[4][4],doors[4][10];static float geometry[4][12];
static int callbacks,guard_seen,port_seen,joint_seen,reentry;
static unsigned flags_seen;static const void* slider_seen;
static void callback(int guard,int port,int joint,const void* data,const void* root,
 const void* cursor,const void* model,const void* slider,unsigned flags){
 (void)data;(void)root;(void)cursor;(void)model;
 ++callbacks;guard_seen=guard;port_seen=port;joint_seen=joint;flags_seen=flags;slider_seen=slider;
 if(reentry){reentry=0;if(melee_web_css_observe_setup(cursors,doors,geometry)!=0)callbacks=99;}
}
static void reset(void){
 memset(&data_object,0,sizeof(data_object));memset(sliders,0,sizeof(sliders));
 mnCharSel_804D6CB0=&data_object;mnCharSel_804D6CC0=&root_object;
 callbacks=guard_seen=flags_seen=0;port_seen=joint_seen=-1;missing_slider=-1;
 for(int i=0;i<4;++i){mnCharSel_804A0BC0[i]=&cursor_objects[i];
  mnCharSel_804A0BD0[i]=&model_objects[i];mnCharSel_803F0DFC.doors[i].cpuslider_joint=i;}
 melee_web_css_observe_setup_diagnostic_once(NULL);
}
static int expected(int guard,int port){
 melee_web_css_observe_setup_diagnostic_once(callback);
 if(melee_web_css_observe_setup(cursors,doors,geometry)!=0||callbacks!=1||
    guard_seen!=guard||port_seen!=port)return 0;
 if(melee_web_css_observe_setup(cursors,doors,geometry)!=0||callbacks!=1)return 0;
 return 1;
}
int main(void){
 reset();mnCharSel_804D6CB0=NULL;mnCharSel_804D6CC0=NULL;
 if(!expected(1,-1))return 1;
 reset();mnCharSel_804D6CC0=NULL;if(!expected(2,-1))return 2;
 reset();melee_web_css_observe_setup_diagnostic_once(callback);
 if(melee_web_css_observe_setup(NULL,doors,geometry)||callbacks!=1||guard_seen!=3)return 3;
 reset();mnCharSel_804A0BC0[2]=NULL;mnCharSel_804A0BD0[2]=NULL;
 if(!expected(4,2))return 4;
 reset();mnCharSel_804A0BD0[2]=NULL;if(!expected(5,2))return 5;
 reset();missing_slider=2;if(!expected(6,2)||joint_seen!=2||slider_seen||flags_seen)return 6;
 reset();sliders[0].flags=JOBJ_MTX_DIRTY;mnCharSel_804A0BC0[2]=NULL;reentry=1;
 if(!expected(7,0)||flags_seen!=JOBJ_MTX_DIRTY||slider_seen!=&sliders[0])return 7;
 /* Normal failure remains silent. The reducer cannot weaken readiness. */
 reset();sliders[2].flags=JOBJ_MTX_DIRTY;
 if(melee_web_css_observe_setup(cursors,doors,geometry)||callbacks)return 8;
 /* Authored USER_DEF + DIRTY semantics remain accepted, source unchanged. */
 reset();sliders[2].flags=JOBJ_USER_DEF_MTX|JOBJ_MTX_DIRTY;
 CSSData saved=data_object;HSD_JObj saved_sliders[4];memcpy(saved_sliders,sliders,sizeof(sliders));
 melee_web_css_observe_setup_diagnostic_once(callback);
 if(!melee_web_css_observe_setup(cursors,doors,geometry)||callbacks||
    memcmp(&saved,&data_object,sizeof(saved))||memcmp(saved_sliders,sliders,sizeof(sliders)))return 9;
 sliders[2].flags=JOBJ_MTX_DIRTY;
 if(melee_web_css_observe_setup(cursors,doors,geometry)||callbacks)return 10;
 puts("Actual observer seven guards, source order, one-shot/reentry, silent normal path and immutable source passed");return 0;
}
'''
        scratch = self.new_workspace(ROOT, "css-observer-failure-")
        source = scratch / "observer.c"
        source.write_text(flags + "\n" + declarations + "\n" + actual + "\n" + controls)
        output = scratch / "observer"
        build = subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                                str(source), "-o", str(output)], capture_output=True,
                               text=True, cwd=ROOT, timeout=30)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(output)], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Actual observer seven guards", run.stdout)
