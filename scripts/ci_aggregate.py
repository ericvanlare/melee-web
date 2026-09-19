#!/usr/bin/env python3
"""Validate the partition evidence emitted by ``scripts/ci_verify.py``."""

import argparse
import hashlib
import json
from pathlib import Path
import sys

from ci_verify import GROUPS, UNIT_GROUPS


def _canonical_hash(ids):
    return hashlib.sha256("\n".join(sorted(ids)).encode("utf-8")).hexdigest()


def _inventory(report, key, *, require_ids=False):
    value = report.get(key)
    if not isinstance(value, dict):
        raise ValueError(f"{key} inventory is missing or not an object")
    count = value.get("count")
    digest = value.get("sha256")
    if type(count) is not int or count < 0 or (key == "discovered" and count == 0):
        raise ValueError(f"{key} inventory count is invalid")
    if not isinstance(digest, str) or len(digest) != 64:
        raise ValueError(f"{key} inventory sha256 is invalid")
    try:
        int(digest, 16)
    except ValueError as exc:
        raise ValueError(f"{key} inventory sha256 is invalid") from exc
    result = {"count": count, "sha256": digest}
    if require_ids:
        ids = value.get("ids")
        if not isinstance(ids, list) or any(not isinstance(test_id, str) for test_id in ids):
            raise ValueError(f"{key} inventory ids must be a list of strings")
        if len(ids) != len(set(ids)):
            raise ValueError(f"{key} inventory ids contain duplicates")
        if count != len(ids):
            raise ValueError(f"{key} inventory count does not match ids")
        expected = _canonical_hash(ids)
        if digest != expected:
            raise ValueError(f"{key} inventory sha256 does not match sorted ids")
        result["ids"] = ids
    return result


def _read_report(directory, group):
    path = directory / f"{group}.json"
    if not path.is_file():
        raise ValueError(f"missing partition report: {path}")
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read partition report {path}: {exc}") from exc
    if not isinstance(report, dict):
        raise ValueError(f"partition report is not an object: {path}")
    if report.get("status") != "passed":
        raise ValueError(f"partition {group} did not pass: {report.get('status')!r}")
    if report.get("group") != group:
        raise ValueError(f"partition report group mismatch for {group}")
    return report


def validate_reports(directory, expected_commit):
    """Validate all expected reports and return a compact aggregate summary."""
    directory = Path(directory)
    reports = {group: _read_report(directory, group) for group in GROUPS}
    for group, report in reports.items():
        if report.get("commit") != expected_commit:
            raise ValueError(f"partition {group} has stale commit {report.get('commit')!r}")
        targets = report.get("targets")
        if not isinstance(targets, list) or targets != list(GROUPS[group]):
            raise ValueError(f"partition {group} target list does not match GROUPS")

    discovered = {group: _inventory(reports[group], "discovered") for group in UNIT_GROUPS}
    if discovered[UNIT_GROUPS[0]] != discovered[UNIT_GROUPS[1]]:
        raise ValueError("unit discovered inventories do not match")

    selected = {
        group: _inventory(reports[group], "selected", require_ids=True)
        for group in UNIT_GROUPS
    }
    first_ids = set(selected[UNIT_GROUPS[0]]["ids"])
    second_ids = set(selected[UNIT_GROUPS[1]]["ids"])
    overlap = sorted(first_ids & second_ids)
    if overlap:
        raise ValueError(f"unit selected inventories overlap: {overlap}")
    union = first_ids | second_ids
    expected = discovered[UNIT_GROUPS[0]]
    if len(union) != expected["count"]:
        raise ValueError("unit selected union count does not match discovered inventory")
    if _canonical_hash(union) != expected["sha256"]:
        raise ValueError("unit selected union sha256 does not match discovered inventory")

    return {
        "status": "passed",
        "commit": expected_commit,
        "groups": list(GROUPS),
        "discovered": expected,
        "selected": {
            group: {"count": value["count"], "sha256": value["sha256"]}
            for group, value in selected.items()
        },
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args(argv)
    try:
        summary = validate_reports(args.reports, args.commit)
    except ValueError as exc:
        print(f"CI aggregate validation failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
