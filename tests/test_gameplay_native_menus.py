"""Original SIS bytecode behavior; runs without proprietary menu assets."""
from pathlib import Path
import json
from collections import defaultdict
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests
sys.path.insert(0, str(ROOT / "tools"))
import compare_transition_trace as transition_compare

def has_menu_trophy_assets(*roots):
    return all(any((root / name).is_file() for root in roots)
               for name in ("TyDatai.usd", "TyDatai.dat"))

def find_stadium_c1_manifest_names(value):
    if isinstance(value, dict):
        if isinstance(value.get("manifest_names"), list):
            return value["manifest_names"]
        for child in value.values():
            found = find_stadium_c1_manifest_names(child)
            if found is not None:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_stadium_c1_manifest_names(child)
            if found is not None:
                return found
    return None

def stadium_c1_selected_file_names():
    preparation = json.loads(
        (ROOT / "docs/evidence/pokemon-stadium-c1a-css-sss-preparation-v1.json")
        .read_text(encoding="utf-8"))
    names = find_stadium_c1_manifest_names(preparation)
    if not isinstance(names, list):
        raise AssertionError("C1 evidence has no selected-file manifest")
    return names

def missing_stadium_fixture_names(menu, game, required_names):
    return sorted(name for name in set(required_names)
                  if not (menu / name).is_file() and not (game / name).is_file())

def stadium_fixture_campaign_is_explicit():
    return (bool(os.environ.get("MELEE_MENU_FIXTURE_ROOT"))
            or os.environ.get("MELEE_REQUIRE_STADIUM_E8_FIXTURES") == "1")


def parse_c1_record(line, prefix):
    parts = line.split()
    if not parts or parts[0] != prefix:
        raise AssertionError(f"expected {prefix} record: {line}")
    result = {}
    for item in parts[1:]:
        if "=" not in item:
            raise AssertionError(f"malformed {prefix} field: {item}")
        key, value = item.split("=", 1)
        if key in result:
            raise AssertionError(f"duplicate {prefix} field: {key}")
        result[key] = value
    return result


def parse_c1_heap_owner_records(stderr, scope, expected_phases=()):
    metadata = []
    results = []
    markers = []
    events = []
    diagnostic_bytes = 0
    for line in stderr.splitlines():
        if line.startswith("C1_HEAP_OWNER_"):
            diagnostic_bytes += len((line + "\n").encode("utf-8"))
            if diagnostic_bytes > 4 * 1024 * 1024:
                raise AssertionError("heap-owner diagnostics exceeded the 4 MiB bound")
        if line.startswith("C1_HEAP_OWNER_META "):
            metadata.append(parse_c1_record(line, "C1_HEAP_OWNER_META"))
        elif line.startswith("C1_HEAP_OWNER_RESULT "):
            results.append(parse_c1_record(line, "C1_HEAP_OWNER_RESULT"))
        elif line.startswith("C1_HEAP_OWNER_MARK "):
            markers.append(parse_c1_record(line, "C1_HEAP_OWNER_MARK"))
        elif line.startswith("C1_HEAP_OWNER_EVENT "):
            events.append(parse_c1_record(line, "C1_HEAP_OWNER_EVENT"))

    if len(metadata) != 1 or len(results) != 1:
        raise AssertionError("heap-owner summary is missing or duplicated")
    meta, result = metadata[0], results[0]
    if meta.get("scope") != scope or result.get("scope") != scope:
        raise AssertionError("heap-owner scope changed")
    if (result.get("status") != meta.get("status") or
            result.get("overflow") != meta.get("overflow") or
            result.get("overflow_count") != meta.get("overflow_count")):
        raise AssertionError("heap-owner status or overflow summaries disagree")
    if result.get("rows") != meta.get("rows") or result.get("markers") != meta.get("markers"):
        raise AssertionError("heap-owner summary counts disagree")
    if (result.get("invalid_count") != meta.get("invalid_count") or
            result.get("pending_rows") != meta.get("pending_rows")):
        raise AssertionError("heap-owner invalid or pending summary counts disagree")
    capacity = int(meta["capacity"])
    row_bytes = int(meta["row_bytes"])
    buffer_bytes = int(meta["buffer_bytes"])
    row_count = int(meta["rows"])
    if (capacity != 4096 or not 0 < row_bytes <= 64 or
            buffer_bytes != capacity * row_bytes or row_count > capacity):
        raise AssertionError("heap-owner fixed storage bounds are inconsistent")
    if meta.get("overflow") != "0" or int(meta.get("overflow_count", "-1")) != 0:
        raise AssertionError("heap-owner buffer overflowed")
    if int(meta.get("invalid_count", "-1")) != 0:
        raise AssertionError("heap-owner completion token was invalid or duplicated")
    if int(meta.get("pending_rows", "-1")) != 0:
        raise AssertionError("heap-owner rows were left incomplete")
    if len(markers) != int(meta["markers"]):
        raise AssertionError("heap-owner marker rows disagree with the declared count")
    if meta.get("status") == "disabled":
        if (meta.get("armed") != "0" or row_count or markers or events or
                result.get("status") != "disabled"):
            raise AssertionError("disabled heap-owner mode recorded partial rows")
        return {"metadata": meta, "markers": [], "events": []}
    if meta.get("status") != "complete" or result.get("status") != "complete" or meta.get("armed") != "1":
        raise AssertionError("heap-owner observer was unavailable")
    if len(events) != row_count or not events:
        raise AssertionError("heap-owner event rows are missing or duplicated")

    if expected_phases:
        if len(markers) != len(expected_phases):
            raise AssertionError("heap-owner phase markers are incomplete")
        previous_count = 0
        for marker, phase in zip(markers, expected_phases):
            event_count = int(marker["next_sequence"]) - 1
            if (marker.get("scope") != scope or marker.get("phase") != phase or
                    int(marker["first_sequence"]) != previous_count + 1 or
                    event_count < previous_count or event_count > row_count or
                    marker.get("census_complete") != "1"):
                raise AssertionError(f"invalid heap-owner phase marker: {marker}")
            previous_count = event_count
        if previous_count != row_count:
            raise AssertionError("heap-owner rows occurred after the final phase marker")
    elif markers:
        raise AssertionError("asset-free heap-owner control unexpectedly emitted phase markers")

    for sequence, event in enumerate(events, 1):
        if int(event.get("sequence", "-1")) != sequence:
            raise AssertionError("heap-owner event order is not monotonic")
        if (event.get("scope") != scope or parse_pointer(event["owner"]) == 0 or
                parse_pointer(event["payload"]) == 0 or
                int(event["hsd_requested"]) <= 0 or
                int(event["lease_status"]) != 0 or event.get("live") != "1" or
                int(event["heap"]) < 0 or
                int(event["lease_requested"]) != int(event["hsd_requested"]) or
                int(event["world"]) <= 0 or
                int(event["allocation_generation"]) <= 0):
            raise AssertionError(f"heap-owner event has no exact live SDK lease: {event}")
        if event.get("kind") not in {
                "objalloc_pool", "class_directory", "class_bucket_metadata", "class_slab"}:
            raise AssertionError(f"unknown heap-owner site kind: {event}")
        if expected_phases:
            expected_phase = expected_phases[-1]
            for marker in markers:
                if sequence < int(marker["next_sequence"]):
                    expected_phase = marker["phase"]
                    break
            if event.get("phase") != expected_phase:
                raise AssertionError(f"heap-owner event phase disagrees with markers: {event}")
        elif event.get("phase") != "asset-free-control":
            raise AssertionError(f"asset-free heap-owner event has an unexpected phase: {event}")
    return {"metadata": meta, "markers": markers, "events": events}


def parse_pointer(value):
    if value in {"0", "0x0", "nullptr", "(nil)"}:
        return 0
    return int(value, 16) if value.startswith("0x") else int(value)


def c1_record_key(row):
    return (int(row["world"]), row["consumer"], int(row["cycle"]), row["phase"])


def parse_c1_heap_dumps(stderr):
    dumps = {}
    current_key = None
    current = None
    section = None
    for line in stderr.splitlines():
        if line.startswith("C1_HEAP_DUMP_BEGIN "):
            fields = parse_c1_record(line, "C1_HEAP_DUMP_BEGIN")
            current_key = c1_record_key(fields)
            if current_key in dumps or current is not None:
                raise AssertionError(f"duplicate/nested OSDumpHeap section: {current_key}")
            current = {"allocated": [], "free": [], "sections": set(), "called": False}
        elif line.startswith("C1_HEAP_DUMP_END "):
            fields = parse_c1_record(line, "C1_HEAP_DUMP_END")
            if current is None or c1_record_key(fields) != current_key:
                raise AssertionError("OSDumpHeap end marker does not match its beginning")
            if current["sections"] != {"allocated", "free"} or not current["called"]:
                raise AssertionError(f"OSDumpHeap INFO rows were unavailable or filtered: {current_key}")
            dumps[current_key] = current
            current_key = None
            current = None
            section = None
        elif current is not None:
            body = line.rsplit("] ", 1)[-1].strip()
            if body.startswith("OSDumpHeap("):
                current["called"] = True
            elif body == "--------Allocated":
                section = "allocated"
                current["sections"].add(section)
            elif body == "--------Free":
                section = "free"
                current["sections"].add(section)
            elif body in {"--------Invalid", "--------Broken"}:
                raise AssertionError(f"OSDumpHeap refused/broke heap {current_key}: {body}")
            elif body.startswith("addr"):
                continue
            elif section and body:
                fields = body.split()
                if len(fields) != 5:
                    raise AssertionError(f"malformed OSDumpHeap {section} row: {body}")
                row = {"addr": parse_pointer(fields[0]), "size": int(fields[1]),
                       "end": parse_pointer(fields[2]), "prev": parse_pointer(fields[3]),
                       "next": parse_pointer(fields[4])}
                current[section].append(row)
    if current is not None:
        raise AssertionError(f"unterminated OSDumpHeap section: {current_key}")
    return dumps


C1_V23_CENSUS_PHASES = (
    "before-light-preparation",
    "after-light-preparation-before-e8",
    "after-oninit",
    "after-stage-last-and-light-destroy",
)
C1_V23_CENSUS_ROW_LIMIT = 4096
C1_V23_CENSUS_OUTPUT_LIMIT = 16 * 1024 * 1024



def parse_c1_v23_heap_census(stderr):
    expected_keys = {(0, "original-oninit", 0, phase)
                     for phase in C1_V23_CENSUS_PHASES}
    statuses, snapshots, guards = {}, {}, {}
    allocations, queries = defaultdict(list), defaultdict(list)
    in_dump = False
    diagnostic_bytes = 0

    def count_bytes(line):
        nonlocal diagnostic_bytes
        diagnostic_bytes += len((line + "\n").encode("utf-8"))
        if diagnostic_bytes > C1_V23_CENSUS_OUTPUT_LIMIT:
            raise AssertionError("V23 census diagnostic output exceeded 16 MiB")

    for line in stderr.splitlines():
        if line.startswith("C1_HEAP_DUMP_BEGIN "):
            in_dump = True
        if in_dump or line.startswith(("C1_V23_CENSUS ", "C1_HEAP_SNAPSHOT ",
                                       "C1_HEAP_ALLOC ", "C1_HEAP_GUARD ",
                                       "C1_HEAP_QUERY ", "C1_HEAP_DUMP_BEGIN ",
                                       "C1_HEAP_DUMP_END ")):
            count_bytes(line)
        if line.startswith("C1_HEAP_DUMP_END "):
            in_dump = False

        if line.startswith("C1_V23_CENSUS "):
            row = parse_c1_record(line, "C1_V23_CENSUS")
            key = (int(row["world"]), row["consumer"], int(row["cycle"]), row["phase"])
            if key not in expected_keys:
                raise AssertionError(f"unexpected V23 census phase: {key}")
            if key in statuses:
                raise AssertionError(f"duplicate V23 census status: {key}")
            statuses[key] = row
        elif line.startswith("C1_HEAP_SNAPSHOT "):
            row = parse_c1_record(line, "C1_HEAP_SNAPSHOT")
            key = c1_record_key(row)
            if key in expected_keys:
                if key in snapshots:
                    raise AssertionError(f"duplicate V23 census snapshot: {key}")
                snapshots[key] = row
        elif line.startswith("C1_HEAP_ALLOC "):
            row = parse_c1_record(line, "C1_HEAP_ALLOC")
            key = c1_record_key(row)
            if key in expected_keys:
                allocations[key].append(row)
        elif line.startswith("C1_HEAP_GUARD "):
            row = parse_c1_record(line, "C1_HEAP_GUARD")
            key = c1_record_key(row)
            if key in expected_keys:
                if key in guards:
                    raise AssertionError(f"duplicate V23 census guard: {key}")
                guards[key] = row
        elif line.startswith("C1_HEAP_QUERY "):
            row = parse_c1_record(line, "C1_HEAP_QUERY")
            key = c1_record_key(row)
            if key in expected_keys:
                queries[key].append(row)

    if in_dump:
        raise AssertionError("unterminated V23 OSDumpHeap section")
    for label, observed in (("status", statuses), ("snapshot", snapshots),
                            ("guard", guards)):
        if set(observed) != expected_keys:
            raise AssertionError(f"missing V23 census {label} phase")
    for key, row in statuses.items():
        if row.get("status") != "complete":
            raise AssertionError(f"V23 census unavailable at {key[3]}: {row.get('error')}")
        if not 0 <= int(row.get("rows", "-1")) <= C1_V23_CENSUS_ROW_LIMIT:
            raise AssertionError(f"V23 census row bound exceeded at {key}")

    dumps = parse_c1_heap_dumps(stderr)
    if set(dumps) != expected_keys:
        raise AssertionError("V23 OSDumpHeap phases are missing or unexpected")

    generation = heap = descriptor_bounds = None
    lease_payloads, metrics = {}, {}
    guard_fields = ("equal", "source_healthy", "world_equal", "heap_owner",
                    "watermark", "ticks", "free_bytes", "roots", "classes",
                    "pools", "gobj_used", "proc_used")
    for phase in C1_V23_CENSUS_PHASES:
        key = (0, "original-oninit", 0, phase)
        snapshot, status, guard = snapshots[key], statuses[key], guards[key]
        if any(guard.get(field) != "1" for field in guard_fields):
            raise AssertionError(f"V23 census purity guard failed at {phase}")
        rows = allocations[key]
        row_count = int(snapshot["rows"])
        if (snapshot.get("overflow") != "0" or row_count > C1_V23_CENSUS_ROW_LIMIT or
                int(status["rows"]) != row_count or len(rows) != row_count):
            raise AssertionError(f"V23 census overflow or row mismatch at {phase}")
        current_generation, current_heap = int(snapshot["generation"]), int(snapshot["heap"])
        watermark = int(snapshot["watermark"])
        if current_generation <= 0 or current_heap < 0 or watermark < 0:
            raise AssertionError(f"invalid V23 source heap identity at {phase}")
        if generation is None:
            generation, heap = current_generation, current_heap
        elif (current_generation, current_heap) != (generation, heap):
            raise AssertionError(f"V23 census changed source world or heap at {phase}")

        allocated, free = dumps[key]["allocated"], dumps[key]["free"]
        if not allocated or not free:
            raise AssertionError(f"V23 OSDumpHeap omitted an allocated/free section at {phase}")
        for cells in (allocated, free):
            for index, cell in enumerate(cells):
                if (cell["addr"] % 32 or cell["size"] < 64 or cell["size"] % 32 or
                        cell["end"] != cell["addr"] + cell["size"] or
                        cell["prev"] != (cells[index - 1]["addr"] if index else 0) or
                        cell["next"] != (cells[index + 1]["addr"]
                                         if index + 1 < len(cells) else 0)):
                    raise AssertionError(f"malformed V23 OSDumpHeap cell at {phase}: {cell}")
        cells = sorted(allocated + free, key=lambda cell: cell["addr"])
        for previous, cell in zip(cells, cells[1:]):
            if previous["end"] != cell["addr"]:
                raise AssertionError(f"V23 heap cells do not partition descriptor at {phase}")
        bounds = (cells[0]["addr"], cells[-1]["end"])
        if descriptor_bounds is None:
            descriptor_bounds = bounds
        elif descriptor_bounds != bounds:
            raise AssertionError(f"V23 heap descriptor bounds changed at {phase}")

        dumped = sorted((cell["addr"] + 32, cell["size"] - 32) for cell in allocated)
        visited = sorted((parse_pointer(row["payload"]), int(row["visitor_capacity"]))
                         for row in rows)
        if dumped != visited or len({payload for payload, _ in visited}) != len(visited):
            raise AssertionError(f"V23 visitor rows differ from OSDumpHeap at {phase}")
        for row in rows:
            payload = parse_pointer(row["payload"])
            capacity, requested = int(row["visitor_capacity"]), int(row["requested"])
            alloc_gen, live = int(row["allocation_generation"]), int(row["live"])
            if (int(row["lease_status"]) != 0 or int(row["referent_capacity"]) != capacity or
                    int(row["heap"]) != heap or int(row["generation"]) != generation or
                    int(row["lease_world"]) != generation):
                raise AssertionError(f"V23 exact lease query disagrees at {phase}: {row}")
            if live not in (0, 1) or requested < 0:
                raise AssertionError(f"invalid V23 live/requested fields at {phase}: {row}")
            if live:
                if not 0 < alloc_gen <= watermark or requested > capacity:
                    raise AssertionError(f"invalid live V23 lease at {phase}: {row}")
                identity = (heap, generation, alloc_gen)
                if lease_payloads.setdefault(identity, payload) != payload:
                    raise AssertionError(f"V23 allocation identity changed exact payload: {row}")
            elif requested or alloc_gen:
                raise AssertionError(f"unknown V23 lease reports allocation metadata: {row}")

        query_by_kind = {row["kind"]: row for row in queries[key]}
        if (len(query_by_kind) != len(queries[key]) or
                set(query_by_kind) != {"unknown", "interior"}):
            raise AssertionError(f"V23 exact-payload controls are missing or duplicated at {phase}")
        for query in query_by_kind.values():
            if (int(query["status"]) != 0 or int(query["live"]) != 0 or
                    int(query["generation"]) != 0 or
                    int(query["lease_world"]) != generation):
                raise AssertionError(f"V23 query is not explicitly unknown at {phase}: {query}")
        exact_payloads = {payload for payload, _ in visited}
        if (parse_pointer(query_by_kind["unknown"]["payload"]) in exact_payloads or
                parse_pointer(query_by_kind["interior"]["payload"]) not in
                {payload + 1 for payload, _ in visited}):
            raise AssertionError(f"V23 unknown/interior query classification failed at {phase}")

        allocated_span = sum(cell["size"] for cell in allocated)
        free_bytes = sum(cell["size"] - 32 for cell in free)
        if free_bytes != int(snapshot["free"]):
            raise AssertionError(f"V23 free-cell byte accounting failed at {phase}")
        metrics[phase] = (free_bytes, allocated_span, len(free))

    transitions = []
    for before_phase, after_phase in zip(C1_V23_CENSUS_PHASES, C1_V23_CENSUS_PHASES[1:]):
        before, after = metrics[before_phase], metrics[after_phase]
        observed = before[0] - after[0]
        derived = after[1] - before[1] + 32 * (after[2] - before[2])
        if observed != derived:
            raise AssertionError(f"V23 heap partition conservation failed: "
                                 f"{before_phase}->{after_phase}")
        transitions.append({"from": before_phase, "to": after_phase,
                            "free_bytes_delta": observed,
                            "allocated_cell_span_delta": after[1] - before[1],
                            "free_cell_count_delta": after[2] - before[2]})
    return {"phase_count": 4, "row_count": sum(int(statuses[
                (0, "original-oninit", 0, phase)]["rows"])
                for phase in C1_V23_CENSUS_PHASES),
            "heap": heap, "generation": generation,
            "descriptor_bounds": descriptor_bounds, "transitions": transitions,
            "diagnostic_bytes": diagnostic_bytes}

class NativeMenuSourceTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-c1a-native-menu-")

    def test_c1_heap_owner_parser_requires_exact_bounded_rows(self):
        meta = ("C1_HEAP_OWNER_META scope=asset-free-control status=complete armed=1 "
                "rows=1 row_bytes=64 capacity=4096 buffer_bytes=262144 "
                "overflow=0 overflow_count=0 invalid_count=0 pending_rows=0 markers=0")
        event = ("C1_HEAP_OWNER_EVENT scope=asset-free-control sequence=1 "
                 "phase=asset-free-control kind=objalloc_pool owner_label=GObj "
                 "owner=0x1000 payload=0x2000 owner_size=56 auxiliary=2 "
                 "hsd_requested=112 lease_status=0 live=1 heap=0 "
                 "lease_requested=112 world=1 allocation_generation=2")
        result = ("C1_HEAP_OWNER_RESULT scope=asset-free-control status=complete "
                  "rows=1 markers=0 overflow=0 overflow_count=0 "
                  "invalid_count=0 pending_rows=0")
        parsed = parse_c1_heap_owner_records(
            "\n".join((meta, event, result)), "asset-free-control")
        self.assertEqual(parsed["events"][0]["sequence"], "1")
        malformed = (
            (event.replace("sequence=1", "sequence=2"), "event order"),
            (event.replace("allocation_generation=2", "allocation_generation=0"),
             "exact live SDK lease"),
            (event.replace("lease_requested=112", "lease_requested=111"),
             "exact live SDK lease"),
            (meta.replace("buffer_bytes=262144", "buffer_bytes=262143"),
             "storage bounds"),
            (meta.replace("overflow=0", "overflow=1"), "status or overflow"),
            (meta.replace("invalid_count=0", "invalid_count=1"),
             "invalid or pending summary counts disagree"),
            (meta.replace("pending_rows=0", "pending_rows=1"),
             "invalid or pending summary counts disagree"),
            (meta.replace("status=complete", "status=unavailable"), "status or overflow"),
            (result.replace("overflow_count=0", "overflow_count=1"),
             "status or overflow"),
        )
        for bad_line, message in malformed:
            with self.subTest(message=message):
                stderr = "\n".join((bad_line if bad_line.startswith("C1_HEAP_OWNER_META") else meta,
                                     bad_line if bad_line.startswith("C1_HEAP_OWNER_EVENT") else event,
                                     bad_line if bad_line.startswith("C1_HEAP_OWNER_RESULT") else result))
                with self.assertRaisesRegex(AssertionError, message):
                    parse_c1_heap_owner_records(stderr, "asset-free-control")

        for field, value, reason in (
                ("overflow_count", "1", "overflowed"),
                ("invalid_count", "1", "invalid or duplicated"),
                ("pending_rows", "1", "left incomplete")):
            bad_meta = meta.replace(f"{field}=0", f"{field}={value}")
            bad_result = result.replace(f"{field}=0", f"{field}={value}")
            if field == "overflow_count":
                bad_meta = bad_meta.replace("overflow=0", "overflow=1")
                bad_result = bad_result.replace("overflow=0", "overflow=1")
            bad_status = bad_meta.replace("status=complete", "status=unavailable")
            bad_result_status = bad_result.replace("status=complete", "status=unavailable")
            with self.subTest(message=reason):
                with self.assertRaisesRegex(AssertionError, reason):
                    parse_c1_heap_owner_records(
                        "\n".join((bad_status, event, bad_result_status)),
                        "asset-free-control")

        marked_meta = meta.replace("markers=0", "markers=2")
        marked_event = event.replace("phase=asset-free-control",
                                     "phase=after-light-preparation-before-e8")
        marked_result = result.replace("markers=0", "markers=2")
        marker_before = ("C1_HEAP_OWNER_MARK scope=asset-free-control "
                         "phase=before-light-preparation first_sequence=1 "
                         "next_sequence=1 census_complete=1")
        marker_after = ("C1_HEAP_OWNER_MARK scope=asset-free-control "
                        "phase=after-light-preparation-before-e8 first_sequence=1 "
                        "next_sequence=2 census_complete=1")
        expected_phases = ("before-light-preparation",
                           "after-light-preparation-before-e8")
        marked = "\n".join((marked_meta, marker_before, marker_after,
                            marked_event, marked_result))
        parsed = parse_c1_heap_owner_records(
            marked, "asset-free-control", expected_phases)
        self.assertEqual(len(parsed["markers"]), 2)
        marked_mutations = (
            (marker_after.replace("scope=asset-free-control", "scope=other"),
             "invalid heap-owner phase marker"),
            (marker_after.replace("next_sequence=2", "next_sequence=1"),
             "rows occurred after the final phase marker"),
        )
        for bad_marker, message in marked_mutations:
            with self.subTest(message=message):
                lines = [marked_meta, marker_before, marker_after,
                         marked_event, marked_result]
                if bad_marker.startswith("C1_HEAP_OWNER_META"):
                    lines[0] = bad_marker
                else:
                    lines[2] = bad_marker
                with self.assertRaisesRegex(AssertionError, message):
                    parse_c1_heap_owner_records(
                        "\n".join(lines), "asset-free-control", expected_phases)
        declared_meta = marked_meta.replace("markers=2", "markers=3")
        declared_result = marked_result.replace("markers=2", "markers=3")
        with self.assertRaisesRegex(AssertionError, "marker rows disagree"):
            parse_c1_heap_owner_records(
                "\n".join((declared_meta, marker_before, marker_after,
                           marked_event, declared_result)),
                "asset-free-control", expected_phases)

    def test_stadium_yakumono_exchange_round_trip_without_assets(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The C1 diagnostic native host trace has not been built")
        command = [str(node_runtime()), str(target),
                   "--stadium-yakumono-exchange"]
        (self.scratch / "yakumono-exchange-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "yakumono-exchange.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "yakumono-exchange.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "yakumono-exchange.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "yakumono-exchange.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Stadium yakumono exchange asset-free control passed; pointer restored",
            run.stdout,
        )
        self.assertIn("source StageInfo/GObj owner state unchanged", run.stdout)

    def test_stadium_c1a_raw_pad_selection_stops_before_match_admission(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        required = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        missing = [str(menu / name) for name in required if not (menu / name).is_file()]
        if not target.is_file() or missing:
            detail = ", ".join(missing[:5])
            self.skipTest("C1a requires its built host trace and exact owned menu closure" +
                          (f"; missing {detail}" if detail else ""))
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "c1a-selection-port.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-c1a-v1"]
        (self.scratch / "c1a-selection-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "c1a-selection.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "c1a-selection.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "c1a-selection.stdout").write_text(run.stdout, encoding="utf-8")
        (self.scratch / "c1a-selection.stderr").write_text(run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "C1a raw PAD CSS->SSS Stadium selection and exact preparation manifest passed",
            run.stdout,
        )
        self.assertIn(
            "ordinary admission and source/stage construction remain closed",
            run.stdout,
        )

    def test_stadium_c1_reopened_context_lifecycle_preflight(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        required = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected = stadium_c1_selected_file_names()
        self.assertEqual(len(required), 76)
        self.assertEqual(len(set(required)), 76)
        self.assertEqual(len(selected), 36)
        self.assertEqual(len(set(selected)), 36)
        required_union = sorted(set(required) | set(selected))
        self.assertEqual(len(required_union), 98)
        missing = missing_stadium_fixture_names(menu, game, required_union)
        if missing:
            detail = ", ".join(missing[:5])
            message = (
                "C1 context preflight requires the exact 98-file menu/selected union"
                f"; missing {detail}"
            )
            if stadium_fixture_campaign_is_explicit():
                self.fail(message)
            self.skipTest(message)
        if not target.is_file():
            self.skipTest("C1 context preflight requires its built host trace")
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "c1-context-preflight.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-c1-context-preflight-v1"]
        (self.scratch / "c1-context-preflight-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "c1-context-preflight.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "c1-context-preflight.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "c1-context-preflight.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "c1-context-preflight.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "C1 reopened-context lifecycle preflight passed; no E8 request",
            run.stdout,
        )
        self.assertIn(
            "stage publication, or source menu entry",
            run.stdout,
        )

    def test_stadium_c1_item_state_owner_preflight(self):
        self.run_stadium_owner_preflight(
            "stadium-c1-item-state-preflight-v1", "c1-item-state-preflight",
            "C1 reopened-context lifecycle and item-state-owner preflight passed; no E8 request")

    def test_stadium_screen_roots_owner_preflight(self):
        self.run_stadium_owner_preflight(
            "stadium-screen-roots-preflight-v1", "screen-roots-preflight",
            "C1 screen-root preflight preserved canonical IMAGE, writable SIS and two owner/catalog lifetimes; no stage entry or ticks")

    def run_stadium_owner_preflight(self, recipe, prefix, success_message):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        required = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected = stadium_c1_selected_file_names()
        required_union = sorted(set(required) | set(selected))
        self.assertEqual(len(required), 76)
        self.assertEqual(len(selected), 36)
        self.assertEqual(len(required_union), 98)
        self.assertIn("ItCo.usd", required_union)
        self.assertIn("GrPs.usd", required_union)
        missing = missing_stadium_fixture_names(menu, game, required_union)
        if missing:
            detail = ", ".join(missing[:5])
            message = (
                "Stadium owner preflight requires the retained 98-file union"
                f"; missing {detail}"
            )
            if stadium_fixture_campaign_is_explicit():
                self.fail(message)
            self.skipTest(message)
        if not target.is_file():
            self.skipTest("Stadium owner preflight requires its built host trace")
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / (prefix + ".jsonl")
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision,
                   recipe]
        (self.scratch / (prefix + "-command.txt")).write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / (prefix + ".stdout")).write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / (prefix + ".stderr")).write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / (prefix + ".stdout")).write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / (prefix + ".stderr")).write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            success_message,
            run.stdout,
        )
        self.assertIn(
            "stage publication, or source menu entry",
            run.stdout,
        )

        if recipe == "stadium-screen-roots-preflight-v1":
            self.assertIn(
                "C1 live Stadium IMAGE source hit/miss/remove passed twice; no stage entry or ticks",
                run.stdout,
            )
            self.assertEqual(len(trace.read_text(encoding="utf-8").splitlines()), 1,
                             "Screen root probe must retain a header-only trace")

    def test_stadium_c1_fixture_preflight_detects_missing_selected_file(self):
        target = self.scratch / "synthetic-c1-fixture-preflight"
        menu, game = target / "native-menus", target / "next-gate"
        menu.mkdir(parents=True)
        game.mkdir()
        script = "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; " \
                 "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), " \
                 "'dsp_coef.bin', 'sislib_font.bin']))"
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        self.assertIn("PlCo.dat", selected_names)
        required_union = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(selected_names), 36)
        self.assertEqual(len(required_union), 98)
        for name in required_union:
            if name == "PlCo.dat":
                continue
            root = menu if name in menu_names else game
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.assertEqual(
            missing_stadium_fixture_names(menu, game, required_union),
            ["PlCo.dat"],
        )

    def test_stadium_e8_one_request_and_checked_teardown(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        if not fixture_value:
            if os.environ.get("MELEE_REQUIRE_STADIUM_E8_FIXTURES") == "1":
                self.fail("MELEE_MENU_FIXTURE_ROOT is required for the frozen E8 packet")
            self.skipTest("The reviewed C1 fixture root is required for the E8 trace")
        fixture_root = Path(fixture_value)
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        self.assertTrue(target.is_file(), f"Build the diagnostic target first: {target}")
        self.assertTrue(menu.is_dir(), f"Missing owned menu fixture root: {menu}")
        self.assertTrue(game.is_dir(), f"Missing owned game fixture root: {game}")

        script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(set(menu_names)), len(menu_names))
        preparation = json.loads(
            (ROOT / "docs/evidence/pokemon-stadium-c1a-css-sss-preparation-v1.json")
            .read_text(encoding="utf-8"))
        self.assertEqual(
            preparation.get("schema"),
            "melee-web-pokemon-stadium-c1a-css-sss-preparation-v1",
        )

        selected_names = stadium_c1_selected_file_names()
        self.assertIsNotNone(selected_names, "C1 evidence has no selected-file manifest")
        self.assertEqual(len(selected_names), 36)
        self.assertEqual(len(set(selected_names)), len(selected_names))
        self.assertEqual(
            set(selected_names) & {
                "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat",
                "GrPs4.dat", "pstadium.ssm", "pstadium.hps", "pokesta.hps",
            },
            {
                "GrPs.usd", "GrPs1.dat", "GrPs2.dat", "GrPs3.dat",
                "GrPs4.dat", "pstadium.ssm", "pstadium.hps", "pokesta.hps",
            },
        )
        required = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(required), 98)
        missing = [name for name in required
                   if not (menu / name).is_file() and not (game / name).is_file()]
        self.assertFalse(
            missing,
            "Frozen C1 selected RuntimeFiles are incomplete before launch: " +
            ", ".join(missing),
        )

        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-e8-request.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-e8-request-v1"]
        (self.scratch / "stadium-e8-request-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        (self.scratch / "stadium-e8-fixture-preflight.json").write_text(
            json.dumps({"menu_names": menu_names,
                        "selected_names": selected_names,
                        "missing": missing}, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "stadium-e8-request.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "stadium-e8-request.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "stadium-e8-request.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "stadium-e8-request.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-6000:])
        self.assertIn(
            "C1 reopened-context lifecycle preflight and one E8 typed request passed",
            run.stdout,
        )
        self.assertIn(
            "ordinary match admission and gameplay entry remain closed",
            run.stdout,
        )
        result = next(json.loads(line) for line in run.stdout.splitlines()
                      if line.startswith('{"probe":"stadium-e8-request"'))
        self.assertEqual(result["source_size_name"], "/GrPs.usd")
        self.assertEqual(result["typed_open_name"], "/GrPs.usd")
        self.assertEqual(result["stage_info_x6E4"][0], -1)
        self.assertIn("stage_info_xA0_observed_only", result)
        self.assertEqual(result["itemdata_public_calls"], 0)
        self.assertEqual(result["map_plit_public_calls"], 0)
        self.assertFalse(result["stage_objects_started"])
        self.assertTrue(result["checked_teardown"])
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        self.assertEqual(rows[0]["record"], "header")
        self.assertEqual(rows[0]["input_recipe"], "stadium-e8-request-v1")
        events = [row for row in rows if row.get("record") == "event"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "stadium_e8_request_returned")
        self.assertEqual(events[0]["selection"]["rules"]["stage_kind"], 3)

    def test_stadium_ground_map1_owner_lifetime(self):
        if os.environ.get("MELEE_RUN_STADIUM_GROUND_MAP1_OWNER") != "1":
            self.skipTest(
                "The single retained map1 constructor experiment requires its explicit run gate"
            )
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        if not fixture_value:
            self.fail("MELEE_MENU_FIXTURE_ROOT is required for the retained map1 packet")
        fixture_root = Path(fixture_value)
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        self.assertTrue(target.is_file(), f"Build the reviewed diagnostic target first: {target}")
        self.assertTrue(menu.is_dir(), f"Missing owned menu fixture root: {menu}")
        self.assertTrue(game.is_dir(), f"Missing owned game fixture root: {game}")

        script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(selected_names), 36)
        required = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(required), 98)
        missing = [name for name in required
                   if not (menu / name).is_file() and not (game / name).is_file()]
        self.assertFalse(
            missing,
            "Frozen C1 map1 RuntimeFiles are incomplete before launch: " +
            ", ".join(missing),
        )

        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-ground-map1-owner.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-ground-map1-owner-v1"]
        (self.scratch / "stadium-ground-map1-owner-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        (self.scratch / "stadium-ground-map1-owner-fixture-preflight.json").write_text(
            json.dumps({"menu_names": menu_names,
                        "selected_names": selected_names,
                        "missing": missing}, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "stadium-ground-map1-owner.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "stadium-ground-map1-owner.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "stadium-ground-map1-owner.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "stadium-ground-map1-owner.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-7000:])
        self.assertIn(
            "C1a raw PAD CSS->SSS selection, one E8 typed request, and one "
            "Ground map1 constructor/removal passed",
            run.stdout,
        )
        e8_result = next(json.loads(line) for line in run.stdout.splitlines()
                         if line.startswith('{"probe":"stadium-e8-request"'))
        self.assertTrue(e8_result["stage_objects_started"])
        self.assertTrue(e8_result["checked_teardown"])
        self.assertEqual(e8_result["source_size_name"], "/GrPs.usd")
        self.assertEqual(e8_result["typed_open_name"], "/GrPs.usd")
        self.assertEqual(e8_result["itemdata_public_calls"], 0)
        self.assertEqual(e8_result["map_plit_public_calls"], 0)
        owner_result = next(json.loads(line) for line in run.stdout.splitlines()
                            if line.startswith('{"probe":"stadium-ground-map1-owner"'))
        self.assertEqual(owner_result["map_id"], 1)
        self.assertEqual(owner_result["requested_bytes"], 64)
        self.assertGreater(owner_result["source_heap"], -1)
        self.assertGreater(owner_result["world_generation"], 0)
        self.assertGreater(owner_result["allocation_generation"], 0)
        self.assertEqual(owner_result["authored_marker_pair_count"], 20)
        self.assertEqual(owner_result["map1_matched_marker_pair_count"], 0)
        self.assertTrue(owner_result["stage_info_marker_baseline_empty"])
        self.assertTrue(owner_result["device_bytes_restored"])
        self.assertTrue(owner_result["buffer_retired"])
        self.assertFalse(owner_result["callback_dispatch"])
        self.assertEqual(owner_result["proc_ticks"], 0)
        self.assertFalse(owner_result["rendered"])
        self.assertTrue(owner_result["single_constructor_removal"])
        owner_ids = owner_result["ft_device_owner_ids"]
        self.assertEqual(len(owner_ids), 6)
        self.assertEqual(len(set(owner_ids)), 6)

        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        self.assertEqual(rows[0]["record"], "header")
        self.assertEqual(rows[0]["input_recipe"], "stadium-ground-map1-owner-v1")
        events = [row for row in rows if row.get("record") == "event"]
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["event"], "stadium_e8_request_returned")
        self.assertEqual(events[0]["selection"]["rules"]["stage_kind"], 3)

    def test_c1_v23_heap_census_parser_rejects_incomplete_output(self):
        fields = "world=0 consumer=original-oninit cycle=0"
        lines = []
        for phase in C1_V23_CENSUS_PHASES:
            lines.extend([
                f"C1_HEAP_DUMP_BEGIN {fields} phase={phase}",
                "[info] [aurora::os::alloc] OSDumpHeap(0)",
                "[info] [aurora::os::alloc] addr\tsize\tend\tprev\tnext",
                "[info] [aurora::os::alloc] --------Allocated",
                "[info] [aurora::os::alloc] 0x1000\t96\t0x1060\t0x0\t0x0",
                "[info] [aurora::os::alloc] --------Free",
                "[info] [aurora::os::alloc] 0x1060\t64\t0x10a0\t0x0\t0x0",
                f"C1_HEAP_DUMP_END {fields} phase={phase}",
                f"C1_HEAP_QUERY {fields} phase={phase} kind=unknown payload=0x2000 status=0 live=0 generation=0 lease_world=1 prior_generation=0 refused=0",
                f"C1_HEAP_QUERY {fields} phase={phase} kind=interior payload=0x1021 status=0 live=0 generation=0 lease_world=1 prior_generation=0 refused=0",
                f"C1_HEAP_GUARD {fields} phase={phase} equal=1 source_healthy=1 world_equal=1 heap_owner=1 watermark=1 ticks=1 free_bytes=1 roots=1 classes=1 pools=1 gobj_used=1 proc_used=1",
                f"C1_HEAP_SNAPSHOT {fields} phase={phase} generation=1 heap=0 free=32 watermark=1 rows=1 overflow=0",
                f"C1_HEAP_ALLOC {fields} phase={phase} generation=1 payload=0x1020 visitor_capacity=64 referent_capacity=64 lease_status=0 live=1 heap=0 lease_world=1 requested=48 allocation_generation=1",
                f"C1_V23_CENSUS status=complete {fields} phase={phase} rows=1",
            ])
        valid = "\n".join(lines) + "\n"
        result = parse_c1_v23_heap_census(valid)
        self.assertEqual(result["phase_count"], 4)
        self.assertEqual(result["row_count"], 4)
        self.assertEqual(len(result["transitions"]), 3)

        unavailable = next(line for line in lines
                           if line.startswith("C1_V23_CENSUS status=complete") and
                           "phase=after-oninit " in line)
        bad_inputs = (
            valid.replace(unavailable + "\n", "", 1),
            valid.replace("overflow=0", "overflow=1", 1),
            valid + unavailable + "\n",
            valid.replace(
                "0x1000\t96\t0x1060\t0x0\t0x0", "0x1000\tbroken", 1),
            valid.replace("generation=1 payload=0x1020", "generation=2 payload=0x1020", 1),
            valid.replace("requested=48 allocation_generation=1", "requested=-1 allocation_generation=1", 1),
            valid.replace("live=1 heap=0", "live=2 heap=0", 1),
            valid.replace("watermark=1 rows=1", "watermark=-1 rows=1", 1),
            valid.replace(
                unavailable,
                unavailable.replace("status=complete", "status=unavailable")
                .replace("rows=1", "error=probe_failed"), 1),
        )
        for malformed in bad_inputs:
            with self.subTest(malformed=malformed[-100:]):
                with self.assertRaises(AssertionError):
                    parse_c1_v23_heap_census(malformed)

    def test_stadium_cache_live_asset_free_controls(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed C1 cache/live reducer first")
        commands = {
            "off": [str(node_runtime()), str(target), "--stadium-cache-live-controls"],
            "on": [str(node_runtime()), str(target), "--stadium-cache-live-controls-owner"],
        }
        (self.scratch / "cache-live-command.txt").write_text(
            "\n".join(f"{mode}: {' '.join(command)}"
                       for mode, command in commands.items()) + "\n",
            encoding="utf-8")
        runs = {}
        for mode, command in commands.items():
            try:
                runs[mode] = subprocess.run(
                    command, cwd=ROOT, capture_output=True, text=True, timeout=30)
            except subprocess.TimeoutExpired as failure:
                for stream in ("stdout", "stderr"):
                    value = getattr(failure, stream)
                    (self.scratch / (f"cache-live-{mode}." + stream)).write_bytes(
                        value.encode() if isinstance(value, str) else (value or b""))
                raise
            (self.scratch / f"cache-live-{mode}.stdout").write_text(
                runs[mode].stdout, encoding="utf-8")
            (self.scratch / f"cache-live-{mode}.stderr").write_text(
                runs[mode].stderr, encoding="utf-8")
        off_run, run = runs["off"], runs["on"]
        self.assertEqual(off_run.returncode, 0,
                         (off_run.stdout + off_run.stderr)[-9000:])
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-9000:])
        self.assertIn("bounded original SDK allocation/free census", run.stdout)
        self.assertIn("source-state purity", run.stdout)
        self.assertNotIn("C1_HEAP_CENSUS_UNAVAILABLE", run.stderr)
        self.assertNotIn("C1_CACHE_LIVE_REFUSAL", run.stderr)

        owner_off = parse_c1_heap_owner_records(
            off_run.stderr, "asset-free-control")
        owner_on = parse_c1_heap_owner_records(
            run.stderr, "asset-free-control")
        self.assertEqual(owner_off["events"], [])
        self.assertGreater(len(owner_on["events"]), 0)
        invariant_prefixes = (
            "C1_HEAP_DUMP_BEGIN ", "C1_HEAP_DUMP_END ",
            "C1_HEAP_SNAPSHOT ", "C1_HEAP_ALLOC ", "C1_HEAP_GUARD ",
            "C1_HEAP_QUERY ", "C1_HEAP_QUERY_GUARD ", "C1_CACHE_LIVE ",
        )
        source_identity_order = lambda stderr: [
            line for line in stderr.splitlines()
            if line.startswith(invariant_prefixes)
        ]
        self.assertEqual(source_identity_order(off_run.stderr),
                         source_identity_order(run.stderr),
                         "Recorder on/off changed source allocation identities, state or order")
        self.assertEqual(parse_c1_heap_dumps(off_run.stderr),
                         parse_c1_heap_dumps(run.stderr),
                         "Recorder on/off changed the exact SDK heap dump")

        snapshots = {}
        allocations = defaultdict(list)
        queries = []
        guards = {}
        query_guards = {}
        cache_rows = [line for line in run.stderr.splitlines()
                      if line.startswith("C1_CACHE_LIVE ")]
        self.assertEqual(len(cache_rows), 24)
        for line in run.stderr.splitlines():
            if line.startswith("C1_HEAP_SNAPSHOT "):
                row = parse_c1_record(line, "C1_HEAP_SNAPSHOT")
                key = c1_record_key(row)
                self.assertNotIn(key, snapshots)
                snapshots[key] = row
            elif line.startswith("C1_HEAP_ALLOC "):
                row = parse_c1_record(line, "C1_HEAP_ALLOC")
                allocations[c1_record_key(row)].append(row)
            elif line.startswith("C1_HEAP_QUERY "):
                queries.append(parse_c1_record(line, "C1_HEAP_QUERY"))
            elif line.startswith("C1_HEAP_GUARD "):
                row = parse_c1_record(line, "C1_HEAP_GUARD")
                guards[c1_record_key(row)] = row
            elif line.startswith("C1_HEAP_QUERY_GUARD "):
                row = parse_c1_record(line, "C1_HEAP_QUERY_GUARD")
                query_guards[c1_record_key(row)] = row

        census_identities = {
            (int(row["generation"]), int(row["allocation_generation"]),
             parse_pointer(row["payload"]), int(row["heap"]))
            for rows in allocations.values() for row in rows
            if int(row["live"]) == 1
        }
        owner_identities = {
            (int(row["world"]), int(row["allocation_generation"]),
             parse_pointer(row["payload"]), int(row["heap"]))
            for row in owner_on["events"]
        }
        self.assertTrue(owner_identities)
        self.assertLessEqual(owner_identities, census_identities,
                             "Owner rows did not join exact source allocation generations")

        expected_phases = [(0, "cold"), (0, "live"), (0, "removed"),
                           (1, "warm"), (1, "live"), (1, "removed")]
        expected_keys = {(world, consumer, cycle, phase)
                         for world in range(2)
                         for consumer in ("jobj", "ground-light")
                         for cycle, phase in expected_phases}
        self.assertEqual(set(snapshots), expected_keys)
        self.assertEqual(set(allocations), expected_keys)
        self.assertEqual(set(guards), expected_keys)
        expected_query_guard_keys = {
            (world, consumer, cycle, phase)
            for world in range(2)
            for consumer in ("jobj", "ground-light")
            for cycle, phase in expected_phases if phase != "cold"
        }
        self.assertEqual(set(query_guards), expected_query_guard_keys)
        guard_fields = ("equal", "source_healthy", "world_equal", "heap_owner",
                        "watermark", "ticks", "free_bytes", "roots", "classes",
                        "pools", "gobj_used", "proc_used")
        for row in list(guards.values()) + list(query_guards.values()):
            for field in guard_fields:
                self.assertEqual(row.get(field), "1", (field, row))

        dumps = parse_c1_heap_dumps(run.stderr)
        self.assertEqual(set(dumps), expected_keys,
                         "Every census snapshot must retain its complete original OSDumpHeap log")
        generation_by_world = {}
        heap_by_world = {}
        descriptor_span_by_world = {}
        identity_payloads = {}
        identity_attributes = {}
        snapshot_metrics = {}

        def validate_linked_rows(rows, label, key):
            self.assertTrue(rows, (label, key, "OSDumpHeap returned no rows"))
            for index, row in enumerate(rows):
                self.assertEqual(row["addr"] % 32, 0, (label, key, row))
                self.assertGreaterEqual(row["size"], 64, (label, key, row))
                self.assertEqual(row["size"] % 32, 0, (label, key, row))
                self.assertEqual(row["end"], row["addr"] + row["size"], (label, key, row))
                self.assertEqual(row["prev"], rows[index - 1]["addr"] if index else 0,
                                 (label, key, index, row))
                self.assertEqual(row["next"], rows[index + 1]["addr"]
                                 if index + 1 < len(rows) else 0,
                                 (label, key, index, row))

        for key in sorted(expected_keys):
            row = snapshots[key]
            self.assertEqual(row.get("overflow"), "0", key)
            self.assertEqual(int(row["rows"]), len(allocations[key]), key)
            generation = int(row["generation"])
            heap = int(row["heap"])
            if key[0] in generation_by_world:
                self.assertEqual(generation, generation_by_world[key[0]], key)
                self.assertEqual(heap, heap_by_world[key[0]], key)
            else:
                generation_by_world[key[0]] = generation
                heap_by_world[key[0]] = heap
            dump = dumps[key]
            allocated_cells, free_cells = dump["allocated"], dump["free"]
            validate_linked_rows(allocated_cells, "allocated", key)
            validate_linked_rows(free_cells, "free", key)
            intervals = sorted((cell["addr"], cell["end"])
                               for cell in allocated_cells + free_cells)
            for previous, current in zip(intervals, intervals[1:]):
                self.assertLessEqual(previous[1], current[0], (key, previous, current))

            dumped_payloads = sorted((cell["addr"] + 32, cell["size"] - 32)
                                     for cell in allocated_cells)
            census_payloads = sorted((parse_pointer(item["payload"]),
                                      int(item["visitor_capacity"]))
                                     for item in allocations[key])
            self.assertEqual(census_payloads, dumped_payloads, key)
            self.assertEqual(len({payload for payload, _ in census_payloads}),
                             len(census_payloads), (key, "duplicate payload"))
            self.assertEqual(int(row["watermark"]) >= 0, True, key)

            live_ids = set()
            for item in allocations[key]:
                self.assertEqual(int(item["lease_status"]), 0, (key, item))
                self.assertEqual(int(item["visitor_capacity"]),
                                 int(item["referent_capacity"]), (key, item))
                self.assertLessEqual(int(item["heap"]), heap, (key, item))
                self.assertEqual(int(item["heap"]), heap, (key, item))
                self.assertEqual(int(item["lease_world"]), generation, (key, item))
                capacity = int(item["visitor_capacity"])
                if int(item["live"]):
                    payload = parse_pointer(item["payload"])
                    allocation_generation = int(item["allocation_generation"])
                    requested = int(item["requested"])
                    self.assertGreater(allocation_generation, 0, (key, item))
                    self.assertLessEqual(allocation_generation, int(row["watermark"]), (key, item))
                    self.assertLessEqual(requested, capacity, (key, item))
                    identity = (key[0], generation, allocation_generation, payload)
                    self.assertNotIn(identity, live_ids, (key, identity))
                    live_ids.add(identity)
                    generation_key = (key[0], generation, allocation_generation)
                    prior_payload = identity_payloads.setdefault(generation_key, payload)
                    self.assertEqual(prior_payload, payload, (key, generation_key))
                    prior_attributes = identity_attributes.setdefault(
                        identity, (requested, capacity))
                    self.assertEqual(prior_attributes, (requested, capacity), (key, identity))
                else:
                    self.assertEqual(int(item["requested"]), 0, (key, item))
                    self.assertEqual(int(item["allocation_generation"]), 0, (key, item))
            free_bytes = sum(cell["size"] - 32 for cell in free_cells)
            allocated_span = sum(cell["size"] for cell in allocated_cells)
            free_span = sum(cell["size"] for cell in free_cells)
            descriptor_span = allocated_span + free_span
            self.assertEqual(free_bytes, int(row["free"]), (key, free_bytes, row["free"]))
            if key[0] in descriptor_span_by_world:
                self.assertEqual(descriptor_span, descriptor_span_by_world[key[0]], key)
            else:
                descriptor_span_by_world[key[0]] = descriptor_span
            snapshot_metrics[key] = {
                "generation": generation, "heap": heap, "free_bytes": free_bytes,
                "allocated_cell_count": len(allocated_cells),
                "free_cell_count": len(free_cells),
                "allocated_cell_span": allocated_span, "free_cell_span": free_span,
                "descriptor_partition_span": descriptor_span,
                "live_exact_lease_count": len(live_ids),
                "unknown_lease_row_count": sum(not int(item["live"])
                                                for item in allocations[key]),
            }

        transitions = []
        cache_observations = []
        for world in range(2):
            for consumer in ("jobj", "ground-light"):
                keys = [(world, consumer, cycle, phase) for cycle, phase in expected_phases]
                rows = [snapshot_metrics[key] for key in keys]
                for before_key, after_key, before, after in zip(keys, keys[1:], rows, rows[1:]):
                    actual = before["free_bytes"] - after["free_bytes"]
                    derived = (after["allocated_cell_span"] - before["allocated_cell_span"] +
                               32 * (after["free_cell_count"] - before["free_cell_count"]))
                    self.assertEqual(actual, derived,
                                     (before_key, after_key, actual, derived))
                    transitions.append({"world": world, "consumer": consumer,
                                        "from": [before_key[2], before_key[3]],
                                        "to": [after_key[2], after_key[3]],
                                        "free_bytes_consumed": actual,
                                        "allocated_cell_span_delta":
                                            after["allocated_cell_span"] - before["allocated_cell_span"],
                                        "free_cell_count_delta":
                                            after["free_cell_count"] - before["free_cell_count"],
                                        "header_bytes_delta":
                                            32 * (after["free_cell_count"] - before["free_cell_count"])})
                cache_observations.append({
                    "world": world, "consumer": consumer,
                    "cold_free_bytes": rows[0]["free_bytes"],
                    "live0_free_bytes": rows[1]["free_bytes"],
                    "removed0_free_bytes": rows[2]["free_bytes"],
                    "warm1_free_bytes": rows[3]["free_bytes"],
                    "live1_free_bytes": rows[4]["free_bytes"],
                    "removed1_free_bytes": rows[5]["free_bytes"],
                    "cold_to_removed0_free_delta":
                        rows[0]["free_bytes"] - rows[2]["free_bytes"],
                    "cold_to_warm1_free_delta":
                        rows[0]["free_bytes"] - rows[3]["free_bytes"],
                })

        query_counts = defaultdict(int)
        prior_object_relations = defaultdict(int)
        for row in queries:
            query_counts[row["kind"]] += 1
            status = int(row["status"])
            live = int(row["live"])
            if row["kind"] in {"unknown", "interior"}:
                self.assertEqual((status, live, int(row["generation"])), (0, 0, 0), row)
                key = c1_record_key(row)
                self.assertEqual(int(row["lease_world"]), int(snapshots[key]["generation"]), row)
            elif row["kind"] == "prior_object_payload":
                relation = row["relation"]
                classification = row["classification"]
                prior_object_relations[(relation, classification)] += 1
                if relation == "after_shutdown":
                    self.assertEqual((status, int(row["refused"])), (1, 1), row)
                    self.assertEqual(classification, "inactive_owner", row)
                elif relation in {"capture", "recheck"}:
                    key = c1_record_key(row)
                    snapshot_generation = int(snapshots[key]["generation"])
                    self.assertEqual(status, 0, row)
                    self.assertEqual(int(row["lease_world"]), snapshot_generation, row)
                if relation == "capture":
                    if classification == "captured_exact_live_sdk_lease":
                        self.assertEqual(live, 1, row)
                        self.assertGreater(int(row["generation"]), 0, row)
                        self.assertEqual(int(row["prior_generation"]),
                                         int(row["generation"]), row)
                    elif classification in {
                            "captured_exact_payload_without_source_lease",
                            "captured_unknown_nonexact_or_unmapped"}:
                        self.assertEqual((live, int(row["generation"]),
                                          int(row["prior_generation"])), (0, 0, 0), row)
                    else:
                        self.fail(f"unclassified object-payload capture: {row}")
                elif relation == "recheck":
                    if classification == "unchanged_live_sdk_lease":
                        self.assertEqual(live, 1, row)
                        self.assertEqual(int(row["generation"]),
                                         int(row["prior_generation"]), row)
                    elif classification == "freed_sdk_lease":
                        self.assertEqual((live, int(row["generation"])), (0, 0), row)
                        self.assertGreater(int(row["prior_generation"]), 0, row)
                    elif classification == "new_generation_reuse":
                        self.assertEqual(live, 1, row)
                        self.assertGreater(int(row["generation"]),
                                           int(row["prior_generation"]), row)
                    elif classification == "unknown_no_exact_lease_baseline":
                        self.assertEqual(int(row["prior_generation"]), 0, row)
                    else:
                        self.fail(f"unclassified prior-object-payload recheck: {row}")
                elif relation != "after_shutdown":
                    self.fail(f"unclassified prior-object-payload relation: {row}")
            elif row["kind"] in {"null_payload", "null_output"}:
                self.assertEqual((status, int(row["refused"])), (3, 1), row)
            elif row["kind"] == "foreign_owner":
                self.assertEqual((status, int(row["refused"])), (-1, 1), row)
            else:
                self.fail(f"unclassified allocation query: {row}")
        self.assertEqual(query_counts["unknown"], 24)
        self.assertEqual(query_counts["interior"], 24)
        self.assertEqual(query_counts["prior_object_payload"], 26)
        self.assertEqual(prior_object_relations[("capture", "captured_exact_live_sdk_lease")] +
                         prior_object_relations[("capture", "captured_exact_payload_without_source_lease")] +
                         prior_object_relations[("capture", "captured_unknown_nonexact_or_unmapped")], 8)
        self.assertEqual(sum(count for (relation, _), count in prior_object_relations.items()
                            if relation == "recheck"), 16)
        self.assertEqual(prior_object_relations[("after_shutdown", "inactive_owner")], 2)
        self.assertEqual(query_counts["null_payload"], 4)
        self.assertEqual(query_counts["null_output"], 4)
        self.assertEqual(query_counts["foreign_owner"], 4)

        accounting = {
            "schema": "issue251-asset-free-heap-census-accounting-v1",
            "scope": "Two-world synthetic C1 JObj/Ground-light cold/live/removed/warm controls only; no Stadium asset or original OnInit",
            "source_heap_header_bytes": 32,
            "census_row_capacity": 4096,
            "census_rows_total": sum(len(rows) for rows in allocations.values()),
            "snapshot_count": len(snapshots), "osdumpheap_logs_emitted": len(dumps),
            "osdumpheap_logger_policy": "Unchanged process policy; every allocated/free section was observed in captured INFO output",
            "descriptor_partition_span_by_world": descriptor_span_by_world,
            "snapshots": [{"world": key[0], "consumer": key[1], "cycle": key[2],
                           "phase": key[3], **snapshot_metrics[key]} for key in sorted(snapshot_metrics)],
            "transitions": transitions,
            "cache_observations": cache_observations,
            "query_counts": dict(query_counts),
            "query_guards": len(query_guards), "snapshot_guards": len(guards),
            "excluded_claims": ["88576-byte original heap gap explained",
                                "source allocation attributed to cache ownership",
                                "stage admitted", "whole-session acceptance"],
        }
        (self.scratch / "cache-live-allocation-census-accounting.json").write_text(
            json.dumps(accounting, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def test_stadium_map_light_asset_free_adoption_controls(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed C1 diagnostic map-light adoption control first")
        command = [str(node_runtime()), str(target), "--stadium-map-light-adoption-controls"]
        (self.scratch / "map-light-adoption-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            for stream in ("stdout", "stderr"):
                value = getattr(failure, stream)
                (self.scratch / ("map-light-adoption." + stream)).write_bytes(
                    value.encode() if isinstance(value, str) else (value or b""))
            raise
        (self.scratch / "map-light-adoption.stdout").write_text(run.stdout, encoding="utf-8")
        (self.scratch / "map-light-adoption.stderr").write_text(run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-5000:])
        self.assertIn("C1 asset-free original Ground map-light creation/adoption", run.stdout)
        self.assertIn("two retire-before-detach cycles and foreign/replaced/bound refusals passed", run.stdout)
        self.assertIn("no camera, scheduled proc dispatch or source ticks", run.stdout)

    def test_stadium_sis_allocator_asset_free_lifecycle_controls(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed C1 diagnostic SIS allocator control first")
        command = [str(node_runtime()), str(target), "--stadium-sis-allocator-controls"]
        (self.scratch / "sis-allocator-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            for stream in ("stdout", "stderr"):
                value = getattr(failure, stream)
                (self.scratch / ("sis-allocator." + stream)).write_bytes(
                    value.encode() if isinstance(value, str) else (value or b""))
            raise
        (self.scratch / "sis-allocator.stdout").write_text(run.stdout, encoding="utf-8")
        (self.scratch / "sis-allocator.stderr").write_text(run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-5000:])
        self.assertIn("C1 asset-free SIS retired-menu baseline, two owned allocator lifetimes", run.stdout)
        self.assertIn("genuine-new-owner and later-epoch refusals passed", run.stdout)
        self.assertRegex(run.stdout, r"actual post-restart retired SIS address reuse=[01]")
        self.assertIn("foreign-root/lease refusals and single drain passed", run.stdout)
        self.assertIn("no camera, scheduled procs or source ticks", run.stdout)

    def test_stadium_on_init_asset_free_refusal_controls(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The reviewed C1 diagnostic native trace target is not built")
        run = subprocess.run(
            [str(node_runtime()), str(target), "--stadium-on-init-controls"],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-5000:])
        self.assertIn("Diagnostic Stadium profile/content gate", run.stdout)
        self.assertIn(
            "C1 source OnInit refusal and synthetic event-journal controls passed; no Stadium stage initialization invoked",
            run.stdout,
        )
        self.assertIn(
            "C1 asset-free original effect prepare/efLib_Init/complete/end passed; no stage callbacks, proc dispatch, or ticks",
            run.stdout,
        )
        self.assertIn(
            "C1 asset-free original HSD_Randi and immutable selection/live-owner phase controls passed; two lifetimes and every compared field refused",
            run.stdout,
        )
        self.assertNotIn("OnInit lifetime", run.stdout)

    def test_stadium_source_oninit_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_ONINIT") != "1":
            self.skipTest(
                "The single source-ordered OnInit experiment requires its explicit run gate"
            )
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        if not fixture_value:
            self.fail("MELEE_MENU_FIXTURE_ROOT is required for the retained OnInit packet")
        fixture_root = Path(fixture_value)
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        self.assertTrue(target.is_file(), f"Build the reviewed diagnostic target first: {target}")
        self.assertTrue(menu.is_dir(), f"Missing owned menu fixture root: {menu}")
        self.assertTrue(game.is_dir(), f"Missing owned game fixture root: {game}")

        script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        self.assertEqual(len(menu_names), 76)
        self.assertEqual(len(selected_names), 36)
        required = sorted(set(menu_names) | set(selected_names))
        self.assertEqual(len(required), 98)
        missing = [name for name in required
                   if not (menu / name).is_file() and not (game / name).is_file()]
        self.assertFalse(
            missing,
            "Frozen C1 OnInit RuntimeFiles are incomplete before launch: " +
            ", ".join(missing),
        )

        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-source-oninit.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source_revision, "stadium-source-oninit-v1"]
        (self.scratch / "stadium-source-oninit-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        (self.scratch / "stadium-source-oninit-fixture-preflight.json").write_text(
            json.dumps({"menu_names": menu_names,
                        "selected_names": selected_names,
                        "missing": missing}, indent=2) + "\n",
            encoding="utf-8",
        )
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "stadium-source-oninit.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "stadium-source-oninit.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "stadium-source-oninit.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "stadium-source-oninit.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-9000:])
        census = parse_c1_v23_heap_census(run.stderr)
        self.assertEqual(census["phase_count"], 4)
        self.assertIn(
            "one source-ordered Stadium OnInit lifetime passed",
            run.stdout,
        )
        result = next(json.loads(line) for line in run.stdout.splitlines()
                      if line.startswith('{"probe":"stadium-source-oninit"'))
        self.assertEqual(result["source_size_name"], "/GrPs.usd")
        self.assertEqual(result["typed_open_name"], "/GrPs.usd")
        self.assertEqual(result["authored_map_sequence"], [0, 1, 2, 5])
        self.assertTrue(result["runtime_map_call_order_observed"])
        source_events = result["runtime_source_events"]
        self.assertEqual(
            [event["kind"] for event in source_events],
            ["stage_e8", "stage_24c", "ground_0800", "stadium_on_init"] +
            ["map_gobj"] * 4,
        )
        self.assertEqual([event["map_id"] for event in source_events[:4]],
                         [None] * 4)
        self.assertEqual([event["map_id"] for event in source_events[4:]],
                         [0, 1, 2, 5])
        self.assertTrue(all(event["gobj"] is None
                            for event in source_events[:4]))
        self.assertTrue(all(isinstance(event["gobj"], int) and
                            event["gobj"] > 0
                            for event in source_events[4:]))
        self.assertTrue(result["map_slots_match_owner_record"])
        self.assertGreater(result["map2_buffer_pointer"], 0)
        self.assertIn(result["map2_buffer_origin"],
                      ("borrowed_preload", "owned_fallback"))
        self.assertEqual(result["ground_storage_requested_bytes"], 64)
        self.assertTrue(result["ground_storage_retired"])
        self.assertTrue(result["ft_device_bytes_restored"])
        self.assertEqual(result["source_tick_delta"], 0)
        self.assertTrue(result["map2_scheduled_proc_dispatch_absent"])
        self.assertFalse(result["camera_called"])
        self.assertFalse(result["onstart_called"])
        self.assertFalse(result["rendered"])
        self.assertTrue(result["ordinary_admission_closed"])
        self.assertTrue(result["checked_teardown"])
        self.assertEqual(result["source_seed_after_cleanup"],
                         result["source_seed_after_oninit"])
        if result["map2_buffer_origin"] == "owned_fallback":
            self.assertEqual(result["map2_requested_bytes"], 0x50000)
            self.assertGreater(result["map2_allocation_generation"], 0)
            self.assertTrue(result["map2_fallback_retired"])
            self.assertFalse(result["borrowed_preload_preserved"])
        else:
            self.assertTrue(result["borrowed_preload_preserved"])
            self.assertFalse(result["map2_fallback_retired"])

        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        self.assertEqual(rows[0]["record"], "header")
        self.assertEqual(rows[0]["input_recipe"], "stadium-source-oninit-v1")
        leases = [row for row in rows if row.get("record") == "sis_lease"]
        self.assertEqual([row["boundary"] for row in leases], [
            "captured_before_menu_leave", "verified_retired_before_world_shutdown", "before_begin",
        ])
        captured, retired, reopened = leases
        self.assertTrue(captured["source_active"])
        self.assertTrue(captured["current_live"])
        self.assertFalse(captured["retirement_verified"])
        self.assertFalse(retired["source_active"])
        self.assertFalse(retired["current_live"])
        self.assertTrue(retired["retirement_verified"])
        self.assertEqual(retired["current_world"], captured["prior_world"])
        self.assertEqual(retired["source_epoch"], captured["prior_source_epoch"] + 1)
        self.assertEqual(reopened["prior_world"], captured["prior_world"])
        self.assertEqual(reopened["prior_allocation"], captured["prior_allocation"])
        self.assertFalse(reopened["source_active"])
        self.assertEqual(reopened["source_epoch"], retired["source_epoch"])
        self.assertGreater(reopened["current_world"], reopened["prior_world"])
        if reopened["current_live"]:
            self.assertNotEqual((reopened["current_world"], reopened["current_allocation"]),
                                (reopened["prior_world"], reopened["prior_allocation"]))
        events = [row for row in rows if row.get("record") == "event"]
        self.assertEqual([row["event"] for row in events], [
            "stadium_source_oninit_returned",
            "stadium_source_oninit_cleaned",
        ])
        self.assertEqual(events[0]["rng"], result["source_seed_after_oninit"])
        self.assertEqual(events[1]["rng"], result["source_seed_after_cleanup"])

    def test_owned_css_scene_lifecycle(self):
        targets = [ROOT / "build" / name / "native_css_callbacks.js"
                   for name in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        menu = ROOT / "assets-local/native-menus"
        audio = ROOT / "assets-local/next-gate"
        required = [menu / name for name in (
            "MnSlChr.usd", "SdSlChr.usd", "MnExtAll.usd", "LbMcGame.usd",
            "NtMemAc.usd", "menu01.hps", "menu3.hps", "nr_select.ssm", "nr_title.ssm",
            "nr_name.ssm", "pokemon.ssm", "end.ssm")] + [audio / name for name in (
            "smash2.sem", "main.ssm", "mario.ssm", "dsp_coef.bin", "sislib_font.bin")]
        if not targets or not all(path.is_file() for path in required):
            self.skipTest("Original CSS target and owned local fixtures are required")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target), str(menu), str(audio)],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original CSS enter, 120 neutral input/scheduler/audio ticks and exit passed in two worlds", run.stdout)

    def test_owned_original_menu_match_loop(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not (game / "PlMr.dat").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu/game fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage_kind in (32, 31):
            with self.subTest(stage_kind=stage_kind):
                with tempfile.TemporaryDirectory(prefix="menu transition trace ") as directory:
                    trace = Path(directory) / "port.jsonl"
                    run = subprocess.run(
                        [str(node_runtime()), str(target), str(menu), str(game),
                         str(stage_kind), str(trace), source_revision], cwd=ROOT,
                        capture_output=True, text=True, timeout=120)
                    self.assertEqual(run.returncode, 0,
                                     (run.stdout + run.stderr)[-4000:])
                    rows = [json.loads(line) for line in trace.read_text().splitlines()]
                    for trace_run in (0, 1):
                        _, events = transition_compare.select_run(rows, "port", trace_run)
                        transition_compare.validate_continuity("port", events)
                self.assertIn(
                    "Native original CSS Mario/Falco to SSS to four-stock match to CSS passed twice",
                    run.stdout)

    def test_link_audio_registry_css_unload(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu = game = ROOT / "assets-local/issue34"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")) or not all(
                (game / name).is_file() for name in ("link.ssm", "clink.ssm")):
            self.skipTest("Build the native menu host and supply owned Link audio fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="link css unload trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "link-css-unload-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original CSS Link audio registry entered, aborted and unloaded", run.stdout)
        self.assertIn("Original CSS Young Link audio registry entered, aborted and unloaded", run.stdout)

    def test_title_and_main_checked_abort_teardown(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="title main abort trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "title-main-abort-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn("Original Title Eject released source ownership and allowed CSS re-entry", run.stdout)
        self.assertIn("Original Main Eject released source ownership and allowed CSS re-entry", run.stdout)
        self.assertIn(
            "Original Opening VS handoff selected four CPUs, suspended with retained PAD input, and cleaned up",
            run.stdout,
        )
        self.assertIn("Original all-unlocked CSS roster, P1/P2 Title Start edges, unsupported Challenger, Title timeout to Opening state 1 and recovery passed", run.stdout)
        self.assertIn("Normal CSS->SSS leave cleared its consumed transition before host teardown", run.stdout)
        self.assertIn("Native Title/Main checked abort and CSS re-entry smoke passed", run.stdout)

    def test_original_main_settings_sound_mix_route(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not targets or not (menu / "MnMaAll.usd").is_file():
            self.skipTest("Build the native menu host and supply owned Main-menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="main settings sound trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "main-settings-sound-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original Main Settings Sound changed SaveData mix to -5, retained through Title/Main, returned, re-entered, and cleaned up",
            run.stdout,
        )

    def test_opening_movie_entry_requires_source_heap_owner(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-public-release",
                                "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        fixture_root = Path(os.environ.get("MELEE_MENU_FIXTURE_ROOT", ROOT / "assets-local"))
        if not fixture_root.is_absolute():
            fixture_root = ROOT / fixture_root
        menu, game = fixture_root / "native-menus", fixture_root / "next-gate"
        if not has_menu_trophy_assets(menu, game) or not targets or not (menu / "MnSlChr.usd").is_file() or not all(
                (menu / name).is_file() for name in ("menu01.hps", "menu3.hps")):
            self.skipTest("Build the native menu host and supply owned menu fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="opening movie preload trace ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "opening-movie-preload-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original Opening movie route selected state 0 and rejected missing source heap/cache ownership explicitly",
            run.stdout,
        )
        self.assertIn("no movie decode or retail-route claim", run.stdout)

    def test_trophy_baseline_waits_for_original_tydati_owner(self):
        targets = [ROOT / "build" / name / "native_menu_host_trace.js"
                   for name in ("browser", "browser-release", "browser-audio-preview-release")]
        targets = [path for path in targets if path.is_file()]
        menu, game = ROOT / "assets-local/native-menus", ROOT / "assets-local/next-gate"
        if not targets or not (menu / "MnSlChr.usd").is_file() or not has_menu_trophy_assets(menu, game):
            self.skipTest("Build the native menu host and supply owned TyDatai fixtures")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        source_revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        with tempfile.TemporaryDirectory(prefix="TyDatai profile baseline ") as directory:
            trace = Path(directory) / "port.jsonl"
            run = subprocess.run(
                [str(node_runtime()), str(target), str(menu), str(game), "32",
                 str(trace), source_revision, "trophy-baseline-v1"],
                cwd=ROOT, capture_output=True, text=True, timeout=120)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original TyDatai-backed save baseline initialized after source-file ownership",
            run.stdout)

    def test_original_sis_layout_and_style_stack(self):
        candidates = [ROOT / "build" / directory / "native_menu_scene_trace.js"
                      for directory in ("browser", "browser-release")]
        targets = [p for p in candidates if p.is_file()]
        if not targets:
            self.skipTest("Build fighter targets before the original SIS consumer check")
        target = max(targets, key=lambda p: p.stat().st_mtime)
        run = subprocess.run([str(node_runtime()), str(target)], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[:4000])
        self.assertIn("Original SIS big-endian layout and style-stack trace passed", run.stdout)
        for argument in ("--bad-image-index", "--bad-palette-index"):
            with self.subTest(argument=argument):
                rejected = subprocess.run([str(node_runtime()), str(target), argument],
                                          cwd=ROOT, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertIn("Native texture animation index exceeds its owned table",
                              rejected.stdout + rejected.stderr)

if __name__ == "__main__": unittest.main()
