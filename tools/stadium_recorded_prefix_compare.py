"""Strict common-wire comparison for original Stadium GO/C/F prefixes.

Each input must independently pass the existing route validator. The candidate
must also pass the strict GO/C/F state companion. Comparison then removes only
the approved candidate state additions at those validated rows and retains all
older wire and decoded payload fields exactly.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
from typing import Any, Iterator

ROOT = Path(__file__).resolve().parents[1]
for _import_root in (ROOT / "tools", ROOT / "reference-capture" / "dolphin"):
    if _import_root.is_dir() and str(_import_root) not in sys.path:
        sys.path.insert(0, str(_import_root))

from reference_observer_stream import (
    EVENT_NAMES,
    HEADER,
    MAX_PAYLOAD,
    ObserverStreamError,
    ObserverStreamStats,
    iter_records,
)
from stadium_go_prefix import (
    EXPECTED_SETUP_RECEIPT_SHA256,
    MAX_STREAM_BYTES,
    MAX_STREAM_RECORDS,
    STADIUM_IDLE_STATE_TAGS,
    StadiumGoPrefixError,
    validate_stadium_go_prefix,
    validate_stadium_idle_state_snapshot,
)


SCHEMA = "melee-web-stadium-common-wire-comparison"
VERSION = 1
_WIRE_FIELDS = (
    (0, "magic"),
    (1, "version"),
    (2, "event_code"),
    (3, "sequence"),
    (5, "pc"),
    (6, "source_tick"),
    (7, "draw_ordinal"),
)
_GO_STATE_METADATA = (
    "state_snapshot_schema",
    "sis_table_entry_relationship_observed",
    "dynamic_title_text_bytes_observed",
)


class StadiumCommonWireMismatch(ValueError):
    """A strict mismatch with a JSON-safe first-difference receipt."""

    def __init__(self, receipt: dict[str, Any]):
        self.receipt = receipt
        super().__init__(json.dumps(receipt, sort_keys=True, separators=(",", ":"),
                               allow_nan=False))


def _first_difference(expected: Any, actual: Any, path: str) -> tuple[str, Any, Any] | None:
    """Return the first exact JSON difference, including key-set differences."""

    if type(expected) is not type(actual):
        return path, expected, actual
    if isinstance(expected, dict):
        expected_keys = set(expected)
        actual_keys = set(actual)
        if expected_keys != actual_keys:
            missing = sorted(expected_keys - actual_keys)
            extra = sorted(actual_keys - expected_keys)
            if missing:
                key = missing[0]
                return f"{path}.{key}", expected[key], "<missing>"
            key = extra[0]
            return f"{path}.{key}", "<missing>", actual[key]
        for key in sorted(expected_keys):
            difference = _first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference is not None:
                return difference
        return None
    if isinstance(expected, list):
        shared = min(len(expected), len(actual))
        for index in range(shared):
            difference = _first_difference(expected[index], actual[index],
                                            f"{path}[{index}]")
            if difference is not None:
                return difference
        if len(expected) != len(actual):
            index = shared
            expected_value = expected[index] if index < len(expected) else "<missing>"
            actual_value = actual[index] if index < len(actual) else "<missing>"
            return f"{path}[{index}]", expected_value, actual_value
        return None
    if expected != actual:
        return path, expected, actual
    return None


def _iter_wire_rows(stream_path: Path, route_summary: dict[str, Any]
                    ) -> Iterator[tuple[dict[str, Any], tuple[int, ...]]]:
    """Pair strict decoded rows with their validated raw wire headers.

    The existing parser validates payload length, CRC, sequence ordering,
    descriptor bounds and packing. A second bounded read preserves raw header
    fields the decoder intentionally omits on JSON events, especially PC.
    Independent digests must agree before this iterator can finish.
    """

    parser_stats = ObserverStreamStats()
    raw_digest = hashlib.sha256()
    raw_bytes = 0
    raw_records = 0
    try:
        rows = iter_records(
            stream_path,
            max_bytes=MAX_STREAM_BYTES,
            max_records=MAX_STREAM_RECORDS,
            stats=parser_stats,
        )
        with stream_path.open("rb") as raw_stream:
            for index, row in enumerate(rows):
                raw_header = raw_stream.read(HEADER.size)
                if len(raw_header) != HEADER.size:
                    raise ObserverStreamError(
                        f"raw framing record {index}: header changed or was truncated")
                header = HEADER.unpack(raw_header)
                payload_size = header[8]
                if payload_size > MAX_PAYLOAD:
                    raise ObserverStreamError(
                        f"raw framing record {index}: payload exceeds bound")
                raw_payload = raw_stream.read(payload_size)
                if len(raw_payload) != payload_size:
                    raise ObserverStreamError(
                        f"raw framing record {index}: payload changed or was truncated")
                raw_digest.update(raw_header)
                raw_digest.update(raw_payload)
                raw_bytes += len(raw_header) + len(raw_payload)
                raw_records += 1

                event_name = EVENT_NAMES.get(header[2])
                if (header[0] != int.from_bytes(b"MWRO", "little") or
                        event_name != row.get("event") or
                        header[3] != row.get("seq") or
                        header[4] != row.get("timestamp_ns") or
                        header[6] != row.get("source_tick") or
                        header[7] != row.get("draw_ordinal")):
                    raise ObserverStreamError(
                        f"raw framing record {index}: parser and raw header disagree")
                yield row, header

            if raw_stream.read(1):
                raise ObserverStreamError("raw framing has trailing bytes after decoded rows")
    except OSError as error:
        raise ObserverStreamError(f"cannot read raw wire headers: {error}") from error

    if (raw_digest.hexdigest() != parser_stats.prefix_sha256 or
            raw_digest.hexdigest() != route_summary["stream_sha256"] or
            raw_bytes != parser_stats.bytes_read or
            raw_bytes != route_summary["stream_bytes"] or
            raw_records != parser_stats.records_read or
            raw_records != route_summary["stream_records"]):
        raise ObserverStreamError("observer stream changed during common-wire validation")


def _candidate_state_sequences(state_summary: dict[str, Any]) -> dict[str, int]:
    snapshots = state_summary.get("snapshots")
    if not isinstance(snapshots, list):
        raise StadiumGoPrefixError("candidate state companion omitted its snapshots")
    sequences: dict[str, int] = {}
    for snapshot in snapshots:
        if not isinstance(snapshot, dict):
            raise StadiumGoPrefixError("candidate state companion returned a malformed snapshot")
        label = snapshot.get("label")
        sequence = snapshot.get("sequence")
        if label not in ("GO", "C", "F") or type(sequence) is not int or label in sequences:
            raise StadiumGoPrefixError("candidate state companion returned invalid row identity")
        sequences[label] = sequence
    if set(sequences) != {"GO", "C", "F"} or \
            len(set(sequences.values())) != 3:
        raise StadiumGoPrefixError("candidate state companion omitted GO, C, or F")
    return sequences


def _candidate_common_payload(payload: dict[str, Any], sequence: int,
                              state_sequences: dict[str, int]) -> dict[str, Any]:
    """Remove only the approved state fields after companion validation."""

    if sequence not in state_sequences.values():
        return payload
    projected = dict(payload)
    if sequence == state_sequences["GO"]:
        for key in _GO_STATE_METADATA:
            if key not in projected:
                raise StadiumGoPrefixError(
                    f"validated candidate GO row lost approved metadata {key}")
            del projected[key]
    slices = projected.get("slices")
    if isinstance(slices, list):
        projected["slices"] = [
            item for item in slices
            if not (isinstance(item, dict) and type(item.get("tag")) is int and
                    item["tag"] in STADIUM_IDLE_STATE_TAGS)
        ]
    return projected


def _identity(stream_path: Path, status_path: Path,
              route_summary: dict[str, Any]) -> dict[str, Any]:
    return {
        "stream_path": str(stream_path),
        "status_path": str(status_path),
        "stream_sha256": route_summary["stream_sha256"],
        "stream_bytes": route_summary["stream_bytes"],
        "stream_records": route_summary["stream_records"],
    }


def _raise_mismatch(original_identity: dict[str, Any],
                    candidate_identity: dict[str, Any], record_index: int,
                    sequence: int | None, path: str, expected: Any, actual: Any) -> None:
    raise StadiumCommonWireMismatch({
        "schema": SCHEMA,
        "version": VERSION,
        "decision": "REFUSE_STADIUM_COMMON_WIRE_MISMATCH",
        "original": original_identity,
        "candidate": candidate_identity,
        "first_difference": {
            "record_index": record_index,
            "sequence": sequence,
            "path": path,
            "expected": expected,
            "actual": actual,
        },
    })


def compare_stadium_common_wire(
        original_stream: str | Path,
        candidate_stream: str | Path,
        *,
        original_status_path: str | Path,
        candidate_status_path: str | Path) -> dict[str, Any]:
    """Compare exact original common fields for two complete Stadium prefixes.

    Both streams must pass the strict GO-prefix route validator. The candidate
    must additionally pass the strict GO/C/F state companion. A mismatch raises
    :class:`StadiumCommonWireMismatch`, a ``ValueError`` carrying a JSON-safe
    ``receipt`` with the first differing sequence and field path.
    """

    original_path = Path(original_stream)
    candidate_path = Path(candidate_stream)
    original_status = Path(original_status_path)
    candidate_status = Path(candidate_status_path)

    original_route = validate_stadium_go_prefix(
        original_path, EXPECTED_SETUP_RECEIPT_SHA256, original_status)
    candidate_route = validate_stadium_go_prefix(
        candidate_path, EXPECTED_SETUP_RECEIPT_SHA256, candidate_status)
    candidate_state = validate_stadium_idle_state_snapshot(
        candidate_path, EXPECTED_SETUP_RECEIPT_SHA256, candidate_status)
    if candidate_state.get("stream_sha256") != candidate_route.get("stream_sha256"):
        raise StadiumGoPrefixError(
            "candidate state companion and route validator admitted different stream bytes")
    state_sequences = _candidate_state_sequences(candidate_state)
    original_identity = _identity(original_path, original_status, original_route)
    candidate_identity = _identity(candidate_path, candidate_status, candidate_route)

    original_rows = _iter_wire_rows(original_path, original_route)
    candidate_rows = _iter_wire_rows(candidate_path, candidate_route)
    original_index = candidate_index = 0

    while True:
        try:
            original_record = next(original_rows)
        except StopIteration:
            original_record = None
        try:
            candidate_record = next(candidate_rows)
        except StopIteration:
            candidate_record = None

        if original_record is None or candidate_record is None:
            if original_record is None and candidate_record is None:
                break
            present = candidate_record if original_record is None else original_record
            assert present is not None
            row, header = present
            _raise_mismatch(
                original_identity, candidate_identity,
                min(original_index, candidate_index), header[3],
                "wire.record_count", "<end-of-stream>" if original_record is None
                else header[3], "<end-of-stream>" if candidate_record is None
                else header[3])

        original_row, original_header = original_record
        candidate_row, candidate_header = candidate_record
        sequence = original_header[3]
        if candidate_header[3] != sequence:
            _raise_mismatch(original_identity, candidate_identity, original_index,
                            sequence, "wire.header.sequence", sequence,
                            candidate_header[3])
        for index, name in _WIRE_FIELDS:
            if original_header[index] != candidate_header[index]:
                _raise_mismatch(
                    original_identity, candidate_identity, original_index,
                    sequence, f"wire.header.{name}", original_header[index],
                    candidate_header[index])
        if original_row["event"] != candidate_row["event"]:
            _raise_mismatch(original_identity, candidate_identity, original_index,
                            sequence, "record.event", original_row["event"],
                            candidate_row["event"])

        original_payload = original_row.get("payload")
        candidate_payload = candidate_row.get("payload")
        if not isinstance(original_payload, dict) or not isinstance(candidate_payload, dict):
            _raise_mismatch(original_identity, candidate_identity, original_index,
                            sequence, "record.payload", original_payload,
                            candidate_payload)
        projected_candidate = _candidate_common_payload(
            candidate_payload, sequence, state_sequences)
        difference = _first_difference(original_payload, projected_candidate, "payload")
        if difference is not None:
            path, expected, actual = difference
            _raise_mismatch(original_identity, candidate_identity, original_index,
                            sequence, path, expected, actual)

        original_index += 1
        candidate_index += 1

    if original_index != candidate_index:
        raise StadiumCommonWireMismatch({
            "schema": SCHEMA,
            "version": VERSION,
            "decision": "REFUSE_STADIUM_COMMON_WIRE_MISMATCH",
            "original": original_identity,
            "candidate": candidate_identity,
            "first_difference": {
                "record_index": min(original_index, candidate_index),
                "sequence": None,
                "path": "wire.record_count",
                "expected": original_index,
                "actual": candidate_index,
            },
        })

    receipt = {
        "schema": SCHEMA,
        "version": VERSION,
        "decision": "PASS_EXACT_STADIUM_COMMON_WIRE",
        "scope": "exact retained common MWRO wire and decoded payload fields",
        "original": original_identity,
        "candidate": candidate_identity,
        "candidate_state_companion": {
            "decision": candidate_state["decision"],
            "snapshot_sequences": state_sequences,
        },
        "comparison": {
            "records_compared": original_index,
            "exact_wire_fields": [name for _, name in _WIRE_FIELDS],
            "exact_payload_policy": "recursive values, key sets, slice order and slice fields",
            "ignored_wire_fields": [
                "timestamp_ns (host timestamp)",
                "payload_size (per-stream framing validated)",
                "payload_crc32 (per-stream CRC validated)",
            ],
            "candidate_state_fields_removed_after_validation": {
                "typed_slice_tags": sorted(STADIUM_IDLE_STATE_TAGS),
                "go_metadata_keys": list(_GO_STATE_METADATA),
                "rows": state_sequences,
            },
        },
    }
    # Enforce strict JSON output, rejecting accidental NaN or non-JSON values.
    json.dumps(receipt, sort_keys=True, separators=(",", ":"), allow_nan=False)
    return receipt
