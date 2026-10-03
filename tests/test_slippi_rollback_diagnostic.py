# SPDX-License-Identifier: MIT
"""Strict controls/configuration checks for the desktop rollback diagnostic."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from slippi_rollback_diagnostic import (  # noqa: E402
    DiagnosticConfigError,
    DiagnosticConfig,
    InputProfile,
    InputOverlay,
    TransportAction,
    load_config,
    parse_config,
    write_config,
)


def _config() -> dict:
    return {
        "schema": "melee-web-slippi-rollback-diagnostic-v1",
        "enabled": True,
        "rng_offset": 0x1234,
        "log_path": "/tmp/rollback-diagnostic.jsonl",
        "stage_id": 32,
        "input_profile": {"name": "mario-fd-rollback-v1", "role": 1},
        "overlay": {"frame": 96, "pad_hex": "01007f8080802a00"},
        "transport": {"action": "hold", "frame": 96, "release_frame": 98},
    }


class SlippiRollbackDiagnosticConfigTests(unittest.TestCase):
    def test_parses_fixed_rng_overlay_and_bounded_hold(self):
        parsed = parse_config(_config())
        self.assertEqual(parsed.rng_offset, 0x1234)
        self.assertEqual(parsed.stage_id, 32)
        self.assertEqual(parsed.input_profile, InputProfile("mario-fd-rollback-v1", 1))
        self.assertEqual(parsed.overlay, InputOverlay(96, "01007f8080802a00"))
        self.assertEqual(parsed.transport, TransportAction("hold", 96, 98))

    def test_role2_duplicate_has_one_packet_frame_and_no_release(self):
        value = _config()
        value.update(
            input_profile={"name": "mario-fd-rollback-v1", "role": 2},
            overlay=None,
            transport={"action": "duplicate", "frame": 98, "release_frame": None},
        )
        parsed = parse_config(value)
        self.assertEqual(parsed.transport, TransportAction("duplicate", 98, None))
        self.assertEqual(parsed.input_profile, InputProfile("mario-fd-rollback-v1", 2))

        for role in (1, None):
            changed = dict(value)
            changed["input_profile"] = None if role is None else {
                "name": "mario-fd-rollback-v1", "role": role,
            }
            with self.subTest(role=role), self.assertRaisesRegex(
                    DiagnosticConfigError, "role-2 input profile"):
                parse_config(changed)
        changed = dict(value)
        changed["transport"] = {"action": "duplicate", "frame": 98, "release_frame": 99}
        with self.assertRaisesRegex(DiagnosticConfigError, "null release_frame"):
            parse_config(changed)

    def test_role2_jitter_and_reorder_require_one_frame_release(self):
        for action in ("jitter", "reorder"):
            value = _config()
            value.update(
                input_profile={"name": "mario-fd-rollback-v1", "role": 2},
                overlay=None,
                transport={"action": action, "frame": 98, "release_frame": 99},
            )
            parsed = parse_config(value)
            self.assertEqual(parsed.transport, TransportAction(action, 98, 99))
            for role in (1, None):
                changed = copy.deepcopy(value)
                changed["input_profile"] = None if role is None else {
                    "name": "mario-fd-rollback-v1", "role": role,
                }
                with self.subTest(action=action, role=role), self.assertRaisesRegex(
                        DiagnosticConfigError, "role-2 input profile"):
                    parse_config(changed)
            changed = copy.deepcopy(value)
            changed["transport"]["release_frame"] = None
            with self.subTest(action=action, invalid="missing release"), self.assertRaises(
                    DiagnosticConfigError):
                parse_config(changed)
            changed = copy.deepcopy(value)
            changed["transport"]["release_frame"] = 107
            with self.subTest(action=action, invalid="wide release"), self.assertRaises(
                    DiagnosticConfigError):
                parse_config(changed)

    def test_round_trip_writes_canonical_json(self):
        parsed = parse_config(_config())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diagnostic.json"
            write_config(path, parsed)
            self.assertEqual(load_config(path), parsed)
            self.assertEqual(json.loads(path.read_text()), parsed.as_dict())

    def test_write_config_refuses_overwrite_and_preserves_bytes(self):
        parsed = parse_config(_config())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diagnostic.json"
            original = b"operator-owned config\x00bytes\n"
            path.write_bytes(original)
            with self.assertRaises(DiagnosticConfigError):
                write_config(path, parsed)
            self.assertEqual(path.read_bytes(), original)

    def test_rejects_unknown_or_missing_keys(self):
        for mutation in (lambda value: value.update(extra=True),
                         lambda value: value.pop("rng_offset")):
            value = _config()
            mutation(value)
            with self.subTest(value=value), self.assertRaises(DiagnosticConfigError):
                parse_config(value)

    def test_rejects_wrong_pad_length_and_unbounded_hold(self):
        value = _config()
        value["overlay"]["pad_hex"] = "00"
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)
        value = _config()
        value["overlay"]["pad_hex"] = "00007f8080802a 0"
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)
        value = _config()
        value["transport"]["action"] = ["hold"]
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)

    def test_rejects_unsupported_stage_and_unsafe_numeric_values(self):
        for stage_id in (0, 31, 33, 0xFFFF, True, 32.0):
            value = _config()
            value["stage_id"] = stage_id
            with self.subTest(stage_id=stage_id), self.assertRaises(DiagnosticConfigError):
                parse_config(value)

        for profile in (
            {"name": "other", "role": 1},
            {"name": "mario-fd-rollback-v1", "role": 0},
            {"name": "mario-fd-rollback-v1", "role": 3},
            {"name": "mario-fd-rollback-v1", "role": True},
        ):
            value = _config()
            value["input_profile"] = profile
            with self.subTest(profile=profile), self.assertRaises(DiagnosticConfigError):
                parse_config(value)

        for key, number in (("rng_offset", True), ("rng_offset", 1.5),
                            ("transport", {"action": "hold", "frame": 0,
                                             "release_frame": 1}),
                            ("transport", {"action": "hold", "frame": 96,
                                             "release_frame": 1_000_005})):
            value = _config()
            value[key] = number
            with self.subTest(key=key, number=number), self.assertRaises(DiagnosticConfigError):
                parse_config(value)

    def test_rejects_nul_log_path(self):
        value = _config()
        value["log_path"] = "/tmp/rollback\x00.jsonl"
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)
        value = _config()
        value["transport"]["release_frame"] = 105
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)

    def test_rejects_disabled_config_with_active_controls(self):
        value = _config()
        value["enabled"] = False
        with self.assertRaises(DiagnosticConfigError):
            parse_config(value)

    def test_disabled_config_is_valid_only_when_empty(self):
        value = copy.deepcopy(_config())
        value.update(enabled=False, stage_id=None, overlay=None,
                     input_profile=None,
                     transport={"action": "none", "frame": None, "release_frame": None})
        parsed = parse_config(value)
        self.assertFalse(parsed.enabled)


if __name__ == "__main__":
    unittest.main()
