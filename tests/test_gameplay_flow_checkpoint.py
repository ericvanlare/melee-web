"""Actual wasm32 owner implementation; synthetic lifecycle, no match acceptance."""
from pathlib import Path
import os
import subprocess
import sys
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class FlowCheckpointOwners(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        if not cls.emcc.is_file():
            raise unittest.SkipTest("Pinned Emscripten toolchain unavailable")
        cls.work = cls.new_workspace(ROOT, "flow-checkpoint-owner-")

    def test_actual_callback_guards_and_owner_preservation(self):
        binary = self.work / "owner-control.js"
        clock_source = Path(os.environ.get(
            "MELEE_WEB_TEST_CLOCK_SOURCE",
            ROOT / "build/gameplay-source/src/melee/gm/gm_1A45.c")).resolve()
        if not clock_source.is_file():
            self.skipTest("Generated original gm_1A45 source is unavailable")
        command = [str(self.emcc), "-std=c11", "-O1", "-UNDEBUG", "-DTARGET_PC",
                   "-ffunction-sections", "-fdata-sections",
                   "-Wl,--gc-sections",
                   "-include", "src/gameplay_compat.h", "-Isrc", "-Ibuild/gameplay-source/src",
                   "-Ibuild/gameplay-source/src/melee/gm", "-I.deps/melee/src",
                   "-I.deps/aurora/include", "-I.deps/melee/extern/dolphin/include",
                   f'-DMELEE_WEB_TEST_CLOCK_SOURCE="{clock_source.as_posix()}"',
                   "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                   "tests/flow_checkpoint_owner_control.c", "-o", str(binary)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        (self.work / "compile.log").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(node_runtime()), str(binary)], cwd=ROOT,
                                capture_output=True, text=True, timeout=30)
        (self.work / "run.log").write_text(result.stdout + result.stderr)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Actual source-clock and flow-owner controls passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
