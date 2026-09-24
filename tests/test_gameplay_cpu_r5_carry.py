"""Focused typed CPU r5 carry checks; no browser or retail input admission."""
from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_RANDOM_SHA256 = "c1ee9c4317a48b59021c23b901e97fe7ad794f148d6ab98783ecfcadcdd566e6"
sys.path.insert(0, str(ROOT / "tools"))
from cpu_r5_source_context import derive_owned_seed_binding


def run(command: list[str], *, cwd: Path = ROOT, timeout: int = 60,
        env: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=timeout)
    if result.returncode:
        raise AssertionError(f"{command!r} failed:\n{result.stdout}\n{result.stderr}")
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


class CpuR5CarryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which(os.environ.get("CC", "cc"))
        if not cls.cc:
            raise unittest.SkipTest("C compiler unavailable")
        configured_source = os.environ.get("MELEE_PINNED_SOURCE")
        cls.melee = Path(configured_source) if configured_source else ROOT / ".deps/melee"
        cls.original_random = cls.melee / "src/sysdolphin/baselib/random.c"
        if not cls.original_random.is_file():
            raise unittest.SkipTest("set MELEE_PINNED_SOURCE to the pinned source checkout")
        cls.original_random_sha256 = sha256(cls.original_random)
        if cls.original_random_sha256 != EXPECTED_RANDOM_SHA256:
            raise AssertionError("pinned random.c hash differs from the reviewed source")
        cls.owned_binding_used = False
        cls.owned_binding_provenance = None
        evidence_root = Path(os.environ.get(
            "MELEE_CPU_R5_EVIDENCE_DIR", str(ROOT.parent / "cpu-r5-carry-evidence")))
        evidence_root.mkdir(parents=True, exist_ok=True)
        cls.evidence = Path(tempfile.mkdtemp(prefix="run-", dir=evidence_root))
        cls.temp = tempfile.TemporaryDirectory(prefix="cpu-r5-carry ")
        cls.directory = Path(cls.temp.name)
        cls.native = cls.directory / "carry"
        cls._run_recorded([cls.cc, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                           "-ffp-contract=off", "-Isrc",
                           "src/gameplay_cpu_r5_carry.c", "tests/gameplay_cpu_r5_carry_trace.c",
                           "-o", str(cls.native)], "native-build", artifacts=(cls.native,))
        cls.source = None
        cls.source_compile_error = None
        sdk = Path(os.environ.get("MELEE_EMSDK", str(ROOT / ".deps/emsdk")))
        emcc = sdk / "upstream/emscripten/emcc.py"
        if emcc.is_file():
            cls.source = cls.directory / "source-carry.js"
            cls.source_wrapper = cls.directory / "random_wrapper.c"
            cls.source_wrapper.write_text(
                '#include <stdio.h>\n'
                '#pragma clang diagnostic push\n'
                '#pragma clang diagnostic ignored "-Warray-parameter"\n'
                '#include "original_startup_compat.h"\n'
                '#pragma clang diagnostic pop\n'
                f'#include "{cls.original_random}"\n'
            )
            setting = next(
                ast.literal_eval(statement.value)
                for statement in ast.parse((sdk / ".emscripten").read_text()).body
                if isinstance(statement, ast.Assign)
                and any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                        for target in statement.targets)
            )
            node = Path(setting.replace("$CFGDIR", str(sdk))).resolve()
            if not node.is_relative_to(sdk.resolve()):
                raise AssertionError("pinned Emscripten Node escapes the SDK")
            env = dict(os.environ, EM_CONFIG=str(sdk / ".emscripten"),
                       EM_CACHE=str(sdk / "upstream/emscripten/cache"),
                       EMSDK=str(sdk), EMSDK_PYTHON=sys.executable)
            cls._run_recorded(
                [sys.executable, str(emcc), "-std=gnu11", "-O1", "-Wall", "-Werror",
                 "-ffp-contract=off", "-Isrc",
                 "-I", str(cls.melee / "src"),
                 "-I", str(cls.melee / "extern/dolphin/include"),
                 "src/gameplay_cpu_r5_carry.c", "tests/gameplay_cpu_r5_source_trace.c",
                 str(cls.source_wrapper), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                 "-sASSERTIONS=2", "-sSAFE_HEAP=1", "-o", str(cls.source)],
                "wasm-build", env=env,
                artifacts=(cls.source, cls.source.with_suffix(".wasm"),
                           cls.source_wrapper))
            cls.source_node = node
            cls.source_env = env
        else:
            cls.source_compile_error = "pinned Emscripten emcc.py unavailable"

    @classmethod
    def tearDownClass(cls):
        if sha256(cls.original_random) != EXPECTED_RANDOM_SHA256:
            raise AssertionError("pinned random.c changed during the source test")
        (cls.evidence / "receipt.json").write_text(json.dumps({
            "schema": "melee-web-cpu-r5-carry-test",
            "version": 2,
            "random_source_sha256": cls.original_random_sha256,
            "random_source_sha256_after": sha256(cls.original_random),
            "source_wasm_compiled": cls.source is not None,
            "synthetic_fighter_identity": True,
            "independently_derived_seed": cls.owned_binding_used,
            "retail_provenance": False,
            "owned_seed_provenance": cls.owned_binding_provenance,
        }, indent=2) + "\n")
        cls.temp.cleanup()

    @classmethod
    def _run_recorded(cls, command, label, *, env=None, artifacts=()):
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                    text=True, timeout=120)
            stdout, stderr, returncode = result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired as error:
            stdout = error.stdout or ""
            stderr = error.stderr or ""
            returncode = "timeout"
        (cls.evidence / f"{label}.command.json").write_text(
            json.dumps([str(part) for part in command], indent=2) + "\n")
        (cls.evidence / f"{label}.stdout").write_text(
            stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout)
        (cls.evidence / f"{label}.stderr").write_text(
            stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr)
        (cls.evidence / f"{label}.returncode").write_text(f"{returncode}\n")
        for artifact in artifacts:
            artifact = Path(artifact)
            if artifact.is_file():
                shutil.copy2(artifact, cls.evidence / f"{label}-{artifact.name}")
        if returncode:
            raise AssertionError(f"{command!r} failed:\n{stdout}\n{stderr}")
        return stdout

    def test_typed_lifetime_and_fail_closed_consumer(self):
        self.assertEqual(self._run_recorded([str(self.native)], "native-run").strip(),
                         "cpu r5 carry trace: passed")

    def test_original_random_source_publishes_seed_route(self):
        if self.source is None:
            self.skipTest(f"pinned Wasm32 source compile unavailable: {self.source_compile_error}")
        configured = {
            name: os.environ.get(name)
            for name in ("MELEE_CPU_DOL", "MELEE_CPU_DISC",
                         "MELEE_CPU_SYMBOLS", "MELEE_CPU_SOURCE_ROOT")
        }
        if all(configured.values()):
            binding = derive_owned_seed_binding(
                dol_path=Path(configured["MELEE_CPU_DOL"]),
                disc_path=Path(configured["MELEE_CPU_DISC"]),
                symbols_path=Path(configured["MELEE_CPU_SYMBOLS"]),
                source_root=Path(configured["MELEE_CPU_SOURCE_ROOT"]),
            )
            self.assertTrue(binding["independently_derived"])
            self.__class__.owned_binding_used = True
            self.__class__.owned_binding_provenance = binding["provenance"]
        elif any(configured.values()):
            self.fail("all four owned CPU provenance inputs are required together")
        else:
            # This still executes the untouched source random.c in Wasm32,
            # but the relocated words are synthetic and cannot establish
            # retail provenance. The owned adapter test is the provenance
            # gate when DOL/disc inputs are configured.
            binding = {"source_word": 0x811230A4,
                       "global_address": 0x81234010}
        source_word = binding["source_word"]
        output = self._run_recorded(
            [str(self.source_node), str(self.source), hex(source_word),
             hex(binding["global_address"]), "0x81234540"],
            "wasm-run", env=self.source_env)
        self.assertEqual(output.strip(), "original random source -> cpu carry: passed")

    def test_no_tracked_numeric_retail_input(self):
        for path in (ROOT / "src/gameplay_cpu_r5_carry.h",
                     ROOT / "src/gameplay_cpu_r5_carry.c",
                     ROOT / "tests/gameplay_cpu_r5_source_trace.c"):
            text = path.read_text()
            self.assertNotIn("804D5F90", text)
            self.assertNotIn("804d5f90", text)
            self.assertNotIn("90,40", text)


if __name__ == "__main__":
    unittest.main()
