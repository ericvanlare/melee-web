"""Run the original SDK ARQ queue against a bounded deferred host backend."""

from __future__ import annotations

import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
ORIGINAL = ROOT / ".deps/melee/extern/dolphin/src/dolphin/ar/arq.c"
SDK = ROOT / ".deps/emsdk"


def node_from_config():
    config = SDK / ".emscripten"
    for statement in ast.parse(config.read_text()).body:
        if not isinstance(statement, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                   for target in statement.targets):
            continue
        setting = ast.literal_eval(statement.value)
        node = Path(setting.replace("$CFGDIR", str(SDK))).resolve()
        if not node.is_relative_to(SDK.resolve()):
            raise RuntimeError("Expected project-local Node runtime")
        return node
    raise RuntimeError("Pinned Emscripten config has no NODE_JS")


class SourceArqCompletionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not ORIGINAL.is_file() or not (SDK / "upstream/emscripten/emcc.py").is_file():
            raise unittest.SkipTest("pinned Emscripten and original ARQ source required")
        cls.source_hash = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
        cls.temp = tempfile.TemporaryDirectory(prefix="source arq completion ")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.oracle = cls.directory / "oracle.js"
        cls.env = dict(
            os.environ,
            EM_CONFIG=str(SDK / ".emscripten"),
            EM_CACHE=str(SDK / "upstream/emscripten/cache"),
            EMSDK=str(SDK),
            EMSDK_PYTHON=sys.executable,
        )
        result = subprocess.run([
            sys.executable, str(SDK / "upstream/emscripten/emcc.py"),
            "-std=gnu11", "-O1", "-ffunction-sections", "-fdata-sections",
            "-Itests/source_arq_oracle_include", "-Isrc",
            "tests/source_arq_oracle.c", "src/source_arq_context.c",
            "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
            "-sSAFE_HEAP=1", "-sERROR_ON_UNDEFINED_SYMBOLS=1",
            "-Wl,--gc-sections", "-o", str(cls.oracle),
        ], cwd=ROOT, env=cls.env, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"ARQ oracle compile failed:\n{result.stdout}\n{result.stderr}")
        cls.node = node_from_config()

    @classmethod
    def tearDownClass(cls):
        if ORIGINAL.is_file():
            actual = hashlib.sha256(ORIGINAL.read_bytes()).hexdigest()
            if actual != cls.source_hash:
                raise AssertionError("original arq.c changed during the test")

    def run_oracle(self, *args, check=True):
        result = subprocess.run(
            [str(self.node), str(self.oracle), *args],
            cwd=ROOT, env=self.env, capture_output=True, text=True, timeout=30,
        )
        if check and result.returncode:
            self.fail(f"oracle failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")
        return result

    def test_original_queue_defers_copy_and_preserves_source_order(self):
        result = self.run_oracle("run")
        receipt = json.loads(result.stdout)
        self.assertEqual(receipt["schema"], "melee-source-arq-completion")
        self.assertTrue(receipt["deferred"])
        self.assertEqual(
            set(receipt["scenarios"]),
            {"priority_chunking", "cancellation", "roundtrip", "reentry",
             "lifetime_guards", "nested_pump", "interrupt_mask"},
        )
        self.assertEqual(result.stderr, "")

    def test_backend_rejects_unsupported_or_unowned_requests(self):
        for case in ("missing-span", "bad-alignment", "busy", "unknown-direction", "bad-length",
                     "missing-callback", "host-alias-aram", "host-overlap-spans",
                     "source-queue-unknown"):
            with self.subTest(case=case):
                result = self.run_oracle("invalid", case, check=False)
                self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
