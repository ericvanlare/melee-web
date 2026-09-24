"""Compile the production Results return branch against owned scene seams.

The whole-session cursor may end on a Results sample only when the original
Results owner requests CSS.  A Prize request at that same exhausted cursor is
an invalid timeline and must be rejected before constructing an unrecorded
Prize scene.  This harness extracts the production branch and records the
owner and PAD handoff calls with small source-shaped stand-ins.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _results_branch() -> str:
    source = (ROOT / "src" / "gameplay_menu_browser.cpp").read_text(encoding="utf-8")
    advance_start = source.index("void advance(){")
    start = source.index("\n if(results){", advance_start) + 1
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError("advance Results branch has no closing brace")


_HARNESS_PREFIX = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

struct RuntimeFiles {};
struct ResultsMatchInfo {};
struct AuroraStats {};
struct MeleeWebMenuHost {
    int destination;
    const struct MeleeWebPadState* input;
};
struct MeleeWebPadState { int identity; };
enum { MELEE_WEB_PAD_STATE_BYTES = 4 };
enum class AssetDestination { None, InitialMenu, Match, ReturnMenu, Replay, Results, Prize };

struct ReplayCompletionState {
    bool final_input_drawn = false;
    bool final_results_or_prize_transitioned = false;
};

namespace melee_web {
class GameplayResultsSession {
public:
    void exit_scene() { ++exit_calls; }
    std::uint32_t random_seed() const { return 17; }
    void close() { ++close_calls; }
    static unsigned exit_calls;
    static unsigned close_calls;
};
unsigned GameplayResultsSession::exit_calls = 0;
unsigned GameplayResultsSession::close_calls = 0;

class GameplayPrizeSession {
public:
    GameplayPrizeSession(const RuntimeFiles&, MeleeWebMenuHost*, std::uint32_t,
                         const MeleeWebPadState& input)
    { observed_input = &input; ++construction_count; }
    static const MeleeWebPadState* observed_input;
    static unsigned construction_count;
};
const MeleeWebPadState* GameplayPrizeSession::observed_input = nullptr;
unsigned GameplayPrizeSession::construction_count = 0;
}

void melee_web_pad_state_free(MeleeWebPadState* input) { delete input; }
std::unique_ptr<MeleeWebPadState, decltype(&melee_web_pad_state_free)>
    results_input{nullptr, melee_web_pad_state_free};
MeleeWebPadState* melee_web_pad_state_decode(const std::uint8_t* bytes,
                                             std::size_t, char*, std::size_t)
{ return new MeleeWebPadState{bytes[0]}; }
void melee_web_pad_state_capture(std::uint8_t* bytes) { bytes[0] = 9; }

RuntimeFiles files;
MeleeWebMenuHost* host = nullptr;
std::unique_ptr<melee_web::GameplayResultsSession> results;
std::unique_ptr<melee_web::GameplayPrizeSession> prize;
ResultsMatchInfo results_info;
ReplayCompletionState replay_completion;
AssetDestination asset_destination = AssetDestination::None;
bool scoped_assets = false;
bool results_route_active = true;
bool prize_route_active = false;
std::uint32_t results_seed = 0;
std::uint32_t prize_seed = 0;
bool first_use_draw_pending = false;
bool pending = true;
bool running = false;
unsigned audio_phase = 0;
std::string message;
struct Clock { void reset() {} } menu_clock, audio_clock;
unsigned enter_world_calls = 0;
unsigned results_exit_calls = 0;
unsigned results_end_calls = 0;
unsigned asset_request_calls = 0;

void check(int value, const char* error)
{ if (!value) throw std::runtime_error(error); }
void report_owner_lifetime(const char*) {}
void report_construction(const char*, double, double, double,
                         const AuroraStats&, const AuroraStats&) {}
AuroraStats aurora_stats_snapshot() { return {}; }
double emscripten_get_now() { return 0; }
void request_assets(AssetDestination destination)
{ assert(destination == AssetDestination::ReturnMenu || destination == AssetDestination::Prize); ++asset_request_calls; }
void enter_world() { ++enter_world_calls; running = true; }
int melee_web_menu_host_results_exit(MeleeWebMenuHost*, char*, std::size_t)
{ ++results_exit_calls; return 1; }
int melee_web_menu_host_results_end(MeleeWebMenuHost*, std::uint32_t,
                                    const std::uint8_t*, char*, std::size_t)
{ ++results_end_calls; return 1; }
int melee_web_menu_host_results_destination(const MeleeWebMenuHost* value)
{ return value->destination; }
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost* value)
{ return value->input; }
char error[256] = {};
'''


class GameplayResultsReturnHandoffTests(unittest.TestCase):
    def test_production_branch_accepts_css_and_rejects_unrecorded_prize(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("a C++ compiler is required")

        branch = _results_branch()
        harness = textwrap.dedent(_HARNESS_PREFIX) + """
void production_results_advance() {
""" + branch + """
}

int main() {
    MeleeWebPadState source_history{41};
    MeleeWebMenuHost menu_host{0, &source_history};
    host = &menu_host;

    // A final Results input whose source mode requests CSS must complete the
    // Results owner, retain its transition bit, and enter the live CSS world.
    results = std::make_unique<melee_web::GameplayResultsSession>();
    replay_completion = {true, false};
    production_results_advance();
    assert(!results);
    assert(!results_route_active);
    assert(replay_completion.final_results_or_prize_transitioned);
    assert(enter_world_calls == 1);
    assert(results_exit_calls == 1 && results_end_calls == 1);

    // If the original Results owner requests Prize after the final recorded
    // sample, the production guard must reject before constructing Prize.
    results = std::make_unique<melee_web::GameplayResultsSession>();
    replay_completion = {true, false};
    menu_host.destination = 192;
    const unsigned prize_before = melee_web::GameplayPrizeSession::construction_count;
    bool rejected = false;
    try {
        production_results_advance();
    } catch (const std::runtime_error& error_value) {
        rejected = std::string(error_value.what()).find("before an original Prize") != std::string::npos;
    }
    assert(rejected);
    assert(melee_web::GameplayPrizeSession::construction_count == prize_before);
    assert(!prize);
    assert(!replay_completion.final_results_or_prize_transitioned);

    // A non-exhausted Results owner may legitimately request Prize; retain the
    // host-owned PAD history through the actual production constructor path.
    results = std::make_unique<melee_web::GameplayResultsSession>();
    results_route_active = true;
    replay_completion = {false, false};
    const unsigned prize_after_rejection = melee_web::GameplayPrizeSession::construction_count;
    production_results_advance();
    assert(!results);
    assert(prize);
    assert(melee_web::GameplayPrizeSession::construction_count == prize_after_rejection + 1);
    assert(melee_web::GameplayPrizeSession::observed_input == &source_history);
    assert(!replay_completion.final_results_or_prize_transitioned);
    prize.reset();

    // Scoped asset routes defer CSS entry until ReturnMenu commits its scope.
    menu_host.destination = 0;
    results = std::make_unique<melee_web::GameplayResultsSession>();
    results_route_active = true;
    replay_completion = {true, false};
    scoped_assets = true;
    production_results_advance();
    assert(!results);
    assert(asset_request_calls == 1);
    assert(enter_world_calls == 1);
    assert(replay_completion.final_results_or_prize_transitioned);
    return 0;
}
"""

        with tempfile.TemporaryDirectory(prefix="gameplay-results-return-") as directory:
            root = Path(directory)
            source = root / "results_return.cpp"
            binary = root / "results_return"
            source.write_text(harness, encoding="utf-8")
            subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(binary)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
