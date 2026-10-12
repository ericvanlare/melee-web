"""Fail-closed exact-source-cursor pairing controls for the A3 route observer."""
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

const [joinPath, fixturePath] = process.argv.slice(1);
const { createSourceCursorJoin } = await import(pathToFileURL(joinPath).href);
const fixture = JSON.parse(await readFile(fixturePath, 'utf8'));
assert.equal(fixture.schema, 'a3-source-cursor-capture4-retained-suffix-v1');
assert.equal(fixture.evidence.capture, 'capture4');
assert.equal(fixture.evidence.retained_poll_pairs, 32);
assert.match(fixture.evidence.limitation, /not the full 4929-poll stream/);

const retainedJoin = createSourceCursorJoin({retainedCursorsPerPeer: 8});
let fixtureSequence = 0;
let rejectedUnalignedPolls = 0;
for (const pair of fixture.pairs) {
  assert.equal(pair.alpha.phase, 3);
  assert.equal(pair.beta.phase, 3);
  assert.equal(pair.alpha.running, 1);
  assert.equal(pair.beta.running, 1);
  assert.notEqual(pair.alpha.source_tick, pair.beta.source_tick);
  rejectedUnalignedPolls++;
  const observedAt = () => new Date(Date.UTC(2026, 9, 11, 0, 0, fixtureSequence++)).toISOString();
  retainedJoin.observe('alpha', pair.alpha, observedAt());
  retainedJoin.observe('beta', pair.beta, observedAt());
  assert.equal(retainedJoin.takePair(), null,
    'the retained source observations contain no exact-key pair');
}
assert.equal(rejectedUnalignedPolls, 32);
assert.deepEqual(retainedJoin.status().retained_rows_by_peer, {alpha: 8, beta: 8});

// Synthetic scheduler control: a single lagging-peer reread reaches the exact
// cursor already observed by the leader. This is not a capture4 sample.
const chase = createSourceCursorJoin({retainedCursorsPerPeer: 4});
const alpha100 = {source_tick: 100, phase: 3, running: 1, stage: {ids: [1, 2]}, marker: 'alpha-100'};
const beta101 = {source_tick: 101, phase: 3, running: 1, stage: {ids: [3, 4]}, marker: 'beta-101'};
chase.observe('alpha', alpha100, '2026-10-11T00:00:00.000Z');
chase.observe('beta', beta101, '2026-10-11T00:00:01.000Z');
assert.equal(chase.laggingPeer(), 'alpha');
assert.equal(chase.takePair(), null);
const alpha101 = {source_tick: 101, phase: 3, running: 1, stage: {ids: [5, 6]}, marker: 'alpha-101'};
chase.observe('alpha', alpha101, '2026-10-11T00:00:02.000Z');
const matched = chase.takePair();
assert.equal(matched.source_tick, 101);
assert.strictEqual(matched.rows[0], alpha101);
assert.strictEqual(matched.rows[1], beta101);
assert.deepEqual(matched.observed_at_by_peer,
  {alpha: '2026-10-11T00:00:02.000Z', beta: '2026-10-11T00:00:01.000Z'});
assert.deepEqual(matched.rows.map(row => row.stage.ids), [[5, 6], [3, 4]],
  'the join returns complete observed rows without cross-cursor field mixing');
assert.equal(chase.takePair(), null, 'an unchanged pair is not emitted twice');

// Preparation may change fields while the source cursor is stationary. A new
// observed row at the same key can be paired, but the route predicate still
// receives running=0 and remains responsible for refusing input readiness.
const beta101Prepared = {...beta101, running: 0, message: 'Preparing first-use rendering...'};
chase.observe('beta', beta101Prepared, '2026-10-11T00:00:03.000Z');
const sameCursorUpdate = chase.takePair();
assert.equal(sameCursorUpdate.source_tick, 101);
assert.strictEqual(sameCursorUpdate.rows[0], alpha101);
assert.strictEqual(sameCursorUpdate.rows[1], beta101Prepared);
assert.deepEqual(sameCursorUpdate.observed_at_by_peer,
  {alpha: '2026-10-11T00:00:02.000Z', beta: '2026-10-11T00:00:03.000Z'});
assert.equal(sameCursorUpdate.rows[1].running, 0);
assert.equal(chase.takePair(), null);

const alpha102 = {source_tick: 102, phase: 3, running: 1, marker: 'alpha-102'};
const beta102 = {source_tick: 102, phase: 3, running: 1, marker: 'beta-102'};
chase.observe('alpha', alpha102, '2026-10-11T00:00:04.000Z');
chase.observe('beta', beta102, '2026-10-11T00:00:05.000Z');
assert.deepEqual(chase.takePair(), {source_tick: 102, rows: [alpha102, beta102],
  observed_at_by_peer: {alpha: '2026-10-11T00:00:04.000Z', beta: '2026-10-11T00:00:05.000Z'},
  observation_sequence_by_peer: {alpha: 3, beta: 3}});
assert.throws(() => chase.observe('alpha', alpha101, '2026-10-11T00:00:06.000Z'), /source_tick moved backward/);
assert.throws(() => chase.observe('beta', {...beta102, source_tick: null}, '2026-10-11T00:00:07.000Z'), /no nonnegative integer/);
assert.throws(() => createSourceCursorJoin({retainedCursorsPerPeer: 0}), /integer from 1 through 64/);

const latestOnly = createSourceCursorJoin({retainedCursorsPerPeer: 4});
const at200 = {source_tick: 200, phase: 3, running: 1};
latestOnly.observe('alpha', at200, '2026-10-11T00:01:00.000Z');
latestOnly.observe('beta', {...at200}, '2026-10-11T00:01:00.001Z');
assert.equal(latestOnly.takePair().source_tick, 200);
latestOnly.observe('alpha', {source_tick: 201, phase: 3, running: 1}, '2026-10-11T00:01:00.002Z');
latestOnly.observe('beta', {source_tick: 202, phase: 3, running: 1}, '2026-10-11T00:01:00.003Z');
assert.equal(latestOnly.takePair(), null,
  'a previously unseen older common key is refused after both peers crossed it');
const alpha202 = {source_tick: 202, phase: 3, running: 1, marker: 'latest alpha row'};
latestOnly.observe('alpha', alpha202, '2026-10-11T00:01:00.004Z');
const latestPair = latestOnly.takePair();
assert.equal(latestPair.source_tick, 202);
assert.strictEqual(latestPair.rows[0], alpha202);
assert.equal(latestPair.observed_at_by_peer.beta, '2026-10-11T00:01:00.003Z');

// A wait after an action starts with a fresh acquisition. It cannot reuse a
// ready pair from a previous wait to hide a newly observed stopped owner.
const priorWait = createSourceCursorJoin();
priorWait.observe('alpha', {source_tick: 300, phase: 3, running: 1}, '2026-10-11T00:02:00.000Z');
priorWait.observe('beta', {source_tick: 300, phase: 3, running: 1}, '2026-10-11T00:02:00.001Z');
assert.equal(priorWait.takePair().rows.every(row => row.running === 1), true);
const nextWait = createSourceCursorJoin();
const alpha301 = {source_tick: 301, phase: 3, running: 1};
const beta301Stopped = {source_tick: 301, phase: 3, running: 0,
  message: 'Preparing first-use rendering...'};
nextWait.observe('alpha', alpha301, '2026-10-11T00:02:00.040Z');
nextWait.observe('beta', beta301Stopped, '2026-10-11T00:02:00.041Z');
const nextWaitPair = nextWait.takePair();
assert.equal(nextWaitPair.source_tick, 301);
assert.strictEqual(nextWaitPair.rows[1], beta301Stopped);
assert.equal(nextWaitPair.rows.every(row => row.running === 1), false,
  'a fresh stopped observation cannot be satisfied by a previous ready pair');

console.log('capture4 retained suffix refuses all 32 unaligned polls; exact latest-cursor join, fresh acquisitions, preparation updates, timestamps, monotonicity, and bounded retention pass');
"""


class A3SourceCursorJoinTests(unittest.TestCase):
    def test_capture4_retained_suffix_and_exact_cursor_join(self):
        result = subprocess.run(
            [
                str(node_runtime(ROOT)),
                "--input-type=module",
                "--eval",
                NODE_CHECK,
                "--",
                str(ROOT / "scripts/a3_source_cursor_join.mjs"),
                str(ROOT / "tests/fixtures/a3_source_cursor_capture4_retained_suffix.json"),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("fresh acquisitions", result.stdout)


if __name__ == "__main__":
    unittest.main()
