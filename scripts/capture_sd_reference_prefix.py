#!/usr/bin/env python3
"""Owned cold-boot Rules gate or authored SD initialization prefix diagnostic.

Menu intents and bounded source predicates must be authored before launch. This runner
cannot synthesize native MWRI input, recover missed samples, or force a result.
No default menu recipe is guessed. The separate original experiment must first
verify its consumed neutral release and original Rules-ready source owner.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time
import threading

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from authored_sd_reference_plan import canonical
from retail_input_plan import load_plan, NEUTRAL_PAD, pipe_commands
from reference_versus_sequence_capture import _atomic_ini, prepare_dual_pipe, DualPipeController, ObserverTail
from sd_reference_diagnostic import RulesMenuReceiver, GciRulesMenuReceiver, SdDiagnosticError, require
from sd_original_menu_plan import validate_packet, matches
from capture_retail_replay import dolphin_command, _copy_tree
from capture_allocation_history import validate_reference_build_manifest
from reference_observer_stream import read_status
from reference_input_stream import validate_status


class BoundedIntentController(DualPipeController):
    """Scope-local pre-write limits; historical controllers remain unchanged."""
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.intent_records=0

    def write(self,port,pad,*,action):
        from original_competitive_timeout import CAPS
        # ASCII labels <=96 guarantee the existing JSON serializer's line is
        # <=512 bytes, including two escaped quotes per character and ns time.
        require(isinstance(action,str) and all(32<=ord(c)<=126 for c in action) and len(action)<=96 and
                len(pad)==22 and self.intent_records<CAPS["intent_records"],
                "Ordinary input intention identity/record cap")
        size=self.log.stat().st_size if self.log.exists() else 0
        require(size+512<=CAPS["intent_bytes"], "Ordinary input intention byte cap")
        self.intent_records+=1
        super().write(port,pad,action=action)


STADIUM_CAPTURE_CAPS = dict(observer_bytes=64*1024*1024, input_bytes=16*1024*1024,
                            observer_records=16384, intent_bytes=1024*1024,
                            intent_records=4096, log_bytes=8*1024*1024)


def check_stadium_capture_bounds(raw, native, deadline, records):
    """Bound accepted evidence and stop the owned producer on the next check."""
    require(time.monotonic() < deadline, "Stadium capture wall deadline exhausted")
    require(records <= STADIUM_CAPTURE_CAPS["observer_records"],
            "Stadium observer record cap")
    for path, key in ((raw, "observer_bytes"), (native, "input_bytes")):
        require(not path.exists() or path.stat().st_size <= STADIUM_CAPTURE_CAPS[key],
                "Stadium capture byte cap: " + key)


class StadiumIntentController(DualPipeController):
    """Pre-write intention bounds for the separate Stadium experiment only."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.intent_records = 0

    def write(self, port, pad, *, action):
        require(isinstance(action, str) and len(action) <= 96 and
                all(32 <= ord(c) <= 126 for c in action) and len(pad) == 22 and
                self.intent_records < STADIUM_CAPTURE_CAPS["intent_records"],
                "Stadium intention identity/record cap")
        size = self.log.stat().st_size if self.log.exists() else 0
        require(size + 512 <= STADIUM_CAPTURE_CAPS["intent_bytes"],
                "Stadium intention byte cap")
        self.intent_records += 1
        super().write(port, pad, action=action)


class BoundedLog:
    """Drain only the owned child's stdout; retain a hard bounded failed log."""
    def __init__(self,source,target,cap):
        self.source,self.target,self.cap=source,target,cap
        self.size=0
        self.error=None
        self.thread=threading.Thread(target=self._drain,name="ordinary-owned-log",daemon=True)
        self.thread.start()

    def _drain(self):
        try:
            while True:
                block=self.source.read(4096)
                if not block: break
                remaining=self.cap-self.size
                kept=block[:remaining]
                self.target.write(kept); self.target.flush(); self.size+=len(kept)
                if len(kept)!=len(block): self.error="Ordinary native log byte cap"
        except (OSError,ValueError) as error:
            self.error=str(error)
        finally:
            self.source.close()

    def check(self):
        require(self.error is None,self.error or "Ordinary owned log failed")

    def finish(self):
        self.thread.join(timeout=5)
        require(not self.thread.is_alive(),"Ordinary owned log drain did not finish after child cleanup")
        self.check()


def rules_dolphin_command(dolphin, user, disc):
    command = dolphin_command(Path(dolphin), Path(user), Path("unused"), Path(disc),
                              cpu="JITARM64", cold_boot=True, audible=False)
    return command + ["-p", "headless", "-C", "Session.Core.SaveDataWritable=False",
                      "-C", "Dolphin.Interface.ConfirmStop=False"]


def check_owned_native_wait(process, bounded_log):
    """Check only the direct child while a declared observer record is absent."""
    returncode = process.poll()
    require(returncode is None,
            "Owned original native child exited before observer record: returncode " + str(returncode))
    if bounded_log is not None:
        bounded_log.check()


def cleanup_process(process, output, scope="rules_ready"):
    """Stop and reap only this runner's direct Popen; always retain the outcome."""
    receipt = {"scope": scope, "pid": process.pid, "ownership": "direct-Popen",
               "terminate_sent": False, "kill_sent": False, "returncode": None, "error": None}
    try:
        if process.poll() is None:
            process.terminate()
            receipt["terminate_sent"] = True
        try:
            receipt["returncode"] = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            receipt["kill_sent"] = True
            receipt["returncode"] = process.wait(timeout=5)
    except (OSError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    finally:
        (Path(output) / "cleanup.json").write_bytes(canonical(receipt))
    require(receipt["error"] is None and receipt["returncode"] is not None,
            "Rules probe direct process cleanup failed")
    return receipt


def wait_terminal_statuses(observer, native, deadline):
    """Two independently published writers; neither status stands in for the other."""
    while time.monotonic() < deadline:
        primary = read_status(observer) if Path(observer).is_file() else None
        inputs = validate_status(native, mode="record", require_complete=False) if Path(native).is_file() else None
        if primary:
            require(not primary["invalid"] and not primary["error"], "SD observer failed during finalization")
            require(primary["state"] != "completed", "SD observer unexpectedly completed a legacy capture")
        if primary and primary["state"] == "interrupted" and inputs and inputs["complete"]:
            return
        time.sleep(0.02)
    raise SdDiagnosticError("SD independent writer finalization deadline expired")


def wait_transform_terminal_statuses(observer, native, deadline, wait_check=None):
    """Wait for independently completed MWRO and MWRI writers."""
    while time.monotonic() < deadline:
        if wait_check is not None:
            wait_check()
        primary = read_status(observer) if Path(observer).is_file() else None
        inputs = validate_status(native, mode="record", require_complete=False) if Path(native).is_file() else None
        if primary:
            require(not primary["invalid"] and primary["error"] is None,
                    "Sheik observer failed during finalization")
        if inputs:
            require(not inputs["invalid"] and inputs["error"] is None,
                    "Sheik native input writer failed during finalization")
        if primary and primary["state"] == "completed" and inputs and inputs["complete"]:
            return primary, inputs
        time.sleep(0.02)
    raise SdDiagnosticError("Sheik independent writer finalization deadline expired")


def _transform_slices(payload):
    """Decode the transform observer's typed source slices without aliasing."""
    from reference_observer_stream import SLICE_NAMES
    values = payload.get("slices")
    require(isinstance(values, list) and len(values) <= 64,
            "Transform boundary slice inventory is invalid")
    data, metadata = {}, {}
    for item in values:
        require(isinstance(item, dict) and
                set(item) == {"name", "tag", "flags", "address", "size", "hex"} and
                all(type(item[key]) is int for key in ("tag", "flags", "address", "size")),
                "Transform boundary slice identity differs")
        require(item["tag"] in SLICE_NAMES and item["name"] == SLICE_NAMES[item["tag"]],
                "Transform boundary slice name differs from its typed observer tag")
        key = (item["tag"], item["flags"])
        require(key not in data, "Transform boundary contains a duplicate source slice")
        try:
            raw = bytes.fromhex(item["hex"])
        except (TypeError, ValueError) as error:
            raise SdDiagnosticError("Transform boundary slice is not hexadecimal") from error
        require(raw.hex() == item["hex"] and len(raw) == item["size"] and len(raw) <= 0x10000 and
                0x80000000 <= item["address"] <= 0x81800000 - len(raw),
                "Transform boundary source slice bounds differ")
        data[key], metadata[key] = raw, item
    return data, metadata


def _transform_consumed_ports(payload, sequence):
    """Read all four source PAD slots and preserve the queue's exact slot identity."""
    data, metadata = _transform_slices(payload)
    require([(item["tag"], item["flags"], item["size"])
             for item in payload["slices"]] == [(2, 0, 0x0C), (3, 0, 0x30)],
            f"PAD consume {sequence} lacks the full typed queue/slot record")
    queue, slot = data[(2, 0)], data[(3, 0)]
    require(metadata[(2, 0)]["address"] == 0x804C1F78 and
            0x80000000 <= metadata[(3, 0)]["address"] < 0x81800000,
            f"PAD consume {sequence} escaped the original source queue")
    registers = payload.get("gprs")
    require(isinstance(registers, list) and len(registers) == 32,
            f"PAD consume {sequence} lacks source queue registers")
    count, base = queue[0], int.from_bytes(queue[8:12], "big")
    read_index, slot_pointer = registers[6] & 0xFF, registers[25]
    slot_address = metadata[(3, 0)]["address"]
    require(count > 0 and read_index < count and base > 0 and
            base + read_index * 0x30 == slot_pointer == slot_address,
            f"PAD consume {sequence} queue/read/slot ownership differs")
    ports = [slot[index * 12:index * 12 + 11].hex() for index in range(4)]
    errors = [byte if byte < 0x80 else byte - 0x100
              for byte in (slot[index * 12 + 10] for index in range(4))]
    return ports, errors, {"queue_base": base, "queue_count": count,
                           "queue_slot_index": read_index, "queue_slot_address": slot_address}


class SheikTransformPrefixReceiver:
    """Small menu/input adapter over the existing passive transform MWRO stream."""
    def __init__(self, plan, menus):
        from sd_original_menu_plan import route_pads
        self.plan, self.menus = plan, menus
        self.allowed_menu_pads = route_pads(menus)
        self.seq = 0
        self.ended = False
        self.menu_polls = 0
        self.menu_consumed = 0
        self.last_pad = None
        self.latest_menu = None
        self.css = None
        self.stage = None
        self.setup_seen = False
        self.setup_hex = None
        self.setup_sample = None
        self.active_source_ticks = 0
        self.last_source_tick = None
        self.source_samples = []
        self.consumed_samples = []
        self.pad_queue_records = []
        self.neutral_consume_sequence = None
        self.grounded_zelda_sequence = None
        self.down_b_consume_sequence = None
        self.down_b_consumed = False
        self.down_b_held = False
        self.down_b_released = False
        self.action_seen = False
        self.after_swap = None
        self.post_swap_neutral = None
        self._stage_identity = None
        self._stage_stable_polls = 0
        self.css_live_owner_sequence = None
        self.sss_live_owner_sequence = None

    def _pad_poll(self, row):
        from sd_reference_diagnostic import menu_state, css_state
        from sheik_transform_prefix import _menu_owner_pad_poll
        payload = row["payload"]
        data, _ = _transform_slices(payload)
        self.menu_polls += 1
        if (40, 0) in data:
            self.latest_menu = menu_state(data)
            menu_owner = _menu_owner_pad_poll(payload)
            if self.latest_menu.get("scene") == 8:
                if menu_owner == "css":
                    if self.css_live_owner_sequence is None:
                        self.css_live_owner_sequence = row["seq"]
                    css_owners = {(48, 0), (44, 0), (43, 0), (43, 1), (47, 0), (47, 1)}
                    self.css = (css_state(data, source_slots=(0, 1))
                                if css_owners <= data.keys() else None)
                else:
                    self.css = None
            else:
                self.css = None
            if self.latest_menu.get("scene") == 9 and menu_owner == "sss":
                index, kind = data.get((41, 0)), data.get((42, 0))
                metadata = { (item["tag"], item["flags"]): item
                             for item in payload["slices"] }
                if index is not None:
                    require(len(index) == 1 and index[0] <= 30 and
                            metadata[(41, 0)]["address"] == 0x804D6CAE,
                            "SSS live source index identity differs")
                    if index[0] == 30:
                        require(kind is None, "SSS random row unexpectedly has a stage owner")
                        stage_kind = None
                    else:
                        require(kind is not None and len(kind) == 1 and
                                metadata[(42, 0)]["address"] == 0x803F06D0 + index[0] * 0x1C + 0x0B,
                                "SSS live source kind identity differs")
                        stage_kind = kind[0]
                    identity = (index[0], stage_kind)
                    self._stage_stable_polls = self._stage_stable_polls + 1 if identity == self._stage_identity else 1
                    self._stage_identity = identity
                    self.stage = {"index": index[0], "kind": stage_kind,
                                  "stable_polls": self._stage_stable_polls}
                    if self.sss_live_owner_sequence is None:
                        require(self.css_live_owner_sequence is not None and
                                self.css_live_owner_sequence < row["seq"],
                                "SSS live owner preceded ordered CSS live-owner poll")
                        self.sss_live_owner_sequence = row["seq"]
                else:
                    self.stage = None
            else:
                self.stage = None

    def _pad_consume(self, row):
        from retail_input_plan import DISCONNECTED_PAD
        from sheik_transform_prefix import _validate_consumed_pad
        ports, errors, queue_record = _transform_consumed_ports(row["payload"], row["seq"])
        self.menu_consumed += 1
        self.pad_queue_records.append({"sequence": row["seq"], **queue_record})
        self.last_pad = ports
        if not self.setup_seen:
            require(ports[2:] == [DISCONNECTED_PAD, DISCONNECTED_PAD] and errors == [0, 0, -1, -1] and
                    (ports[0], ports[1]) in self.allowed_menu_pads,
                    "Original menu consumed PAD escaped its declared P1/P2 route alphabet")
            self.consumed_samples.append({"sequence": row["seq"], "phase": "menu",
                                          "ports": ports, "errors": errors})
            return
        down_b = _validate_consumed_pad(row["payload"], row["seq"])
        witness = self.plan["authored_recipe"]["input_witness"]
        require(errors == witness["expected_pad_errors"],
                "Transform input PAD error statuses differ from the authored four-port declaration")
        expected_press, expected_release = self.plan["frames"]
        require(ports == expected_press or ports == expected_release,
                "Consumed source PAD differs from exact B/Y=-80 or neutral-release witness")
        if ports == expected_press:
            require(not self.down_b_released,
                    "A second down-B episode appeared after the neutral release")
            require(down_b and self.grounded_zelda_sequence is not None and
                    self.neutral_consume_sequence is not None and
                    self.neutral_consume_sequence < self.grounded_zelda_sequence < row["seq"],
                    "Down-B consume preceded ordered neutral readiness")
            if not self.down_b_held:
                require(not self.down_b_consumed, "Down-B source episode repeated after release")
                self.down_b_consumed = True
                self.down_b_consume_sequence = row["seq"]
            self.down_b_held = True
        else:
            require(not down_b, "Neutral witness disagrees with native PAD decoder")
            if self.down_b_held:
                self.down_b_released = True
                self.down_b_held = False
            elif self.neutral_consume_sequence is None:
                self.neutral_consume_sequence = row["seq"]
        self.consumed_samples.append({"sequence": row["seq"],
                                      "phase": "down-b-held" if down_b else "neutral",
                                      "ports": ports, "errors": errors})

    def _source_tick(self, row):
        from sheik_transform_prefix import (NEUTRAL_MOTION, SHEIK_KIND, ZELDA_DOWN_B_MOTION,
                                            ZELDA_KIND, _decode_owner_sample)
        payload = row["payload"]
        tick = payload.get("source_tick")
        require(type(tick) is int and self.active_source_ticks < 600 and
                (self.last_source_tick is None and tick == 0 or
                 self.last_source_tick is not None and tick == self.last_source_tick + 1),
                "Transform source-tick sequence or 600-observation cap differs")
        self.last_source_tick = tick
        self.active_source_ticks += 1
        sample = _decode_owner_sample(payload, setup=False, sequence=row["seq"], source_tick=tick)
        active = sample["portable"]
        stable_identity_fields = ("player_entities_hex_by_slot", "gobj_pointers",
                                  "fighter_pointers", "gobj_user_data_hex",
                                  "fighter_backlink_hex")
        require(self.setup_sample is not None and
                all(sample["local_pointer_checks"][field] ==
                    self.setup_sample["local_pointer_checks"][field]
                    for field in stable_identity_fields),
                "Transform source owner pointers changed after setup")
        if active["active_kind"] == ZELDA_KIND:
            require(active["active_entity_index"] == 0 and self.after_swap is None,
                    "Zelda regained active ownership after Sheik became active")
            if (not self.down_b_consumed and self.neutral_consume_sequence is not None and
                    self.neutral_consume_sequence < row["seq"] and
                    self.grounded_zelda_sequence is None and
                    active["active_motion"] == NEUTRAL_MOTION and active["active_ground_air"] == 0):
                self.grounded_zelda_sequence = row["seq"]
        elif active["active_kind"] == SHEIK_KIND:
            require(self.down_b_consumed and self.down_b_released and self.action_seen and
                    active["active_entity_index"] == 1,
                    "Active Sheik owner preceded consumed down-B or neutral release")
            if self.after_swap is None:
                self.after_swap = sample
            elif active["active_motion"] == NEUTRAL_MOTION and active["active_ground_air"] == 0:
                self.post_swap_neutral = sample
        else:
            raise SdDiagnosticError("Active transform owner left Zelda/Sheik source identities")
        if sample["source_fields"]["motion_hex"][0] == f"{ZELDA_DOWN_B_MOTION:08x}":
            require(self.down_b_consumed,
                    "Original Zelda action 355 preceded the consumed B/Y=-80 source sample")
            self.action_seen = True
        self.source_samples.append(sample)

    def accept(self, row):
        require(not self.ended and row["seq"] == self.seq,
                "Transform observer sequence is repeated, missing, or trailing")
        self.seq += 1
        event, payload = row["event"], row["payload"]
        if event in ("handshake", "start"):
            require(payload.get("diagnostic") == "sheik_transform_prefix" and
                    not payload.get("whole_session"),
                    "Observer handshake/start escaped the declared transform-prefix profile")
            return
        if event == "error":
            raise SdDiagnosticError("Transform observer error: " + str(payload.get("error")))
        if event == "end":
            require(payload.get("status") == "completed" and payload.get("natural") is True and
                    payload.get("match_complete") is False and self.post_swap_neutral is not None,
                    "Transform observer ended before the completed grounded Sheik prefix")
            self.ended = True
            return
        require(event == "boundary" and payload.get("whole_session") is not True,
                "Unexpected event in passive transform-prefix observer stream")
        boundary = payload.get("boundary")
        if boundary == "pad_poll":
            self._pad_poll(row)
        elif boundary == "setup":
            from sheik_transform_prefix import _decode_owner_sample
            from retail_input_plan import verify_entry
            require(not self.setup_seen and self.css_live_owner_sequence is not None and
                    self.sss_live_owner_sequence is not None and
                    self.css_live_owner_sequence < self.sss_live_owner_sequence < row["seq"],
                    "VS setup preceded ordered live CSS and SSS source owner polls")
            _, metadata = _transform_slices(payload)
            setup_field = metadata.get((4, 0))
            require(setup_field is not None and setup_field["size"] == 0x138,
                    "Transform setup lacks its exact source-owned StartMeleeData")
            self.setup_hex = setup_field["hex"]
            verify_entry(self.plan, self.setup_hex)
            self.setup_sample = _decode_owner_sample(payload, setup=True,
                                                     sequence=row["seq"], source_tick=payload.get("source_tick"))
            self.setup_seen = True
        elif boundary == "pad_consume":
            self._pad_consume(row)
        elif boundary == "source_tick":
            require(self.setup_seen, "Transform source tick preceded VS setup")
            self._source_tick(row)
        elif self.setup_seen:
            raise SdDiagnosticError("Unexpected boundary inside transform-prefix source interval")

    def finish(self):
        require(self.ended and self.setup_seen and self.down_b_consumed and self.down_b_released and
                self.grounded_zelda_sequence is not None and self.after_swap is not None and
                self.post_swap_neutral is not None and self.active_source_ticks <= 600,
                "Transform receiver lacks its complete setup/input/owner prefix")
        return {
            "scope": "sheik_transform_prefix",
            "setup_sha256": self.setup_sample["setup_sha256"],
            "source_slot_map": {"pipe_lane_0": 0, "pipe_lane_1": 1,
                                "inactive_source_slots": [2, 3]},
            "menu_owner_sequences": {"css": self.css_live_owner_sequence,
                                     "sss": self.sss_live_owner_sequence,
                                     "setup": self.setup_sample["sequence"]},
            "consumed_source_pads": self.consumed_samples,
            "pad_queue_records": self.pad_queue_records,
            "source_samples": self.source_samples,
            "active_source_tick_observations": self.active_source_ticks,
            "ordered_witness_sequences": {
                "neutral_pad_consume": self.neutral_consume_sequence,
                "grounded_neutral_zelda_source_tick": self.grounded_zelda_sequence,
                "down_b_consume": self.down_b_consume_sequence,
                "neutral_release": next((sample["sequence"] for sample in self.consumed_samples
                                          if sample["phase"] == "neutral" and
                                          self.down_b_consume_sequence is not None and
                                          sample["sequence"] > self.down_b_consume_sequence), None),
                "active_sheik": self.after_swap["sequence"],
                "grounded_neutral_sheik_source_tick": self.post_swap_neutral["sequence"],
            },
        }



class StadiumGoPrefixReceiver(SheikTransformPrefixReceiver):
    """Menu/input adapter; the strict v3 validator owns raw Stadium admission."""
    def __init__(self, menus):
        super().__init__(None, menus)
        self.setup_seen = False
        self.setup_sequence = None
        self.source_tick_rows = 0
        self.draw_return_rows = 0
        self.progress_rows = []
        self.stadium_target_sequence = None
        self.target_neutral_release_sequence = None
        self.target_neutral_polls = 0
        self.confirm_sequence = None
        self.confirm_release_sequence = None
        self.consumed_samples = []

    def _pad_poll(self, row):
        super()._pad_poll(row)
        if (self.latest_menu or {}).get("scene") != 9 or self.stage is None:
            self.target_neutral_polls = 0
            return
        target = (self.stage.get("index") == self.menus["sss"]["target_index"] and
                  self.stage.get("kind") == self.menus["sss"]["target_kind"])
        if target and self.stage.get("stable_polls", 0) >= 2:
            self.stadium_target_sequence = row["seq"]
        if self.target_neutral_release_sequence is not None and row["seq"] > self.target_neutral_release_sequence:
            self.target_neutral_polls = (self.target_neutral_polls + 1
                if target and self.last_pad is not None and self.last_pad[:2] == [NEUTRAL_PAD]*2 else 0)

    def _pad_consume_stadium(self, row):
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD
        ports, errors, queue_record = _transform_consumed_ports(row["payload"], row["seq"])
        require(ports[2:] == [DISCONNECTED_PAD, DISCONNECTED_PAD] and errors == [0, 0, -1, -1],
                "Original Stadium route changed active or disconnected source ports")
        require((ports[0], ports[1]) in self.allowed_menu_pads,
                "Original Stadium consumed PAD escaped the predeclared route alphabet")
        pair = ports[:2]
        if self.setup_seen:
            require(pair == [NEUTRAL_PAD, NEUTRAL_PAD],
                    "Non-neutral source PAD was consumed after Stadium VS setup")
        self.menu_consumed += 1
        self.last_pad = ports
        sample = {"sequence": row["seq"], "phase": "post-setup-neutral" if self.setup_seen else "menu",
                  "ports": ports, "errors": errors, **queue_record}
        self.consumed_samples.append(sample)
        if (not self.setup_seen and (self.latest_menu or {}).get("scene") == 9 and
                self.stage is not None and
                self.stage.get("index") == self.menus["sss"]["target_index"] and
                self.stage.get("kind") == self.menus["sss"]["target_kind"] and
                self.target_neutral_polls >= self.menus["sss"]["target_stable_neutral_polls"] and
                pair == [raw_pad(buttons=["A"]), NEUTRAL_PAD]):
            require(self.confirm_sequence is None,
                    "Original SSS confirm was consumed more than once")
            self.confirm_sequence = row["seq"]
        elif self.confirm_sequence is not None and self.confirm_release_sequence is None and pair == [NEUTRAL_PAD]*2:
            self.confirm_release_sequence = row["seq"]
        if pair != [NEUTRAL_PAD]*2:
            self.target_neutral_polls = 0
        return sample

    def accept(self, row):
        require(not self.ended and row["seq"] == self.seq,
                "Stadium observer sequence is repeated, missing, or trailing")
        self.seq += 1
        event, payload = row["event"], row["payload"]
        from stadium_go_prefix import DIAGNOSTIC
        if event in ("handshake", "start"):
            require(payload.get("diagnostic") == DIAGNOSTIC and
                    payload.get("setup_receipt_sha256") == self.menus["setup_receipt_sha256"] and
                    payload.get("setup_profile_verified_by_observer") is False and
                    not payload.get("whole_session"),
                    "Observer handshake/start escaped the declared Stadium-prefix profile")
            return
        if event == "error":
            raise SdDiagnosticError("Stadium observer error: " + str(payload.get("error")))
        if event == "progress":
            self.progress_rows.append({"sequence": row["seq"], "phase": payload.get("phase"),
                                       "source_tick": payload.get("source_tick")})
            return
        if event == "end":
            require(payload.get("status") == "completed" and payload.get("natural") is True and
                    payload.get("setup_receipt_sha256") == self.menus["setup_receipt_sha256"] and
                    payload.get("setup_profile_verified_by_observer") is False,
                    "Stadium observer did not end naturally under its declared receipt")
            self.ended = True
            return
        require(event == "boundary" and payload.get("whole_session") is not True,
                "Unexpected event in passive Stadium observer stream")
        boundary = payload.get("boundary")
        if boundary == "pad_poll":
            self._pad_poll(row)
        elif boundary == "pad_consume":
            self._pad_consume_stadium(row)
        elif boundary == "setup":
            require(not self.setup_seen and self.css_live_owner_sequence is not None and
                    self.sss_live_owner_sequence is not None and
                    self.css_live_owner_sequence < self.sss_live_owner_sequence < row["seq"] and
                    self.stadium_target_sequence is not None and
                    self.confirm_sequence is not None and self.confirm_sequence < row["seq"],
                    "Original Stadium setup preceded CSS/SSS, selected Stadium, or consumed confirm")
            self.setup_seen = True
            self.setup_sequence = row["seq"]
        elif boundary == "source_tick":
            self.source_tick_rows += 1
        elif boundary == "draw_return":
            self.draw_return_rows += 1

    def finish(self):
        require(self.ended and self.setup_seen and self.stadium_target_sequence is not None and
                self.confirm_sequence is not None and self.confirm_release_sequence is not None and
                self.draw_return_rows >= 1,
                "Stadium driver lacks consumed setup/selection/confirm or natural DrawReturn")
        return {
            "scope": "stadium_go_prefix",
            "menu_owner_sequences": {"css": self.css_live_owner_sequence,
                                     "sss": self.sss_live_owner_sequence,
                                     "stadium_target": self.stadium_target_sequence,
                                     "confirm": self.confirm_sequence,
                                     "confirm_neutral_release": self.confirm_release_sequence,
                                     "setup": self.setup_sequence},
            "consumed_source_pads": self.consumed_samples,
            "source_tick_rows": self.source_tick_rows,
            "draw_return_rows": self.draw_return_rows,
            "progress_rows": self.progress_rows,
        }

def menu_actions(path):
    raw = Path(path).read_bytes()
    require(len(raw) <= 1024 * 1024, "SD menu recipe exceeds its bound")
    value = json.loads(raw)
    if isinstance(value, dict) and value.get("version") in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11):
        validate_packet(value)
        return value, hashlib.sha256(raw).hexdigest()
    require(isinstance(value, dict) and set(value) == {"schema", "version", "actions"} and
            value["schema"] == "melee-web-sd-original-menu-inputs" and value["version"] == 1,
            "SD original menu recipe schema differs")
    actions = value["actions"]
    require(isinstance(actions, list) and 1 <= len(actions) <= 256, "SD menu actions are unbounded")
    for action in actions:
        require(isinstance(action, dict) and set(action) == {"label", "scene", "p1", "p2", "polls", "settle_polls"},
                "SD menu action fields differ")
        require(isinstance(action["label"], str) and action["label"], "SD menu action has no label")
        require(type(action["scene"]) is int and 0 <= action["scene"] <= 255 and
                all(type(action[k]) is int and 1 <= action[k] <= 120 for k in ("polls", "settle_polls")),
                "SD menu polling bounds differ")
        pipe_commands(action["p1"])
        pipe_commands(action["p2"])
    require(actions[-1]["scene"] == 9, "SD final confirmation must originate in original SSS")
    return value, hashlib.sha256(raw).hexdigest()


def prepare_rules_profile(profile, user, *, source_slots=(0, 1)):
    """Customize only a fresh owned copy; retain the shared atomic writer's 0400 freeze."""
    profile = Path(profile)
    require(profile.is_dir(), "SD cold boot requires an existing original profile")
    source_inventory = {}
    for item in profile.rglob("*"):
        require(not item.is_symlink(), "SD profile must not redirect files")
        if item.is_file():
            source_inventory[item.relative_to(profile).as_posix()] = hashlib.sha256(item.read_bytes()).hexdigest()
    user = Path(user)
    require(not user.exists(), "Rules probe profile copy already exists")
    _copy_tree(profile, user, skip={"Pipes"})
    config = user / "Config" / "Dolphin.ini"
    require(config.is_file(), "SD cold boot requires Dolphin.ini")
    from original_source_ports import declared_source_slots, inactive_source_slots
    source_slots = declared_source_slots(source_slots)
    p1, p2 = prepare_dual_pipe(user, source_slots=source_slots)
    # Inactive source PAD ports must remain disconnected, not extra Pipe devices.
    import configparser
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(config)
    require(not ini.get("General", "GDBSocket", fallback="").strip(),
            "SD cold boot must not attach a debugger")
    for port in inactive_source_slots(source_slots):
        ini.set("Core", "SIDevice" + str(port), "0")
    _atomic_ini(config, ini)
    return p1, p2, source_inventory


def run(*, dolphin, disc, profile, input_plan=None, menu_recipe, output, build_manifest, timeout=180, gci=None,
        ordinary_policy=None):
    """Own fresh output before preparation so failures cannot vanish before launch."""
    output = Path(output)
    output.mkdir()  # A collision never overwrites another run or its evidence.
    scope = "rules_ready"
    try:
        scope = menu_actions(menu_recipe)[0].get("scope", scope)
        if ordinary_policy is not None:
            scope="ordinary_timeout_gci"
        return _run(dolphin=dolphin, disc=disc, profile=profile, input_plan=input_plan,
                    menu_recipe=menu_recipe, output=output, build_manifest=build_manifest, timeout=timeout, gci=gci,
                    ordinary_policy=ordinary_policy)
    except Exception as error:
        failure = output / "failure.json"
        if not failure.exists():
            failure.write_bytes(canonical({"scope": scope, "stage": "prelaunch",
                                           "native_launched": False, "error": str(error)}))
        raise


def _run(*, dolphin, disc, profile, input_plan=None, menu_recipe, output, build_manifest, timeout, gci=None,
         ordinary_policy=None):
    build = validate_reference_build_manifest(Path(build_manifest), Path(dolphin))
    menus, menu_hash = menu_actions(menu_recipe)
    stadium_prefix = menus["scope"] == "stadium_go_prefix"
    if stadium_prefix:
        require(input_plan is None and ordinary_policy is None,
                "Stadium GO prefix uses its declared menu recipe, supplied profile and optional exact retained GCI")
        plan, plan_hash = None, None
    else:
        require(input_plan is not None, "This menu route requires its declared input plan")
        plan, plan_hash = load_plan(input_plan, allow_authored=True)
    items_probe = menus["scope"] == "items_row_gci"
    competitive_entry = menus["scope"] == "competitive_entry_gci"
    sparse_pair = menus["scope"] == "sparse_pair_gci"
    transform_prefix = menus["scope"] == "sheik_transform_prefix"
    full_route = menus["scope"] == "sd_prefix_gci" or items_probe or competitive_entry or sparse_pair
    guarded_items = (menus["scope"] == "sd_prefix_gci" and menus["version"] == 7) or competitive_entry
    campaign = menus["scope"] in ("rules_ready_gci", "sd_prefix_gci", "items_row_gci",
                                    "competitive_entry_gci", "sparse_pair_gci")
    scope = menus["scope"]
    ordinary = ordinary_policy is not None
    if ordinary:
        from original_competitive_timeout import load_policy, CAPS
        policy, policy_hash = load_policy(Path(ordinary_policy))
        require(competitive_entry and menus["version"]==8 and
                policy["entry_recipe_sha256"]==plan["authored_recipe_sha256"] and
                hashlib.sha256(canonical(menus)).hexdigest()==policy["menu_sha256"],
                "Ordinary policy must retain exact competitive entry/menu provenance")
        scope="ordinary_timeout_gci"
    bounded_log_cap = (CAPS["log_bytes"] if ordinary else
                       8 * 1024 * 1024 if transform_prefix or stadium_prefix else None)
    expected_recipe_version = (8 if transform_prefix else 7 if sparse_pair else 6 if competitive_entry else
                               5 if full_route else 4 if campaign else 3)
    expected_menu_version = (11 if stadium_prefix else 10 if transform_prefix else 9 if sparse_pair else
                             8 if competitive_entry else 6 if items_probe else 7 if guarded_items else
                             5 if full_route else 4 if campaign else 2)
    if stadium_prefix:
        from stadium_go_prefix import EXPECTED_SETUP_RECEIPT_SHA256
        require(menus["version"] == expected_menu_version and
                menus["setup_receipt_sha256"] == EXPECTED_SETUP_RECEIPT_SHA256 and
                "authored_recipe_sha256" not in menus,
                "Stadium menu packet is not bound to the reviewed observer setup receipt")
    else:
        require(plan["authored_recipe"]["version"] == expected_recipe_version and
                menus["version"] == expected_menu_version and (gci is not None) == campaign and
                menus["authored_recipe_sha256"] == plan["authored_recipe_sha256"],
                "Runnable original diagnostic requires the exact current scoped recipe/menu versions")
    loaded_profile = None
    if campaign or transform_prefix or stadium_prefix:
        manifest_raw = Path(build_manifest).read_bytes()
        overlay = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"
        require(hashlib.sha256(manifest_raw).hexdigest() == build["sha256"] and
                json.loads(manifest_raw).get("observer_source_overlay_sha256", {}).get(
                    "Core/PowerPC/ReferenceCaptureObserver.cpp") == hashlib.sha256(overlay.read_bytes()).hexdigest(),
                "Loaded-profile observer producer is stale or unbound")
        if campaign or (stadium_prefix and gci is not None):
            from sd_gci_profile import prepare_gci_folder
            loaded_profile, owned_gci = prepare_gci_folder(gci, output / "gci-folder")
    if transform_prefix:
        require(gci is None and ordinary_policy is None,
                "Sheik transform prefix must use the supplied original profile without GCI injection")
        receiver = SheikTransformPrefixReceiver(plan, menus)
    elif stadium_prefix:
        receiver = StadiumGoPrefixReceiver(menus)
    else:
        receiver = GciRulesMenuReceiver(plan, loaded_profile, full_route=full_route, items_probe=items_probe,
                                       guarded_items=guarded_items, competitive_entry=competitive_entry,
                                       sparse_pair=sparse_pair) if campaign else RulesMenuReceiver(plan)
    if ordinary:
        from ordinary_timeout_receiver import OrdinaryTimeoutReceiver
        receiver=OrdinaryTimeoutReceiver(plan,loaded_profile)
    require(type(timeout) in (int, float) and 0 < timeout <= (600 if ordinary else 180 if full_route or stadium_prefix else 600),
            "Original diagnostic deadline is unbounded")
    user = output / "user"
    if sparse_pair:
        p1, p2, source_inventory = prepare_rules_profile(profile, user, source_slots=(0, 2))
    else:
        p1, p2, source_inventory = prepare_rules_profile(profile, user)
    raw, status = output / "observer.bin", output / "observer-status.json"
    native, native_status = output / "inputs.mwri", output / "input-status.json"
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(("MWRC_", "DOLPHIN_", "SDL_"))}
    environment.update(MWRC_ENABLE="1", MWRC_CPU="JITARM64", MWRC_SOURCE_REV="GALE01r2",
                       MWRC_DOL_SHA256="dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                       MWRC_OUTPUT=str(raw), MWRC_STATUS=str(status),
                       MWRC_INPUT_RECORD=str(native), MWRC_INPUT_STATUS=str(native_status))
    if transform_prefix:
        environment["MWRC_TRANSFORM_PREFIX"] = "1"
    elif stadium_prefix:
        environment.update(MWRC_STADIUM_GO_PREFIX="1",
                           MWRC_STADIUM_SETUP_RECEIPT_SHA256=menus["setup_receipt_sha256"])
    else:
        environment.update(MWRC_SD_INIT="1",
                           MWRC_SD_RECIPE_SHA256=plan["authored_recipe_sha256"],
                           MWRC_SD_MENU_PROBE="sparse_pair" if sparse_pair else "competitive_entry" if competitive_entry else "items_row" if items_probe else "sd_prefix" if full_route else "rules_ready")
    if ordinary:
        environment.update(MWRC_SD_MENU_PROBE="ordinary_timeout",MWRC_ORDINARY_POLICY_SHA256=policy_hash)
        (output/"ordinary-policy.json").write_bytes(canonical(policy))
    command = rules_dolphin_command(dolphin, user, disc)
    if campaign:
        environment["MWRC_SD_PROFILE_GCI_SHA256"] = loaded_profile["sha256"]
    if loaded_profile is not None:
        command += ["-C", "Dolphin.Core.SlotA=8", "-C",
                    "Dolphin.Core.GCIFolderAPath=" + str(output / "gci-folder")]
    if plan is not None:
        (output / "input-plan.json").write_bytes(canonical(plan))
    (output / "menu-recipe.json").write_bytes(canonical(menus))
    launch = {"scope": scope,
        "input_plan_sha256": plan_hash, "menu_recipe_sha256": menu_hash,
        "profile_sha256": source_inventory, "build": build, "command": command}
    if loaded_profile is not None:
        launch.update(profile_gci_sha256=loaded_profile["sha256"], owned_gci=str(owned_gci),
                      observed_prelaunch_config_modes={name: oct((user / "Config" / name).stat().st_mode & 0o777)
                         for name in ("Dolphin.ini", "GCPadNew.ini")})
    if transform_prefix:
        launch["observer_profile"] = "MWRC_TRANSFORM_PREFIX=1; no SD/GCI probe injection"
    if stadium_prefix:
        launch["observer_profile"] = ("MWRC_STADIUM_GO_PREFIX=1; exact setup-receipt binding; "
                                       "supplied profile; optional exact retained read-only GCI copy")
    if ordinary:
        launch.update(ordinary_policy_sha256=policy_hash,caps=CAPS)
    elif stadium_prefix:
        launch.update(caps=STADIUM_CAPTURE_CAPS, wall_seconds=timeout)
    (output / "launch.json").write_bytes(canonical(launch))
    controller = DualPipeController(p1, p2, output / "input-intentions.jsonl")
    if ordinary:
        controller=BoundedIntentController(p1,p2,output/"input-intentions.jsonl")
    elif stadium_prefix:
        controller=StadiumIntentController(p1,p2,output/"input-intentions.jsonl")
    deadline = time.monotonic() + timeout
    with (output / "dolphin.log").open("xb") as log:
        process = subprocess.Popen(command, env=environment,
                                   stdout=subprocess.PIPE if bounded_log_cap is not None else log,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=True)
        bounded_log=None
        try:
            if bounded_log_cap is not None:
                bounded_log=BoundedLog(process.stdout,log,bounded_log_cap)
            # The dedicated receiver expects an interrupted primary ending, so
            # do not use the whole-session Tail's completion-status policy.
            def stadium_wait_check():
                check_stadium_capture_bounds(raw, native, deadline, receiver.seq)
                check_owned_native_wait(process, bounded_log)
            with ObserverTail(raw, None, wait_check=stadium_wait_check if stadium_prefix else
                              (lambda: check_owned_native_wait(process, bounded_log))
                              if ordinary or transform_prefix else None) as tail:
                def next_row():
                    if stadium_prefix:
                        check_stadium_capture_bounds(raw, native, deadline, receiver.seq + 1)
                    if bounded_log is not None:
                        bounded_log.check()
                    if ordinary:
                        require(raw.stat().st_size<=CAPS["observer_bytes"] if raw.exists() else True,
                                "Ordinary observer byte cap")
                        require(native.stat().st_size<=CAPS["input_bytes"] if native.exists() else True,
                                "Ordinary native input byte cap")
                    row = tail.next(deadline)
                    if ordinary:
                        require(tail.offset<=CAPS["observer_bytes"], "Ordinary consumed observer byte cap")
                    if stadium_prefix:
                        check_stadium_capture_bounds(raw, native, deadline, receiver.seq + 1)
                    receiver.accept(row)
                    return row
                def wait_source(predicate, label, max_polls):
                    first = receiver.menu_polls
                    while not predicate():
                        require(not receiver.ended and receiver.menu_polls - first < max_polls,
                                "Rules menu source predicate/cap failed: " + label)
                        next_row()
                def tap(action, label, max_polls):
                    before = receiver.menu_consumed
                    controller.set_both(action["p1"], action["p2"], action=label)
                    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                                [action["p1"], action["p2"]], label + ":consumed", max_polls)
                    before = receiver.menu_consumed
                    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label + ":release")
                    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                                [NEUTRAL_PAD] * 2, label + ":neutral-consumed", max_polls)
                boot_actions = 0
                while not matches(receiver.latest_menu, menus["actions"][0]["before"]):
                    require(receiver.menu_polls < menus["boot_max_polls"], "Rules cold startup polling cap")
                    next_row()
                    scene = (receiver.latest_menu or {}).get("scene")
                    for action in menus["boot"]:
                        if scene == action["scene"]:
                            boot_actions += 1
                            require(boot_actions <= menus["boot_max_actions"], "Rules cold startup action cap")
                            tap(action, "cold-scene-" + str(scene), 600)
                            # Consume a new poll before considering another boot action.
                            first = receiver.menu_polls
                            wait_source(lambda: receiver.menu_polls > first, "cold-source-poll", 600)
                            break
                for action in menus["actions"]:
                    wait_source(lambda: matches(receiver.latest_menu, action["before"]),
                                action["label"] + ":ready", action["max_polls"])
                    if "when_value" in action:
                        require(receiver.latest_menu["value"] in (0,1), "Competitive item is not a observed switch")
                    if "when_value" not in action or receiver.latest_menu["value"] == action["when_value"]:
                        tap(action, action["label"], action["max_polls"])
                    wait_source(lambda: matches(receiver.latest_menu, action["after"]),
                                action["label"] + ":observed", action["max_polls"])
                if (full_route and not items_probe) or transform_prefix or stadium_prefix:
                    drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap)
                if stadium_prefix and not receiver.setup_seen:
                    wait_source(lambda: receiver.setup_seen, "original Stadium VS setup transition",
                                menus["sss"]["max_vs_transition_polls"])
                if transform_prefix:
                    drive_sheik_transform_input(receiver, plan, controller, next_row)
                if sparse_pair:
                    while not receiver.setup_seen:
                        next_row()
                    press, release = plan["frames"]
                    controller.set_both(press[0], press[2], action="sparse-source-0-2-press")
                    samples_left = plan["authored_recipe"]["input_witness"]["max_source_samples"]
                    while receiver.witness_phase == 0:
                        require(receiver.sparse_source_samples < samples_left and not receiver.ended,
                                "Sparse source input press witness cap exhausted")
                        next_row()
                    require(receiver.witness_phase == 1,
                            "Sparse source input press witness was not observed")
                    controller.set_both(release[0], release[2], action="sparse-source-0-2-release")
                    while not receiver.ended:
                        require(receiver.sparse_source_samples < samples_left,
                                "Sparse source input release witness cap exhausted")
                        next_row()
                while not receiver.ended:
                    next_row()
                    if ordinary and receiver.order==2:
                        state=receiver.ordinary
                        if state.phase=="first-loss" and not getattr(receiver,"direction_sent",False):
                            from reference_versus_sequence_capture import raw_pad
                            controller.set_both(raw_pad(x=-80),NEUTRAL_PAD,action="ordinary-first-loss")
                            receiver.direction_sent=True
                        elif state.phase=="held-bank-drain" and not getattr(receiver,"release_sent",False):
                            controller.set_both(NEUTRAL_PAD,NEUTRAL_PAD,action="ordinary-first-loss:release")
                            receiver.release_sent=True
                # MWRO End is flushed before the writer publishes final status.
                # Do not treat that publication race as native completion.
                if stadium_prefix:
                    primary_status, _ = wait_transform_terminal_statuses(status, native_status, deadline,
                        wait_check=lambda: check_stadium_capture_bounds(raw, native, deadline, receiver.seq))
                    from reference_input_stream import validate_stream as validate_input_stream
                    from stadium_go_prefix import validate_stadium_go_prefix
                    input_summary = validate_input_stream(native)
                    input_status = validate_status(native_status, mode="record",
                                                   events=input_summary["events"], require_complete=True)
                    check_stadium_capture_bounds(raw, native, deadline, receiver.seq)
                    prefix_report = validate_stadium_go_prefix(
                        raw, menus["setup_receipt_sha256"], status_path=status)
                    bounded_log.check()
                    report = {
                        "schema": "melee-web-original-stadium-go-prefix-capture",
                        "version": 1,
                        "scope": scope,
                        "status": "pass",
                        "input_plan_sha256": None,
                        "profile_gci_sha256": loaded_profile["sha256"] if loaded_profile else None,
                        "menu_recipe_sha256": menu_hash,
                        "input_intentions_sha256": hashlib.sha256(
                            (output / "input-intentions.jsonl").read_bytes()).hexdigest(),
                        "profile_sha256": source_inventory,
                        "build": build,
                        "observer_status": primary_status,
                        "input_status": input_status,
                        "input_stream": input_summary,
                        "driver": receiver.finish(),
                        "stadium_go_prefix": prefix_report,
                        "claims": {"original_css_sss_stadium_setup": True,
                                   "source_consumed_stadium_confirm_and_release": True,
                                   "natural_go_draw_return_prefix": True,
                                   "profile_setup_equivalence": False,
                                   "port_rng_equivalence": False,
                                   "whole_match_or_results": False,
                                   "pixels_or_pcm_equivalence": False},
                    }
                    (output / "report.json").write_bytes(canonical(report))
                    return report
                if transform_prefix:
                    primary_status, _ = wait_transform_terminal_statuses(status, native_status, deadline)
                    from reference_input_stream import validate_stream as validate_input_stream
                    from sheik_transform_prefix import validate_transform_prefix
                    input_summary = validate_input_stream(native)
                    input_status = validate_status(native_status, mode="record",
                                                   events=input_summary["events"], require_complete=True)
                    transform_report = validate_transform_prefix(raw, status)
                    bounded_log.check()
                    report = {
                        "schema": "melee-web-original-sheik-transform-prefix-capture",
                        "version": 1,
                        "scope": scope,
                        "status": "pass",
                        "input_plan_sha256": plan_hash,
                        "menu_recipe_sha256": menu_hash,
                        "input_intentions_sha256": hashlib.sha256(
                            (output / "input-intentions.jsonl").read_bytes()).hexdigest(),
                        "profile_sha256": source_inventory,
                        "build": build,
                        "observer_status": primary_status,
                        "input_status": input_status,
                        "input_stream": input_summary,
                        "driver": receiver.finish(),
                        "transform_prefix": transform_report,
                        "claims": {"original_css_sss_setup": True,
                                   "source_consumed_down_b_y_minus_80": True,
                                   "consumed_neutral_release": True,
                                   "active_sheik_grounded_neutral_prefix": True,
                                   "match_complete": False,
                                   "draw_or_browser_equivalence": False},
                    }
                    (output / "report.json").write_bytes(canonical(report))
                    return report
                wait_terminal_statuses(status, native_status, deadline)
                if ordinary:
                    bounded_log.check()
                report = receiver.finish(status, native, native_status)
                (output / "report.json").write_bytes(canonical(report))
                return report
        except Exception as error:
            (output / "failure.json").write_bytes(canonical({"scope": scope, "stage": "native",
                "native_launched": True, "pid": process.pid, "error": str(error)}))
            raise
        finally:
            # Cleanup closes the direct child's output; join its owned drain
            # before closing the retained log. No process-name/group cleanup.
            primary_error=sys.exc_info()[1]
            cleanup_error=None
            log_error=None
            try:
                cleanup_process(process, output, scope=scope)
            except Exception as error:
                cleanup_error=error
            # Independently finalize the owned drain even if PID cleanup failed.
            if ordinary or transform_prefix or stadium_prefix:
                try:
                    if bounded_log:
                        bounded_log.finish()
                    elif process.stdout:
                        process.stdout.close()
                except Exception as error:
                    log_error=error
                (output/"owned-log-cleanup.json").write_bytes(canonical(dict(
                    scope=scope,initialized=bounded_log is not None,
                    bytes=bounded_log.size if bounded_log else 0,
                    thread_alive=bounded_log.thread.is_alive() if bounded_log else False,
                    native_cleanup_error=str(cleanup_error) if cleanup_error else None,
                    error=str(log_error) if log_error else None)))
            if cleanup_error or log_error:
                # A diagnostic success candidate is admitted only after owned
                # process and log retirement; retain the primary source failure.
                if (transform_prefix or stadium_prefix) and (output / "report.json").is_file():
                    candidate = json.loads((output / "report.json").read_text())
                    candidate["status"] = "fail"
                    candidate["cleanup_errors"] = [str(value) for value in
                        (cleanup_error, log_error) if value is not None]
                    (output / "report.json").write_bytes(canonical(candidate))
                error=cleanup_error or log_error
                failure = output / "failure.json"
                if not failure.exists():
                    failure.write_bytes(canonical({"scope": scope, "stage": "cleanup",
                        "native_launched": True, "pid": process.pid, "error": str(error)}))
                if primary_error is None or not (ordinary or transform_prefix or stadium_prefix):
                    raise error


def require_css_join_owner(css, port, *, initial=False):
    """Menu7's original vacant door, or its observed own-Human join.

    mnCharSel_CursorThink joins an NA door when its own cursor enters
    0.2 < y < 22. Movement remains the existing source-owned Mario policy.
    """
    source_slot = css.get("source_slots", [0, 1])[port]
    player, door, cursor = css["players"][port], css["doors"][port], css["cursors"][port]
    model = css["models"][port]
    require(player["slot"] == 0 and cursor["port"] == source_slot and
            cursor["state"] in (0, 1, 2) and
            model["owner"] in (0, source_slot+1) and
            (cursor["state"] != 1 or (cursor["held"] == source_slot and model["owner"] == source_slot+1)),
            "CSS join has foreign slot/cursor ownership")
    if initial and player["kind"] == 3:
        require(player["character"] == 26 and door["kind"] == 3 and door["icon"] == 25 and
                door["costume"] == 0 and cursor["state"] == 0 and cursor["held"] == 0 and
                cursor["x"] == 15.0*source_slot-31.0 and cursor["y"] == -21.5 and
                model["owner"] == 0,
                "CSS vacant door is not the observed initialized owner")
    else:
        require(player["kind"] == door["kind"] == 0, "CSS requires observed own Human join")


def drive_sheik_transform_input(receiver, plan, controller, next_row):
    """Send one exact down-B value only after source-consumed readiness."""
    witness = plan["authored_recipe"]["input_witness"]
    press, release = plan["frames"]
    cap = witness["max_source_samples"]
    while not receiver.grounded_zelda_sequence:
        require(not receiver.ended and receiver.active_source_ticks < cap,
                "Grounded neutral Zelda readiness exhausted the 600-observation cap")
        next_row()
    require(receiver.neutral_consume_sequence is not None and
            receiver.neutral_consume_sequence < receiver.grounded_zelda_sequence,
            "Neutral source consume did not precede grounded Zelda readiness")
    controller.set_both(press[0], press[1], action="sheik-transform-down-b-y-minus-80")
    while not receiver.down_b_consumed:
        require(not receiver.ended,
                "Consumed down-B witness ended before the source PAD record")
        next_row()
    controller.set_both(release[0], release[1], action="sheik-transform-neutral-release")
    while not receiver.down_b_released:
        require(not receiver.ended,
                "Neutral release ended before its source PAD record")
        next_row()
    while not receiver.ended:
        next_row()
    require(receiver.down_b_consumed and receiver.down_b_released and
            receiver.action_seen and receiver.post_swap_neutral is not None,
            "Source transform prefix ended before the consumed input and active Sheik owner")


def drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap):
    """Reuse the original Pipe driver's bounded cursor/door/highlight policy.

    All axes, targets and limits are frozen in the canonical packet. Observer
    input events prove actual consumption; host sleep never proves readiness.
    Menu7 SD and menu8 competitive entry share this source-owned join policy;
    historical menus keep their immediate-Human precondition. There is no
    Results/resolution continuation.
    """
    from reference_versus_sequence_capture import raw_pad
    policy = menus["css"]
    def pair(port, pad):
        pads = [NEUTRAL_PAD] * 2
        pads[port] = pad
        return {"p1": pads[0], "p2": pads[1]}
    def neutral(label):
        before = receiver.menu_consumed
        controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label)
        wait_source(lambda: receiver.menu_consumed > before and
                    receiver.last_pad[:2] == [NEUTRAL_PAD]*2, label, 600)
    def move(port, point, label):
        first = receiver.menu_polls
        while True:
            require(receiver.css is not None and receiver.menu_polls-first < policy["max_move_polls"],
                    "CSS cursor owner/movement cap: " + label)
            cursor = receiver.css["cursors"][port]
            dx, dy = point[0]-cursor["x"], point[1]-cursor["y"]
            if abs(dx) < policy["tolerance"] and abs(dy) < policy["tolerance"]:
                neutral(label + ":neutral")
                stable, previous = 0, receiver.css["cursors"][port].copy()
                while stable < policy["stable_cursor_polls"]:
                    before = receiver.menu_polls
                    wait_source(lambda: receiver.menu_polls > before, label+":settle", 600)
                    require(receiver.css is not None, "CSS cursor lost while settling")
                    current = receiver.css["cursors"][port]
                    stable = stable+1 if all(abs(current[k]-previous[k]) < 0.02 for k in ("x", "y")) else 0
                    previous = current.copy()
                    require(receiver.menu_polls-first < policy["max_move_polls"], "CSS settle cap")
                if all(abs(point[i]-previous[k]) < policy["tolerance"] for i,k in enumerate(("x","y"))):
                    return
                continue
            def axis(delta):
                return 0 if abs(delta) < 0.5 else (70 if abs(delta)>5 else 35)*(1 if delta>0 else -1)
            intent = pair(port, raw_pad(x=axis(dx), y=axis(dy)))
            before = receiver.menu_polls
            controller.set_both(intent["p1"], intent["p2"], action=label)
            wait_source(lambda: receiver.menu_polls > before, label+":cursor", 600)
    wait_source(lambda: receiver.css is not None, "CSS constructor-owned inventory", 600)
    transform_prefix = menus["scope"] == "sheik_transform_prefix"
    stadium_prefix = menus["scope"] == "stadium_go_prefix"
    if menus["version"] in (7, 8, 9, 10, 11):
        for port in policy["ports"]:
            require_css_join_owner(receiver.css, port, initial=True)
    else:
        require([p["kind"] for p in receiver.css["players"]] == [0, 0], "CSS requires two original humans")
    for port, costume in enumerate(policy["costumes"]):
        point = policy["points"][port] if transform_prefix else policy["point"]
        character = policy["characters"][port] if transform_prefix else policy["character"]
        icon = policy["icon_table_indices"][port] if transform_prefix else policy["icon"]
        label = f"original-character-P{port+1}" if transform_prefix else f"Mario-P{port+1}"
        move(port, point, label)
        if menus["version"] in (7, 8, 9, 10, 11):
            wait_source(lambda: receiver.css["players"][port]["kind"] == 0 and
                        receiver.css["doors"][port]["kind"] == 0,
                        f"CSS own Human join P{port+1}", 600)
            require_css_join_owner(receiver.css, port)
        if transform_prefix:
            wait_source(lambda: receiver.css["doors"][port]["icon"] == icon,
                        f"CSS authored icon hover P{port+1}", 600)
        place_label = f"place-P{port+1}" if transform_prefix else f"Mario-place-P{port+1}"
        tap(pair(port, raw_pad(buttons=["A"])), place_label, 600)
        wait_source(lambda: receiver.css["players"][port]["character"] == character,
                    f"original character selected P{port+1}", 600)
        if receiver.css["doors"][port]["costume"] != costume:
            model = receiver.css["models"][port]
            move(port, (model["x"]-2, model["y"]+1.6), "pickup-human-puck")
            tap(pair(port, raw_pad(buttons=["A"])), "pickup-human-puck", 600)
            wait_source(lambda: receiver.css["cursors"][port]["state"] == 1 and
                        receiver.css["cursors"][port]["held"] ==
                        receiver.css.get("source_slots", [0, 1])[port],
                        "held human puck", 600)
            hover_label = ("original-character-costume-hover" if transform_prefix else
                           "Mario-costume-hover")
            move(port, point, hover_label)
            wait_source(lambda: receiver.css["doors"][port]["icon"] == icon,
                        "original character icon", 600)
            for attempt in range(policy["max_costume_taps"]):
                if receiver.css["doors"][port]["costume"] == costume:
                    break
                before = receiver.css["doors"][port]["costume"]
                tap(pair(port, raw_pad(buttons=["X"])), "Mario-costume", 600)
                wait_source(lambda: receiver.css["doors"][port]["costume"] != before, "costume changed", 600)
            require(receiver.css["doors"][port]["costume"] == costume, "Mario costume cap")
            tap(pair(port, raw_pad(buttons=["A"])), "place-colored-Mario", 600)
        wait_source(lambda: receiver.css["cursors"][port]["state"] != 1, "human puck placed", 600)
    expected_lineup = policy["characters"] if transform_prefix else [8, 8]
    require([p["kind"] for p in receiver.css["players"]] == [0,0] and
            [p["character"] for p in receiver.css["players"]] == expected_lineup and
            [d["costume"] for d in receiver.css["doors"]] == policy["costumes"], "CSS final lineup differs")
    before = receiver.menu_polls
    wait_source(lambda: receiver.menu_polls >= before+policy["idle_polls_before_start"], "CSS source idle", 600)
    tap(pair(0, raw_pad(buttons=["START"])), "CSS-start-SSS", 600)
    wait_source(lambda: receiver.stage is not None, "SSS constructor-owned readiness", 600)
    stage = menus["sss"]
    if stadium_prefix:
        drive_stadium_sss(receiver, menus, controller, next_row, wait_source)
        return
    before = receiver.menu_polls
    wait_source(lambda: receiver.menu_polls >= before+stage["initial_idle_polls"], "SSS source idle", 600)
    if receiver.stage["kind"] != stage["stage_kind"]:
        before = receiver.menu_polls
        controller.set_both(raw_pad(x=stage["column_x"]), NEUTRAL_PAD, action="FD-column")
        wait_source(lambda: receiver.menu_polls >= before+stage["column_polls"], "FD column", 600)
        neutral("FD-column-neutral")
        controller.set_both(raw_pad(y=stage["scan_y"]), NEUTRAL_PAD, action="FD-scan-up")
        wait_source(lambda: receiver.stage["kind"] == stage["stage_kind"], "FD highlight", stage["max_scan_polls"])
        neutral("FD-highlight-neutral")
    if transform_prefix:
        wait_source(lambda: receiver.stage["kind"] == 32 and
                    receiver.stage.get("stable_polls", 0) >= 2,
                    "FD live source highlight stable", 600)
    else:
        wait_source(lambda: receiver.stage["kind"] == 32 and receiver.stage["cooldown"] == 0,
                    "FD original confirmation predicate", 600)
    # Release immediately after observed menu consumption. If an A sample
    # reaches VS instead, the unchanged native/receiver neutral checks fail.
    before = receiver.menu_consumed
    controller.set_both(raw_pad(buttons=["A"]), NEUTRAL_PAD, action="choose-FD")
    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                [raw_pad(buttons=["A"]), NEUTRAL_PAD], "choose-FD:consumed", 600)
    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action="choose-FD:release")



def drive_stadium_sss(receiver, menus, controller, next_row, wait_source):
    """Use only the packet's finite cardinal pulses and observed source owner."""
    from reference_versus_sequence_capture import raw_pad
    policy = menus["sss"]
    neutral_pair = [NEUTRAL_PAD, NEUTRAL_PAD]
    target = lambda: (receiver.stage is not None and
                      receiver.stage.get("index") == policy["target_index"] and
                      receiver.stage.get("kind") == policy["target_kind"])
    target_stable = lambda: (target() and receiver.stage.get("stable_polls", 0) >= 2)
    movement_poll_total = 0
    owner_start = receiver.menu_polls

    def set_neutral_and_wait(label):
        before = len(receiver.consumed_samples)
        controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label)
        wait_source(lambda: any(sample["ports"][:2] == neutral_pair
                                for sample in receiver.consumed_samples[before:]),
                    label + ":consumed", policy["max_pad_consume_wait_polls"])
        sample = next(sample for sample in receiver.consumed_samples[before:]
                      if sample["ports"][:2] == neutral_pair)
        return sample["sequence"]

    release_sequence = set_neutral_and_wait("SSS-Stadium-neutral-entry")
    receiver.target_neutral_release_sequence = release_sequence
    if target():
        wait_source(lambda: receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
                    "SSS-Stadium-neutral-stability", policy["max_owner_polls"])

    for pulse_index, pulse in enumerate(policy["pulses"]):
        if receiver.target_neutral_polls >= policy["target_stable_neutral_polls"]:
            break
        pad = raw_pad(x=pulse["x"], y=pulse["y"])
        before_samples = len(receiver.consumed_samples)
        start_polls = receiver.menu_polls
        controller.set_both(pad, NEUTRAL_PAD, action=f"SSS-cardinal-{pulse_index}")
        pulse_consumed = lambda: any(sample["ports"][:2] == [pad, NEUTRAL_PAD]
                                     for sample in receiver.consumed_samples[before_samples:])
        while (receiver.menu_polls - start_polls < pulse["max_source_polls"] and
               not (pulse_consumed() and target_stable())):
            require(receiver.menu_polls - owner_start < policy["max_owner_polls"],
                    "SSS live-owner observation cap exhausted")
            before_poll = receiver.menu_polls
            wait_source(lambda: receiver.menu_polls > before_poll,
                        f"SSS-cardinal-{pulse_index}:source-poll", 1)
            movement_poll_total += receiver.menu_polls - before_poll
            require(movement_poll_total <= policy["max_movement_source_polls"],
                    "SSS cardinal movement budget exceeded")
        require(pulse_consumed(),
                f"SSS cardinal pulse {pulse_index} was not observed in consumed source PAD")
        release_sequence = set_neutral_and_wait(f"SSS-cardinal-{pulse_index}:neutral")
        receiver.target_neutral_release_sequence = release_sequence
        receiver.target_neutral_polls = 0
        if target():
            wait_source(lambda: receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
                        f"SSS-Stadium-neutral-stability-{pulse_index}", policy["max_owner_polls"])

    require(target_stable() and receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
            "Finite SSS cardinal pulse policy did not observe stable Stadium index 18/kind 3")
    before_samples = len(receiver.consumed_samples)
    controller.set_both(raw_pad(buttons=["A"]), NEUTRAL_PAD, action="SSS-Stadium-confirm")
    wait_source(lambda: any(sample["ports"][:2] == [raw_pad(buttons=["A"]), NEUTRAL_PAD]
                            for sample in receiver.consumed_samples[before_samples:]),
                "SSS-Stadium-confirm:consumed", policy["max_pad_consume_wait_polls"])
    release_sequence = set_neutral_and_wait("SSS-Stadium-confirm:neutral-release")
    require(receiver.confirm_sequence is not None and receiver.confirm_release_sequence == release_sequence,
            "SSS Stadium confirm/release did not retain exact consumed PAD rows")


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "profile", "input-plan", "menu-recipe", "output", "build-manifest"):
        parser.add_argument("--" + name, type=Path, required=name != "input-plan")
    parser.add_argument("--gci", type=Path, help="Exact retained re-export; required by GCI campaign, optional for Stadium GO prefix")
    parser.add_argument("--ordinary-policy",type=Path,
                        help="Exact separate adaptive competitive timeout policy; v4 remains entry provenance")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(argv)
    try:
        report = run(**vars(args))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Original menu/SD diagnostic failed: {error}\n")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
