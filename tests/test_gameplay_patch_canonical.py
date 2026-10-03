import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MELEE = ROOT / ".deps/melee"
sys.path.insert(0, str(ROOT / "scripts"))

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


if __name__ == "__main__":
    unittest.main()
