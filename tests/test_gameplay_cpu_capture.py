"""Actual capture/refusal/cleanup bodies with explicitly modeled factory leaves."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(os.environ.get('MELEE_CPU_CAPTURE_REPO_ROOT',Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get('MELEE_CPU_CAPTURE_SOURCE_ROOT',ROOT)).resolve()
sys.path.insert(0,str(ROOT/'tests'))
from owned_test_workspace import OwnedWorkspaceTests

def function(text,needle):
    start=text.index(needle);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        if text[end]=='{':depth+=1
        elif text[end]=='}':depth-=1
        end+=1
    return text[start:end]

class GameplayCpuCaptureControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.output=None
        if 'MELEE_CPU_CAPTURE_OUTPUT_ROOT' not in os.environ:
            cls.output=cls.new_workspace(ROOT,'cpu-capture-refusal-')

    def test_actual_capture_first_refusal_and_production_temporary_cleanup(self):
        compiler=shutil.which('c++')
        if not compiler:self.skipTest('C++ compiler unavailable')
        root=Path(os.environ.get('MELEE_CPU_CAPTURE_OUTPUT_ROOT',ROOT/'work'))
        root.mkdir(parents=True,exist_ok=True)
        out=self.output or Path(tempfile.mkdtemp(prefix='cpu-capture-refusal-',dir=root))
        temporary=out/'tmp';temporary.mkdir();env=dict(os.environ,TMPDIR=str(temporary))
        src=(SOURCE/'src/gameplay_cpu_checkpoint.cpp').read_text()
        header=(SOURCE/'src/gameplay_cpu_checkpoint.hpp').read_text()
        def source(relative):
            p=SOURCE/relative
            return (p if p.is_file() else ROOT/relative).read_text()
        comparison=source('src/gameplay_cpu_comparison.hpp').replace('#pragma once','')
        payload=source('src/gameplay_source_memory_runtime.h')
        payload=payload[payload.index('typedef enum MeleeWebSourceMemoryPayloadSnapshotStatus'):payload.index('MeleeWebSourceMemoryPayloadSnapshotCaptureResult;')+len('MeleeWebSourceMemoryPayloadSnapshotCaptureResult;')]
        diag=header[header.index('enum class CpuCaptureRefusal'):header.index('class GameplayCpuCheckpoint final')]
        generated=source('tests/cpu_capture_refusal_control.cpp.in')
        replacements={
            '@ACTUAL_COMPARISON@':comparison,'@ACTUAL_CAPTURE_DIAGNOSTIC@':diag,
            '@ACTUAL_PAYLOAD_RESULT@':payload,
            '@ACTUAL_CAPTURE_FUNCTION@':function(src,'std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture('),
            '@ACTUAL_RELEASE_FUNCTION@':function(src,'void GameplayCpuCheckpoint::release()'),
            '@ACTUAL_DISARM_FUNCTION@':function(src,'void GameplayCpuCheckpoint::disarm()'),
            '@ACTUAL_DESTRUCTOR@':function(src,'GameplayCpuCheckpoint::~GameplayCpuCheckpoint()'),
        }
        for name,value in replacements.items():generated=generated.replace(name,value)
        self.assertNotIn('@ACTUAL_',generated)
        cpp=out/'control.cpp';cpp.write_text(generated)
        cmd=[compiler,'-std=c++20','-Wall','-Wextra','-Werror','-O1','-UNDEBUG',str(cpp),'-o',str(out/'control')]
        result=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True,timeout=60)
        (out/'compile.json').write_text(json.dumps({'argv':cmd,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2)+'\n')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(out/'control')],cwd=out,env=env,text=True,capture_output=True,timeout=30)
        (out/'run.json').write_text(json.dumps({'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2)+'\n')
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('eight first-leaf refusals',result.stdout)
        print('Actual capture control output:',out)
        print(result.stdout.strip())

if __name__=='__main__':unittest.main()
