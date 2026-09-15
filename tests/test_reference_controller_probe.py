import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / "tools"),
               str(Path(__file__).resolve().parents[1] / "scripts")]
import reference_controller_probe as probe
from reference_capture_environment import EnvironmentError


class ControllerProbeTests(unittest.TestCase):
    def setUp(self):
        self.hardware = {"name": "USB Product", "vendor_id": 123, "product_id": 456,
                         "transport": "USB", "adapter": False}
        self.row = {"name": "Mapped Gamepad", "index": 0, "vendor_id": 123,
                    "product_id": 456, "virtual": False}
        self.payload = {"schema": "webmelee-controller-probe", "version": 1, "status": "ok",
                        "devices": [self.row]}

    def test_backend_and_hid_names_can_differ_only_with_matching_ids(self):
        result = probe.decode_controllers(self.payload, [self.hardware], "a" * 64)
        self.assertEqual(result[0]["name"], "Mapped Gamepad")
        self.assertEqual(result[0]["hardware"], self.hardware)
        self.assertEqual(result[0]["probe_sha256"], "a" * 64)
        unrelated = dict(self.hardware, name="Mapped Gamepad", product_id=789)
        self.assertIsNone(probe.decode_controllers(self.payload, [unrelated], "a" * 64)[0]["hardware"])

    def test_virtual_unknown_and_ambiguous_hardware_never_claim_connected(self):
        for changes, devices in (({"virtual": True}, [self.hardware]),
                                 ({"vendor_id": 0}, [self.hardware]),
                                 ({}, [self.hardware, self.hardware])):
            with self.subTest(changes=changes):
                payload = dict(self.payload, devices=[dict(self.row, **changes)])
                self.assertIsNone(probe.decode_controllers(payload, devices, "a" * 64)[0]["hardware"])

    def test_lost_or_malformed_identities_fail_closed(self):
        for changes in ({"index": -1}, {"index": True}, {"vendor_id": "123"},
                        {"virtual": None}, {"name": ""}):
            with self.subTest(changes=changes):
                with self.assertRaises(EnvironmentError):
                    probe.decode_controllers(dict(self.payload, devices=[dict(self.row, **changes)]), [], "a" * 64)
        with self.assertRaisesRegex(EnvironmentError, "duplicate"):
            probe.decode_controllers(dict(self.payload, devices=[self.row, self.row]), [], "a" * 64)
        with self.assertRaisesRegex(EnvironmentError, "Malformed"):
            probe.decode_controllers(dict(self.payload, status="error", devices=[]), [], "a" * 64)

    def _installation(self, root):
        digest = probe._hash(root / "source")
        folder = root / "ControllerProbes" / digest
        folder.mkdir(parents=True)
        binary = folder / "controller-probe"
        binary.write_bytes((root / "source").read_bytes())
        runtime = [{"load_name": "/usr/lib/libSystem.B.dylib", "system_managed": True}]
        build = {"schema": probe.BUILD_SCHEMA, "version": 1,
                 "dolphin_revision": probe.DOLPHIN_REVISION, "sdl_revision": probe.SDL_REVISION,
                 "binary": {"path": str(root / "source"), "sha256": digest},
                 "runtime_dependencies": runtime}
        receipt = folder / "build.json"
        receipt.write_text(json.dumps(build))
        settings = {"paths": {"profile": str(root / "Configuration")},
                    "controller_probe": {"binary": str(binary), "binary_sha256": digest,
                                         "receipt": str(receipt), "receipt_sha256": probe._hash(receipt)}}
        return settings, runtime, binary, receipt

    def test_installed_binary_receipt_and_runtime_drift_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "source").write_bytes(b"synthetic executable")
            settings, runtime, binary, receipt = self._installation(root)
            with patch.object(probe, "runtime_inventory", return_value=runtime):
                self.assertEqual(probe.verify_probe(settings), binary)
                for path in (binary, receipt):
                    original = path.read_bytes()
                    path.write_bytes(original + b"changed")
                    with self.assertRaisesRegex(EnvironmentError, "drift"):
                        probe.verify_probe(settings)
                    path.write_bytes(original)
            with patch.object(probe, "runtime_inventory", return_value=[]):
                with self.assertRaisesRegex(EnvironmentError, "runtime library drift"):
                    probe.verify_probe(settings)

    def test_probe_uses_exact_profile_and_strips_inherited_sdl_overrides(self):
        settings = {"paths": {"profile": "/private/synthetic-profile"},
                    "controller_probe": {"binary_sha256": "a" * 64, "receipt_sha256": "b" * 64}}
        completed = type("Response", (), {"stdout": json.dumps(self.payload)})()
        with patch.object(probe, "verify_probe", return_value=Path("/private/synthetic-probe")), \
             patch.object(probe.subprocess, "run", return_value=completed) as run, \
             patch.dict(probe.os.environ, {"SDL_GAMECONTROLLERCONFIG": "inherited override"}):
            result = probe.enumerate_controllers(settings, [self.hardware])
        self.assertIsNotNone(result[0]["hardware"])
        self.assertEqual(run.call_args.args[0], ["/private/synthetic-probe", "--config", "/private/synthetic-profile/Dolphin.ini"])
        self.assertNotIn("SDL_GAMECONTROLLERCONFIG", run.call_args.kwargs["env"])

    def test_probe_failure_cannot_become_an_empty_success(self):
        with patch.object(probe, "verify_probe", return_value=Path("/private/synthetic-probe")), \
             patch.object(probe.subprocess, "run", side_effect=OSError("unavailable")):
            with self.assertRaisesRegex(EnvironmentError, "discovery failed"):
                probe.enumerate_controllers({"paths": {"profile": "/private/profile"}}, [])

    def test_install_preserves_mapping_and_old_settings_and_refuses_existing_drift(self):
        import reference_capture_environment as env
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "source").write_bytes(b"synthetic executable")
            _, runtime, binary, receipt = self._installation(root)
            settings = {"schema": env.SCHEMA, "version": 1,
                        "paths": {key: str(root / key) for key in
                                  ("disc", "dol", "dolphin", "fixture_gc", "profile")},
                        "hashes": {}, "dolphin_revision": env.DOLPHIN_REVISION,
                        "observer_identity": "a" * 64,
                        "controller": {"backend": "SDL", "device": "Mapped Gamepad", "index": 2, "port": 1},
                        "timing_policy": {"cpu": "JITARM64", "dual_core": False,
                                          "speed": 1.0, "rtc": 1704067200}}
            settings["paths"]["profile"] = str(root / "Configuration")
            profile = root / "Configuration"
            profile.mkdir()
            mapping = profile / "GCPadNew.ini"
            mapping.write_text("[GCPad1]\nDevice = SDL/2/Mapped Gamepad\nButtons/A = `Button 4`\n")
            mapping_before = mapping.read_bytes()
            path = root / "environment.json"
            path.write_text(json.dumps(settings))
            with patch.object(probe, "runtime_inventory", return_value=runtime):
                probe.install_probe(receipt, root)
                updated = json.loads(path.read_text())
                self.assertEqual(updated["controller"], settings["controller"])
                self.assertEqual(mapping.read_bytes(), mapping_before)
                backups = list((root / "ControllerProbes").glob("settings-before-*.json"))
                self.assertEqual(len(backups), 1)
                self.assertEqual(json.loads(backups[0].read_text()), settings)
                binary.write_bytes(b"changed installed helper")
                with self.assertRaisesRegex(EnvironmentError, "binary drift"):
                    probe.install_probe(receipt, root)
                self.assertEqual(json.loads(path.read_text()), updated)


if __name__ == "__main__":
    unittest.main()
