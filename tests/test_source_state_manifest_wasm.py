import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.source_state_manifest import _parse_map


ROOT = Path(__file__).resolve().parents[1]
EMSDK = ROOT / ".deps/emsdk"
EMCC = EMSDK / "upstream/emscripten/emcc"
EMAR = EMSDK / "upstream/emscripten/emar"
NODE = next(iter(sorted(EMSDK.glob("node/*/bin/node"))), None)


@unittest.skipUnless(EMCC.is_file() and EMAR.is_file() and NODE and NODE.is_file(),
                     "pinned Emscripten and Node are not installed")
class SourceStateWasmVmaTests(unittest.TestCase):
    def test_optimized_wasm_addresses_match_lld_map_and_linear_memory(self):
        fixture = ROOT / "tests/fixtures/source_state_manifest"
        env = os.environ.copy()
        env.update({
            "EMSDK": str(EMSDK),
            "EM_CONFIG": str(EMSDK / ".emscripten"),
            "EM_CACHE": str(EMSDK / "upstream/emscripten/cache"),
            "EMSDK_PYTHON": sys.executable,
            "PATH": os.pathsep.join((str(NODE.parent), env.get("PATH", ""))),
        })
        with tempfile.TemporaryDirectory(prefix="source-state-wasm-vma-") as directory:
            output = Path(directory)
            object_file = output / "fighter.c.o"
            archive = output / "libfighter_source_runtime.a"
            link_map = output / "probe.map"
            module_path = output / "probe.mjs"
            self._run([
                str(EMCC), "-O2", "-g", "-ffunction-sections", "-fdata-sections",
                "-c", str(fixture / "source_root/melee/fighter.c"), "-o", str(object_file),
            ], env)
            self._run([str(EMAR), "rcs", str(archive), str(object_file)], env)
            self._run([
                str(EMCC), "-O2", "-g", "--no-entry",
                "-sASYNCIFY=1", "-sSAFE_HEAP=1", "-sALLOW_MEMORY_GROWTH=1",
                "-sENVIRONMENT=node", "-sMODULARIZE=1", "-sEXPORT_NAME=createProbe",
                "-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32",
                "-sEXPORTED_FUNCTIONS=_probe_source_initialized_address,_probe_source_zero_address",
                f"-Wl,--Map={link_map}", str(fixture / "probe.c"), str(archive),
                "-o", str(module_path),
            ], env)

            rows = _parse_map(link_map)
            addresses = {
                row["section"]: row["address"]
                for row in rows
                if row["section"] in {".data.source_initialized", ".bss.source_zero"}
            }
            self.assertEqual(set(addresses), {".data.source_initialized", ".bss.source_zero"})
            check = output / "check.mjs"
            check.write_text(
                "import createProbe from './probe.mjs';\n"
                "const m = await createProbe();\n"
                "const initialized = m._probe_source_initialized_address();\n"
                "const zero = m._probe_source_zero_address();\n"
                f"if (initialized !== 0x{addresses['.data.source_initialized']:x} || "
                f"zero !== 0x{addresses['.bss.source_zero']:x}) throw new Error('map VMA mismatch');\n"
                "if (m.HEAPU32[initialized >>> 2] !== 0x12345678 || "
                "m.HEAPU32[zero >>> 2] !== 0) throw new Error('linear memory mismatch');\n",
                encoding="utf-8",
            )
            self._run([str(NODE), str(check)], env)

    def _run(self, command, env):
        result = subprocess.run(command, cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode:
            self.fail(
                f"command failed ({result.returncode}): {command!r}\n"
                f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
            )


if __name__ == "__main__":
    unittest.main()
