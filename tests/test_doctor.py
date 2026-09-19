"""Focused, read-only checks for the developer environment doctor."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import doctor


def git(path: Path, *args: str) -> str:
    return subprocess.check_output(["git", *args], cwd=path, text=True).strip()


class DoctorTests(unittest.TestCase):
    def test_mismatched_dependency_is_reported_without_repair(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-deps-") as temporary:
            root = Path(temporary)
            dependency = root / ".deps/aurora"
            dependency.mkdir(parents=True)
            (dependency / "file.txt").write_text("fixture\n", encoding="utf-8")
            git(dependency, "init", "--quiet")
            git(dependency, "add", ".")
            git(dependency, "-c", "user.name=Doctor", "-c", "user.email=doctor@example.invalid",
                "commit", "--quiet", "-m", "fixture")
            actual = git(dependency, "rev-parse", "HEAD")
            lock = {
                "schema": 1,
                "repositories": {
                    "aurora": {"url": "fixture", "commit": "0" * 40},
                },
                "emscripten": "6.0.9",
                "python_build_packages": [],
            }
            report = doctor.inspect_dependencies(root, lock)
            self.assertEqual(report["repositories"]["aurora"]["status"], "mismatch")
            self.assertEqual(git(dependency, "rev-parse", "HEAD"), actual)
            self.assertEqual(git(dependency, "status", "--porcelain"), "")

    def test_reviewed_patch_and_bootstrap_verifier_are_reused(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-patch-") as temporary:
            root = Path(temporary)
            aurora = root / ".deps/aurora"
            aurora.mkdir(parents=True)
            patch = root / "patches/aurora-browser.patch"
            patch.parent.mkdir()
            patch.write_text("reviewed\n", encoding="utf-8")
            gameplay_patch = root / "patches/melee-gameplay.patch"
            gameplay_patch.write_text("reviewed gameplay\n", encoding="utf-8")
            with mock.patch.object(doctor, "_reviewed_patch_state", return_value=("applied", "ok")) as patch_check:
                lock = {
                    "schema": 1,
                    "repositories": {},
                    "emscripten": "6.0.9",
                    "python_build_packages": [],
                }
                report = doctor.inspect_dependencies(root, lock)
            self.assertEqual(report["status"], "ok")
            self.assertEqual(report["reviewed_patches"]["aurora-browser"]["state"], "applied")
            patch_check.assert_called_once_with(aurora, patch)

    def test_reviewed_patch_check_does_not_change_dependency_index_or_objects(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-readonly-") as temporary:
            repository = Path(temporary) / "repo"
            repository.mkdir()
            (repository / "tracked.txt").write_text("before\n", encoding="utf-8")
            git(repository, "init", "--quiet")
            git(repository, "add", ".")
            git(repository, "-c", "user.name=Doctor", "-c", "user.email=doctor@example.invalid",
                "commit", "--quiet", "-m", "base")
            objects = repository / ".git/objects"
            original_objects = {path for path in objects.rglob("*") if path.is_file()}
            (repository / "tracked.txt").write_text("after\n", encoding="utf-8")
            (repository / "added.txt").write_text("new\n", encoding="utf-8")
            git(repository, "add", ".")
            expected = subprocess.check_output(
                ["git", "diff", "--no-ext-diff", "--no-color", "--cached", "--binary", "HEAD"],
                cwd=repository,
            )
            git(repository, "reset", "--quiet")
            # Remove only this fixture's unreferenced patch blobs. A checker
            # using the repository object store would recreate them.
            for path in objects.rglob("*"):
                if path.is_file() and path not in original_objects:
                    path.unlink()
            patch = Path(temporary) / "reviewed.patch"
            patch.write_bytes(expected)
            index = repository / ".git/index"
            before_index = index.read_bytes()
            state, _message = doctor._reviewed_patch_state(repository, patch)
            self.assertEqual(state, "applied")
            self.assertEqual(index.read_bytes(), before_index)
            self.assertEqual({path for path in objects.rglob("*") if path.is_file()}, original_objects)
            self.assertEqual((repository / "added.txt").read_text(encoding="utf-8"), "new\n")

    def test_missing_optional_disc_and_assets_are_unavailable_not_absent(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-optional-") as temporary:
            root = Path(temporary)
            before = sorted(path.relative_to(root).as_posix() for path in root.rglob("*"))
            disc = doctor.inspect_disc(root, None)
            assets = doctor.inspect_assets(root)
            after = sorted(path.relative_to(root).as_posix() for path in root.rglob("*"))
            self.assertEqual(disc["status"], "unavailable")
            self.assertFalse(disc["proof_of_absence"])
            self.assertIn("unknown", disc["message"])
            self.assertEqual(assets["status"], "unavailable")
            self.assertIn("does not establish", assets["message"])
            self.assertEqual(before, after)

    def test_invalid_requested_disc_fails_validation_without_writing(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-disc-") as temporary:
            root = Path(temporary)
            disc = root / "owned.iso"
            disc.write_bytes(b"not a disc")
            before = disc.read_bytes()
            report = doctor.inspect_disc(root, disc)
            self.assertFalse(report["available"])
            self.assertEqual(report["status"], "unavailable")
            self.assertIn("validation failed", report["message"])
            self.assertEqual(disc.read_bytes(), before)

    def test_explicit_optional_inputs_fail_when_invalid(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-inputs-") as temporary:
            root = Path(temporary)
            assets = root / "missing-assets"
            report = doctor.inspect_assets(root, assets)
            self.assertTrue(report["requested"])
            self.assertEqual(report["status"], "unavailable")
            browser = doctor._playwright(root, root / "missing-playwright")
            self.assertTrue(browser["required"])
            self.assertEqual(browser["status"], "error")

    def test_owned_disc_does_not_require_optional_extraction(self):
        assets = {"requested": False, "missing": ["PlMr.dat"], "invalid": []}
        self.assertEqual(doctor._commands(assets, Path("owned.ciso"), None, True), [])

    def test_configured_node_must_execute_successfully(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-node-") as temporary:
            root = Path(temporary)
            executable = root / "node"
            executable.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
            executable.chmod(0o700)
            with mock.patch.object(doctor, "node_runtime", return_value=executable), \
                    mock.patch.object(doctor, "_tool", side_effect=lambda *args: {"status": "ok"}):
                report = doctor.inspect_tools(root, None)
            self.assertEqual(report["status"], "error")
            self.assertIn("node", report["required_failures"])
            self.assertFalse(report["tools"]["node"]["project_available"])

    def test_changed_source_tree_cannot_report_current_build(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-stale-") as temporary:
            root = Path(temporary)
            build = root / "build/browser-public-release"
            build.mkdir(parents=True)
            identity = {"schema": doctor.RUNTIME_IDENTITY_SCHEMA,
                        "artifact_root": "build/browser-public-release"}
            contents = json.dumps(identity).encode()
            (build.parent / "runtime-public-identity.json").write_bytes(contents)
            with mock.patch.object(doctor, "ROOT", root), \
                    mock.patch.object(doctor, "_read_runtime_identity", return_value=(identity, {}, "hash", contents)), \
                    mock.patch.object(doctor, "_validate_runtime_provenance", side_effect=doctor.BuildError(
                        "runtime identity source tree differs from current checkout: src")):
                report = doctor.inspect_build(root, build)
            self.assertEqual(report["status"], "stale")
            self.assertEqual(report["staleness"], "stale")

    def test_build_commit_only_identity_is_unknown_not_stale(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-build-") as temporary:
            root = Path(temporary)
            build = root / "build/browser"
            build.mkdir(parents=True)
            identity = root / "build/runtime-public-identity.json"
            identity.write_text(json.dumps({
                "schema": "melee-web-runtime-public-build-v2",
                "artifact_root": "build/browser",
                "commit": "a" * 40,
            }), encoding="utf-8")
            report = doctor.inspect_build(root, build)
            self.assertEqual(report["staleness"], "unknown")
            self.assertEqual(report["status"], "unknown")
            self.assertIn("staleness is unknown", report["message"].lower())

    def test_json_output_is_machine_readable_and_main_returns_status(self):
        expected = {"ok": True, "status": "pass", "schema": doctor.SCHEMA}
        output = io.StringIO()
        with mock.patch.object(doctor, "run_doctor", return_value=expected), contextlib.redirect_stdout(output):
            self.assertEqual(doctor.main(["--json"]), 0)
        self.assertEqual(json.loads(output.getvalue()), expected)

        output = io.StringIO()
        failed = {"ok": False, "status": "fail", "schema": doctor.SCHEMA}
        with mock.patch.object(doctor, "run_doctor", return_value=failed), contextlib.redirect_stdout(output):
            self.assertEqual(doctor.main(["--json"]), 1)
        self.assertEqual(json.loads(output.getvalue()), failed)


if __name__ == "__main__":
    unittest.main()
