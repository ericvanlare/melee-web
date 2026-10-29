# SPDX-License-Identifier: GPL-3.0-or-later
"""Projection view control plus exact pinned WObj/CObj setter leaves; no runtime claim."""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[3]
MELEE = ROOT / '.deps/melee'

def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if not depth:
            return source[start:i+1]
    raise AssertionError(signature)

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
 uint32_t flags,mtxid; Mtx mtx; HSD_ImageDesc* imagedesc;
} HSD_TObj;
typedef struct HSD_SList { struct HSD_SList* next; void* data; } HSD_SList;
typedef struct HSD_Shadow {
 HSD_SList* objects; HSD_CObj* camera; HSD_TObj* texture;
 float scaleS,scaleT,transS,transT; bool active; unsigned char intensity;
} HSD_Shadow;
typedef struct LbShadow {
 unsigned char x0_b0:1,x0_b1:1,x0_b2:1,x0_b3:1,x0_b4:1,x0_b5:1,x0_b6:1,x0_b7:1;
 HSD_Shadow* shadow;
} LbShadow;
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
 int x2229_b3,x2220_b7; CmSubject* x890_cameraBox; } Fighter;
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
#endif
''')
            (stubs/'sysdolphin/baselib/gobjplink.h').write_text('#include "gobj.h"\n')
            ft=(MELEE/'src/melee/ft/ftlib.c').read_text()
            camera=(MELEE/'src/melee/cm/camera.c').read_text()
            cobj=(MELEE/'src/sysdolphin/baselib/cobj.c').read_text()
            wobj=(MELEE/'src/sysdolphin/baselib/wobj.c').read_text()
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
                function(ft,'bool ftLib_80086A8C('),
                function(ft,'bool ftLib_80086A58('),
                function(ft,'bool ftLib_80086B64(')])
            fixture=(ROOT/'src/slippi_profile/tests/fighter_projection_control.c.in').read_text()
            (scratch/'control.c').write_text(fixture.replace('ORIGINAL_FUNCTIONS',leaves))
            cmd=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                 '-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1','-I'+str(stubs),
                 '-I'+str(ROOT/'src'),str(ROOT/'src/slippi_profile/fighter_projection.c'),
                 str(scratch/'control.c'),'-o',str(scratch/'control')]
            for name,command in [('compile',cmd),('run',[str(scratch/'control')])]:
                with (scratch/(name+'.stdout')).open('w') as out, (scratch/(name+'.stderr')).open('w') as err:
                    subprocess.run(command,check=True,stdout=out,stderr=err)
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
