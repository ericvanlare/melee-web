"""Functional VM and unsupported-native checks for the ownership diagnostic."""

import json
from pathlib import Path
import shutil
import subprocess
import unittest

from owned_test_workspace import OwnedWorkspaceTests

ROOT = Path(__file__).resolve().parents[1]
AURORA = ROOT / ".deps" / "aurora"
PATCH = ROOT / "patches" / "aurora-browser.patch"


def reviewed_new_file(relative_path):
    """Extract reviewed bytes; require any installed dependency to match them."""
    patch_text = PATCH.read_text(encoding="utf-8")
    marker = f"diff --git a/{relative_path} b/{relative_path}"
    start = patch_text.index(marker)
    end = patch_text.find("\ndiff --git ", start + len(marker))
    if end < 0:
        end = len(patch_text)
    body, in_body = [], False
    for line in patch_text[start:end].splitlines():
        if line.startswith("+++ b/"):
            in_body = True
        elif in_body and line.startswith("+") and not line.startswith("+++"):
            body.append(line[1:])
        elif in_body and line.startswith(" "):
            body.append(line[1:])
    reviewed = "\n".join(body) + "\n"
    installed = AURORA / relative_path
    if installed.is_file() and installed.read_text(encoding="utf-8") != reviewed:
        raise AssertionError(f"installed Aurora file differs from reviewed patch: {installed}")
    return reviewed


class AuroraFutureOwnerAccessorTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "aurora-future-owner-controls-")

    def test_accessor_vm_controls(self):
        node = shutil.which("node")
        if node is None:
            self.skipTest("Node.js unavailable")
        library = self.workspace / "aurora_browser_future_owner_accessor_library.js"
        library.write_text(reviewed_new_file("lib/gfx/aurora_browser_future_owner_accessor_library.js"))
        completed = subprocess.run(
            [node, str(ROOT / "tests/aurora_future_owner_accessor_vm.mjs"), "--library", str(library)],
            cwd=ROOT, capture_output=True, text=True, timeout=20, check=False)
        (self.workspace / "vm.stdout.json").write_text(completed.stdout)
        (self.workspace / "vm.stderr.txt").write_text(completed.stderr)
        self.assertEqual(completed.returncode, 0, completed.stderr + completed.stdout)
        result = json.loads(completed.stdout.splitlines()[-1])
        self.assertEqual(result.get("status"), "pass")
        self.assertFalse(result.get("browser"))
        self.assertFalse(result.get("webgpuDevice"))
        self.assertEqual(len(result.get("cases", [])), 6)

    def test_native_refuses_without_writing_output(self):
        compiler = shutil.which("c++")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
        include = self.workspace / "include/aurora"
        include.mkdir(parents=True)
        (include / "aurora_browser_future_owner_accessor.h").write_text(
            reviewed_new_file("include/aurora/aurora_browser_future_owner_accessor.h"))
        source = self.workspace / "accessor.cpp"
        source.write_text(reviewed_new_file("lib/gfx/aurora_browser_future_owner_accessor.cpp"))
        driver = self.workspace / "native.cpp"
        driver.write_text("""#include <aurora/aurora_browser_future_owner_accessor.h>
#include <cstdio>
#include <cstring>
int main() {
  alignas(8) unsigned char output[96], before[96];
  std::memset(output, 0xa5, sizeof(output));
  std::memcpy(before, output, sizeof(output));
  int capability = aurora_browser_future_owner_capability();
  int summary = aurora_browser_future_owner_summary(output, sizeof(output));
  int row = aurora_browser_future_owner_row(output, sizeof(output), 0);
  int dispose = aurora_browser_future_owner_dispose();
  bool unchanged = std::memcmp(output, before, sizeof(output)) == 0;
  bool passed = capability == -8 && summary == -8 && row == -8 && dispose == -8
    && unchanged && aurora_browser_future_owner_row_count() == 0
    && aurora_browser_future_owner_summary_bytes() == 96
    && aurora_browser_future_owner_row_bytes() == 32;
  std::puts(passed ? "passed-native-unsupported-zero-write" : "failed");
  return passed ? 0 : 1;
}
""")
        binary = self.workspace / "native-control"
        compiled = subprocess.run([compiler, "-std=c++17", "-I", str(include.parent),
                                   str(source), str(driver), "-o", str(binary)],
                                  capture_output=True, text=True, timeout=30, check=False)
        (self.workspace / "native-compile.log").write_text(compiled.stdout + compiled.stderr)
        self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
        observed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=5, check=False)
        (self.workspace / "native-output.log").write_text(observed.stdout + observed.stderr)
        self.assertEqual(observed.returncode, 0, observed.stdout + observed.stderr)
        self.assertEqual(observed.stdout.strip(), "passed-native-unsupported-zero-write")


if __name__ == "__main__":
    unittest.main()
