"""Pin Marth's owned GALE01 revision-2 assets and decoded source metadata."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = {
    "PlMs.dat": "0c4b7e49c8d18bfb3f5fd8c463d7c00b70292fa971b1ee94caf99548ba5b45b2",
    "PlMsAJ.dat": "a9560b7fdfca328dd3deeb6e999edc7ea1e89abe837c3846afdd46972b2ca308",
    "EfMsData.dat": "efad8db0868dcb7e8b1ed24f31cc9101cbf9a3659582903f3619f52c1083e8a1",
    "mars.ssm": "458c377ed374baf51029498651b57ba06c9b5dff22e55ff492ef60908aa740b2",
    "PlMsNr.dat": "e4f2b635373f23510d81cdb395b9633501ca52135f7694eb9a31af7aebe38c72",
    "PlMsRe.dat": "930cd89313b0e73b3113d9e9421fae55998e1aa69abbaab5953fda762ce89eb4",
    "PlMsGr.dat": "fa8771bd99690868dfd0fb3508be131a5ccb1b904aa348955d4176aa4315974c",
    "PlMsBk.dat": "3e54661fe43c8edba617e9f0cdcac0c0cd50172ee67953c413ac26626f53e8c5",
    "PlMsWh.dat": "51bf4a608510b43a74c71cb5f22637aab33ab4dd66658c88506af22c44a5bfad",
}

class MarthRealAssetTests(unittest.TestCase):
    def test_exact_assets_and_runtime_metadata(self):
        assets = ROOT / "assets-local/next-gate"
        names = ("PlMs.dat", "PlMsAJ.dat", "EfMsData.dat", "mars.ssm",
                 "PlMsNr.dat", "PlMsRe.dat", "PlMsGr.dat", "PlMsBk.dat", "PlMsWh.dat")
        required = [assets / name for name in names]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Marth revision-2 assets are absent")
        for path in required:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Marth assets ") as directory:
            binary = Path(directory) / "marth_real_asset_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")]
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *map(str, sources),
                 str(ROOT / "tests/marth_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), *map(str, required)], capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("effect bank 16/count 2", result.stdout)


if __name__ == "__main__":
    unittest.main()
