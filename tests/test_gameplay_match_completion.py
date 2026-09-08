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
    def test_ground_and_air_fireball(self):
        self.run_trace('gameplay_article_trace','ground and air PAD_BUTTON_B Article paths passed in two worlds')

    def test_controller_port_does_not_change_costume_color(self):
        binary=ROOT/'build/browser/gameplay_player_context_trace.js'
        if not binary.is_file():self.skipTest('Built original player context trace required')
        result=subprocess.run([str(node_runtime()),str(binary)],cwd=ROOT,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
