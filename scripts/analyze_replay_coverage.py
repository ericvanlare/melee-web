#!/usr/bin/env python3
"""Analyze validated vanilla retail captures for marginal coverage."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from replay_coverage import (  # noqa: E402
    CPUS,
    DEFAULT_SOURCE_ROOT,
    CoverageError,
    build_report,
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Measure observed coverage from validated bounded vanilla captures. "
            "Each capture must be paired with its full input plan."
        )
    )
    parser.add_argument("--cpu", choices=CPUS, required=True)
    parser.add_argument("--capture", action="append", required=True,
                        help="candidate capture JSONL; repeat with --input-plan")
    parser.add_argument("--input-plan", action="append", required=True,
                        help="complete input plan paired by position with --capture")
    parser.add_argument("--held-out", action="append", default=[],
                        help="held-out capture JSONL; repeat with --held-out-input-plan")
    parser.add_argument("--held-out-input-plan", action="append", default=[],
                        help="complete plan paired by position with --held-out")
    parser.add_argument("--select", type=int, default=1, dest="select_limit",
                        help="maximum number of candidate captures to select")
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--output", type=Path, required=True)
    return parser


def _resolved(path: str | Path) -> Path:
    """Resolve aliases so output cannot replace an input through a symlink."""
    return Path(path).resolve()


def main(argv: list[str] | None = None) -> int:
    parser = _parser()
    args = parser.parse_args(argv)
    output_path = _resolved(args.output)
    all_inputs = [*map(_resolved, args.capture), *map(_resolved, args.input_plan),
                  *map(_resolved, args.held_out), *map(_resolved, args.held_out_input_plan)]
    if output_path in all_inputs:
        parser.error("--output must not overwrite a capture or input plan")
    if len(args.capture) != len(args.input_plan):
        parser.error("each --capture requires one --input-plan in the same order")
    if len(args.held_out) != len(args.held_out_input_plan):
        parser.error(
            "each --held-out requires one --held-out-input-plan in the same order")
    try:
        report = build_report(
            args.capture,
            input_plans=args.input_plan,
            cpu=args.cpu,
            held_out=args.held_out,
            held_out_input_plans=args.held_out_input_plan,
            select_limit=args.select_limit,
            source_root=args.source_root,
        )
    except (CoverageError, OSError, ValueError) as error:
        parser.error(str(error))
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(
        f"wrote {output_path} ({len(report['candidate_captures'])} candidates, "
        f"{len(report['selected'])} selected, {len(report['held_out'])} held-out; "
        f"gold_admitted={report['gold_admitted']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
