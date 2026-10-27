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
import zlib
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
HASH = 'b1f4b68f72ba9e0af754c4100845860321cac6b537f3803d6fe41e3d68626e09'
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

    def test_binary_reader_admits_exact_registration_and_rejects_mixed_profiles(self):
        stream = exact_module('reference_observer_stream',
                              'reference-capture/dolphin/reference_observer_stream.py')
        declaration = dict(whole_session=True, match_count=2,
            stadium_whole_feedback=True, setup_receipt_sha256='a' * 64,
            stadium_whole_b_state=True, stadium_observed_matches=[0, 1],
            stadium_whole_long_idle=PAIRED_REGISTRATION_PROFILE)

        def frame(code, sequence, value):
            payload = json.dumps(value, separators=(',', ':')).encode()
            return stream.HEADER.pack(stream.MAGIC_U32, stream.SCHEMA_VERSION,
                code, sequence, sequence + 10, 0, 0, 0, len(payload),
                zlib.crc32(payload) & 0xffffffff) + payload

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'registration.mwro'
            def read(handshake, start):
                path.write_bytes(frame(1, 0, handshake) + frame(2, 1, start))
                return list(stream.iter_records(path))

            self.assertEqual(len(read(declaration, declaration)), 2)
            legacy = dict(declaration, stadium_whole_long_idle=stream.PAIRED_IDLE_PROFILE)
            self.assertEqual(len(read(legacy, legacy)), 2)
            for first, second in ((declaration, legacy), (legacy, declaration)):
                with self.subTest(mixed=True), self.assertRaisesRegex(
                        stream.ObserverStreamError, 'disagrees with prior announcement'):
                    read(first, second)
            for mutation in ({'profile': 'unknown'}, {'comparison_post_go_ticks': 3501},
                             {'natural_tail_max': 5}, {'selected_matches': [1, 0]},
                             {'match_count': True}, {'extra': 1}):
                bad = dict(declaration, stadium_whole_long_idle={
                    **PAIRED_REGISTRATION_PROFILE, **mutation})
                with self.subTest(mutation=mutation), self.assertRaises(stream.ObserverStreamError):
                    read(bad, bad)
            for key, value in (('match_count', 3), ('stadium_whole_feedback', False),
                               ('setup_receipt_sha256', '0' * 64),
                               ('stadium_observed_matches', [1, 0])):
                bad = dict(declaration, **{key: value})
                with self.subTest(key=key), self.assertRaises(stream.ObserverStreamError):
                    read(bad, bad)

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
            '  fixture.profile.profile_sha256 = "'+HASH+'";\n  static std::array<u32,57> model_words; model_words.fill(0x4e800020);\n  for (size_t i=0;i<fixture.functions.size();++i) fixture.functions[i].return_words=&model_words[i];\n  assert(Observer::Arm(fixture.profile, output));',1)
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

    def test_actual_observer_conditional_returns_taken_untaken_and_guard_refusals(self):
        leaves=base.load_exact('_conditional_return_observer_fixture',
            base.SUPPORT_ROOT/'tests/test_reference_allocation_observer.py')
        component='#include <cstdlib>\n'+leaves.COMPONENT
        setup=r'''
  static std::array<u32,57> words;words.fill(0x4e800020u);
  for (size_t i=0;i<fixture.functions.size();++i) fixture.functions[i].return_words=&words[i];
  const bool baseline=mode=="conditional-baseline";
  fixture.profile.profile_sha256=baseline ? "d4dcc28edcf534586b3953d3235e8880173f6dbcd9ecb384e97991137646756e" : "'+HASH+'";
  const size_t faa=53,setup_index=52;
  static std::array<u32,3> pcs;static std::array<u32,3> branch_words{{0x4d820020u,0x4c820020u,0x4e800020u}};
  for (u32 i=0;i<3;++i) {pcs[i]=fixture.functions[faa].address+4+4*i;fixture.system.memory.Write(pcs[i],branch_words[i]);}
  fixture.functions[faa].size=16;fixture.returns[faa]=pcs[2];
  fixture.functions[faa].returns=baseline ? &pcs[2] : pcs.data();
  fixture.functions[faa].return_words=baseline ? &branch_words[2] : branch_words.data();
  fixture.functions[faa].return_count=baseline ? 1 : 3;
'''
        setup=setup.replace("'+HASH+'",HASH)
        component=component.replace('  assert(Observer::Arm(fixture.profile, output));',setup+'  assert(Observer::Arm(fixture.profile, output));',1)
        run=r'''
  if (mode.substr(0,12)=="conditional-") {
    fixture.cpu.gpr[3]=0x80030000u;
    fixture.cpu.pc=fixture.functions[setup_index].address;
    Observer::Observe(&fixture.system,fixture.cpu.pc,&fixture.cpu);
    fixture.cpu.gpr[1]-=56;fixture.cpu.spr[8]=0x8000def0u;
    fixture.cpu.pc=fixture.functions[faa].address;
    Observer::Observe(&fixture.system,fixture.cpu.pc,&fixture.cpu);
    auto branch=[&](u32 pc,u32 cr) {fixture.cpu.pc=pc;fixture.cpu.cr.value=cr;
      if (Observer::IsBoundary(pc)) Observer::Observe(&fixture.system,pc,&fixture.cpu);};
    branch(pcs[0],0); // beqlr not taken: no row/pop.
    branch(pcs[1],0x20000000u); // bnelr not taken: no row/pop.
    if (mode=="conditional-word") {
      fixture.system.memory.Write(pcs[0],0x4c820020u);branch(pcs[0],0x20000000u);
      assert(Observer::Error().find("boundary word differs")!=std::string::npos);
    } else if (mode=="conditional-sp") {
      fixture.cpu.gpr[1]+=4;branch(pcs[0],0x20000000u);
      assert(Observer::Error().find("function, sp, and lr")!=std::string::npos);
    } else {
      branch(mode=="conditional-bne" ? pcs[1] : pcs[0],mode=="conditional-bne" ? 0 : 0x20000000u);
      fixture.cpu.gpr[1]+=56;fixture.cpu.spr[8]=0x8000abcdu;
      fixture.cpu.pc=fixture.returns[setup_index];Observer::Observe(&fixture.system,fixture.cpu.pc,&fixture.cpu);
      if (baseline) assert(Observer::Error().find("function, sp, and lr")!=std::string::npos);
      else assert(Observer::Error().empty());
    }
    const bool expected_error=baseline || mode=="conditional-word" || mode=="conditional-sp";
    assert(Observer::Finish(false)==!expected_error);return 0;
  }
'''
        component=component.replace('  if (mode == "gobjproc-registration") {',run+'  if (mode == "gobjproc-registration") {',1)
        from tools.allocation_history_replay import load_trace
        with tempfile.TemporaryDirectory() as temporary:
            temp=Path(temporary)
            for name,content in leaves.STUBS.items():
                target=temp/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(content)
            source=temp/'component.cpp';source.write_text(component)
            header_root=base.SOURCE_ROOT/'reference-capture/dolphin/source'
            support=base.SUPPORT_ROOT/'reference-capture/dolphin/source'
            env={k:v for k,v in os.environ.items() if not k.startswith('MWRC_')};env.update(ENV)
            baseline_cpp=Path(os.environ.get('MELEE_STADIUM_CONDITIONAL_BASELINE',base.SOURCE_ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceAllocationObserver.cpp'))
            for kind,cpp,modes in [('candidate',header_root/'Core/PowerPC/ReferenceAllocationObserver.cpp',
                    ('conditional-eq','conditional-bne','conditional-sp','conditional-word')),
                    ('baseline',baseline_cpp,('conditional-baseline',))]:
                if kind=='baseline' and not os.environ.get('MELEE_STADIUM_CONDITIONAL_BASELINE'):continue
                binary=temp/kind
                built=subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-fno-exceptions','-Wall','-Wextra','-Werror',
                    '-I',str(temp),'-I',str(header_root),'-I',str(support),str(cpp),str(source),'-lz','-o',str(binary)],capture_output=True,text=True,timeout=120)
                self.assertEqual(built.returncode,0,built.stderr)
                for mode in modes:
                    path=temp/(mode+'.jsonl')
                    result=subprocess.run([str(binary),mode,str(path)],env=env,text=True,capture_output=True,timeout=10)
                    self.assertEqual(result.returncode,0,mode+': '+result.stderr)
                    _,rows,enters,returns=load_trace(path,disk_backed=False)
                    self.assertEqual(rows[-1]['status'],'incomplete')
                    if mode in ('conditional-eq','conditional-bne'):
                        self.assertEqual(len(enters),2);self.assertEqual(len(returns),2)
                        self.assertEqual(rows[-1]['pending_calls'],[])
                        faa_return=[r for r in returns.values() if r['function']=='HSD_GObjProc_8038FAA8']
                        self.assertEqual(len(faa_return),1)
                        self.assertEqual(faa_return[0]['machine']['pc'],0x80013504 if mode=='conditional-eq' else 0x80013508)
                    else:
                        self.assertNotEqual(rows[-1]['status'],'captured')
                        self.assertEqual(len(rows[-1]['pending_calls']),2)

    def test_broad_scope_requires_exact_profile_and_complete_exits(self):
        # Optional real generated profile; ordinary CI uses an explicitly modeled profile/hash.
        actual=os.environ.get('MELEE_STADIUM_PAIRED_REGISTRATION_PROFILE')
        profile=json.loads(Path(actual).read_text()) if actual else {'version':5,'registration_return_policy':'declared-cr0-eq-lr-returns-v1','registration_profile':'gobjproc-registration-v1'}
        digest=hashlib.sha256((json.dumps(profile,sort_keys=True,indent=2)+'\n').encode()).hexdigest()
        if actual:
            self.assertEqual(digest,HASH)
            exact_module('retail_allocation_profile','tools/retail_allocation_profile.py')
            generator=exact_module('generate_reference_allocation_profile','tools/generate_reference_allocation_profile.py')
            self.assertIs(generator.validate_profile(profile),profile)
            reader=exact_module('allocation_history_replay','tools/allocation_history_replay.py')
            self.assertEqual(reader.load_profile(Path(actual))['version'],5)
            extractor=exact_module('extract_original_gobjproc_registration','tools/extract_original_gobjproc_registration.py')
            self.assertIn('HSD_GObjProc_8038FAA8',extractor._function_table(profile))
            historical=os.environ.get('MELEE_STADIUM_REGISTRATION_HISTORICAL_V4')
            if historical:
                old=json.loads(Path(historical).read_text())
                self.assertIs(generator.validate_profile(old),old)
                self.assertEqual(reader.load_profile(Path(historical))['version'],4)
                self.assertIn('HSD_GObjProc_8038FAA8',extractor._function_table(old))
            self.assertEqual(base.driver.stadium_whole_allocation_profile_bytes(profile,registration=True),Path(actual).read_bytes())
        self.assertEqual(base.driver.stadium_whole_allocation_profile_bytes(profile),base.driver.canonical(profile))
        header=dict(scope='selected-gobjproc-registration',recording_filter=allocation.PAIRED_REGISTRATION_FILTER,paired_registration_profile=NEW,profile_version=5,registration_return_policy='declared-cr0-eq-lr-returns-v1',registration_profile='gobjproc-registration-v1',
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

    def test_actual_reducer_selects_exact_v4_v5_trace_header_identity(self):
        reducer = exact_module('extract_original_gobjproc_registration',
                               'tools/extract_original_gobjproc_registration.py')
        actual = os.environ.get('MELEE_STADIUM_PAIRED_REGISTRATION_PROFILE')
        historical = os.environ.get('MELEE_STADIUM_REGISTRATION_HISTORICAL_V4')
        if actual and historical:
            profiles = [json.loads(Path(actual).read_text()), json.loads(Path(historical).read_text())]
        else:
            # Ordinary CI models profile contents; retained-data focused execution supplies both exact files.
            from test_reference_allocation_observer import synthetic_profile
            current = synthetic_profile()
            old = copy.deepcopy(current)
            old['version'] = 4
            old.pop('registration_return_policy')
            for function in old['functions']: function.pop('return_words')
            profiles = [current, old]
        for profile in profiles:
            expected = (reducer.EXPECTED_PROFILE_SHA256_V5 if profile['version'] == 5
                        else reducer.EXPECTED_PROFILE_SHA256)
            symbols = {'gobj_alloc_data': {'address': 0x804ce38c, 'size': 0x2c},
                       'gobjproc_alloc_data': {'address': 0x804ce3b8, 'size': 0x2c}}
            self.assertNotIn('gobj_alloc_data', profile['globals'])
            self.assertNotIn('gobjproc_alloc_data', profile['globals'])
            symbols.update({'grStadium_801D1390': {'address': 0x801d1390},
                            'efLib_particles_proc_main': {'address': 0x8005c9a4},
                            'Fighter_8006A360': {'address': 0x8006a360}})
            header = dict(record='header', sequence=0, schema=reducer.TRACE_SCHEMA, version=1,
                          profile_sha256=expected, scope='selected-gobjproc-registration',
                          start='original_dol_entry', writes_game_state=False)
            enter = dict(record='enter', sequence=1, call=0, function='HSD_ObjAlloc',
                         args=[symbols['gobj_alloc_data']['address']], parent=None,
                         thread=0, sp=0x80010000, lr=0x80020000)
            returned = dict(record='return', sequence=2, call=0, function='HSD_ObjAlloc',
                            thread=0, result=0x804d1000, observed={})
            terminal = dict(record='end', sequence=3, status='captured', calls=1, pending_calls=[])
            rows = [header, enter, returned, terminal]
            result = reducer.reduce_registration_trace(header, rows, {0: enter}, {0: returned},
                                                       profile, symbols)
            self.assertEqual(result['trace']['profile_sha256'], expected)
            self.assertEqual(result['summary']['allocations']['gobj'], 1)
            for key, value in [('address', 0x804ce390), ('size', 0x30), ('address', 0x804ce38d)]:
                bad_symbols = copy.deepcopy(symbols)
                bad_symbols['gobj_alloc_data'][key] = value
                with self.subTest(version=profile['version'], key=key, value=value):
                    with self.assertRaisesRegex(reducer.RegistrationProblem, 'source pool identity'):
                        reducer.reduce_registration_trace(header, rows, {0: enter}, {0: returned},
                                                          profile, bad_symbols)
            contradictory = copy.deepcopy(profile)
            contradictory['globals']['gobj_alloc_data'] = {'address': 0x804ce390, 'size': 0x2c}
            with self.assertRaisesRegex(reducer.RegistrationProblem, 'profile pool identity'):
                reducer.reduce_registration_trace(header, rows, {0: enter}, {0: returned},
                                                  contradictory, symbols)
            for wrong in ('f'*64, reducer.EXPECTED_PROFILE_SHA256 if profile['version'] == 5
                          else reducer.EXPECTED_PROFILE_SHA256_V5):
                with self.subTest(version=profile['version'], wrong=wrong):
                    with self.assertRaisesRegex(reducer.RegistrationProblem, 'pinned observer profile identity'):
                        reducer.reduce_registration_trace(dict(header, profile_sha256=wrong), rows,
                                                          {0: enter}, {0: returned}, profile, symbols)

if __name__=='__main__':unittest.main()
