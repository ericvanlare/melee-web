import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from reference_dolphin_replay import (
    DolphinReplayError, load_source, read_profile, snapshot_profile,
    verify_replay_environment,
)


class DolphinReplayTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_profile_round_trip_preserves_exact_configuration_bytes(self):
        profile = self.root / "Config"
        (profile / "nested").mkdir(parents=True)
        contents = {"Dolphin.ini": b"[Core]\nCPUThread = False\n",
                    "nested/controller.ini": b"[GCPad1]\nDevice = Pipe/0/pad1\n"}
        for name, data in contents.items():
            (profile / name).write_bytes(data)
        snapshot = self.root / "snapshot.json"
        snapshot.write_text(json.dumps(snapshot_profile(profile)))
        self.assertEqual(read_profile(snapshot), contents)

    def test_profile_cannot_escape_new_isolated_directory(self):
        snapshot = self.root / "snapshot.json"
        for name in ("../Dolphin.ini", "/Dolphin.ini", "nested/../../Dolphin.ini",
                     "nested\\Dolphin.ini", "./Dolphin.ini"):
            with self.subTest(name=name):
                snapshot.write_text(json.dumps({"version": 1, "files": {name: "00"}}))
                with self.assertRaises(DolphinReplayError):
                    read_profile(snapshot)
        self.assertFalse((self.root.parent / "Dolphin.ini").exists())

    def test_profile_rejects_symlink_malformed_empty_and_oversized_data(self):
        snapshot = self.root / "snapshot.json"
        for value in ({"version": 2, "files": {"a.ini": "00"}},
                      {"version": 1, "files": {}},
                      {"version": 1, "files": {"a.ini": "not-hex"}},
                      {"version": 1, "files": {"a.ini": "00" * (1024 * 1024 + 1)}}):
            snapshot.write_text(json.dumps(value))
            with self.assertRaises(DolphinReplayError):
                read_profile(snapshot)
        link = self.root / "link.json"
        link.symlink_to(snapshot)
        with self.assertRaises(DolphinReplayError):
            read_profile(link)

    def test_legacy_capture_is_preserved_and_rejected_with_actionable_message(self):
        capture = self.root / "legacy"
        capture.mkdir()
        receipt = capture / "manifest.json"
        receipt.write_bytes(b"legacy evidence remains unchanged\n")
        before = hashlib.sha256(receipt.read_bytes()).hexdigest()
        with self.assertRaisesRegex(DolphinReplayError, "needs a new recording"):
            load_source(capture)
        self.assertEqual(hashlib.sha256(receipt.read_bytes()).hexdigest(), before)
        self.assertEqual(list(capture.iterdir()), [receipt])

    def test_replay_requires_original_environment_but_not_live_controller(self):
        identity = {key: key + "-identity" for key in (
            "disc", "dolphin", "prepared_fixture_sha256", "prepared_fixture_kind",
            "timing_policy", "locale", "os")}
        source = {"header": {"environment": dict(identity, controller="connected")}}
        self.assertTrue(verify_replay_environment(source, dict(identity, controller="unavailable")))
        for key in identity:
            with self.subTest(key=key):
                with self.assertRaisesRegex(DolphinReplayError, "original"):
                    verify_replay_environment(source, dict(identity, **{key: "drifted"}))


if __name__ == "__main__":
    unittest.main()
