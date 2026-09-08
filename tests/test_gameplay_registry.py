"""Run the original fighter registry reset without console global adjacency."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from gameplay_sources import prepare_sources


class GameplayRegistryTests(unittest.TestCase):
    def test_original_reset_preserves_unrelated_fields_and_rejects_old_layout(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        if not (compiler / "emcc.py").is_file():
            self.skipTest("Pinned SDK unavailable; run bootstrap")
        node = node_runtime()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EM_CACHE=str(compiler / "cache"), EMSDK_PYTHON=sys.executable)
        source = prepare_sources(ROOT)
        # Check the pinned original linker evidence for the three named arrays.
        symbols = (ROOT / ".deps/melee/config/GALE01/symbols.txt").read_text()
        for name, address in (("CostumeListsForeachCharacter", "803C0EC0"),
                              ("ftData_Table_Unk0", "803C0FC8"),
                              ("ftData_UnkIntPairs", "803C25F4"),
                              ("ft_8045993C", "8045993C")):
            self.assertRegex(symbols, rf"{name} = \.(?:data|bss):0x{address};")
        with tempfile.TemporaryDirectory(prefix="melee registry reset ") as directory:
            for label, include, succeeds in (("fixed", source, True),
                    ("original", ROOT / ".deps/melee/src", False)):
                with self.subTest(implementation=label):
                    output = Path(directory) / (label + ".js")
                    result = subprocess.run([sys.executable, str(compiler / "emcc.py"),
                        "-O1", "-std=c11", "-DTARGET_PC", "-ffunction-sections", "-fdata-sections",
                        "-ffp-contract=off", "-I", str(ROOT / "src"), "-I", str(include),
                        "-I", str(ROOT / ".deps/aurora/include"), "-include", str(ROOT / "src/gameplay_compat.h"),
                        str(ROOT / "tests/gameplay_registry_trace.c"), "-sENVIRONMENT=node",
                        "-sEXIT_RUNTIME=1", "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-o", str(output)],
                        cwd=directory, env=env, capture_output=True, text=True, timeout=120)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(node), str(output)], cwd=directory, env=env,
                                            capture_output=True, text=True, timeout=30)
                    if succeeds:
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn("Original fighter registry reset and preserved fields: passed", result.stdout)
                    else:
                        self.assertNotEqual(result.returncode, 0, "Unpatched adjacency unexpectedly succeeded")


if __name__ == "__main__":
    unittest.main()
