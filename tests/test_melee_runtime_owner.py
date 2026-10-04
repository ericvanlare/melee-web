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
    def test_fatal_failure_delivers_sanitized_diagnostics(self):
        self.run_owner(['--diagnostics-known-host', '--diagnostics-fatal'],
                       'fatal failure delivers sanitized diagnostics while stopped')

    def test_worklet_failure_stops_owner_and_reports_one_sanitized_incident(self):
        self.run_owner(['--fatal-audio-output'], 'worklet failure reaches terminal native handoff')

    def test_worklet_processor_exception_stops_owner_without_a_port_message(self):
        self.run_owner(['--fatal-audio-processor'], 'worklet failure reaches terminal native handoff')

    def test_fatal_owner_cancels_native_at_the_safe_boundary(self):
        self.run_owner(['--fatal-native-handoff'], 'fatal handoff is sticky')

    def test_fault_inside_command_batch_rejects_remaining_commands(self):
        self.run_owner(['--fatal-command-batch'], 'fatal handoff is sticky')

    def test_native_command_audio_and_teardown_boundaries(self):
        self.run_owner([])

    def test_render_timeout_preserves_prepared_disc_for_retry(self):
        self.run_owner(['--audio-render-timeout'],
                       'renderer timeout leaves prepared disc retryable; stale ack cannot launch retry')

    def test_fatal_stop_cancels_render_wait_before_native_launch(self):
        self.run_owner(['--audio-render-fatal'],
                       'fatal stop cancels pending renderer wait before source launch')

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
        handoff = "EM_ASM_INT({return window.menuServiceCommands?.() || 0;})"
        self.assertEqual(prefix.count(handoff), 1,
                         "the fixture must exercise the production lifecycle boundary")
        self.assertIn("menu_clock.reset();audio_clock.reset();", prefix)
        self.assertTrue(prefix.endswith("service_render_cache_writes();"))
        prefix = prefix.replace(handoff, "SIMULATED_HANDOFF()")
        startup_begin = source.index("void startup_pipeline_service_callback(void*){")
        startup_end = source.index("void schedule_startup_pipeline_service(){", startup_begin)
        startup_callback = source[startup_begin:startup_end]
        terminal_query = "EM_ASM_INT({return window.menuOwnerStopped?.()?1:0;})"
        self.assertEqual(startup_callback.count(terminal_query), 1)
        startup_callback = startup_callback.replace(terminal_query, "owner_stopped")

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
bool faulted = false;
unsigned cancellations = 0;
void emscripten_cancel_main_loop() {{ ++cancellations; }}
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
bool owner_stopped = false;
bool startup_pipeline_service_scheduled = true;
bool startup_pipeline_service_failed = false;
unsigned startup_batches = 0;
struct AuroraStats {{ unsigned queuedPipelines; }};
const AuroraStats* aurora_get_stats() {{ static AuroraStats stats{{1}}; return &stats; }}
bool startup_pipeline_service_allowed() {{ return !faulted; }}
bool aurora_pipeline_service_preparation() {{ ++startup_batches; return true; }}

{startup_callback}

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
    // A delayed ownerless preparation callback must observe JS terminal state
    // before the main-loop handoff has set native faulted.
    startup_pipeline_service_callback(nullptr);
    assert(startup_batches == 1 && !startup_pipeline_service_scheduled);
    owner_stopped = true;
    startup_pipeline_service_scheduled = true;
    startup_pipeline_service_callback(nullptr);
    assert(!faulted && startup_batches == 1 && !startup_pipeline_service_scheduled);
    // The actual production prefix consumes a terminal owner failure before
    // cache/preparation/input/source work, independent of manual-pause gates.
    running = true;
    const auto services_before_fault = cache_services;
    const auto source_before_fault = source_frames;
    const auto audio_before_fault = audio_ticks;
    pending_handoff = -1;
    tick();
    assert(!running && faulted && cancellations == 1);
    assert(cache_services == services_before_fault);
    const Pair after_fault = advance(2000.0);
    assert(after_fault.menu.steps == 0 && after_fault.audio.steps == 0);
    assert(source_frames == source_before_fault && audio_ticks == audio_before_fault);
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

    def test_lifecycle_checkpoint_persists_before_deferred_task(self):
        self.run_owner(['--diagnostics-retention-checkpoint', '--silent'],
                       'pagehide/freeze checkpoint persists before destroy')

    def test_orderly_destroy_persists_after_native_unload(self):
        self.run_owner(['--diagnostics-retention-destroy', '--silent'],
                       'orderly destroy persists before disposing delivery')

    def test_lifecycle_checkpoint_denial_is_isolated(self):
        self.run_owner(['--diagnostics-retention-denied', '--silent'],
                       'denied lifecycle checkpoint remains local and isolated')

    def test_lifecycle_checkpoint_cancels_before_visible_resume(self):
        self.run_owner(['--diagnostics-retention-resume-cancel', '--silent'],
                       'lifecycle checkpoint cancels before visible gameplay')

    def test_lifecycle_checkpoint_follows_later_incident(self):
        self.run_owner(['--diagnostics-retention-followup', '--silent'],
                       'delayed lifecycle checkpoint follows a later incident')

    def test_destroy_refreshes_delayed_ordinary_persist(self):
        self.run_owner(['--diagnostics-retention-ordinary-pause-destroy', '--silent'],
                       'destroy refreshes a delayed ordinary inactive snapshot')

    def test_hidden_fatal_incident_reaches_checkpoint(self):
        self.run_owner(['--diagnostics-retention-hidden-fatal', '--silent'],
                       'hidden fatal incident is included in the local checkpoint')

    def test_mature_incident_delivery_is_not_starved_by_resume(self):
        self.run_owner(['--diagnostics-mature-delivery', '--silent'],
                       'mature inactive diagnostics are delivered before a young incident')

    def test_mature_delivery_retries_latest_inactive_generation(self):
        self.run_owner(['--diagnostics-mature-delivery-slow-load', '--silent'],
                       'mature inactive diagnostics are delivered before a young incident')

    def test_evicted_same_session_retained_incident_remains_deliverable(self):
        self.run_owner(['--diagnostics-mature-delivery-evicted', '--silent'],
                       'evicted same-session retained incident remains deliverable')

    def test_prior_empty_read_does_not_orphan_later_evicted_incident(self):
        self.run_owner(['--diagnostics-mature-delivery-prior-read', '--silent'],
                       'evicted same-session retained incident remains deliverable')

    def test_nonempty_retained_read_does_not_orphan_evicted_young_incident(self):
        self.run_owner(['--diagnostics-mature-delivery-nonempty-evicted', '--silent'],
                       'nonempty-retained evicted same-session retained incident remains deliverable')

    def test_normal_delivery_prefers_fresh_current_post_events(self):
        self.run_owner(['--diagnostics-normal-delivery-freshness', '--silent'],
                       'normal inactive collection prefers the fresh current report')

    def test_empty_checkpoint_does_not_suppress_destroy(self):
        self.run_owner(['--diagnostics-retention-empty-destroy', '--silent'],
                       'empty lifecycle checkpoint does not suppress later destroy persistence')

    def test_failed_checkpoint_does_not_suppress_destroy(self):
        self.run_owner(['--diagnostics-retention-failed-destroy', '--silent'],
                       'failed lifecycle checkpoint does not suppress fresh destroy persistence')

    def test_orderly_destroy_times_out_stalled_storage(self):
        self.run_owner(['--diagnostics-retention-stalled', '--silent'],
                       'stalled destroy checkpoint times out without trapping teardown')

    def test_orderly_destroy_reports_quota_failure(self):
        self.run_owner(['--diagnostics-retention-quota', '--silent'],
                       'quota destroy checkpoint fails explicitly without trapping teardown')

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
