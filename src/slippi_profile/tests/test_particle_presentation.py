# SPDX-License-Identifier: GPL-3.0-or-later
"""Real helper/sort/preparation gates; modeled pool, interval and matrix providers.

This is not a GPU, original matrix arithmetic or full CPU restoration result.
The source bridges are reconstructed from the authoritative downstream patches.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
PROFILE = ROOT / 'src/slippi_profile'
_saved = sys.path.copy()
sys.path.insert(0, str(ROOT / 'scripts'))
from gameplay_bool import transform_source
from check_gameplay import node_runtime
sys.path[:] = _saved


def function(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    level = 1
    end = opening + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


class ParticlePresentationTests(unittest.TestCase):
    def test_actual_sort_preparation_and_helper_lifecycle_refusals(self):
        upstream = ROOT / '.deps/melee/src/sysdolphin/baselib/psdisp.c'
        emcc = ROOT / '.deps/emsdk/upstream/emscripten/emcc'
        prepared = ROOT / 'build/gameplay-source-slippi-profile/src'
        if not (ROOT / '.deps/melee').is_dir() or not emcc.is_file():
            self.skipTest('pinned source checkout/Emscripten compiler unavailable')
        if not (prepared / 'sysdolphin/baselib/psstructs.h').is_file():
            self.skipTest('existing prepared SDK headers unavailable; no runtime build in this control')
        self.assertEqual(hashlib.sha256(upstream.read_bytes()).hexdigest(),
                         'f5aeb59cb2510213053d03819354424166677d75b44e5ab671165a75832b51af')
        parent = Path(os.environ.get('MELEE_PARTICLE_PRESENTATION_EVIDENCE', ROOT / 'work'))
        parent.mkdir(parents=True, exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix='particle-presentation-', dir=parent))
        try:
            source = scratch / 'source'
            relative = 'src/sysdolphin/baselib/psdisp.c'
            file = source / relative
            file.parent.mkdir(parents=True)
            file.write_bytes(upstream.read_bytes())
            env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(scratch))
            for label, patch in [('gameplay', ROOT / 'patches/melee-gameplay.patch'),
                                 ('profile', PROFILE / 'patches/melee-profile-hooks.patch')]:
                if label == 'profile':
                    file.write_text(transform_source(relative, file.read_text()))
                for stage in ('--check', None):
                    argv = ['git', 'apply'] + ([stage] if stage else []) + [
                        '--include=' + relative, str(patch)]
                    self.run_command(label + ('-check' if stage else '-apply'), argv,
                                     source, scratch, env=env)
            text = file.read_text()
            sort = function(text, 'HSD_Particle* particleSort(')
            preflight = function(text, 'int slippi_particle_source_preflight_visit(')
            prepare = function(text, 'int slippi_particle_source_prepare_visit(')
            actual_psdisp = function(text, 'void psDispParticles(')
            sw0 = function(actual_psdisp, 'if (sw == 0)')
            original = upstream.read_text()
            projections = []
            for label, marker, start in [
                ('point', 'static inline void psDispSubAPPSRTPoint(', 'if (pp->appsrt != NULL)'),
                ('polygon', 'static inline void psDispSubAppSRT(', 'if (pp->appsrt->frameNum != psFrameNum)'),
            ]:
                body = function(original, marker)
                block = function(body, start)
                declarations = 'Mtx scratch_mtx, draw_mtx; Vec3 scratch_scale;'
                projections.append('static void reference_' + label + '(HSD_Particle* pp) {' + declarations + block + '}')
                candidate = function(function(text, marker), start.replace('pp->appsrt', 'SLIPPI_APP'))
                candidate = candidate.replace('SLIPPI_APP', 'app_view')
                projections.append('static void candidate_' + label + '(HSD_Particle* pp,HSD_psAppSRT* out) {' + declarations +
                                   'HSD_psAppSRT* app_view=slippi_particle_presentation_app_srt_view(pp->appsrt,out);assert(app_view);' + candidate + '}')
            fixture = (PROFILE / 'tests/particle_presentation_control.c.in').read_text().replace('ACTUAL_PROJECTIONS', '\n'.join(projections))
            for key, value in [('ACTUAL_SW0', sw0), ('ACTUAL_SORT', sort),
                               ('ACTUAL_PREFLIGHT', preflight), ('ACTUAL_PREPARE', prepare)]:
                fixture = fixture.replace(key, value)
            self.assertNotIn('ACTUAL_PREPARE', fixture)
            control = scratch / 'control.c'
            control.write_text(fixture)
            helper_text = (PROFILE / 'particle_presentation.c').read_text()
            row_text = function(helper_text,
                                'int slippi_particle_presentation_cell_endpoint_row(')
            self.assertIn('value.extent=(uint32_t)extent', row_text)
            self.assertIn('value.stride=c->identity.stride', row_text)
            for marker in (
                    'static int cell_endpoint_admit(',
                    'int slippi_particle_presentation_cell_endpoint_header(',
                    'int slippi_particle_presentation_cell_endpoint_row('):
                self.assertNotRegex(function(helper_text, marker),
                                    r'\b(?:malloc|calloc|realloc|free)\s*\(')
            argv = [str(emcc), '-std=gnu11', '-O1', '-ffp-contract=off',
                    '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1',
                    '-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1', '-DTARGET_PC', '-DAURORA',
                    '-Dcalloc=particle_test_calloc',
                    '-I' + str(ROOT / 'src'), '-I' + str(prepared),
                    '-I' + str(ROOT / '.deps/aurora/include'),
                    '-I' + str(ROOT / '.deps/melee/extern/dolphin/include'),
                    '-include', str(ROOT / 'src/gameplay_compat.h'),
                    str(PROFILE / 'particle_presentation.c'), str(control),
                    '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-sWASM_ASYNC_COMPILATION=0',
                    '-o', str(scratch / 'control.js')]
            actual_math = os.environ.get('MELEE_PARTICLE_PRESENTATION_ACTUAL_MATH') == '1'
            build = ROOT / 'build/slippi-synctest-logical-frame-lobj-presentation-tobj-presentation-stage-arena-aurora-staging-release'
            if actual_math:
                self.assertTrue((build / 'libhsd_native_runtime.a').is_file(), 'actual math archive missing')
                compile_flags = argv[:argv.index(str(PROFILE / 'particle_presentation.c'))]
                objects = []
                for name, source_file in [('helper', PROFILE / 'particle_presentation.c'), ('control', control)]:
                    object_file = scratch / (name + '.o')
                    self.run_command('compile-' + name, compile_flags + ['-DACTUAL_LINKED_MATH=1', '-c', str(source_file), '-o', str(object_file)], ROOT, scratch)
                    objects.append(str(object_file))
                # Isolate the actual math archive members, avoiding unrelated
                # runtime globals/providers from the full Node entry target.
                ar = ROOT / '.deps/emsdk/upstream/bin/llvm-ar'
                math_objects = []
                member_pins = []
                for archive, member in [('libhsd_native_runtime.a', 'mtx.c.o'),
                                        ('libgameplay_runtime.a', 'gameplay_ps_math.c.o')]:
                    data = subprocess.check_output([str(ar), 'p', str(build / archive), member])
                    obj = scratch / member
                    obj.write_bytes(data)
                    math_objects.append(str(obj))
                    member_pins.append({'archive': archive, 'archive_sha256': hashlib.sha256((build / archive).read_bytes()).hexdigest(),
                                        'member': member, 'member_sha256': hashlib.sha256(data).hexdigest()})
                (scratch / 'actual-math-members.json').write_text(json.dumps(member_pins, indent=2) + '\n')
                linked = [str(emcc), '-O1', '-fexceptions'] + objects + math_objects + [str(build / '.deps/aurora/libaurora_mtx.a'),
                          '-sEXPORTED_FUNCTIONS=_main', '-sENVIRONMENT=node', '-sEXIT_RUNTIME=1', '-sWASM_ASYNC_COMPILATION=0',
                          '-o', str(scratch / 'control.js')]
                linked_inputs = []
                for item in linked:
                    if item.endswith(('.a', '.o')):
                        obj = Path(item)
                        if not obj.is_absolute(): obj = build / obj
                        linked_inputs.append({'path': str(obj), 'sha256': hashlib.sha256(obj.read_bytes()).hexdigest()})
                (scratch / 'actual-linked-inputs.json').write_text(json.dumps({'origin': 'existing Node archive; not attributed to current source HEAD', 'inputs': linked_inputs}, indent=2) + '\n')
                self.run_command('link-actual-math', linked, build, scratch)
            else:
                self.run_command('compile', argv, ROOT, scratch)
            node = node_runtime(ROOT)
            endpoint_modes = ('endpoint', 'endpoint-stale', 'endpoint-generation',
                              'endpoint-descriptor', 'endpoint-stride',
                              'endpoint-duplicate', 'endpoint-index')
            modes = (('positive', 'wrap') if actual_math else
                     ('positive', 'stale', 'kind', 'foreign', 'changed', 'wrong',
                      'repeat', 'lifetime', 'wrap', 'poolbusy', 'first-error',
                      'first-camera', 'first-bare')) + endpoint_modes
            if os.environ.get('MELEE_PARTICLE_PRESENTATION_MODE'):
                modes = (os.environ['MELEE_PARTICLE_PRESENTATION_MODE'],)
            for mode in modes:
                result = self.run_command('run-' + mode,
                                          [str(node), str(scratch / 'control.js'), mode],
                                          ROOT, scratch)
                self.assertIn('particle presentation control ' + mode + ' passed', result.stdout)
                if mode.startswith('first-'):
                    records = [line for line in result.stderr.splitlines()
                               if line.startswith('Particle helper first failure:')]
                    self.assertEqual(1, len(records), result.stderr)
                    expected = {
                        'first-error': 'Particle callback differs from prepared source visit',
                        'first-camera': 'Particle camera omitted a prepared visit',
                        'first-bare': 'slippi_particle_presentation_render_data:',
                    }[mode]
                    self.assertIn(expected, records[0])
                    self.assertNotIn('Particle draw did not consume', records[0])
                if actual_math:
                    self.assertIn('actual archived HSD_MtxSRT/PS math', result.stdout)
            inputs = [upstream, ROOT / 'patches/melee-gameplay.patch',
                      PROFILE / 'patches/melee-profile-hooks.patch',
                      PROFILE / 'particle_presentation.c', PROFILE / 'particle_presentation.h',
                      PROFILE / 'particle_presentation_source.h',
                      ROOT / 'src/gameplay_source_memory_runtime.h',
                      PROFILE / 'tests/particle_presentation_control.c.in', Path(__file__)]
            (scratch / 'pins.json').write_text(json.dumps({
                'scope': __doc__, 'actual_archived_math': actual_math,
                'input_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in inputs},
                'composed_psdisp_sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                'control_sha256': hashlib.sha256(control.read_bytes()).hexdigest(),
                'node_sha256': hashlib.sha256(node.read_bytes()).hexdigest(),
            }, indent=2) + '\n')
        except BaseException:
            print('Retained particle presentation control:', scratch, flush=True)
            raise
        else:
            if os.environ.get('MELEE_PARTICLE_PRESENTATION_EVIDENCE'):
                print('Retained particle presentation controls:', scratch, flush=True)
            else:
                shutil.rmtree(scratch)

    def run_command(self, label, argv, cwd, scratch, env=None):
        result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=60)
        (scratch / (label + '.json')).write_text(json.dumps({
            'argv': argv, 'returncode': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr,
        }, indent=2) + '\n')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result


if __name__ == '__main__':
    unittest.main()
