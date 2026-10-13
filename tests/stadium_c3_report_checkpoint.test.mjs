import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {createStadiumC3ReportCheckpoint, isStadiumC3ReportCheckpointEligible,
  materializeStadiumC3ReportCheckpoint, STADIUM_C3_FROZEN_SCENARIO_FIELDS,
  STADIUM_C3_REPORT_CHECKPOINT_SCHEMA} from './stadium_c3_report_checkpoint.mjs';

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
      actions: [{schedule_index: 0, accepted: true}]},
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

async function actualPersistReport({report, reportPath, checkpoint, reportFs}) {
  const source = await fs.readFile(RUNNER, 'utf8');
  const context = {
    report, inventoryOutput: reportPath, stadiumC3ReportCheckpoint: checkpoint,
    matchedSetupCfTiming: null, fs: reportFs, Buffer, JSON, Error,
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
    const persistedCheckpoint = JSON.parse(`${JSON.stringify(checkpoint)}\n`);
    const recovered = materializeStadiumC3ReportCheckpoint(persistedCheckpoint, sidecarBytes);
    assert.equal(JSON.stringify(recovered) + '\n', JSON.stringify(report) + '\n',
      'a parsed last-good checkpoint reconstructs the exact in-memory report without advancing it');
    assert.equal(recovered.scenario.matched_setup_cf.actions.length,
      report.scenario.matched_setup_cf.actions.length,
      'checkpoint recovery preserves action/approval state as recorded');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint),
      /sidecar bytes are missing or invalid/,
      'recovery rejects a missing original sidecar byte payload');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint, Buffer.alloc(0)),
      /sidecar byte identity mismatch/,
      'recovery rejects sidecar bytes with the wrong byte length');
    const wrongPayload = JSON.parse(sidecarBytes.toString('utf8'));
    wrongPayload.scenario_fields.first_css_browser_stream.samples[0].state = 'CSS';
    const sameLengthWrongBytes = Buffer.from(`${JSON.stringify(wrongPayload)}\n`);
    assert.equal(sameLengthWrongBytes.byteLength, sidecarBytes.byteLength,
      'wrong-payload control preserves exact sidecar byte length');
    assert.deepEqual(Object.keys(wrongPayload.scenario_fields), Object.keys(
      JSON.parse(sidecarBytes.toString('utf8')).scenario_fields),
      'wrong-payload control keeps the exact sidecar field shape');
    assert.throws(() => materializeStadiumC3ReportCheckpoint(persistedCheckpoint, sameLengthWrongBytes),
      /sidecar byte identity mismatch/,
      'recovery rejects same-shaped, same-length sidecar bytes with a different value');
    const finalReport = await checkpointWriter.prepare({...report, execution_state: 'completed'},
      {terminal: true});
    assert.deepEqual(Object.keys(finalReport), Object.keys(report));
    assert.deepEqual(Object.keys(finalReport.scenario), Object.keys(report.scenario));
    assert.equal(JSON.stringify(finalReport) + '\n',
      JSON.stringify({...report, execution_state: 'completed'}) + '\n');
    assert.equal(Object.hasOwn(finalReport, 'stadium_c3_persistence_checkpoint'), false);
  } finally { await removeTemp(dir); }
});

test('frozen mutation, missing sidecar, and corrupt sidecar fail closed', async () => {
  const dir = await makeTemp('stadium-c3-invalid-sidecar-');
  try {
    const reportPath = path.join(dir, 'report.json');
    const writer = createStadiumC3ReportCheckpoint({reportPath});
    const report = representativeReport();
    await writer.prepare(report);
    assert.equal(Reflect.set(report.scenario, 'first_sss_prefix', {}), false,
      'the selected pre-action root field is immutable after sidecar creation');
    assert.equal(Reflect.set(report.scenario.first_sss_prefix.samples[0], 'value', 'changed'), false,
      'nested selected evidence is immutable after sidecar creation');
    await writer.prepare(report);

    const replacedDir = path.join(dir, 'replaced');
    await fs.mkdir(replacedDir);
    const replacedWriter = createStadiumC3ReportCheckpoint({reportPath: path.join(replacedDir, 'report.json')});
    const replacedReport = representativeReport();
    await replacedWriter.prepare(replacedReport);
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
  assert.ok(runnerSource.indexOf('await checkRuntimeDiagnostics(scheduleIndex, `${action.action}-before-ack`, cfState)') <
    runnerSource.indexOf('() => driver.accept(cfState, comparison)'),
  'actual before-ack diagnostic persistence remains before driver.accept');
  const dir = await makeTemp('stadium-c3-persist-failure-');
  try {
    for (const operation of ['writeFile', 'rename']) {
      const caseDir = path.join(dir, `report-${operation}`);
      await fs.mkdir(caseDir);
      const reportPath = path.join(caseDir, 'report.json');
      const reportFs = makeControllableFileSystem(fs);
      const checkpoint = createStadiumC3ReportCheckpoint({reportPath});
      const report = representativeReport();
      const persist = await actualPersistReport({report, reportPath, checkpoint, reportFs});
      let accepted = 0;
      await persist();
      accepted += 1;
      const previousCheckpointBytes = await fs.readFile(reportPath);
      const previousCheckpoint = JSON.parse(previousCheckpointBytes.toString('utf8'));
      const sidecarBytes = await fs.readFile(checkpoint.sidecarPath);
      const lastGood = materializeStadiumC3ReportCheckpoint(previousCheckpoint, sidecarBytes);
      const expectedLastGood = JSON.parse(JSON.stringify(report));
      report.scenario.mutable_failure_snapshot = {attempt: 2, action_order: 'unchanged'};
      const marker = new Error(`injected report ${operation} failure`);
      reportFs.setFailure(operation, marker);
      await assert.rejects((async () => { await persist(); accepted += 1; })(), error => error === marker);
      assert.equal(accepted, 1, 'failed persist must not reach the following acceptance');
      assert.deepEqual(await fs.readFile(reportPath), previousCheckpointBytes,
        'the last complete checkpoint remains byte-identical after failed replacement');
      const failedTemp = `${reportPath}.stadium-c3-checkpoint-000002.tmp`;
      assert.ok((await fs.stat(failedTemp)).size > 0, 'failed checkpoint temporary bytes remain available');
      assert.equal(JSON.stringify(lastGood), JSON.stringify(expectedLastGood),
        'the previous checkpoint plus immutable sidecar still reconstructs its last-good state');
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
