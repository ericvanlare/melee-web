"""Original rotation instruction order is checked with an unfused C control."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources
from check_gameplay import node_runtime


class GameplayVectorRotationTests(unittest.TestCase):
    def test_camera_vectors_follow_original_fused_instruction_order(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-fno-builtin-sinf", "-fno-builtin-cosf", "-ffp-contract=off",
                  "-ffunction-sections", "-fdata-sections", "-DTARGET_PC",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_vector_rotation_trace.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        output = {}
        with tempfile.TemporaryDirectory(prefix="melee-vector-") as directory:
            for name, matrix_source in (
                ("source", source / "melee/lb/lbvector.c"),
                ("unfused", ROOT / ".deps/melee/src/melee/lb/lbvector.c"),
            ):
                target = Path(directory) / (name + ".js")
                result = subprocess.run(common + [str(matrix_source), "-o", str(target)],
                                        env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(node_runtime()), str(target)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                output[name] = result.stdout.strip().splitlines()
        self.assertEqual(output["source"], [
            "00000000 3e82fdca bf78ab01",
            "be7e7ab7 3e82fdca bf718bc4",
            "42a5daa7 42b8fc86 431fb43a",
            "420a5bc8 4242396c 434157ce",
            "42b92622 41cdb7ec 43321630",
        ])
        self.assertNotEqual(output["unfused"], output["source"])
        self.assertEqual(output["unfused"][0], "00000000 3e82fdca bf78ab00")
        self.assertEqual(output["unfused"][1], "be7e7ab6 3e82fdca bf718bc2")


if __name__ == "__main__":
    unittest.main()
