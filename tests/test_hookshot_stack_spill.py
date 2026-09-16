"""The retail compiler's stack-placement trick must not escape its C object."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = 'src/melee/it/kinds/itlinkhookshot.c'


class HookshotStackSpillTests(unittest.TestCase):
    def test_owned_spill_and_original_out_of_bounds_negative_control(self):
        compiler = shutil.which('clang')
        original = ROOT / '.deps/melee' / SOURCE
        if not compiler or not original.is_file():
            self.skipTest('Clang and pinned Melee sources required')
        with tempfile.TemporaryDirectory(prefix='melee-hookshot-spill-') as temporary:
            directory = Path(temporary)
            patched = directory / SOURCE
            patched.parent.mkdir(parents=True)
            patched.write_text(original.read_text())
            applied = subprocess.run(['git', 'apply', f'--include={SOURCE}',
                                      str(ROOT / 'patches/melee-gameplay.patch')],
                                     cwd=directory, capture_output=True, text=True)
            self.assertEqual(applied.returncode, 0, applied.stderr)
            for name, text in [('fixed', patched.read_text()), ('prior', original.read_text())]:
                start = text.index('static inline f32 it_802A4BFC_sqrtf_offset(')
                end = text.index('\nstatic inline f64 it_802A4BFC_normalize_diff(', start)
                program = directory / f'{name}.c'
                program.write_text('''
#include <assert.h>
#include <dolphin/ppc_math.h>
typedef float f32;
typedef double f64;
#define __frsqrte frsqrte
''' + text[start:end] + '''
int main(void) {
    volatile float inputs[] = {4.0f, 0.0f, 9.0f, 100.0f};
    const float expected[] = {2.0f, 0.0f, 3.0f, 10.0f};
    for(unsigned i=0;i<4;++i) assert(it_802A4BFC_sqrtf_offset(inputs[i]) == expected[i]);
    return 0;
}
''')
                executable = directory / name
                built = subprocess.run([compiler, '-O0', '-std=c11', '-DTARGET_PC',
                                        '-ffp-contract=off', '-fsanitize=address',
                                        '-I', str(ROOT / '.deps/aurora/include'),
                                        str(program), '-lm', '-o', str(executable)],
                                       capture_output=True, text=True, timeout=60)
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True,
                                     env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=30)
                if name == 'fixed':
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                else:
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn('stack-buffer-overflow', run.stderr)


if __name__ == '__main__':
    unittest.main()
