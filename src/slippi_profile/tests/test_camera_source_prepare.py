# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual source camera/history math with modeled registration and player inputs."""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from .test_texp_stage_scratch import function
ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'tests')]
from check_gameplay import node_runtime
from gameplay_bool import composed_patch
from gameplay_sources import compose_slippi_profile_patch
from owned_test_workspace import OwnedWorkspaceTests

class CameraSourcePrepareTests(OwnedWorkspaceTests):
    def test_original_camera_history_math_and_source_consumer(self):
        prepared = ROOT / 'build/gameplay-source-slippi-profile/src'
        emcc = ROOT / '.deps/emsdk/upstream/emscripten/emcc'
        if not emcc.is_file() or not (prepared / 'melee/cm/types.h').is_file():
            self.skipTest('pinned compiler/prepared source headers unavailable')
        parent = os.environ.get('MELEE_CAMERA_HISTORY_EVIDENCE')
        work = Path(tempfile.mkdtemp(prefix='camera-history-', dir=parent)) if parent else self.new_workspace(ROOT, 'camera-history-')
        self.work = work
        pins = {}
        def read(relative):
            p = prepared / relative
            pins[str(p.relative_to(ROOT))] = hashlib.sha256(p.read_bytes()).hexdigest()
            return p.read_text()
        def extract(text, name):
            match = re.search(r'^[\w \t*]+?\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{', text, re.M)
            if not match:
                raise AssertionError('Actual definition missing: '+name)
            text = text[match.start():]
            body = function(text, name)
            return text[:text.index(body)] + body
        def run(label, argv, cwd=ROOT, env=None):
            result = subprocess.run(list(map(str,argv)), cwd=cwd, capture_output=True, text=True, timeout=120, env=env)
            (work / (label+'.json')).write_text(json.dumps(dict(argv=list(map(str,argv)), cwd=str(cwd), git_ceiling_directories=(env or {}).get('GIT_CEILING_DIRECTORIES'), exit_code=result.returncode, stdout=result.stdout, stderr=result.stderr), indent=2)+'\n')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            return result
        # Materialize the actual composed camera source, including its local identity bridge.
        melee = ROOT / '.deps/melee'
        base = composed_patch(melee, ROOT/'patches/melee-gameplay.patch')
        combined = compose_slippi_profile_patch(melee,base,ROOT/'src/slippi_profile/patches/melee-profile-hooks.patch')
        tree = work/'source'; camera_path = tree/'src/melee/cm/camera.c'
        camera_path.parent.mkdir(parents=True)
        lock=json.loads((ROOT/'dependencies.lock.json').read_text())
        original_camera = subprocess.check_output(
            ['git','show',lock['repositories']['melee']['commit']+':src/melee/cm/camera.c'],cwd=melee)
        camera_path.write_bytes(original_camera)
        # An owned workspace may live below this checkout's work/. Prevent Git
        # from discovering that enclosing repository and silently filtering out
        # camera.c as outside the current directory's repository-relative prefix.
        patch_env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(work.resolve()))
        for key in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR'):
            patch_env.pop(key, None)
        run('patch-check',['git','apply','--check','--verbose','--include=src/melee/cm/camera.c',combined],tree,patch_env)
        run('patch-apply',['git','apply','--verbose','--include=src/melee/cm/camera.c',combined],tree,patch_env)
        camera=camera_path.read_text()
        self.assertNotEqual(camera.encode(), original_camera,
            'isolated patch application must change the locked original camera source')
        self.assertEqual(camera.count('void slippi_camera_source_identity('), 1,
                         'composed source must contain exactly one actual camera identity bridge')
        prepared_camera=read('melee/cm/camera.c')
        for name in ('Camera_8002A4AC','Camera_8002AF68'):
            self.assertEqual(extract(camera,name),extract(prepared_camera,name))
        cobj=read('sysdolphin/baselib/cobj.c'); wobj=read('sysdolphin/baselib/wobj.c')
        # Select the actual build's non-MUST_MATCH branches before brace extraction.
        cobj = re.sub(r'#ifdef MUST_MATCH\n((?:(?!#endif).)*?)#else\n(.*?)#endif', lambda m:m[2], cobj, flags=re.S)
        vector=read('melee/lb/lbvector.c'); nametag=read('melee/if/ifnametag.c')
        gx=ROOT/'.deps/aurora/lib/dolphin/gx/GXTransform.cpp'
        pins[str(gx.relative_to(ROOT))]=hashlib.sha256(gx.read_bytes()).hexdigest()
        pieces=[]
        for name in ['HSD_WObjSetPosition','HSD_WObjGetPosition']:
            pieces.append(extract(wobj,name))
        for name in ['HSD_CObjSetFlags','HSD_CObjClearFlags','HSD_CObjGetInterestWObj','HSD_CObjGetEyePositionWObj','HSD_CObjGetInterest','HSD_CObjSetInterest','HSD_CObjGetEyePosition','HSD_CObjSetEyePosition','HSD_CObjGetEyeVector','vec_get_x','roll2upvec','HSD_CObjGetUpVector','HSD_CObjMtxIsDirty','get_up_vector_for_viewing_mtx_inner','get_up_vector_for_viewing_mtx','HSD_CObjSetupViewingMtx','HSD_CObjGetInvViewingMtxPtrDirect','HSD_CObjGetViewingMtxPtr','HSD_CObjSetFov','HSD_CObjSetNear','HSD_CObjSetFar','HSD_CObjGetProjectionType']:
            pieces.append(extract(cobj,name))
        jobj=read('sysdolphin/baselib/jobj.c')
        pieces += [extract(jobj,name) for name in ['HSD_JObjSetFlags','HSD_JObjClearFlags']]
        for name in ['get_stage_floor_height','Camera_8002AF68','Camera_8002A4AC','Camera_80030A50','slippi_camera_source_identity','slippi_particle_source_main_camera_identity']:
            pieces.append(extract(camera,name))
        pieces += [extract(gx.read_text(),'GXProject'),extract(vector,'lbVector_WorldToScreen'),extract(nametag,'has_nametag'),extract(nametag,'fn_802FCC44')]
        template=(ROOT/'src/slippi_profile/tests/camera_source_prepare_control.c.in').read_text()
        source=work/'control.c'; source.write_text(template.replace('/* ACTUAL_SOURCE */','\n\n'.join(pieces)))
        common=['-O2','-std=gnu11','-ffp-contract=off','-DTARGET_PC','-DMELEE_WEB_GAMEPLAY=1','-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1','-ffunction-sections','-fdata-sections', '-I'+str(ROOT/'src'), '-I'+str(prepared), '-I'+str(ROOT/'.deps/aurora/include'), '-I'+str(ROOT/'.deps/melee/extern/dolphin/include'), '-include',ROOT/'src/gameplay_compat.h']
        output=work/'control.js'
        run('compile',[emcc,*common,source,ROOT/'src/slippi_profile/camera_source_prepare.c',ROOT/'src/gameplay_ps_math.c',*[ROOT/('.deps/aurora/lib/dolphin/mtx/'+x) for x in ['mtx.c','mtx44.c','mtxvec.c','vec.c']],'-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sWASM_ASYNC_COMPILATION=0','-o',output])
        result=run('execute',[node_runtime(),output])
        self.assertIn('camera-history actual-source PASS',result.stdout)
        disabled=work/'disabled.c'
        disabled.write_text('#include "slippi_profile/camera_source_prepare.h"\nint main(void) { return !slippi_camera_source_prepare(0,1,0,0); }\n')
        off=[x for x in common if x!='-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1']
        run('disabled-compile',[emcc,*off,disabled,'-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sWASM_ASYNC_COMPILATION=0','-o',work/'disabled.js'])
        run('disabled-execute',[node_runtime(),work/'disabled.js'])
        # Production TU syntax: no alternate fixture declarations can hide type errors.
        run('helper-syntax',[emcc,*common,'-fsyntax-only',ROOT/'src/slippi_profile/camera_source_prepare.c'])
        pins.update({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [ROOT/'src/slippi_profile/camera_source_prepare.c',ROOT/'src/slippi_profile/camera_source_prepare.h',ROOT/'src/slippi_profile/patches/melee-profile-hooks.patch',Path(__file__),ROOT/'src/slippi_profile/tests/camera_source_prepare_control.c.in']})
        (work/'pins.json').write_text(json.dumps(pins,indent=2)+'\n')
