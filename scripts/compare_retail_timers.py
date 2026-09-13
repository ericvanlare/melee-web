#!/usr/bin/env python3
"""Compare repeated retail timer sidecars and then a port timer sidecar."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from retail_timer_validation import compare_paths, status_exit_code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_a", type=Path)
    parser.add_argument("capture_b", type=Path)
    parser.add_argument("port", type=Path)
    binding = parser.add_mutually_exclusive_group(required=True)
    binding.add_argument("--setup-hex", help="canonical lowercase 0x138-byte StartMeleeData")
    binding.add_argument("--reference-capture", type=Path,
                         help="pinned retail capture supplying match_enter.start_melee_hex")
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"),
                        default="Interpreter64")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)

    inputs = [args.capture_a, args.capture_b, args.port]
    if args.reference_capture is not None:
        inputs.append(args.reference_capture)
    if args.output is not None:
        output = args.output.resolve()
        if output in {path.resolve() for path in inputs}:
            parser.error("output cannot overwrite an input or reference capture")

    report = compare_paths(
        args.capture_a, args.capture_b, args.port,
        setup_hex=args.setup_hex, reference_capture=args.reference_capture,
        cpu=args.cpu)
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        except OSError as error:
            parser.exit(2, f"cannot write report: {error}\n")
    print(rendered, end="")
    return status_exit_code(report["status"])


if __name__ == "__main__":
    raise SystemExit(main())
