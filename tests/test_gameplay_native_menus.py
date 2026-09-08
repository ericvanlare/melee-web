"""Original SIS bytecode behavior; runs without proprietary menu assets."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime

class NativeMenuSourceTests(unittest.TestCase):
    def test_owned_css_scene_lifecycle(self):
        targets = [ROOT / "build" / name / "native_css_callbacks.js"
                   for name in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        menu = ROOT / "assets-local/native-menus"
        audio = ROOT / "assets-local/next-gate"
        required = [menu / name for name in (
            "MnSlChr.usd", "SdSlChr.usd", "MnExtAll.usd", "LbMcGame.usd",
            "NtMemAc.usd", "menu01.hps", "nr_select.ssm", "nr_title.ssm",
            "nr_name.ssm", "pokemon.ssm", "end.ssm")] + [audio / name for name in (
            "smash2.sem", "main.ssm", "mario.ssm", "dsp_coef.bin", "sislib_font.bin")]
        if not targets or not all(path.is_file() for path in required):
            self.skipTest("Original CSS target and owned local fixtures are required")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target), str(menu), str(audio)],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original CSS enter, 120 neutral input/scheduler/audio ticks and exit passed in two worlds", run.stdout)

    def test_original_sis_layout_and_style_stack(self):
        candidates = [ROOT / "build" / directory / "native_menu_scene_trace.js"
                      for directory in ("browser", "browser-release")]
        targets = [p for p in candidates if p.is_file()]
        if not targets:
            self.skipTest("Build fighter targets before the original SIS consumer check")
        target = max(targets, key=lambda p: p.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[:4000])
        self.assertIn("Original SIS big-endian layout and style-stack trace passed", run.stdout)
        for argument in ("--bad-image-index", "--bad-palette-index"):
            with self.subTest(argument=argument):
                rejected = subprocess.run([str(node_runtime()), str(target), argument],
                                          cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("Native texture animation index exceeds its owned table",
                              rejected.stdout + rejected.stderr)

if __name__ == "__main__": unittest.main()
