"""Exercise real process locks and fail-closed cleanup using disposable repositories."""
import io
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import workspace_resources as resources
from owned_test_workspace import OwnedWorkspaceTests
import share_toolchain


class ResourceFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="workspace resources ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.environment = patch.dict(os.environ, {
            "MELEE_RESOURCE_STATE": str(self.root / "state"),
            "MELEE_MIN_FREE_GB": "0.001", resources.LEASE_ENV: "",
            "MELEE_KEEP_TEST_ARTIFACTS": "0", "PYTHONPATH": str(ROOT / "scripts"),
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)


@unittest.skipUnless(os.name == "posix", "POSIX host resource coordination")
class LeaseTests(ResourceFixture):
    def worker(self, root, *, crash=False):
        root.mkdir()
        code = """import sys
from workspace_resources import operation
with operation(sys.argv[1], 'fixture worker'):
    print('ready', flush=True)
    sys.stdin.readline()
"""
        child = subprocess.Popen([sys.executable, "-c", code, str(root)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        def stop():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(stop)
        with selectors.DefaultSelector() as selector:
            selector.register(child.stdout, selectors.EVENT_READ)
            self.assertTrue(selector.select(5), "worker did not acquire its lease")
        self.assertEqual(child.stdout.readline().strip(), "ready")
        return child

    def test_two_slots_across_checkouts_and_crash_release(self):
        one = self.worker(self.root / "one")
        two = self.worker(self.root / "two")
        third = self.root / "three"
        third.mkdir()
        with self.assertRaisesRegex(ValueError, "both build slots"):
            with resources.operation(third, "blocked", timeout=0.1):
                self.fail("third operation entered")
        one.kill()
        one.communicate(timeout=5)
        with resources.operation(third, "released", timeout=0.1):
            pass
        two.communicate("done\n", timeout=5)

    def test_checkout_lock_excludes_second_process_and_releases_after_error(self):
        self.worker(self.root / "one")
        with self.assertRaisesRegex(ValueError, "checkout is busy"):
            with resources.operation(self.root / "one", "blocked", timeout=0):
                self.fail("duplicate build entered")
        with self.assertRaisesRegex(RuntimeError, "expected"):
            with resources.operation(self.root, "raises"):
                raise RuntimeError("expected")
        with resources.operation(self.root, "recovered"):
            pass

    def test_nested_process_reuses_live_parent_lease_but_stale_token_does_not(self):
        other = self.worker(self.root / "other")
        with resources.operation(self.root, "parent"):
            token = os.environ[resources.LEASE_ENV]
            command = "from workspace_resources import operation; import sys; " \
                      "\nwith operation(sys.argv[1], 'nested', timeout=0): print('nested-ok')"
            output = subprocess.check_output([sys.executable, "-c", command, str(self.root)], text=True)
            self.assertIn("nested-ok", output)
        with patch.dict(os.environ, {resources.LEASE_ENV: token}):
            with resources.operation(self.root, "not inherited"):
                self.assertNotEqual(os.environ[resources.LEASE_ENV], token)
        other.communicate("done\n", timeout=5)

    def test_disk_floor_and_invalid_configuration_fail_before_body(self):
        with patch.dict(os.environ, {"MELEE_MIN_FREE_GB": "99999"}):
            with self.assertRaisesRegex(ValueError, "free"):
                with resources.operation(self.root, "no space"):
                    self.fail("body ran")
        for value in ("0", "-1", "nan", "inf"):
            with self.subTest(value=value), patch.dict(os.environ, {"MELEE_MIN_FREE_GB": value}):
                with self.assertRaises(ValueError):
                    resources.check_space(self.root)

    def test_redirected_state_does_not_create_files_at_destination(self):
        destination = self.root / "elsewhere"
        destination.mkdir()
        redirect = self.root / "redirect"
        redirect.symlink_to(destination, target_is_directory=True)
        with patch.dict(os.environ, {"MELEE_RESOURCE_STATE": str(redirect / "new-state")}):
            with self.assertRaisesRegex(ValueError, "real directory"):
                resources.state_directory()
        self.assertEqual(list(destination.iterdir()), [])


class RetirementTests(ResourceFixture):
    def setUp(self):
        super().setUp()
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        (self.root / ".gitignore").write_text("build/\n.cache/\n")
        self.build = self.root / "build/browser"
        self.build.mkdir(parents=True)
        for name in ("compiled.o", "library.a", "changed.o", "tracked.o", "unlisted.o", "player.wasm"):
            (self.build / name).write_bytes(b"fixture compiler product")
        subprocess.run(["git", "-C", str(self.root), "add", "-f", "build/browser/tracked.o"], check=True)
        (self.build / ".ninja_log").write_text("# ninja log v5\n" + "".join(
            f"1\t2\t3\t{name}\thash\n" for name in
            ("compiled.o", "library.a", "changed.o", "tracked.o", "player.wasm", "../../outside.o")))
        resources.record_build(self.root, self.build, True)

    def test_cleanup_only_removes_unchanged_journaled_untracked_intermediates(self):
        changed = self.build / "changed.o"
        before = changed.stat()
        changed.write_bytes(changed.read_bytes().replace(b"fixture", b"edited!"))
        self.assertEqual(changed.stat().st_size, before.st_size)
        os.utime(changed, ns=(before.st_atime_ns, before.st_mtime_ns))
        plan = resources.retire_builds(self.root)
        self.assertFalse(plan["applied"])
        self.assertTrue((self.build / "compiled.o").exists())
        with patch.object(resources, "open_files", return_value=set()):
            result = resources.retire_builds(self.root, apply=True)
        self.assertEqual(result["files"], 2)
        self.assertFalse((self.build / "compiled.o").exists())
        self.assertFalse((self.build / "library.a").exists())
        for name in ("changed.o", "tracked.o", "unlisted.o", "player.wasm"):
            self.assertTrue((self.build / name).exists(), name)

    def test_failed_build_and_open_files_keep_products(self):
        with patch.object(resources, "open_files", return_value={str(self.build / "player.wasm")}):
            with self.assertRaisesRegex(ValueError, "still in use"):
                resources.retire_builds(self.root, apply=True)
        resources.record_build(self.root, self.build, False)
        self.assertEqual(resources.retirement_plan(self.root), [])
        self.assertTrue((self.build / "compiled.o").exists())

    def test_symlinks_and_escaping_journal_paths_cannot_delete_outside_files(self):
        outside = self.root / "outside.o"
        outside.write_text("keep")
        (self.build / "compiled.o").unlink()
        (self.build / "compiled.o").symlink_to(outside)
        self.assertNotIn("build/browser/compiled.o", {p["path"] for p in resources.retirement_plan(self.root)})
        journal = self.root / ".cache/workspace/builds.json"
        data = json.loads(journal.read_text())
        data["builds"]["../../escape"] = {"succeeded": True, "products": {}}
        journal.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "invalid build"):
            resources.retirement_plan(self.root)
        self.assertEqual(outside.read_text(), "keep")

    def test_malformed_identity_and_redirected_ninja_log_refuse_cleanup(self):
        journal = self.root / ".cache/workspace/builds.json"
        data = json.loads(journal.read_text())
        data["builds"]["build/browser"]["products"]["build/browser/compiled.o"] = []
        journal.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "invalid build product identity"):
            resources.retire_builds(self.root, apply=True)
        self.assertTrue((self.build / "compiled.o").exists())
        journal.unlink()
        log = self.build / ".ninja_log"
        original = self.root / "other-ninja-log"
        log.rename(original)
        log.symlink_to(original)
        with self.assertRaisesRegex(ValueError, "redirected Ninja"):
            resources.record_build(self.root, self.build, True)
        self.assertFalse(journal.exists())


class ScratchTests(ResourceFixture):
    def run_fixture(self, kind):
        root = self.root
        class Fixture(OwnedWorkspaceTests):
            @classmethod
            def setUpClass(cls):
                cls.work = cls.new_workspace(root, "owned-test-")
                (cls.work / "evidence.txt").write_text("evidence")
                if kind == "setup":
                    raise RuntimeError("setup failed")

            def test_body(self):
                if kind == "failure":
                    self.fail("expected failure")
                if kind == "subtest":
                    with self.subTest(case=1):
                        self.fail("expected subtest failure")

            @classmethod
            def tearDownClass(cls):
                if kind == "teardown":
                    raise RuntimeError("class teardown failed")
        result = unittest.TextTestRunner(stream=io.StringIO()).run(
            unittest.defaultTestLoader.loadTestsFromTestCase(Fixture))
        return Fixture.work, result

    def test_success_removes_only_owned_scratch(self):
        (self.root / "work").mkdir()
        evidence = self.root / "work/recording.mwro"
        evidence.write_text("keep recording")
        directory, result = self.run_fixture("success")
        self.assertTrue(result.wasSuccessful())
        self.assertFalse(directory.exists())
        self.assertEqual(evidence.read_text(), "keep recording")

    def test_failure_setup_failure_subtest_and_class_teardown_preserve_evidence(self):
        for kind in ("failure", "setup", "subtest", "teardown"):
            with self.subTest(kind=kind):
                directory, result = self.run_fixture(kind)
                self.assertFalse(result.wasSuccessful())
                self.assertEqual((directory / "evidence.txt").read_text(), "evidence")

    def test_explicit_retention_keeps_passing_artifacts(self):
        with patch.dict(os.environ, {"MELEE_KEEP_TEST_ARTIFACTS": "1"}):
            directory, result = self.run_fixture("success")
        self.assertTrue(result.wasSuccessful())
        self.assertTrue(directory.exists())


class ToolchainSharingTests(ResourceFixture):
    def setUp(self):
        super().setUp()
        self.source = self.root / "source"
        self.target = self.root / "target"
        self.source.write_bytes(b"same installed binary" * 100)
        self.target.write_bytes(self.source.read_bytes())

    def test_equal_copy_preserves_target_time_and_independent_writes(self):
        stamp = self.target.stat().st_mtime_ns
        with patch.object(share_toolchain, "clone_file", side_effect=shutil.copy2), \
                patch.object(share_toolchain, "attributes", return_value={}):
            self.assertTrue(share_toolchain.share_file(self.source, self.target))
        self.assertEqual(self.target.stat().st_mtime_ns, stamp)
        self.assertNotEqual(self.source.stat().st_ino, self.target.stat().st_ino)
        self.target.write_text("changed target")
        self.assertNotEqual(self.target.read_bytes(), self.source.read_bytes())

    def test_changed_source_or_bad_clone_never_replaces_target(self):
        original = self.target.read_bytes()
        def changed_source(source, destination):
            shutil.copy2(source, destination)
            source.write_text("source changed during clone")
        with patch.object(share_toolchain, "attributes", return_value={}), \
                patch.object(share_toolchain, "clone_file", side_effect=changed_source):
            with self.assertRaisesRegex(ValueError, "changed during sharing"):
                share_toolchain.share_file(self.source, self.target)
        self.assertEqual(self.target.read_bytes(), original)
        self.assertEqual(list(self.root.glob(".share-*.tmp")), [])

    def test_mismatch_and_symlink_are_not_shared(self):
        self.source.write_text("different bytes")
        with patch.object(share_toolchain, "clone_file") as clone:
            self.assertFalse(share_toolchain.share_file(self.source, self.target))
            self.target.unlink()
            self.target.symlink_to(self.source)
            self.assertFalse(share_toolchain.share_file(self.source, self.target))
            clone.assert_not_called()

    def test_incompatible_first_peer_does_not_prevent_verified_sharing(self):
        lock = b'{"fixture": "same pinned tools"}'
        (self.root / "dependencies.lock.json").write_bytes(lock)
        target = self.root / ".deps/emsdk/upstream/bin/clang"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"x" * (1024 * 1024))
        peers = [self.root / "peer-one", self.root / "peer-two"]
        for number, peer in enumerate(peers):
            (peer / ".venv").mkdir(parents=True)
            (peer / "dependencies.lock.json").write_bytes(lock)
            source = peer / target.relative_to(self.root)
            source.parent.mkdir(parents=True)
            source.write_bytes((b"y" if number == 0 else b"x") * (1024 * 1024))
        records = b"".join(b"worktree " + os.fsencode(peer) + b"\0\0" for peer in peers)
        with patch.object(share_toolchain.sys, "platform", "darwin"), \
                patch.object(share_toolchain.subprocess, "check_output", return_value=records), \
                patch.object(share_toolchain, "attributes", return_value={}), \
                patch.object(share_toolchain, "clone_file", side_effect=shutil.copy2):
            result = share_toolchain.share_installed_toolchain(self.root)
        self.assertEqual(result, {"shared_files": 1, "skipped_files": 0})
        self.assertEqual(target.read_bytes(), b"x" * (1024 * 1024))

    @unittest.skipUnless(sys.platform == "darwin", "real APFS clone")
    def test_real_apfs_clone_preserves_bytes_and_copy_on_write(self):
        self.assertTrue(share_toolchain.share_file(self.source, self.target))
        self.target.write_text("independent modification")
        self.assertNotEqual(self.target.read_bytes(), self.source.read_bytes())


if __name__ == "__main__":
    unittest.main()
