"""Check SSM PCM against the independently implemented Aurora THP decoder.

Authored fixtures require no copyrighted assets. Real-bank comparison is
optional and skips when user-owned main.ssm/mario.ssm are absent.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        thp = ROOT / ".deps/aurora/lib/dolphin/thp/THPAudio.cpp"
        fmt = ROOT / "build/browser/_deps/fmt-src/include"
        if not compiler or not thp.is_file() or not (fmt / "fmt/format.h").is_file():
            raise unittest.SkipTest("Bootstrap Aurora and a C++20 compiler for independent DSP comparison")
        cls.directory = tempfile.TemporaryDirectory(prefix="melee SSM decoder ")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "audio"
        result = subprocess.run([compiler, "-std=c++20", "-O1", "-DTARGET_PC",
            "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(fmt), "-include", "dolphin/types.h",
            str(ROOT / "src/dat_audio.cpp"), str(thp),
            str(ROOT / "tests/dat_audio_test.cpp"), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def run_trace(self, *assets):
        result = subprocess.run([str(self.binary), *map(str, assets)],
                                capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("independent decoder comparison passed", result.stdout)
        return result.stdout

    def test_authored_adpcm_loop_history_and_malformed_bounds(self):
        self.run_trace()

    def test_optional_owned_main_and_mario_banks(self):
        assets = [ROOT / "assets-local/next-gate" / name for name in ("main.ssm", "mario.ssm")]
        if not all(path.is_file() for path in assets):
            self.skipTest("Optional user-owned main.ssm and mario.ssm are unavailable")
        output = self.run_trace(*assets)
        self.assertIn("SSM base=0 samples=246 frames=3578201 loops=8 exactly match", output)
        self.assertIn("SSM base=783 samples=32 frames=652032 loops=0 exactly match", output)


if __name__ == "__main__":
    unittest.main()
