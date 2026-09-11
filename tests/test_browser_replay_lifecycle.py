"""Run real page event handlers with controlled asynchronous host boundaries."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from check_gameplay import node_runtime


class BrowserReplayLifecycleTests(unittest.TestCase):
    def test_async_start_failure_and_manual_pause_boundaries(self):
        result=subprocess.run([str(node_runtime()),str(ROOT/'tests/browser_replay_lifecycle_test.mjs')],
                              capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('duplicate start, failed teardown and manual timing pause rejected',result.stdout)
