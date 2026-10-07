#!/usr/bin/env python3
"""Run the strict whole-session MWRO/MWRC/browser state comparator."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from whole_session_state_compare import compare_paths  # noqa: E402


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True, type=Path, help="source capture.mwro")
    parser.add_argument("--recipe", required=True, type=Path, help="source candidate.mwrc")
    parser.add_argument("--port-trace", required=True, type=Path, help="browser retail-port.jsonl")
    parser.add_argument("--browser-report", type=Path,
                        help="optional browser completion report with bound final_scene")
    parser.add_argument("--scope", choices=("whole-session", "v10-first-setup-tick0",
                                            "v10-first-positive-match-frame",
                                            "v10-first-match-clock-ge60"),
                        default="whole-session", help="comparison scope (default: whole-session)")
    parser.add_argument("--expectations", type=Path,
                        help="frozen expectations packet for a bounded v10 prefix scope")
    parser.add_argument("--source-manifest", type=Path,
                        help="source MWRC candidate manifest for the bounded v10 scope")
    parser.add_argument("--source-report", type=Path,
                        help="source capture report for the bounded v10 scope")
    parser.add_argument("--source-audit", type=Path,
                        help="bounded first-setup identity audit for the v10 scope")
    parser.add_argument("--positive-boundary-audit", type=Path,
                        help="bounded first-positive match-frame source audit for its explicit v10 scope")
    parser.add_argument("--clock60-boundary-audit", type=Path,
                        help="bounded first-match clock-60 source audit for its explicit v10 scope")
    parser.add_argument("--browser-capture-report", type=Path,
                        help="browser capture wrapper report for the bounded v10 scope")
    parser.add_argument("--browser-producer-manifest", type=Path,
                        help="browser producer source manifest for the bounded v10 scope")
    parser.add_argument("--out", required=True, type=Path, help="new JSON report path")
    args = parser.parse_args(argv)
    if args.out.exists() or args.out.is_symlink():
        parser.error(f"refusing to overwrite existing report {args.out}")
    report = compare_paths(args.reference, args.recipe, args.port_trace,
                           browser_report=args.browser_report, scope=args.scope,
                           expectations=args.expectations,
                           source_manifest=args.source_manifest,
                           source_report=args.source_report,
                           source_audit=args.source_audit,
                           browser_capture_report=args.browser_capture_report,
                           browser_producer_manifest=args.browser_producer_manifest,
                           positive_boundary_audit=args.positive_boundary_audit,
                           clock60_boundary_audit=args.clock60_boundary_audit)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {"inputs", "boundary_checkpoints"}},
                     indent=2, sort_keys=True))
    return {"equivalent": 0, "incomplete": 1, "diverged": 1, "invalid": 2}.get(
        report.get("result"), 2)


if __name__ == "__main__":
    raise SystemExit(main())
