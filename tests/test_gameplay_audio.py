"""Optional built original SEM/synth/AX voice proof, with source-derived ITD."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from check_gameplay import node_runtime
class GameplayAudioTests(unittest.TestCase):
    def run_audio_trace(self, name, marker):
        target=ROOT/"build/browser"/name
        assets=[ROOT/"assets-local/next-gate"/name for name in ("main.ssm","mario.ssm","smash2.sem","dsp_coef.bin")]
        if not target.is_file() or not all(p.is_file() for p in assets):
            self.skipTest("Build gameplay_audio_trace and provide local SSM/SEM/DSP coefficient fixtures")
        result=subprocess.run([str(node_runtime()),str(target),*map(str,assets)],cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn(marker,result.stdout)
    def test_original_voice_pcm(self):
        self.run_audio_trace("gameplay_audio_trace.js", "partition invariance and scoped restart")

    def test_character_banks_preserve_overlapping_source_sample_ids(self):
        target=ROOT/"build/browser-release/gameplay_audio_trace.js"
        common=ROOT/"assets-local/native-menus"
        fighters=ROOT/"assets-local/next-gate"
        assets=[common/name for name in ("main.ssm","mario.ssm","smash2.sem","dsp_coef.bin")]
        extras=[fighters/name for name in ("kirby.ssm","ice.ssm")]
        if not target.is_file() or not all(p.is_file() for p in [*assets,*extras]):
            self.skipTest("Build gameplay_audio_trace and provide the owned Kirby/Ice audio banks")
        result=subprocess.run([str(node_runtime()),str(target),*map(str,assets),*map(str,extras)],
                              cwd=ROOT,capture_output=True,text=True,timeout=60)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("Overlapping SSM sample IDs retained",result.stdout)
    def test_original_effect_callbacks_and_transport(self):
        self.run_audio_trace("gameplay_audio_fx_trace.js", "three-buffer latency/restart passed")
if __name__=="__main__":unittest.main()
