"""Exercise the real public player shell around a mocked runtime startup."""

from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class PublicPlayerStartupTests(unittest.TestCase):
    def test_public_shell_uses_shared_owner_and_surfaces_startup_failure(self):
        if not (ROOT / ".deps/emsdk/.emscripten").is_file():
            self.skipTest("Pinned Node runtime unavailable before SDK bootstrap")
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/public_player_startup_test.mjs")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("shared owner startup", result.stdout)


if __name__ == "__main__":
    unittest.main()
