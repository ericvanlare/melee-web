"""Exercise the real patched Aurora submission owner, including late callbacks."""
from pathlib import Path
import os
import shutil
import subprocess

from owned_test_workspace import OwnedWorkspaceTests


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / '.deps/aurora/lib/gfx/browser_submission_owner.hpp'
FIXTURE = Path(__file__).with_name('aurora_browser_submission_owner_test.cpp')


class AuroraBrowserSubmissionOwnerTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, 'aurora-submission-owner-test-')

    def test_generation_reuse_spontaneous_completion_and_current_owner_failure(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler unavailable')
        self.assertTrue(HEADER.is_file(), 'patched submission owner header is missing')
        binary = self.scratch / 'submission_owner_test'
        compile_result = subprocess.run(
            [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-pthread',
             '-I', str(HEADER.parent), str(FIXTURE), '-o', str(binary)],
            text=True, capture_output=True, timeout=30)
        (self.scratch / 'compile.log').write_text(compile_result.stdout + compile_result.stderr)
        self.assertEqual(compile_result.returncode, 0, compile_result.stdout + compile_result.stderr)
        run_result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
        (self.scratch / 'run.log').write_text(run_result.stdout + run_result.stderr)
        self.assertEqual(run_result.returncode, 0, run_result.stdout + run_result.stderr)
        self.assertIn('submission owner publication: PASS', run_result.stdout)
