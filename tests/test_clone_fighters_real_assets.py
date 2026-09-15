"""Check Dr. Mario and Roy against owned revision-2 DAT and dependency assets."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

EXPECTED_SHA256 = {
    "PlDr.dat": "dbd729b1e038a6da6a90a732416aca164f5e55dc2ec7024555a6eaa0a926bb8c",
    "PlDrAJ.dat": "f718e88d7d1188d4e55ce66111788d7db6c6fcaeaf2db7c728e2cca5aa0931b3",
    "EfMrData.dat": "39ccac18137b108b31bb4d832475ba60dc45e381dce9005e14116848e465c669",
    "drmario.ssm": "1703530fb3bec04fec6b3ac7e19a28ff84340c92e37e3b20df6b8876cd315890",
    "PlDrNr.dat": "c1be714a4f9c4a5ef24770f427fc13d9b9d315e6ef7eb71a13b9d78d67d7f672",
    "PlDrRe.dat": "a791283f1e5746f24376aefaaaa0292070eda7e255ae0863c87175cc1f6a6acb",
    "PlDrBu.dat": "0a35e13100f8f2fb2d9cc519501800a5bce069b9e7fb15422d48197dcc1d7b1f",
    "PlDrGr.dat": "4cc8279000b4b50aad68685a9c9264deb8d449972dd7844a273d841a5f48b088",
    "PlDrBk.dat": "8d8132347a5651158aaa425c1f01f13e41b23fc845002cce3040e7e99d837139",
    "PlFe.dat": "5d8ec1eb2821e8700ee3ed8020d1b57ca5f857468dad3eddfc28bbc398169648",
    "PlFeAJ.dat": "8c235de3cd367e91c4db74e902f7b064dc8f1a2e34bb8515006c19f8e13fbd99",
    "EfFeData.dat": "6707f90d06afc1b3107efdb66d8d90a797d3081fbfb5385aab095d8f66cb12d2",
    "emblem.ssm": "0c2a406286870c414cc66cf60b1501c08d41dd22e240e589cdc64cbd042300f1",
    "PlFeNr.dat": "0e861f97db059e2672b7102601ff66e55430a1fe24a530d1a6f4630f5af642a7",
    "PlFeRe.dat": "e7a7e080b2cb3bdadd6bb59ba7faf58c5798d2f34a5b0e426b7b61f30a2daf36",
    "PlFeBu.dat": "3068ac9502f0767be1b83c040d63d42249a877402f75dbdca52e6e746396c401",
    "PlFeGr.dat": "216d540811aa49f293826b669b90385d75d8cbce09d7296c5c6194b99a85525e",
    "PlFeYe.dat": "520f1bf9c759eb459fe840287d9fd7b4451cec0e88c2f4861ddcb6059d831b9a",
}


class CloneFighterRealAssetTests(unittest.TestCase):
    def test_metadata_animation_effect_audio_and_costume_dependencies(self):
        assets = ROOT / "assets-local/next-gate"
        names = (
            "PlDr.dat", "PlDrAJ.dat", "EfMrData.dat", "drmario.ssm",
            "PlDrNr.dat", "PlDrRe.dat", "PlDrBu.dat", "PlDrGr.dat", "PlDrBk.dat",
            "PlFe.dat", "PlFeAJ.dat", "EfFeData.dat", "emblem.ssm",
            "PlFeNr.dat", "PlFeRe.dat", "PlFeBu.dat", "PlFeGr.dat", "PlFeYe.dat",
        )
        required = [assets / name for name in names]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Dr. Mario/Roy revision-2 assets are absent")
        for path in required:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee clone fighter assets ") as directory:
            binary = Path(directory) / "clone_fighters_real_asset_trace"
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"),
                 *(str(ROOT / "src" / (name + ".cpp")) for name in
                   ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")),
                 str(ROOT / "tests/clone_fighters_real_asset_trace.cpp"),
                 "-o", str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary), *(str(path) for path in required)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Dr. Mario kind 21/303 actions", result.stdout)
            self.assertIn("effect bank 49/count 2", result.stdout)
            # Build the model parser once, then run ten explicit symbol checks
            # against that fresh binary. Recompiling it per costume obscures
            # the focused test's actual parser coverage.
            checker = Path(directory) / "asset_check"
            checker_sources = ("dat_archive", "dat_texture", "dat_material",
                               "dat_stage", "rigid_model")
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"),
                 *(str(ROOT / "src" / (name + ".cpp")) for name in checker_sources),
                 str(ROOT / "tools/asset_check.cpp"), "-o", str(checker)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            symbols = (
                "PlyDrmario5K_Share_joint", "PlyDrmario5KRe_Share_joint",
                "PlyDrmario5KBu_Share_joint", "PlyDrmario5KGr_Share_joint",
                "PlyDrmario5KBk_Share_joint", "PlyEmblem5K_Share_joint",
                "PlyEmblem5KRe_Share_joint", "PlyEmblem5KBu_Share_joint",
                "PlyEmblem5KGr_Share_joint", "PlyEmblem5KYe_Share_joint",
            )
            for path, symbol in zip(required[4:9] + required[13:18], symbols):
                result = subprocess.run([str(checker), str(path), "--symbol", symbol],
                                        capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
