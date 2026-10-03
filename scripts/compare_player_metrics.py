#!/usr/bin/env python3
"""Compare player-experience metrics from two browser smoke reports.

Both reports must come from scripts/browser_smoke.mjs --disc on the same named
machine and browser. A flagged regression is a prompt to investigate, not an
accuracy or performance-admission result. See docs/PLAYER_EXPERIENCE_METRICS.md.
"""
from pathlib import Path
import argparse
import json
import math
import sys

SCHEMA = "melee-web-player-metrics-v1"
# (label, extractor, absolute slack, relative slack). A value regresses when it
# grows by more than both slacks; small absolute noise never flags.
METRICS = [
    ("page → import ready (ms)", lambda m: m.get("page_to_import_ready_ms"), 250, 0.20),
    ("cold disc → CSS (ms)", lambda m: _attempt(m, "cold", "disc_to_css_ms"), 500, 0.15),
    ("warm disc → CSS (ms)", lambda m: _attempt(m, "warm", "disc_to_css_ms"), 500, 0.15),
    ("longest task, cold import (ms)", lambda m: _attempt(m, "cold", "longest_task_ms"), 100, 0.25),
    ("long-task total, cold import (ms)", lambda m: _attempt(m, "cold", "longtask_total_ms"), 250, 0.25),
    ("wasm heap at CSS (MiB)", lambda m: _mib(_attempt(m, "cold", "wasm_heap_bytes")), 16, 0.10),
    ("JS heap at CSS (MiB)", lambda m: _mib(_attempt(m, "cold", "js_heap_used_bytes")), 16, 0.20),
]


def _attempt(metrics, cache, key):
    for attempt in metrics.get("attempts", []):
        if attempt.get("cache") == cache:
            return attempt.get(key)
    return None


def _mib(value):
    return None if value is None else round(value / 1048576, 1)


def _is_finite_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _require_paired_disc_to_css(path, metrics):
    """Reject reports that cannot support the cold/warm comparison below."""
    values = {cache: _attempt(metrics, cache, "disc_to_css_ms") for cache in ("cold", "warm")}
    if any(not _is_finite_number(value) or value < 0 for value in values.values()):
        raise ValueError(
            f"{path}: player_metrics requires valid cold and warm disc_to_css_ms measurements"
        )


def load(path):
    report = json.loads(Path(path).read_text(encoding="utf-8"))
    metrics = report.get("player_metrics")
    if not metrics or metrics.get("schema") != SCHEMA:
        raise ValueError(f"{path}: no {SCHEMA} player_metrics; rerun browser_smoke.mjs with --disc")
    if report.get("result") != "pass":
        raise ValueError(f"{path}: smoke result is {report.get('result')!r}; compare only passing runs")
    _require_paired_disc_to_css(path, metrics)
    return report, metrics


def compare(baseline, candidate):
    rows, regressions = [], []
    for label, extract, absolute, relative in METRICS:
        before, after = extract(baseline), extract(candidate)
        if before is None or after is None:
            rows.append((label, before, after, "", "unavailable"))
            continue
        delta = after - before
        regressed = delta > absolute and delta > relative * max(before, 1)
        rows.append((label, before, after, f"{delta:+g}", "REGRESSED" if regressed else "ok"))
        if regressed:
            regressions.append(label)
    return rows, regressions


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args(argv)
    (base_report, baseline), (cand_report, candidate) = load(args.baseline), load(args.candidate)
    if base_report.get("browser") != cand_report.get("browser"):
        print(f"warning: browser differs ({base_report.get('browser')} vs {cand_report.get('browser')})",
              file=sys.stderr)
    rows, regressions = compare(baseline, candidate)
    print("| Metric | Baseline | Candidate | Δ | |\n| --- | ---: | ---: | ---: | --- |")
    for label, before, after, delta, verdict in rows:
        print(f"| {label} | {'' if before is None else before} | {'' if after is None else after} | {delta} | {verdict} |")
    return 1 if regressions else 0


if __name__ == "__main__":
    sys.exit(main())
