"""Compile the actual private observer seam with controlled source/GPU endpoints."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class MatchFrameEffectNativeTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "frame-effect-native-")

    def test_asyncify_entry_and_driver_controls(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test",
                                 str(ROOT / "tests/match_frame_effect_driver.test.mjs")],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("tests 7", result.stdout)
        self.assertIn("fail 0", result.stdout)

    def test_schema2_strict_values_and_corruption_controls(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test",
                                 str(ROOT / "tests/match_frame_effect_schema2.test.mjs")],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("tests 4", result.stdout)
        self.assertIn("fail 0", result.stdout)

    def test_bounded_transport_and_normal_frame_adapter_controls(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test",
                                 str(ROOT / "tests/one_normal_frame_observation_adapter.test.mjs")],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("tests 17", result.stdout)
        self.assertIn("fail 0", result.stdout)

    def test_actual_seam_one_shot_admission_failure_and_retirement(self):
        header = ROOT / ".deps/aurora/include/aurora/gfx.h"
        if not header.is_file():
            self.skipTest("Pinned Aurora header unavailable")
        api = header.read_text()
        start = api.find("enum {\n  AURORA_BROWSER_FRAME_EFFECT_SCHEMA_VERSION")
        if start < 0:
            self.skipTest("Reviewed frame-effect Aurora patch not installed")
        end = api.index("#endif", start)
        api = api[start:end]
        source = (ROOT / "src/gameplay_menu_frame_effect.cpp").read_text()
        source = "\n".join(line for line in source.splitlines() if not line.startswith("#include"))
        fixture = (ROOT / "tests/match_frame_effect_native_fixture.cpp").read_text()
        unit = self.work / "control.cpp"
        unit.write_text(fixture.replace("// ACTUAL_AURORA_API", api).replace("// ACTUAL_OBSERVER_SOURCE", source))
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler)
        binary = self.work / "control"
        build = subprocess.run([compiler, "-std=c++20", "-DTARGET_PC=1", "-I", str(ROOT / ".deps/aurora/include"), "-Wall", "-Wextra", "-Werror",
                                str(unit), "-o", str(binary)], capture_output=True, text=True, timeout=60)
        (self.work / "build.log").write_text(build.stdout + build.stderr)
        self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
        (self.work / "run.log").write_text(run.stdout + run.stderr)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        rows = [line[len("SCHEMA2_STATUS "):] for line in run.stdout.splitlines() if line.startswith("SCHEMA2_STATUS ")]
        self.assertEqual(len(rows), 1)
        raw = self.work / "schema2-status.json"
        raw.write_text(rows[0])
        check = self.work / "check-schema2.mjs"
        check.write_text("import fs from 'node:fs';\nimport assert from 'node:assert/strict';\n" +
                         "import {frameEffectObservationFailures,compareFrameEffectObservations} from " +
                         repr((ROOT / 'tests/match_frame_effect_driver.mjs').as_uri()) + ";\n" +
                         "const r=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));\n" +
                         "assert.deepEqual(frameEffectObservationFailures(r),[]);\n" +
                         "assert.deepEqual(compareFrameEffectObservations(r,structuredClone(r)),[]);\n" +
                         "console.log('actual native schema2 JSON decoded exactly');\n")
        decoded = subprocess.run([str(node_runtime(ROOT)), str(check), str(raw)],
                                 capture_output=True, text=True, timeout=15)
        self.assertEqual(decoded.returncode, 0, decoded.stdout + decoded.stderr)
