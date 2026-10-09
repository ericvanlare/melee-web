"""Portable prefix negative controls; synthetic unit data is not retail evidence."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
from authored_sd_reference_plan import make_input_plan
from sd_reference_diagnostic import Receiver, SdDiagnosticError, SCOPE, PCS
from capture_sd_reference_prefix import menu_actions
from test_authored_sd_reference_plan import setup_bytes


def events():
    plan = make_input_plan()
    normal = setup_bytes()
    persistent = bytearray(normal)
    persistent[2] &= ~0x80
    persistent[4] &= ~0x40
    sd = bytearray(persistent)
    sd[0] &= ~2
    sd[2] &= ~4
    for slot in range(2):
        base = 0x60 + slot * 0x24
        sd[base + 2] = 1
        sd[base + 0x12:base + 0x14] = (300).to_bytes(2, "big")
    end = bytearray(0x448)
    end[4:7] = bytes((1, 1, 0))
    end[0xd] = 2
    for base in (0x58, 0x100):
        end[base + 1] = 8
        end[base + 8] = 4
    rows = []
    def row(event, payload):
        rows.append({"seq": len(rows), "event": event, "source_tick": 0, "payload": payload})
    row("handshake", {"diagnostic": SCOPE, "recipe_sha256": plan["authored_recipe_sha256"],
                      "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
                      "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                      "dolphin_commit": "c77bbaa0f372c3f72281602a8b087206706542cb",
                      "cpu": "JITARM64", "writes_guest_memory": False})
    row("start", {"status": "recording"})
    def progress(name, data=(), count=1):
        row("progress", {"diagnostic": SCOPE, "name": name, "pc": PCS[name], "consumed": count,
                         "slices": [{"tag": tag, "flags": flags, "address": 0x80001000,
                                     "hex": bytes(raw).hex()} for tag, flags, raw in data]})
    progress("vs_entry", ((4, 0, normal), (4, 1, persistent)), count=0)
    pad = b"".join(bytes.fromhex(value) + b"\0" for value in plan["frames"][0])
    progress("input", ((3, 0, pad),))
    progress("vs_setup", ((4, 0, normal),))
    progress("vs_exit", ((15, 0, end),))
    progress("vs_retired")
    progress("sd_entry", ((4, 0, sd), (4, 1, persistent)))
    sd[6] = 1
    data = [(4, 0, sd)]
    for slot in range(2):
        head = bytearray(0x100)
        head[12] = slot
        data.extend(((5, slot, head), (7, slot, struct.pack(">f", 300)), (8, slot, b"\1")))
    progress("sd_setup", data)
    row("end", {"status": "interrupted", "natural": False})
    return rows


class SdReferenceDiagnosticTests(unittest.TestCase):
    def test_menu_packets_are_predeclared_bounded_and_finish_in_original_sss(self):
        value = {"schema": "melee-web-sd-original-menu-inputs", "version": 1,
                 "actions": [{"label": "confirm", "scene": 9, "p1": "0000000000000000000000",
                              "p2": "0000000000000000000000", "polls": 1, "settle_polls": 1}]}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "menu.json"
            path.write_text(json.dumps(value))
            self.assertEqual(menu_actions(path)[0], value)
            for key, bad in (("scene", 8), ("polls", 0), ("polls", 121),
                             ("settle_polls", True), ("p2", "00")):
                changed = deepcopy(value)
                changed["actions"][0][key] = bad
                path.write_text(json.dumps(changed))
                with self.assertRaises(ValueError):
                    menu_actions(path)

    def accept(self, rows):
        receiver = Receiver(make_input_plan())
        for row in rows:
            receiver.accept(row)
        return receiver

    def test_declared_unit_prefix_keeps_legacy_completion_interrupted(self):
        receiver = self.accept(events())
        self.assertTrue(receiver.ended)
        self.assertEqual(receiver.consumed, 1)
        for payload in ({"status": "completed", "natural": True},
                        {"status": "interrupted", "natural": True}):
            rows = events()
            rows[-1]["payload"] = payload
            with self.assertRaisesRegex(SdDiagnosticError, "never legacy"):
                self.accept(rows)

    def test_missing_reordered_duplicate_and_wrong_pc_fail(self):
        for mutate in (lambda r: r.pop(5), lambda r: r.insert(5, deepcopy(r[4])),
                       lambda r: r[7]["payload"].__setitem__("name", "sd_setup"),
                       lambda r: r[8]["payload"].__setitem__("pc", 0x8016e9c4)):
            rows = events()
            mutate(rows)
            with self.assertRaises(SdDiagnosticError):
                self.accept(rows)

    def test_input_mismatch_exhaustion_and_wrong_source_counter_fail(self):
        rows = events()
        rows[3]["payload"]["slices"][0]["hex"] = "01" + rows[3]["payload"]["slices"][0]["hex"][2:]
        with self.assertRaisesRegex(ValueError, "Input intent mismatch"):
            self.accept(rows)
        receiver = Receiver(make_input_plan())
        for row in events()[:3]:
            receiver.accept(row)
        receiver.consumed = 4323
        bad = events()[3]
        bad["payload"]["consumed"] = 4324
        with self.assertRaisesRegex(ValueError, "cap exhausted"):
            receiver.accept(bad)
        receiver = self.accept(events()[:-1])
        bad = {"seq": receiver.seq, "event": "progress", "source_tick": 1,
               "payload": {"diagnostic": SCOPE, "name": "tick", "pc": PCS["tick"],
                           "consumed": 1, "slices": []}}
        with self.assertRaisesRegex(SdDiagnosticError, "escaped declared"):
            receiver.accept(bad)

    def test_non_tie_sd_flag_normalization_and_wrong_damage_fail(self):
        for index, field, offset, value in ((5, 0, 0xd, 1), (7, 0, 4, 0x40),
                                           (8, 0, 6, 0), (8, 2, 0, 0)):
            rows = events()
            item = rows[index]["payload"]["slices"][field]
            raw = bytearray.fromhex(item["hex"])
            raw[offset] = value
            item["hex"] = raw.hex()
            with self.assertRaises(SdDiagnosticError):
                self.accept(rows)

    def test_native_completion_is_independent_and_never_synthesized(self):
        receiver = self.accept(events())
        with mock.patch("sd_reference_diagnostic.read_status", return_value={
                "state": "interrupted", "completed": False, "invalid": False, "error": None}), \
                mock.patch("sd_reference_diagnostic.validate_stream", return_value={"events": 99}) as stream, \
                mock.patch("sd_reference_diagnostic.validate_status") as status:
            report = receiver.finish("observer-status", "actual-native-stream", "input-status")
            self.assertFalse(report["whole_session_admission"])
            stream.assert_called_once_with("actual-native-stream")
            status.assert_called_once_with("input-status", mode="record", events=99)
        with mock.patch("sd_reference_diagnostic.read_status", return_value={
                "state": "completed", "completed": True, "invalid": False, "error": None}):
            with self.assertRaisesRegex(SdDiagnosticError, "ending differs"):
                receiver.finish("observer-status", "native", "status")

    def test_real_native_phase_header_and_observer_methods_compile(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        observer = (ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        methods = observer[observer.index("  void SdEvent("):observer.index("  enum class SceneResetAction")]
        enum = observer[observer.index("enum class SliceTag"):observer.index("struct Slot")]
        harness = r"""
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;
#include "ReferenceSdInitState.h"
using namespace ReferenceCapture;
namespace Core { struct Memory { const u8* GetPointerForRange(u32, size_t) { static u8 kind=2; return &kind; } };
struct System { Memory m; Memory& GetMemory() { return m; } }; }
namespace PowerPC { struct PowerPCState { std::array<u32,32> gpr{}; }; }
constexpr u32 CSS_ENTER_RETURN=0x802669f0, PAD_READ_HSD_CALLER=0x80376a28;
enum class Event { Progress };
bool AppendHexBytes(std::string*, const u8*, size_t) { return true; }
""" + enum + r"""
struct Reader {
  SdInitState sd_init;
  u32 sd_menu_polls=0;
  bool css_steering_ready=false;
  size_t slice_count=0, raw_size=0;
  std::array<SliceRef,64> slices{};
  std::array<u8,192*1024> raw{};
  std::array<bool,4> fighter_present{};
  std::array<u32,4> fighter_pointers{};
  void SetInvalid(const char*) {}
  void PushJson(Event,const std::string&,u32,u32) {}
  bool ReadU32(Core::System*,u32,u32*) { return false; }
  bool ReadBytes(Core::System*,u32,size_t,u8*) { return false; }
  bool BoundaryInstructionMatches(Core::System*,u32) { return true; }
  bool AddSceneKindSlice(Core::System*) { return true; }
  bool AddMenuSteeringSlices(Core::System*) { return true; }
  bool AddProfileSlices(Core::System*) { return true; }
  bool ReadProfileRoot(Core::System*,u32*) { return true; }
  bool ReadFighterSourceSlot(Core::System*,u32,u8*) { return true; }
  bool AddSlice(Core::System*,SliceTag,u32,size_t,u16=0) { return true; }
  u32 ReadBE32(const u8*) { return 0; }
  struct Flag { void store(bool) {} } natural_completion,finish_requested;
  struct InputStream { static void RequestFinish(bool) {} };
""" + methods + r"""
};
int main() {
  SdInitState state;
  assert(!state.Entry(0x80001000,true));
  assert(state.Entry(0x80001000,false));
  assert(!state.Entry(0x80002000,false));
  assert(state.Ready(false));
  assert(!state.Retire());
  assert(state.Exit()); assert(state.Retire());
  assert(state.Entry(0x80003000,true));
  assert(state.setup_pointer==0x80003000);
  assert(state.Ready(true));
  assert(state.setup_pointer==0x80003000); // caller-restored R3/R31 are irrelevant
  assert(!state.Consume());
  SdInitState capped; assert(capped.Entry(0x80001000,false));
  for (u32 i=0;i<4323;++i) assert(capped.Consume());
  assert(!capped.Consume()); assert(capped.consumed==4323);
}
"""
        with tempfile.TemporaryDirectory() as directory:
            cpp, executable = Path(directory) / "probe.cpp", Path(directory) / "probe"
            cpp.write_text(harness)
            subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror", "-I", str(ROOT /
                            "reference-capture/dolphin/source/Core/PowerPC"), str(cpp), "-o", str(executable)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(executable)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
