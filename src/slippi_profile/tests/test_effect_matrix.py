# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused helper and lifecycle controls for the private V14 effect phase.

The project helper is compiled directly, while effect owners and SetupMatrixSub
are modeled seams; this does not validate original effect ABI or matrix math.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / "src/slippi_profile"


STUBS = {
    "sysdolphin/baselib/gobj.h": r'''#ifndef FIXTURE_GOBJ_H
#define FIXTURE_GOBJ_H
#include <stdint.h>
typedef struct HSD_GObj HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef void (*FixtureRenderCallback)(HSD_GObj*, int);
typedef struct HSD_GObjList HSD_GObjList;
typedef struct { uint8_t p_link_max; } HSD_GObjLibInitDataType;
struct HSD_GObj {
 uint16_t classifier; uint8_t p_link, gx_link, p_priority, render_priority;
 uint8_t obj_kind, user_data_kind; HSD_GObj *next, *prev;
 HSD_GObjProc *proc; FixtureRenderCallback render_cb;
 void *hsd_obj, *user_data; void (*user_data_remove_func)(void*);
};
#define HSD_GOBJ_CLASS_EFFECT 8
#define HSD_GOBJ_CLASS_FOG 10
#define HSD_GOBJ_OBJ_NONE 255
extern HSD_GObjList *HSD_GObj_Entities;
extern HSD_GObjLibInitDataType HSD_GObjLibInitData;
extern uint8_t HSD_GObj_JObjKind;
extern int8_t HSD_GObj_FogKind;
void HSD_GObj_JObjCallback(HSD_GObj*, int);
#endif
''',
    "sysdolphin/baselib/gobjproc.h": r'''#ifndef FIXTURE_GOBJPROC_H
#define FIXTURE_GOBJPROC_H
#include "gobj.h"
struct HSD_GObjProc { HSD_GObjProc *child; uint8_t s_link; HSD_GObj *gobj; HSD_GObjEvent on_invoke; };
#endif
''',
    "sysdolphin/baselib/gobjuserdata.h": r'''#define HSD_GOBJ_USER_DATA_NONE 255
''',
    "sysdolphin/baselib/objalloc.h": r'''#include <stdint.h>
typedef struct { uint32_t used; } HSD_ObjAllocData;
''',
    "sysdolphin/baselib/jobj.h": r'''#ifndef FIXTURE_JOBJ_H
#define FIXTURE_JOBJ_H
#include <stddef.h>
#include <stdint.h>
#define JOBJ_MTX_DIRTY (1u << 6)
#define JOBJ_USER_DEF_MTX (1u << 23)
typedef struct HSD_JObj { struct HSD_JObj *next, *parent, *child; uint32_t flags; unsigned setup_order; } HSD_JObj;
void HSD_JObjSetupMatrixSub(HSD_JObj*);
#endif
''',
    "melee/ef/types.h": r'''#include <sysdolphin/baselib/gobj.h>
typedef struct EF_Effect { HSD_GObj *gobj; } EF_Effect;
''',
    "melee/ef/eflib.h": r'''#include <sysdolphin/baselib/gobj.h>
void efLib_particles_proc_main(HSD_GObj*);
void efLib_particles_proc_aux(HSD_GObj*);
void efLib_Update(HSD_GObj*);
void efLib_remove_user_data(void*);
''',
}


def _function(source: str, name: str) -> str:
    import re
    match = re.search(r"(?:static\s+)?int\s+" + re.escape(name) + r"\s*\(", source)
    if not match:
        raise AssertionError(f"missing actual bootstrap function {name}")
    opening = source.index("{", match.end())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated actual bootstrap function {name}")


class EffectMatrixTests(unittest.TestCase):
    def test_project_helper_with_modeled_owner_and_setup_seams(self):
        scratch_parent = ROOT / "work"
        scratch_parent.mkdir(exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="effect-matrix-", dir=scratch_parent))
        try:
            include = scratch / "include"
            for name, body in STUBS.items():
                path = include / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(body, encoding="utf-8")
            fixture = scratch / "control.c"
            fixture.write_text(
                (PROFILE / "tests/effect_matrix_control.c.in").read_text(encoding="utf-8"),
                encoding="utf-8",
            )
            command = [
                os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(include), "-I" + str(ROOT / "src"),
                str(PROFILE / "effect_matrix.c"), str(PROFILE / "dynamics_fix.c"),
                str(fixture), "-o", str(scratch / "control"),
            ]
            result = subprocess.run(command, cwd=scratch, capture_output=True, text=True, timeout=60)
            (scratch / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(scratch / "control")], cwd=scratch,
                                    capture_output=True, text=True, timeout=15)
            (scratch / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("exact owners", result.stdout)
        except BaseException:
            print("Retained effect matrix fixture:", scratch, flush=True)
            raise
        else:
            shutil.rmtree(scratch)

    def test_actual_bootstrap_post_callback_is_ephemeral_and_closes_on_error(self):
        scratch_parent = ROOT / "work"
        scratch_parent.mkdir(exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="effect-step-callback-", dir=scratch_parent))
        try:
            bootstrap = (ROOT / "src/gameplay_bootstrap.c").read_text(encoding="utf-8")
            functions = "\n\n".join(_function(bootstrap, name) for name in (
                "gameplay_step", "melee_web_gameplay_step",
                "melee_web_gameplay_step_post_source",
            ))
            source = '''#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "gameplay_bootstrap.h"
static void *arena=(void*)1; static uint64_t ticks=4,generation=9;
static int tables_live=1,shutting_down,stepping;
static int unhealthy_call,health_calls,balanced=1,callback_result=1,callback_calls,order[4],order_count;
static int has_render_objects(void){return 0;}
static int melee_web_gameplay_heap_owns(const void*p){return p==arena;}
static int melee_web_source_memory_healthy(void){health_calls++;return health_calls!=unhealthy_call;}
static int melee_web_source_context_begin_tick(uint64_t w){assert(w==generation);order[order_count++]=1;return 1;}
static int melee_web_source_context_end_tick(void){order[order_count++]=3;return balanced;}
static void HSD_GObj_80390CFC(void){assert(stepping);order[order_count++]=2;}
static int fail(char*e,size_t n,const char*m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int success(char*e,size_t n){if(e&&n)e[0]=0;return 1;}
static int callback(void*ctx,char*e,size_t n){(void)ctx;(void)n;assert(stepping);assert(order_count==2);order[order_count++]=4;callback_calls++;if(!callback_result&&e)strcpy(e,"fixture callback refusal");return callback_result;}
BOOTSTRAP_FUNCTIONS
static void reset(void){unhealthy_call=health_calls=0;balanced=callback_result=1;callback_calls=0;order_count=0;}
int main(void){char e[128]={0};reset();assert(melee_web_gameplay_step_post_source(callback,0,e,sizeof(e)));assert(ticks==5&&!stepping&&callback_calls==1&&order_count==4&&order[0]==1&&order[1]==2&&order[2]==4&&order[3]==3);
 reset();callback_result=0;assert(!melee_web_gameplay_step_post_source(callback,0,e,sizeof(e)));assert(strstr(e,"fixture callback refusal")&&ticks==5&&!stepping&&callback_calls==1&&order[order_count-1]==3);
 reset();unhealthy_call=2;assert(!melee_web_gameplay_step_post_source(callback,0,e,sizeof(e)));assert(ticks==5&&!stepping&&callback_calls==0&&order[order_count-1]==3);
 reset();assert(melee_web_gameplay_step(e,sizeof(e)));assert(ticks==6&&!stepping&&callback_calls==0&&order_count==3&&order[2]==3);
 reset();balanced=0;assert(!melee_web_gameplay_step_post_source(callback,0,e,sizeof(e)));assert(strstr(e,"Source PPC callback frames did not balance")&&ticks==6&&!stepping&&callback_calls==1&&order[order_count-1]==3);
 puts("actual bootstrap post-source callback lifecycle passed");return 0;}
'''.replace("BOOTSTRAP_FUNCTIONS", functions)
            fixture = scratch / "control.c"
            fixture.write_text(source, encoding="utf-8")
            command = [os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                       "-I" + str(ROOT / "src"), str(fixture), "-o", str(scratch / "control")]
            result = subprocess.run(command, cwd=scratch, capture_output=True, text=True, timeout=45)
            (scratch / "compile.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(scratch / "control")], cwd=scratch,
                                    capture_output=True, text=True, timeout=15)
            (scratch / "run.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        except BaseException:
            print("Retained post-source callback fixture:", scratch, flush=True)
            raise
        else:
            shutil.rmtree(scratch)

    def test_only_live_raw_and_normalized_match_paths_use_v14_hook(self):
        source = (ROOT / "src/gameplay_match_context.c").read_text(encoding="utf-8")
        self.assertEqual(source.count("if(!match_gameplay_step(e,n))return 0;"), 2)
        self.assertIn("SLIPPI_PROFILE_IDENTITY_EFFECT_MATRIX_V14", source)
        self.assertIn("return slippi_profile_effect_matrices_post_step", source)
        bootstrap = (ROOT / "src/gameplay_bootstrap.c").read_text(encoding="utf-8")
        body = _function(bootstrap, "gameplay_step")
        self.assertLess(body.index("HSD_GObj_80390CFC();"), body.index("post_source(post_source_context"))
        self.assertLess(body.index("post_source(post_source_context"), body.index("melee_web_source_context_end_tick()"))
        self.assertIn("return gameplay_step(NULL, NULL, error, error_size);", bootstrap)


if __name__ == "__main__":
    unittest.main()
