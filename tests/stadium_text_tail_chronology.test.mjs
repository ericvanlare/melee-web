#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';

const runnerPath = new URL('./stadium_c1a_browser_test.mjs', import.meta.url);
const runnerSource = await fs.readFile(runnerPath, 'utf8');
const cleanupMatch = runnerSource.match(/async function cleanupOwnedProcesses\(\) \{[\s\S]*?\n\}/);
assert.ok(cleanupMatch, 'Actual cleanupOwnedProcesses function is present');

async function exerciseCleanup({unloadAttemptedInitially, unloadCompletedInitially,
  failureLog, afterUnloadLog,
  genuinePreUnloadLog = {sample: 'genuine-pre-unload'},
  genuinePreUnloadValidation = {complete: true, source: 'original-capture'},
  originalError = 'original scenario failure',
  originalCleanupFailures = []}) {
  const events = [];
  const report = {
    result: 'fail',
    error: originalError,
    cleanup: {failures: originalCleanupFailures.map(row => ({...row})),
      unload_attempted: unloadAttemptedInitially,
      unload_completed: unloadCompletedInitially},
    scenario: {first_sss_vs_setup: {text_tail_probe: {
      requested: true,
      log_before_unload: genuinePreUnloadLog,
      log_validation_before_unload: genuinePreUnloadValidation,
      after_unload: null,
      failure_time_log: null,
      failure_time_log_validation: null,
      failure_time_unload_attempted: null,
    }}},
  };
  const sandbox = {
    LIMITS: {cleanupTotalMs: 10000, cleanupMs: 5000},
    cleanupDeadline: 0,
    cleanupRemaining: () => 5000,
    report,
    page: {isClosed: () => false},
    driver: {
      unload: async () => { events.push('unload'); },
      dispose: () => { events.push('dispose'); },
    },
    unloadAttempted: unloadAttemptedInitially,
    context: null,
    browserCdp: null,
    browser: null,
    browserServer: null,
    firstCssBrowserFinalDraw: false,
    firstCssBrowserStream: false,
    firstSssConstructorPair: false,
    firstSssConsumedPadTick: false,
    firstSssDraw: false,
    firstSssPrefix: false,
    firstSssVsEntry: false,
    firstSssVsSetup: true,
    firstSssVsSetupTextTail: true,
    timeout: async operation => operation,
    errorText: error => String(error),
    readFirstSssVsEntryObservation: async () => {
      events.push('setup-failure-snapshot');
      return {captured: true};
    },
    snapshotFirstSssTextTailLog: async () => {
      events.push('failure-time-log');
      return failureLog;
    },
    validateFirstSssTextTailLog: log => ({
      complete: true, errors: [], validated_sample: log.sample,
    }),
    nativeSnapshot: async () => ({}),
    saveScreenshot: async () => {},
    persistReport: async () => {},
    harvestFirstSssTextTailAfterUnload: async () => {
      events.push('after-unload-harvest');
      return {
        ordinary_observation_present: false,
        text_tail_probe: {probe_enabled: false,
          membership_snapshot_active: false, cleanup_done: true},
        text_tail_log: afterUnloadLog,
        log_probe_after_reset: {enabled: false, retained_records: 0},
      };
    },
    cleanupServer: async () => {},
  };
  const context = vm.createContext(sandbox);
  new vm.Script(cleanupMatch[0] + '\nglobalThis.runActualCleanup = cleanupOwnedProcesses;')
    .runInContext(context);
  await context.runActualCleanup();
  return {report, events, genuinePreUnloadLog, genuinePreUnloadValidation,
    originalError, originalCleanupFailures};
}

for (const scenario of [
  {name: 'early failure before unload without a genuine pre-unload sample',
    unloadAttemptedInitially: false, unloadCompletedInitially: false,
    genuinePreUnloadLog: null, genuinePreUnloadValidation: null,
    failureLog: {sample: 'failure-before-unload'},
    afterUnloadLog: {sample: 'harvest-after-cleanup-unload'}},
  {name: 'failure after successful unload', unloadAttemptedInitially: true,
    unloadCompletedInitially: true,
    failureLog: {sample: 'failure-after-unload'},
    afterUnloadLog: {sample: 'harvest-after-prior-unload'}},
  {name: 'failure after refused unload', unloadAttemptedInitially: true,
    unloadCompletedInitially: false,
    originalError: 'original Setup failure retained after unload refusal',
    originalCleanupFailures: [{step: 'unload',
      error: 'Stadium display teardown refused a mismatched live owner'}],
    failureLog: {sample: 'failure-after-refused-unload'},
    afterUnloadLog: {sample: 'harvest-after-refused-unload'}},
]) {
  const result = await exerciseCleanup(scenario);
  const probe = result.report.scenario.first_sss_vs_setup.text_tail_probe;
  assert.deepEqual(probe.log_before_unload, result.genuinePreUnloadLog, scenario.name);
  assert.deepEqual(probe.log_validation_before_unload,
    result.genuinePreUnloadValidation, scenario.name);
  assert.deepEqual(probe.failure_time_log, scenario.failureLog, scenario.name);
  assert.deepEqual(probe.failure_time_log_validation,
    {complete: true, errors: [], validated_sample: scenario.failureLog.sample}, scenario.name);
  assert.equal(probe.failure_time_unload_attempted,
    scenario.unloadAttemptedInitially, scenario.name);
  assert.notDeepEqual(probe.failure_time_log, probe.log_before_unload, scenario.name);
  assert.deepEqual(probe.after_unload.text_tail_log, scenario.afterUnloadLog, scenario.name);
  assert.notDeepEqual(probe.failure_time_log, probe.after_unload.text_tail_log, scenario.name);
  assert.equal(result.report.cleanup.unload_attempted, true, scenario.name);
  assert.equal(result.report.cleanup.unload_completed,
    scenario.unloadCompletedInitially || !scenario.unloadAttemptedInitially, scenario.name);
  assert.equal(result.report.error, result.originalError, scenario.name);
  assert.equal(result.report.result, 'fail', scenario.name);
  assert.deepEqual(result.report.cleanup.failures, result.originalCleanupFailures, scenario.name);
  assert.ok(result.events.indexOf('failure-time-log') <
    result.events.indexOf('after-unload-harvest'), scenario.name);
  if (scenario.unloadAttemptedInitially) {
    assert.equal(result.events.includes('unload'), false, scenario.name);
  } else {
    assert.ok(result.events.indexOf('failure-time-log') < result.events.indexOf('unload'),
      scenario.name);
    assert.ok(result.events.indexOf('unload') <
      result.events.indexOf('after-unload-harvest'), scenario.name);
  }
}
console.log('PASS actual cleanup chronology before/after unload');
