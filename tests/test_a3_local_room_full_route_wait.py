"""Keep the full-route native first-use wait classifier fail-closed."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


NODE_CHECK = r"""
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { pathToFileURL } from 'node:url';

const [routePath, fixturePath, waitDiagnosticsPath] = process.argv.slice(1);
const { classifyStockObservation, validateP2GameplayInputWindow } = await import(pathToFileURL(routePath).href);
const { checksumCompareReached, checksumWaitDiagnostic } = await import(pathToFileURL(waitDiagnosticsPath).href);
const fixture = JSON.parse(await readFile(fixturePath, 'utf8'));
assert.equal(fixture.schema, 'a3-first-use-render-wait-observation-v1');
assert.equal(fixture.alpha.source_tick, fixture.beta.source_tick);

const beta = fixture.beta;
const wait = classifyStockObservation(beta, 'captured live first-use render wait');
assert.equal(wait.wait, 'first-use-rendering');
assert.deepEqual(wait.match.players.map(player => player.stocks), [3, 4]);

const timingPause = structuredClone(beta);
timingPause.message = 'Paused after a timing disruption in the audio clock. Resume to continue.';
timingPause.match.players[0].stocks = 0;
timingPause.match.complete = true;
timingPause.match.ending = true;
timingPause.match.outcome = 2;
assert.throws(() => classifyStockObservation(timingPause, 'timing pause'),
  /stopped phase-7 state is not the authored Results transfer preparation/);

const runtimeFault = structuredClone(beta);
runtimeFault.error = 'native runtime fault';
assert.throws(() => classifyStockObservation(runtimeFault, 'native fault'),
  /native runtime error native runtime fault/);

const otherStoppedState = structuredClone(beta);
otherStoppedState.phase = 6;
assert.throws(() => classifyStockObservation(otherStoppedState, 'unexpected stopped state'),
  /unsupported phase\/running state 6\/0/);

const invalidTerminal = structuredClone(beta);
invalidTerminal.message = 'Preparing original match continuation...';
invalidTerminal.match.complete = true;
invalidTerminal.match.ending = true;
invalidTerminal.match.outcome = 2;
assert.throws(() => classifyStockObservation(invalidTerminal, 'invalid terminal'),
  /preparation stopped before the natural \[0,4\] terminal stocks/);

const validTerminalTransfer = structuredClone(invalidTerminal);
validTerminalTransfer.match.players[0].stocks = 0;
assert.equal(classifyStockObservation(validTerminalTransfer, 'valid Results preparation').handoff,
  'preparing-results');

const counterfeitRenderWait = structuredClone(beta);
counterfeitRenderWait.native_status.local_capture_failure = 'capture failed';
assert.throws(() => classifyStockObservation(counterfeitRenderWait, 'failed native owner'),
  /preparation stopped before the natural \[0,4\] terminal stocks/);

const waitRow = {
  menu_phase: 7,
  menu_running: 1,
  menu_message: 'Original Mario vs Mario match on Final Destination',
  runtime_error: null,
  native_status: {
    active: 1, cursor: 8203, pushed: 8205, blocker: 'none', terminal: {kind: 0},
    local_capture_failure: null,
    network_wait: {active: 0, callbacks: 7, episodes: 7, start_tick: 7649, last_tick: 7649, resume_count: 7},
    indexed: {duplicates: 0, conflicts: 0, gaps: 0, invalid: 0},
  },
  owner_health: {failure: null, peer: {failure: null, endpoint_error_count: 0, protocol: {
    role: 'alpha', local_port: 0, remote_port: 1, ready: true, terminal: null,
    local_input_ticks: 8203, remote_input_ticks: 8203, remote_contiguous_input: 8202,
    remote_ack_input: 8203, local_checksum_ticks: 8203, remote_checksum_ticks: 8203,
    remote_contiguous_checksum: 8202, local_contiguous_input: 8202,
    local_contiguous_checksum: 8202, next_checksum_compare: 8206,
    remote_ack_checksum: 8203, acknowledged_inputs: 8203, acknowledged_checksums: 8203,
    checksum_mismatch_count: 0, native_local_divergences: 0,
  }}},
  match_summary: {leg: 'vs', frame: 7558, ready: true, paused: false, ending: false,
    complete: false, outcome: 0, winner: -1,
    rules: {stage: 32, is_stock: 1, is_vs: 1, is_teams: 0, player_stocks: [4, 4]},
    players: [{source_port: 0, source_character: 8, stocks: 2, damage_percent: 0,
      x: 53.5, y: 0, motion: 14, groundAir: 0},
    {source_port: 1, source_character: 8, stocks: 4, damage_percent: 0,
      x: 61.5, y: 0, motion: 14, groundAir: 0}]},
};
assert.equal(checksumCompareReached(waitRow, 8206, null), true);
assert.equal(checksumCompareReached(waitRow, 8207, null), false);
assert.equal(checksumCompareReached(waitRow, 8206, 1), false);
assert.equal(checksumCompareReached({...waitRow, menu_running: 0}, 8206, null), false);
assert.equal(checksumCompareReached({...waitRow, owner_health: {peer: {
  protocol: {...waitRow.owner_health.peer.protocol, checksum_mismatch_count: 1}}}}, 8206, null), false);
const diagnostic = checksumWaitDiagnostic({...waitRow,
  snapshot: {checksums: Array.from({length: 8220}, () => Array(64).fill(0))}}, 'alpha');
assert.equal(diagnostic.menu_message, waitRow.menu_message);
assert.equal(diagnostic.phase, 7);
assert.equal(diagnostic.running, 1);
assert.equal(diagnostic.source_tick, 8203);
assert.equal(diagnostic.protocol.next_checksum_compare, 8206);
assert.equal(diagnostic.match_summary.frame, 7558);
assert.equal(diagnostic.match_summary.players[0].stocks, 2);
assert.equal(Object.hasOwn(diagnostic, 'snapshot'), false);
assert(JSON.stringify(diagnostic).length < 4096, 'wait diagnostic must stay bounded');

const padRow = (tick, port, bytes) => ({input_tick: tick, source_cursor: tick,
  local_port: port, bytes});
const neutral = () => Array(11).fill(0);
const right = () => { const bytes = neutral(); bytes[2] = 64; return bytes; };
const p2Rows = [padRow(20, 1, right()), padRow(21, 1, right()),
  padRow(22, 1, right()), padRow(23, 1, neutral())];
const p1Rows = [20, 21, 22, 23].map(tick => padRow(tick, 0, neutral()));
assert.deepEqual(validateP2GameplayInputWindow({p2Rows, p1Rows, movementTick: 20,
  releaseTick: 23, direction: 1}), {movement_input_tick: 20,
  neutral_release_input_tick: 23, p1_neutral_capture_count: 4, p2_direction: 1});
assert.throws(() => validateP2GameplayInputWindow({p2Rows: p2Rows.filter(row => row.input_tick !== 21),
  p1Rows, movementTick: 20, releaseTick: 23, direction: 1}), /missing or duplicate tick/);
assert.throws(() => validateP2GameplayInputWindow({p2Rows,
  p1Rows: p1Rows.map((row, index) => index === 2 ? {...row, bytes: right()} : row),
  movementTick: 20, releaseTick: 23, direction: 1}), /P1 was not literally neutral/);
assert.throws(() => validateP2GameplayInputWindow({p2Rows: p2Rows.map((row, index) =>
  index === 3 ? {...row, bytes: [...row.bytes.slice(0, 10)]} : row), p1Rows,
  movementTick: 20, releaseTick: 23, direction: 1}), /exact 11-byte PAD record/);
assert.throws(() => validateP2GameplayInputWindow({p2Rows, p1Rows,
  movementTick: 20, releaseTick: 23, direction: -1}), /requested horizontal direction/);

console.log('captured render settle waits; terminal checks stay strict; checksum waiter gate and bounded diagnostics retain exact state');
console.log('P2 component controls require contiguous movement, literal neutral release, and P1-neutral PAD rows');
"""


class A3LocalRoomFullRouteWaitTests(unittest.TestCase):
    def test_wait_and_p2_literal_input_window_controls(self):
        result = subprocess.run(
            [
                str(node_runtime(ROOT)),
                "--input-type=module",
                "--eval",
                NODE_CHECK,
                "--",
                str(ROOT / "scripts/a3_local_room_full_route.mjs"),
                str(ROOT / "tests/fixtures/a3_first_use_render_wait_observation.json"),
                str(ROOT / "scripts/a3_checksum_wait_diagnostics.mjs"),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("checksum waiter gate and bounded diagnostics retain exact state", result.stdout)
        self.assertIn("P2 component controls require contiguous movement", result.stdout)


if __name__ == "__main__":
    unittest.main()
