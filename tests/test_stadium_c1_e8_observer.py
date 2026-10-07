import json
from pathlib import Path
import shutil
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from owned_test_workspace import OwnedWorkspaceTests


class StadiumE8ObserverTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.scratch = cls.new_workspace(ROOT, "stadium-e8-observer-")

    @staticmethod
    def _capture_output(path, output):
        if isinstance(output, bytes):
            path.write_bytes(output)
        else:
            path.write_text(output or "", encoding="utf-8")

    def test_bounded_wrappers_preserve_exact_names_and_delegate(self):
        binary = self.scratch / "stadium-e8-observer"
        compile_command = [
            shutil.which("clang") or "cc",
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ROOT / "src"),
            "-I",
            str(ROOT / "tests"),
            str(ROOT / "tests/stadium_c1_e8_call_observer.c"),
            str(ROOT / "tests/stadium_c1_e8_call_observer_test.c"),
            "-o",
            str(binary),
        ]
        (self.scratch / "compile-command.json").write_text(
            json.dumps(compile_command, indent=2) + "\n", encoding="utf-8")
        try:
            compiled = subprocess.run(
                compile_command, check=False, capture_output=True, text=True,
                timeout=60,
            )
        except subprocess.TimeoutExpired as failure:
            self._capture_output(self.scratch / "compile.stdout", failure.stdout)
            self._capture_output(self.scratch / "compile.stderr", failure.stderr)
            raise
        (self.scratch / "compile.stdout").write_text(
            compiled.stdout, encoding="utf-8")
        (self.scratch / "compile.stderr").write_text(
            compiled.stderr, encoding="utf-8")
        self.assertEqual(compiled.returncode, 0,
                         (compiled.stdout + compiled.stderr)[-6000:])

        runtime_command = [str(binary)]
        (self.scratch / "runtime-command.json").write_text(
            json.dumps(runtime_command, indent=2) + "\n", encoding="utf-8")
        try:
            result = subprocess.run(
                runtime_command, check=False, capture_output=True, text=True,
                timeout=10,
            )
        except subprocess.TimeoutExpired as failure:
            self._capture_output(self.scratch / "runtime.stdout", failure.stdout)
            self._capture_output(self.scratch / "runtime.stderr", failure.stderr)
            raise
        (self.scratch / "runtime.stdout").write_text(
            result.stdout, encoding="utf-8")
        (self.scratch / "runtime.stderr").write_text(
            result.stderr, encoding="utf-8")
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])


if __name__ == "__main__":
    unittest.main()
