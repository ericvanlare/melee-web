import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class PublicSceneAssetTests(unittest.TestCase):
    def test_audio_free_native_scene_scope(self):
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/public_scene_assets_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Public scene disc scope maps exact files", result.stdout)


if __name__ == "__main__":
    unittest.main()
