import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {classifyStockObservation} from '../scripts/a3_local_room_full_route.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const fixture = JSON.parse(await readFile(path.join(root, 'tests/fixtures/a3_first_use_render_wait_observation.json'), 'utf8'));
const pair = JSON.parse(await readFile(path.join(root, 'tests/fixtures/m2_ko_prepared_first_use_queue_empty_pair.json'), 'utf8'));
const [alpha, beta] = pair.peers;

const refusesAsWait = (mutate, label) => {
  const row = structuredClone(alpha);
  mutate(row);
  assert.throws(() => classifyStockObservation(row, label));
};

test('retained queue-empty alpha row classifies as wait without rewriting capture data', () => {
  const original = structuredClone(alpha);
  const state = classifyStockObservation(alpha, 'retained alpha exact queue-empty first-use row');
  assert.equal(state.wait, 'first-use-rendering');
  assert.deepEqual(state.match.players.map(player => player.stocks), [3, 4]);
  assert.deepEqual(alpha, original);
  assert.equal(beta.native_status.blocker, 'none');
  assert.equal(classifyStockObservation(beta, 'retained beta first-use row').wait, 'first-use-rendering');
});

test('existing blocker-none fixture remains a first-use wait', () => {
  const original = structuredClone(fixture.beta);
  const state = classifyStockObservation(fixture.beta, 'existing fixture');
  assert.equal(state.wait, 'first-use-rendering');
  assert.deepEqual(fixture.beta, original);
});

test('queue-empty acceptance requires an exact source-cursor frontier and safe integer metadata', () => {
  const mutations = [
    ['source tick mismatch', row => { row.source_tick += 1; }],
    ['cursor mismatch', row => { row.native_status.cursor -= 1; }],
    ['pushed mismatch', row => { row.native_status.pushed += 1; }],
    ['source tick noninteger', row => { row.source_tick = 1147.5; }],
    ['source tick boolean', row => { row.source_tick = true; }],
    ['source tick nonfinite', row => { row.source_tick = Number.NaN; }],
    ['cursor noninteger', row => { row.native_status.cursor = 1147.5; }],
    ['cursor boolean', row => { row.native_status.cursor = true; }],
    ['cursor nonfinite', row => { row.native_status.cursor = Number.POSITIVE_INFINITY; }],
    ['pushed noninteger', row => { row.native_status.pushed = 1147.5; }],
    ['pushed boolean', row => { row.native_status.pushed = true; }],
    ['pushed nonfinite', row => { row.native_status.pushed = Number.NaN; }],
  ];
  for (const [label, mutate] of mutations) refusesAsWait(mutate, label);
});

test('queue-empty wait requires healthy active owner, confirmed start, no wait episode or terminal', () => {
  const mutations = [
    ['native owner inactive', row => { row.native_status.active = 0; }],
    ['native context missing', row => { delete row.native_status.context_applied; }],
    ['native context unapplied', row => { row.native_status.context_applied = 0; }],
    ['start not required', row => { row.native_status.start.required = 0; }],
    ['start not recorded', row => { row.native_status.start.recorded = 0; }],
    ['start not confirmed', row => { row.native_status.start.confirmed = 0; }],
    ['start capture failed', row => { row.native_status.start.capture_failed = 1; }],
    ['terminal present', row => { row.native_status.terminal.kind = 1; }],
    ['local capture failure', row => { row.native_status.local_capture_failure = 'PAD capture failure'; }],
    ['active wait episode', row => { row.native_status.network_wait.active = 1; }],
    ['wrong first-use message', row => { row.message = 'Preparing original match continuation...'; }],
  ];
  for (const [label, mutate] of mutations) refusesAsWait(mutate, label);
});

test('queue-empty wait retains all indexed-counter refusals and strict match health', () => {
  const mutations = [
    ['duplicate source frame', row => { row.native_status.indexed.duplicates = 1; }],
    ['conflicting source frame', row => { row.native_status.indexed.conflicts = 1; }],
    ['source gap', row => { row.native_status.indexed.gaps = 1; }],
    ['invalid indexed input', row => { row.native_status.indexed.invalid = 1; }],
    ['not ready', row => { row.match.ready = false; }],
    ['paused', row => { row.match.paused = true; }],
    ['complete', row => { row.match.complete = true; }],
    ['ending', row => { row.match.ending = true; }],
    ['has outcome', row => { row.match.outcome = 2; }],
  ];
  for (const [label, mutate] of mutations) refusesAsWait(mutate, label);
});

test('authored Results handoff remains strict and distinct from first-use wait', () => {
  const invalidTerminal = structuredClone(fixture.beta);
  invalidTerminal.message = 'Preparing original match continuation...';
  invalidTerminal.match.complete = true;
  invalidTerminal.match.ending = true;
  invalidTerminal.match.outcome = 2;
  assert.throws(() => classifyStockObservation(invalidTerminal, 'invalid Results handoff'),
    /preparation stopped before the natural \[0,4\] terminal stocks/);
  invalidTerminal.match.players[0].stocks = 0;
  assert.equal(classifyStockObservation(invalidTerminal, 'valid Results handoff').handoff,
    'preparing-results');
});
