"""Synthetic C++/wire controls only; not observed original or native parity."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('compact_live',ROOT/'tools/stadium_rng_compact_live.py')
ledger=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(ledger)
SOURCE=ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp'
HEADER=SOURCE.with_name('ReferenceCompactRngLedger.h')


def function(text, name):
    start=text.index('  void '+name+'(')
    begin=text.index('{',start);depth=1;end=begin+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]


class CompactLiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=shutil.which('clang++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required for actual private wire leaf')
        cls.scratch=tempfile.TemporaryDirectory(prefix='compact-live-')
        cls.preserve_scratch=False
        cls.addClassCleanup(lambda: None if cls.preserve_scratch else cls.scratch.cleanup())
        scratch=Path(cls.scratch.name)
        cls.binary=scratch/'control'
        prefix='''#include "ReferenceCompactRngLedger.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdint>
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t; using u64=std::uint64_t;
namespace Core {struct System {};}
namespace PowerPC {struct PowerPCState {std::array<u32,32> spr{};};}
using namespace ReferenceCapture;
enum class Boundary {DrawReturn,SourceTick,VsExit,SceneTeardown};
struct CloseControl {
 struct Window {u32 match=0,go_tick=84,target=3584,last_source=84,tail=0,count=0,close_tick=0,
 setup=0x80400000,seed_pointer=0x804d5f90,seed=1;
 u64 go_sequence=100,close_sequence=0,containing_sequence=0,target_sequence=0;
 bool closed=false,containing_seen=false,target_reached=false;};
 bool compact_enabled=true,invalid=false,match_active=true,setup_ready=true; int compact_active=0;
 u32 supplied_seed_pointer=0x804d5f90;unsigned seed_reads=0;
 u64 next_sequence=110;u32 draw_ordinal=0;
 std::array<std::array<u32,2>,4> fighter_entity_pointers{},fighter_entity_kinds{};
 std::array<u8,4> fighter_entity_count{};
 std::array<std::array<u8,64>,4096> compact_ring{};
 std::atomic<u64> compact_head{0},compact_tail{0};
 bool ReadU32(Core::System*,u32 p,u32* out) {
  if(p==0x80479d58) *out=85;
  else if(p==0x80380524) *out=0x4e800020;
  else if(p==0x804d5f94) *out=supplied_seed_pointer;
  else if(p==0x804d5f90) {*out=214013+2531011;++seed_reads;}
  else if(p==0x804d781c||p==0x804d7838) *out=0;
  else return false;
  return true;
 }
 // Only explicit outside-proc leaf reads are modeled. Scheduled-owner memory
 // must fail closed if reached; this fixture is not a game/lifetime simulation.
 bool ReadMem1(Core::System*,u32,std::size_t,u8*) {return false;}
 bool IsMem1Range(u32 p,std::size_t n) {return CompactRng::Mem1(p)&&n<=0x100;}
 bool ReadFighterSourceSlot(Core::System*,u32,u8*) {return false;}
 static u32 ReadBE32(const u8* b) {return(u32(b[0])<<24)|(u32(b[1])<<16)|(u32(b[2])<<8)|b[3];}
 u32 match_index=0,setup_pointer=0x80400000;
 std::array<Window,2> compact_windows{};
 CompactRng::Budget compact_budget;
 std::atomic<unsigned> compact_closed_mask{0};
 void SetInvalid(const char*) { invalid=true; }
'''
        # Actual production close branch; only fields/error leaf are modeled.
        code=prefix+function(SOURCE.read_text(),'RecordCompact')+'\n'+function(SOURCE.read_text(),'CompactBoundaryClosed')+'''\n};
int main() {
 CompactRng::Budget budget;
 for(unsigned i=0;i<CompactRng::MAX_LIFETIME_EVENTS;++i) assert(budget.Accept(0));
 assert(!budget.Accept(0));
 for(unsigned i=budget.total;i<CompactRng::MAX_EVENTS;++i) assert(budget.Accept(1));
 assert(!budget.Accept(1));assert(!budget.Accept(2));
 assert(8+CompactRng::MAX_HEADER_BYTES+std::size_t(CompactRng::MAX_EVENTS)*64+176<=CompactRng::MAX_BYTES);
 assert(CompactRng::FighterIdentity(0x80001000,0x80002000,0x80001000,0,0,0x80002000,0));
 assert(!CompactRng::FighterIdentity(0x80001000,0x80002000,0x80001004,0,0,0x80002000,0));
 assert(!CompactRng::FighterIdentity(0x80001000,0x80002000,0x80001000,0,0,0x80002004,0));
 assert(!CompactRng::FighterIdentity(0x80001000,0x80002000,0x80001000,4,0,0x80002000,0));
 Core::System system;PowerPC::PowerPCState state;state.spr[8]=0x80380000;
 CloseControl pointer_ok;pointer_ok.RecordCompact(&system,0x80380524,&state);
 assert(!pointer_ok.invalid&&pointer_ok.compact_head==1&&pointer_ok.seed_reads==1);
 CloseControl switched;switched.supplied_seed_pointer=0x804d6000;
 switched.RecordCompact(&system,0x80380524,&state);
 assert(switched.invalid&&switched.compact_head==0&&switched.seed_reads==0);
 CloseControl close;close.compact_budget.lifetime[0]=4;
 for(u32 i=0;i<3500;++i) {
  close.CompactBoundaryClosed(Boundary::SourceTick,84+i,100+2*i);
  if(i<3499) close.CompactBoundaryClosed(Boundary::DrawReturn,85+i,101+2*i);
 }
 // Same raw3584 counter can precede the actual target SourceTick in a draw.
 close.CompactBoundaryClosed(Boundary::DrawReturn,3584,7099);
 assert(!close.invalid&&close.compact_active==0&&!close.compact_windows[0].target_reached);
 close.CompactBoundaryClosed(Boundary::SourceTick,3584,7100);
 assert(!close.invalid&&close.compact_windows[0].target_reached);
 close.CompactBoundaryClosed(Boundary::DrawReturn,3585,9999);
 assert(!close.invalid&&close.compact_windows[0].closed&&close.compact_active==-1&&close.compact_closed_mask==1);
 CloseControl tail;tail.compact_budget.lifetime[0]=4;
 for(u32 i=0;i<=3502;++i) {
  tail.CompactBoundaryClosed(Boundary::SourceTick,84+i,100+2*i);
  if(i<3500) tail.CompactBoundaryClosed(Boundary::DrawReturn,85+i,101+2*i);
 }
 tail.CompactBoundaryClosed(Boundary::DrawReturn,3587,9999);
 assert(!tail.invalid&&tail.compact_windows[0].tail==2&&tail.compact_windows[0].closed);
 CloseControl missing;
 for(u32 i=0;i<=3505&&!missing.invalid;++i) {
  missing.CompactBoundaryClosed(Boundary::SourceTick,84+i,100+2*i);
  if(i<3500) missing.CompactBoundaryClosed(Boundary::DrawReturn,85+i,101+2*i);
 }
 assert(missing.invalid&&!missing.compact_windows[0].closed);
 CloseControl early;early.CompactBoundaryClosed(Boundary::VsExit,90,110);assert(early.invalid&&!early.compact_windows[0].closed);
 CloseControl foreign;foreign.setup_pointer+=4;foreign.CompactBoundaryClosed(Boundary::DrawReturn,3585,9999);assert(foreign.invalid);
 CloseControl gap;gap.CompactBoundaryClosed(Boundary::SourceTick,85,101);assert(gap.invalid);
 std::uint32_t seed=1;
 for(unsigned i=0;i<4;++i) {
  CompactRng::Record r;r.site=i%3+1;r.status=1;r.tick=85+i;r.match=0;r.ordinal=i;
  r.sequence=110+i;r.draw=i;r.lr=0x80380000;r.seed_pointer=0x804d5f90;
  seed=214013*seed+2531011;r.seed=seed;r.gobj=0x80001000;r.proc=0x80002000;
  r.callback=i==0?0x801d1390:i==1?0x8005c9a4:i==2?0x8006a360:0x80000004;
  r.symbol=CompactRng::Symbol(r.callback);
  if(i==2) {r.classifier=4;r.p_link=8;r.s_link=1;r.slot=0;r.entity=0;r.fighter_kind=0;}
  std::array<std::uint8_t,64> bytes{};assert(CompactRng::Encode(r,&bytes));
  if(i==2) {auto bad=r;bad.slot=4;assert(!CompactRng::Encode(bad,&bytes));assert(CompactRng::Encode(r,&bytes));}
  assert(std::fwrite(bytes.data(),1,bytes.size(),stdout)==64);
 }
 return 0;
}
'''
        src=scratch/'control.cpp';src.write_text(code)
        built=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(HEADER.parent),str(src),'-o',str(cls.binary)],capture_output=True)
        (scratch/'compile.stdout').write_bytes(built.stdout);(scratch/'compile.stderr').write_bytes(built.stderr)
        if built.returncode:
            # Failed scratch survives class cleanup and its path is reported.
            cls.preserve_scratch=True
            cls.scratch._finalizer.detach()
            raise AssertionError(f'compiler exit{built.returncode}; retained {scratch}: '+built.stderr.decode(errors='replace'))
        run=subprocess.run([str(cls.binary)],capture_output=True)
        (scratch/'run.stderr').write_bytes(run.stderr)
        if run.returncode:
            cls.preserve_scratch=True
            cls.scratch._finalizer.detach()
            raise AssertionError(f'control exit{run.returncode}; retained {scratch}: '+run.stderr.decode(errors='replace'))
        cls.rows=run.stdout
        if len(cls.rows)!=4*64:
            cls.preserve_scratch=True
            cls.scratch._finalizer.detach()
            raise AssertionError('actual C++ encoder geometry differs')

    def tearDown(self):
        result=self._outcome.result
        if any(case.__class__ is self.__class__ for case,_ in result.failures+result.errors):
            self.__class__.preserve_scratch=True
            self.__class__.scratch._finalizer.detach()

    def blob(self, rows=None, *, complete=1, window=None, header=None):
        h=dict(schema='melee-web-live-compact-rng-ledger',version=3,row_bytes=64,
            dol_sha256=ledger.DOL,capture_id='synthetic-capture',sequence_id='synthetic-sequence',
            matches=[0],first_offset=1,last_offset=3500,site_pcs=ledger.PCS,
            source_schedule_required_separately=True,lifecycle_parity=False)
        if header:h.update(header)
        raw=json.dumps(h,separators=(',',':')).encode();body=self.rows if rows is None else rows
        w=(0,84,100,3584,3584,len(body)//64,3585,9999,0x804d5f90,1,101,7100) if window is None else window
        prefix=struct.pack('>4sHH',b'MWRL',3,len(raw))+raw+body
        tail=struct.pack('>4sIII',b'END!',len(body)//64,1,complete)+ledger.WINDOW.pack(*w)+bytes(64)
        return prefix+tail+hashlib.sha256(prefix+tail).digest()

    def decode(self,blob):
        return ledger.decode_live_compact(blob,capture_id='synthetic-capture',
            sequence_id='synthetic-sequence',dol_sha256=ledger.DOL,
            primary_status=dict(state='completed',completed=True,invalid=False,error=None,last_seq=10000))

    def test_actual_cpp_encoder_and_close_contract_roundtrip(self):
        result=self.decode(self.blob())
        self.assertEqual(result['record_count'],4)
        self.assertEqual(result['unresolved_owner_events'],1)
        self.assertFalse(result['owner_comparison_ready'])
        self.assertEqual(result['records'][2]['owner']['source_slot'],0)
        self.assertEqual(result['records'][2]['owner']['entity'],0)
        self.assertEqual(result['records'][2]['primitive'],'HSD_Rand')
        self.assertFalse(result['source_schedule_verified'])
        self.assertFalse(result['lifecycle_parity'])
        self.assertFalse(result['C3'])

    def test_partial_overflow_and_primary_failure_refuse(self):
        for blob in (self.blob()[:-1],self.blob(complete=0)):
            with self.assertRaises(ledger.LiveLedgerError):self.decode(blob)
        with self.assertRaisesRegex(ledger.LiveLedgerError,'byte bound'):
            self.decode(bytes(ledger.MAX_BYTES+1))
        with self.assertRaisesRegex(ledger.LiveLedgerError,'primary stream'):
            ledger.decode_live_compact(self.blob(),capture_id='synthetic-capture',
                sequence_id='synthetic-sequence',dol_sha256=ledger.DOL,
                primary_status=dict(state='interrupted',completed=False,invalid=False,error=None,last_seq=10000))
        self.assertEqual(ledger.MAX_EVENTS,262077)
        self.assertLessEqual(8+4096+ledger.MAX_EVENTS*64+176,ledger.MAX_BYTES)

    def test_wrong_owner_and_seed_gap_refuse_even_rehashed(self):
        for row_index,field,value,message in ((2,3,4,'fighter owner'),(1,11,0,'seed discontinuity'),
                                             (3,2,4,'callback/pointers'),(1,6,0,'encoding/order')):
            rows=bytearray(self.rows)
            values=list(ledger.ROW.unpack_from(rows,row_index*64));values[field]=value
            ledger.ROW.pack_into(rows,row_index*64,*values)
            with self.subTest(field=field),self.assertRaisesRegex(ledger.LiveLedgerError,message):
                self.decode(self.blob(bytes(rows)))

    def test_pointer_switch_and_natural_tail_controls(self):
        rows=bytearray(self.rows)
        values=list(ledger.ROW.unpack_from(rows,64));values[10]=0x804d6000
        ledger.ROW.pack_into(rows,64,*values)
        with self.assertRaisesRegex(ledger.LiveLedgerError,'seed owner differs'):
            self.decode(self.blob(bytes(rows)))
        tail=self.decode(self.blob(window=(0,84,100,3584,3586,4,3587,9999,0x804d5f90,1,101,7100)))
        self.assertEqual(tail['windows'][0]['natural_tail_ticks'],2)
        self.assertEqual(tail['windows'][0]['target_source_sequence'],7100)
        with self.assertRaisesRegex(ledger.LiveLedgerError,'GO/close'):
            self.decode(self.blob(window=(0,84,100,3584,3589,4,3590,9999,0x804d5f90,1,101,7100)))
        with self.assertRaisesRegex(ledger.LiveLedgerError,'GO/close'):
            self.decode(self.blob(window=(0,84,100,3584,3584,4,3585,7100,0x804d5f90,1,101,7100)))

    def test_wrong_identity_sites_window_and_close_refuse(self):
        for header in (dict(site_pcs=ledger.PCS[:2]),dict(capture_id='foreign'),dict(last_offset=3499)):
            with self.assertRaises(ledger.LiveLedgerError):self.decode(self.blob(header=header))
        with self.assertRaisesRegex(ledger.LiveLedgerError,'GO/close'):
            self.decode(self.blob(window=(0,84,100,3584,3584,4,3584,9999,0x804d5f90,1,101,7100)))

if __name__=='__main__':unittest.main()
