"""Run the bounded post-CSS stall probe controls with the pinned Node runtime."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class A3NetworkStallProbeTests(unittest.TestCase):
    def test_policy_and_owned_tcp_queue_controls(self):
        node = node_runtime(ROOT)
        candidate_tests = Path(__file__).resolve().parent
        result = subprocess.run(
            [str(node), "--test", str(candidate_tests / "a3_network_stall_probe.test.mjs"),
             str(candidate_tests / "net_test_tcp_proxy_stall_queue.test.mjs"),
             str(candidate_tests / "net_test_tcp_proxy_controlled_faults.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("pass 18", result.stdout)
        self.assertIn("fail 0", result.stdout)


if __name__ == "__main__":
    unittest.main()
