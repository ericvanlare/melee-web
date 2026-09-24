"""Check dependency safety against real temporary Git repositories."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bootstrap", ROOT / "scripts/bootstrap.py")
bootstrap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bootstrap)
sys.path.insert(0, str(ROOT / "scripts"))
import build_reference_dolphin
sys.path.insert(0, str(ROOT / "tools"))
import reference_capture_environment


def git(path, *arguments):
    return subprocess.check_output(
        ["git", *arguments], cwd=path, stderr=subprocess.PIPE, text=True,
    ).strip()


class RepositoryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="melee web scripts ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo = self.root / "upstream"
        self.repo.mkdir()
        git(self.repo, "init", "--quiet")
        (self.repo / "alpha.txt").write_text("original\n", encoding="utf-8")
        (self.repo / "other.txt").write_text("untouched\n", encoding="utf-8")
        (self.repo / ".gitignore").write_text("cache/\n", encoding="utf-8")
        git(self.repo, "add", ".")
        git(self.repo, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
            "commit", "--quiet", "-m", "Fixture")
        self.commit = git(self.repo, "rev-parse", "HEAD")
        self.deps = self.root / ".deps"
        self.deps.mkdir()
        self.spec = {"url": str(self.repo), "commit": self.commit}
        # Generate the reviewable patch with Git, then restore only our fixture file.
        (self.repo / "alpha.txt").write_text("patched\n", encoding="utf-8")
        self.patch = self.root / "browser changes.patch"
        self.patch.write_text(git(self.repo, "diff", "--binary") + "\n", encoding="utf-8")
        (self.repo / "alpha.txt").write_text("original\n", encoding="utf-8")

    def test_fetches_exact_pin_and_preserves_existing_checkout(self):
        path = bootstrap.ensure_repository(self.deps, "aurora", self.spec)
        self.assertEqual(git(path, "rev-parse", "HEAD"), self.commit)
        (path / "alpha.txt").write_text("my local work\n", encoding="utf-8")
        bootstrap.ensure_repository(self.deps, "aurora", self.spec)
        self.assertEqual((path / "alpha.txt").read_text(), "my local work\n")
        with self.assertRaisesRegex(ValueError, "local changes"):
            bootstrap.require_clean(path)

    def test_failed_fetch_leaves_destination_available_for_retry(self):
        with self.assertRaises(subprocess.CalledProcessError):
            bootstrap.ensure_repository(self.deps, "aurora", {**self.spec, "commit": "0" * 40})
        self.assertFalse((self.deps / "aurora").exists())
        self.assertEqual(list(self.deps.iterdir()), [])
        path = bootstrap.ensure_repository(self.deps, "aurora", self.spec)
        self.assertEqual((path / "alpha.txt").read_text(), "original\n")

    def test_refuses_wrong_pin_without_changing_files(self):
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            bootstrap.verify_repository(self.repo, "0" * 40)
        self.assertEqual(git(self.repo, "rev-parse", "HEAD"), self.commit)
        self.assertEqual((self.repo / "alpha.txt").read_text(), "original\n")

    def test_refuses_directory_inside_another_repository(self):
        nested = self.repo / "nested"
        nested.mkdir()
        with self.assertRaisesRegex(ValueError, "standalone Git checkout"):
            bootstrap.verify_repository(nested, self.commit)

    def test_refuses_symlink_checkout(self):
        (self.deps / "aurora").symlink_to(self.repo, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            bootstrap.ensure_repository(self.deps, "aurora", self.spec)
        self.assertEqual((self.repo / "alpha.txt").read_text(), "original\n")

    def test_patch_is_idempotent_and_leaves_real_index_unchanged(self):
        index = (self.repo / ".git/index").read_bytes()
        self.assertEqual(bootstrap.patch_state(self.repo, self.patch), "clean")
        bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual((self.repo / "alpha.txt").read_text(), "patched\n")
        bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual(bootstrap.patch_state(self.repo, self.patch), "applied")
        self.assertEqual((self.repo / ".git/index").read_bytes(), index)
        self.assertEqual(git(self.repo, "diff", "--cached"), "")

    def prepare_patch_upgrade(self):
        bootstrap.apply_patch(self.repo, self.patch)
        git(self.repo, "add", "alpha.txt")
        previous = git(self.repo, "write-tree")
        git(self.repo, "reset", "--mixed", "--quiet", "HEAD")
        (self.repo / "alpha.txt").write_text("updated reviewed patch\n")
        self.patch.write_text(git(self.repo, "diff", "--binary") + "\n")
        (self.repo / "alpha.txt").write_text("patched\n")
        return previous

    def test_recognized_patch_upgrade_preserves_index_and_unchanged_file_timestamps(self):
        previous = self.prepare_patch_upgrade()
        steady = self.repo / "other.txt"
        os.utime(steady, ns=(1_000_000_000, 1_000_000_000))
        index = (self.repo / ".git/index").read_bytes()
        bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
        self.assertEqual((self.repo / "alpha.txt").read_text(), "updated reviewed patch\n")
        self.assertEqual(steady.stat().st_mtime_ns, 1_000_000_000)
        self.assertEqual(bootstrap.patch_state(self.repo, self.patch), "applied")
        self.assertEqual((self.repo / ".git/index").read_bytes(), index)
        changed_mtime = (self.repo / "alpha.txt").stat().st_mtime_ns
        bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
        self.assertEqual((self.repo / "alpha.txt").stat().st_mtime_ns, changed_mtime)

    def test_upgrade_requires_the_exact_recognized_previous_tree(self):
        self.prepare_patch_upgrade()
        before = git(self.repo, "diff", "--binary")
        for previous in (None, "0" * 40):
            with self.subTest(previous=previous), self.assertRaisesRegex(ValueError, "changes differ"):
                bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
            self.assertEqual(git(self.repo, "diff", "--binary"), before)

    def test_upgrade_refuses_unrelated_edits_without_changing_any_file(self):
        previous = self.prepare_patch_upgrade()
        (self.repo / "other.txt").write_text("unrelated local work\n")
        before = git(self.repo, "diff", "--binary")
        index = (self.repo / ".git/index").read_bytes()
        with self.assertRaisesRegex(ValueError, "changes differ"):
            bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
        self.assertEqual(git(self.repo, "diff", "--binary"), before)
        self.assertEqual((self.repo / ".git/index").read_bytes(), index)

    def test_upgrade_refuses_untracked_and_staged_work(self):
        previous = self.prepare_patch_upgrade()
        notes = self.repo / "notes.txt"
        notes.write_text("keep these notes\n")
        before = git(self.repo, "diff", "--binary")
        with self.assertRaisesRegex(ValueError, "changes differ"):
            bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
        git(self.repo, "add", "notes.txt")
        index = (self.repo / ".git/index").read_bytes()
        with self.assertRaisesRegex(ValueError, "staged changes"):
            bootstrap.apply_patch(self.repo, self.patch, previous_tree=previous)
        self.assertEqual(git(self.repo, "diff", "--binary"), before)
        self.assertEqual(notes.read_text(), "keep these notes\n")
        self.assertEqual((self.repo / ".git/index").read_bytes(), index)

    def test_refuses_patch_with_unrelated_local_change(self):
        bootstrap.apply_patch(self.repo, self.patch)
        (self.repo / "other.txt").write_text("my work\n", encoding="utf-8")
        before = git(self.repo, "diff", "--binary")
        with self.assertRaisesRegex(ValueError, "changes differ"):
            bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual(git(self.repo, "diff", "--binary"), before)

    def test_refuses_partial_or_conflicting_patch(self):
        (self.repo / "alpha.txt").write_text("a different fix\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "changes differ"):
            bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual((self.repo / "alpha.txt").read_text(), "a different fix\n")

    def test_patch_can_add_and_delete_files_idempotently(self):
        added = self.repo / "browser.txt"
        deleted = self.repo / "other.txt"
        added.write_text("new browser implementation\n", encoding="utf-8")
        deleted.unlink()
        git(self.repo, "add", "--all")
        self.patch.write_text(git(self.repo, "diff", "--cached", "--binary") + "\n", encoding="utf-8")
        git(self.repo, "reset", "--mixed", "--quiet", "HEAD")
        added.unlink()
        deleted.write_text("untouched\n", encoding="utf-8")
        bootstrap.apply_patch(self.repo, self.patch)
        bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual(added.read_text(), "new browser implementation\n")
        self.assertFalse(deleted.exists())
        self.assertEqual(git(self.repo, "diff", "--cached"), "")

    def test_refuses_untracked_and_staged_changes(self):
        untracked = self.repo / "notes.txt"
        untracked.write_text("my notes\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "changes differ"):
            bootstrap.apply_patch(self.repo, self.patch)
        git(self.repo, "add", "notes.txt")
        with self.assertRaisesRegex(ValueError, "staged"):
            bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual(untracked.read_text(), "my notes\n")

    def test_ignored_sdk_cache_does_not_count_as_source_drift(self):
        cache = self.repo / "cache"
        cache.mkdir()
        (cache / "generated").write_text("cache data\n", encoding="utf-8")
        bootstrap.require_clean(self.repo)
        bootstrap.apply_patch(self.repo, self.patch)
        self.assertEqual(bootstrap.patch_state(self.repo, self.patch), "applied")


class LockTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.lock = json.loads((ROOT / "dependencies.lock.json").read_text())

    def read(self, lock):
        (self.root / "dependencies.lock.json").write_text(json.dumps(lock), encoding="utf-8")
        return bootstrap.read_lock(self.root)

    def test_current_lock_is_pinned(self):
        self.assertEqual(self.read(self.lock), self.lock)

    def test_rejects_unpinned_or_escaping_entries(self):
        invalid = []
        lock = copy.deepcopy(self.lock)
        lock["repositories"]["aurora"]["commit"] = "main"
        invalid.append(lock)
        lock = copy.deepcopy(self.lock)
        lock["repositories"]["../outside"] = lock["repositories"]["aurora"]
        invalid.append(lock)
        lock = copy.deepcopy(self.lock)
        lock["emscripten"] = "latest"
        invalid.append(lock)
        lock = copy.deepcopy(self.lock)
        lock["python_build_packages"] = ["cmake>=3"]
        invalid.append(lock)
        lock = copy.deepcopy(self.lock)
        lock["reference_tools"]["slippi-js"]["commit"] = "main"
        invalid.append(lock)
        lock = copy.deepcopy(self.lock)
        lock["reference_tools"]["slippi-js"]["version"] = "latest"
        invalid.append(lock)
        for lock in invalid:
            with self.subTest(lock=lock), self.assertRaises(ValueError):
                self.read(lock)

    def test_dolphin_source_pin_is_explicit_and_matches_consumers(self):
        dolphin = self.lock["reference_tools"]["dolphin"]
        self.assertEqual(dolphin["kind"], "source")
        self.assertNotIn("package", dolphin)
        self.assertEqual(dolphin["commit"], build_reference_dolphin.PINNED_COMMIT)
        self.assertEqual(dolphin["commit"], reference_capture_environment.DOLPHIN_REVISION)
        self.assertEqual(self.read(self.lock), self.lock)

    def test_rejects_unpinned_dolphin_source_commit(self):
        lock = copy.deepcopy(self.lock)
        lock["reference_tools"]["dolphin"]["commit"] = "main"
        with self.assertRaisesRegex(ValueError, "reference tool commit"):
            self.read(lock)

    def test_rejects_unknown_reference_tool_kind(self):
        lock = copy.deepcopy(self.lock)
        lock["reference_tools"]["dolphin"]["kind"] = "binary"
        with self.assertRaisesRegex(ValueError, "unsupported reference tool kind"):
            self.read(lock)

    def test_invalid_build_jobs_fail_before_accessing_dependencies(self):
        for jobs in ("0", "-1", "invalid"):
            with self.subTest(jobs=jobs):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "scripts/build.py"), "--jobs", jobs],
                    cwd=self.root, capture_output=True, text=True,
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn("--jobs", result.stderr)


if __name__ == "__main__":
    unittest.main()
