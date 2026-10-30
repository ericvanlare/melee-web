"""Fixed CPU endpoint wire schema and translation-unit compile control."""
from pathlib import Path
import os
import subprocess
import sys
import unittest

ROOT = Path(os.environ.get('MELEE_CPU_ENDPOINT_REPO_ROOT',Path(__file__).resolve().parents[1])).resolve()
SOURCE = Path(os.environ.get('MELEE_CPU_ENDPOINT_SOURCE_ROOT',ROOT)).resolve()
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'scripts'))
from owned_test_workspace import OwnedWorkspaceTests

class GameplayCpuEndpointControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emxx=ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not cls.emxx.is_file():
            raise unittest.SkipTest('Pinned Emscripten C++ compiler unavailable')
        cls.work=cls.new_workspace(ROOT,'gameplay-cpu-endpoint-')

    def common(self):
        return ['-std=c++20','-O1','-UNDEBUG','-DTARGET_PC',
            '-I'+str(SOURCE/'src'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/gameplay-source/src'),
            '-I'+str(ROOT/'build/gameplay-source/src/melee/gm'),'-I'+str(ROOT/'build/gameplay-source/src/melee/lb'),
            '-I'+str(ROOT/'.deps/melee/src'),'-I'+str(ROOT/'.deps/aurora/include'),
            '-I'+str(ROOT/'.deps/melee/extern/dolphin/include'),'-include',str(ROOT/'src/gameplay_compat.h')]

    def test_actual_endpoint_translation_unit_syntax(self):
        result=subprocess.run([str(self.emxx),*self.common(),'-fsyntax-only',
            str(SOURCE/'src/gameplay_cpu_endpoint.cpp')],cwd=ROOT,capture_output=True,text=True,timeout=120)
        (self.work/'endpoint-syntax.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_endpoint_fixed_wire_schema(self):
        directory=self.work/'schema';directory.mkdir();tmp=directory/'tmp';tmp.mkdir()
        binary=directory/'control.js'
        env=dict(os.environ,TMPDIR=str(tmp))
        command=[str(self.emxx),*self.common(),'-ffunction-sections','-fdata-sections',
            '-Wl,--gc-sections',str(SOURCE/'src/gameplay_cpu_endpoint.cpp'),
            str(SOURCE/'tests/gameplay_cpu_endpoint_schema_control.cpp'),
            '-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-o',str(binary)]
        compiled=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
        (directory/'compile.log').write_text(compiled.stdout+compiled.stderr)
        self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
        from check_gameplay import node_runtime
        run=subprocess.run([str(node_runtime(ROOT)),str(binary)],cwd=ROOT,env=env,
            capture_output=True,text=True,timeout=30)
        (directory/'run.log').write_text(run.stdout+run.stderr)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('Fixed CPU endpoint schema offsets',run.stdout)

    def test_actual_admitted_nine_domain_batches(self):
        directory=self.work/'admitted';directory.mkdir();tmp=directory/'tmp';tmp.mkdir()
        binary=directory/'control.js'
        env=dict(os.environ,TMPDIR=str(tmp))
        command=[str(self.emxx),*self.common(),'-DMELEE_WEB_CPU_ENDPOINT_TEST=1',
            '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0',
            '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            str(SOURCE/'src/gameplay_cpu_endpoint.cpp'),
            str(SOURCE/'src/gameplay_cpu_checkpoint.cpp'),
            str(SOURCE/'src/gameplay_cpu_statics.cpp'),
            str(SOURCE/'tests/gameplay_cpu_endpoint_admitted_control.cpp'),
            '-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-o',str(binary)]
        compiled=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
        (directory/'compile.log').write_text(compiled.stdout+compiled.stderr)
        self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
        from check_gameplay import node_runtime
        run=subprocess.run([str(node_runtime(ROOT)),str(binary)],cwd=ROOT,env=env,
            capture_output=True,text=True,timeout=30)
        (directory/'run.log').write_text(run.stdout+run.stderr)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('Retained-history captured/armed/restored PRE and predicted/corrected CPU/DRAW endpoint batches passed',run.stdout)
        self.assertIn('Admitted nine-domain PRE/reference/corrected batches',run.stdout)
        self.assertIn('Current END payload routing enumerates grown rows/cells',run.stdout)

if __name__=='__main__':unittest.main()
