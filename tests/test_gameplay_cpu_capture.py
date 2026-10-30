"""Actual capture/restore/refusal/cleanup bodies with explicitly modeled leaves."""
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
        session_src=(SOURCE/'src/gameplay_match_session.cpp').read_text()
        def source(relative):
            p=SOURCE/relative
            return (p if p.is_file() else ROOT/relative).read_text()
        comparison=source('src/gameplay_cpu_comparison.hpp').replace('#pragma once','')
        payload=source('src/gameplay_source_memory_runtime.h')
        payload=payload[payload.index('typedef enum MeleeWebSourceMemoryPayloadSnapshotStatus'):payload.index('MeleeWebSourceMemoryPayloadSnapshotCaptureResult;')+len('MeleeWebSourceMemoryPayloadSnapshotCaptureResult;')]
        diag=header[header.index('enum class CpuCaptureRefusal'):header.index('class GameplayCpuCheckpoint final')]
        generated=source('tests/cpu_capture_refusal_control.cpp.in')
        capture=function(src,'std::unique_ptr<GameplayCpuCheckpoint> GameplayCpuCheckpoint::capture(')
        capture=capture.replace('slippi_particle_presentation_anchor_capture','fixture_particle_anchor_capture')
        capture=capture.replace('slippi_particle_presentation_anchor_admit','fixture_particle_anchor_admit')
        restore=function(src,'GameplayCpuCommitResult GameplayCpuCheckpoint::restore(')
        self.assertIn('slippi_lobj_current_set_view_reset_after_restore(commit_.lobj_anchor)',restore)
        self.assertIn('slippi_particle_source_reset_after_restore(commit_.particle_anchor)',restore)
        # Replace only the reset leaf with its counted fixture implementation;
        # keep the extracted production guards, ordering, and phase changes.
        restore=restore.replace('slippi_lobj_current_set_view_reset_after_restore','fixture_profile_reset_after_restore')
        restore=restore.replace('slippi_particle_source_reset_after_restore','fixture_particle_source_reset_after_restore')
        restore_corrected=function(src,'GameplayCpuCommitResult GameplayCpuCheckpoint::restore_corrected(')
        restore_corrected=restore_corrected.replace('slippi_lobj_current_set_view_reset_after_restore','fixture_profile_reset_after_restore')
        restore_corrected=restore_corrected.replace('slippi_particle_source_reset_after_restore','fixture_particle_source_reset_after_restore')
        finish_corrected=function(src,'bool GameplayCpuCheckpoint::finish_corrected(')
        finish_corrected=finish_corrected.replace('slippi_particle_presentation_anchor_admit','fixture_particle_anchor_admit')
        retire_confirmed=function(src,'bool GameplayCpuCheckpoint::retire_confirmed(')
        retire_confirmed=retire_confirmed.replace('slippi_particle_presentation_anchor_admit','fixture_particle_anchor_admit')
        retire_confirmed=retire_confirmed.replace('slippi_particle_presentation_anchor_disarm','fixture_particle_anchor_disarm')
        retire_confirmed=retire_confirmed.replace('slippi_particle_source_interval_end','fixture_particle_source_interval_end')
        disarm=function(src,'void GameplayCpuCheckpoint::disarm()')
        disarm=disarm.replace('slippi_particle_presentation_anchor_disarm','fixture_particle_anchor_disarm')
        disarm=disarm.replace('slippi_particle_source_interval_end','fixture_particle_source_interval_end')
        source_quiescent=function(session_src,'bool GameplayMatchSession::diagnostic_source_files_quiescent() const noexcept')
        self.assertIn('storage_->world->ensure_source_files_quiescent();',source_quiescent)
        self.assertIn('catch(...)',source_quiescent)
        self.assertLess(source_quiescent.index('diagnostic_cpu_owners()'),source_quiescent.index('ensure_source_files_quiescent()'))
        replacements={
            '@ACTUAL_COMPARISON@':comparison,'@ACTUAL_CAPTURE_DIAGNOSTIC@':diag,
            '@ACTUAL_PAYLOAD_RESULT@':payload,
            '@ACTUAL_CAPTURE_FUNCTION@':capture,
            '@ACTUAL_INTERVAL_BOUNDARY@':function(src,'bool GameplayCpuCheckpoint::interval_boundary('),
            '@ACTUAL_SEAL_POST@':function(src,'bool GameplayCpuCheckpoint::seal_post('),
            '@ACTUAL_RESTORE@':restore,
            '@ACTUAL_RESTORE_CORRECTED@':restore_corrected,
            '@ACTUAL_FINISH_CORRECTED@':finish_corrected,
            '@ACTUAL_SESSION_SOURCE_QUIESCENT@':source_quiescent,
            '@ACTUAL_RETIRE_CONFIRMED@':retire_confirmed,
            '@ACTUAL_DRAWN_SEAL@':function(src,'bool GameplayCpuCheckpoint::seal_drawn_reference('),
            '@ACTUAL_DRAWN_MATCH@':function(src,'bool GameplayCpuCheckpoint::matches_drawn_reference('),
            '@ACTUAL_SOURCE_OWNERS_RETIRED@':function(src,'bool GameplayCpuCheckpoint::source_owners_retired('),
            '@ACTUAL_RELEASE_FUNCTION@':function(src,'void GameplayCpuCheckpoint::release()'),
            '@ACTUAL_DISARM_FUNCTION@':disarm,
            '@ACTUAL_DESTRUCTOR@':function(src,'GameplayCpuCheckpoint::~GameplayCpuCheckpoint()'),
        }
        for name,value in replacements.items():generated=generated.replace(name,value)
        self.assertNotIn('@ACTUAL_',generated)
        cpp=out/'control.cpp';cpp.write_text(generated)
        # Same actual extracted source through the private V16 profile. Named
        # presentation/action/world leaves remain modeled; this is not a live
        # renderer, ActionStore catalog, or network-confirmation control.
        profiles=[
            ('v7',[]),
            ('v8',['-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1']),
            ('v16',['-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1',
                    '-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1']),
        ]
        for profile,profile_defines in profiles:
            lane=out/profile;lane.mkdir()
            cmd=[compiler,'-std=c++20','-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=1',*profile_defines,'-Wall','-Wextra','-Werror','-O1','-UNDEBUG','-I'+str(SOURCE/'src'),str(cpp),'-o',str(lane/'control')]
            result=subprocess.run(cmd,cwd=lane,env=env,text=True,capture_output=True,timeout=60)
            (lane/'compile.json').write_text(json.dumps({'profile':profile,'argv':cmd,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2)+'\n')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(lane/'control')],cwd=lane,env=env,text=True,capture_output=True,timeout=30)
            (lane/'run.json').write_text(json.dumps({'profile':profile,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2)+'\n')
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('eight first-leaf refusals',result.stdout)
            self.assertIn('restore commit/reset lifecycle guards passed',result.stdout)
            self.assertIn('corrected restore selects its dedicated writer',result.stdout)
            self.assertIn('confirmed retirement refuses before detaching and permits bounded live-owner reuse',result.stdout)
            self.assertIn('retained confirmed disarm cannot clear a newer live interval route',result.stdout)
            self.assertIn('clean Sealed reference releases with zero comparison masks; nonzero mask refuses',result.stdout)
            print('Actual capture control profile/output:',profile,lane)
            print(result.stdout.strip())

if __name__=='__main__':unittest.main()
