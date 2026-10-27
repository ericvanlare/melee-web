"""Actual native startup/compact leaves; modeled input/env only, no game execution."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('MELEE_LATENCY_REPLAY_ROOT', Path(__file__).resolve().parents[1]))
SOURCE = ROOT / 'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp'
SUPPORT = Path(os.environ.get('MELEE_LATENCY_REPLAY_SUPPORT_ROOT', ROOT))

class FirstFormLatencyReplayAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required for actual observer leaves')
        cls.tmp = tempfile.TemporaryDirectory(prefix='latency-replay-admission-')
        cls.addClassCleanup(cls.tmp.cleanup)
        directory = Path(cls.tmp.name)
        text = SOURCE.read_text()
        timing_start = text.index('  bool selected_timing_enabled=')
        timing_state = text[timing_start:text.index(';', timing_start)+1]
        begin = text.index('enum class Boundary : u16')
        boundary = text[begin:text.index('};', begin)+2]
        leaves = text[text.index('struct StadiumWholeBObservation\n'):
                      text.index('enum class StadiumGoPrefixPhase : u8')]
        paired_begin = text.index('    const std::string paired_idle_setting =')
        paired = text[paired_begin:text.index('    whole_session_matches = WholeSessionMatchCount();', paired_begin)]
        begin = text.index('    const std::string whole_feedback_setting =')
        startup = paired + text[begin:text.index('    const std::string constructor_setting =', begin)]
        begin = text.index('  bool StartCompact()')
        compact = text[begin:text.index('  void CompactGo(', begin)]
        begin = text.index('std::string JsonEscape(')
        escape = text[begin:text.index('std::string Env(', begin)]
        header = SUPPORT / 'reference-capture/dolphin/source/Core/PowerPC/ReferenceCompactRngLedger.h'
        fixture = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include """ + '"' + str(header) + '"' + r"""
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
namespace CompactRng=ReferenceCapture::CompactRng;
""" + boundary + leaves + escape + r"""
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
bool SdInitRequested(){return !Env("MWRC_SD_INIT").empty();}
bool AllocationConfigured(){return !Env("MWRC_ALLOCATION_OUTPUT").empty();}
namespace InputStream { bool recording=true,replaying=false;
 bool IsRecording(){return recording;} bool IsReplaying(){return replaying;} }
struct CompactRngSettings { bool present=false,valid=false;std::string selection,output; } settings;
const CompactRngSettings& CompactRngEnvironment(){return settings;}
constexpr const char* EXPECTED_DOL_SHA256="e";
struct Gate {
""" + timing_state + r"""
 StadiumWholeBObservation stadium_whole_b; StadiumWholeLongIdle stadium_whole_long_idle;
 bool stadium_whole_paired_idle=false;
 bool stadium_go_prefix_enabled=false,stadium_whole_feedback=false;
 u32 whole_session_matches=3;
 bool cpu_probe_configured=false,item_probe_configured=false,stadium_idle_3500=false;
 bool compact_enabled=false; std::string compact_path,compact_header;
 std::array<u32,2> compact_matches{};u32 compact_match_count=0;
 std::string capture_id="capture",sequence_id="sequence",stadium_whole_setup_receipt,error;
 void SetInvalid(const char* e){error=e;}
 bool Run(){
""" + startup + r"""
 return true;}
""" + compact + r"""
};
void reset(){ env.clear();settings={};InputStream::recording=true;InputStream::replaying=false;
 env["MWRC_STADIUM_WHOLE_FEEDBACK"]="1";env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]=std::string(64,'a');
 env["MWRC_ALLOCATION_OUTPUT"]="owned";env["MWRC_ALLOCATION_VS_TARGET"]="2";env["MWRC_ALLOCATION_STOP_AT"]="exit"; }
void latency(){reset();InputStream::recording=false;InputStream::replaying=true;
 env["MWRC_STADIUM_FIRST_FORM_LATENCY_REPLAY"]="first_form_latency_replay_v1";
 env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');env["MWRC_STADIUM_WHOLE_B_STATE"]="1";
 env["MWRC_STADIUM_WHOLE_LONG_IDLE"]="selected_b_3500_v1";env["MWRC_STADIUM_POSTTRANSFORM_STATE"]="first_form_v1";
 settings={true,true,"1","absent-owned-compact.mwrl"}; }
void old_rng(){reset();InputStream::recording=false;InputStream::replaying=true;
 env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=std::string(64,'b');env["MWRC_STADIUM_WHOLE_B_STATE"]="1";
 env["MWRC_STADIUM_WHOLE_RNG_RETURNS"]="1";env["MWRC_CPU_PROBE_OUTPUT"]="owned";
 env["MWRC_CPU_PROBE_RNG_RETURN_SITE"]="rng_updates";env["MWRC_CPU_PROBE_MATCH"]="1";
 env["MWRC_CPU_PROBE_FIRST_TICK"]="1";env["MWRC_CPU_PROBE_LAST_TICK"]="3"; }
int main(int argc,char**argv){assert(argc==2);std::string mode=argv[1];using L=StadiumWholeLongIdle;
 if(mode=="legacy"){
  reset();assert(Gate().Run());
  reset();env["MWRC_STADIUM_WHOLE_B_STATE"]="1";env["MWRC_STADIUM_WHOLE_LONG_IDLE"]="selected_b_3500_v1";settings={true,true,"1","absent"};assert(Gate().Run());
  assert(L::Admit("paired_idle_scene_endpoints_v1",true,true,true,false,false,true,"0,1",true));
  assert(L::Admit("paired_idle_registration_v1",true,true,true,false,false,true,"0,1",true));
  old_rng();assert(Gate().Run());
  latency();env["MWRC_STADIUM_FIRST_FORM_LATENCY_REPLAY"]="";assert(!Gate().Run());
  assert(!L::Admit("selected_b_3500_v1",true,true,false,true,false,true,"1"));
 }else if(mode=="exact"){
  latency();Gate g;assert(g.Run()&&g.stadium_whole_long_idle.first_form_replay&&g.stadium_whole_long_idle.enabled);
  assert(g.StartCompact()&&g.compact_enabled&&g.compact_match_count==1&&g.compact_matches[0]==1);
  const auto json=StadiumWholeLongIdle::StadiumFirstFormLatencyReplayJson(true,std::string(64,'b'));
  assert(json.find("\"stadium_first_form_latency_replay\":\"first_form_latency_replay_v1\"")!=std::string::npos);
  assert(json.find("\"stadium_first_form_latency_replay_binding_sha256\":\""+std::string(64,'b')+"\"")!=std::string::npos);
  assert(StadiumWholeLongIdle::StadiumFirstFormLatencyReplayJson(false,"").empty());
 }else if(mode=="refusals"){
  for(const char* key:{"MWRC_STADIUM_FIRST_FORM_LATENCY_REPLAY","MWRC_STADIUM_WHOLE_FEEDBACK","MWRC_STADIUM_WHOLE_B_STATE","MWRC_STADIUM_WHOLE_LONG_IDLE","MWRC_STADIUM_POSTTRANSFORM_STATE"}){
   latency();env[key]="wrong";assert(!Gate().Run()); }
  for(const char* binding:{"","0","BAD", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "0000000000000000000000000000000000000000000000000000000000000000"}){
   latency();env["MWRC_STADIUM_WHOLE_REPLAY_BINDING"]=binding;assert(!Gate().Run()); }
  for(const char* key:{"MWRC_CPU_PROBE_OUTPUT","MWRC_STADIUM_WHOLE_RNG_RETURNS","MWRC_STADIUM_WHOLE_PAIRED_IDLE","MWRC_ITEM_PROBE_OUTPUT","MWRC_STADIUM_CONSTRUCTOR_OWNER","MWRC_SD_INIT"}){
   latency();env[key]="1";assert(!Gate().Run()); }
  latency();InputStream::recording=true;assert(!Gate().Run());
  latency();InputStream::replaying=false;assert(!Gate().Run());
  latency();InputStream::recording=true;InputStream::replaying=false;assert(!Gate().Run());
  latency();Gate wrong;wrong.whole_session_matches=2;assert(!wrong.Run());
  latency();settings.present=false;assert(!Gate().Run());latency();settings.valid=false;assert(!Gate().Run());
  for(const char* selection:{"","0","0,1"}){latency();settings.selection=selection;assert(!Gate().Run());}
 }else if(mode=="compact"){
  latency();Gate g;assert(g.Run());InputStream::recording=true;assert(!g.StartCompact());
  latency();Gate h;assert(h.Run());InputStream::replaying=false;assert(!h.StartCompact());
  latency();Gate p;assert(p.Run());p.cpu_probe_configured=true;assert(!p.StartCompact());
  reset();settings={true,true,"1","absent"};Gate normal;normal.stadium_whole_feedback=true;assert(normal.StartCompact());
  reset();InputStream::recording=false;InputStream::replaying=true;settings={true,true,"1","absent"};Gate refused;refused.stadium_whole_feedback=true;assert(!refused.StartCompact());
 }else assert(false);
}
"""
        cpp=directory/'fixture.cpp';cpp.write_text(fixture);cls.binary=directory/'fixture'
        result=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(cls.binary)],capture_output=True,text=True)
        (directory/'compile.stdout').write_text(result.stdout);(directory/'compile.stderr').write_text(result.stderr)
        if result.returncode:
            cls.tmp._finalizer.detach()
            raise AssertionError(f'actual source fixture compile failed; retained {directory}\n{result.stderr}')

    def run_case(self, mode):
        result=subprocess.run([str(self.binary),mode],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_exact_replay_and_metadata(self): self.run_case('exact')
    def test_wrong_context_binding_input_and_probe_refusals(self): self.run_case('refusals')
    def test_legacy_recording_and_short_rng_replay_unchanged(self): self.run_case('legacy')
    def test_compact_input_gate_rechecks_mode_and_no_probes(self): self.run_case('compact')
    def test_all_three_metadata_outputs_use_actual_serializer(self):
        source=SOURCE.read_text()
        for name in ('handshake','start','json'):
            self.assertIn(name+' += StadiumWholeLongIdle::StadiumFirstFormLatencyReplayJson(stadium_whole_long_idle.first_form_replay,',source)
        self.assertEqual(source.count(' += StadiumWholeLongIdle::StadiumFirstFormLatencyReplayJson('),3)

if __name__=='__main__':unittest.main()
