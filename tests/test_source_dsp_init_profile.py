"""Run the pinned Dolphin DSP task protocol through the original source bodies.

This is a checked-Wasm source-profile fixture, not a browser audio test.  The
fixture supplies only the hardware mailbox/interrupt boundary needed by the
original DSP driver.  It rejects an altered AX image or mail sequence before
the original init callback can report success.
"""
from __future__ import annotations

import os
import hashlib
import json
from pathlib import Path
import ast
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / ".deps" / "emsdk"
DSP_SOURCE = ROOT / ".deps" / "melee" / "extern" / "dolphin" / "src" / "dolphin"
DSP_DIR = DSP_SOURCE / "dsp"
OS_INTERRUPT_SOURCE = DSP_SOURCE / "os" / "OSInterrupt.c"
DOLPHIN_INCLUDE = ROOT / ".deps" / "melee" / "extern" / "dolphin" / "include"


def _generated_source(directory: Path) -> Path:
    dsp_c = (DSP_DIR / "dsp.c").read_text()
    dsp_task_c = (DSP_DIR / "dsp_task.c").read_text()
    dsp_code_c = (ROOT / ".deps/melee/extern/dolphin/src/dolphin/ax/DSPCode.c").read_text()
    interrupt_c = OS_INTERRUPT_SOURCE.read_text()
    set_start = interrupt_c.index("static u32 SetInterruptMask")
    set_end = interrupt_c.index("\nOSInterruptMask OSGetInterruptMask", set_start)
    unmask_start = interrupt_c.index("OSInterruptMask __OSUnmaskInterrupts", set_end)
    unmask_end = interrupt_c.index("\nvoid __OSDispatchInterrupt", unmask_start)
    interrupt_source = interrupt_c[set_start:set_end] + "\n\n" + interrupt_c[unmask_start:unmask_end]
    if interrupt_source.count("static u32 SetInterruptMask") != 1:
        raise AssertionError("SetInterruptMask extraction is not unique")
    if interrupt_source.count("OSInterruptMask __OSUnmaskInterrupts") != 1:
        raise AssertionError("__OSUnmaskInterrupts extraction is not unique")
    if "OSSetInterruptMask" in interrupt_source or "__OSDispatchInterrupt" in interrupt_source:
        raise AssertionError("interrupt extraction included an unrelated source body")
    provider = Path(__file__).with_name("source_dsp_init_trace.cpp").read_text()
    source = provider.replace(
        "/* MELEE_WEB_PINNED_DSP_SOURCES */",
        dsp_code_c + "\n" + dsp_task_c + "\n" + dsp_c,
    )
    source = source.replace("/* MELEE_WEB_PINNED_OS_INTERRUPT */", interrupt_source)
    output = directory / "source_dsp_init_generated.cpp"
    output.write_text(source)
    return output


class SourceDspInitProfileTests(unittest.TestCase):
    SOURCE_FILES = (
        DSP_DIR / "dsp.c",
        DSP_DIR / "dsp_task.c",
        ROOT / ".deps/melee/extern/dolphin/src/dolphin/ax/DSPCode.c",
        OS_INTERRUPT_SOURCE,
    )
    EXPECTED_SOURCE_HASHES = {
        ".deps/melee/extern/dolphin/src/dolphin/ax/DSPCode.c":
            "8807c54266535396ea190e40e370fa045a3461ddbe52c5a5ef09913e4baaddb9",
        ".deps/melee/extern/dolphin/src/dolphin/dsp/dsp.c":
            "29a6864fb05a78a27e063f305844e2cf4f03af22f724863a14fbcff40ac18aa5",
        ".deps/melee/extern/dolphin/src/dolphin/dsp/dsp_task.c":
            "7b212710530cc6617d15e826c2de56caa59fb6e4bc702730812f50c2fb13964f",
        ".deps/melee/extern/dolphin/src/dolphin/os/OSInterrupt.c":
            "69ba045af10aaede13f77fa1040af7fa6e79658e2d317bcee1afc5992d3fca7e",
    }

    @classmethod
    def setUpClass(cls):
        if not (SDK / "upstream/emscripten/em++.py").is_file():
            raise unittest.SkipTest("local Emscripten SDK unavailable")
        cls.work = ROOT / "work" / "source-dsp-init-profile"
        cls.work.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=cls.work))
        cls.directory = directory
        cls.log = directory / "compile.log"
        cls.source_hashes = {
            str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in cls.SOURCE_FILES
        }
        if cls.source_hashes != cls.EXPECTED_SOURCE_HASHES:
            raise RuntimeError(
                "pinned DSP source hash mismatch: "
                f"expected {cls.EXPECTED_SOURCE_HASHES}, got {cls.source_hashes}"
            )
        source = _generated_source(directory)
        cls.output = directory / "source_dsp_init.js"
        (directory / "source-hashes-before.json").write_text(
            json.dumps(cls.source_hashes, indent=2, sort_keys=True) + "\n"
        )
        setting = next(
            ast.literal_eval(statement.value) for statement in ast.parse(
                (SDK / ".emscripten").read_text()
            ).body
            if isinstance(statement, ast.Assign)
            and any(isinstance(target, ast.Name)
                    and target.id == "NODE_JS" for target in statement.targets)
        )
        cls.node = Path(setting.replace("$CFGDIR", str(SDK))).resolve()
        cls.env = dict(
            os.environ,
            EM_CONFIG=str(SDK / ".emscripten"),
            EM_CACHE=str(SDK / "upstream/emscripten/cache"),
            EMSDK=str(SDK),
            EMSDK_PYTHON=sys.executable,
        )
        # The pinned SDK is legacy C compiled as C++; these four diagnostics
        # are confined to its headers/source.  Fixture code remains -Werror.
        command = [
            sys.executable, str(SDK / "upstream/emscripten/em++.py"),
            "-std=gnu++17", "-O1", "-ffp-contract=off", "-DDEBUG=1",
            "-Wall", "-Wextra", "-Werror", "-Wno-writable-strings",
            "-Wno-array-parameter", "-Wno-unused-parameter",
            "-Wno-unused-variable", "-I",
            str(Path(__file__).with_name("source_dsp_init_include")),
            "-I", str(DOLPHIN_INCLUDE),
            "-I", str(DSP_DIR), "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
            "-sASSERTIONS=2", "-sSAFE_HEAP=1", str(source), "-o", str(cls.output),
        ]
        try:
            result = subprocess.run(command, cwd=ROOT, env=cls.env,
                                    capture_output=True, text=True, timeout=180)
            cls.log.write_text("$ " + " ".join(command) + "\n" + result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f"source DSP fixture compile failed; see {cls.log}")
        except subprocess.TimeoutExpired as failure:
            cls.log.write_text(
                "$ " + " ".join(command) + "\ncompile timeout\n"
                f"stdout={failure.stdout!r}\nstderr={failure.stderr!r}\n"
            )
            raise

    @classmethod
    def tearDownClass(cls):
        after = {
            path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
            for path in cls.source_hashes
        }
        (cls.directory / "source-hashes-after.json").write_text(
            json.dumps(after, indent=2, sort_keys=True) + "\n"
        )
        for path, digest in cls.source_hashes.items():
            actual = after[path]
            if actual != digest:
                raise AssertionError(f"pinned DSP source changed: {path}")

    def _run_and_retain(self, args, name):
        try:
            result = subprocess.run(args, cwd=ROOT, env=self.env,
                                    capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            (self.directory / f"{name}.timeout").write_text(repr(failure) + "\n")
            raise
        (self.directory / f"{name}.stdout").write_text(result.stdout)
        (self.directory / f"{name}.stderr").write_text(result.stderr)
        return result

    def test_original_dsp_init_task_and_callback(self):
        result = self._run_and_retain([str(self.node), str(self.output)], "valid")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("source DSP init profile: passed image_bytes=6624 ector_hash=4e8a8b21 mails=10 callback=1",
                      result.stdout)

    def test_rejects_unowned_image_mail_order_and_span(self):
        expected = {
            "bad-image-pointer": "AX image pointer is not the owned image",
            "bad-image-word": "AX image does not match the pinned Dolphin HLE profile",
            "bad-order": "DSP host mailbox low word arrived first",
            "bad-mail": "Failed to sync DSP on boot",
            "bad-register": "DSP source accessed an unsupported MMIO register",
            "bad-status": "DSP control/status wrote an unsupported hardware bit",
            "bad-unmask": "DSPInit changed the DSP interrupt mask",
            "bad-span": "wrong AX image length",
            "masked-pump": "DSP interrupt dispatched while masked",
            "repeat-pump": "DSP init interrupt was not queued",
            "stall-from": "DSP mailbox poll bound exceeded",
            "stall-to": "DSP mailbox poll bound exceeded",
            "unknown-mode": "unknown DSP fixture mode",
        }
        for mode, message in expected.items():
            with self.subTest(mode=mode):
                result = self._run_and_retain(
                    [str(self.node), str(self.output), mode], mode
                )
                self.assertNotEqual(result.returncode, 0,
                                    f"invalid {mode} was accepted:\n{result.stdout}\n{result.stderr}")
                self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
