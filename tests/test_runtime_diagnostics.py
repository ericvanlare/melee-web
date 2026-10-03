"""Focused checks for the bounded browser runtime diagnostics core."""

from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "web" / "runtime-diagnostics.mjs"
JS_TEST = ROOT / "tests" / "runtime_diagnostics_test.mjs"


class RuntimeDiagnosticsTest(unittest.TestCase):
    def test_module_has_positional_api_and_explicit_storage_boundary(self):
        source = MODULE.read_text(encoding="utf-8")
        self.assertIn("export function createRuntimeDiagnostics", source)
        self.assertIn("function observeNative(", source)
        self.assertIn("function trigger(", source)
        self.assertIn("function lifecycle(", source)
        self.assertIn("function audio(", source)
        self.assertIn("function setActive(", source)
        self.assertIn("async function checkpoint(", source)
        self.assertIn("function exportReports(", source)
        self.assertIn("melee-web-runtime-diagnostics", source)
        self.assertIn("objectStoreNames.contains(DIAGNOSTICS_STORE_NAME)", source)
        self.assertNotIn("fetch(", source)
        self.assertNotIn("sendBeacon(", source)
        self.assertNotIn("console.", source)

    def test_node_boundaries_and_privacy(self):
        checked = subprocess.run(
            ["node", "--check", str(MODULE)],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(checked.returncode, 0, checked.stderr)
        exercised = subprocess.run(
            ["node", str(JS_TEST)],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(exercised.returncode, 0, exercised.stdout + exercised.stderr)
        self.assertIn("privacy", exercised.stdout)


if __name__ == "__main__":
    unittest.main()
