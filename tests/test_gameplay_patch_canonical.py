import io
import os
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
AURORA = ROOT / ".deps/aurora"
sys.path.insert(0, str(ROOT / "scripts"))

import canonicalize_gameplay_patch as canonicalizer  # noqa: E402
from canonicalize_gameplay_patch import canonical_patch, merge_patches  # noqa: E402


def git(repository, *args, data=None):
    return subprocess.run(["git", *args], cwd=repository, input=data, capture_output=True, check=True).stdout


class GameplayPatchCanonicalTests(unittest.TestCase):
    """The reviewed gameplay and Aurora patches stay sorted with one diff per file.

    Hand-appended file diffs make unrelated pull requests conflict at the end
    of the patch. Run scripts/canonicalize_gameplay_patch.py (with
    --target aurora for patches/aurora-browser.patch) after editing one.
    """

    @unittest.skipUnless((MELEE / ".git").exists(), "pinned Melee checkout is not bootstrapped")
    def test_reviewed_patch_is_canonical(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/canonicalize_gameplay_patch.py"), "--check"],
                                cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless((AURORA / ".git").exists(), "pinned Aurora checkout is not bootstrapped")
    def test_reviewed_aurora_patch_is_canonical(self):
        result = subprocess.run([sys.executable, str(ROOT / "scripts/canonicalize_gameplay_patch.py"),
                                 "--target", "aurora", "--check"],
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

    def test_canonical_bytes_ignore_user_diff_configuration(self):
        (self.repository / "a.c").write_text("".join(f"line {i}\n" if i % 4 else "\n" for i in range(20)))
        git(self.repository, "commit", "-q", "-am", "blank lines")
        self.commit = git(self.repository, "rev-parse", "HEAD").decode().strip()
        data = self.patch([("a.c", (2, "first\n")), ("a.c", (9, "second\n")), ("b.c", (5, "other\n"))])
        settings = self.state / "user-gitconfig"
        order = self.state / "order"
        order.write_text("b.c\na.c\n")
        settings.write_text("[diff]\n\tcontext = 1\n\tinterHunkContext = 9\n\talgorithm = histogram\n"
                            "\tindentHeuristic = false\n\tsuppressBlankEmpty = true\n\tnoprefix = true\n"
                            f"\torderFile = {order}\n\trelative = true\n")
        isolated = {"GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull}
        with mock_patch.dict("os.environ", isolated):
            expected, _ = canonical_patch(self.repository, self.commit, data)
        with mock_patch.dict("os.environ", dict(isolated, GIT_CONFIG_GLOBAL=str(settings))):
            configured, _ = canonical_patch(self.repository, self.commit, data)
        self.assertEqual(configured, expected)

    def _run_cli(self, arguments):
        lock = {"repositories": {"melee": {"commit": self.commit}, "aurora": {"commit": self.commit}}}
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

    def test_cli_rejects_check_and_merge_without_writing(self):
        target = self.project / "patches/melee-gameplay.patch"
        target.write_bytes(b"preserve")
        with redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as error:
            self._run_cli(["--repository", str(self.repository), "--patch", str(target), "--check",
                           "--merge", "base", "ours", "theirs"])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("not allowed with argument", stderr.getvalue())
        self.assertEqual(target.read_bytes(), b"preserve")

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

    def test_cli_aurora_target_reads_the_pinned_commit_under_an_applied_patch(self):
        # Bootstrap leaves the Aurora patch applied in .deps/aurora's working tree.
        patch = self.patch([("a.c", (1, "browser\n")), ("b.c", (3, "browser\n"))])
        git(self.repository, "apply", "-", data=patch)
        target = self.project / "patches/aurora-browser.patch"
        target.write_bytes(patch)
        canonical, _ = canonical_patch(self.repository, self.commit, patch)
        self.assertNotEqual(patch, canonical)  # `git diff` abbreviates blob indexes
        with redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(self._run_cli(["--target", "aurora", "--repository", str(self.repository),
                                            "--patch", str(target), "--check"]), 1)
        self.assertIn("--target aurora", stderr.getvalue())
        self.assertEqual(self._run_cli(["--target", "aurora", "--repository", str(self.repository),
                                        "--patch", str(target)]), 0)
        self.assertEqual(target.read_bytes(), canonical)
        self.assertEqual(self._run_cli(["--target", "aurora", "--repository", str(self.repository),
                                        "--patch", str(target), "--check"]), 0)
        self.assertIn("browser\n", (self.repository / "a.c").read_text())

    def test_cli_aurora_target_still_requires_the_lockfile_commit(self):
        target = self.project / "patches/aurora-browser.patch"
        target.write_bytes(self.patch([("a.c", (1, "browser\n"))]))
        (self.repository / "a.c").write_text("new commit\n")
        git(self.repository, "commit", "-q", "-am", "unrelated")
        with redirect_stderr(io.StringIO()) as stderr, self.assertRaises(SystemExit) as error:
            self._run_cli(["--target", "aurora", "--repository", str(self.repository),
                           "--patch", str(target), "--check"])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("expected", stderr.getvalue())

    def test_cli_aurora_merge_reads_the_aurora_patch_from_each_revision(self):
        base = self.patch([("a.c", (1, "base\n"))])
        ours = self.patch([("a.c", (1, "base\n")), ("a.c", (5, "ours\n"))])
        theirs = self.patch([("a.c", (1, "base\n")), ("b.c", (7, "theirs\n"))])
        git(self.project, "init", "-q")
        git(self.project, "config", "user.email", "test@example.invalid")
        git(self.project, "config", "user.name", "Test")
        gameplay = self.project / "patches/melee-gameplay.patch"
        target = self.project / "patches/aurora-browser.patch"
        gameplay.write_bytes(b"unrelated gameplay patch\n")
        revisions = []
        for start, data in ((None, base), ("base", ours), ("base", theirs)):
            if start is not None:
                git(self.project, "switch", "-q", "--detach", revisions[0])
            target.write_bytes(data)
            git(self.project, "add", ".")
            git(self.project, "commit", "-q", "--allow-empty", "-m", "revision")
            revisions.append(git(self.project, "rev-parse", "HEAD").decode().strip())
        git(self.project, "switch", "-q", "--detach", revisions[1])

        expected, conflicts = merge_patches(self.repository, self.commit, base, ours, theirs)
        self.assertEqual(conflicts, [])
        self.assertEqual(self._run_cli(["--target", "aurora", "--repository", str(self.repository),
                                        "--merge", *revisions]), 0)
        self.assertEqual(target.read_bytes(), expected)
        self.assertEqual(gameplay.read_bytes(), b"unrelated gameplay patch\n")
        text = self.applied(expected)
        self.assertIn("ours\n", text["a.c"])
        self.assertIn("theirs\n", text["b.c"])


if __name__ == "__main__":
    unittest.main()
