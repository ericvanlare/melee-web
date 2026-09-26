"""Execute focused source-runtime traces using the production CMake closure."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayBootstrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        cls.node = sdk / "node/24.19.0_64bit/bin/node"
        cls.cmake = ROOT / ".venv/bin/cmake"
        if not cls.cmake.is_file():
            cls.cmake = Path(shutil.which("cmake") or "")
        if not (compiler / "emcc.py").is_file() or not config.is_file() or not cls.node.is_file() or not cls.cmake.is_file():
            raise unittest.SkipTest("Project SDK and CMake dependencies unavailable; run bootstrap/build")
        cls.env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                       EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        for target in ("gameplay_scheduler_trace", "gameplay_fighter_input_trace"):
            result = subprocess.run([str(cls.cmake), "--build", str(ROOT / "build/browser"),
                                     "--target", target, "-j8"], cwd=ROOT,
                                    env=cls.env, capture_output=True, text=True, timeout=300)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)

    def test_original_object_world_and_process_order(self):
        self.run_trace("bootstrap")

    def test_original_fighter_input_consumer_and_lifetime(self):
        self.run_trace("fighter")

    def test_generation_accessor_rejects_replaced_heap(self):
        self.run_trace("bootstrap_replaced")

    def test_session_arena_retains_original_payload_between_worlds(self):
        self.run_trace("retained_session")

    def run_trace(self, kind):
        executable = "gameplay_fighter_input_trace" if kind == "fighter" else "gameplay_scheduler_trace"
        arguments = []
        expected = "Original Fighter input consumer and lifetime trace: passed" if kind == "fighter" else None
        if kind == "bootstrap_replaced":
            arguments = ["replaced_heap"]
            expected = "Gameplay generation accessor replacement guard: passed"
        elif kind == "retained_session":
            arguments = ["retained_session"]
            expected = "Original gameplay session arena retention trace: passed"
        elif kind == "bootstrap":
            expected = "Original HSD gameplay-bootstrap scheduler trace: passed"
        asset = ROOT / "assets-local/next-gate/PlCo.dat"
        if kind == "fighter" and asset.is_file():
            arguments = [str(asset)]
        result = subprocess.run([str(self.node), str(ROOT / "build/browser" / f"{executable}.js"),
                                 *arguments], cwd=ROOT, env=self.env,
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(expected, result.stdout)
        if kind == "fighter" and asset.is_file():
            self.assertIn("Local PlCo typed root0 consumed by original walk predicate: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
