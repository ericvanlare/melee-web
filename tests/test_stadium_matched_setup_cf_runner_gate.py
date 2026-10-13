"""Run the asset-free matched Setup C/F runner admission controls."""
import json
import os
from pathlib import Path
import subprocess
import sys

SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT", SOURCE_ROOT)).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
from check_gameplay import node_runtime


class StadiumMatchedSetupCfRunnerGateTests(OwnedWorkspaceTests):
    def test_opt_in_and_packet_preflight_refuse_before_server(self):
        scratch = self.new_workspace(ROOT, "stadium-matched-setup-cf-runner-")
        environment = os.environ.copy()
        environment["MELEE_STADIUM_CF_RUNNER_TEST_ROOT"] = str(scratch)
        environment["MELEE_STADIUM_CF_RUNNER_TEST_REPO_ROOT"] = str(ROOT)
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(SOURCE_ROOT / "tests/stadium_matched_setup_cf_runner_gate.test.mjs")],
            cwd=ROOT, env=environment, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generated_yielded_retirement_blocks_native_access_until_idle(self):
        emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        if not emcc.is_file():
            self.skipTest("pinned Emscripten is unavailable for the yielding retirement fixture")
        version_file = emcc.parent / "emscripten-version.txt"
        if not version_file.is_file():
            self.skipTest("pinned Emscripten version receipt is unavailable")
        expected = json.loads((ROOT / "dependencies.lock.json").read_text())["emscripten"]
        self.assertEqual(version_file.read_text().strip().strip('"'), expected,
                         "the fixture must use the repository-pinned Emscripten")
        scratch = self.new_workspace(ROOT, "stadium-explicit-yield-retirement-")
        temporary = scratch / "tmp"
        temporary.mkdir()
        environment = os.environ.copy()
        environment["TMPDIR"] = str(temporary)
        result = subprocess.run(
            [str(node_runtime(ROOT)),
             str(SOURCE_ROOT / "tests/stadium_explicit_asyncify_yield_control.mjs"),
             str(emcc), str(scratch / "generated")],
            cwd=ROOT, env=environment, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        receipt = json.loads(result.stdout.strip().splitlines()[-1])
        self.assertEqual(receipt["result"], 1)
        self.assertIn(expected, receipt["emcc_version"])
        self.assertFalse(receipt["cleanup_allowed_while_pending"])
        self.assertEqual(receipt["native_getter_calls_while_pending"], 0)
        self.assertEqual(receipt["unload_calls_while_pending"], 0)
        self.assertEqual(receipt["native_getter_calls_after_settlement"], 1)

    def test_host_plan_and_state_comparator_controls(self):
        host_test = Path(os.environ.get(
            "MELEE_STADIUM_CF_HOST_TEST_SCRIPT",
            ROOT / "tests/test_stadium_matched_setup_cf_consumer.mjs",
        )).resolve()
        self.assertTrue(host_test.is_file(), f"host contract test is missing: {host_test}")
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(host_test)],
            cwd=ROOT, capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    import unittest
    unittest.main()
