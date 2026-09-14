import copy
import hashlib
import json
import plistlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import reference_capture_environment as env


class EnvironmentTests(unittest.TestCase):
    def test_no_usb_class_is_an_empty_device_list(self):
        with patch.object(env.subprocess, "check_output", side_effect=[plistlib.dumps([]), b""]):
            self.assertEqual(env.physical_devices(), [])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.profile = self.root / "Configuration"
        self.profile.mkdir()
        (self.profile / "Dolphin.ini").write_text("[Core]\nCPUThread = False\n")
        self.fixture = self.root / "fixture"
        self.fixture.mkdir()
        (self.fixture / "synthetic-card").write_bytes(b"synthetic test only")
        self.binary = self.root / "Dolphin.app/Contents/MacOS/Dolphin"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"synthetic executable")
        self.settings = {
            "schema": env.SCHEMA, "version": 1,
            "paths": {"disc": str(self.root / "disc"), "dol": str(self.root / "dol"),
                      "dolphin": str(self.binary), "fixture_gc": str(self.fixture),
                      "profile": str(self.profile)},
            "hashes": {"dolphin_binary_sha256": env.sha256(self.binary),
                       "dolphin_bundle": env.bundle_inventory(self.binary.parents[2]),
                       "dolphin_runtime_dependencies": [{"synthetic": True}],
                       "disc_image_sha256": "d" * 64,
                       "profile": env.file_inventory(self.profile),
                       "fixture_gc": env.file_inventory(self.fixture)},
            "dolphin_revision": env.DOLPHIN_REVISION, "observer_identity": "a" * 64,
            "controller": {"backend": "adapter", "device": "GameCube adapter", "port": 1},
            "timing_policy": {"cpu": "JITARM64", "dual_core": False,
                              "speed": 1.0, "rtc": 1704067200}}

    def verify(self, settings=None, devices=None):
        with patch.object(env, "verify_disc", return_value={"accepted": True}), \
             patch.object(env, "runtime_inventory", return_value=[{"synthetic": True}]):
            return env.verify_environment(settings or self.settings, self.root,
                                          devices=devices or [])

    def test_no_controller_is_not_physical_validation(self):
        result = self.verify()
        self.assertEqual(result["controller"]["state"], "unavailable")
        self.assertFalse(result["controller"]["physical_session_validated"])

    def test_connected_adapter_is_detection_only(self):
        device = {"name": "adapter", "adapter": True}
        result = self.verify(devices=[device])["controller"]
        self.assertEqual(result["state"], "connected")
        self.assertFalse(result["physical_session_validated"])

    def test_keyboard_is_never_a_physical_gamepad(self):
        self.settings["controller"]["backend"] = "keyboard"
        self.assertEqual(self.verify(devices=[{"name": "keyboard", "adapter": False}])[
            "controller"]["state"], "unavailable")

    def test_sdl_uses_pinned_backend_identity_instead_of_hid_product_name(self):
        self.settings["controller"] = {
            "backend": "SDL", "device": "Mapped Gamepad", "index": 1, "port": 1}
        hid = {"name": "USB Product", "vendor_id": 123, "product_id": 456,
               "transport": "USB", "adapter": False}
        sdl = {"source": "SDL", "name": "Mapped Gamepad", "index": 1,
               "vendor_id": 123, "product_id": 456, "hardware": hid}
        self.assertEqual(self.verify(devices=[sdl])["controller"]["state"], "connected")
        # A matching HID name alone does not identify Dolphin's selected SDL device.
        self.assertEqual(self.verify(devices=[dict(hid, name="Mapped Gamepad")])[
            "controller"]["state"], "unavailable")
        self.assertEqual(self.verify(devices=[dict(sdl, index=0)])[
            "controller"]["state"], "unavailable")
        self.assertEqual(self.verify(devices=[dict(sdl, hardware=None)])[
            "controller"]["state"], "unavailable")

    def test_legacy_sdl_selection_defaults_only_to_index_zero(self):
        self.settings["controller"] = {"backend": "SDL", "device": "Pad", "port": 1}
        device = {"source": "SDL", "name": "Pad", "index": 0,
                  "hardware": {"vendor_id": 123, "product_id": 456}}
        self.assertEqual(self.verify(devices=[device])["controller"]["state"], "connected")
        with self.assertRaisesRegex(env.EnvironmentError, "Ambiguous"):
            self.verify(devices=[device, device])

    def test_sdl_probe_is_used_only_when_sdl_is_configured(self):
        self.settings["controller"] = {"backend": "SDL", "device": "Pad", "port": 1}
        with patch.object(env.subprocess, "check_output", side_effect=[b"", b""]), \
             patch("reference_controller_probe.enumerate_controllers", return_value=[]) as probe:
            self.assertEqual(env.physical_devices(self.settings), [])
            probe.assert_called_once_with(self.settings, [])

    def test_global_profile_rejected_even_with_matching_hashes(self):
        other = self.root / "global-profile"
        other.mkdir()
        self.settings["paths"]["profile"] = str(other)
        self.settings["hashes"]["profile"] = {}
        with self.assertRaisesRegex(env.EnvironmentError, "isolated"):
            self.verify()

    def test_binary_and_config_and_fixture_drift_rejected(self):
        for target in (self.binary, self.profile / "Dolphin.ini", self.fixture / "synthetic-card"):
            with self.subTest(target=target.name):
                old = target.read_bytes()
                target.write_bytes(old + b"changed")
                with self.assertRaises(env.EnvironmentError):
                    self.verify()
                target.write_bytes(old)

    def test_profile_symlink_rejected(self):
        target = self.root / "outside"
        target.write_text("outside")
        (self.profile / "redirect.ini").symlink_to(target)
        with self.assertRaisesRegex(env.EnvironmentError, "symbolic link"):
            self.verify()

    def test_new_unpinned_configuration_file_rejected(self):
        (self.profile / "GameSettings.ini").write_text("unexpected")
        with self.assertRaisesRegex(env.EnvironmentError, "configuration changed"):
            self.verify()

    def test_dolphin_resource_drift_rejected(self):
        (self.binary.parent / "unreviewed-plugin").write_bytes(b"changed runtime")
        with self.assertRaisesRegex(env.EnvironmentError, "bundle drift"):
            self.verify()

    def test_runtime_library_drift_rejected(self):
        self.settings["hashes"]["dolphin_runtime_dependencies"] = [{"synthetic": "changed"}]
        with self.assertRaisesRegex(env.EnvironmentError, "runtime library drift"):
            self.verify()

    def test_local_settings_reject_unknown_fields_and_wrong_backend_policy(self):
        settings = self.root / "environment.json"
        settings.write_text(json.dumps(self.settings))
        self.assertEqual(env.read_settings(settings), self.settings)
        for key, value in (("surprise", 1), ("dolphin_revision", "0" * 40),
                           ("timing_policy", {"cpu": "Interpreter64"})):
            bad = copy.deepcopy(self.settings)
            bad[key] = value
            settings.write_text(json.dumps(bad))
            with self.assertRaises(env.EnvironmentError):
                env.read_settings(settings)

    def test_pinned_disc_dol_is_verified_inside_image(self):
        dol = self.root / "dol"
        dol.write_bytes(b"test" * 64)
        digest = hashlib.sha256(dol.read_bytes()).hexdigest()
        class DifferentImage:
            def __init__(self, _): pass
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def read(self, offset, count):
                return bytes.fromhex("00001000") if offset == 0x420 else b"x" * count
        with patch.object(env, "DOL_SHA256", digest), patch.object(env, "DiscImage", DifferentImage):
            with self.assertRaisesRegex(env.EnvironmentError, "different executable"):
                env.verify_disc(self.root / "disc", dol, "d" * 64)


if __name__ == "__main__":
    unittest.main()
