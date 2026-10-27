"""Actual-source admission/latch/close and driver controls; modeled leaves, no capture."""
from pathlib import Path
import ast
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
import unittest

ROOT=Path(os.environ.get("MELEE_WHOLE_RNG_SOURCE_ROOT",Path(__file__).resolve().parents[1]))
SOURCE=ROOT/"reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"


def section(source,start,end):
    assert source.count(start)==source.count(end)==1,(start,end)
    return source[source.index(start):source.index(end)]


def functions(path,names,namespace):
    tree=ast.parse(path.read_text())
    selected=[node for node in tree.body if isinstance(node,(ast.FunctionDef,ast.AsyncFunctionDef)) and node.name in names]
    assert {node.name for node in selected}==set(names)
    exec(compile(ast.Module(body=selected,type_ignores=[]),str(path),"exec"),namespace)
    return namespace


class StadiumWholeRngAdjunctTests(unittest.TestCase):
    def test_actual_original_admission_is_combined_only_and_close_never_finishes_whole3(self):
        source=SOURCE.read_text()
        body=section(source,'    const std::string whole_feedback_setting =','    const std::string constructor_setting =')
        boundary_start=source.index("enum class Boundary : u16")
        boundary=source[boundary_start:source.index("};",boundary_start)+2]
        declaration=boundary+section(source,'struct StadiumWholeBObservation\n','enum class StadiumGoPrefixPhase : u8')
        close=section(source,'  void CloseCpuProbe(u32 pc, u32 source_tick)','  void CloseCpuProbeAtSourceTick(u32 pc, u32 source_tick)')
        close_tick=section(source,'  void CloseCpuProbeAtSourceTick(u32 pc, u32 source_tick)','  bool LatchWholeRngGo(u32 tick,u64 sequence)')
        latch=section(source,'  bool LatchWholeRngGo(u32 tick,u64 sequence)','  std::string CpuProbeWholeWindowJson() const')
        metadata=section(source,'  std::string CpuProbeWholeWindowJson() const','  std::string CpuProbeCloseStatusJson() const')
        fixture=r'''
#include <cassert>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
using u32=uint32_t;using u64=uint64_t;using u8=uint8_t;using u16=uint16_t;
'''+declaration+r'''
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
bool SdInitRequested(){return !Env("MWRC_SD_INIT").empty();}
bool AllocationConfigured(){return !Env("MWRC_ALLOCATION_OUTPUT").empty();}
namespace InputStream{bool recording=true,replaying=false,finished=false;bool IsRecording(){return recording;}bool IsReplaying(){return replaying;}}
struct CompactRngSettings{bool present=false,valid=false;std::string selection;};
const CompactRngSettings& CompactRngEnvironment(){static const CompactRngSettings value{};return value;}
struct Gate{
 StadiumWholeBObservation stadium_whole_b;StadiumWholeLongIdle stadium_whole_long_idle;unsigned whole_session_matches=3;
 bool stadium_go_prefix_enabled=false,stadium_whole_feedback=false,whole_rng_returns=false,whole_replay=false;
 std::string stadium_whole_setup_receipt,error;void SetInvalid(const char* e){error=e;}
 bool Run(){
'''+body+r'''
 return true;}
};
struct Window{
 bool cpu_probe_whole_go_relative=true,cpu_probe_go_latched=false,cpu_probe_rng_returns=true;
 bool cpu_probe_configured=true,cpu_probe_valid=true,cpu_probe_closed=false,match_active=true,setup_ready=true;
 bool finish_requested=false,natural_completion=false;
 std::atomic<bool> cpu_probe_published{false};
 u32 cpu_probe_match=1,cpu_probe_first_offset=1,cpu_probe_last_offset=3,cpu_probe_go_tick=0;
 u32 cpu_probe_first_tick=0,cpu_probe_last_tick=0,cpu_probe_close_pc=0,cpu_probe_close_tick=0;
 u32 cpu_probe_rng_return_pc=0,match_index=1,whole_phase=5;u64 cpu_probe_go_sequence=0;
 bool whole_session_enabled(){return true;}
'''+close+close_tick+latch+metadata+r'''
};
void reset(){env.clear();InputStream::recording=true;InputStream::replaying=false;env["MWRC_STADIUM_WHOLE_FEEDBACK"]="1";env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]=std::string(64,'a');env["MWRC_ALLOCATION_OUTPUT"]="owned";env["MWRC_ALLOCATION_VS_TARGET"]="2";env["MWRC_ALLOCATION_STOP_AT"]="exit";}
void adjunct(){reset();env["MWRC_STADIUM_WHOLE_B_STATE"]="1";env["MWRC_STADIUM_WHOLE_RNG_RETURNS"]="1";env["MWRC_CPU_PROBE_OUTPUT"]="owned/rng.json";env["MWRC_CPU_PROBE_MATCH"]="1";env["MWRC_CPU_PROBE_FIRST_TICK"]="1";env["MWRC_CPU_PROBE_LAST_TICK"]="3";env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]="rng_returns";}
int main(){
 reset();assert(Gate().Run());adjunct();assert(Gate().Run());
 adjunct();env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]="rng_updates";assert(Gate().Run());
 adjunct();env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]="rng_updates";InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');assert(Gate().Run());
 for(const char* site:{"","rand_return","randf_return","unknown"}){adjunct();env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]=site;assert(!Gate().Run());}
 for(const char* key:{"MWRC_STADIUM_WHOLE_B_STATE","MWRC_CPU_PROBE_OUTPUT","MWRC_STADIUM_WHOLE_RNG_RETURNS"}){adjunct();env[key]="";assert(!Gate().Run());}
 adjunct();env["MWRC_CPU_PROBE_MATCH"]="0";assert(!Gate().Run());
 adjunct();env["MWRC_CPU_PROBE_FIRST_TICK"]="0";assert(!Gate().Run());
 adjunct();env["MWRC_CPU_PROBE_LAST_TICK"]="10";assert(!Gate().Run());
 adjunct();env["MWRC_CPU_PROBE_LAST_TICK"]="1";env["MWRC_CPU_PROBE_FIRST_TICK"]="3";assert(!Gate().Run());
 adjunct();InputStream::replaying=true;assert(!Gate().Run());adjunct();InputStream::recording=false;assert(!Gate().Run());
 adjunct();InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');assert(Gate().Run());
 for(const char* bad:{"","0","not-a-sha"}){adjunct();InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=bad;assert(!Gate().Run());}
 adjunct();env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');assert(!Gate().Run());
 adjunct();InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'0');assert(!Gate().Run());
 adjunct();InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]="rand_return";assert(!Gate().Run());
 adjunct();InputStream::recording=false;InputStream::replaying=true;env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');env["MWRC_STADIUM_WHOLE_FEEDBACK"]="";assert(!Gate().Run());

 for(const char* key:{"MWRC_STADIUM_CONSTRUCTOR_OWNER","MWRC_ITEM_PROBE_OUTPUT","MWRC_ENTITY_PROFILE","MWRC_STADIUM_IDLE_3500"}){adjunct();env[key]="1";assert(!Gate().Run());}
 adjunct();Gate wrong;wrong.whole_session_matches=2;assert(!wrong.Run());
 Window w;w.CloseCpuProbeAtSourceTick(0,999);assert(!w.cpu_probe_closed&&!w.cpu_probe_published);
 assert(w.LatchWholeRngGo(52,777));assert(w.cpu_probe_first_tick==52&&w.cpu_probe_last_tick==56&&w.cpu_probe_go_sequence==777);
 assert(!w.LatchWholeRngGo(53,778));
 w.CloseCpuProbeAtSourceTick(1,56);assert(!w.cpu_probe_closed);
 w.CloseCpuProbeAtSourceTick(2,57);assert(w.cpu_probe_closed&&w.cpu_probe_published&&w.cpu_probe_close_tick==57);
 assert(!w.finish_requested&&!w.natural_completion&&!InputStream::finished);
 const auto json=w.CpuProbeWholeWindowJson();assert(json.find("actual_go_raw_guard_band")!=std::string::npos&&json.find("777")!=std::string::npos);
 Window bad;bad.cpu_probe_match=0;assert(!bad.LatchWholeRngGo(52,777));
 Window overflow;assert(!overflow.LatchWholeRngGo(UINT32_MAX,777));
 Window disabled;disabled.cpu_probe_whole_go_relative=false;assert(disabled.LatchWholeRngGo(52,777)&&!disabled.cpu_probe_go_latched&&disabled.CpuProbeWholeWindowJson().empty());
}
'''
        compiler=shutil.which("clang++") or shutil.which("g++")
        if not compiler:self.skipTest("C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="whole-rng-adjunct-") as temporary:
            cpp=Path(temporary)/"control.cpp";cpp.write_text(fixture);binary=cpp.with_suffix("")
            result=subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(cpp),"-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
        record=source[source.index('  void RecordCpuProbe('):source.index('  void RecordCpuProbe(')+500]
        self.assertIn('if(cpu_probe_whole_go_relative&&!cpu_probe_go_latched)return;',record)
        go=source[source.index('    if (later && !stadium_whole_b.Go('):source.index('    if (later) return; // Whole-route completion',source.index('    if (later && !stadium_whole_b.Go('))]
        self.assertLess(go.index('LatchWholeRngGo(source_tick,next_sequence)'),go.index('PublishStadiumProgress("go_after"'))

    def test_actual_fresh_driver_joins_go_offsets_raw_bounds_and_actual_seed(self):
        def require(value,error):
            if not value:raise ValueError(error)
        namespace=dict(Path=Path,json=json,require=require)
        functions(ROOT/'scripts/capture_sd_reference_prefix.py',['parse_stadium_whole_rng_window','validate_stadium_whole_rng_sidecar'],namespace)
        parse=namespace['parse_stadium_whole_rng_window'];validate=namespace['validate_stadium_whole_rng_sidecar']
        self.assertIsNone(parse(None));self.assertEqual(parse('1:3'),(1,3));self.assertEqual(parse('1:9'),(1,9))
        for bad in ('0:1','1:10','3:1',True,'01:2',''):
            with self.assertRaises(ValueError):parse(bad)
        app=dict(Path=Path,json=json,re=re,EnvironmentError=ValueError,sha256=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest())
        functions(ROOT/'scripts/reference_capture_app.py',['validate_cpu_rng_probe'],app)
        module=types.ModuleType('reference_capture_app');module.validate_cpu_rng_probe=app['validate_cpu_rng_probe']
        old=sys.modules.get('reference_capture_app');sys.modules['reference_capture_app']=module
        self.addCleanup(lambda:sys.modules.__setitem__('reference_capture_app',old) if old is not None else sys.modules.pop('reference_capture_app',None))
        meta=dict(window_basis='actual_go_raw_guard_band',requested_first_offset=1,requested_last_offset=3,go_tick=52,go_observer_sequence=777,first_guard_tick_partial=True)
        selection=dict(rng_return_site='rng_returns',rng_return_pcs=['0x80380524','0x8038057c'])
        owner=dict(status='outside_proc',gobj='0x00000000',proc='0x00000000',callback='0x00000000',classifier=0,p_link=0,s_link=0,callback_symbol=None)
        record=dict(pc='0x80380524',label='rand_return',match=1,source_tick=53,rng_ordinal=0,observer_next_sequence=780,draw_ordinal=53,seed_owner_pointer='0x80412000',seed_after='12345678',owner=owner)
        probe=dict(schema='melee-web-cpu-register-probe',version=4,diagnostic_only=True,window_complete=True,source_revision='GALE01r2',dol_sha256='a'*64,capture_id='stadium-whole3',sequence_id='primary3-allocation2-feedback',match=1,first_tick=52,last_tick=56,record_count=1,records=[record],**selection,**meta)
        status=dict(cpu_probe_close=dict(reason='source_tick_after_window',configured_match=1,first_tick=52,last_tick=56,source_tick=57,**selection,**meta))
        go=dict(source_tick=52,sequence=777)
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'rng.json'
            def run():path.write_text(json.dumps(probe));return validate(path,(1,3),go,status,'a'*64)
            result=run();self.assertEqual(result['raw_source_tick_bounds'],[52,56]);self.assertTrue(result['guard_band_extras_retained']);self.assertIs(result['C3'],False)
            old_probe=json.loads(json.dumps(probe));old_status=json.loads(json.dumps(status))
            probe.update(version=5,rng_return_site='rng_updates',rng_return_pcs=['0x80380524','0x8038057c','0x803805b8'])
            probe['records'][0].update(label='randi_return',pc='0x803805b8')
            status['cpu_probe_close'].update(rng_return_site='rng_updates',rng_return_pcs=probe['rng_return_pcs'])
            path.write_text(json.dumps(probe))
            self.assertEqual(validate(path,(1,3),go,status,'a'*64,coverage='rng_updates')['record_count'],1)
            with self.assertRaises(ValueError):validate(path,(1,3),go,status,'a'*64)
            probe.clear();probe.update(old_probe);record=probe['records'][0]
            status.clear();status.update(old_status)
            for mutate in (lambda:go.update(sequence=778),lambda:status['cpu_probe_close'].update(requested_last_offset=4),lambda:record.update(seed_owner_pointer='0x00000000')):
                before=json.loads(json.dumps([probe,status,go]));mutate()
                with self.assertRaises(ValueError):run()
                probe.clear();probe.update(before[0]);record=probe['records'][0];status.clear();status.update(before[1]);go.clear();go.update(before[2])
