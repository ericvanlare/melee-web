"""Compare one hash-bound original capture with one port or browser trace.

This is a one-sided diagnostic.  It validates the derived candidate and then
uses the existing port and CPU observation validators against one target.  It
does not manufacture a second original run, infer repeatability, or admit a
gold reference.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any, Mapping

from cpu_observation_validation import (
    _compare as compare_cpu_observations,
    domain_divergences,
    load_observation,
)
from port_replay_validation import _read_port_rows, compare_rows
from reference_session_bundle import _canonical_bytes, _final_manifest
from retail_replay_validation import CaptureError, load_capture


SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
DERIVED_SCHEMA = "webmelee-reference-capture-diagnostic-replay-v1"
REPORT_SCHEMA = "melee-web-reference-capture-comparison"


class ComparisonError(ValueError):
    """The derived evidence or comparison input is not safe to compare."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ComparisonError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise ComparisonError(f"{path}: expected a JSON object")
    return value


def _safe_artifact_name(value: Any) -> str:
    if (not isinstance(value, str) or not value or value in {".", ".."}
            or "/" in value or "\\" in value):
        raise ComparisonError(f"derived artifact name is unsafe: {value!r}")
    return value


def _manifest_hash(manifest: Mapping[str, Any]) -> str:
    unsigned = dict(manifest)
    unsigned.pop("manifest_sha256", None)
    return hashlib.sha256(_canonical_bytes(unsigned)).hexdigest()


def validate_derived(derived: str | Path, *, bundle: str | Path | None = None) -> dict[str, Any]:
    """Validate a derived directory and return its binding metadata.

    The derived manifest is checked before any candidate is loaded.  Every
    declared artifact must be a direct regular file with the recorded size,
    digest, and raw source-manifest binding; extra files and symlinks fail.
    When ``bundle`` is supplied, its finalized raw manifest is also checked
    against the derived source digest.
    """

    input_root = Path(derived).expanduser()
    if input_root.is_symlink():
        raise ComparisonError(f"derived directory is a symlink: {input_root}")
    root = input_root.resolve()
    if not root.is_dir():
        raise ComparisonError(f"derived directory is missing or unsafe: {root}")
    manifest_path = root / "derived-manifest.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ComparisonError("derived-manifest.json is missing or unsafe")
    manifest = _read_json(manifest_path)
    if manifest.get("schema") != DERIVED_SCHEMA or manifest.get("version") != 1:
        raise ComparisonError("unsupported derived replay schema")
    manifest_sha256 = manifest.get("manifest_sha256")
    if not isinstance(manifest_sha256, str) or not SHA256_RE.fullmatch(manifest_sha256):
        raise ComparisonError("derived manifest has no valid binding hash")
    if _manifest_hash(manifest) != manifest_sha256:
        raise ComparisonError("derived manifest binding hash mismatch")
    source = manifest.get("source")
    if not isinstance(source, dict):
        raise ComparisonError("derived manifest has no source binding")
    source_manifest_sha256 = source.get("manifest_sha256")
    if (not isinstance(source_manifest_sha256, str)
            or not SHA256_RE.fullmatch(source_manifest_sha256)):
        raise ComparisonError("derived source manifest hash is invalid")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ComparisonError("derived manifest has no artifacts")
    declared: set[str] = set()
    for item in artifacts:
        if not isinstance(item, dict):
            raise ComparisonError("derived artifact entry is not an object")
        name = _safe_artifact_name(item.get("name"))
        if name in declared or name == "derived-manifest.json":
            raise ComparisonError(f"duplicate or reserved derived artifact: {name}")
        declared.add(name)
        artifact = root / name
        if artifact.is_symlink() or not artifact.is_file():
            raise ComparisonError(f"derived artifact is missing or unsafe: {name}")
        if item.get("bytes") != artifact.stat().st_size:
            raise ComparisonError(f"derived artifact byte count mismatch: {name}")
        if item.get("sha256") != _sha256(artifact):
            raise ComparisonError(f"derived artifact hash mismatch: {name}")
        if item.get("source_manifest_sha256") != source_manifest_sha256:
            raise ComparisonError(f"derived artifact source binding mismatch: {name}")
    observed = set()
    for child in root.iterdir():
        if child.name == "derived-manifest.json":
            continue
        if child.is_symlink() or not child.is_file():
            raise ComparisonError(f"derived directory contains unsafe entry: {child.name}")
        observed.add(child.name)
    if observed != declared:
        raise ComparisonError(
            "derived artifact inventory mismatch: "
            f"missing={sorted(declared - observed)}, extra={sorted(observed - declared)}"
        )
    if bundle is not None:
        raw_input = Path(bundle).expanduser()
        if raw_input.is_symlink():
            raise ComparisonError(f"raw bundle is a symlink: {raw_input}")
        raw_root = raw_input.resolve()
        raw_manifest, errors = _final_manifest(raw_root)
        if errors or raw_manifest is None:
            raise ComparisonError("raw bundle manifest is invalid: " + "; ".join(errors))
        if raw_manifest.get("manifest_sha256") != source_manifest_sha256:
            raise ComparisonError("derived source binding does not match raw bundle")
    candidate_path = root / "candidate.jsonl"
    if not candidate_path.is_file() or candidate_path.is_symlink():
        raise ComparisonError("derived candidate.jsonl is missing or unsafe")
    return {
        "root": root,
        "manifest": manifest,
        "manifest_sha256": manifest_sha256,
        "source_manifest_sha256": source_manifest_sha256,
        "candidate_path": candidate_path,
        "candidate_sha256": _sha256(candidate_path),
    }


def _cpu_report(native_path: Path, target_path: Path) -> dict[str, Any]:
    try:
        native = load_observation(native_path)
        target = load_observation(target_path)
    except (OSError, ValueError) as error:
        raise ComparisonError(f"CPU sidecar validation failed: {error}") from error
    first = compare_cpu_observations(native, target, drawing=target.header["source_drawing"])
    domains = domain_divergences(native, target)
    return {
        "status": "matched" if first is None else "diverged",
        "native_sha256": native.sha256,
        "target_sha256": target.sha256,
        "native_frames": len(native.frames),
        "target_frames": len(target.frames),
        "native_draws": len(native.draws),
        "target_draws": len(target.draws),
        "first_divergence": first,
        "first_divergence_by_domain": domains,
        "coverage": {
            domain: {"status": "matched" if value is None else "diverged",
                     "first_divergence": value}
            for domain, value in domains.items()
            if domain != "draw_alignment"
        },
        "draw_alignment": domains.get("draw_alignment"),
    }


def compare(
    derived: str | Path,
    trace: str | Path,
    *,
    trace_kind: str = "browser",
    reference_cpu: str | Path | None = None,
    trace_cpu: str | Path | None = None,
    bundle: str | Path | None = None,
    cpu: str = "JITARM64",
) -> dict[str, Any]:
    """Compare one derived original capture with one port/browser trace."""

    report: dict[str, Any] = {
        "schema": REPORT_SCHEMA,
        "version": 1,
        "status": "invalid",
        "scope": (
            "single original capture versus one declared port/browser trace; "
            "state and observation diagnostics only"
        ),
        "claims": {
            "reference_repeatability": "not_established",
            "port_equivalence": "not_claimed",
            "performance_acceptance": "not_claimed",
            "gold_admission": "not_claimed",
        },
        "gold_admitted": False,
        "missing_coverage": ["cpu_observation"],
    }
    if trace_kind not in {"native", "browser"}:
        report["error"] = f"unsupported trace kind: {trace_kind}"
        return report
    if (reference_cpu is None) != (trace_cpu is None):
        report["error"] = "reference-cpu and trace-cpu must be supplied together"
        return report
    try:
        binding = validate_derived(derived, bundle=bundle)
        trace_input = Path(trace).expanduser()
        if trace_input.is_symlink():
            raise ComparisonError("trace is a symlink")
        trace_path = trace_input.resolve()
        if not trace_path.is_file():
            raise ComparisonError("trace is missing or unsafe")
        reference = load_capture(binding["candidate_path"], cpu=cpu)
        rows = _read_port_rows(trace_path.read_bytes(), trace_path)
        replay = compare_rows(reference, rows)
        report.update({
            "status": "matched" if replay.get("first_divergence") is None else "diverged",
            "derived": {
                "manifest_sha256": binding["manifest_sha256"],
                "source_manifest_sha256": binding["source_manifest_sha256"],
                "candidate_sha256": binding["candidate_sha256"],
            },
            "trace": {
                "kind": trace_kind,
                "sha256": _sha256(trace_path),
                "frames": len(rows) - 4 if len(rows) >= 4 else None,
            },
            "replay": replay,
            "coverage": {
                "frames_compared": replay.get("frames_compared", 0),
                "checks": replay.get("checks", {}),
                "first_divergence_by_group": replay.get("first_divergence_by_group", {}),
            },
        })
        if reference_cpu is None:
            report["coverage"]["cpu"] = {"status": "not_requested", "domains": {}}
        else:
            reference_cpu_input = Path(reference_cpu).expanduser()
            trace_cpu_input = Path(trace_cpu).expanduser()
            try:
                if reference_cpu_input.is_symlink() or trace_cpu_input.is_symlink():
                    raise ComparisonError("CPU observation input is a symlink")
                cpu_result = _cpu_report(reference_cpu_input.resolve(),
                                         trace_cpu_input.resolve())
            except ComparisonError as error:
                report["cpu"] = {
                    "status": "invalid_input",
                    "error": str(error),
                    "coverage": "not_compared",
                }
                report["coverage"]["cpu"] = {
                    "status": "invalid_input", "domains": {},
                }
                report["invalid_inputs"] = ["cpu_observation"]
                report["status"] = "invalid_input"
            else:
                report["cpu"] = cpu_result
                report["coverage"]["cpu"] = {
                    "status": cpu_result["status"],
                    "domains": cpu_result["coverage"],
                }
                report["missing_coverage"] = []
                if cpu_result["status"] == "diverged":
                    report["status"] = "diverged"
    except (ComparisonError, CaptureError, OSError, ValueError, UnicodeError) as error:
        report["error"] = str(error)
    return report


def status_exit_code(status: str) -> int:
    return {"matched": 0, "diverged": 1, "invalid": 2,
            "invalid_input": 2}.get(status, 2)
