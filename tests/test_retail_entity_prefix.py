"""Declared synthetic controls for the actual v8 prefix encoder and parser."""
import copy
from contextlib import contextmanager
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from test_whole_session_replay import _candidate, _whole_session_cpu_setup, _raw_pad_snapshot, _pad_consume
import whole_session_replay as replay
from test_retail_recipe_scene_trace import _HARNESS

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def owned_scratch(prefix):
    path=Path(tempfile.mkdtemp(prefix=prefix))
    try:
        yield path
    except BaseException:
        print(f"Retained failed prefix control scratch: {path}", flush=True)
        raise
    else:
        shutil.rmtree(path)


def synthetic_prefix():
    capture = replay.capture_from_records(_candidate())
    raw = bytearray(_whole_session_cpu_setup(0))
    for slot, kind in enumerate((15,14,8,2)):
        raw[0x60+slot*0x24] = kind
    capture["setup_hex"] = raw.hex()
    capture["setup_hexes"] = [raw.hex()]
    frames = []
    for index in range(62):
        scene = 1 if index==0 else 2 if index==1 else 3
        frames.append({"index":index,"scene_code":scene,"source_tick":max(0,index-2),
                       "pads":["00"*11]*4})
    capture["frames"] = frames
    capture["spans"] = [{"scene":1,"first_frame":0,"last_frame":0},
                        {"scene":2,"first_frame":1,"last_frame":1},
                        {"scene":3,"first_frame":2,"last_frame":61}]
    observations=[]
    for tick in range(60):
        slices=[]
        def add(name,address,raw,flags=0):
            slices.append({"name":name,"address":address,"size":len(raw),"hex":bytes(raw).hex(),"flags":flags})
        add("scene_counter",0x80479d58,tick.to_bytes(4,"big"))
        add("match_clock",0x8046b6c4,b"\0"*4) # gameplay clock remains zero during intro.
        add("rng_pointer",0x804d5f94,(0x80510000).to_bytes(4,"big"))
        add("rng_value",0x80510000,b"\0"*4)
        add("pad_snapshot",0x804c1f84,_raw_pad_snapshot())
        for slot in range(4):add("fighter_stocks",0x80453080+slot*0xe90+0x8e,b"\x04",slot)
        for slot,entity,kind in ((0,0,15),(1,0,10),(1,1,11),(2,0,0),(3,0,1)):
            address=0x80600000+slot*0x10000+entity*0x4000
            head=bytearray(0x100);head[4:8]=kind.to_bytes(4,"big")
            flags=slot|(entity<<8)
            add("fighter_head",address,head,flags)
            add("fighter_input_anim",address+0x620,b"\0"*0x280,flags)
            add("fighter_damage_shield",address+0x1830,b"\0"*0x16c,flags)
        observations.append({"seq":tick+100,"event":"boundary","source_tick":tick,
                             "payload":{"boundary":"source_tick","whole_session":True,"match_index":0,"slices":slices}})
    original=_candidate()
    rows=copy.deepcopy(original[:2])
    for row in rows:
        row["payload"].update(match_count=1,entity_profile=replay.ENTITY_PREFIX_PROFILE)
    for boundary in ("css_enter","sss_enter","entry"):
        row=copy.deepcopy(next(r for r in original if r.get("payload",{}).get("boundary")==boundary and r["payload"]["match_index"]==0))
        if boundary=="entry":
            next(item for item in row["payload"]["slices"] if item["name"]=="match_setup")["hex"]=raw.hex()
        rows.append(row)
        if boundary!="entry":
            pad=_pad_consume(0,0,0);rows.append(pad)
            capture["frames"][0 if boundary=="css_enter" else 1]["pads"]=replay._consumed_ports(pad,0)
    rows.append({"event":"boundary","source_tick":0,
                 "payload":{"boundary":"setup","whole_session":True,"match_index":0}})
    for observation in observations:
        tick=observation["source_tick"]
        pad=_pad_consume(0,0,tick)
        queue=bytearray.fromhex(pad["payload"]["slices"][0]["hex"]);queue[0]=5
        pad["payload"]["slices"][0]["hex"]=queue.hex()
        # Actual consumed values, not a fabricated neutral replacement.
        capture["frames"][tick+2]["pads"]=replay._consumed_ports(pad,tick)
        rows.extend([pad,observation,
                     {"event":"boundary","source_tick":tick+1,"payload":{"boundary":"draw_enter","whole_session":True,"match_index":0}},
                     {"event":"boundary","source_tick":tick+1,"payload":{"boundary":"draw_return","whole_session":True,"match_index":0}}])
    draw_ordinal=0;in_match=False
    for row in rows:
        if row.get("payload",{}).get("boundary")=="setup":in_match=True
        if in_match:
            row["draw_ordinal"]=draw_ordinal
            if row.get("payload",{}).get("boundary")=="draw_return":draw_ordinal+=1
    rows.append({"event":"end","source_tick":60,"payload":{
        "status":"completed","natural":False,"diagnostic_prefix_complete":True,
        "whole_session_equivalent":False,"comparison_source_ticks":60,"observed_source_ticks":60}})
    for seq,row in enumerate(rows):row["seq"]=seq
    return capture,rows


class RetailEntityPrefixTests(unittest.TestCase):
    def test_actual_encoder_preserves_context_and_binds_source_not_gameplay_clock(self):
        capture,rows=synthetic_prefix()
        payload,metadata=replay.encode_v8_entity_prefix(capture,rows)
        self.assertEqual(replay.CONTEXT_HEADER.unpack_from(payload,replay.HEADER.size)[1],1)
        self.assertEqual(metadata["source_observations"],60)
        self.assertEqual((metadata["first_source_tick"],metadata["last_source_tick"]),(0,59))
        self.assertFalse(metadata["whole_session_equivalent"])
        start=replay.HEADER.size+replay.CONTEXT_HEADER.size
        expected=b"".join(bytes.fromhex(capture["first_css"][key]) for key in
                          ("game_rules_hex","save_data_hex","css_data_hex","ko_counts_hex"))
        self.assertEqual(payload[start:start+len(expected)],expected)
        with self.assertRaisesRegex(replay.WholeSessionReplayError,"end in Results"):
            replay.encode_v8(capture)

    def test_actual_encoder_rejects_wrong_scope_counters_entities_and_inputs(self):
        for mutation in (
            lambda c,r:r.pop(),
            lambda c,r:next(row for row in r if row.get("payload",{}).get("boundary")=="source_tick").update(source_tick=99),
            lambda c,r:next(row for row in r if row.get("payload",{}).get("boundary")=="source_tick")["payload"].update(match_index=1),
            lambda c,r:c["frames"][2].update(source_tick=99),
            lambda c,r:c["spans"][-1].update(scene=4),
            lambda c,r:c["setup_hexes"].append(c["setup_hex"]),
            lambda c,r:next(row for row in r if row.get("payload",{}).get("boundary")=="source_tick")["payload"]["slices"].pop(),
        ):
            c,r=synthetic_prefix();mutation(c,r)
            with self.assertRaises((replay.WholeSessionReplayError,ValueError)):
                replay.encode_v8_entity_prefix(c,r)
        for offset,value in ((0x60,8),(0x61,0),(0x62,3),(0x63,1),(0x6f,8),(0,0)):
            c,r=synthetic_prefix();raw=bytearray.fromhex(c["setup_hex"]);raw[offset]=value
            c["setup_hex"]=raw.hex();c["setup_hexes"]=[raw.hex()]
            with self.assertRaises(ValueError):replay.encode_v8_entity_prefix(c,r)

    def test_prefix_identity_footer_and_context_are_independent_admission(self):
        for mutation in (
            lambda c,r:r[-1]["payload"].update(natural=True),
            lambda c,r:r[-1]["payload"].update(diagnostic_prefix_complete=False),
            lambda c,r:r[-1]["payload"].update(whole_session_equivalent=True),
            lambda c,r:r[-1]["payload"].update(observed_source_ticks=59),
            lambda c,r:r[0]["payload"].update(entity_profile="unknown"),
            lambda c,r:r[1]["payload"].update(match_count=3),
            lambda c,r:c["first_css"].update(rng=99),
            lambda c,r:c["frames"][0].update(pads=["00"*11]*4),
        ):
            capture,rows=synthetic_prefix();mutation(capture,rows)
            with self.assertRaises(ValueError):replay.encode_v8_entity_prefix(capture,rows)
        capture,rows=synthetic_prefix()
        with self.assertRaisesRegex(ValueError,"naturally"):
            replay.capture_from_records(rows) # default whole-session admission unchanged.

    def test_first_qualifying_draw_retains_extras_and_rejects_browser_batching(self):
        capture,rows=synthetic_prefix()
        # Synthetic original batching: 59 single-tick draws, then five ticks
        # in one authored batch, so the passive final boundary covers 64.
        begin=next(i for i,row in enumerate(rows) if row.get("payload",{}).get("boundary")=="source_tick")-1
        first=rows[:begin+59*4]
        template=copy.deepcopy(rows[begin+59*4:begin+60*4])
        last=[]
        for tick in range(59,64):
            pair=copy.deepcopy(template[:2])
            for row in pair:row["source_tick"]=tick
            # Retain exact typed source counter, gameplay clock still zero.
            pair[1]["payload"]["slices"][0]["hex"]=tick.to_bytes(4,"big").hex()
            last.extend(pair)
        draw=copy.deepcopy(template[2:])
        for row in draw:row["source_tick"]=64
        end=copy.deepcopy(rows[-1]);end["source_tick"]=64;end["payload"]["observed_source_ticks"]=64
        interval=first+last+draw+[end]
        for seq,row in enumerate(interval):row["seq"]=seq
        selected=replay.entity_prefix_interval(interval)
        self.assertEqual(selected["observed_source_ticks"],64)
        self.assertEqual(selected["comparison_source_ticks"],60)
        self.assertEqual(selected["draw_batches"], [1]*59+[5])
        self.assertEqual(selected["retained_records"],interval[:-1])
        self.assertEqual(len(selected["pad_rows"]),64)
        # Preserve all captured inputs, not just the comparison cutoff.
        for tick in range(60,64):
            capture["frames"].append({**copy.deepcopy(capture["frames"][-1]),"index":tick+2,"source_tick":tick})
        capture["spans"][-1]["last_frame"]=65
        with self.assertRaisesRegex(ValueError,"batching is unsupported"):
            replay.encode_v8_entity_prefix(capture,interval)
        for mutation in (
            lambda r:r[begin+2].update(source_tick=9),
            lambda r:r[begin+1].update(source_tick=9),
            lambda r:r[begin+1].update(draw_ordinal=9),
            lambda r:r.insert(-1,{**copy.deepcopy(r[-2]),"seq":r[-1]["seq"]}),
            lambda r:r.pop(-2),
            lambda r:r[begin]["payload"]["slices"][0].update(hex="04"+r[begin]["payload"]["slices"][0]["hex"][2:]),
        ):
            bad=copy.deepcopy(interval);mutation(bad)
            with self.assertRaises(ValueError):replay.entity_prefix_interval(bad)

    def test_actual_final_draw_block_and_completion_policy(self):
        compiler=shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler)
        source=(ROOT/"src/gameplay_menu_browser.cpp").read_text()
        block=source[source.index("   if(replay&&!replay_final_draw&&replay_cursor==replay->frames.size()&&"):source.index("   if(drew_source&&running)")]
        harness=r'''
#include "gameplay_replay_completion_policy.hpp"
#include <cassert>
#include <vector>
#include <stdexcept>
#include <string>
struct Recipe { bool diagnostic_entity_prefix=true, diagnostic_active_entity_prefix=false; std::vector<int> frames=std::vector<int>(62); bool whole_session(){return true;} unsigned diagnostic_source_observations(){return 60;} };
struct Clock { bool pending(){return true;} void reset(){} };
int observed_replay_scene(){return 3;}
int melee_web_match_rules_publish_result(){throw std::runtime_error("prefix published terminal");}
int melee_web_match_rules_outcome(int*){throw std::runtime_error("prefix read terminal");}
bool run(unsigned ticks,bool owner,bool transition,bool complete,int outcome){
 Recipe recipe;auto* replay=&recipe;bool replay_final_draw=false,drew_source=true;
 unsigned replay_cursor=62;Clock source_frames,menu_clock;
 auto* match=owner?&recipe:nullptr;Recipe* results=nullptr;Recipe* prize=nullptr;
 bool pending=transition,replay_completed_now=false,running=true;
 bool replay_match_complete=complete;int replay_outcome=outcome,replay_winner=-1;
 std::string message;melee_web::ReplayCompletionState replay_completion;
 melee_web::DiagnosticPrefixProgress replay_prefix_progress;
 for(unsigned tick=0;tick<ticks;++tick)assert(replay_prefix_progress.observe(tick,tick+1));
 auto check=[](bool condition,const char* error){if(!condition)throw std::runtime_error(error);};
'''+block+r'''
 return replay_completed_now&&replay_final_draw&&!running;
}
int main(){assert(run(60,true,false,false,0));
 for(auto args: {0,1,2,3,4}){bool rejected=false;try{
  run(args==0?59:60,args!=1,args==2,args==3,args==4?1:0);
 }catch(const std::runtime_error&){rejected=true;}assert(rejected);}}
'''
        with owned_scratch("prefix-final-draw-") as tmp:
            tmp=Path(tmp);(tmp/"h.cpp").write_text(harness)
            ran=subprocess.run([compiler,"-std=c++20",f"-I{ROOT/'src'}",str(tmp/"h.cpp"),"-o",str(tmp/"test")],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            ran=subprocess.run([str(tmp/"test")],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_actual_browser_report_waits_for_teardown(self):
        node=shutil.which("node")
        self.assertIsNotNone(node)
        ran=subprocess.run([node,str(ROOT/"tests/retail_entity_prefix_test.mjs")],capture_output=True,text=True,timeout=10)
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_actual_cpp_parser_with_pinned_headers(self):
        # Read-only existing generated headers may be explicitly supplied by
        # the coordinating owner; no build/bootstrap or stale library is used.
        pinned=Path(os.environ.get("MELEE_WEB_TEST_PINNED_SOURCE_ROOT",ROOT))
        generated=pinned/"build/gameplay-source/src"
        if not (generated/"melee/ft/types.h").is_file():
            self.skipTest("pinned generated source headers required")
        compiler=shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler)
        extra=r'''
static void entity_prefix_checks(){
 auto bytes=recipe_fixture(8);
 bytes[23]=1; // context flag, unchanged v8 byte envelope.
 const size_t setup=20+kRetailReplayContextHeaderBytes+kRetailReplayContextBytes;
 const uint8_t roster[]={15,14,8,2};
 for(unsigned s=0;s<4;++s)bytes[setup+0x130+s]=roster[s];
 size_t spans=bytes.size()-2-3*kRetailReplaySpanBytes;
 bytes[spans+2+kRetailReplaySpanBytes]=kRetailReplaySss;
 bytes[spans+2+2*kRetailReplaySpanBytes]=kRetailReplayMatch;
 // Existing fixture has 5 PAD frames; extend only its final Match span.
 const unsigned old_count=5, new_count=62;
 bytes.insert(bytes.begin()+spans, (new_count-old_count)*44, 0);
 bytes[12]=0;bytes[13]=0;bytes[14]=0;bytes[15]=new_count;
 const size_t enlarged_spans=spans+(new_count-old_count)*44;
 const size_t last=enlarged_spans+2+2*kRetailReplaySpanBytes+8;
 auto set32=[&](size_t offset,unsigned value){bytes[offset]=value>>24;bytes[offset+1]=value>>16;bytes[offset+2]=value>>8;bytes[offset+3]=value;};
 set32(last,new_count-1);
 set32(enlarged_spans+2+kRetailReplaySpanBytes+8,1);
 set32(enlarged_spans+2+2*kRetailReplaySpanBytes+4,2);
 spans=enlarged_spans;
 auto prefix=read_retail_replay(bytes);
 if(!prefix.diagnostic_entity_prefix||prefix.spans.back().scene!=kRetailReplayMatch)throw std::runtime_error("missing profile");
 // This is only transport admission; actual source/draw proof is encoder-owned.
 auto old=bytes;old[23]=0;require_error(old,"end in Results","default_prefix_rejection");
 for(uint32_t v:{9U,10U}){auto other=recipe_fixture(v);other[23]=1;require_error(other,"context flags","wrong_version_prefix");}
 for(int mode=1;mode<=6;++mode){fake::prefix_override=mode;require_error(bytes,"CPU9 roster","wrong_prefix_player");}
 fake::prefix_override=7;require_error(bytes,"foreign active tail","wrong_prefix_tail");
 fake::prefix_override=0;
 auto wrong=bytes;wrong[23]=3;require_error(wrong,"context flags","unknown_prefix");
 auto active=bytes;active[23]=2;
 require_error(active,"batching is unsupported","active_prefix_short_interval");
 active.insert(active.begin()+spans,44,0);
 auto set_active32=[&](size_t offset,unsigned value){active[offset]=value>>24;active[offset+1]=value>>16;active[offset+2]=value>>8;active[offset+3]=value;};
 set_active32(12,new_count+1);set_active32(last+44,new_count);
 auto active_prefix=read_retail_replay(active);
 if(!active_prefix.diagnostic_active_entity_prefix||active_prefix.diagnostic_source_observations()!=61)
  throw std::runtime_error("active prefix did not bind actual interval");
 for(uint32_t v:{9U,10U}){auto other=recipe_fixture(v);other[23]=2;require_error(other,"context flags","wrong_version_active_prefix");}
 wrong=bytes;wrong[setup+0x130]=8;require_error(wrong,"CPU9 roster","wrong_prefix_roster");
 wrong=bytes;wrong[setup]^=1;require_error(wrong,"stock rules","wrong_prefix_rules");
 wrong=bytes;wrong[spans+2+kRetailReplaySpanBytes]=kRetailReplayCss;require_error(wrong,"CSS/SSS/Match","wrong_prefix_scene");
 std::cout<<"PREFIX parser controls passed\n";
}
'''
        harness=_HARNESS.replace("static void print_counts",extra+"\nstatic void print_counts").replace("    reader_format_checks();","    reader_format_checks();\n    entity_prefix_checks();")
        # The setup decoder stub provides wrong decoded fields to exercise
        # the actual parser's declared-configuration admission branches.
        harness=harness.replace("namespace fake {", "namespace fake { int prefix_override=0;", 1)
        harness=harness.replace("    return 1;\n}\nextern \"C\" void melee_web_retail_state", """    auto& p=out->start.players[1];
    if(fake::prefix_override==1)p.slot_type=Gm_PKind_Human;
    if(fake::prefix_override==2)p.cpu_kind=3;
    if(fake::prefix_override==3)p.cpu_level=8;
    if(fake::prefix_override==4)p.stocks=3;
    if(fake::prefix_override==5)p.color=0;
    if(fake::prefix_override==6)p.rumble_enabled=true;
    if(fake::prefix_override==7)out->start.players[4].slot_type=Gm_PKind_Cpu;
    return 1;
}
extern \"C\" void melee_web_retail_state""", 1)
        includes=[ROOT/"src",generated,pinned/".deps/aurora/include",pinned/".deps/melee/extern/dolphin/include"]
        with owned_scratch("retail-entity-prefix-") as tmp:
            tmp=Path(tmp);(tmp/"h.cpp").write_text(harness)
            flags=["-std=c++20","-DTARGET_PC","-DAURORA","-ffunction-sections","-fdata-sections","-ffp-contract=off",*[f"-I{p}" for p in includes]]
            for source,obj in ((ROOT/"src/gameplay_retail_recipe.cpp",tmp/"recipe.o"),(tmp/"h.cpp",tmp/"h.o")):
                ran=subprocess.run([compiler,*flags,"-c",str(source),"-o",str(obj)],capture_output=True,text=True,timeout=30)
                self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            gc="-Wl,-dead_strip" if __import__("sys").platform=="darwin" else "-Wl,--gc-sections"
            ran=subprocess.run([compiler,gc,str(tmp/"recipe.o"),str(tmp/"h.o"),"-o",str(tmp/"test")],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            ran=subprocess.run([str(tmp/"test")],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
            self.assertIn("PREFIX parser controls passed",ran.stdout)
