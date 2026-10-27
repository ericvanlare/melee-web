"""Actual fresh-row codec and extracted lifecycle/JS controls with explicit doubles.

No game/native constructor, browser, accuracy or timing acceptance is claimed.
"""
from pathlib import Path
import ast
import json
import os
import shutil
import subprocess
import sys
ROOT = Path(os.environ.get('MELEE_FRESH_ROW_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
CANDIDATE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests'))
sys.path.insert(0, str(ROOT/'scripts'))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime


def fresh_sss_pair_callback_selector(source):
    start = 'enum class FirstSssFreshPairCallbackAction : unsigned {'
    end = 'struct FirstSssConsumedTickState {'
    assert source.count(start) == 1 and source.count(end) == 1
    return source[source.index(start):source.index(end)]


# Only the actual selector's global pair-state fields are doubled here.
# The enum and selector implementation are extracted verbatim above.
FRESH_SSS_PAIR_CALLBACK_STATE = r'''
struct PairCallbackState {
 FirstCssBrowserMode profile=FirstCssBrowserMode::None;
 bool armed=false,kicked=false,attempted=false,complete=false,failed=false,frame_terminal=false;
}first_sss_constructor_pair;
// These CSS-only controls never arm an SSS draw. The actual readiness helper
// and first-draw dispatch are extracted by first_sss_pair_native_control.py.
bool first_sss_fresh_draw_callback_ready(){return false;}
bool first_sss_fresh_prefix_callback_ready(){return false;}
bool first_sss_sequence_service(){return false;}
// These CSS-only controls never request Setup; its actual first guard returns false.
bool first_sss_vs_setup_service(){return false;}
'''


class FreshCssRowControls(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, 'fresh-css-row-controls-')

    def command(self, argv, label, timeout=60):
        log = self.scratch/(label+'.log')
        with log.open('w') as stream:
            result = subprocess.run(argv, stdout=stream, stderr=subprocess.STDOUT,
                                    env=dict(os.environ, TMPDIR=str(self.scratch)), timeout=timeout)
        self.assertEqual(result.returncode, 0, log.read_text())
        return log.read_text()

    def test_actual_sdk_header_decoder_and_legacy_separation(self):
        compiler = ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not compiler.is_file():
            self.skipTest('Prepared SDK/Emscripten required for exact-header control')
        cpp = self.scratch/'decoder.cpp'
        control = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
using namespace melee_web::stadium_first_css_diagnostic;
static void be(std::vector<uint8_t>& b,size_t offset,uint32_t value){
 for(unsigned i=0;i<4;i++)b[offset+i]=uint8_t(value>>(24-i*8));
}
int main(){
 ContextInput context{};context.envelope_version=2;context.source_entry_sequence=728;
 context.source_return_sequence=859;context.source_sha256=std::string(64,'1');
 std::vector<uint8_t> row(136);std::memcpy(row.data(),"STC3PAD1",8);be(row,8,3);
 std::fill_n(row.data()+12,32,0x11);
 uint32_t metadata[]={728,859,860,861,862,863,864,0,0,1,1,1};
 for(size_t i=0;i<12;i++)be(row,44+i*4,metadata[i]);
 // Every PAD field varies, including negative signed axes and err bytes.
 for(unsigned i=0;i<44;i++)row[92+i]=uint8_t(128+i);
 auto input=decode_fresh_row_v3(row.data(),row.size(),context);
 assert(input.consume_sequence==860&&input.tick_sequence==861&&input.poll_sequence==864);
 assert(input.consumed.source_sha256==context.source_sha256);
 PADStatus statuses[4]{};decode_consumed_pad_statuses(input.consumed,statuses);
 for(unsigned p=0;p<4;p++){
  auto* r=row.data()+92+p*11;
  assert(statuses[p].button==((uint16_t(r[0])<<8)|r[1]));
  assert(statuses[p].stickX==int8_t(r[2])&&statuses[p].stickY==int8_t(r[3]));
  assert(statuses[p].substickX==int8_t(r[4])&&statuses[p].substickY==int8_t(r[5]));
  assert(statuses[p].triggerLeft==r[6]&&statuses[p].triggerRight==r[7]);
  assert(statuses[p].analogA==r[8]&&statuses[p].analogB==r[9]&&statuses[p].err==int8_t(r[10]));
 }
 unsigned refusals=0;
 auto refused=[&](const std::vector<uint8_t>& b,const ContextInput& ctx){
  try{(void)decode_fresh_row_v3(b.data(),b.size(),ctx);}catch(const std::runtime_error&){++refusals;return;}
  assert(false&&"malformed fresh input was accepted");
 };
 for(size_t n=0;n<row.size();n++)refused(std::vector<uint8_t>(row.begin(),row.begin()+n),context);
 auto extra=row;extra.push_back(0);refused(extra,context);
 for(size_t index=0;index<44;index++){auto b=row;b[index]^=1;refused(b,context);}
 auto zero=row;std::fill_n(zero.data()+12,32,0);auto zero_context=context;zero_context.source_sha256=std::string(64,'0');refused(zero,zero_context);
 for(size_t i=0;i<12;i++){auto b=row;be(b,44+i*4,i<7?0:99);refused(b,context);}
 auto old=context;old.envelope_version=1;refused(row,old);
 auto mismatch=context;mismatch.source_entry_sequence++;refused(row,mismatch);
 mismatch=context;mismatch.source_return_sequence++;refused(row,mismatch);
 mismatch=context;mismatch.source_sha256=std::string(64,'2');refused(row,mismatch);
 // Altered metadata remains a different producer-bound first row, without
 // altering legacy v1 constants or accepting it in historical codecs.
 auto alternate=row;auto alternate_context=context;
 alternate_context.source_entry_sequence=10;alternate_context.source_return_sequence=20;
 uint32_t refs[]={10,20,30,35,40,45,50};
 for(size_t i=0;i<7;i++)be(alternate,44+i*4,refs[i]);
 assert(decode_fresh_row_v3(alternate.data(),alternate.size(),alternate_context).tick_sequence==35);
 bool old_refused=false;try{(void)decode_consumed_pad(row.data(),row.size(),context.source_sha256);}catch(const std::runtime_error&){old_refused=true;}
 assert(old_refused&&kPadConsumeSequence==834&&kSourceTickSequence==835&&kEntrySequence==704&&kReturnSequence==833);
 std::cout<<"PASS exact fresh-row decoder; "<<refusals<<" malformed envelope/context refusals; literal44B SDK PAD fields; legacy separation\n";
}
'''
        cpp.write_text('#include "'+str(CANDIDATE/'src/stadium_first_css_diagnostic_input.hpp')+'"\n'+control)
        js = self.scratch/'decoder.js'
        self.command([str(compiler),str(cpp),'-o',str(js),'-std=gnu++20','-O0','-fexceptions',
                      '-DAURORA','-DAURORA_ENABLE_GX','-DTARGET_PC','-sEXIT_RUNTIME=1',
                      '-sSTACK_SIZE=1048576','-I'+str(ROOT/'src'),
                      '-I'+str(ROOT/'build/gameplay-source/src'),'-I'+str(ROOT/'.deps/aurora/include')], 'decoder-compile')
        self.assertIn('PASS exact fresh-row decoder',self.command([str(node_runtime()),str(js)],'decoder-run',30))

    def test_actual_arm_decodes_before_reserving_or_applying_context(self):
        compiler = ROOT/'.deps/emsdk/upstream/emscripten/em++'
        if not compiler.is_file():
            self.skipTest('Prepared SDK/Emscripten required for extracted arm control')
        source = (CANDIDATE/'src/gameplay_menu_browser.cpp').read_text()
        def cut(a,b):
            return source[source.index(a):source.index(b)]
        enum = cut('enum class FirstCssBrowserMode : unsigned {','struct FirstCssDrawSample {')
        failure = cut('void first_css_browser_draw_fail(const std::string& error){','void first_css_browser_draw_check_live(')
        arm = cut('int first_css_fresh_input_arm(',
                  'int melee_web_native_menu_stadium_first_css_draw_kick()')
        state = OWNER_STATE.replace(' int tick_result=0;std::string error;',
            ' melee_web::stadium_first_css_diagnostic::ContextInput context{};\n'
            ' melee_web::stadium_first_css_diagnostic::ConsumedPadInput consumed{};\n'
            ' melee_web::stadium_first_css_diagnostic::FreshRowInput fresh_row{};\n'
            ' melee_web::stadium_first_css_diagnostic::FreshFiniteCssInput finite_css{};\n'
            ' PADStatus consumed_status[4]{};\n int tick_result=0;std::string error;')
        fixture = '\n'.join(line for line in OWNER_FIXTURE.splitlines() if not line.startswith('#include'))
        cpp = self.scratch/'arm.cpp'
        cpp.write_text('#include "'+str(CANDIDATE/'src/stadium_first_css_diagnostic_input.hpp')+'"\n'
            '#include <cassert>\n#include <cstring>\n#include <iostream>\n#include <memory>\n#include <vector>\n'
            'namespace control {\n'+fixture+
            '\nnamespace melee_web { namespace stadium_first_css_diagnostic = ::melee_web::stadium_first_css_diagnostic; }\n'
            +enum+state+ARM_SERVICES+failure+arm+ARM_MAIN.replace(' std::cout<<"PASS actual fresh arm;',FINITE_ARM_MAIN+'\n std::cout<<"PASS actual fresh arm;')+'\n}\nint main(){return control::run();}\n')
        js = self.scratch/'arm.js'
        self.command([str(compiler),str(cpp),'-o',str(js),'-std=gnu++20','-O0','-fexceptions',
                      '-DAURORA','-DAURORA_ENABLE_GX','-DTARGET_PC','-sEXIT_RUNTIME=1',
                      '-sSTACK_SIZE=1048576','-I'+str(ROOT/'src'),
                      '-I'+str(ROOT/'build/gameplay-source/src'),'-I'+str(ROOT/'.deps/aurora/include')], 'arm-compile')
        self.assertIn('PASS actual fresh arm; codecs precede reservation/mutation',
                      self.command([str(node_runtime()),str(js)],'arm-run',30))

    def test_actual_receipt_helper_and_existing_driver_prefix(self):
        output = self.command([str(node_runtime()),
            str(CANDIDATE/'tests/stadium_fresh_css_row_controls.mjs'), str(CANDIDATE)], 'js-controls', 20)
        self.assertIn('"result":"PASS"', output)
        print(output.strip())

    def test_finite_owner_kick_and_source_free_callback_fences(self):
        self.scratch=self.scratch/'finite-owner';self.scratch.mkdir()
        self.test_extracted_owner_kick_and_source_free_callback_fences(finite=True)

    def test_extracted_owner_kick_and_source_free_callback_fences(self, finite=False):
        compiler = shutil.which('clang++')
        if not compiler:
            self.skipTest('clang++ required for owner control')
        source = (CANDIDATE/'src/gameplay_menu_browser.cpp').read_text()
        def cut(a,b):
            self.assertEqual(source.count(a),1,a)
            self.assertEqual(source.count(b),1,b)
            return source[source.index(a):source.index(b)]
        enum=cut('enum class FirstCssBrowserMode : unsigned {','struct FirstCssDrawSample {')
        failure=cut('void first_css_browser_draw_fail(const std::string& error){','void first_css_browser_draw_check_live(')
        live=cut('void first_css_browser_draw_check_live(const char* boundary){','FirstCssDrawSample first_css_browser_draw_capture(')
        constructor=cut('bool stadium_first_css_constructor_return_active(){','\nuint32_t asset_generation=0;\n')
        shared=cut('// Shared private prepared-launch admission.','std::vector<std::string> requested_assets;')
        kicks=cut('int melee_web_native_menu_stadium_first_css_draw_kick(){','const char* melee_web_native_menu_stadium_first_css_draw_observe()')
        fences=cut(' // Diagnostic input must never override the producer-bound literal row.',' if(first_sss_sequence_service())return;')
        command_line=' const int owner_action=EM_ASM_INT({return window.menuServiceCommands?.() || 0;});'
        self.assertIn(command_line,source)
        self.assertLess(source.index(command_line),source.index(fences))
        menu=(CANDIDATE/'src/gameplay_menu_browser_menu.cpp').read_text()
        admission=menu[menu.index('int melee_web_native_menu_prepare(){'):]
        admission=admission[:admission.index('#if defined(MELEE_WEB_PIPELINE_PROVENANCE)')]
        fixture=OWNER_FIXTURE+enum+OWNER_STATE+fresh_sss_pair_callback_selector(source)+failure+live+constructor+shared+kicks
        fixture+='\n'+admission+'++prepare_mutations;return 1;}catch(...){return 0;}}\n'
        launch_admission=menu[menu.index('int melee_web_native_menu_launch(){try{'):
            menu.index(' if(preparation.busy()||pending)', menu.index('int melee_web_native_menu_launch(){try{'))]
        fixture+='\n'+launch_admission+'++constructor_entries;return 1;}catch(...){return 0;}}\n'
        main=OWNER_MAIN
        if finite:
            main=main.replace('FreshConsumedTickDrawV3','FreshFiniteCssPrefixV4').replace('melee_web_native_menu_stadium_first_css_fresh_row_v3_kick','melee_web_native_menu_stadium_first_css_fresh_finite_v4_first_kick')
            main=main.replace('for(int mode:{1,2})','for(int mode:{1,2,3})')
            main=main.replace(' reset();enter();s.mode=FirstCssBrowserMode::LegacyTickDrawV1;',r'''
 for(bool final_failed:{false,true}){reset();enter();auto& b=first_css_browser_draw;
  b.kicked=true;b.complete=true;b.tick_captured=true;b.draw_captured=true;b.frame_end_returned=true;b.tick_result=1;
  b.source_steps=b.source_draws=b.host_tick_calls=b.host_draw_calls=b.aurora_begin_calls=b.aurora_end_calls=1;
  running=false;first_use_draw_pending=false;
  first_css_browser_postdraw_stream.armed=first_css_browser_postdraw_stream.kicked=first_css_browser_postdraw_stream.terminal=true;
  first_css_final_pending_draw.kicked=true;first_css_final_pending_draw.failed=final_failed;first_css_final_pending_draw.complete=!final_failed;
  callback();callback();assert(commands==2&&b.fresh_terminal_callbacks==2&&!b.failed&&poll_or_audio_or_preparation_calls==0);
  queued_unload=true;callback();assert(!host&&!world&&commands==3&&poll_or_audio_or_preparation_calls==1);
 }
 reset();enter();s.mode=FirstCssBrowserMode::LegacyTickDrawV1;''')

        fixture+='\nvoid callback(){'+command_line+'\nif(owner_action<0)return;\n'+fences+'\n++poll_or_audio_or_preparation_calls;}\n'+main
        cpp=self.scratch/'owners.cpp';cpp.write_text(fixture)
        binary=self.scratch/'owners'
        self.command([compiler,'-std=c++20','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],'owners-compile')
        self.assertIn('PASS actual private owner/kick and command-first source-free fences',self.command([str(binary)],'owners-run',20))


OWNER_FIXTURE=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define EM_ASM(...) do{}while(0)
#define EM_ASM_INT(...) service_commands()
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct MeleeWebMenuHost{};
namespace melee_web{struct GameplayMenuWorld{};}
struct GameModeState{struct GameSceneInfo{int scene_kind=8;};};
constexpr int GM_VS=2,MELEE_WEB_MENU_CREATED=0,MELEE_WEB_MENU_CSS=1,MELEE_WEB_MENU_HOST_SCENE_CSS=1;
'''
OWNER_STATE=r'''
struct State{
 FirstCssBrowserMode mode=FirstCssBrowserMode::None;
 bool armed=false,kicked=false,failed=false,complete=false,entry_captured=false,tick_captured=false,draw_captured=false,frame_end_returned=false;
 bool constructor_arm_ready=false,constructor_launch_consumed=false,constructor_entry_attempted=false,constructor_return_terminal=false,arm_source_session_owned=false;
 unsigned source_steps=0,source_draws=0,host_tick_calls=0,host_draw_calls=0,aurora_begin_calls=0,aurora_end_calls=0,source_callbacks_before_kick=0,fresh_terminal_callbacks=0;
 int tick_result=0;std::string error;
 MeleeWebMenuHost* armed_host_owner=nullptr;melee_web::GameplayMenuWorld* armed_world_owner=nullptr;
 uint64_t armed_world_generation=0,world_generation=0;
 const uint32_t* seed_owner=nullptr;const GameModeState::GameSceneInfo* scene_owner=nullptr;
 struct{uint32_t random_seed=0;}entry;
}first_css_browser_draw;
struct Stage{bool armed=false,kicked=false,terminal=false,failed=false,complete=false,compared=false,approved=false,frame_end_returned=false;std::string outcome;}first_css_browser_postdraw_stream,first_css_final_pending_draw;
'''+FRESH_SSS_PAIR_CALLBACK_STATE+r'''
MeleeWebMenuHost host_storage,other_host;MeleeWebMenuHost* host=nullptr;
std::unique_ptr<melee_web::GameplayMenuWorld> world;
GameModeState::GameSceneInfo scene;
uint32_t seed=17,frame=0;const uint32_t* seed_ptr=nullptr;
bool host_entered=false,world_exposed=false,source_session_owned=false,running=false,pending=false,first_use_draw_pending=false,faulted=false;
bool match=false,results=false,prize=false,replay=false,net_active=false,healthy=true,world_exists=true;
uint64_t generation=0;int phase=0,kind=GM_VS,source_scene=1;
unsigned audio_phase=0,refreshes=0,scene_reads=0,frame_reads=0,prepare_mutations=0,commands=0,poll_or_audio_or_preparation_calls=0;
unsigned diagnostic_start_ticks=0,diagnostic_pad_remaining=0,constructor_entries=0;
bool queued_unload=false;
std::string message;
struct Clock{void reset(){}}menu_clock,audio_clock;
struct Preparation{bool active=false;bool busy(){return active;}}preparation;
uint64_t melee_web_gameplay_generation(){return generation;}
bool melee_web_gameplay_world_exists(){return world_exists;}
bool melee_web_source_memory_healthy(){return healthy;}
bool melee_web_net_active(){return net_active;}
int melee_web_menu_host_mode_kind(MeleeWebMenuHost*){return kind;}
int melee_web_menu_host_phase(MeleeWebMenuHost*){return phase;}
int melee_web_menu_host_source_scene(MeleeWebMenuHost*){return source_scene;}
const void* melee_web_current_scene_info(){++scene_reads;return &scene;}
uint32_t gm_801A4BA8(){++frame_reads;return frame;}
void first_css_browser_draw_refresh_observation(){++refreshes;}
int service_commands(){++commands;if(queued_unload){first_css_browser_draw={};host=nullptr;world.reset();host_entered=false;world_exposed=false;source_session_owned=false;running=false;first_use_draw_pending=false;queued_unload=false;}return 0;}
'''
OWNER_MAIN=r'''
void reset(){first_sss_constructor_pair={};first_css_browser_draw={};host=&host_storage;world=std::make_unique<melee_web::GameplayMenuWorld>();generation=7;phase=0;kind=GM_VS;source_scene=1;frame=0;seed=17;seed_ptr=&seed;scene.scene_kind=8;
 host_entered=false;world_exposed=false;source_session_owned=true;running=false;pending=false;first_use_draw_pending=false;faulted=false;match=false;results=false;prize=false;replay=false;net_active=false;healthy=true;world_exists=true;preparation.active=false;diagnostic_start_ticks=0;diagnostic_pad_remaining=0;constructor_entries=0;audio_phase=0;refreshes=0;scene_reads=0;frame_reads=0;prepare_mutations=0;commands=0;poll_or_audio_or_preparation_calls=0;queued_unload=false;
 auto& s=first_css_browser_draw;s.mode=FirstCssBrowserMode::FreshConsumedTickDrawV3;s.armed=true;s.constructor_arm_ready=true;s.arm_source_session_owned=true;s.armed_host_owner=host;s.armed_world_owner=world.get();s.armed_world_generation=7;}
void enter(){auto& s=first_css_browser_draw;s.constructor_launch_consumed=true;s.constructor_entry_attempted=true;s.entry_captured=true;s.world_generation=7;s.seed_owner=&seed;s.scene_owner=&scene;s.entry.random_seed=seed;host_entered=true;world_exposed=true;phase=1;running=true;first_use_draw_pending=true;}
int main(){
 for(int queued:{1,2}){reset();if(queued==1)diagnostic_start_ticks=1;else diagnostic_pad_remaining=1;
  assert(melee_web_native_menu_launch()==0&&constructor_entries==0&&
      !first_css_browser_draw.constructor_launch_consumed&&scene_reads==0&&frame_reads==0);
  assert(first_css_browser_draw.source_steps==0&&first_css_browser_draw.host_tick_calls==0);
 }
 reset();assert(!stadium_first_css_constructor_return_active());assert(stadium_first_css_prepared_diagnostic_active());
 assert(stadium_first_css_prepared_diagnostic_consume_launch());assert(!stadium_first_css_prepared_diagnostic_consume_launch());
 assert(melee_web_native_menu_prepare()==0&&prepare_mutations==0);
 callback();assert(commands==1&&poll_or_audio_or_preparation_calls==0&&!first_css_browser_draw.failed);
 enter();assert(stadium_first_css_prepared_diagnostic_entry_succeeded());callback();
 assert(commands==2&&poll_or_audio_or_preparation_calls==0&&running);
 assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==1);
 assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==0);
 assert(first_css_browser_draw.failed&&!running);
 for(int mode:{1,2}){reset();first_css_browser_draw.mode=static_cast<FirstCssBrowserMode>(mode);enter();
  assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==0);
  if(mode==2){reset();first_css_browser_draw.mode=FirstCssBrowserMode::ConstructorReturnOnlyV2;enter();assert(melee_web_native_menu_stadium_first_css_draw_kick()==0);}}
 reset();enter();assert(melee_web_native_menu_stadium_first_css_draw_kick()==0);
 for(int bad=0;bad<9;bad++){reset();enter();switch(bad){case 0:host=&other_host;break;case 1:source_session_owned=false;break;case 2:++generation;break;case 3:faulted=true;break;case 4:replay=true;break;case 5:net_active=true;break;case 6:pending=true;break;case 7:preparation.active=true;break;case 8:kind=99;break;}
  assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==0);
  assert(first_css_browser_draw.failed&&scene_reads==0&&frame_reads==0);}
 for(bool after_kick:{false,true})for(int queued:{1,2}){
  reset();enter();if(after_kick)assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==1);
  if(queued==1)diagnostic_start_ticks=1;else diagnostic_pad_remaining=1;
  callback();assert(first_css_browser_draw.failed&&poll_or_audio_or_preparation_calls==0&&
      first_css_browser_draw.host_tick_calls==0&&first_css_browser_draw.source_steps==0&&
      first_css_browser_draw.host_draw_calls==0&&first_css_browser_draw.source_draws==0);
  assert(diagnostic_start_ticks==(queued==1?1u:0u)&&diagnostic_pad_remaining==(queued==2?1u:0u));
 }
 for(int queued:{1,2}){reset();enter();if(queued==1)diagnostic_start_ticks=1;else diagnostic_pad_remaining=1;
  assert(melee_web_native_menu_stadium_first_css_fresh_row_v3_kick()==0);
  assert(first_css_browser_draw.failed&&scene_reads==0&&frame_reads==0&&first_css_browser_draw.host_tick_calls==0);}
 reset();enter();auto& s=first_css_browser_draw;s.kicked=true;s.complete=true;s.tick_captured=true;s.draw_captured=true;s.frame_end_returned=true;s.tick_result=1;s.source_steps=s.source_draws=s.host_tick_calls=s.host_draw_calls=s.aurora_begin_calls=s.aurora_end_calls=1;running=false;first_use_draw_pending=false;
 callback();callback();assert(s.fresh_terminal_callbacks==2&&commands==2&&poll_or_audio_or_preparation_calls==0&&!s.failed);
 s.source_steps=2;callback();assert(s.failed&&poll_or_audio_or_preparation_calls==0);
 callback();assert(poll_or_audio_or_preparation_calls==0);queued_unload=true;callback();assert(!world&&!host&&commands==5&&poll_or_audio_or_preparation_calls==1);
 // Exercise every real selector action; default-unarmed state is not a constant Strict stub.
 reset();enter();s.mode=FirstCssBrowserMode::FreshFiniteCssPrefixV4;s.kicked=s.complete=true;
 auto& pair=first_sss_constructor_pair;pair.profile=FirstCssBrowserMode::FreshFiniteCssPrefixV4;
 pair.armed=pair.kicked=true;
 auto& stream=first_css_browser_postdraw_stream;stream.armed=stream.kicked=stream.terminal=true;
 stream.failed=false;stream.outcome="stop_after_last_input_request";
 auto& final_draw=first_css_final_pending_draw;final_draw.complete=final_draw.compared=final_draw.approved=final_draw.frame_end_returned=true;final_draw.failed=false;
 assert(first_sss_fresh_pair_callback_action()==FirstSssFreshPairCallbackAction::ContinueTransition);
 pair.frame_terminal=pair.complete=true;
 assert(first_sss_fresh_pair_callback_action()==FirstSssFreshPairCallbackAction::StopAfterPair);
 pair.complete=false;
 assert(first_sss_fresh_pair_callback_action()==FirstSssFreshPairCallbackAction::StrictCssTerminal);
 pair.frame_terminal=false;pair.profile=FirstCssBrowserMode::LegacyTickDrawV1;
 assert(first_sss_fresh_pair_callback_action()==FirstSssFreshPairCallbackAction::StrictCssTerminal);
 reset();enter();s.mode=FirstCssBrowserMode::LegacyTickDrawV1;assert(melee_web_native_menu_stadium_first_css_draw_kick()==1);
 std::cout<<"PASS actual private owner/kick and command-first source-free fences; v1/v2 refusal regressions\n";
}
'''

ARM_SERVICES=r'''
bool scoped_assets=true,scoped_disc_import=true,asset_committed=false,stadium_c1a_armed=false;
enum class AssetDestination{None};AssetDestination asset_destination=AssetDestination::None;
struct Scope{unsigned pending_generation(){return 0;}}asset_scope;
struct Armed{bool armed=false;}first_sss_consumed_tick,first_sss_draw_sequence,first_sss_prefix_sequence;
struct Attempt{bool attempted=false;}first_sss_vs_entry,first_sss_vs_setup;
struct Matched{bool opted_in=false,armed=false;}matched_setup_cf;
unsigned context_applications=0,host_enable_calls=0,host_return_arm_calls=0,finite_reservations=0,reserved_ordinal=0,reserved_sequence=0;
bool apply_ok=true;
int melee_web_menu_host_apply_replay_context(MeleeWebMenuHost*,uint32_t,const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*,const uint8_t*,char* error,size_t){
 ++context_applications;if(!apply_ok){error[0]='x';error[1]=0;return 0;}return 1;
}
int melee_web_menu_host_enable_stadium_c1a(MeleeWebMenuHost*,char*,size_t){++host_enable_calls;return 1;}
int melee_web_menu_host_arm_first_css_return(MeleeWebMenuHost*,char*,size_t){++host_return_arm_calls;return 1;}
int melee_web_menu_host_reserve_fresh_finite_css_v4(MeleeWebMenuHost*,unsigned ordinal,unsigned sequence,char*,size_t){++finite_reservations;reserved_ordinal=ordinal;reserved_sequence=sequence;return 1;}
'''
ARM_MAIN=r'''
using namespace ::melee_web::stadium_first_css_diagnostic;
void be(std::vector<uint8_t>& b,size_t o,uint32_t v){for(unsigned i=0;i<4;i++)b[o+i]=uint8_t(v>>(24-i*8));}
void reset_arm(){first_sss_constructor_pair={};first_css_browser_draw={};first_css_browser_postdraw_stream={};first_css_final_pending_draw={};world=std::make_unique<melee_web::GameplayMenuWorld>();host=&host_storage;
 host_entered=false;world_exposed=false;source_session_owned=true;generation=7;phase=MELEE_WEB_MENU_CREATED;kind=GM_VS;
 running=false;pending=false;first_use_draw_pending=false;faulted=false;match=false;results=false;prize=false;replay=false;net_active=false;
 context_applications=0;host_enable_calls=0;host_return_arm_calls=0;finite_reservations=0;reserved_ordinal=0;reserved_sequence=0;diagnostic_start_ticks=0;diagnostic_pad_remaining=0;apply_ok=true;}
int run(){
 std::vector<uint8_t> context(kContextBytes);std::memcpy(context.data(),"STC2CSSR",8);be(context,8,2);std::fill_n(context.data()+12,32,0x11);be(context,44,10);be(context,48,20);be(context,52,17);
 be(context,kContextHeaderBytes+4+kPadBytes+4,kKoAddress);
 std::vector<uint8_t> row(136);std::memcpy(row.data(),"STC3PAD1",8);be(row,8,3);std::fill_n(row.data()+12,32,0x11);
 uint32_t metadata[]={10,20,30,35,40,45,50,0,0,1,1,1};for(unsigned i=0;i<12;i++)be(row,44+i*4,metadata[i]);row[92+32]=row[92+43]=0xff;
 auto arm=[&](const auto& c,const auto& r){return melee_web_native_menu_stadium_first_css_fresh_row_v3_arm(c.data(),c.size(),r.data(),r.size());};
 for(int bad=0;bad<4;bad++){reset_arm();auto c=context;auto r=row;
  if(bad==0)c[0]^=1;if(bad==1)r[8]^=1;if(bad==2)r[92+10]=0xff;if(bad==3)host_entered=true;
  auto* owned_world=world.get();assert(arm(c,r)==0);assert(context_applications==0&&host_enable_calls==0&&host_return_arm_calls==0);
  assert(first_css_browser_draw.mode==FirstCssBrowserMode::None&&!first_css_browser_draw.armed&&!first_css_browser_draw.failed&&world.get()==owned_world&&host==&host_storage);
 }
 for(auto mode:{FirstCssBrowserMode::LegacyTickDrawV1,FirstCssBrowserMode::ConstructorReturnOnlyV2}){reset_arm();first_css_browser_draw.mode=mode;first_css_browser_draw.armed=true;
  assert(arm(context,row)==0&&first_css_browser_draw.mode==mode&&!first_css_browser_draw.failed&&context_applications==0);}
 reset_arm();apply_ok=false;auto* retained=world.get();assert(arm(context,row)==0);
 assert(first_css_browser_draw.mode==FirstCssBrowserMode::FreshConsumedTickDrawV3&&first_css_browser_draw.armed&&first_css_browser_draw.failed&&!first_css_browser_draw.constructor_arm_ready);
 assert(first_css_browser_draw.armed_world_owner==retained&&world.get()==retained&&context_applications==1&&host_enable_calls==0);
 reset_arm();assert(arm(context,row)==1);assert(first_css_browser_draw.mode==FirstCssBrowserMode::FreshConsumedTickDrawV3&&first_css_browser_draw.constructor_arm_ready);
 assert(first_css_browser_draw.armed_world_generation==7&&first_css_browser_draw.arm_source_session_owned);
 assert(first_css_browser_draw.consumed_status[2].err==-1&&first_css_browser_draw.consumed_status[3].err==-1);
 assert(arm(context,row)==0&&context_applications==1&&host_enable_calls==1&&host_return_arm_calls==1);
 std::cout<<"PASS actual fresh arm; codecs precede reservation/mutation; partial failure retains reserved owner; v1/v2 refuse replacement\n";
 return 0;
}
'''

# Synthetic input-only rows exercise the real shared arm and real codecs. Their
# count and source references differ from the retained original; no expected
# game state is supplied to native code by this control.
FINITE_ARM_MAIN=r'''
 std::vector<uint8_t> finite(60+3*84);std::memcpy(finite.data(),"STC4PFX1",8);
 be(finite,8,4);std::fill_n(finite.data()+12,32,0x11);
 be(finite,44,10);be(finite,48,20);be(finite,52,36);be(finite,56,3);
 for(unsigned i=0;i<3;i++){size_t o=60+i*84;
  uint32_t fields[]={21+i*5,22+i*5,23+i*5,24+i*5,25+i*5,i,i,i+1,i+1,i+1};
  for(unsigned j=0;j<10;j++)be(finite,o+j*4,fields[j]);
  finite[o+40+32]=finite[o+40+43]=0xff;
 }
 auto arm4=[&](const auto& c,const auto& r){return melee_web_native_menu_stadium_first_css_fresh_finite_v4_arm(c.data(),c.size(),r.data(),r.size());};
 for(int bad=0;bad<12;bad++){reset_arm();auto c=context;auto r=finite;
  switch(bad){case 0:c[0]^=1;break;case 1:r.pop_back();break;case 2:r.push_back(0);break;
   case 3:be(r,56,0xffffffff);break;case 4:r[12]^=1;break;case 5:be(r,60+84,99);break;
   case 6:be(r,60+84+20,99);break;case 7:r[60+84+50]=0xff;break;
   case 8:diagnostic_start_ticks=1;break;case 9:diagnostic_pad_remaining=1;break;
   case 10:source_session_owned=false;break;case 11:host_entered=true;break;}
  auto* before=world.get();assert(arm4(c,r)==0);
  assert(first_css_browser_draw.mode==FirstCssBrowserMode::None&&!first_css_browser_draw.armed&&!first_css_browser_draw.failed);
  assert(context_applications==0&&host_enable_calls==0&&host_return_arm_calls==0&&finite_reservations==0&&world.get()==before);
 }
 for(auto mode:{FirstCssBrowserMode::LegacyTickDrawV1,FirstCssBrowserMode::ConstructorReturnOnlyV2,FirstCssBrowserMode::FreshConsumedTickDrawV3,FirstCssBrowserMode::FreshFiniteCssPrefixV4}){
  reset_arm();first_css_browser_draw.mode=mode;first_css_browser_draw.armed=true;
  assert(arm4(context,finite)==0&&first_css_browser_draw.mode==mode&&!first_css_browser_draw.failed&&context_applications==0&&finite_reservations==0);
 }
 reset_arm();apply_ok=false;assert(arm4(context,finite)==0);
 assert(first_css_browser_draw.mode==FirstCssBrowserMode::FreshFiniteCssPrefixV4&&first_css_browser_draw.failed&&!first_css_browser_draw.constructor_arm_ready&&context_applications==1&&finite_reservations==0);
 reset_arm();assert(arm4(context,finite)==1);
 assert(first_css_browser_draw.mode==FirstCssBrowserMode::FreshFiniteCssPrefixV4&&first_css_browser_draw.constructor_arm_ready&&first_css_browser_draw.finite_css.rows.size()==3);
 assert(finite_reservations==1&&reserved_ordinal==2&&reserved_sequence==31);
 assert(first_css_browser_draw.consumed_status[2].err==-1&&first_css_browser_draw.consumed_status[3].err==-1);
 assert(arm4(context,finite)==0&&arm(context,row)==0&&!first_css_browser_draw.failed&&context_applications==1&&finite_reservations==1);
 std::cout<<"PASS actual V4 arm: 12 pre-mutation refusals, foreign/reused owners, partial reservation retained, dynamic terminal reservation\n";
'''
