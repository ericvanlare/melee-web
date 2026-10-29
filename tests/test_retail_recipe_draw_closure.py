"""Focused v11 draw-closure format controls.

The generated two-Match rows are modeled parser/encoder controls. They are not
new paired-session evidence. Optional retained raw captures independently
check the legacy v8 byte identity and the exact paired v11 source profile.
"""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import textwrap
import unittest

SOURCE_ROOT = Path(os.environ.get(
    "MELEE_RETAIL_RECIPE_SOURCE_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
TEST_ROOT = Path(os.environ.get(
    "MELEE_RETAIL_RECIPE_TEST_ROOT", Path(__file__).resolve().parents[1]
)).resolve()
sys.path.insert(0, str(SOURCE_ROOT / "tools"))

import whole_session_replay as whole  # noqa: E402

PAD_STATE_HEX = "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000" + "00" * (12 * 66)


_HARNESS = r'''
#include "gameplay_retail_recipe.hpp"
#include "gameplay_menu.h"
#include "gameplay_pad_state.h"
#include <melee/pl/forward.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace melee_web;
extern "C" {
int melee_web_retail_setup(const uint8_t*, uint32_t, MeleeWebMenuMatchSelection*, char*, size_t);
void melee_web_retail_state(void);
void melee_web_retail_entities_index(uint32_t);
void melee_web_retail_entities_checked(void);
uint32_t melee_web_retail_rng(void);
void melee_web_retail_entities_reset(void);
int melee_web_cpu_observation_available(void);
void melee_web_cpu_observation_begin(const uint8_t*, size_t, int);
void melee_web_cpu_observation_tick(size_t);
void melee_web_cpu_observation_draw(size_t);
void melee_web_cpu_observation_preparation_draw(void);
void melee_web_cpu_observation_end(size_t);
uint32_t gm_GetFrameCount(void);
uint32_t gm_8016AEEC(void);
uint16_t gm_8016AEFC(void);
int melee_web_match_source_result(void);
int melee_web_match_end_state(void);
MeleeWebPadState* melee_web_pad_state_decode(const uint8_t*, size_t, char*, size_t);
void melee_web_pad_state_free(MeleeWebPadState*);
void melee_web_pad_state_capture(uint8_t*);
}
extern "C" int melee_web_retail_setup(const uint8_t* raw, uint32_t,
    MeleeWebMenuMatchSelection* out, char*, size_t) {
    if (!raw || !out) return 0;
    *out = {};
    const auto be16 = [](const uint8_t* p) {
        return static_cast<uint16_t>(p[0] << 8 | p[1]);
    };
    const auto be32 = [](const uint8_t* p) {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 |
               uint32_t(p[2]) << 8 | p[3];
    };
    auto& rules = out->start.rules;
    rules.match_kind = raw[0] >> 5;
    rules.timer_enabled = (raw[0] >> 1) & 1;
    rules.timer_counts_up = raw[0] & 1;
    rules.timer_shows_hours = (raw[1] >> 1) & 1;
    rules.friendly_fire = raw[1] & 1;
    rules.is_stock = (raw[2] >> 7) & 1;
    rules.disable_pausing = (raw[2] >> 3) & 1;
    rules.is_vs = (raw[4] >> 6) & 1;
    rules.is_teams = raw[8];
    rules.xB = static_cast<int8_t>(raw[0x0B]);
    rules.stkind = be16(raw + 0x0E);
    rules.time_limit = be32(raw + 0x10);
    rules.x14 = raw[0x14];
    rules.x20 = uint64_t(be32(raw + 0x20)) << 32 | be32(raw + 0x24);
    rules.x2C = std::bit_cast<float>(be32(raw + 0x2C));
    rules.x30 = std::bit_cast<float>(be32(raw + 0x30));
    rules.game_speed = std::bit_cast<float>(be32(raw + 0x34));
    out->player_count = 0;
    for (unsigned slot = 0; slot < GM_MAX_PLAYERS; ++slot) {
        auto& player = out->start.players[slot];
        const auto* source = raw + 0x60 + slot * 0x24;
        player.ckind = static_cast<decltype(player.ckind)>(static_cast<int8_t>(source[0]));
        player.slot_type = static_cast<decltype(player.slot_type)>(source[1]);
        player.stocks = static_cast<int8_t>(source[2]);
        player.color = source[3];
        player.slot = source[4];
        player.rumble_enabled = (source[12] >> 7) & 1;
        if (slot < MELEE_WEB_MENU_MAX_PLAYERS &&
            player.slot_type != Gm_PKind_NA)
            ++out->player_count;
    }
    return 1;
}
extern "C" void melee_web_retail_state(void) { std::cout << "\"rng\":1,\"fighters\":[]"; }
extern "C" void melee_web_retail_entities_index(uint32_t) {}
extern "C" void melee_web_retail_entities_checked(void) {}
extern "C" uint32_t melee_web_retail_rng(void) { return 1; }
extern "C" void melee_web_retail_entities_reset(void) {}
extern "C" int melee_web_cpu_observation_available(void) { return 0; }
extern "C" void melee_web_cpu_observation_begin(const uint8_t*, size_t, int) {}
extern "C" void melee_web_cpu_observation_tick(size_t) {}
extern "C" void melee_web_cpu_observation_draw(size_t) {}
extern "C" void melee_web_cpu_observation_preparation_draw(void) {}
extern "C" void melee_web_cpu_observation_end(size_t) {}
extern "C" uint32_t gm_GetFrameCount(void) { return 0; }
extern "C" uint32_t gm_8016AEEC(void) { return 0; }
extern "C" uint16_t gm_8016AEFC(void) { return 0; }
extern "C" int melee_web_match_source_result(void) { return 0; }
extern "C" int melee_web_match_end_state(void) { return 0; }
extern "C" MeleeWebPadState* melee_web_pad_state_decode(const uint8_t*, size_t, char*, size_t) {
    return reinterpret_cast<MeleeWebPadState*>(uintptr_t{1});
}
extern "C" void melee_web_pad_state_free(MeleeWebPadState*) {}
extern "C" void melee_web_pad_state_capture(uint8_t* bytes) {
    std::fill_n(bytes, MELEE_WEB_PAD_STATE_BYTES, 0);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try {
        std::ifstream file(argv[1], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
        auto recipe = read_retail_replay(bytes);
        std::cout << "PARSED version=" << recipe.version
                  << " whole=" << recipe.whole_session()
                  << " setups=" << recipe.match_setups.size()
                  << " selections=" << recipe.match_selections.size()
                  << " spans=" << recipe.spans.size()
                  << " closures=" << recipe.draw_boundaries.size()
                  << " draws=" << recipe.expected_draws() << " bits=";
        for (bool bit : recipe.draw_boundaries) std::cout << (bit ? '1' : '0');
        std::cout << "\n";
        const std::string mode(argv[2]);
        if (mode == "valid-end" || mode == "one-end") {
            retail_replay_session_initial(recipe);
            const size_t entries = mode == "one-end" ? 1 : recipe.match_selections.size();
            for (size_t index = 0; index < entries; ++index)
                retail_replay_initial(recipe, true, recipe.match_selections[index].start);
            retail_replay_end(recipe.frames.size(), true);
        }
    } catch (const std::exception& error) {
        std::cout << "ERROR " << error.what() << "\n";
    }
}
'''


def _fixture() -> tuple[dict, list[dict]]:
    """Build a realistic-shape, explicitly modeled strict boundary fixture."""
    scene_css, scene_sss, scene_match, scene_results = 1, 2, 3, 4
    spans = [
        {"scene": scene_css, "first_frame": 0, "last_frame": 1},
        {"scene": scene_sss, "first_frame": 2, "last_frame": 2},
        {"scene": scene_match, "first_frame": 3, "last_frame": 5},
        {"scene": scene_results, "first_frame": 6, "last_frame": 6},
        {"scene": scene_css, "first_frame": 7, "last_frame": 7},
        {"scene": scene_sss, "first_frame": 8, "last_frame": 8},
        {"scene": scene_match, "first_frame": 9, "last_frame": 10},
        {"scene": scene_results, "first_frame": 11, "last_frame": 11},
    ]
    pads = ["00" * whole.PAD_SEMANTIC_SIZE] * 4
    frames = []
    for index in range(12):
        frames.append({"index": index, "scene_code": next(
            span["scene"] for span in spans
            if span["first_frame"] <= index <= span["last_frame"]),
            "source_tick": None, "source_seq": None, "pads": pads.copy()})

    rows: list[dict] = []
    sequence = 1

    def boundary(kind: str, match: int | None = None, *, tick: int | None = None,
                 draw: int | None = None, slices: list[dict] | None = None,
                 gprs: list[int] | None = None) -> dict:
        nonlocal sequence
        payload = {"boundary": kind}
        if match is not None:
            payload["match_index"] = match
        row = {"seq": sequence, "event": "boundary", "payload": payload}
        if tick is not None:
            row["source_tick"] = tick
        if draw is not None:
            row["draw_ordinal"] = draw
        if slices is not None:
            payload["slices"] = slices
        if gprs is not None:
            payload["gprs"] = gprs
        rows.append(row)
        sequence += 1
        return row

    frame_index = 3
    for match_index, ticks_per_batch in ((0, (2, 1)), (1, (2,))):
        boundary("entry", match_index)
        local_tick = draw = 0
        for batch_size in ticks_per_batch:
            for _ in range(batch_size):
                frame = frames[frame_index]
                frame["source_tick"] = local_tick
                queue_base = 0x81234000 + match_index * 0x1000 + local_tick * 0x30
                slot = bytearray(0x30)
                for port in range(4):
                    slot[port * 12:port * 12 + 11] = bytes.fromhex(pads[port])
                queue = bytearray(0x0C)
                queue[0] = 5
                queue[8:12] = queue_base.to_bytes(4, "big")
                gprs = [0] * 32
                gprs[6] = 0
                gprs[25] = queue_base
                pad_row = boundary("pad_consume", match_index, tick=local_tick,
                    draw=draw, slices=[
                        {"name": "pad_queue", "address": 0x804C1F78,
                         "size": 0x0C, "hex": queue.hex()},
                        {"name": "pad_slot", "address": queue_base,
                         "size": 0x30, "hex": slot.hex()},
                    ], gprs=gprs)
                frame["source_seq"] = pad_row["seq"]
                boundary("source_tick", match_index, tick=local_tick, draw=draw)
                local_tick += 1
                frame_index += 1
            boundary("draw_enter", match_index, tick=local_tick, draw=draw)
            boundary("draw_return", match_index, tick=local_tick, draw=draw)
            draw += 1
        boundary("results_enter", match_index)
        frame_index += 1
        if match_index == 0:
            frame_index += 2  # intervening CSS and SSS frames
    setup = bytearray(whole.GAME_INFO_SIZE)
    setup[0] = 1 << 5                         # VS melee; no timer
    setup[2] = 1 << 7                         # stock; pausing enabled
    setup[4] = 1 << 6                         # ordinary VS profile
    setup[0x0B] = 0xFF                        # Items None
    setup[0x0E:0x10] = (3).to_bytes(2, "big") # Pokemon Stadium
    setup[0x10:0x14] = (0).to_bytes(4, "big")
    setup[0x14] = 0
    setup[0x20:0x28] = b"\xff" * 8
    setup[0x2C:0x30] = bytes.fromhex("3f800000")
    setup[0x30:0x34] = bytes.fromhex("3f800000")
    setup[0x34:0x38] = bytes.fromhex("3f800000")
    for slot in range(6):
        base = 0x60 + slot * 0x24
        setup[base + 1] = 3                    # source NA by default
    for port, costume in ((1, 1), (2, 0)):
        base = 0x60 + (port - 1) * 0x24
        setup[base] = 8                        # external Mario
        setup[base + 1] = 0                    # human
        setup[base + 2] = 4
        setup[base + 3] = costume
        setup[base + 4] = port
        setup[base + 12] = 0x80                # rumble enabled
    setup_hex = setup.hex()
    capture = {
        "identity": {"match_count": 2},  # modeled exact-two input identity
        "setup_hex": setup_hex,
        "setup_hexes": [setup_hex] * 2,
        "frames": frames,
        "spans": spans,
        "first_css": {
            "rng": 1,
            "pad_state_hex": PAD_STATE_HEX,
            "profile_masks": {"characters": 0xFFFF, "stages": 0xFFFF},
            "game_rules_hex": "00" * whole.semantics.GAME_RULES_SIZE,
            "save_data_hex": "ffff ffff".replace(" ", "") +
                             "00" * (whole.semantics.SAVE_DATA_SIZE - 4),
            "css_data_hex": "00" * whole.CSS_DATA_SIZE,
            "ko_counts_hex": "00" * whole.KO_COUNTS_SIZE,
        },
    }
    return capture, rows


class RetailRecipeDrawClosureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        generated = TEST_ROOT / "build" / "gameplay-source" / "src"
        include_dirs = [
            SOURCE_ROOT / "src", TEST_ROOT / "src", generated,
            TEST_ROOT / ".deps" / "aurora" / "include",
            TEST_ROOT / ".deps" / "melee" / "extern" / "dolphin" / "include",
        ]
        if compiler is None or not (generated / "melee" / "ft" / "types.h").is_file():
            raise unittest.SkipTest("native compiler and generated gameplay source are required")
        if not all(path.is_dir() for path in include_dirs):
            raise unittest.SkipTest("pinned native include roots are required")
        cls._tmp = tempfile.TemporaryDirectory(prefix="retail-draw-closure-")
        tmp = Path(cls._tmp.name)
        harness = tmp / "draw_closure.cpp"
        recipe_object = tmp / "gameplay_retail_recipe.o"
        harness_object = tmp / "draw_closure.o"
        cls.executable = tmp / "draw_closure"
        harness.write_text(textwrap.dedent(_HARNESS), encoding="utf-8")
        flags = ["-std=c++20", "-DTARGET_PC", "-DAURORA",
                 "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                 *(f"-I{path}" for path in include_dirs)]
        for source, output in ((SOURCE_ROOT / "src/gameplay_retail_recipe.cpp", recipe_object),
                               (harness, harness_object)):
            result = subprocess.run(
                [compiler, *flags, "-c", str(source), "-o", str(output)],
                capture_output=True, text=True, timeout=45)
            if result.returncode:
                raise RuntimeError(
                    f"native fixture compile failed for {source}:\n"
                    f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
        linker_gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        result = subprocess.run(
            [compiler, linker_gc, str(recipe_object), str(harness_object),
             "-o", str(cls.executable)],
            capture_output=True, text=True, timeout=45)
        if result.returncode:
            raise RuntimeError(
                "native fixture link failed:\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
        cls.capture, cls.rows = _fixture()
        cls.payload, cls.metadata = whole.encode_v11(cls.capture, cls.rows)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "_tmp"):
            cls._tmp.cleanup()

    def parse(self, payload: bytes, mode: str = "parse") -> str:
        with tempfile.TemporaryDirectory(prefix="mwrc-v11-case-") as directory:
            recipe = Path(directory) / "recipe.mwrc"
            recipe.write_bytes(payload)
            result = subprocess.run([str(self.executable), str(recipe), mode],
                                    check=True, capture_output=True, text=True, timeout=10)
        return result.stdout

    def test_encoder_derives_each_match_batch_and_native_reader_roundtrips(self):
        self.assertEqual(self.metadata["version"], 11)
        self.assertEqual(self.metadata["draw_closure_count"], 12)
        self.assertEqual(self.metadata["draw_closure_false_count"], 2)
        self.assertEqual(self.metadata["non_match_draw_policy"], "close_each_step_unobserved")
        trace = self.metadata["draw_boundary_trace"]
        self.assertEqual(trace["schema"], "melee-web-mwro-draw-boundary-trace-v1")
        self.assertEqual(trace["matches"], [
            {"match_index": 0, "entry_seq": 1, "results_enter_seq": 12,
             "ticks": [
                 {"source_tick": 0, "frame_index": 3, "draws_before": 0,
                  "pad_consume_seq": 2, "source_tick_seq": 3},
                 {"source_tick": 1, "frame_index": 4, "draws_before": 0,
                  "pad_consume_seq": 4, "source_tick_seq": 5},
                 {"source_tick": 2, "frame_index": 5, "draws_before": 1,
                  "pad_consume_seq": 8, "source_tick_seq": 9},
             ],
             "draws": [
                 {"draw_ordinal": 0, "source_tick_after": 2,
                  "draw_enter_source_tick": 2, "draw_enter_ordinal": 0,
                  "draw_return_source_tick": 2, "draw_return_ordinal": 0,
                  "draw_enter_seq": 6, "draw_return_seq": 7,
                  "first_frame": 3, "last_frame": 4,
                  "source_tick_seqs": [3, 5]},
                 {"draw_ordinal": 1, "source_tick_after": 3,
                  "draw_enter_source_tick": 3, "draw_enter_ordinal": 1,
                  "draw_return_source_tick": 3, "draw_return_ordinal": 1,
                  "draw_enter_seq": 10, "draw_return_seq": 11,
                  "first_frame": 5, "last_frame": 5,
                  "source_tick_seqs": [9]},
             ]},
            {"match_index": 1, "entry_seq": 13, "results_enter_seq": 20,
             "ticks": [
                 {"source_tick": 0, "frame_index": 9, "draws_before": 0,
                  "pad_consume_seq": 14, "source_tick_seq": 15},
                 {"source_tick": 1, "frame_index": 10, "draws_before": 0,
                  "pad_consume_seq": 16, "source_tick_seq": 17},
             ],
             "draws": [
                 {"draw_ordinal": 0, "source_tick_after": 2,
                  "draw_enter_source_tick": 2, "draw_enter_ordinal": 0,
                  "draw_return_source_tick": 2, "draw_return_ordinal": 0,
                  "draw_enter_seq": 18, "draw_return_seq": 19,
                  "first_frame": 9, "last_frame": 10,
                  "source_tick_seqs": [15, 17]},
             ]},
        ])
        output = self.parse(self.payload, "valid-end")
        self.assertIn("PARSED version=11 whole=1 setups=2 selections=2 spans=8 closures=12 draws=10 ",
                      output)
        self.assertIn("bits=111011111011", output)
        self.assertIn('"record":"end","frames":12,"status":"captured"', output)
        self.assertNotIn("ERROR ", output)
        refused = self.parse(self.payload, "one-end")
        self.assertIn("ERROR Whole-session replay did not enter its exact declared match count", refused)

    def test_native_reader_rejects_v11_geometry_and_policy_mutations(self):
        blob = self.payload
        count_offset = len(blob) - 6
        malformed = bytearray(blob)
        malformed[count_offset:count_offset + 4] = struct.pack(">I", 11)
        self.assertIn("ERROR Whole-session draw closure count must equal its frame count",
                      self.parse(bytes(malformed)))
        self.assertIn("ERROR Whole-session span table disagrees with its transport",
                      self.parse(blob[:-1]))
        self.assertIn("ERROR Whole-session span table disagrees with its transport",
                      self.parse(blob + b"\0"))
        malformed = bytearray(blob)
        malformed[-1] |= 0x01
        self.assertIn("ERROR Whole-session draw closure bitset has noncanonical unused bits",
                      self.parse(bytes(malformed)))
        malformed = bytearray(blob)
        malformed[-1] &= ~0x10  # final Results span endpoint, frame 11
        self.assertIn("ERROR Whole-session scene span must end at a closed draw boundary",
                      self.parse(bytes(malformed)))
        malformed = bytearray(blob)
        malformed[-2] &= ~0x80  # first frame of a two-frame non-Match CSS span
        self.assertIn("ERROR Non-Match draw closures must use the declared close-each-step policy",
                      self.parse(bytes(malformed)))
        context_bytes = (whole.semantics.GAME_RULES_SIZE + whole.semantics.SAVE_DATA_SIZE +
                         whole.CSS_DATA_SIZE + whole.KO_COUNTS_SIZE)
        setup_table = whole.HEADER.size + whole.CONTEXT_HEADER.size + context_bytes
        malformed = bytearray(blob)
        malformed[setup_table:setup_table + 2] = struct.pack(">H", 3)
        self.assertIn("ERROR Whole-session setup table requires exactly two setups and zero flags",
                      self.parse(bytes(malformed)))

    def test_native_reader_rejects_wrong_profile_opaque_setup_and_route(self):
        setup_table = whole.HEADER.size + whole.CONTEXT_HEADER.size + whole.CONTEXT_BYTES
        first_setup = setup_table + 4
        second_setup = first_setup + whole.GAME_INFO_SIZE

        wrong_profile = bytearray(self.payload)
        for start in (first_setup, second_setup):
            wrong_profile[start + 0x0F] = 4  # both rows agree, but stage is not Stadium
        self.assertIn("ERROR MWRC v11 setup is not the exact paired Stadium stock4 ItemsNone rules profile",
                      self.parse(bytes(wrong_profile)))

        unequal_raw = bytearray(self.payload)
        unequal_raw[second_setup + 0x5C] = 1  # ignored by decoded profile, still a source byte
        self.assertIn("ERROR MWRC v11 paired Stadium setup rows differ in full StartMeleeData bytes",
                      self.parse(bytes(unequal_raw)))

        wrong_route = bytearray(self.payload)
        closure_size = 4 + (len(self.capture["frames"]) + 7) // 8
        span_start = len(wrong_route) - closure_size - (2 + 8 * whole.SPAN.size)
        wrong_route[span_start + 2 + 3 * whole.SPAN.size] = whole.SCENES["prize"]
        self.assertIn("ERROR MWRC v11 requires the normal CSS/SSS/Match/Results route twice",
                      self.parse(bytes(wrong_route)))

    def test_python_encoder_refuses_wrong_profile_entity_prefix_and_prize(self):
        wrong_setup = bytearray.fromhex(self.capture["setup_hex"])
        wrong_setup[0x0F] = 4
        bad_capture = {**self.capture, "setup_hex": wrong_setup.hex(),
                       "setup_hexes": [wrong_setup.hex()] * 2}
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "not the exact paired Stadium stock4 Mario profile"):
            whole.encode_v11(bad_capture, self.rows)

        entity_rows = [{"seq": 0, "event": "start",
                        "payload": {"entity_profile": "unapproved-prefix"}}, *self.rows]
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "does not accept an entity-prefix observer variant"):
            whole.encode_v11(self.capture, entity_rows)

        prize_rows = [*self.rows, {"seq": 100, "event": "boundary",
                      "payload": {"boundary": "prize_scene_enter"}}]
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "does not accept a Prize scene or entity-prefix lifecycle"):
            whole.encode_v11(self.capture, prize_rows)

    def test_python_pair_setup_requires_full_raw_byte_equality(self):
        second = bytearray.fromhex(self.capture["setup_hex"])
        second[0x5C] = 1
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "setup rows differ in their full StartMeleeData bytes"):
            whole.validate_stadium_pair_setups([self.capture["setup_hex"], second.hex()])

    def test_producer_refuses_missing_or_reordered_consumed_input(self):
        missing = list(self.rows)
        missing = [row for row in missing
                   if not (row["event"] == "boundary" and
                           row["payload"].get("boundary") == "pad_consume" and
                           row["payload"].get("match_index") == 0)]
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "PAD and SourceTick counts disagree"):
            whole.encode_v11(self.capture, missing)
        reordered = list(self.rows)
        reordered[0], reordered[1] = reordered[1], reordered[0]
        with self.assertRaisesRegex(whole.WholeSessionReplayError,
                                    "records are out of sequence"):
            whole.encode_v11(self.capture, reordered)
        foreign = [dict(row) for row in self.rows]
        foreign[1] = {**foreign[1], "payload": {
            **foreign[1]["payload"], "match_index": 1}}
        with self.assertRaises(whole.WholeSessionReplayError):
            whole.encode_v11(self.capture, foreign)

    def test_retained_v8_export_is_byte_identical_when_inputs_are_pinned(self):
        raw_path = os.environ.get("MELEE_RETAIL_RECIPE_RAW_CAPTURE")
        expected_path = os.environ.get("MELEE_RETAIL_RECIPE_EXPECTED_V8")
        if not raw_path and not expected_path:
            self.skipTest("retained MWRO and expected v8 recipe were not configured")
        if not raw_path or not expected_path:
            self.fail("partial retained v8 fixture configuration is not allowed")
        capture = whole.capture_from_path(raw_path)
        payload, _ = whole.encode_v8(capture)
        self.assertEqual(payload, Path(expected_path).read_bytes())

    def test_retained_paired_stadium_stream_encodes_as_exact_v11(self):
        raw_path = os.environ.get("MELEE_RETAIL_RECIPE_PAIRED_RAW_CAPTURE")
        status_path = os.environ.get("MELEE_RETAIL_RECIPE_PAIRED_STATUS")
        if not raw_path and not status_path:
            self.skipTest("retained paired MWRO/status fixture was not configured")
        if not raw_path or not status_path:
            self.fail("partial retained paired MWRO/status configuration is not allowed")
        capture = whole.capture_from_path(raw_path, status_path)
        payload, metadata = whole.encode_v11(capture, whole.iter_records(
            Path(raw_path)))
        self.assertEqual(metadata["version"], 11)
        self.assertEqual(metadata["draw_closure_count"], len(capture["frames"]))
        self.assertEqual([span["scene"] for span in capture["spans"]],
                         list(whole.V11_PAIRED_STADIUM_SCENES))
        self.assertEqual(whole.validate_stadium_pair_setups(capture["setup_hexes"]),
                         [whole.V11_PAIRED_STADIUM_SETUP] * 2)
        self.assertTrue(payload.startswith(b"MWRC"))
        native = self.parse(payload, "valid-end")
        self.assertIn("PARSED version=11 whole=1 setups=2 selections=2 spans=8 closures=", native)
        self.assertNotIn("ERROR ", native)


if __name__ == "__main__":
    unittest.main()
