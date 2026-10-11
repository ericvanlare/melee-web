"""Keep external relay admission separate from the default local Worker path."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from owned_test_workspace import OwnedWorkspaceTests


NODE_CHECK = r"""
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';

const [helperPath] = process.argv.slice(1);
const {resolveRoomRelayMode} = await import(pathToFileURL(helperPath).href);
const relayUrl = 'wss://relay.staging.example.invalid';
const clientOrigin = 'http://127.0.0.1:8787';
const preflight = {
  schema: 'melee-web-a3-public-relay-preflight-v1',
  version: 1,
  scope: 'read-only-public-staging-worker-binding',
  observed_at: '2026-10-11T20:00:00Z',
  runtime: {relay_url: relayUrl, relay_origin: relayUrl},
  source: {
    worker_name: 'melee-web-room-relay-staging-288',
    environment: 'staging', deployment_id: 'deployment-version-123',
    script_sha256: 'a'.repeat(64), config_sha256: 'b'.repeat(64),
  },
  client: {origin: clientOrigin, allowed_origins: [clientOrigin]},
  evidence: {
    deployment_receipt: {file: 'deployment-receipt.json', read_only: true, sha256: 'c'.repeat(64)},
    origin_policy_receipt: {file: 'origin-policy-receipt.json', read_only: true, sha256: 'd'.repeat(64)},
  },
};

assert.deepEqual(resolveRoomRelayMode({}), {mode: 'local'},
  'omitting external flags preserves the owned local Worker mode');
const mode = resolveRoomRelayMode({relayUrl, preflight, clientOrigin});
assert.equal(mode.mode, 'external');
assert.equal(mode.endpoint.url, relayUrl);
assert.equal(mode.endpoint.origin, relayUrl);
assert.equal(mode.source.deployment_id, 'deployment-version-123');
assert.equal(mode.client_origin, clientOrigin);
assert.equal(mode.validation_scope.includes('no live provider query'), true);

assert.throws(() => resolveRoomRelayMode({relayUrl, clientOrigin}), /requires both/,
  'external endpoint without a documented preflight refuses');
assert.throws(() => resolveRoomRelayMode({preflight, clientOrigin}), /requires both/,
  'preflight without an external endpoint refuses');
assert.throws(() => resolveRoomRelayMode({preflight: null, clientOrigin}), /requires both/,
  'an explicitly supplied null preflight is not the absent local-default case');
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: null, clientOrigin}), /unsupported or missing fields/,
  'an explicit null preflight with an endpoint refuses instead of falling back');
assert.throws(() => resolveRoomRelayMode({relayUrl: 'ws://relay.example.invalid', preflight, clientOrigin}),
  /canonical wss/);
assert.throws(() => resolveRoomRelayMode({relayUrl: 'wss://user:secret@relay.staging.example.invalid', preflight, clientOrigin}),
  /credentials/);
assert.throws(() => resolveRoomRelayMode({relayUrl: relayUrl + '/path', preflight, clientOrigin}),
  /base URL/);

const wrongEndpoint = structuredClone(preflight);
wrongEndpoint.runtime.relay_url = 'wss://other.example.invalid';
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: wrongEndpoint, clientOrigin}), /does not match/);
const wrongClient = structuredClone(preflight);
wrongClient.client.origin = 'https://webmelee-staging.pages.dev';
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: wrongClient, clientOrigin}), /does not match/);
const disallowedClient = structuredClone(preflight);
disallowedClient.client.allowed_origins = ['https://webmelee-staging.pages.dev'];
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: disallowedClient, clientOrigin}), /does not establish/);
const wrongEnvironment = structuredClone(preflight);
wrongEnvironment.source.environment = 'production';
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: wrongEnvironment, clientOrigin}), /staging environment/);
const missingSourceHash = structuredClone(preflight);
missingSourceHash.source.script_sha256 = null;
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: missingSourceHash, clientOrigin}), /source and config SHA-256/);
const missingReceipt = structuredClone(preflight);
missingReceipt.evidence.origin_policy_receipt.read_only = false;
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: missingReceipt, clientOrigin}), /read-only evidence/);
const unknownField = structuredClone(preflight);
unknownField.operator_guess = true;
assert.throws(() => resolveRoomRelayMode({relayUrl, preflight: unknownField, clientOrigin}), /unsupported or missing fields/);

console.log('public relay preflight default, binding and refusal controls pass');
"""


class A3PublicRelayPreflightTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.work = cls.new_workspace(ROOT, "public-relay-cli-")

    def test_actual_cli_distinguishes_absent_and_null_preflight(self):
        null_preflight = self.work / "null-preflight.json"
        null_preflight.write_text("null\n")
        output = self.work / "browser-output-must-not-exist"
        base = [str(node_runtime(ROOT)), str(ROOT / "scripts/a3_local_room_keyboard_browser.mjs"),
                "--url", "http://127.0.0.1:8787/runtime.html",
                "--disc", str(self.work / "nonexistent-disc.ciso"), "--out", str(output)]
        cases = [([], "ENOENT"),
                 (["--relay-preflight", str(null_preflight)], "requires both"),
                 (["--relay-preflight", str(null_preflight), "--relay-url",
                   "wss://relay.staging.example.invalid"], "unsupported or missing fields")]
        for flags, expected in cases:
            with self.subTest(flags=flags):
                result = subprocess.run(base + flags, cwd=ROOT, capture_output=True,
                                        text=True, timeout=15)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected, result.stderr)
                self.assertFalse(output.exists(), "CLI admission must stop before browser output")

    def test_external_mode_requires_bound_read_only_staging_identity_and_origin(self):
        result = subprocess.run(
            [
                str(node_runtime(ROOT)),
                "--input-type=module",
                "--eval",
                NODE_CHECK,
                "--",
                str(ROOT / "scripts/a3_public_relay_preflight.mjs"),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("public relay preflight default, binding and refusal controls pass", result.stdout)


if __name__ == "__main__":
    unittest.main()
