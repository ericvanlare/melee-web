#!/usr/bin/env python3
"""Summarize recorded browser replay failures into JSON and Markdown."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from browser_failure_summary import SummaryError, write_summary  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_directory", type=Path,
                        help="existing browser replay output directory")
    parser.add_argument("--out", required=True, type=Path,
                        help="new output directory for summary.json and summary.md")
    args = parser.parse_args(argv)
    try:
        summary = write_summary(args.run_directory, args.out)
    except (OSError, SummaryError) as error:
        parser.exit(2, f"browser failure summary: {error}\n")
    print(f"summary generated: {args.out / 'summary.md'} ({summary['run_outcome']['classification']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
