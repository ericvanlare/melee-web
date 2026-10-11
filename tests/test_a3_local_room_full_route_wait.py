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

const [routePath, fixturePath] = process.argv.slice(1);
const { classifyStockObservation } = await import(pathToFileURL(routePath).href);
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

console.log('captured render settle waits; timing/fault/unexpected/invalid terminal states refuse; valid Results handoff remains strict');
"""


class A3LocalRoomFullRouteWaitTests(unittest.TestCase):
    def test_captured_first_use_render_settle_is_the_only_new_stopped_wait(self):
        result = subprocess.run(
            [
                str(node_runtime(ROOT)),
                "--input-type=module",
                "--eval",
                NODE_CHECK,
                "--",
                str(ROOT / "scripts/a3_local_room_full_route.mjs"),
                str(ROOT / "tests/fixtures/a3_first_use_render_wait_observation.json"),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("valid Results handoff remains strict", result.stdout)


if __name__ == "__main__":
    unittest.main()
