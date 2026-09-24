"""Exercise publication checks against real, isolated Git snapshots."""

from __future__ import annotations

import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import check_repository_content as guard  # noqa: E402


class RepositoryContentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee-content-check-")
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name)
        self.git("init", "-q", "--template=", "--initial-branch=fixture")
        self.git("config", "core.autocrlf", "false")
        self.policy = {"schema": guard.SCHEMA, "file_exceptions": [], "synthetic_secrets": []}
        self.stage_policy()
        self.stage("README.md", b"Synthetic repository fixture.\n")

    def git(self, *args, input=None):
        return subprocess.run(["git", "-C", str(self.repo), *args], input=input,
                              capture_output=True, check=True).stdout

    def stage(self, path, content):
        destination = self.repo / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(content)
        self.git("add", "-f", "--", path)

    def stage_policy(self):
        self.stage(guard.POLICY_PATH, json.dumps(self.policy).encode())

    def commit(self):
        self.git("-c", "user.name=Repository Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "-c", "core.hooksPath=/dev/null",
                 "commit", "-qm", "Synthetic fixture")
        return self.git("rev-parse", "HEAD").decode().strip()

    def exception(self, path, rule, data, kind="file_exceptions"):
        self.policy[kind].append({"path": path, "rule": rule,
                                  "sha256": hashlib.sha256(data).hexdigest(),
                                  "reason": "Synthetic, nonfunctional test fixture."})
        self.stage_policy()

    def findings(self, ref=None):
        return guard.check(self.repo, ref)[2]

    def rules(self, path, ref=None):
        return {finding.rule for finding in self.findings(ref) if finding.path == path}

    def cli(self, *args):
        return subprocess.run([sys.executable, str(ROOT / "scripts/check_repository_content.py"),
                               "--repo", str(self.repo), *args], capture_output=True, text=True)

    def test_clean_index_and_cli(self):
        self.assertEqual(self.findings(), [])
        result = self.cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 tracked entries checked (index); 0 findings", result.stdout)

    def test_ignored_untracked_inputs_are_not_read_but_force_add_is_rejected(self):
        self.stage(".gitignore", b"assets-local/\n")
        path = "assets-local/private.DAT"
        (self.repo / "assets-local").mkdir()
        (self.repo / path).write_bytes(b"\0synthetic game input")
        self.assertEqual(self.findings(), [])
        self.git("add", "-f", "--", path)
        self.assertEqual(self.rules(path), {"private_path", "asset_extension", "binary_content"})

    def test_index_is_checked_instead_of_unstaged_working_bytes(self):
        (self.repo / "README.md").write_bytes(b"\0unstaged fixture")
        self.assertEqual(self.findings(), [])
        self.git("add", "README.md")
        (self.repo / "README.md").write_bytes(b"apparently clean working tree\n")
        self.assertEqual(self.rules("README.md"), {"binary_content"})

    def test_commit_uses_its_own_tree_and_policy(self):
        self.stage("fixture.txt", b"\0unreviewed fixture")
        revision = self.commit()
        self.exception("fixture.txt", "binary_content", b"\0unreviewed fixture")
        self.assertEqual(self.findings(), [])
        self.assertEqual(self.rules("fixture.txt", revision), {"binary_content"})
        self.assertEqual(self.cli("--ref", revision).returncode, 1)

    def test_missing_policy_and_invalid_ref_fail_incomplete(self):
        self.git("rm", "--cached", guard.POLICY_PATH)
        result = self.cli()
        self.assertEqual(result.returncode, 2)
        self.assertIn("incomplete", result.stderr)
        self.assertEqual(self.cli("--ref", "does-not-exist").returncode, 2)

    def test_renamed_binary_and_encoded_payload_are_rejected(self):
        self.stage("notes.txt", b"\0synthetic payload")
        self.stage("notes.md", base64.b64encode(bytes(range(256)) * 3))
        self.stage("short.b64", b"even invalid encoding requires review")
        self.assertEqual(self.rules("notes.txt"), {"binary_content"})
        self.assertEqual(self.rules("notes.md"), {"encoded_payload"})
        self.assertEqual(self.rules("short.b64"), {"encoded_payload"})

    def test_binary_exception_requires_exact_path_and_full_file_hash(self):
        original = b"\0reviewed fixture"
        self.stage("image.png", original)
        self.exception("image.png", "binary_content", original)
        self.assertEqual(self.findings(), [])
        self.stage("copy.png", original)
        self.assertEqual(self.rules("copy.png"), {"binary_content"})
        self.stage("image.png", original + b"modified")
        self.assertEqual(self.rules("image.png"), {"binary_content"})

    def test_file_exception_does_not_suppress_secret_scan(self):
        secret = b"AK" + b"IA" + b"X" * 16
        content = b"\0fixture " + secret
        self.stage("image.png", content)
        self.exception("image.png", "binary_content", content)
        self.assertEqual(self.rules("image.png"), {"aws_access_key"})

    def test_synthetic_exception_does_not_exempt_other_tokens_or_files(self):
        known = b"AK" + b"IA" + b"X" * 16
        unknown = b"AK" + b"IA" + b"Y" * 16
        self.stage("fixture.py", known)
        self.exception("fixture.py", "aws_access_key", known, "synthetic_secrets")
        self.assertEqual(self.findings(), [])
        self.stage("copy.py", known)
        self.stage("fixture.py", known + b"\n" + unknown)
        findings = self.findings()
        self.assertIn(guard.Finding("copy.py", "aws_access_key", 1), findings)
        self.assertIn(guard.Finding("fixture.py", "aws_access_key", 2), findings)
        self.assertNotIn(guard.Finding("fixture.py", "aws_access_key", 1), findings)

    def test_secret_rules_reject_generated_nonfunctional_samples(self):
        samples = {
            "private_key": b"-----BEGIN " + b"RSA PRIVATE KEY-----",
            "aws_access_key": b"AS" + b"IA" + b"Z" * 16,
            "github_token": b"gh" + b"p_" + b"x" * 36,
            "slack_token": b"xo" + b"xb-" + b"x" * 24,
            "api_secret": b"sk" + b"-proj-" + b"x" * 40,
            "sensitive_assignment": b'password = "' + b"x" * 24 + b'"',
        }
        for rule, content in samples.items():
            with self.subTest(rule=rule):
                self.stage("fixture.txt", content)
                self.assertIn(rule, self.rules("fixture.txt"))
                result = self.cli()
                self.assertEqual(result.returncode, 1)
                self.assertNotIn(content.decode(), result.stdout + result.stderr)

    def test_secret_filename_is_redacted_and_control_characters_escaped(self):
        secret = "gh" + "p_" + "x" * 36
        self.stage(secret + ".txt", b"fixture")
        self.stage("line\n::error::name.txt", b"fixture")
        result = self.cli()
        self.assertEqual(result.returncode, 1)
        self.assertNotIn(secret, result.stdout + result.stderr)
        self.assertIn('"<redacted path>": sensitive_filename', result.stdout)
        self.assertIn('"line\\n::error::name.txt": unsafe_path', result.stdout)

    def test_private_paths_and_personal_paths(self):
        self.stage(".env.example", b"LOCAL_OPTION=example\n")
        self.assertEqual(self.findings(), [])
        self.stage(".env", b"LOCAL_OPTION=example\n")
        self.stage("nested/.aws/config", b"example")
        self.stage("notes.txt", b"/" + b"Users/fixture/personally-owned-file")
        self.assertEqual(self.rules(".env"), {"private_path"})
        self.assertEqual(self.rules("nested/.aws/config"), {"private_path"})
        self.assertEqual(self.rules("notes.txt"), {"personal_path"})

    def test_symlink_is_rejected_without_following_target(self):
        (self.repo / "outside").write_bytes(b"\0private target")
        (self.repo / "link.txt").symlink_to(self.repo / "outside")
        self.git("add", "link.txt")
        self.assertEqual(self.rules("link.txt"), {"non_regular_entry"})

    def test_reference_capture_local_state_paths_are_rejected_even_when_text(self):
        paths = ("captures/note.txt", "private/note.txt", "SECRETS/note.txt",
                 "save-states/note.txt", "states/note.txt", "memorycards/note.txt",
                 "config/note.txt", "configuration/note.txt", "config.json",
                 "dolphin.ini", "gcpadnew.ini")
        for path in paths:
            self.stage(path, b"synthetic local state")
        for path in paths:
            with self.subTest(path=path):
                self.assertEqual(self.rules(path), {"private_path"})
        for path in ("input.slp", "input.mwri"):
            self.stage(path, b"synthetic input")
            self.assertEqual(self.rules(path), {"asset_extension"})

    def test_gitlink_is_rejected(self):
        revision = self.commit()
        self.git("update-index", "--add", "--cacheinfo", f"160000,{revision},vendor")
        self.assertEqual(self.rules("vendor"), {"non_regular_entry"})

    def test_unmerged_index_fails_incomplete(self):
        oid = self.git("hash-object", "-w", "--stdin", input=b"conflict fixture").decode().strip()
        self.git("update-index", "--index-info", input=f"100644 {oid} 1\tconflict.txt\n100644 {oid} 2\tconflict.txt\n".encode())
        with self.assertRaisesRegex(guard.CheckError, "unmerged"):
            self.findings()

    def test_oversized_blob_is_rejected_before_reading_its_contents(self):
        self.stage("oversized.txt", b"x" * (guard.MAX_BLOB_BYTES + 1))
        actual_read = guard.BlobReader.read

        def bounded_read(reader, oid, size):
            self.assertLessEqual(size, guard.MAX_BLOB_BYTES)
            return actual_read(reader, oid, size)

        with patch.object(guard.BlobReader, "read", bounded_read):
            self.assertEqual(self.rules("oversized.txt"), {"large_blob"})

    def test_policy_cannot_exempt_secret_rules_or_private_paths_as_files(self):
        for rule in ("private_path", "private_key", "aws_access_key", "large_blob"):
            with self.subTest(rule=rule):
                self.policy["file_exceptions"] = []
                self.exception("fixture.txt", rule, b"fixture")
                with self.assertRaises(guard.CheckError):
                    self.findings()

    def test_private_key_marker_cannot_be_a_synthetic_exception(self):
        self.exception("fixture.txt", "private_key", b"fixture", "synthetic_secrets")
        with self.assertRaises(guard.CheckError):
            self.findings()

    def test_duplicate_or_unknown_policy_fields_fail_incomplete(self):
        malformed = [b'{"schema": "one", "schema": "two"}', b"not JSON",
                     json.dumps(self.policy).encode("utf-16"),
                     json.dumps({**self.policy, "ignore_all": True}).encode()]
        for content in malformed:
            with self.subTest(content=content):
                self.stage(guard.POLICY_PATH, content)
                self.assertEqual(self.cli().returncode, 2)


if __name__ == "__main__":
    unittest.main()
