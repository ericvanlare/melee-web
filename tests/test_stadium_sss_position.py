"""Scoped synthetic source-position observations; no original gameplay claim."""
from pathlib import Path
import copy
import json
import shutil
import struct
import subprocess
import sys
import unittest
from owned_test_workspace import OwnedWorkspaceTests
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from stadium_go_prefix import (StadiumSssPositionObservations, StadiumGoPrefixError,
                               DIAGNOSTIC, EXPECTED_SETUP_RECEIPT_SHA256)


def source_slice(tag, address, data, flags=0):
    return dict(tag=tag,flags=flags,address=address,size=len(data),hex=data.hex())


def fixture(phase, call=1, cursor=True, target=False):
    g,j,p,sp,t = 0x80500000,0x80500100,0x80500200,0x81700000,0x80500300
    owner=bytearray(0x38);struct.pack_into(">I",owner,0x28,j)
    proc=bytearray(0x18);struct.pack_into(">II",proc,0x10,g,0x8025A310)
    tile=bytearray(0x1c);struct.pack_into(">I",tile,0,t);tile[8]=2;tile[11]=3
    tile[12:20]=bytes.fromhex("40466666402ccccd")
    slices=[source_slice(17,0x80479D30,b"\x02"+bytes(5)),source_slice(40,0x80500400,b"\x09"),
            source_slice(61,g,owner),source_slice(62,p,proc),source_slice(63,0x803F08C8,tile),
            source_slice(64,0x804D781C,struct.pack(">I",g)),
            source_slice(64,0x804D7838,struct.pack(">I",p),1),
            source_slice(65,0x804D6CAC,bytes((40,0,18 if target else 13,0 if cursor else 1))),source_slice(66,j+0x38,struct.pack(">3f",0,-13,0))]
    if cursor:slices.append(source_slice(59,sp+0x1c,struct.pack(">3f",0,-13,0)))
    if phase=="sss_position_target":slices.append(source_slice(60,sp+0x10,struct.pack(">3f",15,5,0)))
    pc,word={"sss_position_cursor":(0x8025A4C0,0x3C60803F),"sss_position_target":(0x8025A4E8,0xC0410010),"sss_position_end":(0x8025A548,0x8001003C)}[phase]
    payload=dict(phase=phase,diagnostic=DIAGNOSTIC,setup_receipt_sha256=EXPECTED_SETUP_RECEIPT_SHA256,
                 setup_profile_verified_by_observer=False,pc=f"0x{pc:08x}",word=f"0x{word:08x}",
                 lr=f"0x{pc:08x}",argument=f"0x{g:08x}",position_call=call,cursor_gobj=f"0x{g:08x}",
                 cursor_jobj=f"0x{j:08x}",cursor_proc=f"0x{p:08x}",frame_sp=f"0x{sp:08x}",
                 cursor_observed=cursor,target_observed=target,entry_cursor_state=0 if cursor else 1,
                 end_row_index=(18 if target else 13) if phase=="sss_position_end" and cursor else None,
                 source_tick=5,draw_ordinal=8,slices=slices)
    if phase=="sss_position_end":payload["lr"]="0x8025a4e8" if cursor else "0x8025a340"
    return dict(event="progress",seq=10,source_tick=5,draw_ordinal=8,payload=payload)


class StadiumSssPositionTests(OwnedWorkspaceTests):
    def test_complete_and_explicit_target_unobserved_calls(self):
        v=StadiumSssPositionObservations()
        self.assertFalse(v.accept(fixture("sss_position_cursor"),route_ready=True)["complete_tuple"])
        self.assertTrue(v.accept(fixture("sss_position_target",target=True),route_ready=True)["complete_tuple"])
        v.accept(fixture("sss_position_end",target=True),route_ready=True)
        v.accept(fixture("sss_position_cursor",call=2),route_ready=True)
        v.accept(fixture("sss_position_end",call=2),route_ready=True)
        v.accept(fixture("sss_position_end",call=3,cursor=False),route_ready=True)
        v.require_closed()

    def test_owner_addresses_extents_flags_and_finite_bits_are_strict(self):
        base=fixture("sss_position_cursor")
        def mutate(tag,field,value):
            r=copy.deepcopy(base);next(x for x in r["payload"]["slices"]if x["tag"]==tag)[field]=value;return r
        cases=[mutate(64,"hex","80500004"),mutate(63,"address",0x803F08AC),
               mutate(63,"hex",bytes(0x1c).hex()),mutate(59,"address",0x81700020),
               mutate(59,"hex",bytes.fromhex("7fc000000000000000000000").hex()),
               mutate(17,"flags",1),mutate(62,"hex",bytes(0x18).hex()),
               mutate(40,"hex","08")]
        for r in cases:
            with self.subTest(row=r):
                with self.assertRaises(StadiumGoPrefixError):StadiumSssPositionObservations().accept(r,route_ready=True)
        with self.assertRaises(StadiumGoPrefixError):StadiumSssPositionObservations().accept(base,route_ready=False)

    def test_no_cross_call_target_or_missing_epilogue(self):
        v=StadiumSssPositionObservations();v.accept(fixture("sss_position_cursor"),route_ready=True)
        with self.assertRaises(StadiumGoPrefixError):v.require_closed()
        for r in [fixture("sss_position_target",call=2,target=True),fixture("sss_position_end",target=True),
                  fixture("sss_position_cursor")]:
            with self.assertRaises(StadiumGoPrefixError):v.accept(r,route_ready=True)
        v.accept(fixture("sss_position_end"),route_ready=True)
        with self.assertRaises(StadiumGoPrefixError):v.accept(fixture("sss_position_target",target=True),route_ready=True)

    def test_full_prefix_admits_only_authored_position_phases_in_sss(self):
        from test_stadium_go_prefix import _fixture, _frame, observer_stream
        from stadium_go_prefix import validate_stadium_go_prefix
        def prefix(position_rows, at=10):
            data,status=_fixture();records=[];offset=0
            while offset<len(data):
                h=observer_stream.HEADER.unpack_from(data,offset)
                size=observer_stream.HEADER.size+h[8]
                records.append(data[offset:offset+size]);offset+=size
            extra=[]
            for row in position_rows:
                p=copy.deepcopy(row["payload"]);p["source_tick"]=40;p["draw_ordinal"]=0
                extra.append(_frame(4,0,json.dumps(p).encode(),pc=int(p["pc"],16),source_tick=40))
            records[at:at]=extra
            rebased=[]
            for seq,r in enumerate(records):
                h=list(observer_stream.HEADER.unpack(r[:observer_stream.HEADER.size]));h[3]=seq
                rebased.append(observer_stream.HEADER.pack(*h)+r[observer_stream.HEADER.size:])
            status.update(event_count=len(records),last_seq=len(records)-1)
            return b"".join(rebased),status
        good=[fixture("sss_position_cursor"),fixture("sss_position_target",target=True),
              fixture("sss_position_end",target=True)]
        scratch=self.new_workspace(ROOT,"sss-position-prefix-")
        def validate(rows,at=10):
            raw,status=prefix(rows,at);stream=scratch/(str(len(list(scratch.iterdir())))+".mwro")
            stream.write_bytes(raw);Path(str(stream)+".status.json").write_text(json.dumps(status))
            return validate_stadium_go_prefix(stream)
        self.assertEqual(validate(good)["decision"],"PASS_ORIGINAL_RAW_GO_PREFIX_ONLY")
        for rows,at in [(good,6),(good[:1],10)]:
            with self.assertRaises(StadiumGoPrefixError):validate(rows,at)
        unknown=copy.deepcopy(good);unknown[0]["payload"]["phase"]="sss_position_unknown"
        with self.assertRaises(StadiumGoPrefixError):validate(unknown)

    def test_exact_cpp_witness_and_serializer_with_fixture_memory(self):
        compiler=shutil.which("c++")
        if not compiler:self.skipTest("C++ compiler unavailable")
        source=(ROOT/"reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        methods=source[source.index("  bool StadiumSssPositionOwner"):source.index("  void ObserveStadiumMenuHook")]
        publish=source[source.index("  void PublishStadiumProgress"):source.index("  // Pinned fn_8025A310")]
        fields=source[source.index("  struct StadiumSssPositionCall"):source.index("  bool stadium_go_prefix_enabled = false;")]
        add_slice=source[source.index("  bool AddSlice("):source.index("  bool HasSingleSlice(")]
        enum=source[source.index("enum class SliceTag"):source.index("struct SliceRef")]
        # None of these exact production blocks can store through the guest API/state.
        for forbidden in ("Write_U", "WriteBytes", "state->gpr[3] =", "state->spr[8] ="):
            self.assertNotIn(forbidden,methods+publish+add_slice)
        self.assertIn('Env("MWRC_STADIUM_GO_PREFIX") == "1"',source[source.index("static bool IsCaptureBoundary"):])
        import re
        self.assertIsNone(re.search(r"state->(?:gpr|spr)\[[^\]]+\]\s*=(?!=)", methods+publish))
        prefix=r"""
#include <array>
#include <map>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <cassert>
#include <utility>
using u8=uint8_t;using u16=uint16_t;using u32=uint32_t;using u64=uint64_t;
namespace Core {struct System {};}
namespace PowerPC {struct PowerPCState {u32 gpr[32]{};u32 spr[16]{};};}
bool IsMem1Range(u32 p,size_t n){return p>=0x80000000 && uint64_t(p)+n<=0x81800000 && p%4==0;}
bool AppendHex(std::string* s,u32 v,size_t n){std::ostringstream x;x<<std::hex<<std::setfill('0')<<std::setw(n)<<v;*s+=x.str();return true;}
bool AppendHexBytes(std::string* s,const u8* p,size_t n){for(size_t i=0;i<n;++i)AppendHex(s,p[i],2);return true;}
enum class Event {Progress};enum class StadiumGoPrefixPhase {AwaitSssExit,Other};
"""
        middle=r"""
struct SliceRef {SliceTag tag;u16 flags;u32 address,size,offset;};
struct Fixture {
 std::map<u32,u8> mem;std::vector<std::string> published;bool invalid=false;
 std::array<u8,8192> raw{};std::array<SliceRef,64>slices{};size_t raw_size=0,slice_count=0;
 static constexpr size_t MAX_SLICES=64,MAX_RAW=8192,RING_PAYLOAD=16384;
 u32 draw_ordinal=8;std::string stadium_go_prefix_setup_receipt_sha256="e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda";
 StadiumGoPrefixPhase stadium_go_prefix_phase=StadiumGoPrefixPhase::AwaitSssExit;
 bool stadium_go_prefix_sss_ready=true,stadium_go_prefix_sss_exit_seen=false;
 bool ReadBytes(Core::System*,u32 p,size_t n,u8* out){for(size_t i=0;i<n;++i){auto it=mem.find(p+i);if(it==mem.end())return false;out[i]=it->second;}return true;}
 bool ReadU32(Core::System* s,u32 p,u32* v){u8 b[4];if(!ReadBytes(s,p,4,b))return false;*v=(u32(b[0])<<24)|(u32(b[1])<<16)|(u32(b[2])<<8)|b[3];return true;}
 void SetInvalid(const char*){invalid=true;stadium_sss_position={};}
 void PushJson(Event,const std::string& j,u32,u32,u32){published.push_back(j);}
 bool AddSceneKindSlice(Core::System* s){return AddSlice(s,SliceTag::SceneKind,0x80500400,1);}
 void bytes(u32 p,size_t n,u8 b=0){for(size_t i=0;i<n;++i)mem[p+i]=b;}
 void word(u32 p,u32 v){for(int i=0;i<4;++i)mem[p+i]=v>>(24-i*8);}
"""
        # Exact anchor words are read by the actual production method above.
        import re
        anchors=re.findall(r"\{(0x[0-9a-f]+), (0x[0-9a-f]+)\}",methods)
        setup="""void setup(){bytes(0x80500000,0x38);bytes(0x80500100,0x44);bytes(0x80500200,0x18);bytes(0x80500300,0x44);bytes(0x81700000,0x40);bytes(0x803f08c8,0x1c);bytes(0x80479d30,6);mem[0x80479d30]=2;word(0x80479d58,5);word(0x804d6720,0x80500400);mem[0x80500400]=9;word(0x804d781c,0x80500000);word(0x804d7838,0x80500200);word(0x80500210,0x80500000);word(0x80500214,0x8025a310);word(0x80500028,0x80500100);word(0x803f08c8,0x80500300);mem[0x803f08d0]=2;mem[0x803f08d3]=3;word(0x803f08d4,0x40466666);word(0x803f08d8,0x402ccccd);bytes(0x804d6cac,4);mem[0x804d6cae]=13;"""
        setup+=''.join('word('+a+','+b+');'for a,b in anchors)+"}\n};\n"
        main=r"""
int main(){Core::System sys;
 auto entry=[&](Fixture& f,PowerPC::PowerPCState& s){s.gpr[3]=0x80500000;s.gpr[1]=0x81700038;s.gpr[13]=0x804db6a0;s.spr[8]=0x80390e00;f.ObserveStadiumSssPosition(&sys,0x8025a310,&s);s.gpr[1]=0x81700000;s.gpr[31]=0x80500100;};
 auto cursor=[&](Fixture& f,PowerPC::PowerPCState& s){s.spr[8]=0x8025a4c0;f.ObserveStadiumSssPosition(&sys,0x8025a4c0,&s);};
 {Fixture f;f.setup();auto before=f.mem;PowerPC::PowerPCState s;entry(f,s);cursor(f,s);s.gpr[30]=18;s.gpr[31]=0x803f08c8;s.spr[8]=0x8025a4e8;f.ObserveStadiumSssPosition(&sys,0x8025a4e8,&s);f.mem[0x804d6cae]=18;before=f.mem;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(!f.invalid&&!f.stadium_sss_position.active&&f.mem==before&&f.published.size()==3);for(auto&j:f.published)std::cout<<j<<"\n";}
 {Fixture f;f.setup();auto before=f.mem;PowerPC::PowerPCState s;entry(f,s);cursor(f,s);s.gpr[30]=13;s.gpr[31]=0x803f083c;s.spr[8]=0x8025a4e8;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(!f.invalid&&f.mem==before&&f.published.size()==2);for(auto&j:f.published)std::cout<<j<<"\n";}
 for(int n=0;n<8;++n){Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);if(n==0)s.gpr[1]+=4;if(n==1)f.word(0x80500028,0x80500104);if(n==2)f.word(0x804d7838,0x80500204);if(n==3)f.word(0x803f08c8,0x80500304);if(n==4)s.spr[8]=0;if(n==5)f.word(0x8025a4bc,0);if(n==6)f.mem[0x80500400]=8;if(n==7)f.mem[0x803f08d3]=4;auto before=f.mem;if(n!=4)s.spr[8]=0x8025a4c0;f.ObserveStadiumSssPosition(&sys,0x8025a4c0,&s);assert(f.invalid&&!f.stadium_sss_position.active&&f.mem==before&&f.published.empty());}
 {Fixture f;f.setup();f.mem[0x804d6caf]=1;auto before=f.mem;PowerPC::PowerPCState s;entry(f,s);s.spr[8]=0x8025a340;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(!f.invalid&&!f.stadium_sss_position.active&&f.mem==before&&f.published.size()==1);for(auto&j:f.published)std::cout<<j<<"\n";}
 {Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);s.gpr[30]=18;s.gpr[31]=0x803f08c8;s.spr[8]=0x8025a4e8;f.ObserveStadiumSssPosition(&sys,0x8025a4e8,&s);assert(f.invalid&&!f.stadium_sss_position.active);}
 {Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);s.spr[8]=0x8025a340;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(f.invalid&&!f.stadium_sss_position.active&&f.published.empty());}
 {Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);cursor(f,s);s.gpr[30]=20;s.gpr[31]=0x803f0900;s.spr[8]=0x8025a4e8;f.mem[0x804d6cae]=20;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(f.invalid&&!f.stadium_sss_position.active);}
 {Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);f.word(0x804d6720,0x80500404);f.mem[0x80500404]=9;cursor(f,s);assert(f.invalid&&!f.stadium_sss_position.active&&f.published.empty());}
 {Fixture f;f.setup();PowerPC::PowerPCState s;entry(f,s);f.stadium_go_prefix_phase=StadiumGoPrefixPhase::Other;f.ObserveStadiumSssPosition(&sys,0x8025a548,&s);assert(!f.stadium_sss_position.active&&f.published.empty());}
}
"""
        work=self.new_workspace(ROOT,"sss-position-control-");cpp=work/"control.cpp";exe=work/"control"
        cpp.write_text(prefix+enum+middle+fields+add_slice+publish+methods+setup+main)
        build=subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror",str(cpp),"-o",str(exe)],capture_output=True,text=True,timeout=30)
        (work/"compile.stdout").write_text(build.stdout);(work/"compile.stderr").write_text(build.stderr)
        self.assertEqual(build.returncode,0,build.stderr)
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
        (work/"stdout.jsonl").write_text(run.stdout);(work/"stderr.log").write_text(run.stderr)
        self.assertEqual(run.returncode,0,run.stderr)
        records=[json.loads(line)for line in run.stdout.splitlines()]
        self.assertEqual(len(records),6)
        for group in (records[:3],records[3:5],records[5:]):
            v=StadiumSssPositionObservations()
            for p in group:v.accept(dict(event="progress",source_tick=p["source_tick"],draw_ordinal=p["draw_ordinal"],payload=p),route_ready=True)
            v.require_closed()


if __name__=="__main__":unittest.main()
