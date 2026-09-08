"""Owned common effect sparse shape trees and rejection against actual models."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources
class ShapeAnimationTests(unittest.TestCase):
    def test_local_common_sparse_shape_graphs(self):
        asset=ROOT/'assets-local/next-gate/EfCoData.dat'
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/em++.py'
        if not asset.is_file() or not compiler.is_file():
            self.skipTest('Owned common effect archive and pinned SDK required')
        source=prepare_sources(ROOT)
        with tempfile.TemporaryDirectory(prefix='melee shape animation ') as directory:
            output=Path(directory)/'shape.js'
            command=[sys.executable,str(compiler),'-std=c++20','-O1','-fexceptions','-DTARGET_PC',
                '-I',str(ROOT/'src'),'-I',str(source),'-I',str(ROOT/'.deps/aurora/include'),
                *[str(ROOT/'src'/(unit+'.cpp')) for unit in ('dat_archive','dat_texture','dat_material','rigid_model','dat_native_joint','dat_animation','dat_shape_animation')],
                str(ROOT/'tests/dat_shape_animation_trace.cpp'),'-sNODERAWFS=1','-sENVIRONMENT=node',
                '-sALLOW_MEMORY_GROWTH=1','-sSAFE_HEAP=1','-o',str(output)]
            result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(node_runtime()),str(output),str(asset)],cwd=ROOT,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('10 entries, 37 joints, 22 DObjs passed',result.stdout)

    def test_local_stage_shape_geometry_and_pairing(self):
        asset=ROOT/'assets-local/native-menus/MnSlMap.usd'
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/em++.py'
        if not asset.is_file() or not compiler.is_file():
            self.skipTest('Owned stage selection archive and pinned SDK required')
        source=prepare_sources(ROOT)
        with tempfile.TemporaryDirectory(prefix='melee stage shape ') as directory:
            output=Path(directory)/'shape.js'
            command=[sys.executable,str(compiler),'-std=c++20','-O1','-fexceptions','-DTARGET_PC',
                '-I',str(ROOT/'src'),'-I',str(source),'-I',str(ROOT/'.deps/aurora/include'),
                *[str(ROOT/'src'/(unit+'.cpp')) for unit in ('dat_archive','dat_texture','dat_material','rigid_model','dat_native_joint','dat_animation','dat_shape_animation')],
                str(ROOT/'tests/dat_shape_native_trace.cpp'),'-sNODERAWFS=1','-sENVIRONMENT=node',
                '-sALLOW_MEMORY_GROWTH=1','-sSAFE_HEAP=1','-o',str(output)]
            result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(node_runtime()),str(output),str(asset)],cwd=ROOT,capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('2 PObjs, 3 shape joints, 3 shape DObj descriptors',result.stdout)
