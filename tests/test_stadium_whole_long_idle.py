"""Actual extracted source leaf and retained-raw composition; no new capture."""
import copy
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(os.environ.get("MELEE_LONG_IDLE_SOURCE_ROOT", Path(__file__).resolve().parents[1]))
SUPPORT = Path(os.environ.get("MELEE_LONG_IDLE_SUPPORT_ROOT", ROOT))
sys.path[:0] = [str(ROOT / "tools"), str(SUPPORT / "tools"),
                str(SUPPORT / "reference-capture/dolphin")]
from stadium_whole_long_idle import LongIdleBObservations, PROFILE
from stadium_go_prefix import StadiumGoPrefixError
from reference_observer_stream import iter_records


class LongIdleSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler:
            raise unittest.SkipTest("C++ compiler required for actual private source leaf")
        cls.scratch = tempfile.TemporaryDirectory(prefix="whole-long-idle-")
        cls.preserve_scratch = False
        cls.addClassCleanup(lambda: None if cls.preserve_scratch else cls.scratch.cleanup())
        source = (ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        start = source.index("enum class Boundary :")
        end = source.index("// Optional whole3 adjunct", start)
        enum = source[start:end]
        start = source.index("struct StadiumWholeLongIdle\n{")
        end = source.index("enum class StadiumGoPrefixPhase", start)
        leaf = source[start:end]
        control = r'''#include <cassert>
#include <cstdint>
#include <string>
#include <string_view>
using u8=std::uint8_t;using u16=std::uint16_t;using u32=std::uint32_t;
''' + enum + leaf + r'''
int main(int argc, char**) {
 assert(argc==2);
 using L=StadiumWholeLongIdle;
 assert(L::Admit("",false,false,false,true,true,false,""));
 assert(L::Admit("selected_b_3500_v1",true,true,true,false,false,true,"1"));
 assert(!L::Admit("selected_b_3500_v1",true,true,true,false,false,true,"0,1"));
 assert(!L::Admit("selected_b_3500_v1",true,true,true,false,true,true,"1"));
 assert(!L::Admit("selected_b_3500_v1",true,true,false,true,false,true,"1"));
 assert(!L::Admit("selected_b_3500_v1",false,true,true,false,false,true,"1"));
 assert(!L::Admit("selected_b_3500_v1",true,false,true,false,false,true,"1"));
 assert(!L::Admit("selected_b_3500_v1",true,true,true,false,false,false,"1"));
 assert(!L::Admit("unknown",true,true,true,false,false,true,"1"));
 L inactive;assert(inactive.Complete()&&!inactive.Omit(1,Boundary::DrawEnter));
 L x;x.enabled=true;assert(!x.Go(0,0x80400000,84));assert(x.Go(1,0x80400000,84));
 assert(!x.Go(1,0x80400000,84));
 assert(!x.Observe(1,5,0x80400004,Boundary::SourceTick,84));
 assert(!x.Observe(0,5,0x80400000,Boundary::SourceTick,84));
 assert(!x.Observe(1,4,0x80400000,Boundary::SourceTick,84));
 for(u32 t=84;t<3584;++t) assert(x.Observe(1,5,0x80400000,Boundary::SourceTick,t));
 assert(x.Observe(1,5,0x80400000,Boundary::DrawReturn,3584));
 assert(!x.target_seen&&!x.Complete()); // Earlier raw3584 draw is not target SourceTick.
 assert(x.Observe(1,5,0x80400000,Boundary::SourceTick,3584));
 assert(x.target_seen&&!x.Complete());
 assert(x.Omit(1,Boundary::DrawEnter)&&x.Omit(1,Boundary::DrawReturn));
 assert(!x.Omit(1,Boundary::PadPoll)&&!x.Omit(1,Boundary::PadConsume));
 assert(!x.Omit(1,Boundary::SourceTick)&&!x.Omit(0,Boundary::DrawReturn));
 L tail=x;
 assert(x.Observe(1,5,0x80400000,Boundary::DrawEnter,3585));
 assert(x.Observe(1,5,0x80400000,Boundary::DrawReturn,3585));
 assert(x.Complete()&&!x.Omit(1,Boundary::DrawReturn));
 for(u32 t=3585;t<=3588;++t) assert(tail.Observe(1,5,0x80400000,Boundary::SourceTick,t));
 L excess=tail;assert(!excess.Observe(1,5,0x80400000,Boundary::SourceTick,3589));
 assert(!tail.Complete()); // Missing final DrawReturn refuses, no forced extra tick.
 assert(tail.Observe(1,5,0x80400000,Boundary::DrawReturn,3589));assert(tail.Complete());
 L early;early.enabled=true;assert(early.Go(1,0x80400000,84));
 assert(!early.Observe(1,5,0x80400000,Boundary::DrawReturn,85));
 assert(!early.Go(1,0x80400000,UINT32_MAX));
}
'''
        scratch = Path(cls.scratch.name)
        cpp = scratch / "control.cpp"; cpp.write_text(control)
        cls.binary = scratch / "control"
        result = subprocess.run([compiler,"-std=c++20","-Wall","-Wextra","-Werror",str(cpp),
                                 "-o",str(cls.binary)], capture_output=True, text=True)
        (scratch / "compile.stdout").write_text(result.stdout)
        (scratch / "compile.stderr").write_text(result.stderr)
        if result.returncode:
            cls.preserve_scratch = True
            cls.scratch._finalizer.detach()
            raise AssertionError(f"actual long idle leaf compile failed: {scratch}\n{result.stderr}")

    def test_actual_source_admission_target_and_natural_tails(self):
        subprocess.run([str(self.binary), "all"], check=True, capture_output=True)

    def test_serialization_and_legacy_gates_remain_source_ordered(self):
        source = (ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        begin = source.index("    // Validate all source reads above before omitting")
        reserve = source.index("    Slot* slot = Reserve(Event::Boundary", begin)
        self.assertLess(source.index("if (omit_long_idle_raw) { slice_count = 0; raw_size = 0; }", begin),reserve)
        self.assertIn("PutU32(out, 32);", source[reserve:reserve+1000])
        self.assertIn("for (u32 value : state->gpr)", source[reserve:reserve+1000])
        self.assertIn("if (stadium_idle_3500 && (next_sequence >= STADIUM_IDLE_RECORD_CAP - 1", source)
        self.assertIn("if (!stadium_whole_long_idle.Complete())", source)
        self.assertEqual(source.count('StadiumWholeLongIdle::ProfileJson()'),3)


class LongIdleRetainedCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture = os.environ.get("MELEE_LONG_IDLE_RETAINED")
        if fixture is None:
            raise unittest.SkipTest("optional retained original idle fixture absent")
        cls.fixture = Path(fixture)
        if hashlib.sha256(cls.fixture.read_bytes()).hexdigest() != "2c1b69f6afbb47f1d71de2f6628b2c62d16839bbb7859f630c20082d6e363452":
            raise AssertionError("retained original idle fixture identity differs")

    def rows(self, mutation=None):
        # Explicit external composition: inject declared profile/whole metadata
        # around actual retained standalone bytes; this is not new producer proof.
        active = False
        for original in iter_records(self.fixture):
            row = copy.deepcopy(original); p = row.get("payload", {})
            if row["event"] in ("handshake", "start"):
                p.update(stadium_whole_long_idle=copy.deepcopy(PROFILE),whole_session=True,
                         match_count=3,stadium_whole_b_state=True,stadium_observed_match=1)
            if row["event"] == "progress" and p.get("phase") == "go_after":
                active = True
                p["match_index"] = 1
            if row["event"] == "boundary":
                p.update(whole_session=True,match_index=1,audio_owner_epoch=0)
                if active and p.get("boundary") in ("draw_enter", "draw_return"):
                    p["slices"] = []
            if mutation:
                mutation(row)
            yield row

    def test_retained_actual_state_input_order_positive(self):
        control=LongIdleBObservations()
        for row in self.rows(): control.accept(row)
        result=control.finish()
        self.assertEqual((result["state_samples"],result["natural_tail_ticks"],result["close"]["sequence"]),
                         (3501,0,20505))
        self.assertFalse(result["native_parity"])

    def test_wrong_profile_and_foreign_draw_refuse(self):
        def profile(row):
            if row["event"] == "start":
                row["payload"]["stadium_whole_long_idle"]["selected_matches"]=[0,1]
        with self.assertRaisesRegex(StadiumGoPrefixError,"profile identity"):
            control=LongIdleBObservations()
            for row in self.rows(profile): control.accept(row)
        def foreign(row):
            if row["seq"] == 3008: row["payload"]["match_index"]=0
        with self.assertRaisesRegex(StadiumGoPrefixError,"foreign whole-session"):
            control=LongIdleBObservations()
            for row in self.rows(foreign): control.accept(row)

    def test_missing_terminal_draw_and_source_state_refuse(self):
        control=LongIdleBObservations()
        for row in self.rows():
            if row["seq"] == 20505: break
            control.accept(row)
        with self.assertRaisesRegex(StadiumGoPrefixError,"missing complete"):
            control.finish()
        def missing(row):
            if row["seq"] == 3007: row["payload"]["slices"]=[]
        with self.assertRaises(StadiumGoPrefixError):
            control=LongIdleBObservations()
            for row in self.rows(missing): control.accept(row)
