"""Compile and run the bounded original AIInit oracle prototype.

The test generates a checked translation unit from the pinned, untouched
Dolphin ai.c, replacing only the unreachable PowerPC callback-stack assembly.
All compiler, runtime, and source-hash evidence is retained in a fresh ignored
work directory for each invocation.
"""

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
SDK = ROOT / ".deps/emsdk"
AI_SOURCE = ROOT / ".deps/melee/extern/dolphin/src/dolphin/ai/ai.c"
OS_INTERRUPT_SOURCE = ROOT / ".deps/melee/extern/dolphin/src/dolphin/os/OSInterrupt.c"
EMXX = SDK / "upstream/emscripten/em++.py"
PROVIDER = ROOT / "tests/source_ai_init_profile.cpp"
SOURCE_PATCH = ROOT / "patches/source-ai-callback-stack.patch"
# This is the pinned source identity, checked before and after generation.
KNOWN_AI_SOURCE_SHA256 = "cb8508338aa0c4b10b9134f2f51936618929fe8f63515e137a5ff2f2d4fcc0d9"
KNOWN_OS_INTERRUPT_SOURCE_SHA256 = "69ba045af10aaede13f77fa1040af7fa6e79658e2d317bcee1afc5992d3fca7e"


def _configured_node() -> Path | None:
    config = SDK / ".emscripten"
    if not config.is_file():
        return None
    tree = ast.parse(config.read_text(), filename=str(config))
    for statement in tree.body:
        if not isinstance(statement, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == "NODE_JS"
                   for target in statement.targets):
            continue
        value = ast.literal_eval(statement.value)
        if not isinstance(value, str):
            return None
        return Path(value.replace("$CFGDIR", str(SDK))).resolve()
    return None


NODE = _configured_node()


def _environment() -> dict[str, str]:
    env = dict(os.environ)
    env.update(
        {
            "EMSDK": str(SDK),
            "EM_CONFIG": str(SDK / ".emscripten"),
            "EM_CACHE": str(SDK / "upstream/emscripten/cache"),
            "EMSDK_PYTHON": sys.executable,
        }
    )
    return env


def _source_transform(source: str) -> str:
    """Remove only the compiler-incompatible PPC callback-stack body."""

    start = source.index("static asm void __AICallbackStackSwitch")
    end_marker = "\n}\n\nvoid __AI_SRC_INIT"
    end = source.index(end_marker, start) + 2
    replacement = """static void __AICallbackStackSwitch(AIDCallback cb)
{
    (void) cb;
    OSPanic(__FILE__, 0x344, \"AI callback stack is unsupported by this oracle\");
}
"""
    transformed = source[:start] + replacement + source[end:]
    transformed = transformed.replace(
        "static void __AICallbackStackSwitch(void *cb);",
        "static void __AICallbackStackSwitch(AIDCallback cb);",
        1,
    )
    if "static asm void __AICallbackStackSwitch" in transformed:
        raise AssertionError("PPC callback assembly was not removed")
    if transformed.count("void AIInit(u8 *stack)") != 1:
        raise AssertionError("AIInit source body was not preserved")
    return transformed


def _interrupt_source_transform(source: str) -> str:
    """Extract only the authored mask helper and unmask function bodies."""

    set_start = source.index("static u32 SetInterruptMask")
    set_end = source.index("\nOSInterruptMask OSGetInterruptMask", set_start)
    unmask_start = source.index("OSInterruptMask __OSUnmaskInterrupts", set_end)
    unmask_end = source.index("\nvoid __OSDispatchInterrupt", unmask_start)
    transformed = source[set_start:set_end] + "\n\n" + source[unmask_start:unmask_end]
    if transformed.count("static u32 SetInterruptMask") != 1:
        raise AssertionError("SetInterruptMask extraction is not unique")
    if transformed.count("OSInterruptMask __OSUnmaskInterrupts") != 1:
        raise AssertionError("__OSUnmaskInterrupts extraction is not unique")
    if "OSSetInterruptMask" in transformed or "__OSDispatchInterrupt" in transformed:
        raise AssertionError("interrupt extraction included an unrelated source body")
    return transformed


def _write_failure(evidence: Path, message: str) -> None:
    (evidence / "failure.txt").write_text(message)


def _new_evidence(prefix: str) -> Path:
    root = ROOT / "work"
    root.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=prefix, dir=root))


def _available_dependencies() -> list[Path]:
    return [path for path in (AI_SOURCE, OS_INTERRUPT_SOURCE, EMXX, PROVIDER, SOURCE_PATCH,
                              SDK / ".emscripten", NODE)
            if path is None or not path.is_file()]


def _apply_source_patch(source: str, evidence: Path) -> str:
    """Apply the reviewed callback adaptation through git apply only."""

    patched_input = evidence / "ai.c"
    generated = evidence / "ai_source_generated.c"
    patched_input.write_text(source)
    base_command = ["git", "apply", "--unsafe-paths", "--directory", str(evidence)]
    check = subprocess.run(
        [*base_command, "--check", str(SOURCE_PATCH)], cwd=ROOT,
        text=True, capture_output=True,
    )
    (evidence / "patch-check.stdout").write_text(check.stdout)
    (evidence / "patch-check.stderr").write_text(check.stderr)
    if check.returncode:
        _write_failure(evidence, "callback patch --check failed\n")
        raise AssertionError(f"callback patch check failed; retained evidence at {evidence}")
    applied = subprocess.run(
        [*base_command, str(SOURCE_PATCH)], cwd=ROOT,
        text=True, capture_output=True,
    )
    (evidence / "patch-apply.stdout").write_text(applied.stdout)
    (evidence / "patch-apply.stderr").write_text(applied.stderr)
    if applied.returncode:
        _write_failure(evidence, "callback patch application failed\n")
        raise AssertionError(f"callback patch application failed; retained evidence at {evidence}")
    transformed = patched_input.read_text()
    if transformed != _source_transform(source):
        _write_failure(evidence, "applied callback patch output differs from reviewed transform\n")
        raise AssertionError("callback patch output differs from the reviewed transform")
    generated.write_text(transformed)
    return transformed


def _compile_and_run(run_args: tuple[str, ...] = ()) -> dict:
    missing = _available_dependencies()
    if missing:
        raise unittest.SkipTest(
            "missing pinned AI oracle dependency: " + ", ".join(map(str, missing))
        )

    source_bytes_before = AI_SOURCE.read_bytes()
    source_hash_before = hashlib.sha256(source_bytes_before).hexdigest()
    if source_hash_before != KNOWN_AI_SOURCE_SHA256:
        raise AssertionError(
            f"pinned AI source hash changed before generation: {source_hash_before}"
        )
    source = source_bytes_before.decode()
    interrupt_bytes_before = OS_INTERRUPT_SOURCE.read_bytes()
    interrupt_hash_before = hashlib.sha256(interrupt_bytes_before).hexdigest()
    if interrupt_hash_before != KNOWN_OS_INTERRUPT_SOURCE_SHA256:
        raise AssertionError(
            "pinned OSInterrupt.c hash changed before generation: "
            f"{interrupt_hash_before}"
        )
    interrupt_source = interrupt_bytes_before.decode()
    evidence = _new_evidence("ai-init-profile-")
    output = evidence / "ai-init-profile.js"
    _apply_source_patch(source, evidence)
    (evidence / "os_interrupt_source_generated.c").write_text(
        _interrupt_source_transform(interrupt_source)
    )
    (evidence / "source.sha256").write_text(
        source_hash_before + "  ai.c\n" +
        interrupt_hash_before + "  OSInterrupt.c\n"
    )

    include = [
        f"-I{ROOT / '.deps/melee/extern/dolphin/include'}",
        f"-I{ROOT / '.deps/melee/extern/dolphin/src/dolphin/gx'}",
        f"-I{ROOT / '.deps/melee/src'}",
        f"-I{ROOT / '.deps/melee/include'}",
        f"-I{evidence}",
    ]
    command = [
        sys.executable,
        str(EMXX),
        "-std=c++17",
        "-DDEBUG=0",
        "-ffp-contract=off",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wno-writable-strings",
        # These are unused locals/parameters in the pinned DEBUG=0 source;
        # provider warnings remain errors, while preserving source bytes.
        "-Wno-unused-parameter",
        "-Wno-unused-variable",
        "-Wno-unused-but-set-variable",
        "-Wno-unused-function",
        "-sSAFE_HEAP=1",
        "-sSTACK_OVERFLOW_CHECK=2",
        "-sEXIT_RUNTIME=1",
        "-sASSERTIONS=2",
        *include,
        str(PROVIDER),
        "-o",
        str(output),
    ]
    (evidence / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    try:
        completed = subprocess.run(
            command, cwd=ROOT, env=_environment(), text=True,
            capture_output=True, timeout=60,
        )
    except subprocess.TimeoutExpired as exc:
        _write_failure(evidence, "Emscripten compile timed out after 60 seconds\n")
        (evidence / "compile.stdout").write_text(str(exc.stdout or ""))
        (evidence / "compile.stderr").write_text(str(exc.stderr or ""))
        raise AssertionError(f"AI provider compile timed out; retained evidence at {evidence}") from exc
    (evidence / "compile.stdout").write_text(completed.stdout)
    (evidence / "compile.stderr").write_text(completed.stderr)
    if completed.returncode:
        _write_failure(evidence, f"compile exited {completed.returncode}\n")
        raise AssertionError(
            "AI provider compile failed; retained evidence at "
            f"{evidence}\n{completed.stderr}"
        )

    source_hash_after = hashlib.sha256(AI_SOURCE.read_bytes()).hexdigest()
    if source_hash_after != KNOWN_AI_SOURCE_SHA256:
        _write_failure(evidence, "pinned source changed after compilation\n")
        raise AssertionError(f"pinned AI source changed after compile: {source_hash_after}")
    (evidence / "source-after.sha256").write_text(source_hash_after + "  ai.c\n")
    interrupt_hash_after = hashlib.sha256(OS_INTERRUPT_SOURCE.read_bytes()).hexdigest()
    if interrupt_hash_after != KNOWN_OS_INTERRUPT_SOURCE_SHA256:
        _write_failure(evidence, "pinned OSInterrupt.c changed after compilation\n")
        raise AssertionError(
            f"pinned OSInterrupt.c hash changed after compile: {interrupt_hash_after}"
        )
    (evidence / "source-after.sha256").write_text(
        source_hash_after + "  ai.c\n" +
        interrupt_hash_after + "  OSInterrupt.c\n"
    )

    run = subprocess.run(
        [str(NODE), str(output), *run_args],
        cwd=ROOT, env=_environment(), text=True,
        capture_output=True, timeout=10,
    )
    (evidence / "run.stdout").write_text(run.stdout)
    (evidence / "run.stderr").write_text(run.stderr)
    if run.returncode:
        _write_failure(evidence, f"node exited {run.returncode}\n")
        raise AssertionError(
            f"AI provider execution failed ({run.returncode}); retained evidence at "
            f"{evidence}\n{run.stderr}"
        )
    rows = [line for line in run.stdout.splitlines() if line.strip()]
    if len(rows) != 2:
        _write_failure(evidence, f"unexpected JSON rows: {rows!r}\n")
        raise AssertionError(f"expected startup and synthetic receipts, got {rows!r}")
    parsed = [json.loads(row) for row in rows]
    startup = next((row for row in parsed if row.get("kind") == "ai_init_return"), None)
    receipt = next((row for row in parsed if row.get("kind") == "synthetic_validation"), None)
    if startup is None or receipt is None:
        _write_failure(evidence, f"missing receipt kinds: {parsed!r}\n")
        raise AssertionError(f"expected ai_init_return and synthetic_validation, got {parsed!r}")
    receipt["startup_snapshot"] = startup
    receipt.update(
        {
            "ai_source_sha256": source_hash_after,
            "ai_source_sha256_before": source_hash_before,
            "ai_source_sha256_after": source_hash_after,
            "os_interrupt_source_sha256": interrupt_hash_after,
            "os_interrupt_source_sha256_before": interrupt_hash_before,
            "os_interrupt_source_sha256_after": interrupt_hash_after,
            "command": command,
            "node": str(NODE),
            "evidence_dir": str(evidence),
            "output": str(output),
        }
    )
    (evidence / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


def _run_existing(receipt: dict, args: tuple[str, ...], expected_error: str) -> Path:
    evidence = _new_evidence("ai-init-profile-negative-")
    output = Path(receipt["output"])
    command = [str(NODE), str(output), *args]
    (evidence / "command.json").write_text(json.dumps(command, indent=2) + "\n")
    try:
        run = subprocess.run(command, cwd=ROOT, env=_environment(), text=True,
                             capture_output=True, timeout=10)
    except subprocess.TimeoutExpired as exc:
        stdout = exc.stdout.decode() if isinstance(exc.stdout, bytes) else (exc.stdout or "")
        stderr = exc.stderr.decode() if isinstance(exc.stderr, bytes) else (exc.stderr or "")
        (evidence / "run.stdout").write_text(stdout)
        (evidence / "run.stderr").write_text(stderr)
        _write_failure(evidence, "negative probe timed out after 10 seconds\n")
        raise AssertionError(f"negative probe timed out; retained evidence at {evidence}") from exc
    (evidence / "run.stdout").write_text(run.stdout)
    (evidence / "run.stderr").write_text(run.stderr)
    (evidence / "receipt.json").write_text(json.dumps(
        {"args": args, "returncode": run.returncode, "stderr": run.stderr}, indent=2
    ) + "\n")
    if run.returncode == 0 or expected_error not in run.stderr:
        raise AssertionError(
            f"negative probe {args!r} did not fail as expected; evidence at {evidence}\n"
            f"stdout={run.stdout}\nstderr={run.stderr}"
        )
    return evidence


class SourceAiInitProfileTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        missing = _available_dependencies()
        if missing:
            raise unittest.SkipTest(
                "missing pinned AI oracle dependency: " + ", ".join(map(str, missing))
            )

    def test_audio_interface_manager_startup_and_dma_proxy(self) -> None:
        receipt = _compile_and_run()
        self.assertTrue(receipt["ai_init"])
        self.assertFalse(receipt["hardware_audio_verified"])
        self.assertTrue(receipt["nonnull_stack_adapter_rejected"])
        self.assertEqual(receipt["control_mode"], "audio_interface_manager")
        self.assertEqual(receipt["ai_control"], 0x46)
        startup = receipt["startup_snapshot"]
        self.assertTrue(startup["ai_init_flag"])
        self.assertEqual(startup["ai_control"], 0x46)
        self.assertEqual(startup["ai_volume"], 0)
        self.assertEqual(startup["ai_sample_count"], 0)
        self.assertEqual(startup["ai_trigger"], 0)
        self.assertTrue(startup["stream_callback_null"])
        self.assertTrue(startup["dma_callback_null"])
        self.assertTrue(startup["callback_stack_null"])
        self.assertEqual(startup["unmask_requested"], 0x04800000)
        self.assertEqual(startup["dsp_control"] & 0x0150, 0x10)
        self.assertTrue(startup["handler_5_is_AIDHandler"])
        self.assertTrue(startup["handler_8_is_AISHandler"])
        self.assertEqual(startup["sample_reads"], 0)
        self.assertEqual(startup["sample_advances"], 0)
        self.assertEqual(startup["time_reads"], 0)
        self.assertEqual(receipt["ai_source_sha256"], KNOWN_AI_SOURCE_SHA256)
        self.assertEqual(receipt["clock_hz"], 162000000)
        self.assertEqual(receipt["cpu_clock_hz"], 486000000)
        self.assertEqual(receipt["service_tick_quantum"], 64)
        self.assertEqual(receipt["source_divisor_32"], 3372)
        self.assertEqual(receipt["source_divisor_48"], 2248)
        self.assertEqual(receipt["sample_reads"], 0)
        self.assertEqual(receipt["sample_advances"], 0)
        self.assertEqual(receipt["time_reads"], 0)
        self.assertEqual(receipt["unmask_requested"], 0x04800000)
        self.assertEqual(receipt["dsp_control"] & 0x0150, 0x10)
        self.assertTrue(receipt["handler_5_is_AIDHandler"])
        self.assertTrue(receipt["handler_8_is_AISHandler"])
        self.assertGreater(receipt["interrupt_restore_calls"], 0)
        self.assertEqual(receipt["dsp_dma_control"] & 0x8000, 0x8000)
        self.assertEqual(receipt["dsp_dma_control"] & 0x7FFF, 0x14)
        self.assertTrue(receipt["dma_programming_checked"])
        self.assertEqual(receipt["dma_length"], 0x280)
        self.assertEqual(receipt["dma_start"] & 0x1F, 0)
        self.assertEqual(receipt["dma_start"], receipt["dma_buffer_start"] & 0x03FFFFE0)

    def test_calibration_mode_is_explicit_and_clock_derived(self) -> None:
        receipt = _compile_and_run(("--control", "calibration"))
        self.assertEqual(receipt["control_mode"], "calibration")
        self.assertEqual(receipt["ai_control"], 0x46)
        self.assertGreater(receipt["sample_reads"], 0)
        self.assertGreater(receipt["sample_advances"], 0)
        self.assertGreater(receipt["time_reads"], 0)
        self.assertEqual(receipt["startup_snapshot"]["sample_reads"], receipt["sample_reads"])

    def test_negative_controls_fail_closed(self) -> None:
        receipt = _compile_and_run()
        _run_existing(receipt, ("--dma-unowned",), "DMA enable is not bound")
        _run_existing(receipt, ("--invalid-handler",), "null interrupt handler")
        _run_existing(receipt, ("--unknown-register",), "unknown AI register")
        _run_existing(receipt, ("--unknown-dsp-register",), "unknown DSP register")
        _run_existing(receipt, ("--unknown-interrupt-mask",), "unsupported interrupt mask")
        _run_existing(receipt, ("--unknown-control",), "unsupported AI control bits")
        _run_existing(
            receipt, ("--control", "calibration", "--clock-stall"),
            "synthetic source clock stalled",
        )

    def test_transform_preserves_source_and_rejects_stack_path(self) -> None:
        source = AI_SOURCE.read_text()
        self.assertEqual(hashlib.sha256(source.encode()).hexdigest(), KNOWN_AI_SOURCE_SHA256)
        transformed = _source_transform(source)
        self.assertIn("void AIInit(u8 *stack)", transformed)
        self.assertIn("AI callback stack is unsupported by this oracle", transformed)
        self.assertNotIn("mflr r0", transformed)
        interrupt_source = OS_INTERRUPT_SOURCE.read_text()
        self.assertEqual(
            hashlib.sha256(interrupt_source.encode()).hexdigest(),
            KNOWN_OS_INTERRUPT_SOURCE_SHA256,
        )
        interrupt = _interrupt_source_transform(interrupt_source)
        self.assertIn("static u32 SetInterruptMask", interrupt)
        self.assertIn("OSInterruptMask __OSUnmaskInterrupts", interrupt)
        self.assertNotIn("__OSDispatchInterrupt", interrupt)


if __name__ == "__main__":
    unittest.main()
