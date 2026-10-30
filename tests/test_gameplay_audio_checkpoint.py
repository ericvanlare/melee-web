"""Actual wasm32 owner implementation; synthetic state, no DSP acceptance."""
from pathlib import Path
import subprocess
import sys
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class AudioCheckpointOwners(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        if not cls.emcc.is_file():
            raise unittest.SkipTest("Pinned Emscripten toolchain unavailable")
        cls.work = cls.new_workspace(ROOT, "audio-checkpoint-owner-")

    def test_actual_owner_bytes_and_atomic_refusals(self):
        self.run_control("audio_checkpoint_owner_control.c", "owner-control",
                         "Actual wasm32 audio owner checkpoint controls passed")

    def test_stream_aggregate_admission_and_failed_interval_cleanup(self):
        self.run_control("audio_stream_aggregate_control.c", "stream-aggregate-control",
                         "Actual audio aggregate / synthetic stream lifecycle controls passed")

    def run_control(self, source, name, expected):
        binary = self.work / (name + ".js")
        command = [str(self.emcc), "-std=c11", "-O1", "-UNDEBUG", "-DTARGET_PC",
                   "-ffunction-sections", "-fdata-sections",
                   "-include", "src/gameplay_compat.h", "-Isrc", "-I.deps/melee/src",
                   "-I.deps/aurora/include", "-I.deps/melee/extern/dolphin/include",
                   "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                   "tests/" + source, "-o", str(binary)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        (self.work / (name + "-compile.log")).write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(node_runtime()), str(binary)], cwd=ROOT,
                                capture_output=True, text=True, timeout=30)
        (self.work / (name + "-run.log")).write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(expected, result.stdout)


if __name__ == "__main__":
    unittest.main()
