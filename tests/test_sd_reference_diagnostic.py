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
from sd_reference_diagnostic import Receiver, RulesMenuReceiver, SdDiagnosticError, SCOPE, PCS
from sd_original_menu_plan import rules_ready_packet, matches
from capture_sd_reference_prefix import cleanup_process, menu_actions, wait_terminal_statuses
from test_authored_sd_reference_plan import setup_bytes


def events(recipe_version=1):
    plan = make_input_plan(recipe_version)
    normal = setup_bytes()
    for slot in range(2):
        normal[0x60 + slot * 0x24 + 0xa] = 120
    persistent = bytearray(normal)
    persistent[2] &= ~0x80
    persistent[4] &= ~0x40
    # Persistent settings need not have been normalized by the last match.
    for slot in (0, 2):
        persistent[0x60 + slot * 0x24 + 0xc] |= 0x80
    sd = bytearray(persistent)
    for slot in range(6):
        sd[0x60 + slot * 0x24 + 0xc] &= ~0x80
    sd[0] &= ~2
    sd[2] &= ~4
    for slot in range(2):
        base = 0x60 + slot * 0x24
        sd[base + 2] = 1
        sd[base + 0x12:base + 0x14] = (300).to_bytes(2, "big")
    end = bytearray(0x448)
    end[4:7] = bytes((1, 1, 0))
    end[0xd] = 2
    end[8:12] = (3600).to_bytes(4, "big")
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
    consumed = 0
    def progress(name, data=(), count=None):
        row("progress", {"diagnostic": SCOPE, "name": name, "pc": PCS[name], "consumed": count,
                         "slices": [{"tag": tag, "flags": flags, "address": 0x80001000,
                                     "hex": bytes(raw).hex()} for tag, flags, raw in data]})
        rows[-1]["payload"]["consumed"] = consumed if count is None else count
    preferences = bytes((0, 0, 1, 1)) if recipe_version >= 2 else b"\0" * 4
    progress("vs_entry", ((4, 0, normal), (4, 1, persistent), (54, 0, preferences)), count=0)
    pad = b"".join(bytes.fromhex(value) + b"\0" for value in plan["frames"][0])
    consumed += 1
    progress("input", ((3, 0, pad),))
    progress("vs_setup", ((4, 0, normal),))
    for frame in range(1, 3601):
        consumed += 1
        progress("input", ((3, 0, pad),))
        clock = bytearray(0x2e)
        clock[0x24:0x28] = frame.to_bytes(4, "big")
        clock[0x28:0x2c] = (60 - (frame + 59) // 60).to_bytes(4, "big")
        clock[0x2c:0x2e] = ((frame + 59) % 60).to_bytes(2, "big")
        progress("tick", ((14, 0, clock),))
        rows[-1]["source_tick"] = frame - 1
    progress("vs_exit", ((15, 0, end), (14, 0, clock)))
    rows[-1]["source_tick"] = 3600
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


def index(rows, name):
    return next(i for i, row in enumerate(rows) if row["payload"].get("name") == name)


class SdReferenceDiagnosticTests(unittest.TestCase):
    def test_direct_process_cleanup_retains_reaping_and_failed_wait_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            process = mock.Mock(pid=12345)
            process.poll.return_value = None
            process.wait.side_effect = [subprocess.TimeoutExpired("owned", 5), -9]
            receipt = cleanup_process(process, directory)
            self.assertEqual(receipt["ownership"], "direct-Popen")
            self.assertEqual(receipt["returncode"], -9)
            process.terminate.assert_called_once_with()
            process.kill.assert_called_once_with()
            self.assertEqual(json.loads((Path(directory) / "cleanup.json").read_text()), receipt)
            process.wait.side_effect = subprocess.TimeoutExpired("owned", 5)
            with self.assertRaises(SdDiagnosticError): cleanup_process(process, directory)
            self.assertIsNotNone(json.loads((Path(directory) / "cleanup.json").read_text())["error"])

    def test_cli_requires_explicit_inputs_and_does_not_succeed_as_a_noop(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/capture_sd_reference_prefix.py")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--menu-recipe", result.stderr)

    def test_independent_status_writer_race_and_invalid_native_status(self):
        primary = {"state": "interrupted", "invalid": False, "error": None}
        with mock.patch("capture_sd_reference_prefix.Path.is_file", return_value=True), \
                mock.patch("capture_sd_reference_prefix.read_status", return_value=primary), \
                mock.patch("capture_sd_reference_prefix.validate_status", side_effect=[
                    {"complete": False}, {"complete": True}]) as status, \
                mock.patch("capture_sd_reference_prefix.time.sleep") as sleep:
            wait_terminal_statuses("observer", "native", float("inf"))
            self.assertEqual(status.call_count, 2)
            sleep.assert_called_once_with(0.02)
        with mock.patch("capture_sd_reference_prefix.Path.is_file", return_value=True), \
                mock.patch("capture_sd_reference_prefix.read_status", return_value=primary), \
                mock.patch("capture_sd_reference_prefix.validate_status", side_effect=ValueError("invalid native")), \
                mock.patch("capture_sd_reference_prefix.time.sleep") as sleep:
            with self.assertRaisesRegex(ValueError, "invalid native"):
                wait_terminal_statuses("observer", "native", float("inf"))
            sleep.assert_not_called()

    def test_missing_ticks_clock_jump_or_missing_iteration_consumption_fail(self):
        for name in ("no_ticks", "skip_tick", "no_consumption", "clock_jump"):
            rows = events()
            if name == "no_ticks":
                rows = [r for i, r in enumerate(rows) if i < 5 or
                        r["payload"].get("name") not in ("input", "tick")]
                for row in rows[5:]:
                    if "consumed" in row["payload"]:
                        row["payload"]["consumed"] = 1
            elif name == "skip_tick":
                rows.pop(index(rows, "tick"))
            elif name == "no_consumption":
                rows.pop(5)
                for row in rows[5:]:
                    if "consumed" in row["payload"]:
                        row["payload"]["consumed"] -= 1
            else:
                raw = bytearray.fromhex(rows[index(rows, "tick")]["payload"]["slices"][0]["hex"])
                raw[0x24:0x28] = (3600).to_bytes(4, "big")
                rows[index(rows, "tick")]["payload"]["slices"][0]["hex"] = raw.hex()
            for i, row in enumerate(rows):
                row["seq"] = i
            with self.subTest(name=name), self.assertRaises(SdDiagnosticError):
                self.accept(rows)

    def test_profile_rumble_and_unnamed_ports_are_exact_supported_requirements(self):
        for field, offset, value in ((2, 0, 1), (1, 0x6a, 0)):
            rows = events()
            item = rows[2]["payload"]["slices"][field]
            raw = bytearray.fromhex(item["hex"])
            raw[offset] = value
            item["hex"] = raw.hex()
            with self.assertRaises(SdDiagnosticError):
                self.accept(rows)

    def test_sd_rumble_normalizes_owned_bit_and_preserves_every_other_bit(self):
        rows = events()
        persistent = bytes.fromhex(rows[2]["payload"]["slices"][1]["hex"])
        sd = bytes.fromhex(rows[index(rows, "sd_entry")]["payload"]["slices"][0]["hex"])
        self.assertEqual(persistent[0x6c] & 0x80, 0x80)
        self.assertEqual(sd[0x6c] & 0x80, 0)
        self.accept(rows)
        for mask in (0x80, 1):
            rows = events()
            item = rows[index(rows, "sd_entry")]["payload"]["slices"][0]
            raw = bytearray.fromhex(item["hex"])
            raw[0x6c] ^= mask
            item["hex"] = raw.hex()
            with self.assertRaisesRegex(SdDiagnosticError, "SD setup differs"):
                self.accept(rows)

    def test_versioned_cold_profile_preserves_inactive_preferences_and_port_mapping(self):
        original = make_input_plan()
        corrected = make_input_plan(2)
        self.assertNotEqual(original["authored_recipe_sha256"], corrected["authored_recipe_sha256"])
        self.assertNotIn("cold_original_context", original["authored_recipe"])
        receiver = Receiver(corrected)
        for row in events(2):
            receiver.accept(row)
        self.assertTrue(receiver.ended)
        for field, offset, value in ((2, 2, 0), (2, 0, 1), (1, 0x64, 0), (1, 0x6a, 119)):
            rows = events(2)
            item = rows[2]["payload"]["slices"][field]
            raw = bytearray.fromhex(item["hex"])
            raw[offset] = value
            item["hex"] = raw.hex()
            receiver = Receiver(corrected)
            with self.assertRaises(SdDiagnosticError):
                for row in rows:
                    receiver.accept(row)

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

    def test_reduced_guarded_packet_binds_recipe_and_exact_source_predicates(self):
        packet = rules_ready_packet()
        self.assertEqual(packet["authored_recipe_sha256"], make_input_plan(3)["authored_recipe_sha256"])
        self.assertEqual(packet["scope"], "rules_ready")
        state = dict(packet["stop"])
        self.assertTrue(matches(state, packet["stop"]))
        for field in ("value", "cooldown", "entering", "row", "kind", "scene"):
            changed = dict(state)
            changed[field] += 1
            self.assertFalse(matches(changed, packet["stop"]))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "menu.json"
            path.write_text(json.dumps(packet))
            self.assertEqual(menu_actions(path)[0], packet)
            for change in ("scope", "guard", "pad", "recipe"):
                value = deepcopy(packet)
                if change == "scope": value["scope"] = "sd_initialization_prefix"
                elif change == "guard": value["actions"][0]["before"]["cooldown"] = 1
                elif change == "pad": value["actions"][0]["p2"] = value["actions"][0]["p1"]
                else: value["authored_recipe_sha256"] = make_input_plan()["authored_recipe_sha256"]
                path.write_text(json.dumps(value))
                with self.assertRaises(ValueError): menu_actions(path)

    def test_recipe_v3_binds_source_get_port_without_discarding_raw_slot_bytes(self):
        plan = make_input_plan(3)
        for source_slots in ((0, 0), (1, 2)):
            rows = events(3)
            for row in rows:
                for item in row["payload"].get("slices", []):
                    if item["tag"] == 4:
                        raw = bytearray.fromhex(item["hex"])
                        for slot, source in enumerate(source_slots): raw[0x64 + slot * 0x24] = source
                        item["hex"] = raw.hex()
            receiver = Receiver(plan)
            for row in rows: receiver.accept(row)
            self.assertTrue(receiver.ended)
        rows = events(3)
        item = rows[2]["payload"]["slices"][1]
        raw = bytearray.fromhex(item["hex"])
        raw[0x88] = 1  # P2 now resolves to P1 port, not a valid encoding.
        item["hex"] = raw.hex()
        receiver = Receiver(plan)
        with self.assertRaisesRegex(SdDiagnosticError, "source port mapping"):
            for row in rows: receiver.accept(row)

    def test_rules_receiver_requires_observed_input_neutral_owner_and_reduced_scope(self):
        rows = events(3)[:2]
        rows[0]["payload"]["menu_probe"] = "rules_ready"
        pad = b"".join(bytes.fromhex(v) + b"\0" for v in make_input_plan(2)["frames"][0])
        def add(name, count, data):
            rows.append({"seq": len(rows), "event": "progress", "source_tick": 0,
                         "payload": {"diagnostic": SCOPE, "name": name, "pc": PCS[name],
                         "consumed": 0, "menu_consumed": count,
                         "slices": [{"tag": tag, "flags": 0, "address": 0x80001000,
                                     "hex": bytes(raw).hex()} for tag, raw in data]}})
        add("menu_input", 1, [(3, pad)])
        flow = bytearray(0x18)
        flow[0] = 13
        data = [(40, b"\1"), (45, flow), (46, b"\0" * 8)]
        add("menu", 1, data)
        add("rules_ready", 1, data + [(54, b"\1" * 4)])
        rows.append({"seq": len(rows), "event": "end", "source_tick": 0,
                     "payload": {"status": "interrupted", "natural": False}})
        receiver = RulesMenuReceiver(make_input_plan(3))
        for row in rows: receiver.accept(row)
        self.assertTrue(receiver.ended)
        with self.assertRaises(SdDiagnosticError): Receiver(make_input_plan(3)).accept(rows[0])
        variants = []
        missing = deepcopy(rows)
        del missing[2]
        variants.append(missing)
        for tag, changed in ((54, "00000101"), (46, "0001" + "00" * 6),
                             (45, "0d" + "00" * 3 + "01" + "00" * 19)):
            bad = deepcopy(rows)
            next(s for s in bad[4]["payload"]["slices"] if s["tag"] == tag)["hex"] = changed
            variants.append(bad)
        gameplay = deepcopy(rows)
        gameplay[3] = events(2)[2]
        variants.append(gameplay)
        undeclared = deepcopy(rows)
        undeclared[2]["payload"]["slices"][0]["hex"] = "0400" + pad[2:].hex()  # X
        variants.append(undeclared)
        for bad in variants:
            for seq, row in enumerate(bad): row["seq"] = seq
            receiver = RulesMenuReceiver(make_input_plan(3))
            with self.assertRaises(SdDiagnosticError):
                for row in bad: receiver.accept(row)

    def accept(self, rows):
        receiver = Receiver(make_input_plan())
        for row in rows:
            receiver.accept(row)
        return receiver

    def test_declared_unit_prefix_keeps_legacy_completion_interrupted(self):
        receiver = self.accept(events())
        self.assertTrue(receiver.ended)
        self.assertEqual(receiver.consumed, 3601)
        self.assertEqual(receiver.tick_count, 3600)
        self.assertEqual(receiver.vs_inventory["last"], 3599)
        for payload in ({"status": "completed", "natural": True},
                        {"status": "interrupted", "natural": True}):
            rows = events()
            rows[-1]["payload"] = payload
            with self.assertRaisesRegex(SdDiagnosticError, "never legacy"):
                self.accept(rows)

    def test_missing_reordered_duplicate_and_wrong_pc_fail(self):
        for mutate in (lambda r: r.pop(index(r, "vs_exit")), lambda r: r.insert(5, deepcopy(r[4])),
                       lambda r: r[index(r, "sd_entry")]["payload"].__setitem__("name", "sd_setup"),
                       lambda r: r[index(r, "sd_setup")]["payload"].__setitem__("pc", 0x8016e9c4)):
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
                           "consumed": receiver.consumed, "slices": []}}
        with self.assertRaisesRegex(SdDiagnosticError, "escaped declared"):
            receiver.accept(bad)

    def test_non_tie_sd_flag_normalization_and_wrong_damage_fail(self):
        for name, field, offset, value in (("vs_exit", 0, 0xd, 1), ("sd_entry", 0, 4, 0x40),
                                           ("sd_setup", 0, 6, 0), ("sd_setup", 2, 0, 0)):
            rows = events()
            item = rows[index(rows, name)]["payload"]["slices"][field]
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
  u32 sd_menu_consumed=0;
  bool sd_menu_neutral=false;
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
  std::string Env(const char*) { return ""; }
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
