import importlib.util
import json
from pathlib import Path
import stat
import sys
import tempfile
import threading
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("reference_capture_app_under_test", ROOT / "scripts/reference_capture_app.py")
APP = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "scripts")]
SPEC.loader.exec_module(APP)


class ReferenceCaptureAppTests(unittest.TestCase):
    def test_failed_derived_report_preserves_finalized_replay(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            final = root / "finalized"
            final.mkdir()
            marker = final / "manifest.json"
            marker.write_bytes(b"immutable raw receipt")
            app.replay_source = {"path": root / "original"}
            try:
                with mock.patch("reference_session_comparison.compare_bundles", return_value={"status": "matched"}), \
                     mock.patch.object(APP.ReferenceCaptureInbox, "store_derived", side_effect=OSError("writer unavailable")):
                    app._finish_replay_comparison("replay", final)
                self.assertEqual(app.status["state"], "replay_comparison_failed")
                self.assertIn("writer unavailable", app.status["message"])
                self.assertEqual(marker.read_bytes(), b"immutable raw receipt")
                self.assertEqual(list(final.iterdir()), [marker])
            finally:
                app.close()

    def test_replay_does_not_probe_a_live_controller_and_clears_mode(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            source = {"path": root / "original", "header": {}}
            try:
                def replay_capture():
                    self.assertIs(app.replay_source, source)
                    with mock.patch.object(APP, "physical_devices") as devices:
                        app._check_controller_connection()
                        devices.assert_not_called()
                    raise ValueError("preserved replay failure")
                with mock.patch.object(APP, "load_source", return_value=source), \
                     mock.patch.object(app, "capture", side_effect=replay_capture):
                    with self.assertRaisesRegex(ValueError, "preserved replay failure"):
                        app.replay(root / "original")
                self.assertIsNone(app.replay_source)
            finally:
                app.close()

    def test_verify_replay_can_be_ready_without_physical_controller(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            app.replay_source = {"path": root / "original"}
            settings = {"input_recording_version": 1}
            identity = {"controller": {"state": "unavailable"}}
            try:
                with mock.patch.object(APP, "read_settings", return_value=settings), \
                     mock.patch.object(APP, "verify_environment", return_value=identity) as verify, \
                     mock.patch.object(APP, "verify_replay_environment") as replay_verify:
                    app.verify()
                    self.assertEqual(verify.call_args.kwargs["devices"], [])
                    replay_verify.assert_called_once_with(app.replay_source, identity)
                self.assertEqual(app.status["state"], "ready")
                self.assertEqual(app.status["physical_controller"], "replay")
            finally:
                app.close()

    def test_verify_rejects_dolphin_without_input_recording_capability(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            try:
                with mock.patch.object(APP, "read_settings", return_value={}), \
                     mock.patch.object(APP, "verify_environment", return_value={"controller": {"state": "connected"}}):
                    with self.assertRaisesRegex(APP.EnvironmentError, "lacks replay recording"):
                        app.verify()
                self.assertNotEqual(app.status["state"], "ready")
            finally:
                app.close()

    def test_dolphin_and_probe_use_profile_owned_sdl_configuration(self):
        with mock.patch.dict(APP.os.environ, {"SDL_GAMECONTROLLERCONFIG": "override",
                                             "SDL_JOYSTICK_HIDAPI": "0",
                                             "MWRC_INPUT_REPLAY": "/private/wrong-input",
                                             "DOLPHIN_EMU_USERPATH": "/private/global"}):
            environment = APP.isolated_dolphin_environment()
        self.assertFalse(any(key.startswith("SDL_") for key in environment))
        self.assertNotIn("DOLPHIN_EMU_USERPATH", environment)
        self.assertFalse(any(key.startswith("MWRC_") for key in environment))

    def test_live_sdl_connection_check_never_opens_another_sdl_client(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            hardware = {"name": "USB Product", "vendor_id": 123, "product_id": 456,
                        "transport": "USB", "adapter": False}
            app.identity = {"controller": {"selected": {"source": "SDL", "hardware": hardware}}}
            app.settings = {"controller": {"backend": "SDL", "device": "Mapped Pad", "port": 1}}
            try:
                with mock.patch.object(APP, "physical_devices", return_value=[hardware]) as devices:
                    app._check_controller_connection()
                    devices.assert_called_once_with()
                self.assertEqual(app.status["physical_controller"], "connected")
                with mock.patch.object(APP, "physical_devices", return_value=[dict(hardware, product_id=789)]):
                    with self.assertRaisesRegex(APP.EnvironmentError, "disconnected"):
                        app._check_controller_connection()
            finally:
                app.close()

    def test_controller_setup_rejects_environment_drift_before_launch(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            try:
                with mock.patch.object(APP, "read_settings", return_value={}), \
                     mock.patch.object(APP, "verify_environment", side_effect=APP.EnvironmentError("binary drift")), \
                     mock.patch.object(APP.subprocess, "Popen") as launch:
                    with self.assertRaisesRegex(APP.EnvironmentError, "binary drift"):
                        app.configure_controller()
                    launch.assert_not_called()
                    self.assertFalse((root / "ControllerSetup").exists())
            finally:
                app.close()

    def test_write_json_creates_private_atomic_files_before_replace(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "environment.json"
            APP.write_json(path, {"private": True})
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            path.chmod(0o644)
            APP.write_json(path, {"private": "rewritten"})
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertEqual(json.loads(path.read_text()), {"private": "rewritten"})

    def _capture_app(self, root, bundle, identity, settings):
        app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
        app.identity = identity
        app.settings = settings
        app.verify = lambda: app.status.update(
            state="ready", accepted_disc=True, physical_controller="connected",
            dolphin="stopped", capture="idle")
        return app

    @staticmethod
    def _capture_identity():
        return {
            "controller": {"state": "connected", "devices": []},
            "disc": {"dol_sha256": "a" * 64},
        }

    @staticmethod
    def _capture_settings():
        return {
            "paths": {
                "dolphin": "/private/Dolphin", "disc": "/private/game.iso",
                "fixture_gc": "/private/fixture", "profile": "/private/profile",
            },
            "observer_identity": "observer-test",
        }

    def _run_capture_failure(self, process, *, file_inventory_values=("stable", "stable"),
                              observer_status=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Sessions").mkdir()
            bundle_path = root / "Captures" / "capture.partial"
            bundle_path.mkdir(parents=True)
            bundle = mock.Mock(path=bundle_path)
            identity = self._capture_identity()
            settings = self._capture_settings()
            (root / "environment.json").write_text(json.dumps(settings), encoding="utf-8")
            app = self._capture_app(root, bundle, identity, settings)
            user = root / "session-user"
            (user / "Config").mkdir(parents=True)
            inventory = iter(file_inventory_values)
            status_value = {
                "state": "recording", "event_count": 0, "last_seq": -1,
                "source_tick": 0, "draw_ordinal": 0, "completed": False,
                "invalid": False, "error": None,
            }
            if observer_status is not None:
                status_value.update(observer_status)
            fake_stream = types.SimpleNamespace(
                iter_records=lambda _path: [],
                read_status=lambda _path: dict(status_value),
            )

            class FakePopen:
                def __init__(self, *_args, **kwargs):
                    self.pid = 321
                    self.returncode = None
                    self._process = process
                    process.owner = app
                    if observer_status is not None:
                        Path(kwargs["env"]["MWRC_STATUS"]).write_text("{}\n", encoding="utf-8")

                def poll(self):
                    return self._process.poll()

                def wait(self, timeout=None):
                    return self._process.wait(timeout)

            patches = [
                mock.patch.object(APP, "ReferenceSessionBundle") ,
                mock.patch.object(APP, "prepare_user", return_value=user),
                mock.patch.object(APP, "dolphin_command", return_value=["Dolphin"]),
                mock.patch.object(APP, "verify_environment", return_value=identity),
                mock.patch.object(APP, "file_inventory", side_effect=lambda _path: next(inventory)),
                mock.patch.object(APP.subprocess, "Popen", FakePopen),
                mock.patch.object(APP.os, "killpg"),
                mock.patch.dict(sys.modules, {"reference_observer_stream": fake_stream}),
            ]
            patches[0].start().begin.return_value = bundle
            for patcher in patches[1:]:
                patcher.start()
            try:
                app.capture()
            finally:
                for patcher in reversed(patches):
                    patcher.stop()
                app.close()
            return app, bundle, process

    def test_verify_reports_controller_required_and_never_claims_keyboard_ready(self):
        events = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            settings = {"controller": {"backend": "keyboard"}, "input_recording_version": 1}
            identity = {"controller": {"state": "unavailable", "devices": []}}
            with mock.patch.object(APP, "read_settings", return_value=settings), \
                 mock.patch.object(APP, "verify_environment", return_value=identity):
                app = APP.Supervisor(root / "environment.json", root=root, emit=events.append)
                try:
                    app.verify()
                finally:
                    app.close()
        self.assertEqual(app.status["state"], "controller_required")
        self.assertFalse(any(event.get("state") == "ready" for event in events))
        self.assertEqual(app.status["physical_controller"], "unavailable")
        self.assertTrue(app.status["accepted_disc"])

    def test_automated_mode_bypasses_only_the_physical_controller_gate(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = APP.Supervisor(Path(temporary) / "environment.json",
                                 root=Path(temporary), emit=lambda row: None,
                                 automated=True)
            try:
                app.identity = {"controller": {"state": "unavailable"}}
                app.status["state"] = "controller_required"
                self.assertTrue(app._capture_prerequisites_verified())
                app.status["state"] = "failed"
                self.assertFalse(app._capture_prerequisites_verified())
                app.status["state"] = "controller_required"
                app.identity["controller"]["state"] = "connected"
                self.assertFalse(app._capture_prerequisites_verified())
            finally:
                app.close()

    def test_observer_status_counters_are_consistent_and_monotonic(self):
        first = {"event_count": 2, "last_seq": 1}
        self.assertIs(APP.validate_observer_status(first), first)
        with self.assertRaisesRegex(APP.EnvironmentError, "does not match"):
            APP.validate_observer_status({"event_count": 2, "last_seq": 0})
        with self.assertRaisesRegex(APP.EnvironmentError, "backwards"):
            APP.validate_observer_status({"event_count": 1, "last_seq": 0}, first)

    def test_observer_handshake_binds_dolphin_and_disc_identity(self):
        identity = {
            "disc": {"dol_sha1": "11" * 20, "dol_sha256": "22" * 32},
            "dolphin": {"source_revision": "reviewed-commit"},
        }
        payload = {
            "schema": "melee-web-passive-dolphin-observer", "version": 1,
            "dolphin_commit": "reviewed-commit", "dol_sha1": "11" * 20,
            "dol_sha256": "22" * 32, "cpu": "JITARM64",
            "writes_guest_memory": False, "ring_capacity": 1024,
        }
        self.assertEqual(APP.validate_observer_handshake(
            [{"seq": 0, "event": "handshake", "payload": payload}], identity), payload)
        with self.assertRaisesRegex(APP.EnvironmentError, "sequence 0"):
            APP.validate_observer_handshake(
                [{"seq": 4, "event": "handshake", "payload": payload}], identity)
        payload["dol_sha256"] = "33" * 32
        with self.assertRaisesRegex(APP.EnvironmentError, "identity mismatch"):
            APP.validate_observer_handshake(
                [{"seq": 0, "event": "handshake", "payload": payload}], identity)
        with self.assertRaisesRegex(APP.EnvironmentError, "missing its handshake"):
            APP.validate_observer_handshake(iter(()), identity)

    def test_controller_disconnect_is_failed_closed_for_physical_capture(self):
        events = []
        with tempfile.TemporaryDirectory() as temporary:
            app = APP.Supervisor(Path(temporary) / "environment.json",
                                 root=Path(temporary), emit=events.append)
            app.settings = {"controller": {"backend": "SDL", "device": "Pad", "port": 1}}
            with mock.patch.object(APP, "physical_devices", return_value=[]), \
                 mock.patch.object(APP, "configured_controller",
                                   return_value={"state": "unavailable"}):
                with self.assertRaisesRegex(APP.EnvironmentError, "disconnected"):
                    app._check_controller_connection()
            app.close()
        self.assertEqual(app.status["physical_controller"], "unavailable")
        self.assertEqual(events[-1]["physical_controller"], "unavailable")

    def test_startup_discovers_orphaned_active_partial(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bundle = APP.ReferenceSessionBundle.begin(
                root / "Captures", "orphaned", "GALE01r2", "run-orphan")
            bundle.fail("test process crash", error_type="crash")
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            try:
                discovered = app.discover_partial_captures()
                self.assertEqual([row["session_id"] for row in discovered], ["orphaned.partial"])
                self.assertEqual(app.status["partial_captures"], discovered)
            finally:
                app.close()

    def test_command_dispatches_verify_start_stop_and_rejects_unknown(self):
        events = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=events.append)
            calls = []
            app.verify = lambda: calls.append("verify")
            app.capture = lambda: calls.append("start")
            app.configure_controller = lambda: calls.append("configure_controller")
            try:
                for command in ("verify", "start", "configure_controller"):
                    app.command(command)
                    self.assertIsNotNone(app.worker)
                    app.worker.join(timeout=2)
                app.command("stop")
                app.command("unknown")
            finally:
                app.close()
        self.assertEqual(calls, ["verify", "start", "configure_controller"])
        self.assertTrue(app.stop_requested.is_set())
        self.assertEqual(app.status["state"], "failed")
        self.assertIn("Unknown capture action", app.status["message"])

    def test_malformed_command_envelope_is_visible_as_failed_status(self):
        events = []
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=events.append)
            try:
                # This is the same envelope rule used by the JSONL main loop;
                # an operator error is surfaced rather than ignored.
                with self.assertRaises(ValueError):
                    value = {"command": "verify", "extra": True}
                    if set(value) != {"command"}:
                        raise ValueError("Invalid command envelope")
            finally:
                app.close()

    def test_dolphin_command_forces_isolated_user_and_read_only_session(self):
        settings = {"paths": {"dolphin": "/private/Dolphin", "disc": "/private/game.iso",
                                "fixture_gc": "/private/fixture"}}
        command = APP.dolphin_command(settings, Path("/private/session"))
        self.assertEqual(command[:6], ["/private/Dolphin", "-u", "/private/session", "-b", "-e", "/private/game.iso"])
        self.assertIn("Dolphin.Core.CPUCore=4", command)
        self.assertIn("Session.Core.SaveDataWritable=False", command)

    def test_dolphin_process_exit_preserves_incomplete_capture(self):
        class Process:
            pid = 321

            def poll(self):
                return 17

            def wait(self, timeout=None):
                return 17

        app, bundle, _ = self._run_capture_failure(Process())
        self.assertEqual(app.status["state"], "failed")
        self.assertIn("final status", app.status["message"])
        self.assertFalse(any(call.kwargs.get("error_type") == "accepted" for call in bundle.fail.call_args_list))

    def test_operator_cancel_terminates_owned_process_and_preserves_partial(self):
        class Process:
            pid = 321
            calls = 0

            def poll(self):
                self.calls += 1
                if self.calls == 1:
                    self.owner.stop_requested.set()
                return None

            def wait(self, timeout=None):
                return 0

        app, bundle, process = self._run_capture_failure(Process())
        self.assertEqual(app.status["state"], "failed")
        self.assertIn("final status", app.status["message"])
        self.assertTrue(app.stop_requested.is_set())
        bundle.fail.assert_called_once()
        self.assertGreaterEqual(process.calls, 2)

    def test_observer_failure_is_failed_and_dolphin_is_terminated(self):
        class Process:
            pid = 321

            def poll(self):
                return None

            def wait(self, timeout=None):
                return 0

        app, bundle, _ = self._run_capture_failure(
            Process(), observer_status={"error": "observer CRC mismatch"})
        self.assertEqual(app.status["state"], "failed")
        self.assertIn("Observer failure", app.status["message"])
        bundle.fail.assert_called_once()
        self.assertEqual(bundle.fail.call_args.kwargs["error_type"], "capture")

    def test_quit_terminates_owned_dolphin_process(self):
        class Process:
            pid = 654

            def __init__(self):
                self.waited = False

            def poll(self):
                return None

            def wait(self, timeout=None):
                self.waited = True
                return 0

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = APP.Supervisor(root / "environment.json", root=root, emit=lambda row: None)
            process = Process()
            app.process = process
            with mock.patch.object(APP.os, "killpg") as killpg:
                app.close()
        killpg.assert_called_once_with(process.pid, APP.signal.SIGTERM)
        self.assertTrue(process.waited)

    def test_configuration_drift_fails_before_semantic_acceptance(self):
        class Process:
            pid = 321

            def poll(self):
                return None

            def wait(self, timeout=None):
                return 0

        app, bundle, _ = self._run_capture_failure(
            Process(), file_inventory_values=("before", "after"))
        self.assertEqual(app.status["state"], "failed")
        self.assertIn("configuration changed during recording", app.status["message"])
        bundle.fail.assert_called_once()


if __name__ == "__main__":
    unittest.main()
