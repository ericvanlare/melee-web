"""Actual private CPU components; synthetic boundaries, no browser acceptance."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import unittest

ROOT = Path(os.environ.get('MELEE_CPU_CHECKPOINT_REPO_ROOT',Path(__file__).resolve().parents[1])).resolve()
SOURCE = Path(os.environ.get('MELEE_CPU_CHECKPOINT_CANDIDATE_ROOT',ROOT)).resolve()
sys.path.insert(0,str(ROOT/'tests'))
sys.path.insert(0,str(ROOT/'scripts'))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime

class GameplayCpuCheckpointControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emcc=ROOT/'.deps/emsdk/upstream/emscripten/emcc'
        cls.emxx=ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not cls.emcc.is_file() or not cls.emxx.is_file():
            raise unittest.SkipTest('Pinned Emscripten toolchain unavailable')
        cls.work=cls.new_workspace(ROOT,'gameplay-cpu-checkpoint-')

    def compile_run(self,name,sources,extra=(),cpp=True):
        directory=self.work/name;directory.mkdir();temporary=directory/'tmp';temporary.mkdir()
        binary=directory/'control.js'
        command=[str(self.emxx if cpp else self.emcc),'-std=c++20' if cpp else '-std=gnu11','-O1','-UNDEBUG','-DTARGET_PC',
                 '-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-ffp-contract=off','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                 '-I'+str(SOURCE/'src'),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/gameplay-source/src'),
                 '-I'+str(ROOT/'build/gameplay-source/src/melee/gm'),'-I'+str(ROOT/'build/gameplay-source/src/melee/lb'),
                 '-I'+str(ROOT/'.deps/melee/src'),'-I'+str(ROOT/'.deps/aurora/include'),'-I'+str(ROOT/'.deps/melee/extern/dolphin/include'),
                 *extra,*map(str,sources),'-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-o',str(binary)]
        env=dict(os.environ,TMPDIR=str(temporary))
        compiled=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True,timeout=120)
        (directory/'compile.log').write_text(compiled.stdout+compiled.stderr)
        self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
        run=subprocess.run([str(node_runtime()),str(binary)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=30)
        (directory/'run.log').write_text(run.stdout+run.stderr)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        return run.stdout

    @staticmethod
    def extract_function(text,signature):
        start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
        while depth:
            if text[end]=='{':depth+=1
            elif text[end]=='}':depth-=1
            end+=1
        return text[start:end]

    def test_actual_rumble_interpreter_capture_only_journal(self):
        source=ROOT/'.deps/melee/src/sysdolphin/baselib/rumble.c'
        actual=self.extract_function(source.read_text(),'void HSD_PadRumbleInterpret(void)')
        fixture=(ROOT/'tests/cpu_interval_motor_journal_control.c').read_text()
        self.assertEqual(fixture.count('@ACTUAL_INTERPRETER@'),1)
        control=self.work/'motor-journal-control.c'
        control.write_text(fixture.replace('@ACTUAL_INTERPRETER@',actual))
        output=self.compile_run('motor-journal',[SOURCE/'src/gameplay_cpu_interval.c',control],
            ['-include',str(ROOT/'src/hsd_probe_compat.h'),
             '-I'+str(ROOT/'.deps/melee/src'),
             '-I'+str(ROOT/'.deps/melee/extern/dolphin/include')],cpp=False)
        self.assertIn('Actual HSD rumble interpreter preserves source state',output)
        self.assertIn('Legacy MOTOR barrier, token/frame/channel validation and fixed journal bounds passed',output)

    def test_match_rumble_bracket_uses_authoritative_source_clock(self):
        match_context=(SOURCE/'src/gameplay_match_context.c').read_text()
        bracket=self.extract_function(match_context,'static int interpret_match_rumble(')
        control=self.work/'match-rumble-source-clock.c'
        control.write_text(r'''#include "gameplay_cpu_interval.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct MeleeWebMatchContext { uint64_t generation,ticks; } MeleeWebMatchContext;
static uint64_t owner_epoch=11;
static uint32_t source_frame=401;
static unsigned source_frame_reads;
static int fail(char* e,size_t n,const char* message){if(e&&n)snprintf(e,n,"%s",message);return 0;}
uint32_t melee_web_match_source_frames(void){++source_frame_reads;return source_frame;}
void HSD_PadRumbleInterpret(void){melee_web_cpu_interval_pad_motor(2,1);}
void PADControlMotor(unsigned channel,unsigned command){(void)channel;(void)command;assert(!"capture-only route dispatched hardware");}
'''+bracket+r'''
int main(void){
 MeleeWebMatchContext context={9,3};char error[160]={0};
 uint64_t token=melee_web_cpu_interval_begin();assert(token);
 assert(melee_web_cpu_interval_motor_capture_begin(token,context.generation,&context,
        owner_epoch,source_frame,1));
 assert(interpret_match_rumble(&context,error,sizeof(error)));
 assert(source_frame_reads==1&&context.ticks==3);
 const MeleeWebCpuMotorEvidence* evidence=melee_web_cpu_interval_motor_evidence();
 assert(evidence&&evidence->baseline.frames_seen==1&&evidence->baseline.event_count==1);
 assert(evidence->baseline.events[0].source_frame==401&&
        evidence->baseline.events[0].channel==2&&evidence->baseline.events[0].command==1);
 assert(melee_web_cpu_interval_motor_baseline_seal(token,402));
 assert(melee_web_cpu_interval_failure()==MELEE_WEB_CPU_EFFECT_NONE);
 assert(melee_web_cpu_interval_end(token));
 puts("Production rumble bracket binds one authoritative source-clock frame across interpretation; raw-step counter remains separate");
}
''')
        output=self.compile_run('match-rumble-source-clock',
            [SOURCE/'src/gameplay_cpu_interval.c',control],cpp=False)
        self.assertIn('Production rumble bracket binds one authoritative source-clock frame',output)

    def test_actual_static_engine_refusal_and_prepared_noalloc_commit(self):
        output=self.compile_run('statics',[SOURCE/'src/gameplay_cpu_statics.cpp',SOURCE/'tests/cpu_statics_control.cpp'],
            ['-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0'])
        self.assertIn('Actual CPU static range engine',output)

    def test_actual_fixed_coordinator_injected_refusals_and_dependency_order(self):
        output=self.compile_run('coordinator',[SOURCE/'src/gameplay_cpu_statics.cpp',SOURCE/'src/gameplay_cpu_commit.cpp',
            SOURCE/'tests/cpu_commit_control.cpp'],['-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0'])
        self.assertIn('eight admission refusals write zero',output)
        self.assertIn('Corrected coordinator preserves all refusal-before-write checks',output)

    def test_actual_fixed_coordinator_light_anchor_refuses_before_writes(self):
        output=self.compile_run('coordinator-light',[SOURCE/'src/gameplay_cpu_statics.cpp',SOURCE/'src/gameplay_cpu_commit.cpp',
            SOURCE/'tests/cpu_commit_control.cpp'],['-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0',
                '-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=1'])
        self.assertIn('nine admission refusals write zero; light leaf modeled',output)
        self.assertIn('Corrected coordinator preserves all refusal-before-write checks',output)

    def test_actual_fixed_coordinator_texture_anchor_refuses_before_writes(self):
        output=self.compile_run('coordinator-texture',[SOURCE/'src/gameplay_cpu_statics.cpp',SOURCE/'src/gameplay_cpu_commit.cpp',
            SOURCE/'tests/cpu_commit_control.cpp'],['-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0',
                '-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=1','-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1'])
        self.assertIn('ten admission refusals write zero; light/texture leaves modeled',output)
        self.assertIn('Corrected coordinator preserves all refusal-before-write checks',output)

    def test_actual_fixed_coordinator_particle_anchor_refuses_before_writes(self):
        output=self.compile_run('coordinator-particle',[SOURCE/'src/gameplay_cpu_statics.cpp',SOURCE/'src/gameplay_cpu_commit.cpp',
            SOURCE/'tests/cpu_commit_control.cpp'],['-fexceptions','-sDISABLE_EXCEPTION_CATCHING=0',
                '-DMELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE=1','-DMELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE=1',
                '-DMELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE=1'])
        self.assertIn('eleven admission refusals write zero; light/texture/particle leaves modeled',output)
        self.assertIn('Corrected coordinator preserves all refusal-before-write checks',output)

    def test_actual_effect_barrier_and_inactive_transport_link(self):
        def function(text,needle):
            start=text.index(needle);brace=text.index('{',start);depth=1;end=brace+1
            while depth:
                if text[end]=='{':depth+=1
                elif text[end]=='}':depth-=1
                end+=1
            return text[start:end]+'\n'
        text=(SOURCE/'src/gameplay_audio_stream.c').read_text()
        stream=text[:text.index('static int fail(')]
        for name in ['static int fail(', 'static int hps_interval_guard(',
                     'static void stop(', 'static const StreamFile* file_at(',
                     'static void probe_record(',
                     'static const MeleeWebAudioStreamBlock* block_at(',
                     'static const char* validate_stream_request(',
                     'static int stream_interval_bound(', 'int HSD_DevComRequest(']:
            stream+=function(text,name)
        # This closed-barrier fixture never constructs an HPS stream/receipt.
        # The actual binding helper must short-circuit before its live matcher;
        # accidental bound-path reach is a hard failure, never modeled success.
        # The original-HPS trace separately exercises real bound admission.
        stream+='static int snapshot_matches_live(MeleeWebAudio* a,const MeleeWebAudioStreamSnapshot* p,MeleeWebAudioStream* s){abort();}\n'
        stream+='int fixture_hps_interval_site(const char* site){return hps_interval_guard(site);}\n'
        stream+='int melee_web_audio_bank_transport_file(int f){return 0;}\nint melee_web_audio_bank_transport_active(void){return 0;}\n'
        stream_file=self.work/'effect-stream.c';stream_file.write_text(stream)
        text=(SOURCE/'src/gameplay_audio_bank_transport.c').read_text()
        bank=text[:text.index('static uint32_t be32(')]
        bank+=function(text,'static MeleeWebAudioResidencyInfo info(')
        bank+=function(text,'int melee_web_audio_bank_transport_request(')
        bank+='''
int melee_web_audio_residency_info(const MeleeWebAudioResidency* r,int f,MeleeWebAudioResidencyInfo* i,char* e,size_t n){abort();}
void fixture_bank_barrier(void){
 unsigned char before[sizeof(state)];memcpy(before,&state,sizeof(state));
 assert(melee_web_audio_bank_transport_request(-1,0,0,0,0,0,NULL,NULL)==-1);
 assert(memcmp(before,&state,sizeof(state))==0);
 assert(melee_web_audio_heap_alloc(32)==NULL);melee_web_audio_heap_free((void*)0x1000);
 assert(memcmp(before,&state,sizeof(state))==0);
}
'''
        bank=bank.replace('#include <stdio.h>','#include <stdio.h>\n#include <assert.h>')
        bank_file=self.work/'effect-bank.c';bank_file.write_text(bank)
        output=self.compile_run('effects',[SOURCE/'src/gameplay_cpu_interval.c',SOURCE/'src/gameplay_source_files.c',
            stream_file,bank_file,SOURCE/'tests/cpu_interval_effect_control.c'],
            ['-include',str(ROOT/'src/gameplay_compat.h')],cpp=False)
        self.assertIn('inactive continuation passed',output)
        run_log=(self.work/'effects/run.log').read_text()
        self.assertEqual(run_log.count('HPS_INTERVAL_FIRST_REFUSAL site=devcom-request'),1,run_log)
        self.assertNotIn('HPS_INTERVAL_FIRST_REFUSAL site=later-site',run_log)
        failed=subprocess.run([str(node_runtime()),str(self.work/'effects/control.js'),'invalid'],cwd=ROOT,
            capture_output=True,text=True,timeout=30)
        (self.work/'effects/ended-invalid-request.log').write_text(failed.stdout+failed.stderr)
        self.assertNotEqual(failed.returncode,0)
        self.assertIn('unsupported or unowned source DVD/DevCom transfer',failed.stderr)
        # No gameplay interval object/library is linked on the generic path.
        ordinary=self.work/'ordinary-source-files.c'
        ordinary.write_text('''#include "gameplay_source_files.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned calls;
static void done(int r,int a,void* b,int c){assert(!c);++calls;}
int main(void){unsigned char bytes[32]={4,5};_Alignas(32) unsigned char out[32]={0};
MeleeWebSourceFileInput f={"test.dat",bytes,32};char e[160];
MeleeWebSourceFileScope* s=melee_web_source_files_begin(&f,1,e,sizeof(e));assert(s);
assert(melee_web_source_file_region_register(out,32,e,sizeof(e)));int r=0;
assert(melee_web_source_file_request(melee_web_source_file_entry("test.dat"),0,(uintptr_t)out,32,0x21,1,done,NULL,&r,e,sizeof(e)));
assert(melee_web_source_files_pump(e,sizeof(e))&&calls==1&&!memcmp(bytes,out,32));
assert(melee_web_source_file_region_unregister(out,32,e,sizeof(e)));assert(melee_web_source_files_end(s,e,sizeof(e)));
puts("Generic source-file bridge links without interval implementation and preserves ordinary copied-byte requests");}
''')
        self.compile_run('ordinary-source-files',[SOURCE/'src/gameplay_source_files.c',ordinary],
            ['-UMELEE_WEB_CPU_INTERVAL_ENABLED'],cpp=False)

    def test_actual_audio_snapshot_scalar_storage_lifetime(self):
        control=self.work/'audio-scalars.c'
        control.write_text('#include "'+str(SOURCE/'src/gameplay_audio.c')+'"'+r'''
#include <assert.h>
int main(void){
 MeleeWebAudio state={0};state.generation=17;active=&state;
 command_budget=65532;source_sound_mode=1;
 MeleeWebAudio before=state;
 MeleeWebAudioCpuStorage s=melee_web_audio_cpu_storage(&state,17);
 assert(s.command_budget==&command_budget&&s.sound_mode==&source_sound_mode);
 assert(*s.command_budget==65532&&*s.sound_mode==1);
 assert(!melee_web_audio_cpu_storage((MeleeWebAudio*)1,17).command_budget);
 assert(!melee_web_audio_cpu_storage(&state,16).command_budget);
 state.source_closed=1;assert(!melee_web_audio_cpu_storage(&state,17).sound_mode);state.source_closed=0;
 assert(!memcmp(&before,&state,sizeof(state))&&command_budget==65532&&source_sound_mode==1);
 active=NULL;assert(!melee_web_audio_cpu_storage(&state,17).command_budget);
 active=&state;++state.generation;assert(!melee_web_audio_cpu_storage(&state,17).command_budget);
 puts("Actual audio scalar storage pointer/extent, foreign owner, generation, retirement and nonmutation passed");
}
''')
        self.compile_run('audio-scalars',[control],['-include',str(ROOT/'src/gameplay_compat.h'),'-include',str(SOURCE/'src/gameplay_audio.h')],cpp=False)

    def test_actual_cpu_runner_and_existing_census_node_contracts(self):
        mirror=self.work/'node-source';mirror.mkdir()
        files=['src/slippi_profile/tests/'+name for name in [
            'cpu_restore_runner.mjs','test_cpu_restore_runner.mjs','cpu_two_tick_runner.mjs',
            'test_cpu_two_tick_runner.mjs','cpu_seven_tick_runner.mjs',
            'test_cpu_seven_tick_runner.mjs','cpu_corrected_runner.mjs','test_cpu_corrected_runner.mjs',
            'cpu_corrected_two_window_runner.mjs','test_cpu_corrected_two_window_runner.mjs',
            'cpu_endpoint.mjs','test_cpu_endpoint.mjs','run_synctest.mjs',
            'run_write_census.mjs','runner_contract.mjs','test_write_census_contract.mjs','synctest_browser.cpp']]
        files+=['scripts/browser_tools.mjs','scripts/net_rollback_input_timeline.mjs',
                'scripts/net_lockstep_core.mjs','tests/quiescent_wasm_snapshot.mjs']
        for relative in files:
            source=SOURCE/relative
            if not source.is_file():source=ROOT/relative
            destination=mirror/relative;destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,destination)
        directory=mirror/'src/slippi_profile/tests'
        for module in ['cpu_restore_runner.mjs','cpu_two_tick_runner.mjs','cpu_seven_tick_runner.mjs',
                       'cpu_corrected_runner.mjs','cpu_endpoint.mjs',
                       'cpu_corrected_two_window_runner.mjs',
                       'run_synctest.mjs','run_write_census.mjs']:
            result=subprocess.run([str(node_runtime()),'--check',str(directory/module)],capture_output=True,text=True,timeout=30)
            (mirror/(module+'.syntax.log')).write_text(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        result=subprocess.run([str(node_runtime()),'--test',str(directory/'test_cpu_restore_runner.mjs'),
                               str(directory/'test_cpu_two_tick_runner.mjs'),str(directory/'test_cpu_seven_tick_runner.mjs'),
                               str(directory/'test_cpu_corrected_runner.mjs'),str(directory/'test_cpu_endpoint.mjs'),
                               str(directory/'test_cpu_corrected_two_window_runner.mjs'),
                               str(directory/'test_write_census_contract.mjs')],cwd=mirror,
                              capture_output=True,text=True,timeout=60)
        (mirror/'node-contracts.log').write_text(result.stdout+result.stderr)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_actual_flow_clock_cadence_bootstrap_cpu_writers(self):
        clock=Path(os.environ.get('MELEE_WEB_TEST_CLOCK_SOURCE',ROOT/'build/gameplay-source/src/melee/gm/gm_1A45.c')).resolve()
        cadence=Path(os.environ.get('MELEE_WEB_TEST_CADENCE_SOURCE',ROOT/'build/gameplay-source/src/melee/lb/lb_0195.c')).resolve()
        if not clock.is_file() or not cadence.is_file():
            self.skipTest('Generated original clock/cadence source unavailable')
        bootstrap=self.work/'bootstrap-control.c'
        bootstrap.write_text((SOURCE/'src/gameplay_bootstrap.c').read_text()+'''
/* Synthetic owner setup uses the ACTUAL production private declarations. */
void fixture_bootstrap_owner_begin(void){arena=(void*)0x1000;tables_live=1;generation=9;disabled_links=0;}
void fixture_bootstrap_owner_rebind(void){++generation;}
u64* fixture_bootstrap_mask(void){return &disabled_links;}
''')
        output=self.compile_run('clock-cadence',[SOURCE/'tests/flow_cpu_checkpoint_control.c',bootstrap],
            ['-include',str(ROOT/'src/gameplay_compat.h'),f'-DMELEE_WEB_TEST_CLOCK_SOURCE="{clock.as_posix()}"',
             f'-DMELEE_WEB_TEST_CADENCE_SOURCE="{cadence.as_posix()}"'],cpp=False)
        self.assertIn('Actual flow clock cadence CPU restore',output)

if __name__=='__main__':unittest.main()
