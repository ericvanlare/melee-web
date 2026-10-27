"""Fixed-profile mechanism controls; synthetic data is not original/runtime admission.

Optional retained input requires MELEE_WEB_STADIUM_FRESH_SSS_PREFIX_132_INPUT.
Prepared SDK dependencies may be selected read-only with STADIUM_REVIEWED_SOURCE_ROOT.
"""
from pathlib import Path
import hashlib,os,struct,subprocess,sys,unittest
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
D=Path(__file__).resolve().parents[1]
S=Path(os.environ.get('STADIUM_REVIEWED_SOURCE_ROOT', D))
class Fresh132CandidateControls(OwnedWorkspaceTests):
 def run_command(self,args,work,label,timeout=60):
  r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=timeout)
  (work/(label+'.log')).write_text(r.stdout)
  self.assertEqual(r.returncode,0,r.stdout)
  return r.stdout
 def test_actual_selector_and_readiness(self):
  work=self.new_workspace(D,'fresh132-dispatch-')
  self.run_command([sys.executable,'-B',str(D/'tests/first_sss_pair_native_control.py'),str(work)],work,'dispatch')
 def test_actual_sdk_header_synthetic_fixed_fresh_codec(self):
  # Deliberately synthetic order/ports: tests the codec contract, not the
  # original middle132 schedule. Original source comparisons remain optional.
  wire=bytearray(7968);wire[:8]=b'STC1SSSP';struct.pack_into('>I',wire,8,2)
  wire[12:44]=bytes.fromhex('4fd54b4aa9537292295ad83fbb75655f8213a475a876fdd1468affb68d3d211f')
  struct.pack_into('>I',wire,44,132)
  for i in range(132):
   seq=(1603,1607,1608,1609) if i==0 else ((2475,2477,2478,2479) if i==131 else tuple(1610+i*6+j for j in range(4)))
   struct.pack_into('>4I',wire,48+i*60,*seq)
   wire[48+i*60+16:48+i*60+60]=bytes((128+i+j)%256 for j in range(44))
  self.run_codec_wire(wire)
 @unittest.skipUnless(os.environ.get('MELEE_WEB_STADIUM_FRESH_SSS_PREFIX_132_INPUT'),
  'optional original132 input not supplied; synthetic SDK codec remains separate')
 def test_actual_sdk_header_optional_original_wire(self):
  wire=Path(os.environ['MELEE_WEB_STADIUM_FRESH_SSS_PREFIX_132_INPUT']).read_bytes()
  self.assertEqual(hashlib.sha256(wire).hexdigest(),'f568192072378923fc5806252554ab735e1be5d75a0a9098643edaea1b41fd83')
  self.run_codec_wire(wire)
 def run_codec_wire(self,wire):
  compiler=S/'.deps/emsdk/upstream/emscripten/em++'
  if not compiler.is_file():
   self.skipTest('Prepared SDK/Emscripten required for exact-header control')
  work=self.new_workspace(D,'fresh132-codec-')
  cpp=work/'codec.cpp';js=work/'codec.js'
  cpp.write_text('#include "'+str(D/'src/stadium_first_css_diagnostic_input.hpp')+'"\n#include <cassert>\n#include <iostream>\nusing namespace melee_web::stadium_first_css_diagnostic;\nstatic const uint8_t wire[]={'+','.join(map(str,wire))+'};\n'+r'''
int main(){
 std::vector<uint8_t> b(std::begin(wire),std::end(wire));
 const auto sha=hex(b.data()+12,32);auto input=decode_fresh_sss_prefix_132_input(b.data(),b.size(),sha);
 assert(input.records.size()==132&&input.records.front().sequences[0]==1603&&input.records.back().sequences[3]==2479);
 for(unsigned i=0;i<132;++i){ConsumedPadInput consumed{};consumed.ports=input.records[i].ports;PADStatus typed[4]{};decode_consumed_pad_statuses(consumed,typed);
  for(unsigned p=0;p<4;++p){auto* raw=b.data()+48+i*60+16+p*11;assert(typed[p].button==((uint16_t(raw[0])<<8)|raw[1]));
   assert(typed[p].stickX==int8_t(raw[2])&&typed[p].stickY==int8_t(raw[3])&&typed[p].substickX==int8_t(raw[4])&&typed[p].substickY==int8_t(raw[5]));
   assert(typed[p].triggerLeft==raw[6]&&typed[p].triggerRight==raw[7]&&typed[p].analogA==raw[8]&&typed[p].analogB==raw[9]&&typed[p].err==int8_t(raw[10]));}}
 auto refuse=[&](const std::vector<uint8_t>& x,const std::string& identity){bool rejected=false;try{(void)decode_fresh_sss_prefix_132_input(x.data(),x.size(),identity);}catch(const std::runtime_error&){rejected=true;}assert(rejected);};
 for(size_t n:{size_t(0),size_t(48),size_t(7488),size_t(7967)})refuse(std::vector<uint8_t>(b.begin(),b.begin()+n),sha);
 auto extra=b;extra.push_back(0);refuse(extra,sha);
 for(size_t offset:{size_t(0),size_t(8),size_t(12),size_t(44),size_t(48),size_t(48+131*60+12)}){auto bad=b;bad[offset]^=1;refuse(bad,sha);}
 refuse(b,std::string(64,'0'));bool old_refused=false;try{(void)decode_first_sss_prefix_input(b.data(),b.size(),sha);}catch(const std::runtime_error&){old_refused=true;}assert(old_refused&&kFirstSssPrefixCount==124);
 std::cout<<"PASS actual SDK header fresh132 literal PAD wire, fixed version/count/source/order/endpoints and legacy separation\n";
}
''')
  self.run_command([str(compiler),str(cpp),'-o',str(js),'-std=gnu++20','-O0','-fexceptions','-DAURORA','-DAURORA_ENABLE_GX','-DTARGET_PC','-sEXIT_RUNTIME=1','-sSTACK_SIZE=1048576','-I'+str(S/'src'),'-I'+str(S/'build/gameplay-source/src'),'-I'+str(S/'.deps/aurora/include')],work,'compile')
  self.assertIn('PASS actual SDK header fresh132',self.run_command([str(node_runtime()),str(js)],work,'run',30))
if __name__=='__main__':unittest.main()
