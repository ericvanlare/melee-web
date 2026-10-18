# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused native and Wasm controls for isolated UCF history storage."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime

EXPECTED_STDOUT = (
    "SNAPSHOT|"
    "007f80ff010203040001a1a2"
    "11223344556677880102a3a4"
    "908172635445362702fea5a6"
    "f0e1d2c3b4a5968703ffa7a8\n"
    "PASS ucf-input-history: 48-byte snapshots, four rows, transactional guards\n"
)


def _run_captured(out, name, command, *, env=None, input_text=None, timeout=180):
    result = subprocess.run(
        command, cwd=ROOT, text=True, capture_output=True, input=input_text,
        env=env, timeout=timeout,
    )
    (out / f"{name}.stdout").write_text(result.stdout, encoding="utf-8")
    (out / f"{name}.stderr").write_text(result.stderr, encoding="utf-8")
    if result.returncode:
        raise AssertionError(
            f"{name} failed with exit {result.returncode}; retained {out}: {result.stderr}"
        )
    return result.stdout


class UcfInputHistoryTests(unittest.TestCase):
    def test_isolated_history_native_and_wasm_controls(self):
        evidence = Path(os.environ.get("MELEE_WEB_TEST_OUTPUT", ROOT / "work"))
        evidence.mkdir(parents=True, exist_ok=True)
        out = Path(tempfile.mkdtemp(prefix="ucf-input-history-controls-", dir=evidence))
        implementation = ROOT / "src/slippi_profile/ucf_input_history.c"
        fixture = ROOT / "src/slippi_profile/tests/ucf_input_history_fixture.c"
        source = [str(implementation), str(fixture)]
        common = [
            "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-fno-fast-math", "-ffp-contract=off", "-I", str(ROOT / "src"),
        ]
        try:
            compiler = shlex.split(os.environ.get("CC", "cc"))
            native = out / "native"
            native_compile = [
                os.environ.get("PYTHON", "python3"),
                str(ROOT / "scripts/agent_workspace.py"), "run", "--",
                *compiler, *common, "-fsanitize=undefined",
                "-fno-sanitize-recover=undefined", *source, "-o", str(native),
            ]
            _run_captured(out, "native-compile", native_compile)
            native_result = _run_captured(out, "native-run", [str(native)])

            sdk = ROOT / ".deps/emsdk"
            env = dict(
                os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                EM_CACHE=str(sdk / "upstream/emscripten/cache"),
                EMSDK_PYTHON=sys.executable,
            )
            emcc = [sys.executable, str(sdk / "upstream/emscripten/emcc.py")]
            wasm = out / "ucf-input-history.js"
            wasm_compile = [
                os.environ.get("PYTHON", "python3"),
                str(ROOT / "scripts/agent_workspace.py"), "run", "--",
                *emcc, *common, "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                "-sASSERTIONS=2", *source, "-o", str(wasm),
            ]
            _run_captured(out, "wasm-compile", wasm_compile, env=env)
            wasm_result = _run_captured(
                out, "wasm-run", [str(node_runtime()), str(wasm)], env=env,
            )
            if native_result != wasm_result:
                raise AssertionError(
                    f"native and Wasm outputs differ; retained {out}: "
                    f"native={native_result!r}, wasm={wasm_result!r}"
                )
            if native_result != EXPECTED_STDOUT:
                raise AssertionError(
                    f"focused fixture emitted an unexpected snapshot vector; retained {out}: "
                    f"actual={native_result!r}, expected={EXPECTED_STDOUT!r}"
                )
        except Exception as error:
            self.fail(f"UCF input-history controls failed: {error}; retained reproducer: {out}")
        else:
            shutil.rmtree(out)


if __name__ == "__main__":
    unittest.main()
