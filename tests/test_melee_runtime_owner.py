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

    def test_missing_webgpu_adapter_stops_before_audio_and_native_module_load(self):
        self.run_owner(['--no-webgpu-adapter'],
                       'missing WebGPU adapter stops before native download or audio setup')

    def test_concurrent_adapter_requests_reserve_one_document_owner(self):
        self.run_owner(['--adapter-race'],
                       'deferred concurrent adapter requests reserve one document owner')

    def test_null_and_rejected_adapter_preflight_allow_safe_retry(self):
        self.run_owner(['--adapter-retry'],
                       'null and rejected adapter preflight releases only its reservation and allows retry')

    def test_timed_out_adapter_result_cannot_start_after_safe_retry(self):
        self.run_owner(['--adapter-timeout-late'],
                       'timed-out adapter attempts release safely; late adapter results cannot start a runtime')

    def test_startup_deadline_includes_adapter_preflight(self):
        self.run_owner(['--adapter-deadline-span'],
                       'one bounded startup deadline covers adapter preflight and native startup')

    def test_explicit_silent_owner_never_opens_audio(self):
        self.run_owner(['--silent'])

    def test_known_host_diagnostics_wiring_is_inactive_and_opt_out_safe(self):
        self.run_owner(['--diagnostics-known-host'],
                       'known-host diagnostics identity, scalar incident wiring, inactive delivery delay/cancel')

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
