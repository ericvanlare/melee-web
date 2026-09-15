#!/usr/bin/env python3
"""Ingest one accepted human capture and derive a diagnostic MWRC replay."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from reference_capture_replay import ReplayError, ingest_and_derive  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle", required=True, type=Path, help="finalized acceptedunprocessed bundle directory")
    parser.add_argument("--derived-root", required=True, type=Path, help="new directory for immutable derived artifacts")
    parser.add_argument("--cpu", choices=("JITARM64",), default="JITARM64")
    args = parser.parse_args(argv)
    try:
        report = ingest_and_derive(args.bundle, args.derived_root, cpu=args.cpu)
    except ReplayError as error:
        print(json.dumps({"status": "failed", "error": str(error)}, sort_keys=True), file=sys.stderr)
        return 2
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
