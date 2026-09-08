"""Run original material template/constant readers with native class storage."""
import ast
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from gameplay_sources import prepare_sources

def material_text(source):
    return (source/'melee/ft/ftmaterial.c').read_text()
def function(text,name):
    match=re.search(r'(?m)^\w[^\n;]*\b'+name+r'\([^;]*?\)\s*\{',text)
    if not match:raise AssertionError('Missing original function '+name)
    end=match.end();depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[match.start():end]

class GameplayMaterialAbiTests(unittest.TestCase):
    def test_original_material_consumers_and_broken_adjacency_control(self):
        sdk=ROOT/'.deps/emsdk';compiler=sdk/'upstream/emscripten';config=sdk/'.emscripten'
        if not (compiler/'emcc.py').is_file():self.skipTest('Project SDK unavailable')
        setting=next(ast.literal_eval(x.value) for x in ast.parse(config.read_text()).body
            if isinstance(x,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='NODE_JS' for t in x.targets))
        node=Path(setting.replace('$CFGDIR',str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        env=dict(os.environ,EMSDK=str(sdk),EM_CONFIG=str(config),EM_CACHE=str(compiler/'cache'),EMSDK_PYTHON=sys.executable)
        source=prepare_sources(ROOT)
        original=(ROOT/'.deps/melee/src/melee/ft/ftmaterial.c').read_text()
        patched=material_text(source)
        texp=(source/'sysdolphin/baselib/texp.c').read_text()
        getter=(source/'melee/ft/ftdevice.c').read_text()
        includes='''#include "gameplay_compat.h"
#include <melee/ft/ftmaterial.h>
#include <melee/ft/fighter.h>
#include <melee/ft/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/kinds/ftCommon/ftCo_09F4.h>
#include <melee/lb/lb_00B0.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/texp.h>
'''
        with tempfile.TemporaryDirectory(prefix='melee material ABI ') as directory:
            directory=Path(directory)
            for label,text in [('patched',patched),('broken',original)]:
                globals_=text[text.index('HSD_MObjInfo ftMObj ='):text.index('\nvoid ftMaterial_800BF260(')]
                # Keep only the original data objects, excluding the independent
                # setup adapter added between the data and class initializer.
                globals_=globals_.split('#if defined(MELEE_WEB_GAMEPLAY)')[0]
                unit=includes+globals_+'\n'+function(getter,'ftCo_800C0658')+'\n'
                unit+=texp[texp.index('static GXTevKColorID id1'):texp.index('\nvoid HSD_TExpSetupTev(')]+'\n'
                unit+=function(text,'ftMaterial_800BF534')+'\n'+function(text,'ftMaterial_800BF6BC')+'\n'
                unit+=(ROOT/'tests/gameplay_material_abi_trace.c').read_text()
                c=directory/(label+'.c');c.write_text(unit);out=directory/(label+'.js')
                cmd=[sys.executable,str(compiler/'emcc.py'),'-O1','-std=c11','-DTARGET_PC',
                     '-I',str(ROOT/'src'),'-I',str(ROOT/'.deps/aurora/include'),'-I',str(source),
                     str(c),'-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-sSAFE_HEAP=1','-o',str(out)]
                result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                result=subprocess.run([str(node),str(out)],env=env,capture_output=True,text=True,timeout=15)
                if label=='patched':
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    self.assertIn('named RGB stack lifetime: passed',result.stdout)
                else:
                    self.assertNotEqual(result.returncode,0,'Broken global adjacency unexpectedly passed')
