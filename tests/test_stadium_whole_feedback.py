"""Actual-source controls; projected retained scope metadata is synthetic, not whole3 evidence."""
from pathlib import Path
import copy, hashlib, importlib.util, json, os, subprocess, sys, unittest
from unittest.mock import patch
from owned_test_workspace import OwnedWorkspaceTests
REPO_ROOT=Path(os.environ.get('MELEE_STADIUM_FEEDBACK_REPO_ROOT',Path(__file__).resolve().parents[1]))
SOURCE_ROOT=Path(os.environ.get('MELEE_STADIUM_FEEDBACK_SOURCE_ROOT',REPO_ROOT))
sys.path[:0]=[str(SOURCE_ROOT/'scripts'),str(SOURCE_ROOT/'tools'),str(REPO_ROOT/'scripts'),str(REPO_ROOT/'tools'),str(REPO_ROOT/'reference-capture/dolphin')]
from reference_observer_stream import iter_records
TRACE=Path(os.environ['MELEE_STADIUM_RETAINED_SSS_TRACE']) if os.environ.get('MELEE_STADIUM_RETAINED_SSS_TRACE') else None
def load_exact(name,path):
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);sys.modules[name]=module
    spec.loader.exec_module(module)
    assert Path(module.__file__).resolve()==path.resolve()
    assert hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()==digest
    return module
def whole_b_declaration(source):
    start='struct StadiumWholeBObservation\n'
    end='enum class StadiumGoPrefixPhase : u8'
    assert source.count(start)==source.count(end)==1
    return source[source.index(start):source.index(end)]

class StadiumWholeFeedbackTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        global decoder, driver
        cls.workspace=cls.new_workspace(REPO_ROOT,'stadium-whole-feedback-')
        prior={name:sys.modules.get(name) for name in ('stadium_go_prefix','capture_sd_reference_prefix')}
        def restore():
            for name,value in prior.items():
                if value is None:sys.modules.pop(name,None)
                else:sys.modules[name]=value
        cls.addClassCleanup(restore)
        decoder=load_exact('stadium_go_prefix',SOURCE_ROOT/'tools/stadium_go_prefix.py')
        driver=load_exact('capture_sd_reference_prefix',SOURCE_ROOT/'scripts/capture_sd_reference_prefix.py')

    def test_actual_strict_admitted_gci_copy_and_profile_separation(self):
        packet_setting=os.environ.get('MELEE_STADIUM_WHOLE_CAPTURE_PACKET')
        if not packet_setting:self.skipTest('Retained strict-admitted whole Stadium packet not configured')
        packet=Path(packet_setting)
        contract=json.loads((packet/'capture-plan.json').read_text())
        admission_path=packet/'strict-loader-admission.json'
        self.assertEqual(hashlib.sha256(admission_path.read_bytes()).hexdigest(),contract['strict_admission_sha256'])
        setup=json.loads(admission_path.read_text())['setup']
        def argument(flag):return Path(contract['argv'][contract['argv'].index(flag)+1])
        self.assertEqual(hashlib.sha256(argument('--stadium-whole-setup').read_bytes()).hexdigest(),setup['sha256'])
        source=argument('--gci');card=argument('--stadium-whole-gc')
        original=source.read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(),setup['initial_profile_sha256'])
        # This real original file is admitted by strict setup, not the separate fixed personal profile.
        from sd_gci_profile import load_profile
        with self.assertRaisesRegex(ValueError,'Selected original profile GCI identity differs'):load_profile(source)
        target=driver.prepare_stadium_setup_gci_folder(card,source,self.workspace/'admitted-card',setup)
        self.assertEqual(target.name,source.name);self.assertEqual(target.read_bytes(),original)
        self.assertEqual(target.stat().st_mode&0o777,0o400);self.assertEqual(source.read_bytes(),original)
        with self.assertRaises(FileExistsError):driver.prepare_stadium_setup_gci_folder(card,source,self.workspace/'admitted-card',setup)
        wrong=copy.deepcopy(setup);wrong['initial_profile_sha256']='0'*64
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(card,source,self.workspace/'wrong-admission',wrong)
        mutated=self.workspace/source.name;mutated.write_bytes(original[:-1]+bytes([original[-1]^1]))
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(card,mutated,self.workspace/'wrong-bytes',setup)
        link_parent=self.workspace/'linked-source';link_parent.mkdir()
        linked=link_parent/source.name;linked.symlink_to(source)
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(card,linked,self.workspace/'linked-card',setup)
        copied_card=self.workspace/'changed-checkpoint'
        driver._copy_tree(card/'USA/Card A',copied_card/'USA/Card A')
        (copied_card/'USA/Card A/extra.gci').write_bytes(b'not an admitted card row')
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(copied_card,copied_card/'USA/Card A'/source.name,self.workspace/'extra-card',setup)
        self.assertEqual(source.read_bytes(),original)
        # Use owned copies of actual retained inputs so a regression cannot mutate originals.
        safe_card=self.workspace/'safe-checkpoint';safe_profile=self.workspace/'safe-profile'
        driver._copy_tree(card/'USA/Card A',safe_card/'USA/Card A')
        driver._copy_tree(argument('--profile'),safe_profile)
        def inventory(tree):
            return {x.relative_to(tree).as_posix():('directory' if x.is_dir() else hashlib.sha256(x.read_bytes()).hexdigest())
                    for x in tree.rglob('*')}
        before_card=inventory(safe_card);before_profile=inventory(safe_profile)
        run_args=dict(dolphin=argument('--dolphin'),disc=argument('--disc'),profile=safe_profile,
            menu_recipe=argument('--menu-recipe'),build_manifest=argument('--build-manifest'),
            target_path=argument('--stadium-whole-target'),setup_path=argument('--stadium-whole-setup'),
            checkpoint_gc=safe_card,dol=argument('--stadium-whole-dol'),
            provenance=argument('--stadium-whole-provenance'),gci=safe_card/'USA/Card A'/source.name,timeout=180)
        alias=self.workspace/'card-alias';alias.symlink_to(safe_card,target_is_directory=True)
        for output in [safe_card,safe_card/'not-created',safe_profile/'not-created',alias/'not-created']:
            existed=output.exists()
            with self.assertRaisesRegex(ValueError,'output overlaps a source tree'):
                driver.run_stadium_whole_three(output=output,**run_args)
            self.assertEqual(output.exists(),existed)
            self.assertEqual(inventory(safe_card),before_card);self.assertEqual(inventory(safe_profile),before_profile)
        nested=safe_card/'USA/Card A/not-created'
        with self.assertRaisesRegex(ValueError,'output overlaps a source tree'):
            driver.prepare_stadium_setup_gci_folder(safe_card,safe_card/'USA/Card A'/source.name,nested,setup)
        self.assertFalse(nested.exists());self.assertEqual(inventory(safe_card),before_card)
        self.assertEqual(source.read_bytes(),original)

    def test_retained_prefix_and_scoped_whole_sss_operands(self):
        if TRACE is None:self.skipTest('Retained original SSS trace was not configured')
        rows=[r for r in iter_records(TRACE) if r['event']=='progress' and r['payload'].get('phase') in decoder.SSS_POSITION_PHASES]
        self.assertGreater(len(rows),3)
        old=decoder.StadiumSssPositionObservations()
        for r in rows:old.accept(r,route_ready=True)
        old.require_closed()
        # Controlled metadata projection only; operands remain actual retained original bytes.
        projected=copy.deepcopy(rows)
        for r in projected:r['payload'].update(diagnostic='stadium_whole3_feedback_v1',whole_session=True,match_index=0)
        receipt=rows[0]['payload']['setup_receipt_sha256']
        fresh=decoder.StadiumSssPositionObservations()
        for r in projected:fresh.accept(r,route_ready=True,expected_receipt=receipt,scope='stadium_whole3_feedback_v1',match_index=0)
        fresh.require_closed()
        for changes,kwargs in [({'match_index':1},{}),({'match_index':True},{}),({'whole_session':False},{}),({'diagnostic':decoder.DIAGNOSTIC},{}),({},dict(match_index=3)),({},dict(scope='unknown')),({},dict(expected_receipt='0'*64))]:
            r=copy.deepcopy(projected[0]);r['payload'].update(changes)
            options=dict(route_ready=True,expected_receipt=receipt,scope='stadium_whole3_feedback_v1',match_index=0);options.update(kwargs)
            with self.assertRaises(ValueError):decoder.StadiumSssPositionObservations().accept(r,**options)
        with self.assertRaises(ValueError):decoder.StadiumSssPositionObservations().accept(projected[0],route_ready=True)
    def test_native_admission_actual_slice(self):
        source=(SOURCE_ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        start=source.index('    const std::string whole_feedback_setting =')
        stop=source.index('    const std::string constructor_setting =',start)
        body=source[start:stop]
        fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
using u32=uint32_t;using u8=uint8_t;
'''+whole_b_declaration(source)+r'''
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
bool SdInitRequested(){return !Env("MWRC_SD_INIT").empty();}
bool AllocationConfigured(){return !Env("MWRC_ALLOCATION_OUTPUT").empty();}
namespace InputStream { bool recording=true,replaying=false; bool IsRecording(){return recording;} bool IsReplaying(){return replaying;} }
struct Gate { StadiumWholeBObservation stadium_whole_b;unsigned whole_session_matches=3;bool stadium_go_prefix_enabled=false,stadium_whole_feedback=false;std::string stadium_whole_setup_receipt,error;void SetInvalid(const char* e){error=e;}
bool Run(){
''' + body + r'''
return true;
}};
void reset(){env.clear();InputStream::recording=true;InputStream::replaying=false;env["MWRC_STADIUM_WHOLE_FEEDBACK"]="1";env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]=std::string(64,'a');env["MWRC_ALLOCATION_OUTPUT"]="owned";env["MWRC_ALLOCATION_VS_TARGET"]="2";env["MWRC_ALLOCATION_STOP_AT"]="exit";}
int main(){reset();assert(Gate().Run());
reset();Gate off;assert(off.Run()&&!off.stadium_whole_b.enabled);
reset();env["MWRC_STADIUM_WHOLE_B_STATE"]="1";Gate on;assert(on.Run()&&on.stadium_whole_b.enabled);
reset();env["MWRC_STADIUM_WHOLE_B_STATE"]="2";assert(!Gate().Run());
reset();env["MWRC_STADIUM_WHOLE_B_STATE"]="1";env["MWRC_STADIUM_WHOLE_FEEDBACK"]="";env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]="";assert(!Gate().Run());
for(const char* key:{"MWRC_STADIUM_CONSTRUCTOR_OWNER","MWRC_STADIUM_IDLE_3500","MWRC_TRANSFORM_PREFIX","MWRC_ENTITY_PROFILE","MWRC_SD_MENU_PROBE","MWRC_ORDINARY_POLICY_SHA256","MWRC_CPU_PROBE_OUTPUT","MWRC_ITEM_PROBE_OUTPUT","MWRC_SD_INIT"}){reset();env[key]="1";assert(!Gate().Run());}
reset();env["MWRC_ALLOCATION_VS_TARGET"]="1";assert(!Gate().Run());
reset();env["MWRC_ALLOCATION_OUTPUT"]="";assert(!Gate().Run());
reset();env["MWRC_ALLOCATION_STOP_AT"]="entry";assert(!Gate().Run());
reset();env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]=std::string(64,'0');assert(!Gate().Run());
reset();InputStream::replaying=true;assert(!Gate().Run());
reset();Gate g;g.whole_session_matches=2;assert(!g.Run());
reset();g=Gate();g.stadium_go_prefix_enabled=true;assert(!g.Run());
reset();env["MWRC_STADIUM_WHOLE_FEEDBACK"]="";env["MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256"]="";g=Gate();assert(g.Run()&&!g.stadium_whole_feedback);
}
'''
        tmp=self.workspace;cpp=tmp/'actual-whole-feedback-admission.cpp';binary=tmp/'actual-whole-feedback-admission'
        cpp.write_text(fixture)
        subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True,timeout=60)
        subprocess.run([str(binary)],check=True,timeout=15)
    def test_driver_whole_identity_before_callbacks(self):
        # Real menu policy retained; no synthetic source menu samples.
        from sd_original_menu_plan import stadium_go_feedback_packet
        menus=stadium_go_feedback_packet()
        receipt='a'*64
        target={}  # Identity refusals do not enter Setup decoding.
        r=driver.StadiumWholeFeedbackReceiver(menus,receipt,target)
        with self.assertRaises(ValueError):r.next_lifetime()
        row=dict(seq=0,event='handshake',payload=dict(whole_session=True,match_count=3,stadium_whole_feedback=True,setup_receipt_sha256=receipt))
        r.accept(row)
        r.accept(dict(seq=1,event='start',payload=row['payload']))
        with self.assertRaises(ValueError):r.accept(dict(seq=2,event='progress',payload=dict(whole_session=True,match_index=1,diagnostic='stadium_whole3_feedback_v1',setup_receipt_sha256=receipt)))
    def test_actual_publisher_default_off_bytes_and_scoped_identity(self):
        source=(SOURCE_ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        start=source.index('  void PublishStadiumProgress(')
        stop=source.index('  // Pinned fn_8025A310',start)
        current=source[start:stop]
        helper=source[source.index('bool AppendBounded('):source.index('\n}  // namespace\n\nstruct Observer::Impl')]
        import re
        constants='\n'.join(re.findall(r'^constexpr size_t (?:RING_PAYLOAD|CPU_PROBE_MAX_JSON_BYTES) = .*?;',source,re.M))
        self.assertEqual(len(constants.splitlines()),2)
        fixture='''#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;
namespace PowerPC {struct PowerPCState {std::array<u32,32> gpr{};std::array<u32,1024> spr{};};}
enum class Event:u16 {Progress=4};enum class SliceTag:u16 {Sample=1};
struct SliceRef {SliceTag tag;u16 flags;u32 address,size,offset;};
'''+constants+'\n'+whole_b_declaration(source)+helper
        fields='''StadiumWholeBObservation stadium_whole_b;
bool stadium_whole_feedback=false;
std::string stadium_whole_setup_receipt=std::string(64,'b');
std::string stadium_go_prefix_setup_receipt_sha256=std::string(64,'a');
u32 match_index=2,draw_ordinal=7;size_t slice_count=1;
std::array<u8,4> raw{{1,2,3,4}};
std::array<SliceRef,1> slices{{{SliceTag::Sample,0,0x80010000,4,0}}};
struct Position {bool cursor_seen=true,target_seen=true;u32 id=1,gobj=0x80001000,jobj=0x80002000,proc=0x80003000,frame_sp=0x80004000,entry_cursor_state=0;} stadium_sss_position;
std::string emitted;
void SetInvalid(const char* error){throw std::runtime_error(error);}
void PushJson(Event,const std::string& value,u32,u32,u32){emitted=value;}
'''
        fixture+='struct Before {'+fields+LEGACY_PUBLISHER+'};\nstruct After {'+fields+current+'};\n'
        fixture+='''int main(){PowerPC::PowerPCState state;state.gpr[30]=18;state.spr[8]=0x8025a4e8;
for(auto phase:{"css_entry","css_return","sss_return","sss_position_cursor","sss_position_target","sss_position_end","constructor_commit","go_after"}){
for(size_t count:{0,1}){Before a;After b;a.slice_count=b.slice_count=count;
a.PublishStadiumProgress(phase,0x8025a548,0x8001003c,0x80001000,&state,40);
b.PublishStadiumProgress(phase,0x8025a548,0x8001003c,0x80001000,&state,40);assert(a.emitted==b.emitted);}}
After scoped;scoped.stadium_whole_feedback=true;scoped.PublishStadiumProgress("sss_return",1,2,3,&state,4);
assert(scoped.emitted.find("stadium_whole3_feedback_v1")!=std::string::npos);
assert(scoped.emitted.find("\\\"whole_session\\\":true,\\\"match_index\\\":2")!=std::string::npos);
scoped.stadium_whole_b.enabled=true;scoped.match_index=1;
scoped.PublishStadiumProgress("constructor_commit",1,2,3,&state,4);
assert(scoped.emitted.find("\\\"stadium_b_state_observation\\\":true")!=std::string::npos);
scoped.match_index=0;scoped.PublishStadiumProgress("sss_return",1,2,3,&state,4);
assert(scoped.emitted.find("stadium_b_state_observation")==std::string::npos);
}
'''
        cpp=self.workspace/'actual-publisher-off.cpp';binary=self.workspace/'actual-publisher-off'
        cpp.write_text(fixture)
        subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True,timeout=60)
        subprocess.run([str(binary)],check=True,timeout=15)

    def test_actual_cold_scene1_route_and_consumed_input_refusals(self):
        # Synthetic endpoint transcript only. Run the actual whole driver,
        # including its wait_source/tap and deadline wrapper; no native service.
        from sd_original_menu_plan import stadium_go_feedback_packet
        from retail_input_plan import DISCONNECTED_PAD
        class ReachedCss(Exception):
            pass
        def simulate(mode):
            menus=stadium_go_feedback_packet()
            class Receiver:
                css=None;latest_menu=None;seq=0;menu_polls=0;menu_consumed=0;ended=False
                last_pad=[driver.NEUTRAL_PAD]*2
                def accept(self,row):
                    self.seq+=1
                    if row['kind']=='consume':
                        self.last_pad=row['ports'][:2]
                        if not row.get('stale'):self.menu_consumed+=1
                    else:
                        self.menu_polls+=1;self.latest_menu=row['menu']
                        if row.get('css'):self.css=True
            receiver=Receiver();writes=[];queue=[]
            initial=dict(menus['actions'][0]['before'])
            # Actual menu transition is not ready until source cooldown clears.
            queue.extend([dict(kind='poll',menu=dict(initial,cooldown=20)),
                          dict(kind='poll',menu=initial)])
            class Controller:
                sleep=staticmethod(lambda seconds:None)
                def set_both(self,p1,p2,*,action):
                    writes.append(dict(action=action,p1=p1,p2=p2,
                                       observed=copy.deepcopy(receiver.latest_menu)))
                    queue.append(dict(kind='consume',ports=[p1,p2,DISCONNECTED_PAD,DISCONNECTED_PAD],
                                      stale=mode=='stale-consume'))
                    if not action.endswith(':release'):return
                    label=action[:-len(':release')]
                    if label=='main-down-to-VS':
                        state=dict(menus['actions'][0]['after'])
                        if mode=='wrong-kind':state['kind']=13
                        queue.append(dict(kind='poll',menu=state))
                    elif label=='main-select-VS':
                        state=dict(menus['actions'][1]['after'])
                        if mode=='wrong-row':state['row']=3
                        queue.append(dict(kind='poll',menu=state))
                    elif label=='whole-VS-Melee-to-CSS' and mode!='missing-CSS':
                        queue.append(dict(kind='poll',menu=dict(scene=8),css=True))
            class Tail:
                def next(self,deadline):
                    if queue:return queue.pop(0)
                    return dict(kind='poll',menu=copy.deepcopy(receiver.latest_menu))
            def reached(*args):raise ReachedCss()
            with patch.object(driver.time,'monotonic',return_value=0.0),patch.object(driver,'drive_authored_css_sss',reached):
                try:
                    driver.drive_stadium_whole_three(receiver,Tail(),Controller(),menus,10.0)
                except (ReachedCss,ValueError) as error:
                    return error,writes,receiver
            self.fail('Cold route unexpectedly bypassed CSS endpoint control')
        error,writes,receiver=simulate('positive')
        self.assertIsInstance(error,ReachedCss)
        self.assertEqual([r['action'] for r in writes],[
            'main-down-to-VS','main-down-to-VS:release',
            'main-select-VS','main-select-VS:release',
            'whole-VS-Melee-to-CSS','whole-VS-Melee-to-CSS:release'])
        self.assertEqual(receiver.menu_consumed,6)
        self.assertEqual(receiver.last_pad,[driver.NEUTRAL_PAD]*2)
        self.assertTrue(receiver.css)
        for row in writes:
            self.assertEqual(row['observed']['cooldown'],0)
            self.assertEqual(row['p2'],driver.NEUTRAL_PAD)
        self.assertEqual(writes[0]['p1'],stadium_go_feedback_packet()['actions'][0]['p1'])
        self.assertEqual(writes[4]['observed'],stadium_go_feedback_packet()['actions'][1]['after'])
        # Never enter Rules/Items or select a different row to bypass readiness.
        for mode,maximum_writes in [('wrong-kind',2),('wrong-row',4),('stale-consume',1),('missing-CSS',6)]:
            with self.subTest(mode=mode):
                error,writes,receiver=simulate(mode)
                self.assertIsInstance(error,ValueError)
                self.assertLessEqual(len(writes),maximum_writes)
                self.assertIsNone(receiver.css)
                self.assertLessEqual(receiver.menu_polls,605)
                self.assertTrue(all(r['action'] in {
                    'main-down-to-VS','main-down-to-VS:release',
                    'main-select-VS','main-select-VS:release',
                    'whole-VS-Melee-to-CSS','whole-VS-Melee-to-CSS:release'} for r in writes))
        # Direct helper control: a continuously nonzero transition cooldown
        # must exhaust the inherited boot poll budget without any input.
        from sd_original_menu_plan import stadium_go_feedback_packet
        menus=stadium_go_feedback_packet();writes=[]
        class Waiting:
            css=None;latest_menu=dict(scene=1,kind=0,row=0,entering=0,cooldown=1)
            menu_polls=0
        receiver=Waiting()
        def next_row():receiver.menu_polls+=1
        driver_wait=lambda *args:self.fail('Unexpected wait or input during cooldown')
        with self.assertRaisesRegex(ValueError,'cold startup polling cap'):
            driver.drive_stadium_cold_css(receiver,menus,next_row,driver_wait,driver_wait)
        self.assertEqual(receiver.menu_polls,menus['boot_max_polls'])

    def test_retained_pause_ready_and_fresh_exit_input_contract(self):
        trace=os.environ.get('MELEE_STADIUM_PAUSE_TRACE')
        if not trace:self.skipTest('Retained first-pause source trace not configured')
        path=Path(trace)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),os.environ['MELEE_STADIUM_PAUSE_TRACE_SHA256'])
        from sd_original_menu_plan import stadium_go_feedback_packet
        from retail_setup_validation import _decode_setup
        rows={};header=None;expected=None
        for row in iter_records(path):
            if row['seq']==0:header=row
            if row['event']=='boundary' and row['payload']['boundary']=='setup':
                expected=_decode_setup(next(x['hex'] for x in row['payload']['slices'] if x['tag']==4))
            if row['seq'] in (2525,2526,2530,2576):rows[row['seq']]=row
            if row['seq']==2576:break
        # Actual full prefix/operands, not synthetic pause samples. Expected
        # Setup is retained bytes here; this does not admit a new declaration.
        receiver=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),header['payload']['setup_receipt_sha256'],expected)
        receiver.recorded_input=True
        positive=None
        for row in iter_records(path):
            if row['seq']==2525:
                self.assertEqual(receiver.tick_count,180)
                self.assertIsNone(receiver.pending_neutral_sample)
                receiver.chord_requested=True
            receiver.accept(row)
            if row['seq']==2526:positive=copy.copy(receiver)
            if row['seq']==2576:break
        self.assertEqual(receiver.pause_consumed['sequence'],2525)
        self.assertEqual(receiver.pause_released['sequence'],2530)
        self.assertEqual(receiver.pause_positive_clock['pause_timer'],10)
        self.assertEqual(receiver.pause_ready_clock['sequence'],2576)
        self.assertEqual(receiver.pause_ready_clock['pause_timer'],0)
        self.assertEqual(receiver.pause_ready_clock['frame_count'],56)
        self.assertIn('indirect',receiver.pause_ready_clock['evidence'])
        self.assertIsNone(receiver.chord_consumed)
        def clock_mutation(offset,value):
            row=copy.deepcopy(rows[2576]);item=next(x for x in row['payload']['slices'] if x['tag']==14)
            raw=bytearray.fromhex(item['hex']);raw[offset]=value;item['hex']=raw.hex();return row
        for offset,value in [(1,1),(2,11),(3,1),(5,0),(8,7),(0x27,57)]:
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):copy.copy(positive).observe_pause_clock(clock_mutation(offset,value))
        for field in ('address','size'):
            row=copy.deepcopy(rows[2576]);item=next(x for x in row['payload']['slices'] if x['tag']==14)
            if field=='address':item[field]+=4
            else:item['size']=1;item['hex']='00'
            with self.assertRaises(ValueError):copy.copy(positive).observe_pause_clock(row)
        stale=copy.deepcopy(rows[2576]);stale['seq']=2526
        with self.assertRaises(ValueError):copy.copy(positive).observe_pause_clock(stale)
        foreign=copy.deepcopy(rows[2576]);foreign['payload']['match_index']=1
        with self.assertRaises(ValueError):copy.copy(positive).observe_pause_clock(foreign)
        missing=copy.copy(positive);missing.pause_released=None;missing.observe_pause_clock(rows[2576])
        self.assertIsNone(missing.pause_ready_clock)
        wrong=copy.copy(positive);wrong.pause_released=dict(receiver.pause_released,match_index=1)
        with self.assertRaises(ValueError):wrong.observe_pause_clock(rows[2576])
        # Synthetic continuation only: actual consumed operands copied into
        # distinct later sequences to falsify the fresh-ready/exit contract.
        def fresh_consume(original,seq):
            row=copy.deepcopy(original);row['seq']=seq;row['source_tick']=191;return row
        def clone():
            r=copy.copy(receiver);r.boundaries=[];r.consumed_samples=[];r.exit_requested=True;r.seq=2577
            return r
        premature=copy.copy(positive);premature.exit_requested=True;premature.seq=2577;premature.boundaries=[];premature.consumed_samples=[]
        with self.assertRaises(ValueError):premature.accept(fresh_consume(rows[2525],2577))
        def outcome(seq,value):
            return dict(seq=seq,event='boundary',source_tick=191,draw_ordinal=191,
                payload=dict(whole_session=True,match_index=0,boundary='vs_exit_return',
                             slices=[dict(name='result',tag=15,flags=0,address=0x80479da4,
                                          size=0x28,hex=(bytes(4)+bytes([value])+bytes(0x23)).hex())]))
        missing=clone()
        with self.assertRaises(ValueError):missing.accept(outcome(2577,7))
        ready=clone();ready.accept(fresh_consume(rows[2525],2577))
        self.assertEqual(ready.chord_consumed['sequence'],2577)
        wrong=copy.copy(ready);wrong.boundaries=[]
        with self.assertRaises(ValueError):wrong.accept(outcome(2578,0))
        ready.accept(outcome(2578,7))
        self.assertTrue(ready.outcome_seen);self.assertIsNone(ready.chord_released)
        ready.accept(fresh_consume(rows[2530],2579))
        self.assertEqual(ready.chord_released['sequence'],2579)
        self.assertGreater(ready.chord_consumed['sequence'],receiver.pause_ready_clock['sequence'])

    def test_retained_completed_MatchEnd_header_and_outcome_refusals(self):
        trace=os.environ.get('MELEE_STADIUM_EXIT_TRACE')
        if not trace:self.skipTest('Retained two-phase source exit trace not configured')
        path=Path(trace)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),os.environ['MELEE_STADIUM_EXIT_TRACE_SHA256'])
        from sd_original_menu_plan import stadium_go_feedback_packet
        from retail_setup_validation import _decode_setup
        rows=[]
        for row in iter_records(path):
            rows.append(row)
            if row['payload'].get('boundary')=='vs_exit_return':break
        expected=_decode_setup(next(x['hex'] for r in rows if r['payload'].get('boundary')=='setup' for x in r['payload']['slices'] if x['tag']==4))
        receiver=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),rows[0]['payload']['setup_receipt_sha256'],expected)
        receiver.recorded_input=True;before=None
        from reference_versus_sequence_capture import raw_pad
        chord=raw_pad(buttons=['L','R','A','START'])
        for row in rows:
            if row['payload'].get('boundary')=='pad_consume' and receiver.setup_seen:
                ports,_,_=driver._transform_consumed_ports(row['payload'],row['seq'])
                if ports[:2]==[chord,driver.NEUTRAL_PAD]:
                    if receiver.pause_consumed is None:
                        self.assertGreaterEqual(receiver.tick_count,180);receiver.chord_requested=True
                    else:
                        self.assertIsNotNone(receiver.pause_ready_clock);receiver.exit_requested=True
            if row is rows[-1]:before=copy.copy(receiver)
            receiver.accept(row)
        self.assertTrue(receiver.outcome_seen)
        self.assertEqual(receiver.pause_consumed['sequence'],2489)
        self.assertEqual(receiver.pause_released['sequence'],2494)
        self.assertEqual(receiver.pause_ready_clock['sequence'],2540)
        self.assertEqual(receiver.chord_consumed['sequence'],2549)
        self.assertIsNone(receiver.chord_released) # Not retained before this exit.
        item=next(x for x in rows[-1]['payload']['slices'] if x['name']=='result')
        self.assertEqual((item['address'],item['size']),(0x80479da4,0x28))
        self.assertEqual((bytes.fromhex(item['hex'])[0],bytes.fromhex(item['hex'])[4]),(0,7))
        def refuse(row):
            test=copy.copy(before);test.boundaries=[]
            with self.assertRaises(ValueError):test.accept(row)
            self.assertFalse(test.outcome_seen)
        for field,value in [('tag',14),('flags',1),('address',0x80479da0),('size',0x227c),('hex','07')]:
            row=copy.deepcopy(rows[-1]);next(x for x in row['payload']['slices'] if x['name']=='result')[field]=value
            refuse(row)
        for outcome in (0,1,6,8):
            row=copy.deepcopy(rows[-1]);item=next(x for x in row['payload']['slices'] if x['name']=='result')
            data=bytearray.fromhex(item['hex']);data[0]=7;data[4]=outcome;item['hex']=data.hex();refuse(row)
        for duplicate in (False,True):
            row=copy.deepcopy(rows[-1]);items=row['payload']['slices']
            if duplicate:items.append(copy.deepcopy(next(x for x in items if x['name']=='result')))
            else:row['payload']['slices']=[x for x in items if x['name']!='result']
            refuse(row)

    def test_retained_SSS_global_call_continuity_after_natural_CSS(self):
        trace=os.environ.get('MELEE_STADIUM_SSS_CONTINUITY_TRACE')
        if not trace:self.skipTest('Retained second-SSS source trace not configured')
        path=Path(trace)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),os.environ['MELEE_STADIUM_SSS_CONTINUITY_TRACE_SHA256'])
        from sd_original_menu_plan import stadium_go_feedback_packet
        from stadium_go_prefix import StadiumSssPositionObservations
        from retail_setup_validation import _decode_setup
        from reference_versus_sequence_capture import raw_pad
        rows=list(iter_records(path));setup=next(r for r in rows if r['payload'].get('boundary')=='setup')
        expected=_decode_setup(next(x['hex'] for x in setup['payload']['slices'] if x['tag']==4))
        receipt=rows[0]['payload']['setup_receipt_sha256']
        receiver=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),receipt,expected)
        receiver.recorded_input=True;chord=raw_pad(buttons=['L','R','A','START']);prior=None;handoff=None
        first_positions=[];second_positions=[]
        for row in rows:
            p=row['payload'];name=p.get('boundary')
            if row['event']=='end':break # Raw run is incomplete, not a positive terminal.
            if name=='pad_consume' and receiver.setup_seen:
                ports,_,_=driver._transform_consumed_ports(p,row['seq'])
                if ports[:2]==[chord,driver.NEUTRAL_PAD]:
                    if receiver.pause_consumed is None:
                        self.assertGreaterEqual(receiver.tick_count,180);receiver.chord_requested=True
                    else:self.assertIsNotNone(receiver.pause_ready_clock);receiver.exit_requested=True
                if ports[0]==raw_pad(buttons=['START']):
                    self.assertIsNotNone(receiver.chord_released);receiver.start_allowed=True
            receiver.accept(row)
            if row['event']=='progress' and p.get('phase','').startswith('sss_position_'):
                (first_positions if p['match_index']==0 else second_positions).append(row)
            if name=='return_css':
                self.assertTrue(receiver.outcome_seen);self.assertIsNotNone(receiver.chord_released)
                self.assertEqual(row['seq'],3461);prior=receiver.sss_positions
                self.assertEqual(prior.last_call,131);prior.require_closed()
                natural_return_context=copy.copy(receiver)
                receiver.next_lifetime();receiver.recorded_input=True;handoff=receiver.sss_positions
                self.assertIsNot(prior,handoff);self.assertEqual(handoff.last_call,131)
                self.assertIsNone(handoff.active);self.assertIsNone(receiver.sss_geometry_token)
                self.assertIsNone(receiver.sss_live_owner_sequence)
        self.assertEqual(receiver.match_index,1);self.assertFalse(receiver.setup_seen)
        receiver.sss_positions.require_closed();self.assertEqual(receiver.sss_positions.last_call,147)
        self.assertEqual(second_positions[0]['seq'],3855)
        self.assertEqual(second_positions[0]['payload']['position_call'],132)
        # Component interpositions below use real retained operands; no game
        # or source fields are changed, and no new capture is implied.
        def fresh():
            value=StadiumSssPositionObservations();value.last_call=131;return value
        def accept(value,row,index=1):
            return value.accept(row,route_ready=True,expected_receipt=receipt,scope='stadium_whole3_feedback_v1',match_index=index)
        for bad_call in (131,133):
            row=copy.deepcopy(second_positions[0]);row['payload']['position_call']=bad_call
            with self.assertRaises(ValueError):accept(fresh(),row)
        row=copy.deepcopy(second_positions[0]);row['payload']['match_index']=0
        with self.assertRaises(ValueError):accept(fresh(),row)
        with self.assertRaises(ValueError):accept(fresh(),second_positions[1]) # target before cursor
        partial=fresh();accept(partial,second_positions[0])
        with self.assertRaises(ValueError):partial.require_closed()
        # Actual checked ReturnCss context with synthetic invalid interpositions.
        active_reset=copy.copy(natural_return_context);active_reset.sss_positions=partial
        with self.assertRaises(ValueError):active_reset.next_lifetime()
        early_reset=copy.copy(natural_return_context);early_reset.returned_css=False
        with self.assertRaises(ValueError):early_reset.next_lifetime()
        # Historical fresh construction still starts at0 and admits call1.
        legacy=StadiumSssPositionObservations();self.assertEqual(legacy.last_call,0)
        for row in first_positions[:3]:accept(legacy,row,index=0)
        legacy.require_closed();self.assertEqual(legacy.last_call,1)

    def test_retained_Results_mixed_release_preserves_phase_and_PAD_guards(self):
        trace=os.environ.get('MELEE_STADIUM_RESULTS_RELEASE_TRACE')
        if not trace:self.skipTest('Retained Results mixed-release source trace not configured')
        path=Path(trace)
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),os.environ['MELEE_STADIUM_RESULTS_RELEASE_TRACE_SHA256'])
        from sd_original_menu_plan import stadium_go_feedback_packet
        from retail_setup_validation import _decode_setup
        from reference_versus_sequence_capture import raw_pad
        rows=[]
        for row in iter_records(path):
            rows.append(row)
            if row['seq']==3181:break
        setup=next(r for r in rows if r['payload'].get('boundary')=='setup')
        expected=_decode_setup(next(x['hex'] for x in setup['payload']['slices'] if x['tag']==4))
        receiver=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),rows[0]['payload']['setup_receipt_sha256'],expected)
        receiver.recorded_input=True;chord=raw_pad(buttons=['L','R','A','START']);start=raw_pad(buttons=['START']);before=None
        for row in rows:
            p=row['payload']
            if p.get('boundary')=='pad_consume' and receiver.setup_seen:
                ports,_,_=driver._transform_consumed_ports(p,row['seq'])
                if ports[:2]==[chord,driver.NEUTRAL_PAD]:
                    if receiver.pause_consumed is None:
                        self.assertGreaterEqual(receiver.tick_count,180);receiver.chord_requested=True
                    else:self.assertIsNotNone(receiver.pause_ready_clock);receiver.exit_requested=True
                if start in ports[:2] and receiver.outcome_seen:
                    self.assertIsNotNone(receiver.chord_released);receiver.start_allowed=True
            if row is rows[-1]:before=copy.copy(receiver)
            receiver.accept(row)
        self.assertTrue(before.start_allowed);self.assertTrue(before.outcome_seen)
        self.assertEqual(before.chord_released['sequence'],2554)
        self.assertEqual(before.last_pad[:2],[start,start])
        self.assertEqual(receiver.last_pad[:2],[driver.NEUTRAL_PAD,start])
        actual_ports,actual_errors,actual_queue=driver._transform_consumed_ports(rows[-1]['payload'],3181)
        self.assertEqual(actual_errors,[0,0,-1,-1]);self.assertEqual(actual_queue['queue_slot_address'],0x8046b108)
        # Synthetic negative/finite-state interpositions retain the real queue,
        # register and source identity; they are not new original observations.
        def clone():
            value=copy.copy(before);value.boundaries=[];value.consumed_samples=[];return value
        def with_ports(pads):
            row=copy.deepcopy(rows[-1]);item=next(x for x in row['payload']['slices'] if x['tag']==3)
            raw=bytearray.fromhex(item['hex'])
            for i,pad in enumerate(pads):raw[i*12:i*12+11]=bytes.fromhex(pad)
            item['hex']=raw.hex();return row
        for p1 in (driver.NEUTRAL_PAD,start):
            for p2 in (driver.NEUTRAL_PAD,start):
                value=clone();value.accept(with_ports([p1,p2,*actual_ports[2:]]))
                self.assertEqual(value.last_pad[:2],[p1,p2])
        outside=clone();outside.start_allowed=False
        with self.assertRaises(ValueError):outside.accept(rows[-1])
        bad_pairs=([raw_pad(buttons=['A']),start],[driver.NEUTRAL_PAD,raw_pad(buttons=['L','START'])],[chord,driver.NEUTRAL_PAD])
        for pair in bad_pairs:
            with self.assertRaises(ValueError):clone().accept(with_ports([*pair,*actual_ports[2:]]))
        with self.assertRaises(ValueError):clone().accept(with_ports([*actual_ports[:2],driver.NEUTRAL_PAD,actual_ports[3]]))
        bad=copy.deepcopy(rows[-1]);item=next(x for x in bad['payload']['slices'] if x['tag']==3)
        raw=bytearray.fromhex(item['hex']);raw[10]=0xff;item['hex']=raw.hex()
        with self.assertRaises(ValueError):clone().accept(bad)
        bad=copy.deepcopy(rows[-1]);bad['payload']['gprs'][25]+=0x30
        with self.assertRaises(ValueError):clone().accept(bad)

    def test_native_finalizer_package_import_from_isolated_script_context(self):
        # -I removes cwd/PYTHONPATH import help, matching the script entry
        # boundary. The real decoder must reach real missing-stream refusal;
        # there is no native/game success endpoint or capture here.
        code="""from pathlib import Path
import sys
root=Path(sys.argv[1]);scratch=Path(sys.argv[2])
assert str(root) not in sys.path
sys.path.insert(0,str(root/'scripts'))
import capture_allocation_history as capture
assert capture.ROOT==root
from whole_session_replay import WholeSessionReplayError
try:
    capture.validate_stadium_three_native(scratch/'absent-native',scratch/'absent-profile.json',{})
except WholeSessionReplayError as error:
    assert 'cannot decode raw observer stream' in str(error)
    assert 'native-primary.mwro' in str(error)
else:
    raise AssertionError('Missing source stream must refuse')
module=sys.modules['tools.allocation_lifetime_replay']
assert module.__package__=='tools'
assert Path(module.__file__).resolve()==root/'tools/allocation_lifetime_replay.py'
assert 'allocation_lifetime_replay' not in sys.modules
print('Actual package finalizer imports; real missing-stream guard retained')
"""
        result=subprocess.run([sys.executable,'-I','-c',code,str(REPO_ROOT),str(self.workspace)],
                              capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('real missing-stream guard retained',result.stdout)

    def test_actual_results_helper_retains_events_before_exit_release(self):
        # Synthetic transport rows exercise the actual Results state machine;
        # they are not a new original Results observation.
        from reference_versus_sequence_capture import SequenceRunner, ObserverTail, CaptureError
        def row(name,seq,index=0):
            return dict(seq=seq,event='boundary',source_tick=191,draw_ordinal=191,
                        payload=dict(whole_session=True,match_index=index,boundary=name))
        class Tail:
            boundary=staticmethod(ObserverTail.boundary)
            def __init__(self,names):self.rows=[row(name,i) for i,name in enumerate(names)];self.released=False;self.seen=[]
            def next(self,deadline):
                if not self.rows:raise CaptureError('observer record did not arrive before deadline')
                value=self.rows.pop(0);self.seen.append(value['payload']['boundary'])
                if value['payload']['boundary']=='pad_consume':self.released=True
                return value
        class Controller:
            def __init__(self,tail,require_release=True):self.tail=tail;self.require_release=require_release;self.calls=[]
            def hold(self,*args,**kwargs):
                if self.require_release:self_outer.assertTrue(self.tail.released)
                self.calls.append(kwargs['action'])
        self_outer=self
        chain=['results_gobj','pad_consume','results_exit','results_mode_exit','scene_teardown','return_css']
        tail=Tail(chain);controller=Controller(tail);policy=SequenceRunner([],[],tail,controller)
        with patch('reference_versus_sequence_capture.time.monotonic',return_value=0):
            policy._confirm_results(0,2.5,input_ready=lambda:tail.released)
        self.assertEqual(controller.calls,['results-start-1'])
        self.assertEqual([x['name'] for x in policy.boundaries],['results_gobj','results_exit','results_mode_exit','scene_teardown','return_css'])
        # Default None retains immediate Start and the same required boundaries.
        tail=Tail([x for x in chain if x!='pad_consume']);controller=Controller(tail,False);policy=SequenceRunner([],[],tail,controller)
        with patch('reference_versus_sequence_capture.time.monotonic',return_value=0):policy._confirm_results(0,2.5)
        self.assertEqual(controller.calls,['results-start-1'])
        for names in (['results_gobj'],['vs_mode_exit','pad_consume'],['pad_consume','results_exit','results_mode_exit','scene_teardown','return_css']):
            tail=Tail(names);controller=Controller(tail);policy=SequenceRunner([],[],tail,controller)
            with patch('reference_versus_sequence_capture.time.monotonic',return_value=0):
                with self.assertRaises(CaptureError):policy._confirm_results(0,2.5,input_ready=lambda:tail.released)
            if 'pad_consume' not in names or names[0]=='vs_mode_exit':self.assertEqual(controller.calls,[])
        tail=Tail(['results_gobj','pad_consume']);tail.rows[0]['payload']['match_index']=1
        controller=Controller(tail);policy=SequenceRunner([],[],tail,controller)
        with self.assertRaises(CaptureError):policy._confirm_results(0,driver.time.monotonic()+2.5,input_ready=lambda:tail.released)
        self.assertEqual(controller.calls,[])

    def test_actual_driver_exit_results_order_and_remaining_deadline(self):
        # These are source transport endpoints, not a simulated game/graphics service.
        from reference_versus_sequence_capture import raw_pad, SequenceRunner
        class Receiver:
            def __init__(self):
                self.seq=self.match_index=self.menu_polls=self.menu_consumed=self.tick_count=0
                self.css=True;self.latest_menu=None;self.setup_seen=True;self.previous_tick=None
                self.ended=self.returned_css=self.outcome_seen=self.chord_requested=self.start_allowed=False
                self.chord_consumed=self.chord_released=None;self.last_pad=[driver.NEUTRAL_PAD]*2
                self.pause_consumed=self.pause_released=self.pause_ready_clock=None
                self.exit_requested=False;self.results_entered=False
            def accept(self,row):
                self.seq+=1;kind=row['payload']['boundary'] if row['event']=='boundary' else 'end'
                if kind=='source_tick':
                    if not self.chord_requested:self.tick_count+=1
                    elif self.pause_released is not None:self.pause_ready_clock=dict(sequence=self.seq)
                    self.previous_tick=self.tick_count
                elif kind=='pad_consume':
                    self.menu_consumed+=1;self.menu_polls+=1;self.last_pad=controller.pad
                    if controller.pad[0]==raw_pad(buttons=['L','R','A','START']):
                        if self.exit_requested:self.chord_consumed=dict(sequence=self.seq,source_tick=self.tick_count+3)
                        else:self.pause_consumed=dict(sequence=self.seq)
                    elif self.exit_requested and self.chord_consumed is not None:self.chord_released=dict(sequence=self.seq,source_tick=self.tick_count+4)
                    elif self.pause_consumed is not None:self.pause_released=dict(sequence=self.seq)
                elif kind=='vs_exit_return':self.outcome_seen=True
                elif kind=='results_enter':self.results_entered=True
                elif kind=='pad_poll':self.menu_polls+=1
                elif kind=='return_css':self.returned_css=True
                elif kind=='end':self.ended=True
            def next_lifetime(self):
                index=self.match_index+1;seq=self.seq;self.__init__();self.match_index=index;self.seq=seq
        class Controller:
            def __init__(self):self.pad=[driver.NEUTRAL_PAD]*2;self.pending=False;self.requests=[];self.sleep=lambda seconds:None;self.defer_release=False;self.drop_release=False;self.bad_order=False
            def set_both(self,p1,p2,*,action):
                self.pad=[p1,p2];self.pending=True
                self.requests.append((receiver.match_index,receiver.tick_count,action))
        class Tail:
            def __init__(self):self.phase={};self.waits=[];self.seen=[]
            def next(self,deadline):
                if (controller.pending and not (receiver.exit_requested and controller.pad==[driver.NEUTRAL_PAD]*2 and
                    (controller.drop_release or (controller.defer_release and not receiver.results_entered)))):
                    controller.pending=False;kind='pad_consume'
                elif controller.drop_release and receiver.results_entered:kind='pad_poll'
                elif not receiver.chord_requested or receiver.pause_ready_clock is None:kind='source_tick'
                elif not receiver.outcome_seen:kind='vs_exit_return'
                else:
                    index=receiver.match_index;phase=self.phase.get(index,0)
                    kinds=['results_enter','vs_mode_exit','return_css','end'] if controller.bad_order else ['vs_mode_exit','results_enter','return_css','end']
                    kind=kinds[phase]
                    self.phase[index]=phase+1
                self.seen.append(kind)
                return dict(seq=receiver.seq,event='end' if kind=='end' else 'boundary',
                    payload=dict(boundary=kind,whole_session=True,match_index=receiver.match_index),source_tick=0,draw_ordinal=0)
            def require_completed_status(self,timeout=5):self.waits.append(timeout)
        receiver=Receiver();controller=Controller();tail=Tail();order=[]
        def confirm(policy,index,deadline,*,input_ready=None):
            self.assertEqual([x for x in tail.seen if x in ('vs_mode_exit','results_enter')][-2:],['vs_mode_exit','results_enter'])
            while input_ready is not None and not input_ready():policy.observer.next(deadline)
            self.assertIsNotNone(receiver.chord_released)
            self.assertTrue(receiver.start_allowed)
            order.append(index);policy.observer.next(deadline)
        with patch.object(driver,'drive_authored_css_sss',lambda *args:None),patch.object(SequenceRunner,'_confirm_results',confirm):
            values=driver.drive_stadium_whole_three(receiver,tail,controller,{'boot':[]},driver.time.monotonic()+20)
        self.assertEqual(order,[0,1,2]);self.assertEqual(len(values),3)
        self.assertEqual([x['request']['minimum_neutral_ticks'] for x in values],[180]*3)
        self.assertTrue(all(x['actual_chord']['source_tick']==183 for x in values))
        self.assertEqual(len(tail.waits),1);self.assertGreater(tail.waits[0],0);self.assertLessEqual(tail.waits[0],5)
        self.assertTrue(all(ticks>=180 for _,ticks,label in controller.requests if label=='whole-no-contest'))
        # Source can finish VS before the next PAD consume sees release.
        # Actual driver must preserve mode/Results order and delay Start.
        receiver=Receiver();controller=Controller();controller.defer_release=True;tail=Tail();order=[]
        with patch.object(driver,'drive_authored_css_sss',lambda *args:None),patch.object(SequenceRunner,'_confirm_results',confirm):
            delayed=driver.drive_stadium_whole_three(receiver,tail,controller,{'boot':[]},driver.time.monotonic()+20)
        self.assertEqual(order,[0,1,2]);self.assertEqual(len(delayed),3)
        self.assertTrue(all(x['actual_pause_chord'] is not None and x['actual_pause_release'] is not None and
                            x['pause_ready'] is not None for x in delayed))
        for mode in ('drop_release','bad_order'):
            receiver=Receiver();controller=Controller();setattr(controller,mode,True);tail=Tail();order=[]
            with patch.object(driver,'drive_authored_css_sss',lambda *args:None),patch.object(SequenceRunner,'_confirm_results',confirm):
                with self.assertRaises((ValueError,RuntimeError)):
                    driver.drive_stadium_whole_three(receiver,tail,controller,{'boot':[]},driver.time.monotonic()+20)
            self.assertEqual(order,[])

        # Deterministic remaining<5 falsifies an unconditional five-second wait.
        receiver=Receiver();controller=Controller();tail=Tail()
        with patch.object(driver.time,'monotonic',return_value=0.0),patch.object(driver,'drive_authored_css_sss',lambda *args:None),patch.object(SequenceRunner,'_confirm_results',confirm):
            driver.drive_stadium_whole_three(receiver,tail,controller,{'boot':[]},2.5)
        self.assertEqual(tail.waits,[2.5])
        # The same actual driver must refuse a completion-status call that
        # returns after the absolute operation deadline.
        receiver=Receiver();controller=Controller();tail=Tail();clock=[0.0]
        tail.require_completed_status=lambda timeout:clock.__setitem__(0,21.0)
        with patch.object(driver.time,'monotonic',side_effect=lambda:clock[0]),patch.object(driver,'drive_authored_css_sss',lambda *args:None),patch.object(SequenceRunner,'_confirm_results',confirm):
            with self.assertRaises(ValueError):
                driver.drive_stadium_whole_three(receiver,tail,controller,{'boot':[]},20.0)



    def test_actual_deadline_controller_refuses_press_release_and_late_status(self):
        class Pipe:
            def __init__(self):self.writes=[];self.sleep=lambda seconds:None
            def set_both(self,p1,p2,*,action):self.writes.append((p1,p2,action))
        pipe=Pipe()
        with patch.object(driver.time,'monotonic',return_value=10):
            wrapped=driver.StadiumDeadlineController(pipe,10)
            with self.assertRaises(ValueError):wrapped.set_both('p1','p2',action='expired')
            with self.assertRaises(ValueError):wrapped.hold('p1','p2',.12,action='expired-hold')
        self.assertEqual(pipe.writes,[])
        with patch.object(driver.time,'monotonic',side_effect=[0,0,11]):
            wrapped=driver.StadiumDeadlineController(pipe,10)
            with self.assertRaises(ValueError):wrapped.hold('p1','p2',.12,action='overslept')
        self.assertEqual(pipe.writes,[('p1','p2','overslept')])


    def test_actual_retained_first_tick_consumed_owner_and_stale_refusals(self):
        if TRACE is None:self.skipTest('Retained original source tick/PAD trace was not configured')
        from sd_original_menu_plan import stadium_go_feedback_packet
        rows=list(iter_records(TRACE))
        setup=next(r for r in rows if r['event']=='boundary' and r['payload']['boundary']=='setup')
        actual=[r for r in rows if r['seq']>setup['seq'] and r['event']=='boundary' and
                r['payload']['boundary'] in ('pad_consume','source_tick')]
        self.assertGreaterEqual(len(actual),4)
        first_consume,first_tick=actual[:2]
        self.assertEqual((first_consume['payload']['boundary'],first_tick['payload']['boundary']),('pad_consume','source_tick'))
        self.assertEqual((first_consume['source_tick'],first_tick['source_tick']),(0,0))
        pending=None;pairs=0
        for row in actual:
            if row['payload']['boundary']=='pad_consume':
                self.assertIsNone(pending);pending=row
            else:
                self.assertIsNotNone(pending)
                self.assertLess(pending['seq'],row['seq'])
                self.assertEqual(pending['source_tick'],row['source_tick'])
                pending=None;pairs+=1
        self.assertIsNone(pending)
        self.assertGreater(pairs,1)
        def projected(row,seq=None):
            value=copy.deepcopy(row)
            value['payload'].update(whole_session=True,match_index=0)
            if seq is not None:value['seq']=seq
            return value
        def receiver(seq):
            value=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),'a'*64,{})
            # Synthetic scope/Setup admission wrapper only; retained original
            # PAD/source operands and callback identities are not rewritten.
            value.announcements=['handshake','start'];value.setup_seen=True;value.seq=seq
            return value
        value=receiver(first_consume['seq'])
        value.accept(projected(first_consume));value.accept(projected(first_tick))
        self.assertEqual(value.tick_count,1)
        self.assertIsNone(value.pending_neutral_sample)
        stale=projected(actual[3],value.seq)
        with self.assertRaisesRegex(ValueError,'fresh neutral'):value.accept(stale)
        value=receiver(first_tick['seq']);value.last_pad=[driver.NEUTRAL_PAD]*2
        with self.assertRaisesRegex(ValueError,'fresh neutral'):value.accept(projected(first_tick))
        value=receiver(first_consume['seq']);value.accept(projected(first_consume))
        with self.assertRaisesRegex(ValueError,'repeated unpaired'):value.accept(projected(first_consume,value.seq))
        value=receiver(first_consume['seq']);value.accept(projected(first_consume))
        wrong=projected(first_tick);wrong['source_tick']=1
        with self.assertRaises(ValueError):value.accept(wrong)


    def test_actual_native_boundary_dispatch_five_pcs_and_default_off(self):
        import re
        source=(SOURCE_ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        start=source.index('static bool IsCaptureBoundary(')
        stop=source.index('\nbool Observer::IsRngReturnBoundary',start)
        predicate=source[start:stop]
        start=source.index('void Observer::OnBoundary(')
        stop=source.index('\nvoid Observer::Observe(',start)
        dispatch=source[start:stop]
        constants='\n'.join(re.findall(r'^constexpr u32 (?:SSS_ENTER_RETURN|STADIUM_GO_AFTER_PC) = .*?;',source,re.M))
        array=re.search(r'constexpr std::array<u32, 7> STADIUM_CONSTRUCTOR_PCS = \{.*?\};',source,re.S).group(0)
        fixture=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
using u32=uint32_t;
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
bool SdInitRequested(){return !Env("MWRC_SD_INIT").empty();}
bool OrdinaryTimeoutRequested(){return false;} bool SparsePairRequested(){return false;}
bool CpuProbeEnabled(){return false;} bool ItemProbeEnabled(){return false;}
struct ProbeEnvironment{u32 rng_return_pc=0;};
ProbeEnvironment CpuProbeEnvironment(){return {};}
const void* FindCpuProbePoint(u32){return nullptr;}
const void* FindItemProbePoint(u32){return nullptr;}
bool IsEnabled(){return true;} bool AllocationConfigured(){return !Env("MWRC_ALLOCATION_OUTPUT").empty();}
namespace Core{struct System{};} namespace PowerPC{struct PowerPCState{};}
unsigned allocation_calls=0,primary_calls=0;
namespace ReferenceAllocation{struct Observer{static void Observe(Core::System*,u32,PowerPC::PowerPCState*){++allocation_calls;}static std::string Error(){return {};}};}
struct Impl{void SetInvalid(const std::string&) {assert(false);}};
struct Observer{Impl* m_impl;void Observe(Core::System*,u32,PowerPC::PowerPCState*){++primary_calls;}static void OnBoundary(Core::System*,u32,PowerPC::PowerPCState*);};
Impl impl;Observer instance{&impl};Observer& Instance(){return instance;}
'''+constants+'\n'+array+'\n'+LEGACY_CAPTURE_BOUNDARY.replace('IsCaptureBoundary','BeforeCaptureBoundary',1)+predicate+dispatch
        fixture+=r'''
int main(){Core::System system;PowerPC::PowerPCState state;
const std::array<u32,5> added{SSS_ENTER_RETURN,0x8025a310,0x8025a4c0,0x8025a4e8,0x8025a548};
for(const char* legacy:{"none","go","constructor","transform","sd"}){
env.clear();if(std::string(legacy)=="go"||std::string(legacy)=="constructor")env["MWRC_STADIUM_GO_PREFIX"]="1";
if(std::string(legacy)=="constructor")env["MWRC_STADIUM_CONSTRUCTOR_OWNER"]="1";
if(std::string(legacy)=="transform")env["MWRC_TRANSFORM_PREFIX"]="1";
if(std::string(legacy)=="sd"){env["MWRC_SD_INIT"]="1";env["MWRC_SD_MENU_PROBE"]="sd_prefix";}
for(u32 pc:added)assert(BeforeCaptureBoundary(pc)==IsCaptureBoundary(pc));
for(u32 pc:STADIUM_CONSTRUCTOR_PCS)assert(BeforeCaptureBoundary(pc)==IsCaptureBoundary(pc));
for(u32 pc:{STADIUM_GO_AFTER_PC,0x8025bbd0u,0x8034dd8cu,0xdeadbeefu})assert(BeforeCaptureBoundary(pc)==IsCaptureBoundary(pc));}
env.clear();env["MWRC_STADIUM_WHOLE_FEEDBACK"]="1";env["MWRC_ALLOCATION_OUTPUT"]="owned";
for(u32 pc:added){assert(IsCaptureBoundary(pc));primary_calls=allocation_calls=0;Observer::OnBoundary(&system,pc,&state);assert(primary_calls==1&&allocation_calls==1);}
for(u32 pc:STADIUM_CONSTRUCTOR_PCS){assert(!IsCaptureBoundary(pc));primary_calls=0;Observer::OnBoundary(&system,pc,&state);assert(primary_calls==0);}
assert(!IsCaptureBoundary(STADIUM_GO_AFTER_PC));assert(!IsCaptureBoundary(0xdeadbeef));
env.clear();for(u32 pc:added){primary_calls=0;Observer::OnBoundary(&system,pc,&state);assert(primary_calls==0);}
}
'''
        cpp=self.workspace/'actual-five-pc-dispatch.cpp';binary=self.workspace/'actual-five-pc-dispatch'
        cpp.write_text(fixture)
        subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True,timeout=60)
        subprocess.run([str(binary)],check=True,timeout=15)

    def test_actual_stock4_none_target_and_context_refusals(self):
        allocation=load_exact('capture_allocation_history',SOURCE_ROOT/'scripts/capture_allocation_history.py')
        expected=dict(stage=3,match_kind=1,timer_enabled=False,timer_counts_up=False,
            time_limit_seconds=0,is_stock=True,disable_pausing=False,is_teams=False,
            item_frequency=-1,item_mask_hex='ffffffffffffffff',damage_ratio_bits='3f800000',
            game_speed_bits='3f800000',players=[dict(port=p,character_kind=8,costume=c,
                stocks=4,player_type=0,rumble_enabled=True) for p,c in [(1,1),(2,0)]])
        target=self.workspace/'stock4-none.json'
        target.write_text(json.dumps({'expected_setup':expected}))
        self.assertEqual(allocation.stadium_three_target(target)['source_expected_setup'],expected)
        self.assertEqual(allocation.stadium_three_rule_profile(expected),'stock4_none')
        time120=copy.deepcopy(expected)
        time120.update(match_kind=0,is_stock=False,timer_enabled=True,time_limit_seconds=120,item_frequency=2)
        for p in time120['players']:p['stocks']=3
        self.assertEqual(allocation.stadium_three_rule_profile(time120),'boot_time120')
        for key,value in [('stage',32),('timer_enabled',True),('time_limit_seconds',60),
                          ('item_frequency',0),('item_mask_hex','0000000000000000'),
                          ('disable_pausing',True),('damage_ratio_bits','40000000')]:
            bad=copy.deepcopy(expected);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):allocation.stadium_three_rule_profile(bad)
        for key,value in [('costume',0),('stocks',3),('character_kind',0),('rumble_enabled',False)]:
            bad=copy.deepcopy(expected);bad['players'][0][key]=value
            with self.subTest(player_key=key),self.assertRaises(ValueError):allocation.stadium_three_rule_profile(bad)
        # Execute the actual receiver branch; this synthetic context is a mechanism
        # fixture, not an original recording or proof of firstCSS ownership.
        from sd_original_menu_plan import stadium_go_feedback_packet
        rules=bytearray(24);rules[2:9]=bytes([1,2,4,0,10,0,0]);rules[10]=1
        save=bytearray(0x55e8);save[0x448]=255;save[0x450:0x458]=b'\xff'*8;save[0x458:0x45a]=b'\x01\x01'
        def admit(raw_rules,raw_save):
            r=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),'a'*64,expected)
            r.announcements=['handshake','start']
            row=dict(seq=0,event='boundary',payload=dict(whole_session=True,match_index=0,boundary='css_enter'))
            with patch('whole_session_replay._first_css_context',return_value={
                'game_rules_hex':raw_rules.hex(),'save_data_hex':raw_save.hex()}):r.accept(row)
        admit(rules,save)
        for index,value in [(2,0),(4,3),(8,1),(9,1),(10,0)]:
            bad=rules.copy();bad[index]=value
            with self.assertRaises(ValueError):admit(bad,save)
        for index,value in [(0x448,2),(0x450,0),(0x458,0)]:
            bad=save.copy();bad[index]=value
            with self.assertRaises(ValueError):admit(rules,bad)

    def test_actual_whole_items_lock_receiver_identity(self):
        from sd_original_menu_plan import stadium_go_feedback_packet
        from reference_observer_stream import SLICE_NAMES
        def sample(kind=16,lock=0,address=0x804d6bec,missing=False):
            def field(tag,addr,raw):return dict(name=SLICE_NAMES[tag],tag=tag,flags=0,address=addr,size=len(raw),hex=raw.hex())
            flow=bytearray(0x18);flow[0]=kind;flow[4]=1;flow[0x11]=1
            fields=[field(40,0x80479d30,b'\x01'),field(45,0x804a04f0,flow),field(46,0x804d6bc8,b'\0'*8)]
            if not missing:fields.append(field(56,address,bytes([lock])))
            return dict(seq=1,event='boundary',payload=dict(pc='0x8001985c',slices=fields))
        def receive(row):
            r=driver.StadiumWholeFeedbackReceiver(stadium_go_feedback_packet(),'a'*64,{})
            r._pad_poll(row);return r.latest_menu
        self.assertEqual(receive(sample())['items_locked'],0)
        self.assertEqual(receive(sample(lock=1))['items_locked'],1)
        for row in [sample(missing=True),sample(address=0x804d6bed),sample(lock=2),sample(kind=13)]:
            with self.assertRaises(ValueError):receive(row)

    def test_actual_stock4_cold_rules_route_and_refusals(self):
        # Synthetic endpoints drive the actual wait_source/tap/cold-route body.
        # This proves finite policy/order/refusal, not live game progression.
        from sd_original_menu_plan import stadium_go_feedback_packet
        from retail_input_plan import DISCONNECTED_PAD
        class ReachedCss(Exception):pass
        def simulate(mode):
            menus=stadium_go_feedback_packet();actions={a['label']:a for a in menus['actions']}
            class Receiver:
                css=None;latest_menu=None;seq=0;menu_polls=0;menu_consumed=0;ended=False
                expected={'is_stock':True};last_pad=[driver.NEUTRAL_PAD]*2
                def accept(self,row):
                    self.seq+=1
                    if row['kind']=='consume':
                        self.last_pad=row['ports'][:2]
                        if not row.get('stale'):self.menu_consumed+=1
                    else:
                        self.menu_polls+=1;self.latest_menu=row['menu']
                        if row.get('css'):self.css=True
            r=Receiver();queue=[dict(kind='poll',menu=dict(menus['actions'][0]['before']))];writes=[]
            class Controller:
                sleep=staticmethod(lambda seconds:None)
                def set_both(self,p1,p2,*,action):
                    writes.append((action,p1,p2,copy.deepcopy(r.latest_menu)))
                    queue.append(dict(kind='consume',ports=[p1,p2,DISCONNECTED_PAD,DISCONNECTED_PAD],stale=mode=='stale-consume'))
                    if not action.endswith(':release'):return
                    a=actions[action[:-len(':release')]];state=dict(a['after'])
                    if state.get('kind')==16:state.setdefault('items_locked',0)
                    if mode=='locked' and a['label']=='open-items':state['items_locked']=1
                    if mode=='wrong-owner' and a['label']=='open-items':state['kind']=13
                    queue.append(dict(kind='poll',menu=state,css=state.get('scene')==8 and mode!='missing-CSS'))
            class Tail:
                def next(self,deadline):
                    if queue:return queue.pop(0)
                    return dict(kind='poll',menu=copy.deepcopy(r.latest_menu))
            def reached(*args):raise ReachedCss()
            with patch.object(driver.time,'monotonic',return_value=0.0),patch.object(driver,'drive_authored_css_sss',reached):
                try:driver.drive_stadium_whole_three(r,Tail(),Controller(),menus,10.0)
                except (ReachedCss,ValueError) as error:return error,writes,r,menus
            self.fail('Policy bypassed CSS control')
        error,writes,r,menus=simulate('positive');self.assertIsInstance(error,ReachedCss)
        self.assertEqual([x[0] for x in writes],[v for a in menus['actions'] for v in [a['label'],a['label']+':release']])
        self.assertEqual(r.menu_consumed,len(menus['actions'])*2);self.assertTrue(r.css)
        for a in menus['actions']:
            press=next(x for x in writes if x[0]==a['label'])
            self.assertEqual(press[1:3],(a['p1'],a['p2']))
            self.assertTrue(driver.matches(press[3],a['before']))
        for mode in ['locked','wrong-owner','missing-CSS','stale-consume']:
            error,writes,r,menus=simulate(mode)
            with self.subTest(mode=mode):
                self.assertIsInstance(error,ValueError);self.assertIsNone(r.css)
                if mode in ['locked','wrong-owner']:self.assertNotIn('items-frequency-row',[x[0] for x in writes])
                if mode=='stale-consume':self.assertEqual(len(writes),1)

    def test_actual_whole_items_emitter_on_off_and_owner(self):
        source=(SOURCE_ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        begin=source.index('  bool AddMenuSteeringSlices(')
        end=source.index('    // Source menu globals survive arena teardown.',begin)
        body=source[begin:end]+'    return true;\n  }\n'
        enum_begin=source.index('enum class SliceTag : u16');enum_end=source.index('\n};',enum_begin)+3
        fixture=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;
namespace Core {struct System{};}
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
bool OrdinaryTimeoutRequested(){return false;} bool SparsePairRequested(){return false;}
''' + source[enum_begin:enum_end] + r'''
struct Gate {
 bool transform_prefix_enabled=false,stadium_go_prefix_enabled=false,stadium_whole_feedback=false;
 u8 scene=1,kind=16;bool valid_word=true;std::vector<SliceTag> tags;
 bool ReadU32(Core::System*,u32 address,u32* value){
  if(address==0x804d6720){*value=0x80479d30;return true;}
  const std::array<u32,3> words={0x880db54c,0x28000000,0x40820180};
  assert(address>=0x80233ec0 && address<=0x80233ec8);
  *value=words[(address-0x80233ec0)/4]^(valid_word?0:1);return true;
 }
 bool ReadBytes(Core::System*,u32 address,size_t size,u8* value){assert(size==1);if(address==0x80479d30)*value=scene;else{assert(address==0x804a04f0);*value=kind;}return true;}
 bool AddSlice(Core::System*,SliceTag tag,u32 address,size_t size){
  if(tag==SliceTag::SdItemsLock){assert(address==0x804d6bec&&size==1);}tags.push_back(tag);return true;
 }
''' + body + r'''
};
bool lock(const Gate& g){for(auto tag:g.tags)if(tag==SliceTag::SdItemsLock)return true;return false;}
int main(){Core::System s;Gate off;assert(off.AddMenuSteeringSlices(&s));assert(!lock(off));assert(off.tags==std::vector<SliceTag>({SliceTag::MenuMainFlow,SliceTag::MenuMainInput}));
 Gate on;on.stadium_whole_feedback=true;assert(on.AddMenuSteeringSlices(&s));assert(lock(on));
 Gate old;old.stadium_go_prefix_enabled=true;assert(old.AddMenuSteeringSlices(&s));assert(old.tags==on.tags);
 Gate wrong;wrong.stadium_whole_feedback=true;wrong.valid_word=false;assert(!wrong.AddMenuSteeringSlices(&s));assert(!lock(wrong));
 Gate owner;owner.stadium_whole_feedback=true;owner.kind=13;assert(owner.AddMenuSteeringSlices(&s));assert(!lock(owner));
 owner=Gate();owner.stadium_whole_feedback=true;owner.scene=8;assert(owner.AddMenuSteeringSlices(&s));assert(!lock(owner));
}
'''
        cpp=self.workspace/'actual-whole-items-emitter.cpp';binary=self.workspace/'actual-whole-items-emitter'
        cpp.write_text(fixture)
        subprocess.run(['/usr/bin/clang++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(binary)],check=True,timeout=60)
        subprocess.run([str(binary)],check=True,timeout=15)


    def test_retained_personal_export_wrong_gci_always_refuses(self):
        allocation=load_exact('capture_allocation_history',SOURCE_ROOT/'scripts/capture_allocation_history.py')
        bad=self.workspace/'wrong-personal.gci';bad.write_bytes(b'not the retained source export')
        with self.assertRaises(ValueError):allocation.validate_retained_personal_export({},bad)

    def test_actual_retained_personal_export_admission_and_copy_refusals(self):
        setting=os.environ.get('MELEE_STADIUM_PERSONAL_EXPORT_BINDING')
        if not setting:self.skipTest('Retained exact Personal export binding not configured')
        binding=json.loads(Path(setting).read_text())
        for path,digest in binding['files'].items():
            self.assertEqual(hashlib.sha256(Path(path).read_bytes()).hexdigest(),digest)
        allocation=load_exact('capture_allocation_history',SOURCE_ROOT/'scripts/capture_allocation_history.py')
        card=Path(binding['card']);source_report=Path(binding['report']);source_manifest=Path(binding['manifest'])
        expected_hash=hashlib.sha256(card.read_bytes()).hexdigest()
        original=card.read_bytes()
        provenance=json.loads(Path(binding['provenance']).read_text())
        allocation.validate_retained_personal_provenance(provenance,
            {'USA/Card A/'+card.name:expected_hash},expected_hash,
            hashlib.sha256(source_report.read_bytes()).hexdigest())
        for key,value in [('source_profile_mode','Everything unlocked'),('source_profile_gci_sha256','0'*64),
                          ('source_profile_export_report_sha256','0'*64),('source_revision','0'*40)]:
            bad=copy.deepcopy(provenance);bad[key]=value
            with self.subTest(provenance_key=key),self.assertRaises(ValueError):
                allocation.validate_retained_personal_provenance(bad,{'USA/Card A/'+card.name:expected_hash},
                    expected_hash,hashlib.sha256(source_report.read_bytes()).hexdigest())
        bad=copy.deepcopy(provenance);bad.pop('parent_provenance')
        with self.assertRaises(ValueError):allocation.validate_retained_personal_provenance(bad,
            {'USA/Card A/'+card.name:expected_hash},expected_hash,hashlib.sha256(source_report.read_bytes()).hexdigest())
        export=dict(source_run_report=str(source_report),source_run_report_sha256=hashlib.sha256(source_report.read_bytes()).hexdigest(),
            source_run_manifest=str(source_manifest),source_run_manifest_sha256=hashlib.sha256(source_manifest.read_bytes()).hexdigest(),
            source_run_check='personalStateSurvivedReload',profile_mode='Personal progress',
            exported_gci_sha256=expected_hash,exported_gci_bytes=card.stat().st_size)
        receipt=dict(disc=dict(sha256='b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c'),
            initial_card=dict(file_count=1,format='single imported retained Personal progress GCI',
                gci_filename=card.name,gci_sha256=expected_hash,save_mode='Personal progress'),
            source_profile_export=export)
        link=self.workspace/'personal-link.gci';link.symlink_to(card)
        with self.assertRaises(ValueError):allocation.validate_retained_personal_export(receipt,link)
        # Execute the complete existing setup loader over actual retained files
        # and the genuinely linked9494 manifest; no native process or fake service.
        from types import SimpleNamespace
        setup_path=Path(binding['setup_receipt'])
        full_args=SimpleNamespace(disc=Path(binding['disc']),dol=Path(binding['dol']),
            dolphin=Path(binding['native_binary']),template_user=Path(binding['profile']),
            checkpoint_gc=Path(binding['checkpoint_gc']),initial_gci=card,
            provenance=Path(binding['provenance']))
        actual_manifest=allocation.validate_reference_build_manifest(
            Path(binding['native_manifest']),Path(binding['native_binary']))
        admitted=allocation.validate_setup_receipt(setup_path,full_args,actual_manifest)
        self.assertEqual(admitted['card_baseline'],'single imported retained Personal progress GCI')
        self.assertEqual(admitted['initial_profile_sha256'],expected_hash)
        bad_args=copy.copy(full_args);bad_args.initial_gci=link
        with self.assertRaises(ValueError):allocation.validate_setup_receipt(setup_path,bad_args,actual_manifest)
        # The initial source is ordinary; independently reject a linked card
        # directory or linked isolated card file before admission, not only copy.
        for linked_directory in [True,False]:
            root=self.workspace/('linked-personal-directory' if linked_directory else 'linked-personal-file')
            (root/'USA').mkdir(parents=True)
            if linked_directory:
                (root/'USA/Card A').symlink_to(Path(binding['checkpoint_gc'])/'USA/Card A',target_is_directory=True)
            else:
                (root/'USA/Card A').mkdir()
                (root/'USA/Card A'/card.name).symlink_to(card)
            linked_args=copy.copy(full_args);linked_args.checkpoint_gc=root
            with self.subTest(linked_directory=linked_directory),self.assertRaises(ValueError):
                allocation.validate_setup_receipt(setup_path,linked_args,actual_manifest)
            self.assertEqual(card.read_bytes(),original)
        false_setup=json.loads(setup_path.read_text());false_setup['initial_card']['save_mode']='Everything unlocked'
        false_path=self.workspace/'wrong-personal-setup.json';false_path.write_text(json.dumps(false_setup))
        with self.assertRaises(ValueError):allocation.validate_setup_receipt(false_path,full_args,actual_manifest)
        bad_manifest=copy.deepcopy(actual_manifest);bad_manifest['sha256']='0'*64
        with self.assertRaises(ValueError):allocation.validate_setup_receipt(setup_path,full_args,bad_manifest)
        result=allocation.validate_retained_personal_export(receipt,card)
        self.assertEqual(result[0],source_report.resolve());self.assertEqual(result[2],source_manifest.resolve())
        for key,value in [('profile_mode','Everything unlocked'),('exported_gci_sha256','0'*64),
                          ('exported_gci_bytes',1),('source_run_check','invented-PASS'),
                          ('source_run_report_sha256','0'*64),('source_run_manifest_sha256','0'*64)]:
            wrong=copy.deepcopy(receipt);wrong['source_profile_export'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):allocation.validate_retained_personal_export(wrong,card)
        for key,value in [('save_mode','Everything unlocked'),('file_count',2),('format','single imported Everything unlocked GCI')]:
            wrong=copy.deepcopy(receipt);wrong['initial_card'][key]=value
            with self.subTest(card_key=key),self.assertRaises(ValueError):allocation.validate_retained_personal_export(wrong,card)
        wrong=copy.deepcopy(receipt);wrong['disc']['sha256']='0'*64
        with self.assertRaises(ValueError):allocation.validate_retained_personal_export(wrong,card)
        wrong=copy.deepcopy(receipt);wrong['source_profile_export']['source_run_result']='pass'
        with self.assertRaises(ValueError):allocation.validate_retained_personal_export(wrong,card)
        for source_key in ['source_run_report','source_run_manifest']:
            changed=self.workspace/(source_key+'.json');value=json.loads(Path(export[source_key]).read_text());value['unreviewed_change']=True;changed.write_text(json.dumps(value))
            wrong=copy.deepcopy(receipt);wrong['source_profile_export'][source_key]=str(changed)
            wrong['source_profile_export'][source_key+'_sha256']=hashlib.sha256(changed.read_bytes()).hexdigest()
            with self.assertRaises(ValueError):allocation.validate_retained_personal_export(wrong,card)
        setup=dict(card_baseline='single imported retained Personal progress GCI',
            initial_profile_sha256=expected_hash,external_save_hashes={'USA/Card A/'+card.name:expected_hash})
        checkpoint=self.workspace/'personal-checkpoint';(checkpoint/'USA/Card A').mkdir(parents=True)
        copied=checkpoint/'USA/Card A'/card.name;copied.write_bytes(original)
        out=self.workspace/'personal-owned'
        result=driver.prepare_stadium_setup_gci_folder(checkpoint,copied,out,setup)
        self.assertEqual(result.read_bytes(),original);self.assertEqual(result.stat().st_mode&0o777,0o400)
        for label in ['unknown','single imported Everything unlocked GCI']:
            wrong=copy.deepcopy(setup);wrong['card_baseline']=label
            if label=='unknown':
                with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(checkpoint,copied,self.workspace/'wrong-label',wrong)
            # Existing Everything label alone is not this strict admission: the
            # unchanged full setup loader rejects its hash/export-mode mismatch.
        nested=checkpoint/'USA/Card A/not-created'
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(checkpoint,copied,nested,setup)
        self.assertFalse(nested.exists())
        (checkpoint/'USA/Card A/extra.gci').write_bytes(b'extra')
        with self.assertRaises(ValueError):driver.prepare_stadium_setup_gci_folder(checkpoint,copied,self.workspace/'extra-card-personal',setup)
        self.assertEqual(card.read_bytes(),original)



LEGACY_PUBLISHER = '  void PublishStadiumProgress(const char* phase, u32 pc, u32 word, u32 argument,\n                              PowerPC::PowerPCState* state, u32 source_tick)\n  {\n    std::string json =\n        "{\\"diagnostic\\":\\"stadium_go_prefix_v3\\",\\"phase\\":\\"" +\n        std::string(phase) + "\\",\\"setup_receipt_sha256\\":\\"" +\n        stadium_go_prefix_setup_receipt_sha256 +\n        "\\",\\"setup_profile_verified_by_observer\\":false,\\"pc\\":\\"0x";\n    if (!AppendHex(&json, pc, 8))\n      return SetInvalid("Stadium named Progress PC serialization exceeded its bound"), void();\n    json += "\\",\\"word\\":\\"0x";\n    if (!AppendHex(&json, word, 8))\n      return SetInvalid("Stadium named Progress word serialization exceeded its bound"), void();\n    json += "\\",\\"argument\\":\\"0x";\n    if (!AppendHex(&json, argument, 8))\n      return SetInvalid("Stadium named Progress argument serialization exceeded its bound"), void();\n    json += "\\",\\"lr\\":\\"0x";\n    if (!AppendHex(&json, state->spr[8], 8))\n      return SetInvalid("Stadium named Progress LR serialization exceeded its bound"), void();\n    json += "\\"";\n    if (std::string(phase).rfind("constructor_", 0) == 0)\n    {\n      json += ",\\"constructor_gprs\\":[";\n      const std::array<u32, 6> indices = {3, 13, 27, 28, 30, 31};\n      for (size_t i = 0; i < indices.size(); ++i)\n      {\n        if (i) json += ",";\n        json += std::to_string(state->gpr[indices[i]]);\n      }\n      json += "]";\n    }\n    if (std::string(phase).rfind("sss_position_", 0) == 0)\n    {\n      json += ",\\"position_call\\":" + std::to_string(stadium_sss_position.id);\n      const std::array<std::pair<const char*, u32>, 4> owners = {{{"cursor_gobj", stadium_sss_position.gobj},\n          {"cursor_jobj", stadium_sss_position.jobj}, {"cursor_proc", stadium_sss_position.proc},\n          {"frame_sp", stadium_sss_position.frame_sp}}};\n      for (const auto& owner : owners)\n      {\n        json += ",\\"" + std::string(owner.first) + "\\":\\"0x";\n        if (!AppendHex(&json, owner.second, 8))\n          return SetInvalid("Stadium position owner serialization exceeded its bound"), void();\n        json += "\\"";\n      }\n      json += ",\\"entry_cursor_state\\":" + std::to_string(stadium_sss_position.entry_cursor_state);\n      if (std::string(phase) == "sss_position_end" && stadium_sss_position.cursor_seen)\n        json += ",\\"end_row_index\\":" + std::to_string(state->gpr[30]);\n      else\n        json += ",\\"end_row_index\\":null";\n      json += std::string(",\\"cursor_observed\\":") + (stadium_sss_position.cursor_seen ? "true" : "false") +\n              ",\\"target_observed\\":" + (stadium_sss_position.target_seen ? "true" : "false");\n    }\n    if (std::string(phase) == "go_after")\n      json += ",\\"callsite_pc\\":\\"0x8016b820\\",\\"callsite_word\\":\\"0x48068821\\"";\n    if (std::string(phase) == "go_after")\n      json += ",\\"state_snapshot_schema\\":\\"stadium_idle_state_v1\\",\\"sis_table_entry_relationship_observed\\":false,\\"dynamic_title_text_bytes_observed\\":false";\n    json += ",\\"source_tick\\":" + std::to_string(source_tick) +\n            ",\\"draw_ordinal\\":" + std::to_string(draw_ordinal) + ",\\"slices\\":[";\n    for (size_t index = 0; index < slice_count; ++index)\n    {\n      const SliceRef& slice = slices[index];\n      if (index)\n        json += ",";\n      json += "{\\"tag\\":" + std::to_string(static_cast<u16>(slice.tag)) +\n              ",\\"flags\\":" + std::to_string(slice.flags) +\n              ",\\"address\\":" + std::to_string(slice.address) +\n              ",\\"size\\":" + std::to_string(slice.size) + ",\\"hex\\":\\"";\n      if (!AppendHexBytes(&json, raw.data() + slice.offset, slice.size))\n        return SetInvalid("Stadium named Progress slices exceed their JSON bound"), void();\n      json += "\\"}";\n    }\n    json += "]}";\n    if (json.size() > RING_PAYLOAD)\n      return SetInvalid("Stadium named Progress exceeds its bounded payload"), void();\n    PushJson(Event::Progress, json, pc, source_tick, draw_ordinal);\n  }\n\n'

LEGACY_CAPTURE_BOUNDARY = 'static bool IsCaptureBoundary(u32 guest_pc)\n{\n  switch (guest_pc)\n  {\n  case 0x8034DD8C:\n  case 0x80377584:\n  case 0x800693A8:\n  case 0x8016E934:\n  case 0x8016E9C4:\n  case 0x80390EB4:\n  case 0x80390FC0:\n  case 0x80391040:\n  case 0x8016E9C8:\n  case 0x8016EBBC:\n  case 0x8039157C:\n  case 0x801A4B70:\n  case 0x8026688C:\n  case 0x802669F0:\n  case 0x80266D70:\n  case 0x8025A998:\n  case 0x8025BBD0:\n  case 0x801A5AF0:\n  case 0x80177368:\n  case 0x80177704:\n  case 0x801A5F64:\n  case 0x80179350:\n  case 0x801BFCFC:\n  case 0x802FEBE0:\n  case 0x802FED10:\n  case 0x801A6308:\n  case 0x801BFF7C:\n  case 0x8038E8EC:\n    return true;\n  default:\n    // Diagnostic CPU PCs are JIT boundaries only for the fully validated,\n    // opt-in companion configuration.  The normal observer boundary set and\n    // its disabled path remain unchanged.\n    return (Env("MWRC_STADIUM_GO_PREFIX") == "1" &&\n            Env("MWRC_STADIUM_CONSTRUCTOR_OWNER") == "1" &&\n            std::find(STADIUM_CONSTRUCTOR_PCS.begin(), STADIUM_CONSTRUCTOR_PCS.end(), guest_pc) != STADIUM_CONSTRUCTOR_PCS.end()) ||\n           (Env("MWRC_STADIUM_GO_PREFIX") == "1" &&\n            (guest_pc == STADIUM_GO_AFTER_PC || guest_pc == SSS_ENTER_RETURN ||\n             guest_pc == 0x8025a310 || guest_pc == 0x8025a4c0 ||\n             guest_pc == 0x8025a4e8 || guest_pc == 0x8025a548)) ||\n           (Env("MWRC_TRANSFORM_PREFIX") == "1" && guest_pc == SSS_ENTER_RETURN) ||\n           (SdInitRequested() && (guest_pc == 0x8016ebc0 || guest_pc == 0x8016ec24 ||\n            ((Env("MWRC_SD_MENU_PROBE") == "sd_prefix" || Env("MWRC_SD_MENU_PROBE") == "competitive_entry" || OrdinaryTimeoutRequested() || SparsePairRequested()) && guest_pc == 0x8025b84c))) ||\n           (CpuProbeEnabled() && FindCpuProbePoint(guest_pc) != nullptr &&\n            (CpuProbeEnvironment().rng_return_pc == 0 ||\n             CpuProbeEnvironment().rng_return_pc == guest_pc)) ||\n           (ItemProbeEnabled() && FindItemProbePoint(guest_pc) != nullptr);\n  }\n}\n'
