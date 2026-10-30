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
    MELEE / "if/textdraw.c": "5086ba8ff2449cc31c02824cabd4b85d61638dc74e10ef9f6d91430889ac71b0",
    MELEE / "if/ifcoget.c": "33c87ce9f2ab4068cca892c1e64bde31545f632138f44f618e833382dba99df0",
    MELEE / "if/ifall.c": "545d32d9a4ca3f88bd7ab8d73a5c3c68e404f60f188a5935590f9c043fd7f879",
    MELEE / "if/ifnametag.c": "5b1975b769609ed6bf43cd324056faaf84cd25a0bd7c41d8936974e6e383a225",
    MELEE / "lb/lbbgflash.c": "9739385802bd101f778e8bbead41cf211d1362271b238674d7f33568b9778848",
    MELEE / "gr/ground.c": "302f2542da48d7aef4d4ee637b2f375e834feb82a36e2f08caae4e810ec4bdc2",
    BASELIB / "gobjplink.c": "0dd495c0dc5ea268e2f9b4a0932c6b2179e25526dc81593367831f4e3c52089e",
    BASELIB / "gobjgxlink.c": "b6c199614c8e5bca821a0e448a28b541947d77ea2dcfc9a517a5810e20b55bfb",
    BASELIB / "lobj.c": "500352c3a6bf1ff1ddfc4ce79a51775fdd999b5a44bd5cc2aa3c3d29c8e024ca",
    ROOT / "patches/melee-gameplay.patch": "96b284fffada9c553ecf6e4a4ca8f8690002d243bd74f009c8cae0f9049af1e6",
    PROFILE / "effect_matrix.c": "928edfb6524bc45fbfba2393bda1c765069352d2ea53587112f97a08749ba570",
}

PARTICLE_SOURCE_DIAG_PREFIX = "MELEE_PARTICLE_SOURCE_DIAG_V1"


def particle_source_diagnostics(output):
    groups = {}
    current = None
    for line in output.splitlines():
        if not line.startswith(PARTICLE_SOURCE_DIAG_PREFIX + " "):
            continue
        fields = {}
        for item in line.split()[1:]:
            if "=" in item:
                key, value = item.split("=", 1)
                fields[key] = value
        if fields.get("event") == "begin":
            current = int(fields["world"])
            if current in groups:
                raise AssertionError(f"duplicate particle diagnostic for world {current}")
            groups[current] = [fields]
        elif current is not None:
            groups[current].append(fields)
            if fields.get("event") == "end":
                current = None
    return groups

STUBS = {
    "Runtime/platform.h": r'''#ifndef FIXTURE_PLATFORM_H
#define FIXTURE_PLATFORM_H
typedef void (*Event)(void);
typedef unsigned char u8;
typedef unsigned int u32;
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
#define HSD_OBJECT_METHOD(o) (o->parent.parent.class_info)
#endif
''',
    "sysdolphin/baselib/lobj.h": r'''#ifndef FIXTURE_LOBJ_H
#define FIXTURE_LOBJ_H
#include "object.h"
struct HSD_CObj;
typedef struct HSD_LObj { HSD_Obj parent; } HSD_LObj;
typedef struct HSD_LObjInfo { HSD_ClassInfo parent; } HSD_LObjInfo;
extern HSD_LObjInfo hsdLobj;
void HSD_LObj_803668EC(HSD_LObj*);
void HSD_LObjSetupInit(struct HSD_CObj*);
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
#define HSD_GOBJ_CLASS_CAMERA 0x13
#define HSD_GOBJ_OBJ_NONE 255
#define HSD_GOBJ_USER_DATA_NONE 255
extern HSD_GObj* HSD_GObj_804D7814;
extern HSD_GObj* HSD_GObj_804D7818;
extern HSD_GObj** HSD_GObjGXLinkHead;
extern HSD_GObj** HSD_GObj_804D7820;
extern uint8_t HSD_GObj_CameraKind;
extern uint8_t HSD_GObj_JObjKind;
extern uint8_t HSD_GObj_LightKind;
typedef struct { uint8_t gx_link_max; } HSD_GObjLibInitDataType;
extern HSD_GObjLibInitDataType HSD_GObjLibInitData;
void HSD_GObj_80390ED0(HSD_GObj*, uint32_t);
void HSD_GObj_803910D8(HSD_GObj*, int);
void HSD_GObj_80390FC0(void);
void HSD_GObj_JObjCallback(HSD_GObj*, int);
void HSD_GObj_LObjCallback(HSD_GObj*, int);
void GObj_SetupGXLinkMax(HSD_GObj*, GObj_RenderFunc, uint32_t);
uint32_t HSD_GObj_80390EB8(int);
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
HSD_CObj* HSD_CObjGetCurrent(void);
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
void HSD_JObjDispAll(HSD_JObj*, void*, uint32_t, uint32_t);
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
typedef struct HSD_TObj { HSD_Obj parent; struct HSD_TObj *next; } HSD_TObj;
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
void ifMagnify_802FB8C0(HSD_GObj*, int);
#endif
''',
    "melee/if/ifall.h": r'''#ifndef FIXTURE_IFALL_H
#define FIXTURE_IFALL_H
#include <stdbool.h>
#include <sysdolphin/baselib/gobj.h>
bool ifAll_IsHUDHidden(void);
#endif
''',
    "melee/if/ifstatus.h": "#include <sysdolphin/baselib/gobj.h>\nvoid ifStatus_802F5DE0(HSD_GObj*, int);\nvoid ifStatus_802F5E50(HSD_GObj*, int);\n",
    "melee/if/if_2F72.h": "#include <sysdolphin/baselib/gobj.h>\nvoid fn_802F770C(HSD_GObj*, int);\n",
    "melee/if/if_2FD9.h": "#include <sysdolphin/baselib/gobj.h>\nvoid fn_802FDA4C(HSD_GObj*, int);\n",
    "melee/if/ifstock.h": "#include <sysdolphin/baselib/gobj.h>\n",
    "melee/if/iftime.h": "#include <sysdolphin/baselib/gobj.h>\n",
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
    ifall = (MELEE / "if/ifall.c").read_text(encoding="utf-8")
    textdraw = (MELEE / "if/textdraw.c").read_text(encoding="utf-8")
    ifcoget = (MELEE / "if/ifcoget.c").read_text(encoding="utf-8")
    nametag = (MELEE / "if/ifnametag.c").read_text(encoding="utf-8")
    gxlink = (BASELIB / "gobjgxlink.c").read_text(encoding="utf-8")
    gameplay_patch = (ROOT / "patches/melee-gameplay.patch").read_text(encoding="utf-8")
    profile_patch = (PROFILE / "patches/melee-profile-hooks.patch").read_text(encoding="utf-8")
    ground_source = (MELEE / "gr/ground.c").read_text(encoding="utf-8")
    flash_source = (MELEE / "lb/lbbgflash.c").read_text(encoding="utf-8")

    def added_patch_function(source: str, marker: str) -> str:
        body = extract_function(source, marker)
        return "\n".join(line[1:] if line.startswith("+") else line
                          for line in body.splitlines())

    def source_registration(source: str, marker: str, old_owner: str,
                            new_owner: str) -> str:
        line = next(line.strip() for line in source.splitlines() if marker in line)
        return line.replace(old_owner, new_owner)

    def added_patch_registration(source: str, marker: str, old_owner: str,
                                 new_owner: str) -> str:
        line = next(line[1:].strip() for line in source.splitlines()
                    if line.startswith("+") and marker in line)
        return line.replace(old_owner, new_owner)

    hud_storage_start = ifall.index("static struct ifAll_804A0FD8_t {")
    hud_storage_end = ifall.index("\n\n", ifall.index("static u8 hidden;", hud_storage_start))
    nametag_owner = next(line for line in nametag.splitlines()
                         if "static HSD_GObj* un_804D6D68;" in line)
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
                "void HSD_GObj_LObjCallback(HSD_GObj* gobj, int unused)",
                "void HSD_GObj_JObjCallback(HSD_GObj* gobj, int arg1)",
            )
        ),
        "ACTUAL_DEVTEXT_SOURCE": "\n\n".join([
            extract_function(textdraw, "HSD_GObj* DevText_GetGObj(void)"),
            extract_function(textdraw, "void DevText_DrawAll(HSD_GObj* gobj, int pass)"),
            extract_function(textdraw, "void DevText_CreateCObj(int classifier,")
                .replace("    HSD_GObj* gobj = GObj_Create(classifier, p_link, gobj_priority);",
                         "    if (!slippi_particle_source_devtext_constructing(classifier, p_link, gobj_priority, gx_link, gx_priority)) __builtin_trap();\n"
                         "    HSD_GObj* gobj = GObj_Create(classifier, p_link, gobj_priority);")
                .replace("            gobj->gxlink_prios = 1LL << gx_link;",
                         "            gobj->gxlink_prios = 1LL << gx_link;\n"
                         "            if (!slippi_particle_source_devtext_created(gobj)) __builtin_trap();"),
        ]),
        "ACTUAL_GOBJ_RETIRE": extract_function(
            (BASELIB / "gobjplink.c").read_text(encoding="utf-8"),
            "void HSD_GObjPLink_80390228(HSD_GObj* gobj)",
        ).replace("    GObj_RemoveUserData(gobj);",
                  "    if (!slippi_particle_source_gobj_retiring(gobj)) __builtin_trap();\n"
                  "    GObj_RemoveUserData(gobj);"),
        "ACTUAL_GXLINK_SOURCE": "\n\n".join(
            extract_function(gxlink, marker)
            for marker in (
                "void GObj_GXReorder(HSD_GObj* gobj, HSD_GObj* hiprio_gobj)",
                "void GObj_SetupGXLink(HSD_GObj* gobj, GObj_RenderFunc render_cb,",
                "void GObj_SetupGXLinkMax(HSD_GObj* gobj, GObj_RenderFunc render_cb,",
            )
        ),
        "ACTUAL_EFLIB_SOURCE": extract_function(
            eflib, "void efLib_render_callback(HSD_GObj* gobj, int code)"
        ),
        "ACTUAL_HUD_STORAGE": ifall[hud_storage_start:hud_storage_end],
        "ACTUAL_HUD_HIDDEN": extract_function(ifall, "bool ifAll_IsHUDHidden(void)"),
        "ACTUAL_HUD_CALLBACK": extract_function(
            ifall, "static void fn_802F36B8(HSD_GObj* gobj, int unused)"
        ),
        "ACTUAL_NAMETAG_OWNER": nametag_owner,
        "ACTUAL_NAMETAG_CALLBACK": extract_function(
            nametag, "void fn_802FCAC4(HSD_GObj* gobj, int pass)"
        ),
        "ACTUAL_MAGNIFY_WRAPPER": added_patch_function(
            gameplay_patch,
            "static void melee_web_magnify_camera(HSD_GObj* gobj, int pass)",
        ),
        "ACTUAL_HUD_BRIDGE": added_patch_function(
            profile_patch,
            "void slippi_particle_source_hud_identity(HSD_GObj** camera,",
        ),
        "ACTUAL_NAMETAG_BRIDGE": added_patch_function(
            profile_patch,
            "void slippi_particle_source_nametag_identity(HSD_GObj** camera,",
        ),
        "ACTUAL_MAGNIFY_BRIDGE": added_patch_function(
            profile_patch,
            "GObj_RenderFunc slippi_particle_source_magnify_callback(void)",
        ),
        "ACTUAL_MAIN_BRIDGE": added_patch_function(
            profile_patch,
            "+void slippi_particle_source_main_camera_identity(",
        ),
        "ACTUAL_FLASH_OWNER": added_patch_function(
            gameplay_patch,
            "+HSD_GObj* melee_web_bg_flash_camera_owner(void)",
        ),
        "SOURCE_REGISTER_MAIN": source_registration(
            camera, "GObj_SetupGXLinkMax(gobj, fn_800301D0, 2);",
            "gobj", "&main_camera",
        ),
        "SOURCE_REGISTER_GROUND": source_registration(
            ground_source,
            "GObj_SetupGXLinkMax(new_var2, &grDisplay_801C5F60, 5);",
            "new_var2", "&ground_camera",
        ),
        "SOURCE_REGISTER_MAGNIFY": added_patch_registration(
            gameplay_patch,
            "GObj_SetupGXLinkMax(gobj, melee_web_magnify_camera, 0);",
            "gobj", "&magnify_camera",
        ),
        "SOURCE_REGISTER_FLASH": source_registration(
            flash_source,
            "GObj_SetupGXLinkMax(*gobj1_slot, HSD_GObj_803910D8, 0xa);",
            "*gobj1_slot", "&flash_camera",
        ),
        "SOURCE_REGISTER_NAMETAG": source_registration(
            nametag, "GObj_SetupGXLinkMax(gobj, fn_802FCAC4, 6);",
            "gobj", "&nametag_camera",
        ),
        "SOURCE_REGISTER_IFCOGET": source_registration(
            ifcoget, "GObj_SetupGXLinkMax(gobj_camera, HSD_GObj_803910D8, 9);",
            "gobj_camera", "&ifcoget_camera",
        ),
        "SOURCE_REGISTER_HUD": source_registration(
            ifall, "GObj_SetupGXLinkMax(gobj, fn_802F36B8, 8);",
            "gobj", "&hud_camera",
        ),
    }
    fixture = (PROFILE / "tests/particle_pass_source_control.c.in").read_text(
        encoding="utf-8"
    )
    for name, body in snippets.items():
        fixture = fixture.replace(name, body)
    if "ACTUAL_" in fixture or "SOURCE_REGISTER_" in fixture:
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
        ifall = (MELEE / "if/ifall.c").read_text(encoding="utf-8")
        hud_setup = extract_function(ifall, "void ifAll_802F390C(void)")
        self.assertIn("GObj_SetupGXLinkMax(gobj, fn_802F36B8, 8);", hud_setup)
        self.assertIn("gobj->gxlink_prios = 0xD00;", hud_setup)
        nametag_source = (MELEE / "if/ifnametag.c").read_text(encoding="utf-8")
        nametag_setup = extract_function(nametag_source, "void un_802FD4C8(void)")
        self.assertIn("GObj_SetupGXLinkMax(gobj, fn_802FCAC4, 6);", nametag_setup)
        self.assertIn("gobj->gxlink_prios = 0x200;", nametag_setup)
        gameplay_patch = (ROOT / "patches/melee-gameplay.patch").read_text(encoding="utf-8")
        self.assertIn("GObj_SetupGXLinkMax(gobj, melee_web_magnify_camera, 0);",
                      gameplay_patch)
        flash_source = (MELEE / "lb/lbbgflash.c").read_text(encoding="utf-8")
        flash_setup = extract_function(flash_source, "void lbBgFlash_800209F4(void)")
        self.assertIn("GObj_SetupGXLinkMax(*gobj1_slot, HSD_GObj_803910D8, 0xa);",
                      flash_setup)
        self.assertIn("(*gobj1_slot)->gxlink_prios = 0x10000;", flash_setup)
        ground_setup = extract_function(
            (MELEE / "gr/ground.c").read_text(encoding="utf-8"),
            "Ground_GObj* Ground_GetStageGObj(int map_id)",
        )
        self.assertIn("GObj_SetupGXLinkMax(new_var2, &grDisplay_801C5F60, 5);",
                      ground_setup)
        self.assertIn("temp_r23_2->gxlink_prios = 8;", ground_setup)
        profile_patch = (PROFILE / "patches/melee-profile-hooks.patch").read_text(encoding="utf-8")
        self.assertIn("*camera = ifAll_804A0FD8.gobj;", profile_patch)
        ifcoget = extract_function(
            (MELEE / "if/ifcoget.c").read_text(encoding="utf-8"),
            "void un_802FEFAC(void)")
        self.assertIn("GObj_Create(HSD_GOBJ_CLASS_CAMERA, 21, 0)", ifcoget)
        self.assertIn("gobj_camera->gxlink_prios = 0x8400;", ifcoget)
        self.assertIn("GObj_SetupGXLink(gobj_light, HSD_GObj_LObjCallback, 10, 0);", ifcoget)
        self.assertIn("GObj_SetupGXLink(gobj_ui, HSD_GObj_JObjCallback, 15, 0);", ifcoget)
        # Hooks are private/additive; constructor before-store refusal and actual
        # destruction after deferred return precede original source release.
        part = profile_patch.split("diff --git a/src/melee/if/ifcoget.c", 1)[1]
        self.assertLess(part.index("slippi_particle_source_ifcoget_constructing"),
                        part.index("gobj_camera = GObj_Create"))
        self.assertLess(part.index("gobj_camera->gxlink_prios = 0x8400"),
                        part.index("slippi_particle_source_ifcoget_created"))
        retire = profile_patch.split("diff --git a/src/sysdolphin/baselib/gobjplink.c", 1)[1]
        self.assertLess(retire.index("slippi_particle_source_gobj_retiring"),
                        retire.index("GObj_RemoveUserData(gobj)"))
        original_retire = extract_function(
            (BASELIB / "gobjplink.c").read_text(encoding="utf-8"),
            "void HSD_GObjPLink_80390228(HSD_GObj* gobj)")
        self.assertLess(original_retire.index("return;"),
                        original_retire.index("GObj_RemoveUserData(gobj)"))
        devtext = extract_function(
            (MELEE / "if/textdraw.c").read_text(encoding="utf-8"),
            "void DevText_CreateCObj(int classifier,")
        part = profile_patch.split("diff --git a/src/melee/if/textdraw.c", 1)[1]
        self.assertLess(part.index("slippi_particle_source_devtext_constructing"),
                        part.index("HSD_GObj* gobj = GObj_Create"))
        self.assertLess(part.index("gobj->gxlink_prios = 1LL << gx_link"),
                        part.index("slippi_particle_source_devtext_created"))
        self.assertIn("GObj_SetupGXLinkMax(gobj, HSD_GObj_803910D8, gx_priority);", devtext)
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
                "-DMELEE_WEB_CPU_INTERVAL_ENABLED=1",
                "-DMELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET=12288",
                "-I" + str(include),
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
                PROFILE / "particle_presentation.h",
                PROFILE / "particle_source_schedule.c",
                PROFILE / "particle_source_schedule.h",
                ROOT / "src/gameplay_cpu_interval.h",
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
            diagnostics = particle_source_diagnostics(result.stdout)
            self.assertEqual({102, 105, 109, 110, 200, 202, 303}, set(diagnostics))
            for world, rows in diagnostics.items():
                block_bytes = sum(len(PARTICLE_SOURCE_DIAG_PREFIX + " " +
                                      " ".join(f"{key}={value}" for key, value in row.items())) + 1
                                  for row in rows)
                self.assertLessEqual(block_bytes, 12288, f"world {world} exceeded output cap")
                end = next(row for row in rows if row.get("event") == "end")
                if world not in (109, 110):
                    self.assertEqual("1", end["complete"])

            complete = diagnostics[105]
            begin = complete[0]
            self.assertEqual("bind-camera-refusal", begin["boundary"])
            self.assertEqual("105", begin["world"])
            camera_rows = [row for row in complete if row.get("event") == "camera"]
            self.assertEqual(8, len(camera_rows))
            self.assertEqual("7", camera_rows[-1]["idx"])
            self.assertTrue({"owner", "cobj", "classifier", "p_link", "p_priority",
                             "gx_link", "render_priority", "obj_kind", "mask",
                             "render_entry", "process_gobj", "process_entry"}
                            .issubset(camera_rows[-1]))
            complete_end = next(row for row in complete if row.get("event") == "end")
            self.assertEqual("1", complete_end["complete"])
            self.assertEqual("0", complete_end["overflow"])
            self.assertEqual("0", complete_end["invalid_chain"])
            self.assertEqual("0x0000000000038f90", complete_end["gx_union"])
            gx_rows = [row for row in complete if row.get("event") == "gx-link"]
            self.assertEqual(9, len(gx_rows))
            self.assertEqual({4, 7, 8, 9, 10, 11, 15, 16, 17},
                             {int(row["link"]) for row in gx_rows})
            self.assertTrue(all({"owner", "classifier", "obj_kind", "p_link",
                                 "p_priority", "render_priority", "attached",
                                 "render_entry", "process_gobj", "process_child",
                                 "process_link", "process_entry"}.issubset(row)
                                for row in complete if row.get("event") == "gx-node"))
            self.assertTrue(all(row["topology_complete"] == "1" and
                                row["records_complete"] == "1"
                                for row in gx_rows))

            invalid = diagnostics[109]
            self.assertEqual("109", invalid[0]["world"])
            invalid_end = next(row for row in invalid if row.get("event") == "end")
            self.assertEqual("0", invalid_end["complete"])
            self.assertEqual("1", invalid_end["invalid_chain"])
            invalid_gx17 = next(row for row in invalid
                                if row.get("event") == "gx-link" and row.get("link") == "17")
            self.assertEqual("2", invalid_gx17["visited"])
            self.assertEqual("0", invalid_gx17["tail_reached"])
            self.assertEqual("0", invalid_gx17["topology_complete"])

            truncated = diagnostics[110]
            self.assertEqual("110", truncated[0]["world"])
            truncated_end = next(row for row in truncated if row.get("event") == "end")
            self.assertEqual("0", truncated_end["complete"])
            self.assertEqual("1", truncated_end["overflow"])
            self.assertEqual("0", truncated_end["invalid_chain"])
            truncated_gx17 = next(row for row in truncated
                                  if row.get("event") == "gx-link" and row.get("link") == "17")
            self.assertEqual("129", truncated_gx17["visited"])
            self.assertEqual("1", truncated_gx17["tail_reached"])
            self.assertEqual("1", truncated_gx17["topology_complete"])
            self.assertEqual("0", truncated_gx17["records_complete"])
            first = subprocess.run([str(scratch / "control"), "first-error"],
                                   cwd=scratch, capture_output=True, text=True, timeout=15)
            (scratch / "first-error.log").write_text(first.stdout + first.stderr)
            self.assertEqual(0, first.returncode, first.stdout + first.stderr)
            records = [line for line in first.stderr.splitlines()
                       if line.startswith("Particle schedule first failure:")]
            self.assertEqual(["Particle schedule first failure: Unexpected source camera end"], records)
            self.assertIn("source first-error preservation control passed", first.stdout)
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
