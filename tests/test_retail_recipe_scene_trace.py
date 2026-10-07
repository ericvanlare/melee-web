"""Compile the production retail trace against observers that only support matches.

The whole-session path must be safe while the original CSS, SSS, Results, and
Prize owners are alive.  This test keeps the observer deliberately small: a
fighter-state call is the failure that aborted the real CSS prefix, while the
CPU observer represents the match-only diagnostic stream.
"""

from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


_HARNESS = r'''
#include "gameplay_retail_recipe.hpp"
#include "gameplay_pad_state.h"
#include <melee/pl/forward.h>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace melee_web;

extern "C" {
int melee_web_retail_setup(const uint8_t*, uint32_t,
    MeleeWebMenuMatchSelection*, char*, size_t);
void melee_web_retail_state(void);
uint32_t melee_web_retail_rng(void);
uint32_t gm_GetFrameCount(void);
uint32_t gm_8016AEEC(void);
uint16_t gm_8016AEFC(void);
int melee_web_match_source_result(void);
int melee_web_match_end_state(void);
void melee_web_pad_state_capture(uint8_t*);
int melee_web_cpu_observation_available(void);
void melee_web_cpu_observation_begin(const uint8_t*, size_t, int);
void melee_web_cpu_observation_tick(size_t);
void melee_web_cpu_observation_draw(size_t);
void melee_web_cpu_observation_preparation_draw(void);
void melee_web_cpu_observation_end(size_t);
}

namespace fake {
int state = 0;
int rng = 0;
int cpu_begin = 0;
int cpu_tick = 0;
int cpu_draw = 0;
int cpu_preparation_draw = 0;
int cpu_end = 0;
int entity_resets = 0;
std::vector<uint32_t> entity_indices;
bool state_allowed = false;
bool cpu_available = true;
void reset() {
    state = rng = cpu_begin = cpu_tick = cpu_draw =
        cpu_preparation_draw = cpu_end = 0;
    entity_resets = 0;
    entity_indices.clear();
    state_allowed = false;
    cpu_available = true;
}
void write_state() {
    // This fake is intentionally match-only.  It is valid output, but has no
    // menu fallback, so a CSS/SSS/Results/Prize call would be observable.
    if (!state_allowed) {
        std::cerr << "fighter-state observer called outside VS\n";
        std::exit(91);
    }
    ++state;
    std::cout << "\"rng\":7,\"match_frame\":0,\"fighters\":[]";
}
}

constexpr std::array<uint8_t, 0x60> kTestSetupRules = {
    0x30,0x00,0x86,0x4c,0xc3,0x00,0x00,0x00,0x00,0x00,0x00,0xff,
    0xff,0x6e,0x00,0x20,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x3f,0x80,0x00,0x00,
    0x3f,0x80,0x00,0x00,0x3f,0x80,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};
constexpr uint8_t kTestV9Roster[3][4] = {
    {8, 2, 20, 9}, {22, 23, 6, 21}, {0, 25, 7, 13},
};
constexpr uint8_t kTestV10Roster[3][4] = {
    {1, 5, 11, 12}, {15, 10, 24, 18}, {4, 14, 16, 17},
};

extern "C" int melee_web_retail_setup(const uint8_t* raw, uint32_t,
    MeleeWebMenuMatchSelection* out, char*, size_t) {
    if (!raw || !out) return 0;
    out->player_count = 4;
    out->start.rules.stkind = 0x20;
    for (unsigned slot = 0; slot < 4; ++slot) {
        auto& player = out->start.players[slot];
        player.slot_type = Gm_PKind_Cpu;
        player.cpu_kind = 4;
        player.cpu_level = 9;
        player.stocks = 4;
        player.color = slot;
        player.rumble_enabled = false;
        player.ckind = raw[0x130 + slot];
    }
    for (unsigned slot = 4; slot < GM_MAX_PLAYERS; ++slot)
        out->start.players[slot].slot_type = Gm_PKind_NA;
    return 1;
}
extern "C" void melee_web_retail_state(void) { fake::write_state(); }
extern "C" void melee_web_retail_entities(void) {
    if (!fake::state_allowed) std::abort();
    std::cout << ",\"fighter_entities\":[]";
}
extern "C" void melee_web_retail_entities_index(uint32_t match_index) {
    if (!fake::state_allowed) std::abort();
    fake::entity_indices.push_back(match_index);
    std::cout << ",\"fighter_entities\":[";
    for (unsigned slot = 0; slot < 4; ++slot) {
        if (slot) std::cout << ",";
        std::cout << "{\"match_index\":" << match_index
                  << ",\"slot\":" << slot
                  << ",\"entity_index\":0,\"generation\":0"
                  << ",\"fighter_player_id\":" << slot
                  << ",\"fighter_gobj_linked\":true}";
    }
    std::cout << "]";
}
extern "C" void melee_web_retail_entities_reset(void) { ++fake::entity_resets; }
extern "C" uint32_t melee_web_retail_rng(void) { ++fake::rng; return 123; }
extern "C" uint32_t gm_GetFrameCount(void) { return 0; }
extern "C" uint32_t gm_8016AEEC(void) { return 0; }
extern "C" uint16_t gm_8016AEFC(void) { return 0; }
extern "C" int melee_web_match_source_result(void) { return 0; }
extern "C" int melee_web_match_end_state(void) { return 0; }
extern "C" MeleeWebPadState* melee_web_pad_state_decode(
    const uint8_t*, size_t, char*, size_t) {
    return reinterpret_cast<MeleeWebPadState*>(uintptr_t{1});
}
extern "C" void melee_web_pad_state_free(MeleeWebPadState*) {}
extern "C" void melee_web_pad_state_capture(uint8_t* bytes) {
    for (size_t i = 0; i < MELEE_WEB_PAD_STATE_BYTES; ++i) bytes[i] = 0;
}
extern "C" int melee_web_cpu_observation_available(void) { return fake::cpu_available; }
extern "C" void melee_web_cpu_observation_begin(const uint8_t*, size_t, int) {
    ++fake::cpu_begin;
}
extern "C" void melee_web_cpu_observation_tick(size_t) { ++fake::cpu_tick; }
extern "C" void melee_web_cpu_observation_draw(size_t) { ++fake::cpu_draw; }
extern "C" void melee_web_cpu_observation_preparation_draw(void) {
    ++fake::cpu_preparation_draw;
}
extern "C" void melee_web_cpu_observation_end(size_t) { ++fake::cpu_end; }

static void put_u16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}
static void put_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 24));
    bytes.push_back(static_cast<uint8_t>(value >> 16));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}
static std::vector<uint8_t> setup_record(uint32_t version, size_t index) {
    std::vector<uint8_t> raw(0x138, 0);
    std::copy(kTestSetupRules.begin(), kTestSetupRules.end(), raw.begin());
    if (version == 9)
        std::copy(std::begin(kTestV9Roster[index]), std::end(kTestV9Roster[index]),
                  raw.begin() + 0x130);
    else if (version == 10)
        std::copy(std::begin(kTestV10Roster[index]), std::end(kTestV10Roster[index]),
                  raw.begin() + 0x130);
    raw[0x137] = static_cast<uint8_t>(index);
    return raw;
}
static void add_span(std::vector<uint8_t>& bytes, uint8_t scene,
                     uint32_t first, uint32_t last) {
    bytes.push_back(scene);
    bytes.push_back(0);
    put_u16(bytes, 0);
    put_u32(bytes, first);
    put_u32(bytes, last);
}
static std::vector<uint8_t> recipe_fixture(uint32_t version) {
    constexpr uint32_t frames = 5;
    std::vector<uint8_t> bytes{'M', 'W', 'R', 'C'};
    put_u32(bytes, version);
    put_u32(bytes, 0x12345678);
    put_u32(bytes, frames);
    put_u16(bytes, 0xffff);
    put_u16(bytes, 0xffff);
    put_u16(bytes, kRetailReplayContextVersion);
    put_u16(bytes, 0);
    put_u32(bytes, kRetailReplayContextBytes);
    bytes.insert(bytes.end(), kRetailReplayGameRulesBytes, 0);
    std::vector<uint8_t> save(kRetailReplaySaveDataBytes, 0);
    save[0] = save[1] = save[2] = save[3] = 0xff;
    bytes.insert(bytes.end(), save.begin(), save.end());
    bytes.insert(bytes.end(), kRetailReplayCssDataBytes, 0);
    bytes.insert(bytes.end(), kRetailReplayKoCountsBytes, 0);
    if (version == 9 || version == 10) {
        put_u16(bytes, 3);
        put_u16(bytes, 0);
        for (size_t index = 0; index < 3; ++index) {
            const auto raw = setup_record(version, index);
            bytes.insert(bytes.end(), raw.begin(), raw.end());
        }
    } else {
        const auto raw = setup_record(8, 0);
        bytes.insert(bytes.end(), raw.begin(), raw.end());
    }
    bytes.insert(bytes.end(), MELEE_WEB_PAD_STATE_BYTES, 0);
    bytes.insert(bytes.end(), frames * 44, 0);
    if (version == 9 || version == 10) {
        put_u16(bytes, 5);
        add_span(bytes, kRetailReplayCss, 0, 0);
        add_span(bytes, kRetailReplayMatch, 1, 1);
        add_span(bytes, kRetailReplayMatch, 2, 2);
        add_span(bytes, kRetailReplayMatch, 3, 3);
        add_span(bytes, kRetailReplayResults, 4, 4);
    } else {
        put_u16(bytes, 3);
        add_span(bytes, kRetailReplayCss, 0, 0);
        add_span(bytes, kRetailReplayMatch, 1, 3);
        add_span(bytes, kRetailReplayResults, 4, 4);
    }
    return bytes;
}
static std::string recipe_error(const std::vector<uint8_t>& bytes) {
    try {
        auto parsed = melee_web::read_retail_replay(bytes);
        (void) parsed;
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}
static void require_error(const std::vector<uint8_t>& bytes,
                          const char* expected, const char* label) {
    const auto error = recipe_error(bytes);
    if (error.find(expected) == std::string::npos)
        throw std::runtime_error(std::string("format fixture ") + label +
                                 " expected error containing: " + expected +
                                 "; actual: " + error);
    std::cout << "FORMAT " << label << " rejected" << std::endl;
}
static void reader_format_checks() {
    using namespace melee_web;
    auto v10 = read_retail_replay(recipe_fixture(10));
    if (v10.version != 10 || !v10.whole_session() || v10.frames.size() != 5 ||
        v10.match_setups.size() != 3 || v10.match_selections.size() != 3 ||
        v10.spans.size() != 5 ||
        std::count_if(v10.spans.begin(), v10.spans.end(),
            [](const RetailReplaySpan& span) { return span.scene == kRetailReplayMatch; }) != 3)
        throw std::runtime_error("valid v10 fixture lost its three setup/span profile");
    std::cout << "FORMAT valid_v10_count_span_profile" << std::endl;

    auto malformed_count = recipe_fixture(10);
    const size_t setup_table = 16 + 4 + kRetailReplayContextHeaderBytes + kRetailReplayContextBytes;
    malformed_count[setup_table] = 0;
    malformed_count[setup_table + 1] = 2;
    require_error(malformed_count, "requires exactly three setups", "v10_setup_count");

    auto truncated = recipe_fixture(10);
    truncated.resize(setup_table + 4 + 3 * 0x138 - 1);
    require_error(truncated, "setup table is truncated", "v10_truncated_setup_table");

    auto wrong_roster = recipe_fixture(10);
    wrong_roster[setup_table + 4 + 0x130] = 2;
    require_error(wrong_roster, "distinct twelve-character CPU9 roster", "v10_wrong_roster");

    auto wrong_rules = recipe_fixture(10);
    wrong_rules[setup_table + 4] ^= 1;
    require_error(wrong_rules, "rules differ from the accepted four-stock CPU9 profile",
                  "v10_wrong_rules");

    auto wrong_spans = recipe_fixture(10);
    const size_t span_table = wrong_spans.size() - (2 + 5 * kRetailReplaySpanBytes);
    wrong_spans[span_table + 2 + kRetailReplaySpanBytes] = kRetailReplayCss;
    require_error(wrong_spans, "setup table does not match its match scene spans",
                  "v10_wrong_match_span_count");

    auto v9 = read_retail_replay(recipe_fixture(9));
    if (v9.version != 9 || !v9.whole_session() || v9.match_setups.size() != 3 ||
        v9.match_selections.size() != 3 || v9.spans.size() != 5)
        throw std::runtime_error("valid v9 fixture changed its established transport");
    std::cout << "FORMAT preserved_v9" << std::endl;

    auto v8 = read_retail_replay(recipe_fixture(8));
    if (v8.version != 8 || !v8.whole_session() || v8.match_setups.size() != 1 ||
        v8.spans.size() != 3)
        throw std::runtime_error("valid v8 fixture changed its established transport");
    std::cout << "FORMAT preserved_v8" << std::endl;
}

static void print_counts(const char* label) {
    std::cout << "COUNTS " << label << " " << fake::state << ' ' << fake::rng
              << ' ' << fake::cpu_begin << ' ' << fake::cpu_tick << ' '
              << fake::cpu_draw << ' ' << fake::cpu_preparation_draw << ' '
              << fake::cpu_end << "\n";
}
static void print_entity_counts(const char* label) {
    std::cout << "ENTITY_COUNTS " << label << " " << fake::entity_resets << " "
              << fake::entity_indices.size();
    for (auto index : fake::entity_indices) std::cout << " " << index;
    std::cout << "\n";
}
static void identity_observation_case(uint32_t version) {
    using namespace melee_web;
    fake::reset();
    auto recipe = read_retail_replay(recipe_fixture(version));
    fake::state_allowed = true;
    retail_replay_session_initial(recipe);
    retail_replay_initial(recipe, true, recipe.match_selections[0].start);
    const auto match = std::find_if(recipe.spans.begin(), recipe.spans.end(),
        [](const RetailReplaySpan& span) { return span.scene == kRetailReplayMatch; });
    if (match == recipe.spans.end()) throw std::runtime_error("fixture lost match span");
    retail_replay_frame(recipe, match->first_frame, kRetailReplayMatch);
    fake::state_allowed = false;
    print_entity_counts(version == 9 ? "v9" : "v10");
}

int main() {
    using namespace melee_web;
    RetailReplayRecipe whole;
    whole.version = 8;
    whole.frames.resize(1);
    whole.frames[0].bytes.fill(0);

    // The session header is emitted before any scene owns a fighter world.
    retail_replay_session_initial(whole);
    print_counts("session_initial");

    retail_replay_frame(whole, 0, kRetailReplayCss);
    retail_replay_frame(whole, 0, kRetailReplaySss);
    // The v8 regression emits state snapshots but does not capture CPU diagnostics.
    fake::state_allowed = true;
    retail_replay_initial(whole, true);
    retail_replay_frame(whole, 0, kRetailReplayMatch);
    fake::state_allowed = false;
    retail_replay_frame(whole, 0, kRetailReplayResults);
    retail_replay_frame(whole, 0, kRetailReplayPrize);
    fake::state_allowed = true;
    retail_replay_initial(whole, true);
    retail_replay_frame(whole, 0, kRetailReplayMatch);
    retail_replay_draw(whole, 0);
    retail_replay_preparation_draw(whole);
    retail_replay_end(1, true);
    print_counts("whole");
    print_entity_counts("v8");

    // A fresh session can capture again and close during its first match.
    fake::reset();
    retail_replay_session_initial(whole);
    fake::state_allowed = true;
    retail_replay_initial(whole, true);
    retail_replay_frame(whole, 0, kRetailReplayMatch);
    retail_replay_end(1, true);
    print_counts("restart");

    fake::reset();
    fake::cpu_available = false;
    retail_replay_session_initial(whole);
    fake::state_allowed = true;
    retail_replay_initial(whole, true);
    retail_replay_frame(whole, 0, kRetailReplayMatch);
    retail_replay_end(1, true);
    print_counts("disabled");

    // Version 3 keeps its established match-only tracing contract.
    fake::reset();
    RetailReplayRecipe legacy;
    legacy.version = 3;
    legacy.frames.resize(1);
    legacy.frames[0].bytes.fill(0);
    fake::state_allowed = true;
    retail_replay_initial(legacy, false);
    retail_replay_frame(legacy, 0);
    retail_replay_draw(legacy, 0);
    retail_replay_preparation_draw(legacy);
    retail_replay_end(1);
    print_counts("legacy");
    reader_format_checks();
    std::cout << "IDENTITY_CASE_BEGIN v9\n";
    identity_observation_case(9);
    std::cout << "IDENTITY_CASE_BEGIN v10\n";
    identity_observation_case(10);
}
'''


class RetailRecipeSceneTraceTests(unittest.TestCase):
    def test_scene_aware_trace_skips_match_observers_outside_vs(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        generated = ROOT / "build" / "gameplay-source" / "src"
        if compiler is None or not (generated / "melee" / "ft" / "types.h").is_file():
            self.skipTest("native compiler and generated gameplay source are required")

        include_dirs = [
            ROOT / "src",
            generated,
            ROOT / ".deps" / "aurora" / "include",
            ROOT / ".deps" / "melee" / "extern" / "dolphin" / "include",
        ]
        if not all(path.is_dir() for path in include_dirs):
            self.skipTest("pinned native include roots are required")

        with tempfile.TemporaryDirectory(prefix="retail-scene-trace-") as directory:
            tmp = Path(directory)
            harness = tmp / "scene_trace.cpp"
            recipe_object = tmp / "gameplay_retail_recipe.o"
            harness_object = tmp / "scene_trace.o"
            executable = tmp / "scene_trace"
            harness.write_text(textwrap.dedent(_HARNESS))
            flags = [
                "-std=c++20", "-DTARGET_PC", "-DAURORA",
                "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                *(f"-I{path}" for path in include_dirs),
            ]
            for source, output in (
                (ROOT / "src" / "gameplay_retail_recipe.cpp", recipe_object),
                (harness, harness_object),
            ):
                subprocess.run(
                    [compiler, *flags, "-c", str(source), "-o", str(output)],
                    check=True, capture_output=True, text=True, timeout=30,
                )
            linker_gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
            subprocess.run(
                [compiler, linker_gc, str(recipe_object), str(harness_object),
                 "-o", str(executable)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            result = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True,
                timeout=10,
            )

        baseline_output = result.stdout.split("IDENTITY_CASE_BEGIN", 1)[0]
        records = [json.loads(line) for line in baseline_output.splitlines()
                   if line.startswith("{")]
        self.assertEqual(records[0]["record"], "header")
        self.assertEqual(records[0]["schema"], "melee-web-port-session-diagnostic")
        self.assertEqual(records[0]["version"], 1)
        self.assertNotIn("fighter_entities", records[0])
        self.assertEqual(records[0]["cpu_observations"], "not_captured")
        self.assertEqual(
            [record["record"] for record in records[1:10]],
            ["session_frame", "session_frame", "session_match_enter_complete",
             "session_frame", "session_frame", "session_frame",
             "session_match_enter_complete", "session_frame", "end"],
        )
        counts = {}
        for line in result.stdout.splitlines():
            if line.startswith("COUNTS "):
                _, label, *values = line.split()
                counts[label] = tuple(map(int, values))

        # session_initial: no state or CPU observer is reachable before CSS.
        self.assertEqual(counts["session_initial"], (0, 0, 0, 0, 0, 0, 0))
        # Two matches emit state, while the v8 regression stays CPU-observer free.
        self.assertEqual(counts["whole"], (4, 4, 0, 0, 0, 0, 0))
        self.assertEqual(counts["restart"], (2, 0, 0, 0, 0, 0, 0))
        self.assertEqual(counts["disabled"], (2, 0, 0, 0, 0, 0, 0))
        self.assertEqual([r["cpu_observations"] for r in records
                          if r.get("schema") == "melee-web-port-session-diagnostic"],
                         ["not_captured", "not_captured", "not_captured"])
        # legacy: preserve the existing initial/frame/draw/preparation/end calls.
        self.assertEqual(counts["legacy"], (2, 0, 1, 1, 1, 1, 1))
        format_rows = {line for line in result.stdout.splitlines()
                       if line.startswith("FORMAT ")}
        self.assertEqual(format_rows, {
            "FORMAT valid_v10_count_span_profile",
            "FORMAT v10_setup_count rejected",
            "FORMAT v10_truncated_setup_table rejected",
            "FORMAT v10_wrong_roster rejected",
            "FORMAT v10_wrong_rules rejected",
            "FORMAT v10_wrong_match_span_count rejected",
            "FORMAT preserved_v9",
            "FORMAT preserved_v8",
        })

        identity_counts = {}
        identity_records = {}
        current_case = None
        for line in result.stdout.splitlines():
            if line.startswith("IDENTITY_CASE_BEGIN "):
                current_case = line.split()[1]
                identity_records[current_case] = []
            elif line.startswith("ENTITY_COUNTS "):
                _, label, *values = line.split()
                identity_counts[label] = tuple(map(int, values))
            elif current_case and line.startswith("{"):
                identity_records[current_case].append(json.loads(line))
        self.assertEqual(identity_counts["v8"], (0, 0))
        self.assertEqual(identity_counts["v9"], (1, 2, 0, 0))
        self.assertEqual(identity_counts["v10"], (1, 2, 0, 0))
        for version in ("v9", "v10"):
            rows = identity_records[version]
            self.assertEqual(rows[0]["record"], "header")
            self.assertNotIn("fighter_entities", rows[0])
            setup, tick = rows[1:]
            self.assertEqual(setup["record"], "session_match_enter_complete")
            self.assertEqual(tick["record"], "session_frame")
            self.assertEqual(tick["scene"], 3)
            self.assertEqual(tick["index"], 1)
            for record in (setup, tick):
                entities = record["fighter_entities"]
                self.assertEqual(
                    [(e["match_index"], e["slot"], e["entity_index"],
                      e["generation"], e["fighter_player_id"], e["fighter_gobj_linked"])
                     for e in entities],
                    [(0, slot, 0, 0, slot, True) for slot in range(4)],
                )
