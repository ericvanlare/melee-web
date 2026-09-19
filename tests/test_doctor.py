"""Focused read-only checks for the task-scoped environment doctor."""

from __future__ import annotations

import contextlib
import io
import json
import os
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
    def test_non_executable_configured_node_fails_build_check(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-node-") as temporary:
            root = Path(temporary)
            node = root / "node"
            node.write_text("#!/bin/sh\necho v20\n", encoding="utf-8")
            node.chmod(0o600)
            with mock.patch.object(doctor, "node_runtime", return_value=node), \
                    mock.patch.object(doctor.shutil, "which", return_value=None):
                report = doctor.inspect_tools(root, None, "build")
            self.assertEqual(report["tools"]["node"]["status"], "missing")
            self.assertFalse(report["tools"]["node"]["project_available"])

    def test_browser_task_uses_path_node_without_build_dependencies(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-browser-") as temporary:
            root = Path(temporary)
            (root / "scripts").mkdir()
            (root / "scripts/browser_tools.mjs").write_text("// resolver\n", encoding="utf-8")
            bin_dir = root / "bin"
            bin_dir.mkdir()
            node = bin_dir / "node"
            node.write_text("#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo v22; else echo '{\"playwrightPath\":\"/pw\",\"browser\":{\"executablePath\":\"/chrome\"}}'; fi\n", encoding="utf-8")
            node.chmod(0o700)
            with mock.patch.dict(os.environ, {"PATH": str(bin_dir)}), \
                    mock.patch.object(doctor, "node_runtime", side_effect=AssertionError("SDK fallback used")):
                report = doctor.run_doctor(root, task="browser")
            self.assertTrue(report["ok"], report)
            self.assertEqual(report["browser"]["playwrightPath"], "/pw")
            self.assertEqual(report["tools"]["required_failures"], [])
            self.assertEqual(report["dependencies"]["status"], "not_checked")
            self.assertNotIn("cmake", report["tools"]["tools"])

    def test_build_task_does_not_resolve_browser(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-scope-") as temporary:
            root = Path(temporary)
            with mock.patch.object(doctor, "_resolve_browser", side_effect=AssertionError("browser check")):
                report = doctor.inspect_tools(root, None, "build")
            self.assertIn("cmake", report["tools"])
            self.assertIn("ninja", report["tools"])

    def test_invalid_explicit_build_and_disc_are_failures_without_repair(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-inputs-") as temporary:
            root = Path(temporary)
            missing_build = root / "build/missing"
            build = doctor.inspect_build(root, missing_build)
            self.assertEqual(build["status"], "missing")
            self.assertEqual(build["freshness"], "unknown")
            self.assertNotIn("build.py", build.get("message", ""))
            malformed = root / "owned.iso"
            malformed.write_bytes(b"not a disc")
            disc = doctor.inspect_disc(root, malformed)
            self.assertEqual(disc["status"], "error")
            self.assertIn("validation failed", disc["message"])

    def test_unsupported_disc_revision_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-disc-") as temporary:
            root = Path(temporary)
            disc = root / "rev1.gcm"
            header = bytearray(0x440)
            header[:8] = b"GALE01\0\1"
            disc.write_bytes(header)
            report = doctor.inspect_disc(root, disc)
            self.assertEqual(report["status"], "error")
            self.assertIn("revision 2", report["message"])

    def test_development_build_never_reads_public_sibling_or_claims_freshness(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-build-") as temporary:
            root = Path(temporary)
            development = root / "build/browser"
            development.mkdir(parents=True)
            (root / "build/runtime-public-identity.json").write_text("{}", encoding="utf-8")
            report = doctor.inspect_build(root, development)
            self.assertEqual(report["status"], "present")
            self.assertEqual(report["freshness"], "unknown")
            self.assertNotIn("runtime-public-identity", report.get("message", ""))

    def test_reviewed_patch_check_is_presence_only(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-patches-") as temporary:
            root = Path(temporary)
            (root / "patches").mkdir()
            (root / "patches/aurora-browser.patch").write_text("patch", encoding="utf-8")
            (root / "patches/melee-gameplay.patch").write_text("patch", encoding="utf-8")
            report = doctor.inspect_dependencies(root, {"repositories": {}})
            self.assertEqual(report["status"], "ok")
            for patch in report["reviewed_patches"].values():
                self.assertEqual(patch["application"], "not_checked")
                self.assertIn("not checked", patch["message"])

    def test_real_mismatched_git_dependency_is_read_only(self):
        with tempfile.TemporaryDirectory(prefix="melee-doctor-git-") as temporary:
            root = Path(temporary)
            dependency = root / ".deps/aurora"
            dependency.mkdir(parents=True)
            (dependency / "file.txt").write_text("fixture\n", encoding="utf-8")
            git(dependency, "init", "--quiet")
            git(dependency, "add", ".")
            git(dependency, "-c", "user.name=Doctor", "-c", "user.email=doctor@example.invalid",
                "commit", "--quiet", "-m", "fixture")
            actual = git(dependency, "rev-parse", "HEAD")
            index = dependency / ".git/index"
            before_index = index.read_bytes()
            objects = {path for path in (dependency / ".git/objects").rglob("*") if path.is_file()}
            report = doctor.inspect_dependencies(root, {"repositories": {
                "aurora": {"commit": "0" * 40, "url": "fixture"}},})
            self.assertEqual(report["repositories"]["aurora"]["status"], "mismatch")
            self.assertEqual(git(dependency, "rev-parse", "HEAD"), actual)
            self.assertEqual(index.read_bytes(), before_index)
            self.assertEqual({path for path in (dependency / ".git/objects").rglob("*") if path.is_file()}, objects)

    def test_json_output_is_machine_readable(self):
        expected = {"schema": doctor.SCHEMA, "task": "build", "ok": True, "status": "pass"}
        output = io.StringIO()
        with mock.patch.object(doctor, "run_doctor", return_value=expected), contextlib.redirect_stdout(output):
            self.assertEqual(doctor.main(["--json"]), 0)
        self.assertEqual(json.loads(output.getvalue()), expected)


if __name__ == "__main__":
    unittest.main()
