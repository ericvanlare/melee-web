# SPDX-License-Identifier: GPL-3.0-or-later
"""Focused native controls for the GPL-3.0-or-later Slippi profile helpers."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


class ProfileControlsTests(unittest.TestCase):
    def test_profile_policy_and_dependency_controls(self):
        work_root = ROOT / "work"
        work_root.mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix="slippi-profile-controls-", dir=work_root))
        executable = output / "profile-controls"
        compiler = shlex.split(os.environ.get("CC", "cc"))
        command = [
            *compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I", str(ROOT / "src/slippi_profile/tests/stubs"),
            "-I", str(ROOT / "src"),
            str(ROOT / "src/slippi_profile/profile.c"),
            str(ROOT / "src/slippi_profile/offscreen_damage.c"),
            str(ROOT / "src/slippi_profile/dynamics_fix.c"),
            str(ROOT / "src/slippi_profile/tests/profile_controls_test.c"),
            "-lm",
            "-o", str(executable),
        ]
        wrapped = [
            os.environ.get("PYTHON", "python3"),
            str(ROOT / "scripts/agent_workspace.py"),
            "run", "--", *command,
        ]
        try:
            subprocess.run(wrapped, cwd=ROOT, check=True)
            subprocess.run([str(executable)], cwd=ROOT, check=True)
        except Exception as error:
            self.fail(f"Profile controls failed: {error}; retained reproducer directory: {output}")
        else:
            shutil.rmtree(output)


if __name__ == "__main__":
    unittest.main()
