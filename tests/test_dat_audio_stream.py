"""Check HPS PCM against the independently implemented Aurora THP decoder.

Authored fixtures require no copyrighted assets. Real-bank comparison is
optional and skips when user-owned sp_end.hps is absent.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DatAudioStreamTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("c++")
        thp = ROOT / ".deps/aurora/lib/dolphin/thp/THPAudio.cpp"
        fmt = ROOT / "build/browser/_deps/fmt-src/include"
        if not compiler or not thp.is_file() or not (fmt / "fmt/format.h").is_file():
            raise unittest.SkipTest("Bootstrap Aurora and a C++20 compiler for independent DSP comparison")
        cls.directory = tempfile.TemporaryDirectory(prefix="melee HPS decoder ")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "audio"
        result = subprocess.run([compiler, "-std=c++20", "-O1", "-DTARGET_PC",
            "-I", str(ROOT / "src"), "-I", str(ROOT / ".deps/aurora/include"),
            "-I", str(fmt), "-include", "dolphin/types.h",
            str(ROOT / "src/dat_audio.cpp"), str(ROOT / "src/dat_audio_stream.cpp"), str(thp),
            str(ROOT / "tests/dat_audio_stream_test.cpp"), "-o", str(cls.binary)],
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

    def test_optional_owned_final_destination_music(self):
        asset = ROOT / "assets-local/next-gate/sp_end.hps"
        if not asset.is_file():
            self.skipTest("Optional user-owned sp_end.hps is unavailable")
        output = self.run_trace(asset)
        self.assertIn("HPS blocks=50 loop=3", output)


if __name__ == "__main__":
    unittest.main()
