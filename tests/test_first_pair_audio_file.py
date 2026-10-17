"""Node-only synthetic checks for the private first-pair PCM/file guard."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from first_pair_test_support import pinned_node_or_skip


class FirstPairAudioFileGuardTests(unittest.TestCase):
    def test_synthetic_audio_journal_and_file_poison_controls(self):
        node = pinned_node_or_skip(self, ROOT)
        result = subprocess.run(
            [str(node), str(ROOT / "tests/first_pair_audio_file_guard_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        self.assertIn("postcallback provenance cannot arm", output)
        self.assertIn("Zero-audio returned provenance and later actual audio schedule PASS", output)

    def test_first_draw_host_owner_point_read_controls(self):
        node = pinned_node_or_skip(self, ROOT)
        result = subprocess.run([str(node), str(ROOT / "tests/first_draw_host_owners_test.mjs")],
                                cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Actual first-draw host owner point-read controls PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
