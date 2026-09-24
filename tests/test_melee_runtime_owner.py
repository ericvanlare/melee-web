"""Exercise shared player lifecycle without claiming native gameplay evidence."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime

class SharedRuntimeOwnerTests(unittest.TestCase):
    def test_native_command_audio_and_teardown_boundaries(self):
        self.run_owner([])

    def test_explicit_silent_owner_never_opens_audio(self):
        self.run_owner(['--silent'])

    def test_unavailable_persistence_keeps_required_directory(self):
        self.run_owner(['--cache-unavailable'])

    def test_startup_cache_readiness_gates_import_until_first_native_ready_result(self):
        self.run_owner(['--silent', '--startup-cache-delay'])

    def test_optional_native_cache_error_keeps_import_eligible(self):
        self.run_owner(['--silent', '--startup-cache-error'])

    def test_permanently_pending_startup_cache_hits_explicit_readiness_deadline(self):
        self.run_owner(['--silent', '--startup-cache-timeout'],
                       'pending startup cache work fails at the bounded readiness deadline')

    def test_missing_required_cache_readiness_service_fails_explicitly(self):
        self.run_owner(['--silent', '--missing-cache-service'],
                       'missing required cache readiness service fails explicitly')

    def test_invalid_cache_readiness_state_fails_explicitly(self):
        self.run_owner(['--silent', '--invalid-cache-service'],
                       'invalid cache readiness state fails explicitly')

    def test_required_directory_failure_stops_native_startup(self):
        self.run_owner(['--silent', '--mkdir-failure'], 'required directory failure prevents native initialization')

    def run_owner(self, args, expected='repeat launch and reload-only destruction pass'):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/melee_runtime_owner_test.mjs'), *args],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(expected, result.stdout)
