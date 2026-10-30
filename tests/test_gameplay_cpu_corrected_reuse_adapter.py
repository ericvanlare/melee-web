"""Compile and exercise the private corrected adapter's bounded reuse path."""
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

from test_gameplay_cpu_restore_adapter import (
    REPO_ROOT, STUB_CHECKPOINT_HEADER, source_overlay,
    extract_fixture_observations, write_record,
)
from owned_test_workspace import OwnedWorkspaceTests


class GameplayCpuCorrectedReuseAdapterTests(OwnedWorkspaceTests):
    def test_actual_two_window_reuse_adapter_guards(self):
        sdk=REPO_ROOT/'.deps/emsdk'
        compiler=sdk/'upstream/emscripten'
        config=sdk/'.emscripten'
        source=REPO_ROOT/'build/gameplay-source/src'
        if not (compiler/'em++.py').is_file() or not config.is_file() or not (source/'melee/ft/forward.h').is_file():
            self.skipTest('Pinned SDK/generated source headers unavailable')
        out=self.new_workspace(REPO_ROOT,'cpu-corrected-reuse-')
        env=dict(os.environ,EMSDK=str(sdk),EM_CONFIG=str(config),EM_CACHE=str(compiler/'cache'),EMSDK_PYTHON=sys.executable)
        node=None
        for statement in ast.parse(config.read_text()).body:
            if isinstance(statement,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='NODE_JS' for t in statement.targets):
                node=Path(ast.literal_eval(statement.value).replace('$CFGDIR',str(sdk))).resolve()
        self.assertIsNotNone(node)
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        lock=json.loads((REPO_ROOT/'dependencies.lock.json').read_text())
        self.assertEqual((compiler/'emscripten-version.txt').read_text().strip().strip('"'),lock['emscripten'])
        overlay=source_overlay(out)
        stubs=out/'stubs';stubs.mkdir()
        header=STUB_CHECKPOINT_HEADER.replace('    bool finish_replay(',
            '    GameplayCpuCommitResult restore_corrected(const GameplayMatchSession&) noexcept;\n'
            '    bool finish_corrected(const GameplayMatchSession&) noexcept;\n'
            '    bool retire_confirmed(const GameplayMatchSession&) noexcept;\n    bool finish_replay(')
        self.assertEqual(header.count('bool retire_confirmed('),1)
        (stubs/'gameplay_cpu_checkpoint.hpp').write_text(header)
        base=(REPO_ROOT/'tests/cpu_restore_adapter_control.cpp.in').read_text().split('\nint main() {')[0]
        owner_fixture=('    struct CpuOwners {MeleeWebAudio* audio;};\n'
            '    CpuOwners diagnostic_cpu_owners() const noexcept {return {reinterpret_cast<MeleeWebAudio*>(std::uintptr_t(0x102))};}')
        self.assertEqual(base.count(owner_fixture),1)
        base=base.replace(owner_fixture,
            '    struct CpuOwners {const void* storage; MeleeWebMatchContext* context; MeleeWebMatchFlow* flow; '
            'MeleeWebAudio* audio; std::uint64_t world,context_epoch; std::uint32_t frame;};\n'
            '    CpuOwners diagnostic_cpu_owners() const noexcept {return {reinterpret_cast<const void*>(std::uintptr_t(0x100)), '
            'reinterpret_cast<MeleeWebMatchContext*>(std::uintptr_t(0x101)), '
            'reinterpret_cast<MeleeWebMatchFlow*>(std::uintptr_t(0x104)), '
            'reinterpret_cast<MeleeWebAudio*>(std::uintptr_t(0x102)),0x103u,0x105u,frame_};}')
        base=base.replace('@ACTUAL_FIXTURE_OBSERVATIONS@',extract_fixture_observations(REPO_ROOT/'tests/gameplay_snapshot_probe.cpp'))
        browser=(REPO_ROOT/'src/slippi_profile/tests/synctest_browser.cpp').read_text()
        start=browser.index('uint32_t payload_admission_source_boundary(')
        end=browser.index('uint32_t payload_admission_boundary(',start)
        base=base.replace('@ACTUAL_PAYLOAD_SOURCE_BOUNDARY@',browser[start:end].rstrip())
        base=base.replace('#include "cpu_restore_adapter.inc"',
            '#include "gameplay_cpu_endpoint.hpp"\n#define MELEE_WEB_SYNCTEST_CORRECTED_CPU 1\n'
            'namespace { // This fixture links only the legacy reusable route; history has no owner here.\n'
            'bool cpu_history_active(){ return false; }\n'
            'void cpu_history_forbid(std::vector<melee_web::CpuIntervalSpan>&) { if(cpu_history_active()) std::abort(); }\n'
            'bool cpu_corrected_active(); void cpu_corrected_forbid(std::vector<melee_web::CpuIntervalSpan>&); }\n'
            '#include "cpu_restore_adapter.inc"\n#include "cpu_corrected_adapter.inc"')
        base=base.replace('std::uint32_t payload_admission_runtime_boundary() { return 0; }',
            'bool corrected_runtime_refused=false;\n'
            'std::uint32_t payload_admission_runtime_boundary() { return corrected_runtime_refused?1:0; }')
        # The fixture's pending_submission models the actual staging/Aurora idle
        # condition, which production payload_admission_boundary checks even
        # when no new draw is required.
        idle_fixture='if(require_idle) { ++idle_boundary_calls; if(pending_submission) return 1; }'
        self.assertEqual(base.count(idle_fixture),1)
        base=base.replace(idle_fixture,
            'if(pending_submission)return 1; if(require_idle)++idle_boundary_calls;')
        self.assertEqual(base.count('std::array<unsigned,16> tick_samples{}'),1)
        base=base.replace('std::array<unsigned,16> tick_samples{}','std::array<unsigned,64> tick_samples{}')
        base=base.replace('command>31u','command>53u')
        split=base.index('extern "C" int melee_web_snapshot_step(unsigned sample) {')
        providers=base[split:]
        self.assertEqual(providers.count('cpu_restore.seven_active'),4)
        providers=providers.replace('cpu_restore.seven_active',
            '(cpu_restore.seven_active||cpu_corrected_active())')
        self.assertEqual(providers.count('sample<=306'),1)
        providers=providers.replace('sample<=306','sample<=UINT32_MAX')
        raw_start=providers.index('extern "C" int melee_web_snapshot_step_raw() {')
        raw_end=providers.index('extern "C" int melee_web_synctest_draw_present()',raw_start)
        raw_step='''extern "C" int melee_web_snapshot_step_raw() {
    if(!(cpu_restore.seven_active||cpu_corrected_active()))return 0;
    unsigned sample=300u+static_cast<unsigned>(tick_sample_count%7u);
    if(cpu_corrected_reuse.enabled){
        if(!match||match->source_frames()<cpu_corrected_reuse.first_capture_frame)return 0;
        const uint32_t delta=match->source_frames()-cpu_corrected_reuse.first_capture_frame;
        if(delta>UINT32_MAX-300u)return 0;
        sample=300u+delta;
    }
    return melee_web_snapshot_step(sample);
}
'''
        providers=providers[:raw_start]+raw_step+providers[raw_end:]
        self.assertEqual(providers.count('cpu_corrected_active())?8u:'),1)
        providers=providers.replace('cpu_corrected_active())?8u:', 'cpu_corrected_active())?46u:')
        base=base[:split]+providers
        template=REPO_ROOT/'tests/cpu_corrected_reuse_adapter_control.cpp.in'
        generated=template.read_text().replace('@BASE_CONTROL@',base)
        self.assertNotIn('@ACTUAL_',generated)
        control=out/'corrected_reuse.cpp';control.write_text(generated)
        common=['-O1','-fexceptions','-DTARGET_PC','-DMELEE_WEB_CPU_INTERVAL_ENABLED=1',
                '-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK=1','-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1',
                '-ffunction-sections','-fdata-sections','-ffp-contract=off']
        for p in [stubs,overlay,overlay/'slippi_profile/tests',REPO_ROOT/'.deps/aurora/include',source,REPO_ROOT/'tests']:
            common+=['-I',str(p)]
        common+=['-include',str(overlay/'gameplay_compat.h')]
        objects=[]
        for i,p in enumerate([overlay/'gameplay_cpu_commit.cpp',overlay/'gameplay_cpu_statics.cpp',control]):
            obj=out/f'{i}.o';objects.append(obj)
            cmd=[sys.executable,str(compiler/'em++.py'),*common,'-std=c++20','-c',str(p),'-o',str(obj)]
            result=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True,timeout=120)
            write_record(out/f'compile-{i}.json',cmd,result)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        target=out/'control.js'
        cmd=[sys.executable,str(compiler/'em++.py'),'-O1','-fexceptions',*map(str,objects),
             '-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-sSAFE_HEAP=1','-sALLOW_MEMORY_GROWTH=1','-o',str(target)]
        result=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True,timeout=120)
        write_record(out/'link.json',cmd,result);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        cmd=[str(node),str(target)]
        result=subprocess.run(cmd,cwd=out,env=env,text=True,capture_output=True,timeout=45)
        write_record(out/'run.json',cmd,result);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('Corrected reuse adapter PASS',result.stdout)
        pins=[template,Path(__file__),REPO_ROOT/'tests/cpu_restore_adapter_control.cpp.in',
              REPO_ROOT/'tests/test_gameplay_cpu_restore_adapter.py',
              REPO_ROOT/'tests/cpu_corrected_adapter_control.cpp.in',
              REPO_ROOT/'src/slippi_profile/tests/cpu_corrected_adapter.inc',
              REPO_ROOT/'src/slippi_profile/tests/cpu_restore_adapter.inc']
        (out/'receipt.json').write_text(json.dumps({'status':'PASS','scope':__doc__,
            'pins':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in pins},
            'runtime_stdout':result.stdout},indent=2)+'\n')
        print(result.stdout,end='')
        print(f'Corrected reuse adapter receipt: {out / "receipt.json"}')


if __name__=='__main__':unittest.main()
