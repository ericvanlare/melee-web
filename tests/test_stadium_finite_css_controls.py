"""Synthetic finite codec/loader controls; optional retained-original controls."""
from pathlib import Path
import json,os,subprocess,sys,unittest
ROOT=Path(os.environ.get('MELEE_FINITE_CSS_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
CANDIDATE=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(CANDIDATE/'tests'),str(ROOT/'tests'),str(ROOT/'scripts')]
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime
from stadium_finite_css_fixture import write_fixture

class FiniteCssControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch=cls.new_workspace(ROOT,'fresh-finite-css-controls-')
        cls.fixture=write_fixture(cls.scratch/'synthetic-inputs')
    def command(self,args,label,seconds=60):
        log=self.scratch/(label+'.log')
        with log.open('x') as stream:
            result=subprocess.run(args,stdout=stream,stderr=subprocess.STDOUT,timeout=seconds,
                env=dict(os.environ,TMPDIR=str(self.scratch)))
        self.assertEqual(result.returncode,0,log.read_text())
        return log.read_text()
    def test_candidate_javascript_syntax(self):
        for name in ['web/runtime-development.mjs','tests/stadium_c1a_browser_test.mjs',
                     'tests/stadium_first_css_stream_compare.mjs']:
            self.command([str(node_runtime()),'--check',str(CANDIDATE/name)],Path(name).stem+'-syntax')
    def test_actual_loader_comparator_and_queued_stages(self):
        self.check_loader(self.fixture,'synthetic')
    @unittest.skipUnless(os.environ.get('MELEE_FINITE_CSS_EVIDENCE_ROOT'),
                         'Optional retained-original evidence root not configured')
    def test_retained_original_loader_comparator_and_queued_stages(self):
        self.check_loader(Path(os.environ['MELEE_FINITE_CSS_EVIDENCE_ROOT']),'retained-original')
    def check_loader(self,base,label):
        output=self.command([str(node_runtime()),str(CANDIDATE/'tests/stadium_finite_css_controls.mjs'),
            str(CANDIDATE),str(base),label],'finite-js-controls-'+label,30)
        self.assertIn('"result":"PASS"',output);print(output.strip())
    def test_real_sdk_finite_decoder(self):
        self.check_decoder(self.fixture,'synthetic')
    @unittest.skipUnless(os.environ.get('MELEE_FINITE_CSS_EVIDENCE_ROOT'),
                         'Optional retained-original evidence root not configured')
    def test_retained_original_real_sdk_finite_decoder(self):
        self.check_decoder(Path(os.environ['MELEE_FINITE_CSS_EVIDENCE_ROOT']),'retained-original')
    def check_decoder(self,base,label):
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not compiler.is_file():self.skipTest('Existing SDK required')
        finite=base/'validation-v1/first-css-finite-preparation-v1/extraction-v1'
        descriptor=json.loads((finite/'input-only-descriptor.json').read_text())
        raw=(finite/'literal-input-rows.raw').read_bytes()
        manifest=json.loads((base/'producer-v1/manifest.json').read_text())
        context=Path(manifest['context_input']['path']).read_bytes()
        value=bytearray(b'STC4PFX1'+(4).to_bytes(4,'big')+bytes.fromhex(descriptor['source_sha256']))
        for number in [descriptor['context_entry_sequence'],descriptor['context_return_sequence'],
                       descriptor['exclusive_phase_end_sequence'],descriptor['row_count']]:
            value+=number.to_bytes(4,'big')
        for i,row in enumerate(descriptor['rows']):
            fields=[row[k] for k in ['consumed_pad_sequence','source_tick_sequence','draw_enter_sequence',
                'draw_return_sequence','pad_poll_sequence','tick_source_tick','tick_draw_ordinal',
                'draw_source_tick','poll_source_tick','poll_draw_ordinal']]
            for number in fields:value+=number.to_bytes(4,'big')
            value+=raw[i*44:(i+1)*44]
        arrays='\n'.join('const std::vector<uint8_t> '+name+'={'+','.join(map(str,data))+'};'
            for name,data in [('input',value),('context_bytes',context),('raw',raw)])
        cpp=self.scratch/('finite-decoder-'+label+'.cpp')
        cpp.write_text('#include "'+str(CANDIDATE/'src/stadium_first_css_diagnostic_input.hpp')+'"\n'
          '#include <cassert>\n#include <iostream>\n'+arrays+r'''
using namespace melee_web::stadium_first_css_diagnostic;
static void be(std::vector<uint8_t>& b,size_t at,uint32_t value){
 for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(value>>(24-8*i));
}
int main(){
 const auto context=decode_constructor_return_context_v2(context_bytes.data(),context_bytes.size());
 auto decoded=decode_fresh_finite_css_v4(input.data(),input.size(),context);
 assert(decoded.rows.size()==raw.size()/44 && input.size()==60+decoded.rows.size()*84);
 for(size_t i=0;i<decoded.rows.size();++i){
  const auto& row=decoded.rows[i];assert(row.source_tick==i&&row.draw_ordinal==i);
  assert(row.consume_sequence==read_be32(input.data()+60+i*84));
  assert(std::equal(row.consumed.ports.begin(),row.consumed.ports.end(),raw.begin()+i*44));
  PADStatus status[4]{};decode_consumed_pad_statuses(row.consumed,status);
  for(size_t port=0;port<4;++port){const auto* bytes=raw.data()+i*44+port*11;
   assert(status[port].button==((uint16_t(bytes[0])<<8)|bytes[1]));
   assert(status[port].stickX==int8_t(bytes[2])&&status[port].stickY==int8_t(bytes[3]));
   assert(status[port].substickX==int8_t(bytes[4])&&status[port].substickY==int8_t(bytes[5]));
   assert(status[port].triggerLeft==bytes[6]&&status[port].triggerRight==bytes[7]);
   assert(status[port].analogA==bytes[8]&&status[port].analogB==bytes[9]);
   assert(status[port].err==int8_t(bytes[10]));
  }
 }
 unsigned refusals=0;
 auto refuses=[&](std::vector<uint8_t> bytes,ContextInput owner){
  bool stopped=false;try{decode_fresh_finite_css_v4(bytes.data(),bytes.size(),owner);}
  catch(const std::exception&){stopped=true;}assert(stopped);++refusals;
 };
 auto changed=input;changed.pop_back();refuses(changed,context);
 changed=input;changed.push_back(0);refuses(changed,context);
 for(size_t at:{size_t(0),size_t(8),size_t(12),size_t(44),size_t(48),size_t(52),size_t(56)}){
  changed=input;changed[at]^=1;refuses(changed,context);
 }
 changed=input;be(changed,56,0xffffffff);refuses(changed,context);
 changed=input;be(changed,56,1);refuses(changed,context);
 changed=input;std::fill(changed.begin()+12,changed.begin()+44,0);refuses(changed,context);
 for(size_t i:{size_t(0),decoded.rows.size()/2,decoded.rows.size()-1}){
  for(unsigned field=0;field<10;++field){
   changed=input;const size_t offset=60+i*84+field*4;
   be(changed,offset,read_be32(changed.data()+offset)+1);refuses(changed,context);
  }
 }
 changed=input;be(changed,60,0xffffffff);refuses(changed,context);
 changed=input;changed[60+40+10]=0xff;refuses(changed,context);
 auto wrong=context;wrong.source_sha256=std::string(64,'2');refuses(input,wrong);
 wrong=context;wrong.envelope_version=1;refuses(input,wrong);
 bool legacy=false;try{decode_postdraw_input(input.data(),input.size(),context.source_sha256);}
 catch(const std::exception&){legacy=true;}assert(legacy);
 std::cout<<"PASS real finite decoder; literal rows="<<decoded.rows.size()<<"; refusals="<<refusals<<"\n";
}
''')
        js=self.scratch/('finite-decoder-'+label+'.js')
        self.command([str(compiler),'-std=gnu++20','-O0','-fexceptions','-DAURORA_ENABLE_GX','-DTARGET_PC',str(cpp),'-o',str(js),'-sENVIRONMENT=node','-sEXIT_RUNTIME=1',
            '-sASSERTIONS=1','-sSTACK_SIZE=1048576','-I'+str(CANDIDATE/'src'),
            '-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/gameplay-source/src'),
            '-I'+str(ROOT/'.deps/aurora/include')],'finite-decoder-compile-'+label)
        output=self.command([str(node_runtime()),str(js)],'finite-decoder-run-'+label,30)
        self.assertIn('PASS real finite decoder',output);print(label+': '+output.strip())



    def test_whole_native_source_loop_pending_and_command_boundary(self):
        self.scratch=self.scratch/'whole-wrapper';self.scratch.mkdir()
        self.test_actual_native_finite_stages_and_terminal_tick_branch(whole_wrapper=True)

    def test_actual_native_finite_stages_and_terminal_tick_branch(self, whole_wrapper=False):
        # Reuse the established explicit source-owner service fixture. Native
        # arm/codec and command-first callback fences are separate controls.
        from test_stadium_fresh_css_row_controls import (OWNER_FIXTURE,OWNER_STATE,OWNER_MAIN,ARM_SERVICES,
            FRESH_SSS_PAIR_CALLBACK_STATE,fresh_sss_pair_callback_selector)
        source=(CANDIDATE/'src/gameplay_menu_browser.cpp').read_text()
        def cut(a,b):
            self.assertEqual(source.count(a),1,a);self.assertEqual(source.count(b),1,b)
            return source[source.index(a):source.index(b)]
        enums_and_states=cut('enum class FirstCssBrowserMode : unsigned {','enum class CurrentSssExitProfile : unsigned {')
        final_state=cut('struct FirstCssFinalPendingDrawState {','struct FirstSssConstructorPairState {')
        globals=OWNER_STATE[OWNER_STATE.index('MeleeWebMenuHost host_storage'):]
        globals=globals.replace('struct Preparation{bool active=false;bool busy(){return active;}}',
            'struct Preparation{bool active=false;bool busy(){return active;} melee_web::MenuPreparationState::Phase phase(){return melee_web::MenuPreparationState::Phase::Idle;}}')
        fixture='\n'.join(line for line in OWNER_FIXTURE.splitlines() if not line.startswith('#include'))
        fixture=fixture.replace('#define EM_ASM_INT(...) service_commands()','#define EM_ASM_INT(...) 1')
        states='struct MeleeWebMenuCssReturnSnapshot{uint32_t random_seed=0;};\n'+enums_and_states+'FirstCssBrowserPostdrawStreamState first_css_browser_postdraw_stream;\n'+final_state+FRESH_SSS_PAIR_CALLBACK_STATE+fresh_sss_pair_callback_selector(source)
        actual_failure=cut('void first_css_browser_draw_fail(const std::string& error){','void first_css_browser_draw_check_live(')
        actual_live_and_capture=cut('void first_css_browser_draw_check_live(const char* boundary){','void first_css_browser_draw_write_route(')
        helpers=cut('unsigned first_css_postdraw_count(){','std::string first_css_browser_postdraw_stream_actual_json(')
        failures=cut('void first_css_browser_postdraw_stream_fail(const std::string& error,','int first_css_final_pending_draw_compare(')
        live=cut('void first_css_final_pending_draw_check_live(const char* boundary){','FirstCssDrawSample first_css_final_pending_draw_capture(')
        stream_live=cut('void first_css_browser_postdraw_stream_check_live(const char* boundary,','bool first_css_browser_postdraw_stream_compare(')
        stages=cut('int melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm(){','int melee_web_native_menu_stadium_first_sss_draw_arm()')
        start=source.index('   if(first_css_browser_postdraw_stream.armed){\n    auto& stream=first_css_browser_postdraw_stream;\n    check(stream.kicked')
        select=source[start:source.index('   }else if(first_css_browser_draw.armed){',start)]+'   }\n'
        start=source.index('    if(first_css_browser_postdraw_stream.armed){\n     auto& stream=first_css_browser_postdraw_stream;\n     check(stream.current_input_index')
        tick=source[start:source.index('    }else{\n     if(first_css_browser_draw.armed){',start)]+'    }\n'
        reset=OWNER_MAIN[:OWNER_MAIN.index('int main(){')].replace('FreshConsumedTickDrawV3','FreshFiniteCssPrefixV4')
        reset=reset.replace('first_css_browser_draw={};host=', 'first_css_browser_draw={};first_css_browser_postdraw_stream={};first_css_final_pending_draw={};host=')

        callback='\nvoid source_callback(){int result=0;char error[256]{};bool first_css_draw_terminal=false;\n do{\n'+select+'\nconst PADStatus* sample=first_css_browser_postdraw_stream.input_status;\n'+tick+'\n}while(false);(void)result;(void)first_css_draw_terminal;}\n'
        services=NATIVE_SERVICES;main=NATIVE_MAIN;extra='';includes=''
        if whole_wrapper:
            includes='#include "'+str(ROOT/'src/menu_preparation_state.hpp')+'"\n'
            fixture='#define MELEE_WEB_PUBLIC_RUNTIME 1\n'+fixture
            fixture=fixture.replace('#define EM_ASM_INT(...) 1','#define EM_ASM_INT(...) control_em_asm(#__VA_ARGS__)')
            globals=globals.replace('bool match=false,results=false,prize=false,replay=false,net_active=false,healthy=true,world_exists=true;',
                'FakeSession *match=nullptr,*results=nullptr,*prize=nullptr;FakeReplay* replay=nullptr;bool net_active=false,healthy=true,world_exists=true;')
            globals=globals.replace('struct Preparation{bool active=false;bool busy(){return active;} melee_web::MenuPreparationState::Phase phase(){return melee_web::MenuPreparationState::Phase::Idle;}}preparation;',
                '::melee_web::MenuPreparationState preparation;')
            fixture+=WRAPPER_TYPES
            services=services.replace('void did_step(){++count;}',
                'void did_step(bool=true){++count;}template<class F>void before_step(F){}')
            services=services.replace('assert(sample[0].button==first_css_browser_draw.finite_css.rows[first_css_browser_postdraw_stream.current_input_index+1].consumed.ports[1]);',
                'if(first_css_browser_postdraw_stream.armed)assert(sample[0].button==first_css_browser_draw.finite_css.rows[first_css_browser_postdraw_stream.current_input_index+1].consumed.ports[1]);')
            services+=WRAPPER_SERVICES
            reset=reset.replace('match=false;results=false;prize=false;replay=false;',
                'match=nullptr;results=nullptr;prize=nullptr;replay=nullptr;').replace('preparation.active=false;','preparation.reset();')
            loop=cut('  for(unsigned step=0;step<elapsed.steps;step++){',
                '#if !defined(MELEE_WEB_PUBLIC_RUNTIME)\n  replay_boundary_mark("ordinary_audio_boundary_begin"')
            self.assertTrue(loop.rstrip().endswith('}'))
            terminal_end='      first_css_browser_draw_refresh_observation();\n      break;\n     }\n     check(first_css_browser_draw.mode!='
            self.assertEqual(loop.count(terminal_end),1)
            mutant=loop.replace(terminal_end,terminal_end.replace('break;','goto control_shared_result3;'))
            marker='   if(result==3){\n    if(results){'
            self.assertEqual(mutant.count(marker),1)
            mutant=mutant.replace(marker,'control_shared_result3:\n'+marker)
            def wrap(name,body):
                return '\nvoid '+name+'(){char error[256]{};bool first_css_draw_terminal=false;struct{unsigned steps=1;}elapsed;auto present_source=[](){return false;};\n'+body+'\n(void)first_css_draw_terminal;}\n'
            callback=wrap('source_callback',loop)+wrap('source_callback_mutated_fallthrough',mutant)
            fences=cut(' // Diagnostic input must never override the producer-bound literal row.',' if(first_sss_vs_setup_service())return;')
            preparation=cut('void begin_preparation(){','bool preparation_uses_source_draws(){')
            extra=preparation+'\nvoid next_command_callback(){\n const int owner_action=EM_ASM_INT({return window.menuServiceCommands?.() || 0;});\n if(owner_action<0)return;\n'+fences+'\n if(pending)begin_preparation();\n}\n'
            main=NATIVE_MAIN[:NATIVE_MAIN.index('int run(){')]+WRAPPER_MAIN
        cpp=self.scratch/'finite-native.cpp'
        cpp.write_text(includes+'#include "'+str(CANDIDATE/'src/stadium_first_css_diagnostic_input.hpp')+'"\n'
            '#include <cassert>\n#include <cstring>\n#include <iostream>\n#include <memory>\n#include <vector>\n'
            'namespace control {\n'+fixture+'\nnamespace melee_web{namespace stadium_first_css_diagnostic=::melee_web::stadium_first_css_diagnostic; struct MenuPreparationState{enum class Phase{Idle};};}\n'
            +states+globals+ARM_SERVICES.replace(',first_css_browser_postdraw_stream,first_css_final_pending_draw;',';')+services+actual_failure+actual_live_and_capture+helpers+failures+
            'void first_css_browser_postdraw_stream_check_live(const char*,uint32_t);\n'+live+stream_live+stages+reset+
            callback+extra+main+'\n}\nint main(){return control::run();}\n')
        if whole_wrapper:
            cpp.write_text(cpp.read_text().replace('struct MenuPreparationState{enum class Phase{Idle};};','using MenuPreparationState=::melee_web::MenuPreparationState;'))
        js=self.scratch/'finite-native.js'
        compiler=ROOT/'.deps/emsdk/upstream/emscripten/em++'
        self.command([str(compiler),str(cpp),'-o',str(js),'-std=gnu++20','-O0','-fexceptions',
            '-DAURORA','-DAURORA_ENABLE_GX','-DTARGET_PC','-sEXIT_RUNTIME=1','-sSTACK_SIZE=1048576',
            '-I'+str(ROOT/'src'),'-I'+str(ROOT/'build/gameplay-source/src'),'-I'+str(ROOT/'.deps/aurora/include')],'finite-native-compile')
        output=self.command([str(node_runtime()),str(js)],'finite-native-run',30)
        self.assertIn('PASS whole native source loop' if whole_wrapper else 'PASS actual finite native stages',output);print(output.strip())

# Explicit source-tick, renderer/comparator services are doubles. The extracted
# native branch below observes their return; this is no original-game parity run.
NATIVE_SERVICES=r'''
unsigned native_ticks=0,host_final_arms=0,transition_compares=0,tick_compares=0;
unsigned final_ordinal=0,final_sequence=0;int native_result=1;
bool transition_approval=false;
std::array<uint8_t,MELEE_WEB_PAD_STATE_BYTES> pad{};
void melee_web_pad_state_capture(uint8_t* out){std::copy(pad.begin(),pad.end(),out);}
int gm_GetCurrentGameMode(){return 2;}int gm_GetPreviousGameMode(){return 0;}
int gm_GetCurrentSceneIndex(){return 0;}int gm_GetPreviousSceneIndex(){return 0;}
struct Frames{unsigned count=0;unsigned steps(){return count;}unsigned draws(){return 0;}bool pending(){return false;}void did_step(){++count;}}source_frames;
int melee_web_menu_host_tick(MeleeWebMenuHost*,const PADStatus* sample,char*,size_t){
 ++native_ticks;++frame;assert(sample[0].button==first_css_browser_draw.finite_css.rows[first_css_browser_postdraw_stream.current_input_index+1].consumed.ports[1]);return native_result;}
void current_sss_exit_tick_begin(){}void current_sss_exit_tick_returned(){}
bool first_css_browser_postdraw_stream_compare(const char* phase,const std::string&){
 if(std::string(phase)=="transition"){++transition_compares;return transition_approval;}
 assert(std::string(phase)=="tick");++tick_compares;return true;
}
std::string first_css_browser_postdraw_stream_actual_json(const FirstCssDrawSample&,unsigned,unsigned,int,bool){return "explicit comparator double";}
int melee_web_menu_host_arm_final_pending_css_draw(MeleeWebMenuHost*,unsigned,unsigned,const PADStatus*,char*,size_t){assert(false);return 0;}
int melee_web_menu_host_arm_fresh_final_pending_css_draw_v4(MeleeWebMenuHost*,unsigned ordinal,unsigned seq,const PADStatus*,char*,size_t){
 ++host_final_arms;final_ordinal=ordinal;final_sequence=seq;return 1;
}
'''
NATIVE_MAIN=r'''
void baseline(unsigned count){reset();enter();auto& b=first_css_browser_draw;
 b.kicked=true;b.complete=true;b.tick_captured=true;b.draw_captured=true;b.frame_end_returned=true;b.tick_result=1;
 b.source_steps=b.source_draws=b.host_tick_calls=b.host_draw_calls=b.aurora_begin_calls=b.aurora_end_calls=1;
 b.context.source_sha256="explicit-control-input";b.finite_css.source_sha256=b.context.source_sha256;
 b.finite_css.rows.resize(count);
 for(unsigned i=0;i<count;i++){auto& row=b.finite_css.rows[i];row.draw_ordinal=i;row.consume_sequence=1000+i*5;
  row.consumed.source_sha256=b.context.source_sha256;row.consumed.ports[1]=uint8_t(i+1);}
 frame=1;b.draw.scene_frame=1;b.draw.seed=seed;running=false;first_use_draw_pending=false;
 native_ticks=host_final_arms=transition_compares=tick_compares=0;source_frames={};transition_approval=false;pad={};
}
void before(unsigned index){auto& s=first_css_browser_postdraw_stream;
 s.next_input_index=index;s.current_input_index=index;s.consumed_inputs=index;s.host_tick_calls=index;
 s.executed_host_ticks=index+1;s.matched_ticks=index;s.matched_draws=index;s.host_draw_calls=index;
 s.tick_captured=false;s.transition_captured=false;s.frame_end_returned=false;
 s.aurora_begin_calls=s.aurora_end_calls=index;frame=1+index;source_frames={};}
int run(){
 for(unsigned count:{3u,7u}){
  baseline(count);assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
  assert(first_css_postdraw_count()==count-1&&first_css_postdraw_consume(0)==first_css_browser_draw.finite_css.rows[1].consume_sequence);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==0);
  assert(melee_web_native_menu_stadium_first_css_postdraw_stream_arm(nullptr,0)==0);
  assert(melee_web_native_menu_stadium_first_css_postdraw_stream_kick()==0&&!first_css_browser_postdraw_stream.failed);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);
  before(0);native_result=1;source_callback();assert(native_ticks==1&&tick_compares==1&&transition_compares==0);
  assert(first_css_browser_postdraw_stream.matched_ticks==1&&source_frames.count==1);
  before(count-2);native_result=3;source_callback();auto& s=first_css_browser_postdraw_stream;
  assert(s.terminal&&!s.failed&&!s.complete&&s.tick_result==3&&s.transition_captured&&!s.frame_end_returned);
  assert(s.outcome=="stop_after_last_input_request"&&transition_compares==1&&!running);
  assert(s.host_draw_calls==count-2&&s.matched_ticks==count-2&&s.matched_draws==count-2);
  assert(melee_web_native_menu_stadium_first_css_final_draw_arm()==0&&!first_css_final_pending_draw.armed);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm()==1);
  assert(host_final_arms==1&&final_ordinal==count-1&&final_sequence==first_css_browser_draw.finite_css.rows.back().consume_sequence);
  assert(melee_web_native_menu_stadium_first_css_final_draw_kick()==0&&!first_css_final_pending_draw.kicked);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_kick()==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_kick()==0&&first_css_final_pending_draw.failed);
 }
 for(int bad=0;bad<8;bad++){
  baseline(4);assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);before(0);
  scene_reads=frame_reads=0;
  switch(bad){case 0:host=&other_host;break;case 1:source_session_owned=false;break;case 2:++generation;break;
   case 3:diagnostic_start_ticks=1;break;case 4:diagnostic_pad_remaining=1;break;case 5:pending=true;break;
   case 6:seed_ptr=nullptr;break;case 7:net_active=true;break;}
  bool refused=false;try{source_callback();}catch(const std::exception&){refused=true;}
  assert(refused&&native_ticks==0&&tick_compares==0&&transition_compares==0&&host_final_arms==0);
  if(bad!=6)assert(scene_reads==0);
 }
 for(int bad=0;bad<4;bad++){
  baseline(4);assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);
  before(bad==0?0:2);native_result=bad==1?1:bad==2?2:3;transition_approval=bad==3;
  bool refused=false;try{source_callback();}catch(const std::exception&){refused=true;}
  if(bad==0)assert(first_css_browser_postdraw_stream.failed&&first_css_browser_postdraw_stream.outcome=="early_source_transition_before_raw_pair_cap");
  else assert(refused);
  assert(native_ticks==1&&host_final_arms==0);
 }
 for(int bad=0;bad<10;bad++){
  baseline(4);assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);before(2);native_result=3;source_callback();
  native_ticks=0;scene_reads=frame_reads=0;
  switch(bad){case 0:host=&other_host;break;case 1:source_session_owned=false;break;case 2:++generation;break;
   case 3:diagnostic_start_ticks=1;break;case 4:diagnostic_pad_remaining=1;break;case 5:pending=true;break;
   case 6:seed_ptr=nullptr;break;case 7:net_active=true;break;case 8:++seed;break;case 9:pad[0]^=1;break;}
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm()==0);
  assert(first_css_final_pending_draw.failed&&host_final_arms==0&&native_ticks==0);
 }
 baseline(3);assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
 assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);
 assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==0&&first_css_browser_postdraw_stream.failed&&native_ticks==0);
 for(auto mode:{FirstCssBrowserMode::LegacyTickDrawV1,FirstCssBrowserMode::ConstructorReturnOnlyV2,FirstCssBrowserMode::FreshConsumedTickDrawV3}){
  baseline(3);first_css_browser_draw.mode=mode;
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==0);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==0);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm()==0);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_kick()==0&&native_ticks==0&&host_final_arms==0);
 }
 std::cout<<"PASS actual finite native stages: dynamic 3/7-row reservations; actual input selection and result3 terminal branch; 8 source-free owner/input refusals;10 final-owner/pointer/value refusals; early/missing/unsupported/approved-terminal refusals; V1/V2/V3 isolation\\n";return 0;
}
'''
WRAPPER_TYPES=r'''
struct FakeSession{
 struct Stats{int stocks=4,ground_or_air=0,motion_id=0;float position[3]{};};
 unsigned source_frames(){assert(false);return 0;}bool paused(){assert(false);return false;}
 bool complete(){assert(false);return false;}bool ending(){assert(false);return false;}
 bool opening_demo(){assert(false);return false;}bool sudden_death(){assert(false);return false;}
 bool requested(){assert(false);return false;}void tick(const PADStatus*){assert(false);}
 int outcome(int&){assert(false);return 0;}Stats player_stats(int){assert(false);return {};}
};
struct FakeReplay{
 struct Frame{std::array<PADStatus,4> pads{};};std::vector<Frame> frames;
 bool diagnostic_entity_prefix=false,diagnostic_active_entity_prefix=false;
 bool whole_session(){assert(false);return false;}bool closes_draw_batch(size_t){assert(false);return false;}
};
'''
WRAPPER_SERVICES=r'''
int control_em_asm(const char* code){return std::strstr(code,"menuServiceCommands")?service_commands():1;}
struct RawInput{PADStatus raw[4]{};unsigned samples=0;}input_storage;RawInput* input=&input_storage;
namespace melee_web{
 struct ResultsSourceFramePauseSchedule{enum class Boundary{none,missed,due};};
 template<class...T>void retail_replay_session_initial(T&&...){assert(false);}
 template<class...T>void retail_replay_frame(T&&...){assert(false);}
}
struct Schedule{
 melee_web::ResultsSourceFramePauseSchedule::Boundary before_tick(unsigned){assert(false);return melee_web::ResultsSourceFramePauseSchedule::Boundary::none;}
 bool all_consumed(){assert(false);return true;}
}scheduled_results_pad,scheduled_results_pauses;
void diagnostic_incident(int){assert(false);}void activate_scheduled_results_pad(unsigned,const PADStatus*){assert(false);}
void clear_scheduled_results_pad(){assert(false);}void clear_scheduled_results_pauses(){assert(false);}
unsigned replay_cursor=0,replay_steps=0,replay_draw_boundaries=0;bool replay_started=false,replay_trace=false,replay_match_complete=false;
int replay_outcome=0,replay_winner=0;int stock_check=0,stock_tick=0,stock_count=4,stock_respawns=0;
bool stock_lost=false,stock_jump=false;constexpr int OUTCOME_ELIMINATION=1;
PADStatus diagnostic_pad{};unsigned diagnostic_pad_port=0;
bool prepare_deferred_pipelines(){return false;}
bool melee_web_net_capture_local_input(unsigned,const PADStatus*){assert(false);return false;}
const PADStatus* melee_web_net_before_step(uint32_t){assert(false);return nullptr;}
void melee_web_net_after_step(){assert(false);}
int observed_replay_scene(){assert(false);return 1;}int expected_replay_scene(const FakeReplay&,unsigned){assert(false);return 1;}
std::string replay_scene_mismatch(const char*,unsigned,int,int){assert(false);return {};}
void melee_web_cpu_observation_set_event_cursor(unsigned){assert(false);}void melee_web_cpu_observation_scheduler_return(){assert(false);}
struct Progress{template<class...T>bool observe(T&&...){assert(false);return false;}}replay_prefix_progress;
void current_sss_exit_trace(const char*,unsigned,unsigned){assert(false);}
unsigned clear_diagnostic_calls=0,source_leave_calls=0,preparation_profile_calls=0;
void clear_diagnostic_pad(){++clear_diagnostic_calls;diagnostic_pad_remaining=0;}
int pending_menu_source_scene=0;bool transition_audio_continues=false;
struct Profile{void begin(bool,double){++preparation_profile_calls;}}preparation_profile;
double emscripten_get_now(){return 0;}
int melee_web_menu_host_leave(MeleeWebMenuHost*,int,char*,size_t){
 assert(preparation.waiting_for_audio()&&!pending&&!running);++source_leave_calls;phase=5;source_scene=0;return 1;
}
'''
WRAPPER_MAIN=r'''
int run(){
 for(unsigned count:{3u,7u}){
  baseline(count);clear_diagnostic_calls=source_leave_calls=preparation_profile_calls=0;
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);
  before(count-2);native_result=3;
  source_callback();auto& s=first_css_browser_postdraw_stream;
  assert(s.terminal&&!s.failed&&s.outcome=="stop_after_last_input_request"&&s.tick_result==3);
  assert(native_ticks==1&&transition_compares==1&&!pending&&!running&&!preparation.busy());
  assert(clear_diagnostic_calls==0&&source_leave_calls==0&&preparation_profile_calls==0);
  next_command_callback();assert(!first_css_browser_draw.failed&&!pending&&!preparation.busy()&&native_ticks==1&&source_leave_calls==0);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm()==1&&host_final_arms==1);
  assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_kick()==1&&!pending&&!preparation.busy());
  // Rendering remains covered by the real host fixture; this whole-loop control
  // stops at its one-use final authorization, without a fake renderer pass.
 }
 baseline(4);clear_diagnostic_calls=source_leave_calls=preparation_profile_calls=0;
 assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_arm()==1);
 assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_stream_kick()==1);before(2);native_result=3;
 source_callback_mutated_fallthrough();
 assert(first_css_browser_postdraw_stream.terminal&&pending&&clear_diagnostic_calls==1&&!preparation.busy());
 // The disputed shared handler is reached by this test-only control-flow
 // mutation. The unchanged final-arm guard must detect the resulting mismatch.
 assert(melee_web_native_menu_stadium_first_css_fresh_finite_v4_final_arm()==0&&first_css_final_pending_draw.failed&&host_final_arms==0);
 assert(pending); // Refusal never clears genuine pending preparation.
 baseline(3);first_css_browser_draw.mode=FirstCssBrowserMode::None;first_css_browser_draw.armed=false;
 first_css_browser_postdraw_stream={};clear_diagnostic_calls=source_leave_calls=preparation_profile_calls=0;
 native_result=3;source_callback();assert(pending&&clear_diagnostic_calls==1&&native_ticks==1&&!preparation.busy());
 next_command_callback();assert(!pending&&!running&&preparation.waiting_for_audio()&&source_leave_calls==1&&preparation_profile_calls==1&&!host_entered);
 std::cout<<"PASS whole native source loop: final result3 breaks before shared pending handler; next command fence and final authorization succeed; forced shared-tail mutation detected; ordinary result3 enters real preparation\n";
 return 0;
}
'''

if __name__=='__main__': unittest.main()
