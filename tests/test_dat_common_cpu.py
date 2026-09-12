"""Exercise the real PlCo root22 CPU graph and its owning lifetime."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatCommonCpuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for CPU common-data tests")
        cls.temporary = tempfile.TemporaryDirectory(prefix="melee CPU common tests ")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary = Path(cls.temporary.name) / "dat_common_cpu_trace"
        result = subprocess.run(
            [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
             "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
             str(ROOT / "src/dat_common.cpp"), str(ROOT / "tests/dat_common_cpu_trace.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"CPU common test compilation failed:\n{result.stdout}{result.stderr}")

    def test_real_plco_root22_and_retained_lifetime(self):
        asset = ROOT / "assets-local/next-gate/PlCo.dat"
        if not asset.is_file():
            self.skipTest("Local PlCo.dat is unavailable")
        result = subprocess.run([str(self.binary), str(asset)], capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Local PlCo root22 command graph and ownership: passed", result.stdout)

    def test_malformed_root22_inputs_fail_closed(self):
        asset = ROOT / "assets-local/next-gate/PlCo.dat"
        if not asset.is_file():
            self.skipTest("Local PlCo.dat is unavailable")
        for case in ("opcode", "row0", "sentinel", "divisor", "relocation"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), str(asset), case],
                                        capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn(f"Malformed PlCo root22 input rejected: {case}", result.stdout)

    def test_pinned_source_layout_is_explicit(self):
        if not (ROOT / ".deps/melee/src/melee/ft/fighter.h").is_file():
            self.skipTest("Pinned source checkout is unavailable")
        fighter = (ROOT / ".deps/melee/src/melee/ft/fighter.h").read_text()
        self.assertRegex(fighter, r"u8\*\* cmdscripts;.*\+00")
        for field in ("void** x4", "void** x8", "UNK_T* xC", "void** x10",
                      "void** x14", "void** x18", "void** x1C", "float* x20",
                      "void* x24"):
            self.assertIn(field, fighter)
        commands = (ROOT / ".deps/melee/src/melee/ft/ftcmdscript.h").read_text()
        self.assertIn("CpuCmd_Done = 0x7F", commands)
        self.assertIn("CpuCmd_OneArgEnd = 0xBF", commands)
        self.assertIn("CpuCmd_LstickForwardClamped", commands)


if __name__ == "__main__":
    unittest.main()
