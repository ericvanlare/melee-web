from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
import shutil
import subprocess
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin")]

import reference_observer_stream as observer_stream
from stadium_go_prefix import (  # noqa: E402
    DIAGNOSTIC,
    EXPECTED_SETUP_RECEIPT_SHA256,
    StadiumGoPrefixError,
    validate_stadium_go_prefix,
)


def _slice(tag: int, size: int, *, data: bytes | None = None,
           address: int | None = None, flags: int = 0) -> dict:
    if data is None:
        data = bytes(size)
    if address is None:
        address = 0x80400000 + tag * 0x10000 + flags * 0x100
    return {"tag": tag, "flags": flags, "address": address,
            "size": size, "hex": data.hex()}


def _setup() -> bytes:
    raw = bytearray(0x138)
    raw[0] = 0x20  # ordinary VS match kind
    raw[2] = 0x80  # stock mode
    raw[4] = 0x40  # ordinary VS route
    raw[0x0E:0x10] = struct.pack(">H", 3)
    raw[0x2C:0x30] = bytes.fromhex("3f800000")
    for index in range(6):
        raw[0x60 + index * 0x24 + 1] = 3
    raw[0x60:0x65] = bytes((8, 0, 4, 1, 0))
    raw[0x84:0x89] = bytes((8, 0, 4, 0, 2))
    return bytes(raw)


def _menu_slices(phase: str, route: int = 1) -> list[dict]:
    route_bytes = bytes((route,))
    common = [
        _slice(17, 6, data=bytes((2, 0, 0, 0, 0, 0))),
        _slice(19, 4, data=bytes.fromhex("80401000")),
        _slice(20, 4, data=bytes.fromhex("00000001")),
        _slice(33, 0x40), _slice(34, 4), _slice(21, 0x358),
    ]
    if phase == "css_entry":
        return common + [
            _slice(31, 0xF0), _slice(38, 0x18), _slice(39, 0x55E8),
            _slice(50, 0x148, address=0x80410000), _slice(51, 6),
        ]
    if phase == "css_return":
        return common + [_slice(31, 0xF0)]
    if phase == "sss_entry":
        return common + [
            _slice(32, 0xF0, address=0x80420010),
            _slice(35, 1, data=route_bytes, address=0x80420004),
        ]
    return common + [_slice(32, 0xF0), _slice(35, 1, data=route_bytes)]


def _progress(phase: str, seq: int, *, source_tick: int = 40,
              route: int = 1, setup: bytes | None = None) -> bytes:
    pcs = {"css_entry": 0x8026688C, "css_return": 0x802669F0,
           "sss_entry": 0x8025A998, "sss_return": 0x8025B84C,
           "sss_exit": 0x8025BBD0, "go_after": 0x8016B824}
    words = {"css_entry": 0x7C0802A6, "css_return": 0x4E800020,
             "sss_entry": 0x7C0802A6, "sss_return": 0x4E800020,
             "sss_exit": 0x4E800020, "go_after": 0x881F24C9}
    payload = {
        "diagnostic": DIAGNOSTIC,
        "phase": phase,
        "setup_receipt_sha256": EXPECTED_SETUP_RECEIPT_SHA256,
        "setup_profile_verified_by_observer": False,
        "pc": f"0x{pcs[phase]:08x}",
        "word": f"0x{words[phase]:08x}",
        "argument": ("0x80410000" if phase == "css_entry" else
                     "0x80420000" if phase == "sss_entry" else "0x00000000"),
        "lr": "0x8016b824" if phase == "go_after" else "0x00000000",
        "source_tick": source_tick,
        "draw_ordinal": 0,
        "slices": _menu_slices(phase, route),
    }
    if phase == "go_after":
        payload.update({
            "callsite_pc": "0x8016b820",
            "callsite_word": "0x48068821",
            "slices": [
                _slice(4, 0x138, data=setup or _setup()),
                _slice(2, 0xC, data=bytes((1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0))),
                *_menu_slices("sss_exit", route)[:1],
                *_menu_slices("sss_exit", route)[1:3],
            ],
        })
    return _frame(4, seq, json.dumps(payload, separators=(",", ":")).encode(),
                  pc=pcs[phase], source_tick=source_tick)


def _boundary(kind: int, seq: int, *, pc: int, source_tick: int,
              slices: list[dict] | None = None) -> bytes:
    slices = slices or []
    metadata = observer_stream.BOUNDARY.pack(kind, 0, 0, 32, len(slices), 0)
    gprs = struct.pack("<32I", *([0] * 32))
    descriptors = bytearray()
    data = bytearray()
    cursor = len(metadata) + len(gprs) + len(slices) * observer_stream.SLICE.size
    for item in slices:
        raw = bytes.fromhex(item["hex"])
        descriptors += observer_stream.SLICE.pack(
            item["tag"], item["flags"], item["address"], len(raw), cursor)
        data += raw
        cursor += len(raw)
    return _frame(3, seq, metadata + gprs + descriptors + data,
                  pc=pc, source_tick=source_tick)


def _pad_poll(scene_kind: int, *, live_owner: bool = True,
              stage_index: int = 18, stage_kind: int = 3,
              index_address: int = 0x804D6CAE,
              kind_address: int | None = None) -> list[dict]:
    values = [
        _slice(1, 0x30), _slice(2, 0xC),
        _slice(17, 6, data=bytes((2, 0, 0, 0, 0, 0))),
        _slice(21, 0x358), _slice(27, 4),
        _slice(40, 1, data=bytes((scene_kind,))),
    ]
    if scene_kind == 8 and live_owner:
        values.extend([
            _slice(48, 0x148, address=0x80410000),
            _slice(44, 0x90, address=0x803F0DFC),
        ])
    elif scene_kind == 9 and live_owner:
        if kind_address is None:
            kind_address = 0x803F06D0 + stage_index * 0x1C + 0x0B
        values.extend([
            _slice(41, 1, data=bytes((stage_index,)), address=index_address),
            _slice(42, 1, data=bytes((stage_kind,)), address=kind_address),
        ])
    return values


def _pad_consume() -> list[dict]:
    return [_slice(2, 0xC), _slice(3, 0x30, address=0x804C3000)]


def _frame(event: int, sequence: int, payload: bytes, *, pc: int = 0,
           source_tick: int = 0) -> bytes:
    header = observer_stream.HEADER.pack(
        observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION, event,
        sequence, sequence + 100, pc, source_tick, 0, len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def _fixture(*, route: int = 1, gap_f: bool = False, tail: int = 1,
             setup: bytes | None = None,
             go_setup: bytes | None = None,
             draw_between_c_f: bool = False,
             boot_pad: bool = False,
             boot_match: bool = False,
             sss_navigation: tuple[tuple[int, int, int | None, int | None], ...] =
             ((0, 4, None, None), (18, 3, None, None))) -> tuple[bytes, dict]:
    receipt = EXPECTED_SETUP_RECEIPT_SHA256
    records = [
        _frame(1, 0, json.dumps({
            "schema": "melee-web-passive-dolphin-observer", "version": 1,
            "dolphin_commit": "c77bbaa0f372c3f72281602a8b087206706542cb",
            "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
            "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
            "cpu": "JITARM64", "writes_guest_memory": False,
            "diagnostic": DIAGNOSTIC, "setup_receipt_sha256": receipt,
            "setup_profile_verified_by_observer": False,
        }, separators=(",", ":")).encode()),
        _frame(2, 1, json.dumps({
            "status": "recording", "source_revision": "GALE01r2",
            "diagnostic": DIAGNOSTIC, "setup_receipt_sha256": receipt,
            "setup_profile_verified_by_observer": False,
        }, separators=(",", ":")).encode()),
    ]
    if boot_pad or boot_match:
        boot = _pad_poll(1)
        boot += [_slice(45, 0x18, address=0x804A04F0),
                 _slice(46, 8, address=0x804D6BC8)]
        records.extend([
            _boundary(1, 2, pc=0x8034DD8C, source_tick=0, slices=boot),
            _boundary(2, 3, pc=0x80377584, source_tick=0, slices=_pad_consume()),
        ])
        if boot_match:
            records.append(_boundary(4, 4, pc=0x8016E934, source_tick=0,
                                     slices=[_slice(4, 0x138, data=_setup())]))
    setup = setup or _setup()
    records.extend([
        _progress("css_entry", 2),
        _boundary(1, 3, pc=0x8034DD8C, source_tick=40,
                  slices=_pad_poll(8, live_owner=False)),
        _progress("css_return", 4),
        _boundary(1, 5, pc=0x8034DD8C, source_tick=40, slices=_pad_poll(8)),
        _boundary(2, 6, pc=0x80377584, source_tick=40, slices=_pad_consume()),
        _progress("sss_entry", 7),
        _boundary(1, 8, pc=0x8034DD8C, source_tick=40,
                  slices=_pad_poll(9, live_owner=False)),
        _progress("sss_return", 9),
    ])
    next_seq = 10
    for stage_index, stage_kind, index_address, kind_address in sss_navigation:
        records.append(_boundary(
            1, next_seq, pc=0x8034DD8C, source_tick=40,
            slices=_pad_poll(9, stage_index=stage_index, stage_kind=stage_kind,
                            index_address=(0x804D6CAE if index_address is None else index_address),
                            kind_address=kind_address)))
        next_seq += 1
    records.append(_progress("sss_exit", next_seq, route=route))
    next_seq += 1
    records.extend([
        _boundary(4, next_seq, pc=0x8016E934, source_tick=40,
                  slices=[_slice(4, 0x138, data=setup)]),
        _boundary(5, next_seq + 1, pc=0x8016E9C4, source_tick=40,
                  slices=[_slice(4, 0x138, data=setup)]),
        _boundary(6, next_seq + 2, pc=0x80390EB4, source_tick=40,
                  slices=[_slice(2, 0xC)]),
        _boundary(8, next_seq + 3, pc=0x80391040, source_tick=40,
                  slices=[_slice(2, 0xC)]),
        _progress("go_after", next_seq + 4, source_tick=125,
                  setup=(go_setup if go_setup is not None else setup)),
        _boundary(6, next_seq + 5, pc=0x80390EB4, source_tick=125,
                  slices=[_slice(2, 0xC)]),
    ])
    next_seq += 6
    if draw_between_c_f:
        records.append(_boundary(8, next_seq, pc=0x80391040, source_tick=125,
                                 slices=[_slice(2, 0xC)]))
        next_seq += 1
    f_tick = 127 if gap_f else 126
    records.append(_boundary(6, next_seq, pc=0x80390EB4, source_tick=f_tick,
                             slices=[_slice(2, 0xC)]))
    next_seq += 1
    for index in range(tail):
        tick = f_tick + index + 1
        records.append(_boundary(6, next_seq, pc=0x80390EB4, source_tick=tick,
                                 slices=[_slice(2, 0xC)]))
        next_seq += 1
    records.append(_boundary(8, next_seq, pc=0x80391040,
                             source_tick=f_tick + tail,
                             slices=[_slice(2, 0xC)]))
    next_seq += 1
    records.append(_frame(6, next_seq, json.dumps({
        "status": "completed", "natural": True,
        "diagnostic": DIAGNOSTIC, "setup_receipt_sha256": receipt,
        "setup_profile_verified_by_observer": False,
        "whole_session_equivalent": False,
        "finishing_draw_return_seen": True,
        "tail_ticks_before_draw_return": tail,
    }, separators=(",", ":")).encode(), source_tick=f_tick + tail))
    if boot_pad or boot_match:
        rebased = []
        for sequence, record in enumerate(records):
            values = list(observer_stream.HEADER.unpack(record[:observer_stream.HEADER.size]))
            values[3] = sequence
            values[4] = sequence + 100
            rebased.append(observer_stream.HEADER.pack(*values) + record[observer_stream.HEADER.size:])
        records = rebased
    status = {"state": "completed", "event_count": len(records),
              "last_seq": len(records) - 1, "completed": True,
              "source_tick": f_tick + tail, "draw_ordinal": 0,
              "invalid": False, "error": None}
    return b"".join(records), status


class StadiumGoPrefixTests(unittest.TestCase):
    def _validate(self, data: bytes, status: dict) -> dict:
        with tempfile.TemporaryDirectory() as directory:
            stream = Path(directory) / "prefix.mwro"
            stream.write_bytes(data)
            status_path = Path(str(stream) + ".status.json")
            status_path.write_text(json.dumps(status), encoding="utf-8")
            return validate_stadium_go_prefix(stream)

    def test_accepts_c_f_then_first_natural_return_without_one_to_one_draw_assumption(self):
        data, status = _fixture(draw_between_c_f=True)
        result = self._validate(data, status)
        self.assertEqual(result["decision"], "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY")
        self.assertEqual((result["go_source_tick_C"], result["first_full_post_go_source_tick_F"]),
                         (125, 126))
        self.assertFalse(result["setup_profile_verified_by_observer"])
        self.assertEqual(result["sss_navigation_poll_rows"], 1)
        self.assertTrue(result["sss_selected_stadium_at_exit"])
        self.assertEqual(result["rng_equality"], "not_compared")

    def test_boot_pad_precedes_first_css_but_cannot_admit_a_match(self):
        data, status = _fixture(boot_pad=True)
        result = self._validate(data, status)
        self.assertEqual(result["decision"], "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY")
        self.assertTrue(result["sss_selected_stadium_at_exit"])
        data, status = _fixture(boot_match=True)
        with self.assertRaisesRegex(StadiumGoPrefixError, "pre-CSS source boundary"):
            self._validate(data, status)

    def test_rejects_sss_cancel_and_counter_gap(self):
        cancelled, cancelled_status = _fixture(route=0)
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(cancelled, cancelled_status)
        gap, gap_status = _fixture(gap_f=True)
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(gap, gap_status)

    def test_rejects_five_tick_tail_after_f(self):
        tail, status = _fixture(tail=5)
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(tail, status)

    def test_rejects_a_different_two_player_roster(self):
        setup = bytearray(_setup())
        setup[0x84] = 7
        data, status = _fixture(setup=bytes(setup))
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(data, status)

    def test_rejects_losing_stadium_selection_before_sss_exit(self):
        data, status = _fixture(sss_navigation=((18, 3, None, None),
                                                (0, 4, None, None)))
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(data, status)

    def test_rejects_foreign_stage_table_addresses_and_out_of_range_index(self):
        for navigation in (
            ((18, 3, 0x804D6CAF, None),),
            ((18, 3, None, 0x803F06DC),),
            ((30, 3, None, None),),
        ):
            data, status = _fixture(sss_navigation=navigation)
            with self.assertRaises(StadiumGoPrefixError):
                self._validate(data, status)

    def test_rejects_non_stadium_start_rules_kind_at_setup(self):
        setup = bytearray(_setup())
        setup[0x0E:0x10] = struct.pack(">H", 4)
        data, status = _fixture(setup=bytes(setup))
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(data, status)

    def test_rejects_non_stadium_start_rules_kind_at_go(self):
        go_setup = bytearray(_setup())
        go_setup[0x0E:0x10] = struct.pack(">H", 4)
        data, status = _fixture(go_setup=bytes(go_setup))
        with self.assertRaises(StadiumGoPrefixError):
            self._validate(data, status)



class StadiumBootGateControl(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("clang++") or shutil.which("g++")
        if cls.compiler is None:
            raise unittest.SkipTest("A native C++ compiler is not installed")
        cls.scratch = cls.new_workspace(ROOT, "stadium-boot-gate-")

    def test_exact_observe_gate_admits_only_boot_pad_before_css(self):
        observer = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"
        source = observer.read_text()
        enums = source[source.index("enum class Boundary : u16"):
                       source.index("// gmMain calls HSD_PadInit")]
        observe = source.index("  void Observe(Core::System*")
        begin = source.index("      if (stadium_go_prefix_phase ==", observe)
        end = source.index("    if (SdInitRequested())", begin)
        gate = source[begin:end].rsplit("    }", 1)[0]
        # Execute the exact production filter with a finite boundary-PC seam.
        # No guest memory, Observe implementation or match admission is mocked.
        harness = """#include <cassert>
#include <cstdint>
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
""" + enums + """
bool BoundaryForPC(u32 pc, bool whole, Boundary* out) {
  assert(!whole);
  if (pc == 0) return false;
  *out = static_cast<Boundary>(pc); return true;
}
bool admitted(StadiumGoPrefixPhase stadium_go_prefix_phase,
              bool stadium_go_prefix_sss_exit_seen, u32 pc) {
""" + gate.replace("return;", "return false;") + """
  return true;
}
int main() {
  for (u32 boundary=1; boundary<=30; ++boundary) {
    const bool pad = boundary == 1 || boundary == 2;
    assert(admitted(StadiumGoPrefixPhase::AwaitCss, false, boundary) == pad);
    assert(admitted(StadiumGoPrefixPhase::AwaitCss, true, boundary) == pad);
    assert(!admitted(StadiumGoPrefixPhase::Complete, true, boundary));
  }
  assert(!admitted(StadiumGoPrefixPhase::AwaitCss, false, 0));
  for (u32 boundary : {3u,4u,5u}) {
    assert(!admitted(StadiumGoPrefixPhase::AwaitSss, false, boundary));
    assert(admitted(StadiumGoPrefixPhase::AwaitVsEntry, true, boundary));
  }
}
"""
        harness = harness.replace("#include <cassert>", "#include <cassert>\n#include <initializer_list>")
        cpp, binary = self.scratch / "gate.cpp", self.scratch / "gate"
        cpp.write_text(harness)
        compiled = subprocess.run([self.compiler, "-std=c++17", str(cpp), "-o", str(binary)],
                                  capture_output=True, text=True, timeout=30)
        (self.scratch / "compile.log").write_text(compiled.stdout + compiled.stderr)
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        (self.scratch / "run.log").write_text(ran.stdout + ran.stderr)
        self.assertEqual(ran.returncode, 0, ran.stderr)

if __name__ == "__main__":
    unittest.main()
