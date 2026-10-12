"""Check the browser input adapter contract, not physical-device behavior."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BrowserInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        aurora = ROOT / ".deps/aurora/include"
        sdl = ROOT / "build/browser/_deps/sdl-src/include"
        if not (sdl / "SDL3/SDL_keyboard.h").is_file():
            sdl = ROOT / "build/browser-release/_deps/sdl-src/include"
        if not (aurora / "dolphin/pad.h").is_file() or not (sdl / "SDL3/SDL_keyboard.h").is_file():
            raise unittest.SkipTest("Bootstrap and configure the browser build to obtain pinned PAD/SDL headers")
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for input adapter tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="melee input tests ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "browser_input_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror", "-O1",
             "-I", str(ROOT / "src"), "-I", str(aurora), "-I", str(sdl),
             str(ROOT / "src/browser_input.cpp"), str(ROOT / "tests/browser_input_test.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        diagnostic = json.loads(result.stdout)
        self.assertEqual(len(diagnostic["pads"]), 4)
        self.assertIn(diagnostic["active"], (0, 1))
        return diagnostic

    def test_boxx_mapping_and_layout_switch_clear_held_keys(self):
        self.run_case("boxx_layout")

    def test_boxx_focus_physical_priority_and_keyboard_disable(self):
        self.run_case("boxx_focus_and_sources")

    def test_boxx_event_watch_startup_failure_is_explicit(self):
        self.run_case("boxx_startup_failure")

    def test_documented_keyboard_button_and_axis_mapping(self):
        result = self.run_case("binding_mapping")
        self.assertEqual(result["pads"][0]["source"], "keyboard")

    def test_keyboard_default_and_prestartup_frontend_preference(self):
        self.run_case("keyboard_preference")

    def test_explicit_p2_profile_and_p1_restore(self):
        result = self.run_case("keyboard_port_two_profile")
        self.assertEqual(result["keyboard_requested_mask"], 1)

    def test_physical_p2_precedes_keyboard_only_on_port_two(self):
        result = self.run_case("per_port_physical_priority")
        self.assertEqual(result["keyboard_active_mask"], 3)
        self.assertEqual(result["physical_mask"], 0)

    def test_raw_sampling_and_separate_provider_clamp(self):
        result = self.run_case("raw_and_clamped")
        self.assertEqual(result["physical_mask"], 1)
        self.assertEqual(result["pads"][0]["stick"], [127, 0])

    def test_focus_and_visibility_neutralize_without_waiting_for_frame(self):
        self.run_case("focus_and_visibility")

    def test_repeat_checkpoint_uses_actual_producer_schema_and_current_sample_counter(self):
        import sys
        sys.path.insert(0, str(ROOT / "scripts"))
        from run_network_simulation import validate_repeat_checkpoint_input
        from check_gameplay import node_runtime
        result = self.run_case("repeat_checkpoint_neutral")
        self.assertNotIn("port", result)
        self.assertNotIn("sample", result)
        self.assertEqual(result["samples"], 2)
        self.assertEqual(result["last_non_neutral"]["sample"], 1)
        for port in (0, 1):
            validate_repeat_checkpoint_input(result, port)
        source = (ROOT / "scripts/a3_local_room_keyboard_browser.mjs").read_text()
        helper = source[source.index("function validateRepeatCheckpointInput("):
                        source.index("async function readInputDiagnostics(")]
        collector = source[source.index("    const collectCheckpoint = async label => {"):
                           source.index("    const samePageObjects =", source.index("    const collectCheckpoint = async label => {"))]
        script = "const input = " + json.dumps(result) + ";\n" + """
const assert = require('node:assert/strict');
function requireCondition(condition, message) { if (!condition) throw Error(message); }
""" + helper + """
for (const port of [0, 1]) validateRepeatCheckpointInput(input, port, 'native fixture');
for (const corrupt of [
  row => { delete row.samples; row.sample = 2; row.port = 0; },
  row => { row.samples = true; }, row => { row.keyboard_layout = 1; },
  row => { row.keyboard_active_mask = 0; }, row => { row.focused = 0; },
  row => { row.pads[0].source = 'gamepad'; }, row => { row.pads[0].buttons = 1; },
  row => { row.pads[0].stick = [1, 0]; }, row => { row.pads[0].clamped.triggers = [0, 1]; },
  row => { row.error = 'input error'; }, row => { row.pads.pop(); },
]) {
  const damaged = structuredClone(input); corrupt(damaged);
  assert.throws(() => validateRepeatCheckpointInput(damaged, 0, 'negative native fixture'));
}
"""
        script += """
const instances = {alpha: {role:'alpha'}, beta: {role:'beta'}};
const initialPages = instances, repeat = {checkpoint_observations: []}, roomIdentity = {};
let nextInput = input;
const readInputDiagnostics = async () => structuredClone(nextInput);
const protocol = instance => ({role:instance.role,local_port:instance.role==='alpha'?0:1,
  local_checksum_ticks:2,remote_checksum_ticks:2,next_checksum_compare:2});
const readRoomState = async instance => ({native_status:{cursor:2},
  owner_health:{peer:{protocol:protocol(instance)}}});
const readPeerSnapshot = async instance => ({health:{peer:{protocol:protocol(instance)}},
  snapshot:{checksums:[[],[]]}});
const assertHealthyState = () => {};
""" + collector + """
(async () => {
  await collectCheckpoint('positive native producer');
  assert.equal(repeat.checkpoint_observations.length,1);
  assert.deepEqual(repeat.checkpoint_observations[0].input_observations.alpha,input);
  nextInput=structuredClone(input);nextInput.pads[0].buttons=1;
  await assert.rejects(collectCheckpoint('negative native producer'));
  const failed=repeat.checkpoint_observations[1];
  assert.equal(failed.result,'observed_pending_validation');
  assert.deepEqual(failed.input_observations.alpha,nextInput);
  assert.deepEqual(failed.input_observations.beta,nextInput);
})().catch(error=>{console.error(error);process.exitCode=1;});
"""
        command = [node_runtime(ROOT), "-e", script]
        checked = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_last_sample_history_survives_release_but_current_input_resets(self):
        result = self.run_case("last_non_neutral")
        self.assertEqual(result["samples"], 3)
        self.assertEqual(result["pads"][0]["buttons"], 0)
        self.assertEqual(result["pads"][0]["stick"], [0, 0])
        self.assertEqual(result["last_non_neutral"]["sample"], 1)
        self.assertEqual(result["last_non_neutral"]["port"], 0)
        self.assertEqual(result["last_non_neutral"]["source"], "keyboard")
        self.assertEqual(result["last_non_neutral"]["stick"], [127, 0])
        self.assertEqual(result["last_non_neutral"]["clamped"]["stick"], [41, 0])

    def test_disconnect_provider_errors_and_rumble_mask_distinction(self):
        self.run_case("disconnect_and_errors")

    def test_keyboard_fallback_does_not_mix_with_physical_controller(self):
        self.run_case("keyboard_source_changes")

    def test_setup_failure_and_shutdown_are_explicit(self):
        self.run_case("startup_failure_and_shutdown")


if __name__ == "__main__":
    unittest.main()
