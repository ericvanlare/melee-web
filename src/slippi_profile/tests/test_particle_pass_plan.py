# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual-source camera/GObj/particle dispatch comparison for the pure planner."""
from hashlib import sha256
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / "src/slippi_profile"
MELEE = ROOT / ".deps/melee/src/melee"
BASELIB = ROOT / ".deps/melee/src/sysdolphin/baselib"

SOURCE_PINS = {
    MELEE / "cm/camera.c": "b149ae8c5f8c2e393bdfe2b6107311501fe5d23771ee9ec21a91d837d88fe8f3",
    BASELIB / "gobj.c": "a8eb5cd248fac8a0e0f3f8d0491c91efbf185329655e2042ba1250c7883de75c",
    BASELIB / "cobj.c": "b49cc2726fa8617c73c12090815611e7c554721f4f5d43bfca77df2cfa24cdce",
    MELEE / "ef/eflib.c": "5450ecd99821f09ea4b9706d400d84b9a42d9d55d2864f530b2a98ce7481dbd4",
    MELEE / "gr/grdisplay.c": "6b1058e4cb5b979dbb8e38a238148d58ce20b47facc39ea241a6b567267cdb37",
    MELEE / "if/ifmagnify.c": "42c8a7477d15836e4771c4b77944b3e2b136b5203e01ecfa91dc314415d38a2b",
    PROFILE / "effect_matrix.c": "928edfb6524bc45fbfba2393bda1c765069352d2ea53587112f97a08749ba570",
}

STUBS = {
    "Runtime/platform.h": r'''#ifndef FIXTURE_PLATFORM_H
#define FIXTURE_PLATFORM_H
typedef void (*Event)(void);
#endif
''',
    "sysdolphin/baselib/class.h": r'''#ifndef FIXTURE_CLASS_H
#define FIXTURE_CLASS_H
#include <stddef.h>
typedef struct HSD_ClassInfo { struct { size_t nb_exist; } head; } HSD_ClassInfo;
typedef struct HSD_Class { HSD_ClassInfo *class_info; } HSD_Class;
#define HSD_CLASS_INFO(o) ((HSD_ClassInfo *)(o))
#endif
''',
    "sysdolphin/baselib/object.h": r'''#ifndef FIXTURE_OBJECT_H
#define FIXTURE_OBJECT_H
#include "class.h"
typedef struct HSD_Obj { HSD_Class parent; } HSD_Obj;
#define HSD_OBJECT_METHOD(o) (((HSD_Obj *)(o))->parent.class_info)
#endif
''',
    "sysdolphin/baselib/gobj.h": r'''#ifndef FIXTURE_GOBJ_H
#define FIXTURE_GOBJ_H
#include <stddef.h>
#include <stdint.h>
#include "Runtime/platform.h"
typedef struct HSD_GObj HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef void (*GObj_RenderFunc)(HSD_GObj*, int);
struct HSD_GObj {
 uint16_t classifier; uint8_t p_link, gx_link, p_priority, render_priority;
 uint8_t obj_kind, user_data_kind; HSD_GObj *next, *prev, *next_gx, *prev_gx;
 HSD_GObjProc *proc; GObj_RenderFunc render_cb; uint64_t gxlink_prios;
 void *hsd_obj, *user_data; void (*user_data_remove_func)(void*);
};
#define HSD_GOBJ_CLASS_EFFECT 8
#define HSD_GOBJ_OBJ_NONE 255
#define HSD_GOBJ_USER_DATA_NONE 255
extern HSD_GObj* HSD_GObj_804D7814;
extern HSD_GObj* HSD_GObj_804D7818;
extern HSD_GObj** HSD_GObjGXLinkHead;
extern HSD_GObj** HSD_GObj_804D7820;
extern uint8_t HSD_GObj_CameraKind;
extern uint8_t HSD_GObj_JObjKind;
typedef struct { uint8_t gx_link_max; } HSD_GObjLibInitDataType;
extern HSD_GObjLibInitDataType HSD_GObjLibInitData;
void HSD_GObj_80390ED0(HSD_GObj*, uint32_t);
void HSD_GObj_803910D8(HSD_GObj*, int);
void HSD_GObj_80390FC0(void);
void HSD_GObj_JObjCallback(HSD_GObj*, int);
#endif
''',
    "sysdolphin/baselib/gobjproc.h": r'''#ifndef FIXTURE_GOBJPROC_H
#define FIXTURE_GOBJPROC_H
#include "gobj.h"
struct HSD_GObjProc { HSD_GObjProc *child; uint8_t s_link; HSD_GObj *gobj; HSD_GObjEvent on_invoke; };
#endif
''',
    "sysdolphin/baselib/gobjuserdata.h": "#define HSD_GOBJ_USER_DATA_NONE 255\n",
    "sysdolphin/baselib/objalloc.h": r'''#ifndef FIXTURE_OBJALLOC_H
#define FIXTURE_OBJALLOC_H
#include <stdint.h>
typedef struct HSD_ObjAllocData { uint32_t used; } HSD_ObjAllocData;
#endif
''',
    "sysdolphin/baselib/cobj.h": r'''#ifndef FIXTURE_COBJ_H
#define FIXTURE_COBJ_H
#include "object.h"
typedef struct HSD_CObj { HSD_Obj parent; int token; } HSD_CObj;
typedef struct HSD_CObjInfo { HSD_ClassInfo parent; } HSD_CObjInfo;
extern HSD_CObjInfo hsdCObj;
#endif
''',
    "sysdolphin/baselib/jobj.h": r'''#ifndef FIXTURE_JOBJ_H
#define FIXTURE_JOBJ_H
#include "object.h"
#define JOBJ_PTCL (1u << 5)
#define JOBJ_SPLINE (1u << 6)
#define JOBJ_INSTANCE (1u << 12)
typedef struct HSD_DObj HSD_DObj;
typedef struct HSD_JObj {
 HSD_Obj object; struct HSD_JObj *next, *parent, *child; uint32_t flags;
 union { void *ptcl; HSD_DObj *dobj; void *spline; } u;
} HSD_JObj;
typedef struct HSD_JObjInfo { HSD_ClassInfo parent; } HSD_JObjInfo;
extern HSD_JObjInfo hsdJObj;
#endif
''',
    "sysdolphin/baselib/dobj.h": r'''#ifndef FIXTURE_DOBJ_H
#define FIXTURE_DOBJ_H
#include "class.h"
typedef struct HSD_DObj { HSD_Class parent; struct HSD_DObj *next; struct HSD_MObj *mobj; struct HSD_PObj *pobj; } HSD_DObj;
typedef struct HSD_DObjInfo { HSD_ClassInfo parent; } HSD_DObjInfo;
extern HSD_DObjInfo hsdDObj;
#endif
''',
    "sysdolphin/baselib/mobj.h": r'''#ifndef FIXTURE_MOBJ_H
#define FIXTURE_MOBJ_H
#include "class.h"
typedef struct HSD_MObj { HSD_Class parent; struct HSD_TObj *tobj; } HSD_MObj;
typedef struct HSD_MObjInfo { HSD_ClassInfo parent; } HSD_MObjInfo;
extern HSD_MObjInfo hsdMObj;
#endif
''',
    "sysdolphin/baselib/pobj.h": r'''#ifndef FIXTURE_POBJ_H
#define FIXTURE_POBJ_H
#include "class.h"
typedef struct HSD_PObj { HSD_Class parent; struct HSD_PObj *next; } HSD_PObj;
typedef struct HSD_PObjInfo { HSD_ClassInfo parent; } HSD_PObjInfo;
extern HSD_PObjInfo hsdPObj;
#endif
''',
    "sysdolphin/baselib/tobj.h": r'''#ifndef FIXTURE_TOBJ_H
#define FIXTURE_TOBJ_H
#include "object.h"
typedef struct HSD_TObj { HSD_Obj object; struct HSD_TObj *next; } HSD_TObj;
typedef struct HSD_TObjInfo { HSD_ClassInfo parent; } HSD_TObjInfo;
extern HSD_TObjInfo hsdTObj;
#endif
''',
    "melee/ef/types.h": r'''#ifndef FIXTURE_EF_TYPES_H
#define FIXTURE_EF_TYPES_H
#include <sysdolphin/baselib/gobj.h>
typedef struct EF_Effect { HSD_GObj *gobj; } EF_Effect;
#endif
''',
    "melee/ef/eflib.h": r'''#ifndef FIXTURE_EFLIB_H
#define FIXTURE_EFLIB_H
#include <sysdolphin/baselib/gobj.h>
void efLib_render_callback(HSD_GObj*, int);
void efLib_particles_proc_main(HSD_GObj*);
void efLib_particles_proc_aux(HSD_GObj*);
void efLib_Update(HSD_GObj*);
void efLib_remove_user_data(void*);
#endif
''',
    "melee/gr/grdisplay.h": r'''#ifndef FIXTURE_GRDISPLAY_H
#define FIXTURE_GRDISPLAY_H
#include <sysdolphin/baselib/gobj.h>
void grDisplay_801C5F60(HSD_GObj*, int);
#endif
''',
    "melee/if/ifmagnify.h": r'''#ifndef FIXTURE_IFMAGNIFY_H
#define FIXTURE_IFMAGNIFY_H
#include <sysdolphin/baselib/gobj.h>
void ifMagnify_802FBBDC(HSD_GObj*);
#endif
''',
}


def extract_function(source: str, marker: str) -> str:
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated source function at {marker}")


def source_fixture() -> str:
    camera = (MELEE / "cm/camera.c").read_text(encoding="utf-8")
    gobj = (BASELIB / "gobj.c").read_text(encoding="utf-8")
    eflib = (MELEE / "ef/eflib.c").read_text(encoding="utf-8")
    snippets = {
        "ACTUAL_CAMERA_SOURCE": "\n\n".join(
            extract_function(camera, marker)
            for marker in (
                "static inline s64 gxlink_prio8(void)",
                "static inline s64 gxlink_prio1(void)",
                "static inline s64 gxlink_prio80(void)",
                "static inline void render_gxlink_pass(",
                "static void fn_800301D0(HSD_GObj* gobj, int arg1)",
            )
        ),
        "ACTUAL_GOBJ_SOURCE": "\n\n".join(
            extract_function(gobj, marker)
            for marker in (
                "static inline void render_gobj(HSD_GObj* cur, int i)",
                "void HSD_GObj_80390ED0(HSD_GObj* gobj, u32 mask)",
                "void HSD_GObj_80390FC0(void)",
                "void HSD_GObj_803910D8(HSD_GObj* gobj, int renderpass)",
            )
        ),
        "ACTUAL_EFLIB_SOURCE": extract_function(
            eflib, "void efLib_render_callback(HSD_GObj* gobj, int code)"
        ),
    }
    fixture = (PROFILE / "tests/particle_pass_source_control.c.in").read_text(
        encoding="utf-8"
    )
    for name, body in snippets.items():
        fixture = fixture.replace(name, body)
    if "ACTUAL_" in fixture:
        raise AssertionError("unexpanded actual-source fixture placeholder")
    return fixture


class ParticlePassPlanTests(unittest.TestCase):
    def test_actual_camera_dispatch_matches_pure_plan_and_refusals(self):
        if not (ROOT / ".deps/melee").is_dir():
            self.skipTest("Melee source checkout is required")
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required")

        for path, expected in SOURCE_PINS.items():
            actual = sha256(path.read_bytes()).hexdigest()
            self.assertEqual(actual, expected, f"source pin changed: {path.relative_to(ROOT)}")

        init = (BASELIB / "gobjinit.c").read_text(encoding="utf-8")
        self.assertIn("0x3F,\n    0x3F,", init)
        self.assertIn("arg0->gx_link_max + 2", init)
        ground = extract_function(
            (MELEE / "gr/grdisplay.c").read_text(encoding="utf-8"),
            "void grDisplay_801C5F60(",
        )
        self.assertLess(ground.index("gobj->gxlink_prios = 8;"),
                        ground.index("HSD_GObj_80390ED0(gobj, 7);"))
        magnify = extract_function(
            (MELEE / "if/ifmagnify.c").read_text(encoding="utf-8"),
            "void ifMagnify_802FC618(void)",
        )
        self.assertIn("(Event) ifMagnify_802FBBDC", magnify)
        self.assertIn("gobj->gxlink_prios = 0x10;", magnify)
        cobj = (BASELIB / "cobj.c").read_text(encoding="utf-8")
        screen_setup = extract_function(cobj, "static bool setupNormalCamera(HSD_CObj* cobj)")
        set_current = extract_function(cobj, "bool HSD_CObjSetCurrent(HSD_CObj* cobj)")
        self.assertTrue(screen_setup.rstrip().endswith("return true;\n}"))
        self.assertLess(set_current.index("case HSD_RP_SCREEN:"),
                        set_current.index("result = setupNormalCamera(cobj);"))

        evidence = os.environ.get("SLIPPI_PARTICLE_PASS_EVIDENCE")
        keep_evidence = evidence is not None
        scratch_parent = Path(evidence) if keep_evidence else ROOT / "work"
        scratch_parent.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="particle-pass-plan-", dir=scratch_parent))
        try:
            include = scratch / "include"
            for name, body in STUBS.items():
                header = include / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text(body, encoding="utf-8")
            fixture = scratch / "control.c"
            fixture.write_text(source_fixture(), encoding="utf-8")
            command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-parameter", "-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1",
                "-DMELEE_WEB_CPU_INTERVAL_ENABLED=1", "-I" + str(include),
                "-I" + str(ROOT / "src"),
                str(PROFILE / "particle_pass_plan.c"),
                str(PROFILE / "particle_source_schedule.c"), str(fixture),
                "-o", str(scratch / "control"),
            ]
            (scratch / "command.txt").write_text(
                "\n".join(command) + "\n", encoding="utf-8"
            )
            (scratch / "source-pins.txt").write_text(
                "\n".join(f"{sha256(path.read_bytes()).hexdigest()}  {path.relative_to(ROOT)}"
                          for path in SOURCE_PINS),
                encoding="utf-8",
            )
            inputs = list(SOURCE_PINS) + [
                BASELIB / "gobjinit.c",
                PROFILE / "particle_pass_plan.c",
                PROFILE / "particle_pass_plan.h",
                PROFILE / "particle_source_schedule.c",
                PROFILE / "particle_source_schedule.h",
                PROFILE / "tests/particle_pass_source_control.c.in",
                PROFILE / "tests/test_particle_pass_plan.py",
                ROOT / "tests/test_slippi_particle_pass_plan.py",
            ]
            generated_inputs = [fixture, scratch / "command.txt"] + [
                path for path in include.rglob("*") if path.is_file()
            ]
            input_hashes = [
                f"{sha256(path.read_bytes()).hexdigest()}  {path.relative_to(ROOT)}"
                for path in inputs
            ]
            input_hashes.extend(
                f"{sha256(path.read_bytes()).hexdigest()}  generated/{path.relative_to(scratch)}"
                for path in generated_inputs
            )
            (scratch / "input-hashes.txt").write_text(
                "\n".join(input_hashes) + "\n", encoding="utf-8"
            )
            result = subprocess.run(command, cwd=scratch, capture_output=True,
                                    text=True, timeout=45)
            (scratch / "compile.log").write_text(
                result.stdout + result.stderr, encoding="utf-8"
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(scratch / "control")], cwd=scratch,
                                    capture_output=True, text=True, timeout=15)
            (scratch / "run.log").write_text(
                result.stdout + result.stderr, encoding="utf-8"
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("actual-source particle camera dispatch", result.stdout)
        except BaseException:
            print("Retained particle pass planner fixture:", scratch, flush=True)
            raise
        else:
            if keep_evidence:
                (scratch / "control").unlink()
                shutil.rmtree(scratch / "include")
            else:
                shutil.rmtree(scratch)


if __name__ == "__main__":
    unittest.main()
