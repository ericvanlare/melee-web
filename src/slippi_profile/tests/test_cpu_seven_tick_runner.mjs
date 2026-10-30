// SPDX-License-Identifier: GPL-3.0-or-later
// Focused component controls: these modeled pages exercise runner contracts,
// not a browser, GPU, or complete source-game session.
import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {
  CPU_SEVEN_TICK_EVIDENCE_BYTES, CPU_SEVEN_TICK_PAIR_BYTES,
  CPU_SEVEN_TICK_PAD44_CORPUS, CPU_SEVEN_TICK_INITIAL_REMOTE_PAD,
  createCpuSevenTickTimeline, planCpuSevenTickForward,
  acknowledgeCpuSevenTickForward, decodeCpuSevenTickEvidence,
  runBrowserCpuSevenTick,
} from './cpu_seven_tick_runner.mjs';
import {decodeDrawDiagnostics, inspectInteractiveObservation,
  validateSelectedDrawPath, validateSourceInputMapping} from './runner_contract.mjs';

const b64 = bytes => Buffer.from(bytes).toString('base64');
const from64 = value => Buffer.from(value, 'base64');
const PAD_BYTES = 11;
const mapping = [
  {sourceSlot: 0, initializerPort: 0, rawController: 0},
  {sourceSlot: 1, initializerPort: 0, rawController: 1},
];

function observation(frame) {
  const bytes = new Uint8Array(1280), view = new DataView(bytes.buffer);
  view.setUint32(0, frame, true); view.setUint32(8, 1, true);
  for (let player = 0; player < 2; player++) {
    const base = 72 + player * 192;
    view.setInt32(base + 20, 0, true);
    view.setUint32(base + 136, player, true);
    view.setInt32(base + 148, 4, true);
    view.setInt32(base + 152, 1, true);
  }
  return bytes;
}

function diagnostics({drawn = false} = {}) {
  const bytes = new Uint8Array(132), view = new DataView(bytes.buffer);
  view.setUint32(0, 6, true);
  if (drawn) {
    for (const offset of [8, 32, 56]) {
      view.setUint32(offset, 1, true); view.setUint32(offset + 4, 1, true);
    }
    view.setInt32(80, 0, true); view.setInt32(84, 1, true);
    view.setInt32(104, 1, true); view.setInt32(108, 1, true);
    view.setUint32(128, 3, true);
  }
  return bytes;
}

function createNativeModel() {
  const heap = new Uint8Array(180000), evidencePtr = 10000, diagPtr = 8000;
  const observationPtr = 100, inputPtr = 2000, pcmPtr = 3000;
  const evidence = heap.subarray(evidencePtr, evidencePtr + CPU_SEVEN_TICK_EVIDENCE_BYTES);
  const write32 = (bytes, offset, value) => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).setUint32(offset, value, true);
  const liveObservation = observation(300), initialPad = new Uint8Array(44);
  initialPad[32] = 0xff; initialPad[43] = 0xff;
  heap.set(liveObservation, observationPtr); heap.set(initialPad, inputPtr);
  heap.set(diagnostics(), diagPtr); heap.fill(0x5a, pcmPtr, pcmPtr + 4272);
  const setEvidenceHeader = ({state, lastCommand, diagnosticMask = 1, baselineMask = 0}) => {
    evidence.fill(0); write32(evidence, 0, 1); write32(evidence, 4, CPU_SEVEN_TICK_EVIDENCE_BYTES);
    write32(evidence, 8, 7); write32(evidence, 12, state); write32(evidence, 20, 300);
    write32(evidence, 24, 307); write32(evidence, 56, diagnosticMask);
    write32(evidence, 60, lastCommand); write32(evidence, 64, baselineMask);
    heap.set(heap.subarray(diagPtr, diagPtr + 132), evidencePtr + 91400);
  };
  const model = {
    HEAPU8: heap,
    _malloc: () => 150000, _free: () => {},
    _melee_web_snapshot_observation_size: () => 1280,
    _melee_web_snapshot_pcm_size: () => 4272,
    _melee_web_synctest_draw_diagnostics_size: () => 132,
    _melee_web_synctest_cpu_seven_tick_evidence_bytes: () => CPU_SEVEN_TICK_EVIDENCE_BYTES,
    _melee_web_synctest_cpu_seven_tick_evidence: () => evidencePtr,
    _melee_web_snapshot_observation: () => observationPtr,
    _melee_web_snapshot_input: () => inputPtr,
    _melee_web_snapshot_pcm: () => pcmPtr,
    _melee_web_synctest_draw_diagnostics: () => diagPtr,
    _melee_web_synctest_motion_boundary: () => 0,
    _melee_web_synctest_is_mario_kind: kind => kind === 1 ? 1 : 0,
    _melee_web_snapshot_error: () => 1,
    _melee_web_synctest_submission_json: () => 2,
    UTF8ToString: pointer => pointer === 2 ? JSON.stringify({rendererErrorCount: 0,
      pendingFramePackets: 0, pendingStagingBuffers: 0, workerBusy: 0,
      drawCallCount: 1, lastBeginFrameId: 11, lastEndFrameId: 11}) : 'modeled refusal',
    _melee_web_synctest_cpu_restore_configure: () => 0,
    async ccall(_name, _returnType, _argTypes, args) {
      const command = args[0];
      if (command === 14) { setEvidenceHeader({state: 14, lastCommand: 14}); return 0; }
      if (command === 15) {
        setEvidenceHeader({state: 15, lastCommand: 15, baselineMask: 1});
        const pair = evidence.subarray(72, 72 + CPU_SEVEN_TICK_PAIR_BYTES);
        write32(pair, 0, 1); write32(pair, 4, CPU_SEVEN_TICK_PAIR_BYTES);
        write32(pair, 16, 300); write32(pair, 20, 301);
        pair.set(observation(301), 32); pair.set(heap.subarray(pcmPtr, pcmPtr + 4272), 2592);
        pair.set(heap.subarray(inputPtr, inputPtr + 44), 11136);
        heap.set(diagnostics({drawn: true}), diagPtr);
        return 0;
      }
      if (command === 16) return 7; // Intentional stop after tick 300 validates its GPU frame.
      throw new Error(`Unexpected modeled native command ${command}`);
    },
  };
  return {model, initialObservation: liveObservation, initialPad,
    initialDiagnostics: diagnostics()};
}

function pageContext(module) {
  const window = {__slippiSynctest: {module}};
  const context = vm.createContext({window, atob, btoa, performance, setTimeout,
    SharedArrayBuffer: undefined, Uint8Array, DataView, console});
  return {window, evaluate: async (callback, argument) => {
    context.__argument = argument;
    return await vm.runInContext(`(${callback.toString()})(__argument)`, context);
  }};
}

test('seven-tick prediction keeps actual local bytes and remote seed until explicit acknowledgement', () => {
  const schedule = createCpuSevenTickTimeline(0, 1, CPU_SEVEN_TICK_INITIAL_REMOTE_PAD);
  const actual = from64(CPU_SEVEN_TICK_PAD44_CORPUS[0]);
  const plan = planCpuSevenTickForward(schedule, 300, CPU_SEVEN_TICK_PAD44_CORPUS[0]);
  assert.deepEqual(Buffer.from(plan.frame).subarray(0, PAD_BYTES), actual.subarray(0, PAD_BYTES));
  assert.deepEqual(Buffer.from(plan.frame).subarray(PAD_BYTES, 2 * PAD_BYTES), from64(CPU_SEVEN_TICK_INITIAL_REMOTE_PAD));
  assert.notDeepEqual(Buffer.from(plan.frame), actual);
  assert.equal(schedule.timeline.status().simulated_through, 299);
  acknowledgeCpuSevenTickForward(schedule, plan);
  assert.equal(schedule.timeline.status().simulated_through, 300);
  schedule.timeline.dispose();
});

test('raw seven-tick evidence rejects corrupt extent, pair headers, and unknown diagnostic slots', () => {
  const bytes = new Uint8Array(CPU_SEVEN_TICK_EVIDENCE_BYTES), view = new DataView(bytes.buffer);
  view.setUint32(0, 1, true); view.setUint32(4, bytes.length, true); view.setUint32(8, 7, true);
  for (const offset of [...Array.from({length: 7}, (_, i) => 72 + i * CPU_SEVEN_TICK_PAIR_BYTES), 79984]) {
    view.setUint32(offset, 1, true); view.setUint32(offset + 4, CPU_SEVEN_TICK_PAIR_BYTES, true);
  }
  assert.equal(decodeCpuSevenTickEvidence(bytes).tickPairs.length, 7);
  const badExtent = bytes.slice(); new DataView(badExtent.buffer).setUint32(4, bytes.length - 1, true);
  assert.throws(() => decodeCpuSevenTickEvidence(badExtent), /schema\/mode/);
  const badPair = bytes.slice(); new DataView(badPair.buffer).setUint32(76, CPU_SEVEN_TICK_PAIR_BYTES - 1, true);
  assert.throws(() => decodeCpuSevenTickEvidence(badPair), /raw-pair/);
  const badMask = bytes.slice(); new DataView(badMask.buffer).setUint32(56, 0x10000, true);
  assert.throws(() => decodeCpuSevenTickEvidence(badMask), /unknown slots/);
});

test('native step refusal leaves the seven-tick prediction unacknowledged and disposes its timeline', async () => {
  const {model} = createNativeModel();
  model._melee_web_synctest_draw_decision = () => 0;
  model._melee_web_snapshot_step_raw = () => 0;
  const predicted = pageContext(model);
  const authoritative = pageContext(model);
  const checkpointPad = new Uint8Array(44); checkpointPad[32] = 0xff; checkpointPad[43] = 0xff;
  const checkpoint = {boundary: {sourceFrame: 300}, observation: b64(observation(300)),
    pad: b64(checkpointPad), drawDiagnostics: b64(diagnostics())};
  const before = decodeDrawDiagnostics(diagnostics());
  const helpers = {inputMappingSource: validateSourceInputMapping.toString(),
    inspectSource: inspectInteractiveObservation.toString(),
    decodeDrawSource: decodeDrawDiagnostics.toString()};
  let thrown;
  try {
    await runBrowserCpuSevenTick(predicted, authoritative, [[100, 4, 1, 1]], checkpoint,
      mapping, [4, 4], {beginFrameId: 10, endFrameId: 10}, before, helpers);
  } catch (error) { thrown = error; }
  assert.match(String(thrown), /Predicted sample 300 refused/);
  assert.equal(thrown.cpuSevenTickPredictedPartial.simulatedThrough, 299);
  assert.equal(thrown.cpuSevenTickPredictedPartial.rows.length, 0);
});

test('baseline page callback reconstructs its validator and records one checked sample before refusal', async () => {
  const {model} = createNativeModel();
  const predictedProgress = {status: 'running', rows: [], simulatedThrough: 299};
  const predictedPage = {evaluate: async (_callback, argument) => {
    if (argument?.frameBase64) {
      const row = {sample: argument.tick, pad44Base64: argument.frameBase64,
        boundary: {sourceFrame: argument.checkpointSourceFrame + 1, interactive: true,
          players: [{rawController: 1, motion: 0}]}};
      predictedProgress.rows.push(row); return row;
    }
    if (argument?.index !== undefined) {
      predictedProgress.rows[argument.index].timelineAcknowledged = true;
      predictedProgress.simulatedThrough = argument.tick; return null;
    }
    return predictedProgress;
  }};
  const authoritativePage = pageContext(model);
  const checkpointPad = new Uint8Array(44); checkpointPad[32] = 0xff; checkpointPad[43] = 0xff;
  const checkpoint = {boundary: {sourceFrame: 300}, observation: b64(observation(300)),
    pad: b64(checkpointPad), drawDiagnostics: b64(diagnostics())};
  const helpers = {inputMappingSource: validateSourceInputMapping.toString(),
    inspectSource: inspectInteractiveObservation.toString(),
    selectedDrawSource: validateSelectedDrawPath.toString(),
    decodeDrawSource: decodeDrawDiagnostics.toString()};
  let thrown;
  try {
    await runBrowserCpuSevenTick(predictedPage, authoritativePage, [[100, 4, 1, 1]], checkpoint,
      mapping, [4, 4], {beginFrameId: 10, endFrameId: 10}, decodeDrawDiagnostics(diagnostics()), helpers);
  } catch (error) { thrown = error; }
  assert.match(String(thrown), /Seven-tick command 16 refused 7/);
  assert.equal(thrown.cpuSevenTickPartial.baseline.length, 1);
  assert.equal(thrown.cpuSevenTickPartial.baseline[0].sample, 300);
  assert.equal(thrown.cpuSevenTickPartial.baseline[0].gpuStatus.lastBeginFrameId, 11);
  assert.equal(predictedProgress.simulatedThrough, 306);
  assert.equal(predictedProgress.rows.every(row => row.timelineAcknowledged), true);
});
