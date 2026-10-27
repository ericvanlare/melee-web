"""Synthetic actual-source selector/decoder controls, not an original recording."""
from pathlib import Path
import copy,json,os,sys,unittest,importlib.util
ROOT=Path(os.environ.get('MELEE_STADIUM_WHOLE_B_SOURCE_ROOT',Path(__file__).resolve().parents[1]))
BASE=Path(os.environ.get('MELEE_STADIUM_WHOLE_B_BASE_ROOT',ROOT))
sys.path[:0]=[str(ROOT/'tools'),str(BASE/'reference-capture/dolphin'),str(BASE/'scripts'),str(BASE/'tests')]
from owned_test_workspace import OwnedWorkspaceTests
from test_stadium_current_sss_exit import run
from stadium_go_prefix import StadiumConstructorObservations,StadiumGoPrefixError
from test_stadium_constructor_observations import witnesses

def cut(text,a,b):
    assert text.count(a)==text.count(b)==1,(a,b)
    return text[text.index(a):text.index(b)]

class WholeBObservationControls(OwnedWorkspaceTests):
    def test_actual_selected_state_and_mode_gate(self):
        source=(ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        boundary_start=source.index("enum class Boundary : u16")
        boundary=source[boundary_start:source.index("};",boundary_start)+2]
        state=boundary+cut(source,'struct StadiumWholeBObservation\n','enum class StadiumGoPrefixPhase : u8')
        timing_start=source.index('  bool selected_timing_enabled=')
        timing_state=source[timing_start:source.index(';',timing_start)+1]
        paired_init=source[source.index('    const std::string paired_idle_setting ='):
                            source.index('    whole_session_matches = WholeSessionMatchCount();')]
        mode=paired_init+cut(source,'    const std::string whole_b_setting =','    const std::string constructor_setting =')
        # Compare the exact current parent guard; applied-tree BASE self-reads.
        guard_start='    if (stadium_whole_feedback &&\n'
        guard_end='    const std::string whole_b_setting ='
        candidate=cut(source,guard_start,guard_end)
        original=(BASE/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp').read_text()
        # Old-base byte equality is external composition evidence; an applied tree self-reads.
        original_end=(guard_end if guard_end in original else '    const std::string constructor_setting =')
        first=original.index(guard_start);last=original.index(original_end,first)
        self.assertEqual(candidate,original[first:last])
        fixture=r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <iostream>
using u32=uint32_t;using u16=uint16_t;using u8=uint8_t;
'''+state+timing_state+r'''
std::map<std::string,std::string> env;
std::string Env(const char* key){return env[key];}
namespace InputStream{bool IsRecording(){return true;}bool IsReplaying(){return false;}}
struct CompactRngSettings{bool present=false,valid=false;std::string selection;};
const CompactRngSettings& CompactRngEnvironment(){static const CompactRngSettings value{};return value;}
bool whole_rng_returns=false,whole_replay=false;
// The prior parsed match-count is a modeled input; pair-profile validation and flag derivation below are actual source.
u32 whole_session_matches=3;
bool stadium_whole_paired_idle=false;
bool stadium_whole_feedback=false;std::string error;
StadiumWholeBObservation stadium_whole_b;StadiumWholeLongIdle stadium_whole_long_idle;
void SetInvalid(const char* text){error=text;}
bool actual_mode(){
'''+mode+r'''
 return true;
}
StadiumWholeBObservation setup(){StadiumWholeBObservation s;s.enabled=true;
 assert(s.SssExit(1,3,1)&&s.Entry(1,4,0x80480530));
 assert(s.Constructor(1,5,0x80480530));assert(s.Setup(1,5,0x80480530,5));return s;}
int main(){
 for(const std::string value:{"","1","0","2","true"})for(bool parent:{false,true}){
  env.clear();env["MWRC_STADIUM_WHOLE_B_STATE"]=value;stadium_whole_feedback=parent;stadium_whole_b={};error.clear();
  const bool pass=value.empty()||(value=="1"&&parent);assert(actual_mode()==pass);
  assert(pass==error.empty());assert(stadium_whole_b.enabled==(pass&&value=="1"));}
 env.clear();env["MWRC_STADIUM_WHOLE_PAIRED_IDLE"]="unknown";error.clear();
 assert(!actual_mode()&&error=="Stadium paired-idle profile identity differs");
 StadiumWholeBObservation off;
 assert(off.Complete()&&!off.Selected(1)&&off.SssExit(1,0,0)&&off.Entry(1,0,0)&&off.Setup(1,0,0,0)&&off.Close(1,0,0));
 assert(!off.Constructor(1,5,1)&&!off.Go(1,5,1,123)&&!off.Tick(1,5,1,123));
 auto s=setup();assert(!s.Complete());assert(s.Go(1,5,0x80480530,123)); // Actual GO is not inferred as84.
 assert(s.Tick(1,5,0x80480530,123)&&s.Tick(1,5,0x80480530,124));
 assert(s.Close(1,5,0x80480530)&&s.Complete()&&s.samples==2&&s.go_tick==123);
 assert(!s.Go(1,5,0x80480530,125)&&!s.Tick(1,5,0x80480530,125)&&!s.Close(1,5,0x80480530));
 for(unsigned index:{0U,2U}){StadiumWholeBObservation other;other.enabled=true;
  assert(!other.Selected(index)&&other.SssExit(index,3,1)&&other.Entry(index,4,123)&&other.Setup(index,5,123,5)&&other.Close(index,5,123));
  assert(other.phase==StadiumWholeBObservation::Phase::AwaitSssExit&&other.setup_owner==0&&!other.Complete());}
 for(int fault=0;fault<9;++fault){StadiumWholeBObservation bad;bad.enabled=true;
  if(fault==0){assert(!bad.Entry(1,4,1));continue;}
  if(fault==1){assert(!bad.SssExit(1,3,0));continue;}
  assert(bad.SssExit(1,3,1));if(fault==2){assert(!bad.SssExit(1,3,1));continue;}
  assert(bad.Entry(1,4,1));if(fault==3){assert(!bad.Entry(1,4,1));continue;}
  if(fault==4){assert(!bad.Constructor(1,4,1));continue;}
  if(fault==5){assert(!bad.Constructor(1,5,2));continue;}
  if(fault==6){assert(!bad.Setup(1,5,1,4));continue;}
  if(fault==7){assert(!bad.Go(1,5,1,99));continue;}
  assert(!bad.Close(1,5,1));}
 for(int fault=0;fault<8;++fault){auto bad=setup();assert(bad.Go(1,5,0x80480530,123));
  if(fault==0){assert(!bad.Go(1,5,0x80480530,123));continue;}
  if(fault==1){assert(!bad.Tick(1,5,0x80480530,124));continue;}
  if(fault==2){assert(!bad.Tick(1,4,0x80480530,123));continue;}
  if(fault==3){assert(!bad.Tick(1,5,0x80480534,123));continue;}
  if(fault==4){assert(!bad.Close(1,5,0x80480530));continue;}
  assert(bad.Tick(1,5,0x80480530,123));
  if(fault==5)assert(!bad.Tick(1,5,0x80480530,123));
  if(fault==6)assert(!bad.Tick(1,5,0x80480530,125));
  if(fault==7)assert(!bad.Close(1,5,0x80480534));}
 std::cout<<"PASS actual selected-B state/local opt-in with modeled parent validation; no guest/capture equivalence\n";
}
'''
        run(fixture,self.new_workspace(BASE,'stadium-whole-b-state-'))

    def test_actual_whole_receiver_selected_progress_and_legacy_refusal(self):
        # Model only previously checked Entry/announcement state; actual accept dispatch,
        # constructor decoder, GO instruction/roster/RNG and typed state validators run.
        spec=importlib.util.spec_from_file_location('whole_b_candidate_driver',ROOT/'scripts/capture_sd_reference_prefix.py')
        driver=importlib.util.module_from_spec(spec);spec.loader.exec_module(driver)
        from test_stadium_go_prefix import _progress_payload
        from test_stadium_idle_state_contract import _record
        receipt='c9fbd6a5585cf596811b3b824d6e112c5f104cfbbd457544dbf855d1b871a777'
        def receiver(enabled=True,index=1):
            r=object.__new__(driver.StadiumWholeFeedbackReceiver)
            r.ended=False;r.seq=100;r.announcements=['handshake','start'];r.match_index=index
            r.receipt=receipt;r.stadium_whole_b_state=enabled;r.stadium_whole_long_idle=False;r.b_entry_seen=True
            r.stadium_whole_stockout=False
            # Modeled already-admitted legacy profile; actual dispatch/validators remain live.
            r.progress_scope='stadium_whole3_feedback_v1';r.stadium_whole_paired_idle=False;r.b_state_matches=(1,)
            r.b_constructor=StadiumConstructorObservations();r.setup_seen=False;r.b_go=None
            return r
        def selected(row):
            row=copy.deepcopy(row);row['payload'].update(diagnostic='stadium_whole3_feedback_v1',
                setup_receipt_sha256=receipt,whole_session=True,match_index=1,stadium_b_state_observation=True)
            return row
        rows=[selected(row) for row in witnesses()]
        r=receiver()
        for row in rows:r.accept(row)
        r.setup_seen=True # Modeled already validated Setup; not a fake source service.
        state=_record('GO');go=copy.deepcopy(state)
        go['seq']=105;go['source_tick']=123;go['payload']=_progress_payload('go_after',source_tick=123)
        go['payload']['slices']+=state['payload']['slices']
        for key in ('state_snapshot_schema','sis_table_entry_relationship_observed','dynamic_title_text_bytes_observed'):
            go['payload'][key]=state['payload'][key]
        go=selected(go);r.accept(go)
        self.assertEqual(r.b_go['source_tick'],123)
        duplicate=copy.deepcopy(go);duplicate['seq']=106
        with self.assertRaises(ValueError):r.accept(duplicate)
        for enabled,index in ((False,1),(True,0),(True,2)):
            r=receiver(enabled,index);bad=copy.deepcopy(rows[0]);bad['payload']['match_index']=index
            with self.assertRaises(ValueError):r.accept(bad)
        for key,value in [('phase','foreign'),('stadium_b_state_observation',False),('setup_receipt_sha256','0'*64)]:
            r=receiver();bad=copy.deepcopy(rows[0]);bad['payload'][key]=value
            with self.assertRaises(ValueError):r.accept(bad)
        for field,value in [('lr','0x00000000'),('callsite_word','0x00000000'),('match_index',0)]:
            r=receiver()
            for row in rows:r.accept(row)
            r.setup_seen=True;bad=copy.deepcopy(go);bad['payload'][field]=value
            with self.assertRaises(ValueError):r.accept(bad)

    def test_existing_constructor_decoder_requires_explicit_match1_scope(self):
        rows=witnesses();receipt='c9fbd6a5585cf596811b3b824d6e112c5f104cfbbd457544dbf855d1b871a777'
        for row in rows:
            row['payload'].update(diagnostic='stadium_whole3_feedback_v1',setup_receipt_sha256=receipt,
                whole_session=True,match_index=1,stadium_b_state_observation=True)
        def validate(items,**kw):
            tracker=StadiumConstructorObservations()
            for row in items:tracker.accept(row,route_ready=True,scope='stadium_whole3_feedback_v1',expected_receipt=receipt,match_index=1,**kw)
            return tracker.finish(include_state_projection=True)
        result=validate(rows)
        self.assertEqual(result['state_projection']['constructor_entry_count'],1)
        self.assertFalse(result['registration_event_observed']);self.assertFalse(result['lifetime_pairing_observed'])
        for mutate in [lambda p:p.update(match_index=0),lambda p:p.update(match_index=2),
            lambda p:p.update(match_index=True),lambda p:p.update(whole_session=False),
            lambda p:p.update(stadium_b_state_observation=False),lambda p:p.update(diagnostic='stadium_go_prefix_v3'),
            lambda p:p.update(setup_receipt_sha256='0'*64),lambda p:p.update(word='0x00000000')]:
            bad=copy.deepcopy(rows);mutate(bad[0]['payload'])
            with self.assertRaises(StadiumGoPrefixError):validate(bad)
        for bad in [rows[1:],rows[:-1],rows[:2]+[rows[2]]+rows[2:],rows[:1]+[rows[2],rows[1]]+rows[3:]]:
            with self.assertRaises(StadiumGoPrefixError):validate(bad)
        with self.assertRaises(StadiumGoPrefixError):
            StadiumConstructorObservations().accept(rows[0],route_ready=True)
        # Original legacy helper still validates the sealed scope without whole fields.
        legacy=StadiumConstructorObservations()
        for row in witnesses():legacy.accept(row,route_ready=True)
        self.assertTrue(legacy.finish()['source_sequences'])

if __name__=='__main__':unittest.main()
