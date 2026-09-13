#!/usr/bin/env python3
"""Produce a bounded diagnostic for a port replay that stopped before teardown."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from port_replay_diagnostics import diagnose_paths, status_exit_code  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference_a", type=Path)
    parser.add_argument("reference_b", type=Path)
    parser.add_argument("port", type=Path)
    parser.add_argument("--output", type=Path,
                        help="optional path for the machine-readable report")
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"),
                        default="Interpreter64")
    args = parser.parse_args(argv)
    paths = [args.reference_a, args.reference_b, args.port]
    if args.output is not None and args.output.resolve() in {path.resolve() for path in paths}:
        parser.error("Output cannot overwrite a capture")
    report = diagnose_paths(args.reference_a, args.reference_b, args.port,
                            cpu=args.cpu)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
        except OSError as error:
            parser.exit(2, f"cannot write report: {error}\n")
    print(encoded, end="")
    return status_exit_code(report["status"])


if __name__ == "__main__":
    raise SystemExit(main())
