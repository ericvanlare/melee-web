"""Exercise source registry and checked fighter binding with game-free fixtures."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("fighter_registry", ROOT / "scripts/generate_fighter_registry.py")
REGISTRY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(REGISTRY)


class FighterBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="melee fighter binding ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "fighter_binding_test"
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise RuntimeError("A C++20 compiler is required")
        subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                        "-I", str(ROOT / "src"), str(ROOT / "src/dat_archive.cpp"),
                        str(ROOT / "src/dat_animation.cpp"), str(ROOT / "src/fighter_binding.cpp"),
                        str(ROOT / "tests/fighter_binding_test.cpp"), "-o", str(cls.binary)],
                       check=True, capture_output=True, timeout=120)

    def test_binding(self):
        for case in ("registry_identity", "common_layout", "action_membership",
                     "container_slicing", "binding_order"):
            with self.subTest(case=case):
                result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generator_narrow_syntax(self):
        self.assertEqual(REGISTRY.initializer("static T arr[] = {{foo, bar}, {0, 303},};", "arr"),
                         [["foo", "bar"], ["0", "303"]])
        for text in ("{foo + 1}", "{[3] = foo}", "{MACRO(foo)}", "{foo bar}", "{foo", "{foo} bar", "{040}", "{00}", ""):
            with self.subTest(text=text), self.assertRaises(REGISTRY.RegistryError):
                REGISTRY.parse_list(text)
        with self.assertRaises(REGISTRY.RegistryError):
            REGISTRY.initializer("T a[] = {x}; T a[] = {y};", "a")
        with self.assertRaises(REGISTRY.RegistryError):
            REGISTRY.initializer("T b[] = {x};", "a")
        self.assertEqual(REGISTRY.strip_comments('"http://file" /* note */ // next\nvalue'),
                         '"http://file"    \nvalue')

    def test_generated_registry_matches_pinned_source(self):
        if not (ROOT / ".deps/melee/src/melee/ft/ftdata.c").is_file():
            self.skipTest("Pinned source unavailable; build.py checks the generated registry after bootstrap")
        generated = REGISTRY.generate(ROOT / ".deps/melee")
        self.assertEqual(generated, (ROOT / "src/fighter_registry.inc").read_text())


if __name__ == "__main__":
    unittest.main()
