#!/usr/bin/env python3
"""Check a diagnostic CPU-register/source-state prefix against repeatable gold."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from cpu_register_prefix import compare_prefix_paths, status_exit_code  # noqa: E402


def _metadata_files(path: Path) -> set[Path]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return set()
    diagnostic = value.get("diagnostic") if isinstance(value, dict) else None
    if not isinstance(diagnostic, dict):
        return set()
    result = set()
    for key in ("full_input_plan_path", "executed_input_prefix_path"):
        item = diagnostic.get(key)
        if isinstance(item, str) and item:
            target = Path(item).expanduser()
            if not target.is_absolute():
                target = path.parent / target
            result.add(target.resolve())
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gold_a", type=Path,
                        help="complete original capture A")
    parser.add_argument("gold_b", type=Path,
                        help="complete original capture B")
    parser.add_argument("prefix", type=Path,
                        help="ended bounded diagnostic source-state prefix JSONL")
    parser.add_argument("metadata", type=Path,
                        help="diagnostic run metadata JSON (status diagnostic_only)")
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"),
                        default="Interpreter64")
    parser.add_argument("--allow-failed", action="store_true",
                        help="retain a diagnostic_failed run as non-admitting evidence")
    parser.add_argument("--output", type=Path,
                        help="optional report path; never overwrites an input")
    args = parser.parse_args(argv)
    inputs = (args.gold_a, args.gold_b, args.prefix, args.metadata)
    protected = {path.expanduser().resolve() for path in inputs}
    protected.update(_metadata_files(args.metadata))
    if args.output is not None and args.output.expanduser().resolve() in protected:
        parser.error("output cannot overwrite a gold, prefix, or metadata input")
    if args.output is not None and args.output.exists():
        parser.error("output already exists; choose a new report path")
    report = compare_prefix_paths(
        args.gold_a, args.gold_b, args.prefix, args.metadata, cpu=args.cpu,
        allow_failed=args.allow_failed)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return status_exit_code(report["status"])


if __name__ == "__main__":
    raise SystemExit(main())
