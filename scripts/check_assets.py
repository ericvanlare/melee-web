#!/usr/bin/env python3
"""Report native CPU parser coverage as JSON lines; no rendering is tested.

Pass explicit DAT paths or one directory (nonrecursive .dat/.usd selection).
Use --manifest for a checks-only JSON object; each row may select its own
path, symbol, stage_entry and opaque pass.
Every public root is attempted as an HSD joint; other root types can be rejected.
Use --symbol to select one known model root without attempting other root types.
Use --stage-entry N to decode one map_head entry; --symbol then selects the exact
stage header symbol. --opaque selects only the explicit opaque inspection pass
for a stage entry. Stage services are reported as present but unapplied.
Textures counts distinct TObj descriptors. Exit 1 means a parser rejection or
input error; exit 2 means the compiler/checking process could not complete.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]


_CHECK_FIELDS = frozenset(("path", "symbol", "stage_entry", "opaque"))


def unique_json_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON key {key!r}")
        value[key] = item
    return value


def parse_checks(value: Any, base_dir: Path | str) -> list[dict[str, Any]]:
    """Validate and resolve a manifest ``checks`` array.

    ``value`` must be the array of row objects from the shared manifest
    schema. Each row has a required ``path`` and may select one
    ``symbol``, ``stage_entry`` and/or ``opaque`` mode. Paths are resolved
    relative to ``base_dir`` (or accepted as an explicit absolute path) and
    returned as ``Path`` values in otherwise schema-shaped dictionaries. File
    existence is checked separately by :func:`validate_check_paths` so callers
    that inspect a configuration can reuse this parser without touching the
    filesystem.
    """
    if not isinstance(value, list) or not value:
        raise ValueError("manifest checks must be a nonempty list")
    base = Path(base_dir).resolve()
    checks: list[dict[str, Any]] = []
    for row_number, row in enumerate(value, 1):
        if not isinstance(row, dict):
            raise ValueError(f"manifest check {row_number} must be an object")
        fields = set(row)
        unknown = fields - _CHECK_FIELDS
        missing = "path" not in fields
        if unknown:
            names = ", ".join(sorted(repr(name) for name in unknown))
            raise ValueError(f"manifest check {row_number} has unexpected field(s): {names}")
        if missing:
            raise ValueError(f"manifest check {row_number} requires a path")

        path_value = row["path"]
        if not isinstance(path_value, str) or not path_value or "\x00" in path_value:
            raise ValueError(f"manifest check {row_number} path must be a nonempty string")
        input_path = Path(path_value)

        symbol = row.get("symbol")
        if "symbol" in row and (not isinstance(symbol, str) or not symbol):
            raise ValueError(f"manifest check {row_number} symbol must be a nonempty string")

        stage_entry = row.get("stage_entry")
        if "stage_entry" in row:
            if isinstance(stage_entry, bool) or not isinstance(stage_entry, int):
                raise ValueError(f"manifest check {row_number} stage_entry must be an unsigned 32-bit integer")
            if not 0 <= stage_entry <= 0xFFFFFFFF:
                raise ValueError(f"manifest check {row_number} stage_entry must be an unsigned 32-bit integer")

        opaque = row.get("opaque", False)
        if not isinstance(opaque, bool):
            raise ValueError(f"manifest check {row_number} opaque must be a boolean")
        if opaque and stage_entry is None:
            raise ValueError(f"manifest check {row_number} opaque requires stage_entry")

        check: dict[str, Any] = {"path": (input_path if input_path.is_absolute()
                                           else base / input_path).resolve()}
        if symbol is not None:
            check["symbol"] = symbol
        if stage_entry is not None:
            check["stage_entry"] = stage_entry
        if "opaque" in row:
            check["opaque"] = opaque
        checks.append(check)
    return checks


def load_manifest(path: Path | str) -> list[dict[str, Any]]:
    """Load the checks-only asset manifest using the shared row schema."""
    manifest = Path(path)
    try:
        raw = manifest.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ValueError(f"cannot read asset manifest {manifest}: {error}") from error
    try:
        value = json.loads(raw, object_pairs_hook=unique_json_object)
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"asset manifest is not valid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ValueError("asset manifest root must be an object")
    if set(value) != {"checks"}:
        raise ValueError("asset manifest must contain only the checks field")
    return parse_checks(value["checks"], manifest.resolve().parent)


def validate_check_paths(checks: list[dict[str, Any]]) -> None:
    """Reject missing or non-file inputs before compiling the native checker."""
    for check_number, check in enumerate(checks, 1):
        try:
            path = Path(check["path"])
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"asset check {check_number} configuration requires a path") from error
        if not path.is_file():
            raise ValueError(
                f"asset check {check_number} path is missing or not a regular file: {path}"
            )


def _run_one_check(binary: Path, check: dict[str, Any]) -> int:
    path = Path(check["path"])
    command = [str(binary), str(path)]
    if check.get("symbol"):
        command += ["--symbol", check["symbol"]]
    if check.get("stage_entry") is not None:
        command += ["--stage-entry", str(check["stage_entry"])]
    if check.get("opaque", False):
        command.append("--opaque")
    result = subprocess.run(command, capture_output=True, timeout=60)
    if result.returncode not in (0, 1):
        sys.stderr.buffer.write(result.stderr)
        return 2
    for line in result.stdout.splitlines():
        # Preserve non-UTF8 archive symbol bytes as escaped surrogates;
        # filenames are reported by Python with their original spelling.
        record = json.loads(line.decode("utf-8", errors="surrogateescape"))
        selection = {key: value for key, value in check.items() if key != "path" and value is not None}
        print(json.dumps({"file": str(path), "selection": selection, **record}, sort_keys=True), flush=True)
    return result.returncode


def run_checks(checks: list[dict[str, Any]]) -> int:
    """Compile the checker once and run every explicit check in order."""
    if not checks:
        raise ValueError("asset checks must be a nonempty list")
    validate_check_paths(checks)
    compiler = shutil.which("clang++") or shutil.which("c++")
    if compiler is None:
        raise ValueError("a C++20 compiler is required")
    binary = ROOT / "build/native/asset_check"
    binary.parent.mkdir(parents=True, exist_ok=True)
    # Recompile once per invocation so dependency/flag changes cannot leave a
    # stale parser binary producing misleading corpus results.
    sources = ["src/dat_archive.cpp", "src/dat_texture.cpp", "src/dat_material.cpp",
               "src/dat_stage.cpp", "src/rigid_model.cpp", "tools/asset_check.cpp"]
    compiled = subprocess.run(
        [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
         "-I", str(ROOT / "src"), *(str(ROOT / source) for source in sources),
         "-o", str(binary)], capture_output=True, timeout=120,
    )
    if compiled.returncode:
        sys.stderr.buffer.write(compiled.stdout + compiled.stderr)
        return 2
    status = 0
    for check in checks:
        status = max(status, _run_one_check(binary, check))
        if status == 2:
            return 2
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--manifest", type=Path,
                        help="JSON checks manifest; rows contain explicit asset paths")
    parser.add_argument("--symbol", help="check only this exact public model symbol")
    parser.add_argument("--stage-entry", type=int, help="check this zero-based map_head entry")
    parser.add_argument("--opaque", action="store_true", help="select a stage entry's opaque pass")
    args = parser.parse_args()

    if args.manifest is not None:
        if args.paths:
            parser.error("--manifest cannot be combined with positional paths")
        if args.symbol is not None or args.stage_entry is not None or args.opaque:
            parser.error("--manifest rows specify symbol, stage-entry and opaque selections")
        try:
            checks = load_manifest(args.manifest)
            validate_check_paths(checks)
        except ValueError as error:
            parser.error(str(error))
    else:
        if not args.paths:
            parser.error("provide input paths or --manifest")
        if args.stage_entry is not None and not 0 <= args.stage_entry <= 0xffffffff:
            parser.error("--stage-entry must be an unsigned 32-bit index")
        if args.opaque and args.stage_entry is None:
            parser.error("--opaque requires --stage-entry")
        paths = args.paths
        if len(paths) == 1 and paths[0].is_dir():
            paths = sorted(path for path in paths[0].iterdir()
                           if path.is_file() and path.suffix.lower() in (".dat", ".usd"))
        elif any(path.is_dir() for path in paths):
            parser.error("pass either explicit files or one directory")
        if not paths:
            parser.error("the directory contains no DAT/USD files")
        checks = []
        for path in paths:
            check = {"path": path}
            if args.symbol is not None:
                check["symbol"] = args.symbol
            if args.stage_entry is not None:
                check["stage_entry"] = args.stage_entry
            if args.opaque:
                check["opaque"] = True
            checks.append(check)
    try:
        return run_checks(checks)
    except (OSError, subprocess.TimeoutExpired, ValueError) as error:
        print(f"Asset check could not complete: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
