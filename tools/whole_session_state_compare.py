#!/usr/bin/env python3
"""Strict state/input comparison for one MWRO + MWRC whole-session run.

The raw observer is the source of expected order.  Its ``pad_consume`` rows
bind one-for-one to MWRC frames; no scene or input search is performed.  Menu
rows validate browser field shape and exact scene/input order; their scalar
state is explicitly outside this comparator's source-to-browser join. VS
``setup`` and ``source_tick`` rows additionally carry the exact four-fighter
state.

This module is intentionally separate from the older indexed-prefix
comparator.  It checks the complete source-consumed timeline and reports the
known boundary limitation: non-match source snapshots have no match clock or
FighterHead state, and the browser report supplies the final CSS endpoint
without a scalar CSS state join.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any, Iterable, Mapping


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "reference-capture" / "dolphin"), str(ROOT / "tools")]

from reference_observer_stream import iter_records  # noqa: E402
import reference_capture_semantics as semantics  # noqa: E402
from reference_capture_semantics import SliceMemory, pad_snapshot_bytes, state_snapshot  # noqa: E402
from whole_session_replay import (  # noqa: E402
    CONTEXT_BYTES,
    CONTEXT_HEADER,
    CSS_DATA_SIZE,
    GAME_INFO_SIZE,
    KO_COUNTS_SIZE,
    MAX_SPANS,
    PAD_STATE_BYTES,
    SCENES,
    SPAN,
    _consumed_ports,
)


SCHEMA = "melee-web-whole-session-state-comparison-v1"
BROWSER_SCHEMA = "melee-web-port-session-diagnostic"
BROWSER_VERSION = 1
MWRC_HEADER = struct.Struct(">4sIIIHH")
MWRC_CONTEXT_VERSION = 2
MWRC_VERSION = 8
FRAME_BYTES = 44
PORT_BYTES = 11
PORT_COUNT = 4
MAX_UINT32 = 0xFFFFFFFF
SCENE_NAMES = {value: key for key, value in SCENES.items()}
COMPARE_FIELDS = ("rng", "match_frame", "pad_state_hex", "fighters")
NONMATCH_FIELDS: tuple[str, ...] = ()
FIGHTER_KEYS = {
    "slot", "kind", "motion", "animation", "ground_air", "facing_bits",
    "position_bits", "velocity_bits", "knockback_bits", "animation_frame_bits",
    "animation_speed_bits", "damage_bits", "shield_bits", "stocks", "input_hex",
}
HEX_RE = re.compile(r"^[0-9a-f]+$")


class ComparisonError(ValueError):
    """Input or observer evidence cannot be admitted to this comparison."""


def _is_match_field(field: Any) -> bool:
    if not isinstance(field, str):
        return False
    root = field.split(".", 1)[0].split("[", 1)[0]
    return root in COMPARE_FIELDS


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ComparisonError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _int(value: Any, context: str, low: int = 0, high: int = MAX_UINT32) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ComparisonError(f"{context}: expected integer in [{low}, {high}]")
    return value


def _hex(value: Any, size: int, context: str) -> str:
    if not isinstance(value, str) or len(value) != size * 2 or not HEX_RE.fullmatch(value):
        raise ComparisonError(f"{context}: expected lowercase hexadecimal {size:#x}-byte value")
    return value


def _first_difference(expected: Any, actual: Any, path: str = "") -> tuple[str, Any, Any] | None:
    if type(expected) is not type(actual):
        return path or "$", expected, actual
    if isinstance(expected, dict):
        for key in expected:
            child = f"{path}.{key}" if path else key
            if key not in actual:
                return child, expected[key], None
            difference = _first_difference(expected[key], actual[key], child)
            if difference:
                return difference
        for key in actual:
            child = f"{path}.{key}" if path else key
            if key not in expected:
                return child, None, actual[key]
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}.length", len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = _first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if expected == actual else (path or "$", expected, actual)


def _slice(payload: Mapping[str, Any], name: str, size: int, context: str) -> tuple[dict[str, Any], bytes]:
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    matches = [item for item in slices if isinstance(item, dict) and item.get("name") == name]
    if len(matches) != 1:
        raise ComparisonError(f"{context}: expected exactly one {name} slice")
    item = matches[0]
    if item.get("size") != size:
        raise ComparisonError(f"{context}: {name} slice has wrong size")
    raw = bytes.fromhex(item.get("hex", ""))
    if len(raw) != size:
        raise ComparisonError(f"{context}: {name} slice bytes disagree with size")
    return item, raw


def _snapshot_values(payload: Mapping[str, Any], context: str) -> dict[str, Any]:
    rng_items = [x for x in payload.get("slices", []) if x.get("name") == "rng_value"]
    pad_items = [x for x in payload.get("slices", []) if x.get("name") == "pad_snapshot"]
    if len(rng_items) != 1 or len(pad_items) != 1:
        raise ComparisonError(f"{context}: expected one typed RNG and PAD snapshot")
    rng_item = rng_items[0]
    pad_item = pad_items[0]
    rng_raw = bytes.fromhex(rng_item.get("hex", ""))
    pad_raw = bytes.fromhex(pad_item.get("hex", ""))
    if len(rng_raw) != 4:
        raise ComparisonError(f"{context}: RNG slice must be four bytes")
    if len(pad_raw) != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot slice must be 0x358 bytes")
    # The first-CSS context intentionally includes a large SaveData slice that
    # exceeds SliceMemory's small typed-span limit.  The observer stream has
    # already validated descriptor bounds; validate only the exact fields used
    # by this comparator and convert the canonical source PAD block.
    return {"rng": int.from_bytes(rng_raw, "big"),
            "pad_state_hex": pad_snapshot_bytes(pad_raw)}


def _pad_value(payload: Mapping[str, Any], context: str) -> str:
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    matches = [item for item in slices if isinstance(item, dict) and item.get("name") == "pad_snapshot"]
    if len(matches) != 1:
        raise ComparisonError(f"{context}: expected one typed PAD snapshot")
    item = matches[0]
    if item.get("size") != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot slice must be 0x358 bytes")
    raw = bytes.fromhex(item.get("hex", ""))
    if len(raw) != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot bytes disagree with size")
    return pad_snapshot_bytes(raw)


def _state_from_payload(payload: Mapping[str, Any], context: str) -> dict[str, Any]:
    slices = payload.get("slices", [])
    heads: dict[int, int] = {}
    for item in slices:
        if item.get("name") == "fighter_head":
            slot = item.get("flags")
            if type(slot) is not int or slot in heads or not 0 <= slot <= 3:
                raise ComparisonError(f"{context}: invalid fighter head slot")
            heads[slot] = item.get("address")
    if set(heads) != set(range(4)):
        raise ComparisonError(f"{context}: expected all four fighter heads")
    state = state_snapshot(SliceMemory(slices), heads)
    if len(state["fighters"]) != 4:
        raise ComparisonError(f"{context}: expected all four fighter states")
    return state


class Recipe:
    """Decoded, checked MWRC v8 transport."""

    def __init__(self, path: Path, raw: bytes) -> None:
        self.path = path
        self.raw = raw
        if len(raw) < MWRC_HEADER.size + CONTEXT_HEADER.size:
            raise ComparisonError("MWRC recipe is truncated")
        magic, version, self.seed, count, self.characters, self.stages = MWRC_HEADER.unpack_from(raw)
        if magic != b"MWRC" or version != MWRC_VERSION:
            raise ComparisonError("whole-session comparison requires MWRC v8")
        self.frame_count = count
        if not 1 <= count <= 108000:
            raise ComparisonError("MWRC frame count is outside v8 bounds")
        cursor = MWRC_HEADER.size
        context_version, flags, context_size = CONTEXT_HEADER.unpack_from(raw, cursor)
        cursor += CONTEXT_HEADER.size
        if context_version != MWRC_CONTEXT_VERSION or flags != 0 or context_size != CONTEXT_BYTES:
            raise ComparisonError("MWRC first-CSS context header is unsupported")
        if cursor + CONTEXT_BYTES + GAME_INFO_SIZE + PAD_STATE_BYTES > len(raw):
            raise ComparisonError("MWRC context is truncated")
        context = raw[cursor:cursor + CONTEXT_BYTES]
        cursor += CONTEXT_BYTES
        self.game_rules = context[:0x18]
        self.save_data = context[0x18:0x18 + 0x55E8]
        self.css_data = context[0x18 + 0x55E8:0x18 + 0x55E8 + CSS_DATA_SIZE]
        self.ko_counts = context[-KO_COUNTS_SIZE:]
        self.setup = raw[cursor:cursor + GAME_INFO_SIZE]
        cursor += GAME_INFO_SIZE
        self.initial_pad = raw[cursor:cursor + PAD_STATE_BYTES]
        cursor += PAD_STATE_BYTES
        self.frames: list[dict[str, Any]] = []
        for index in range(count):
            end = cursor + FRAME_BYTES
            if end > len(raw):
                raise ComparisonError(f"MWRC frame {index} is truncated")
            frame_raw = raw[cursor:end]
            pads = [frame_raw[offset:offset + PORT_BYTES].hex()
                    for offset in range(0, FRAME_BYTES, PORT_BYTES)]
            self.frames.append({"index": index, "pads": pads})
            cursor = end
        if cursor + 2 > len(raw):
            raise ComparisonError("MWRC span table is truncated")
        span_count = int.from_bytes(raw[cursor:cursor + 2], "big")
        cursor += 2
        if not 1 <= span_count <= MAX_SPANS:
            raise ComparisonError("MWRC span count is outside bounds")
        self.spans: list[dict[str, int]] = []
        next_frame = 0
        for index in range(span_count):
            if cursor + SPAN.size > len(raw):
                raise ComparisonError("MWRC span table is truncated")
            scene, span_flags, reserved, first, last = SPAN.unpack_from(raw, cursor)
            cursor += SPAN.size
            if span_flags != 0 or reserved != 0 or scene not in SCENE_NAMES:
                raise ComparisonError(f"MWRC span {index} has unsupported flags or scene")
            if not (0 <= first <= last < count) or first != next_frame:
                raise ComparisonError(f"MWRC span {index} is not ordered and contiguous")
            self.spans.append({"scene": scene, "first_frame": first, "last_frame": last})
            next_frame = last + 1
        if next_frame != count or self.spans[0]["scene"] != SCENES["css"]:
            raise ComparisonError("MWRC spans do not cover the admitted whole-session timeline")
        if self.spans[-1]["scene"] not in (SCENES["results"], SCENES["prize"]):
            raise ComparisonError("MWRC timeline does not end in Results or Prize")
        if cursor != len(raw):
            raise ComparisonError("MWRC recipe has trailing bytes")
        for span in self.spans:
            for index in range(span["first_frame"], span["last_frame"] + 1):
                self.frames[index]["scene"] = span["scene"]


class BrowserReader:
    """Strict streaming reader for the browser diagnostic JSONL."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.stream = path.open("r", encoding="utf-8")
        self.line = 0
        try:
            self.header = self._read()
        except StopIteration as error:
            self.stream.close()
            raise ComparisonError("browser trace is empty") from error
        except Exception:
            self.stream.close()
            raise
        try:
            self._require(self.header, {"record", "schema", "version", "frames_requested",
                                        "comparison", "cpu_observations", "draw_state"}, "browser header")
            if self.header["record"] != "header" or self.header["schema"] != BROWSER_SCHEMA \
                    or self.header["version"] != BROWSER_VERSION:
                raise ComparisonError("unsupported browser diagnostic header")
            if self.header["comparison"] != "not_run":
                raise ComparisonError("browser trace header comparison must be not_run")
            if self.header["cpu_observations"] != "not_captured" or self.header["draw_state"] != "not_captured":
                raise ComparisonError("browser trace carries an unsupported observation mode")
        except Exception:
            self.stream.close()
            raise

    @staticmethod
    def _require(row: Mapping[str, Any], keys: set[str], context: str) -> None:
        if set(row) != keys:
            missing = sorted(keys - set(row))
            unknown = sorted(set(row) - keys)
            raise ComparisonError(f"{context}: fields differ (missing={missing}, unknown={unknown})")

    def _read(self) -> dict[str, Any]:
        line = self.stream.readline()
        if not line:
            raise StopIteration
        self.line += 1
        if not line.strip():
            raise ComparisonError(f"browser trace line {self.line}: blank line")
        try:
            row = json.loads(line, object_pairs_hook=_reject_duplicate_keys)
        except (json.JSONDecodeError, ComparisonError) as error:
            raise ComparisonError(f"browser trace line {self.line}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise ComparisonError(f"browser trace line {self.line}: record is not an object")
        return row

    def next(self) -> dict[str, Any]:
        try:
            row = self._read()
        except StopIteration as error:
            raise ComparisonError("browser trace ended before the expected record") from error
        return row

    def close(self) -> None:
        self.stream.close()


class Comparator:
    def __init__(self, recipe: Recipe, browser: BrowserReader) -> None:
        self.recipe = recipe
        self.browser = browser
        if browser.header["frames_requested"] != recipe.frame_count:
            raise ComparisonError("browser header frames_requested disagrees with MWRC recipe")
        self.frame_index = 0
        self.current_match = -1
        self.setup_count = 0
        self.compared = 0
        self.nonmatch_compared = 0
        self.match_compared = 0
        self.first_difference: dict[str, Any] | None = None
        self.divergence: str | None = None
        self.source_spans: list[dict[str, int]] = []
        self.setup_states: list[dict[str, Any]] = []
        self.final_css_source: dict[str, Any] | None = None
        self.boundary_checkpoints: list[dict[str, Any]] = []

    def fail(self, message: str, *, record: str | None = None,
             index: int | None = None, field: str | None = None,
             expected: Any = None, actual: Any = None) -> None:
        if self.divergence is None:
            self.divergence = message
            self.first_difference = {
                "record": record, "index": index, "field": field,
                "expected": expected, "actual": actual, "message": message,
            }
        raise ComparisonError(message)

    def _compare(self, expected: Mapping[str, Any], actual: Mapping[str, Any],
                 fields: Iterable[str], *, record: str, index: int,
                 context: str) -> None:
        left = {field: expected[field] for field in fields}
        right = {field: actual[field] for field in fields}
        difference = _first_difference(left, right)
        if difference:
            field, wanted, got = difference
            self.fail(f"{context}: exact state differs at {field}", record=record,
                      index=index, field=field, expected=wanted, actual=got)

    def _expect_frame_row(self, expected_scene: int, index: int) -> dict[str, Any]:
        try:
            row = self.browser.next()
        except ComparisonError as error:
            self.fail("browser trace is missing the expected session frame", record="session_frame",
                      index=index, expected={"scene": expected_scene, "index": index},
                      actual=str(error))
        expected_keys = {"record", "scene", "index", "supplied_inputs", "rng", "pad_state_hex"}
        if expected_scene == SCENES["match"]:
            expected_keys |= {"match_frame", "fighters"}
        BrowserReader._require(row, expected_keys,
                               f"browser frame index {index} line {self.browser.line}")
        if row.get("record") != "session_frame":
            self.fail("browser record order contains a non-session frame", record=row.get("record"),
                      index=index, expected="session_frame", actual=row.get("record"))
        if row["scene"] != expected_scene or row["index"] != index:
            self.fail("browser scene/index is missing, extra, or reordered", record="session_frame",
                      index=index, field="scene/index",
                      expected={"scene": expected_scene, "index": index},
                      actual={"scene": row.get("scene"), "index": row.get("index")})
        inputs = row["supplied_inputs"]
        if not isinstance(inputs, list) or len(inputs) != PORT_COUNT:
            self.fail("browser supplied_inputs has the wrong port count", record="session_frame",
                      index=index, field="supplied_inputs", expected=PORT_COUNT, actual=inputs)
        for port, value in enumerate(inputs):
            _hex(value, PORT_BYTES, f"browser frame {index} supplied_inputs[{port}]")
        if inputs != self.recipe.frames[index]["pads"]:
            self.fail("browser supplied_inputs disagree with the MWRC input cursor",
                      record="session_frame", index=index, field="supplied_inputs",
                      expected=self.recipe.frames[index]["pads"], actual=inputs)
        _int(row["rng"], f"browser frame {index}.rng")
        _hex(row["pad_state_hex"], PAD_STATE_BYTES, f"browser frame {index}.pad_state_hex")
        if expected_scene == SCENES["match"]:
            _int(row["match_frame"], f"browser frame {index}.match_frame")
            fighters = row["fighters"]
            if not isinstance(fighters, list) or len(fighters) != 4:
                raise ComparisonError(f"browser frame {index}: expected four fighters")
            for slot, fighter in enumerate(fighters):
                if not isinstance(fighter, dict) or set(fighter) != FIGHTER_KEYS or fighter.get("slot") != slot:
                    raise ComparisonError(f"browser frame {index}: malformed fighter {slot}")
        return row

    def on_setup(self, match_index: int, state: dict[str, Any], source_seq: int) -> None:
        if match_index != self.setup_count:
            raise ComparisonError(f"source setup match order is not contiguous ({match_index})")
        try:
            row = self.browser.next()
        except ComparisonError as error:
            self.fail("browser trace is missing the expected match setup record",
                      record="session_match_enter_complete", index=match_index,
                      expected="session_match_enter_complete", actual=str(error))
        expected_keys = {"record", "rng", "match_frame", "pad_state_hex", "fighters"}
        BrowserReader._require(row, expected_keys,
                               f"browser match_enter_complete before match {match_index}")
        if row.get("record") != "session_match_enter_complete":
            self.fail("browser match setup record is missing or reordered", record=row.get("record"),
                      index=self.frame_index, expected="session_match_enter_complete",
                      actual=row.get("record"))
        self._compare(state, row, COMPARE_FIELDS, record="match_enter_complete",
                      index=match_index, context=f"match {match_index} setup")
        self.setup_states.append(state)
        self.current_match = match_index
        self.setup_count += 1
        self.boundary_checkpoints.append({"match_index": match_index,
                                          "setup_source_seq": source_seq,
                                          "match_entry_frame": self.frame_index})

    def on_frame(self, frame: dict[str, Any]) -> None:
        index = self.frame_index
        if index >= self.recipe.frame_count:
            self.fail("source has an extra PAD-consumed frame", record="source_frame", index=index)
        recipe_frame = self.recipe.frames[index]
        expected_scene = frame["scene"]
        if recipe_frame["pads"] != frame["pads"] or recipe_frame["scene"] != expected_scene:
            self.fail("MWRC recipe disagrees with source PAD/scene order", record="source_frame",
                      index=index, field="scene/pads",
                      expected={"scene": recipe_frame["scene"], "pads": recipe_frame["pads"]},
                      actual={"scene": expected_scene, "pads": frame["pads"]})
        if self.source_spans and self.source_spans[-1]["scene"] == expected_scene:
            self.source_spans[-1]["last_frame"] = index
        else:
            self.source_spans.append({"scene": expected_scene, "first_frame": index,
                                      "last_frame": index})
        row = self._expect_frame_row(expected_scene, index)
        if index == 0 and expected_scene == SCENES["css"]:
            self._compare({"rng": self.recipe.seed}, row,
                          ("rng",), record="initial_css",
                          index=index, context="initial CSS")
        if expected_scene == SCENES["match"]:
            if "state" not in frame:
                raise ComparisonError(f"source match frame {index} lacks state snapshot")
            self._compare(frame["state"], row, COMPARE_FIELDS, record="session_frame",
                          index=index, context=f"match frame {index}")
            self.match_compared += 1
        else:
            # The original observer has no match clock or FighterHead slices
            # in menu/result scenes. Keep this explicit in the report.
            self.nonmatch_compared += 1
        self.compared += 1
        self.frame_index += 1

    def on_final_css(self, state: dict[str, Any], source_seq: int) -> None:
        self.final_css_source = state
        self.boundary_checkpoints.append({"final_css_source_seq": source_seq,
                                          "final_css_frame": None})

    def finalize(self) -> dict[str, Any]:
        if self.frame_index != self.recipe.frame_count:
            raise ComparisonError("source frame count does not cover the MWRC recipe")
        if self.source_spans != self.recipe.spans:
            raise ComparisonError("source scene spans disagree with the MWRC recipe")
        if self.setup_count == 0:
            raise ComparisonError("source session has no match setup")
        row = self.browser.next()
        if row.get("record") != "end":
            raise ComparisonError("browser trace has an extra or missing terminal record")
        BrowserReader._require(row, {"record", "frames", "status"}, "browser end")
        if row["frames"] != self.recipe.frame_count or row["status"] != "captured":
            raise ComparisonError("browser terminal record disagrees with the recipe frame count")
        try:
            extra = self.browser.next()
        except ComparisonError as error:
            if "ended before the expected record" in str(error):
                extra = None
            else:
                raise
        if extra is not None:
            raise ComparisonError("browser trace contains records after end")
        return {}


class SourceCollector:
    """Stream MWRO and invoke a comparator without retaining its huge slices."""

    def __init__(self, callback: Comparator, recipe: Recipe) -> None:
        self.callback = callback
        self.recipe = recipe
        self.handshake: dict[str, Any] | None = None
        self.start: dict[str, Any] | None = None
        self.end: dict[str, Any] | None = None
        self.scene: str | None = None
        self.match_index = 0
        self.match_count = sum(1 for span in recipe.spans if span["scene"] == SCENES["match"])
        self.started = False
        self.finished = False
        self.pending: dict[str, Any] | None = None
        self.lifecycle: dict[int, list[tuple[str, int, int, int]]] = {}
        self.lifecycle_rows: list[dict[str, Any]] = []
        self.first_css: dict[str, Any] | None = None
        self.setup_bytes: list[bytes] = []
        self.final_css: dict[str, Any] | None = None
        self.last_source_tick: dict[int, int] = {}
        self.record_count = 0

    def _lifecycle(self, boundary: str, row: Mapping[str, Any]) -> None:
        payload = row["payload"]
        match = _int(payload.get("match_index"), f"{boundary}.match_index", 0, 63)
        if match != self.match_index:
            raise ComparisonError(f"{boundary} match index {match} disagrees with active {self.match_index}")
        self.lifecycle.setdefault(match, []).append((boundary, row["seq"], row["source_tick"], row["draw_ordinal"]))
        self.lifecycle_rows.append(dict(row))

    def _transition(self, boundary: str, row: Mapping[str, Any]) -> None:
        self._lifecycle(boundary, row)
        if boundary in {"prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                        "startup_prize_mode_exit"} and not self.started:
            return
        if boundary == "css_enter":
            if self.started or self.scene is not None or self.match_index != 0:
                raise ComparisonError("source CSS entry is duplicated or reordered")
            self.started = True
            self.scene = "css"
            return
        if not self.started:
            raise ComparisonError(f"source boundary {boundary} preceded first CSS entry")
        if boundary in {"css_cancel_enter", "return_css"}:
            if boundary == "return_css":
                if self.scene not in {"results", "prize"}:
                    raise ComparisonError("source return_css arrived outside Results/Prize")
                if self.pending is not None:
                    raise ComparisonError("source return_css has an uncommitted VS state")
                if self.match_index + 1 > self.match_count:
                    raise ComparisonError("source return_css match index is invalid")
                if self.match_index + 1 < self.match_count:
                    self.match_index += 1
                    self.scene = "css"
                else:
                    self.finished = True
                    self.scene = "css"
            else:
                self.scene = "css"
            return
        expected = {
            "css_exit": "css", "sss_enter": "css", "sss_exit": "sss",
            "entry": "sss", "setup": "match", "draw_return": "match",
            "vs_exit": "match", "vs_exit_return": "match", "vs_mode_exit": "match",
            "results_enter": "match", "results_gobj": "results",
            "results_exit": "results", "results_mode_exit": "results",
            "scene_teardown": "results", "prize_mode_enter": "results",
            "prize_scene_enter": "prize", "prize_scene_exit": "prize",
            "prize_mode_exit": "prize",
        }.get(boundary)
        if expected is None:
            raise ComparisonError(f"unsupported source lifecycle boundary {boundary}")
        if self.scene != expected:
            raise ComparisonError(f"source {boundary} expected scene {expected}, got {self.scene}")
        if boundary == "sss_enter":
            self.scene = "sss"
        elif boundary == "entry":
            self.scene = "match"
        elif boundary == "results_enter":
            self.scene = "results"
        elif boundary == "prize_mode_enter":
            self.scene = "prize"

    def _poll(self, row: Mapping[str, Any]) -> None:
        # pad_poll is retained as source evidence, but it has no browser join:
        # it carries PAD without an RNG slice and can be interrupted by VI
        # polls.  Do not assign it to a session_frame by adjacency.
        if self.scene in {"css", "sss", "match", "results"} and not self.finished:
            _pad_value(row["payload"], "source pad_poll")

    def _consume(self, row: Mapping[str, Any]) -> None:
        # Startup PAD traffic precedes the first CSS lifecycle owner and is
        # outside the admitted MWRC timeline.  Once the session has started,
        # every consume must belong to a scene (including the final CSS
        # transition, which deliberately has no consumed frame).
        if not self.started and self.scene is None:
            return
        if self.scene not in {"css", "sss", "match", "results"} or self.finished:
            raise ComparisonError("source PAD consume occurred outside the admitted session")
        if self.scene == "match" and self.pending is not None:
            raise ComparisonError("source VS has multiple PAD consumes before source_tick")
        frame = {"scene": SCENES[self.scene], "pads": _consumed_ports(row, 0),
                 "source_seq": row["seq"],
                 "source_tick": row["source_tick"]}
        if self.scene == "match":
            self.pending = frame
        else:
            self.callback.on_frame(frame)

    def _setup(self, row: Mapping[str, Any]) -> None:
        if self.scene != "match":
            raise ComparisonError("source setup occurred outside VS")
        _, raw = _slice(row["payload"], "match_setup", GAME_INFO_SIZE, "source setup")
        self.setup_bytes.append(raw)
        state = _state_from_payload(row["payload"], "source setup")
        state.update(_snapshot_values(row["payload"], "source setup"))
        self.callback.on_setup(self.match_index, state, row["seq"])

    def _tick(self, row: Mapping[str, Any]) -> None:
        if self.scene != "match" or self.pending is None:
            raise ComparisonError("source_tick is missing its preceding VS PAD consume")
        state = _state_from_payload(row["payload"], "source_tick")
        expected_tick = self.last_source_tick.get(self.match_index, -1) + 1
        if row["source_tick"] != expected_tick:
            raise ComparisonError(
                f"source_tick for match {self.match_index} is not contiguous "
                f"(expected {expected_tick}, got {row['source_tick']})")
        if self.pending["source_tick"] != row["source_tick"]:
            raise ComparisonError("source_tick envelope disagrees with its PAD consume")
        state.update(_snapshot_values(row["payload"], "source_tick"))
        if state["scene_frame"] != row["source_tick"]:
            raise ComparisonError("source_tick envelope disagrees with source scene frame")
        frame = self.pending
        frame["state"] = state
        self.callback.on_frame(frame)
        self.pending = None
        self.last_source_tick[self.match_index] = row["source_tick"]

    def consume(self, row: Mapping[str, Any]) -> None:
        event = row.get("event")
        if self.end is not None:
            raise ComparisonError("source stream contains records after end")
        # The raw decoder checks continuity after its first record. This
        # complete-session consumer also requires the original zero origin
        # and announcement order; lifecycle filtering must not repair them.
        sequence = _int(row.get("seq"), "source sequence", 0, (1 << 64) - 1)
        if sequence != self.record_count:
            raise ComparisonError(f"source sequence must equal record index {self.record_count}")
        if self.record_count < 2 and event != ("handshake", "start")[self.record_count]:
            raise ComparisonError("source stream must begin with handshake then start")
        self.record_count += 1
        if event == "handshake":
            if self.handshake is not None:
                raise ComparisonError("source has duplicate handshake")
            self.handshake = row["payload"]
            return
        if event == "start":
            if self.start is not None:
                raise ComparisonError("source has duplicate start")
            self.start = row["payload"]
            return
        if event == "end":
            if self.end is not None:
                raise ComparisonError("source has duplicate end")
            self.end = row["payload"]
            return
        if event != "boundary":
            raise ComparisonError(f"source contains unsupported event {event!r}")
        payload = row.get("payload")
        if not isinstance(payload, dict) or payload.get("whole_session") is not True:
            raise ComparisonError("source boundary is not flagged whole-session")
        boundary = payload.get("boundary")
        if boundary in {"css_enter", "css_cancel_enter", "css_exit", "sss_enter", "sss_exit",
                        "entry", "setup", "draw_return", "vs_exit", "vs_exit_return",
                        "vs_mode_exit", "results_enter", "results_gobj", "results_exit",
                        "results_mode_exit", "scene_teardown", "return_css", "prize_mode_enter",
                        "prize_scene_enter", "prize_scene_exit", "prize_mode_exit",
                        "startup_prize_mode_exit"}:
            self._transition(boundary, row)
            if boundary == "css_enter" and self.first_css is None:
                self.first_css = {**_snapshot_values(payload, "first CSS"), "payload": payload,
                                  "source_seq": row["seq"], "source_tick": row["source_tick"]}
            elif boundary == "setup":
                self._setup(row)
            elif boundary == "return_css" and self.finished:
                self.final_css = {**_snapshot_values(payload, "final CSS"),
                                  "source_seq": row["seq"], "source_tick": row["source_tick"]}
                self.callback.on_final_css(self.final_css, row["seq"])
            return
        if boundary == "pad_poll":
            self._poll(row)
        elif boundary == "pad_consume":
            self._consume(row)
        elif boundary == "source_tick":
            self._tick(row)
        else:
            # Typed diagnostics such as fighter_create/draw_enter are retained
            # by the raw stream but do not define the replay timeline. Unknown
            # boundaries remain errors in the decoder, while known diagnostics
            # are deliberately ignored here after their stream-level checks.
            if boundary not in {"fighter_create", "draw_enter", "scene_exit", "result_enter",
                                "result_return", "scene_reset", "css_exit", "sss_exit"}:
                raise ComparisonError(f"source contains unsupported boundary {boundary!r}")

    def finish(self) -> None:
        if self.handshake is None or self.start is None or self.end is None:
            raise ComparisonError("source stream is missing handshake, start, or end")
        if self.handshake.get("whole_session") is not True or self.start.get("whole_session") is not True:
            raise ComparisonError("source stream is not a whole-session capture")
        if self.handshake.get("capture_id") != self.start.get("capture_id") \
                or self.handshake.get("sequence_id") != self.start.get("sequence_id"):
            raise ComparisonError("source capture/sequence identity disagrees")
        if self.start.get("source_revision") != "GALE01r2":
            raise ComparisonError("source revision is not GALE01r2")
        if self.start.get("match_count") != len(self.setup_bytes):
            raise ComparisonError("source match count disagrees with setup boundaries")
        if self.start.get("match_count") != self.match_count:
            raise ComparisonError("source match count disagrees with MWRC scene spans")
        try:
            semantics.validate_whole_session_observer_records(
                [{"event": "handshake", "payload": self.handshake},
                 {"event": "start", "payload": self.start},
                 *self.lifecycle_rows])
        except (semantics.SemanticError, KeyError, TypeError, ValueError) as error:
            raise ComparisonError(f"source lifecycle is missing, extra, or reordered: {error}") from error
        if not self.finished or self.pending is not None:
            raise ComparisonError("source stream did not complete the final return to CSS")
        if self.end.get("status") != "completed" or self.end.get("natural") is not True:
            raise ComparisonError("source stream did not complete naturally")
        if self.first_css is None or self.final_css is None:
            raise ComparisonError("source stream lacks first or final CSS boundary")
        if self.setup_bytes[0] != self.recipe.setup or any(raw != self.recipe.setup for raw in self.setup_bytes):
            raise ComparisonError("source StartMeleeData setup changed or disagrees with MWRC")
        # Bind the typed first-CSS context and initial source identity to the
        # recipe. The full save block stays byte exact; no profile field is guessed.
        payload = self.first_css["payload"]
        _, rules = _slice(payload, "profile_game_rules", 0x18, "first CSS")
        _, save = _slice(payload, "profile_save_data", 0x55E8, "first CSS")
        _, css = _slice(payload, "menu_css_context", CSS_DATA_SIZE, "first CSS")
        _, ko = _slice(payload, "menu_css_ko_counts", KO_COUNTS_SIZE, "first CSS")
        if (rules != self.recipe.game_rules or save != self.recipe.save_data or
                css != self.recipe.css_data or ko != self.recipe.ko_counts):
            raise ComparisonError("MWRC first-CSS typed context disagrees with source")
        chars = int.from_bytes(_slice(payload, "profile_characters", 2, "first CSS")[1], "big")
        stages = int.from_bytes(_slice(payload, "profile_stages", 2, "first CSS")[1], "big")
        if chars != self.recipe.characters or stages != self.recipe.stages:
            raise ComparisonError("MWRC unlock masks disagree with source first CSS")
        if self.first_css["rng"] != self.recipe.seed:
            raise ComparisonError("MWRC seed disagrees with source first CSS RNG")
        if bytes.fromhex(self.first_css["pad_state_hex"]) != self.recipe.initial_pad:
            raise ComparisonError("MWRC initial PAD state disagrees with source first CSS")
        self.callback.finalize()


def _browser_report(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ComparisonError) as error:
        raise ComparisonError(f"browser report cannot be read: {error}") from error
    if not isinstance(value, dict):
        raise ComparisonError("browser report must be an object")
    return value


def _validate_browser_report(report: Mapping[str, Any], *, recipe_sha: str,
                             trace_sha: str, frame_count: int) -> None:
    """Bind the browser completion report to this exact recipe and trace."""
    if report.get("schema") != "melee-web-browser-retail-replay" or report.get("version") != 1:
        raise ComparisonError("browser report schema/version is unsupported")
    if report.get("recipe_sha256") != recipe_sha:
        raise ComparisonError("browser report recipe_sha256 disagrees with MWRC")
    if report.get("trace_sha256") != trace_sha:
        raise ComparisonError("browser report trace_sha256 disagrees with browser trace")
    if report.get("frames") != frame_count:
        raise ComparisonError("browser report frames disagrees with MWRC recipe")
    metrics = report.get("metrics")
    if not isinstance(metrics, dict):
        raise ComparisonError("browser report is missing metrics")
    for key in ("sourceFrames", "sourceSteps", "sourceDraws"):
        if metrics.get(key) != frame_count:
            raise ComparisonError(f"browser report metrics.{key} disagrees with MWRC recipe")


def _browser_completion_ok(report: Mapping[str, Any]) -> bool:
    return (report.get("final_scene") == SCENES["css"] and
            report.get("complete") is True and report.get("pass") is True and
            not report.get("failures") and not report.get("errors"))


def compare_paths(reference: str | Path, recipe: str | Path, port_trace: str | Path,
                  *, browser_report: str | Path | None = None) -> dict[str, Any]:
    """Compare one source capture, its MWRC recipe, and one browser trace."""
    reference_path = Path(reference)
    recipe_path = Path(recipe)
    port_path = Path(port_trace)
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "scope": "Exact source-consumed scene/input state comparison; no timing, draw cadence, pixels, PCM, or admission claim",
        "fields_compared": list(COMPARE_FIELDS),
        "nonmatch_fields_compared": list(NONMATCH_FIELDS),
        "match_fields_compared": list(COMPARE_FIELDS),
        "limitations": {
            "nonmatch_state": "MWRO CSS/SSS/Results boundary snapshots carry RNG, PAD, and scene-frame fields but do not carry match-frame/FighterHead state; pad_poll rows carry PAD without RNG and can be VI-interrupted. The comparator does not infer a boundary-to-browser per-tick join, so non-match scalar state is not claimed equivalent",
            "final_css": "The browser completion report must bind the recipe/trace hashes and report final_scene=1, complete=true, pass=true with no failures/errors; CSS scalar state is not claimed equivalent",
            "menu_lifecycle": "Menu asset/audio/process fields are outside the browser trace and are not fabricated from scene numbers",
        },
        "inputs": {},
    }
    comparator: Comparator | None = None
    browser: BrowserReader | None = None
    try:
        recipe_obj = Recipe(recipe_path, recipe_path.read_bytes())
        result["inputs"] = {
            "reference": {"path": str(reference_path), "bytes": reference_path.stat().st_size,
                          "sha256": _sha256(reference_path)},
            "recipe": {"path": str(recipe_path), "bytes": recipe_path.stat().st_size,
                       "sha256": _sha256(recipe_path)},
            "port_trace": {"path": str(port_path), "bytes": port_path.stat().st_size,
                           "sha256": _sha256(port_path)},
        }
        if browser_report is not None:
            report_path = Path(browser_report)
            result["inputs"]["browser_report"] = {
                "path": str(report_path), "bytes": report_path.stat().st_size,
                "sha256": _sha256(report_path),
            }
        browser = BrowserReader(port_path)
        comparator = Comparator(recipe_obj, browser)
        source = SourceCollector(comparator, recipe_obj)
        for row in iter_records(reference_path):
            source.consume(row)
        source.finish()
        report_sidecar = _browser_report(Path(browser_report) if browser_report else None)
        final_check = "missing"
        if report_sidecar:
            _validate_browser_report(
                report_sidecar,
                recipe_sha=result["inputs"]["recipe"]["sha256"],
                trace_sha=result["inputs"]["port_trace"]["sha256"],
                frame_count=recipe_obj.frame_count,
            )
            endpoint_scene = report_sidecar.get("final_scene") == SCENES["css"]
            endpoint_ok = _browser_completion_ok(report_sidecar)
            if endpoint_ok:
                final_check = "report_pass"
            elif endpoint_scene:
                final_check = "scene_only"
        nonmatch_pad_check = "uncovered"
        complete = final_check in {"pass", "report_pass"}
        result.update({
            "result": "equivalent" if complete else "incomplete",
            "complete": complete,
            "checks": {
                "source_stream": "pass", "recipe_binding": "pass", "scene_order": "pass",
                "input_order": "pass", "nonmatch_pad": nonmatch_pad_check,
                "initial_css_rng": "pass",
                "match_state": "pass", "final_css": final_check,
            },
            "frames_requested": recipe_obj.frame_count,
            "frames_compared": comparator.compared,
            "nonmatch_frames_compared": comparator.nonmatch_compared,
            "match_frames_compared": comparator.match_compared,
            "scene_spans": recipe_obj.spans,
            "boundary_checkpoints": comparator.boundary_checkpoints,
            "match_setups_compared": comparator.setup_count,
            "source_final_css": {"source_seq": source.final_css["source_seq"],
                                 "source_tick": source.final_css["source_tick"]},
        })
        browser.close()
        return result
    except (ComparisonError, OSError, ValueError, KeyError, struct.error) as error:
        source_error = str(error).startswith(("source ", "MWRC", "this comparator",
                                               "observer", "first CSS", "MWRC"))
        result.update({"result": "invalid" if browser is None or source_error else "diverged",
                       "complete": False, "error": str(error),
                       "checks": result.get("checks", {})})
        if comparator is not None:
            checks = {
                "source_stream": "prefix_only",
                "recipe_binding": "pass_prefix",
                "scene_order": "pass_prefix",
                "input_order": "pass_prefix",
                "initial_css_rng": "pass_prefix",
                "match_state": "pass_prefix",
                "final_css": "unverified",
            }
            result.update({"frames_requested": comparator.recipe.frame_count,
                           "frames_compared": comparator.compared,
                           "nonmatch_frames_compared": comparator.nonmatch_compared,
                           "match_frames_compared": comparator.match_compared,
                           "match_setups_compared": comparator.setup_count,
                           "boundary_checkpoints": comparator.boundary_checkpoints,
                           "checks": checks})
            if comparator.first_difference is not None:
                result["first_difference"] = comparator.first_difference
                if _is_match_field(comparator.first_difference.get("field")) or \
                        comparator.first_difference.get("record") == "match_enter_complete":
                    checks["match_state"] = "fail"
        if browser is not None:
            browser.close()
        return result
