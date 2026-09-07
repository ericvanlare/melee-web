"""Run the pinned original AObj/FObj/spline source on authored analytic curves."""
import ast
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HsdAnimationBridgeTests(unittest.TestCase):
    def test_original_interpolation_seek_loop_flags_and_pose_boundaries(self):
        sdk = ROOT / ".deps/emsdk"
        compiler_root = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler_root / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Project-local SDK unavailable; run scripts/bootstrap.py")
        node_setting = None
        for statement in ast.parse(config.read_text()).body:
            if (isinstance(statement, ast.Assign) and
                    any(isinstance(target, ast.Name) and target.id == "NODE_JS" for target in statement.targets)):
                node_setting = ast.literal_eval(statement.value)
        self.assertIsInstance(node_setting, str)
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        version = (compiler_root / "emscripten-version.txt").read_text().strip().strip('"')
        self.assertEqual(version, lock["emscripten"])
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EM_CACHE=str(compiler_root / "cache"), EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee original animation ") as directory:
            output = Path(directory) / "animation.js"
            common = ["-O1", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off", "-I", str(ROOT / "src")]
            c_sources = [ROOT / "src/hsd_animation_bridge.c", ROOT / "src/hsd_host_support.c"]
            c_sources += [ROOT / f".deps/melee/src/sysdolphin/baselib/{name}.c" for name in ("aobj", "fobj", "spline")]
            commands = [
                [sys.executable, str(compiler_root / "emcc.py"), *common, "-std=c11", "-DTARGET_PC",
                 "-I", str(ROOT / ".deps/melee/src"), "-I", str(ROOT / ".deps/aurora/include"),
                 "-include", str(ROOT / "src/hsd_probe_compat.h"), "-c", *map(str, c_sources)],
                [sys.executable, str(compiler_root / "em++.py"), *common, "-std=c++20", "-fexceptions",
                 "-Wall", "-Wextra", "-Werror", "-c", str(ROOT / "src/dat_archive.cpp"),
                 str(ROOT / "src/dat_animation.cpp"), str(ROOT / "src/hsd_animation_bridge.cpp"),
                 str(ROOT / "tests/hsd_animation_trace.cpp")],
            ]
            # C and C++ bridge files share a basename; compile each group in its
            # own directory so neither object silently replaces the other.
            objects = []
            for index, command in enumerate(commands):
                build = Path(directory) / str(index)
                build.mkdir()
                result = subprocess.run(command, cwd=build, env=env, capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.extend(build.glob("*.o"))
            result = subprocess.run([
                sys.executable, str(compiler_root / "em++.py"), "-O1", "-fexceptions",
                *map(str, objects), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sNODERAWFS=1", "-o", str(output),
            ], cwd=directory, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # No copyrighted test fixture is required or fetched. If this user
            # has explicitly extracted the local gate clip, also validate it.
            clip = ROOT / "assets-local/next-gate/MarioWait1.dat"
            command = [str(node), str(output)]
            if clip.is_file():
                command.append(str(clip))
            result = subprocess.run(command, cwd=directory, env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Original HSD animation trace: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
