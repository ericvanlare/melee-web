"""Exercise shared player lifecycle without claiming native gameplay evidence."""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime

class SharedRuntimeOwnerTests(unittest.TestCase):
    def test_native_command_audio_and_teardown_boundaries(self):
        self.run_owner([])

    def test_missing_webgpu_adapter_stops_before_audio_and_native_module_load(self):
        self.run_owner(['--no-webgpu-adapter'],
                       'missing WebGPU adapter stops before native download or audio setup')

    def test_concurrent_adapter_requests_reserve_one_document_owner(self):
        self.run_owner(['--adapter-race'],
                       'deferred concurrent adapter requests reserve one document owner')

    def test_null_and_rejected_adapter_preflight_allow_safe_retry(self):
        self.run_owner(['--adapter-retry'],
                       'null and rejected adapter preflight releases only its reservation and allows retry')

    def test_timed_out_adapter_result_cannot_start_after_safe_retry(self):
        self.run_owner(['--adapter-timeout-late'],
                       'timed-out adapter attempts release safely; late adapter results cannot start a runtime')

    def test_startup_deadline_includes_adapter_preflight(self):
        self.run_owner(['--adapter-deadline-span'],
                       'one bounded startup deadline covers adapter preflight and native startup')

    def test_hidden_and_page_lifecycle_handoff_is_consumed_once(self):
        self.run_owner(['--lifecycle-handoff'],
                       'hidden/page lifecycle handoff neutralizes once before current activity')

    def test_native_tick_consumes_lifecycle_handoff_before_clock_work(self):
        source = (ROOT / "src" / "gameplay_menu_browser.cpp").read_text()
        tick = source.index("void tick(){")
        cache_call = source.index("service_render_cache_writes();", tick)
        prefix = source[tick:cache_call + len("service_render_cache_writes();")]
        handoff = "EM_ASM_INT({return window.menuServiceCommands?.() === 1 ? 1 : 0;})"
        self.assertEqual(prefix.count(handoff), 1,
                         "the fixture must exercise the production lifecycle boundary")
        self.assertIn("menu_clock.reset();audio_clock.reset();", prefix)
        self.assertTrue(prefix.endswith("service_render_cache_writes();"))
        prefix = prefix.replace(handoff, "SIMULATED_HANDOFF()")

        compiler_name = os.environ.get("CXX", "clang++")
        compiler_words = shlex.split(compiler_name)
        compiler = shutil.which(compiler_words[0]) if compiler_words else None
        if compiler is None:
            compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("no C++ compiler is available for the native clock fixture")
        fixture = f'''#include "animation_clock.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using melee_web::FixedTickClock;

FixedTickClock menu_clock;
FixedTickClock audio_clock{{FixedTickClock::OverrunPolicy::CatchUp}};
bool running = true;
bool visible = true;
int pending_handoff = 0;
unsigned source_frames = 0;
unsigned audio_ticks = 0;
unsigned cache_services = 0;

int simulated_consumable_handoff() noexcept {{
    const int result = pending_handoff;
    pending_handoff = 0;
    return result;
}}
#define SIMULATED_HANDOFF() simulated_consumable_handoff()
void service_render_cache_writes() {{ ++cache_services; }}

{prefix}
}}

#undef SIMULATED_HANDOFF
struct Pair {{ FixedTickClock::Tick menu; FixedTickClock::Tick audio; }};
Pair advance(double now_ms) {{
    const auto menu = menu_clock.tick(now_ms, running && visible);
    const auto audio = audio_clock.tick(now_ms, running && visible);
    source_frames += menu.steps;
    audio_ticks += audio.steps;
    return {{menu, audio}};
}}
void assert_not_stalled(const Pair& pair) {{
    assert(!pair.menu.stalled);
    assert(!pair.audio.stalled);
}}

int main() {{
    // Establish the paired source/audio cadence and verify the ordinary
    // source counters before exercising the lifecycle boundary.
    assert_not_stalled(advance(0.0));
    const Pair ordinary = advance(16.6666667);
    assert(ordinary.menu.steps == 1 && ordinary.audio.steps == 1);
    assert(source_frames == 1 && audio_ticks == 1);

    // A 350 ms hidden interval is consumed exactly once. Both clocks start
    // the first reactivation callback at zero debt and resume at cadence on
    // the following 16.67 ms callback.
    pending_handoff = 1;
    visible = false;
    tick();
    assert(pending_handoff == 0 && cache_services == 1);
    visible = true;
    const Pair reactivated = advance(366.6666667);
    assert(reactivated.menu.steps == 0 && reactivated.audio.steps == 0);
    const Pair resumed = advance(383.3333334);
    assert(resumed.menu.steps == 1 && resumed.audio.steps == 1);
    assert(source_frames == 2 && audio_ticks == 2);

    // A manual pause remains an external state; lifecycle servicing does not
    // rewrite it or manufacture a resume.
    running = false;
    pending_handoff = 1;
    tick();
    assert(!running && pending_handoff == 0);
    assert_not_stalled(advance(1000.0));

    // Without a handoff, a foreground 350 ms simulation gap still trips the
    // production eight-step guard.
    running = true;
    visible = true;
    menu_clock.reset();
    const Pair foreground_simulation = advance(0.0);
    assert_not_stalled(foreground_simulation);
    tick(); // No suspension event at the real native command boundary.
    const Pair simulation_stall = advance(350.0);
    assert(simulation_stall.menu.stalled);
    assert(simulation_stall.menu.reason == FixedTickClock::StallReason::Debt);
    assert(simulation_stall.menu.threshold == 8);

    // The audio catch-up clock keeps its independent 60-step guard.
    audio_clock.reset();
    assert(!audio_clock.tick(0.0, true).stalled);
    tick();
    const auto audio_stall = audio_clock.tick(1100.0, true);
    assert(audio_stall.stalled);
    assert(audio_stall.reason == FixedTickClock::StallReason::Debt);
    assert(audio_stall.threshold == 60);

    // A second service call consumes no event and cannot forgive the next
    // foreground simulation stall by resetting the clock again.
    menu_clock.reset();
    assert(!menu_clock.tick(0.0, true).stalled);
    pending_handoff = 1;
    tick();
    assert(pending_handoff == 0);
    assert(simulated_consumable_handoff() == 0);
    assert(!menu_clock.tick(350.0, true).stalled);
    tick(); // A second boundary cannot consume the same lifecycle event.
    const auto after_double_service = menu_clock.tick(700.0, true);
    assert(after_double_service.stalled);
    assert(after_double_service.threshold == 8);
    std::cout << "native lifecycle handoff fixture passed\\n";
}}
'''
        with tempfile.TemporaryDirectory(prefix="melee-runtime-clock-") as directory:
            directory = Path(directory)
            fixture_path = directory / "lifecycle_clock_fixture.cpp"
            binary_path = directory / "lifecycle_clock_fixture"
            fixture_path.write_text(fixture)
            command = [compiler, *compiler_words[1:], "-std=c++17", "-Wall", "-Wextra",
                       "-Werror", "-I", str(ROOT / "src"), str(fixture_path), "-o",
                       str(binary_path)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(binary_path)], capture_output=True, text=True,
                                      timeout=10)
            self.assertEqual(executed.returncode, 0,
                             executed.stdout + executed.stderr)
            self.assertIn("native lifecycle handoff fixture passed", executed.stdout)

    def test_explicit_silent_owner_never_opens_audio(self):
        self.run_owner(['--silent'])

    def test_known_host_diagnostics_wiring_is_inactive_and_opt_out_safe(self):
        self.run_owner(['--diagnostics-known-host'],
                       'known-host diagnostics identity, scalar incident wiring, inactive delivery delay/cancel')

    def test_known_host_delivery_waits_for_native_lifecycle_handoff(self):
        self.run_owner(['--diagnostics-known-host', '--lifecycle-handoff'],
                       'known-host diagnostics identity, scalar incident wiring, inactive delivery delay/cancel')

    def test_unavailable_persistence_keeps_required_directory(self):
        self.run_owner(['--cache-unavailable'])

    def test_startup_cache_readiness_gates_import_until_first_native_ready_result(self):
        self.run_owner(['--silent', '--startup-cache-delay'])

    def test_optional_native_cache_error_keeps_import_eligible(self):
        self.run_owner(['--silent', '--startup-cache-error'])

    def test_permanently_pending_startup_cache_hits_explicit_readiness_deadline(self):
        self.run_owner(['--silent', '--startup-cache-timeout'],
                       'pending startup cache work fails at the bounded readiness deadline')

    def test_missing_required_cache_readiness_service_fails_explicitly(self):
        self.run_owner(['--silent', '--missing-cache-service'],
                       'missing required cache readiness service fails explicitly')

    def test_invalid_cache_readiness_state_fails_explicitly(self):
        self.run_owner(['--silent', '--invalid-cache-service'],
                       'invalid cache readiness state fails explicitly')

    def test_required_directory_failure_stops_native_startup(self):
        self.run_owner(['--silent', '--mkdir-failure'], 'required directory failure prevents native initialization')

    def run_owner(self, args, expected='repeat launch and reload-only destruction pass'):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/melee_runtime_owner_test.mjs'), *args],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(expected, result.stdout)
