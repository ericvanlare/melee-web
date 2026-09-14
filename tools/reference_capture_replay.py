"""Turn one accepted human reference capture into a diagnostic replay.

The capture app records a single human-versus-CPU retail execution.  This
module extracts the source-owned semantic rows emitted by
``reference_capture_semantics.SemanticSession`` and validates them with the
existing strict retail candidate and MWRC validators.  It never pairs a run,
claims repeatability, or treats CPU decisions as replay input: only the PAD
queue consumed by the source is encoded into MWRC.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any, Iterable, Mapping

OBSERVER_ROOT = Path(__file__).resolve().parents[1] / "reference-capture/dolphin"
if str(OBSERVER_ROOT) not in sys.path:
    sys.path.insert(0, str(OBSERVER_ROOT))

from reference_capture_semantics import SemanticSession  # noqa: E402
from reference_observer_stream import iter_records  # noqa: E402
from cpu_observation_validation import load_observation  # noqa: E402
from retail_replay_recipe import RecipeError, encode_mwrc
from retail_replay_validation import (
    CaptureError,
    DEQUEUED_INPUT_PHASE,
    EXPECTED_PROVENANCE,
    GAME_REVISION,
    INITIAL_PHASE,
    PHASE,
    SCHEMA as CANDIDATE_SCHEMA,
    load_capture,
)
from reference_session_bundle import (
    ACCEPTED_UNPROCESSED,
    MANIFEST_NAME,
    RAW_OBSERVER_NAME,
    ReferenceCaptureInbox,
    _canonical_bytes,
    _final_manifest,
    _sha256_file,
    validate_bundle,
)
from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD


EXPECTED_DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"


class ReplayError(ValueError):
    """The raw capture cannot produce a safe diagnostic replay."""


def _read_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReplayError(f"cannot read JSON {path}: {error}") from error


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        raise ReplayError(f"cannot read semantic records {path}: {error}") from error
    records: list[dict[str, Any]] = []
    for index, line in enumerate(lines, 1):
        if not line.strip():
            raise ReplayError(f"semantic records line {index} is blank")
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ReplayError(f"semantic records line {index} is invalid JSON: {error}") from error
        if not isinstance(value, dict):
            raise ReplayError(f"semantic records line {index} is not an object")
        records.append(value)
    if not records:
        raise ReplayError("semantic records are empty")
    return records


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _hex(value: Any, length: int, context: str) -> str:
    if not isinstance(value, str) or re.fullmatch(rf"[0-9a-fA-F]{{{length * 2}}}", value) is None:
        raise ReplayError(f"{context} must contain exactly {length} bytes of hexadecimal data")
    return value.lower()


def _payload(record: Mapping[str, Any], index: int) -> dict[str, Any]:
    value = record.get("payload")
    if not isinstance(value, dict):
        raise ReplayError(f"semantic record {index} has no object payload")
    return value


def _record_event(record: Mapping[str, Any], index: int) -> str:
    event = record.get("event")
    if not isinstance(event, str) or not event:
        raise ReplayError(f"semantic record {index} has no typed event")
    return event


def _observer_environment(header: Mapping[str, Any]) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    environment = header.get("environment")
    if not isinstance(environment, Mapping):
        raise ReplayError("raw capture is missing the verified observer environment")
    disc = environment.get("disc")
    dolphin = environment.get("dolphin")
    if not isinstance(disc, Mapping) or not isinstance(dolphin, Mapping):
        raise ReplayError("raw capture environment has incomplete disc/Dolphin identity")
    expected_lengths = {"dol_sha1": 40, "dol_sha256": 64}
    for field, length in expected_lengths.items():
        value = disc.get(field)
        if (not isinstance(value, str) or
                re.fullmatch(rf"[0-9a-fA-F]{{{length}}}", value) is None):
            raise ReplayError(f"raw capture environment is missing {field}")
    if disc["dol_sha1"].lower() != EXPECTED_PROVENANCE["dol_sha1"]:
        raise ReplayError("raw capture environment is bound to an unsupported DOL SHA-1")
    if disc["dol_sha256"].lower() != EXPECTED_DOL_SHA256:
        raise ReplayError("raw capture environment is bound to an unsupported DOL SHA-256")
    revision = dolphin.get("source_revision")
    if not isinstance(revision, str) or not revision:
        raise ReplayError("raw capture environment is missing the Dolphin revision")
    return disc, dolphin


def _read_observer_records(path: Path, header: Mapping[str, Any]) -> list[dict[str, Any]]:
    """Decode and identity-check the immutable MWRO stream before derivation."""
    try:
        records = list(iter_records(path))
    except Exception as error:
        raise ReplayError(f"raw observer stream is malformed: {error}") from error
    if not records:
        raise ReplayError("raw observer stream is empty")
    first = records[0]
    if first.get("seq") != 0 or first.get("event") != "handshake":
        raise ReplayError("raw observer stream must begin with handshake sequence 0")
    payload = first.get("payload")
    if not isinstance(payload, Mapping):
        raise ReplayError("raw observer handshake payload is invalid")
    disc, dolphin = _observer_environment(header)
    expected = {
        "schema": "melee-web-passive-dolphin-observer",
        "version": 1,
        "dolphin_commit": dolphin["source_revision"],
        "dol_sha1": disc["dol_sha1"],
        "dol_sha256": disc["dol_sha256"],
        "cpu": "JITARM64",
        "writes_guest_memory": False,
    }
    for field, value in expected.items():
        if payload.get(field) != value:
            raise ReplayError(f"raw observer handshake identity mismatch: {field}")
    ring_capacity = payload.get("ring_capacity")
    if isinstance(ring_capacity, bool) or not isinstance(ring_capacity, int) or ring_capacity <= 0:
        raise ReplayError("raw observer handshake ring capacity is invalid")
    if len(records) < 3 or records[1].get("seq") != 1 or records[1].get("event") != "start":
        raise ReplayError("raw observer stream is missing its recording start")
    start = records[1].get("payload")
    if (not isinstance(start, Mapping) or start.get("status") != "recording" or
            start.get("source_revision") != "GALE01r2"):
        raise ReplayError("raw observer recording start is invalid")
    if any(record.get("event") == "error" for record in records):
        raise ReplayError("raw observer stream contains an error record")
    final = records[-1]
    if final.get("event") != "end":
        raise ReplayError("raw observer stream is missing its final end record")
    end = final.get("payload")
    if (not isinstance(end, Mapping) or end.get("status") != "completed" or
            end.get("natural") is not True):
        raise ReplayError("raw observer stream did not end with a natural completed match")
    return records


def _semantic_row(session: SemanticSession, record: Mapping[str, Any]) -> dict[str, Any]:
    converted = session.consume(record)
    if not isinstance(converted, dict):
        raise ReplayError("authoritative semantic decoder returned a non-object row")
    if record["event"] in ("handshake", "start", "progress", "error", "end"):
        converted["payload"] = record["payload"]
    if record["event"] == "end":
        converted["event"] = "observer_end"
    return converted


def _without_bundle_identity(value: Mapping[str, Any], header: Mapping[str, Any], index: int) -> dict[str, Any]:
    normalized = dict(value)
    for field in ("session_id", "game_id", "run_id"):
        if field in normalized:
            if normalized.pop(field) != header.get(field):
                raise ReplayError(f"semantic record {index} has mismatched {field}")
    return normalized


def _validate_authoritative_semantics(header: Mapping[str, Any], observer_path: Path,
                                      stored: list[dict[str, Any]]) -> dict[str, Any]:
    raw = _read_observer_records(observer_path, header)
    try:
        session = SemanticSession()
        regenerated = [_semantic_row(session, record) for record in raw]
        report = session.completion()
    except ReplayError:
        raise
    except Exception as error:
        raise ReplayError(f"authoritative semantic decoding failed: {error}") from error
    if not isinstance(report, Mapping) or report.get("complete") is not True:
        raise ReplayError("authoritative semantic session is incomplete")
    if len(regenerated) != len(stored):
        raise ReplayError("stored semantic record count does not match observer stream")
    for index, (expected, actual) in enumerate(zip(regenerated, stored)):
        expected = _without_bundle_identity(expected, header, index)
        actual = _without_bundle_identity(actual, header, index)
        if expected != actual:
            raise ReplayError(f"stored semantic record {index} differs from observer decode")
    return dict(report)


def _collector_hash(header: Mapping[str, Any], observer_path: Path) -> str:
    candidates: list[Any] = [header.get("collector_sha256"), header.get("observer_identity")]
    for container_name in ("provenance", "observer"):
        container = header.get(container_name)
        if isinstance(container, Mapping):
            candidates.extend((container.get("collector_sha256"), container.get("observer_identity"), container.get("sha256")))
    environment = header.get("environment")
    if isinstance(environment, Mapping):
        dolphin = environment.get("dolphin")
        if isinstance(dolphin, Mapping):
            candidates.extend((dolphin.get("observer_sha256"), dolphin.get("binary_sha256")))
    for candidate in candidates:
        if isinstance(candidate, str) and re.fullmatch(r"[0-9a-fA-F]{64}", candidate):
            return candidate.lower()
    # The observer byte stream is immutable and is retained beside the raw
    # bundle.  Its digest is a concrete collector identity when the bundle's
    # header did not include a separate source-code digest.
    try:
        return _sha256_file(observer_path)
    except OSError as error:
        raise ReplayError(f"cannot bind observer identity: {error}") from error


def _binary_hash(provenance: Mapping[str, Any], header: Mapping[str, Any]) -> str:
    candidates: list[Any] = [provenance.get("dolphin_binary_sha256"), header.get("dolphin_binary_sha256")]
    observer = header.get("observer")
    if isinstance(observer, Mapping):
        candidates.append(observer.get("dolphin_binary_sha256"))
    environment = header.get("environment")
    if isinstance(environment, Mapping):
        dolphin = environment.get("dolphin")
        if isinstance(dolphin, Mapping):
            candidates.append(dolphin.get("binary_sha256"))
    for candidate in candidates:
        if isinstance(candidate, str) and re.fullmatch(r"[0-9a-fA-F]{64}", candidate):
            return candidate.lower()
    raise ReplayError("raw capture is missing the pinned Dolphin binary hash")


def _provenance(header: Mapping[str, Any], *, cpu: str) -> dict[str, Any]:
    if cpu != "JITARM64":
        raise ReplayError("single-capture diagnostic replay requires JITARM64")
    supplied = header.get("provenance")
    if supplied is not None and not isinstance(supplied, Mapping):
        raise ReplayError("raw provenance is not an object")
    supplied = dict(supplied or {})
    result = dict(EXPECTED_PROVENANCE)
    result["cpu"] = cpu
    # The checked validator pins every execution setting.  Copy optional
    # identity hashes from the private header but never copy local paths.
    result["dolphin_binary_sha256"] = _binary_hash(supplied, header)
    for key in ("Dolphin.ini_sha256", "GCPadNew.ini_sha256", "input_plan_sha256"):
        value = supplied.get(key)
        if value is not None:
            result[key] = _hex(value, 32, f"provenance.{key}")
    return result


def _capture_id(raw_manifest_sha256: str) -> str:
    value = raw_manifest_sha256[:32]
    value = value[:12] + "4" + value[13:16] + "8" + value[17:]
    return value


def _extract_semantics(records: Iterable[Mapping[str, Any]]) -> dict[str, Any]:
    retail: list[dict[str, Any]] = []
    cpu_initial: dict[str, Any] | None = None
    cpu_frames: list[dict[str, Any]] = []
    cpu_draws: list[tuple[int, dict[str, Any]]] = []
    draw_rows: list[dict[str, Any]] = []
    result_rows: list[dict[str, Any]] = []
    teardown_rows: list[dict[str, Any]] = []
    events: list[str] = []
    for index, record in enumerate(records):
        event = _record_event(record, index)
        events.append(event)
        payload = _payload(record, index)
        envelope = {
            "seq": record.get("seq"),
            "source_tick": record.get("source_tick"),
            "draw_ordinal": record.get("draw_ordinal"),
            "event": event,
        }
        cpu = payload.get("cpu")
        if event == "match_initial" and isinstance(cpu, Mapping):
            if cpu_initial is not None:
                raise ReplayError("semantic capture contains duplicate CPU initial snapshots")
            cpu_initial = dict(cpu)
        elif event == "source_tick" and isinstance(cpu, Mapping):
            cpu_frames.append(dict(cpu))
        elif event == "draw_return" and isinstance(cpu, Mapping):
            draw = payload.get("draw")
            source_index = draw.get("source_index") if isinstance(draw, Mapping) else None
            if type(source_index) is not int:
                raise ReplayError("semantic CPU draw is missing its source index")
            cpu_draws.append((source_index, dict(cpu)))
        if isinstance(payload.get("draw"), Mapping):
            draw_rows.append({**envelope, "draw": payload["draw"]})
        if isinstance(payload.get("result"), Mapping):
            result_rows.append({**envelope, "result": payload["result"]})
        if event == "scene_reset":
            teardown_rows.append({**envelope, "teardown": dict(payload)})
        value = payload.get("retail")
        if isinstance(value, Mapping):
            retail.append(dict(value))
    if not any(row.get("record") == "match_enter" for row in retail):
        raise ReplayError("semantic capture is missing match_enter")
    if not any(row.get("record") == "match_enter_complete" for row in retail):
        raise ReplayError("semantic capture is missing match_enter_complete")
    frames = [row for row in retail if row.get("record") == "frame"]
    if not frames:
        raise ReplayError("semantic capture has no source frame rows")
    for expected, frame in enumerate(frames):
        if frame.get("index") != expected:
            raise ReplayError(f"semantic source frame order is not contiguous at {expected}")
        consumed = frame.get("consumed_inputs")
        if not isinstance(consumed, list) or len(consumed) != 1:
            raise ReplayError(f"source frame {expected} does not contain one consumed PAD queue sample")
        vector = consumed[0]
        if not isinstance(vector, list) or len(vector) != 4:
            raise ReplayError(f"source frame {expected} does not contain four PAD ports")
        for port, value in enumerate(vector[1:], 2):
            if value not in (NEUTRAL_PAD, DISCONNECTED_PAD):
                raise ReplayError(
                    f"source frame {expected} CPU/unused port {port} contains a non-neutral PAD sample")
    if not result_rows:
        raise ReplayError("semantic capture is missing result_return")
    if not teardown_rows:
        raise ReplayError("semantic capture is missing scene_reset teardown")
    if cpu_initial is None or not cpu_frames:
        raise ReplayError("semantic capture is missing CPU observation snapshots")
    if not draw_rows:
        raise ReplayError("semantic capture is missing draw audit rows")
    required = {"match_enter", "match_initial", "source_tick", "result_return", "scene_reset"}
    if not required.issubset(events):
        raise ReplayError(f"semantic lifecycle is incomplete: missing {sorted(required - set(events))}")
    match_enter = next(row for row in retail if row.get("record") == "match_enter")
    initial = next(row for row in retail if row.get("record") == "match_enter_complete")
    candidate_rows = [match_enter, initial, *frames, {"record": "end", "frames": len(frames), "status": "captured"}]

    setup_hex = match_enter.get("start_melee_hex")
    if not isinstance(setup_hex, str):
        raise ReplayError("semantic match_enter is missing CPU observation setup")
    result = result_rows[-1].get("result")
    if not isinstance(result, Mapping):
        raise ReplayError("semantic result row is missing the observed result")
    teardown = teardown_rows[-1].get("teardown")
    if not isinstance(teardown, Mapping):
        raise ReplayError("semantic teardown row is missing teardown state")
    remaining = teardown.get("remaining_fighter_slots")
    if not isinstance(remaining, list):
        raise ReplayError("semantic teardown is missing remaining fighter slots")
    cpu_rows = [
        {"record": "header", "schema": "melee-web-cpu-observation", "version": 1,
         "frames_requested": len(frames), "source_drawing": bool(cpu_draws),
         "setup_hex": setup_hex},
        {"record": "initial", **cpu_initial},
    ]
    draw_index = 0
    for index, snapshot in enumerate(cpu_frames):
        cpu_rows.append({"record": "frame", "index": index, **snapshot})
        if draw_index < len(cpu_draws) and cpu_draws[draw_index][0] == index:
            source_index, draw_snapshot = cpu_draws[draw_index]
            cpu_rows.append({"record": "draw", "index": draw_index,
                             "source_index": source_index, **draw_snapshot})
            draw_index += 1
    if draw_index != len(cpu_draws):
        raise ReplayError("semantic CPU draw source indices are not ordered with source frames")
    cpu_rows.append({"record": "end", "frames": len(cpu_frames), "draws": len(cpu_draws),
                     "remaining_fighter_slots": remaining, "result": dict(result),
                     "status": "captured"})
    return {
        "retail_rows": candidate_rows,
        "frames": frames,
        "cpu_rows": cpu_rows,
        "draw_rows": draw_rows,
        "result_rows": result_rows,
        "teardown_rows": teardown_rows,
        "events": events,
    }


def _semantic_validator(_header: Mapping[str, Any], records: list[dict[str, Any]]) -> list[str] | None:
    try:
        _extract_semantics(records)
    except ReplayError as error:
        return [str(error)]
    return None


def _prepare_candidate(header: Mapping[str, Any], extracted: Mapping[str, Any], manifest_sha256: str,
                       observer_path: Path, *, cpu: str) -> list[dict[str, Any]]:
    retail_rows = list(extracted["retail_rows"])
    match_enter = retail_rows[0]
    initial = retail_rows[1]
    if not isinstance(match_enter.get("pad_state_hex"), str):
        raise ReplayError("semantic match_enter is missing PAD history required by MWRC version 3")
    if not isinstance(initial.get("pad_state_hex"), str):
        raise ReplayError("semantic match_enter_complete is missing PAD history")
    candidate_header = {
        "record": "header",
        "schema": CANDIDATE_SCHEMA,
        "version": 3,
        "active_player_count": len(initial["fighters"]),
        "phase": PHASE,
        "input_phase": DEQUEUED_INPUT_PHASE,
        "initial_phase": INITIAL_PHASE,
        "game_revision": GAME_REVISION,
        "frames_requested": len(extracted["frames"]),
        "provenance": _provenance(header, cpu=cpu),
        "collector_sha256": _collector_hash(header, observer_path),
        "writes_game_state": False,
        "capture_id": _capture_id(manifest_sha256),
    }
    return [candidate_header, *retail_rows]


def _write_jsonl(path: Path, rows: Iterable[Mapping[str, Any]]) -> None:
    path.write_text("".join(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")


def _artifact_record(path: Path, *, raw_manifest_sha256: str) -> dict[str, Any]:
    return {
        "name": path.name,
        "bytes": path.stat().st_size,
        "sha256": _sha256_file(path),
        "source_manifest_sha256": raw_manifest_sha256,
    }


def derive_replay(bundle_path: str | Path, derived_root: str | Path, *, cpu: str = "JITARM64") -> dict[str, Any]:
    """Validate one finalized bundle and atomically write derived artifacts."""
    bundle = Path(bundle_path).expanduser().resolve()
    if not bundle.is_dir() or bundle.is_symlink():
        raise ReplayError(f"bundle is not a safe directory: {bundle}")
    manifest, manifest_errors = _final_manifest(bundle)
    if manifest_errors or manifest is None:
        raise ReplayError("raw bundle manifest is invalid: " + "; ".join(manifest_errors))
    raw_manifest_sha256 = manifest.get("manifest_sha256")
    if not isinstance(raw_manifest_sha256, str) or re.fullmatch(r"[0-9a-f]{64}", raw_manifest_sha256) is None:
        raise ReplayError("raw bundle manifest has no valid binding hash")
    observer_path = bundle / RAW_OBSERVER_NAME
    if not observer_path.is_file() and RAW_OBSERVER_NAME != "observer.bin":
        # The supervisor's raw output name is part of the bundle contract;
        # accept the newer name while older finalized bundles still use the
        # constant exported by reference_session_bundle.
        candidate = bundle / "observer.bin"
        if candidate.is_file():
            observer_path = candidate
    if not observer_path.is_file() or observer_path.is_symlink():
        raise ReplayError(f"raw observer stream is missing: {RAW_OBSERVER_NAME}")
    header = _read_json(bundle / "header.json")
    if not isinstance(header, dict):
        raise ReplayError("raw bundle header is not an object")
    records = _read_jsonl(bundle / "records.jsonl")
    report = validate_bundle(bundle, require_complete=True, semantic_validator=_semantic_validator)
    if not report.valid:
        raise ReplayError("raw bundle validation failed: " + "; ".join(report.errors))
    _validate_authoritative_semantics(header, observer_path, records)
    extracted = _extract_semantics(records)
    candidate_rows = _prepare_candidate(header, extracted, raw_manifest_sha256, observer_path, cpu=cpu)

    destination_root = Path(derived_root).expanduser().resolve()
    destination = destination_root / str(header.get("session_id", "unknown"))
    if destination.exists() or destination.is_symlink():
        raise ReplayError(f"derived destination already exists: {destination}")
    destination_root.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{destination.name}.", dir=str(destination_root)))
    try:
        candidate_path = temporary / "candidate.jsonl"
        _write_jsonl(candidate_path, candidate_rows)
        try:
            capture = load_capture(candidate_path, cpu=cpu)
            mwrc, input_hash = encode_mwrc(capture)
        except (CaptureError, RecipeError) as error:
            raise ReplayError(f"derived candidate validation failed: {error}") from error
        mwrc_path = temporary / "capture.mwrc"
        mwrc_path.write_bytes(mwrc)
        cpu_path = temporary / "cpu-sidecar.jsonl"
        _write_jsonl(cpu_path, extracted["cpu_rows"])
        try:
            load_observation(cpu_path, capture)
        except (OSError, ValueError, CaptureError) as error:
            raise ReplayError(f"derived CPU sidecar validation failed: {error}") from error
        _write_jsonl(temporary / "draw-audit.jsonl", extracted["draw_rows"])
        (temporary / "result.json").write_text(json.dumps({
            "source_manifest_sha256": raw_manifest_sha256,
            "result_rows": extracted["result_rows"],
        }, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        (temporary / "teardown.json").write_text(json.dumps({
            "source_manifest_sha256": raw_manifest_sha256,
            "teardown_rows": extracted["teardown_rows"],
        }, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        artifacts = [_artifact_record(path, raw_manifest_sha256=raw_manifest_sha256)
                     for path in sorted(temporary.iterdir()) if path.is_file()]
        derived_manifest = {
            "schema": "webmelee-reference-capture-diagnostic-replay-v1",
            "version": 1,
            "session_id": header["session_id"],
            "source": {
                "state": bundle.parent.name,
                "manifest_sha256": raw_manifest_sha256,
                "observer_raw_sha256": _sha256_file(observer_path),
                "raw_files_retained": ["header.json", "records.jsonl", observer_path.name, MANIFEST_NAME],
            },
            "artifacts": artifacts,
            "transport": {
                "mwrc_sha256": _sha256_file(mwrc_path),
                "input_sha256": input_hash,
                "frames": len(extracted["frames"]),
                "cpu_generated_decisions": "excluded from replay input",
                "human_input_ports": [0],
            },
            "claims": {
                "single_capture_diagnostic": True,
                "reference_repeatability": "not_established",
                "port_equivalence": "not_claimed",
                "performance_acceptance": "not_claimed",
                "gold_admission": "not_claimed",
            },
        }
        canonical = _canonical_bytes(derived_manifest)
        derived_manifest["manifest_sha256"] = _sha256_bytes(canonical)
        (temporary / "derived-manifest.json").write_bytes(_canonical_bytes(derived_manifest) + b"\n")
        destination_root.mkdir(parents=True, exist_ok=True)
        os.replace(temporary, destination)
    except Exception:
        # Keep the raw accepted/ingested bundle untouched on every derivation
        # failure, while cleaning only our temporary output directory.
        if temporary.exists():
            import shutil
            shutil.rmtree(temporary, ignore_errors=True)
        raise
    return {"status": "derived", "path": str(destination), "manifest_sha256": derived_manifest["manifest_sha256"],
            "source_manifest_sha256": raw_manifest_sha256, "mwrc_sha256": _sha256_file(destination / "capture.mwrc"),
            "frames": len(extracted["frames"])}


def ingest_and_derive(bundle_path: str | Path, derived_root: str | Path, *, cpu: str = "JITARM64") -> dict[str, Any]:
    """Validate, ingest an accepted bundle, then produce its derived replay."""
    bundle = Path(bundle_path).expanduser().resolve()
    if bundle.parent.name == ACCEPTED_UNPROCESSED:
        inbox = ReferenceCaptureInbox(bundle.parent.parent)
        session_id = bundle.name
        manifest, errors = _final_manifest(bundle)
        if errors or manifest is None:
            raise ReplayError("raw bundle manifest is invalid: " + "; ".join(errors))
        report = validate_bundle(bundle, require_complete=True, semantic_validator=_semantic_validator)
        if not report.valid:
            raise ReplayError("raw bundle validation failed: " + "; ".join(report.errors))
        header = _read_json(bundle / "header.json")
        if not isinstance(header, dict):
            raise ReplayError("raw bundle header is not an object")
        observer_path = bundle / RAW_OBSERVER_NAME
        if not observer_path.is_file() or observer_path.is_symlink():
            raise ReplayError(f"raw observer stream is missing: {RAW_OBSERVER_NAME}")
        records = _read_jsonl(bundle / "records.jsonl")
        _validate_authoritative_semantics(header, observer_path, records)
        ingested = inbox.ingest(session_id, expected_manifest_sha256=manifest["manifest_sha256"])
        return derive_replay(ingested, derived_root, cpu=cpu)
    return derive_replay(bundle, derived_root, cpu=cpu)
