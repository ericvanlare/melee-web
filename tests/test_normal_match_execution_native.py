"""Actual shared match/audio/finish helpers; explicit synthetic endpoint scope."""
from pathlib import Path
import hashlib
import shutil
import subprocess
from owned_test_workspace import OwnedWorkspaceTests
ROOT=Path(__file__).resolve().parents[1]
BODY_HASHES={
    'MATCH_STEP':'355858ae22af5205d623160df5e66e91e42e8dcff26ca295d5332c9b5726f08d',
    'NORMAL_AUDIO':'fcc409fccf915a69b44c0183c432ac6d5ec2b8a1c978e92116ee1bb12001b3d7',
    'SOURCE_FINISH':'fbedbd6c83695eab7bd953dbc08dbaede080cf6a612f43923f17ff23c1f24d4e',
}
class NormalMatchExecutionNativeTests(OwnedWorkspaceTests):
    def test_actual_shared_match_audio_finish_order(self):
        compiler=shutil.which('clang++')
        if not compiler:self.skipTest('clang++ required')
        source=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
        start=source.index('// Synchronous source execution only;')
        end=source.index('void tick(){',start)
        implementation=source[start:end]
        for name,expected in BODY_HASHES.items():
            marker=' // '+name+'_BODY_BEGIN\n'
            body=implementation.split(marker,1)[1].split(' // '+name+'_BODY_END',1)[0]
            if name=='MATCH_STEP':body=body[:-1]
            self.assertEqual(hashlib.sha256(body.encode()).hexdigest(),expected,
                             'Reviewed original block changed: '+name)
        for call in ['execute_match_source_step(sample,source_frames,elapsed,audio_elapsed,',
                     'render_normal_callback_audio(callback_audio_owner,callback_audio_elapsed,audio_before_construction,error\n',
                     'finish_source_callback_draw(source_frames,present_source\n']:
            self.assertEqual(source.count(call),1)
        fixture=(ROOT/'tests/normal_match_execution_native_controls.cpp').read_text()
        selector_start=source.index('   if(diagnostic_pad_remaining){',source.index('void tick(){'))
        selector_end=source.index('   if(match&&match->sudden_death())',selector_start)
        selector=source[selector_start:selector_end]
        self.assertNotIn('static unsigned replay_steps',fixture)
        self.assertNotIn('static void replay_boundary_mark',fixture)
        self.assertEqual(fixture.count('const LocalMarker replay_boundary_mark{'),4)
        self.assertEqual(fixture.count('// ACTUAL_NORMAL_MATCH_IMPLEMENTATION'),1)
        scratch=self.new_workspace(ROOT,'normal-match-execution-native-')
        cpp=scratch/'actual-normal-match.cpp'
        cpp.write_text(fixture.replace('// ACTUAL_NORMAL_MATCH_IMPLEMENTATION',implementation)
                       .replace('// ACTUAL_QUEUED_DIAGNOSTIC_PAD_SELECTION',selector))
        for name,flags in [('diagnostic',['-DMELEE_WEB_MATCH_FRAME_EFFECT_DIAGNOSTIC']),
                           ('public',['-DMELEE_WEB_PUBLIC_RUNTIME'])]:
            binary=scratch/name
            argv=[compiler,'-std=c++20','-O3','-DNDEBUG','-DTARGET_PC',*flags,'-I',str(ROOT/'src'),'-I',str(ROOT/'.deps/aurora/include'),str(cpp),'-o',str(binary)]
            build=subprocess.run(argv,capture_output=True,text=True,timeout=90)
            (scratch/(name+'-compile.log')).write_text(build.stdout+build.stderr)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            (scratch/(name+'-run.log')).write_text(run.stdout+run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('actual shared match/audio/finish endpoint-order controls passed',run.stdout)
