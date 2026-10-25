import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {createStadiumC3ReportCheckpoint, isStadiumC3ReportCheckpointEligible,
  materializeStadiumC3ReportCheckpoint, recoverStadiumC3ReportCheckpoint,
  STADIUM_C3_FROZEN_SCENARIO_FIELDS, STADIUM_C3_REPORT_CHECKPOINT_SCHEMA} from './stadium_c3_report_checkpoint.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RUNNER = path.join(HERE, 'stadium_c1a_browser_test.mjs');
const OWNER_READER = process.env.MELEE_STADIUM_OWNER_READER || null;
const makeTemp = async prefix => fs.mkdtemp(path.join(os.tmpdir(), prefix));
const removeTemp = async pathName => fs.rm(pathName, {recursive: true, force: true});

function representativeReport() {
  const scenario = {
    before: {phase: 3, running: 1},
    first_css_browser_stream: {samples: [{cursor: 12, state: 'css'}], nested: {x: 1}},
    first_css_final_pending_draw: {phase: 1, complete: true},
    first_sss_constructor_pair: {entry: {cursor: 20}, returned: {cursor: 21}},
    first_sss_consumed_tick: {tick: 1, input: [0, 0]},
    first_sss_draw: {draw: 1, digest: 'd1'},
    first_sss_prefix: {samples: Array.from({length: 3}, (_, i) => ({i, value: `s${i}`}))},
    first_sss_vs_entry: {phase: 'entry', owner: 'source'},
    matched_setup_cf: {mode: 'run2-proper-prefix-v2', explicit_step_mode: true,
      actions: [{schedule_index: 0, accepted: true, detail: {owner: {source_index: 0}}}],
      runtime_diagnostics: [{schedule_index: 0, message: 'initial'}],
      host_comparisons: []},
    mutable_failure_snapshot: null,
  };
  return {
    schema: 'retained-capture-report-fixture', result: 'pass', execution_state: 'running',
    browser_process_info: [{id: 42, type: 'page'}],
    browser_process_groups: [{pgid: 41}], browser_process_ps: ['owned-chrome'],
    browser_process_start: [{pid: 41, start: 'captured'}],
    browser_process_cleanup: [{pid: 41, status: 'alive'}],
    browser_process_group_cleanup: [{pgid: 41, status: 'alive'}],
    scenario,
  };
}

function extractPersistReport(source) {
  const begin = source.indexOf('const persistReport = async () => {');
  const endMarker = '\n};\n\nlet failure = null;';
  const end = source.indexOf(endMarker, begin);
  assert.ok(begin >= 0 && end > begin, 'actual persistReport function is discoverable');
  return `${source.slice(begin, end + 3)}\nglobalThis.__persistReport = persistReport;`;
}

async function actualPersistReport({report, reportPath, checkpoint, reportFs, events = []}) {
  const source = await fs.readFile(RUNNER, 'utf8');
  const checkpointApi = {
    shouldUse: value => checkpoint.shouldUse(value),
    prepare: async (...args) => checkpoint.prepare(...args),
    nextReportTemporaryPath: () => checkpoint.nextReportTemporaryPath(),
    commitPrepared: value => { events.push('checkpoint-commit'); return checkpoint.commitPrepared(value); },
  };
  const observedFs = {...reportFs,
    rename: async (from, to) => {
      const result = await reportFs.rename(from, to);
      if (to === reportPath) events.push('report-rename-complete');
      return result;
    }};
  const context = {
    report, inventoryOutput: reportPath, stadiumC3ReportCheckpoint: checkpointApi,
    recreationCanary: false,
    publishRecreationSupervisorReport: async () => { throw Error('legacy persist must not publish canary report'); },
    matchedSetupCfTiming: null, fs: observedFs, Buffer, JSON, Error,
    monotonicNowNs: () => 0n, elapsedMs: () => 0,
    timedActionSync: (_sample, _stage, _field, operation) => operation(),
    timedActionAsync: async (_sample, _stage, _field, operation) => operation(),
  };
  vm.runInNewContext(extractPersistReport(source), context, {timeout: 1000});
  return context.__persistReport;
}

function makeFileSystemFailure(realFs, failingOperation, error) {
  return {
    access: (...args) => realFs.access(...args),
    readFile: (...args) => realFs.readFile(...args),
    appendFile: async (file, data, ...args) => {
      if (failingOperation === 'appendFile') {
        await realFs.appendFile(file, Buffer.from('partial-retained-journal-bytes\n'));
        throw error;
      }
      return realFs.appendFile(file, data, ...args);
    },
    writeFile: async (file, data, ...args) => {
      if (failingOperation === 'writeFile') {
        await realFs.writeFile(file, Buffer.from('partial-retained-temp-bytes'));
        throw error;
      }
      return realFs.writeFile(file, data, ...args);
    },
    rename: async (...args) => {
      if (failingOperation === 'rename') throw error;
      return realFs.rename(...args);
    },
  };
}

function makeControllableFileSystem(realFs) {
  let failure = null;
  return {
    setFailure(operation, error) { failure = {operation, error}; },
    access: (...args) => realFs.access(...args),
    readFile: (...args) => realFs.readFile(...args),
    appendFile: async (file, data, ...args) => {
      if (failure?.operation === 'appendFile') {
        await realFs.appendFile(file, Buffer.from('partial-retained-journal-bytes\n'));
        throw failure.error;
      }
      return realFs.appendFile(file, data, ...args);
    },
    writeFile: async (file, data, ...args) => {
      if (failure?.operation === 'writeFile') {
        await realFs.writeFile(file, Buffer.from('partial-retained-temp-bytes'));
        throw failure.error;
      }
      return realFs.writeFile(file, data, ...args);
    },
    rename: async (...args) => {
      if (failure?.operation === 'rename') throw failure.error;
      return realFs.rename(...args);
    },
  };
}

test('normal reports keep the original full report persistence path', async () => {
  const report = {result: 'fail', scenario: {before: true}};
  assert.equal(isStadiumC3ReportCheckpointEligible(report), false);
  const dir = await makeTemp('stadium-c3-normal-report-');
  try {
    const checkpoint = createStadiumC3ReportCheckpoint({reportPath: path.join(dir, 'report.json')});
    assert.equal(await checkpoint.prepare(report), report);
    assert.equal(checkpoint.shouldUse(report), false);
    await assert.rejects(fs.access(checkpoint.sidecarPath), {code: 'ENOENT'});
  } finally { await removeTemp(dir); }
});

test('intermediate checkpoint keeps owner fields and materializes exact key order and JSON', async () => {
  const report = representativeReport();
  const dir = await makeTemp('stadium-c3-roundtrip-');
  const reportPath = path.join(dir, 'report.json');
  try {
    const checkpointWriter = createStadiumC3ReportCheckpoint({reportPath});
    const checkpoint = await checkpointWriter.prepare(report);
    assert.equal(checkpoint.stadium_c3_persistence_checkpoint.schema,
      STADIUM_C3_REPORT_CHECKPOINT_SCHEMA);
    assert.equal(checkpoint.browser_process_info[0].id, 42);
    assert.equal(checkpoint.browser_process_groups[0].pgid, 41);
    assert.equal(checkpoint.browser_process_ps[0], 'owned-chrome');
    assert.equal(checkpoint.browser_process_start[0].pid, 41);
    assert.equal(checkpoint.browser_process_cleanup[0].pid, 41);
    assert.equal(checkpoint.browser_process_group_cleanup[0].pgid, 41);
    assert.equal(Object.hasOwn(checkpoint.scenario, 'first_sss_prefix'), false);
    const sidecarBytes = await fs.readFile(checkpointWriter.sidecarPath);
    assert.equal(sidecarBytes.byteLength, checkpoint.stadium_c3_persistence_checkpoint.sidecar.byte_length);
    assert.equal(crypto.createHash('sha256').update(sidecarBytes).digest('hex'),
      checkpoint.stadium_c3_persistence_checkpoint.sidecar.sha256);
    const dynamicBytes = await fs.readFile(checkpointWriter.dynamicRecordPath);
    const persistedCheckpoint = JSON.parse(`${JSON.stringify(checkpoint)}\n`);
    const recovered = materializeStadiumC3ReportCheckpoint(persistedCheckpoint, sidecarBytes, dynamicBytes);
    checkpointWriter.commitPrepared(checkpoint);
    assert.equal(JSON.stringify(recovered) + '\n', JSON.stringify(report) + '\n',
      'a parsed last-good checkpoint reconstructs the exact in-memory report without advancing it');
    assert.equal(recovered.scenario.matched_setup_cf.actions.length,
      report.scenario.matched_setup_cf.actions.length,
      'checkpoint recovery preserves action/approval state as recorded');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint, sidecarBytes),
      /dynamic-record sidecar bytes are missing or invalid/,
      'recovery rejects a missing original dynamic-record payload');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint, sidecarBytes,
      Buffer.alloc(0)), /dynamic-record sidecar is truncated/,
      'recovery rejects dynamic-record bytes with the wrong byte length');
    const wrongPayload = JSON.parse(sidecarBytes.toString('utf8'));
    wrongPayload.scenario_fields.first_css_browser_stream.samples[0].state = 'CSS';
    const sameLengthWrongBytes = Buffer.from(`${JSON.stringify(wrongPayload)}\n`);
    assert.equal(sameLengthWrongBytes.byteLength, sidecarBytes.byteLength,
      'wrong-payload control preserves exact sidecar byte length');
    assert.deepEqual(Object.keys(wrongPayload.scenario_fields), Object.keys(
      JSON.parse(sidecarBytes.toString('utf8')).scenario_fields),
      'wrong-payload control keeps the exact sidecar field shape');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint,
      sameLengthWrongBytes, dynamicBytes), /sidecar byte identity mismatch/,
      'recovery rejects same-shaped, same-length frozen sidecar bytes with a different value');
    const finalReport = await checkpointWriter.prepare({...report, execution_state: 'completed'},
      {terminal: true});
    assert.deepEqual(Object.keys(finalReport), Object.keys(report));
    assert.deepEqual(Object.keys(finalReport.scenario), Object.keys(report.scenario));
    assert.equal(JSON.stringify(finalReport) + '\n',
      JSON.stringify({...report, execution_state: 'completed'}) + '\n');
    assert.equal(Object.hasOwn(finalReport, 'stadium_c3_persistence_checkpoint'), false);
  } finally { await removeTemp(dir); }
});

test('append-only record segments reconstruct in order and reject missing, truncated, reordered, or altered bytes', async () => {
  const report = representativeReport();
  const dir = await makeTemp('stadium-c3-dynamic-records-');
  const reportPath = path.join(dir, 'report.json');
  try {
    const writer = createStadiumC3ReportCheckpoint({reportPath});
    const first = await writer.prepare(report);
    writer.commitPrepared(first);
    const actions = report.scenario.matched_setup_cf.actions;
    assert.equal(Reflect.set(actions[0], 'accepted', false), false,
      'a committed history row cannot be mutated in place');
    assert.equal(Reflect.set(actions[0].detail.owner, 'source_index', 99), false,
      'a committed history row is recursively frozen');
    assert.equal(Reflect.set(actions, '0', {schedule_index: -1}), false,
      'a committed history index cannot be replaced');
    assert.equal(Reflect.set(actions, 'length', 0), false,
      'a committed history prefix cannot be shrunk');
    report.scenario.matched_setup_cf.actions.push({schedule_index: 1, accepted: true});
    report.scenario.matched_setup_cf.runtime_diagnostics.push({schedule_index: 1, message: 'second'});
    report.scenario.matched_setup_cf.host_comparisons.push({label: 'C', decision: 'PASS_EXACT'});
    const second = await writer.prepare(report);
    writer.commitPrepared(second);
    const parsed = JSON.parse(`${JSON.stringify(second)}\n`);
    const frozenBytes = await fs.readFile(writer.sidecarPath);
    const dynamicBytes = await fs.readFile(writer.dynamicRecordPath);
    assert.equal(parsed.stadium_c3_persistence_checkpoint.dynamic_records.segment_count, 2);
    assert.equal(JSON.stringify(materializeStadiumC3ReportCheckpoint(parsed, frozenBytes, dynamicBytes)),
      JSON.stringify(report), 'ordered deltas reconstruct the exact final object and array order');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(parsed, frozenBytes),
      /dynamic-record sidecar bytes are missing or invalid/);
    assert.throws(() => materializeStadiumC3ReportCheckpoint(parsed, frozenBytes,
      dynamicBytes.subarray(0, dynamicBytes.byteLength - 1)), /truncated/);
    const lines = dynamicBytes.toString('utf8').trimEnd().split('\n');
    const reordered = Buffer.from(`${lines.reverse().join('\n')}\n`);
    assert.throws(() => materializeStadiumC3ReportCheckpoint(parsed, frozenBytes, reordered),
      /segment order or shape is invalid/);
    const altered = Buffer.from(dynamicBytes.toString('utf8').replace('initial', 'Initial'));
    assert.equal(altered.byteLength, dynamicBytes.byteLength);
    assert.notDeepEqual(altered, dynamicBytes);
    assert.throws(() => materializeStadiumC3ReportCheckpoint(parsed, frozenBytes, altered),
      /segment order or shape is invalid/,
      'changing the first payload invalidates the next segment previous hash');
    const alteredLast = Buffer.from(dynamicBytes.toString('utf8').replace('second', 'Second'));
    assert.equal(alteredLast.byteLength, dynamicBytes.byteLength);
    assert.notDeepEqual(alteredLast, dynamicBytes);
    assert.throws(() => materializeStadiumC3ReportCheckpoint(parsed, frozenBytes, alteredLast),
      /count or hash-chain identity mismatch/,
      'changing the terminal payload invalidates the descriptor chain hash');
    const badCounts = JSON.parse(JSON.stringify(parsed));
    badCounts.stadium_c3_persistence_checkpoint.dynamic_records.record_counts.actions += 1;
    assert.throws(() => materializeStadiumC3ReportCheckpoint(badCounts, frozenBytes, dynamicBytes),
      /count or hash-chain identity mismatch/);
    const badHash = JSON.parse(JSON.stringify(parsed));
    badHash.stadium_c3_persistence_checkpoint.dynamic_records.chain_sha256 = '0'.repeat(64);
    assert.throws(() => materializeStadiumC3ReportCheckpoint(badHash, frozenBytes, dynamicBytes),
      /count or hash-chain identity mismatch/);
    const unknownMarker = JSON.parse(JSON.stringify(parsed));
    unknownMarker.stadium_c3_persistence_checkpoint.unrecognized = true;
    assert.throws(() => recoverStadiumC3ReportCheckpoint(unknownMarker, frozenBytes, dynamicBytes),
      /checkpoint descriptor is missing or malformed/,
    'recovery rejects unknown descriptor fields rather than rebuilding a reduced marker');
    const missingDynamicMarker = JSON.parse(JSON.stringify(parsed));
    delete missingDynamicMarker.stadium_c3_persistence_checkpoint.dynamic_records;
    assert.throws(() => materializeStadiumC3ReportCheckpoint(
      missingDynamicMarker, frozenBytes, dynamicBytes),
    /dynamic-record descriptor is missing/,
    'a compact checkpoint cannot fall back to the fixed-only schema after losing its stream descriptor');
    assert.throws(() => recoverStadiumC3ReportCheckpoint(
      missingDynamicMarker, frozenBytes, dynamicBytes),
    /dynamic-record descriptor is missing/,
    'recovery applies the same missing-stream guard as direct materialization');
    await assert.rejects(fs.readFile(`${writer.dynamicRecordPath}.missing`), {code: 'ENOENT'});
  } finally { await removeTemp(dir); }
});

test('actual persistReport accepts append-only matched metadata additions and terminally restores inline history', async () => {
  const dir = await makeTemp('stadium-c3-matched-metadata-');
  try {
    const successDir = path.join(dir, 'success');
    await fs.mkdir(successDir);
    const successReportPath = path.join(successDir, 'report.json');
    const successReport = representativeReport();
    const successWriter = createStadiumC3ReportCheckpoint({reportPath: successReportPath});
    const successPersist = await actualPersistReport({report: successReport,
      reportPath: successReportPath, checkpoint: successWriter, reportFs: fs});
    await successPersist();
    successReport.scenario.matched_setup_cf.actions_completed = 1;
    await successPersist();
    const successCheckpoint = JSON.parse((await fs.readFile(successReportPath)).toString('utf8'));
    const successSidecar = await fs.readFile(successWriter.sidecarPath);
    const successRecords = await fs.readFile(successWriter.dynamicRecordPath);
    assert.deepEqual(materializeStadiumC3ReportCheckpoint(successCheckpoint,
      successSidecar, successRecords), successReport,
    'the successful actions_completed metadata addition remains in the committed checkpoint');
    successReport.execution_state = 'completed';
    await successPersist();
    const completed = JSON.parse((await fs.readFile(successReportPath)).toString('utf8'));
    assert.equal(Object.hasOwn(completed, 'stadium_c3_persistence_checkpoint'), false);
    assert.equal(completed.scenario.matched_setup_cf.actions_completed, 1);
    assert.deepEqual(completed.scenario.matched_setup_cf.actions,
      successReport.scenario.matched_setup_cf.actions);

    const failureDir = path.join(dir, 'failure');
    await fs.mkdir(failureDir);
    const failureReportPath = path.join(failureDir, 'report.json');
    const failureReport = representativeReport();
    const failureWriter = createStadiumC3ReportCheckpoint({reportPath: failureReportPath});
    const failurePersist = await actualPersistReport({report: failureReport,
      reportPath: failureReportPath, checkpoint: failureWriter, reportFs: fs});
    await failurePersist();
    failureReport.scenario.matched_setup_cf.stop_reason = 'bounded-first-failure';
    await failurePersist();
    const failedCheckpoint = JSON.parse((await fs.readFile(failureReportPath)).toString('utf8'));
    const failureSidecar = await fs.readFile(failureWriter.sidecarPath);
    const failureRecords = await fs.readFile(failureWriter.dynamicRecordPath);
    assert.deepEqual(materializeStadiumC3ReportCheckpoint(failedCheckpoint,
      failureSidecar, failureRecords), failureReport,
    'a failure stop_reason appended after startup remains in the checkpoint with exact key order');
    failureReport.execution_state = 'completed';
    failureReport.result = 'fail';
    await failurePersist();
    const finalFailure = JSON.parse((await fs.readFile(failureReportPath)).toString('utf8'));
    assert.equal(Object.hasOwn(finalFailure, 'stadium_c3_persistence_checkpoint'), false);
    assert.equal(finalFailure.scenario.matched_setup_cf.stop_reason, 'bounded-first-failure');
    assert.deepEqual(finalFailure.scenario.matched_setup_cf.actions,
      failureReport.scenario.matched_setup_cf.actions);
  } finally { await removeTemp(dir); }
});

test('frozen mutation, missing sidecar, and corrupt sidecar fail closed', async () => {
  const dir = await makeTemp('stadium-c3-invalid-sidecar-');
  try {
    const reportPath = path.join(dir, 'report.json');
    const writer = createStadiumC3ReportCheckpoint({reportPath});
    const report = representativeReport();
    const initialCheckpoint = await writer.prepare(report);
    writer.commitPrepared(initialCheckpoint);
    assert.equal(Reflect.set(report.scenario, 'first_sss_prefix', {}), false,
      'the selected pre-action root field is immutable after sidecar creation');
    assert.equal(Reflect.set(report.scenario.first_sss_prefix.samples[0], 'value', 'changed'), false,
      'nested selected evidence is immutable after sidecar creation');
    const unchangedCheckpoint = await writer.prepare(report);
    writer.commitPrepared(unchangedCheckpoint);

    const replacedDir = path.join(dir, 'replaced');
    await fs.mkdir(replacedDir);
    const replacedWriter = createStadiumC3ReportCheckpoint({reportPath: path.join(replacedDir, 'report.json')});
    const replacedReport = representativeReport();
    const replacedCheckpoint = await replacedWriter.prepare(replacedReport);
    replacedWriter.commitPrepared(replacedCheckpoint);
    const replacementScenario = JSON.parse(JSON.stringify(replacedReport.scenario));
    replacementScenario.first_sss_prefix.samples[0].value = 'altered';
    replacedReport.scenario = replacementScenario;
    await assert.rejects(replacedWriter.prepare(replacedReport),
      /scenario root identity changed/,
      'recovery rejects a replaced scenario carrying altered frozen roots');

    const missingDir = path.join(dir, 'missing');
    await fs.mkdir(missingDir);
    const missingWriter = createStadiumC3ReportCheckpoint({reportPath: path.join(missingDir, 'report.json')});
    const missingReport = representativeReport();
    await missingWriter.prepare(missingReport);
    await fs.unlink(missingWriter.sidecarPath);
    await assert.rejects(missingWriter.prepare({...missingReport, execution_state: 'completed'},
      {terminal: true}), {code: 'ENOENT'});

    const corruptDir = path.join(dir, 'corrupt');
    await fs.mkdir(corruptDir);
    const corruptWriter = createStadiumC3ReportCheckpoint({reportPath: path.join(corruptDir, 'report.json')});
    const corruptReport = representativeReport();
    const compact = await corruptWriter.prepare(corruptReport);
    await fs.writeFile(corruptWriter.sidecarPath, '{"broken":true}\n');
    await assert.rejects(corruptWriter.prepare({...corruptReport, execution_state: 'completed'},
      {terminal: true}), /sidecar byte identity mismatch/);
    assert.ok(compact.stadium_c3_persistence_checkpoint);
  } finally { await removeTemp(dir); }
});

test('actual persistReport preserves last-good checkpoint and blocks acceptance on failures', async () => {
  const runnerSource = await fs.readFile(RUNNER, 'utf8');
  const timedBefore = runnerSource.indexOf('() => checkRuntimeDiagnostics(scheduleIndex, `${action.action}-before-ack`, cfState)');
  const timedAccept = runnerSource.indexOf('() => driver.accept(cfState, comparison, commonComparison)');
  const untimedBefore = runnerSource.indexOf('await checkRuntimeDiagnostics(scheduleIndex, `${action.action}-before-ack`, cfState)');
  const untimedAccept = runnerSource.indexOf('accepted = await driver.accept(cfState, comparison, commonComparison)');
  for (const [label, index] of Object.entries({timedBefore, timedAccept, untimedBefore, untimedAccept}))
    assert.ok(index >= 0, `actual source marker is present: ${label}`);
  assert.ok(timedBefore < timedAccept,
    'actual timed before-ack diagnostic persistence remains before driver.accept');
  assert.ok(untimedBefore < untimedAccept,
    'actual untimed before-ack diagnostic persistence remains before driver.accept');
  const dir = await makeTemp('stadium-c3-persist-failure-');
  try {
    for (const operation of ['writeFile', 'rename']) {
      const caseDir = path.join(dir, `report-${operation}`);
      await fs.mkdir(caseDir);
      const reportPath = path.join(caseDir, 'report.json');
      const reportFs = makeControllableFileSystem(fs);
      const checkpointFs = makeControllableFileSystem(fs);
      const checkpoint = createStadiumC3ReportCheckpoint({reportPath, fileSystem: checkpointFs});
      const report = representativeReport();
      const events = [];
      const persist = await actualPersistReport({report, reportPath, checkpoint, reportFs, events});
      let accepted = 0;
      await persist();
      accepted += 1;
      assert.deepEqual(events.slice(-2), ['report-rename-complete', 'checkpoint-commit'],
        'the append-only descriptor advances only after the atomic report rename succeeds');
      const previousCheckpointBytes = await fs.readFile(reportPath);
      const previousCheckpoint = JSON.parse(previousCheckpointBytes.toString('utf8'));
      const sidecarBytes = await fs.readFile(checkpoint.sidecarPath);
      const dynamicBytes = await fs.readFile(checkpoint.dynamicRecordPath);
      const lastGood = materializeStadiumC3ReportCheckpoint(previousCheckpoint, sidecarBytes, dynamicBytes);
      const expectedLastGood = JSON.parse(JSON.stringify(report));
      assert.equal(JSON.stringify(lastGood), JSON.stringify(expectedLastGood),
        'the committed checkpoint reconstructs its exact pre-failure report');
      report.scenario.mutable_failure_snapshot = {attempt: 2, action_order: 'unchanged'};
      report.scenario.matched_setup_cf.actions.push({schedule_index: 1, accepted: true});
      report.scenario.matched_setup_cf.runtime_diagnostics.push({schedule_index: 1, message: 'next'});
      report.scenario.matched_setup_cf.host_comparisons.push({label: 'F', decision: 'PASS_EXACT'});
      const eventCountBeforeFailure = events.length;
      const marker = new Error(`injected report ${operation} failure`);
      reportFs.setFailure(operation, marker);
      await assert.rejects((async () => { await persist(); accepted += 1; })(), error => error === marker);
      assert.equal(accepted, 1, 'failed persist must not reach the following acceptance');
      assert.equal(events.length, eventCountBeforeFailure,
        'a failed report replacement does not advance the descriptor commit callback');
      assert.deepEqual(await fs.readFile(reportPath), previousCheckpointBytes,
        'the last complete checkpoint remains byte-identical after failed replacement');
      const failedTemp = `${reportPath}.stadium-c3-checkpoint-000002.tmp`;
      assert.ok((await fs.stat(failedTemp)).size > 0, 'failed checkpoint temporary bytes remain available');
      const journalAfterFailure = await fs.readFile(checkpoint.dynamicRecordPath);
      assert.throws(() => materializeStadiumC3ReportCheckpoint(
        previousCheckpoint, sidecarBytes, journalAfterFailure), /uncommitted tail/,
      'strict checkpoint admission refuses bytes beyond its committed descriptor');
      const recovery = recoverStadiumC3ReportCheckpoint(previousCheckpoint, sidecarBytes, journalAfterFailure);
      assert.equal(JSON.stringify(recovery.report), JSON.stringify(expectedLastGood),
        'the previous checkpoint plus its committed journal prefix reconstructs the last-good report');
      assert.ok(recovery.uncommittedDynamicTail?.byte_length > 0,
        'the append preceding a failed report replacement is explicitly reported as an uncommitted tail');
      assert.equal(recovery.uncommittedDynamicTail.sha256,
        crypto.createHash('sha256').update(journalAfterFailure.subarray(
          previousCheckpoint.stadium_c3_persistence_checkpoint.dynamic_records.byte_length)).digest('hex'));
      assert.equal(recovery.uncommittedDynamicTail.basename,
        previousCheckpoint.stadium_c3_persistence_checkpoint.dynamic_records.basename);
      assert.throws(() => materializeStadiumC3ReportCheckpoint(
        previousCheckpoint, sidecarBytes, journalAfterFailure), /uncommitted tail/,
      'strict materialization refuses the tail rather than silently discarding it');
      assert.ok(lastGood, 'strict materialization succeeded before the journal tail was appended');
    }

    {
      const caseDir = path.join(dir, 'appendFile');
      await fs.mkdir(caseDir);
      const reportPath = path.join(caseDir, 'report.json');
      const reportFs = makeControllableFileSystem(fs);
      const checkpointFs = makeControllableFileSystem(fs);
      const checkpoint = createStadiumC3ReportCheckpoint({reportPath, fileSystem: checkpointFs});
      const report = representativeReport();
      const persist = await actualPersistReport({report, reportPath, checkpoint, reportFs});
      await persist();
      const previousBytes = await fs.readFile(reportPath);
      const previous = JSON.parse(previousBytes.toString('utf8'));
      const frozenBytes = await fs.readFile(checkpoint.sidecarPath);
      const previousDynamicBytes = await fs.readFile(checkpoint.dynamicRecordPath);
      const expected = materializeStadiumC3ReportCheckpoint(previous, frozenBytes, previousDynamicBytes);
      report.scenario.matched_setup_cf.actions.push({schedule_index: 1, accepted: true});
      const marker = new Error('injected dynamic append failure');
      checkpointFs.setFailure('appendFile', marker);
      let approvals = 0;
      const events = [];
      const persistWithEvents = await actualPersistReport({report, reportPath, checkpoint, reportFs, events});
      // The first persisted checkpoint has already committed; the next attempt fails in prepare.
      await assert.rejects((async () => { await persistWithEvents(); approvals += 1; })(), error => error === marker);
      assert.equal(approvals, 0, 'a failed journal append must stop before any following approval');
      assert.equal(events.length, 0, 'an append failure cannot rename or commit a new report checkpoint');
      assert.deepEqual(await fs.readFile(reportPath), previousBytes,
        'append failure leaves the prior atomic report target unchanged');
      const journalWithPartialTail = await fs.readFile(checkpoint.dynamicRecordPath);
      const recovery = recoverStadiumC3ReportCheckpoint(previous, frozenBytes, journalWithPartialTail);
      assert.deepEqual(recovery.report, expected, 'the prior committed checkpoint remains recoverable');
      assert.ok(recovery.uncommittedDynamicTail?.byte_length > 0,
        'partial append bytes remain present and are explicitly surfaced as an uncommitted tail');
      assert.ok(journalWithPartialTail.byteLength > previousDynamicBytes.byteLength,
        'failed append does not truncate or delete the retained journal tail');
    }

    for (const operation of ['writeFile', 'rename']) {
      const caseDir = path.join(dir, `sidecar-${operation}`);
      await fs.mkdir(caseDir);
      const reportPath = path.join(caseDir, 'report.json');
      const previousFull = Buffer.from(`${JSON.stringify(representativeReport())}\n`);
      await fs.writeFile(reportPath, previousFull);
      const marker = new Error(`injected sidecar ${operation} failure`);
      const checkpointFs = makeFileSystemFailure(fs, operation, marker);
      const checkpoint = createStadiumC3ReportCheckpoint({reportPath, fileSystem: checkpointFs});
      const persist = await actualPersistReport({report: representativeReport(), reportPath,
        checkpoint, reportFs: fs});
      let accepted = 0;
      await assert.rejects((async () => { await persist(); accepted += 1; })(), error => error === marker);
      assert.equal(accepted, 0, 'failed sidecar persistence must precede and prevent acceptance');
      assert.deepEqual(await fs.readFile(reportPath), previousFull,
        'failed sidecar publication preserves the preceding self-contained report');
      const failedTemp = `${checkpoint.sidecarPath}.tmp`;
      assert.ok((await fs.stat(failedTemp)).size > 0, 'failed sidecar temporary bytes remain available');
      await assert.rejects(fs.access(checkpoint.sidecarPath), {code: 'ENOENT'});
    }
  } finally { await removeTemp(dir); }
});

test('retained successful prefix report materializes byte-for-byte', async t => {
  const retained = process.env.STADIUM_C3_RETAINED_REPORT;
  if (!retained) return t.skip('set STADIUM_C3_RETAINED_REPORT for the retained-report control');
  const sourceBytes = await fs.readFile(retained);
  const report = JSON.parse(sourceBytes.toString('utf8'));
  assert.ok(STADIUM_C3_FROZEN_SCENARIO_FIELDS.every(key =>
    Object.hasOwn(report.scenario, key)), 'retained report contains all enumerated frozen fields');
  const dir = await makeTemp('stadium-c3-retained-report-');
  try {
    const reportPath = path.join(dir, 'report.json');
    const writer = createStadiumC3ReportCheckpoint({reportPath});
    const materialized = await writer.prepare(report, {terminal: true});
    const expected = JSON.stringify(report) + '\n';
    const actual = JSON.stringify(materialized) + '\n';
    assert.equal(expected, sourceBytes.toString('utf8'),
      'retained report bytes are the runner’s canonical JSON plus newline');
    assert.equal(actual, sourceBytes.toString('utf8'),
      'terminal materialization reproduces the retained report byte-for-byte');
    assert.deepEqual(materialized, report);
  } finally { await removeTemp(dir); }
});

test('actual capture owner reader accepts compact checkpoint process attribution', async t => {
  if (!OWNER_READER) return t.skip('set MELEE_STADIUM_OWNER_READER for owner-reader integration control');
  const report = representativeReport();
  const dir = await makeTemp('stadium-c3-owner-reader-');
  try {
    const reportPath = path.join(dir, 'report.json');
    const writer = createStadiumC3ReportCheckpoint({reportPath});
    const checkpoint = await writer.prepare(report);
    await fs.writeFile(reportPath, `${JSON.stringify(checkpoint)}\n`);
    const program = [
      'import json, sys',
      'from pathlib import Path',
      `sys.path.insert(0, ${JSON.stringify(path.dirname(OWNER_READER))})`,
      'from capture_owner_deadline import read_capture_report',
      'out = {}',
      'pids = read_capture_report(Path(sys.argv[1]), out)',
      'assert out["capture_report_available"] is True',
      'assert out["cdp_browser_process_info"][0]["id"] == 42',
      'assert out["cdp_browser_process_groups"][0]["pgid"] == 41',
      'assert out["capture_browser_cleanup"]["pids"][0]["pid"] == 41',
      'assert out["capture_browser_cleanup"]["groups"][0]["pgid"] == 41',
      'assert pids == [42]',
      'print(json.dumps({"readable": True, "cdp_pids": pids}))',
    ].join('\n');
    const child = await import('node:child_process');
    const result = child.spawnSync(process.env.PYTHON || 'python3', ['-c', program, reportPath],
      {encoding: 'utf8', timeout: 10000});
    assert.equal(result.status, 0, result.stdout + result.stderr);
    assert.match(result.stdout, /"readable": true/);
  } finally { await removeTemp(dir); }
});

function fresh132FullAReport() {
  const report = representativeReport();
  Object.assign(report.scenario.matched_setup_cf, {mode: 'fresh132-full-a-v1',
    schedule_actions: 7168, common_field_count: 15, seed_value_count: 1});
  return report;
}

test('Fresh132 full-A checkpoint eligibility is exact and legacy7167 stays separate', () => {
  const report = fresh132FullAReport();
  assert.equal(isStadiumC3ReportCheckpointEligible(report), true);
  for (const [key, value] of [['mode', 'fresh132-full-a-v2'], ['schedule_actions', 7167],
    ['schedule_actions', 7169], ['common_field_count', 14], ['seed_value_count', 0],
    ['explicit_step_mode', false]]) {
    const invalid = structuredClone(report);
    invalid.scenario.matched_setup_cf[key] = value;
    assert.equal(isStadiumC3ReportCheckpointEligible(invalid), false, key);
  }
  for (const mode of ['run2-full-schedule-v3', 'run2-full-common-fields-seed-v1']) {
    const legacy = fresh132FullAReport();
    Object.assign(legacy.scenario.matched_setup_cf, {mode, schedule_actions: 7167});
    assert.equal(isStadiumC3ReportCheckpointEligible(legacy), true);
    legacy.scenario.matched_setup_cf.schedule_actions = 7168;
    assert.equal(isStadiumC3ReportCheckpointEligible(legacy), false);
  }
});

test('Fresh132 journal append and checkpoint rename failure stop before acceptance', async () => {
  const dir = await makeTemp('stadium-c3-fresh132-failure-');
  try {
    for (const operation of ['appendFile', 'rename']) {
      const caseDir = path.join(dir, operation);
      await fs.mkdir(caseDir);
      const reportPath = path.join(caseDir, 'report.json');
      const reportFs = makeControllableFileSystem(fs);
      const checkpointFs = makeControllableFileSystem(fs);
      const checkpoint = createStadiumC3ReportCheckpoint({reportPath, fileSystem: checkpointFs});
      const report = fresh132FullAReport();
      const persist = await actualPersistReport({report, reportPath, checkpoint, reportFs});
      await persist();
      const previousBytes = await fs.readFile(reportPath);
      const previous = JSON.parse(previousBytes);
      const frozen = await fs.readFile(checkpoint.sidecarPath);
      const priorJournal = await fs.readFile(checkpoint.dynamicRecordPath);
      const lastGood = materializeStadiumC3ReportCheckpoint(previous, frozen, priorJournal);
      report.scenario.matched_setup_cf.actions.push({schedule_index: 1, accepted: true});
      const marker = new Error(`Fresh132 injected ${operation}`);
      (operation === 'appendFile' ? checkpointFs : reportFs).setFailure(operation, marker);
      let approvals = 0;
      await assert.rejects((async () => { await persist(); ++approvals; })(), error => error === marker);
      assert.equal(approvals, 0);
      assert.deepEqual(await fs.readFile(reportPath), previousBytes);
      const journal = await fs.readFile(checkpoint.dynamicRecordPath);
      assert.throws(() => materializeStadiumC3ReportCheckpoint(previous, frozen, journal), /uncommitted tail/);
      const recovery = recoverStadiumC3ReportCheckpoint(previous, frozen, journal);
      assert.deepEqual(recovery.report, lastGood);
      assert.ok(recovery.uncommittedDynamicTail.byte_length > 0);
    }
  } finally { await removeTemp(dir); }
});
