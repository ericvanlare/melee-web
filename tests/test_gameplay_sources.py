"""Exercise generated source ownership against actual local Git checkouts."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources


def git(path, *args):
    return subprocess.check_output(["git", *args], cwd=path, text=True, stderr=subprocess.PIPE).strip()


class GameplaySourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="melee generated source ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = self.root / ".deps/melee"
        (self.original / "src").mkdir(parents=True)
        (self.original / "src/fixture.c").write_text("int value = 1;\n")
        git(self.original, "init", "--quiet")
        git(self.original, "add", ".")
        git(self.original, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "commit", "--quiet", "-m", "Authored source")
        self.lock = {"repositories": {"melee": {"commit": git(self.original, "rev-parse", "HEAD")}}}
        (self.root / "patches").mkdir()
        # Dependency verification itself has real-repository tests already.
        # This fixture isolates source preparation without downloading an SDK.
        self.verification = patch("gameplay_sources.verify_sources")
        self.verification.start()
        self.addCleanup(self.verification.stop)
        self.write_patch(2)

    def write_patch(self, value):
        file = self.original / "src/fixture.c"
        file.write_text(f"int value = {value};\n")
        (self.root / "patches/melee-gameplay.patch").write_text(git(self.original, "diff", "--binary") + "\n")
        file.write_text("int value = 1;\n")

    def test_pristine_dependency_idempotence_and_reviewed_patch_update(self):
        source = prepare_sources(self.root, self.lock)
        self.assertEqual((self.original / "src/fixture.c").read_text(), "int value = 1;\n")
        self.assertEqual((source / "fixture.c").read_text(), "int value = 2;\n")
        mtime = (source / "fixture.c").stat().st_mtime_ns
        self.assertEqual(prepare_sources(self.root, self.lock), source)
        self.assertEqual((source / "fixture.c").stat().st_mtime_ns, mtime)
        self.write_patch(4)
        prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 4;\n")
        self.assertEqual(git(self.original, "status", "--porcelain"), "")
        self.assertEqual(git(source.parent, "diff", "--cached"), "")

    def test_unexplained_generated_edits_are_never_overwritten(self):
        source = prepare_sources(self.root, self.lock)
        (source / "fixture.c").write_text("int value = 99;\n")
        self.write_patch(4)
        with self.assertRaisesRegex(ValueError, "changes differ"):
            prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 99;\n")

    def test_invalid_new_patch_keeps_previous_source(self):
        source = prepare_sources(self.root, self.lock)
        patch_file = self.root / "patches/melee-gameplay.patch"
        patch_file.write_text(patch_file.read_text().replace("-int value = 1;", "-int value = 77;"))
        with self.assertRaises(subprocess.CalledProcessError):
            prepare_sources(self.root, self.lock)
        self.assertEqual((source / "fixture.c").read_text(), "int value = 2;\n")


if __name__ == "__main__":
    unittest.main()
