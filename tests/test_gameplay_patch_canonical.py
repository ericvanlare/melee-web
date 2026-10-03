import io
import shutil
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr
from unittest.mock import patch as mock_patch
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MELEE = ROOT / ".deps/melee"
sys.path.insert(0, str(ROOT / "scripts"))

import canonicalize_gameplay_patch as canonicalizer  # noqa: E402
from canonicalize_gameplay_patch import canonical_patch, merge_patches  # noqa: E402


def git(repository, *args, data=None):
    return subprocess.run(["git", *args], cwd=repository, input=data, capture_output=True, check=True).stdout


class GameplayPatchCanonicalTests(unittest.TestCase):
    """The reviewed patch stays sorted with one diff per file.

    Hand-appended file diffs make unrelated pull requests conflict at the end
    of the patch. Run scripts/canonicalize_gameplay_patch.py after editing it.
    """

    @unittest.skipUnless((MELEE / ".git").exists(), "pinned Melee checkout is not bootstrapped")
    def test_reviewed_patch_is_canonical(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/canonicalize_gameplay_patch.py"), "--check"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.repository = Path(self.temporary.name)
        git(self.repository, "init", "-q")
        git(self.repository, "config", "user.email", "test@example.invalid")
        git(self.repository, "config", "user.name", "Test")
        for name in ("a.c", "b.c"):
            (self.repository / name).write_text("".join(f"line {i}\n" for i in range(20)))
        git(self.repository, "add", ".")
        git(self.repository, "commit", "-q", "-m", "base")
        self.commit = git(self.repository, "rev-parse", "HEAD").decode().strip()
        self.project = Path(tempfile.mkdtemp(prefix="canonical-project "))
        self.addCleanup(shutil.rmtree, self.project)
        (self.project / "patches").mkdir()
        self.state = Path(tempfile.mkdtemp(prefix="canonical-state "))
        self.addCleanup(shutil.rmtree, self.state)

    def tearDown(self):
        self.temporary.cleanup()

    def patch(self, edits):
        for name, (index, text) in edits:
            lines = (self.repository / name).read_text().splitlines(keepends=True)
            lines[index] = text
            (self.repository / name).write_text("".join(lines))
        data = git(self.repository, "diff")
        git(self.repository, "checkout", "-q", "--", ".")
        return data

    def applied(self, data):
        git(self.repository, "apply", "-", data=data)
        text = {name: (self.repository / name).read_text() for name in ("a.c", "b.c")}
        git(self.repository, "checkout", "-q", "--", ".")
        return text

    def test_canonical_form_merges_repeated_file_diffs_in_path_order(self):
        first = self.patch([("a.c", (1, "first\n"))])
        # A later hand-appended diff of a.c, relative to the file after `first`.
        git(self.repository, "apply", "-", data=first)
        git(self.repository, "commit", "-q", "-am", "first")
        (self.repository / "a.c").write_text((self.repository / "a.c").read_text().replace("line 15\n", "second\n"))
        second = git(self.repository, "diff")
        git(self.repository, "checkout", "-q", "--", ".")
        git(self.repository, "reset", "-q", "--hard", self.commit)
        appended = self.patch([("b.c", (1, "other\n"))]) + first + second
        canonical, _ = canonical_patch(self.repository, self.commit, appended)
        files = [line.split()[2] for line in canonical.decode().splitlines() if line.startswith("diff --git")]
        self.assertEqual(files, ["a/a.c", "a/b.c"])
        self.assertEqual(self.applied(canonical), self.applied(appended))
        self.assertIn("second\n", self.applied(canonical)["a.c"])

    def test_tree_merge_combines_disjoint_edits_to_one_file(self):
        base = self.patch([("a.c", (1, "base\n"))])
        ours = self.patch([("a.c", (1, "base\n")), ("a.c", (5, "ours\n"))])
        theirs = self.patch([("a.c", (1, "base\n")), ("a.c", (7, "theirs\n"))])
        merged, conflicts = merge_patches(self.repository, self.commit, base, ours, theirs)
        self.assertEqual(conflicts, [])
        text = self.applied(merged)["a.c"]
        self.assertIn("base\n", text)
        self.assertIn("ours\n", text)
        self.assertIn("theirs\n", text)

    def test_tree_merge_reports_overlapping_source_edits(self):
        base = self.patch([("a.c", (1, "base\n"))])
        ours = self.patch([("a.c", (6, "ours\n"))])
        theirs = self.patch([("a.c", (6, "theirs\n"))])
        merged, conflicts = merge_patches(self.repository, self.commit, base, ours, theirs)
        self.assertIsNone(merged)
        self.assertEqual(conflicts, ["a.c"])

    def _run_cli(self, arguments):
        lock = {"repositories": {"melee": {"commit": self.commit}}}
        with mock_patch.object(canonicalizer, "ROOT", self.project), \
             mock_patch.object(canonicalizer, "read_lock", return_value=lock), \
             mock_patch.dict("os.environ", {
                 "MELEE_RESOURCE_STATE": str(self.state.resolve()),
                 "MELEE_WARN_FREE_GB": "0.001",
             }):
            return canonicalizer.main(arguments)

    def test_cli_rejects_a_symlink_patch_target_before_writing(self):
        target = self.project / "patches/melee-gameplay.patch"
        outside = self.project / "outside.patch"
        outside.write_bytes(b"preserve")
        target.symlink_to(outside)
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            self._run_cli(["--repository", str(self.repository), "--patch", str(target)])
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(outside.read_bytes(), b"preserve")

    def test_cli_rejects_a_target_changed_during_canonicalization(self):
        target = self.project / "patches/melee-gameplay.patch"
        target.write_bytes(b"before")

        def canonicalize_with_concurrent_edit(repository, commit, current):
            self.assertEqual(current, b"before")
            target.write_bytes(b"edited by another operation")
            return b"canonical", "tree"

        with mock_patch.object(canonicalizer, "canonical_patch", canonicalize_with_concurrent_edit), \
             redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            self._run_cli(["--repository", str(self.repository), "--patch", str(target)])
        self.assertEqual(error.exception.code, 2)
        self.assertEqual(target.read_bytes(), b"edited by another operation")

    def test_cli_merge_writes_the_source_tree_merge_atomically(self):
        base = self.patch([("a.c", (1, "base\n"))])
        ours = self.patch([("a.c", (1, "base\n")), ("a.c", (5, "ours\n"))])
        theirs = self.patch([("a.c", (1, "base\n")), ("a.c", (7, "theirs\n"))])
        git(self.project, "init", "-q")
        git(self.project, "config", "user.email", "test@example.invalid")
        git(self.project, "config", "user.name", "Test")
        target = self.project / "patches/melee-gameplay.patch"
        target.write_bytes(base)
        git(self.project, "add", ".")
        git(self.project, "commit", "-q", "-m", "base")
        base_revision = git(self.project, "rev-parse", "HEAD").decode().strip()
        target.write_bytes(ours)
        git(self.project, "add", ".")
        git(self.project, "commit", "-q", "-m", "ours")
        ours_revision = git(self.project, "rev-parse", "HEAD").decode().strip()
        git(self.project, "switch", "-q", "-c", "theirs", base_revision)
        target.write_bytes(theirs)
        git(self.project, "add", ".")
        git(self.project, "commit", "-q", "-m", "theirs")
        theirs_revision = git(self.project, "rev-parse", "HEAD").decode().strip()
        git(self.project, "switch", "-q", "--detach", ours_revision)

        expected, conflicts = merge_patches(self.repository, self.commit, base, ours, theirs)
        self.assertEqual(conflicts, [])
        self.assertEqual(
            self._run_cli(["--repository", str(self.repository), "--patch", str(target), "--merge",
                           base_revision, ours_revision, theirs_revision]), 0)
        self.assertEqual(target.read_bytes(), expected)
        self.assertEqual(self.applied(target.read_bytes())["a.c"].count("ours\n"), 1)
        self.assertIn("theirs\n", self.applied(target.read_bytes())["a.c"])

    def test_cli_requires_a_clean_pinned_source_checkout(self):
        target = self.project / "patches/melee-gameplay.patch"
        target.write_bytes(self.patch([("a.c", (1, "base\n"))]))
        (self.repository / "a.c").write_text("dirty\n")
        with redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as error:
            self._run_cli(["--repository", str(self.repository), "--patch", str(target), "--check"])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("local changes", stderr.getvalue())

    def test_cli_requires_the_lockfile_commit_at_source_head(self):
        target = self.project / "patches/melee-gameplay.patch"
        target.write_bytes(self.patch([("a.c", (1, "base\n"))]))
        (self.repository / "a.c").write_text("new commit\n")
        git(self.repository, "add", ".")
        git(self.repository, "commit", "-q", "-m", "unrelated")
        with redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as error:
            self._run_cli(["--repository", str(self.repository), "--patch", str(target), "--check"])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("expected", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
