"""Synthetic checks for controller-only reference capture automation."""

from __future__ import annotations

import json
import os
from pathlib import Path
import select
import threading
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "tools"))

from reference_capture_automation import PipeController, prepare_pipe, wait_for_match_initial  # noqa: E402
from retail_input_plan import (  # noqa: E402
    DISCONNECTED_PAD,
    NEUTRAL_PAD,
    PAD,
    POLICY,
    SCHEMA,
)


def cpu_plan(*, version=2):
    value = {
        "schema": SCHEMA,
        "version": version,
        "policy": POLICY,
        "source_sha256": "a" * 64,
        "first_frame": -123,
        "source_stage": 32,
        "source_characters": [8, 18],
        "frames": [],
    }
    if version == 2:
        value.update({
            "source_player_types": [0, 1],
            "source_cpu_kinds": [None, 4],
            "source_cpu_levels": [None, 9],
            "source_cpu_pad_modes": [None, "disconnected"],
            "controlled_ports": [1],
        })
    return value


def write_plan(path: Path, *, version=2, controlled_ports=(1,), frame_count=2):
    value = cpu_plan(version=version)
    if version == 2:
        value["controlled_ports"] = list(controlled_ports)
        value["frames"] = [
            [PAD.pack(256 if index == 0 else 0, index, -index, 0, 0, 0, 0, 0, 0, 0).hex(),
             DISCONNECTED_PAD]
            for index in range(frame_count)
        ]
    else:
        value["frames"] = [[NEUTRAL_PAD, NEUTRAL_PAD] for _ in range(frame_count)]
    path.write_text(json.dumps(value), encoding="utf-8")
    return value


class _StopAfter:
    def __init__(self, stop_on_call):
        self.calls = 0
        self.stop_on_call = stop_on_call

    def wait(self, _timeout):
        self.calls += 1
        return self.calls >= self.stop_on_call


class ReferenceCaptureAutomationTests(unittest.TestCase):
    def test_prepare_pipe_configures_only_port_one_and_raw_packet_mapping(self):
        with self.subTest("configuration"), tempfile.TemporaryDirectory() as directory:
            user = Path(directory)
            (user / "Config").mkdir()
            (user / "Config/Dolphin.ini").write_text("[Core]\nSIDevice0 = 0\nSIDevice1 = 0\n")
            (user / "Config/GCPadNew.ini").write_text("[GCPad1]\nDevice = Keyboard\n")
            fifo = prepare_pipe(user)
            self.assertEqual(fifo, user / "Pipes/pad1")
            self.assertTrue(fifo.is_fifo())
            self.assertEqual(fifo.stat().st_mode & 0o777, 0o600)
            pad_config = (user / "Config/GCPadNew.ini").read_text()
            self.assertIn("Device = Pipe/0/pad1", pad_config)
            self.assertIn("Buttons/A = `Button A`", pad_config)
            self.assertIn("Main Stick/Right = `Axis MAIN X +`", pad_config)
            self.assertIn("Triggers/L-Analog = `Axis L +`", pad_config)
            dolphin_config = (user / "Config/Dolphin.ini").read_text()
            self.assertIn("SIDevice0 = 6", dolphin_config)
            self.assertIn("SIDevice1 = 0", dolphin_config)

    def test_write_sends_exact_pipe_commands_and_logs_human_port_zero(self):
        with __import__("tempfile").TemporaryDirectory() as directory:
            root = Path(directory)
            fifo = root / "pad1"
            os.mkfifo(fifo, 0o600)
            log = root / "intent.jsonl"
            ready = threading.Event()
            received = []

            def reader():
                fd = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
                ready.set()
                try:
                    while True:
                        readable, _, _ = select.select([fd], [], [], 1)
                        if not readable:
                            continue
                        data = os.read(fd, 4096)
                        if not data:
                            break
                        received.append(data)
                finally:
                    os.close(fd)

            thread = threading.Thread(target=reader)
            thread.start()
            self.assertTrue(ready.wait(1))
            pad = PAD.pack(256 | 64, 80, -80, 0, 0, 255, 0, 0, 0, 0).hex()
            PipeController(fifo, log).write(pad)
            thread.join(1)
            self.assertFalse(thread.is_alive())
            self.assertEqual(b"".join(received).decode("ascii"),
                             "RELEASE D_LEFT\nRELEASE D_RIGHT\n" +
                             "RELEASE D_DOWN\nRELEASE D_UP\n" +
                             "RELEASE Z\nRELEASE R\nPRESS L\nPRESS A\n" +
                             "RELEASE B\n" +
                             "RELEASE X\nRELEASE Y\nRELEASE START\n" +
                             "SET MAIN 0.81496062992125984 0.18503937007874016\n" +
                             "SET C 0.5 0.5\nSET L 1\nSET R 0\n")
            row = json.loads(log.read_text().strip())
            self.assertEqual(row["human_port"], 0)
            self.assertEqual(row["intended_pad"], pad)
            self.assertIsInstance(row["host_monotonic_ns"], int)
            self.assertGreater(row["host_monotonic_ns"], 0)

    def test_play_plan_writes_only_p1_and_neutralizes_on_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            plan_path = Path(directory) / "plan.json"
            p1_plan = write_plan(plan_path, frame_count=2)
            controller = PipeController(Path(directory) / "unused", Path(directory) / "log")
            writes = []
            controller.write = writes.append
            controller.play_plan(plan_path, _StopAfter(99))
            self.assertEqual(writes, [p1_plan["frames"][0][0], p1_plan["frames"][1][0], NEUTRAL_PAD])
            self.assertNotIn(DISCONNECTED_PAD, writes[:-1])

    def test_cancellation_releases_controls_and_does_not_continue_cpu_input(self):
        with tempfile.TemporaryDirectory() as directory:
            plan_path = Path(directory) / "plan.json"
            p1_plan = write_plan(plan_path, frame_count=2)
            controller = PipeController(Path(directory) / "unused", Path(directory) / "log")
            writes = []
            controller.write = writes.append
            controller.play_plan(plan_path, _StopAfter(2))
            self.assertEqual(writes, [p1_plan["frames"][0][0], NEUTRAL_PAD])

    def test_malformed_and_unsupported_plans_are_rejected_before_pipe_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            malformed = root / "malformed.json"
            malformed.write_text("{\"schema\": \"wrong\"}")
            controller = PipeController(root / "unused", root / "log")
            controller.write = mock.Mock()
            with self.assertRaises(ValueError):
                controller.play_plan(malformed, _StopAfter(99))
            controller.write.assert_not_called()

            unsupported = root / "v1.json"
            write_plan(unsupported, version=1)
            with self.assertRaisesRegex(ValueError, "only supplies"):
                controller.play_plan(unsupported, _StopAfter(99))
            controller.write.assert_not_called()

            bad_ports = root / "bad-ports.json"
            write_plan(bad_ports, controlled_ports=(2,))
            with self.assertRaisesRegex(ValueError, "control human P1"):
                controller.play_plan(bad_ports, _StopAfter(99))
            controller.write.assert_not_called()


class ObserverScheduledInputTests(unittest.TestCase):
    def test_plan_waits_for_setup_and_rejects_corrupt_transport(self):
        import struct
        from test_reference_observer_stream import frame, boundary_payload
        payload = struct.pack("<H", 5) + boundary_payload()[2:]
        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "observer.bin"
            valid = frame(1, 0, b"{}") + frame(2, 1, b"{}") + frame(3, 2, payload, pc=0x8016E9C4)
            raw.write_bytes(valid)
            self.assertEqual(wait_for_match_initial(raw, threading.Event(), timeout=1)["observer_seq"], 2)
            raw.write_bytes(valid[:-1] + bytes([valid[-1] ^ 1]))
            with self.assertRaisesRegex(ValueError, "checksum"):
                wait_for_match_initial(raw, threading.Event(), timeout=1)


if __name__ == "__main__":
    unittest.main()
