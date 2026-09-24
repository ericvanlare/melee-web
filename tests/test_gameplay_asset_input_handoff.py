"""Compile the production scoped Match/Replay asset handoff tail.

The browser owns the menu PAD history while its scoped asset boundary is
refilled. This small harness extracts the actual production branch and runs it
with stand-ins, so a future no-input constructor call cannot silently reset the
source history again.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _handoff_tail() -> str:
    source = (ROOT / "src" / "gameplay_menu_browser.cpp").read_text(encoding="utf-8")
    function_start = source.index("bool finish_asset_handoff(){")
    tail_start = source.index(
        " check(destination==AssetDestination::Match||destination==AssetDestination::Replay,",
        function_start,
    )
    depth = 0
    function_end = None
    for index in range(function_start, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                function_end = index + 1
                break
    if function_end is None:
        raise AssertionError("finish_asset_handoff has no closing brace")
    return source[tail_start:function_end]


_HARNESS_PREFIX = r'''
#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>

struct RuntimeFiles {};
struct MeleeWebMenuMatchSelection {};
struct MeleeWebPadState { int identity; };
struct MeleeWebMenuHost { const MeleeWebPadState* input; };

enum class AssetDestination { None, Match, Replay };
struct ReplayRecipe { const MeleeWebPadState* initial_input; };

namespace melee_web {
struct RuntimeArchiveCache {
    explicit RuntimeArchiveCache(const RuntimeFiles&) {}
};
enum class GameplayMatchConstruction { Deferred };
class GameplayMatchSession {
public:
    GameplayMatchSession(const RuntimeFiles&, const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&, GameplayMatchConstruction) {
        observed_input = nullptr;
        ++construction_count;
    }
    GameplayMatchSession(const RuntimeFiles&, const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&, GameplayMatchConstruction,
                         const MeleeWebPadState& input) {
        observed_input = &input;
        ++construction_count;
    }
    static const MeleeWebPadState* observed_input;
    static unsigned construction_count;
};
const MeleeWebPadState* GameplayMatchSession::observed_input = nullptr;
unsigned GameplayMatchSession::construction_count = 0;
}

AssetDestination asset_destination = AssetDestination::None;
AssetDestination destination = AssetDestination::None;
bool asset_committed = false;
RuntimeFiles files;
MeleeWebMenuMatchSelection asset_selection;
MeleeWebMenuHost* host = nullptr;
ReplayRecipe* replay = nullptr;
std::unique_ptr<melee_web::RuntimeArchiveCache> archive_cache;
std::unique_ptr<melee_web::GameplayMatchSession> match;
bool running = false;

void check(int value, const char* error) {
    if (!value) throw std::runtime_error(error);
}
const MeleeWebPadState* melee_web_menu_host_input(const MeleeWebMenuHost* value) {
    return value == nullptr ? nullptr : value->input;
}

'''


class GameplayAssetInputHandoffTests(unittest.TestCase):
    def test_scoped_match_and_replay_preserve_their_owned_pad_histories(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("a C++ compiler is required")
        tail = _handoff_tail()
        harness = textwrap.dedent(_HARNESS_PREFIX) + """
bool production_handoff_tail() {
""" + tail + """

int main() {
    MeleeWebPadState host_state{11};
    MeleeWebPadState replay_state{22};
    MeleeWebMenuHost menu_host{&host_state};
    ReplayRecipe recipe{&replay_state};
    host = &menu_host;
    replay = &recipe;

    destination = AssetDestination::Match;
    archive_cache = std::make_unique<melee_web::RuntimeArchiveCache>(files);
    assert(!production_handoff_tail());
    assert(melee_web::GameplayMatchSession::observed_input == &host_state);
    assert(melee_web::GameplayMatchSession::construction_count == 1);
    match.reset();
    archive_cache.reset();

    menu_host.input = nullptr;
    destination = AssetDestination::Replay;
    archive_cache = std::make_unique<melee_web::RuntimeArchiveCache>(files);
    assert(!production_handoff_tail());
    assert(melee_web::GameplayMatchSession::observed_input == &replay_state);
    assert(melee_web::GameplayMatchSession::construction_count == 2);
    match.reset();
    archive_cache.reset();

    destination = AssetDestination::Match;
    archive_cache = std::make_unique<melee_web::RuntimeArchiveCache>(files);
    const unsigned before = melee_web::GameplayMatchSession::construction_count;
    bool rejected = false;
    try {
        (void)production_handoff_tail();
    } catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("PAD history") != std::string::npos;
    }
    assert(rejected);
    assert(melee_web::GameplayMatchSession::construction_count == before);
    assert(!match);
    return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="gameplay-asset-input-") as directory:
            root = Path(directory)
            source = root / "handoff.cpp"
            binary = root / "handoff"
            source.write_text(harness, encoding="utf-8")
            subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                 str(source), "-o", str(binary)],
                check=True, capture_output=True, text=True, timeout=30,
            )
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
