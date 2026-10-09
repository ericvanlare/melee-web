import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import {observeRuntimeOwner} from './runtime_owner_observation.mjs';

// Execute the actual R292 health/capture callsite with the actual serialized reader.
const source = fs.readFileSync(new URL('./vs_rules_item_menu_browser_test.mjs', import.meta.url), 'utf8');
const callsite = source.slice(source.indexOf('const observeTimeoutOwner ='),
  source.indexOf('const waitForNoQueuedPad'));
function harness({phase = 7, error = null, timingPause = false, countersFail = false} = {}) {
  let evaluations = 0;
  const report = {competitiveTimeoutRoute: {}, errors: [], timingPauses: []};
  const browser = vm.createContext({
    Module: {
      UTF8ToString: value => value,
      _melee_web_native_menu_message: () => timingPause ? 'Paused after a timing disruption' : 'Original Results',
      _melee_web_native_menu_phase: () => phase,
      _melee_web_native_menu_running: () => 1,
      _melee_web_native_menu_match_observe: () => JSON.stringify({ownerPhase: phase, frame: 28800}),
      _melee_web_native_menu_results_pad_trace: () => JSON.stringify({ownerPhase: phase, rows: []}),
    },
    document: {querySelector: selector => selector === '#status' ?
      {textContent: 'Reading local data', dataset: {runtimeError: error}} :
      selector === '#pause' ? {disabled: false} : null},
  });
  const page = {evaluate: async (reader, options) => {
    evaluations++;
    browser.options = options;
    const snapshot = vm.runInContext(`(${reader.toString()})(options)`, browser);
    // Scene advances between evaluation tasks, never between synchronous getters.
    phase = 5;
    return structuredClone(snapshot);
  }};
  const context = vm.createContext({page, report, observeRuntimeOwner, Date,
    lastRuntimeCounterPollAt: 0,
    checkRuntimeDiagnosticCounters: async () => {if (countersFail) throw Error('counter guard');}});
  vm.runInContext(callsite + '\nglobalThis.observe = observeTimeoutOwner;', context);
  return {report, observe: context.observe, evaluations: () => evaluations};
}
test('actual R292 timeout callsite captures one coherent host/match before scene advances', async () => {
  const h = harness();
  const snapshot = await h.observe('terminal');
  assert.equal(h.evaluations(), 1);
  assert.equal(snapshot.state.phase, 7);
  assert.equal(snapshot.match.ownerPhase, 7);
  assert.equal(h.report.competitiveTimeoutRoute.latestOwnerObservation.match.ownerPhase, 7);
  // The actual timeout route must consume this pair at its phase/match branches.
  assert(source.includes('const {state, match} = await observeTimeoutOwner(\'competitive timeout match remains unpaused\')'));
  assert(source.includes('const {state: stateAfterTimeout, match: terminal} = await observeTimeoutOwner('));
  assert.equal((source.match(/observeSample: observeResultsSample/g) || []).length, 2);
});
test('actual R292 Results callsite retains coherent host/trace before health rejects', async () => {
  for (const options of [{error: 'runtime failure'}, {timingPause: true}, {countersFail: true}]) {
    const h = harness({phase: 8, ...options});
    await assert.rejects(h.observe('Results', {includeMatch: false, includeResultsTrace: true}));
    const route = h.report.competitiveTimeoutRoute;
    assert.equal(h.evaluations(), 1);
    assert.equal(route.latestOwnerObservation.state.phase, 8);
    assert.equal(route.latestOwnerObservation.trace.ownerPhase, 8);
    assert.equal(route.results_pad_trace_latest.ownerPhase, 8);
    assert.equal(route.latestOwnerObservation.match, undefined);
  }
});
