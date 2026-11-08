# SPDX-License-Identifier: GPL-3.0-or-later
"""Projection view control plus exact pinned WObj/CObj setter leaves; no runtime claim."""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest
import sys
ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / '.deps/melee'
sys.path.insert(0, str(ROOT / 'scripts'))
from gameplay_sources import prepare_sources

def function(source, signature, *, last=False):
    start = source.rindex(signature) if last else source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if not depth:
            return source[start:i+1]
    raise AssertionError(signature)

def shadow_selector_members(source):
    """Extract the original per-Fighter selector/active block only.

    The surrounding source callback performs camera and GX work unrelated to
    this control. Keeping the exact selection block avoids reimplementing the
    authored predicates while keeping the fixture on the shadow boundary.
    """
    body=function(source,'void lbShadow_8000F38C(')
    start=body.index('        for (gobj = HSD_GObj_Entities->fighters;')
    end=body.index('            if (fp->x20A4.shadow != NULL && cm != NULL',start)
    selected=body[start:end]
    selected=selected.replace('CmSubject* cm = fp->x890_cameraBox;',
                              'CmSubject* cm = fp->x890_cameraBox; (void)cm;')
    for marker in ['HSD_ShadowDeleteObject(fp2->x20A4.shadow, NULL);',
                   'fp2->x21FC_flag.b7','fp2->x20A0_accessory',
                   'HSD_ShadowSetActive(lbs->shadow, 1);']:
        if marker not in selected:
            raise AssertionError(f'original selector block lacks {marker}')
    # The original only casts a 0/1 pointer sentinel to s32. Keep the source
    # expression in the host control and suppress that one portability warning.
    return ('#pragma GCC diagnostic push\n'
            '#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"\n'
            'static void original_shadow_selector_members(void) {\n'
            '    HSD_GObj* gobj; HSD_LObj* lobj; int noLight=0;\n'
            +selected+'    }\n}\n#pragma GCC diagnostic pop')

class FighterProjectionTests(unittest.TestCase):
    def test_actual_view_and_original_projection_branches(self):
        if not (MELEE/'src/melee/ft/ftlib.c').is_file():
            self.skipTest('pinned original source unavailable')
        scratch = Path(tempfile.mkdtemp(prefix='fighter-projection-',
                      dir=ROOT/'work'))
        try:
            stubs = scratch/'include'
            for name in ['melee/ft','sysdolphin/baselib']:
                (stubs/name).mkdir(parents=True,exist_ok=True)
            (stubs/'dolphin').mkdir(parents=True,exist_ok=True)
            (stubs/'dolphin/pad.h').write_text('''
#ifndef FIXTURE_DOLPHIN_PAD
#define FIXTURE_DOLPHIN_PAD
void PADControlMotor(unsigned int, unsigned int);
#endif
''')
            (stubs/'profile_shadow_types.h').write_text('''
#ifndef FIXTURE_SHADOW_TYPES
#define FIXTURE_SHADOW_TYPES
#include <stdint.h>
#include <stdbool.h>
typedef struct { float x,y,z; } Vec3;
typedef float Mtx[3][4];
typedef int GXTexFmt;
typedef struct { float xmin,xmax,ymin,ymax; } HSD_RectF32;
typedef struct { uint16_t left,right,top,bottom; } Scissor;
typedef struct HSD_WObj { uint32_t flags; Vec3 pos; } HSD_WObj;
typedef struct HSD_CObj {
 uint32_t flags; HSD_RectF32 viewport; Scissor scissor;
 HSD_WObj* eyepos; HSD_WObj* interest;
 union { float roll; Vec3 up; } u;
 float near,far;
 union { struct { float fov,aspect; } perspective;
         struct { float top,bottom,left,right; } frustum;
         struct { float top,bottom,left,right; } ortho; } projection_param;
 unsigned char projection_type; Mtx view_mtx; Mtx* proj_mtx;
} HSD_CObj;
typedef struct HSD_ImageDesc {
 void* image_ptr; uint16_t width,height; GXTexFmt format; uint32_t mipmap;
} HSD_ImageDesc;
typedef struct HSD_TObj {
 struct HSD_TObj* next; uint32_t flags,mtxid; Mtx mtx; HSD_ImageDesc* imagedesc;
} HSD_TObj;
typedef struct HSD_SList { struct HSD_SList* next; void* data; } HSD_SList;
typedef struct HSD_JObj { int refs; } HSD_JObj;
typedef struct HSD_Shadow {
 HSD_SList* objects; HSD_CObj* camera; HSD_TObj* texture;
 float scaleS,scaleT,transS,transT; bool active; unsigned char intensity;
} HSD_Shadow;
typedef struct LbShadow {
 unsigned char x0_b0:1,x0_b1:1,x0_b2:1,x0_b3:1,x0_b4:1,x0_b5:1,x0_b6:1,x0_b7:1;
 HSD_Shadow* shadow;
} LbShadow;
typedef struct HSD_Material {
 void *ambient,*diffuse,*specular; float alpha,shininess;
} HSD_Material;
typedef struct HSD_TExp HSD_TExp;
typedef struct HSD_MObj {
 uint32_t rendermode; HSD_Material* mat; HSD_TObj* tobj;
 HSD_TExp *tevdesc,*texp; void *pe;
} HSD_MObj;
typedef struct Ground Ground;
typedef struct HSD_LObj HSD_LObj;
#define TEX_COORD_MASK 0x0fu
#define TEX_COORD_SHADOW 3u
#define TEX_MTX_DIRTY (1u << 31)
#define PROJ_ORTHO 3
#define tobj_coord(t) ((t)->flags & TEX_COORD_MASK)
#endif
''')
            (stubs/'melee/ft/types.h').write_text('''
#ifndef FIXTURE_TYPES
#define FIXTURE_TYPES
#include <stdint.h>
#include <stdbool.h>
#include "profile_shadow_types.h"
typedef struct { int32_t x,y; } S32Vec2;
typedef struct { Vec3 bone_pos; } CmSubject;
typedef struct Fighter { S32Vec2 x2188; unsigned char x221F_b0:1;
 unsigned char neighbor:1; unsigned char other:6; LbShadow x20A4;
 int x2229_b3,x2220_b7; CmSubject* x890_cameraBox;
 struct { unsigned b7:1; } x21FC_flag;
 int invisible,x221E_b5;
 struct { void* xC[2]; } x5AC;
 void* x20A0_accessory; } Fighter;
#endif
''')
            for header in ['cobj.h','list.h','shadow.h','tobj.h','wobj.h']:
                (stubs/'sysdolphin/baselib'/header).write_text(
                    '#include <melee/ft/types.h>\n')
            (stubs/'sysdolphin/baselib/gobj.h').write_text('''
#ifndef FIXTURE_GOBJ
#define FIXTURE_GOBJ
#include <stddef.h>
typedef struct HSD_GObj { struct HSD_GObj* next; void* user_data; void* hsd_obj; } HSD_GObj;
typedef struct { HSD_GObj* fighters; } FixtureEntities;
extern FixtureEntities* HSD_GObj_Entities;
extern HSD_GObj* HSD_GObj_804D7814;
#endif
''')
            (stubs/'sysdolphin/baselib/gobjplink.h').write_text('#include "gobj.h"\n')
            ft=(MELEE/'src/melee/ft/ftlib.c').read_text()
            camera=(MELEE/'src/melee/cm/camera.c').read_text()
            cobj=(MELEE/'src/sysdolphin/baselib/cobj.c').read_text()
            wobj=(MELEE/'src/sysdolphin/baselib/wobj.c').read_text()
            # Prepare the exact composed profile source tree. This exercises
            # the original selector and HSD/MObj consumers with the reviewed
            # hook patch applied; pristine .deps sources are used only by the
            # existing scalar-leaf controls above.
            profile_src=Path(prepare_sources(ROOT,slippi_profile=True))
            lbshadow=(profile_src/'melee/lb/lbshadow.c').read_text()
            shadow=(profile_src/'sysdolphin/baselib/shadow.c').read_text()
            mobj=(profile_src/'sysdolphin/baselib/mobj.c').read_text()
            grmaterial=(profile_src/'melee/gr/grmaterial.c').read_text()
            leaves='\n'.join([function(wobj,'void HSD_WObjSetPosition('),
                function(cobj,'HSD_WObj* HSD_CObjGetInterestWObj('),
                function(cobj,'HSD_WObj* HSD_CObjGetEyePositionWObj('),
                function(cobj,'void HSD_CObjSetInterest('),
                function(cobj,'void HSD_CObjSetEyePosition('),
                function(cobj,'void HSD_CObjSetMtxDirty('),
                function(cobj,'void HSD_CObjSetNear('),
                function(cobj,'void HSD_CObjSetFar('),
                function(cobj,'void HSD_CObjSetScissorx4('),
                function(cobj,'void HSD_CObjSetViewportfx4('),
                function(cobj,'void HSD_CObjSetOrtho('),
                function(camera,'bool Camera_80030BBC('),
                function(camera,'bool Camera_80030CD8('),
                function((profile_src/'melee/ft/ftlib.c').read_text(),'melee_source_bool ftLib_80086A8C('),
                function(ft,'bool ftLib_80086A58('),
                function(ft,'bool ftLib_80086B64(')])
            shadow_functions='\n'.join([
                function(lbshadow,'void lbShadow_8000EEE0('),
                function(shadow,'void HSD_ShadowDeleteObject('),
                function(shadow,'void HSD_ShadowAddObject('),
                function(shadow,'void HSD_ShadowSetActive('),
                function(mobj,'void HSD_MObjAddShadowTexture('),
                function(mobj,'void HSD_MObjDeleteShadowTexture(')])
            selector_members=shadow_selector_members(lbshadow)
            mobj_functions='\n'.join([
                function(mobj,'void HSD_MObjSetup('),
                function(mobj,'void HSD_MObjCompileTev(')])
            material_consumer=function(grmaterial,'static void fn_801C8EF8(',last=True)
            direct_consumers=[material_consumer,
                function((profile_src/'melee/ft/ftmaterial.c').read_text(),'void ftMaterial_800BF2B8(',last=True),
                function((profile_src/'melee/it/itmaterial.c').read_text(),'void fn_80277D8C(',last=True)]
            for body in direct_consumers:
                self.assertIn('slippi_fighter_projection_mobj_preflight',body)
                self.assertIn('slippi_fighter_projection_mobj_toon_link',body)
            # The original ground setup callback's second parameter is unused.
            material_consumer=('#pragma GCC diagnostic push\n'
                '#pragma GCC diagnostic ignored "-Wunused-parameter"\n'
                +material_consumer+'\n#pragma GCC diagnostic pop')
            for body, hook in [(shadow_functions,'slippi_fighter_projection_shadow_members'),
                               (shadow_functions,'slippi_fighter_projection_shadow_active'),
                               (shadow_functions,'slippi_fighter_projection_shadow_texture_operation'),
                               (mobj_functions,'slippi_fighter_projection_mobj_compile'),
                               (mobj_functions,'slippi_fighter_projection_mobj_toon_link'),
                               (mobj_functions,'slippi_fighter_projection_mobj_preflight')]:
                self.assertIn(hook,body,f'prepared source lacks {hook}')
            fixture=(ROOT/'src/slippi_profile/tests/fighter_projection_control.c.in').read_text()
            fixture=fixture.replace('ORIGINAL_FUNCTIONS',leaves)
            fixture=fixture.replace('ORIGINAL_SHADOW_FUNCTIONS',shadow_functions)
            fixture=fixture.replace('ORIGINAL_SELECTOR_MEMBERS',selector_members)
            fixture=fixture.replace('ORIGINAL_MOBJ_FUNCTIONS',mobj_functions)
            fixture=fixture.replace('ORIGINAL_MOBJ_CONSUMER',material_consumer)
            results=(ROOT/'src/gameplay_results_context.c').read_text()
            fixture=fixture.replace('ACTUAL_RESULTS_DRAW',
                function(results,'int melee_web_results_context_draw('))
            (scratch/'control.c').write_text(fixture)
            # Exercise the actual draw owner with a modeled private-arm latch.
            # The separate camera reducer exercises the real arming function.
            projection=scratch/'projection.c'
            projection.write_text(
                '#include "fighter_projection.c"\n'
                'void fixture_enable_private_draw(void) { private_armed=1; }\n')

            cmd=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                 '-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1',
                 '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1',
                 '-DMELEE_WEB_FIGHTER_PROJECTION_SOURCE_CONTROLS=1','-I'+str(stubs),
                 '-I'+str(ROOT/'src'),str(ROOT/'src/gameplay_cpu_interval.c'),
                 '-I'+str(ROOT/'src/slippi_profile'),str(projection),
                 str(scratch/'control.c'),'-o',str(scratch/'control')]
            for name,command in [('compile',cmd),('run',[str(scratch/'control')])]:
                with (scratch/(name+'.stdout')).open('w') as out, (scratch/(name+'.stderr')).open('w') as err:
                    subprocess.run(command,check=True,stdout=out,stderr=err)
            for mode in ['results-fighter-draw','private-canonical-carry','active-topology','failed-active','active-rebuilt','inactive-rebuilt',
                         'active-order','arm-mid-draw','disarm-mid-draw','tuple-active',
                         'image-active','inverse-active','source-root','source-accessory',
                         'source-dedup','source-visibility','source-late-consumer',
                         'source-texture-alias','source-setup-toon','source-toon-alias',
                         'source-raw-active','source-compile-refusal','source-compile-ordinary',
                         'source-refuse-foreign','source-refuse-owner',
                         'source-refuse-image','source-unowned-texture',
                         'source-inactive-texture-link','source-refuse-initial-owner']:
                result=subprocess.run([str(scratch/'control'),mode],capture_output=True,
                                      text=True,timeout=30)
                (scratch/(mode+'.log')).write_text(result.stdout+result.stderr)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        except BaseException:
            print('Retained Fighter projection control:',scratch,flush=True)
            raise
        else:
            shutil.rmtree(scratch)

    def test_actual_provenance_receipt_and_checked_restore_clear(self):
        scratch = Path(tempfile.mkdtemp(prefix='fighter-provenance-',dir=ROOT/'work'))
        try:
            source=(ROOT/'src/gameplay_source_memory_runtime.cpp').read_text()
            actual='\n'.join(function(source,'extern "C" int '+name+'(') for name in
                ['melee_web_source_memory_fighter_projection_identity',
                 'melee_web_source_memory_fighter_projection_matches'])
            template=(ROOT/'src/slippi_profile/tests/fighter_projection_provenance_control.cpp.in').read_text()
            fixture=scratch/'control.cpp'
            fixture.write_text(template.replace('ACTUAL_PROVENANCE_FUNCTIONS',actual))
            cmd=[os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror',
                 '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-I'+str(ROOT/'src'),
                 str(fixture),'-o',str(scratch/'control')]
            for name,command in [('compile',cmd),('run',[str(scratch/'control')])]:
                with (scratch/(name+'.stdout')).open('w') as out, (scratch/(name+'.stderr')).open('w') as err:
                    subprocess.run(command,check=True,stdout=out,stderr=err)
        except BaseException:
            print('Retained actual provenance control:',scratch,flush=True)
            raise
        else:
            shutil.rmtree(scratch)

if __name__ == '__main__': unittest.main()
