"""Run the original SSM bank lifecycle against owned local banks."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class NativeAudioBankTests(unittest.TestCase):
    def test_original_bank_lifecycle_two_worlds(self):
        candidates = [
            ROOT / "build" / directory / "native_audio_banks.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in candidates if path.is_file()]
        if not targets:
            self.skipTest("Build the native_audio_banks fighter target first")
        target = max(targets, key=lambda path: path.stat().st_mtime)

        menu_dir = ROOT / "assets-local" / "native-menus"
        audio_dir = ROOT / "assets-local" / "next-gate"
        required = [
            audio_dir / name
            for name in ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin")
        ] + [
            menu_dir / name
            for name in ("nr_select.ssm", "nr_title.ssm", "nr_name.ssm",
                         "pokemon.ssm", "end.ssm")
        ]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned SSM/SEM/DSP fixtures are unavailable")

        result = subprocess.run(
            [str(node_runtime()), str(target), str(menu_dir), str(audio_dir)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "Original SSM bank startup/menu flag-switch/cancel/PCM/closure "
            "passed in two worlds",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()

