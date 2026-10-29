// SPDX-License-Identifier: GPL-3.0-or-later
// Source-only seven-tick input correction witness. This runner has no browser,
// renderer, server, or child-process path.
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {MatchInputTimeline, ROLLBACK_MAX_PREDICTION_TICKS} from '../../../scripts/net_rollback_input_timeline.mjs';
import {NET_FRAME_BYTES, PAD_BYTES} from '../../../scripts/net_lockstep_core.mjs';
import {QuiescentWasmSnapshot} from '../../../tests/quiescent_wasm_snapshot.mjs';
import {
  assertDrawDiagnosticsClear, decodeDrawDiagnostics, inspectInteractiveObservation,
  requireDrawDiagnosticSize, validateSourceInputMapping,
} from './runner_contract.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const PROFILE = 'slippi-render-dependencies-dbooc-shield-drop-v5';
const SOURCE_SEED = 0x13579bdf;
const PREFIX_SAMPLES = 300;
const INPUT_SAMPLES = Object.freeze([300, 301, 302, 303, 304, 305, 306]);
const LOOKAHEAD_SAMPLE = 307;
const OBSERVATION_BYTES = 1280;
const PCM_BYTES = 1068 * Float32Array.BYTES_PER_ELEMENT;
const DRAW_DIAGNOSTIC_BYTES = 132;
const TRANSFER_DIAGNOSTIC_BYTES = 40;
const MAX_HEAP_BYTES = 512 * 1024 * 1024;
const NO_DRAW_TICK_KIND = 1;
const MATCH_FRAME_SEMANTICS = 'Observation byte offset 0 is the source VS match frame count (lbl_8046B6A0.frame_count), not the caller input sample or scheduler tick; it may stall before HUD activation.';

if (NET_FRAME_BYTES !== 44 || PAD_BYTES !== 11 || ROLLBACK_MAX_PREDICTION_TICKS !== 7)
  throw new Error('Rollback input timeline ABI differs from the four-port, seven-tick fixture contract');

const SOURCE_FILES = Object.freeze([
  'src/slippi_profile/tests/run_input_timeline.mjs',
  'src/slippi_profile/tests/runner_contract.mjs',
  'src/slippi_profile/tests/run_synctest.mjs',
  'src/slippi_profile/tests/synctest_node.cpp',
  'src/slippi_profile/profile.h', 'src/slippi_profile/profile.c',
  'src/slippi_profile/offscreen_damage.h', 'src/slippi_profile/offscreen_damage.c',
  'src/slippi_profile/dynamics_fix.h', 'src/slippi_profile/dynamics_fix.c',
  'src/slippi_profile/deadupfall_fix.h', 'src/slippi_profile/deadupfall_fix.c',
  'src/slippi_profile/draw_diagnostics.h', 'src/slippi_profile/draw_diagnostics.c',
  'src/slippi_profile/whispy_direction_fix.h', 'src/slippi_profile/whispy_direction_fix.c',
  'src/slippi_profile/ps_camera_independent_monitor_fix.h',
  'src/slippi_profile/ps_camera_independent_monitor_fix.c',
  'src/slippi_profile/ucf_dbooc_fix.h', 'src/slippi_profile/ucf_dbooc_fix.c',
  'src/slippi_profile/ucf_shield_drop_fix.h', 'src/slippi_profile/ucf_shield_drop_fix.c',
  'src/slippi_profile/ucf_shield_drop_source_guards.h',
  'src/slippi_profile/patches/melee-profile-hooks.patch',
  'tests/gameplay_snapshot_probe.cpp', 'tests/quiescent_wasm_snapshot.mjs',
  'scripts/net_rollback_input_timeline.mjs', 'scripts/net_lockstep_core.mjs',
  'src/gameplay_trig.h', '.deps/melee/src/melee/ft/kinds/ftMario/forward.h',
  'cmake/FighterRuntime.cmake', 'CMakeLists.txt', 'scripts/build.py',
  'scripts/gameplay_sources.py',
]);

const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
const fileSha = file => sha256(fs.readFileSync(file));
const cstring = (module, pointer) => module.UTF8ToString(pointer);

function parseArgs(argv) {
  const values = {};
  const allowed = new Set(['--node-runtime', '--assets', '--out']);
  if (argv.length % 2 !== 0) throw new Error('Usage: run_input_timeline.mjs --node-runtime FILE --assets DIR --out NEW_DIR');
  for (let i = 0; i < argv.length; i += 2) {
    const [key, value] = [argv[i], argv[i + 1]];
    if (!allowed.has(key) || !value || values[key])
      throw new Error('Usage: run_input_timeline.mjs --node-runtime FILE --assets DIR --out NEW_DIR');
    values[key] = value;
  }
  for (const key of allowed) if (!values[key]) throw new Error(`Missing ${key}`);
  const options = Object.fromEntries(Object.entries(values).map(([key, value]) =>
    [key, path.resolve(value)]));
  if (!fs.statSync(options['--assets']).isDirectory()) throw new Error('Assets path is not a directory');
  if (!fs.statSync(options['--node-runtime']).isFile()) throw new Error('Node runtime is not a file');
  if (!fs.statSync(options['--node-runtime'].replace(/\.js$/, '.wasm')).isFile())
    throw new Error('Node runtime Wasm companion is missing');
  if (!options['--node-runtime'].endsWith('.js')) throw new Error('Node runtime must be the generated .js module');
  if (fs.existsSync(options['--out'])) throw new Error(`Output path already exists: ${options['--out']}`);
  if (!fs.statSync(path.dirname(options['--out'])).isDirectory())
    throw new Error('Output parent directory must already exist');
  return options;
}

function copyBytes(module, pointer, size, label) {
  if (!Number.isInteger(pointer) || pointer < 0 || !Number.isInteger(size) || size < 0 ||
      pointer + size > module.HEAPU8.byteLength) {
    throw new Error(`${label} byte range is outside Wasm memory: pointer=${pointer}, size=${size}`);
  }
  return Buffer.from(module.HEAPU8.subarray(pointer, pointer + size));
}

function laneBytes(module) {
  const observationSize = module._melee_web_snapshot_observation_size();
  const pcmSize = module._melee_web_snapshot_pcm_size();
  if (observationSize !== OBSERVATION_BYTES || pcmSize !== PCM_BYTES)
    throw new Error(`Unsupported source lane ABI: observation=${observationSize}, PCM=${pcmSize}`);
  const observation = copyBytes(module,
    module._melee_web_snapshot_observation(), observationSize, 'Observation');
  return {
    observation,
    matchFrame: new DataView(observation.buffer, observation.byteOffset,
      observation.byteLength).getUint32(0, true),
    pcm: copyBytes(module, module._melee_web_snapshot_pcm(), pcmSize, 'PCM'),
    pad: copyBytes(module, module._melee_web_snapshot_input(), NET_FRAME_BYTES, 'PAD'),
  };
}

function drawDiagnostics(module) {
  const size = requireDrawDiagnosticSize(
    module._melee_web_synctest_draw_diagnostics_size(), DRAW_DIAGNOSTIC_BYTES);
  return decodeDrawDiagnostics(copyBytes(module,
    module._melee_web_synctest_draw_diagnostics(), size, 'Draw diagnostics'));
}

function checked(module, owner, label, call) {
  const result = owner.call(call);
  if (result !== 1) throw new Error(`${label}: ${cstring(module, module._melee_web_snapshot_error())}`);
}

function sourceBoundary(module, owner, lane, mapping, stocks, sample, phase) {
  const state = inspectInteractiveObservation(lane.observation, {
    classifyMotion: motion => owner.call(() => module._melee_web_synctest_motion_boundary(motion)),
    isMarioKind: kind => owner.call(() => module._melee_web_synctest_is_mario_kind(kind)) === 1,
    slotCount: drawDiagnostics(module).slotCount,
    expectedInputMapping: mapping,
    expectedStocks: stocks,
  });
  if (!state.interactive) {
    throw new Error(`Interactive boundary refused at sample ${sample} (${phase}): ${JSON.stringify({
      reasons: state.refusalReasons, players: state.players,
    })}`);
  }
  assertDrawDiagnosticsClear(drawDiagnostics(module), `Source-only ${phase} sample ${sample}`);
  return state;
}

function noDraw(module, owner, sample) {
  const decision = owner.call(() => module._melee_web_synctest_draw_decision(NO_DRAW_TICK_KIND));
  if (decision !== 0) throw new Error(`No-draw correction policy returned ${decision} at sample ${sample}`);
  assertDrawDiagnosticsClear(drawDiagnostics(module), `Before no-draw sample ${sample}`);
}

function sourceStep(module, owner, sample, kind, clockExpectation = {}) {
  noDraw(module, owner, sample);
  checked(module, owner, `source sample ${sample}`, () => kind === 'fixed'
    ? module._melee_web_snapshot_step(sample)
    : module._melee_web_snapshot_step_raw());
  const lane = laneBytes(module);
  if (clockExpectation.expectedMatchFrame !== undefined &&
      lane.matchFrame !== clockExpectation.expectedMatchFrame) {
    throw new Error(`VS match frame did not advance exactly once at input sample ${sample} (${clockExpectation.phase || 'source lane'}): expected=${clockExpectation.expectedMatchFrame}, actual=${lane.matchFrame}`);
  }
  if (clockExpectation.previousMatchFrame !== undefined) {
    const delta = lane.matchFrame - clockExpectation.previousMatchFrame;
    if (delta < 0 || delta > 1)
      throw new Error(`VS match frame regressed or jumped during startup prefix at input sample ${sample}: previous=${clockExpectation.previousMatchFrame}, actual=${lane.matchFrame}`);
  }
  return lane;
}

function transferDiagnostic(module, owner) {
  const size = owner.call(() => module._melee_web_snapshot_transfer_diagnostic_size());
  if (size !== TRANSFER_DIAGNOSTIC_BYTES)
    throw new Error(`Unsupported transfer diagnostic ABI: ${size}`);
  const bytes = copyBytes(module,
    owner.call(() => module._melee_web_snapshot_transfer_diagnostic()), size, 'Transfer diagnostic');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  // Native TransferDiagnostic.sample is match->source_frames() BEFORE tick,
  // not the caller-owned input sample (the clocks differ during countdown).
  const result = {preTickMatchFrame: view.getUint32(0, true)};
  for (const [index, phase] of ['beforeTick', 'afterTick', 'afterAudio'].entries()) {
    const offset = 4 + index * 12;
    result[phase] = {drive: view.getInt32(offset, true),
      source: view.getInt32(offset + 4, true), bank: view.getInt32(offset + 8, true)};
  }
  // Drive aggregates HPS/source/bank work. Only the explicit source/bank leaves
  // attribute those owners; do not infer HPS from a generic busy error alone.
  return result;
}

function requireQuiescent(module, owner, report, writeReport, label) {
  const record = {label, transfer: transferDiagnostic(module, owner), admitted: false};
  (report.quiescenceBoundaries ??= []).push(record);
  writeReport();
  checked(module, owner, label, () => module._melee_web_snapshot_quiescent());
  record.admitted = true;
}

function writePadFrame(module, frame) {
  if (!(Array.isArray(frame) || frame instanceof Uint8Array) || frame.length !== NET_FRAME_BYTES ||
      Array.from(frame).some(value => !Number.isInteger(value) || value < 0 || value > 255))
    throw new Error('Timeline plan did not contain one literal four-port PAD frame');
  const bytes = Buffer.from(frame);
  const pointer = module._melee_web_snapshot_input();
  if (!Number.isInteger(pointer) || pointer < 0 || pointer + NET_FRAME_BYTES > module.HEAPU8.byteLength)
    throw new Error('Source PAD buffer is outside Wasm memory');
  module.HEAPU8.set(bytes, pointer);
  const actual = copyBytes(module, pointer, NET_FRAME_BYTES, 'Injected PAD');
  if (!actual.equals(bytes)) throw new Error('Literal PAD frame was not written exactly');
}

function padSlot(pad, port) { return pad.subarray(port * PAD_BYTES, (port + 1) * PAD_BYTES); }
function sameBytes(left, right) { return left.equals(right); }
function firstDifference(left, right) {
  const count = Math.min(left.length, right.length);
  for (let offset = 0; offset < count; ++offset)
    if (left[offset] !== right[offset]) return {offset, expected: left[offset], actual: right[offset]};
  return left.length === right.length ? null : {offset: count, expectedLength: left.length, actualLength: right.length};
}
function boundaryRecord(boundary) {
  if (!boundary) return null;
  const {sourceFrame: matchFrame, ...state} = boundary;
  return {...state, matchFrame};
}

function laneRecord(sample, lane, boundary) {
  return {
    sample,
    matchFrame: lane.matchFrame,
    observationBase64: lane.observation.toString('base64'),
    observationSha256: sha256(lane.observation),
    pcmBase64: lane.pcm.toString('base64'),
    pcmSha256: sha256(lane.pcm),
    pad44Base64: lane.pad.toString('base64'),
    pad44Sha256: sha256(lane.pad),
    ...(boundary ? {boundary: boundaryRecord(boundary), fighterState: boundary.players.map(player => ({
      sourceSlot: player.sourceSlot, rawController: player.rawController,
      fighterKind: player.fighterKind, stocks: player.stocks,
      motion: player.motion, motionClass: player.motionClass,
    }))} : {}),
  };
}
function laneDifferences(expected, actual) {
  return Object.fromEntries(['observation', 'pcm', 'pad'].flatMap(key =>
    sameBytes(expected[key], actual[key]) ? [] : [[key, firstDifference(expected[key], actual[key])]]));
}
function laneFromRecord(record) {
  return {
    observation: Buffer.from(record.observationBase64, 'base64'),
    pcm: Buffer.from(record.pcmBase64, 'base64'),
    pad: Buffer.from(record.pad44Base64, 'base64'),
  };
}
function heapSha(module) { return sha256(module.HEAPU8); }
function heapDifference(expected, actual) {
  const difference = firstDifference(expected, actual);
  if (!difference) return null;
  const start = Math.max(0, difference.offset - 8);
  const end = Math.min(expected.length, difference.offset + 9);
  return {...difference, contextStart: start, contextBytes: end - start,
    expectedContextHex: expected.subarray(start, end).toString('hex'),
    actualContextHex: actual.subarray(start, end).toString('hex')};
}
function exactCheckpoint(module, expectedSha, label) {
  const actual = heapSha(module);
  if (actual !== expectedSha) throw new Error(`${label} heap hash differs: expected=${expectedSha}, actual=${actual}`);
  return actual;
}

function assetInventory(root) {
  const files = fs.readdirSync(root).sort().flatMap(name => {
    const full = path.join(root, name);
    return fs.statSync(full).isFile()
      ? [{name, bytes: fs.statSync(full).size, sha256: fileSha(full)}] : [];
  });
  if (files.length === 0) throw new Error('Source fixture asset directory has no top-level files');
  return files;
}

function machine() {
  return {cpu: os.cpus()[0]?.model || 'unknown', arch: os.arch(), os: os.type(),
    release: os.release(), node: process.version, nodeExecutable: process.execPath};
}

function sourceHashes() {
  const files = [...SOURCE_FILES];
  return Object.fromEntries(files.map(name => [name, fileSha(path.join(ROOT, name))]));
}

export async function runTimelineWitness(module, report, writeReport) {
  const owner = new QuiescentWasmSnapshot(module, MAX_HEAP_BYTES);
  let initialized = false;
  let timeline = null;
  let firstFailure = null;
  try {
    report.stage = 'fixture-initialization';
    checked(module, owner, 'configure source seed', () => module._melee_web_snapshot_configure_seed(SOURCE_SEED));
    checked(module, owner, 'source fixture initialization', () => module.ccall(
      'melee_web_snapshot_init', 'number', ['string'], [report.assetRoot]));
    initialized = true;
    const initializer = JSON.parse(cstring(module, module._melee_web_snapshot_initializer_json()));
    const profile = cstring(module, module._melee_web_synctest_profile_identity());
    const runtimeSource = cstring(module, module._melee_web_snapshot_source_identity());
    const expectedSource = report.sourceHashes['tests/gameplay_snapshot_probe.cpp'];
    if (profile !== PROFILE) throw new Error(`Unexpected source profile identity: ${profile}`);
    if (runtimeSource !== expectedSource)
      throw new Error(`Runtime fixture identity differs from current source: runtime=${runtimeSource}, current=${expectedSource}`);
    if (initializer.profile !== 0 || initializer.seed !== SOURCE_SEED)
      throw new Error(`Unexpected fixture profile/seed: ${JSON.stringify(initializer)}`);
    const diagnostics = drawDiagnostics(module);
    assertDrawDiagnosticsClear(diagnostics, 'Fixture initialization');
    if (!Array.isArray(initializer.players) || initializer.players.length !== 2)
      throw new Error('Fixture initializer must provide exactly two actual players');
    const mapping = validateSourceInputMapping(initializer.players.map((player, sourceSlot) => ({
      sourceSlot, initializerPort: player.port, rawController: player.controller,
    })), diagnostics.slotCount);
    const stocks = initializer.players.map(player => player.stocks);
    if (stocks.length !== 2 || stocks.some(value => value !== 4))
      throw new Error(`Fixture must initialize exactly four stocks per player: ${JSON.stringify(stocks)}`);
    const localPort = mapping[0].rawController;
    const remotePort = mapping[1].rawController;
    const staticInitial = laneBytes(module);
    const initialMatchFrame = staticInitial.matchFrame;
    if (initialMatchFrame !== 0)
      throw new Error(`Initial VS match frame must be zero before the first consumed input sample: ${initialMatchFrame}`);
    report.fixture = {profile, sourceIdentity: runtimeSource, initializer, sourceSlotCount: diagnostics.slotCount,
      inputMapping: mapping, localPort, remotePort, stocks, seed: SOURCE_SEED,
      initialMatchFrame, observationBytes: staticInitial.observation.length, pcmBytes: staticInitial.pcm.length,
      padBytes: staticInitial.pad.length};
    report.observationClock = {field: 'Observation.frame at byte offset 0', semantics: MATCH_FRAME_SEMANTICS,
      schedulerGetterAvailable: false,
      schedulerGetterScope: 'The synchronous Node fixture exports source match-frame observations, not gm_801A4BA8()/gm_80479D58.unk_0; successful source-step calls remain the exact tick-call boundary.'};
    writeReport();

    report.stage = 'prefix';
    const prefixHash = crypto.createHash('sha256');
    let previousPrefixMatchFrame = initialMatchFrame;
    const prefixClockTransitions = [];
    const prefixClockStalls = [];
    let prefixLane = staticInitial;
    for (let sample = 0; sample < PREFIX_SAMPLES; ++sample) {
      prefixLane = sourceStep(module, owner, sample, 'fixed', {previousMatchFrame: previousPrefixMatchFrame});
      if (sample === 0) requireQuiescent(module, owner, report, writeReport, 'Initial sample0 quiescence');
      if (sample === 107) report.startupTransferSample107 = transferDiagnostic(module, owner);
      if (prefixLane.matchFrame === previousPrefixMatchFrame) {
        const lastStall = prefixClockStalls.at(-1);
        if (lastStall && lastStall.lastInputSample === sample - 1) lastStall.lastInputSample = sample;
        else prefixClockStalls.push({firstInputSample: sample, lastInputSample: sample});
      } else {
        const lastTransition = prefixClockTransitions.at(-1);
        if (lastTransition && lastTransition.lastInputSample === sample - 1) {
          lastTransition.lastInputSample = sample;
          lastTransition.toMatchFrame = prefixLane.matchFrame;
        } else {
          prefixClockTransitions.push({firstInputSample: sample, lastInputSample: sample,
            fromMatchFrame: previousPrefixMatchFrame, toMatchFrame: prefixLane.matchFrame});
        }
      }
      previousPrefixMatchFrame = prefixLane.matchFrame;
      prefixHash.update(Buffer.from([sample >>> 24, sample >>> 16, sample >>> 8, sample]));
      prefixHash.update(prefixLane.observation).update(prefixLane.pcm).update(prefixLane.pad);
    }
    const prefixBoundary = sourceBoundary(module, owner, prefixLane, mapping, stocks, PREFIX_SAMPLES - 1, 'prefix-end');
    const checkpointPad = Buffer.from(prefixLane.pad);
    for (let port = 0; port < 4; ++port) {
      if (port === localPort || port === remotePort) {
        if (checkpointPad[port * PAD_BYTES + 10] !== 0)
          throw new Error(`Selected source PAD port ${port} is not connected at the checkpoint`);
      }
    }
    requireQuiescent(module, owner, report, writeReport, 'Checkpoint sample299 quiescence');
    const checkpoint = owner.capture();
    const checkpointSha = heapSha(module);
    const checkpointMatchFrame = prefixLane.matchFrame;
    report.prefix = {sampleCount: PREFIX_SAMPLES, lastInputSample: PREFIX_SAMPLES - 1,
      initialMatchFrame, checkpointMatchFrame, sourceSeed: prefixBoundary.randomSeed,
      matchFrameClock: {semantics: MATCH_FRAME_SEMANTICS,
        observedTransitions: prefixClockTransitions, stalledInputRanges: prefixClockStalls},
      digest: prefixHash.digest('hex'), checkpointHeapBytes: checkpoint.byteLength,
      checkpointHeapSha256: checkpointSha, checkpointInputPad44Base64: checkpointPad.toString('base64'),
      activeBoundary: boundaryRecord(prefixBoundary)};
    report.inputGeneration = {inputSamples: [...INPUT_SAMPLES], checkpointMatchFrame, ticks: [], purpose:
      'Capture fixture-authored literal PAD bytes; restore checkpoint before all comparable raw-input lanes.'};
    const generatedInputs = [];
    report.stage = 'input-generation';
    for (const sample of INPUT_SAMPLES) {
      const lane = sourceStep(module, owner, sample, 'fixed', {
        expectedMatchFrame: checkpointMatchFrame + (sample - INPUT_SAMPLES[0]) + 1,
        phase: 'input-generation'});
      const boundary = sourceBoundary(module, owner, lane, mapping, stocks, sample, 'input-generation');
      for (let port = 0; port < 4; ++port) {
        const connected = port === localPort || port === remotePort;
        if (padSlot(lane.pad, port)[10] !== (connected ? 0 : 0xff))
          throw new Error(`Fixture PAD connection state differs from selected source ports at sample ${sample}, port ${port}`);
      }
      const record = laneRecord(sample, lane, boundary);
      generatedInputs.push(record);
      report.inputGeneration.ticks.push(record);
      writeReport();
    }
    requireQuiescent(module, owner, report, writeReport, 'Restore before authoritative lane quiescence');
    checkpoint.restore();
    exactCheckpoint(module, checkpointSha, 'Restore before authoritative lane');

    report.authoritative = {inputSamples: [...INPUT_SAMPLES], checkpointMatchFrame,
      ticks: [], endpointHeapSha256: null};
    report.stage = 'authoritative';
    writeReport();
    for (const sample of INPUT_SAMPLES) {
      const expectedPad = Buffer.from(generatedInputs[sample - INPUT_SAMPLES[0]].pad44Base64, 'base64');
      writePadFrame(module, expectedPad);
      const lane = sourceStep(module, owner, sample, 'raw', {
        expectedMatchFrame: checkpointMatchFrame + (sample - INPUT_SAMPLES[0]) + 1,
        phase: 'authoritative'});
      if (!lane.pad.equals(expectedPad)) throw new Error(`Authoritative source sample ${sample} did not consume its captured literal PAD`);
      const boundary = sourceBoundary(module, owner, lane, mapping, stocks, sample, 'authoritative');
      const record = laneRecord(sample, lane, boundary);
      report.authoritative.ticks.push(record);
      writeReport();
    }
    requireQuiescent(module, owner, report, writeReport, 'Authoritative endpoint quiescence');
    const authoritativeEndpointBytes = Buffer.from(module.HEAPU8);
    const authoritativeEndpointSha = sha256(authoritativeEndpointBytes);
    report.authoritative.endpointHeapSha256 = authoritativeEndpointSha;
    report.authoritative.result = 'retained';
    writeReport();

    // One real source row supplies the eighth-tick prediction-cap guard only.
    report.stage = 'prediction-cap-guard';
    const guardLane = sourceStep(module, owner, LOOKAHEAD_SAMPLE, 'fixed', {
      expectedMatchFrame: checkpointMatchFrame + INPUT_SAMPLES.length + 1,
      phase: 'prediction-cap-guard-only'});
    const guardBoundary = sourceBoundary(module, owner, guardLane, mapping, stocks,
      LOOKAHEAD_SAMPLE, 'prediction-cap-guard-only');
    report.predictionCapGuard = {sample: LOOKAHEAD_SAMPLE, checkpointMatchFrame,
      expectedMatchFrame: checkpointMatchFrame + INPUT_SAMPLES.length + 1,
      scope: 'one guard-only source sample; excluded from seven-tick comparisons and cost claims',
      localPad11Base64: padSlot(guardLane.pad, localPort).toString('base64'),
      observedMatchFrame: guardLane.matchFrame};
    writeReport();

    requireQuiescent(module, owner, report, writeReport, 'Restore before predicted lane quiescence');
    checkpoint.restore();
    exactCheckpoint(module, checkpointSha, 'Restore before predicted lane');
    const initialRemotePad = padSlot(checkpointPad, remotePort);
    timeline = new MatchInputTimeline({localPort, remotePort, startTick: INPUT_SAMPLES[0],
      initialRemotePad, maxHistoryTicks: 8, maxLookaheadTicks: 8});
    report.predicted = {checkpointMatchFrame, ticks: [], endpointHeapSha256: null, predictionLimit: null};
    report.stage = 'predicted';
    writeReport();

    for (let index = 0; index < INPUT_SAMPLES.length; ++index) {
      const sample = INPUT_SAMPLES[index];
      const literalPad = Buffer.from(generatedInputs[index].pad44Base64, 'base64');
      const localPad = literalPad.subarray(localPort * PAD_BYTES,
        (localPort + 1) * PAD_BYTES);
      timeline.recordLocal(sample, localPad);
      const plan = timeline.planForward();
      if (plan.kind !== 'forward' || plan.tick !== sample || plan.remote_source !== 'last_known_prediction' ||
          plan.speculative_ticks !== index + 1)
        throw new Error(`Unexpected predicted plan at sample ${sample}: ${JSON.stringify(plan)}`);
      const frameBytes = Buffer.from(plan.frame);
      const expectedLocal = padSlot(literalPad, localPort);
      if (!padSlot(frameBytes, localPort).equals(expectedLocal))
        throw new Error(`Predicted frame lost original local PAD at sample ${sample}`);
      writePadFrame(module, plan.frame);
      const lane = sourceStep(module, owner, sample, 'raw', {
        expectedMatchFrame: checkpointMatchFrame + index + 1, phase: 'predicted'});
      const boundary = sourceBoundary(module, owner, lane, mapping, stocks, sample, 'predicted');
      if (!lane.pad.equals(frameBytes)) throw new Error(`Source PAD did not consume the predicted plan at sample ${sample}`);
      const record = laneRecord(sample, lane, boundary);
      record.remoteSource = plan.remote_source;
      record.speculativeTicks = plan.speculative_ticks;
      report.predicted.ticks.push(record);
      writeReport();
      timeline.commitForward(plan);
    }

    timeline.recordLocal(LOOKAHEAD_SAMPLE,
      Buffer.from(report.predictionCapGuard.localPad11Base64, 'base64'));
    const blocked = timeline.planForward();
    if (blocked.status !== 'blocked' || blocked.reason !== 'prediction_limit' || blocked.tick !== LOOKAHEAD_SAMPLE)
      throw new Error(`Timeline did not block exactly the eighth prediction: ${JSON.stringify(blocked)}`);
    report.predicted.predictionLimit = {sample: LOOKAHEAD_SAMPLE, status: blocked.status,
      reason: blocked.reason, remoteContiguousThrough: timeline.status().remote_contiguous_through};
    requireQuiescent(module, owner, report, writeReport, 'Predicted endpoint quiescence');
    report.predicted.endpointHeapSha256 = heapSha(module);

    const actualRemote = report.authoritative.ticks.map(record => ({
      sample: record.sample,
      pad: padSlot(Buffer.from(record.pad44Base64, 'base64'), remotePort),
    }));
    let firstChanged = null;
    for (const row of actualRemote) {
      const accepted = timeline.acceptRemote(row.sample, row.pad);
      if (accepted.status !== 'accepted') throw new Error(`Actual remote PAD was not accepted at ${row.sample}: ${JSON.stringify(accepted)}`);
      if (firstChanged === null && accepted.earliest_changed_tick !== null)
        firstChanged = accepted.earliest_changed_tick;
    }
    const status = timeline.status();
    if (firstChanged !== INPUT_SAMPLES[0] || status.dirty_from !== INPUT_SAMPLES[0] ||
        status.remote_contiguous_through !== INPUT_SAMPLES.at(-1))
      throw new Error(`Actual remote delivery did not identify the earliest correction: ${JSON.stringify({firstChanged, status})}`);
    const replay = timeline.planReplay();
    if (replay.kind !== 'replay' || replay.from !== INPUT_SAMPLES[0] ||
        replay.through !== INPUT_SAMPLES.at(-1) || replay.frames.length !== INPUT_SAMPLES.length ||
        replay.speculative_ticks !== 0)
      throw new Error(`Correction plan is not the complete seven-tick suffix: ${JSON.stringify(replay)}`);
    for (let index = 0; index < replay.frames.length; ++index) {
      const sample = INPUT_SAMPLES[index];
      const expectedPad = Buffer.from(report.authoritative.ticks[index].pad44Base64, 'base64');
      const frame = replay.frames[index];
      if (frame.tick !== sample || !Buffer.from(frame.local_pad).equals(padSlot(expectedPad, localPort)) ||
          !Buffer.from(frame.remote_pad).equals(padSlot(expectedPad, remotePort)) ||
          !Buffer.from(frame.frame).equals(expectedPad))
        throw new Error(`Replay plan changed literal source input at sample ${sample}`);
    }
    report.correction = {checkpointMatchFrame, earliestChangedTick: firstChanged, from: replay.from, through: replay.through,
      tickCount: replay.frames.length, originalLocalPadsPreserved: true,
      replayFrames: replay.frames.map(frame => ({tick: frame.tick,
        localPad11Base64: Buffer.from(frame.local_pad).toString('base64'),
        remotePad11Base64: Buffer.from(frame.remote_pad).toString('base64'),
        pad44Base64: Buffer.from(frame.frame).toString('base64')})),
      ticks: [], endpointHeapSha256: null, endpointHeapDifference: null};
    report.stage = 'negative-control';
    writeReport();

    const predictedRows = report.predicted.ticks;
    const authoritativeRows = report.authoritative.ticks;
    const negativeControlDifferences = predictedRows.map((row, index) =>
      laneDifferences(laneFromRecord(authoritativeRows[index]), laneFromRecord(row)));
    const remoteMotionDifferences = predictedRows.map((row, index) => {
      const remoteActual = authoritativeRows[index].fighterState.find(player => player.rawController === remotePort);
      const remotePredicted = row.fighterState.find(player => player.rawController === remotePort);
      if (!remoteActual || !remotePredicted) throw new Error('Existing source observation inspector omitted the remote fighter');
      return {tick: INPUT_SAMPLES[index], authoritativeMotion: remoteActual.motion,
        predictedMotion: remotePredicted.motion, differs: remoteActual.motion !== remotePredicted.motion};
    });
    const firstStateDivergence = remoteMotionDifferences.findIndex(row => row.differs);
    if (firstStateDivergence < 0)
      throw new Error('Withheld-remote prediction did not change the declared remote fighter motion field');
    report.predicted.negativeControl = {result: 'detected',
      testedField: 'remote fighter players[].motion from inspectInteractiveObservation',
      firstFighterStateDivergenceTick: INPUT_SAMPLES[firstStateDivergence],
      remoteMotionByTick: remoteMotionDifferences, laneDifferencesByTick: negativeControlDifferences};
    writeReport();

    report.stage = 'corrected-replay';
    requireQuiescent(module, owner, report, writeReport, 'Restore before correction replay quiescence');
    checkpoint.restore();
    exactCheckpoint(module, checkpointSha, 'Restore before correction replay');
    for (let index = 0; index < replay.frames.length; ++index) {
      const frame = replay.frames[index];
      const sample = frame.tick;
      writePadFrame(module, frame.frame);
      const lane = sourceStep(module, owner, sample, 'raw', {
        expectedMatchFrame: checkpointMatchFrame + index + 1, phase: 'corrected-replay'});
      const boundary = sourceBoundary(module, owner, lane, mapping, stocks, sample, 'corrected');
      const expected = {
        observation: Buffer.from(authoritativeRows[index].observationBase64, 'base64'),
        pcm: Buffer.from(authoritativeRows[index].pcmBase64, 'base64'),
        pad: Buffer.from(authoritativeRows[index].pad44Base64, 'base64'),
      };
      const record = laneRecord(sample, lane, boundary);
      report.correction.ticks.push(record);
      report.correction.ticks.at(-1).differences = laneDifferences(expected, lane);
      writeReport();
      if (Object.keys(report.correction.ticks.at(-1).differences).length !== 0)
        throw new Error(`Corrected source lane differs from authoritative sample ${sample}: ${JSON.stringify(report.correction.ticks.at(-1).differences)}`);
    }
    timeline.commitReplay(replay);
    if (timeline.status().dirty_from !== null || timeline.status().simulated_through !== INPUT_SAMPLES.at(-1))
      throw new Error(`Input timeline did not acknowledge its successful replay: ${JSON.stringify(timeline.status())}`);
    requireQuiescent(module, owner, report, writeReport, 'Corrected endpoint quiescence');
    const correctedEndpointBytes = Buffer.from(module.HEAPU8);
    const correctedEndpoint = sha256(correctedEndpointBytes);
    if (correctedEndpoint !== authoritativeEndpointSha) {
      report.correction.endpointHeapDifference = heapDifference(authoritativeEndpointBytes, correctedEndpointBytes);
      writeReport();
      throw new Error(`Corrected endpoint heap differs: authoritative=${authoritativeEndpointSha}, corrected=${correctedEndpoint}, firstDifference=${JSON.stringify(report.correction.endpointHeapDifference)}`);
    }
    report.correction.endpointHeapSha256 = correctedEndpoint;
    report.correction.result = 'passed';
    report.result = 'passed';
    report.stage = 'complete';
    return report;
  } catch (error) {
    firstFailure = error;
    report.result = 'failed';
    report.failure = {stage: report.stage || 'source-only-witness', message: String(error?.stack || error)};
    throw error;
  } finally {
    const cleanupFailures = [];
    try { timeline?.dispose(); } catch (error) { cleanupFailures.push(`timeline dispose: ${String(error)}`); }
    let fixtureClosed = false;
    if (initialized) {
      try {
        checked(module, owner, 'source fixture close', () => module._melee_web_snapshot_close());
        fixtureClosed = true;
      }
      catch (error) { cleanupFailures.push(error instanceof Error ? error.message : String(error)); }
    }
    let snapshotOwnerClosed = false;
    try { owner.close(); snapshotOwnerClosed = true; }
    catch (error) { cleanupFailures.push(`snapshot owner close: ${String(error)}`); }
    report.cleanup = {fixtureClosed, snapshotOwnerClosed, failures: cleanupFailures};
    if (cleanupFailures.length && !firstFailure) {
      report.result = 'failed';
      report.failure = {stage: 'cleanup', message: cleanupFailures[0]};
    }
    writeReport();
    if (cleanupFailures.length && !firstFailure) throw new Error(`Cleanup failed: ${cleanupFailures.join('; ')}`);
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  fs.mkdirSync(options['--out']);
  const nodeRuntime = options['--node-runtime'];
  const nodeWasm = nodeRuntime.replace(/\.js$/, '.wasm');
  const report = {
    schema: 'melee-web-source-only-input-timeline-witness-v2',
    scope: 'Synchronous actual-source Node fixture; seven no-draw correction ticks; not browser restore or performance evidence.',
    result: 'incomplete', machine: machine(),
    nodeRuntime: {js: path.basename(nodeRuntime), jsSha256: fileSha(nodeRuntime),
      wasm: path.basename(nodeWasm), wasmSha256: fileSha(nodeWasm)},
    sourceHashes: sourceHashes(), assetRoot: options['--assets'], assets: assetInventory(options['--assets']),
    fixture: null, prefix: null, inputGeneration: null, authoritative: null, predictionCapGuard: null,
    predicted: null, correction: null, cleanup: null, failure: null, stage: 'module-load',
    processPolicy: 'No child process, browser, HTTP server, renderer, GPU, or process-signal action.',
  };
  const evidencePath = path.join(options['--out'], 'evidence.json');
  const writeReport = () => fs.writeFileSync(evidencePath, `${JSON.stringify(report, null, 2)}\n`);
  writeReport();
  let module = null;
  try {
    const factory = createRequire(import.meta.url)(nodeRuntime);
    module = await factory({print: () => {}, printErr: line => process.stderr.write(`${line}\n`)});
    await runTimelineWitness(module, report, writeReport);
  } catch (error) {
    report.result = 'failed';
    report.failure ??= {stage: 'module-load', message: String(error?.stack || error)};
    writeReport();
    process.exitCode = 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  await main();
