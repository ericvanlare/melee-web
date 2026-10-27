"""Exact opt-in registration admission; modeled framing is not a capture receipt."""
from pathlib import Path
import copy
import hashlib
import io
import json
import os
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import test_stadium_paired_idle_profile as base
def exact_module(name, relative):
    path = base.SOURCE_ROOT / relative
    module = __import__(name)
    if Path(module.__file__).resolve() != path.resolve():
        module = base.load_exact(name, path)
    if Path(module.__file__).resolve() != path.resolve():
        raise AssertionError('selected registration module source differs')
    return module

allocation = exact_module('capture_allocation_history', 'scripts/capture_allocation_history.py')
validate_stadium_paired_allocation_environment = allocation.validate_stadium_paired_allocation_environment
validate_stadium_registration_scope = allocation.validate_stadium_registration_scope
iter_stadium_registration_rows = allocation.iter_stadium_registration_rows
long_idle = exact_module('stadium_whole_long_idle', 'tools/stadium_whole_long_idle.py')
PAIRED_REGISTRATION_PROFILE = long_idle.PAIRED_REGISTRATION_PROFILE
LongIdleBObservations = long_idle.LongIdleBObservations
semantics = exact_module('reference_capture_semantics', 'tools/reference_capture_semantics.py')
base.whole_session_replay = exact_module('whole_session_replay', 'tools/whole_session_replay.py')

NEW = 'paired_idle_registration_v1'
OLD = 'paired_idle_scene_endpoints_v1'
HASH = 'd4dcc28edcf534586b3953d3235e8880173f6dbcd9ecb384e97991137646756e'
ENV = {'MWRC_STADIUM_WHOLE_PAIRED_IDLE': NEW, 'MWRC_STADIUM_WHOLE_LONG_IDLE': NEW,
       'MWRC_ALLOCATION_PAIRED_REGISTRATION_PROFILE': NEW,
       'MWRC_WHOLE_SESSION_MATCHES': '2', 'MWRC_STADIUM_WHOLE_FEEDBACK': '1',
       'MWRC_STADIUM_WHOLE_B_STATE': '1', 'MWRC_ALLOCATION_VS_TARGET': '2',
       'MWRC_ALLOCATION_STOP_AT': 'exit', 'MWRC_ALLOCATION_MAX_EVENTS': '2000000'}


def extract(source, marker):
    start = source.index(marker); brace = source.index('{', start); depth = 0
    for index in range(brace, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if depth == 0: return source[start:index + 1]
    raise AssertionError('source function did not close')


class StadiumPairedRegistrationProfileTests(unittest.TestCase):
    def test_actual_native_primary_and_allocation_startup_helpers(self):
        root = base.SOURCE_ROOT / 'reference-capture/dolphin/source/Core/PowerPC'
        primary = extract((root/'ReferenceCaptureObserver.cpp').read_text(), 'bool PairedIdleStartupProfilesAgree(')
        alloc = extract((root/'ReferenceAllocationObserver.cpp').read_text(), 'bool PairedRegistrationCaptureScopeAgree(')
        statements = '\n'.join('setenv('+json.dumps(k)+','+json.dumps(v)+',1);' for k,v in ENV.items())
        fixture = '#include <string_view>\n#include <cstdlib>\nusing u32=unsigned;\n'+primary+'\n'+alloc+'''
int main() {
'''+statements+'''
unsetenv("MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE");
if (!PairedRegistrationCaptureScopeAgree("'''+NEW+'","'+HASH+'''")) return 1;
if (!PairedIdleStartupProfilesAgree(true,2,"","'''+NEW+'","'+NEW+'''","2000000","2","exit")) return 2;
if (PairedRegistrationCaptureScopeAgree("'''+NEW+'''","bad")) return 3;
setenv("MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE","'''+OLD+'''",1);
if (PairedRegistrationCaptureScopeAgree("'''+NEW+'","'+HASH+'''")) return 4;
unsetenv("MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE");
setenv("MWRC_ALLOCATION_MAX_EVENTS","64",1);
if (PairedRegistrationCaptureScopeAgree("'''+NEW+'","'+HASH+'''")) return 5;
if (PairedIdleStartupProfilesAgree(true,2,"","'''+NEW+'","'+NEW+'''","64","2","exit")) return 6;
if (PairedIdleStartupProfilesAgree(true,2,"'''+OLD+'","'+NEW+'","'+NEW+'''","2000000","2","exit")) return 7;
if (!PairedIdleStartupProfilesAgree(true,2,"'''+OLD+'","'+OLD+'''")) return 8;
return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            cpp=Path(temporary)/'scope.cpp';binary=cpp.with_suffix('');cpp.write_text(fixture)
            built=subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            ran=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stderr)

    def test_exact_host_startup_and_mixed_profile_refusals(self):
        self.assertEqual(validate_stadium_paired_allocation_environment(ENV)['max_events'],2000000)
        for key in ENV:
            bad=dict(ENV);bad.pop(key)
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_stadium_paired_allocation_environment(bad)
        bad=dict(ENV,MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE=OLD)
        with self.assertRaises(ValueError): validate_stadium_paired_allocation_environment(bad)
        self.assertEqual(allocation.PAIRED_REGISTRATION_PROFILE_SHA256,HASH)

    def test_actual_primary_identity_and_mixed_announcement_refusal(self):
        w=base.whole_session_replay
        declaration=dict(whole_session=True,match_count=2,capture_id='registration-test',sequence_id='registration-test',
            stadium_whole_feedback=True,setup_receipt_sha256='a'*64,stadium_whole_b_state=True,
            stadium_observed_matches=[0,1],stadium_whole_long_idle=PAIRED_REGISTRATION_PROFILE,source_revision='GALE01r2')
        handshake=dict(declaration,schema=w.EXPECTED_OBSERVER_SCHEMA,version=1,dolphin_commit=w.EXPECTED_DOLPHIN_COMMIT,
            dol_sha1=w.EXPECTED_DOL_SHA1,dol_sha256=w.EXPECTED_DOL_SHA256,cpu='JITARM64',writes_guest_memory=False)
        def rows(start):
            return [dict(seq=0,event='handshake',payload=handshake),dict(seq=1,event='start',payload=start),
                    dict(seq=2,event='end',payload={'status':'completed','natural':True})]
        self.assertEqual(w._capture_identity(rows(declaration))['match_count'],2)
        # Admission succeeds, then the unchanged full-route validator refuses missing boundaries.
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError, 'no flagged boundaries'):
            semantics.validate_whole_session_observer_records(rows(declaration))
        bad=dict(declaration,stadium_whole_long_idle=dict(PAIRED_REGISTRATION_PROFILE,profile=OLD))
        with self.assertRaises(w.WholeSessionReplayError): w._capture_identity(rows(bad))
        self.assertTrue(LongIdleBObservations(PAIRED_REGISTRATION_PROFILE).paired)
        with self.assertRaises(ValueError): LongIdleBObservations(dict(PAIRED_REGISTRATION_PROFILE,natural_tail_max=5))

    def test_receiver_and_cli_require_explicit_paired_route(self):
        driver=base.driver
        r=driver.StadiumWholeFeedbackReceiver(base.stadium_go_feedback_packet(),'a'*64,{},
             stadium_whole_b_state=True,stadium_whole_long_idle=True,
             stadium_whole_paired_idle=True,stadium_whole_paired_registration=True)
        self.assertEqual(r.paired_observation_profile(),PAIRED_REGISTRATION_PROFILE)
        with self.assertRaises(ValueError):
            driver.StadiumWholeFeedbackReceiver(base.stadium_go_feedback_packet(),'a'*64,{},stadium_whole_paired_registration=True)
        captured={}
        args=['--dolphin','x','--disc','x','--profile','x','--menu-recipe','x','--output','x','--build-manifest','x',
              '--stadium-whole-paired-idle','--stadium-whole-paired-registration']
        with patch.object(driver,'run',side_effect=lambda **kw:captured.update(kw) or {}),patch('sys.stdout',new=io.StringIO()):
            self.assertEqual(driver.main(args),0)
        self.assertTrue(captured['stadium_whole_paired_registration'])

    def test_streaming_framing_retains_broad_rows_and_refuses_gaps(self):
        rows=[{'record':'header','sequence':0}]+[{'record':'enter','sequence':i,'function':'HSD_GObjProc_8038FD54'} for i in range(1,100)]+[{'record':'end','sequence':100}]
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'rows.jsonl'
            def write(values):path.write_text(''.join(json.dumps(x)+'\n' for x in values))
            write(rows);inventory={}
            self.assertEqual(list(iter_stadium_registration_rows(path,inventory)),rows)
            self.assertEqual(inventory['row_count'],101)
            self.assertFalse(inventory['registration_semantics_validated'])
            for values in (rows[:-1],rows[:2]+rows[3:],rows+[rows[-1]],rows+[{'record':'error','sequence':101}]):
                write(values)
                with self.assertRaises(ValueError):list(iter_stadium_registration_rows(path,{}))

    def test_actual_observer_filter_stack_accounting_and_real_loader(self):
        # Reuse the existing modeled Dolphin memory/crypto leaves, compiling the
        # complete actual observer. This is source protocol evidence, not PPC execution.
        fixture_module=base.load_exact('_paired_registration_observer_fixture',
            base.SUPPORT_ROOT/'tests/test_reference_allocation_observer.py')
        component='#include <cstdlib>\n'+fixture_module.COMPONENT
        component=component.replace('  assert(Observer::Arm(fixture.profile, output));',
            '  fixture.profile.profile_sha256 = "'+HASH+'";\n  assert(Observer::Arm(fixture.profile, output));',1)
        block=r'''  if (mode == "registration-selected" || mode == "registration-filtered-child") {
    auto index = [&](std::string_view name) {
      for (size_t i=0;i<fixture.functions.size();++i)
        if (name == fixture.functions[i].name) return i;
      std::abort();
    };
    for (u32 pool : {0x804ce38cu,0x804ce3b8u,0x804c2580u})
      for (u32 offset=0;offset<44;offset+=4) fixture.system.memory.Write(pool+offset,0);
    auto enter = [&](std::string_view name,u32 arg) {
      fixture.cpu.gpr[3]=arg;fixture.cpu.gpr[4]=0x80040000u;fixture.cpu.gpr[5]=4;
      fixture.cpu.pc=fixture.functions[index(name)].address;
      Observer::Observe(&fixture.system,fixture.cpu.pc,&fixture.cpu);
    };
    auto leave = [&](std::string_view name) {
      fixture.cpu.gpr[3]=0x80030000u;
      fixture.cpu.pc=fixture.returns[index(name)];
      Observer::Observe(&fixture.system,fixture.cpu.pc,&fixture.cpu);
    };
    if (mode == "registration-filtered-child") {
      enter("gm_Scene_Vs_OnEnter",0x80030000u);
      enter("HSD_ObjAlloc",0x804c2580u);
      enter("HSD_GObj_SetupProc",0x80030000u);
      assert(Observer::Error().find("filtered pool parent") != std::string::npos);
      assert(!Observer::Finish(false));return 0;
    }
    assert(!Observer::IsBoundary(fixture.functions[index("HSD_MemAlloc")].address));
    for (int lifetime=0;lifetime<2;++lifetime) {
      enter("gm_Scene_Vs_OnEnter",0x80030000u);leave("gm_Scene_Vs_OnEnter");
      for (auto name : {"HSD_ObjAlloc","HSD_ObjFree","HSD_ObjAllocInit"}) {
        enter(name,0x804c2580u);leave(name);
      }
      enter("HSD_GObj_SetupProc",0x80030000u);
      enter("HSD_ObjAlloc",0x804ce3b8u);leave("HSD_ObjAlloc");
      leave("HSD_GObj_SetupProc");
      enter("HSD_ObjAllocInit",0x804ce38cu);leave("HSD_ObjAllocInit");
      enter("HSD_ObjFree",0x804ce38cu);leave("HSD_ObjFree");
      for (auto name : {"HSD_GObjProc_8038FAA8","HSD_GObjProc_8038FC18",
                        "HSD_GObjProc_8038FCE4","HSD_GObjProc_8038FE24","_HSD_ObjAllocForgetMemory"}) {
        enter(name,0x80030000u);leave(name);
      }
      enter("gm_Scene_Vs_OnExit",0x80030000u);leave("gm_Scene_Vs_OnExit");
    }
    assert(Observer::Finish(true));return 0;
  }
'''
        component=component.replace('  if (mode == "gobjproc-registration") {',block+'  if (mode == "gobjproc-registration") {',1)
        from tools.allocation_history_replay import load_trace,ReplayProblem
        with tempfile.TemporaryDirectory() as temporary:
            temp=Path(temporary)
            for name,content in fixture_module.STUBS.items():
                target=temp/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(content)
            source=temp/'component.cpp';source.write_text(component);binary=temp/'component'
            root=base.SOURCE_ROOT/'reference-capture/dolphin/source'
            support=base.SUPPORT_ROOT/'reference-capture/dolphin/source'
            built=subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fno-exceptions','-Wall','-Wextra','-Werror',
                '-I',str(temp),'-I',str(root),'-I',str(support),str(root/'Core/PowerPC/ReferenceAllocationObserver.cpp'),
                str(source),'-lz','-o',str(binary)],text=True,capture_output=True,timeout=120)
            self.assertEqual(built.returncode,0,built.stderr)
            env={k:v for k,v in os.environ.items() if not k.startswith('MWRC_')};env.update(ENV)
            path=temp/'selected.jsonl'
            result=subprocess.run([str(binary),'registration-selected',str(path)],env=env,text=True,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            header,rows,enters,returns=load_trace(path,disk_backed=False)
            self.assertEqual(header['recording_filter'],allocation.PAIRED_REGISTRATION_FILTER)
            self.assertEqual(header['scope'],'selected-gobjproc-registration')
            self.assertEqual(rows[-1]['calls'],len(enters));self.assertEqual(len(enters),len(returns))
            self.assertEqual(rows[-1]['filtered_pool_calls'],6)
            self.assertEqual(len(enters),22)
            self.assertTrue(all(r['args'][0] in (0x804ce38c,0x804ce3b8) for r in enters.values()
                                if r['function'] in ('HSD_ObjAlloc','HSD_ObjFree','HSD_ObjAllocInit')))
            self.assertTrue(any(r.get('parent') is not None for r in enters.values()))
            wrong=[dict(r) for r in rows];wrong[-1]['calls']+=6
            path.write_text(''.join(json.dumps(r)+'\n' for r in wrong))
            with self.assertRaises(ReplayProblem):load_trace(path,disk_backed=False)
            failed=temp/'filtered-child.jsonl'
            result=subprocess.run([str(binary),'registration-filtered-child',str(failed)],env=env,text=True,capture_output=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            _,failed_rows,failed_enters,_=load_trace(failed,disk_backed=False)
            self.assertEqual(len(failed_enters),1)
            self.assertNotEqual(failed_rows[-1]['status'],'captured')
            self.assertIn('filtered pool parent',failed_rows[-1]['error'])

    def test_broad_scope_requires_exact_profile_and_complete_exits(self):
        # Optional real generated profile; ordinary CI uses an explicitly modeled profile/hash.
        actual=os.environ.get('MELEE_STADIUM_PAIRED_REGISTRATION_PROFILE')
        profile=json.loads(Path(actual).read_text()) if actual else {'version':4,'registration_profile':'gobjproc-registration-v1'}
        digest=hashlib.sha256((json.dumps(profile,sort_keys=True,indent=2)+'\n').encode()).hexdigest()
        if actual:
            self.assertEqual(digest,HASH)
            self.assertEqual(base.driver.stadium_whole_allocation_profile_bytes(profile,registration=True),Path(actual).read_bytes())
        self.assertEqual(base.driver.stadium_whole_allocation_profile_bytes(profile),base.driver.canonical(profile))
        header=dict(scope='selected-gobjproc-registration',recording_filter=allocation.PAIRED_REGISTRATION_FILTER,paired_registration_profile=NEW,profile_version=4,registration_profile='gobjproc-registration-v1',
            profile_sha256=digest,allocation_history_complete=False,registration_lifetime_complete=False,
            vs_target=2,stop_at='exit',max_events=2000000,compression='none')
        footer=dict(header,status='captured',target_complete=True,vs_entries=2,vs_exits=2,active_owner=False,pending_calls=[],
                    calls=22,emitted_calls=22,filtered_pool_calls=6,filtered_pool_repeated_stops=0)
        with patch.object(allocation,'PAIRED_REGISTRATION_PROFILE_SHA256',digest):
            self.assertEqual(validate_stadium_registration_scope(header,footer,profile),digest)
            self.assertEqual(allocation.exact_stadium_allocation_profile_hash(profile),digest)
            for key,value in [('max_events',64),('scope','scene-rng-endpoints-only'),('profile_version',3),('vs_exits',1),('registration_lifetime_complete',True),('profile_sha256','0'*64),('emitted_calls',23),('filtered_pool_calls',-1)]:
                with self.subTest(key=key),self.assertRaises(ValueError):validate_stadium_registration_scope(header,dict(footer,**{key:value}),profile)
            with self.assertRaises(ValueError):validate_stadium_registration_scope(header,footer,dict(profile,version=3))

if __name__=='__main__':unittest.main()
