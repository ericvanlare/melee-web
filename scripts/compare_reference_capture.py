#!/usr/bin/env python3
"""Compare one hash-bound original capture with one port or browser trace."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from reference_capture_comparison import compare, status_exit_code  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--derived", required=True, type=Path,
                        help="derived capture directory containing derived-manifest.json")
    parser.add_argument("--trace", required=True, type=Path,
                        help="one port or browser state trace in port replay JSONL format")
    parser.add_argument("--trace-kind", choices=("native", "browser"), default="browser")
    parser.add_argument("--reference-cpu", type=Path,
                        help="optional original CPU observation JSONL sidecar")
    parser.add_argument("--trace-cpu", type=Path,
                        help="optional port/browser CPU observation JSONL sidecar")
    parser.add_argument("--bundle", type=Path,
                        help="optional raw bundle to recheck against the derived source hash")
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"), default="JITARM64")
    parser.add_argument("--output", type=Path, help="optional JSON report destination")
    args = parser.parse_args(argv)
    report = compare(args.derived, args.trace, trace_kind=args.trace_kind,
                     reference_cpu=args.reference_cpu, trace_cpu=args.trace_cpu,
                     bundle=args.bundle, cpu=args.cpu)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        try:
            output = args.output.expanduser()
            if output.exists() or output.is_symlink():
                parser.exit(2, f"refusing to overwrite existing report: {output}\n")
            output.parent.mkdir(parents=True, exist_ok=True)
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            if hasattr(os, "O_NOFOLLOW"):
                flags |= os.O_NOFOLLOW
            descriptor = os.open(output, flags, 0o600)
            try:
                with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                    descriptor = -1
                    stream.write(encoded)
                    stream.flush()
                    os.fsync(stream.fileno())
            finally:
                if descriptor >= 0:
                    os.close(descriptor)
        except OSError as error:
            parser.exit(2, f"cannot write report: {error}\n")
    print(encoded, end="")
    return status_exit_code(report["status"])


if __name__ == "__main__":
    raise SystemExit(main())
