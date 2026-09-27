"""Actual Samus chain functions versus an owned-DOL operation transcription.

The production check requires bit-exact agreement. Sources are generated in an
isolated directory so this check cannot overwrite another build's source tree.
"""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_bool import transform_source

# Owned GALE01r2 body identities, not copies of executable bytes.
BODIES = {
    "__init_registers": (0x80005340, 0x1C, "9d23a6c84a3910e42fd0d8296a6ea4255b32579b667d12b115b851d6c09ae135"),
    "it_802B900C": (0x802B900C, 0x1B8, "a6ca8a99d48233fb4814c02269fad20dbca71c3fe8901c7678e5c59aaf2f2771"),
    "it_802B99A0": (0x802B99A0, 0x348, "be52e1c12b874d1c4d76e8f5d61e0e295a85919cdc18440b0f1c390c696da33a"),
    "it_802B9CE8": (0x802B9CE8, 0x2EC, "5b55be7d31973ba70fdffed54f82e02904dd9d8e37a8fffaa016775f60ba8172"),
    "it_802B9FD4": (0x802B9FD4, 0x1C0, "e7e1d2acd5c37e2fd1839aac3f31b27d05de0948f5969c49fd4c01e35655257d"),
    "it_802A3C98": (0x802A3C98, 0xF8, "aec1d0296c2d3254f5ae8732ab9d8517205d0938ded5cd7de23ba8e45615e4cf"),
    "it_802A43B8": (0x802A43B8, 0x34, "4902cd7bb84fa8fd39bbe46a0ea7aaf609076a7b75588d11a08d72eb2caaf796"),
    "it_802A43EC": (0x802A43EC, 0x34, "fa4c960f622d5922bd7e8307ad13cb8fecd5fae5da1229f205870a081d468de5"),
    "it_802A4420": (0x802A4420, 0x34, "266e567234a3f487478fad7065571fe2d31dabc37dea81b80640376e18b0679b"),
    "it_802A4454": (0x802A4454, 0x78, "f4d94ac7d0659715feb94ad2b7e7c7cd0fc6ebe878d3b2776d9c79ed5dfe719a"),
    "HSD_Randf": (0x80380528, 0x58, "3459ef5fe054d0eeca21136bbadfe585f93b19f7adf5e09872d4aed0d25ac33e"),
}


def private_sources(directory):
    source = directory / "src"
    shutil.copytree(ROOT / ".deps/melee/src", source)
    subprocess.run(["git", "apply", "--include=src/**",
                    str(ROOT / "patches/melee-gameplay.patch")],
                   cwd=directory, capture_output=True, text=True,
                   check=True, timeout=30)
    for path in source.rglob("*"):
        if path.suffix not in {".c", ".h"}:
            continue
        before = path.read_text()
        after = transform_source(path.relative_to(directory), before)
        if before != after:
            path.write_text(after)
    return source


class SamusGrappleMathTests(unittest.TestCase):
    def test_production_chain_arithmetic(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc.py"
        if not compiler.is_file() or not (ROOT / ".deps/melee/src").is_dir():
            self.skipTest("Pinned Melee source and project SDK required")
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="pr86-samus-math-") as temporary:
            directory = Path(temporary)
            source = private_sources(directory)
            target = directory / "samus_grapple.js"
            command = [sys.executable, str(compiler), "-O2", "-std=c11",
                       "-DTARGET_PC", "-ffp-contract=off", "-ffunction-sections",
                       "-fdata-sections", "-I", str(ROOT / "src"), "-I", str(source),
                       "-I", str(ROOT / ".deps/aurora/include"),
                       "-include", str(ROOT / "src/gameplay_compat.h"),
                       str(ROOT / "tests/samus_grapple_math_trace.c"),
                       str(source / "melee/it/kinds/itsamusgrapple.c"),
                       str(source / "melee/it/kinds/itlinkhookshot.c"),
                       str(source / "sysdolphin/baselib/random.c"),
                       "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)]
            built = subprocess.run(command, env=env, capture_output=True,
                                   text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(node_runtime()), str(target)],
                                 capture_output=True, text=True, timeout=15)
            print(run.stdout, end="")
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("cases=1024 source_differences=0 helper_differences=0", run.stdout)

    def test_owned_dol_body_and_operation_profile(self):
        configured = os.environ.get("MELEE_CPU_DOL")
        if not configured:
            self.skipTest("owned DOL not configured (MELEE_CPU_DOL)")
        sys.path.insert(0, str(ROOT / "tools"))
        from retail_allocation_profile import Dol, read_symbols
        dol = Dol(Path(configured))  # verifies the full GALE01r2 DOL SHA-1
        symbols = read_symbols(ROOT / ".deps/melee/config/GALE01/symbols.txt")
        for name, (address, size, digest) in BODIES.items():
            with self.subTest(body=name):
                self.assertEqual((symbols[name]["address"], symbols[name]["size"]),
                                 (address, size))
                self.assertEqual(hashlib.sha256(dol.read(address, size)).hexdigest(), digest)

        def word(address):
            return struct.unpack(">I", dol.read(address, 4))[0]

        # __init_registers: lis r2,0x804d; ori r2,r2,0xf9e0.
        r2 = ((word(0x80005348) & 0xFFFF) << 16) | (word(0x8000534C) & 0xFFFF)
        self.assertEqual(r2, 0x804DF9E0)
        constants = {
            -0x2928: ("d", 0.9), -0x2920: ("d", 0.0),
            -0x2918: ("f", 0.6), -0x2948: ("f", 0.0),
            -0x2BF0: ("d", 0.5), -0x2BE8: ("d", 3.0),
            -0x2BE0: ("d", 0.0), -0x2C20: ("d", 1.0), -0x2C28: ("f", 0.0),
        }
        for offset, (kind, value) in constants.items():
            self.assertEqual(dol.read(r2 + offset, struct.calcsize(kind)),
                             struct.pack(">" + kind, value))

        # A-form fields: opcode, destination, A, C, B, XO; no disassembler needed.
        def arithmetic(address):
            w = word(address)
            return (w >> 26, (w >> 21) & 31, (w >> 16) & 31,
                    (w >> 6) & 31, (w >> 11) & 31, (w >> 1) & 31)

        placement = {
            0x802B9060: (1, 28), 0x802B9070: (1, 28), 0x802B9080: (1, 28),
            0x802B910C: (1, 2), 0x802B9120: (2, 1), 0x802B9134: (2, 1),
            0x802B9154: (1, 2), 0x802B9168: (2, 1), 0x802B917C: (2, 1),
            0x802BA09C: (1, 2), 0x802BA0B0: (2, 1), 0x802BA0C4: (2, 1),
            0x802BA104: (1, 2), 0x802BA118: (2, 1), 0x802BA12C: (2, 1),
        }
        for address, (a, c) in placement.items():
            self.assertEqual(arithmetic(address), (59, 0, a, c, 0, 29))  # fmadds
        for address in (0x802B90B4, 0x802BA04C):
            self.assertEqual(arithmetic(address), (59, 1, 31, 1, 28, 30))  # fnmsubs
        for address in (0x802B90C0, 0x802BA058):
            self.assertEqual(arithmetic(address), (59, 1, 31, 1, 28, 29))
        # Other callers of the same gravity inline: two sites per body.
        for negative, positive, a, b in (
                (0x802B9A88, 0x802B9A98, 0, 29),
                (0x802B9B50, 0x802B9B5C, 31, 28),
                (0x802B9DEC, 0x802B9DFC, 0, 29),
                (0x802B9E9C, 0x802B9EA8, 31, 28)):
            self.assertEqual(arithmetic(negative), (59, 1, a, 1, b, 30))
            self.assertEqual(arithmetic(positive), (59, 1, a, 1, b, 29))
        for address in (0x802B9054, 0x802B90F4, 0x802BA084, 0x802BA0E4):
            w = word(address)
            self.assertEqual(w >> 26, 18)
            self.assertEqual(w & 3, 1)  # relative bl
            displacement = w & 0x03FFFFFC
            if displacement & 0x02000000:
                displacement -= 0x04000000
            self.assertEqual(address + displacement, 0x802A3C98)
        # Standalone helper uses rounded products and sums, NOT the Link inline FMA sum.
        for address in (0x802A3CD8, 0x802A3CDC, 0x802A3CE0):
            self.assertEqual((word(address) >> 26, (word(address) >> 1) & 31), (59, 25))
        for address in (0x802A3CE8, 0x802A3CEC):
            self.assertEqual((word(address) >> 26, (word(address) >> 1) & 31), (59, 21))
        for address in (0x802A3D0C, 0x802A3D1C, 0x802A3D2C):
            self.assertEqual(arithmetic(address), (63, 0, 1, 0, 3, 30))


if __name__ == "__main__":
    unittest.main()
