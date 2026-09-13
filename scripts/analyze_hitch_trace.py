#!/usr/bin/env python3
"""Correlate a saved diagnostic hitch report with a saved Chrome trace.

This command is offline-only.  It reads the report, trace and optional
``trace-metadata.json`` sidecar and writes a JSON evidence report.  Unknown
marker alignment, missing thread clocks, and trace loss remain explicit in the
output; no hitch cause is automatically classified.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from hitch_trace import (  # noqa: E402
    DEFAULT_MAX_DECOMPRESSED_BYTES,
    DEFAULT_TOP_N,
    HitchTraceError,
    analyze_capture,
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=Path,
                        help="saved browser report containing diagnostic_capture.events")
    parser.add_argument("--trace", required=True, type=Path,
                        help="CDP trace JSON or gzip-compressed JSON")
    parser.add_argument("--trace-metadata", "--metadata", dest="trace_metadata", type=Path,
                        help="optional trace-metadata.json from Tracing.tracingComplete")
    parser.add_argument("--output", type=Path,
                        help="write analysis JSON here as well as stdout")
    parser.add_argument("--top-n", type=int, default=DEFAULT_TOP_N,
                        help=f"maximum context slices per event (default {DEFAULT_TOP_N})")
    parser.add_argument("--max-decompressed-bytes", type=int,
                        default=DEFAULT_MAX_DECOMPRESSED_BYTES,
                        help="maximum decompressed trace bytes")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    try:
        report = json.loads(args.report.read_text(encoding="utf-8"))
        result = analyze_capture(
            report,
            args.trace,
            args.trace_metadata,
            top_n=args.top_n,
            max_decompressed_bytes=args.max_decompressed_bytes,
        )
    except (OSError, UnicodeError, json.JSONDecodeError, HitchTraceError) as error:
        parser.error(str(error))
    encoded = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x', encoding='utf-8') as output:
            output.write(encoded)
    print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
