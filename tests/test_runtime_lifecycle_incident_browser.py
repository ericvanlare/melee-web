"""Syntax and CLI contract for the bounded lifecycle browser detector."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeLifecycleIncidentBrowserTests(unittest.TestCase):
    def test_node_syntax_and_help_are_local_only(self):
        script = ROOT / "tests" / "runtime_lifecycle_incident_browser_test.mjs"
        node = shutil.which("node")
        self.assertIsNotNone(node, "node is required for the local CLI contract")
        checked = subprocess.run([node, "--check", str(script)],
                                 capture_output=True, text=True, timeout=30)
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
        help_result = subprocess.run([node, str(script), "--help"],
                                     capture_output=True, text=True, timeout=30)
        self.assertEqual(help_result.returncode, 0, help_result.stdout + help_result.stderr)
        self.assertIn("--site AUDITED_AUDIO_PLAYER", help_result.stdout)
        self.assertIn("--manifest MANIFEST", help_result.stdout)
        self.assertIn("--disc OWNED_ISO", help_result.stdout)
        self.assertIn("--out FRESH_EVIDENCE_DIR", help_result.stdout)
        self.assertIn("--synthetic", help_result.stdout)
        self.assertIn("--synthetic-hidden-hold", help_result.stdout)
        self.assertIn("--startup-only", help_result.stdout)

    def test_lifecycle_modes_are_explicitly_gated(self):
        source = (ROOT / "tests" / "runtime_lifecycle_incident_browser_test.mjs").read_text()
        self.assertNotIn("Emulation.setDocumentVisibilityState", source)
        self.assertIn("Page.setWebLifecycleState", source)
        self.assertIn("genuine_lifecycle_events_unavailable", source)
        self.assertIn("game_imported: false", source)
        self.assertIn("can_import_timeout", source)
        self.assertIn("native_hooks_unavailable", source)
        self.assertIn("no browser lifecycle or user-root-cause claim", source)

    def test_synthetic_hidden_hold_is_bounded_and_explicit(self):
        source = (ROOT / "tests" / "runtime_lifecycle_incident_browser_test.mjs").read_text()
        self.assertIn("synthetic-hidden-hold", source)
        self.assertIn("HIDDEN_DWELL_MS = 350", source)
        self.assertIn("synthetic_main_loop_callback_unavailable", source)
        self.assertIn("synthetic_visibility_override_unavailable", source)
        self.assertIn("nativeRafCallbacks", source)
        self.assertIn("native_sample_count", source)
        self.assertIn("native_source_steps", source)
        self.assertIn("if (fixture.samples.length >= 128) fixture.samples.shift()", source)
        self.assertIn("trace_sequence", source)
        self.assertIn("synthetic_hidden_native_progress", source)
        self.assertIn("synthetic_simulation_debt_missing", source)
        self.assertIn("manual-pause", source)
        self.assertIn("startup-only", source)
        self.assertIn("STARTUP_ONLY_TIMEOUT_MS = 5000", source)
        self.assertNotIn("graphics_ready_timeout", source)
        self.assertNotIn("Emulation.setDocumentVisibilityState", source)


if __name__ == "__main__":
    unittest.main()
