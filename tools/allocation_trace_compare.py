#!/usr/bin/env python3
"""Compare bounded original/browser allocation event histories.

The supported raw pair is the original GALE01 allocation JSONL plus the
browser's retained source-allocation ring in its diagnostic report. A small
normalized JSONL schema is also accepted for fixtures and future exports.
"""
from __future__ import annotations

import argparse
from collections import Counter, deque
from contextlib import contextmanager
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Iterable, Iterator

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.allocation_history_replay import (  # noqa: E402
    ReplayProblem,
    _close_trace_views,
    load_trace,
)


REPORT_SCHEMA = "melee-web-allocation-trace-comparison"
REPORT_VERSION = 1
NORMALIZED_SCHEMA = "melee-web-normalized-allocation-trace"
NORMALIZED_VERSION = 1
SOURCE_ALIGNMENT = 32
SOURCE_ALIGNMENT_EVIDENCE = (
    "OSAllocFromHeap and source::Heap use the pinned 32-byte source allocator "
    "contract; see docs/SOURCE_ADDRESS_CONTEXT.md and src/source_address_context.cpp"
)
ORIGINAL_ALLOCATOR_FUNCTIONS = {"OSAllocFromHeap", "OSFreeToHeap"}
SOURCE_ALLOCATION_TRACE_CAPACITY = 16384


class CompareProblem(Exception):
    def __init__(self, kind: str, message: str):
        super().__init__(message)
        self.kind = kind
        self.message = message


@dataclass
class ReadResult:
    format: str
    metadata: dict[str, Any]
    events: Iterable[dict[str, Any]]
    complete: bool
    initial_events_present: bool
    truncation: dict[str, Any] | None = None
    close: Any = None

    def dispose(self) -> None:
        if self.close:
            self.close()


def _sha_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _nonempty(value: Any) -> bool:
    return isinstance(value, str) and bool(value.strip())


def _u32(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= 0xFFFFFFFF:
        raise CompareProblem("input", f"{context} must be a source u32 integer")
    return value


def _jsonl_rows(path: Path) -> Iterator[tuple[int, str, dict[str, Any], bool]]:
    """Yield JSONL objects, retaining only one row and recognizing a torn tail."""
    import gzip

    opener = gzip.open if path.name.lower().endswith((".gz", ".gzip")) else open
    try:
        stream = opener(path, "rt", encoding="utf-8", newline="")
    except OSError as error:
        raise CompareProblem("input", f"cannot read {path}: {error}") from error
    with stream:
        pending: tuple[int, str] | None = None
        for lineno, line in enumerate(stream, 1):
            if not line.strip():
                continue
            if pending is not None:
                yield _decode_jsonl_row(path, pending[0], pending[1], is_final=False)
            pending = (lineno, line)
        if pending is not None:
            yield _decode_jsonl_row(path, pending[0], pending[1], is_final=True)


def _decode_jsonl_row(path: Path, lineno: int, line: str,
                      *, is_final: bool) -> tuple[int, str, dict[str, Any], bool]:
    try:
        row = json.loads(line)
    except json.JSONDecodeError as error:
        # Only a final record without its newline can be an interrupted write.
        if is_final and line and not line.endswith(("\n", "\r")):
            return lineno, line, {"record": "incomplete_final_record"}, True
        raise CompareProblem("malformed", f"{path}:{lineno}: invalid JSON: {error}") from error
    if not isinstance(row, dict):
        raise CompareProblem("malformed", f"{path}:{lineno}: JSONL record is not an object")
    return lineno, line, row, False


def _validate_original_trace(path: Path):
    try:
        loaded = load_trace(path, disk_backed=True, disk_threshold=0)
    except ReplayProblem as error:
        raise CompareProblem(error.kind, error.message) from error
    header, rows, enters, returns = loaded
    return header, rows, enters, returns


def _source_event(*, index: int, sequence: int, line: int | None,
                  operation: str, owner_id: str, owner_proof: str,
                  world_id: str, world_proof: str,
                  boundary_id: str, boundary_proof: str,
                  size: int | None, source_address: int | None,
                  local_id: str | None, callsite: dict[str, Any] | None,
                  native_owner: dict[str, Any], side: str) -> dict[str, Any]:
    event: dict[str, Any] = {
        "record": "event",
        "event_index": index,
        "source_location": {"path": side, "line": line, "sequence": sequence},
        "world": {"identity": world_id, "evidence_id": world_proof},
        "boundary": {"identity": boundary_id, "evidence_id": boundary_proof},
        "operation": operation,
        "owner": {"identity": owner_id, "evidence_id": owner_proof,
                  "raw_selector": native_owner},
        "callsite": callsite,
    }
    if operation == "allocate":
        event["request"] = {
            "size": size,
            "alignment": SOURCE_ALIGNMENT,
            "alignment_evidence": SOURCE_ALIGNMENT_EVIDENCE,
        }
        event["allocation"] = {"local_id": local_id}
    elif operation == "free":
        event["allocation"] = {"local_id": local_id}
    if source_address is not None:
        event["source_address"] = f"0x{source_address:08x}"
    return event


@contextmanager
def _original_events(args, *, owner_proof: str, world_proof: str,
                     boundary_proof: str):
    if args.original_start_sequence is None or args.original_end_sequence is None:
        raise CompareProblem(
            "selection",
            "original trace needs --original-start-sequence and --original-end-sequence",
        )
    if args.original_start_sequence >= args.original_end_sequence:
        raise CompareProblem("selection", "original sequence boundary is empty or reversed")
    if args.original_heap is None:
        raise CompareProblem("selection", "original trace needs --original-heap")

    header, rows, enters, returns = _validate_original_trace(args.original_trace)
    def events() -> Iterator[dict[str, Any]]:
        start_row = rows[args.original_start_sequence]
        end_row = rows[args.original_end_sequence]
        if (start_row.get("record") != "enter" or
                start_row.get("function") != "HSD_CreateMainHeap"):
            raise CompareProblem(
                "selection",
                "selected original start sequence is not HSD_CreateMainHeap entry",
            )
        if (end_row.get("record") != "enter" or
                end_row.get("function") != "Fighter_Create"):
            raise CompareProblem(
                "selection",
                "selected original end sequence is not Fighter_Create entry",
            )
        active: dict[int, str] = {}
        semantic_index = 0
        for row in rows:
            sequence = row.get("sequence")
            if sequence < args.original_start_sequence:
                continue
            if sequence >= args.original_end_sequence:
                break
            if row.get("record") != "enter" or row.get("function") not in ORIGINAL_ALLOCATOR_FUNCTIONS:
                continue
            call = row["call"]
            args_row = row.get("args")
            if not isinstance(args_row, list) or len(args_row) < 2:
                raise CompareProblem("evidence", f"original call {call} lacks allocator arguments")
            heap = _u32(args_row[0], f"original call {call}.args[0]")
            if heap != args.original_heap:
                continue
            returned = returns.get(call)
            if returned is None or returned.get("sequence", args.original_end_sequence) >= args.original_end_sequence:
                raise CompareProblem("truncated", f"original allocator call {call} has no return inside the selected boundary")
            parent = enters.get(row.get("parent")) if row.get("parent") is not None else None
            callsite = {
                "function": row.get("function"),
                "raw_caller_lr": f"0x{_u32(row.get('lr'), f'original call {call}.lr'):08x}",
                "symbolized": False,
                "parent_function": parent.get("function") if parent else None,
                "parent_args": parent.get("args") if parent else None,
                "parent_sequence": parent.get("sequence") if parent else None,
                "call": call,
            }
            if row["function"] == "OSAllocFromHeap":
                size = _u32(args_row[1], f"original call {call}.size")
                address = _u32(returned.get("result"), f"original call {call}.result")
                if not address:
                    raise CompareProblem("evidence", f"original allocation call {call} returned a null source address")
                if address in active:
                    raise CompareProblem("lifecycle", f"original source address 0x{address:08x} was allocated twice while live")
                local_id = f"original-{semantic_index}"
                active[address] = local_id
                event = _source_event(
                    index=semantic_index, sequence=sequence, line=None,
                    operation="allocate", owner_id=args.owner_identity,
                    owner_proof=owner_proof, world_id=args.world_identity,
                    world_proof=world_proof, boundary_id=args.boundary_identity,
                    boundary_proof=boundary_proof, size=size,
                    source_address=address, local_id=local_id, callsite=callsite,
                    native_owner={"kind": "original_os_heap", "heap": heap},
                    side=str(args.original_trace),
                )
            else:
                address = _u32(args_row[1], f"original call {call}.free_address")
                local_id = active.pop(address, None)
                event = _source_event(
                    index=semantic_index, sequence=sequence, line=None,
                    operation="free", owner_id=args.owner_identity,
                    owner_proof=owner_proof, world_id=args.world_identity,
                    world_proof=world_proof, boundary_id=args.boundary_identity,
                    boundary_proof=boundary_proof, size=None,
                    source_address=address, local_id=local_id, callsite=callsite,
                    native_owner={"kind": "original_os_heap", "heap": heap},
                    side=str(args.original_trace),
                )
            semantic_index += 1
            yield event

    metadata = {
        "path": str(args.original_trace),
        "format": "melee-web-original-allocation-history/v1",
        "profile_sha256": header.get("profile_sha256"),
        "capture_status": None,
        "coverage": "selected OS heap allocation/free projection",
        "selected_sequence_range": {
            "start_inclusive": args.original_start_sequence,
            "end_exclusive": args.original_end_sequence,
        },
        "selected_heap": args.original_heap,
        "world_generation_recorded": False,
        "caller_recorded": True,
        "request_alignment_recorded": False,
        "source_address_field": "return.result / free argument",
        "address_domain": "original GALE01 source address space (source-pointer result)",
    }
    end_row = None
    for row in rows:
        if row.get("record") == "end":
            end_row = row
    if end_row:
        metadata["capture_status"] = end_row.get("status")
        metadata["boundary_complete"] = end_row.get("boundary_complete")
        metadata["ownership_complete"] = end_row.get("ownership_complete")
    try:
        yield ReadResult(
            format="original-allocation-history",
            metadata=metadata,
            events=events(),
            complete=bool(end_row and end_row.get("status") == "captured"
                          and end_row.get("boundary_complete") is True),
            initial_events_present=True,
            close=lambda: _close_trace_views((rows, enters, returns)),
        )
    finally:
        _close_trace_views((rows, enters, returns))


def _browser_report_events(args, *, owner_proof: str, world_proof: str,
                           boundary_proof: str) -> ReadResult:
    try:
        if args.browser_trace.stat().st_size > 256 * 1024 * 1024:
            raise CompareProblem("input", "browser report exceeds the 256 MiB safety bound")
        report = json.loads(args.browser_trace.read_text(encoding="utf-8"))
    except CompareProblem:
        raise
    except (OSError, json.JSONDecodeError) as error:
        raise CompareProblem("input", f"cannot read browser report {args.browser_trace}: {error}") from error
    if not isinstance(report, dict):
        raise CompareProblem("input", "browser report root is not a JSON object")
    snapshot = report.get("final_snapshot")
    if not isinstance(snapshot, dict):
        raise CompareProblem("evidence", "browser report has no final_snapshot allocation diagnostics")
    raw_events = snapshot.get("source_allocation_trace")
    if not isinstance(raw_events, list):
        raise CompareProblem("evidence", "browser final snapshot has no source_allocation_trace array")
    if len(raw_events) > SOURCE_ALLOCATION_TRACE_CAPACITY:
        raise CompareProblem("malformed", "browser allocation ring exceeds its declared 16,384 event capacity")
    total = snapshot.get("source_allocation_trace_total")
    if isinstance(total, bool) or not isinstance(total, int) or total < 0:
        raise CompareProblem("malformed", "browser source_allocation_trace_total is invalid")
    if any(not isinstance(row, dict) for row in raw_events):
        raise CompareProblem("malformed", "browser allocation ring record is not an object")
    indexed_rows = sorted(enumerate(raw_events), key=lambda pair: pair[1].get("sequence", -1))
    rows = [row for _, row in indexed_rows]
    sequences = [row.get("sequence") for row in rows]
    if any(isinstance(value, bool) or not isinstance(value, int) or value < 0 for value in sequences):
        raise CompareProblem("malformed", "browser allocation ring contains an invalid sequence")
    if len(set(sequences)) != len(sequences):
        raise CompareProblem("malformed", "browser allocation ring contains duplicate sequences")
    contiguous = bool(rows) and sequences == list(range(sequences[0], sequences[0] + len(rows)))
    no_overwrite = total == len(rows) and sequences == list(range(total))
    coverage_complete = contiguous and no_overwrite
    selected_rows = []
    for array_index, row in indexed_rows:
        if row.get("generation") != args.browser_generation or row.get("heap") != args.browser_heap:
            continue
        selected_rows.append((array_index, row))
    if not selected_rows:
        raise CompareProblem("selection", "browser heap/world selection matched no allocation events")

    events: list[dict[str, Any]] = []
    active: dict[int, str] = {}
    for index, (array_index, row) in enumerate(selected_rows):
        seq = _u32(row.get("sequence"), "browser sequence")
        op = row.get("operation")
        if op == 1:
            operation = "allocate"
        elif op == 0:
            operation = "free"
        else:
            raise CompareProblem("unsupported", f"browser operation {op!r} at sequence {seq} is not mapped")
        source_address = _u32(row.get("source"), f"browser sequence {seq}.source")
        size = _u32(row.get("requested"), f"browser sequence {seq}.requested")
        if operation == "allocate":
            if not source_address:
                raise CompareProblem("evidence", f"browser allocation sequence {seq} has a null source address")
            if source_address in active:
                raise CompareProblem("lifecycle", f"browser source address 0x{source_address:08x} was allocated twice while live")
            local_id = f"browser-{index}"
            active[source_address] = local_id
        else:
            local_id = active.pop(source_address, None)
        events.append(_source_event(
            index=index, sequence=seq, line=None, operation=operation,
            owner_id=args.owner_identity, owner_proof=owner_proof,
            world_id=args.world_identity, world_proof=world_proof,
            boundary_id=args.boundary_identity, boundary_proof=boundary_proof,
            size=size if operation == "allocate" else None,
            source_address=source_address, local_id=local_id,
            callsite=None,
            native_owner={"kind": "browser_source_heap", "heap": args.browser_heap,
                          "generation": args.browser_generation},
            side=f"{args.browser_trace}#/final_snapshot/source_allocation_trace/{array_index}",
        ))
    browser_result = report.get("browser_report")
    metadata = {
        "path": str(args.browser_trace),
        "format": "browser-session-report/source_allocation_trace",
        "capture_status": "complete_event_ring" if coverage_complete else "ring_buffered_or_truncated",
        "ring_capacity": SOURCE_ALLOCATION_TRACE_CAPACITY,
        "ring_total": total,
        "ring_retained": len(rows),
        "ring_sequence_range": [sequences[0], sequences[-1]] if rows else None,
        "selected_event_count": len(selected_rows),
        "selected_heap": args.browser_heap,
        "selected_generation": args.browser_generation,
        "world_generation_recorded": True,
        "caller_recorded": False,
        "request_alignment_recorded": False,
        "source_address_field": "source",
        "address_domain": "source::Address from source_address_context",
        "browser_scenario_complete": bool(browser_result.get("complete")) if isinstance(browser_result, dict) else None,
        "browser_scenario_pass": bool(browser_result.get("pass")) if isinstance(browser_result, dict) else None,
        "allocator_export_error": report.get("alloc_trace_error"),
        "allocation_events_retained_in_report": True,
        "scope_note": "ring coverage and browser scenario completion are separate; this scenario report may be incomplete",
    }
    selected_generation_first = next((row["sequence"] for row in rows
                                      if row.get("generation") == args.browser_generation), None)
    if not no_overwrite and selected_generation_first is not None and sequences[0] > selected_generation_first:
        coverage_complete = False
    return ReadResult(
        format="browser-source-allocation-report",
        metadata=metadata,
        events=events,
        complete=coverage_complete,
        initial_events_present=coverage_complete,
        truncation=None if coverage_complete else {
            "kind": "ring_buffer_or_truncated",
            "first_retained_sequence": sequences[0] if rows else None,
            "total": total,
            "retained": len(rows),
        },
    )


def _parse_normalized_trace(path: Path, side: str, selector_world: str | None,
                            selector_boundary: str | None) -> ReadResult:
    rows_iter = iter(_jsonl_rows(path))
    try:
        lineno, _, header, incomplete = next(rows_iter)
    except StopIteration:
        raise CompareProblem("malformed", f"{path}: trace is empty")
    if incomplete:
        raise CompareProblem("truncated", f"{path}:{lineno}: incomplete header")
    if header.get("record") != "header" or header.get("schema") != NORMALIZED_SCHEMA or header.get("version") != NORMALIZED_VERSION:
        raise CompareProblem("unsupported", f"{path}:{lineno}: unsupported normalized allocation trace schema/version")
    coverage = header.get("coverage")
    if not isinstance(coverage, dict):
        raise CompareProblem("malformed", f"{path}:{lineno}: coverage object is missing")
    initial = coverage.get("initial_events_present")
    if not isinstance(initial, bool):
        initial = None
    status: dict[str, Any] = {"end_seen": False, "complete": False, "event_count": 0,
                              "incomplete_final_record": False, "error": None}
    observed_worlds: set[str] = set()
    observed_boundaries: set[str] = set()
    expected_sequence = 0
    selected_count = 0
    for line_no, _, row, torn in rows_iter:
        if torn:
            status["incomplete_final_record"] = True
            break
        record = row.get("record")
        if record == "event":
            if status["end_seen"]:
                raise CompareProblem("malformed", f"{path}:{line_no}: event appears after end record")
            sequence = row.get("sequence")
            if isinstance(sequence, bool) or not isinstance(sequence, int) or sequence != expected_sequence:
                raise CompareProblem("malformed", f"{path}:{line_no}: noncontiguous event sequence {sequence!r}; expected {expected_sequence}")
            expected_sequence += 1
            status["event_count"] += 1
            world = row.get("world")
            boundary = row.get("boundary")
            if isinstance(world, dict) and _nonempty(world.get("id")):
                observed_worlds.add(world["id"])
            if isinstance(boundary, dict) and _nonempty(boundary.get("id")):
                observed_boundaries.add(boundary["id"])
            if ((selector_world is None or (isinstance(world, dict) and world.get("id") == selector_world))
                    and (selector_boundary is None or (isinstance(boundary, dict) and boundary.get("id") == selector_boundary))):
                selected_count += 1
        elif record == "end":
            if status["end_seen"]:
                raise CompareProblem("malformed", f"{path}:{line_no}: duplicated end record")
            status["end_seen"] = True
            status["complete"] = row.get("complete") is True and row.get("status", "complete") == "complete"
            status["declared_event_count"] = row.get("event_count")
            if (status["declared_event_count"] is not None and
                    (isinstance(status["declared_event_count"], bool) or
                     not isinstance(status["declared_event_count"], int) or
                     status["declared_event_count"] < 0)):
                raise CompareProblem("malformed", f"{path}:{line_no}: end event_count is invalid")
        else:
            raise CompareProblem("malformed", f"{path}:{line_no}: unsupported record kind {record!r}")
    if status.get("declared_event_count") is not None and status["declared_event_count"] != status["event_count"]:
        raise CompareProblem("malformed", f"{path}: end event_count differs from observed event count")
    if selector_world is None and len(observed_worlds) > 1:
        raise CompareProblem("selection", f"{path}: multiple world ids require explicit selection: {sorted(observed_worlds)}")
    if selector_boundary is None and len(observed_boundaries) > 1:
        raise CompareProblem("selection", f"{path}: multiple boundaries require explicit selection: {sorted(observed_boundaries)}")
    if selected_count == 0:
        raise CompareProblem("selection", f"{path}: world/boundary selection matched no allocation events")
    def selected_events() -> Iterator[dict[str, Any]]:
        stream = iter(_jsonl_rows(path))
        next(stream, None)
        for line_no, _, row, torn in stream:
            if torn or row.get("record") != "event":
                continue
            world = row.get("world")
            boundary = row.get("boundary")
            if ((selector_world is None or (isinstance(world, dict) and world.get("id") == selector_world))
                    and (selector_boundary is None or (isinstance(boundary, dict) and boundary.get("id") == selector_boundary))):
                event = dict(row)
                event["source_location"] = {"path": str(path), "line": line_no,
                                             "sequence": row.get("sequence")}
                yield event

    return ReadResult(
        format="normalized-allocation-events/v1",
        metadata={"path": str(path), "format": NORMALIZED_SCHEMA,
                  "capture": coverage, "header_address_domain": header.get("address_domain"),
                  "observed_worlds": sorted(observed_worlds),
                  "observed_boundaries": sorted(observed_boundaries),
                  "selected_event_count": selected_count, "end": status},
        events=selected_events(),
        complete=status["complete"] and not status["incomplete_final_record"],
        initial_events_present=initial is True,
        truncation={"kind": "incomplete_final_record"} if status["incomplete_final_record"] else None,
    )


def _format_jsonl(path: Path) -> str:
    try:
        first = next(_jsonl_rows(path))[2]
    except StopIteration:
        return "empty"
    schema = first.get("schema")
    if schema == "melee-web-original-allocation-history":
        return "original"
    if schema == NORMALIZED_SCHEMA:
        return "normalized"
    if schema == "melee-web-port-session-diagnostic":
        return "port-session"
    return "unknown-jsonl"


def _read_port_session_gap(path: Path) -> dict[str, Any]:
    kinds: Counter[str] = Counter()
    total = 0
    header = None
    last_sequence = None
    for lineno, _, row, torn in _jsonl_rows(path):
        if torn:
            break
        total += 1
        if total == 1:
            header = row
        kinds[str(row.get("record", "<unknown>"))] += 1
        if isinstance(row.get("index"), int) and not isinstance(row.get("index"), bool):
            last_sequence = row["index"]
    allocation_records = sum(count for kind, count in kinds.items()
                             if "alloc" in kind.lower())
    return {
        "path": str(path),
        "format": "melee-web-port-session-diagnostic/v1",
        "records": dict(kinds),
        "record_count": total,
        "allocation_event_records": allocation_records,
        "last_session_frame_index": last_sequence,
        "header": header,
        "complete": False,
        "coverage": "session/state records; no allocation operation records",
    }


def _identity(value: Any, context: str) -> tuple[str, str] | None:
    if not isinstance(value, dict):
        return None
    identity = value.get("identity", value.get("id"))
    proof = value.get("evidence_id", value.get("proof_id"))
    if not _nonempty(identity) or not _nonempty(proof):
        return None
    return identity, proof


def _compare_event(left: dict[str, Any], right: dict[str, Any],
                   left_domain: Any, right_domain: Any
                   ) -> tuple[list[dict[str, Any]], list[str], dict[str, Any]]:
    differences: list[dict[str, Any]] = []
    unsupported: list[str] = []

    if left.get("operation") != right.get("operation"):
        differences.append({"field": "operation", "original": left.get("operation"), "browser": right.get("operation")})
        return differences, unsupported, {}
    operation = left.get("operation")
    if not _nonempty(operation):
        unsupported.append("operation is missing")
        return differences, unsupported, {}

    left_owner = _identity(left.get("owner"), "original owner")
    right_owner = _identity(right.get("owner"), "browser owner")
    if left_owner is None or right_owner is None:
        unsupported.append("heap/pool owner identity lacks a source-established mapping on one or both sides")
    elif left_owner[1] != right_owner[1]:
        unsupported.append("heap/pool owner identity mappings have different evidence ids")
    elif left_owner[0] != right_owner[0]:
        differences.append({"field": "owner.identity", "original": left_owner[0], "browser": right_owner[0]})

    if operation in ("allocate", "pool_allocate", "arena_allocate"):
        for field in ("size", "alignment"):
            lreq = left.get("request")
            rreq = right.get("request")
            lv = lreq.get(field) if isinstance(lreq, dict) else None
            rv = rreq.get(field) if isinstance(rreq, dict) else None
            if isinstance(lv, bool) or not isinstance(lv, int) or isinstance(rv, bool) or not isinstance(rv, int):
                unsupported.append(f"request.{field} is unknown on one or both sides")
            elif lv != rv:
                differences.append({"field": f"request.{field}", "original": lv, "browser": rv})

    if operation in ("allocate", "pool_allocate", "arena_allocate", "free", "pool_free"):
        lalloc = left.get("allocation")
        ralloc = right.get("allocation")
        lid = lalloc.get("local_id") if isinstance(lalloc, dict) else None
        rid = ralloc.get("local_id") if isinstance(ralloc, dict) else None
        if not _nonempty(lid) or not _nonempty(rid):
            unsupported.append("allocation/free lifecycle identity is missing or has no earlier allocation event")
        elif operation in ("free", "pool_free"):
            if lalloc.get("paired_allocation_index") != ralloc.get("paired_allocation_index"):
                differences.append({"field": "allocation.lifetime", "original": lalloc.get("paired_allocation_index"),
                                    "browser": ralloc.get("paired_allocation_index")})

    address_context: dict[str, Any] = {"compared": False, "reason": None}
    laddr, raddr = left.get("source_address"), right.get("source_address")
    ld, rd = _identity(left_domain, "original address domain"), _identity(right_domain, "browser address domain")
    if laddr is not None and raddr is not None:
        if ld is not None and rd is not None and ld == rd:
            address_context["compared"] = True
            if laddr != raddr:
                differences.append({"field": "source_address", "original": laddr, "browser": raddr})
        else:
            address_context["reason"] = "address domains lack the same explicit evidence id; values were not compared"
    elif laddr is not None or raddr is not None:
        address_context["reason"] = "source address is absent on one side"

    # Caller identity is context only; a raw LR is never symbolized here.
    callers = []
    for event in (left, right):
        caller = event.get("callsite") or event.get("caller")
        if isinstance(caller, dict):
            callers.append(caller)
    if len(callers) != 2:
        caller_note = "caller context is not recorded on both sides"
    else:
        left_symbol = callers[0].get("identity", callers[0].get("symbol"))
        right_symbol = callers[1].get("identity", callers[1].get("symbol"))
        left_build = callers[0].get("build_id", callers[0].get("evidence_id"))
        right_build = callers[1].get("build_id", callers[1].get("evidence_id"))
        if (_nonempty(left_symbol) and _nonempty(right_symbol) and
                _nonempty(left_build) and left_build == right_build):
            if left_symbol != right_symbol:
                differences.append({"field": "caller.identity", "original": left_symbol, "browser": right_symbol})
            caller_note = "caller symbols share a build identity"
        else:
            caller_note = "caller identities are unavailable or build-incompatible; caller was not compared"
    return differences, unsupported, {"source_address": address_context,
                                     "caller_note": caller_note}


def _world_boundary_identity(event: dict[str, Any], key: str) -> tuple[str, str] | None:
    value = event.get(key)
    return _identity(value, key)


def _take(iterator: Iterator[dict[str, Any]], count: int) -> list[dict[str, Any]]:
    result = []
    for _ in range(count):
        try:
            result.append(next(iterator))
        except StopIteration:
            break
    return result


def compare_streams(left: ReadResult, right: ReadResult, *, window: int,
                    owner_identity: str | None = None,
                    owner_evidence: str | None = None,
                    world_identity: str | None = None,
                    world_evidence: str | None = None,
                    boundary_identity: str | None = None,
                    boundary_evidence: str | None = None,
                    address_domain: dict[str, Any] | None = None) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    if not left.initial_events_present or not right.initial_events_present:
        return ({"status": "trace_exhaustion_or_truncation", "matched_prefix_length": 0,
                 "reason": "at least one selected stream does not establish that its initial events are present",
                 "first_difference": None, "first_unsupported": None}, [])
    if owner_identity is None or world_identity is None or boundary_identity is None:
        return ({"status": "incompatible_or_insufficient_evidence_for_alignment", "matched_prefix_length": 0,
                 "reason": "explicit owner, world and boundary mappings are required before event alignment",
                 "first_difference": None, "first_unsupported": None}, [])

    owner_proof = _sha_text(owner_evidence or "")
    world_proof = _sha_text(world_evidence or "")
    boundary_proof = _sha_text(boundary_evidence or "")
    left_it, right_it = iter(left.events), iter(right.events)
    matched = 0
    left_before: deque[dict[str, Any]] = deque(maxlen=window)
    right_before: deque[dict[str, Any]] = deque(maxlen=window)
    windows: list[dict[str, Any]] = []
    left_active: dict[str, int] = {}
    right_active: dict[str, int] = {}
    first_unsupported = None
    notes: set[str] = set()

    while True:
        try:
            levent = next(left_it)
            left_done = False
        except StopIteration:
            levent = None
            left_done = True
        try:
            revent = next(right_it)
            right_done = False
        except StopIteration:
            revent = None
            right_done = True
        if left_done or right_done:
            if left_done and right_done:
                status = "matching_comparable_prefix" if left.complete and right.complete else "trace_exhaustion_or_truncation"
                reason = "both selected event streams were exhausted" if status == "matching_comparable_prefix" else "one or both captures are incomplete or truncated"
            else:
                status = "trace_exhaustion_or_truncation"
                reason = "one selected event stream ended before the other"
            if not left_done and levent is not None:
                windows.append({"side": "original", "offset": 0, "event": levent})
            if not right_done and revent is not None:
                windows.append({"side": "browser", "offset": 0, "event": revent})
            return ({"status": status, "matched_prefix_length": matched, "reason": reason,
                     "first_difference": None, "first_unsupported": None,
                     "comparison_notes": sorted(notes)}, windows)

        for key in ("world", "boundary"):
            li = _world_boundary_identity(levent, key)
            ri = _world_boundary_identity(revent, key)
            if li is None or ri is None:
                first_unsupported = {"event_index": matched, "field": key,
                                     "reason": f"{key} identity mapping is missing on one or both sides",
                                     "original": levent, "browser": revent}
                break
            expected_proof = world_proof if key == "world" else boundary_proof
            expected_identity = world_identity if key == "world" else boundary_identity
            if li[1] != ri[1] or li[1] != expected_proof:
                first_unsupported = {"event_index": matched, "field": key,
                                     "reason": f"{key} mappings do not share the explicitly selected evidence identity",
                                     "original": li, "browser": ri}
                break
            if li[0] != ri[0] or li[0] != expected_identity:
                first_unsupported = {"event_index": matched, "field": key,
                                     "reason": f"selected {key} identities differ",
                                     "original": li[0], "browser": ri[0]}
                break
        if first_unsupported:
            left_after = _take(left_it, window)
            right_after = _take(right_it, window)
            for offset, event in enumerate(list(left_before) + [levent] + left_after, -len(left_before)):
                windows.append({"side": "original", "offset": offset, "event": event})
            for offset, event in enumerate(list(right_before) + [revent] + right_after, -len(right_before)):
                windows.append({"side": "browser", "offset": offset, "event": event})
            return ({"status": "incompatible_or_insufficient_evidence_for_alignment",
                     "matched_prefix_length": matched, "reason": first_unsupported["reason"],
                     "first_difference": None, "first_unsupported": first_unsupported,
                     "comparison_notes": sorted(notes)}, windows)

        levent_owner = _identity(levent.get("owner"), "original owner")
        revent_owner = _identity(revent.get("owner"), "browser owner")
        if (levent_owner is None or revent_owner is None or
                levent_owner[1] != owner_proof or revent_owner[1] != owner_proof):
            first_unsupported = {"event_index": matched, "field": "owner",
                                 "reason": "heap/pool owner lacks the explicit source identity mapping",
                                 "original": levent.get("owner"), "browser": revent.get("owner")}
            left_after = _take(left_it, window)
            right_after = _take(right_it, window)
            for offset, event in enumerate(list(left_before) + [levent] + left_after, -len(left_before)):
                windows.append({"side": "original", "offset": offset, "event": event})
            for offset, event in enumerate(list(right_before) + [revent] + right_after, -len(right_before)):
                windows.append({"side": "browser", "offset": offset, "event": event})
            return ({"status": "incompatible_or_insufficient_evidence_for_alignment",
                     "matched_prefix_length": matched, "reason": first_unsupported["reason"],
                     "first_difference": None, "first_unsupported": first_unsupported,
                     "comparison_notes": sorted(notes)}, windows)

        differences, gaps, context = _compare_event(levent, revent,
                                                     left.metadata.get("header_address_domain"),
                                                     right.metadata.get("header_address_domain"))
        # Raw captured formats use one explicit source address domain supplied
        # for this run; normalized inputs use their independently evidenced
        # header domains. Do not use host addresses as a fallback.
        domains = [left.metadata.get("address_domain_declared"), right.metadata.get("address_domain_declared")]
        if address_domain is not None:
            domains = [address_domain, address_domain]
            differences, gaps, context = _compare_event(levent, revent, *domains)

        if gaps:
            first_unsupported = {"event_index": matched, "reason": gaps[0],
                                 "original": levent, "browser": revent}
            left_after = _take(left_it, window)
            right_after = _take(right_it, window)
            for offset, event in enumerate(list(left_before) + [levent] + left_after, -len(left_before)):
                windows.append({"side": "original", "offset": offset, "event": event})
            for offset, event in enumerate(list(right_before) + [revent] + right_after, -len(right_before)):
                windows.append({"side": "browser", "offset": offset, "event": event})
            return ({"status": "incompatible_or_insufficient_evidence_for_alignment",
                     "matched_prefix_length": matched, "reason": gaps[0],
                     "first_difference": None, "first_unsupported": first_unsupported,
                     "comparison_notes": sorted(notes)}, windows)
        if context.get("source_address", {}).get("reason"):
            notes.add(context["source_address"]["reason"])
        if context.get("caller_note"):
            notes.add(context["caller_note"])

        operation = levent.get("operation")
        if operation in ("allocate", "pool_allocate", "arena_allocate"):
            la = levent.get("allocation") or {}
            ra = revent.get("allocation") or {}
            if not _nonempty(la.get("local_id")) or not _nonempty(ra.get("local_id")):
                first_unsupported = {"event_index": matched, "field": "allocation.lifecycle",
                                     "reason": "allocation has no local identity for subsequent free matching",
                                     "original": levent, "browser": revent}
            elif la["local_id"] in left_active or ra["local_id"] in right_active:
                first_unsupported = {"event_index": matched, "field": "allocation.lifecycle",
                                     "reason": "trace reuses a live local allocation identity",
                                     "original": levent, "browser": revent}
            elif not differences:
                left_active[la["local_id"]] = matched
                right_active[ra["local_id"]] = matched
        elif operation in ("free", "pool_free"):
            la = levent.get("allocation") or {}
            ra = revent.get("allocation") or {}
            lid, rid = la.get("local_id"), ra.get("local_id")
            if lid not in left_active or rid not in right_active:
                first_unsupported = {"event_index": matched, "field": "allocation.lifecycle",
                                     "reason": "free does not reference a known earlier live allocation",
                                     "original": levent, "browser": revent}
            else:
                lparent, rparent = left_active[lid], right_active[rid]
                if lparent != rparent:
                    differences.append({"field": "allocation.lifetime", "original": lparent, "browser": rparent})
                if not differences:
                    del left_active[lid]
                    del right_active[rid]

        if first_unsupported:
            left_after = _take(left_it, window)
            right_after = _take(right_it, window)
            for offset, event in enumerate(list(left_before) + [levent] + left_after, -len(left_before)):
                windows.append({"side": "original", "offset": offset, "event": event})
            for offset, event in enumerate(list(right_before) + [revent] + right_after, -len(right_before)):
                windows.append({"side": "browser", "offset": offset, "event": event})
            return ({"status": "incompatible_or_insufficient_evidence_for_alignment",
                     "matched_prefix_length": matched, "reason": first_unsupported["reason"],
                     "first_difference": None, "first_unsupported": first_unsupported,
                     "comparison_notes": sorted(notes)}, windows)

        if differences:
            left_after = _take(left_it, window)
            right_after = _take(right_it, window)
            for offset, event in enumerate(list(left_before) + [levent] + left_after, -len(left_before)):
                windows.append({"side": "original", "offset": offset, "event": event})
            for offset, event in enumerate(list(right_before) + [revent] + right_after, -len(right_before)):
                windows.append({"side": "browser", "offset": offset, "event": event})
            first_diff = {"event_index": matched, "differences": differences,
                          "equal_or_unknown_context": context,
                          "original": levent, "browser": revent,
                          "alignment": "strict positional comparison; no operations were skipped"}
            return ({"status": "first_differing_comparable_event", "matched_prefix_length": matched,
                     "reason": "first strict comparable event differs",
                     "first_difference": first_diff, "first_unsupported": None,
                     "comparison_notes": sorted(notes)}, windows)

        left_before.append(levent)
        right_before.append(revent)
        matched += 1


def _canonical_address_domain(metadata: dict[str, Any]) -> dict[str, Any] | None:
    value = metadata.get("header_address_domain")
    if isinstance(value, dict):
        return value
    return None


def _markdown(report: dict[str, Any]) -> str:
    status = report.get("status", "unknown")
    lines = ["# Allocation trace comparison", "", f"- **Outcome:** `{status}`",
             f"- **Matched comparable prefix:** {report.get('matched_prefix_length', 0)} events"]
    first = report.get("first_difference")
    if isinstance(first, dict):
        lines.extend([f"- **First strict difference:** event {first.get('event_index')} (zero-based)"])
        for diff in first.get("differences", []):
            lines.append(f"  - `{diff.get('field')}`: original `{diff.get('original')}`; browser `{diff.get('browser')}`")
        left_loc = (first.get("original") or {}).get("source_location")
        right_loc = (first.get("browser") or {}).get("source_location")
        lines.append(f"- **Locations:** original `{left_loc}`; browser `{right_loc}`")
        lines.append("- This is the earliest observed comparable difference in the selected projection; it is not a root-cause claim.")
    if report.get("reason"):
        lines.append(f"- **Scope note:** {report['reason']}")
    lines.extend(["", "Inputs and capture completeness are recorded in `report.json`.",
                  "The bounded side-by-side context is in `event-window.jsonl`.", ""])
    return "\n".join(lines)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--original-trace", type=Path, required=True,
                   help="original allocation-history JSONL or normalized event JSONL")
    p.add_argument("--browser-trace", type=Path, required=True,
                   help="browser allocation report JSON or normalized event JSONL")
    p.add_argument("--out", type=Path, required=True, help="new output directory; must not already exist")
    p.add_argument("--original-start-sequence", type=int)
    p.add_argument("--original-end-sequence", type=int,
                   help="exclusive original boundary; the selected row must be Fighter_Create entry")
    p.add_argument("--original-heap", type=int)
    p.add_argument("--browser-generation", type=int)
    p.add_argument("--browser-heap", type=int)
    p.add_argument("--original-world", help="selector for normalized input world id")
    p.add_argument("--browser-world", help="selector for normalized input world id")
    p.add_argument("--original-boundary", help="selector for normalized input boundary id")
    p.add_argument("--browser-boundary", help="selector for normalized input boundary id")
    p.add_argument("--world-identity", help="source-evidenced shared identity for selected original/browser worlds")
    p.add_argument("--world-evidence", help="evidence statement for the world mapping")
    p.add_argument("--boundary-identity", help="source-evidenced shared identity for selected boundaries")
    p.add_argument("--boundary-evidence", help="evidence statement for the boundary mapping")
    p.add_argument("--owner-identity", help="source-evidenced shared heap/pool owner identity")
    p.add_argument("--owner-evidence", help="evidence statement mapping original and browser owner selectors")
    p.add_argument("--address-domain", help="shared source address domain id; omit to exclude address outputs")
    p.add_argument("--address-domain-evidence", help="evidence for the shared source-address domain")
    p.add_argument("--window", type=int, default=4, help="events before/after a difference, from 1 through 64")
    return p


def _classify_input(path: Path, side: str, args) -> str:
    if not path.is_file():
        raise CompareProblem("input", f"{side} trace does not exist: {path}")
    if side == "browser" and path.suffix.lower() == ".json":
        return "browser-report"
    kind = _format_jsonl(path)
    return kind


def _validate_output_location(output: Path, inputs: Iterable[Path]) -> None:
    resolved_output = output.resolve()
    for input_path in inputs:
        capture_directory = input_path.resolve().parent
        if resolved_output.is_relative_to(capture_directory):
            raise CompareProblem(
                "output",
                f"--out must be outside the retained input directory: {capture_directory}",
            )


def _main(args) -> tuple[int, dict[str, Any], list[dict[str, Any]]]:
    if args.window < 1 or args.window > 64:
        raise CompareProblem("input", "--window must be between 1 and 64")
    for key in ("owner_identity", "owner_evidence", "world_identity", "world_evidence",
                "boundary_identity", "boundary_evidence"):
        if not _nonempty(getattr(args, key)):
            raise CompareProblem("selection", f"--{key.replace('_', '-')} is required for an evidence-bound comparison")
    if args.original_trace.resolve() == args.browser_trace.resolve():
        raise CompareProblem("input", "original and browser inputs must be distinct files")

    owner_proof = _sha_text(args.owner_evidence)
    world_proof = _sha_text(args.world_evidence)
    boundary_proof = _sha_text(args.boundary_evidence)
    owner_mapping = {"identity": args.owner_identity, "evidence": args.owner_evidence,
                     "evidence_sha256": owner_proof,
                     "selectors": {"original_heap": args.original_heap,
                                   "browser_heap": args.browser_heap,
                                   "browser_generation": args.browser_generation}}
    world_mapping = {"identity": args.world_identity, "evidence": args.world_evidence,
                     "evidence_sha256": world_proof}
    boundary_mapping = {"identity": args.boundary_identity, "evidence": args.boundary_evidence,
                        "evidence_sha256": boundary_proof,
                        "original_sequence_range": [args.original_start_sequence,
                                                    args.original_end_sequence]}
    address_domain = None
    if args.address_domain is not None or args.address_domain_evidence is not None:
        if not _nonempty(args.address_domain) or not _nonempty(args.address_domain_evidence):
            raise CompareProblem("selection", "--address-domain and --address-domain-evidence must be supplied together")
        address_domain = {"identity": args.address_domain,
                          "evidence_id": _sha_text(args.address_domain_evidence),
                          "evidence": args.address_domain_evidence}

    original_kind = _classify_input(args.original_trace, "original", args)
    browser_kind = _classify_input(args.browser_trace, "browser", args)
    original = browser = None
    report = {"schema": REPORT_SCHEMA, "version": REPORT_VERSION,
              "inputs": {"original": {"path": str(args.original_trace), "format": original_kind},
                         "browser": {"path": str(args.browser_trace), "format": browser_kind}},
              "selection": {"owner_mapping": owner_mapping, "world_mapping": world_mapping,
                            "boundary_mapping": boundary_mapping,
                            "address_domain": address_domain,
                            "comparison_projection": "main-heap OS allocation/free operations only"},
              "matched_prefix_length": 0, "first_difference": None,
              "first_unsupported": None, "event_window": "event-window.jsonl"}
    windows: list[dict[str, Any]] = []
    if original_kind == "original" and browser_kind == "browser-report":
        with _original_events(args, owner_proof=owner_proof, world_proof=world_proof,
                              boundary_proof=boundary_proof) as original:
            browser = _browser_report_events(args, owner_proof=owner_proof,
                                             world_proof=world_proof,
                                             boundary_proof=boundary_proof)
            original.metadata["owner_mapping"] = owner_mapping
            original.metadata["world_mapping"] = world_mapping
            original.metadata["boundary_mapping"] = boundary_mapping
            original.metadata["address_domain_declared"] = address_domain
            browser.metadata["owner_mapping"] = owner_mapping
            browser.metadata["world_mapping"] = world_mapping
            browser.metadata["boundary_mapping"] = boundary_mapping
            browser.metadata["address_domain_declared"] = address_domain
            summary, windows = compare_streams(
                original, browser, window=args.window,
                owner_identity=args.owner_identity,
                owner_evidence=args.owner_evidence,
                world_identity=args.world_identity,
                world_evidence=args.world_evidence,
                boundary_identity=args.boundary_identity,
                boundary_evidence=args.boundary_evidence,
                address_domain=address_domain,
            )
            report["trace_summaries"] = {"original": original.metadata,
                                         "browser": browser.metadata}
            report.update(summary)
            report["limitations"] = [
                "Only OSAllocFromHeap/OSFreeToHeap requests on the explicitly selected owner are projected; HSD pool operations and other owners are outside this shared browser trace schema.",
                "Original caller LR is retained as a raw value and is not symbolized. The browser allocation trace has no caller field.",
                "The explicit owner, world, boundary and optional address-domain mappings are recorded as comparison premises; this tool does not independently prove those mappings.",
                "A first observed event difference is diagnostic evidence, not a root-cause claim.",
            ]
            if browser.metadata.get("allocator_export_error"):
                report["notes"] = ["The separate browser export call failed, but the report snapshot retained the bounded allocation event array."]
    elif original_kind == "normalized" and browser_kind == "normalized":
        original = _parse_normalized_trace(args.original_trace, "original",
                                           args.original_world, args.original_boundary)
        browser = _parse_normalized_trace(args.browser_trace, "browser",
                                          args.browser_world, args.browser_boundary)
        original.metadata["address_domain_declared"] = _canonical_address_domain(original.metadata)
        browser.metadata["address_domain_declared"] = _canonical_address_domain(browser.metadata)
        report["trace_summaries"] = {"original": original.metadata, "browser": browser.metadata}
        summary, windows = compare_streams(
            original, browser, window=args.window,
            owner_identity=args.owner_identity,
            owner_evidence=args.owner_evidence,
            world_identity=args.world_identity,
            world_evidence=args.world_evidence,
            boundary_identity=args.boundary_identity,
            boundary_evidence=args.boundary_evidence,
        )
        report.update(summary)
        report["limitations"] = ["Normalized trace evidence ids and event identity maps are accepted as declared by the inputs."]
    elif original_kind == "original" and browser_kind == "port-session":
        gap = _read_port_session_gap(args.browser_trace)
        report["trace_summaries"] = {"original": {
            "path": str(args.original_trace),
            "format": "melee-web-original-allocation-history/v1",
            "coverage": "original allocator call/return stream; use the existing replay tool for derived identities",
        }, "browser": gap}
        report.update({"status": "incompatible_or_insufficient_evidence_for_alignment",
                       "matched_prefix_length": 0,
                       "reason": "browser session/state JSONL contains no allocation operation records",
                       "first_difference": None,
                       "first_unsupported": {"side": "browser", "missing": [
                           "allocation/free operation order", "source heap/pool owner identity",
                           "request size and alignment", "allocation/free lifecycle identity",
                           "world generation for allocation events", "caller identity"],
                           "allocation_event_records": gap["allocation_event_records"]},
                       "limitations": ["No event alignment or first divergence was fabricated from session/state rows."]})
        windows = []
    else:
        report.update({"status": "incompatible_or_insufficient_evidence_for_alignment",
                       "matched_prefix_length": 0, "reason": f"unsupported pair of input schemas: {original_kind} vs {browser_kind}",
                       "first_difference": None,
                       "first_unsupported": {"original_format": original_kind,
                                               "browser_format": browser_kind},
                       "limitations": ["No event alignment was attempted for incompatible schemas."]})
        windows = []
    return (0 if report.get("status") == "matching_comparable_prefix" else 1, report, windows)


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        _validate_output_location(args.out, (args.original_trace, args.browser_trace))
        if args.out.exists():
            raise CompareProblem("output", f"output directory already exists: {args.out}")
        args.out.mkdir(parents=True, exist_ok=False)
        try:
            code, report, windows = _main(args)
        except CompareProblem as error:
            code = 2
            report = {"schema": REPORT_SCHEMA, "version": REPORT_VERSION,
                      "status": "incompatible_or_insufficient_evidence_for_alignment",
                      "matched_prefix_length": 0, "reason": error.message,
                      "error_kind": error.kind,
                      "first_difference": None, "first_unsupported": None,
                      "inputs": {"original": str(args.original_trace),
                                 "browser": str(args.browser_trace)}}
            windows = []
        (args.out / "report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        (args.out / "report.md").write_text(_markdown(report), encoding="utf-8")
        with (args.out / "event-window.jsonl").open("w", encoding="utf-8") as stream:
            for row in windows:
                stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        print(json.dumps({"status": report.get("status"),
                          "matched_prefix_length": report.get("matched_prefix_length"),
                          "first_difference": report.get("first_difference"),
                          "reason": report.get("reason"),
                          "output": str(args.out.resolve())}, sort_keys=True))
        return code
    except CompareProblem as error:
        print(json.dumps({"status": error.kind, "error": error.message}, sort_keys=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
