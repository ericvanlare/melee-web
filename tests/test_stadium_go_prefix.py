from __future__ import annotations

import hashlib
import json
import os
import re
from contextlib import contextmanager
from unittest.mock import patch
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
RETAINED_RAW_REPLAY_ENV = "MELEE_WEB_STADIUM_GO_RAW_REPLAY"
RETAINED_RAW_REPLAY_BYTES = 2_139_497
RETAINED_RAW_REPLAY_SHA256 = "18575587bdf014d088a33d69f70a9e1c90d8f25044aa37d09426c32aadf9315c"
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"),
                str(ROOT / "scripts")]

import reference_observer_stream as observer_stream
from stadium_go_prefix import (  # noqa: E402
    DIAGNOSTIC,
    EXPECTED_SETUP_RECEIPT_SHA256,
    SCENE_ROUTING_TAG,
    StadiumGoPrefixError,
    SOURCE_TICK_PC,
    FIRST_CSS_CONTEXT_BYTES,
    FIRST_CSS_CONTEXT_ENV,
    FIRST_CSS_CONTEXT_MAGIC,
    FIRST_CSS_CONTEXT_VERSION,
    FIRST_CSS_DATA_ADDRESS,
    FIRST_CSS_ENTRY_SEQUENCE,
    FIRST_CSS_KO_ADDRESS,
    FIRST_CSS_PAD_ADDRESS,
    FIRST_CSS_RETURN_SEQUENCE,
    FIRST_CSS_RNG_POINTER_ADDRESS,
    FIRST_CSS_RNG_VALUE_ADDRESS,
    FIRST_CSS_RULES_ADDRESS,
    FIRST_CSS_SAVE_ADDRESS,
    FIRST_CSS_SCENE_ADDRESS,
    FIRST_CSS_SCENE_FRAME_ADDRESS,
    FIRST_CSS_STREAM_BYTES,
    FIRST_CSS_STREAM_SHA256,
    FIRST_CSS_CONSUME_SEQUENCE,
    FIRST_CSS_SOURCE_TICK_SEQUENCE,
    FIRST_CSS_CONSUMED_PAD_BYTES,
    FIRST_CSS_CONSUMED_PAD_MAGIC,
    FIRST_CSS_CONSUMED_PAD_VERSION,
    SCENE_ROUTING_ADDRESS,
    _extract_first_css_consumed_tick_rows,
    _extract_first_css_context_rows,
    decode_first_css_consumed_pad_bundle,
    decode_first_css_context_bundle,
    extract_stadium_first_css_consumed_tick,
    extract_stadium_first_css_context,
    _check_boundary_contract,
    _classify_stadium_sss_owner,
    _slice as _read_source_slice,
    validate_stadium_go_prefix,
)
from sd_reference_diagnostic import SdDiagnosticError, require  # noqa: E402
from retail_input_plan import NEUTRAL_PAD  # noqa: E402
from sd_original_menu_plan import (  # noqa: E402
    matches,
    stadium_go_prefix_packet,
    validate_packet,
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


def _pad_poll(scene_kind: int | None, *, live_owner: bool = True,
              stage_index: int = 18, stage_kind: int | None = 3,
              index_address: int = 0x804D6CAE,
              kind_address: int | None = None,
              routing_address: int = 0x80479D30,
              routing_flags: int = 0) -> list[dict]:
    values = [
        _slice(1, 0x30), _slice(2, 0xC),
        _slice(17, 6, data=bytes((2, 0, 0, 0, 0, 0)), address=routing_address,
               flags=routing_flags),
        _slice(21, 0x358), _slice(27, 4),
    ]
    if scene_kind is not None:
        values.append(_slice(40, 1, data=bytes((scene_kind,))))
    if scene_kind == 8 and live_owner:
        values.extend([
            _slice(48, 0x148, address=0x80410000),
            _slice(44, 0x90, address=0x803F0DFC),
        ])
    elif scene_kind == 9 and live_owner:
        if kind_address is None:
            kind_address = 0x803F06D0 + stage_index * 0x1C + 0x0B
        values.append(_slice(41, 1, data=bytes((stage_index,)), address=index_address))
        if stage_kind is not None:
            values.append(_slice(42, 1, data=bytes((stage_kind,)), address=kind_address))
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
             boot_unobserved_scene: bool = False,
             boot_extra_slices: tuple[dict, ...] = (),
             boot_pad_pc: int = 0x8034DD8C,
             boot_routing_flags: int = 0,
             missing_css_scene: bool = False,
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
        boot = _pad_poll(None if boot_unobserved_scene else 1,
                         routing_flags=boot_routing_flags)
        boot += [_slice(45, 0x18, address=0x804A04F0),
                 _slice(46, 8, address=0x804D6BC8)]
        boot += list(boot_extra_slices)
        records.extend([
            _boundary(1, 2, pc=boot_pad_pc, source_tick=0, slices=boot),
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
        _boundary(1, 5, pc=0x8034DD8C, source_tick=40,
                  slices=([item for item in _pad_poll(8) if item["tag"] != 40]
                          if missing_css_scene else _pad_poll(8))),
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


class StadiumGoPrefixTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-go-prefix-")
        cls.validation_serial = 0

    def _validate(self, data: bytes, status: dict) -> dict:
        directory = self.scratch / f"validate-{self.validation_serial:04d}"
        type(self).validation_serial += 1
        directory.mkdir()
        stream = directory / "prefix.mwro"
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
        data, status = _fixture(boot_pad=True, boot_unobserved_scene=True)
        result = self._validate(data, status)
        self.assertEqual(result["decision"], "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY")
        data, status = _fixture(boot_match=True)
        with self.assertRaisesRegex(StadiumGoPrefixError, "pre-CSS source boundary"):
            self._validate(data, status)

    def test_boot_missing_scene_keeps_base_pad_pc_and_owner_contracts(self):
        payload = {"boundary": "pad_poll", "slices": _pad_poll(None)}
        _check_boundary_contract(payload, allow_missing_scene=True)
        for tag in (1, 2, 17, 21, 27):
            bad = {"boundary": "pad_poll",
                   "slices": [item for item in payload["slices"] if item["tag"] != tag]}
            with self.subTest(missing_tag=tag), self.assertRaisesRegex(
                    StadiumGoPrefixError, f"required source tag={tag}"):
                _check_boundary_contract(bad, allow_missing_scene=True)

        for extra in (_slice(4, 0x138),
                      _slice(41, 1, data=b"\x1e", address=0x804D6CAE)):
            data, status = _fixture(boot_pad=True, boot_unobserved_scene=True,
                                    boot_extra_slices=(extra,))
            with self.subTest(extra_tag=extra["tag"]), self.assertRaisesRegex(
                    StadiumGoPrefixError, "premature live CSS/SSS owner"):
                self._validate(data, status)

        data, status = _fixture(boot_pad=True, boot_unobserved_scene=True,
                                boot_pad_pc=0x8034DD90)
        with self.assertRaisesRegex(StadiumGoPrefixError, "pinned original HSD poll"):
            self._validate(data, status)

        data, status = _fixture(boot_pad=True, boot_unobserved_scene=True,
                                boot_routing_flags=1)
        with self.assertRaisesRegex(StadiumGoPrefixError,
                                    "boot PAD routing: expected one tag=17 flags=0"):
            self._validate(data, status)

    def test_scene_is_still_required_after_css_entry(self):
        data, status = _fixture(missing_css_scene=True)
        with self.assertRaisesRegex(StadiumGoPrefixError,
                                    "pad_poll: required source tag=40 is missing"):
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

    def test_authored_random_row_is_navigation_and_stadium_qualification_is_exact(self):
        random = _pad_poll(9, stage_index=30, stage_kind=None)
        stadium = _pad_poll(9, stage_index=18, stage_kind=3)
        other_stage = _pad_poll(9, stage_index=18, stage_kind=4)
        other_kind_three = _pad_poll(9, stage_index=0, stage_kind=3)
        self.assertEqual(_classify_stadium_sss_owner({"slices": random}),
                         "sss_navigation")
        self.assertEqual(_classify_stadium_sss_owner({"slices": stadium}), "sss")
        self.assertEqual(_classify_stadium_sss_owner({"slices": other_stage}),
                         "sss_navigation")
        self.assertEqual(_classify_stadium_sss_owner({"slices": other_kind_three}),
                         "sss_navigation")

        data, status = _fixture(sss_navigation=((30, None, None, None), (18, 3, None, None)))
        result = self._validate(data, status)
        self.assertEqual(result["sss_navigation_poll_rows"], 1)
        self.assertTrue(result["sss_selected_stadium_at_exit"])

    def test_random_and_stage_owner_descriptors_reject_wrong_counts_sizes_and_addresses(self):
        cases = [
            (_pad_poll(9, stage_index=30, stage_kind=3), "random row unexpectedly"),
            (_pad_poll(9, routing_address=0x80479D31), "routing escaped"),
            (_pad_poll(9, stage_index=18, stage_kind=3, index_address=0x804D6CAF),
             "index escaped"),
            (_pad_poll(9, stage_index=31, stage_kind=3), "index escaped"),
            (_pad_poll(9, stage_index=18, stage_kind=3, kind_address=0x803F06DC),
             "kind escaped"),
            (_pad_poll(9, stage_index=18, stage_kind=None), "expected one tag=42"),
            (_pad_poll(9, stage_index=18, stage_kind=3) +
             [_slice(41, 1, data=b"\x12", address=0x804D6CAE)], "duplicate or ambiguous"),
            (_pad_poll(9, stage_index=18, stage_kind=3) +
             [_slice(42, 1, data=b"\x03", address=0x803F08FB)], "duplicate or ambiguous"),
            (_pad_poll(9, stage_index=18, stage_kind=3)[:-1] +
             [_slice(42, 2, data=b"\x03\x00", address=0x803F08FB)],
             "unexpected byte length"),
        ]
        for slices, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(
                    StadiumGoPrefixError, message):
                _classify_stadium_sss_owner({"slices": slices})

        kind_without_index = _pad_poll(9, live_owner=False) + [
            _slice(42, 1, data=b"\x03", address=0x803F08FB)]
        with self.assertRaisesRegex(StadiumGoPrefixError, "without its owner index"):
            _classify_stadium_sss_owner({"slices": kind_without_index}, allow_missing=True)

    def test_actual_retained_raw_prefix_reaches_random_navigation_on_receiver(self):
        configured_path = os.environ.get(RETAINED_RAW_REPLAY_ENV)
        if configured_path is None:
            self.skipTest(
                f"optional retained raw replay not configured; set {RETAINED_RAW_REPLAY_ENV} "
                "to run this actual-capture control")
        stream = Path(configured_path).expanduser()
        self.assertTrue(stream.is_file(),
                        f"configured {RETAINED_RAW_REPLAY_ENV} is not a file: {stream}")
        self.assertEqual(stream.stat().st_size, RETAINED_RAW_REPLAY_BYTES,
                         f"configured {RETAINED_RAW_REPLAY_ENV} has an unexpected byte length")
        self.assertEqual(hashlib.sha256(stream.read_bytes()).hexdigest(),
                         RETAINED_RAW_REPLAY_SHA256,
                         f"configured {RETAINED_RAW_REPLAY_ENV} has an unexpected SHA-256")
        receiver_type, helper_module = _load_stadium_receiver_class()
        packet = stadium_go_prefix_packet()
        found_boot = found_random = False
        with patch.dict(sys.modules, {"sheik_transform_prefix": helper_module}):
            receiver = receiver_type(packet)
            for row in observer_stream.iter_records(stream, max_bytes=8 * 1024 * 1024,
                                                     max_records=2_000):
                if row["seq"] == 2:
                    payload = row["payload"]
                    tags = {item["tag"] for item in payload["slices"]}
                    self.assertNotIn(40, tags)
                    self.assertTrue({1, 2, 17, 21, 22, 23, 27} <= tags)
                    self.assertFalse({4, 41, 42, 43, 44, 47, 48, 50, 51} & tags)
                    _check_boundary_contract(payload, allow_missing_scene=True)
                    routing = _read_source_slice(payload, SCENE_ROUTING_TAG,
                                                 "boot PAD routing", 6)
                    self.assertEqual((routing["flags"], routing["address"],
                                      routing["raw"][0]),
                                     (0, 0x80479D30, 0x00))
                    found_boot = True
                receiver.accept(row)
                if row["seq"] == 1652:
                    self.assertEqual(row["event"], "boundary")
                    self.assertEqual(row["payload"]["boundary"], "pad_poll")
                    tags = row["payload"]["slices"]
                    self.assertEqual([(item["tag"], item["address"], item["hex"])
                                     for item in tags if item["tag"] in (40, 41, 42)],
                                     [(40, 0x803DD9C4, "09"),
                                      (41, 0x804D6CAE, "1e")])
                    found_random = True
                    break
        self.assertTrue(found_boot)
        self.assertTrue(found_random)
        self.assertEqual(receiver.stage,
                         {"index": 30, "kind": None, "stable_polls": 1})
        self.assertIsNotNone(receiver.sss_live_owner_sequence)
        self.assertIsNone(receiver.stadium_target_sequence)
        self.assertIsNone(receiver.confirm_sequence)

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



def _load_stadium_receiver_class():
    """Execute exact receiver/decoder methods without unrelated CLI imports."""
    import ast
    import types
    source_path = ROOT / "scripts/capture_sd_reference_prefix.py"
    tree = ast.parse(source_path.read_text(encoding="utf-8"), filename=str(source_path))
    selected = [node for node in tree.body
                if isinstance(node, ast.FunctionDef) and node.name in (
                    "_transform_slices", "_transform_consumed_ports")
                or isinstance(node, ast.ClassDef) and node.name in (
                    "SheikTransformPrefixReceiver", "StadiumGoPrefixReceiver")]
    names = {node.name for node in selected}
    if names != {"_transform_slices", "_transform_consumed_ports",
                 "SheikTransformPrefixReceiver", "StadiumGoPrefixReceiver"}:
        raise AssertionError("production Stadium receiver extraction is incomplete")
    module = ast.Module(body=[ast.ImportFrom(module="__future__", names=[
        ast.alias(name="annotations")], level=0), *selected], type_ignores=[])
    ast.fix_missing_locations(module)
    namespace = {"__name__": "stadium_items_lock_receiver_control",
                 "SdDiagnosticError": SdDiagnosticError,
                 "require": require, "NEUTRAL_PAD": NEUTRAL_PAD}
    exec(compile(module, str(source_path), "exec"), namespace)

    # The receiver's inherited PADPoll path imports this pure source classifier.
    # Extract its exact helper bodies rather than importing the unrelated
    # transform validator and its whole-session dependencies.
    helper_path = ROOT / "tools/sheik_transform_prefix.py"
    helper_tree = ast.parse(helper_path.read_text(encoding="utf-8"), filename=str(helper_path))
    helper_defs = {"TransformPrefixError", "_require", "_raw_slice", "_menu_owner_pad_poll"}
    helper_constants = {"SCENE_KIND_TAG", "SCENE_ROUTING_TAG", "MENU_CSS_LIVE_STATE_TAG",
                        "MENU_CSS_DOORS_TAG", "CSS_LIVE_STATE_SIZE", "CSS_DOORS_ADDRESS",
                        "MEM1_BASE", "MEM1_END"}
    helpers = []
    for node in helper_tree.body:
        if isinstance(node, ast.ClassDef) and node.name in helper_defs:
            helpers.append(node)
        elif isinstance(node, ast.FunctionDef) and node.name in helper_defs:
            helpers.append(node)
        elif isinstance(node, ast.Assign) and any(
                isinstance(target, ast.Name) and target.id in helper_constants
                for target in node.targets):
            helpers.append(node)
    found = {node.name for node in helpers if isinstance(node, (ast.ClassDef, ast.FunctionDef))}
    found_constants = {target.id for node in helpers if isinstance(node, ast.Assign)
                       for target in node.targets if isinstance(target, ast.Name)}
    if found != helper_defs or not helper_constants <= found_constants:
        raise AssertionError("production MenuOwnerPadPoll helper extraction is incomplete")
    helper_module = ast.Module(body=[ast.ImportFrom(module="__future__", names=[
        ast.alias(name="annotations")], level=0), *helpers], type_ignores=[])
    ast.fix_missing_locations(helper_module)
    helper_namespace = {"__name__": "stadium_items_lock_menu_helper_control"}
    exec(compile(helper_module, str(helper_path), "exec"), helper_namespace)
    helper_module_obj = types.ModuleType("sheik_transform_prefix")
    helper_module_obj._menu_owner_pad_poll = helper_namespace["_menu_owner_pad_poll"]
    return namespace["StadiumGoPrefixReceiver"], helper_module_obj


@contextmanager
def _stadium_receiver(packet):
    receiver_type, helper_module = _load_stadium_receiver_class()
    # The extracted class needs the real helper bodies only during this control;
    # restore any preexisting module so this test cannot poison other imports.
    with patch.dict(sys.modules, {"sheik_transform_prefix": helper_module}):
        yield receiver_type(packet)


class StadiumItemsLockGuardTests(unittest.TestCase):
    @staticmethod
    def _slice(tag: int, size: int, data: bytes, address: int) -> dict:
        return {"name": observer_stream.SLICE_NAMES[tag], "tag": tag,
                "flags": 0, "address": address, "size": size,
                "hex": data.hex()}

    def _poll(self, receiver, *, lock=None, kind=16, row=0, value=1,
              lock_address=0x804D6BEC, duplicate=False):
        flow = bytearray(0x18)
        flow[0] = kind
        flow[2:4] = row.to_bytes(2, "big")
        flow[4] = value
        flow[0x11] = 1
        fields = [
            self._slice(40, 1, b"\x01", 0x804A04C0),
            self._slice(45, 0x18, bytes(flow), 0x804A04F0),
            self._slice(46, 8, bytes(8), 0x804D6BC8),
        ]
        if lock is not None:
            lock_field = self._slice(56, 1, bytes((lock,)), lock_address)
            fields.append(lock_field)
            if duplicate:
                fields.append(dict(lock_field))
        receiver._pad_poll({"seq": 0, "payload": {"slices": fields}})

    def test_stadium_one_up_waits_for_observed_items_lock_zero(self):
        packet = stadium_go_prefix_packet()
        validate_packet(packet)
        self.assertEqual(packet["version"], 11)
        action = next(action for action in packet["actions"]
                      if action["label"] == "items-frequency-row")
        self.assertEqual(action["p1"], "0008000000000000000000")
        self.assertEqual(action["before"]["items_locked"], 0)
        self.assertEqual(action["after"]["items_locked"], 0)

        for lock, expected_ready in ((1, False), (0, True)):
            with _stadium_receiver(packet) as receiver:
                self._poll(receiver, lock=lock)
                self.assertEqual(receiver.latest_menu["items_locked"], lock)
                self.assertEqual(matches(receiver.latest_menu, action["before"]), expected_ready)

        self.assertEqual(action["after"]["row"], 31)
        self.assertEqual(action["after"]["value"], 3)
        for lock, expected_ready in ((1, False), (0, True)):
            with _stadium_receiver(packet) as receiver:
                self._poll(receiver, lock=lock, row=31, value=3)
                self.assertEqual(receiver.latest_menu["items_locked"], lock)
                self.assertEqual(matches(receiver.latest_menu, action["after"]), expected_ready)

    def test_stadium_items_lock_owner_is_required_and_exact(self):
        packet = stadium_go_prefix_packet()
        cases = [
            ({}, "Items lock owner is missing or unexpected"),
            ({"lock": 0, "lock_address": 0x804D6BED}, "Items lock address/size/value differs"),
            ({"lock": 2}, "Items lock address/size/value differs"),
            ({"lock": 0, "duplicate": True}, "duplicate source slice"),
        ]
        for kwargs, message in cases:
            with self.subTest(kwargs=kwargs), _stadium_receiver(packet) as receiver:
                with self.assertRaisesRegex(SdDiagnosticError, message):
                    self._poll(receiver, **kwargs)

        with _stadium_receiver(packet) as receiver:
            with self.assertRaisesRegex(SdDiagnosticError, "missing or unexpected"):
                self._poll(receiver, lock=0, kind=13)


class StadiumItemsLockSliceControl(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("clang++") or shutil.which("g++")
        if cls.compiler is None:
            raise unittest.SkipTest("A native C++ compiler is not installed")
        cls.scratch = cls.new_workspace(ROOT, "stadium-items-lock-slice-")

    def test_stadium_observer_emits_existing_checked_lock_slice_only_for_items(self):
        observer = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"
        source = observer.read_text()
        marker = "    if (scene_kind == 1 && (Env(\"MWRC_SD_MENU_PROBE\")"
        begin = source.index(marker)
        end = source.index("    // Source menu globals survive arena teardown", begin)
        gate = source[begin:end]
        harness = r'''#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
namespace Core { struct System {}; }
using u8 = uint8_t; using u16 = uint16_t; using u32 = uint32_t;
enum class SliceTag { SdItemsLock = 56 };
struct Probe {
  Core::System system;
  bool stadium_go_prefix_enabled = false;
  bool read_bytes_ok = true, read_words_ok = true, add_ok = true;
  u8 menu_kind = 16;
  int byte_reads = 0, word_reads = 0, adds = 0;
  SliceTag added_tag{}; u32 added_address = 0; size_t added_size = 0; u16 added_flags = 0;
  std::string Env(const char*) { return {}; }
  bool OrdinaryTimeoutRequested() { return false; }
  bool SparsePairRequested() { return false; }
  bool ReadBytes(Core::System*, u32 address, size_t size, u8* out) {
    ++byte_reads;
    if (!read_bytes_ok || address != 0x804a04f0 || size != 1) return false;
    *out = menu_kind; return true;
  }
  bool ReadU32(Core::System*, u32 address, u32* out) {
    static constexpr std::array<u32, 3> words{0x880db54c, 0x28000000, 0x40820180};
    ++word_reads;
    if (!read_words_ok || address < 0x80233ec0 || address > 0x80233ec8 ||
        ((address - 0x80233ec0) & 3) != 0) return false;
    *out = words[(address - 0x80233ec0) / 4]; return true;
  }
  bool AddSlice(Core::System*, SliceTag tag, u32 address, size_t size, u16 flags = 0) {
    ++adds; added_tag = tag; added_address = address; added_size = size; added_flags = flags;
    return add_ok;
  }
  bool Apply(u8 scene_kind) {
    Core::System* system_pointer = &system;
    Core::System* system = system_pointer;
''' + gate + r'''
    return true;
  }
};
int main() {
  Probe disabled; assert(disabled.Apply(1));
  assert(disabled.byte_reads == 0 && disabled.word_reads == 0 && disabled.adds == 0);
  Probe active; active.stadium_go_prefix_enabled = true; assert(active.Apply(1));
  assert(active.byte_reads == 1 && active.word_reads == 3 && active.adds == 1);
  assert(active.added_tag == SliceTag::SdItemsLock && active.added_address == 0x804d6bec &&
         active.added_size == 1 && active.added_flags == 0);
  Probe other_scene; other_scene.stadium_go_prefix_enabled = true; assert(other_scene.Apply(8));
  assert(other_scene.byte_reads == 0 && other_scene.adds == 0);
  Probe other_menu; other_menu.stadium_go_prefix_enabled = true; other_menu.menu_kind = 13;
  assert(other_menu.Apply(1)); assert(other_menu.byte_reads == 1 && other_menu.word_reads == 0 && other_menu.adds == 0);
  Probe bad_code; bad_code.stadium_go_prefix_enabled = true; bad_code.read_words_ok = false;
  assert(!bad_code.Apply(1) && bad_code.adds == 0);
  Probe add_failure; add_failure.stadium_go_prefix_enabled = true; add_failure.add_ok = false;
  assert(!add_failure.Apply(1) && add_failure.adds == 1);
}
'''
        source_path = self.scratch / "items_lock_gate.cpp"
        executable = self.scratch / "items_lock_gate"
        source_path.write_text(harness, encoding="utf-8")
        subprocess.run([self.compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source_path), "-o", str(executable)],
                       check=True, text=True, capture_output=True)
        subprocess.run([str(executable)], check=True, text=True, capture_output=True)


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

class StadiumFirstCssContextTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-first-css-context-")

    @staticmethod
    def _source_slice(tag, size, data, address):
        return {"tag": tag, "flags": 0, "address": address,
                "size": size, "hex": data.hex()}

    @classmethod
    def _rows(cls):
        rules = bytes.fromhex("0036010204000a00000001000000080800000800ffffffff")
        save = bytearray(0x55E8)
        save[:4] = bytes.fromhex("07ff07ff")
        css = bytearray(0x148)
        css[4:8] = FIRST_CSS_KO_ADDRESS.to_bytes(4, "big")
        pad = bytearray(0x358)
        pad_config = struct.pack(
            ">iibb fBBbbBBBBBBbBBBBB", 15, 4, 0, 0, 1.0, 0, 0, 80, 5,
            0, 150, 30, 0, 150, 30, 80, 1, 1, 0, 0, 0)
        pad[:10] = pad_config[:10]
        pad[12:32] = pad_config[10:]
        pad = bytes(pad)
        seed_pointer = FIRST_CSS_RNG_VALUE_ADDRESS.to_bytes(4, "big")
        route = bytes.fromhex("020201000000")

        def make(phase, sequence):
            common = [
                cls._source_slice(31, 0xF0, bytes(0xF0), 0x804807C0),
                cls._source_slice(33, 0x40, bytes(0x40), 0x803BB300),
                cls._source_slice(34, 4, bytes(4), 0x804D6038),
                cls._source_slice(36, 2, bytes(save[:2]), FIRST_CSS_SAVE_ADDRESS),
                cls._source_slice(37, 2, bytes(save[2:4]), FIRST_CSS_SAVE_ADDRESS + 2),
                cls._source_slice(21, 0x358, pad, FIRST_CSS_PAD_ADDRESS),
                cls._source_slice(17, 6, route, SCENE_ROUTING_ADDRESS),
                cls._source_slice(30, 4, bytes(4), 0x80479D58),
                cls._source_slice(40, 1, b"\x08", FIRST_CSS_SCENE_ADDRESS),
                cls._source_slice(19, 4, seed_pointer, FIRST_CSS_RNG_POINTER_ADDRESS),
                cls._source_slice(20, 4, bytes.fromhex("312151c3"),
                                  FIRST_CSS_RNG_VALUE_ADDRESS),
                cls._source_slice(50, 0x148, bytes(css), FIRST_CSS_DATA_ADDRESS),
                cls._source_slice(51, 6, bytes(6), FIRST_CSS_KO_ADDRESS),
            ]
            if phase == "css_entry":
                common.extend([
                    cls._source_slice(38, 0x18, rules, FIRST_CSS_RULES_ADDRESS),
                    cls._source_slice(39, 0x55E8, bytes(save), FIRST_CSS_SAVE_ADDRESS),
                ])
                common.sort(key=lambda item: item["tag"])
                pc, word, argument = "0x8026688c", "0x7c0802a6", "0x804807b0"
            else:
                pc, word, argument = "0x802669f0", "0x4e800020", "0x00000000"
            return {"event": "progress", "seq": sequence, "source_tick": 101,
                    "payload": {
                        "diagnostic": DIAGNOSTIC, "phase": phase,
                        "setup_receipt_sha256": EXPECTED_SETUP_RECEIPT_SHA256,
                        "setup_profile_verified_by_observer": False,
                        "pc": pc, "word": word, "argument": argument,
                        "source_tick": 101, "draw_ordinal": 0, "slices": common,
                    }}
        return make("css_entry", FIRST_CSS_ENTRY_SEQUENCE), \
               make("css_return", FIRST_CSS_RETURN_SEQUENCE)

    def test_first_css_input_bundle_is_fixed_layout_and_input_only(self):
        entry, returned = self._rows()
        result = _extract_first_css_context_rows(
            entry, returned, FIRST_CSS_STREAM_SHA256)
        bundle = result["input_bundle_bytes"]
        self.assertEqual(len(bundle), FIRST_CSS_CONTEXT_BYTES)
        self.assertEqual(result["input_bundle"]["magic_hex"],
                         FIRST_CSS_CONTEXT_MAGIC.hex())
        self.assertEqual(result["input_bundle"]["version"], FIRST_CSS_CONTEXT_VERSION)
        self.assertFalse(result["input_bundle"]["has_return_expectations"])
        decoded = decode_first_css_context_bundle(bundle)
        self.assertFalse(decoded["contains_expected_return"])
        self.assertEqual(decoded["source_stream_sha256"], FIRST_CSS_STREAM_SHA256)
        self.assertEqual(decoded["entry_sequence"], FIRST_CSS_ENTRY_SEQUENCE)
        self.assertEqual(decoded["return_sequence"], FIRST_CSS_RETURN_SEQUENCE)
        self.assertEqual(decoded["seed_hex"], "312151c3")
        self.assertEqual(len(decoded["pad_state_hex"]), 822 * 2)
        self.assertEqual(len(decoded["css_data_hex"]), 0x148 * 2)
        self.assertEqual(len(decoded["game_rules_hex"]), 0x18 * 2)
        self.assertEqual(len(decoded["save_data_hex"]), 0x55E8 * 2)
        self.assertEqual(result["expected_return"]["scene_kind"], 8)
        self.assertEqual(result["expected_return"]["random_seed_hex"], "312151c3")
        self.assertEqual(result["expected_return"]["css"]["ko_counts_owner"],
                         "source_vs_owned")
        for player in result["expected_return"]["css"]["vs"]["start"]["players"]:
            self.assertIn("xB", player)
        self.assertEqual(len(result["expected_return"]["pad_state_hex"]), 822 * 2)

    def test_first_css_semantic_players_include_defined_xB_byte(self):
        entry, returned = self._rows()
        css = next(item for item in returned["payload"]["slices"]
                   if item["tag"] == 50)
        raw = bytearray.fromhex(css["hex"])
        raw[16 + 0x60 + 0x0B] = 0xA6
        css["hex"] = raw.hex()
        result = _extract_first_css_context_rows(
            entry, returned, FIRST_CSS_STREAM_SHA256)
        player = result["expected_return"]["css"]["vs"]["start"]["players"][0]
        self.assertEqual(player["xB"], 0xA6)

    def test_first_css_cpp_source_pin_matches_exact_python_digest(self):
        source = (ROOT / "tests/native_menu_host_trace.cpp").read_text()
        match = re.search(
            r"expected_stream_sha\[32\]\s*=\s*\{(.*?)\};", source, re.S)
        self.assertIsNotNone(match)
        raw = bytes(int(value, 16) for value in
                    re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))
        self.assertEqual(raw.hex(), FIRST_CSS_STREAM_SHA256)

    def test_first_css_rows_reject_owner_phase_and_slice_mutations(self):
        cases = []
        entry, returned = self._rows()
        duplicate = json.loads(json.dumps(entry))
        extra_slice = dict(duplicate["payload"]["slices"][-1])
        extra_slice["tag"] = 99
        duplicate["payload"]["slices"].append(extra_slice)
        cases.append((duplicate, returned, "slice inventory"))

        entry, returned = self._rows()
        foreign_pointer = json.loads(json.dumps(entry))
        css = next(item for item in foreign_pointer["payload"]["slices"]
                   if item["tag"] == 50)
        changed = bytearray.fromhex(css["hex"])
        changed[4:8] = (FIRST_CSS_KO_ADDRESS + 4).to_bytes(4, "big")
        css["hex"] = changed.hex()
        cases.append((foreign_pointer, returned, "inconsistent source owners"))

        entry, returned = self._rows()
        wrong_scene = json.loads(json.dumps(returned))
        scene = next(item for item in wrong_scene["payload"]["slices"]
                     if item["tag"] == 40)
        scene["hex"] = "09"
        cases.append((entry, wrong_scene, "route, scene or RNG changed"))

        entry, returned = self._rows()
        wrong_profile = json.loads(json.dumps(entry))
        mask = next(item for item in wrong_profile["payload"]["slices"]
                    if item["tag"] == 36)
        mask["hex"] = "0000"
        cases.append((wrong_profile, returned, "inconsistent source owners"))

        entry, returned = self._rows()
        bad_pointer = json.loads(json.dumps(entry))
        css = next(item for item in bad_pointer["payload"]["slices"]
                   if item["tag"] == 50)
        changed = bytearray.fromhex(css["hex"])
        changed[0x10 + 0x38] = 1
        css["hex"] = changed.hex()
        cases.append((bad_pointer, returned, "pointer-safe initial ordinary VS"))

        entry, returned = self._rows()
        bad_pad = json.loads(json.dumps(entry))
        pad = next(item for item in bad_pad["payload"]["slices"]
                   if item["tag"] == 21)
        pad["size"] -= 1
        pad["hex"] = pad["hex"][:-2]
        cases.append((bad_pad, returned, "unexpected byte length"))

        for bad_entry, bad_return, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(StadiumGoPrefixError, message):
                    _extract_first_css_context_rows(
                        bad_entry, bad_return, FIRST_CSS_STREAM_SHA256)

        entry, returned = self._rows()
        with self.assertRaisesRegex(StadiumGoPrefixError,
                                    "not the retained v6 observer"):
            _extract_first_css_context_rows(entry, returned, "c" * 64)

        entry, returned = self._rows()
        bad_sequence = dict(returned, seq=FIRST_CSS_RETURN_SEQUENCE + 1)
        with self.assertRaisesRegex(StadiumGoPrefixError, "sequence identities"):
            _extract_first_css_context_rows(
                entry, bad_sequence, FIRST_CSS_STREAM_SHA256)

    def test_first_css_bundle_rejects_wrong_length_magic_and_sequences(self):
        entry, returned = self._rows()
        bundle = _extract_first_css_context_rows(
            entry, returned, FIRST_CSS_STREAM_SHA256)[
            "input_bundle_bytes"]
        for changed in (bundle[:-1], bundle + b"\0",
                        b"BADMAGIC" + bundle[8:],
                        bundle[:44] + b"\0\0\0\0" + bundle[48:],
                        bundle[:12] + bytes(32) + bundle[44:]):
            with self.subTest(size=len(changed), header=changed[:52].hex()):
                with self.assertRaises(StadiumGoPrefixError):
                    decode_first_css_context_bundle(changed)

    @classmethod
    def _consumed_tick_rows(cls):
        _, returned = cls._rows()
        returned_slices = returned["payload"]["slices"]
        pad = next(item for item in returned_slices if item["tag"] == 21)
        seed = next(item for item in returned_slices if item["tag"] == 20)
        route = bytes.fromhex("020201000000")
        queue = bytes.fromhex("05030300000000008046b108")
        port_statuses = [bytes(11), bytes(11), bytes(10) + b"\xff",
                         bytes(10) + b"\xff"]
        slot = bytearray(0x30)
        for index, status in enumerate(port_statuses):
            slot[index * 12:index * 12 + 11] = status
        consume = {
            "event": "boundary", "seq": FIRST_CSS_CONSUME_SEQUENCE,
            "payload": {
                "boundary": "pad_consume", "pc": 0x80377584,
                "source_tick": 0, "draw_ordinal": 0,
                "gprs": [0] * 32,
                "slices": [
                    dict(cls._source_slice(2, 0xC, queue, 0x804C1F78),
                         name="pad_queue"),
                    dict(cls._source_slice(3, 0x30, bytes(slot), 0x8046B168),
                         name="pad_slot"),
                ],
            },
        }
        consume["payload"]["gprs"][6] = 2
        consume["payload"]["gprs"][25] = 0x8046B168
        tick_slices = [
            cls._source_slice(2, 0xC, queue, 0x804C1F78),
            cls._source_slice(17, 6, route, SCENE_ROUTING_ADDRESS),
            cls._source_slice(20, 4, bytes.fromhex(seed["hex"]),
                              FIRST_CSS_RNG_VALUE_ADDRESS),
            cls._source_slice(19, 4, FIRST_CSS_RNG_VALUE_ADDRESS.to_bytes(4, "big"),
                              FIRST_CSS_RNG_POINTER_ADDRESS),
            cls._source_slice(21, 0x358, bytes.fromhex(pad["hex"]),
                              FIRST_CSS_PAD_ADDRESS),
            cls._source_slice(30, 4, bytes(4), FIRST_CSS_SCENE_FRAME_ADDRESS),
            cls._source_slice(40, 1, b"\x08", FIRST_CSS_SCENE_ADDRESS),
        ]
        tick = {
            "event": "boundary", "seq": FIRST_CSS_SOURCE_TICK_SEQUENCE,
            "payload": {
                "boundary": "source_tick", "pc": SOURCE_TICK_PC,
                "source_tick": 0, "draw_ordinal": 0, "slices": tick_slices,
            },
        }
        return [consume], [tick]

    def test_first_css_consumed_pad_bundle_is_fixed_and_input_only(self):
        consume_rows, tick_rows = self._consumed_tick_rows()
        result = _extract_first_css_consumed_tick_rows(
            consume_rows, tick_rows, FIRST_CSS_STREAM_SHA256)
        bundle = result["input_bundle_bytes"]
        self.assertEqual(len(bundle), FIRST_CSS_CONSUMED_PAD_BYTES)
        self.assertEqual(result["input_bundle"]["magic_hex"],
                         FIRST_CSS_CONSUMED_PAD_MAGIC.hex())
        self.assertEqual(result["input_bundle"]["version"],
                         FIRST_CSS_CONSUMED_PAD_VERSION)
        self.assertFalse(result["input_bundle"]["contains_expected_post_tick_state"])
        decoded = decode_first_css_consumed_pad_bundle(bundle)
        self.assertFalse(decoded["contains_expected_post_tick_state"])
        self.assertEqual(decoded["source_stream_sha256"], FIRST_CSS_STREAM_SHA256)
        self.assertEqual(decoded["consumed_pad_sequence"], FIRST_CSS_CONSUME_SEQUENCE)
        self.assertEqual(decoded["source_tick_sequence"], FIRST_CSS_SOURCE_TICK_SEQUENCE)
        self.assertEqual(decoded["port_status_hex"], result["input_bundle"]["port_status_bytes"])
        expected = result["expected_post_tick"]
        self.assertEqual((expected["original_source_frame"],
                          expected["native_post_host_tick_frame"]), (0, 1))
        self.assertEqual(expected["source_scene_kind"], 8)
        self.assertEqual(expected["source_routing_raw_hex"], "020201000000")
        self.assertEqual(expected["scene_routing_getters"], {
            "current_game_mode": 2,
            "previous_game_mode": 1,
            "current_scene_index": 0,
            "previous_scene_index": 0,
        })
        self.assertEqual(expected["routing_raw_fields_excluded"],
                         ["pending_mode", "next_state_id"])
        self.assertNotIn("pending_mode", expected["scene_routing_getters"])
        self.assertNotIn("next_state_id", expected["scene_routing_getters"])

    def test_first_css_consumed_pad_bundle_rejects_bad_header_and_sequences(self):
        consume_rows, tick_rows = self._consumed_tick_rows()
        bundle = _extract_first_css_consumed_tick_rows(
            consume_rows, tick_rows, FIRST_CSS_STREAM_SHA256)["input_bundle_bytes"]
        for changed in (bundle[:-1], bundle + b"\0",
                        b"BADMAGIC" + bundle[8:],
                        bundle[:12] + bytes(32) + bundle[44:],
                        bundle[:44] + bytes(4) + bundle[48:]):
            with self.subTest(size=len(changed), header=changed[:52].hex()):
                with self.assertRaises(StadiumGoPrefixError):
                    decode_first_css_consumed_pad_bundle(changed)

    def test_first_css_consumed_tick_rejects_wrong_source_row_boundaries(self):
        cases = []
        consumes, ticks = self._consumed_tick_rows()
        cases.append(([], ticks, "one exact consume/tick row"))
        consumes, ticks = self._consumed_tick_rows()
        cases.append((consumes + consumes, ticks, "one exact consume/tick row"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(consumes))
        changed[0]["payload"]["pc"] += 4
        cases.append((changed, ticks, "consume boundary differs"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(consumes))
        changed[0]["payload"]["slices"][1]["address"] += 4
        cases.append((changed, ticks, "source slot is invalid"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(ticks))
        changed[0]["payload"]["source_tick"] = 1
        cases.append((consumes, changed, "pre-increment phase"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(ticks))
        route = next(item for item in changed[0]["payload"]["slices"]
                     if item["tag"] == 17)
        route["hex"] = "020201000001"
        cases.append((consumes, changed, "routing differs"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(ticks))
        frame = next(item for item in changed[0]["payload"]["slices"]
                     if item["tag"] == 30)
        frame["hex"] = "00000001"
        cases.append((consumes, changed, "frame zero"))

        consumes, ticks = self._consumed_tick_rows()
        changed = json.loads(json.dumps(ticks))
        changed[0]["payload"]["slices"].append(
            dict(next(item for item in changed[0]["payload"]["slices"]
                      if item["tag"] == 40)))
        cases.append((consumes, changed, "expected one tag=40"))

        for bad_consume, bad_tick, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(StadiumGoPrefixError, message):
                    _extract_first_css_consumed_tick_rows(
                        bad_consume, bad_tick, FIRST_CSS_STREAM_SHA256)

        consumes, ticks = self._consumed_tick_rows()
        with self.assertRaisesRegex(StadiumGoPrefixError, "retained v6 observer"):
            _extract_first_css_consumed_tick_rows(consumes, ticks, "c" * 64)

        consumes, ticks = self._consumed_tick_rows()
        ticks[0]["seq"] += 1
        with self.assertRaisesRegex(StadiumGoPrefixError, "SourceTick sequence/event"):
            _extract_first_css_consumed_tick_rows(
                consumes, ticks, FIRST_CSS_STREAM_SHA256)

    def test_retained_first_css_raw_is_optional_but_strict_when_configured(self):
        source_value = os.environ.get(FIRST_CSS_CONTEXT_ENV)
        if source_value is None:
            self.skipTest(
                "optional retained v6 first-CSS raw is not configured; "
                f"set {FIRST_CSS_CONTEXT_ENV} to enable exact source replay")
        source = Path(source_value)
        result = extract_stadium_first_css_context(
            source, source.with_name("observer-status.json"))
        self.assertEqual(result["provenance"]["stream_bytes"], FIRST_CSS_STREAM_BYTES)
        self.assertEqual(result["provenance"]["stream_sha256"], FIRST_CSS_STREAM_SHA256)
        self.assertEqual(result["provenance"]["css_entry_sequence"],
                         FIRST_CSS_ENTRY_SEQUENCE)
        self.assertEqual(result["provenance"]["css_return_sequence"],
                         FIRST_CSS_RETURN_SEQUENCE)
        bundle_path = self.scratch / "first-css-input.mwsc"
        expected_path = self.scratch / "first-css-return-expected.json"
        bundle_path.write_bytes(result["input_bundle_bytes"])
        expected_path.write_text(json.dumps({
            key: value for key, value in result.items() if key != "input_bundle_bytes"
        }, indent=2) + "\n")
        self.assertEqual(bundle_path.stat().st_size, FIRST_CSS_CONTEXT_BYTES)
        self.assertEqual(hashlib.sha256(bundle_path.read_bytes()).hexdigest(),
                         result["input_bundle"]["sha256"])
        self.assertEqual(json.loads(expected_path.read_text())["expected_return"],
                         result["expected_return"])
        tick_result = extract_stadium_first_css_consumed_tick(
            source, source.with_name("observer-status.json"))
        tick_bundle = tick_result["input_bundle_bytes"]
        decoded_tick_bundle = decode_first_css_consumed_pad_bundle(tick_bundle)
        self.assertEqual(len(tick_bundle), FIRST_CSS_CONSUMED_PAD_BYTES)
        self.assertEqual(decoded_tick_bundle["consumed_pad_sequence"], 834)
        self.assertEqual(decoded_tick_bundle["source_tick_sequence"], 835)
        self.assertEqual(tick_result["expected_post_tick"]["source_tick_value"], 0)
        self.assertEqual(tick_result["expected_post_tick"]["source_draw_ordinal"], 0)
        tick_input_path = self.scratch / "first-css-consumed-pad.mwst"
        tick_expected_path = self.scratch / "first-css-consumed-tick-expected.json"
        tick_input_path.write_bytes(tick_bundle)
        tick_expected_path.write_text(json.dumps({
            key: value for key, value in tick_result.items()
            if key != "input_bundle_bytes"
        }, indent=2, sort_keys=True) + "\n")
        self.assertEqual(hashlib.sha256(tick_input_path.read_bytes()).hexdigest(),
                         tick_result["input_bundle"]["sha256"])
        self.assertFalse((self.scratch / source.name).exists(),
                         "retained raw source must stay outside test scratch")

if __name__ == "__main__":
    unittest.main()
