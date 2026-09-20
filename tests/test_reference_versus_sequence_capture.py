import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "reference-capture" / "dolphin"))
import reference_observer_stream as observer  # noqa: E402
import reference_versus_sequence_capture as runner  # noqa: E402


def _frame(event, sequence, payload):
    header = observer.HEADER.pack(
        observer.MAGIC_U32, observer.SCHEMA_VERSION, event, sequence,
        sequence + 100, 0, sequence, sequence, len(payload),
        zlib.crc32(payload) & 0xffffffff)
    return header + payload


def _whole_boundary(kind, match_index):
    prefix = observer.BOUNDARY.pack(kind, observer.WHOLE_SESSION_FLAG,
                                    0x80300000, 32, 0, 0)
    gprs = struct.pack("<32I", *range(32))
    return prefix + gprs + observer.WHOLE_METADATA.pack(match_index, kind, 0)


class ReferenceVersusSequenceCaptureTest(unittest.TestCase):
    def test_inventory_requires_an_explicit_ordinary_recipe(self):
        inventory = runner.load_inventory("repeatMario")
        self.assertEqual(len(inventory), 3)
        with self.assertRaises(runner.CaptureError):
            runner.load_recipe(None, "repeatMario", len(inventory))
        with self.assertRaises(runner.CaptureError):
            runner.load_recipe(None, "rotate", 4)

    def test_recipe_decodes_an_explicit_css_and_sss_route(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "recipe.json"
            path.write_text(json.dumps({
                "sequence": "repeatMario",
                "css": [{"p1": {"buttons": ["START"]},
                          "p2": {"buttons": ["START"]}, "label": "css-confirm"}],
                "sss": [{"p1": {"buttons": ["A"]},
                          "p2": {"buttons": ["A"]}, "label": "sss-confirm"}],
            }), encoding="utf-8")
            recipe = runner.load_recipe(path, "repeatMario", 3)
            self.assertEqual(len(recipe), 3)
            self.assertEqual(recipe[0]["css"][0].label, "css-confirm")
            self.assertEqual(recipe[0]["sss"][0].p1,
                             runner.raw_pad(buttons=["A"]))

    def test_prepare_dual_pipe_preserves_an_existing_ini(self):
        with tempfile.TemporaryDirectory() as temp:
            user = Path(temp) / "user"
            config = user / "Config"
            config.mkdir(parents=True)
            (config / "Dolphin.ini").write_text("[Core]\nSomeSetting = 7\n", encoding="utf-8")
            (config / "GCPadNew.ini").write_text("[Other]\nKeep = value\n", encoding="utf-8")
            p1, p2 = runner.prepare_dual_pipe(user)
            self.assertTrue(p1.is_fifo())
            self.assertTrue(p2.is_fifo())
            core = runner.configparser.ConfigParser(interpolation=None)
            core.read(config / "Dolphin.ini")
            self.assertEqual(core.get("Core", "SomeSetting"), "7")
            self.assertEqual(core.get("Core", "SIDevice0"), "6")
            self.assertEqual(core.get("Core", "SIDevice1"), "6")
            pad = runner.configparser.ConfigParser(interpolation=None)
            pad.read(config / "GCPadNew.ini")
            self.assertEqual(pad.get("Other", "Keep"), "value")
            self.assertEqual(pad.get("GCPad1", "Device"), "Pipe/0/pad1")
            self.assertEqual(pad.get("GCPad2", "Device"), "Pipe/0/pad2")

    def test_pipe_controller_writes_both_ports_without_retry(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            p1, p2 = root / "pad1", root / "pad2"
            os.mkfifo(p1)
            os.mkfifo(p2)
            readers = [os.open(path, os.O_RDONLY | os.O_NONBLOCK) for path in (p1, p2)]
            try:
                controller = runner.DualPipeController(
                    p1, p2, root / "inputs.jsonl", sleep=lambda _seconds: None)
                pad = runner.raw_pad(buttons=["START"])
                controller.set_both(pad, pad, action="test-start")
                packets = [os.read(fd, 4096) for fd in readers]
                self.assertTrue(all(b"PRESS START\n" in packet for packet in packets))
                rows = [json.loads(line) for line in (root / "inputs.jsonl").read_text().splitlines()]
                self.assertEqual([row["port"] for row in rows], [1, 2])
                self.assertTrue(all(row["intended_pad"] == pad for row in rows))
            finally:
                for fd in readers:
                    os.close(fd)

    def test_live_observer_requires_whole_announcements_and_reads_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "observer.bin"
            handshake = json.dumps({"whole_session": True, "match_count": 3,
                                    "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            start = json.dumps({"whole_session": True, "match_count": 3,
                                "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            end = json.dumps({"complete": True}).encode()
            path.write_bytes(_frame(1, 0, handshake) + _frame(2, 1, start) +
                             _frame(3, 2, _whole_boundary(13, 0)) + _frame(6, 3, end))
            tail = runner.ObserverTail(path, None, poll=0.001)
            tail.require_announcements(3, runner.time.monotonic() + 1,
                                       capture_id="capture-1", sequence_id="sequence-1")
            self.assertEqual(tail.identity,
                             {"capture_id": "capture-1", "sequence_id": "sequence-1"})
            row = tail.wait_boundary("css_enter", 0, runner.time.monotonic() + 1)
            self.assertEqual(row["payload"]["boundary"], "css_enter")
            self.assertEqual(tail.next(runner.time.monotonic() + 1)["event"], "end")
            tail.close()

    def test_initial_css_admits_only_the_ordered_startup_prize_prelude(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "observer.bin"
            handshake = json.dumps({"whole_session": True, "match_count": 3,
                                    "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            start = json.dumps({"whole_session": True, "match_count": 3,
                                "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            kinds = [26, 27, 28, 30, 13]
            payload = b"".join(_frame(3, index + 2, _whole_boundary(kind, 0))
                                for index, kind in enumerate(kinds))
            path.write_bytes(_frame(1, 0, handshake) + _frame(2, 1, start) + payload)
            tail = runner.ObserverTail(path, None, poll=0.001)
            tail.require_announcements(3, runner.time.monotonic() + 1,
                                       capture_id="capture-1", sequence_id="sequence-1")
            result = tail.wait_initial_css(runner.time.monotonic() + 1)
            self.assertEqual([row["payload"]["boundary"] for row in result["startup_prize"]], [
                "prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                "startup_prize_mode_exit"])
            self.assertEqual(result["css"]["payload"]["boundary"], "css_enter")
            tail.close()

    def test_initial_css_rejects_incomplete_startup_prize_prelude(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "observer.bin"
            handshake = json.dumps({"whole_session": True, "match_count": 3,
                                    "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            start = json.dumps({"whole_session": True, "match_count": 3,
                                "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            path.write_bytes(_frame(1, 0, handshake) + _frame(2, 1, start) +
                             _frame(3, 2, _whole_boundary(26, 0)) +
                             _frame(3, 3, _whole_boundary(13, 0)))
            tail = runner.ObserverTail(path, None, poll=0.001)
            tail.require_announcements(3, runner.time.monotonic() + 1,
                                       capture_id="capture-1", sequence_id="sequence-1")
            with self.assertRaisesRegex(runner.CaptureError, "prelude was incomplete"):
                tail.wait_initial_css(runner.time.monotonic() + 1)
            tail.close()

    def test_setup_values_are_source_bytes_and_not_recipe_metadata(self):
        raw = bytearray(0x138)
        raw[0x60] = 8
        raw[0x84] = 8
        raw[14:16] = (32).to_bytes(2, "big")
        descriptor_offset = observer.BOUNDARY.size + 32 * 4 + observer.SLICE.size
        prefix = observer.BOUNDARY.pack(5, observer.WHOLE_SESSION_FLAG,
                                         0x80300000, 32, 1, 0)
        payload = (prefix + struct.pack("<32I", *range(32)) + observer.SLICE.pack(
            4, 0, 0x80400000, len(raw), descriptor_offset) + bytes(raw) +
            observer.WHOLE_METADATA.pack(0, 5, 0))
        row = observer._decode_record(
            observer.HEADER.unpack(_frame(3, 0, payload)[:observer.HEADER.size]),
            payload, "setup")
        self.assertEqual(runner._setup_values(row), (8, 8, 32))

    def test_source_fighter_state_requires_authored_head_and_stock_slices(self):
        def head(motion, ground_air):
            raw = bytearray(0x100)
            raw[0x10:0x14] = motion.to_bytes(4, "big")
            raw[0xe0:0xe4] = ground_air.to_bytes(4, "big")
            return bytes(raw).hex()

        row = {"event": "boundary", "payload": {"boundary": "source_tick", "slices": [
            {"name": "fighter_head", "flags": 0, "size": 0x100,
             "hex": head(14, 0)},
            {"name": "fighter_stocks", "flags": 0, "size": 1, "hex": "04"},
            {"name": "fighter_head", "flags": 1, "size": 0x100,
             "hex": head(14, 0)},
            {"name": "fighter_stocks", "flags": 1, "size": 1, "hex": "04"},
        ]}}
        self.assertEqual(runner._source_fighter_state(row),
                         {"stocks": (4, 4), "motions": (14, 14), "ground_air": (0, 0)})
        row["payload"]["slices"] = row["payload"]["slices"][:-1]
        with self.assertRaisesRegex(runner.CaptureError, "complete two-player"):
            runner._source_fighter_state(row)

    def test_source_state_wait_observes_a_stock_transition(self):
        def make_row(p1_stocks):
            def head():
                raw = bytearray(0x100)
                raw[0x10:0x14] = (14).to_bytes(4, "big")
                raw[0xe0:0xe4] = (0).to_bytes(4, "big")
                return bytes(raw).hex()
            return {"event": "boundary", "payload": {
                "whole_session": True, "boundary": "source_tick", "match_index": 0,
                "slices": [
                    {"name": "fighter_head", "flags": 0, "size": 0x100, "hex": head()},
                    {"name": "fighter_stocks", "flags": 0, "size": 1,
                     "hex": f"{p1_stocks:02x}"},
                    {"name": "fighter_head", "flags": 1, "size": 0x100, "hex": head()},
                    {"name": "fighter_stocks", "flags": 1, "size": 1, "hex": "04"},
                ]}}

        tail = runner.ObserverTail(Path("/dev/null"), None)
        rows = iter((make_row(4), make_row(3)))
        tail.next = lambda _deadline: next(rows)
        result = tail.wait_source_state(
            0, runner.time.monotonic() + 1,
            lambda state: state["stocks"] == (3, 4),
            description="P1 stock transition 4->3")
        self.assertEqual(result["state"]["stocks"], (3, 4))

    def test_announcements_reject_identity_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "observer.bin"
            handshake = json.dumps({"whole_session": True, "match_count": 3,
                                    "capture_id": "capture-1", "sequence_id": "sequence-1"}).encode()
            start = json.dumps({"whole_session": True, "match_count": 3,
                                "capture_id": "capture-2", "sequence_id": "sequence-1"}).encode()
            path.write_bytes(_frame(1, 0, handshake) + _frame(2, 1, start))
            tail = runner.ObserverTail(path, None, poll=0.001)
            try:
                with self.assertRaisesRegex(runner.CaptureError, "identities disagree"):
                    tail.require_announcements(3, runner.time.monotonic() + 1)
            finally:
                tail.close()


if __name__ == "__main__":
    unittest.main()
