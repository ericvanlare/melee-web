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


def parse_pad_snapshot_wire(raw):
    import math
    import struct

    if len(raw) != 822:
        raise AssertionError(f"PAD wire length was {len(raw)}, expected 822")

    def u32(offset):
        return struct.unpack_from(">I", raw, offset)[0]

    def i32(offset):
        return struct.unpack_from(">i", raw, offset)[0]

    def i8(offset):
        return struct.unpack_from("b", raw, offset)[0]

    config = {
        "repeat_start": i32(0), "repeat_interval": i32(4),
        "adc_type": i8(8), "adc_th": i8(9), "adc_angle_bits": u32(10),
        "clamp_stick_type": raw[14], "clamp_stick_shift": raw[15],
        "clamp_stick_max": i8(16), "clamp_stick_min": i8(17),
        "clamp_lr": (raw[18], raw[19], raw[20]),
        "clamp_ab": (raw[21], raw[22], raw[23]),
        "scale": (i8(24), raw[25], raw[26]),
        "cross_dir": raw[27], "reset": (raw[28], raw[29]),
    }
    adc_angle = struct.unpack(">f", config["adc_angle_bits"].to_bytes(4, "big"))[0]
    invalid = []

    def require(condition, field):
        if not condition:
            invalid.append(field)

    require(config["repeat_start"] > 0, f"repeat_start={config['repeat_start']}")
    require(config["repeat_interval"] > 0,
            f"repeat_interval={config['repeat_interval']}")
    require(0 <= config["adc_type"] <= 3, f"adc_type={config['adc_type']}")
    require(config["adc_th"] >= 0, f"adc_th={config['adc_th']}")
    require(math.isfinite(adc_angle), f"adc_angle_bits={config['adc_angle_bits']:08x}")
    require(config["clamp_stick_type"] <= 1,
            f"clamp_stick_type={config['clamp_stick_type']}")
    require(config["clamp_stick_shift"] <= 1,
            f"clamp_stick_shift={config['clamp_stick_shift']}")
    require(config["clamp_stick_min"] >= 0,
            f"clamp_stick_min={config['clamp_stick_min']}")
    require(config["clamp_stick_max"] > config["clamp_stick_min"],
            f"clamp_stick_max={config['clamp_stick_max']}<=clamp_stick_min={config['clamp_stick_min']}")
    require(config["clamp_lr"][0] <= 1, f"clamp_lr_shift={config['clamp_lr'][0]}")
    require(config["clamp_lr"][1] > config["clamp_lr"][2],
            f"clamp_lr_max={config['clamp_lr'][1]}<=clamp_lr_min={config['clamp_lr'][2]}")
    require(config["clamp_ab"][0] <= 1, f"clamp_ab_shift={config['clamp_ab'][0]}")
    require(config["clamp_ab"][1] > config["clamp_ab"][2],
            f"clamp_ab_max={config['clamp_ab'][1]}<=clamp_ab_min={config['clamp_ab'][2]}")
    require(config["scale"][0] > 0, f"scale_stick={config['scale'][0]}")
    require(config["scale"][1] > 0, f"scale_analog_lr={config['scale'][1]}")
    require(config["scale"][2] > 0, f"scale_analog_ab={config['scale'][2]}")
    require(config["cross_dir"] <= 3, f"cross_dir={config['cross_dir']}")
    require(config["reset"][0] <= 1,
            f"reset_switch_status={config['reset'][0]}")
    require(config["reset"][1] <= 1, f"reset_switch={config['reset'][1]}")
    config_invalid = list(invalid)

    histories = []
    float_names = ("nml_stickX", "nml_stickY", "nml_subStickX", "nml_subStickY",
                   "nml_analogL", "nml_analogR", "nml_analogA", "nml_analogB")
    for bank in range(3):
        for slot in range(4):
            base = 30 + (bank * 4 + slot) * 66
            bits = [u32(base + 32 + index * 4) for index in range(8)]
            finite_count = 0
            for name, value_bits in zip(float_names, bits):
                value = struct.unpack(">f", value_bits.to_bytes(4, "big"))[0]
                if math.isfinite(value):
                    finite_count += 1
                else:
                    invalid.append(
                        f"history[{bank}][{slot}].{name}=0x{value_bits:08x}")
            histories.append({
                "bank": bank, "slot": slot,
                "buttons": [u32(base + offset) for offset in (0, 4, 8, 12, 16)],
                "repeat_count": i32(base + 20),
                "sticks": [i8(base + offset) for offset in (24, 25, 26, 27)],
                "analog": list(raw[base + 28:base + 32]),
                "normalized_bits": bits, "finite_count": finite_count,
                "cross_dir": raw[base + 64], "err": i8(base + 65),
            })
    return config, histories, config_invalid, invalid


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

    def test_stadium_ready_rules_and_callback_owner_controls(self):
        """Actual adapter predicates with explicit synthetic service witnesses.

        This does not run Stage, Ready animation, audio or source construction.
        """
        import shutil
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler or not (ROOT / "build/gameplay-source/src/melee/gm/types.h").is_file():
            self.skipTest("Prepared source headers and C compiler are required")
        rules = r"""
#define main ordinary_rules_main
#define melee_web_match_prepare_source fixture_ordinary_prepare_source
#include "gameplay_match_item_mask_trace.c"
#undef main
#undef melee_web_match_prepare_source
#include "gameplay_source_memory_runtime.h"
#include <sysdolphin/baselib/random.h>
static unsigned fixture_seed=17; u32* seed_ptr=&fixture_seed;
static int prepare_failure,fixture_status,fixture_healthy=1;
static uint64_t fixture_world=1;
static const StartMeleeData* prepared_pointer;
int melee_web_source_memory_healthy(void){return fixture_healthy;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* c)
{*c=(MeleeWebSourceMemoryContext){.source_heap_handle=7,.world_generation=fixture_world};return (MeleeWebSourceMemoryReadStatus)fixture_status;}
int melee_web_match_prepare_source(StartMeleeData* start,int demo)
{
 if(start->rules.stkind!=St_Kind_PStadium)return fixture_ordinary_prepare_source(start,demo);
 ++g_prepare_calls;prepared_pointer=start;
 if(!melee_web_match_rules_stadium_source_start(start))return 0;
 StartMeleeData copy=*start;
 if(melee_web_match_rules_stadium_source_start(&copy))return 0;
 u32* saved=seed_ptr;u32 foreign=17;seed_ptr=&foreign;
 int admitted=melee_web_match_rules_stadium_source_start(start);seed_ptr=saved;
 if(admitted)return 0;
 ++g_stats.generation;admitted=melee_web_match_rules_stadium_source_start(start);--g_stats.generation;
 if(admitted)return 0;
 return !prepare_failure;
}
static int diagnostic_route(int variant,int expect,int fail_source)
{
 StartMeleeData start;const int teams[]={0,0,0,0};char error[256];
 set_supported_vs_payload(&start,-1,UINT64_MAX,0,teams,2);
 start.rules.stkind=St_Kind_PStadium;
 switch(variant){
 case 1:start.rules.stkind=St_Kind_Last;break;
 case 2:start.players[0].ckind=FTKIND_MARIO;break;
 case 3:start.players[1].slot_type=Gm_PKind_Cpu;break;
 case 4:start.players[2]=start.players[1];break;
 case 5:start.rules.is_teams=1;break;
 case 6:start.rules.x6=1;break;
 case 7:start.players[1].slot_type=Gm_PKind_NA;break;
 case 8:fixture_world=2;break;
 case 9:seed_ptr=NULL;break;
 case 10:fixture_status=MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE;break;
 case 11:fixture_healthy=0;break;
 }
 MeleeWebMatchRules* owner=melee_web_match_rules_begin(error,sizeof(error));
 if(!owner)return 0;
 g_prepare_calls=0;prepared_pointer=NULL;prepare_failure=fail_source;
 int result=melee_web_match_rules_prepare_stadium_from_menu(owner,&start,error,sizeof(error));
 fixture_world=1;seed_ptr=&fixture_seed;fixture_status=0;fixture_healthy=1;
 if(result!=expect || g_prepare_calls!=(unsigned)(variant==0) ||
    melee_web_match_rules_stadium_source_start(&start) ||
    (prepared_pointer && melee_web_match_rules_stadium_source_start(prepared_pointer)))return 0;
 if(!melee_web_match_rules_end(owner,error,sizeof(error)))return 0;
 return !melee_web_match_rules_stadium_source_start(&start);
}
int main(void)
{
 if(CKIND_MARIO!=8 || FTKIND_MARIO!=0 || ordinary_rules_main())return 1;
 if(!diagnostic_route(0,1,0) || !diagnostic_route(0,0,1))return 2;
 for(int i=1;i<=11;++i)if(!diagnostic_route(i,0,0))return 3;
 puts("Stadium rules: actual CK8 positive; FT0/CPU/third/team/SD/ordinary-stage refusal; pointer/RNG/world/failure reset");return 0;
}
"""
        callbacks = r"""
#include "gameplay_hud.c"
#define owner fixture_match_owner
#define fail fixture_match_fail
#define ok fixture_match_ok
#include "gameplay_match_context.c"
#undef owner
#undef fail
#undef ok
#include <sysdolphin/baselib/gobjproc.h>
static uint64_t fixture_generation=1;
MeleeWebGameplayStats melee_web_gameplay_stats(void){return (MeleeWebGameplayStats){.generation=fixture_generation};}
uint64_t melee_web_gameplay_generation(void){return fixture_generation;}
void fn_8016B7F8(void){}
void if_802F73C4(HSD_GObj* p){(void)p;}
HSD_GObj* HSD_GObj_804D781C;HSD_GObj* HSD_GObj_804D7814;
HSD_GObjProc* HSD_GObj_804D7838;
Element_803F9628 ifStatus_803F9628[8];
CmSubject *cm_804D6458,*cm_804D645C,*cm_804D6460,*cm_804D6468;
u32* seed_ptr;
int melee_web_source_memory_healthy(void){return 1;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* c)
{*c=(MeleeWebSourceMemoryContext){.source_heap_handle=7,.world_generation=1};return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(const void* p,MeleeWebSourceMemoryAllocation* a)
{if(p!=fixture_match_owner->pool)return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;*a=fixture_match_owner->camera_lease;return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
int main(void)
{
 char error[256];HSD_GObj object={0},foreign={0};HSD_GObjProc proc={0},other={0};
 MeleeWebHud hud={.generation=1,.ready_object=&object,.ready_proc=&proc};owner=&hud;
 object.proc=&proc;proc.gobj=&object;proc.on_invoke=if_802F73C4;
 ifStatus_803F9628[3]=(Element_803F9628){.x0=&object,.x8=if_802F73C4,.x1C=intro_finished};
 HSD_GObj_804D781C=&object;HSD_GObj_804D7838=&proc;
 CmSubject pool[3]={0};MeleeWebMatchContext match={.generation=1,.camera_count=3,.pool=pool,.seed=17};
 fixture_match_owner=&match;seed_ptr=&match.seed;cm_804D645C=pool;
 match.camera_lease=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.world_generation=1,.allocation_generation=9,.requested_bytes=sizeof(pool),.live=1};
 cm_804D6460=cm_804D6468=&pool[0];cm_804D6458=&pool[2];pool[2].prev=&pool[1];
 if(!melee_web_hud_stadium_ready_context(&object,&proc) ||
    !melee_web_match_camera_available_ready(&match,error,sizeof(error)) ||
    !melee_web_match_camera_subject_preflight_ready(&match,&pool[0],error,sizeof(error)) ||
    melee_web_match_camera_available(&match,error,sizeof(error)) ||
    melee_web_match_camera_subject_preflight(&match,&pool[0],error,sizeof(error)))return 1;
 for(int kind=0;kind<7;++kind){
  Element_803F9628 saved=ifStatus_803F9628[3];
  if(kind==0)ifStatus_803F9628[3].x0=&foreign;
  if(kind==1)ifStatus_803F9628[3].x8=NULL;
  if(kind==2)ifStatus_803F9628[3].x1C=NULL;
  if(kind==3)HSD_GObj_804D7838=&other;
  if(kind==4)HSD_GObj_804D781C=&foreign;
  if(kind==5)HSD_GObj_804D7814=&object;
  if(kind==6)fixture_generation=2;
  if(melee_web_hud_stadium_ready_context(NULL,NULL) ||
     melee_web_match_camera_available_ready(&match,error,sizeof(error)) ||
     melee_web_match_camera_subject_preflight_ready(&match,&pool[0],error,sizeof(error)))return 2;
  ifStatus_803F9628[3]=saved;HSD_GObj_804D7838=&proc;HSD_GObj_804D781C=&object;HSD_GObj_804D7814=NULL;fixture_generation=1;
 }
 CmSubject copy[3];memcpy(copy,pool,sizeof(pool));
 pool[2].prev=&pool[2];
 if(melee_web_match_camera_available_ready(&match,error,sizeof(error)))return 3;
 pool[2].prev=&pool[1];
 if(memcmp(copy,pool,sizeof(pool)) || cm_804D6460!=&pool[0] || cm_804D6468!=&pool[0] ||
    melee_web_match_camera_subject_preflight_ready(&match,&foreign,error,sizeof(error)))return 4;
 HSD_GObj_804D781C=NULL;HSD_GObj_804D7838=NULL;
 if(!melee_web_match_camera_available(&match,error,sizeof(error)) ||
    !melee_web_match_camera_subject_preflight(&match,&pool[0],error,sizeof(error)) ||
    melee_web_match_camera_available_ready(&match,error,sizeof(error)))return 5;
 puts("Stadium Ready HUD/camera: exact object/proc/row3 positive; foreign/cycle/GX/world refused without mutation; idle retirement unchanged");return 0;
}
"""
        phase = r"""
/* ACTUAL_STAGE_PHASE_SOURCE */
static uint64_t fixture_generation=1;
uint64_t melee_web_gameplay_generation(void){return fixture_generation;}
int main(void)
{
 const MeleeWebStageProfile profile={.diagnostic_only=1,.stage_kind=St_Kind_PStadium};
 MeleeWebStageLast h={.generation=1,.definition=&profile,.source_ordered=1,
                     .stadium_ready_route=1,.stadium_started=1,.stadium_ready_armed=1};
 active=&h;
 if(!stadium_ready_phase(&h,St_Kind_PStadium,1) || stadium_ready_phase(&h,St_Kind_Last,1) ||
    stadium_ready_phase(&h,St_Kind_PStadium,2))return 1;
 h.stadium_ready_armed=2;
 if(!stadium_ready_phase(&h,St_Kind_PStadium,2) || stadium_ready_phase(&h,St_Kind_PStadium,1))return 2;
 h.stadium_started=2;h.stadium_ready_armed=0;
 MeleeWebStageLast copy=h;
 if(stadium_ready_phase(&h,St_Kind_PStadium,1) || stadium_ready_phase(&h,St_Kind_PStadium,2) ||
    memcmp(&h,&copy,sizeof(h)))return 3;
 h.stadium_started=1;h.stadium_ready_armed=1;fixture_generation=2;
 if(stadium_ready_phase(&h,St_Kind_PStadium,1))return 4;
 fixture_generation=1;active=NULL;
 if(stadium_ready_phase(&h,St_Kind_PStadium,1))return 5;
 puts("Stadium Ready actual phase predicate: before/after once; duplicate/foreign-kind/world/owner refused without mutation");return 0;
}
"""
        # Compile the actual pure owner-phase predicate and actual owner type,
        # excluding unrelated functions whose historical warnings differ from
        # this reducer's -Werror profile. This is a source-fragment control.
        def balanced_body(text, start):
            opening = text.index("{", start)
            depth = 1
            cursor = opening + 1
            while depth:
                depth += (text[cursor] == "{") - (text[cursor] == "}")
                cursor += 1
            return text[start:cursor]
        stage_source = (ROOT / "src/gameplay_stage_last.c").read_text()
        owner_struct = balanced_body(stage_source, stage_source.index("struct MeleeWebStageLast {")) + ";"
        helper = balanced_body(stage_source, stage_source.index("static int stadium_ready_phase("))
        prefix = stage_source[:stage_source.index("struct MeleeWebStageLast {")]
        # The owner type needs only the authored array bound. Preserve its
        # initializer in sizeof without emitting an unused callback table.
        table_start = prefix.index("static const HSD_GObjEvent stadium_pending_callbacks[]=")
        table_end = prefix.index(";", table_start) + 1
        table = prefix[table_start:table_end]
        initializer = table.split("=", 1)[1].removesuffix(";")
        prefix = prefix[:table_start] + (
            "extern const HSD_GObjEvent stadium_pending_callbacks[sizeof((HSD_GObjEvent[])" +
            initializer + ")/sizeof(HSD_GObjEvent)];") + prefix[table_end:]
        phase = phase.replace("/* ACTUAL_STAGE_PHASE_SOURCE */",
                              prefix + owner_struct + "\nstatic MeleeWebStageLast* active;\n" + helper)
        import re
        caller_source = (ROOT / "tests/native_menu_host_trace.cpp").read_text()
        begin = caller_source.index(" if(input_recipe&&!css_observer_recipe")
        end = caller_source.index("throw std::runtime_error(\"Unknown transition input recipe\")", begin)
        predicate = caller_source[begin:end].strip()
        flags = sorted(set(re.findall(r"\b\w+_recipe\b", predicate)) - {"input_recipe"})
        admission = "int main(void){const char* input_recipe=\"stadium-source-ready-session-v1\";\n"
        admission += "\n".join("int " + flag + "=0;" for flag in flags)
        admission += "\nstadium_ready_session_recipe=1;\n" + predicate + " return 1;\n"
        admission += "stadium_ready_session_recipe=0;int rejected=0;\n" + predicate + " rejected=1;\n"
        admission += "return !rejected;}\n"
        for name, source in (("rules", rules), ("callbacks", callbacks), ("phase", phase), ("admission", admission)):
            input_path = self.scratch / ("stadium-ready-" + name + ".c")
            output = self.scratch / ("stadium-ready-" + name)
            input_path.write_text(source)
            command = [compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-variable", "-DAURORA", "-DTARGET_PC",
                       "-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC=1", "-O1",
                       "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                       "-include", str(ROOT / "src/gameplay_compat.h")]
            for directory in ("src", "tests", "build/gameplay-source/src",
                              ".deps/aurora/include", ".deps/melee/extern/dolphin/include"):
                command += ["-I", str(ROOT / directory)]
            command += [str(input_path)]
            if name == "rules":
                command += [str(ROOT / "src/gameplay_match_rules.c")]
            command += ["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                        "-o", str(output)]
            (self.scratch / ("stadium-ready-" + name + "-command.json")).write_text(json.dumps(command))
            build = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
            (self.scratch / ("stadium-ready-" + name + "-build.log")).write_text(build.stdout + build.stderr)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(output)], cwd=ROOT, capture_output=True, text=True, timeout=10)
            (self.scratch / ("stadium-ready-" + name + "-run.log")).write_text(run.stdout + run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_vs_sudden_death_source_callbacks_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        command = [str(node_runtime()), str(target),
                   "--vs-sudden-death-source-control"]
        (self.scratch / "sudden-death-source-command.txt").write_text(
            " ".join(command) + "\n", encoding="utf-8")
        try:
            run = subprocess.run(
                command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired as failure:
            (self.scratch / "sudden-death-source.stdout").write_bytes(
                failure.stdout.encode() if isinstance(failure.stdout, str)
                else (failure.stdout or b""))
            (self.scratch / "sudden-death-source.stderr").write_bytes(
                failure.stderr.encode() if isinstance(failure.stderr, str)
                else (failure.stderr or b""))
            raise
        (self.scratch / "sudden-death-source.stdout").write_text(
            run.stdout, encoding="utf-8")
        (self.scratch / "sudden-death-source.stderr").write_text(
            run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-4000:])
        self.assertIn(
            "Original VS timeout/tie and Sudden Death-to-Results source callbacks passed",
            run.stdout,
        )

    def test_sudden_death_world_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-world-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_ordinary_nontied_timeout_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-ordinary-timeout-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"ordinary-timeout-{stage}.jsonl"),
                    revision, "ordinary-nontied-timeout-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"ordinary-timeout-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"ordinary-timeout-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sparse_source_pad_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sparse-source-pad-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"sparse-source-pad-{stage}.jsonl"),
                    revision, "sparse-source-pad-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"sparse-source-pad-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"sparse-source-pad-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sparse_actual_css_sss_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sparse-actual-css-sss-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"sparse-actual-css-sss-{stage}.jsonl"),
                    revision, "sparse-actual-css-sss-control-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"sparse-actual-css-sss-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"sparse-actual-css-sss-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sparse_css_observer_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sparse-css-observer-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in ((32, "Missing owned menu host fixture: MnSlChr.usd"),
                                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                run = subprocess.run([str(node_runtime()), str(target), str(absent),
                    str(absent), str(stage), str(self.scratch / f"sparse-css-observer-{stage}.jsonl"),
                    revision, "sparse-css-observer-preflight-v1"], cwd=ROOT,
                    capture_output=True, text=True, timeout=30)
                (self.scratch / f"sparse-css-observer-{stage}.stdout").write_text(run.stdout)
                (self.scratch / f"sparse-css-observer-{stage}.stderr").write_text(run.stderr)
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_natural_timeout_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-natural-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"natural-sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-natural-timeout-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"natural-sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"natural-sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_natural_resolution_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-resolution-sd-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                trace = self.scratch / f"resolution-sd-dispatch-{stage}.jsonl"
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage), str(trace), revision,
                           "sudden-death-natural-resolution-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"resolution-sd-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"resolution-sd-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_returned_menu_reducer_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-returned-menu-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for recipe in ("returned-menu-results-control-v1",
                       "returned-menu-two-human-results-input-control-v1",
                       "returned-menu-two-human-timeout-results-control-v1"):
            for stage, expected in (
                    (32, "Missing owned menu host fixture: MnSlChr.usd"),
                    (31, "Explicit FD recipes require Final Destination")):
                with self.subTest(stage=stage, recipe=recipe):
                    trace = self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.jsonl"
                    command = [str(node_runtime()), str(target), str(absent),
                               str(absent), str(stage), str(trace), revision,
                               recipe]
                    run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                         text=True, timeout=30)
                    (self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.stdout").write_text(
                        run.stdout, encoding="utf-8")
                    (self.scratch / f"returned-menu-dispatch-{recipe}-{stage}.stderr").write_text(
                        run.stderr, encoding="utf-8")
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn(expected, run.stderr)
                    self.assertNotIn("Unknown transition input recipe", run.stderr)

    def test_sudden_death_resolution_manifest_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        run = subprocess.run([str(node_runtime()), str(target),
                              "--sd-resolution-fixture-manifest"], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        records = [json.loads(line) for line in run.stdout.splitlines()
                   if line.startswith('{')]
        self.assertEqual(len(records), 1)
        names = records[0]["required"]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({"PlMrNr.dat", "PlMrYe.dat", "GrNLa.dat",
                         "GmRst.usd", "GmRstMMr.dat", "ff_mario.hps",
                         "IfPrize.usd", "SdPrize.usd", "s_info1.hps"}.issubset(names))
    def test_sudden_death_returned_menu_manifest_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        run = subprocess.run([str(node_runtime()), str(target),
                              "--returned-menu-fixture-manifest"], cwd=ROOT,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        records = [json.loads(line) for line in run.stdout.splitlines()
                   if line.startswith('{')]
        self.assertEqual(len(records), 1)
        names = records[0]["required"]
        self.assertEqual(len(names), len(set(names)))
        self.assertTrue({"PlMrNr.dat", "PlMrYe.dat",
                         "GmRst.usd", "GmRstMMr.dat", "ff_mario.hps",
                         "IfPrize.usd", "SdPrize.usd", "s_info1.hps"}.issubset(names))
        self.assertNotIn("GrNLa.dat", names)

    def test_sudden_death_menu_setup_dispatch_without_assets(self):
        target = ROOT / "build/browser-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("The native menu host trace has not been built")
        absent = self.scratch / "absent-sd-menu-fixtures"
        self.assertFalse(absent.exists())
        revision = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        for stage, expected in (
                (32, "Missing owned menu host fixture: MnSlChr.usd"),
                (31, "Explicit FD recipes require Final Destination")):
            with self.subTest(stage=stage):
                command = [str(node_runtime()), str(target), str(absent),
                           str(absent), str(stage),
                           str(self.scratch / f"sd-menu-dispatch-{stage}.jsonl"),
                           revision, "sudden-death-menu-setup-control-v1"]
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
                (self.scratch / f"sd-menu-dispatch-{stage}.stdout").write_text(
                    run.stdout, encoding="utf-8")
                (self.scratch / f"sd-menu-dispatch-{stage}.stderr").write_text(
                    run.stderr, encoding="utf-8")
                self.assertNotEqual(run.returncode, 0)
                self.assertIn(expected, run.stderr)
                self.assertNotIn("Unknown transition input recipe", run.stderr)
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
        off_flag = "--stadium-cache-live-controls=0"
        on_flag = "--stadium-cache-live-controls=1"
        self.assertEqual(len(off_flag), len(on_flag))
        commands = {
            "off": [str(node_runtime()), str(target), off_flag],
            "on": [str(node_runtime()), str(target), on_flag],
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

    def test_stadium_sis_text_event_controls(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed C1 diagnostic SIS text-event control first")
        command = [str(node_runtime()), str(target), "--stadium-sis-text-event-controls"]
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=30)
        (self.scratch / "sis-text-event.stdout").write_text(run.stdout, encoding="utf-8")
        (self.scratch / "sis-text-event.stderr").write_text(run.stderr, encoding="utf-8")
        self.assertEqual(run.returncode, 0, (run.stdout + run.stderr)[-5000:])
        self.assertIn("C1 asset-free SIS retired-menu baseline", run.stdout)
        self.assertRegex(run.stderr,
                         r"C1_SIS_TEXT_EVENT event=append call=5ACC tail=\S+ text=\S+")
        self.assertRegex(run.stderr,
                         r"C1_SIS_TEXT_EVENT event=remove call=5A2C text=\S+ previous=\S+ next=\S+")
        membership_controls = [line for line in run.stderr.splitlines()
                              if line.startswith("C1_SIS_TEXT_MEMBERSHIP_CONTROL ")]
        self.assertEqual(len(membership_controls), 2, run.stderr)
        for line in membership_controls:
            self.assertIn("snapshot_duplicate=refused", line)
            self.assertIn("stale_epoch=refused", line)
            self.assertIn("stale_world=refused", line)
            self.assertIn("stale_source_heap=refused", line)
            self.assertIn("stale_allocation=refused", line)
            self.assertIn("removed_payload=absent", line)
            self.assertIn("malformed_used_head=refused", line)
        started_layouts = [line for line in run.stderr.splitlines()
                           if line.startswith("C1_SIS_STARTED_RETIREMENT layout=")]
        self.assertEqual(len(started_layouts), 12, run.stderr)
        for layout in ("prefix", "middle", "suffix"):
            observations = [line for line in started_layouts
                            if f"layout={layout} " in line]
            self.assertEqual(len(observations), 4, run.stderr)
            self.assertEqual(sum("initial_snapshot=accepted" in line
                                 for line in observations), 2, run.stderr)
            self.assertEqual(sum("selective_drain_preserved_order_and_bytes=1" in line
                                 for line in observations), 2, run.stderr)
        for line in started_layouts:
            if "initial_snapshot=accepted" in line:
                self.assertIn("text_cycle_refused=1", line)
                self.assertIn("context_cycle_refused=1", line)
                self.assertIn("negative_state_unchanged=1", line)
        reuse_rows = [line for line in run.stderr.splitlines()
                      if line.startswith("C1_SIS_STARTED_REUSE ")]
        self.assertEqual(len(reuse_rows), 2, run.stderr)
        for line in reuse_rows:
            for field in ("original_text_remove_recreate=1", "text_cells_reused=1",
                          "renderer_cells_reused=1", "original_order_restored=1",
                          "prior_destruction_latched=7", "refusal_unchanged=1"):
                self.assertIn(field, line)
        extra_context = [line for line in run.stderr.splitlines()
                         if line.startswith(
                             "C1_SIS_STARTED_RETIREMENT extra_font1_context_refused=")]
        self.assertEqual(len(extra_context), 2, run.stderr)
        for line in extra_context:
            self.assertIn("extra_font1_context_refused=1", line)
            self.assertIn("no_mutation_before_original_drain=1", line)
            self.assertIn("original_all_text_context_drain_cleanup=1", line)
        started_controls = [line for line in run.stderr.splitlines()
                            if line.startswith(
                                "C1_SIS_STARTED_RETIREMENT_CONTROL ")]
        self.assertEqual(len(started_controls), 2, run.stderr)
        for line in started_controls:
            self.assertIn("source_611c=1", line)
            self.assertIn("source_5acc=1", line)
            self.assertIn("source_5da0=1", line)
            self.assertIn("source_5e70_cleanup=1", line)
            self.assertIn("all_three_splice_positions=1", line)
            self.assertIn("destroyed_borrowed_parent_context_x4_null=1", line)

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

    def test_stadium_pad_leave_probe_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_PAD_LEAVE_PROBE") != "1":
            self.skipTest("Real Stadium PAD leave probe requires its reviewed run gate")
        import hashlib
        import re
        from capture_sd_reference_prefix import cleanup_process

        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        self.assertTrue(fixture_value, "Retained Stadium fixture root is required")
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_absolute(), "Use the frozen absolute fixture root")
        menu, game = fixture / "native-menus", fixture / "next-gate"
        self.assertTrue(target.is_file())
        self.assertTrue(target.with_suffix(".wasm").is_file())
        menu_script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", menu_script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        names = sorted(set(menu_names) | set(selected_names))
        self.assertEqual((len(menu_names), len(selected_names), len(names)), (76, 36, 98))
        paths = {name: (menu / name if (menu / name).is_file() else game / name)
                 for name in names}
        self.assertTrue(all(path.is_file() for path in paths.values()),
                        "Retained exact Stadium source fixture union is incomplete")
        before = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for name, path in paths.items()}
        source = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        tree = subprocess.check_output(
            ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-pad-leave-probe.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source, "stadium-pad-leave-probe-v1"]
        output = self.scratch / "stadium-pad-leave-node-owner"
        output.mkdir(exist_ok=False)
        identity = {
            "scope": "stadium-pad-leave-probe-v1", "ownership": "direct-Popen",
            "source_revision": source, "source_tree": tree,
            "argv": command, "cwd": str(ROOT), "timeout_seconds": 120,
            "outer_timeout_seconds": 180,
            "observation_caps": {"max_bytes": 16 * 1024 * 1024, "max_rows": 4096},
            "fixture_sha256_before": before,
            "binary_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in [Path(command[0]).resolve(), target,
                                           target.with_suffix(".wasm")]},
        }
        stdout_path = self.scratch / "stadium-pad-leave.stdout"
        stderr_path = self.scratch / "stadium-pad-leave.stderr"
        process = None
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
                try:
                    identity["pid"] = process.pid
                    with (output / "identity.json").open("x", encoding="utf-8") as receipt:
                        receipt.write(json.dumps(identity, indent=2) + "\n")
                    process.wait(timeout=120)
                finally:
                    cleanup_process(process, output, scope="stadium-pad-leave-probe-v1")
        finally:
            after = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                     for name, path in paths.items()}
            with (output / "fixture-after.json").open("x", encoding="utf-8") as receipt:
                receipt.write(json.dumps(after, indent=2) + "\n")
            self.assertEqual(after, before,
                             "Real Stadium PAD leave probe changed a retained source fixture")

        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
        trace_bytes = trace.stat().st_size if trace.is_file() else -1
        trace_lines = trace.read_text(encoding="utf-8").splitlines() if trace.is_file() else []
        self.assertLessEqual(stdout_path.stat().st_size + stderr_path.stat().st_size +
                             max(trace_bytes, 0), 16 * 1024 * 1024,
                             "PAD leave probe exceeded its 16 MiB observation cap")
        self.assertLessEqual(len(trace_lines), 4096,
                             "PAD leave probe exceeded its 4096-row trace cap")
        self.assertIsNotNone(process)
        self.assertEqual(process.returncode, 0, (stdout + stderr)[-12000:])

        cleanup = json.loads((output / "cleanup.json").read_text(encoding="utf-8"))
        self.assertEqual(cleanup, {
            "scope": "stadium-pad-leave-probe-v1", "pid": process.pid,
            "ownership": "direct-Popen", "terminate_sent": False,
            "kill_sent": False, "returncode": 0, "error": None,
        })
        self.assertEqual(json.loads((output / "identity.json").read_text())["pid"],
                         process.pid)
        self.assertEqual(json.loads((output / "fixture-after.json").read_text()), before)

        trace_rows = [json.loads(line) for line in trace_lines]
        self.assertTrue(trace_rows, "PAD leave probe produced no transition trace")
        header = trace_rows[0]
        self.assertEqual((header.get("record"), header.get("schema"),
                          header.get("source_revision"), header.get("input_recipe")),
                         ("header", "melee-web-transition-trace", source,
                          "stadium-pad-leave-probe-v1"))
        leases = [row for row in trace_rows if row.get("record") == "sis_lease"]
        self.assertEqual([row.get("boundary") for row in leases],
                         ["captured_before_menu_leave",
                          "verified_retired_before_world_shutdown"])

        def records(prefix):
            result = []
            for line in stderr.splitlines():
                if line.startswith(prefix + " "):
                    # A decode failure's human-readable suffix contains spaces;
                    # the structured fields before it remain unambiguous.
                    structured = re.sub(r" decode_error=.*$", "", line)
                    result.append(parse_c1_record(structured, prefix))
            return result

        snapshots = records("C1_PAD_SNAPSHOT")
        self.assertEqual([row.get("boundary") for row in snapshots],
                         ["before-menu-leave", "after-menu-leave"], stderr[-12000:])
        raw_rows = records("C1_PAD_RAW")
        self.assertEqual([row.get("boundary") for row in raw_rows],
                         ["before-menu-leave", "after-menu-leave"])
        raw_by_boundary = {row["boundary"]: bytes.fromhex(row["hex"]) for row in raw_rows}
        self.assertTrue(all(len(raw) == 822 for raw in raw_by_boundary.values()))
        config_rows = records("C1_PAD_CONFIG")
        self.assertEqual([row.get("boundary") for row in config_rows],
                         ["before-menu-leave", "after-menu-leave"])
        histories = records("C1_PAD_HISTORY")
        self.assertEqual(len(histories), 24)

        def fnv64(raw):
            value = 14695981039346656037
            for byte in raw:
                value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
            return value

        invalid_by_boundary = {}
        for boundary, meta, config_row in zip(
                ("before-menu-leave", "after-menu-leave"), snapshots, config_rows):
            raw = raw_by_boundary[boundary]
            config, parsed_history, config_invalid, invalid = parse_pad_snapshot_wire(raw)
            self.assertEqual(int(meta["bytes"]), 822)
            self.assertEqual(meta["fnv64"], f"{fnv64(raw):016x}")
            self.assertEqual(int(meta["config_valid"]), not config_invalid)
            self.assertEqual(int(meta["decode"]), not invalid,
                             f"{boundary}: production decode disagrees with raw wire semantics; "
                             f"invalid={invalid}; raw={raw.hex()}")
            expected_config = {
                "repeat_start": str(config["repeat_start"]),
                "repeat_interval": str(config["repeat_interval"]),
                "adc_type": str(config["adc_type"]), "adc_th": str(config["adc_th"]),
                "adc_angle_bits": f"{config['adc_angle_bits']:08x}",
                "clamp_stick_type": str(config["clamp_stick_type"]),
                "clamp_stick_shift": str(config["clamp_stick_shift"]),
                "clamp_stick_max": str(config["clamp_stick_max"]),
                "clamp_stick_min": str(config["clamp_stick_min"]),
                "clamp_lr": ",".join(map(str, config["clamp_lr"])),
                "clamp_ab": ",".join(map(str, config["clamp_ab"])),
                "scale": ",".join(map(str, config["scale"])),
                "cross_dir": str(config["cross_dir"]),
                "reset": ",".join(map(str, config["reset"])),
            }
            self.assertEqual({key: config_row[key] for key in expected_config},
                             expected_config, f"{boundary} raw config field mismatch")
            observed_history = [row for row in histories if row.get("boundary") == boundary]
            self.assertEqual(len(observed_history), 12)
            for observed, expected in zip(observed_history, parsed_history):
                self.assertEqual((int(observed["bank"]), int(observed["slot"])),
                                 (expected["bank"], expected["slot"]))
                self.assertEqual([observed[key] for key in
                                  ("button", "last", "trigger", "repeat", "release")],
                                 [f"{value:08x}" for value in expected["buttons"]])
                self.assertEqual(int(observed["repeat_count"]), expected["repeat_count"])
                self.assertEqual(list(map(int, observed["sticks"].split(","))),
                                 expected["sticks"])
                self.assertEqual(list(map(int, observed["analog"].split(","))),
                                 expected["analog"])
                self.assertEqual(observed["normalized_bits"],
                                 ",".join(f"{value:08x}" for value in
                                          expected["normalized_bits"]))
                self.assertEqual(observed["finite"], f"{expected['finite_count']}/8")
                self.assertEqual(int(observed["cross_dir"]), expected["cross_dir"])
                self.assertEqual(int(observed["err"]), expected["err"])
            invalid_by_boundary[boundary] = invalid

        self.assertEqual(int(snapshots[0]["previous_equal"]), -1)
        self.assertEqual(int(snapshots[1]["previous_equal"]),
                         raw_by_boundary["before-menu-leave"] ==
                         raw_by_boundary["after-menu-leave"])
        self.assertFalse(invalid_by_boundary["before-menu-leave"],
                         f"Pre-leave captured PAD was invalid: "
                         f"{invalid_by_boundary['before-menu-leave']}; "
                         f"raw={raw_by_boundary['before-menu-leave'].hex()}")
        post_invalid = invalid_by_boundary["after-menu-leave"]
        self.assertTrue(post_invalid,
                        "Post-leave PAD wire unexpectedly decoded cleanly; raw="
                        f"{raw_by_boundary['after-menu-leave'].hex()}")
        self.assertTrue(any("=" in field for field in post_invalid),
                        f"Post-leave strict refusal lacks a numeric invalid field: {post_invalid}")
        (self.scratch / "stadium-pad-leave-wire-validation.json").write_text(
            json.dumps({"schema": "stadium-pad-leave-wire-validation-v1",
                        "snapshots": {boundary: {"raw_hex": raw_by_boundary[boundary].hex(),
                                                  "strict_decode": not invalid_by_boundary[boundary],
                                                  "numeric_invalid": invalid_by_boundary[boundary]}
                                      for boundary in raw_by_boundary}},
                       indent=2, sort_keys=True) + "\n", encoding="utf-8")

        retained = records("C1_PAD_LEAVE_RETAINED")
        self.assertEqual(len(retained), 1)
        # Leaving SSS closes the source scene, but the owning GameplayMenuWorld
        # and its SDK world stay live until the explicit world->close() below.
        self.assertEqual({key: int(retained[0][key]) for key in
                          ("host_input", "host_phase", "source_scene", "source_world_exists")},
                         {"host_input": 1, "host_phase": 5, "source_scene": 0,
                          "source_world_exists": 1})
        close = records("C1_PAD_CLOSE")
        self.assertEqual(len(close), 1)
        self.assertEqual({key: int(close[0][key]) for key in
                          ("retained_equal", "host_phase", "source_scene", "world_exists")},
                         {"retained_equal": 1, "host_phase": 5,
                          "source_scene": 0, "world_exists": 0})
        self.assertNotIn(close[0].get("input"), {None, "0x0", "(nil)"}, close[0])
        selection_rng = [row for row in records("C1_SELECTION_RNG")
                         if row.get("boundary") == "reopened-context-start"]
        self.assertEqual(len(selection_rng), 1)
        self.assertEqual(retained[0]["rng_owner"], selection_rng[0]["owner"])
        self.assertEqual(int(retained[0]["rng_value"]), int(selection_rng[0]["initial"]))
        self.assertEqual((selection_rng[0]["live_available"], selection_rng[0]["live"]),
                         ("1", selection_rng[0]["initial"]))

        closed = records("C3_SESSION_CLOSED")
        self.assertEqual(len(closed), 1)
        self.assertEqual({key: int(closed[0][key]) for key in
                          ("identity", "generation", "bytes", "world_exists")},
                         {"identity": 0, "generation": 0, "bytes": 0, "world_exists": 0})
        self.assertIn("Stadium source PAD leave probe captured pre/post menu-leave wire state",
                      stdout)
        self.assertIn("no MatchSession or Ready construction", stdout)
        self.assertNotIn("STADIUM_READY_SESSION", stderr)
        self.assertNotIn("STADIUM_READY_CONSTRUCTION", stderr)

    def _assert_stadium_source_text_events(
            self, stdout, stderr, *, probe_marker, first_failure_marker,
            observation_path, scope, require_membership=False):
        import re

        first_failure = stderr.index(first_failure_marker)
        self.assertIn("C1_HUD_TEXT_PHASE phase=begin-before-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=begin-after-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=end-before-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=end-after-ifAll", stderr)

        # All rows share stderr ordering. Reconstruct pointer linkage only from
        # the already observed append/remove operands; never dereference them.
        append_pattern = re.compile(
            r"C1_SIS_TEXT_EVENT event=append call=5ACC tail=(\S+) text=(\S+) "
            r"entity=(\S+) font=(\d+) context=(-?\d+)")
        remove_pattern = re.compile(
            r"C1_SIS_TEXT_EVENT event=remove call=5A2C text=(\S+) "
            r"previous=(\S+) next=(\S+) font=(\d+)")
        event_pattern = re.compile(
            r"C1_SIS_TEXT_EVENT event=(append|remove|remove-miss) call=(5ACC|5A2C) "
            r"(.*)")
        append_rows = list(append_pattern.finditer(stderr))
        remove_rows = list(remove_pattern.finditer(stderr))
        self.assertGreaterEqual(len(append_rows), 3, stderr)
        tag_rows = list(re.finditer(
            r"C1_NAMETAG_TEXT event=create function=un_802FD4C8 "
            r"text=(\S+) context=(-?\d+) gobj=(\S+)", stderr))
        self.assertEqual(len(tag_rows), 1, stderr)
        tag_ptr = tag_rows[0].group(1)
        tag_context = int(tag_rows[0].group(2), 10)
        tag_append_rows = [row for row in append_rows
                           if row.group(2) == tag_ptr and row.group(4) == "2"]
        self.assertEqual(len(tag_append_rows), 1, stderr)
        self.assertEqual(tag_context, int(tag_append_rows[0].group(5), 10),
                         "Nametag SIS context ID differs from its matching append event")
        probe_armed = stderr.index(probe_marker)
        begin_before = stderr.index("C1_HUD_TEXT_PHASE phase=begin-before-ifAll")
        begin_after = stderr.index("C1_HUD_TEXT_PHASE phase=begin-after-ifAll")
        end_before = stderr.index("C1_HUD_TEXT_PHASE phase=end-before-ifAll")
        end_after = stderr.index("C1_HUD_TEXT_PHASE phase=end-after-ifAll")
        self.assertLess(probe_armed, append_rows[0].start())
        self.assertLess(begin_before, tag_append_rows[0].start())
        self.assertLess(tag_append_rows[0].start(), tag_rows[0].start())
        self.assertLess(tag_rows[0].start(), begin_after)
        self.assertLess(begin_after, end_before)
        self.assertLess(end_before, end_after)
        self.assertTrue(all(probe_armed < row.start() < first_failure for row in append_rows))
        self.assertTrue(all(probe_armed < row.start() < first_failure for row in remove_rows))
        self.assertLess(tag_rows[0].start(), first_failure)

        def pointer(value):
            return 0 if value in {"(nil)", "0", "0x0"} else int(value, 16)

        observed_unremoved = []
        observed_events = []
        for match in event_pattern.finditer(stderr):
            event, _call, fields = match.groups()
            if event == "append":
                row = append_pattern.match(match.group(0))
                self.assertIsNotNone(row, match.group(0))
                tail, text, entity, font, context = row.groups()
                self.assertEqual(pointer(tail), observed_unremoved[-1] if observed_unremoved else 0,
                                 "An unobserved prefix prevents source-pointer attribution")
                text_address = pointer(text)
                self.assertNotIn(text_address, observed_unremoved,
                                 "Duplicate pointer in the observed SIS list")
                observed_unremoved.append(text_address)
                observed_events.append({"event": event, "tail": tail, "text": text,
                                        "entity": entity, "font": int(font),
                                        "context": int(context)})
            elif event == "remove":
                row = remove_pattern.match(match.group(0))
                self.assertIsNotNone(row, match.group(0))
                text, previous, next_text, font = row.groups()
                address = pointer(text)
                self.assertIn(address, observed_unremoved,
                              "Removal lacks a retained observed append")
                index = observed_unremoved.index(address)
                expected_previous = observed_unremoved[index - 1] if index else 0
                expected_next = (observed_unremoved[index + 1]
                                 if index + 1 < len(observed_unremoved) else 0)
                self.assertEqual((pointer(previous), pointer(next_text)),
                                 (expected_previous, expected_next))
                observed_unremoved.pop(index)
                observed_events.append({"event": event, "text": text,
                                        "previous": previous, "next": next_text,
                                        "font": int(font)})
            else:
                observed_events.append({"event": event, "fields": fields})

        refusal_match = re.search(
            r"STADIUM_TEXT_TOPOLOGY_REJECT primitive=(\S+) index=(\d+) "
            r"actual=(\S+) expected=(\S+) operand_lifetime=unverified", stderr)
        refusal_relation = "unattributed"
        actual = expected = None
        if refusal_match:
            primitive, index, actual, expected = refusal_match.groups()
            self.assertLess(refusal_match.start(), first_failure)
            if pointer(actual) in observed_unremoved:
                refusal_relation = "matches-observed-unremoved-pointer-only"
            if pointer(actual) == pointer(tag_ptr):
                refusal_relation = "matches-nametag-pointer-only"
        else:
            primitive = index = None

        membership_record = None
        membership_lines = [line for line in stderr.splitlines()
                            if line.startswith("STADIUM_TEXT_MEMBERSHIP_OBSERVATION ")]
        if require_membership:
            self.assertIsNotNone(refusal_match, stderr)
            self.assertEqual(len(membership_lines), 1, stderr)
            snapshot_lines = [line for line in stderr.splitlines()
                              if line.startswith("STADIUM_READY_TEXT_MEMBERSHIP phase=snapshot-captured ")]
            self.assertEqual(len(snapshot_lines), 1, stderr)
            snapshot = parse_c1_record(snapshot_lines[0],
                                       "STADIUM_READY_TEXT_MEMBERSHIP")
            membership_line = membership_lines[0]
            membership = parse_c1_record(membership_line,
                                         "STADIUM_TEXT_MEMBERSHIP_OBSERVATION")
            self.assertLess(refusal_match.start(), stderr.index(membership_line))
            preflight_reject = stderr.index("STADIUM_DISPLAY_PREFLIGHT_REJECT")
            self.assertLess(stderr.index(membership_line), preflight_reject)
            self.assertEqual((membership["clause"], membership["index"],
                              membership["actual"], membership["expected"]),
                             (primitive, index, actual, expected))
            self.assertEqual((snapshot["tick"], snapshot["requested_bytes"],
                              snapshot["retirement_verified"]), ("0", "18432", "0"))
            for snapshot_key, observation_key in (
                    ("heap", "snapshot_heap"), ("world", "snapshot_world"),
                    ("source_heap", "snapshot_source_heap"),
                    ("allocation", "snapshot_allocation"),
                    ("epoch", "snapshot_epoch")):
                self.assertEqual(snapshot[snapshot_key], membership[observation_key])
            self.assertEqual(membership["snapshot_requested"], snapshot["requested_bytes"])
            self.assertIn(membership["status"], {"present", "absent", "unavailable"})
            payload_size = int(membership["payload_size"], 10)
            expected_payload_size = int(membership["expected_payload_size"], 10)
            if (membership["status"] in {"present", "absent"} or
                    membership["reason"] in {"used-head-invalid",
                                              "expected-payload-size-mismatch"}):
                self.assertEqual(membership["current_heap"], snapshot["heap"])
                self.assertEqual(membership["current_world"], snapshot["world"])
                self.assertEqual(membership["current_source_heap"],
                                 snapshot["source_heap"])
                self.assertEqual(membership["current_allocation"],
                                 snapshot["allocation"])
                self.assertEqual(membership["current_epoch"], snapshot["epoch"])
                self.assertEqual((membership["current_context_status"],
                                  membership["current_allocation_status"],
                                  membership["current_live"],
                                  membership["current_requested"]),
                                 ("0", "0", "1", "18432"))
            if membership["status"] == "present":
                self.assertEqual(membership["reason"], "none")
                self.assertEqual(membership["payload_size"],
                                 membership["expected_payload_size"])
            elif membership["status"] == "absent":
                self.assertEqual(membership["reason"], "none")
                self.assertEqual(payload_size, 0)
            else:
                self.assertIn(membership["reason"],
                              {"backing-lease-mismatch", "used-head-invalid",
                               "expected-payload-size-mismatch"})
                if membership["reason"] == "expected-payload-size-mismatch":
                    self.assertNotEqual(payload_size, expected_payload_size)
                else:
                    self.assertEqual(payload_size, 0)
                if membership["reason"] == "backing-lease-mismatch":
                    self.assertTrue(
                        membership["current_heap"] != snapshot["heap"] or
                        membership["current_active"] != "1" or
                        membership["current_epoch"] != snapshot["epoch"] or
                        membership["current_context_status"] != "0" or
                        membership["current_world"] != snapshot["world"] or
                        membership["current_source_heap"] != snapshot["source_heap"] or
                        membership["current_allocation_status"] != "0" or
                        membership["current_allocation"] != snapshot["allocation"] or
                        membership["current_live"] != "1" or
                        membership["current_requested"] != "18432")
            membership_record = {"snapshot": snapshot, "observation": membership,
                                 "expected_payload_size": expected_payload_size}
        else:
            self.assertEqual(membership_lines, [],
                             "The zero-tick probe must not invent a Ready snapshot")

        if probe_marker.startswith("STADIUM_SOURCE_TEXT_LIFETIME"):
            construction_tick_match = re.search(
                r"STADIUM_SOURCE_TEXT_LIFETIME phase=construction-complete "
                r"world_exists=\d+ ticks=(\d+) match_retained=\d+", stderr)
        else:
            construction_tick_match = re.search(
                r"STADIUM_READY_SESSION phase=construction-complete-before-source-ticks "
                r"world_exists=\d+ world=\d+ ticks=(\d+)", stderr)
        self.assertIsNotNone(construction_tick_match, stderr)
        claims = ["pointer identity and original append/remove call ordering only",
                  "no target-object lifetime claim from address equality",
                  "no StageLast/display retirement or C3 acceptance"]
        if require_membership:
            claims.insert(1, "SIS backing membership observed independently of semantic text identity")
        else:
            claims.insert(1, "SIS backing/suballocation membership remains unverified")
        observation = {
            "scope": scope,
            "lifecycle": "failed; incomplete close, not teardown success",
            "source_ticks_at_construction_complete": int(construction_tick_match.group(1)),
            "nametag_pointer": tag_ptr,
            "nametag_append": {
                "tail": tag_append_rows[0].group(1),
                "font": int(tag_append_rows[0].group(4)),
                "context": int(tag_append_rows[0].group(5)),
            },
            "first_refusal_primitive": primitive,
            "first_refusal_index": index,
            "first_refusal_actual": actual,
            "first_refusal_expected": expected,
            "first_refusal_pointer_relation": refusal_relation,
            "first_refusal_operand_lifetime": (
                "unverified" if refusal_match else "no topology operand was observed"),
            "source_append_remove_rows": observed_events,
            "matched_remove_count": len(remove_rows),
            "observed_unremoved_append_pointers": [hex(value) for value in observed_unremoved],
            "membership": membership_record,
            "claims": claims,
        }
        Path(observation_path).write_text(
            json.dumps(observation, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        self.assertNotIn("normal-close-returned", stderr)
        if scope.startswith("zero-tick"):
            self.assertNotIn("source Ready", stdout)
        return observation

    def test_stadium_source_ready_session_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_READY_SESSION") != "1":
            self.skipTest("Real Stadium source Ready session requires its reviewed run gate")
        self._run_stadium_source_ready_session_one_shot()

    def test_stadium_source_post_ready_tick_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_POST_READY_TICK") != "1":
            self.skipTest("One post-Ready tick requires its separately reviewed run gate")
        self._run_stadium_source_ready_session_one_shot(post_ready_tick=True)

    def test_stadium_source_go_alignment_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_GO_ALIGNMENT") != "1":
            self.skipTest("Original GO/HUD source canary requires its separately reviewed run gate")
        self._run_stadium_source_ready_session_one_shot(go_alignment_probe=True)

    def test_stadium_source_setup_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_SETUP") != "1":
            self.skipTest("Original CSS/SSS setup receipt requires its separately reviewed run gate")
        self._run_stadium_source_ready_session_one_shot(setup_only=True)

    def test_stadium_source_toy_owner_controls_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_TOY_OWNER_CONTROLS") != "1":
            self.skipTest("Toy owner controls require their separately reviewed run gate")
        self._run_stadium_source_ready_session_one_shot(toy_owner_controls=True)

    def _run_stadium_source_ready_session_one_shot(
            self, toy_owner_controls=False, post_ready_tick=False, go_alignment_probe=False,
            setup_only=False):
        import hashlib
        from capture_sd_reference_prefix import cleanup_process

        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        self.assertTrue(fixture_value, "Retained Stadium fixture root is required")
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_absolute(), "Use the frozen absolute fixture root")
        menu, game = fixture / "native-menus", fixture / "next-gate"
        self.assertTrue(target.is_file())
        menu_script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", menu_script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        names = sorted(set(menu_names) | set(selected_names))
        self.assertEqual((len(menu_names), len(selected_names), len(names)), (76, 36, 98))
        paths = {name: (menu / name if (menu / name).is_file() else game / name)
                 for name in names}
        self.assertTrue(all(path.is_file() for path in paths.values()))
        before = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for name, path in paths.items()}
        source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        scope = ("stadium-source-toy-owner-controls-v1" if toy_owner_controls
                 else "stadium-source-post-ready-tick-v1" if post_ready_tick
                 else "stadium-source-go-alignment-v1" if go_alignment_probe
                 else "stadium-source-setup-v1" if setup_only
                 else "stadium-source-ready-session-v1")
        prefix = ("stadium-toy-owner" if toy_owner_controls
                  else "stadium-post-ready" if post_ready_tick
                  else "stadium-go-alignment" if go_alignment_probe
                  else "stadium-source-setup" if setup_only
                  else "stadium-source-ready")
        trace = self.scratch / (prefix + "-session.jsonl")
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source, scope]
        output = self.scratch / (prefix + "-node-owner")
        output.mkdir(exist_ok=False)
        identity = {
            "scope": scope, "ownership": "direct-Popen",
            "source_revision": source,
            "source_tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"],
                                                     cwd=ROOT, text=True).strip(),
            "argv": command, "cwd": str(ROOT), "timeout_seconds": 120,
            "fixture_sha256_before": before,
            "binary_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in [Path(command[0]).resolve(), target,
                                           target.with_suffix(".wasm")]},
        }
        stdout_path = self.scratch / (prefix + ".stdout")
        stderr_path = self.scratch / (prefix + ".stderr")
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
                try:
                    identity["pid"] = process.pid
                    with (output / "identity.json").open("x", encoding="utf-8") as receipt:
                        receipt.write(json.dumps(identity, indent=2) + "\n")
                    process.wait(timeout=120)
                finally:
                    cleanup_process(process, output, scope=scope)
        finally:
            after = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                     for name, path in paths.items()}
            with (output / "fixture-after.json").open("x", encoding="utf-8") as receipt:
                receipt.write(json.dumps(after, indent=2) + "\n")
            self.assertEqual(after, before, "Real Stadium fixture changed during source Ready session")
        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
        self.assertEqual(process.returncode, 0, (stdout + stderr)[-12000:])
        import re
        if setup_only:
            self.assertIn("Stadium source CSS/SSS setup receipt emitted; no OnInit, match construction, Ready/GO, draw or gameplay claim", stdout)
            self.assertNotIn("STADIUM_READY_SESSION", stderr)
            self.assertIn("C3_SESSION_CLOSED identity=0 generation=0 bytes=0 world_exists=0", stderr)
            rows = [json.loads(line) for line in trace.read_text().splitlines()]
            self.assertEqual([row["record"] for row in rows], ["header", "stadium_source_setup"])
            self.assertEqual(rows[0]["input_recipe"], "stadium-source-setup-v1")
            self.assertEqual(rows[0]["schema"], "melee-web-transition-trace")
            self.assertEqual(rows[0]["producer"], "port")
            self.assertEqual(rows[0]["game_revision"], "GALE01r2")
            self.assertEqual(rows[0]["build_configuration"], "browser-release")
            self.assertEqual(rows[0]["source_revision"], source)
            receipt = rows[1]
            self.assertEqual((receipt["schema"], receipt["boundary"], receipt["source_route"]),
                             ("stadium-c3-source-setup-v1",
                              "closed_original_sss_after_selection_before_match_entry",
                              "original_css_to_original_sss"))
            self.assertEqual(receipt["stage"]["kind"], 3)
            self.assertIsInstance(receipt["first_css_seed"], int)
            self.assertIsInstance(receipt["sss_selected_seed"], int)
            self.assertIn("equality is not assumed", receipt["seed_relationship"])
            self.assertIn("seed_ptr sampled after original CSS entry", receipt["seed_sources"]["first_css_seed"])
            self.assertEqual(receipt["seed_sources"]["sss_selected_seed"],
                             "random_seed from checked original SSS selection")
            self.assertEqual(receipt["first_css_source_observation"]["source_scene"], 1)
            self.assertEqual(receipt["sss_source_observation_before_leave"]["source_scene"], 2)
            self.assertEqual(receipt["save_profile"]["schema"], "melee-web-save-profile-card-v1")
            self.assertEqual(receipt["save_profile"]["bytes"],
                             len(receipt["save_profile"]["immutable_pre_css_baseline_hex"]) // 2)
            for field in ("immutable_pre_css_baseline_hex", "first_css_entry_current_hex",
                          "post_sss_transition_current_hex"):
                value = receipt["save_profile"][field]
                self.assertEqual(len(value), 2 * receipt["save_profile"]["bytes"])
                self.assertRegex(value, r"^[0-9a-f]+$")
            payloads = receipt["setup_payloads"]
            self.assertEqual(payloads["source_calls"], {
                "raw_sss_start": "melee_web_menu_host_stadium_c1a_raw_selection after original SSS leave",
                "normalized_selection": "melee_web_menu_host_stadium_c1a_selection after original SSS leave",
                "post_vs_mode": "melee_web_menu_host_post_vs_mode after original SSS leave"})
            self.assertEqual(payloads["raw_sss_start"]["rules"]["stage_kind"], 3)
            normalized = payloads["normalized_selection"]
            self.assertEqual(normalized["start"]["rules"]["stage_kind"], 3)
            self.assertGreaterEqual(normalized["player_count"], 0)
            self.assertLessEqual(normalized["player_count"], 4)
            self.assertEqual(len(normalized["start"]["players"]), 6)
            self.assertEqual(len(normalized["players"]), 4)
            self.assertEqual(payloads["post_vs_mode"]["start"]["rules"]["stage_kind"], 3)
            self.assertTrue({"loser", "ordered_stage_index", "winner", "unk_0x3", "unk_0x4",
                             "unk_0x5", "unk_0x6", "unk_0x7", "start"}.issubset(
                                 payloads["post_vs_mode"]))
            for payload in (payloads["raw_sss_start"], normalized["start"],
                            payloads["post_vs_mode"]["start"]):
                self.assertEqual(len(payload["players"]), 6)
                for player in payload["players"]:
                    self.assertTrue({"ckind", "slot_type", "stocks", "color", "slot",
                                     "spawn", "spawn_direction", "sub_color", "handicap",
                                     "team", "nametag", "xB", "rumble_enabled", "xC_b1",
                                     "xC_b2", "xC_b3", "vs_invisible", "xC_b5", "xC_b6",
                                     "xC_b7", "xD_b0", "xD_b1", "xD_b2", "xD_b3", "xD_b4",
                                     "xD_b5", "xD_b6", "xD_b7", "cpu_kind", "cpu_level",
                                     "damage_10", "damage_12", "hp", "attack_ratio_bits",
                                     "defense_ratio_bits", "model_scale_bits"}.issubset(player))
                rules = payload["rules"]
                self.assertTrue({"match_kind", "x0_3", "timer_enabled", "timer_counts_up",
                                 "x1_0", "x1_1", "x1_2", "x1_3", "x1_4", "x1_5",
                                 "timer_shows_hours", "friendly_fire", "is_stock", "x2_1",
                                 "x2_2", "single_button", "disable_pausing", "x2_5", "x2_6",
                                 "x2_7", "x3_0", "x3_1", "x3_2", "x3_3", "x3_4", "x3_5",
                                 "x3_6", "x3_7", "x4_0", "is_vs", "x4_2", "x4_3", "x4_4",
                                 "x4_5", "x4_6", "x4_7", "x5_0", "x5_1", "x5_2", "x5_3",
                                 "x5_4", "x5_5", "x5_6", "x5_7", "x6", "x7", "is_teams",
                                 "x9", "xA", "xB", "xC", "xD", "stage_kind", "time_limit",
                                 "x14", "x18", "x1C_pad", "item_mask", "x28", "x2C_bits",
                                 "damage_ratio_bits", "game_speed_bits", "on_unpause_override",
                                 "on_pause_override", "check_for_pauser_override",
                                 "on_match_start", "on_frame_start", "on_frame_end", "on_match_end",
                                 "x54_pointer", "x58_pointer", "pad_x5C"}.issubset(rules))
                for key in ("on_unpause_override", "on_pause_override",
                            "check_for_pauser_override", "on_match_start",
                            "on_frame_start", "on_frame_end", "on_match_end",
                            "x54_pointer", "x58_pointer"):
                    self.assertIn(rules[key], ("null", "unresolved_nonnull"))
                self.assertNotIn("raw_start_hex", payload)
            pad = receipt["pad"]
            self.assertEqual(pad["wire_bytes"], 822)
            self.assertEqual(len(pad["wire_hex"]), 2 * pad["wire_bytes"])
            self.assertRegex(pad["wire_hex"], r"^[0-9a-f]+$")
            self.assertEqual(len(pad["first_css_entry_sample"]), 4)
            self.assertEqual(len(pad["sss_confirmation_attempt_sample"]), 4)
            self.assertEqual(len(pad["sss_completion_sample"]), 4)
            self.assertEqual(pad["transition_ticks"]["completion_result"], 3)
            self.assertTrue(pad["transition_ticks"]["first_css_sample_consumed_by_successful_tick"])
            self.assertTrue(pad["transition_ticks"]["tick_returned_successfully"])
            ticks = pad["transition_ticks"]
            self.assertTrue(ticks["queue_count_empty_before_enqueue_checked"])
            self.assertTrue(ticks["sample_consumed_before_tick_return_checked"])
            self.assertEqual(ticks["completion_sample_source"],
                             "neutral_completion_wait_tick" if ticks["neutral_completion_wait_ticks"]
                             else "confirmation_tick")
            first_sample = pad["first_css_entry_sample"]
            self.assertEqual([row["port"] for row in first_sample], [0, 1, 2, 3])
            for row in first_sample:
                self.assertEqual((row["button"], row["stick_x"], row["stick_y"],
                                  row["substick_x"], row["substick_y"],
                                  row["trigger_left"], row["trigger_right"],
                                  row["analog_a"], row["analog_b"]), (0,) * 9)
            self.assertNotEqual(pad["sss_confirmation_attempt_sample"][0]["button"], 0)
            self.assertTrue(receipt["retained_closed_sss_input_available"])
            self.assertEqual(receipt["scope"], {"on_init": False, "match_session_constructed": False,
                                                  "ready_or_go": False, "draw": False,
                                                  "gameplay_ticks": 0})
            self.assertIn("full 0x55E8 transient SaveData bytes", receipt["unobserved"])
            self.assertIn("private PAD queue slots and qcount snapshot", receipt["unobserved"])
            self.assertIn("full CSS-to-SSS PAD input history; only initial/confirmation/completion samples and final history wire are recorded",
                          receipt["unobserved"])
            self.assertIn("raw StartMeleeData ABI padding and native pointer bit patterns",
                          receipt["unobserved"])
            return
        phases = re.findall(r"STADIUM_READY_SESSION phase=(\S+) world_exists=(\d+) world=(\d+) ticks=(\d+) heap=(-?\d+) objects=(\d+) processes=(\d+)", stderr)
        self.assertEqual([row[0] for row in phases], ["before-construction",
                         "construction-complete-before-source-ticks", "first-ready-before-close",
                         "checked-session-close"])
        self.assertEqual(int(phases[0][1]), 0)
        self.assertEqual(int(phases[1][3]), 0)
        self.assertGreater(int(phases[2][2]), 0)
        self.assertGreater(int(phases[2][3]), 0)
        self.assertEqual([int(phases[3][i]) for i in (1, 2, 3, 5, 6)], [0, 0, 0, 0, 0])
        if toy_owner_controls:
            self.assertIn("Stadium bounded Toy owner controls passed; one Ready plus no-acquisition and unarmed OnInit lifetimes, no C3 claim", stdout)
            self.assertIn("STADIUM_TOY_OWNER phase=entry-null-archive-table-refused unchanged=1", stderr)
            self.assertIn("STADIUM_TOY_OWNER phase=no-acquisition-close null_archive_table_refused=1 foreign_archive_refused=1 reset=1 ticks=0", stderr)
            self.assertIn("STADIUM_TOY_OWNER phase=source-on-init-destructor-close wrong_locale_refused=1 reset=1 ticks=0", stderr)
        elif go_alignment_probe:
            self.assertIn("Stadium source GO containing tick and immediately following full tick observed through HUD and checked close; source-frame values retained without ordinal inference; no C3 claim", stdout)
            event_rows = re.findall(
                r"STADIUM_SOURCE_GO_EVENT event=(\S+) sequence=(\d+) generation=(\d+) world_ticks=(\d+) source_frame=(\d+) object=(\d+) proc=(\d+) callback_index=(-?\d+) source_branch=(-?\d+) map2_gate=(-?\d+) display_mode=(-?\d+) remap_branch=(-?\d+) callback_identity=(-?\d+) hud_enabled=(-?\d+)",
                stderr)
            self.assertEqual([row[0] for row in event_rows],
                             ["StageBefore", "StageAfter", "GoAfter", "HudAfter"], stderr)
            self.assertEqual([int(row[1]) for row in event_rows], [1, 2, 3, 4])
            self.assertEqual(len({int(row[2]) for row in event_rows}), 1)
            stage_before, stage_after, go_event, hud_event = event_rows
            self.assertEqual((int(stage_before[9]), int(stage_after[9])), (1, 0))
            self.assertEqual((int(go_event[9]), int(go_event[10]), int(go_event[11])), (0, 0xB, 0))
            self.assertEqual((int(hud_event[9]), int(hud_event[10]), int(hud_event[11])), (0, 1, 0))
            self.assertEqual((int(go_event[12]), int(go_event[13])), (1, -1))
            self.assertEqual(int(hud_event[12]), 1)
            expected_branch = int(re.search(
                r"STADIUM_SOURCE_GO phase=armed generation=\d+ world_ticks=0 source_frame=\d+ expected_branch=(\d+) draw=0 tick_cap=600",
                stderr).group(1))
            self.assertEqual(int(go_event[8]), expected_branch)
            self.assertEqual(int(hud_event[8]), expected_branch)
            self.assertEqual(int(hud_event[7]), 4 if expected_branch == 0 else -1)
            containing = re.findall(
                r"STADIUM_SOURCE_GO_TICK kind=containing world_before=(\d+) world_after=(\d+) session_before=(\d+) session_after=(\d+) source_frame_before=(\d+) source_frame_after=(\d+) ready_after=(\d+) draw=0",
                stderr)
            following = re.findall(
                r"STADIUM_SOURCE_GO_TICK kind=first-full-post-go world_before=(\d+) world_after=(\d+) session_before=(\d+) session_after=(\d+) source_frame_before=(\d+) source_frame_after=(\d+) go_containing_source_frame_after=(\d+) ready_after=(\d+) draw=0",
                stderr)
            self.assertEqual(len(containing), 1, stderr)
            self.assertEqual(len(following), 1, stderr)
            c, f = tuple(map(int, containing[0])), tuple(map(int, following[0]))
            self.assertEqual((c[1], c[3]), (c[0] + 1, c[2] + 1))
            self.assertEqual((f[0], f[2]), (c[1], c[3]))
            self.assertEqual((f[1], f[3]), (f[0] + 1, f[2] + 1))
            self.assertEqual(f[6], c[5])
            complete = re.findall(
                r"STADIUM_SOURCE_GO phase=complete generation=(\d+) expected_branch=(\d+) go_branch=(\d+) hud_index=(-?\d+) stage_before_world=(\d+) stage_after_world=(\d+) go_world=(\d+) hud_world=(\d+) stage_before_frame=(\d+) stage_after_frame=(\d+) go_frame=(\d+) hud_frame=(\d+) first_post_go_tick=1 draw=0 hud_ready_at_observation=(\d+)",
                stderr)
            self.assertEqual(len(complete), 1, stderr)
            self.assertEqual(int(complete[0][0]), int(event_rows[0][2]))
            self.assertEqual(int(complete[0][3]), int(hud_event[7]))
            self.assertIn("STADIUM_SOURCE_GO phase=checked-close snapshot_unavailable=1", stderr)
            phase_position = stderr.index("STADIUM_READY_SESSION phase=first-ready-before-close")
            follow_position = stderr.index("kind=first-full-post-go")
            if c[6] == 1:
                self.assertLess(phase_position, follow_position,
                                "A Ready-triggered follow-up must be reported after first-ready")
            elif f[7] == 1:
                self.assertLess(follow_position, phase_position,
                                "A follow-up that reaches Ready belongs before first-ready")
            self.assertNotIn("STADIUM_POST_READY_TICK", stderr)
        elif post_ready_tick:
            self.assertIn("Stadium original source-session Ready plus one post-Ready source tick and checked owned-world retirement passed; exact GO alignment unobserved; no C3 claim", stdout)
            row = re.findall(
                r"STADIUM_POST_READY_TICK ticks_before=(\d+) ticks_after=(\d+) frames_before=(\d+) frames_after=(\d+) world_before=(\d+) world_after=(\d+) memory_status_before=(\d+) memory_status_after=(\d+) memory_world_before=(\d+) memory_world_after=(\d+) source_heap_before=(-?\d+) source_heap_after=(-?\d+) ready=(\d+) ending=(\d+) complete=(\d+) draw=(\d+) go_alignment=(\S+)",
                stderr)
            self.assertEqual(len(row), 1, stderr)
            values = row[0]
            ticks_before, ticks_after, frames_before, frames_after = map(int, values[:4])
            world_before, world_after = map(int, values[4:6])
            memory_status_before, memory_status_after = map(int, values[6:8])
            memory_world_before, memory_world_after = map(int, values[8:10])
            source_heap_before, source_heap_after = map(int, values[10:12])
            ready, ending, complete, draw = map(int, values[12:16])
            self.assertGreater(ticks_before, 0)
            self.assertEqual(ticks_after, ticks_before + 1)
            self.assertEqual(frames_after, frames_before + 1)
            self.assertEqual(world_before, int(phases[2][2]))
            self.assertEqual(ticks_before, int(phases[2][3]))
            self.assertEqual(world_after, world_before)
            self.assertEqual((memory_status_before, memory_status_after), (0, 0))
            self.assertEqual((memory_world_before, memory_world_after),
                             (world_before, world_before))
            self.assertGreaterEqual(source_heap_before, 0)
            self.assertEqual(source_heap_after, source_heap_before)
            self.assertEqual((ready, ending, complete, draw), (1, 0, 0, 0))
            self.assertEqual(values[16], "unobserved")
        else:
            self.assertIn("Stadium original source-session Ready and checked owned-world retirement passed; one lifetime, no draw/post-GO idle/C3 claim", stdout)
        self.assertNotIn("STADIUM_READY_SESSION_FIRST_FAILURE", stderr)
        if toy_owner_controls:
            begin = "STADIUM_TOY_OWNER phase=expected-entry-refusal-begin\n"
            end = "STADIUM_TOY_OWNER phase=expected-entry-refusal-end\n"
            expected = ("STADIUM_READY_CONSTRUCTION_FIRST_FAILURE "
                        "Match world requires a clean original Toy archive/table baseline")
            self.assertEqual(stderr.count(begin), 1)
            self.assertEqual(stderr.count(end), 1)
            before, bracket_and_after = stderr.split(begin)
            bracket, after = bracket_and_after.split(end)
            self.assertEqual([line for line in bracket.splitlines()
                              if "STADIUM_READY_CONSTRUCTION_FIRST_FAILURE" in line], [expected])
            self.assertNotIn("STADIUM_READY_CONSTRUCTION_FIRST_FAILURE", before + after)
        else:
            self.assertNotIn("STADIUM_READY_CONSTRUCTION_FIRST_FAILURE", stderr)
        self.assertIn("C3_SESSION_CLOSED identity=0 generation=0 bytes=0 world_exists=0", stderr)
        self.assertNotIn('"probe":"stadium-source-oninit"', stdout)

    def test_stadium_source_text_lifetime_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_TEXT_LIFETIME") != "1":
            self.skipTest("Zero-tick source text lifetime probe requires its reviewed run gate")
        import hashlib
        from capture_sd_reference_prefix import cleanup_process

        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        self.assertTrue(fixture_value, "Retained Stadium fixture root is required")
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_absolute(), "Use the frozen absolute fixture root")
        menu, game = fixture / "native-menus", fixture / "next-gate"
        self.assertTrue(target.is_file())
        self.assertTrue(target.with_suffix(".wasm").is_file())
        menu_script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", menu_script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        names = sorted(set(menu_names) | set(selected_names))
        self.assertEqual((len(menu_names), len(selected_names), len(names)), (76, 36, 98))
        paths = {name: (menu / name if (menu / name).is_file() else game / name)
                 for name in names}
        self.assertTrue(all(path.is_file() for path in paths.values()))
        before = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for name, path in paths.items()}
        source = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-source-text-lifetime.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source, "stadium-source-text-lifetime-v1"]
        output = self.scratch / "stadium-source-text-node-owner"
        output.mkdir(exist_ok=False)
        identity = {
            "scope": "stadium-source-text-lifetime-v1", "ownership": "direct-Popen",
            "source_revision": source,
            "source_tree": subprocess.check_output(
                ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip(),
            "argv": command, "cwd": str(ROOT), "timeout_seconds": 120,
            "fixture_sha256_before": before,
            "binary_sha256": {
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [Path(command[0]).resolve(), target, target.with_suffix(".wasm")]},
        }
        stdout_path = self.scratch / "stadium-source-text.stdout"
        stderr_path = self.scratch / "stadium-source-text.stderr"
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
                try:
                    identity["pid"] = process.pid
                    with (output / "identity.json").open("x", encoding="utf-8") as receipt:
                        receipt.write(json.dumps(identity, indent=2) + "\n")
                    process.wait(timeout=120)
                finally:
                    cleanup_process(
                        process, output, scope="stadium-source-text-lifetime-v1")
        finally:
            after = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                     for name, path in paths.items()}
            with (output / "fixture-after.json").open("x", encoding="utf-8") as receipt:
                receipt.write(json.dumps(after, indent=2) + "\n")
            self.assertEqual(after, before, "Real Stadium fixture changed during zero-tick text probe")
        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
        self.assertEqual(process.returncode, 1, (stdout + stderr)[-12000:])
        self.assertRegex(
            stderr,
            r"STADIUM_SOURCE_TEXT_LIFETIME phase=construction-complete world_exists=1 ticks=0 match_retained=1")
        self.assertIn(
            "STADIUM_SOURCE_TEXT_LIFETIME_FIRST_FAILURE", stderr,
            "The known refusal must remain explicitly a failed Session.close")
        first_failure = stderr.index("STADIUM_SOURCE_TEXT_LIFETIME_FIRST_FAILURE")
        self.assertRegex(stderr[first_failure:], r"ticks=0 match_retained=1 lifecycle=failed")
        self.assertIn("C1_HUD_TEXT_PHASE phase=begin-before-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=begin-after-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=end-before-ifAll", stderr)
        self.assertIn("C1_HUD_TEXT_PHASE phase=end-after-ifAll", stderr)

        self._assert_stadium_source_text_events(
            stdout, stderr,
            probe_marker="STADIUM_SOURCE_TEXT_LIFETIME phase=probe-armed",
            first_failure_marker="STADIUM_SOURCE_TEXT_LIFETIME_FIRST_FAILURE",
            observation_path=self.scratch / "stadium-source-text-observation.json",
            scope="zero-tick construction followed by normal Session.close",
            require_membership=False)

    def test_stadium_source_ready_text_membership_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_READY_TEXT_MEMBERSHIP") != "1":
            self.skipTest("Ready text membership observation requires its reviewed run gate")
        import hashlib
        from capture_sd_reference_prefix import cleanup_process

        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        self.assertTrue(fixture_value, "Retained Stadium fixture root is required")
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_absolute(), "Use the frozen absolute fixture root")
        menu, game = fixture / "native-menus", fixture / "next-gate"
        self.assertTrue(target.is_file())
        self.assertTrue(target.with_suffix(".wasm").is_file())
        menu_script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", menu_script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        names = sorted(set(menu_names) | set(selected_names))
        self.assertEqual((len(menu_names), len(selected_names), len(names)), (76, 36, 98))
        paths = {name: (menu / name if (menu / name).is_file() else game / name)
                 for name in names}
        self.assertTrue(all(path.is_file() for path in paths.values()))
        before = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for name, path in paths.items()}
        source = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-source-text-lifetime.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source, "stadium-source-ready-text-membership-v1"]
        output = self.scratch / "stadium-source-text-node-owner"
        output.mkdir(exist_ok=False)
        identity = {
            "scope": "stadium-source-ready-text-membership-v1",
            "cleanup_scope": "stadium-source-text-lifetime-v1",
            "ownership": "direct-Popen", "source_revision": source,
            "source_tree": subprocess.check_output(
                ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip(),
            "argv": command, "cwd": str(ROOT), "timeout_seconds": 120,
            "fixture_sha256_before": before,
            "binary_sha256": {
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [Path(command[0]).resolve(), target, target.with_suffix(".wasm")]},
        }
        stdout_path = self.scratch / "stadium-source-text.stdout"
        stderr_path = self.scratch / "stadium-source-text.stderr"
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
                try:
                    identity["pid"] = process.pid
                    with (output / "identity.json").open("x", encoding="utf-8") as receipt:
                        receipt.write(json.dumps(identity, indent=2) + "\n")
                    process.wait(timeout=120)
                finally:
                    cleanup_process(
                        process, output, scope="stadium-source-text-lifetime-v1")
        finally:
            after = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                     for name, path in paths.items()}
            with (output / "fixture-after.json").open("x", encoding="utf-8") as receipt:
                receipt.write(json.dumps(after, indent=2) + "\n")
            self.assertEqual(after, before,
                             "Real Stadium fixture changed during Ready text observation")
        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
        self.assertEqual(process.returncode, 1, (stdout + stderr)[-12000:])
        self.assertRegex(
            stderr,
            r"STADIUM_READY_SESSION phase=construction-complete-before-source-ticks "
            r"world_exists=1 world=\d+ ticks=0 ")
        self.assertIn(
            "STADIUM_READY_TEXT_MEMBERSHIP phase=probe-armed tick_cap=600 draw=0", stderr)
        self.assertRegex(
            stderr,
            r"STADIUM_READY_SESSION phase=first-ready-before-close world_exists=1 "
            r"world=\d+ ticks=\d+ ")
        self.assertIn("STADIUM_READY_SESSION_FIRST_FAILURE", stderr)
        first_failure = stderr.index("STADIUM_READY_SESSION_FIRST_FAILURE")
        self.assertRegex(stderr[first_failure:], r"match_retained=1 pre_OnStart_close_unsupported=1")
        self._assert_stadium_source_text_events(
            stdout, stderr,
            probe_marker="STADIUM_READY_TEXT_MEMBERSHIP phase=probe-armed",
            first_failure_marker="STADIUM_READY_SESSION_FIRST_FAILURE",
            observation_path=self.scratch / "stadium-source-ready-text-observation.json",
            scope="600-tick original Ready route then first normal Session.close refusal",
            require_membership=True)

    def test_stadium_source_world_lifecycle_one_shot(self):
        if os.environ.get("MELEE_RUN_STADIUM_SOURCE_WORLD_LIFECYCLE") != "1":
            self.skipTest("Real Stadium two-world continuation requires its reviewed run gate")
        import hashlib
        from capture_sd_reference_prefix import cleanup_process

        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        fixture_value = os.environ.get("MELEE_MENU_FIXTURE_ROOT")
        self.assertTrue(fixture_value, "Retained Stadium fixture root is required")
        fixture = Path(fixture_value)
        self.assertTrue(fixture.is_absolute(), "Use the frozen absolute fixture root")
        menu, game = fixture / "native-menus", fixture / "next-gate"
        self.assertTrue(target.is_file())
        menu_script = (
            "import {NATIVE_MENU_DISC_FILES} from './web/runtime-assets.mjs'; "
            "console.log(JSON.stringify([...Object.keys(NATIVE_MENU_DISC_FILES), "
            "'dsp_coef.bin', 'sislib_font.bin']))"
        )
        menu_names = json.loads(subprocess.check_output(
            [str(node_runtime()), "--input-type=module", "-e", menu_script],
            cwd=ROOT, text=True))
        selected_names = stadium_c1_selected_file_names()
        names = sorted(set(menu_names) | set(selected_names))
        self.assertEqual((len(menu_names), len(selected_names), len(names)), (76, 36, 98))
        paths = {name: (menu / name if (menu / name).is_file() else game / name)
                 for name in names}
        self.assertTrue(all(path.is_file() for path in paths.values()))
        before = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                  for name, path in paths.items()}
        source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        trace = self.scratch / "stadium-source-world-lifecycle.jsonl"
        command = [str(node_runtime()), str(target), str(menu), str(game), "3",
                   str(trace), source, "stadium-source-world-lifecycle-v1"]
        output = self.scratch / "stadium-source-world-node-owner"
        output.mkdir(exist_ok=False)
        identity = {
            "scope": "stadium-source-world-lifecycle-v1", "ownership": "direct-Popen",
            "source_revision": source,
            "source_tree": subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"],
                                                     cwd=ROOT, text=True).strip(),
            "argv": command, "cwd": str(ROOT), "timeout_seconds": 120,
            "fixture_sha256_before": before,
            "binary_sha256": {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in [Path(command[0]).resolve(), target,
                                           target.with_suffix(".wasm")]},
        }
        stdout_path = self.scratch / "stadium-source-world.stdout"
        stderr_path = self.scratch / "stadium-source-world.stderr"
        try:
            with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
                process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
                try:
                    identity["pid"] = process.pid
                    with (output / "identity.json").open("x", encoding="utf-8") as receipt:
                        receipt.write(json.dumps(identity, indent=2) + "\n")
                    process.wait(timeout=120)
                finally:
                    cleanup_process(process, output, scope="stadium-source-world-lifecycle-v1")
        finally:
            after = {name: hashlib.sha256(path.read_bytes()).hexdigest()
                     for name, path in paths.items()}
            with (output / "fixture-after.json").open("x", encoding="utf-8") as receipt:
                receipt.write(json.dumps(after, indent=2) + "\n")
            self.assertEqual(after, before, "Real Stadium fixture changed during continuation")
        stdout, stderr = stdout_path.read_text(), stderr_path.read_text()
        self.assertEqual(process.returncode, 0, (stdout + stderr)[-12000:])
        rows = [json.loads(line) for line in stdout.splitlines() if line.startswith('{"probe":')]
        started = [row for row in rows if row["probe"] == "stadium-source-start-retired"]
        pending = [row for row in rows if row["probe"] == "stadium-owned-world-before-close"]
        closed = [row for row in rows if row["probe"] == "stadium-owned-world-closed"]
        self.assertEqual([row["lifetime"] for row in pending], [0, 1])
        self.assertEqual([row["lifetime"] for row in closed], [0, 1])
        self.assertEqual(len(started), 2)
        self.assertEqual([row["world"] for row in started], [row["world"] for row in pending])
        self.assertEqual([row["retired_world"] for row in closed], [row["world"] for row in pending])
        self.assertGreater(pending[1]["world"], pending[0]["world"])
        self.assertEqual(pending[1]["fresh_heap"], pending[0]["fresh_heap"])
        self.assertEqual(closed[1]["session_identity"], closed[0]["session_identity"])
        self.assertTrue(all(row["inactive"] for row in closed))
        self.assertTrue(all(row["onload_onstart_called"] and row["checked_retirement"]
                            and row["ticks"] == 0 for row in started))
        self.assertTrue(all(row["objects"] == row["processes"] == row["ticks"] == 0
                            for row in pending))
        self.assertNotIn('"probe":"stadium-source-oninit"', stdout)
        self.assertIn("Ready/GO and ticks remain unrun", stdout)
        events = [row["event"] for row in map(json.loads, trace.read_text().splitlines())
                  if row.get("record") == "event"]
        self.assertEqual(events, ["stadium_source_oninit_returned",
                                  "stadium_source_onstart_returned",
                                  "stadium_source_oninit_cleaned"] * 2)

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
        # This one-shot owns only its directly launched Node child.  Fresh receipt
        # paths preserve prior evidence, including a timeout's actual output.
        import hashlib
        from capture_sd_reference_prefix import cleanup_process

        owner_output = self.scratch / "stadium-source-oninit-node-owner"
        owner_output.mkdir(exist_ok=False)
        binary_paths = [Path(command[0]).resolve(), target, target.with_suffix(".wasm")]
        identity = {
            "scope": "stadium-source-oninit-v1",
            "ownership": "direct-Popen",
            "source_revision": source_revision,
            "source_tree": subprocess.check_output(
                ["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip(),
            "argv": command,
            "cwd": str(ROOT),
            "binary_sha256": {
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in binary_paths
            },
            "timeout_seconds": 120,
        }
        stdout_path = self.scratch / "stadium-source-oninit.stdout"
        stderr_path = self.scratch / "stadium-source-oninit.stderr"
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            process = subprocess.Popen(command, cwd=ROOT, stdout=stdout, stderr=stderr)
            try:
                identity["pid"] = process.pid
                with (owner_output / "identity.json").open("x", encoding="utf-8") as receipt:
                    receipt.write(json.dumps(identity, indent=2) + "\n")
                process.wait(timeout=120)
            finally:
                cleanup_process(process, owner_output, scope="stadium-source-oninit-v1")
        run = subprocess.CompletedProcess(
            command, process.returncode,
            stdout_path.read_text(encoding="utf-8"),
            stderr_path.read_text(encoding="utf-8"))
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
