#!/usr/bin/env node
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {guardMatchedSetupCfNativeCleanup,
  matchedSetupCfNativeAccessAllowed} from './stadium_matched_setup_cf_runner_gate.mjs';

const runnerPath = new URL('./stadium_c1a_browser_test.mjs', import.meta.url);
const runnerSource = await fs.readFile(runnerPath, 'utf8');
const idleMatch = runnerSource.match(/async function assertMatchedSetupCfAsyncifyIdle\(label\) \{[\s\S]*?\n\}/);
assert.ok(idleMatch, 'Actual default-off Asyncify idle helper is present');
const cleanupMatch = runnerSource.match(/async function cleanupOwnedProcesses\(\) \{[\s\S]*?\n\}/);
assert.ok(cleanupMatch, 'Actual cleanupOwnedProcesses function is present');

async function exerciseCleanup({unloadAttemptedInitially, unloadCompletedInitially,
  failureLog, afterUnloadLog,
  unloadError = null,
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
    // Cleanup chronology here covers the ordinary default-off route.
    guardMatchedSetupCfNativeCleanup,
    matchedSetupCfNativeAccessAllowed,
    firstSssVsSetupCfExplicitStep: false,
    pendingMatchedSetupCfExplicitStep: null,
    matchedSetupCfExplicitAsyncifyUnsafe: false,
    matchedSetupCfExplicitNativeAccessClosed: false,
    matchedSetupCfExplicitArmed: false,
    LIMITS: {cleanupTotalMs: 10000, cleanupMs: 5000},
    cleanupDeadline: 0,
    cleanupRemaining: () => 5000,
    report,
    page: {isClosed: () => false},
    driver: {
      unload: async () => { events.push('unload'); if (unloadError) throw unloadError; },
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
    firstSssFreshReadyStart: false,
    firstSssVsSetup: true,
    firstSssVsSetupCf: false,
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
  new vm.Script(idleMatch[0] + '\n' + cleanupMatch[0] + '\nglobalThis.runActualCleanup = cleanupOwnedProcesses;')
    .runInContext(context);
  await context.runActualCleanup();
  return {report, events, genuinePreUnloadLog, genuinePreUnloadValidation,
    originalError, originalCleanupFailures, unloadError};
}

const unloadDiagnostic = {url: 'http://127.0.0.1:8787/runtime.html',
  phase: 5, native_owner: 'mismatched-live-owner', errors: ['native unload refused']};
const unloadCause = new Error('Stadium display teardown refused a mismatched live owner');
const wrappedUnloadError = new Error('Browser driver unload step failed', {cause: unloadCause});
wrappedUnloadError.step = 'unload';
wrappedUnloadError.diagnostics = unloadDiagnostic;

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
  {name: 'failure-path unload retains driver diagnostics without replacing primary failure',
    unloadAttemptedInitially: false, unloadCompletedInitially: false,
    originalError: 'primary action failure remains authoritative', unloadError: wrappedUnloadError,
    failureLog: {sample: 'primary-failure'},
    afterUnloadLog: {sample: 'harvest-after-diagnostic-unload'}},
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
    scenario.unloadCompletedInitially || (!scenario.unloadAttemptedInitially && !scenario.unloadError),
    scenario.name);
  assert.equal(result.report.error, result.originalError, scenario.name);
  assert.equal(result.report.result, 'fail', scenario.name);
  if (scenario.unloadError) {
    assert.equal(result.report.cleanup.failures.length, 1, scenario.name);
    assert.deepEqual(JSON.parse(JSON.stringify(result.report.cleanup.failures[0])), {
      step: 'unload', error: String(scenario.unloadError),
      driver_step: 'unload', cause: String(unloadCause), diagnostics: unloadDiagnostic,
    }, scenario.name);
  } else {
    assert.deepEqual(result.report.cleanup.failures, result.originalCleanupFailures, scenario.name);
  }
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
