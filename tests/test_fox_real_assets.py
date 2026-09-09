"""Check Fox against the owned revision 2 DAT, animation and dependency assets."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

EXPECTED_SHA256 = {
    "PlFx.dat": "846b075d3379de041ecfb5504f5442484c23783422a0851851c96489d697b690",
    "PlFxAJ.dat": "a9f3865a0c085b321876543900625d7d54dfef5fe2438ea8068f6221073a6c7d",
    "EfFxData.dat": "6029fc0740155310322c4313c29cfed0d2acc3a6cb90c8a4945b9cb605f7570e",
    "fox.ssm": "cb012129694c3317e6aaa2fa5b2d8a050e505cca4a0af25198696f710aad2e4d",
    "PlFxNr.dat": "f1ebe2af74d34be113614da3204730ea6d09b078ebf8a82d2a9036d58d8e4f8b",
    "PlFxOr.dat": "3d233ecd659a8fa4462baf6795a37f8ec29a0db87da653b2cf2d68c086f269e2",
    "PlFxLa.dat": "4fc636ad964a8011b3cec119adfdbf92128f9ced8034146c6293415a45925b19",
    "PlFxGr.dat": "b8a41b28a72c5ce2adc0705075662f73763553838356557ecd302e40c066e6d4",
}


class FoxRealAssetTests(unittest.TestCase):
    def test_metadata_animation_effect_and_audio_dependencies(self):
        assets = ROOT / "assets-local/next-gate"
        required = [assets / name for name in
                    ("PlFx.dat", "PlFxAJ.dat", "EfFxData.dat", "fox.ssm",
                     "PlFxNr.dat", "PlFxOr.dat", "PlFxLa.dat", "PlFxGr.dat")]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Fox revision 2 assets are absent")
        for path in required:
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            self.assertEqual(digest, EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Fox assets ") as directory:
            binary = Path(directory) / "fox_real_asset_trace"
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *(str(ROOT / "src" / (name + ".cpp")) for name in
                 ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")),
                 str(ROOT / "tests/fox_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(binary), *(str(path) for path in required)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("effect bank 3/count 6", result.stdout)
        checker = ROOT / "scripts/check_assets.py"
        symbols = ("PlyFox5K_Share_joint", "PlyFox5KOr_Share_joint",
                   "PlyFox5KLa_Share_joint", "PlyFox5KGr_Share_joint")
        for path, symbol in zip(required[4:], symbols):
            result = subprocess.run(
                ["python3", str(checker), str(path), "--symbol", symbol],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
