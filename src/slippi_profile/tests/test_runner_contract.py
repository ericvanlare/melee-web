# SPDX-License-Identifier: GPL-3.0-or-later
"""Run behavior controls for the live no-draw browser runner contract."""

from pathlib import Path
import os
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[3]


class SlippiSynctestRunnerContractTests(unittest.TestCase):
    def test_draw_frame_cleanup_and_owned_process_controls(self):
        command = [
            sys.executable, str(ROOT / "scripts/agent_workspace.py"), "run", "--",
            "node", "--test", str(ROOT / "src/slippi_profile/tests/test_runner_contract.mjs"),
        ]
        subprocess.run(command, cwd=ROOT, check=True,
                       env=dict(os.environ, FORCE_COLOR="0"))


if __name__ == "__main__":
    unittest.main()
