#!/usr/bin/env python3
"""Run the existing native CPU trace executable and retain its evidence.

This is a capture boundary only.  It does not build, patch, or copy the
runtime or its game assets.  The executable is always given
``--require-match-complete``; a zero exit status therefore records only that
the requested native workload completed under the executable's own checks.
It is not an accuracy claim.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_capture_identity import runtime_inputs
TRACE_NAME = "gameplay_retail_trace.js"
WASM_NAME = "gameplay_retail_trace.wasm"


class NativeCaptureError(RuntimeError):
    """A capture prerequisite or output contract failed."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
    except OSError as error:
        raise NativeCaptureError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def regular_file(value: str | os.PathLike[str], label: str) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise NativeCaptureError(f"{label} must be a regular file: {path}")
    return path


def directory(value: str | os.PathLike[str], label: str) -> Path:
    path = Path(value).expanduser().resolve()
    if not path.is_dir():
        raise NativeCaptureError(f"{label} must be a directory: {path}")
    return path


def new_output(value: str | os.PathLike[str]) -> Path:
    """Create the output directory without ever reusing an old run."""

    path = Path(value).expanduser().resolve()
    if path.exists():
        raise NativeCaptureError(f"output directory already exists: {path}")
    try:
        path.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        raise NativeCaptureError(f"cannot create output directory {path}: {error}") from error
    return path


def _write_bytes(path: Path, data: bytes) -> None:
    try:
        path.write_bytes(data)
    except OSError as error:
        raise NativeCaptureError(f"cannot write {path}: {error}") from error


def _write_json(path: Path, value: object) -> None:
    try:
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    except (OSError, TypeError, ValueError) as error:
        raise NativeCaptureError(f"cannot write {path}: {error}") from error


def _extract_lines(stderr: bytes, prefix: bytes) -> bytes:
    """Extract diagnostic payloads while preserving their original bytes."""

    rows = []
    for line in stderr.splitlines(keepends=True):
        if line.startswith(prefix):
            rows.append(line[len(prefix):])
    return b"".join(rows)


def _extract_prefixed_lines(stderr: bytes, prefix: bytes) -> bytes:
    """Extract diagnostic records without discarding their record marker."""

    return b"".join(line for line in stderr.splitlines(keepends=True)
                     if line.startswith(prefix))


def _artifact_state(js: Path, wasm: Path, recipe: Path) -> dict[str, str]:
    return {
        TRACE_NAME: sha256(js),
        WASM_NAME: sha256(wasm),
        "recipe": sha256(recipe),
    }


def run_capture(*, build_directory: Path, menu_assets: Path, game_assets: Path,
                recipe: Path, output: Path, node: str = "node",
                cpu_hitlag_diagnostic: bool = False,
                timeout: float = 900.0) -> int:
    """Run one fresh native capture and return its process-style status."""

    if not math.isfinite(timeout) or timeout <= 0:
        raise NativeCaptureError("timeout must be a positive finite number of seconds")
    build = directory(build_directory, "build directory")
    menu = directory(menu_assets, "menu assets")
    game = directory(game_assets, "game assets")
    input_recipe = regular_file(recipe, "recipe")
    executable = regular_file(build / TRACE_NAME, "gameplay_retail_trace.js")
    wasm = regular_file(build / WASM_NAME, "gameplay_retail_trace.wasm")
    node_path = shutil.which(node)
    if node_path is None:
        node_candidate = Path(node).expanduser()
        if node_candidate.is_file():
            node_path = str(node_candidate.resolve())
        else:
            raise NativeCaptureError(f"Node executable was not found: {node}")

    out = new_output(output)
    before = _artifact_state(executable, wasm, input_recipe)
    inputs_before = runtime_inputs(node_path, menu, game)
    config = {
        "schema": "melee-web-cpu-native-capture",
        "version": 1,
        "artifacts": {
            TRACE_NAME: before[TRACE_NAME],
            WASM_NAME: before[WASM_NAME],
        },
        "recipe_sha256": before["recipe"],
        "require_match_complete": True,
        "cpu_hitlag_diagnostic": cpu_hitlag_diagnostic,
        "timeout_seconds": timeout,
        "native_executable": TRACE_NAME,
        "node_executable": Path(node_path).name,
        "menu_assets": menu.name,
        "game_assets": game.name,
        "runtime_inputs": inputs_before,
    }
    _write_json(out / "capture-config.json", config)

    trace_path = out / "trace.jsonl"
    stderr_path = out / "stderr.log"
    returncode: int | None = None
    launch_error: str | None = None
    stdout = b""
    stderr = b""
    timed_out = False
    command = [node_path, str(executable), str(menu), str(game), str(input_recipe),
               "--require-match-complete"]
    if cpu_hitlag_diagnostic:
        command.append("--cpu-hitlag-diagnostic")
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, check=False,
                                   timeout=timeout)
        returncode = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
    except subprocess.TimeoutExpired as error:
        timed_out = True
        launch_error = f"native capture timed out after {timeout:g} seconds"
        stdout = error.stdout if isinstance(error.stdout, bytes) else (error.stdout or b"")
        stderr = error.stderr if isinstance(error.stderr, bytes) else (error.stderr or b"")
    except (OSError, subprocess.SubprocessError) as error:
        launch_error = str(error)
        stderr = (f"native capture launch failed: {error}\n").encode("utf-8", "replace")
    _write_bytes(trace_path, stdout)
    _write_bytes(stderr_path, stderr)
    _write_bytes(out / "cpu-observation.jsonl", _extract_lines(stderr, b"CPU_AUDIT "))
    _write_bytes(out / "hitlag-audit.log",
                 _extract_prefixed_lines(stderr, b"HITLAG_AUDIT "))
    _write_bytes(out / "matrix-audit.jsonl", _extract_lines(stderr, b"MATRIX_AUDIT "))

    try:
        after = _artifact_state(executable, wasm, input_recipe)
        inputs_after = runtime_inputs(node_path, menu, game)
    except (NativeCaptureError, OSError, ValueError) as error:
        after = {}
        inputs_after = {}
        launch_error = launch_error or str(error)
    changed = after != before or inputs_after != inputs_before
    if changed:
        launch_error = launch_error or "build, recipe, Node or asset bytes changed during capture"

    completed_ok = returncode == 0 and not changed and launch_error is None
    result = {
        "schema": "melee-web-cpu-native-capture-result",
        "version": 1,
        "status": "completed" if completed_ok else "failed",
        "returncode": returncode,
        "full_completion": completed_ok,
        "full_completion_status": "completed" if completed_ok else "failed",
        "timed_out": timed_out,
        "artifact_change": changed,
        "artifacts_before": {
            TRACE_NAME: before[TRACE_NAME],
            WASM_NAME: before[WASM_NAME],
        },
        "artifacts_after": {
            key: value for key, value in after.items() if key != "recipe"
        },
        "recipe_sha256_before": before["recipe"],
        "recipe_sha256_after": after.get("recipe"),
        "runtime_inputs_before": inputs_before,
        "runtime_inputs_after": inputs_after,
        "outputs": {
            "trace.jsonl": sha256(trace_path),
            "cpu-observation.jsonl": sha256(out / "cpu-observation.jsonl"),
            "stderr.log": sha256(stderr_path),
            "hitlag-audit.log": sha256(out / "hitlag-audit.log"),
            "matrix-audit.jsonl": sha256(out / "matrix-audit.jsonl"),
        },
        "capture_config_sha256": sha256(out / "capture-config.json"),
    }
    if launch_error:
        result["error"] = launch_error
    _write_json(out / "capture-result.json", result)
    if not completed_ok:
        return returncode if returncode not in (None, 0) else 1
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True,
                        help="Existing build directory containing the trace JS/Wasm")
    parser.add_argument("--menu-assets", type=Path, required=True,
                        help="Existing owned menu asset directory")
    parser.add_argument("--game-assets", type=Path, required=True,
                        help="Existing owned game asset directory")
    parser.add_argument("--recipe", type=Path, required=True,
                        help="Existing MWRC recipe")
    parser.add_argument("--output", type=Path, required=True,
                        help="Fresh output directory; it must not already exist")
    parser.add_argument("--node", default="node",
                        help="Node executable (default: node on PATH)")
    parser.add_argument("--timeout", type=float, default=900.0,
                        help="Maximum native process duration in seconds (default: 900)")
    parser.add_argument("--cpu-hitlag-diagnostic", action="store_true",
                        help="Request the read-only HITLAG_AUDIT diagnostic")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return run_capture(build_directory=args.build_directory,
                           menu_assets=args.menu_assets,
                           game_assets=args.game_assets,
                           recipe=args.recipe,
                           output=args.output,
                           node=args.node,
                           cpu_hitlag_diagnostic=args.cpu_hitlag_diagnostic,
                           timeout=args.timeout)
    except NativeCaptureError as error:
        parser.error(str(error))
    return 2


if __name__ == "__main__":
    sys.exit(main())
