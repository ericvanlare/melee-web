"""Pin Samus's owned GALE01 revision-2 archives and source metadata."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = {
    "PlSs.dat": "8fc0047354d551c8854e9834a3aad08e377f9855ab2572f47d0b41adbf347c22",
    "PlSsAJ.dat": "1c64ba70c454d67282b2b0817cbfd24036cf4bb9dfc24765229e87b402d6be70",
    "PlSsNr.dat": "3d14d8e66612ed11c5522608b5f3d50ffd571263c47f4767ae03fdedcc00f291",
    "PlSsPi.dat": "71269d0fd4a92bbfa9e7b80f525574082687fb09dd1f37c8d5244e0727d5b85c",
    "PlSsBk.dat": "581aef31fd1ead4067902c13a3592edb3492e281e4680e9375a7647651f07f94",
    "PlSsGr.dat": "85a4430f2b3ad54363ef91374f4db3741ecd8925a7c048703743ab4c22821470",
    "PlSsLa.dat": "c8dd8e5d201df1e9fff6b3c21c04bd75158d9019b7b0cc8a4f066a15164b2a25",
    "EfSsData.dat": "50e0f2702f7c44c1d7ab3da22cd769b71cce6f43811e5d707c9b4b48dfb759bf",
    "GmRstMSs.dat": "702378fa9c43f2b6676dcf84c624cc1a8ea4aff0a65697b24938dd900213b273",
    "samus.ssm": "019749042a7eb0c7e3f788dfbf26241044cccdf068a377d79a397497473c79fa",
}


class SamusRealAssetTests(unittest.TestCase):
    def test_exact_assets_and_runtime_metadata(self):
        assets = ROOT / "assets-local/next-gate"
        names = tuple(EXPECTED_SHA256)
        required = [assets / name for name in names]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Samus revision-2 assets are absent")
        for path in required:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Samus assets ") as directory:
            binary = Path(directory) / "samus_real_asset_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")]
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *map(str, sources),
                 str(ROOT / "tests/samus_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Fighter/action/effect/audio/result followed by the five costume roots.
            ordered = ["PlSs.dat", "PlSsAJ.dat", "EfSsData.dat", "samus.ssm",
                       "GmRstMSs.dat", "PlSsNr.dat", "PlSsPi.dat", "PlSsBk.dat",
                       "PlSsGr.dat", "PlSsLa.dat"]
            result = subprocess.run([str(binary), *(str(assets / name) for name in ordered)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("effect bank 2/count 4", result.stdout)


if __name__ == "__main__":
    unittest.main()
