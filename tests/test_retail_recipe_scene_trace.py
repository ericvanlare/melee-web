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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

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
bool state_allowed = false;
bool cpu_available = true;
void reset() {
    state = rng = cpu_begin = cpu_tick = cpu_draw =
        cpu_preparation_draw = cpu_end = 0;
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

extern "C" int melee_web_retail_setup(const uint8_t*, uint32_t,
    MeleeWebMenuMatchSelection* out, char*, size_t) {
    out->player_count = 2;
    return 1;
}
extern "C" void melee_web_retail_state(void) { fake::write_state(); }
extern "C" uint32_t melee_web_retail_rng(void) { ++fake::rng; return 123; }
extern "C" uint32_t gm_GetFrameCount(void) { return 0; }
extern "C" uint32_t gm_8016AEEC(void) { return 0; }
extern "C" uint16_t gm_8016AEFC(void) { return 0; }
extern "C" int melee_web_match_source_result(void) { return 0; }
extern "C" int melee_web_match_end_state(void) { return 0; }
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

static void print_counts(const char* label) {
    std::cout << "COUNTS " << label << " " << fake::state << ' ' << fake::rng
              << ' ' << fake::cpu_begin << ' ' << fake::cpu_tick << ' '
              << fake::cpu_draw << ' ' << fake::cpu_preparation_draw << ' '
              << fake::cpu_end << "\n";
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
    // Each match has a state snapshot; only the first owns CPU diagnostics.
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

        records = [json.loads(line) for line in result.stdout.splitlines()
                   if line.startswith("{")]
        self.assertEqual(records[0]["record"], "header")
        self.assertEqual(records[0]["schema"], "melee-web-port-session-diagnostic")
        self.assertEqual(records[0]["cpu_observations"], "first_match_only")
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
        # Two matches emit state, but CPU observations end once at first Results.
        self.assertEqual(counts["whole"], (4, 4, 1, 1, 0, 0, 1))
        self.assertEqual(counts["restart"], (2, 0, 1, 1, 0, 0, 1))
        self.assertEqual(counts["disabled"], (2, 0, 0, 0, 0, 0, 0))
        self.assertEqual([r["cpu_observations"] for r in records
                          if r.get("schema") == "melee-web-port-session-diagnostic"],
                         ["first_match_only", "first_match_only", "not_captured"])
        # legacy: preserve the existing initial/frame/draw/preparation/end calls.
        self.assertEqual(counts["legacy"], (2, 0, 1, 1, 1, 1, 1))
