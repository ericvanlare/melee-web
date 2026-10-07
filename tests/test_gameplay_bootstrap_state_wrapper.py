#!/usr/bin/env python3
"""Exercise the exact Wasm wrapper's publication and refusal boundary.

This compiles the production wrapper source unchanged with tiny explicit mocks
for the producer and owner APIs. It does not exercise the source producer or
claim source-runtime equivalence.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WRAPPER = ROOT / "tests" / "gameplay_bootstrap_state_probe.c"
HEADER = ROOT / "src" / "gameplay_bootstrap.h"
SCRATCH_PARENT = ROOT / "work" / "bootstrap-state-wrapper-tests"
COMPILE_TIMEOUT_SECONDS = 30
RUN_TIMEOUT_SECONDS = 10


MOCK_RUNTIME_HEADER = r'''#ifndef TEST_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H
#define TEST_GAMEPLAY_SOURCE_MEMORY_RUNTIME_H
#include <stddef.h>
#include <stdint.h>
int melee_web_source_memory_healthy(void);
#endif
'''


HARNESS = r'''#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gameplay_bootstrap.h"

int melee_web_gameplay_bootstrap_state_capture(MeleeWebGameplayBootstrapState*, uint32_t);

static int producer_ok;
static int producer_calls;
static int source_ok;
static int world_exists;
static int session_active;
static MeleeWebGameplayBootstrapState producer_state;
static MeleeWebGameplayAllocation allocation_state;
static MeleeWebGameplayStats stats_state;

int melee_web_gameplay_bootstrap_state(MeleeWebGameplayBootstrapState* out,
                                       uint32_t out_size)
{
    ++producer_calls;
    if (!producer_ok) {
        /* Deliberately scribble the producer's staged record before failing. */
        if (out && out_size == sizeof(*out)) memset(out, 0x3c, sizeof(*out));
        return 0;
    }
    if (!out || out_size != sizeof(*out)) return 0;
    memcpy(out, &producer_state, sizeof(*out));
    return 1;
}
int melee_web_source_memory_healthy(void) { return source_ok; }
int melee_web_gameplay_world_exists(void) { return world_exists; }
int melee_web_gameplay_session_active(void) { return session_active; }
MeleeWebGameplayAllocation melee_web_gameplay_allocation(void) { return allocation_state; }
MeleeWebGameplayStats melee_web_gameplay_stats(void) { return stats_state; }

static void valid_owner(void)
{
    memset(&producer_state, 0, sizeof(producer_state));
    producer_state.abi_version = MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_ABI_VERSION;
    producer_state.abi_size = sizeof(producer_state);
    producer_state.schema = MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SCHEMA;
    producer_state.ticks = 17;
    producer_state.generation = 9;
    producer_state.allocation_generation = 12;
    producer_state.arena_bytes = 4 * 1024 * 1024;
    producer_state.session_bytes = 4 * 1024 * 1024;
    producer_state.arena_identity = 0x1000;
    producer_state.session_identity = 0x2000;
    producer_state.heap_handle = 3;
    producer_state.tables_live = 1;
    producer_state.vs_borrowed_sis_slot = UINT32_MAX;
    producer_state.vs_dynamics_ready = 1;
    producer_state.vs_manager_ready = 1;
    producer_state.vs_sis_live = 1;
    allocation_state.identity = producer_state.arena_identity;
    allocation_state.generation = producer_state.allocation_generation;
    allocation_state.bytes = producer_state.arena_bytes;
    stats_state.ticks = producer_state.ticks;
    stats_state.generation = producer_state.generation;
    producer_ok = source_ok = world_exists = session_active = 1;
    producer_calls = 0;
}

static int unchanged(const unsigned char* bytes)
{
    for (size_t i = 0; i < sizeof(producer_state); ++i)
        if (bytes[i] != 0xa5) return 0;
    return 1;
}

static int refusal(void)
{
    union { MeleeWebGameplayBootstrapState state; unsigned char bytes[128]; } output;
    memset(output.bytes, 0xa5, sizeof(output.bytes));
    int result = melee_web_gameplay_bootstrap_state_capture(&output.state,
                                                            sizeof(output.state));
    return result == 0 && unchanged(output.bytes);
}

int main(void)
{
    union { MeleeWebGameplayBootstrapState state; unsigned char bytes[128]; } output;
    valid_owner();

    /* Wrapper argument guards must not call the producer or write the caller. */
    memset(output.bytes, 0xa5, sizeof(output.bytes));
    if (melee_web_gameplay_bootstrap_state_capture(NULL, sizeof(output.state)) != 0 ||
        producer_calls != 0) return 1;
    if (melee_web_gameplay_bootstrap_state_capture(&output.state,
                                                   sizeof(output.state) - 1) != 0 ||
        !unchanged(output.bytes) || producer_calls != 0) return 2;

    /* Producer failure is atomic at the wrapper boundary. */
    producer_ok = 0; producer_calls = 0;
    if (!refusal() || producer_calls != 1) return 3;
    producer_ok = 1;

    /* Every lifecycle flag is an explicit refusal, with no partial output. */
    uint32_t* lifecycle_flags[] = {
        &producer_state.tables_live, &producer_state.stepping,
        &producer_state.shutting_down,
        &producer_state.startup_in_progress};
    for (size_t i = 0; i < sizeof(lifecycle_flags) / sizeof(lifecycle_flags[0]); ++i) {
        uint32_t* flag = lifecycle_flags[i];
        *flag = flag == &producer_state.tables_live ? 0 : 1;
        if (!refusal()) return 4;
        *flag = flag == &producer_state.tables_live ? 1 : 0;
    }

    producer_state.shutting_down = 2;
    if (!refusal()) return 14;
    producer_state.shutting_down = 0;

    /* Source-memory, world/session, allocation, and stats relations are atomic. */
    source_ok = 0; if (!refusal()) return 5; source_ok = 1;
    world_exists = 0; if (!refusal()) return 6; world_exists = 1;
    session_active = 0; if (!refusal()) return 7; session_active = 1;
    allocation_state.identity ^= 1; if (!refusal()) return 8; allocation_state.identity ^= 1;
    allocation_state.generation ^= 1; if (!refusal()) return 9; allocation_state.generation ^= 1;
    allocation_state.bytes += 65536; if (!refusal()) return 10; allocation_state.bytes -= 65536;
    stats_state.generation ^= 1; if (!refusal()) return 11; stats_state.generation ^= 1;
    stats_state.ticks += 1; if (!refusal()) return 12; stats_state.ticks -= 1;

    /* A valid owned record is published exactly, after all guards pass. */
    memset(output.bytes, 0xa5, sizeof(output.bytes));
    if (melee_web_gameplay_bootstrap_state_capture(&output.state,
                                                   sizeof(output.state)) != 1 ||
        memcmp(output.bytes, &producer_state, sizeof(output.state)) != 0) return 13;
    return 0;
}
'''


def _write_result(directory: Path, label: str, command: list[str], result) -> None:
    (directory / f"{label}-command.json").write_text(
        json.dumps(command, indent=2) + "\n", encoding="utf-8"
    )
    (directory / f"{label}-stdout.txt").write_text(result.stdout or "", encoding="utf-8")
    (directory / f"{label}-stderr.txt").write_text(result.stderr or "", encoding="utf-8")
    (directory / f"{label}-result.json").write_text(
        json.dumps({"returncode": result.returncode}, indent=2) + "\n", encoding="utf-8"
    )


def _text(value) -> str:
    return value.decode(errors="replace") if isinstance(value, bytes) else (value or "")


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _run(directory: Path, label: str, command: list[str], timeout: int):
    try:
        result = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True,
            timeout=timeout, check=False,
        )
    except subprocess.TimeoutExpired as error:
        (directory / f"{label}-timeout.json").write_text(
            json.dumps({"timeout_seconds": timeout, "stdout": _text(error.stdout),
                        "stderr": _text(error.stderr)}, indent=2) + "\n",
            encoding="utf-8",
        )
        raise AssertionError(f"{label} exceeded {timeout}s; retained {directory}") from error
    _write_result(directory, label, command, result)
    if result.returncode != 0:
        raise AssertionError(f"{label} returned {result.returncode}; retained {directory}")


class OwnedWorkspaceTests(unittest.TestCase):
    def test_wrapper_refusals_are_atomic_and_valid_owner_publishes(self):
        compiler = shutil.which("cc")
        if compiler is None:
            self.fail("cc is required for the wrapper boundary control")
        if not WRAPPER.is_file() or WRAPPER.is_symlink():
            self.fail("the exact gameplay bootstrap wrapper must be a regular file")
        SCRATCH_PARENT.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="run-", dir=SCRATCH_PARENT))
        try:
            shim = directory / "gameplay_source_memory_runtime.h"
            shim.write_text(MOCK_RUNTIME_HEADER, encoding="utf-8")
            wrapper_copy = directory / "gameplay_bootstrap_state_probe.c"
            header_copy = directory / "gameplay_bootstrap.h"
            wrapper_copy.write_bytes(WRAPPER.read_bytes())
            header_copy.write_bytes(HEADER.read_bytes())
            harness = directory / "wrapper_harness.c"
            harness.write_text(HARNESS, encoding="utf-8")
            (directory / "inputs.json").write_text(json.dumps({
                "wrapper": str(WRAPPER),
                "wrapper_copy": str(wrapper_copy),
                "wrapper_bytes": WRAPPER.stat().st_size,
                "wrapper_sha256": _sha256(WRAPPER),
                "header": str(HEADER),
                "header_copy": str(header_copy),
                "header_bytes": HEADER.stat().st_size,
                "header_sha256": _sha256(HEADER),
            }, indent=2) + "\n", encoding="utf-8")
            binary = directory / "wrapper_harness"
            command = [
                compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(directory), "-I", str(ROOT / "src"),
                "-DMELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_PROBE_SHA256=\"wrapper-test\"",
                "-DMELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_C_SHA256=\"source-c-test\"",
                "-DMELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SOURCE_H_SHA256=\"source-h-test\"",
                "-include", str(directory / "emscripten.h"),
                str(wrapper_copy), str(harness), "-o", str(binary),
            ]
            (directory / "emscripten.h").write_text(
                "#define EMSCRIPTEN_KEEPALIVE\n", encoding="utf-8"
            )
            _run(directory, "compile", command, COMPILE_TIMEOUT_SECONDS)
            _run(directory, "run", [str(binary)], RUN_TIMEOUT_SECONDS)
            (directory / "passed.json").write_text(
                json.dumps({"result": "passed", "wrapper": str(WRAPPER)}, indent=2) + "\n",
                encoding="utf-8",
            )
        except Exception:
            (directory / "failure.json").write_text(
                json.dumps({"result": "failed", "wrapper": str(WRAPPER)}, indent=2) + "\n",
                encoding="utf-8",
            )
            raise
        else:
            shutil.rmtree(directory)


if __name__ == "__main__":
    unittest.main()
