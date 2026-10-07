import assert from 'node:assert/strict';
import test from 'node:test';
import {readFile} from 'node:fs/promises';
import vm from 'node:vm';
import {LOCKSTEP_DELAY} from '../scripts/net_lockstep_core.mjs';
import {verifyDisconnectBoundary} from '../scripts/net_lockstep_observers.mjs';

// Exercise the actual poll/injection and final report writer without launching
// services. The injected field is produced by the action, never by this fixture.
const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
const pollSource = source.slice(source.indexOf('async function pollRun()'),
  source.indexOf('async function waitForPositivePeerCompletion()'));
const finalizerSource = source.slice(source.lastIndexOf('  if (transportErrors.length || callbackErrors.length || closeNotes.length)'),
  source.indexOf("\n}\nconsole.log(JSON.stringify({scenario: pairResults.scenario"));

export function reporterFixture({closeFailure = false, acknowledgement = 3} = {}) {
  const pairResults = {scenario: 'disconnect', outcome: 'expected-disconnect', first_error: null,
    disconnect_missing_input_tick: 4};
  const peers = Object.fromEntries(['alpha', 'beta'].map(role => [role, {
    terminal: null,
    summary: () => ({terminal: null, remote_ack_input: acknowledgement, local_input_ticks: 4,
      remote_input_ticks: 4, checksum_mismatches: [], next_checksum_compare: 4}),
    setNativeProgress: async () => {},
  }]));
  const closeCalls = [], writes = [];
  const context = vm.createContext({Date, JSON, Object, Promise, Error,
    pairResults, peers, instanceRows: {alpha: {last_wait_episodes: 0}, beta: {last_wait_episodes: 0}},
    transportErrors: [], callbackErrors: [], closeNotes: [], waitObservations: [],
    deadline: Date.now() + 1000, stallMs: 1000, pollMs: 0,
    scenario: 'disconnect', probe: false, inputSampling: false, localWebRtc: true, browserOwned: true,
    disconnectAt: 6, sourceTicks: 7,
    verifyDisconnectBoundary, LOCKSTEP_DELAY,
    refreshBrowserPeers: async () => {}, drainChecksums: async () => {}, sleep: async () => {},
    checkedHealth: async () => ({status: {cursor: 6, pushed: 6, wait_episodes: 0,
      blocker: 'network_wait', network_wait: {active: 1}}, native: {phase: 1}}),
    relay: {beta: {close: async intentional => {
      closeCalls.push({intentional, event: pairResults.injected_disconnect ? JSON.parse(JSON.stringify(pairResults.injected_disconnect)) : null});
      if (closeFailure) throw Error('endpoint close failed');
      peers.alpha.terminal = peers.beta.terminal = {kind: 'disconnect'};
    }}},
    output: '/owned-output', path: {join: (...parts) => parts.join('/')},
    fs: {writeFile: async (path, bytes) => {writes.push({path, report: JSON.parse(bytes)});}},
  });
  vm.runInContext(pollSource + '\n globalThis.poll = pollRun;\n' +
    'globalThis.finalize = async () => {\n' + finalizerSource + '\n};', context);
  return {context, pairResults, closeCalls, writes};
}

test('actual disconnect action is exported by the final report writer before close', async () => {
  const f = reporterFixture();
  await f.context.poll();
  await f.context.finalize();
  assert.equal(f.closeCalls.length, 1);
  assert.equal(f.closeCalls[0].intentional, false);
  assert.deepEqual(f.closeCalls[0].event, f.writes[0].report.injected_disconnect);
  assert.equal(f.writes[0].report.injected_disconnect.role, 'beta');
  assert.equal(f.writes[0].report.injected_disconnect.source_tick, 6);
  assert.ok(Number.isSafeInteger(f.writes[0].report.injected_disconnect.at_ms));
  assert.equal(f.writes[0].report.disconnect_before_close.alpha.remote_ack_input, 3);
  assert.equal(f.writes[0].report.outcome, 'expected-disconnect');
});

test('actual reporter retains the injection when endpoint close rejects and cleanup fails', async () => {
  const f = reporterFixture({closeFailure: true});
  await assert.rejects(f.context.poll(), /endpoint close failed/);
  f.context.closeNotes.push('endpoint close failed');
  await f.context.finalize();
  const report = f.writes[0].report;
  assert.equal(report.outcome, 'fail');
  assert.equal(report.first_error, 'endpoint close failed');
  assert.equal(report.injected_disconnect.source_tick, 6);
  assert.equal(report.injected_disconnect.role, 'beta');
  assert.deepEqual(report.cleanup_notes, ['endpoint close failed']);
});

test('missing pre-close ACK prevents both injection event and close action', async () => {
  const f = reporterFixture({acknowledgement: 2});
  await assert.rejects(f.context.poll(), /Disconnect pre-close boundary is incomplete/);
  f.context.closeNotes.push('pre-close ACK failed');
  await f.context.finalize();
  assert.equal(f.closeCalls.length, 0);
  assert.equal(f.writes[0].report.injected_disconnect, undefined);
  assert.equal(f.writes[0].report.outcome, 'fail');
});
