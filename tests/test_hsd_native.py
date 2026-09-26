"""Checked native descriptors and original HSD object lifetimes; no game assets required."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class NativeJointDescriptorTests(unittest.TestCase):
    def test_shared_zero_material_identity_and_owned_payloads(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee native descriptors ") as directory:
            output = Path(directory) / "native_test"
            result = subprocess.run([compiler, "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "src"), *[str(ROOT / "src" / (unit + ".cpp")) for unit in
                ("dat_archive", "dat_texture", "dat_material", "rigid_model", "dat_native_joint")],
                str(ROOT / "tests/dat_native_joint_test.cpp"), "-o", str(output)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(output)], capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("typed native graph identity/lifetime/rejection checks passed", result.stdout)
            fighter=ROOT/"assets-local/next-gate/PlMr.dat"
            costume=ROOT/"assets-local/next-gate/PlMrNr.dat"
            if fighter.is_file() and costume.is_file():
                result=subprocess.run([str(output),str(fighter),str(costume)],capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("Local Mario metal graph:61 matching joints,8 DObj occurrences,21 PObjs passed",result.stdout)
            samus=ROOT/"assets-local/next-gate/PlSs.dat"
            if samus.is_file():
                result=subprocess.run([str(output),"--samus",str(samus)],capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("Samus grapple native graph:",result.stdout)
            yoshi=ROOT/"assets-local/next-gate/PlYs.dat"
            if yoshi.is_file():
                result=subprocess.run([str(output),"--yoshi",str(yoshi)],capture_output=True,text=True,timeout=20)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                self.assertIn("Yoshi EggThrow native model:",result.stdout)


class NativeFighterOutputGuardTests(unittest.TestCase):
    def test_terminal_singleton_output_is_defined_at_source_consumer(self):
        targets = [ROOT / 'build' / directory / 'hsd_native_trace.js'
                   for directory in ('browser', 'browser-release')]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest('Build hsd_native_trace for source interpolation guard checks')
        target = max(targets, key=lambda path: path.stat().st_mtime)
        for mode in ('--terminal-branch-linear', '--terminal-branch-spline',
                     '--terminal-pass-endpoint', '--terminal-stop-ceil-endpoint'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(node_runtime()), str(target), mode], cwd=ROOT,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('original terminal branch visibility constant passed',
                              result.stdout + result.stderr)


class NativeJointRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / ".deps/emsdk/.emscripten").is_file():
            raise unittest.SkipTest("Pinned SDK unavailable; run bootstrap")
        cls.node = node_runtime()
        cls.trace = ROOT / "build/browser/hsd_native_trace.js"
        cls.probe = ROOT / "build/browser/hsd_native_dat_probe.js"
        if not cls.trace.is_file() or not cls.probe.is_file():
            raise unittest.SkipTest("Native HSD targets unavailable; run scripts/build.py --target gameplay")

    def run_target(self, target, *arguments):
        return subprocess.run([str(self.node), str(target), *map(str, arguments)],
                              cwd=ROOT, capture_output=True, text=True, timeout=60)

    def test_original_allocation_callback_rejection_and_restart(self):
        result = self.run_target(self.trace)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("original native joint allocation/reference/destruction/restart passed", result.stdout)

    def test_replacement_heap_is_never_used_for_teardown(self):
        result = self.run_target(self.trace, "--replace-heap")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("native teardown safely rejected replacement heap", result.stdout)

    def local_asset(self, name):
        for directory in (ROOT / "assets-local/next-gate", ROOT / "assets-local"):
            if (directory / name).is_file():
                return directory / name
        self.skipTest(f"Optional local {name} unavailable")

    def test_local_common_and_mario_native_envelopes_restart(self):
        common, costume = self.local_asset("PlCo.dat"), self.local_asset("PlMrNr.dat")
        result = self.run_target(self.probe, common, costume, "PlyMario5K_Share_joint")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("root20 native original consumer:"), 2)
        self.assertEqual(result.stdout.count("costume native original loader: 61 joints"), 2)
        self.assertRegex(result.stdout, r"[1-9][0-9]* resolved influences")
        self.assertEqual(result.stdout.count("Mario original material animation: two eye textures"), 2)
        self.assertEqual(result.stdout.count("Local common context: 18 copied roots"), 2)
        self.assertIn("Original common root20 initialization/destruction/restart passed", result.stdout)

    def test_local_costume_requires_exact_public_symbol(self):
        common, costume = self.local_asset("PlCo.dat"), self.local_asset("PlMrNr.dat")
        result = self.run_target(self.probe, common, costume, "PlyMario5K_Share_join")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Exact requested costume joint symbol is absent", result.stderr)


if __name__ == "__main__":
    unittest.main()
