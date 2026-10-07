"""Keep A2 lockstep and browser-route JavaScript contracts in unittest discovery."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class NetLockstepContractTests(unittest.TestCase):
    def test_observer_protocol_and_route_contracts(self):
        result = subprocess.run([
            str(node_runtime()), '--test',
            str(ROOT / 'tests/net_lockstep_observers.test.mjs'),
            str(ROOT / 'tests/net_source_accounting.test.mjs'),
            str(ROOT / 'tests/net_lockstep_core.test.mjs'),
            str(ROOT / 'tests/net_lockstep_protocol.test.mjs'),
            str(ROOT / 'tests/net_lockstep_transport.test.mjs'),
            str(ROOT / 'tests/net_room_relay_worker.test.mjs'),
            str(ROOT / 'tests/net_lockstep_browser_handshake_probe.test.mjs'),
            str(ROOT / 'tests/net_determinism_browser.test.mjs'),
        ], cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
