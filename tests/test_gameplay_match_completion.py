"""Optional owned-asset integration checks for original match completion."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime
class MatchCompletion(unittest.TestCase):
    def run_trace(self,target,expected):
        assets=ROOT/'assets-local/next-gate'
        names=('PlCo.dat','PlMr.dat','PlMrNr.dat','PlMrAJ.dat','GrNLa.dat','ItCo.usd','EfMrData.dat','EfCoData.dat','PdPm.dat','sislib_font.bin')
        binary=ROOT/'build/browser'/(target+'.js')
        if not binary.is_file() or not all((assets/name).is_file() for name in names):
            self.skipTest('Optional built source trace and owned runtime assets required')
        result=subprocess.run([str(node_runtime()),str(binary),str(assets)],cwd=ROOT,capture_output=True,text=True,timeout=90)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn(expected,result.stdout)
    def test_four_stock_elimination_respawn_and_winner(self):
        self.run_trace('gameplay_stock_trace','four-stock elimination, three respawns and winner passed in two worlds')

    def test_all_mario_costumes_repeat_source_lifecycle(self):
        assets=ROOT/'assets-local/next-gate'
        costumes=ROOT/'assets-local/native-menus'
        binary=ROOT/'build/browser/gameplay_stock_trace.js'
        names=('PlCo.dat','PlMr.dat','PlMrNr.dat','PlMrAJ.dat','GrNLa.dat','ItCo.usd','EfMrData.dat','EfCoData.dat','PdPm.dat','sislib_font.bin')
        extra=('PlMrYe.dat','PlMrBk.dat','PlMrBu.dat','PlMrGr.dat')
        if not binary.is_file() or not all((assets/name).is_file() for name in names) or not all((costumes/name).is_file() for name in extra):
            self.skipTest('Built stock trace and owned Mario costume archives required')
        result=subprocess.run([str(node_runtime()),str(binary),str(assets),str(costumes)],cwd=ROOT,capture_output=True,text=True,timeout=180)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('Five Mario costume sources hydrated, selected, respawned and torn down twice',result.stdout)
    def test_ground_and_air_fireball(self):
        self.run_trace('gameplay_article_trace','ground and air PAD_BUTTON_B Article paths passed in two worlds')

    def run_edge_trace(self, *arguments):
        assets=ROOT/'assets-local/next-gate'
        binary=ROOT/'build/browser'/'gameplay_edge_trace.js'
        names=('PlCo.dat','PlMr.dat','PlMrNr.dat','PlMrAJ.dat','GrNLa.dat','ItCo.usd','EfMrData.dat','EfCoData.dat','PdPm.dat','sislib_font.bin')
        if not binary.is_file() or not all((assets/name).is_file() for name in names):
            self.skipTest('Optional built full-stage edge trace and owned runtime assets required')
        result=subprocess.run([str(node_runtime()),str(binary),str(assets),*arguments],cwd=ROOT,capture_output=True,text=True,timeout=90)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        return result

    def test_full_stage_edge_phase_at_plain_reference_position(self):
        result=self.run_edge_trace('phase','59.153053283691406')
        self.assertIn('Original initial-spawn raw-stick phase trace captured',result.stdout)

    def test_full_stage_edge_post_respawn_stock_trace(self):
        result=self.run_edge_trace()
        self.assertIn('Original post-respawn raw-stick edge trace captured',result.stdout)

    def test_controller_port_and_costume_are_distinct(self):
        binary=ROOT/'build/browser/gameplay_player_context_trace.js'
        if not binary.is_file():self.skipTest('Built original player context trace required')
        result=subprocess.run([str(node_runtime()),str(binary)],cwd=ROOT,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('fighter identity mapping',result.stdout)
