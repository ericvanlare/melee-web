"""Synthetic Git trees exercise source reconstruction and immutable build receipts."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("reference_dolphin_build", ROOT / "scripts/build_reference_dolphin.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)


class ReferenceDolphinBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.work = self.root / "source"
        self.work.mkdir()
        git(self.work, "init", "-q")
        git(self.work, "config", "user.name", "Synthetic fixture")
        git(self.work, "config", "user.email", "fixture@example.invalid")
        (self.work / "upstream.txt").write_text("original\n")
        (self.work / ".gitignore").write_text("ignored/\n")
        git(self.work, "add", ".")
        git(self.work, "commit", "-qm", "synthetic baseline")
        (self.work / "upstream.txt").write_text("reviewed patch\n")
        self.patch_dir = self.root / "patches"
        self.patch_dir.mkdir()
        self.patch = self.patch_dir / "0001-reviewed.patch"
        self.patch.write_bytes(git(self.work, "diff", "--binary"))
        git(self.work, "checkout", "--", "upstream.txt")
        self.overlay = self.root / "overlay"
        (self.overlay / "PowerPC").mkdir(parents=True)
        (self.overlay / "PowerPC/Observer.cpp").write_text("synthetic observer\n")

    def prepare(self):
        git(self.work, "apply", "--index", str(self.patch))
        markers = self.work / ".mwrc"
        markers.mkdir()
        (markers / self.patch.name).write_text(BUILD.sha256(self.patch) + "\n")
        BUILD.copy_overlay(self.overlay, self.work / "Source/Core/Core")

    def verify(self, complete=True):
        return BUILD.verify_source_composition(self.work, [self.patch], self.overlay,
                                               require_complete=complete)

    def test_clean_prefix_and_complete_source_are_reconstructable_without_mutation(self):
        self.assertEqual(self.verify(False), 0)
        self.prepare()
        before = git(self.work, "status", "--porcelain")
        self.assertEqual(self.verify(), 1)
        self.assertEqual(self.verify(), 1)
        self.assertEqual(git(self.work, "status", "--porcelain"), before)

    def test_markers_cannot_mask_unstaged_or_staged_upstream_changes(self):
        self.prepare()
        upstream = self.work / "upstream.txt"
        upstream.write_text("unreviewed\n")
        with self.assertRaisesRegex(SystemExit, "worktree edits"):
            self.verify()
        git(self.work, "add", "upstream.txt")
        with self.assertRaisesRegex(SystemExit, "staged edits"):
            self.verify()
        self.assertEqual(upstream.read_text(), "unreviewed\n")

    def test_extra_untracked_and_ignored_files_are_preserved_and_rejected(self):
        self.prepare()
        for name in ("untracked file\n.cpp", "ignored/cache.cpp"):
            target = self.work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text("unexpected\n")
            with self.assertRaisesRegex(SystemExit, "untracked build-source edit"):
                self.verify()
            self.assertEqual(target.read_text(), "unexpected\n")
            target.unlink()

    def test_overlay_drift_is_rejected_before_copy_and_missing_overlay_is_incomplete(self):
        self.prepare()
        target = self.work / "Source/Core/Core/PowerPC/Observer.cpp"
        target.write_text("unreviewed observer\n")
        with self.assertRaisesRegex(SystemExit, "overlay differs"):
            self.verify()
        with self.assertRaisesRegex(SystemExit, "overlay differs"):
            BUILD.copy_overlay(self.overlay, self.work / "Source/Core/Core")
        self.assertEqual(target.read_text(), "unreviewed observer\n")
        target.unlink()
        with self.assertRaisesRegex(SystemExit, "overlay is missing"):
            self.verify()

    def test_dirty_submodule_rejected(self):
        # Pin an independent tiny repository as a submodule, then dirty it.
        child = self.root / "child"
        subprocess.run(["git", "clone", "-q", str(self.work), str(child)], check=True)
        git(self.work, "-c", "protocol.file.allow=always", "submodule", "add", str(child), "child")
        git(self.work, "commit", "-qam", "pin synthetic submodule")
        self.prepare()
        self.assertEqual(self.verify(), 1)
        (self.work / "child/untracked.txt").write_text("unexplained\n")
        with self.assertRaisesRegex(SystemExit, "worktree edits"):
            self.verify()

    def test_pristine_source_cannot_be_reused_as_mutable_work_or_build_directory(self):
        for work, build in ((self.work, self.root / "build"),
                            (self.work / "nested", self.root / "build"),
                            (self.root / "mutable", self.work / "build")):
            with self.subTest(work=work, build=build), mock.patch.object(BUILD, "run") as run:
                with self.assertRaisesRegex(SystemExit, "pinned source checkout"):
                    BUILD.main(["--source-dir", str(self.work), "--work-dir", str(work),
                                "--build-dir", str(build)])
                run.assert_not_called()
        self.assertEqual(git(self.work, "status", "--porcelain"), b"")

    def test_dirty_source_rejected_before_submodule_update_or_cmake(self):
        self.prepare()
        # A separate pristine source checkout is still required by main.
        source = self.root / "pristine"
        subprocess.run(["git", "clone", "-q", str(self.work), str(source)], check=True)
        (self.work / "upstream.txt").write_text("unreviewed\n")
        with mock.patch.object(BUILD, "PINNED_COMMIT", git(source, "rev-parse", "HEAD").decode().strip()), \
             mock.patch.object(BUILD, "PATCH_DIR", self.patch_dir), \
             mock.patch.object(BUILD, "SOURCE_OVERLAY", self.root / "source-overlay"), \
             mock.patch.object(BUILD, "run") as run:
            with self.assertRaisesRegex(SystemExit, "worktree edits"):
                BUILD.main(["--source-dir", str(source), "--work-dir", str(self.work),
                            "--build-dir", str(self.root / "build")])
            run.assert_not_called()

    def test_repeated_receipt_publication_is_idempotent_and_collision_preserves_receipt(self):
        manifest = self.root / "receipt.json"
        archives = self.root / "archives"
        value = {"binary_sha256": "a" * 64, "source_revision": "synthetic"}
        archive = BUILD.publish_build_receipt(value, manifest, archives, self.overlay, self.patch_dir)
        original = manifest.read_bytes()
        self.assertEqual(json.loads(original)["provenance_archive"], str(archive))
        self.assertEqual((archive / manifest.name).read_bytes(), original)
        BUILD.publish_build_receipt(value, manifest, archives, self.overlay, self.patch_dir)
        self.assertEqual(manifest.read_bytes(), original)
        value["source_revision"] = "different"
        with self.assertRaisesRegex(SystemExit, "provenance collision"):
            BUILD.publish_build_receipt(value, manifest, archives, self.overlay, self.patch_dir)
        self.assertEqual(manifest.read_bytes(), original)
        self.assertEqual((archive / manifest.name).read_bytes(), original)

    def test_archive_source_collision_is_preflighted_before_creating_other_files(self):
        manifest = self.root / "receipt.json"
        archives = self.root / "archives"
        archive = archives / ("b" * 64)
        (archive / "source/PowerPC").mkdir(parents=True)
        collision = archive / "source/PowerPC/Observer.cpp"
        collision.write_text("existing evidence\n")
        manifest.write_text("previous valid receipt\n")
        with self.assertRaisesRegex(SystemExit, "provenance collision"):
            BUILD.publish_build_receipt({"binary_sha256": "b" * 64}, manifest, archives,
                                        self.overlay, self.patch_dir)
        self.assertEqual(manifest.read_text(), "previous valid receipt\n")
        self.assertEqual(collision.read_text(), "existing evidence\n")
        self.assertFalse((archive / "receipt.json").exists())


if __name__ == "__main__":
    unittest.main()
