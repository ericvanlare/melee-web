"""Pin Zelda and Sheik GALE01 revision-2 fighter assets and identities."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = {
    "PlZd.dat": "e523bb0fe551f64aa6ae5b3ae59cd1e62434baeded076d4a5e1ea993e122a79a",
    "PlZdAJ.dat": "c01e3e509fc4daca840c2e38d8dc2158700c8e2dc8a3bb1523b24a260c14d3fa",
    "PlZdNr.dat": "aad695c91d980a19f8a355ee4d027601962a7b3882fb0ab768dd0e8d7cd65512",
    "PlZdRe.dat": "bb07b2827deec70febf82f8f1d71421d74359d71de6a352dc49e2c05c09168b2",
    "PlZdBu.dat": "d9b13c45adf2317349b26591207975661141e91096f08a024c86a01ac29f6885",
    "PlZdGr.dat": "85a4c2219c209a5a8d1de58dafa7c469093b2e8d13ff4254894a7ab04d0ff211",
    "PlZdWh.dat": "890781004398089d64b9db4ed7bea35217227263fa5bc46d6074515c01a1ff56",
    "PlSk.dat": "b75ee373521c4deb97ea8863e4d07e37b589dc7756c9e375e750e2043075ce89",
    "PlSkAJ.dat": "448f2a9d660da98c64812ca3ac0d1e20e7ce70a5f630f44b753a8aedef91477c",
    "PlSkNr.dat": "46dc4659d7112beeb7d0190f9ad8ae28d47a9db8877f6ec503e3c48044b8d29f",
    "PlSkRe.dat": "358c9e37942475382f6a3660ade10aa68adeada69ac85bec3594043faac0abd5",
    "PlSkBu.dat": "4c2b1541df5d62e9cfeb72b1fde4fb68dc9d14a239346b575533614632b4ba2c",
    "PlSkGr.dat": "4031e46af79adb3445776447ccbec3e559938fdfb58f3db5c86a6dd39463f81e",
    "PlSkWh.dat": "702dab8a9b932829fc3c65cdda2b8422b7a6169a76e186174cab117f0a20d1ba",
    "EfSsData.dat": "50e0f2702f7c44c1d7ab3da22cd769b71cce6f43811e5d707c9b4b48dfb759bf",
    "EfZdData.dat": "ab246332c215c928d0e7a85000b510956969772631f39898f8174cce473d9f47",
    "GmRstMZd.dat": "5c6a65c4caf6044f6ba248d0d4c85713e55d23c0a3dc437867e66df7f583a19e",
    "GmRstMSk.dat": "4e986a567c04c8375d7fda6f20fb7ba3f862be3be21be48983960a83fce5e726",
    "zs.ssm": "94dfa4790904a8b91e75aeb267ecaad4946277294910ce564d72a897ddc904fe",
}


class ZeldaSheikRealAssetTests(unittest.TestCase):
    def test_exact_assets_and_source_identities(self):
        assets = ROOT / "assets-local/next-gate"
        required = [assets / name for name in EXPECTED_SHA256]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Zelda/Sheik revision-2 assets are absent")
        for path in required:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Zelda Sheik assets ") as directory:
            binary = Path(directory) / "zelda_seak_real_asset_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")]
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *map(str, sources),
                 str(ROOT / "tests/zelda_seak_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            ordered = ["PlZd.dat", "PlZdAJ.dat", "PlSk.dat", "PlSkAJ.dat",
                       "EfSsData.dat", "EfZdData.dat", "zs.ssm", "GmRstMZd.dat",
                       "GmRstMSk.dat", "PlZdNr.dat", "PlZdRe.dat", "PlZdBu.dat",
                       "PlZdGr.dat", "PlZdWh.dat", "PlSkNr.dat", "PlSkRe.dat",
                       "PlSkBu.dat", "PlSkGr.dat", "PlSkWh.dat"]
            result = subprocess.run([str(binary), *(str(assets / name) for name in ordered)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Zelda FTKind 19/311 and Sheik FTKind 7/317", result.stdout)


if __name__ == "__main__":
    unittest.main()
