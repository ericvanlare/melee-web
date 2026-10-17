"""Focused synthetic and real-glue controls for the private Wasm accessor."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))

from first_pair_test_support import pinned_node_or_skip
from owned_test_workspace import OwnedWorkspaceTests


class WasmInstanceOwnershipTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "wasm-instance-ownership-")
        cls.log = cls.work / "wasm_instance_ownership.log"
        cls.log.write_text("", encoding="utf-8")

    def _run(self, label, argv, env, timeout=30):
        result = subprocess.run([str(value) for value in argv], cwd=ROOT, env=env,
                                capture_output=True, text=True, timeout=timeout)
        with self.log.open("a", encoding="utf-8") as log:
            log.write(f"\n=== {label} ===\n$ {' '.join(map(str, argv))}\n")
            log.write(f"returncode={result.returncode}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}\n")
        self.assertEqual(result.returncode, 0,
                         f"{label} failed; see retained workspace log {self.log}\n"
                         f"{result.stdout}{result.stderr}")
        return result

    def _environment(self, node):
        emsdk = ROOT / ".deps/emsdk"
        if not (emsdk / ".emscripten").is_file():
            self.skipTest("Pinned Emscripten SDK configuration unavailable")
        env = dict(os.environ)
        env["EMSDK"] = str(emsdk)
        env["EM_CONFIG"] = str(emsdk / ".emscripten")
        env["EM_CACHE"] = str(emsdk / "upstream/emscripten/cache")
        env["EMSDK_PYTHON"] = sys.executable
        env["TMPDIR"] = str(self.work)
        env["PATH"] = str(Path(node).parent) + os.pathsep + env.get("PATH", "")
        return emsdk, env

    def test_reviewed_accessor_rejects_invalid_identity_and_state(self):
        node = pinned_node_or_skip(self, ROOT)
        result = self._run(
            "run reviewed synthetic accessor controls",
            [node, ROOT / "tests/test_wasm_ownership_accessor_v1.mjs"],
            dict(os.environ),
        )
        self.assertIn("private Wasm ownership controls passed", result.stdout)
        self.assertIn("Asyncify", result.stdout)
        self.assertIn("table/memory", result.stdout)
        self.assertIn("tempRet0", result.stdout)

    def test_pinned_emscripten_glue_fixture_and_early_refusals(self):
        emcc = ROOT / ".deps/emsdk/upstream/emscripten/emcc"
        if not emcc.is_file():
            self.skipTest("Pinned Emscripten compiler unavailable")
        node = pinned_node_or_skip(self, ROOT)
        emsdk, env = self._environment(node)
        sys.path.insert(0, str(ROOT / "scripts"))
        from bootstrap import read_lock

        version = self._run("read pinned Emscripten compiler version", [emcc, "--version"], env)
        self.assertIn(read_lock(ROOT)["emscripten"], version.stdout)

        output_js = self.work / "wasm-ownership-fixture.js"
        source = ROOT / "tests/quiescent_wasm_ownership_fixture.c"
        library = ROOT / "src/melee_web_wasm_ownership_library.js"
        self.assertTrue(library.is_file(), "private JS library source is part of this source patch")
        compile_argv = [
            emcc, "-O3", "-DNDEBUG", "-sASSERTIONS=0", "-sSAFE_HEAP=0",
            "-sENVIRONMENT=node", "-sASYNCIFY=1", "-sEXIT_RUNTIME=0",
            "-sALLOW_MEMORY_GROWTH=1", "-sSTACK_SIZE=8388608",
            "-sEXPORTED_RUNTIME_METHODS=HEAPU8,stackSave,stackRestore,getTempRet0,setTempRet0",
            "-sEXPORTED_FUNCTIONS=_main,_wasm_ownership_fixture_phase,_wasm_ownership_fixture_ping",
            f"--js-library={library}", "-o", output_js, source,
        ]
        self._run("compile small actual Emscripten fixture", compile_argv, env, timeout=60)
        run = self._run(
            "run generated Asyncify glue and setup refusal controls",
            [node, ROOT / "tests/quiescent_wasm_ownership_glue_fixture.mjs",
             output_js, self.work / "wasm-ownership-fixture.wasm"],
            env,
            timeout=20,
        )
        report = json.loads(run.stdout)
        self.assertEqual(report["schema"], "melee-web-wasm-instance-ownership-glue-fixture-v2")
        self.assertEqual([row["shape"] for row in report["reports"]], ["function", "array"])
        for row in report["reports"]:
            self.assertTrue(row["runtimeInitialized"])
            self.assertGreater(row["asyncifyWriterCalls"], 0)
            self.assertGreater(row["tableLength"], 0)
        self.assertEqual([row["kind"] for row in report["refusals"]],
                         ["missing-installer", "custom-instantiate"])
        for row in report["refusals"]:
            self.assertEqual(row["state"], "refused")
            self.assertFalse(row["runtimeInitialized"])
            self.assertFalse(row["mainReached"])


if __name__ == "__main__":
    unittest.main()
