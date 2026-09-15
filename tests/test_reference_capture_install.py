import json
import fcntl
import os
import plistlib
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import install_reference_capture as installer


class ReferenceCaptureInstallerTests(unittest.TestCase):
    def test_upgrade_requires_closed_application_and_releases_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with installer.installation_guard(root):
                with self.assertRaisesRegex(installer.InstallError, "Quit WebMelee"):
                    with installer.installation_guard(root):
                        self.fail("Open application was replaced")
            with installer.installation_guard(root):
                pass

    def test_runtime_copy_is_source_only_and_records_hashes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "runtime"
            (root / "scripts").mkdir(parents=True)
            (root / "tools").mkdir()
            (root / "scripts/reference_capture_app.py").write_text("print('supervisor')\n")
            (root / "tools/helper.py").write_text("VALUE = 1\n")
            (root / "README.md").write_text("kept as a runtime notice\n")
            output = Path(temporary) / "copied"
            records = installer.copy_runtime(root, output)

            self.assertEqual(
                {record["path"] for record in records},
                {"README.md", "scripts/reference_capture_app.py", "tools/helper.py"},
            )
            self.assertEqual(
                installer.sha256_file(output / "tools/helper.py"),
                next(record["sha256"] for record in records if record["path"] == "tools/helper.py"),
            )

    def test_runtime_rejects_disc_and_private_payloads(self):
        for relative in ("assets-local/disc.iso", "captures/capture.jsonl", "private/config.json", "dolphin.ini"):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary) / "runtime"
                (root / "scripts").mkdir(parents=True)
                (root / "scripts/reference_capture_app.py").write_text("# supervisor\n")
                forbidden = root / relative
                forbidden.parent.mkdir(parents=True, exist_ok=True)
                forbidden.write_bytes(b"private")
                with self.assertRaises(installer.InstallError):
                    installer.runtime_files(root)

    def test_source_runtime_materialization_does_not_execute_or_copy_non_python(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source"
            (source / "scripts").mkdir(parents=True)
            (source / "tools").mkdir()
            (source / "reference-capture/dolphin").mkdir(parents=True)
            (source / "scripts/reference_capture_app.py").write_text("raise SystemExit\n")
            (source / "tools/helper.py").write_text("VALUE = 1\n")
            (source / "tools/fixture.iso").write_bytes(b"disc")
            (source / "reference-capture/dolphin/observer.py").write_text("VALUE = 2\n")
            destination = Path(temporary) / "runtime"
            installer.materialize_source_runtime(source, destination)
            self.assertTrue((destination / "scripts/reference_capture_app.py").is_file())
            self.assertTrue((destination / "tools/helper.py").is_file())
            self.assertTrue((destination / "reference-capture/dolphin/observer.py").is_file())
            self.assertFalse((destination / "tools/fixture.iso").exists())

    def test_atomic_install_replaces_app_and_keeps_previous_on_failed_bundle(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination = root / "WebMelee Reference Capture.app"
            (destination / "Contents").mkdir(parents=True)
            (destination / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": "org.webmelee.reference-capture", "TestVersion": "old"}))
            staging = root / "staged.app"
            (staging / "Contents").mkdir(parents=True)
            (staging / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": "org.webmelee.reference-capture", "TestVersion": "new"}))
            installed = installer.atomic_install(staging, destination)
            self.assertEqual(installed, destination.resolve())
            self.assertEqual(plistlib.loads((destination / "Contents/Info.plist").read_bytes())["TestVersion"], "new")

            failed = root / "failed.app"
            failed.mkdir()
            with self.assertRaises(installer.InstallError):
                installer.atomic_install(failed, destination)
            self.assertEqual(plistlib.loads((destination / "Contents/Info.plist").read_bytes())["TestVersion"], "new")

    def test_installer_rejects_populated_directory_foreign_app_and_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            staging = root / "staged.app"
            (staging / "Contents").mkdir(parents=True)
            (staging / "Contents/Info.plist").write_bytes(installer._plist())
            applications = root / "Applications"
            applications.mkdir()
            (applications / "important.txt").write_bytes(b"preserve")
            foreign = root / "Foreign.app"
            (foreign / "Contents").mkdir(parents=True)
            (foreign / "Contents/Info.plist").write_bytes(plistlib.dumps({"CFBundleIdentifier": "org.someone.else"}))
            alias = root / "WebMelee Reference Capture.app"
            alias.symlink_to(applications, target_is_directory=True)
            for destination in (applications, foreign, alias):
                with self.subTest(destination=destination), self.assertRaises(installer.InstallError):
                    installer.atomic_install(staging, destination)
                self.assertTrue(staging.exists())
                self.assertEqual((applications / "important.txt").read_bytes(), b"preserve")
                self.assertEqual(plistlib.loads((foreign / "Contents/Info.plist").read_bytes())["CFBundleIdentifier"], "org.someone.else")
                self.assertTrue(alias.is_symlink())

    def test_failed_publish_restores_the_verified_previous_app(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            destination, staging = root / installer.APP_NAME, root / "staged.app"
            for app in (destination, staging):
                (app / "Contents").mkdir(parents=True)
                (app / "Contents/Info.plist").write_bytes(installer._plist())
            (destination / "preserved.txt").write_bytes(b"old application")
            replace = os.replace
            def fail_publish(source, target):
                if source == staging:
                    raise OSError("synthetic publish failure")
                return replace(source, target)
            with mock.patch.object(installer.os, "replace", side_effect=fail_publish):
                with self.assertRaisesRegex(OSError, "publish failure"):
                    installer.atomic_install(staging, destination)
            self.assertEqual((destination / "preserved.txt").read_bytes(), b"old application")
            self.assertTrue(staging.exists())

    def test_python_metadata_pins_path_version_and_binary_hash(self):
        python = installer.resolve_python(sys.executable)
        metadata = installer.python_metadata(python)
        self.assertEqual(metadata["schema"], installer.PYTHON_SCHEMA)
        self.assertEqual(metadata["path"], str(python))
        self.assertRegex(metadata["version"], r"^\d+\.\d+")
        self.assertEqual(metadata["sha256"], installer.sha256_file(python))

    def test_finder_alias_uses_bundled_native_helper(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / installer.APP_NAME
            executable = app / "Contents/MacOS" / installer.EXECUTABLE_NAME
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"native helper")
            desktop = root / "Desktop"
            with mock.patch.object(installer.sys, "platform", "darwin"), \
                 mock.patch.object(installer.subprocess, "run") as run:
                self.assertTrue(installer.create_finder_alias(app, desktop))
            run.assert_called_once_with(
                [str(executable), "--create-desktop-alias", str(desktop / installer.APP_NAME)],
                check=True, capture_output=True, text=True,
            )


if __name__ == "__main__":
    unittest.main()
