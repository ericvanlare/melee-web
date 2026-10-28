// SPDX-License-Identifier: GPL-3.0-or-later
// Mock-only orchestration controls. Passing these tests is not gameplay evidence.
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {runTimelineWitness} from './run_input_timeline.mjs';

const PROFILE = 'slippi-render-dependencies-dbooc-shield-drop-v5';
const SEED = 0x13579bdf;
const OBSERVATION = 0;
const PCM = 2048;
const PAD = 7000;
const DRAW = 8000;
const SAMPLE = 9000;
const SOURCE_SEED = 9004;
const MATCH_FRAME = 9008;
const TRANSFER = 10000;

function mockFixture({ignoreRemote = false, tamperCorrection = false,
  tamperCorrectedHeap = false, initialFrame = 0, countdownStallSamples = 16,
  skipAtSample = -1, postAdmissionClockFault = null, postAdmissionFaultRawCall = 1,
  stackChangeAt = 0, closeResult = 1, transientBusySample = -1, busyBoundary = -1} = {}) {
  const heap = new Uint8Array(64 * 1024);
  const view = new DataView(heap.buffer);
  let stackCalls = 0;
  let stepCalls = 0;
  let quiescenceCalls = 0;
  let currentCallerSample = -1;
  const fixedSamples = [];
  let rawSample300Calls = 0;
  let rawSample306Calls = 0;
  let currentSeed = SEED;
  let initialized = false;
  const putPad = pads => heap.set(pads, PAD);
  const padFor = sample => {
    const pads = new Uint8Array(44);
    pads[10] = 0; pads[21] = 0; pads[32] = 0xff; pads[43] = 0xff;
    if (sample >= 300) {
      const phase = (sample - 300) % 120;
      pads[2] = phase < 60 ? 80 : 176;
      pads[13] = phase < 60 ? 176 : 80;
      if (phase === 0 || phase === 40) {
        pads[1] = 2; pads[12] = 1;
      }
      if (phase === 20) { pads[1] = 4; pads[12] = 4; }
    }
    return pads;
  };
  const publish = (sample, matchFrame, preTickMatchFrame = matchFrame) => {
    currentCallerSample = sample;
    const transfer = new DataView(heap.buffer, TRANSFER, 40);
    transfer.setUint32(0, preTickMatchFrame, true);
    for (let phase = 0; phase < 3; ++phase) {
      transfer.setInt32(4 + phase * 12, sample === transientBusySample ? 1 : 0, true);
      transfer.setInt32(8 + phase * 12, sample === transientBusySample ? 1 : 0, true);
      transfer.setInt32(12 + phase * 12, 0, true);
    }
    const pads = heap.slice(PAD, PAD + 44);
    heap.fill((sample + 1) % 251, PCM, PCM + 4272);
    const bytes = heap.subarray(OBSERVATION, OBSERVATION + 1280);
    bytes.fill(0);
    const observation = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    observation.setUint32(0, matchFrame, true);
    observation.setUint32(4, currentSeed, true);
    observation.setUint32(8, 1, true);
    for (let player = 0; player < 2; ++player) {
      const base = 72 + player * 192;
      const remoteButton = pads[12] !== 0 || pads[13] !== 0;
      const motion = !ignoreRemote && player === 1 && remoteButton ? 201 : 20;
      observation.setInt32(base + 20, motion, true);
      observation.setUint32(base + 136, player, true);
      observation.setInt32(base + 148, 4, true);
      observation.setInt32(base + 152, 42, true);
    }
    if (tamperCorrection && sample === 300 && rawSample300Calls === 3)
      heap[PAD + 2] ^= 1;
  };
  const module = {
    HEAPU8: heap,
    stackSave() {
      ++stackCalls;
      return stackChangeAt === stackCalls ? 17 : 16;
    },
    stackRestore() {},
    UTF8ToString(pointer) {
      if (pointer === 1) return 'mock fixture error';
      if (pointer === 2) return PROFILE;
      if (pointer === 3) return 'mock-source-hash';
      if (pointer === 4) return JSON.stringify({profile: 0, seed: SEED,
        players: [{port: 0, controller: 0, stocks: 4}, {port: 0, controller: 1, stocks: 4}]});
      return '';
    },
    ccall(name) {
      if (name === 'melee_web_snapshot_init') {
        initialized = true;
        currentSeed = view.getUint32(SOURCE_SEED, true);
        view.setUint32(SAMPLE, 0, true);
        view.setUint32(MATCH_FRAME, initialFrame, true);
        putPad(padFor(0));
        publish(0, initialFrame);
      }
      return 1;
    },
    _melee_web_snapshot_configure_seed(seed) {
      currentSeed = seed;
      view.setUint32(SOURCE_SEED, seed, true);
      return 1;
    },
    _melee_web_snapshot_error: () => 1,
    _melee_web_snapshot_initializer_json: () => 4,
    _melee_web_snapshot_source_identity: () => 3,
    _melee_web_synctest_profile_identity: () => 2,
    _melee_web_synctest_motion_boundary: () => 0,
    _melee_web_synctest_is_mario_kind: kind => kind === 42 ? 1 : 0,
    _melee_web_synctest_draw_decision: () => 0,
    _melee_web_synctest_draw_diagnostics_size: () => 132,
    _melee_web_synctest_draw_diagnostics: () => DRAW,
    _melee_web_snapshot_observation_size: () => 1280,
    _melee_web_snapshot_observation: () => OBSERVATION,
    _melee_web_snapshot_pcm_size: () => 4272,
    _melee_web_snapshot_pcm: () => PCM,
    _melee_web_snapshot_input: () => PAD,
    _melee_web_snapshot_transfer_diagnostic_size: () => 40,
    _melee_web_snapshot_transfer_diagnostic: () => TRANSFER,
    _melee_web_snapshot_quiescent() {
      ++quiescenceCalls;
      return quiescenceCalls === busyBoundary ||
        currentCallerSample === transientBusySample ? 0 : 1;
    },
    _melee_web_snapshot_step(sample) {
      ++stepCalls;
      fixedSamples.push(sample);
      putPad(padFor(sample));
      view.setUint32(SAMPLE, sample + 1, true);
      const previousFrame = view.getUint32(MATCH_FRAME, true);
      let matchFrame = previousFrame;
      if (sample >= countdownStallSamples || sample >= 300) ++matchFrame;
      if (sample === skipAtSample && matchFrame > previousFrame) ++matchFrame;
      view.setUint32(MATCH_FRAME, matchFrame, true);
      publish(sample, matchFrame, previousFrame);
      return 1;
    },
    _melee_web_snapshot_step_raw() {
      ++stepCalls;
      const sample = view.getUint32(SAMPLE, true);
      if (sample === 300) ++rawSample300Calls;
      if (sample === 306) ++rawSample306Calls;
      view.setUint32(SAMPLE, sample + 1, true);
      const previousFrame = view.getUint32(MATCH_FRAME, true);
      let matchFrame = previousFrame;
      if (sample >= countdownStallSamples || sample >= 300) ++matchFrame;
      if (sample === skipAtSample && matchFrame > previousFrame) ++matchFrame;
      if (sample === 300 && rawSample300Calls === postAdmissionFaultRawCall && postAdmissionClockFault === 'skip')
        ++matchFrame;
      if (sample === 300 && rawSample300Calls === postAdmissionFaultRawCall && postAdmissionClockFault === 'repeat')
        matchFrame = previousFrame;
      view.setUint32(MATCH_FRAME, matchFrame, true);
      publish(sample, matchFrame, previousFrame);
      if (tamperCorrectedHeap && sample === 306 && rawSample306Calls === 3)
        heap[12000] ^= 1;
      return 1;
    },
    _melee_web_snapshot_close: () => closeResult,
  };
  const diagnostic = new DataView(heap.buffer, DRAW, 132);
  diagnostic.setUint32(0, 6, true);
  return {module, fixedSamples, get stepCalls() { return stepCalls; },
    get quiescenceCalls() { return quiescenceCalls; }, get initialized() { return initialized; }};
}

function fixtureReport() {
  return {assetRoot: '/fixture-assets',
    sourceHashes: {'tests/gameplay_snapshot_probe.cpp': 'mock-source-hash'}};
}

async function executeCapture(options = {}) {
  const fixture = mockFixture(options);
  const report = fixtureReport();
  const writes = [];
  let error = null;
  try { await runTimelineWitness(fixture.module, report, () => writes.push(JSON.stringify(report))); }
  catch (caught) { error = caught; }
  return {fixture, report, writes, error};
}

async function execute(options = {}) {
  const outcome = await executeCapture(options);
  if (outcome.error) throw outcome.error;
  return outcome;
}

test('mock control path proves seven raw-tick prediction and corrective replay', async () => {
  const {fixture, report, writes} = await execute();
  assert.equal(fixture.initialized, true);
  assert.equal(report.result, 'passed');
  assert.equal(report.observationClock.field, 'Observation.frame at byte offset 0');
  assert.equal(report.observationClock.schedulerGetterAvailable, false);
  assert.equal(report.fixture.initialMatchFrame, 0);
  assert.equal(report.prefix.matchFrameClock.stalledInputRanges[0].firstInputSample, 0);
  assert.equal(report.prefix.matchFrameClock.stalledInputRanges[0].lastInputSample, 15);
  assert.equal(report.prefix.matchFrameClock.observedTransitions[0].firstInputSample, 16);
  assert.equal(report.prefix.checkpointMatchFrame, 284);
  const expectedFrames = Array.from({length: 7}, (_, index) => 285 + index);
  assert.equal(report.inputGeneration.ticks.length, 7);
  assert.deepEqual(report.inputGeneration.ticks.map(row => row.matchFrame), expectedFrames);
  assert.equal(report.authoritative.ticks.length, 7);
  assert.deepEqual(report.authoritative.ticks.map(row => row.matchFrame), expectedFrames);
  assert.equal(report.predicted.ticks.length, 7);
  assert.deepEqual(report.predicted.ticks.map(row => row.matchFrame), expectedFrames);
  assert.equal(report.predictionCapGuard.sample, 307);
  assert.equal(report.predictionCapGuard.observedMatchFrame, 292);
  assert.equal(report.predicted.predictionLimit.reason, 'prediction_limit');
  assert.equal(report.predicted.negativeControl.testedField,
    'remote fighter players[].motion from inspectInteractiveObservation');
  assert.equal(report.predicted.negativeControl.firstFighterStateDivergenceTick, 300);
  assert.equal(report.correction.ticks.length, 7);
  assert.deepEqual(report.correction.ticks.map(row => row.matchFrame), expectedFrames);
  assert.equal(report.correction.endpointHeapSha256, report.authoritative.endpointHeapSha256);
  assert.equal(report.cleanup.fixtureClosed, true);
  assert.ok(writes.length >= 8);
});

test('mock negative control fails when withheld remote input changes no fighter state', async () => {
  await assert.rejects(execute({ignoreRemote: true}), /did not change the declared remote fighter motion field/);
});

test('mock corrected replay rejects a changed source PAD or observation lane', async () => {
  await assert.rejects(execute({tamperCorrection: true}), /Corrected source lane differs from authoritative sample 300/);
});

test('mock restoration and cleanup failures remain terminal', async () => {
  await assert.rejects(execute({stackChangeAt: 2}), /Wasm stack is not at its captured boundary/);
  await assert.rejects(execute({closeResult: 0}), /Cleanup failed: source fixture close/);
});

test('mock rejects a nonzero initial match frame and a prefix jump while allowing a countdown stall', async () => {
  await assert.rejects(execute({initialFrame: 1}), /Initial VS match frame must be zero/);
  await assert.rejects(execute({skipAtSample: 19}), /VS match frame regressed or jumped during startup prefix at input sample 19/);
});

test('mock rejects skipped or repeated match frames after interactive checkpoint admission', async () => {
  await assert.rejects(execute({postAdmissionClockFault: 'skip'}),
    /VS match frame did not advance exactly once at input sample 300 \(authoritative\): expected=285, actual=286/);
  await assert.rejects(execute({postAdmissionClockFault: 'repeat', postAdmissionFaultRawCall: 1}),
    /VS match frame did not advance exactly once at input sample 300 \(authoritative\): expected=285, actual=284/);
  await assert.rejects(execute({postAdmissionClockFault: 'skip', postAdmissionFaultRawCall: 2}),
    /VS match frame did not advance exactly once at input sample 300 \(predicted\): expected=285, actual=286/);
  await assert.rejects(execute({postAdmissionClockFault: 'repeat', postAdmissionFaultRawCall: 3}),
    /VS match frame did not advance exactly once at input sample 300 \(corrected-replay\): expected=285, actual=284/);
});

test('mock endpoint heap mismatch retains a bounded first-byte context', async () => {
  const {error, report} = await executeCapture({tamperCorrectedHeap: true});
  assert.match(error.message, /Corrected endpoint heap differs/);
  assert.equal(report.correction.endpointHeapDifference.offset, 12000);
  assert.equal(report.correction.endpointHeapDifference.contextBytes, 17);
  assert.equal(Buffer.from(report.correction.endpointHeapDifference.expectedContextHex, 'hex').length, 17);
  assert.equal(Buffer.from(report.correction.endpointHeapDifference.actualContextHex, 'hex').length, 17);
});


test('mock allows transient startup transfers without extra steps or draining', async () => {
  const {fixture, report} = await execute({transientBusySample: 107});
  assert.equal(fixture.stepCalls, 329);
  assert.deepEqual(fixture.fixedSamples.slice(0, 300), Array.from({length: 300}, (_, i) => i));
  assert.equal(fixture.quiescenceCalls, 8);
  assert.equal(report.startupTransferSample107.preTickMatchFrame, 91);
  assert.notEqual(report.startupTransferSample107.preTickMatchFrame, 107);
  assert.deepEqual(report.startupTransferSample107.afterAudio, {drive: 1, source: 1, bank: 0});
  assert.ok(report.quiescenceBoundaries.every(row => row.admitted));
});

test('mock refuses busy capture, restore and accepted endpoint boundaries', async () => {
  const labels = ['Initial sample0', 'Checkpoint sample299', 'Restore before authoritative lane',
    'Authoritative endpoint', 'Restore before predicted lane', 'Predicted endpoint',
    'Restore before correction replay', 'Corrected endpoint'];
  for (let index = 0; index < labels.length; ++index) {
    const {error, fixture, report} = await executeCapture({busyBoundary: index + 1});
    assert.match(error.message, new RegExp(labels[index]));
    assert.equal(report.quiescenceBoundaries.at(-1).admitted, false);
    assert.equal(report.cleanup.fixtureClosed, true);
    assert.equal(report.cleanup.snapshotOwnerClosed, true);
    if (index === 1) {
      assert.equal(report.inputGeneration, undefined);
      assert.equal(fixture.stepCalls, 300);
    }
  }
});
