"""Synthetic provisioning and build-refresh checks for reference capture."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))

import configure_reference_capture as configure  # noqa: E402
import reference_capture_environment as environment  # noqa: E402


class ReferenceCaptureConfigureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.disc = self.root / "owned-disc"
        self.dol = self.root / "main.dol"
        self.disc.write_bytes(b"synthetic owned disc")
        self.dol.write_bytes(b"synthetic pinned DOL")
        self.fixture = self.root / "prepared-fixture"
        (self.fixture / "USA/Card A").mkdir(parents=True)
        self.gci = self.fixture / "USA/Card A/01-GALE-SuperSmashBros0110290334.gci"
        self.sram = self.fixture / "SRAM.raw"
        self.gci.write_bytes(b"synthetic prepared GCI")
        self.sram.write_bytes(b"synthetic prepared SRAM")
        self.app = self.root / "Dolphin.app"
        self.binary = self.app / "Contents/MacOS/Dolphin"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_bytes(b"synthetic reviewed Dolphin")
        (self.app / "Contents/Resources").mkdir(parents=True)
        (self.app / "Contents/Resources/runtime.dylib").write_bytes(b"synthetic runtime")
        self.build_manifest = self.root / "reference-dolphin-build.json"
        self.runtime_dependencies = [{"load_name": "@rpath/runtime.dylib",
                                      "sha256": "b" * 64, "size": 17}]
        self._write_build(self.binary, self.build_manifest, self.runtime_dependencies)
        self.fixture_patch = patch.object(
            configure, "PREPARED_GCI", hashlib.sha256(self.gci.read_bytes()).hexdigest()
        )
        self.sram_patch = patch.object(
            configure, "PREPARED_SRAM", hashlib.sha256(self.sram.read_bytes()).hexdigest()
        )
        self.disc_patch = patch.object(configure, "verify_disc", return_value={"accepted": True})
        self.runtime_patch = patch.object(
            configure, "runtime_inventory", return_value=self.runtime_dependencies
        )

    def _write_build(self, binary, path, runtime_dependencies):
        observer_root = configure.ROOT / "reference-capture/dolphin"
        source_root = observer_root / "source"
        overlay = {
            item.relative_to(source_root).as_posix(): configure.sha256(item)
            for item in sorted(source_root.rglob("*")) if item.is_file()
        }
        patches = [configure.sha256(item)
                   for item in sorted((observer_root / "patches").glob("*.patch"))]
        value = {
            "schema": "melee-web-reference-dolphin-build",
            "dolphin_commit": configure.DOLPHIN_REVISION,
            "cpu": "JITARM64",
            "binary": str(binary),
            "binary_sha256": configure.sha256(binary),
            "observer_source_overlay_sha256": overlay,
            "observer_patch_sha256": patches,
            "bundle_inventory": configure.bundle_inventory(binary.parents[2]),
            "runtime_dependencies": runtime_dependencies,
        }
        path.write_text(json.dumps(value), encoding="utf-8")
        archive = self.root / f"{path.stem}-provenance"
        archive.mkdir(parents=True, exist_ok=True)
        (archive / path.name).write_bytes(path.read_bytes())
        (archive / "source/observer/ReferenceObserver.cpp").parent.mkdir(parents=True)
        (archive / "source/observer/ReferenceObserver.cpp").write_text(
            "/* synthetic corresponding open-source observer */\n", encoding="utf-8")
        (archive / "patches/0001-observer.patch").parent.mkdir(parents=True)
        (archive / "patches/0001-observer.patch").write_text(
            "diff --git a/observer b/observer\n", encoding="utf-8")
        value["provenance_archive"] = str(archive)
        path.write_text(json.dumps(value), encoding="utf-8")
        (archive / path.name).write_bytes(path.read_bytes())
        return value

    def provision(self, *, root=None, manifest=None, refresh_build=False,
                  install_dolphin=False):
        with self.fixture_patch, self.sram_patch, self.disc_patch, self.runtime_patch:
            return configure.configure(
                disc=self.disc,
                dol=self.dol,
                build_manifest=manifest or self.build_manifest,
                fixture_gc=self.fixture,
                root=root or (self.root / "support"),
                refresh_build=refresh_build,
                install_dolphin=install_dolphin,
            )

    def test_fresh_provision_writes_private_hash_bound_settings(self):
        settings_path = self.provision()
        root = settings_path.parent
        value = json.loads(settings_path.read_text())
        self.assertEqual(value["paths"]["disc"], str(self.disc.resolve()))
        managed = environment.managed_fixture_path(root, environment.file_inventory(self.fixture))
        self.assertEqual(value["paths"]["fixture_gc"], str(managed))
        self.assertEqual(environment.file_inventory(managed), environment.file_inventory(self.fixture))
        for name in value["hashes"]["fixture_gc"]:
            self.assertEqual(stat.S_IMODE((managed / name).stat().st_mode), 0o400)
        self.assertEqual(value["hashes"]["profile"], environment.file_inventory(root / "Configuration"))
        self.assertEqual(value["hashes"]["fixture_gc"], environment.file_inventory(self.fixture))
        self.assertEqual(value["controller"], {"backend": "keyboard", "device": "Keyboard", "port": 1})
        self.assertEqual(stat.S_IMODE(settings_path.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE((root / "dolphin-build.json").stat().st_mode), 0o600)
        self.assertTrue((root / "Configuration/Dolphin.ini").is_file())
        self.assertTrue((root / "Configuration/GCPadNew.ini").is_file())

    def legacy_settings(self):
        path = self.provision()
        settings = json.loads(path.read_text())
        settings["paths"]["fixture_gc"] = str(self.fixture)
        settings["controller"] = {"backend": "adapter", "device": "Adapter", "port": 1}
        settings["controller_probe"] = {"preserved": True}
        path.write_text(json.dumps(settings))
        return path

    def migrate(self, root):
        with self.fixture_patch, self.sram_patch:
            return configure.migrate_fixture(root)

    def test_migration_preserves_source_hashes_profile_controller_and_prior_settings(self):
        path = self.legacy_settings()
        before = path.read_bytes()
        previous = json.loads(before)
        source_inventory = environment.file_inventory(self.fixture)
        self.migrate(path.parent)
        after = json.loads(path.read_text())
        expected = copy.deepcopy(previous)
        expected["paths"]["fixture_gc"] = str(environment.managed_fixture_path(path.parent, source_inventory))
        self.assertEqual(after, expected)
        self.assertEqual(environment.file_inventory(self.fixture), source_inventory)
        archive = path.parent / "ConfigurationHistory" / hashlib.sha256(before).hexdigest() / "environment.json"
        self.assertEqual(archive.read_bytes(), before)
        self.assertEqual(stat.S_IMODE(archive.stat().st_mode), 0o600)
        # Once migrated, even provisioning no longer needs the original location.
        self.fixture.rename(self.fixture.with_name("original-evidence-preserved"))
        migrated = path.read_bytes()
        self.migrate(path.parent)
        self.assertEqual(path.read_bytes(), migrated)

    def test_migration_rejects_fixture_drift_without_changing_settings(self):
        path = self.legacy_settings()
        before = path.read_bytes()
        self.sram.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "verified private prepared fixture"):
            self.migrate(path.parent)
        self.assertEqual(path.read_bytes(), before)

    def test_fixture_import_rejects_redirect_and_existing_drift(self):
        path = self.legacy_settings()
        before = path.read_bytes()
        inventory = json.loads(before)["hashes"]["fixture_gc"]
        managed = environment.managed_fixture_path(path.parent, inventory)
        gci = managed / self.gci.relative_to(self.fixture)
        gci.chmod(0o600)
        gci.write_bytes(b"tampered private import")
        with self.assertRaisesRegex(ValueError, "installed private prepared fixture changed"):
            self.migrate(path.parent)
        self.assertEqual(path.read_bytes(), before)
        moved = self.root / "preserved-import"
        managed.parent.rename(moved)
        managed.parent.symlink_to(moved, target_is_directory=True)
        with self.assertRaisesRegex(environment.EnvironmentError, "symbolic link"):
            self.migrate(path.parent)

    def test_migration_write_failure_preserves_previous_settings(self):
        path = self.legacy_settings()
        before = path.read_bytes()
        with patch.object(configure, "_write_private", side_effect=OSError("cannot publish settings")):
            with self.assertRaisesRegex(OSError, "cannot publish"):
                self.migrate(path.parent)
        self.assertEqual(path.read_bytes(), before)

    def test_existing_settings_require_explicit_refresh_without_mutation(self):
        settings_path = self.provision()
        before_settings = settings_path.read_bytes()
        before_profile = environment.file_inventory(settings_path.parent / "Configuration")
        with self.assertRaisesRegex(ValueError, "Existing private settings"):
            self.provision()
        self.assertEqual(settings_path.read_bytes(), before_settings)
        self.assertEqual(environment.file_inventory(settings_path.parent / "Configuration"), before_profile)

    def test_refresh_preserves_controller_profile_and_private_inputs_and_archives_receipt(self):
        settings_path = self.provision()
        root = settings_path.parent
        previous = json.loads(settings_path.read_text())
        previous["controller"] = {"backend": "adapter", "device": "GameCube adapter", "port": 1}
        previous["controller_probe"] = {"synthetic_preserved_descriptor": True}
        profile = root / "Configuration"
        (profile / "GCPadNew.ini").write_text("[GCPad1]\nDevice = SDL/0/Owned Pad\n")
        previous["hashes"]["profile"] = environment.file_inventory(profile)
        settings_path.write_text(json.dumps(previous))
        old_settings = settings_path.read_bytes()
        old_receipt = (root / "dolphin-build.json").read_bytes()
        old_hash = previous["hashes"]["dolphin_binary_sha256"]

        newer_app = self.root / "Dolphin-new.app"
        newer_binary = newer_app / "Contents/MacOS/Dolphin"
        newer_binary.parent.mkdir(parents=True)
        newer_binary.write_bytes(b"synthetic reviewed Dolphin v2")
        (newer_app / "Contents/Resources").mkdir(parents=True)
        (newer_app / "Contents/Resources/runtime.dylib").write_bytes(b"synthetic runtime v2")
        newer_manifest = self.root / "reference-dolphin-build-v2.json"
        self._write_build(newer_binary, newer_manifest, self.runtime_dependencies)
        new_settings_path = self.provision(root=root, manifest=newer_manifest, refresh_build=True)
        updated = json.loads(new_settings_path.read_text())

        self.assertEqual(updated["controller"], previous["controller"])
        self.assertEqual(updated["controller_probe"], previous["controller_probe"])
        self.assertEqual(updated["paths"]["disc"], previous["paths"]["disc"])
        self.assertEqual(updated["paths"]["dol"], previous["paths"]["dol"])
        self.assertEqual(updated["paths"]["fixture_gc"], previous["paths"]["fixture_gc"])
        self.assertEqual(updated["hashes"]["fixture_gc"], previous["hashes"]["fixture_gc"])
        self.assertEqual((profile / "GCPadNew.ini").read_text(), "[GCPad1]\nDevice = SDL/0/Owned Pad\n")
        archive = root / "BuildHistory" / old_hash
        self.assertEqual((archive / "environment.json").read_bytes(), old_settings)
        self.assertEqual((archive / "dolphin-build.json").read_bytes(), old_receipt)
        self.assertEqual(stat.S_IMODE((archive / "environment.json").stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE((archive / "dolphin-build.json").stat().st_mode), 0o600)

    def test_refresh_rejects_disc_fixture_or_profile_drift_before_writes(self):
        settings_path = self.provision()
        root = settings_path.parent
        original = settings_path.read_bytes()
        cases = ("disc", "fixture", "profile")
        for case in cases:
            with self.subTest(case=case):
                if case == "disc":
                    self.disc.write_bytes(b"changed disc")
                elif case == "fixture":
                    self.sram.write_bytes(b"changed SRAM")
                else:
                    (root / "Configuration/Dolphin.ini").write_text("drift")
                with self.assertRaises(ValueError):
                    self.provision(root=root, refresh_build=True)
                self.assertEqual(settings_path.read_bytes(), original)
                if case == "disc":
                    self.disc.write_bytes(b"synthetic owned disc")
                elif case == "fixture":
                    self.sram.write_bytes(b"synthetic prepared SRAM")
                else:
                    value = json.loads(original)
                    (root / "Configuration/Dolphin.ini").write_text("[Core]\nCPUCore = 4\n")
                    value["hashes"]["profile"] = environment.file_inventory(root / "Configuration")
                    settings_path.write_text(json.dumps(value))
                    original = settings_path.read_bytes()

    def test_source_patch_bundle_and_runtime_receipt_mismatch_reject_before_profile_writes(self):
        root = self.root / "fresh-support"
        cases = ("observer_source_overlay_sha256", "observer_patch_sha256",
                 "bundle_inventory", "runtime_dependencies")
        for field in cases:
            with self.subTest(field=field):
                value = json.loads(self.build_manifest.read_text())
                if field in ("observer_source_overlay_sha256", "observer_patch_sha256"):
                    value[field] = {"changed": "0" * 64} if field.endswith("overlay_sha256") else ["0" * 64]
                elif field == "bundle_inventory":
                    value[field] = []
                else:
                    value[field] = [{"load_name": "@rpath/wrong.dylib", "sha256": "0" * 64, "size": 1}]
                bad_manifest = self.root / f"bad-{field}.json"
                bad_manifest.write_text(json.dumps(value))
                with self.assertRaises(ValueError):
                    self.provision(root=root, manifest=bad_manifest)
                self.assertFalse(root.exists())

    def test_optional_install_relocates_verified_bundle_and_exact_provenance(self):
        settings_path = self.provision(install_dolphin=True)
        root = settings_path.parent
        settings = json.loads(settings_path.read_text())
        build = json.loads(self.build_manifest.read_text())
        version = root / "Dolphin" / build["binary_sha256"]
        self.assertEqual(Path(settings["paths"]["dolphin"]),
                         version / "Dolphin.app/Contents/MacOS/Dolphin")
        self.assertTrue((version / "installed.json").is_file())
        self.assertEqual((version / "dolphin-build.json").read_bytes(),
                         self.build_manifest.read_bytes())
        source_archive = Path(build["provenance_archive"])
        copied_archive = version / "provenance"
        source_files = sorted(path.relative_to(source_archive)
                              for path in source_archive.rglob("*") if path.is_file())
        copied_files = sorted(path.relative_to(copied_archive)
                              for path in copied_archive.rglob("*") if path.is_file())
        self.assertEqual(copied_files, source_files)
        for relative in source_files:
            self.assertEqual((copied_archive / relative).read_bytes(),
                             (source_archive / relative).read_bytes())
        descriptor = json.loads((version / "installed.json").read_text())
        self.assertEqual(descriptor["binary_sha256"], build["binary_sha256"])
        self.assertEqual(descriptor["bundle_inventory"], settings["hashes"]["dolphin_bundle"])
        self.assertEqual(descriptor["runtime_dependencies"], configure._runtime_identity(
            settings["hashes"]["dolphin_runtime_dependencies"]))
        self.assertTrue(all("resolved_path" not in entry
                            for entry in descriptor["runtime_dependencies"]))
        self.assertTrue((self.app / "Contents/MacOS/Dolphin").is_file(),
                        "relocation must not consume the build output")

    def test_refresh_installs_new_version_and_preserves_prior_version(self):
        settings_path = self.provision(install_dolphin=True)
        root = settings_path.parent
        first = json.loads(settings_path.read_text())
        first_version = Path(first["paths"]["dolphin"]).parents[3]

        newer_app = self.root / "Dolphin-new.app"
        newer_binary = newer_app / "Contents/MacOS/Dolphin"
        newer_binary.parent.mkdir(parents=True)
        newer_binary.write_bytes(b"synthetic reviewed Dolphin v2")
        (newer_app / "Contents/Resources").mkdir(parents=True)
        (newer_app / "Contents/Resources/runtime.dylib").write_bytes(b"synthetic runtime v2")
        newer_manifest = self.root / "reference-dolphin-build-v2.json"
        self._write_build(newer_binary, newer_manifest, self.runtime_dependencies)
        self.provision(root=root, manifest=newer_manifest, refresh_build=True,
                       install_dolphin=True)
        second = json.loads(settings_path.read_text())
        second_version = Path(second["paths"]["dolphin"]).parents[3]
        self.assertNotEqual(first_version, second_version)
        self.assertTrue(first_version.is_dir())
        self.assertTrue(second_version.is_dir())
        self.assertEqual(first_version.parent, root / "Dolphin")
        self.assertTrue((root / "BuildHistory" / first["hashes"]["dolphin_binary_sha256"]).is_dir())

    def test_identical_installed_version_is_idempotent(self):
        settings_path = self.provision(install_dolphin=True)
        root = settings_path.parent
        before = settings_path.read_bytes()
        self.provision(root=root, refresh_build=True, install_dolphin=True)
        self.assertEqual(settings_path.read_bytes(), before)

    def test_same_executable_with_changed_bundle_or_provenance_rejects_without_writes(self):
        settings_path = self.provision(install_dolphin=True)
        root = settings_path.parent
        settings_before = settings_path.read_bytes()
        receipt_before = (root / "dolphin-build.json").read_bytes()
        version = Path(json.loads(settings_before)["paths"]["dolphin"]).parents[3]
        installed_before = configure._tree_inventory(version)
        # The executable is identical; only a bundled resource and the new
        # corresponding-source receipt differ.
        (self.app / "Contents/Resources/runtime.dylib").write_bytes(b"different runtime")
        newer_manifest = self.root / "same-executable-build.json"
        self._write_build(self.binary, newer_manifest, self.runtime_dependencies)
        with self.assertRaisesRegex(ValueError, "version collision"):
            self.provision(root=root, manifest=newer_manifest, refresh_build=True, install_dolphin=True)
        self.assertEqual(settings_path.read_bytes(), settings_before)
        self.assertEqual((root / "dolphin-build.json").read_bytes(), receipt_before)
        self.assertEqual(configure._tree_inventory(version), installed_before)

    def test_existing_drifted_version_is_never_overwritten(self):
        settings_path = self.provision(install_dolphin=True)
        root = settings_path.parent
        settings_before = settings_path.read_bytes()
        version = Path(json.loads(settings_before)["paths"]["dolphin"]).parents[3]
        installed_binary = version / "Dolphin.app/Contents/MacOS/Dolphin"
        installed_binary.write_bytes(b"tampered installed binary")
        with self.assertRaisesRegex(ValueError, "installed Dolphin binary has drifted"):
            self.provision(root=root, refresh_build=True, install_dolphin=True)
        self.assertEqual(settings_path.read_bytes(), settings_before)
        self.assertEqual(installed_binary.read_bytes(), b"tampered installed binary")

    def test_install_failure_rolls_back_new_version(self):
        root = self.root / "support"
        with patch.object(configure, "_write_private",
                          side_effect=[None, OSError("synthetic receipt write failure")]):
            with self.assertRaisesRegex(OSError, "synthetic receipt write failure"):
                self.provision(root=root, install_dolphin=True)
        build = json.loads(self.build_manifest.read_text())
        self.assertFalse((root / "Dolphin" / build["binary_sha256"]).exists())
        self.assertFalse((root / "environment.json").exists())


if __name__ == "__main__":
    unittest.main()
