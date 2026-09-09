"""Check Falco against the owned revision 2 DAT, animation and dependency assets."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FalcoRealAssetTests(unittest.TestCase):
    def test_metadata_animation_effect_and_audio_dependencies(self):
        assets = ROOT / "assets-local/next-gate"
        required = [assets / name for name in
                    ("PlFc.dat", "PlFcAJ.dat", "EfFxData.dat", "falco.ssm",
                     "PlFcNr.dat", "PlFcRe.dat", "PlFcBu.dat", "PlFcGr.dat")]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Falco revision 2 assets are absent")
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Falco assets ") as directory:
            binary = Path(directory) / "falco_real_asset_trace"
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *(str(ROOT / "src" / (name + ".cpp")) for name in
                 ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")),
                 str(ROOT / "tests/falco_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(binary), *(str(path) for path in required[:4])],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("effect bank 3/count 6", result.stdout)
        checker = ROOT / "scripts/check_assets.py"
        symbols = ("PlyFalco5K_Share_joint", "PlyFalco5KRe_Share_joint",
                   "PlyFalco5KBu_Share_joint", "PlyFalco5KGr_Share_joint")
        for path, symbol in zip(required[4:], symbols):
            result = subprocess.run(
                ["python3", str(checker), str(path), "--symbol", symbol],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
