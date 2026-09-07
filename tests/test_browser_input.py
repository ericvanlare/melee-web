"""Check the browser input adapter contract, not physical-device behavior."""

import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BrowserInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        aurora = ROOT / ".deps/aurora/include"
        sdl = ROOT / "build/browser/_deps/sdl-src/include"
        if not (aurora / "dolphin/pad.h").is_file() or not (sdl / "SDL3/SDL_keyboard.h").is_file():
            raise unittest.SkipTest("Bootstrap and configure the browser build to obtain pinned PAD/SDL headers")
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            raise RuntimeError("A C++20 compiler is required for input adapter tests")
        cls.temp = tempfile.TemporaryDirectory(prefix="melee input tests ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "browser_input_test"
        result = subprocess.run(
            [compiler, "-std=c++20", "-DTARGET_PC", "-Wall", "-Wextra", "-Werror", "-O1",
             "-I", str(ROOT / "src"), "-I", str(aurora), "-I", str(sdl),
             str(ROOT / "src/browser_input.cpp"), str(ROOT / "tests/browser_input_test.cpp"),
             "-o", str(cls.binary)], capture_output=True, text=True, timeout=120,
        )
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        diagnostic = json.loads(result.stdout)
        self.assertEqual(len(diagnostic["pads"]), 4)
        self.assertIn(diagnostic["active"], (0, 1))
        return diagnostic

    def test_documented_keyboard_button_and_axis_mapping(self):
        result = self.run_case("binding_mapping")
        self.assertEqual(result["pads"][0]["source"], "keyboard")

    def test_keyboard_default_and_prestartup_frontend_preference(self):
        self.run_case("keyboard_preference")

    def test_raw_sampling_and_separate_provider_clamp(self):
        result = self.run_case("raw_and_clamped")
        self.assertEqual(result["physical_mask"], 1)
        self.assertEqual(result["pads"][0]["stick"], [127, 0])

    def test_focus_and_visibility_neutralize_without_waiting_for_frame(self):
        self.run_case("focus_and_visibility")

    def test_last_sample_history_survives_release_but_current_input_resets(self):
        result = self.run_case("last_non_neutral")
        self.assertEqual(result["samples"], 3)
        self.assertEqual(result["pads"][0]["buttons"], 0)
        self.assertEqual(result["pads"][0]["stick"], [0, 0])
        self.assertEqual(result["last_non_neutral"]["sample"], 1)
        self.assertEqual(result["last_non_neutral"]["port"], 0)
        self.assertEqual(result["last_non_neutral"]["source"], "keyboard")
        self.assertEqual(result["last_non_neutral"]["stick"], [127, 0])
        self.assertEqual(result["last_non_neutral"]["clamped"]["stick"], [41, 0])

    def test_disconnect_provider_errors_and_rumble_mask_distinction(self):
        self.run_case("disconnect_and_errors")

    def test_keyboard_fallback_does_not_mix_with_physical_controller(self):
        self.run_case("keyboard_source_changes")

    def test_setup_failure_and_shutdown_are_explicit(self):
        self.run_case("startup_failure_and_shutdown")


if __name__ == "__main__":
    unittest.main()
