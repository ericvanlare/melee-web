"""Run the original HPS scheduler proof when built and owned audio is available."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayAudioStreamTests(unittest.TestCase):
    def test_original_music_id_stream_loop_and_restart(self):
        target = ROOT / "build/browser/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        assets = [ROOT / "assets-local/next-gate" / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin", "sp_end.hps")]
        if not target.is_file() or not nodes or not all(p.is_file() for p in assets):
            self.skipTest("Build fighter/audio stream target and supply optional owned HPS/SSM/SEM/coefficient inputs")
        result = subprocess.run([str(nodes[0]), str(target), *map(str, assets)],
                                text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("HPS payloads58 revisited8"), 2)
        self.assertIn("Original HPS three-slot scheduler, native PCM loop, and restart passed", result.stdout)

    def test_original_music_registry_switches_authored_hps_files(self):
        target = ROOT / "build/browser/gameplay_audio_stream_trace.js"
        nodes = list((ROOT / ".deps/emsdk/node").glob("*/bin/node"))
        next_gate = ROOT / "assets-local/next-gate"
        prize = ROOT / "assets-local/results-mario"
        assets = [next_gate / name for name in
                  ("main.ssm", "mario.ssm", "smash2.sem", "dsp_coef.bin")]
        info = [prize / ("s_info%d.hps" % index) for index in (1, 2, 3)]
        if not target.is_file() or not nodes or not all(p.is_file() for p in assets + info):
            self.skipTest("Build the HPS trace and supply the owned Prize music fixtures")
        result = subprocess.run([str(nodes[0]), str(target), *(str(p) for p in
            assets + [info[0]] + info)], text=True, capture_output=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original HPS registry selected three authored files and preserved source changes", result.stdout)


if __name__ == "__main__":
    unittest.main()
