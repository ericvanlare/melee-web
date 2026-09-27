"""Owned test scratch: clean successful classes, retain failures and explicit evidence."""
import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from workspace_resources import operation


class OwnedWorkspaceTests(unittest.TestCase):
    @classmethod
    def new_workspace(cls, root, prefix):
        root = Path(root).resolve()
        work = root / "work"
        work.mkdir(exist_ok=True)
        if work.is_symlink() or work.resolve() != work:
            raise ValueError("test work directory must not be redirected")
        lease = operation(root, cls.__name__)
        lease.__enter__()
        cls.addClassCleanup(lease.__exit__, None, None, None)
        directory = Path(tempfile.mkdtemp(prefix=prefix, dir=work))
        inode = directory.stat().st_ino
        if not cls.__dict__.get("_owned_workspace_active", False):
            cls._owned_workspace_active = True
            cls._workspace_tests_started = False
            cls._workspace_failed = False
            cls._workspace_result = None
            cls.addClassCleanup(setattr, cls, "_owned_workspace_active", False)

        def finish():
            keep = (os.environ.get("MELEE_KEEP_TEST_ARTIFACTS") == "1"
                    or not cls._workspace_tests_started or cls._workspace_failed)
            if cls._workspace_result is not None:
                result = cls._workspace_result
                # unittest records tearDownClass errors before class cleanups.
                keep |= cls._workspace_last_counts != (len(result.failures), len(result.errors),
                                                        len(result.unexpectedSuccesses))
            if keep:
                print(f"Retained test evidence: {directory}", file=sys.stderr)
                return
            if directory.is_symlink() or directory.resolve() != directory or directory.stat().st_ino != inode:
                raise ValueError("test workspace changed ownership; refusing cleanup")
            shutil.rmtree(directory)

        cls.addClassCleanup(finish)
        return directory

    def run(self, result=None):
        if result is None:
            result = self.defaultTestResult()
        self.__class__._workspace_tests_started = True
        self.__class__._workspace_result = result
        before = (len(result.failures), len(result.errors), len(result.unexpectedSuccesses))
        try:
            return super().run(result)
        except BaseException:
            self.__class__._workspace_failed = True
            raise
        finally:
            after = (len(result.failures), len(result.errors), len(result.unexpectedSuccesses))
            self.__class__._workspace_last_counts = after
            if after != before:
                self.__class__._workspace_failed = True
