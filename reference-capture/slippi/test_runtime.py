# SPDX-License-Identifier: MIT
"""Focused checks for private Slippi profile and controller setup."""

import hashlib
import json
import os
from pathlib import Path
import socket
import stat
import struct
import tempfile
import unittest

from runtime import (ControllerPipe, MEMORY_WATCHER_SNAPSHOT_MARKER, MemoryWatcher,
                     WATCHED_WORDS, create_client_profile)


def _initial_memory_packets(values):
    snapshot = dict.fromkeys(set(WATCHED_WORDS), 0)
    snapshot.update(values)
    packets = []
    current = ""
    marker_size = len(MEMORY_WATCHER_SNAPSHOT_MARKER) + len("\n1\n")
    for key, value in sorted(snapshot.items()):
        record = f"{key}\n{value:08x}\n"
        if current and len(current) + len(record) + marker_size > 1024:
            packets.append(current.encode("ascii"))
            current = ""
        current += record
    current += f"{MEMORY_WATCHER_SNAPSHOT_MARKER}\n1\n"
    packets.append(current.encode("ascii"))
    return packets


class LocalRuntimeTests(unittest.TestCase):
    def test_profile_isolated_identity_ports_and_game_modification(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="p-") as directory:
            root = Path(directory)
            mod = root / "GALE01r2.ini"
            mod.write_text("pinned game modification\n", encoding="utf-8")
            digest = hashlib.sha256(mod.read_bytes()).hexdigest()
            profile = create_client_profile(
                root=root / "client1",
                replay_root=root / "replays1",
                identity="local-player-1",
                play_key="local-play-key-1",
                display_name="Local player 1",
                connect_code="LOCAL#001",
                opponent_code="LOCAL#002",
                peer_port=41001,
                spectator_port=41011,
                game_modification=mod,
                expected_modification_sha256=digest,
                latest_version="0.0.0",
            )
            self.assertTrue(stat.S_ISFIFO(profile.pads[0].stat().st_mode))
            self.assertTrue(stat.S_ISFIFO(profile.pads[1].stat().st_mode))
            self.assertEqual(profile.pads[0].stat().st_mode & 0o777, 0o600)
            self.assertEqual(profile.pads[1].stat().st_mode & 0o777, 0o600)
            pad_config = (profile.user_root / "Config" / "GCPadNew.ini").read_text()
            self.assertIn("Device = Pipe/0/pad1", pad_config)
            self.assertIn("Device = Pipe/0/pad2", pad_config)
            self.assertEqual(
                (profile.user_root / "GameSettings" / "GALE01r2.ini").read_bytes(),
                mod.read_bytes(),
            )
            user = json.loads((profile.user_root / "Slippi" / "user.json").read_text())
            codes = json.loads(
                (profile.user_root / "Slippi" / "direct-codes.json").read_text()
            )
            self.assertEqual(user["uid"], "local-player-1")
            self.assertEqual(user["playKey"], "local-play-key-1")
            self.assertEqual(user["latestVersion"], "0.0.0")
            self.assertEqual(codes[0]["connectCode"], "LOCAL#002")
            dolphin_ini = (profile.user_root / "Config" / "Dolphin.ini").read_text()
            logger_ini = (profile.user_root / "Config" / "Logger.ini").read_text()
            self.assertIn("EnableSpectator = False", dolphin_ini)
            self.assertIn("NetplayPort = 41001", dolphin_ini)
            self.assertIn("Backend = No Audio Output", dolphin_ini)
            self.assertIn("SLIPPI_ONLINE = True", logger_ini)
            self.assertIn("Verbosity = 4", logger_ini)
            self.assertIn("WriteToConsole = True", logger_ini)
            self.assertIn("WriteToFile = True", logger_ini)

    def test_profile_rejects_external_identity_and_wrong_game_modification(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="e-") as directory:
            root = Path(directory)
            mod = root / "GALE01r2.ini"
            mod.write_text("local fixture\n", encoding="utf-8")
            args = {
                "replay_root": root / "replays",
                "identity": "local-player",
                "play_key": "local-key",
                "display_name": "Local player",
                "connect_code": "LOCAL#001",
                "opponent_code": "LOCAL#002",
                "peer_port": 41001,
                "spectator_port": 41011,
                "game_modification": mod,
                "expected_modification_sha256": "0" * 64,
                "latest_version": "0.0.0",
            }
            with self.assertRaisesRegex(ValueError, "hash differs"):
                create_client_profile(root=root / "client", **args)
            with self.assertRaisesRegex(ValueError, "local test identity"):
                create_client_profile(
                    root=root / "official-client",
                    **{**args, "identity": "official-account"},
                )

    def test_memory_watcher_decodes_only_configured_source_words(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="w-") as directory:
            root = Path(directory)
            mod = root / "GALE01r2.ini"
            mod.write_text("local fixture\n", encoding="utf-8")
            digest = hashlib.sha256(mod.read_bytes()).hexdigest()
            profile = create_client_profile(
                root=root / "client1",
                replay_root=root / "replays1",
                identity="local-player-1",
                play_key="local-key-1",
                display_name="Local player 1",
                connect_code="LOCAL#001",
                opponent_code="LOCAL#002",
                peer_port=41001,
                spectator_port=41011,
                game_modification=mod,
                expected_modification_sha256=digest,
                latest_version="0.0.0",
            )
            with MemoryWatcher(profile) as watcher:
                sender = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
                try:
                    sender.sendto(
                        b"80479d58\n10\n",
                        str(profile.memory_socket),
                    )
                    self.assertEqual(watcher.receive(1), {})
                    sender.sendto(
                        f"{MEMORY_WATCHER_SNAPSHOT_MARKER}\n1\n".encode("ascii"),
                        str(profile.memory_socket),
                    )
                    with self.assertRaisesRegex(ValueError, "initial scan omitted"):
                        watcher.receive(1)
                    self.assertFalse(watcher._received_initial_snapshot)
                    self.assertEqual(watcher.values, {})

                    snapshot = {"80479d58": 0x10}
                    packets = _initial_memory_packets(snapshot)
                    for payload in packets:
                        sender.sendto(payload, str(profile.memory_socket))
                finally:
                    sender.close()
                while not watcher._received_initial_snapshot:
                    received = watcher.receive(1)
                self.assertEqual(received, watcher.values)
                self.assertEqual(len(watcher.values), len(set(WATCHED_WORDS)))
                self.assertEqual(watcher.values["80479d58"], 0x10)
                self.assertEqual(watcher.values["804a04f0"], 0)
            self.assertIn("804d6720 0", WATCHED_WORDS)

    def test_memory_watcher_decodes_scene_css_and_stage_state(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="s-") as directory:
            root = Path(directory)
            mod = root / "GALE01r2.ini"
            mod.write_text("local fixture\n", encoding="utf-8")
            digest = hashlib.sha256(mod.read_bytes()).hexdigest()
            profile = create_client_profile(
                root=root / "client1",
                replay_root=root / "replays1",
                identity="local-player-1",
                play_key="local-key-1",
                display_name="Local player 1",
                connect_code="P1",
                opponent_code="P2",
                peer_port=41001,
                spectator_port=41011,
                game_modification=mod,
                expected_modification_sha256=digest,
                latest_version="0.0.0",
            )
            with MemoryWatcher(profile) as watcher:
                sender = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
                try:
                    payloads = _initial_memory_packets({
                        "804d6720 0": 0x08000000,
                        "804d6cae": 0x03000000,
                        "803f072f": 0x20000000,
                        "804d6cb0 70": 0x08000400,
                    })
                    for payload in payloads:
                        sender.sendto(payload, str(profile.memory_socket))
                finally:
                    sender.close()
                while not watcher._received_initial_snapshot:
                    watcher.receive(1)
                self.assertEqual(watcher.scene_kind(), 8)
                self.assertEqual(watcher.selected_stage(), (3, 32))
                self.assertEqual(
                    watcher.css_player(1),
                    {"character_kind": 8, "slot_type": 0, "stocks": 4, "costume": 0},
                )

    def test_memory_watcher_decodes_original_css_cursor_targets(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="c-") as directory:
            root = Path(directory)
            mod = root / "GALE01r2.ini"
            mod.write_text("local fixture\n", encoding="utf-8")
            digest = hashlib.sha256(mod.read_bytes()).hexdigest()
            profile = create_client_profile(
                root=root / "client1",
                replay_root=root / "replays1",
                identity="local-player-1",
                play_key="local-key-1",
                display_name="Local player 1",
                connect_code="P1",
                opponent_code="P2",
                peer_port=41001,
                spectator_port=41011,
                game_modification=mod,
                expected_modification_sha256=digest,
                latest_version="0.0.0",
            )
            with MemoryWatcher(profile) as watcher:
                for index in range(25):
                    base = 0x803F0B24 + index * 0x1C
                    kind = 8 if index == 1 else 0 if index == 7 else 1
                    available = 1 if index in (1, 7) else 0
                    row = {
                        base + 1: (kind << 24),
                        base + 2: (available << 24),
                        base + 12: int.from_bytes(struct.pack(">f", -1.0), "big"),
                        base + 16: int.from_bytes(struct.pack(">f", 1.0), "big"),
                        base + 20: int.from_bytes(struct.pack(">f", 2.0), "big"),
                        base + 24: int.from_bytes(struct.pack(">f", -2.0), "big"),
                    }
                    watcher.values.update({f"{key:08x}": value for key, value in row.items()})
                watcher.values.update({
                    "804a0bc0 c": int.from_bytes(struct.pack(">f", 3.0), "big"),
                    "804a0bc0 10": int.from_bytes(struct.pack(">f", 4.0), "big"),
                    "804a0bc0 4": 0,
                    "804a0bc0 5": 0,
                    "804a0bc0 6": 0,
                    "804a0bd0 8": int.from_bytes(struct.pack(">f", 0.5), "big"),
                    "804a0bd0 c": int.from_bytes(struct.pack(">f", 0.75), "big"),
                    "803f0e0a": 1 << 24,
                    "804a0bc4 c": int.from_bytes(struct.pack(">f", -3.0), "big"),
                    "804a0bc4 10": int.from_bytes(struct.pack(">f", -4.0), "big"),
                    "804a0bc4 4": 1 << 24,
                    "804a0bc4 5": 0,
                    "804a0bc4 6": 0,
                    "804a0bd4 8": int.from_bytes(struct.pack(">f", -0.5), "big"),
                    "804a0bd4 c": int.from_bytes(struct.pack(">f", -0.75), "big"),
                    "803f0e1e": 1 << 24,
                    "803f0e2e": 7 << 24,
                })
                self.assertEqual(
                    watcher.css_cursor(1),
                    {
                        "cursor": (3.0, 4.0),
                        "model": (0.5, 0.75),
                        "selected": 8,
                        "held": -1,
                        "cursor_port": 0,
                        "cursor_mode": 0,
                        "cursor_target": 0,
                        "bounds": (-1.0, 1.0, 2.0, -2.0),
                        "target": 1,
                        "available_icons": [
                            {"index": 1, "kind": 8, "bounds": (-1.0, 1.0, 2.0, -2.0)},
                            {"index": 7, "kind": 0, "bounds": (-1.0, 1.0, 2.0, -2.0)},
                        ],
                    },
                )
                self.assertEqual(watcher.css_cursor(2)["selected"], 0)

    def test_controller_pipe_sends_ordinary_button_and_axis_state(self):
        with tempfile.TemporaryDirectory(dir="/tmp", prefix="q-") as directory:
            root = Path(directory)
            fifo = root / "pad1"
            log = root / "input.jsonl"
            os.mkfifo(fifo, 0o600)
            reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                controller = ControllerPipe(fifo, log, "p1")
                controller.set_axis("MAIN", 1.0, 0.5)
                data = os.read(reader, 4096).decode("ascii")
                self.assertIn("SET MAIN 1 0.5", data)
                controller.set_button("A", True)
                data = os.read(reader, 4096).decode("ascii")
                self.assertIn("PRESS A", data)
                records = [json.loads(line) for line in log.read_text().splitlines()]
                self.assertEqual(records[-1]["buttons"], ["A"])
            finally:
                os.close(reader)


if __name__ == "__main__":
    unittest.main()
