"""Real-Git regression cases for complete selected-history traversal."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import audit_repository_history as history  # noqa: E402
import check_repository_content as guard  # noqa: E402


class RepositoryHistoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="melee-history-")
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        self.git("init", "-q", "--template=", "--initial-branch=fixture")
        for key, value in (("user.name", "Fixture"), ("user.email", "fixture@example.invalid"),
                           ("commit.gpgsign", "false"), ("tag.gpgsign", "false"),
                           ("core.hooksPath", "/dev/null")):
            self.git("config", key, value)
        self.git("commit", "--allow-empty", "-qm", "Empty root")
        path = self.repo / guard.POLICY_PATH
        path.parent.mkdir()
        path.write_text(json.dumps({"schema": guard.SCHEMA, "file_exceptions": [], "synthetic_secrets": []}))
        self.git("add", ".")
        self.git("commit", "-qm", "Policy")

    def git(self, *args, input=None):
        return subprocess.check_output(["git", "-C", str(self.repo), *args], input=input, stderr=subprocess.PIPE)

    def commit_file(self, name, data, message="Fixture"):
        path = self.repo / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.git("add", "-f", "--", name)
        self.git("commit", "-qm", message)
        return self.git("rev-parse", "HEAD").decode().strip()

    def test_empty_ancestor_is_scanned_and_deleted_payload_is_found(self):
        revision = self.commit_file("removed.txt", b"\0synthetic")
        self.git("rm", "removed.txt")
        self.git("commit", "-qm", "Remove fixture")
        report = history.audit(self.repo, ["HEAD"])
        self.assertEqual(report["commits"], 4)
        self.assertEqual(report["findings"][0]["blob"], self.git("rev-parse", revision + ":removed.txt").decode().strip())

    def test_same_blob_is_checked_at_each_historical_path(self):
        self.commit_file("ordinary.txt", b"plain fixture")
        self.commit_file("private/ordinary.txt", b"plain fixture")
        report = history.audit(self.repo, ["HEAD"])
        self.assertEqual([(x["path"], x["rule"]) for x in report["findings"]],
                         [("private/ordinary.txt", "private_path")])

    def test_selected_namespace_includes_unmerged_branch(self):
        self.git("checkout", "-qb", "separate")
        bad = self.commit_file("input.DAT", b"fixture")
        self.git("update-ref", "refs/publication-audit/heads/separate", bad)
        self.git("checkout", "-q", "fixture")
        self.assertEqual(history.audit(self.repo, ["HEAD"])["findings"], [])
        report = history.audit(self.repo, ["HEAD"], ["refs/publication-audit"])
        self.assertEqual(report["findings"][0]["rule"], "asset_extension")

    def test_secrets_in_commit_and_nested_tag_messages_are_redacted(self):
        token = "gh" + "p_" + "x" * 36
        self.commit_file("note.txt", b"fixture", "Synthetic " + token)
        self.git("tag", "-a", "inner", "-m", "Synthetic " + token)
        self.git("tag", "-a", "outer", "inner", "-m", "Outer annotation")
        report = history.audit(self.repo, ["outer"])
        self.assertEqual([x["rule"] for x in report["metadata_findings"]], ["github_token", "github_token"])
        self.assertNotIn(token, json.dumps(report))

    def test_shallow_history_and_replace_refs_are_rejected(self):
        tip = self.git("rev-parse", "HEAD").strip()
        (self.repo / ".git/shallow").write_bytes(tip + b"\n")
        with self.assertRaisesRegex(guard.CheckError, "shallow"):
            history.audit(self.repo, ["HEAD"])
        (self.repo / ".git/shallow").unlink()
        self.git("update-ref", "refs/replace/" + "0" * 40, tip.decode())
        with self.assertRaisesRegex(guard.CheckError, "replacement"):
            history.audit(self.repo, ["HEAD"])

    def test_missing_namespace_cannot_produce_an_empty_success(self):
        with self.assertRaisesRegex(guard.CheckError, "no refs"):
            history.audit(self.repo, [], ["refs/publication-audit"])

    def test_oversized_commit_cannot_hide_its_tree_in_a_completed_audit(self):
        (self.repo / "fixture.txt").write_bytes(b"AK" + b"IA" + b"X" * 16)
        self.git("add", "fixture.txt")
        self.git("commit", "-q", "--file=-", input=b"x" * (guard.MAX_BLOB_BYTES + 1))
        with self.assertRaisesRegex(guard.CheckError, "Oversized metadata"):
            history.audit(self.repo, ["HEAD"])

    def test_oversized_blob_makes_the_history_content_scan_incomplete(self):
        self.commit_file("large.txt", b"x" * (guard.MAX_BLOB_BYTES + 1))
        with self.assertRaisesRegex(guard.CheckError, "Oversized blob"):
            history.audit(self.repo, ["HEAD"])


if __name__ == "__main__":
    unittest.main()
