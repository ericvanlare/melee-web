"""Focused source controls for the named original Sheik capture driver."""
from pathlib import Path
import struct
import sys
import unittest
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))

from authored_sd_reference_plan import make_input_plan, recipe
from capture_sd_reference_prefix import (SheikTransformPrefixReceiver, drive_sheik_transform_input,
                                          drive_authored_css_sss)
from reference_versus_sequence_capture import raw_pad
from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD, verify_entry, verify_tick
from sd_original_menu_plan import (rules_ready_packet, sheik_transform_prefix_packet,
                                   validate_packet, route_pads)

SLICE_NAMES = {2: "pad_queue", 3: "pad_slot", 17: "scene_routing",
               40: "scene_kind", 41: "stage_select_index", 42: "stage_select_kind",
               43: "menu_css_cursor", 44: "menu_css_doors", 47: "menu_css_model",
               48: "menu_css_live_state"}


def _setup_bytes():
    raw = bytearray(0x138)
    raw[0] = 0x20  # ordinary VS stock match, timer disabled
    raw[2] = 0x80  # stock; pause remains enabled
    raw[4] = 0x40  # ordinary VS
    raw[0x0E:0x10] = (32).to_bytes(2, "big")
    raw[0x0B] = 0xFF
    raw[0x20:0x28] = b"\xFF" * 8
    raw[0x2C:0x30] = bytes.fromhex("3f800000")
    raw[0x30:0x34] = bytes.fromhex("3f800000")
    raw[0x34:0x38] = bytes.fromhex("3f800000")
    for index, (character, costume) in enumerate(((18, 1), (8, 0))):
        base = 0x60 + index * 0x24
        raw[base:base + 5] = bytes((character, 0, 4, costume, index + 1))
        raw[base + 12] = 0x80
    for index in range(2, 6):
        raw[0x60 + index * 0x24 + 1] = 3
    return raw


def _consume_payload(pads, errors, *, sequence_index=0):
    base = 0x804C3000
    queue = bytearray(0x0C)
    queue[0] = 1
    queue[8:12] = base.to_bytes(4, "big")
    slot = bytearray(0x30)
    for index, (pad, error) in enumerate(zip(pads, errors)):
        slot[index * 12:index * 12 + 11] = bytes.fromhex(pad)
        slot[index * 12 + 10] = error & 0xFF
        slot[index * 12 + 11] = 0xA0 + index  # trailing ABI padding is not PAD err
    registers = [0] * 32
    registers[6] = sequence_index
    registers[25] = base + sequence_index * 0x30
    return {
        "boundary": "pad_consume",
        "slices": [
            {"name": "pad_queue", "tag": 2, "flags": 0, "address": 0x804C1F78,
             "size": len(queue), "hex": queue.hex()},
            {"name": "pad_slot", "tag": 3, "flags": 0, "address": base + sequence_index * 0x30,
             "size": len(slot), "hex": slot.hex()},
        ],
        "gprs": registers,
    }


def _slice(tag, flags, address, raw):
    return {"name": SLICE_NAMES[tag], "tag": tag, "flags": flags, "address": address,
            "size": len(raw), "hex": raw.hex()}


def _css_poll(sequence, mode, *, include_cursor_models=True):
    live = bytearray(0x148)
    doors = bytearray(0x90)
    for slot in range(2):
        base = 0x70 + slot * 0x24
        live[base] = 26
        live[base + 1] = 3  # source-owned vacant door
        doors[slot * 0x24 + 11] = 3
        doors[slot * 0x24 + 14] = 25
        live[base + 4] = 0
    for slot in range(2, 4):
        live[0x70 + slot * 0x24 + 1] = 3
    slices = [_slice(40, 0, 0x804D6720, b"\x08"),
              _slice(17, 0, 0x80479D30, bytes((mode, 0, 0, 0, 0, 0))),
              _slice(48, 0, 0x804D6CB0, bytes(live)),
              _slice(44, 0, 0x803F0DFC, bytes(doors))]
    if include_cursor_models:
        for slot in range(2):
            cursor = bytearray(0x14)
            cursor[4], cursor[5], cursor[6] = slot, 0, 0
            cursor[12:20] = struct.pack(">ff", 15.0 * slot - 31.0, -21.5)
            model = bytearray(0x18)
            model[5] = 0
            model[8:16] = struct.pack(">ff", 15.0 * slot - 31.0, -21.5)
            slices.append(_slice(43, slot, 0x804A1000 + slot * 0x20, bytes(cursor)))
            slices.append(_slice(47, slot, 0x804A2000 + slot * 0x20, bytes(model)))
    return {"seq": sequence, "event": "boundary", "payload": {
        "boundary": "pad_poll", "pc": 0x80377584, "source_tick": 0,
        "slices": slices}}


def _sss_poll(sequence, mode):
    index = 4
    kind_address = 0x803F06D0 + index * 0x1C + 0x0B
    return {"seq": sequence, "event": "boundary", "payload": {
        "boundary": "pad_poll", "pc": 0x80377584, "source_tick": 0,
        "slices": [_slice(40, 0, 0x804D6720, b"\x09"),
                   _slice(17, 0, 0x80479D30, bytes((mode, 0, 0, 0, 0, 0))),
                   _slice(41, 0, 0x804D6CAE, bytes((index,))),
                   _slice(42, 0, kind_address, b"\x20")]}}


def _serialized_transform(path, *, mutation=None):
    # Reuse the existing source-owner fixture writer, then serialize the scoped
    # menu/setup values through the actual MWRO transport and production decoder.
    from test_sheik_transform_prefix import (_capture, _frame, _json, _boundary_payload)
    import reference_observer_stream as stream
    _capture(path, mutations=mutation or {})
    rows = list(stream.iter_records(path))
    for row in rows:
        if row["event"] != "boundary":
            continue
        payload = row["payload"]
        if payload["boundary"] == "pad_poll":
            scene = next(item for item in payload["slices"] if item["tag"] == 40)
            replacement = _css_poll(row["seq"], 2) if scene["hex"] == "08" else _sss_poll(row["seq"], 2)
            payload["slices"] = replacement["payload"]["slices"]
        if payload["boundary"] == "setup":
            next(item for item in payload["slices"] if item["tag"] == 4)["hex"] = _setup_bytes().hex()
    encoded = []
    reverse = {value: key for key, value in stream.BOUNDARY_NAMES.items()}
    for row in rows:
        if row["event"] == "boundary":
            values = [(item["tag"], item["flags"], item["address"], bytes.fromhex(item["hex"]))
                      for item in row["payload"]["slices"]]
            encoded.append(_frame(3, row["seq"], _boundary_payload(reverse[row["payload"]["boundary"]], values,
                pad_slot=values[1][3] if row["payload"]["boundary"] == "pad_consume" else None),
                pc=row["payload"]["pc"], source_tick=row["payload"]["source_tick"]))
        else:
            encoded.append(_json({"handshake": 1, "start": 2, "end": 6}[row["event"]], row["seq"], row["payload"]))
    path.write_bytes(b"".join(encoded))
    return list(stream.iter_records(path))


class SheikTransformCaptureDriverTests(unittest.TestCase):
    def test_serialized_production_decoder_receiver_and_live_input_complete(self):
        from sheik_transform_prefix import validate_transform_prefix
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic-transform.mwro"
            rows = _serialized_transform(path)
            receiver = SheikTransformPrefixReceiver(make_input_plan(8), sheik_transform_prefix_packet())
            pending = iter(rows)
            def next_row():
                row = next(pending)
                receiver.accept(row)
                return row
            while not receiver.setup_seen:
                next_row()
            sent = []
            class Controller:
                def set_both(self, p1, p2, *, action):
                    sent.append((p1, p2, action))
            drive_sheik_transform_input(receiver, receiver.plan, Controller(), next_row)
            self.assertEqual([list(item[:2]) for item in sent],
                             [frame[:2] for frame in receiver.plan["frames"]])
            result = receiver.finish()
            self.assertEqual(result["active_source_tick_observations"], 4)
            self.assertEqual(result["source_samples"][-1]["portable"]["active_kind"], 7)
            self.assertTrue(all(item["errors"] == [0, 0, -1, -1]
                                for item in result["consumed_source_pads"]))
            self.assertEqual(validate_transform_prefix(path)["status"], "pass")
            self.assertEqual(list(pending), [])

    def test_complete_css_sss_adapter_uses_serialized_source_inventory(self):
        # Synthetic source progression only: execute the actual menu adapter,
        # serialize every full poll/consume row, then use the production decoder
        # and receiver. This is not a gameplay/CSS accuracy assertion.
        from test_sheik_transform_prefix import _frame, _boundary_payload
        import reference_observer_stream as stream
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic-menu.mwro"
            receiver = SheikTransformPrefixReceiver(make_input_plan(8), sheik_transform_prefix_packet())
            menus = receiver.menus
            css = _css_poll(0, 2)
            scene, index, pending, last = 8, 0, [NEUTRAL_PAD] * 2, [NEUTRAL_PAD] * 2
            kinds, characters, colors, held = [3, 3], [26, 26], [0, 0], [False, False]
            coordinates = [[-31., -21.5], [-16., -21.5]]
            emitted, actions = [], []
            def emit(payload):
                nonlocal index
                values = [(v["tag"], v["flags"], v["address"], bytes.fromhex(v["hex"])) for v in payload["slices"]]
                kind = 2 if payload["boundary"] == "pad_consume" else 1
                binary = _frame(3, index, _boundary_payload(kind, values,
                    pad_slot=values[1][3] if kind == 2 else None), pc=0x80377584 if kind == 2 else 0x8034DD8C)
                with path.open("ab") as output:
                    output.write(binary)
                row = list(stream.iter_records(path))[-1]
                receiver.accept(row); emitted.append(row); index += 1
            def next_row():
                nonlocal scene, last, css
                emit(_consume_payload([*pending, DISCONNECTED_PAD, DISCONNECTED_PAD], [0, 0, -1, -1]))
                if scene == 8:
                    for port, pad in enumerate(pending):
                        raw = bytes.fromhex(pad)
                        for axis in range(2):
                            amount = raw[axis + 2]; amount = amount - 256 if amount >= 128 else amount
                            coordinates[port][axis] += (5. if abs(amount) == 70 else .5) * (1 if amount > 0 else -1) if amount else 0
                        if kinds[port] == 3 and 0.2 < coordinates[port][1] < 22:
                            kinds[port], held[port] = 0, True
                        buttons = int.from_bytes(raw[:2], "big")
                        edge = buttons & ~int.from_bytes(bytes.fromhex(last[port])[:2], "big")
                        if edge & 0x0100:
                            if held[port]:
                                characters[port], held[port] = menus["css"]["characters"][port], False
                            else:
                                held[port] = True
                        if edge & 0x0400:
                            colors[port] += 1
                        if edge & 0x1000:
                            scene = 9
                    css = _css_poll(index, 2)
                    values = { (v["tag"], v["flags"]): v for v in css["payload"]["slices"] }
                    live, doors = bytearray.fromhex(values[(48, 0)]["hex"]), bytearray.fromhex(values[(44, 0)]["hex"])
                    for port in range(2):
                        b = 0x70 + port * 0x24; live[b:b+2] = bytes((characters[port], kinds[port]))
                        b = port * 0x24; doors[b+11], doors[b+13], doors[b+14] = kinds[port], colors[port], menus["css"]["icon_table_indices"][port]
                        cursor, model = bytearray.fromhex(values[(43, port)]["hex"]), bytearray.fromhex(values[(47, port)]["hex"])
                        cursor[5], cursor[6] = (1, port) if held[port] else (0, 0)
                        cursor[12:20] = struct.pack(">ff", *coordinates[port]); model[5] = port + 1 if held[port] else 0
                        model[8:16] = struct.pack(">ff", *coordinates[port])
                        values[(43, port)]["hex"], values[(47, port)]["hex"] = cursor.hex(), model.hex()
                    values[(48, 0)]["hex"], values[(44, 0)]["hex"] = live.hex(), doors.hex()
                last = pending.copy()
                emit(css["payload"] if scene == 8 else _sss_poll(index, 2)["payload"])
            class Controller:
                def set_both(self, p1, p2, *, action):
                    nonlocal pending
                    pending = [p1, p2]; actions.append((action, pending.copy()))
            controller = Controller()
            def wait_source(predicate, label, cap):
                start = receiver.menu_polls
                while not predicate():
                    self.assertLess(receiver.menu_polls - start, cap, label)
                    next_row()
            def tap(action, label, cap):
                count = receiver.menu_consumed
                controller.set_both(action["p1"], action["p2"], action=label)
                wait_source(lambda: receiver.menu_consumed > count and receiver.last_pad[:2] == pending, label, cap)
                count = receiver.menu_consumed
                controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label + ":release")
                wait_source(lambda: receiver.menu_consumed > count and receiver.last_pad[:2] == pending, label, cap)
            emit(css["payload"])
            drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap)
            self.assertEqual(characters, [18, 8]); self.assertEqual(colors, [1, 0])
            self.assertEqual(kinds, [0, 0]); self.assertEqual(receiver.stage["kind"], 32)
            self.assertEqual(actions[-2][0], "choose-FD"); self.assertEqual(actions[-1][0], "choose-FD:release")
            self.assertEqual(pending, [NEUTRAL_PAD, NEUTRAL_PAD])
            self.assertEqual(len(list(stream.iter_records(path))), len(emitted))

    def test_serialized_semantic_error_and_missing_release_rejected(self):
        for mutation in ({"bad_port": 0}, {"release": False}):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as tmp:
                rows = _serialized_transform(Path(tmp) / "synthetic-invalid.mwro", mutation=mutation)
                receiver = SheikTransformPrefixReceiver(make_input_plan(8), sheik_transform_prefix_packet())
                with self.assertRaises(ValueError):
                    for row in rows:
                        receiver.accept(row)
                    receiver.finish()

    def test_exact_recipe_input_and_default_mario_route_compatibility(self):
        plan = make_input_plan(8)
        declaration = recipe(8)
        self.assertEqual(plan["source_characters"], [18, 8])
        self.assertEqual(plan["controlled_ports"], [1, 2])
        self.assertEqual(plan["frames"], [declaration["input_witness"]["press"],
                                           declaration["input_witness"]["release"]])
        self.assertEqual(plan["frames"][0], [raw_pad(buttons=["B"], y=-80), NEUTRAL_PAD,
                                              DISCONNECTED_PAD, DISCONNECTED_PAD])
        self.assertEqual(plan["frames"][1], [NEUTRAL_PAD, NEUTRAL_PAD,
                                              DISCONNECTED_PAD, DISCONNECTED_PAD])
        verify_tick(plan, 0, plan["frames"][0])
        verify_tick(plan, 1, plan["frames"][1])
        with self.assertRaisesRegex(ValueError, "Input intent mismatch"):
            verify_tick(plan, 0, [raw_pad(buttons=["B"], y=-79), *plan["frames"][0][1:]])

        default = make_input_plan()
        self.assertEqual(default["source_characters"], [8, 8])
        self.assertEqual(default["controlled_ports"], [1, 2])
        self.assertEqual(len(default["frames"]), 4323)
        self.assertEqual(validate_packet(rules_ready_packet()), rules_ready_packet())

    def test_actual_finally_preserves_primary_and_closes_owned_log(self):
        import ast
        from unittest.mock import Mock
        import capture_sd_reference_prefix as driver
        tree = ast.parse(Path(driver.__file__).read_text())
        run = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "_run")
        blocks = [node for node in ast.walk(run) if isinstance(node, ast.Try) and node.finalbody
                  and any(isinstance(child, ast.Call) and isinstance(child.func, ast.Name)
                          and child.func.id == "cleanup_process" for item in node.finalbody
                          for child in ast.walk(item))]
        self.assertEqual(len(blocks), 1)
        code = compile(ast.fix_missing_locations(ast.Module(body=blocks[0].finalbody,
                                                            type_ignores=[])), driver.__file__, "exec")
        for cleanup_fails, log_fails in ((False, False), (True, False), (False, True), (True, True)):
            with self.subTest(cleanup_fails=cleanup_fails, log_fails=log_fails), tempfile.TemporaryDirectory() as scratch:
                cleanup = Mock(side_effect=RuntimeError("cleanup failure") if cleanup_fails else None)
                log = Mock(size=17)
                log.thread.is_alive.return_value = False
                log.finish.side_effect = RuntimeError("log failure") if log_fails else None
                scope = dict(driver.__dict__, process=Mock(pid=123), output=Path(scratch),
                             scope="sheik_transform_prefix", ordinary=False, transform_prefix=True,
                             bounded_log=log, cleanup_process=cleanup)
                (Path(scratch) / "report.json").write_text(json.dumps({"status": "pass"}))
                primary = RuntimeError("original input failure")
                try:
                    raise primary
                except RuntimeError:
                    exec(code, scope)
                self.assertIs(scope["primary_error"], primary)
                candidate = json.loads((Path(scratch) / "report.json").read_text())
                self.assertEqual(candidate["status"], "fail" if cleanup_fails or log_fails else "pass")
                cleanup.assert_called_once()
                log.finish.assert_called_once()
                retained = json.loads((Path(scratch) / "owned-log-cleanup.json").read_text())
                self.assertEqual(retained["native_cleanup_error"], "cleanup failure" if cleanup_fails else None)
                self.assertEqual(retained["error"], "log failure" if log_fails else None)

    def test_independent_transform_finalization_requires_both_writers(self):
        from unittest.mock import patch
        import capture_sd_reference_prefix as driver
        with tempfile.TemporaryDirectory() as scratch:
            observer, native = Path(scratch) / "observer", Path(scratch) / "input"
            observer.touch(); native.touch()
            primary = {"invalid": False, "error": None, "state": "completed"}
            inputs = {"invalid": False, "error": None, "complete": True}
            with patch.object(driver, "read_status", return_value=primary), patch.object(driver, "validate_status", return_value=inputs):
                self.assertEqual(driver.wait_transform_terminal_statuses(observer, native, driver.time.monotonic() + 1), (primary, inputs))
            with patch.object(driver, "read_status", return_value=primary), patch.object(driver, "validate_status", return_value={**inputs, "invalid": True}):
                with self.assertRaisesRegex(driver.SdDiagnosticError, "input writer failed"):
                    driver.wait_transform_terminal_statuses(observer, native, driver.time.monotonic() + 1)
            with self.assertRaisesRegex(driver.SdDiagnosticError, "deadline expired"):
                driver.wait_transform_terminal_statuses(observer, native, 0)

    def test_menu_geometry_separates_zelda_row_from_hud_icon(self):
        packet = validate_packet(sheik_transform_prefix_packet())
        css = packet["css"]
        source = css["geometry_source"]
        self.assertNotIn("profile_gci_sha256", packet)
        self.assertEqual(packet["scope"], "sheik_transform_prefix")
        self.assertEqual(packet["version"], 10)
        self.assertEqual(css["source_slots"], [0, 1])
        self.assertEqual(css["characters"], [18, 8])
        self.assertEqual(css["icon_table_indices"], [15, 1])
        self.assertEqual(css["hud_icons"], [18, 1])
        self.assertEqual(packet["sss"]["stage_kind"], 32)
        self.assertEqual(source["dependency_commit"],
                         "b43912cc78606f96c9569f5d6229bc9d7e265ea5")
        lock = json.loads((ROOT / "dependencies.lock.json").read_text(encoding="utf-8"))
        self.assertEqual(lock["repositories"]["melee"]["commit"], source["dependency_commit"])
        self.assertEqual(source["sha256"],
                         "7d8ab4fd55c5904bb05065a5b1e57b00693998d2446894d9b0b6a695d1cb77de")
        for row, point in zip(source["rows"], css["points"]):
            self.assertEqual(row["point"], point)
            self.assertLess(row["bounds"]["x"][0], point[0])
            self.assertLess(point[0], row["bounds"]["x"][1])
            self.assertLess(row["bounds"]["y"][0], point[1])
            self.assertLess(point[1], row["bounds"]["y"][1])
        allowed = route_pads(packet)
        self.assertIn((raw_pad(buttons=["A"]), NEUTRAL_PAD), allowed)
        self.assertIn((NEUTRAL_PAD, raw_pad(buttons=["A"])), allowed)

    def test_live_menu_readiness_requires_mode2_css_then_sss_polls(self):
        receiver = SheikTransformPrefixReceiver(make_input_plan(8),
                                                sheik_transform_prefix_packet())
        receiver._pad_poll(_css_poll(0, 1))
        self.assertIsNone(receiver.css)
        self.assertIsNone(receiver.css_live_owner_sequence)
        receiver._pad_poll(_css_poll(1, 2, include_cursor_models=False))
        self.assertIsNone(receiver.css)
        self.assertEqual(receiver.css_live_owner_sequence, 1)
        receiver._pad_poll(_css_poll(2, 2))
        self.assertIsNotNone(receiver.css)
        self.assertEqual(receiver.css_live_owner_sequence, 1)
        receiver._pad_poll(_sss_poll(3, 1))
        self.assertIsNone(receiver.stage)
        self.assertIsNone(receiver.sss_live_owner_sequence)
        receiver._pad_poll(_sss_poll(4, 2))
        self.assertEqual(receiver.stage["kind"], 32)
        self.assertEqual(receiver.sss_live_owner_sequence, 4)
        self.assertEqual(receiver.menu_polls, 5)

    def test_setup_verifier_requires_zeldamario_four_stock_fd(self):
        plan = make_input_plan(8)
        setup = _setup_bytes()
        verify_entry(plan, setup.hex())
        changed = bytearray(setup)
        changed[0x60] = 8
        with self.assertRaisesRegex(ValueError, "differs from the authored setup"):
            verify_entry(plan, changed.hex())

    def test_source_consumer_accepts_only_exact_press_then_neutral_release(self):
        plan = make_input_plan(8)
        menus = sheik_transform_prefix_packet()
        receiver = SheikTransformPrefixReceiver(plan, menus)
        receiver.setup_seen = True
        receiver.neutral_consume_sequence = 10
        receiver.grounded_zelda_sequence = 12
        press = plan["frames"][0]
        release = plan["frames"][1]
        receiver._pad_consume({"seq": 14, "payload": _consume_payload(press, [0, 0, -1, -1])})
        self.assertTrue(receiver.down_b_consumed)
        self.assertEqual(receiver.down_b_consume_sequence, 14)
        receiver._pad_consume({"seq": 17, "payload": _consume_payload(release, [0, 0, -1, -1])})
        self.assertTrue(receiver.down_b_released)
        self.assertEqual([row["sequence"] for row in receiver.consumed_samples], [14, 17])

        invalid = SheikTransformPrefixReceiver(plan, menus)
        invalid.setup_seen = True
        invalid.neutral_consume_sequence = 10
        invalid.grounded_zelda_sequence = 12
        wrong_press = [raw_pad(buttons=["B"], y=-79), *press[1:]]
        with self.assertRaisesRegex(ValueError, "exact B/Y=-80"):
            invalid._pad_consume({"seq": 14,
                                  "payload": _consume_payload(wrong_press, [0, 0, -1, -1])})


if __name__ == "__main__":
    unittest.main()
