"""Exact long-B original reference operands; never native/browser admission.

Reuses existing transport, whole-session, typed endpoint, constructor/state,
consumed-port and compact-ledger readers. Old2013/943 adapter remains unchanged.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(root / "scripts"), str(root / "reference-capture/dolphin")]

import stadium_later_b_reference as reference
import stadium_current_context_packet as context
from stadium_go_prefix import (CONSTRUCTOR_POINTS, StadiumConstructorObservations,
    _validate_progress, _validate_stadium_idle_state_record)
from stadium_rng_compact_live import decode_live_compact
from stadium_whole_long_idle import LongIdleBObservations, PROFILE
from retail_setup_validation import _decode_setup, _validate_expected_setup

SCHEMA = "melee-web-stadium-later-b-long-reference-operands-v1"
PROJECTION_SCHEMA = "melee-web-stadium-later-b-long-reference-projection-v1"
REFERENCE_ID = "original-b-5422-951-long-v1"
RECIPE_SHA256 = "e669f831538b90e39241a569b6c38884300bfe246b252cacda994cbc45c10bbf"
PREFIX = 951
FRAMES = 5422
ROLES = reference.ROLES | {"compact_rng"}
require = reference.require


def verified_files(binding):
    require(set(binding) == {"schema", "files"} and binding["schema"] == SCHEMA and
            set(binding["files"]) == ROLES, "unknown long-reference schema/roles")
    # Reuse the unchanged eight-role raw pin reader without invoking its
    # incompatible fixed2013 derivation or its one-tick/one-draw context adapter.
    paths = reference.verified_files({"schema": reference.SCHEMA,
        "files": {k: binding["files"][k] for k in reference.ROLES}})
    pin = binding["files"]["compact_rng"]
    require(set(pin) == {"path", "bytes", "sha256"}, "malformed compact operand pin")
    path = Path(pin["path"])
    require(path.is_file() and not path.is_symlink(), "compact operand missing/symlinked")
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    require(path.stat().st_size == pin["bytes"] and digest == pin["sha256"], "changed compact operand")
    paths["compact_rng"] = path
    return paths


def endpoint(row, entry, capture, expected, match):
    item, raw = context.replay._slice(row, "match_setup", context.replay.GAME_INFO_SIZE, "endpoint")
    require(item["address"] == context.replay._gpr(entry["payload"], 3, "Entry") and
            raw.hex() == capture["setup_hexes"][match] and _decode_setup(raw.hex()) == expected,
            "actual Entry/Setup owner/full Start payload differs")
    return {**context.coordinate(row), "start_melee_hex": raw.hex(),
            "start_melee_sha256": hashlib.sha256(raw).hexdigest(),
            "declared_setup": _decode_setup(raw.hex()), "rng": context.seed(row)}


def source_actions(rows, setup_seq, close_seq, frames):
    """Literal variable source batches: no manufactured initial or missing draw."""
    actions = []; pending = None; drawing = None; ticks = draws = 0
    for row in rows:
        p = row.get("payload", {}); kind = p.get("boundary")
        if row["event"] != "boundary" or p.get("match_index") != 1 or not setup_seq < row["seq"] <= close_seq:
            continue
        if kind not in ("pad_consume", "source_tick", "draw_enter", "draw_return", "pad_poll"):
            raise ValueError("selected action interval crossed a lifecycle boundary")
        require(p.get("whole_session") is True and
                context.replay.semantics.BOUNDARIES.get(p["pc"]) == kind,
                "source boundary lost original PC/whole owner")
        if kind == "pad_consume":
            require(pending is None and drawing is None, "repeated/nested consumed PAD")
            pending = row
        elif kind == "source_tick":
            require(pending is not None and drawing is None and row["source_tick"] == ticks and
                    pending["source_tick"] == ticks and row["draw_ordinal"] == draws == pending["draw_ordinal"],
                    "source tick/consume/draw coordinates differ")
            item, raw = context.replay._slice(row, "scene_frame", 4, "SourceTick")
            require(item["address"] == 0x80479D58 and int.from_bytes(raw,"big") == ticks,
                    "SourceTick differs from actual scene-frame owner")
            ports = context.replay._consumed_ports(pending, pending["seq"])
            frame = frames[PREFIX + ticks]
            require(frame["source_seq"] == pending["seq"] and frame["pads"] == ports and frame["scene"] == "match",
                    "action input escaped exact global four-port corpus")
            actions.append({"action_index":len(actions),"kind":"tick","bundle":ticks,
                "draws_before":draws,"pads":ports,"consume":context.coordinate(pending),
                "source":context.coordinate(row)})
            ticks += 1; pending = None
        elif kind == "draw_enter":
            require(pending is None and drawing is None and ticks > 0 and
                    row["source_tick"] == ticks and row["draw_ordinal"] == draws,
                    "DrawEnter differs from actual source batch")
            drawing = row
        elif kind == "draw_return":
            require(drawing is not None and pending is None and row["source_tick"] == ticks and
                    row["draw_ordinal"] == draws == drawing["draw_ordinal"] and
                    row["source_tick"] == drawing["source_tick"], "unpaired DrawReturn")
            actions.append({"action_index":len(actions),"kind":"draw","draw":draws,
                "ticks_before":ticks,"draw_enter":context.coordinate(drawing),"source":context.coordinate(row)})
            draws += 1; drawing = None
    require(pending is None and drawing is None and actions and actions[-1]["kind"] == "draw",
            "incomplete natural action interval")
    return actions



def action_rng_projection(actions, compact):
    """Map each actual return to its literal source-call boundary bracket.

    Cursor ranges are half-open ordinal ranges into the pinned compact stream.
    They preserve empty actions and raw event order. A return outside a real
    consume→SourceTick or DrawEnter→DrawReturn bracket refuses, never guessed.
    """
    records = compact["records"]; window = compact["windows"][0]
    first_action = next(a["action_index"] for a in actions if a["kind"] == "tick" and
                        a["source"]["sequence"] == window["containing_source_sequence"])
    last_action = next(a["action_index"] for a in actions if
                       a["source"]["sequence"] == window["close_sequence"])
    require(first_action == 168 and last_action == 7167,"actual per-action RNG extent differs")
    rows = []; cursor = 0
    for action in actions[first_action:last_action+1]:
        lower = (action["consume"] if action["kind"] == "tick" else action["draw_enter"])["sequence"]
        upper = action["source"]["sequence"]
        require(lower < upper,"action has no actual source-call bracket")
        begin = cursor; selected = []
        while cursor < len(records) and records[cursor]["observer_next_sequence"] <= upper:
            event = records[cursor]
            require(event["rng_ordinal"] == cursor and lower < event["observer_next_sequence"] <= upper,
                    "RNG return is outside actual action boundary bracket")
            require(event["match"] == 1 and event["source_tick"] == action["source"]["source_tick"] and
                    event["draw_ordinal"] == action["source"]["draw_ordinal"],
                    "RNG return action source coordinates differ")
            if window["containing_source_sequence"] < event["observer_next_sequence"] <= window["target_source_sequence"]:
                selected.append(cursor)
            cursor += 1
        if selected:
            require(selected == list(range(selected[0],selected[-1]+1)),"selected RNG action subset is not contiguous")
        selected_range = [selected[0],selected[-1]+1] if selected else [cursor,cursor]
        rows.append({"action_index":action["action_index"],"kind":action["kind"],
            "source_coordinate":{"source_tick":action["source"]["source_tick"],
                                 "draw_ordinal":action["source"]["draw_ordinal"]},
            "original_boundary_bracket":{"after_sequence":lower,"through_sequence":upper},
            "event_ordinals":[begin,cursor],"selected_event_ordinals":selected_range,
            "event_count":cursor-begin,"selected_event_count":len(selected)})
    require(cursor == len(records) and len(rows) == 7000,"unassigned RNG return or missing actual action")
    return {"selector":{"schema":"melee-web-later-b-long-action-rng-v1","reference_id":REFERENCE_ID,
                "prefix_cursor":PREFIX,"reference_go_tick":window["go_tick"],
                "first_action":first_action,"last_action":last_action},
            "row_schema":"melee-web-stadium-long-b-action-rng-row-v1",
            "coordinate_contract":"actual local SourceTick/DrawReturn header pair; full original provenance retained separately",
            "event_range_contract":"half-open original rng_ordinal ranges; no events dropped, empty actions retained",
            "rows":rows,"record_count":cursor,"selected_record_count":sum(r["selected_event_count"] for r in rows),
            "native_parity":False}


def _build_long_b_projection(binding_path):
    raw_binding, identity = reference._read_binding_snapshot(binding_path)
    binding = json.loads(raw_binding)
    paths = verified_files(binding)
    launch = json.loads(paths["launch"].read_text()); report = json.loads(paths["report"].read_text())
    require(report["result"] == "pass" and report["cleanup_errors"] == [] and
            report["native"]["primary_sha256"] == binding["files"]["primary"]["sha256"] and
            report["input_stream"]["sha256"] == binding["files"]["si"]["sha256"], "same-run raw/report joins differ")
    for role, key in (("setup_receipt","setup"),("producer_manifest","manifest")):
        require(launch[key]["sha256"] == binding["files"][role]["sha256"] and
                Path(launch[key]["path"]) == paths[role], "same-run producer/setup role differs")
    compact_launch = launch["stadium_whole_long_idle_compact_launch"]
    require(launch["stadium_whole_long_idle"] == PROFILE and compact_launch["matches"] == [1] and
            compact_launch["output_absent_at_prepare"] is True and
            Path(compact_launch["output_path"]) == paths["compact_rng"] and
            launch.get("input_mode","record") == "record", "long profile is not a fresh selected-B record")
    capture = context.replay.capture_from_path(paths["primary"], paths["primary_status"])
    encoded, _ = context.replay.encode_v8(capture)
    require(hashlib.sha256(encoded).hexdigest() == RECIPE_SHA256 and len(encoded) == 262226 and
            len(capture["frames"]) == FRAMES and capture["spans"][6]["first_frame"] == PREFIX,
            "not the exact5422/951 long reference corpus")
    expected = _validate_expected_setup(launch["expected"])
    require(capture["declared_setups"] == [expected]*3 and len(set(capture["setup_hexes"])) == 1,
            "three original typed/raw setups differ")
    events = reference.si.validate_stream(paths["si"])
    reference.si.validate_status(paths["si_status"],mode="record",events=events["events"],require_complete=True)
    require(events == report["input_stream"], "reported input stream differs from actual recording")
    rows = list(context.replay.iter_records(paths["primary"]))
    reducer = LongIdleBObservations()
    for row in rows: reducer.accept(row)
    window = reducer.finish()
    require(window == report["stadium_whole_long_idle_source_validation"], "reported long window differs from raw")
    by_seq = {row["seq"]:row for row in rows}
    matches = []
    for index in range(3):
        selected = {}
        for kind in ("entry","setup","vs_exit","vs_exit_return"):
            found = [r for r in rows if r["event"] == "boundary" and
                r["payload"].get("match_index") == index and r["payload"].get("boundary") == kind]
            require(len(found) == 1,"missing/duplicate original lifecycle endpoint")
            selected[kind] = found[0]
        matches.append({"match_index":index,
            "entry":endpoint(selected["entry"],selected["entry"],capture,expected,index),
            "setup":endpoint(selected["setup"],selected["entry"],capture,expected,index),
            "exit":context.coordinate(selected["vs_exit"]),
            "exit_return":context.coordinate(selected["vs_exit_return"])})
    # The short A prefix retains full draw slices and its old strict context
    # reader. Do not apply that reader to long B variable batches/omitted raw.
    matches[0]["steps"] = context.match_steps(rows,0,by_seq[matches[0]["setup"]["sequence"]],
                                             by_seq[matches[0]["exit"]["sequence"]])
    entry, setup = matches[1]["entry"], matches[1]["setup"]
    constructor = StadiumConstructorObservations(); go = None
    for row in rows:
        p = row.get("payload",{})
        if row["event"] != "progress" or p.get("match_index") != 1: continue
        phase = p.get("phase")
        if phase in CONSTRUCTOR_POINTS:
            require(entry["sequence"] < row["seq"] < setup["sequence"],"constructor escaped Entry/Setup")
            constructor.accept(row,route_ready=True,scope="stadium_whole3_feedback_v1",
                expected_receipt=launch["setup"]["sha256"],match_index=1)
        elif phase == "go_after":
            require(go is None and setup["sequence"] < row["seq"],"duplicate/early actual GO")
            _validate_progress(row,phase,launch["setup"]["sha256"],scope="stadium_whole3_feedback_v1",match_index=1)
            go = row
    require(go is not None and go["seq"] == window["go_sequence"],"actual GO owner differs")
    construction = constructor.finish(include_state_projection=True)
    shared = construction["state_projection"]
    require(shared["seed_before_hex"] == entry["rng"]["value_hex"],"constructor parent seed differs")
    actions = source_actions(rows,setup["sequence"],window["close"]["sequence"],capture["frames"])
    require(len(actions) == 7168 and sum(a["kind"] == "tick" for a in actions) == 3585 and
            sum(a["kind"] == "draw" for a in actions) == 3583,"actual bounded action dimensions differ")
    tick_actions = {a["bundle"]:a for a in actions if a["kind"] == "tick"}
    require(tick_actions[go["source_tick"]]["consume"]["sequence"] < go["seq"] <
            tick_actions[go["source_tick"]]["source"]["sequence"],"GO escaped its real consume/tick bracket")
    states = []
    for tick in range(go["source_tick"],go["source_tick"]+3501):
        row = by_seq[tick_actions[tick]["source"]["sequence"]]
        states.append({"tick":context.coordinate(row),"state":_validate_stadium_idle_state_record(row,"IDLE",tick),
                       "rng":context.seed(row)})
    require([x["state"] for x in states] == report["stadium_b_observation"]["source_tick_states"][:3501],
            "reported typed state prefix differs from raw")
    primary_status = json.loads(paths["primary_status"].read_text())
    compact = decode_live_compact(paths["compact_rng"].read_bytes(),capture_id=capture["identity"]["capture_id"],
        sequence_id=capture["identity"]["sequence_id"],dol_sha256=capture["identity"]["dol_sha256"],primary_status=primary_status)
    require(compact.get("valid") is True,"compact strict decoder refused")
    reported_compact = {k:v for k,v in compact.items() if k not in ("records","valid")}
    reported_compact["output_path"] = str(paths["compact_rng"])
    require(reported_compact == report["stadium_whole_long_idle_compact_rng"], "reported compact validation differs from raw")
    require(len(compact["windows"]) == 1 and compact["header"]["matches"] == [1],"compact selected owner differs")
    w = compact["windows"][0]
    require((w["go_sequence"],w["containing_source_sequence"],w["target_source_sequence"],w["close_sequence"]) ==
        (go["seq"],tick_actions[go["source_tick"]]["source"]["sequence"],
         tick_actions[go["source_tick"]+3500]["source"]["sequence"],window["close"]["sequence"]),
        "compact/full-primary GO/target/close join differs")
    go_rng = reference._named_seed(go)
    require(w["seed_pointer"] == int(go_rng["pointer_hex"],16) and w["seed"] == int(go_rng["value_hex"],16),
            "compact actual GO seed owner/value differs")
    inventory = collections.Counter()
    for event in compact["records"]:
        owner = event["owner"]
        inventory[(owner["status"],owner["callback_symbol"],owner["source_slot"],owner["entity"],owner["fighter_kind"])] += 1
    owners = [{"status":k[0],"callback_symbol":k[1],"source_slot":k[2],"entity":k[3],"fighter_kind":k[4],"events":n}
              for k,n in sorted(inventory.items(),key=lambda item:repr(item[0]))]
    literal = [{**frame,**context.coordinate(by_seq[frame["source_seq"]])} for frame in capture["frames"]]
    a_receipts = [x for x in report["actual_inputs"] if x["match_index"] == 0]
    require(len(a_receipts) == 1,"ambiguous preceding A input receipt")
    a_pause = a_receipts[0]
    pause_first = capture["spans"][2]["first_frame"] + a_pause["actual_pause_chord"]["source_tick"]
    pause_last = capture["spans"][2]["first_frame"] + a_pause["actual_chord"]["source_tick"]
    for name in ("actual_pause_chord","actual_pause_release","actual_chord"):
        note = a_pause[name]
        require(context.replay._consumed_ports(by_seq[note["sequence"]],note["sequence"]) == note["ports"],
                "preceding pause receipt differs from raw consume")
    pause_rows = []
    for step in matches[0]["steps"]:
        global_cursor = capture["spans"][2]["first_frame"] + step["tick"]["source_tick"]
        if pause_first <= global_cursor <= pause_last:
            row = by_seq[step["tick"]["sequence"]]
            item, raw = context.replay._slice(row,"match_clock",0x2e,"preceding pause clock")
            require(item["address"] == 0x8046b6a0,"preceding pause clock owner differs")
            pause_rows.append({"global_cursor":global_cursor,"source":context.coordinate(row),
                "pauser":int.from_bytes(raw[1:2],"big",signed=True),"pause_timer":raw[2],
                "outcome":raw[8],"frame_count":int.from_bytes(raw[0x24:0x28],"big")})
    require(pause_rows and len(pause_rows) == pause_last-pause_first+1,"preceding pause clock interval incomplete")
    zero_cursor = next(x["global_cursor"] for x in pause_rows if x["pause_timer"] == 0)
    sss = [r for r in rows if r["event"] == "boundary" and
           r["payload"].get("match_index") == 1 and r["payload"].get("boundary") == "sss_exit"]
    require(len(sss) == 1,"ambiguous selected SSS exit")
    descriptor = {"id":REFERENCE_ID,"profile":4,"recipe_sha256":RECIPE_SHA256,
        "recipe_bytes":len(encoded),"frames":FRAMES,
        "first":[span["first_frame"] for span in capture["spans"]],
        "last":[span["last_frame"] for span in capture["spans"]],
        "pause_first":pause_first,"pause_last":pause_last,"pause_frame":pause_rows[0]["frame_count"],
        "pause_timer_zero_cursor":zero_cursor,
        "menu_ticks":sum(span["last_frame"]-span["first_frame"]+1 for span in capture["spans"][:6] if span["scene"] in (1,2)),
        "notification_order":None,"notification_order_contract":"captured_native_complete_trace_not_original_draw_ordinal",
        "sss_sequence":sss[0]["seq"],"sss_tick":sss[0]["source_tick"],"sss_draw":sss[0]["draw_ordinal"],
        "entry_sequence":entry["sequence"],"setup_sequence":setup["sequence"],
        "entry_seed":entry["rng"]["value_hex"],"setup_seed":setup["rng"]["value_hex"],
        "a_entry_sequence":matches[0]["entry"]["sequence"],"a_setup_sequence":matches[0]["setup"]["sequence"],
        "a_tick":matches[0]["entry"]["source_tick"],
        "start_sha256":entry["start_melee_sha256"],
        "a_entry_seed":matches[0]["entry"]["rng"]["value_hex"],"a_setup_seed":matches[0]["setup"]["rng"]["value_hex"]}
    partitions = dict(collections.Counter(x["coordinate_scope"] for x in compact["records"]))
    current = {"first_css":capture["first_css"],"identity":capture["identity"],"lifecycle":capture["lifecycle"],
        "inputs":literal,"spans":capture["spans"],"matches":matches,"runtime_admission":False}
    result = {"schema":PROJECTION_SCHEMA,"reference_id":REFERENCE_ID,
        "binding_sha256":hashlib.sha256(raw_binding).hexdigest(),"source_binding":binding,
        "recipe_sha256":RECIPE_SHA256,"recipe_bytes":len(encoded),"recipe_frames":FRAMES,"recipe_hex":encoded.hex(),
        "selected_match":1,"setup_global_cursor":PREFIX,"whole_session_context":current,
        "reference_descriptor":descriptor,"preceding_pause_post_tick_clock":pause_rows,
        "menu_prefix":{"scope":"literal global prefix, not local source ticks","input_count":PREFIX,
                       "inputs":literal[:PREFIX],"pads_sha256":hashlib.sha256(bytes.fromhex(''.join(p for f in literal[:PREFIX] for p in f['pads']))).hexdigest()},
        "source_spans":capture["spans"],"entry":entry,"setup":setup,"actions":actions,
        "go":{"coordinate":reference._progress_coordinate(go),"state":_validate_stadium_idle_state_record(go,"GO",go['source_tick']),"rng":go_rng},
        "states":states,"natural_terminal":{"target_source_tick":go["source_tick"]+3500,
            "first_later_natural_draw_return":window["close"],"natural_tail_ticks":window["natural_tail_ticks"]},
        "constructor_shared_projection":{"scope":"original-and-native-shared-aggregate-only","entry_count":shared["constructor_entry_count"],
            "parameter_roles":{"min_parameter":"parameter_x0","max_parameter":"parameter_x4"},
            "min_parameter":shared["parameter_x0"],"max_parameter":shared["parameter_x4"],"random_bound":shared["random_bound"],
            "random_result":shared["random_result"],"seed_before":shared["seed_before_hex"],"seed_after":shared["seed_after_hex"]},
        "constructor_original_witnesses":{"scope":"original-only phase order, no native dispatch pairing",
            "coordinates":[reference._progress_coordinate(by_seq[q]) for q in construction["source_sequences"]]},
        "action_rng":action_rng_projection(actions,compact),
        "compact_rng":{"source":binding["files"]["compact_rng"],"validated":{k:v for k,v in compact.items() if k!='records'},
            "semantic_owner_inventory":owners,"actual_sequence_scope_counts":partitions,"ordered_events_available_from":"unchanged strict compact decoder; raw pin retained, not summarized into parity"},
        "same_run_operand_joins_verified":True,"producer_approval_verified":False,
        "runtime_admission":False,"browser_parity":False,"per_owner_rng_parity":False,"C3":False}
    final_raw, final_identity = reference._read_binding_snapshot(binding_path)
    require(final_raw == raw_binding and final_identity == identity,"binding changed during projection")
    verified_files(binding)
    return result, rows, encoded


def build_long_b_projection(binding_path):
    # Existing public API/serialization unchanged; private return reuses the
    # one decoded source for the new Gate1 packet, never caller supplied data.
    return _build_long_b_projection(binding_path)[0]


def build_gate1_transport(binding_path):
    """Strict rawbound expected packet plus exact existing MWRCv8 bytes.

    No API accepts a mutable caller projection as authority. The same source
    rows/encoded recipe from strict long derivation feed existing SSS helpers.
    """
    from stadium_current_sss_exit_expected import (project_sss_expected_from_packet,
                                                  attach_stock4_prepared_profile)
    projected, rows, encoded = _build_long_b_projection(binding_path)
    binding = projected["source_binding"]
    current = projected["whole_session_context"]
    descriptor = projected["reference_descriptor"]
    topology = [(span["scene"],span["first_frame"],span["last_frame"])
                for span in projected["source_spans"]]
    value = project_sss_expected_from_packet(binding,current,rows,match_index=1,later_topology=topology)
    value = attach_stock4_prepared_profile(value,current,match_index=1,frames=FRAMES)
    require(value["recipe"] == {"sha256":RECIPE_SHA256,"bytes":len(encoded),"frames":FRAMES,"version":8} and
            value["notification_cursor"] == PREFIX and
            value["source"]["sequence"] == descriptor["sss_sequence"] and
            value["prepared_profile"]["entry"]["sequence"] == descriptor["entry_sequence"] and
            value["prepared_profile"]["setup"]["sequence"] == descriptor["setup_sequence"] and
            value["prepared_profile"]["entry"]["rng"]["value_hex"] == descriptor["entry_seed"] and
            value["prepared_profile"]["setup"]["rng"]["value_hex"] == descriptor["setup_seed"],
            "long Gate1 expected endpoints differ from strict source descriptor")
    # Canonical shell Context omits the separate shared Start hash and adds
    # its explicit route provenance. Preserve the raw projection unchanged.
    packet_descriptor = {k:v for k,v in descriptor.items() if k != "start_sha256"}
    packet_descriptor.update(handoff_origin="current_sss_prepared_later_951",
                             original_B_constructor_GO="REFERENCE_OBSERVED_BROWSER_UNPAIRED")
    value.update(schema="melee-web-stock4-none-later-sss-reference-expectation-v1",
        observation_profile=4,selected_occurrence=1,preceding_spans=current["spans"][:6],
        menu_session_ticks=descriptor["menu_ticks"],preceding_profile=current["matches"][0],
        reference_context=packet_descriptor,reference_order_contract=descriptor["notification_order_contract"],
        original_B_constructor_GO="REFERENCE_OBSERVED_BROWSER_UNPAIRED",
        preceding_pause_post_tick_clock=projected["preceding_pause_post_tick_clock"],runtime_admission=False)
    verified_files(binding)
    return value, encoded


def build_gate1_expected(binding_path):
    return build_gate1_transport(binding_path)[0]


def export_gate1_packet(binding_path, output):
    """Create one absent transport directory after all strict reads succeed."""
    binding_raw,binding_identity = reference._read_binding_snapshot(binding_path)
    expected,encoded = build_gate1_transport(binding_path)
    expected_raw = serialize_projection(expected)
    require(reference._read_binding_snapshot(binding_path) == (binding_raw,binding_identity),
            "binding changed during Gate1 transport derivation")
    directory = Path(output)
    require(not directory.exists() and not directory.is_symlink(),"Gate1 transport output must be absent")
    directory.mkdir()
    with (directory/"recipe.mwrc").open("xb") as stream:stream.write(encoded)
    with (directory/"expected.json").open("xb") as stream:stream.write(expected_raw)
    value = {"schema":"melee-web-later-b-long-gate1-transport-v1","reference_id":REFERENCE_ID,
        "source_binding":{"bytes":len(binding_raw),"sha256":hashlib.sha256(binding_raw).hexdigest()},
        "recipe":{"path":"recipe.mwrc","bytes":len(encoded),"sha256":hashlib.sha256(encoded).hexdigest(),
                  "frames":FRAMES,"version":8},
        "expected":{"path":"expected.json","bytes":len(expected_raw),"sha256":hashlib.sha256(expected_raw).hexdigest()},
        "runtime_admission":False}
    verified_files(expected["source_binding"])
    require(reference._read_binding_snapshot(binding_path) == (binding_raw,binding_identity),
            "binding changed during Gate1 transport export")
    with (directory/"transport.json").open("xb") as stream:stream.write(serialize_projection(value))
    return value


MAX_SERIALIZED_PROJECTION_BYTES = 64 * 1024 * 1024


def serialize_projection(value):
    return (json.dumps(value,separators=(",",":")) + "\n").encode("utf-8")


def admit_serialized_projection(binding_path, projection_path):
    """Exact raw regeneration admission only, never mutable digest authority."""
    binding_raw, binding_identity = reference._read_binding_snapshot(binding_path)
    projection_raw, projection_identity = reference._read_binding_snapshot(
        projection_path,max_bytes=MAX_SERIALIZED_PROJECTION_BYTES)
    projected = build_long_b_projection(binding_path)
    require(projection_raw == serialize_projection(projected),
            "serialized projection differs from strict raw binding regeneration")
    verified_files(json.loads(binding_raw))
    require(reference._read_binding_snapshot(binding_path) == (binding_raw,binding_identity) and
            reference._read_binding_snapshot(projection_path,max_bytes=MAX_SERIALIZED_PROJECTION_BYTES) ==
            (projection_raw,projection_identity),"projection or binding changed during strict admission")
    return {"schema":"melee-web-later-b-serialized-projection-admission-v1",
            "source_binding":{"bytes":len(binding_raw),"sha256":hashlib.sha256(binding_raw).hexdigest()},
            "projection":{"bytes":len(projection_raw),"sha256":hashlib.sha256(projection_raw).hexdigest()},
            "runtime_admission":False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binding",type=Path)
    parser.add_argument("--binding-sha256")
    parser.add_argument("--out",type=Path)
    parser.add_argument("--admit-serialized-projection",nargs=2,type=Path,metavar=("BINDING","PROJECTION"))
    parser.add_argument("--export-gate1-packet",nargs=2,type=Path,metavar=("BINDING","DIRECTORY"))
    args = parser.parse_args()
    if args.admit_serialized_projection is not None:
        require(args.binding is None and args.binding_sha256 is None and args.out is None and args.export_gate1_packet is None,
                "serialized admission refuses mixed export arguments")
        print(json.dumps(admit_serialized_projection(*args.admit_serialized_projection),sort_keys=True))
        return
    if args.export_gate1_packet is not None:
        require(args.binding is None and args.out is None and args.binding_sha256 is not None,
                "Gate1 export requires binding-sha256 and refuses mixed export arguments")
        binding,directory = args.export_gate1_packet
        raw,_ = reference._read_binding_snapshot(binding)
        require(hashlib.sha256(raw).hexdigest() == args.binding_sha256,"Gate1 binding CLI identity differs")
        print(json.dumps(export_gate1_packet(binding,directory),sort_keys=True))
        return
    require(args.binding is not None and args.binding_sha256 is not None and args.out is not None,
            "export requires binding, binding-sha256 and absent output")
    raw,_ = reference._read_binding_snapshot(args.binding)
    require(hashlib.sha256(raw).hexdigest() == args.binding_sha256,"binding CLI identity differs")
    value = build_long_b_projection(args.binding)
    with args.out.open("xb") as stream:stream.write(serialize_projection(value))

if __name__ == "__main__":main()
