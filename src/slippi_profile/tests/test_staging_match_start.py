# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real page agreement module with bounded modeled transports."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]


class StagingMatchStartTests(unittest.TestCase):
    def test_source_context_agreement_and_socket_handoff(self):
        result = subprocess.run(
            ["node", "--test",
             str(ROOT / "src/slippi_profile/tests/test_staging_match_start.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
