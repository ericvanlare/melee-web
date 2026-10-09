from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin")]

import reference_observer_stream as observer_stream
from sheik_transform_prefix import TransformPrefixError, validate_transform_prefix


PLAYER_TABLE = 0x80453080
PLAYER_STRIDE = 0xE90
PLAYER_ENTITIES = 0xB0
GOBJ_USER_DATA = 0x2C
OBSERVER_SOURCE = ROOT / "reference-capture" / "dolphin" / "source" / "Core" / "PowerPC" / "ReferenceCaptureObserver.cpp"
P1_ZELDA_GOBJ = 0x80010000
P1_SHEIK_GOBJ = 0x80011000
P2_MARIO_GOBJ = 0x80012000
P1_ZELDA_FIGHTER = 0x80020000
P1_SHEIK_FIGHTER = 0x80021000
P2_MARIO_FIGHTER = 0x80022000
PAD_QUEUE = 0x804C1F78
PAD_SLOT = 0x804C3000
PAD_POLL_PC = 0x8034DD8C


def _retain_cpp_failure(directory: Path, command: list[str], result: subprocess.CompletedProcess) -> None:
    retained = os.environ.get("MWRC_SHEIK_FAILURE_DIR")
    if not retained:
        return
    target = Path(retained)
    target.mkdir(parents=True, exist_ok=True)
    (target / "owner.cpp").write_bytes((directory / "owner.cpp").read_bytes())
    (target / "command.json").write_text(json.dumps(command), encoding="utf-8")
    (target / "stdout.txt").write_text(result.stdout, encoding="utf-8")
    (target / "stderr.txt").write_text(result.stderr, encoding="utf-8")
    (target / "returncode.txt").write_text(str(result.returncode), encoding="utf-8")


def _retain_fixture(path: Path) -> None:
    retained = os.environ.get("MWRC_SHEIK_FAILURE_DIR")
    if not retained:
        return
    fixtures = Path(retained) / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    index = sum(1 for item in fixtures.glob("fixture-*.mwro"))
    target = fixtures / f"fixture-{index:03d}.mwro"
    shutil.copyfile(path, target)
    shutil.copyfile(Path(str(path) + ".status.json"),
                    Path(str(target) + ".status.json"))


def _frame(event: int, sequence: int, payload: bytes, *, pc: int = 0,
           source_tick: int = 0) -> bytes:
    header = observer_stream.HEADER.pack(
        observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION, event,
        sequence, sequence + 100, pc, source_tick, 0, len(payload),
        zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def _json(event: int, sequence: int, value: dict) -> bytes:
    return _frame(event, sequence, json.dumps(value, separators=(",", ":")).encode())


def _setup() -> bytes:
    raw = bytearray(0x138)
    raw[4] = 0x40
    raw[2] = 0x80
    raw[0x0E:0x10] = struct.pack(">H", 32)
    raw[0x2C:0x30] = bytes.fromhex("3f800000")
    for index in range(6):
        base = 0x60 + index * 0x24
        raw[base + 1] = 3
    raw[0x60:0x64] = bytes((18, 0, 4, 0))
    raw[0x84:0x88] = bytes((8, 0, 4, 0))
    return bytes(raw)


def _fighter(kind: int, player_id: int, motion: int, gobj: int) -> bytes:
    raw = bytearray(0x100)
    raw[:4] = struct.pack(">I", gobj)
    raw[4:8] = struct.pack(">I", kind)
    raw[0x0C] = player_id
    raw[0x10:0x14] = struct.pack(">I", motion)
    raw[0xE0:0xE4] = struct.pack(">I", 0)
    return bytes(raw)


def _sample_slices(*, transformed=(0, 1), zelda_kind=19, zelda_motion=14,
                   zelda_ground_air=0, sheik_motion=362, sheik_ground_air=0,
                   zelda_gobj=P1_ZELDA_GOBJ,
                   zelda_fighter=P1_ZELDA_FIGHTER,
                   setup=False) -> list[tuple[int, int, int, bytes]]:
    values = []
    if setup:
        values.append((4, 0, 0x80400000, _setup()))
    p1 = PLAYER_TABLE
    values.extend([
        (52, 0, p1 + PLAYER_ENTITIES,
         struct.pack(">II", zelda_gobj, P1_SHEIK_GOBJ)),
        (58, 0, p1 + 0x0C, bytes(transformed)),
        (53, 0, zelda_gobj + GOBJ_USER_DATA, struct.pack(">I", zelda_fighter)),
        (5, 0, zelda_fighter, _fighter(zelda_kind, 0, zelda_motion, zelda_gobj)),
        (53, 0x100, P1_SHEIK_GOBJ + GOBJ_USER_DATA,
         struct.pack(">I", P1_SHEIK_FIGHTER)),
        (5, 0x100, P1_SHEIK_FIGHTER,
         _fighter(7, 0, sheik_motion, P1_SHEIK_GOBJ)),
        (52, 1, PLAYER_TABLE + PLAYER_STRIDE + PLAYER_ENTITIES,
         struct.pack(">II", P2_MARIO_GOBJ, 0)),
        (53, 1, P2_MARIO_GOBJ + GOBJ_USER_DATA,
         struct.pack(">I", P2_MARIO_FIGHTER)),
        (5, 1, P2_MARIO_FIGHTER, _fighter(0, 1, 14, P2_MARIO_GOBJ)),
    ])
    for index, (tag, flags, address, raw) in enumerate(values):
        if tag == 5 and flags == 0 and zelda_ground_air:
            value = bytearray(raw)
            value[0xE0:0xE4] = struct.pack(">I", zelda_ground_air)
            values[index] = (tag, flags, address, bytes(value))
        elif tag == 5 and flags == 0x100 and sheik_ground_air:
            value = bytearray(raw)
            value[0xE0:0xE4] = struct.pack(">I", sheik_ground_air)
            values[index] = (tag, flags, address, bytes(value))
    return values


def _boundary_payload(kind: int, values: list[tuple[int, int, int, bytes]], *,
                      pad_slot: bytes | None = None) -> bytes:
    if pad_slot is not None:
        queue = bytearray(12)
        queue[0] = 1
        queue[8:12] = struct.pack(">I", PAD_SLOT)
        values = [(2, 0, PAD_QUEUE, bytes(queue)), (3, 0, PAD_SLOT, pad_slot)]
    prefix = observer_stream.BOUNDARY.pack(kind, 0, 0x80300000, 32, len(values), 0)
    registers = [0] * 32
    if pad_slot is not None:
        registers[25] = PAD_SLOT
    gprs = struct.pack("<32I", *registers)
    offset = observer_stream.BOUNDARY.size + len(gprs) + observer_stream.SLICE.size * len(values)
    descriptors = bytearray()
    data = bytearray()
    for tag, flags, address, raw in values:
        descriptors += observer_stream.SLICE.pack(tag, flags, address, len(raw), offset)
        data += raw
        offset += len(raw)
    return prefix + gprs + descriptors + data


def _menu_owner_poll(scene_kind: int, *, mode=2, css_live=True,
                     sss_live=True) -> bytes:
    values = [
        (17, 0, 0x80479D30, bytes((mode, 0, 0, 0, 0, 0))),
        (40, 0, 0x803D0000 + scene_kind * 0x100, bytes((scene_kind,))),
    ]
    if scene_kind == 8 and css_live:
        values.extend([
            (48, 0, 0x80400000, bytes(0x148)),
            (44, 0, 0x803F0DFC, bytes(0x90)),
        ])
    if scene_kind == 9 and sss_live:
        stage_index = 4
        kind_address = 0x803F06D0 + stage_index * 0x1C + 0x0B
        values.extend([
            (41, 0, 0x804D6CAE, bytes((stage_index,))),
            # Readiness proves the live source owner exists; setup remains the
            # authority for the selected Final Destination stage identity.
            (42, 0, kind_address, bytes((0x91,))),
        ])
    return _boundary_payload(1, values)


def _pad_slot(*, down_b=False, bad_port=None) -> bytes:
    raw = bytearray(0x30)
    raw[11], raw[23], raw[35], raw[47] = 0xA1, 0xA2, 0xA3, 0xA4
    raw[34] = raw[46] = 0xFF
    if down_b:
        raw[0:2] = struct.pack(">H", 0x0200)
        raw[3] = 0xB0  # authored raw y=-80
    if bad_port == 2:
        raw[24] = 1
    elif bad_port == 3:
        raw[46] = 0
    elif bad_port == 0:
        raw[10] = 1
    elif bad_port == 1:
        raw[12] = 1
    return bytes(raw)


def _capture(path: Path, *, swap=True, mutations=None, ticks=4) -> Path:
    mutations = mutations or {}
    records = [
        _json(1, 0, {"schema": "melee-web-passive-dolphin-observer", "version": 1,
                     "dolphin_commit": "c77bbaa0f372c3f72281602a8b087206706542cb",
                     "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
                     "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                     "cpu": "JITARM64",
                     "writes_guest_memory": False, "diagnostic": "sheik_transform_prefix",
                     "max_active_source_ticks": 600,
                     "completion_boundary": "active_sheik_grounded_neutral_source_tick_after_owner_change",
                     **({"dolphin_commit": "0" * 40} if mutations.get("bad_provenance") else {})}),
        _json(2, 1, {"status": "recording", "source_revision": "GALE01r2",
                     "diagnostic": "sheik_transform_prefix", "max_active_source_ticks": 600,
                     "completion_boundary": "active_sheik_grounded_neutral_source_tick_after_owner_change"}),
    ]
    sequence = 2
    if mutations.get("sss_before_css"):
        records.append(_frame(3, sequence, _menu_owner_poll(9),
                              pc=PAD_POLL_PC, source_tick=0))
        sequence += 1
    if not mutations.get("missing_css_owner"):
        records.append(_frame(3, sequence, _menu_owner_poll(
            8, mode=1 if mutations.get("wrong_css_mode") else 2,
            css_live=not mutations.get("missing_css_live_state")),
            pc=PAD_POLL_PC, source_tick=0))
        sequence += 1
    if not mutations.get("sss_before_css") and not mutations.get("missing_sss_owner"):
        records.append(_frame(3, sequence, _menu_owner_poll(
            9, sss_live=not mutations.get("missing_sss_kind")),
            pc=PAD_POLL_PC, source_tick=0))
        sequence += 1
    records.append(_frame(3, sequence, _boundary_payload(5, _sample_slices(
            transformed=mutations.get("initial_transformed", (0, 1)),
            zelda_motion=99 if mutations.get("setup_airborne") else 14,
            zelda_ground_air=1 if mutations.get("setup_airborne") else 0, setup=True)),
               pc=0x8016E9C4, source_tick=0))
    sequence += 1
    neutral_pad_sequence = None
    if not mutations.get("missing_neutral_consume"):
        neutral_pad_sequence = sequence
        records.append(_frame(3, sequence, _boundary_payload(2, [], pad_slot=_pad_slot()),
                              pc=0x80377584, source_tick=0))
        sequence += 1
    # Setup is not the readiness signal: require a consumed neutral PAD row,
    # followed by a completed grounded Zelda source tick.
    first_source_tick = 1 if mutations.get("missing_initial_source_tick") else 0
    grounded_source_sequence = sequence
    records.append(_frame(3, sequence, _boundary_payload(6, _sample_slices(
        zelda_motion=14, zelda_ground_air=0)), pc=0x80390EB4,
                          source_tick=first_source_tick))
    sequence += 1
    down_b_sequence = sequence
    records.append(_frame(3, sequence, _boundary_payload(
        2, [], pad_slot=_pad_slot(down_b=not mutations.get("no_down_b", False),
                                  bad_port=mutations.get("bad_port"))),
                          pc=0x80377584, source_tick=0))
    sequence += 1
    if mutations.get("release", True):
        records.append(_frame(3, sequence, _boundary_payload(2, [], pad_slot=_pad_slot()),
                              pc=0x80377584, source_tick=0))
        sequence += 1
    if mutations.get("second_episode"):
        records.append(_frame(3, sequence, _boundary_payload(2, [], pad_slot=_pad_slot(down_b=True)),
                              pc=0x80377584, source_tick=0))
        sequence += 1
        records.append(_frame(3, sequence, _boundary_payload(2, [], pad_slot=_pad_slot()),
                              pc=0x80377584, source_tick=0))
        sequence += 1
    last_source_tick = first_source_tick
    for index in range(1, ticks):
        is_after = swap and index >= 2
        transformed = mutations.get("after_transformed", (1, 0) if is_after else (0, 1))
        zelda_kind = mutations.get("after_zelda_kind", 19) if is_after else 19
        zelda_motion = 355 if index == 1 else 14
        sheik_motion = (14 if index >= 3 and not mutations.get("no_post_swap_neutral")
                        else 362)
        sheik_ground_air = 1 if (index >= 3 and mutations.get("no_post_swap_neutral")) else 0
        values = _sample_slices(transformed=transformed, zelda_kind=zelda_kind,
                                zelda_motion=zelda_motion, sheik_motion=sheik_motion,
                                sheik_ground_air=sheik_ground_air)
        if mutations.get("bad_user_data") and is_after:
            values[2] = (53, 0, P1_ZELDA_GOBJ + GOBJ_USER_DATA,
                         struct.pack(">I", P1_SHEIK_FIGHTER))
        tick = index + int(bool(mutations.get("source_tick_gap")))
        if mutations.get("missing_initial_source_tick"):
            tick += 1
        last_source_tick = tick
        records.append(_frame(3, sequence, _boundary_payload(6, values),
                              pc=0x80390EB4, source_tick=tick))
        sequence += 1
    readiness_source_sequence = {
        "neutral_pad_consume": neutral_pad_sequence,
        "grounded_neutral_source_tick": grounded_source_sequence,
        "down_b_consume": down_b_sequence,
    }
    if mutations.get("bad_readiness_source_sequence"):
        readiness_source_sequence["grounded_neutral_source_tick"] += 1
    records.append(_json(6, sequence, {
        "status": "completed", "natural": True,
        "completion_boundary": ("wrong_boundary" if mutations.get("bad_completion") else
                                "active_sheik_grounded_neutral_source_tick_after_owner_change"),
        "match_complete": bool(mutations.get("bad_completion")),
        "readiness_source_sequence": readiness_source_sequence,
    }))
    sequence += 1
    path.write_bytes(b"".join(records))
    status = {
        "state": "completed", "event_count": sequence, "last_seq": sequence - 1,
        "source_tick": last_source_tick, "draw_ordinal": 0,
        "completed": True, "invalid": False, "error": None,
    }
    Path(str(path) + ".status.json").write_text(json.dumps(status), encoding="utf-8")
    _retain_fixture(path)
    return path


class SheikTransformPrefixTests(unittest.TestCase):
    def test_actual_dispatch_reaches_transform_returns_without_enabling_other_probes(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = OBSERVER_SOURCE.read_text(encoding="utf-8")
        dispatcher = source[source.index("static bool IsCaptureBoundary("):
                            source.index("bool Observer::IsRngReturnBoundary(")]
        request_helpers = source[source.index("bool SdInitRequested()"):
                                 source.index("bool SparsePadErrorsValid(")]
        harness = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
using u32 = uint32_t;
constexpr u32 SSS_ENTER_RETURN = 0x8025B84C;
std::map<std::string, std::string> environment;
std::string Env(const char* name) { return environment[name]; }
bool CpuProbeEnabled() { return false; }
bool ItemProbeEnabled() { return false; }
const void* FindCpuProbePoint(u32) { return nullptr; }
const void* FindItemProbePoint(u32) { return nullptr; }
struct Settings { u32 rng_return_pc = 0; } settings;
const Settings& CpuProbeEnvironment() { return settings; }
''' + request_helpers + dispatcher + r'''
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  bool sss = false, sd = false;
  if (mode == "transform") { environment["MWRC_TRANSFORM_PREFIX"] = "1"; sss = true; }
  else if (mode == "invalid-transform") environment["MWRC_TRANSFORM_PREFIX"] = "0";
  else if (mode == "ordinary-without-sd") environment["MWRC_SD_MENU_PROBE"] = "ordinary_timeout";
  else if (mode != "default") {
    environment["MWRC_SD_INIT"] = "1"; sd = true;
    environment["MWRC_SD_MENU_PROBE"] = mode;
    sss = mode == "ordinary_timeout" || mode == "sd_prefix" ||
          mode == "competitive_entry" || mode == "sparse_pair";
  }
  // Every transform-required existing PC remains reachable under all scopes.
  for (u32 pc : {0x8026688Cu, 0x802669F0u, 0x8025A998u, 0x8034DD8Cu,
                 0x80377584u, 0x800693A8u, 0x8016E934u, 0x8016E9C4u,
                 0x80390EB4u, 0x80390FC0u, 0x80391040u, 0x8039157Cu})
    assert(IsCaptureBoundary(pc));
  assert(IsCaptureBoundary(SSS_ENTER_RETURN) == sss);
  assert(IsCaptureBoundary(0x8016EBC0) == sd);
  assert(IsCaptureBoundary(0x8016EC24) == sd);
  assert(!IsCaptureBoundary(0x8025B850));
  assert(!IsCaptureBoundary(0xDEADBEEF));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "owner.cpp").write_text(harness, encoding="utf-8")
            command = [compiler, "-std=c++17", "-Wall", "-Werror",
                       str(path / "owner.cpp"), "-o", str(path / "owner")]
            built = subprocess.run(command, capture_output=True, text=True)
            if built.returncode:
                _retain_cpp_failure(path, command, built)
            self.assertEqual(built.returncode, 0, built.stderr)
            for mode in ("default", "transform", "invalid-transform", "ordinary-without-sd",
                         "ordinary_timeout", "sd_prefix", "competitive_entry", "sparse_pair",
                         "rules_ready"):
                with self.subTest(mode=mode):
                    command = [str(path / "owner"), mode]
                    checked = subprocess.run(command, capture_output=True, text=True)
                    if checked.returncode:
                        _retain_cpp_failure(path, command, checked)
                    self.assertEqual(checked.returncode, 0, checked.stderr)

    def test_entity_identity_and_transform_tags_remain_distinct_in_actual_namespace(self):
        source = OBSERVER_SOURCE.read_text(encoding="utf-8")
        enum = source[source.index("enum class SliceTag : u16"):
                      source.index("struct SliceRef")]
        entries = re.findall(r"^\s+(\w+) = (\d+),", enum, re.MULTILINE)
        tags = {name: int(value) for name, value in entries}
        self.assertEqual(sorted(tags.values()), list(range(1, 59)))
        self.assertEqual(tags["PlayerIdentity"], 57)
        self.assertEqual(tags["PlayerTransformed"], 58)
        identity = struct.pack(">III", 2, 18, 0) + bytes((0, 0, 0, 0))
        transformed = bytes((0, 1))
        values = [(57, 0, PLAYER_TABLE, identity),
                  (58, 0, PLAYER_TABLE + 0x0C, transformed)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "namespace.mwro"
            path.write_bytes(_frame(1, 0, b'{}') + _frame(2, 1, b'{}') +
                             _frame(3, 2, _boundary_payload(5, values)))
            records = list(observer_stream.iter_records(path))
        slices = records[-1]["payload"]["slices"]
        self.assertEqual([(row["name"], row["hex"]) for row in slices],
                         [("player_identity", identity.hex()),
                          ("player_transformed", transformed.hex())])

    def test_valid_prefix_requires_ready_action_swap_and_post_swap_neutral_tick(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"setup_airborne": True})
            report = validate_transform_prefix(source)
        self.assertEqual(report["status"], "pass")
        self.assertTrue(report["input"]["p1_down_b_consumed"])
        self.assertEqual(report["portable_comparison_fields"]["before_active_index"], 0)
        self.assertEqual(report["portable_comparison_fields"]["after_active_index"], 1)
        self.assertEqual(report["portable_comparison_fields"]["before_kinds"], [19, 7])
        self.assertEqual(report["active_source_ticks"], 4)
        self.assertEqual(report["completion"]["match_complete"], False)
        self.assertEqual(report["completion"]["post_swap_neutral_source_tick"]["source_tick"], 3)
        self.assertEqual(report["input"]["readiness_source_sequence"], {
            "neutral_pad_consume": 5,
            "grounded_neutral_source_tick": 6,
            "down_b_consume": 7,
        })
        self.assertEqual(report["menu_owner_readiness"], {
            "css_live_owner_pad_poll_sequence": 2,
            "sss_live_owner_pad_poll_sequence": 3,
            "ordered_before_setup": True,
            "meaning": "mode-2 live source-owner slices after the observer's checked menu-return gates; does not compare CSS/SSS scalar state or prove consumed input",
        })
        self.assertNotEqual(report["initial_owner"]["local_pointer_checks"]["fighter_pointers"][0],
                            report["initial_owner"]["local_pointer_checks"]["fighter_pointers"][1])

    def test_setup_requires_ordered_live_css_and_sss_owner_polls(self):
        cases = (
            ({"missing_css_owner": True}, "SSS live owner preceded"),
            ({"wrong_css_mode": True}, "SSS live owner preceded"),
            ({"missing_css_live_state": True}, "SSS live owner preceded"),
            ({"missing_sss_owner": True}, "setup preceded ordered live CSS and SSS"),
            ({"missing_sss_kind": True}, "setup preceded ordered live CSS and SSS"),
            ({"sss_before_css": True}, "SSS live owner preceded"),
        )
        for mutation, expected in cases:
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                source = _capture(Path(directory) / "capture.mwro", mutations=mutation)
                with self.assertRaisesRegex(TransformPrefixError, expected):
                    validate_transform_prefix(source)

    def test_dormant_sheik_without_owner_swap_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", swap=False)
            with self.assertRaisesRegex(TransformPrefixError, "lacks grounded readiness.*active 19→7"):
                validate_transform_prefix(source)

    def test_unpinned_source_identity_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"bad_provenance": True})
            with self.assertRaisesRegex(TransformPrefixError, "pinned passive Sheik-prefix"):
                validate_transform_prefix(source)

    def test_source_tick_gap_and_cap_are_strict(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", ticks=3,
                              mutations={"source_tick_gap": True})
            with self.assertRaisesRegex(TransformPrefixError, "gap or reordering"):
                validate_transform_prefix(source)

        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", ticks=601,
                              mutations={"no_post_swap_neutral": True})
            with self.assertRaisesRegex(TransformPrefixError, "600 active source-tick cap"):
                validate_transform_prefix(source)

    def test_first_source_tick_must_be_zero_and_post_swap_neutral_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"missing_initial_source_tick": True})
            with self.assertRaisesRegex(TransformPrefixError, "missing original tick zero"):
                validate_transform_prefix(source)

        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"no_post_swap_neutral": True})
            with self.assertRaisesRegex(TransformPrefixError, "post-swap neutral tick"):
                validate_transform_prefix(source)

    def test_all_four_source_pad_statuses_are_pinned_and_padding_is_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro")
            validate_transform_prefix(source)
        for port in (0, 1, 2, 3):
            with self.subTest(port=port), tempfile.TemporaryDirectory() as directory:
                source = _capture(Path(directory) / "capture.mwro",
                                  mutations={"bad_port": port})
                with self.assertRaisesRegex(TransformPrefixError, "four-port source statuses"):
                    validate_transform_prefix(source)

    def test_completion_label_does_not_claim_match_end(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"bad_completion": True})
            with self.assertRaisesRegex(TransformPrefixError,
                                        "declared bounded prefix completion boundary"):
                validate_transform_prefix(source)

    def test_malformed_terminal_status_is_a_diagnostic_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro")
            Path(str(source) + ".status.json").write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(TransformPrefixError, "status sidecar is malformed"):
                validate_transform_prefix(source)

    def test_same_pointer_kind_flip_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"after_zelda_kind": 7})
            with self.assertRaisesRegex(TransformPrefixError, "ownership link differs"):
                validate_transform_prefix(source)

    def test_malformed_owner_links_and_transform_indexes_are_rejected(self):
        for mutation, message in (({"bad_user_data": True}, "ownership link differs"),
                                  ({"after_transformed": (0, 0)}, "not a permutation")):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                source = _capture(Path(directory) / "capture.mwro", mutations=mutation)
                with self.assertRaisesRegex(TransformPrefixError, message):
                    validate_transform_prefix(source)

    def test_missing_consumed_down_b_or_second_episode_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"missing_neutral_consume": True})
            with self.assertRaisesRegex(TransformPrefixError, "consumed neutral PAD record"):
                validate_transform_prefix(source)

        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", mutations={"release": False})
            with self.assertRaisesRegex(TransformPrefixError, "neutral release"):
                validate_transform_prefix(source)

        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", mutations={"no_down_b": True})
            with self.assertRaisesRegex(TransformPrefixError, r"down\+B"):
                validate_transform_prefix(source)

        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro", mutations={"second_episode": True})
            with self.assertRaisesRegex(TransformPrefixError, "more than one"):
                validate_transform_prefix(source)

    def test_terminal_readiness_sequences_must_match_observed_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            source = _capture(Path(directory) / "capture.mwro",
                              mutations={"bad_readiness_source_sequence": True})
            with self.assertRaisesRegex(TransformPrefixError, "ordered neutral-readiness source sequences"):
                validate_transform_prefix(source)

    def test_extracted_original_owner_reader_keeps_distinct_entities_and_links(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = OBSERVER_SOURCE.read_text(encoding="utf-8")
        source_slot_and_flags = source[source.index("  bool ReadFighterSourceSlot("):
                                       source.index("  bool RegisterFighterEntity(")]
        pad_status_helper = source[source.index("bool TransformPrefixPadStatusMatches("):
                                   source.index("u32 WholeSessionMatchCount()")]
        read_u32 = source[source.index("  bool ReadU32("):
                          source.index("  bool TransformPrefixRosterValid(")]
        owner_methods = source[source.index("  bool TransformPrefixRosterValid("):
                                  source.index("  bool ReadMem1(")]
        slice_tags = source[source.index("enum class SliceTag : u16"):
                            source.index("struct SliceRef")]
        harness = r"""
#include <array>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
constexpr u16 ReadBE16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
""" + pad_status_helper + r"""
namespace Core { struct System {}; }
""" + slice_tags + r"""
static_assert(static_cast<u16>(SliceTag::PlayerIdentity) == 57);
static_assert(static_cast<u16>(SliceTag::PlayerTransformed) == 58);
struct SliceRef { SliceTag tag; u16 flags; u32 address; u32 size; std::vector<u8> bytes; };
struct Reader {
  std::unordered_map<u32, u8> memory;
  std::vector<SliceRef> slices;
  u32 setup_pointer = 0x80400000;
  u32 active_slot_count = 2;
  std::array<bool, 4> cpu_slots{};
  std::array<std::array<u32, 2>, 4> fighter_entity_pointers{};
  std::array<std::array<u32, 2>, 4> fighter_entity_kinds{};
  std::array<u8, 4> fighter_entity_count{};
  bool IsMem1Range(u32 address, size_t size) const {
    return address >= 0x80000000 && address <= 0x81800000 &&
           size <= 0x81800000 - address;
  }
  bool ReadBytes(Core::System*, u32 address, size_t size, u8* output) const {
    if (!IsMem1Range(address, size)) return false;
    for (size_t i = 0; i < size; ++i) {
      const auto found = memory.find(address + static_cast<u32>(i));
      if (found == memory.end()) return false;
      output[i] = found->second;
    }
    return true;
  }
  bool AddSlice(Core::System* system, SliceTag tag, u32 address, size_t size, u16 flags = 0) {
    std::vector<u8> bytes(size);
    if (!ReadBytes(system, address, size, bytes.data())) return false;
    slices.push_back({tag, flags, address, static_cast<u32>(size), bytes});
    return true;
  }
  static u16 ReadBE16(const u8* p) { return static_cast<u16>((p[0] << 8) | p[1]); }
  static u32 ReadBE32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | p[3];
  }
  void byte(u32 address, u8 value) { memory[address] = value; }
  void word(u32 address, u32 value) {
    for (int i = 0; i < 4; ++i) byte(address + i, static_cast<u8>(value >> (24 - i * 8)));
  }
""" + source_slot_and_flags + read_u32 + owner_methods + r"""
};
static void seed(Reader& r) {
  Core::System system;
  std::array<u8, 0x138> setup{};
  setup[4] = 0x40; setup[2] = 0x80; setup[0x0e] = 0; setup[0x0f] = 32;
  setup[0x60] = 18; setup[0x61] = 0; setup[0x62] = 4;
  setup[0x84] = 8; setup[0x85] = 0; setup[0x86] = 4;
  for (size_t i = 0; i < setup.size(); ++i) r.byte(r.setup_pointer + i, setup[i]);
  const u32 p1 = 0x80453080, p2 = p1 + 0xe90;
  const u32 zelda_gobj = 0x80010000, sheik_gobj = 0x80011000, mario_gobj = 0x80012000;
  const u32 zelda = 0x80020000, sheik = 0x80021000, mario = 0x80022000;
  r.byte(p1 + 0x0c, 0); r.byte(p1 + 0x0d, 1);
  r.word(p1 + 0xb0, zelda_gobj); r.word(p1 + 0xb4, sheik_gobj);
  r.word(p2 + 0xb0, mario_gobj); r.word(p2 + 0xb4, 0);
  r.word(zelda_gobj + 0x2c, zelda); r.word(sheik_gobj + 0x2c, sheik);
  r.word(mario_gobj + 0x2c, mario);
  for (u32 base : std::array<u32, 3>{zelda, sheik, mario})
    for (u32 i = 0; i < 0x100; ++i) r.byte(base + i, 0);
  r.word(zelda, zelda_gobj); r.word(zelda + 4, 19); r.byte(zelda + 0x0c, 0);
  r.word(sheik, sheik_gobj); r.word(sheik + 4, 7); r.byte(sheik + 0x0c, 0);
  r.word(mario, mario_gobj); r.word(mario + 4, 0); r.byte(mario + 0x0c, 1);
  r.fighter_entity_count = {2, 1, 0, 0};
  r.fighter_entity_pointers[0] = {zelda, sheik}; r.fighter_entity_pointers[1][0] = mario;
  r.fighter_entity_kinds[0] = {19, 7}; r.fighter_entity_kinds[1][0] = 0;
  assert(r.TransformPrefixRosterValid(&system));
}
int main() {
  assert(ClassifyTransformPrefixEntry(0x18, false) ==
         TransformPrefixEntryDisposition::IgnoreOpeningAttract);
  assert(ClassifyTransformPrefixEntry(0x02, false) ==
         TransformPrefixEntryDisposition::AcceptVsEntry);
  assert(ClassifyTransformPrefixEntry(0x18, true) ==
         TransformPrefixEntryDisposition::Reject);
  assert(ClassifyTransformPrefixEntry(0x02, true) ==
         TransformPrefixEntryDisposition::Reject);
  assert(ClassifyTransformPrefixEntry(0x01, false) ==
         TransformPrefixEntryDisposition::Reject);
  assert(TransformPrefixSourceTickIsNext(false, 0, 0));
  assert(!TransformPrefixSourceTickIsNext(false, 0, 1));
  assert(TransformPrefixSourceTickIsNext(true, 7, 8));
  assert(!TransformPrefixSourceTickIsNext(true, 7, 9));
  assert(!TransformPrefixSourceTickIsNext(true, std::numeric_limits<u32>::max(), 0));
  assert(TransformPrefixReadinessSequencesValid(3, 4, 5));
  assert(!TransformPrefixReadinessSequencesValid(4, 3, 5));
  assert(!TransformPrefixReadinessSequencesValid(3, 5, 5));
  assert(TransformPrefixCssOwnerReady(true, true, true, true));
  assert(!TransformPrefixCssOwnerReady(false, true, true, true));
  assert(!TransformPrefixCssOwnerReady(true, false, true, true));
  assert(!TransformPrefixCssOwnerReady(true, true, false, true));
  assert(TransformPrefixSssOwnerReady(true, true, true, true, true));
  assert(!TransformPrefixSssOwnerReady(true, false, true, true, true));
  assert(!TransformPrefixSssOwnerReady(true, true, false, true, true));
  assert(!TransformPrefixSssOwnerReady(true, true, true, true, false));
  assert(TransformPrefixMenuOwnersReady(true, true));
  assert(!TransformPrefixMenuOwnersReady(true, false));
  assert(!TransformPrefixMenuOwnersReady(false, true));
  assert(!TransformPrefixTeardownArmed(true, false));
  assert(TransformPrefixTeardownArmed(true, true));
  assert(!TransformPrefixTeardownArmed(false, true));

  std::array<u8, 0x30> pad{};
  pad[11] = 0xa1; pad[23] = 0xa2; pad[35] = 0xa3; pad[47] = 0xa4;
  pad[34] = pad[46] = 0xff;
  assert(TransformPrefixPadStatusMatches(pad.data(), false));
  pad[0] = 0x02; pad[3] = 0xb0;
  assert(TransformPrefixPadStatusMatches(pad.data(), true));
  pad[24] = 1;
  assert(!TransformPrefixPadStatusMatches(pad.data(), true));
  pad[24] = 0; pad[46] = 0;
  assert(!TransformPrefixPadStatusMatches(pad.data(), true));
  pad[46] = 0xff; pad[10] = 1;
  assert(!TransformPrefixPadStatusMatches(pad.data(), true));
  pad[10] = 0; pad[12] = 1;
  assert(!TransformPrefixPadStatusMatches(pad.data(), true));
  pad[12] = 0; pad[22] = 0xff;
  assert(!TransformPrefixPadStatusMatches(pad.data(), true));

  Core::System system;
  Reader reader; seed(reader);
  u32 index = 99, kind = 99, fighter = 0;
  assert(reader.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  assert(index == 0 && kind == 19 && fighter == 0x80020000);
  assert(reader.slices.size() == 9);
  assert(reader.slices[0].tag == SliceTag::PlayerEntities && reader.slices[0].flags == 0);
  assert(reader.slices[0].bytes == std::vector<u8>({0x80, 0x01, 0, 0, 0x80, 0x01, 0x10, 0}));
  assert(reader.slices[1].tag == SliceTag::PlayerTransformed &&
         reader.slices[1].address == 0x8045308c && reader.slices[1].bytes == std::vector<u8>({0, 1}));
  assert(reader.slices[2].tag == SliceTag::PlayerEntityUserData &&
         reader.slices[2].address == 0x80010000 + 0x2c && reader.slices[2].flags == 0 &&
         reader.slices[2].bytes == std::vector<u8>({0x80, 0x02, 0, 0}));
  assert(reader.slices[4].tag == SliceTag::PlayerEntityUserData &&
         reader.slices[4].address == 0x80011000 + 0x2c && reader.slices[4].flags == 0x100 &&
         reader.slices[4].bytes == std::vector<u8>({0x80, 0x02, 0x10, 0}));
  assert(reader.slices[5].tag == SliceTag::FighterHead && reader.slices[5].flags == 0x100);
  assert(reader.slices[6].tag == SliceTag::PlayerEntities && reader.slices[6].flags == 1 &&
         reader.slices[6].bytes == std::vector<u8>({0x80, 0x01, 0x20, 0, 0, 0, 0, 0}));
  reader.slices.clear(); reader.byte(0x8045308c, 1); reader.byte(0x8045308d, 0);
  assert(reader.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  assert(index == 1 && kind == 7 && fighter == 0x80021000);

  Reader bad_permutation; seed(bad_permutation);
  bad_permutation.byte(0x8045308d, 0);
  assert(!bad_permutation.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  Reader bad_link; seed(bad_link); bad_link.word(0x80010000 + 0x2c, 0x80021000);
  assert(!bad_link.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  Reader same_pointer_kind_flip; seed(same_pointer_kind_flip);
  same_pointer_kind_flip.word(0x80020000 + 4, 7);
  assert(!same_pointer_kind_flip.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  Reader bad_player; seed(bad_player); bad_player.byte(0x80021000 + 0x0c, 1);
  assert(!bad_player.AddTransformPrefixOwnerSlices(&system, &index, &kind, &fighter));
  Reader bad_roster; seed(bad_roster); bad_roster.byte(0x80400000 + 0x60, 7);
  assert(!bad_roster.TransformPrefixRosterValid(&system));
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "owner.cpp").write_text(harness, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                    str(path / "owner.cpp"), "-o", str(path / "owner")],
                                   capture_output=True, text=True)
            if built.returncode != 0:
                _retain_cpp_failure(path, [compiler, "-std=c++17", "-Wall", "-Werror",
                                           str(path / "owner.cpp"), "-o", str(path / "owner")],
                                    built)
            self.assertEqual(built.returncode, 0, built.stderr)
            checked = subprocess.run([str(path / "owner")], capture_output=True, text=True)
            if checked.returncode != 0:
                _retain_cpp_failure(path, [str(path / "owner")], checked)
            self.assertEqual(checked.returncode, 0, checked.stderr)


if __name__ == "__main__":
    unittest.main()
