#!/usr/bin/env python3
"""Calibrate a changed retail collector against an immutable trajectory.

This is a deliberately narrow gate for replacing the collector implementation
while keeping the pinned Dolphin execution and observed trajectory unchanged.
The two original captures must first pass the normal strict repeatability
validator. The candidate is then compared with reference A field by field;
its collector identity and explicitly selected CPU backend may differ. No input, state, or trace data is
rewritten while validating or reporting a result. CPU profile changes are
permitted only when both the requested profile and the capture provenance
identify the explicitly selected, supported backends.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from retail_replay_validation import (
    CaptureError,
    _first_difference,
    _initial_semantics,
    _lower_hex_strings,
    compare_validated,
    load_capture,
)


SCOPE = (
    "collector calibration on this trajectory only; no broad equivalence, "
    "performance acceptance, or gold admission"
)
STATUS = "collector_calibrated"
SUPPORTED_CPUS = ("Interpreter64", "JITARM64")
_MISSING = object()


def _read_hash(path: str | Path) -> str | None:
    try:
        return hashlib.sha256(Path(path).expanduser().resolve().read_bytes()).hexdigest()
    except OSError:
        return None


def _capture_metadata(capture, raw_hash: str | None) -> dict[str, Any]:
    return {
        "sha256": capture.sha256 or raw_hash,
        "capture_id": capture.header["capture_id"],
        "collector_sha256": capture.header["collector_sha256"],
        "input_phase": capture.header["input_phase"],
        "frames_requested": capture.header["frames_requested"],
        "version": capture.header["version"],
        "cpu": capture.header["provenance"].get("cpu"),
        # Retain the complete declared provenance, including every optional
        # input/setup hash, alongside the immutable capture hash.
        "provenance": dict(capture.header["provenance"]),
    }


def _base_report(reference_a_hash: str | None, reference_b_hash: str | None,
                 candidate_hash: str | None) -> dict[str, Any]:
    return {
        "status": "invalid_calibration",
        "calibrated": False,
        "gold_admitted": False,
        "performance": "not_evaluated",
        "scope": SCOPE,
        "capture_hashes": {
            "reference_a": reference_a_hash,
            "reference_b": reference_b_hash,
            "candidate": candidate_hash,
        },
        "reference_repeatability": "not_checked",
        "candidate_validation": "not_checked",
        "first_divergence": None,
    }


def _difference(expected: Any, actual: Any, root: str) -> dict[str, Any] | None:
    """Return a machine-readable first difference without changing either input."""

    difference = _first_difference(_lower_hex_strings(expected),
                                   _lower_hex_strings(actual), root)
    if difference is None:
        return None
    # Report the actual semantic difference, not an earlier harmless hex-case
    # difference. The original capture bytes are independently hash-bound.
    field, expected_value, actual_value = difference
    return {"record": root.split(".", 1)[0], "frame": None,
            "field": field, "expected": expected_value, "actual": actual_value}


def _frame_difference(reference, candidate) -> dict[str, Any] | None:
    for index, (expected, actual) in enumerate(zip(reference.frames, candidate.frames)):
        difference = _difference(expected, actual, f"frame[{index}]")
        if difference:
            difference["record"] = "frame"
            difference["frame"] = index
            return difference
    return None


def _provenance_difference(reference, candidate, *, allow_cpu_change: bool) -> dict[str, Any] | None:
    # The validator already enforces EXPECTED_PROVENANCE on both captures.  A
    # full comparison additionally binds optional setup/input hashes and their
    # presence to the immutable reference run.
    reference_provenance = dict(reference.header["provenance"])
    candidate_provenance = dict(candidate.header["provenance"])
    if allow_cpu_change:
        reference_cpu = reference_provenance.get("cpu")
        candidate_cpu = candidate_provenance.get("cpu")
        reference_provenance.pop("cpu", None)
        candidate_provenance.pop("cpu", None)
        # The marker is optional metadata, but it is valid only on the
        # explicitly selected JIT side. Remove it from the profile comparison
        # only after checking both sides, so it cannot smuggle a profile
        # change into an Interpreter capture.
        for label, provenance, cpu in (
                ("reference", reference_provenance, reference_cpu),
                ("candidate", candidate_provenance, candidate_cpu)):
            marker = provenance.pop("experimental_reference_backend", _MISSING)
            if marker is not _MISSING and (cpu != "JITARM64" or marker is not True):
                return {
                    "record": "header", "frame": None,
                    "field": f"header.{label}.provenance.experimental_reference_backend",
                    "expected": True, "actual": marker,
                }
    return _difference(reference_provenance, candidate_provenance,
                       "header.provenance")


def _collector_identity(reference_a, reference_b, candidate) -> dict[str, Any]:
    reference_hash = reference_a.header["collector_sha256"]
    candidate_hash = candidate.header["collector_sha256"]
    return {
        "reference_a": reference_hash,
        "reference_b": reference_b.header["collector_sha256"],
        "candidate": candidate_hash,
        "reference_pair_stable": reference_hash == reference_b.header["collector_sha256"],
        "candidate_differs": candidate_hash != reference_hash,
        "difference_permitted": True,
    }


def _compare_candidate(reference, candidate, *, allow_cpu_change: bool) -> dict[str, Any] | None:
    """Compare trajectory data across explicitly recorded collector boundaries.

    Both input-phase labels have already passed the supported-schema gate.
    Calibration may move from entry-queue observation to the protected consumed
    slot, but every observed PAD vector and subsequent state must remain exact.
    Ordinary repeat pairs still require identical input phases.
    """

    if reference.header["version"] != candidate.header["version"]:
        return {"record": "header", "frame": None, "field": "header.version",
                "expected": reference.header["version"],
                "actual": candidate.header["version"]}
    if reference.header["frames_requested"] != candidate.header["frames_requested"]:
        return {"record": "header", "frame": None,
                "field": "header.frames_requested",
                "expected": reference.header["frames_requested"],
                "actual": candidate.header["frames_requested"]}

    difference = _provenance_difference(
        reference, candidate, allow_cpu_change=allow_cpu_change)
    if difference:
        return difference
    difference = _difference(reference.match_enter, candidate.match_enter,
                             "match_enter")
    if difference:
        return difference
    difference = _difference(_initial_semantics(reference),
                             _initial_semantics(candidate),
                             "match_enter_complete")
    if difference:
        return difference
    difference = _frame_difference(reference, candidate)
    if difference:
        return difference
    return _difference(reference.end, candidate.end, "end")


def _invalid(report: dict[str, Any], capture: str, error: str,
             *, phase: str | None = None) -> dict[str, Any]:
    report.update({
        "status": "invalid_calibration",
        "calibrated": False,
        "invalid_capture": {"capture": capture, "error": error},
    })
    if phase is not None:
        report["failed_phase"] = phase
    return report


def calibrate(reference_a: str | Path, reference_b: str | Path,
              candidate: str | Path, *, reference_cpu: str = "Interpreter64",
              candidate_cpu: str = "Interpreter64") -> dict[str, Any]:
    """Run the read-only collector calibration gate and return a report."""

    report = _base_report(_read_hash(reference_a), _read_hash(reference_b),
                          _read_hash(candidate))
    report["reference_cpu_requested"] = reference_cpu
    report["candidate_cpu_requested"] = candidate_cpu
    if reference_cpu not in SUPPORTED_CPUS:
        return _invalid(report, "reference",
                        f"unsupported reference CPU {reference_cpu!r}; "
                        f"choose one of {', '.join(SUPPORTED_CPUS)}",
                        phase="reference_cpu")
    if candidate_cpu not in SUPPORTED_CPUS:
        return _invalid(report, "candidate",
                        f"unsupported candidate CPU {candidate_cpu!r}; "
                        f"choose one of {', '.join(SUPPORTED_CPUS)}",
                        phase="candidate_cpu")
    try:
        reference_a_capture = load_capture(reference_a, cpu=reference_cpu)
    except CaptureError as error:
        return _invalid(report, "reference_a", str(error), phase="reference_validation")
    try:
        reference_b_capture = load_capture(reference_b, cpu=reference_cpu)
    except CaptureError as error:
        return _invalid(report, "reference_b", str(error), phase="reference_validation")

    report['capture_hashes'].update(reference_a=reference_a_capture.sha256,
                                    reference_b=reference_b_capture.sha256)

    repeatability = compare_validated(reference_a_capture, reference_b_capture)
    report["reference_repeatability"] = repeatability.get("status")
    report["reference_report"] = repeatability
    if repeatability.get("status") != "repeatable":
        report["failed_phase"] = "reference_repeatability"
        report["error"] = "original reference pair is not strictly repeatable"
        return report
    reference_provenance_difference = _provenance_difference(
        reference_a_capture, reference_b_capture, allow_cpu_change=False)
    if reference_provenance_difference:
        report.update(failed_phase='reference_provenance',
                      first_divergence=reference_provenance_difference,
                      error='Original reference pair has different declared provenance')
        return report

    try:
        candidate_capture = load_capture(candidate, cpu=candidate_cpu)
    except CaptureError as error:
        return _invalid(report, "candidate", str(error), phase="candidate_validation")
    report["candidate_validation"] = "pass"
    report['capture_hashes']['candidate'] = candidate_capture.sha256

    report["captures"] = {
        "reference_a": _capture_metadata(reference_a_capture, report["capture_hashes"]["reference_a"]),
        "reference_b": _capture_metadata(reference_b_capture, report["capture_hashes"]["reference_b"]),
        "candidate": _capture_metadata(candidate_capture, report["capture_hashes"]["candidate"]),
    }
    reference_plan = reference_a_capture.header["provenance"].get("input_plan_sha256")
    candidate_plan = candidate_capture.header["provenance"].get("input_plan_sha256")
    report["input_plan_identity"] = {
        "reference": reference_plan,
        "candidate": candidate_plan,
        "match": reference_plan == candidate_plan,
    }
    report["collector_identity_change"] = _collector_identity(
        reference_a_capture, reference_b_capture, candidate_capture)
    reference_actual_cpu = reference_a_capture.header["provenance"]["cpu"]
    candidate_actual_cpu = candidate_capture.header["provenance"]["cpu"]
    report["cpu_identity_change"] = {
        "reference": reference_actual_cpu,
        "candidate": candidate_actual_cpu,
        "reference_requested": report["reference_cpu_requested"],
        "candidate_requested": candidate_cpu,
        "changed": candidate_actual_cpu != reference_actual_cpu,
        "difference_permitted": candidate_actual_cpu != reference_actual_cpu,
        "changed_fields": (["cpu"] if candidate_actual_cpu != reference_actual_cpu else []),
    }

    candidate_id = candidate_capture.header["capture_id"].lower()
    reference_ids = {
        reference_a_capture.header["capture_id"].lower(),
        reference_b_capture.header["capture_id"].lower(),
    }
    if candidate_id in reference_ids:
        return _invalid(
            report, "candidate",
            "candidate capture_id must be distinct from both immutable reference captures",
            phase="candidate_identity")

    difference = _compare_candidate(
        reference_a_capture, candidate_capture,
        allow_cpu_change=(candidate_actual_cpu != reference_actual_cpu))
    if difference is not None:
        report.update({
            "status": "diverged",
            "calibrated": False,
            "first_divergence": difference,
            "error": "collector candidate diverges from the repeated reference trajectory",
        })
        return report

    report.update({
        "status": STATUS,
        "calibrated": True,
        "frames_compared": len(candidate_capture.frames),
        "checks": {
            "reference_repeatability": "pass",
            "candidate_schema": "pass",
            "candidate_capture_id": "distinct",
            "version": "pass",
            "frame_count": "pass",
            "input_plan_identity": "pass",
            "pinned_runtime_profile": "pass",
            "cpu_identity": ("intentional_change" if candidate_actual_cpu != reference_actual_cpu
                              else "pass"),
            "entry": "pass",
            "initial_semantics": "pass",
            "frames_and_inputs": "pass",
            "end": "pass",
        },
    })
    return report


def status_exit_code(status: str) -> int:
    return {STATUS: 0, "diverged": 1, "invalid_calibration": 2}.get(status, 2)


def cli_main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Calibrate a changed retail collector on a repeated trajectory")
    parser.add_argument("--reference-a", required=True, type=Path)
    parser.add_argument("--reference-b", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--reference-cpu", choices=SUPPORTED_CPUS,
                        default="Interpreter64",
                        help="CPU backend used by both immutable reference captures")
    parser.add_argument("--candidate-cpu", choices=SUPPORTED_CPUS,
                        default="Interpreter64",
                        help="CPU backend used by the candidate capture")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    if args.output.expanduser().resolve() in {
            p.expanduser().resolve() for p in (args.reference_a, args.reference_b, args.candidate)}:
        parser.error('Output cannot overwrite an input capture')
    report = calibrate(args.reference_a, args.reference_b, args.candidate,
                       reference_cpu=args.reference_cpu,
                       candidate_cpu=args.candidate_cpu)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    try:
        args.output.expanduser().resolve().parent.mkdir(parents=True, exist_ok=True)
        args.output.expanduser().resolve().write_text(encoded, encoding="utf-8")
    except OSError as error:
        parser.exit(2, f"cannot write report: {error}\n")
    print(encoded, end="")
    return status_exit_code(report["status"])


def main() -> int:
    return cli_main()


if __name__ == "__main__":
    raise SystemExit(main())
