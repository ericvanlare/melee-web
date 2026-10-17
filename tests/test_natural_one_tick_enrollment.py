"""Permanent native/JS checks for the private naturally-one-tick enrollment seam."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class NaturalOneTickEnrollmentTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, 'natural-one-enrollment-test-')

    def test_actual_native_book_exports_and_serialized_host_join(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A C++20 compiler is required')
        production = (ROOT / 'src/gameplay_menu_frame_effect.cpp').read_text()
        fixture = (ROOT / 'tests/natural_one_tick_enrollment_controls.cpp').read_text()
        for begin, end in [
            ('int melee_web_native_menu_frame_effect_arm(unsigned expected_frame){',
             'int melee_web_native_menu_frame_effect_arm_first_draw('),
            ('int melee_web_native_menu_frame_effect_request_natural_one(unsigned expected_frame){',
             'const char* melee_web_native_menu_frame_effect_natural_one_enrollment(){'),
            ('const char* melee_web_native_menu_frame_effect_natural_one_enrollment(){',
             '\n#endif\nconst char* melee_web_native_menu_frame_effect_status(){'),
        ]:
            self.assertIn(begin, production, 'Production enrollment seam missing: ' + begin)
            start = production.index(begin)
            self.assertIn(end, production[start:], 'Production seam terminator missing: ' + end)
            body = production[start:production.index(end, start)]
            self.assertEqual(fixture.count(body), 1,
                             'Native controls must embed exactly the current full production body: ' + begin)
        binary = self.scratch / 'natural_one_controls'
        built = subprocess.run(
            [compiler, '-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror',
             '-DMELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC=1', '-I', str(ROOT / 'src'),
             str(ROOT / 'tests/natural_one_tick_enrollment_controls.cpp'), '-o', str(binary)],
            capture_output=True, text=True, timeout=30)
        (self.scratch / 'compile.log').write_text(built.stdout + built.stderr)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        native_json = self.scratch / 'native.json'
        native_json.write_text(ran.stdout)
        (self.scratch / 'native.stderr').write_text(ran.stderr)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn('actual shared book positive0/2/1', ran.stderr)
        script = """
import assert from 'node:assert/strict';
import fs from 'node:fs';
import {pathToFileURL} from 'node:url';
const root=process.argv[1];
const {naturalOneEnrollmentFailures}=await import(pathToFileURL(root+'/tests/match_frame_effect_first_draw_driver.mjs'));
const {naturalEnrollmentFixture,enrolledOuterJoin}=await import(pathToFileURL(root+'/tests/natural_one_tick_enrollment_fixture.mjs'));
const value=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
assert.deepEqual(value,naturalEnrollmentFixture());
assert.deepEqual(naturalOneEnrollmentFailures(value,enrolledOuterJoin(),{originFrame:180}),[]);
"""
        checked = subprocess.run([str(node_runtime()), '--input-type=module', '-e', script,
                                  str(ROOT), str(native_json)], capture_output=True, text=True, timeout=30)
        (self.scratch / 'host-join.log').write_text(checked.stdout + checked.stderr)
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_private_enrollment_and_preserved_adapter_javascript(self):
        scripts = ['natural_one_tick_enrollment.test.mjs', 'one_first_draw_observation_adapter.test.mjs',
                   'one_normal_frame_observation_adapter.test.mjs', 'first_draw_components.test.mjs']
        result = subprocess.run([str(node_runtime()), '--test',
                                 *[str(ROOT / 'tests' / name) for name in scripts]],
                                capture_output=True, text=True, timeout=30)
        (self.scratch / 'javascript.log').write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
