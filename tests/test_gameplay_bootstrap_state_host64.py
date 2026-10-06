#!/usr/bin/env python3
"""Execute the exact bootstrap producer's host-64 refusal boundary.

This test extracts the producer and identity helpers from the applied source,
includes the applied header, and executes the generated harness. It does not
claim a source-runtime, Wasm, or gameplay result.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "gameplay_bootstrap.h"
SOURCE = ROOT / "src" / "gameplay_bootstrap.c"
SCRATCH_PARENT = ROOT / "work" / "bootstrap-state-host64-tests"
COMPILE_TIMEOUT_SECONDS = 30
RUN_TIMEOUT_SECONDS = 10


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _text(value) -> str:
    return value.decode(errors="replace") if isinstance(value, bytes) else (value or "")


def _function(source: str, name: str) -> str:
    """Extract one complete C function, allowing nested callback signatures."""
    match = re.search(
        r"(?:static\s+)?(?:uint32_t|int)\s+" + re.escape(name) + r"\b",
        source,
    )
    if not match:
        raise AssertionError(f"producer function is absent: {name}")
    start = match.start()
    brace = source.find("{", match.end())
    if brace < 0:
        raise AssertionError(f"producer function has no body: {name}")
    depth = 0
    quote = None
    escaped = False
    for index in range(brace, len(source)):
        char = source[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in "\"'":
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated producer function: {name}")


def _generated_source() -> str:
    source = SOURCE.read_text(encoding="utf-8")
    functions = "\n\n".join(_function(source, name) for name in (
        "bootstrap_pointer_identity",
        "bootstrap_startup_identity",
        "bootstrap_shutdown_identity",
        "bootstrap_finish_identity",
        "melee_web_gameplay_bootstrap_state",
    ))
    return f'''/* Generated only for the host-64 ABI refusal test. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef int OSHeapHandle;
#include "gameplay_bootstrap.h"

static void* arena;
static size_t arena_bytes;
static void* session_arena;
static size_t session_bytes;
static OSHeapHandle heap = -1;
static uint64_t ticks, disabled_links, generation, allocation_generation;
static int stepping, shutting_down, tables_live;
static int vs_startup_pending, startup_in_progress, vs_sis_live;
static int vs_dynamics_ready, vs_manager_ready;
static MeleeWebGameplayVSStartup vs_startup_callback;
static MeleeWebGameplayVSShutdown vs_shutdown_callback;
static unsigned object_kind_count;
static void (*finish_hsd_objects)(void);

{functions}

static int unchanged(const unsigned char* bytes, size_t size)
{{
    for (size_t i = 0; i < size; ++i) if (bytes[i] != 0xa5) return 0;
    return 1;
}}

int main(void)
{{
    union {{
        MeleeWebGameplayBootstrapState state;
        unsigned char bytes[sizeof(MeleeWebGameplayBootstrapState)];
    }} storage;
    memset(storage.bytes, 0xa5, sizeof(storage.bytes));
    if (melee_web_gameplay_bootstrap_state(NULL, sizeof(storage.state)) != 0) return 1;
    if (melee_web_gameplay_bootstrap_state(&storage.state,
                                           sizeof(storage.state) - 1) != 0 ||
        !unchanged(storage.bytes, sizeof(storage.bytes))) return 2;
    if (melee_web_gameplay_bootstrap_state(&storage.state,
                                           sizeof(storage.state)) != 0 ||
        !unchanged(storage.bytes, sizeof(storage.bytes))) return 3;
    return 0;
}}
'''


def _write_process_result(directory: Path, label: str, command: list[str], result) -> None:
    (directory / f"{label}-command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    (directory / f"{label}-stdout.txt").write_text(result.stdout or "", encoding="utf-8")
    (directory / f"{label}-stderr.txt").write_text(result.stderr or "", encoding="utf-8")
    (directory / f"{label}-result.json").write_text(
        json.dumps({"returncode": result.returncode}, indent=2) + "\n", encoding="utf-8"
    )


def _run_bounded(directory: Path, label: str, command: list[str], timeout: int):
    try:
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        (directory / f"{label}-timeout.json").write_text(
            json.dumps({"timeout_seconds": timeout, "stdout": _text(error.stdout),
                        "stderr": _text(error.stderr)}, indent=2) + "\n",
            encoding="utf-8",
        )
        raise AssertionError(f"{label} exceeded {timeout}s; retained {directory}") from error
    _write_process_result(directory, label, command, result)
    if result.returncode != 0:
        raise AssertionError(f"{label} returned {result.returncode}; retained {directory}")
    return result


class OwnedWorkspaceTests(unittest.TestCase):
    def test_snapshot_inventories_every_private_bootstrap_global(self):
        source = SOURCE.read_text(encoding="utf-8")
        declarations = source.split("static void* arena;", 1)[1].split("static int fail(", 1)[0]
        declarations = "static void* arena;" + declarations
        globals_seen = set()
        for line in declarations.splitlines():
            line = line.strip()
            if not line.startswith("static ") or not line.endswith(";"):
                continue
            function_pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)", line)
            if function_pointer:
                globals_seen.add(function_pointer.group(1))
                continue
            declarators = line.removeprefix("static ").removesuffix(";").split(",")
            for declarator in declarators:
                match = re.search(r"(?:\*\s*)?(\w+)\s*(?:=[^,]*)?$", declarator.strip())
                self.assertIsNotNone(match, f"unparsed bootstrap global: {line}")
                globals_seen.add(match.group(1))

        expected = {
            "arena", "arena_bytes", "session_arena", "session_bytes", "heap",
            "ticks", "disabled_links", "generation", "allocation_generation",
            "stepping", "shutting_down", "tables_live", "vs_startup_pending",
            "startup_in_progress", "vs_sis_live", "vs_dynamics_ready",
            "vs_manager_ready", "vs_startup_callback", "vs_shutdown_callback",
            "object_kind_count", "finish_hsd_objects",
        }
        self.assertEqual(globals_seen, expected, "private bootstrap owner inventory changed")
        snapshot = _function(source, "melee_web_gameplay_bootstrap_state")
        captured = set(re.findall(r"\bout->(\w+)\s*=", snapshot))
        report_fields = {
            "arena": "arena_identity", "arena_bytes": "arena_bytes",
            "session_arena": "session_identity", "session_bytes": "session_bytes",
            "heap": "heap_handle", "ticks": "ticks",
            "disabled_links": "disabled_links", "generation": "generation",
            "allocation_generation": "allocation_generation", "stepping": "stepping",
            "shutting_down": "shutting_down", "tables_live": "tables_live",
            "vs_startup_pending": "vs_startup_pending",
            "startup_in_progress": "startup_in_progress", "vs_sis_live": "vs_sis_live",
            "vs_dynamics_ready": "vs_dynamics_ready", "vs_manager_ready": "vs_manager_ready",
            "vs_startup_callback": "vs_startup_callback",
            "vs_shutdown_callback": "vs_shutdown_callback",
            "object_kind_count": "object_kind_count",
            "finish_hsd_objects": "finish_hsd_objects",
        }
        self.assertEqual({name: field for name, field in report_fields.items() if field in captured},
                         report_fields,
                         "bootstrap snapshot must report every private owner field")

    def test_exact_producer_host64_refuses_without_writing(self):
        if struct.calcsize("P") != 8:
            self.skipTest("host-64 refusal control requires an 8-byte host pointer")

        SCRATCH_PARENT.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=SCRATCH_PARENT))
        try:
            if not HEADER.is_file() or HEADER.is_symlink() or not SOURCE.is_file() or SOURCE.is_symlink():
                raise AssertionError("candidate source/header must be regular files")
            generated = _generated_source()
            generated_path = directory / "host64.c"
            binary = directory / "host64"
            generated_path.write_text(generated, encoding="utf-8")
            (directory / "inputs.json").write_text(json.dumps({
                "source_c": str(SOURCE), "source_c_sha256": _sha256(SOURCE),
                "source_h": str(HEADER), "source_h_sha256": _sha256(HEADER),
                "pointer_bytes": struct.calcsize("P"),
            }, indent=2) + "\n", encoding="utf-8")
            compiler = "cc"
            compile_command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(ROOT / "src"), str(generated_path), "-o", str(binary),
            ]
            _run_bounded(directory, "compile", compile_command, COMPILE_TIMEOUT_SECONDS)
            _run_bounded(directory, "run", [str(binary)], RUN_TIMEOUT_SECONDS)
            (directory / "passed.json").write_text(json.dumps({
                "result": "passed", "source_c_sha256": _sha256(SOURCE),
                "source_h_sha256": _sha256(HEADER), "runtime_admission": False,
            }, indent=2) + "\n", encoding="utf-8")
        except Exception:
            (directory / "failure.json").write_text(json.dumps({
                "result": "failed", "source_c": str(SOURCE),
                "source_h": str(HEADER),
            }, indent=2) + "\n", encoding="utf-8")
            raise
        else:
            shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
