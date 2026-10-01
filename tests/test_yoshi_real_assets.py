"""Pin Yoshi's owned GALE01 revision-2 archives and source metadata."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = {
    "PlYs.dat": "95649bbf535503832190c9f34f400f27ad9c17523cebed88c1925bad1884f52b",
    "PlYsAJ.dat": "3282abc5431952243958af60e1d06e5aac6e3c185e02e2433d0c247581c1b3fd",
    "PlYsNr.dat": "34145fa82daf1180bfb3930c712ecc3b0adccb2342953e522e201c136b9c834f",
    "PlYsRe.dat": "2a2cff935b88f41cc802314c7bce523483b9bcc11fdaf1c11b9222cd48bbf570",
    "PlYsBu.dat": "b56cbb5bebc169c261917e529525ea7081f20725156fed3289101c668e91023a",
    "PlYsYe.dat": "15c3fae3fc94510ef767d8f5cfa8cd660bc2c9349e0ccf7aa565f5bbaee2e6c1",
    "PlYsPi.dat": "0507e2f95841611331e670e1b9b9da6eed8a1564ea2d991a11704aa716f13aa1",
    "PlYsAq.dat": "637bc33c8da9abab38ae432febe526417d80bb5a850d3f2d1c0cb2ba1da7f289",
    "EfYsData.dat": "259b168c57c19b28684c3e2465166b2fae1f01e2072cf5bd55055d7925216398",
    "GmRstMYs.dat": "917bf476b0745bdb31aadf1ff1fb7c8a3ad48f7022817619214b4a6f01d471a2",
    "yoshi.ssm": "49303e00aa1e15c2f7aef1acc72b88b948299a219be9f200a23f9a4c84f21e21",
}


class YoshiRealAssetTests(unittest.TestCase):
    def test_exact_assets_and_runtime_metadata(self):
        assets = ROOT / "assets-local/next-gate"
        required = [assets / name for name in EXPECTED_SHA256]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned Yoshi revision-2 assets are absent")
        for path in required:
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),
                             EXPECTED_SHA256[path.name], path.name)
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            self.skipTest("A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee Yoshi assets ") as directory:
            binary = Path(directory) / "yoshi_real_asset_trace"
            sources = [ROOT / "src" / (name + ".cpp") for name in
                       ("dat_archive", "dat_animation", "fighter_binding", "dat_fighter_runtime")]
            result = subprocess.run(
                [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
                 "-I", str(ROOT / "src"), *map(str, sources),
                 str(ROOT / "tests/yoshi_real_asset_trace.cpp"), "-o", str(binary)],
                capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            ordered = ["PlYs.dat", "PlYsAJ.dat", "EfYsData.dat", "yoshi.ssm",
                       "GmRstMYs.dat", "PlYsNr.dat", "PlYsRe.dat", "PlYsBu.dat",
                       "PlYsYe.dat", "PlYsPi.dat", "PlYsAq.dat"]
            result = subprocess.run([str(binary), *(str(assets / name) for name in ordered)],
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("effect bank 9/count", result.stdout)


if __name__ == "__main__":
    unittest.main()
