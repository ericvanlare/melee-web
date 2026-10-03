#!/usr/bin/env node
// Source-only shared-page snapshot experiment. This is deliberately
// separate from scripts/check_source_snapshot.mjs: its owner binds after the
// synchronous native init has finished, so init-time Wasm growth is observed
// rather than mistaken for a restorable snapshot capacity. It makes no
// nominal-memory, browser, renderer, audio-sink, or practical-performance claim.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { performance } from 'node:perf_hooks';
import { SharedPageWasmSnapshotRing } from '../tests/shared_page_wasm_snapshot.mjs';

const SOURCE_ROOT = path.resolve(import.meta.dirname, '..');
const SCRIPT_SOURCE = path.resolve(import.meta.filename);
const PROTOTYPE_SOURCE = path.resolve(import.meta.dirname, '../tests/shared_page_wasm_snapshot.mjs');
const SOURCE_FILES = [
  'tests/gameplay_snapshot_probe.cpp',
  'tests/quiescent_wasm_snapshot.mjs',
  'tests/shared_page_wasm_snapshot.mjs',
  'scripts/check_shared_page_snapshot.mjs',
  'scripts/check_source_snapshot.mjs',
  'scripts/build.py',
  'cmake/FighterRuntime.cmake',
  'dependencies.lock.json',
];
const MAX_SNAPSHOT_BYTES = 512 * 1024 * 1024;
const MAX_RING_STATES = 8;
const OBSERVATION_BYTES = 1216;
const PCM_BYTES = 1068 * 4;
const BUFFER_IDENTITIES = new WeakMap();
let nextBufferIdentity = 1;

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileSha256 = file => sha256(fs.readFileSync(file));
const writeExclusive = (file, value) => fs.writeFileSync(file, value, { flag: 'wx' });

function bufferIdentity(buffer) {
  if (!BUFFER_IDENTITIES.has(buffer)) BUFFER_IDENTITIES.set(buffer, nextBufferIdentity++);
  return BUFFER_IDENTITIES.get(buffer);
}

function parseArgs(argv) {
  const options = {};
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!['--runtime', '--assets', '--out'].includes(key) || !value || options[key]) {
      throw new Error('Usage: check_shared_page_snapshot.mjs --runtime FILE --assets DIR --out NEW_DIR');
    }
    options[key] = path.resolve(value);
  }
  for (const key of ['--runtime', '--assets', '--out']) if (!options[key]) throw new Error(`Missing ${key}`);
  if (fs.existsSync(options['--out'])) throw new Error(`Output directory already exists: ${options['--out']}`);
  if (!fs.existsSync(path.dirname(options['--out']))) throw new Error('Output parent must already exist');
  return options;
}

function sourceHashes() {
  return Object.fromEntries(SOURCE_FILES.map(file => [file, fileSha256(path.join(SOURCE_ROOT, file))]));
}

function assetHashes(root) {
  return Object.fromEntries(fs.readdirSync(root).sort().flatMap(name => {
    const file = path.join(root, name);
    return fs.statSync(file).isFile() ? [[name, fileSha256(file)]] : [];
  }));
}

function copyRange(module, pointer, size) {
  return Buffer.from(module.HEAPU8.slice(pointer, pointer + size));
}

function firstDifference(expected, actual) {
  const limit = Math.min(expected.length, actual.length);
  for (let offset = 0; offset < limit; offset++) {
    if (expected[offset] !== actual[offset]) return { offset, expected: expected[offset], actual: actual[offset] };
  }
  return expected.length === actual.length
    ? null
    : { offset: limit, expectedLength: expected.length, actualLength: actual.length };
}

function observationRecord(bytes) {
  return {
    frame: bytes.readUInt32LE(0),
    rng: bytes.readUInt32LE(4),
    ready: bytes.readUInt32LE(8),
    ending: bytes.readUInt32LE(12),
    complete: bytes.readUInt32LE(16),
    paused: bytes.readUInt32LE(20),
    outcome: bytes.readUInt32LE(24),
    winner: bytes.readInt32LE(28),
    pcmFrames: bytes.readUInt32LE(32),
    audioPhase: bytes.readUInt32LE(36),
    stocks: [bytes.readInt32LE(40), bytes.readInt32LE(44)],
    motions: [bytes.readInt32LE(48), bytes.readInt32LE(52)],
    fireballs: bytes.readInt32LE(56),
    objects: bytes.readUInt32LE(60),
    processes: bytes.readUInt32LE(64),
  };
}

function machineIdentity() {
  return {
    cpu: os.cpus()[0]?.model || 'unknown',
    arch: os.arch(),
    os: os.type(),
    release: os.release(),
    node: process.version,
    executable: process.execPath,
  };
}

function expectFailure(action, label) {
  try {
    action();
  } catch (error) {
    return { label, result: 'detected', error: String(error?.message || error) };
  }
  throw new Error(`${label} was accepted unexpectedly`);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  fs.mkdirSync(options['--out']);
  const hostLog = [];
  const runtimeWasm = options['--runtime'].replace(/\.js$/, '.wasm');
  const report = {
    schema: 'melee-web-shared-page-snapshot-experiment-v1',
    result: 'incomplete',
    boundary: 'synchronous source-only exported-call return; shared-page owner binds after init and native quiescence',
    scenario: 'two human Mario players / Final Destination / four stocks',
    sourceDrawing: false,
    browserGameplay: false,
    rollbackAdmission: false,
    machine: machineIdentity(),
    runtime: null,
    sourceSha256: null,
    helperSha256: null,
    assets: null,
    factoryHeap: null,
    postInitHeap: null,
    ownerHeap: null,
    entrySamples: null,
    ownerBinding: null,
    cycles: [],
    sharedRing: null,
    coverage: null,
    negativeControl: null,
    cleanup: null,
    hostPeakRssBytes: null,
    timing: 'native-step timings cover only the synchronous checked source step; observation copies, hashes, comparisons, and report writes are outside baseline/replay step timings; capture includes native quiescence and shared-page capture',
    comparison: 'exact observation and PCM bytes at every baseline/replay sample; exact entire-linear-memory Buffer equality at each depth endpoint; full SHA-256 retained for every depth endpoint and retained ring state',
    exclusions: [
      'source draw traversal and hidden draw-dependent simulation',
      'GPU and JS renderer ownership',
      'browser input/clock/queue state',
      'Web Audio and externally committed sound',
      'persistent saves',
      'retail or Slippi semantic equivalence',
      'foreground timing, physical input and Internet connectivity',
      'Wasm mutable globals and function tables outside linear memory',
      'imported host state, filesystem state, process state, and external allocator state',
      'nominal 64 MiB memory or practical rollback performance admission',
    ],
  };
  const writeReport = () => fs.writeFileSync(path.join(options['--out'], 'evidence.json'), JSON.stringify(report, null, 2) + '\n');
  // Keep the report overwrite-safe while retaining partial cycles after a failure.
  writeReport();
  let module = null;
  let owner = null;
  let initialized = false;
  let initAttempted = false;
  let forwardStates = [];
  let inputSamples = [];
  try {
    report.runtime = {
      path: options['--runtime'],
      jsSha256: fileSha256(options['--runtime']),
      wasmPath: runtimeWasm,
      wasmSha256: fileSha256(runtimeWasm),
    };
    report.sourceSha256 = sourceHashes();
    report.helperSha256 = {
      runner: fileSha256(SCRIPT_SOURCE),
      prototype: fileSha256(PROTOTYPE_SOURCE),
    };
    report.assets = assetHashes(options['--assets']);
    writeReport();
    const factory = createRequire(import.meta.url)(options['--runtime']);
    module = await factory({ print: text => hostLog.push(String(text)), printErr: text => hostLog.push(String(text)) });
    report.factoryHeap = {
      capacityBytes: module.HEAPU8.byteLength,
      bufferBytes: module.HEAPU8.buffer.byteLength,
      bufferIdentity: bufferIdentity(module.HEAPU8.buffer),
    };
    if (module.UTF8ToString(module._melee_web_snapshot_source_identity()) !==
        report.sourceSha256['tests/gameplay_snapshot_probe.cpp']) {
      throw new Error('Snapshot runtime was built from a different gameplay_snapshot_probe.cpp');
    }
    const checkedBeforeOwner = (label, value) => {
      if (value !== 1) throw new Error(`${label}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
    };
    initAttempted = true;
    checkedBeforeOwner('snapshot_init', module.ccall(
      'melee_web_snapshot_init', 'number', ['string'], [options['--assets']]));
    initialized = true;
    report.postInitHeap = {
      capacityBytes: module.HEAPU8.byteLength,
      bufferBytes: module.HEAPU8.buffer.byteLength,
      bufferIdentity: bufferIdentity(module.HEAPU8.buffer),
      bufferReplaced: bufferIdentity(module.HEAPU8.buffer) !== report.factoryHeap.bufferIdentity,
    };
    // Initialization leaves original audio/file transfers pending. The small
    // retained guard locator observed their completion after source sample 0.
    // Execute that same original neutral tick explicitly, then demand native
    // quiescence before binding the owner. Record sample 0 in the forward trace
    // below; no source work or warm-up is hidden and no snapshot guard is waived.
    const bindingStepStart = performance.now();
    checkedBeforeOwner('snapshot_step(binding-sample-0)', module._melee_web_snapshot_step(0));
    const bindingStepMs = performance.now() - bindingStepStart;
    checkedBeforeOwner('snapshot_quiescent(after-sample-0)', module._melee_web_snapshot_quiescent());
    report.ownerBinding = { sample: 0, nativeStepMs: bindingStepMs,
      nativeQuiescence: 'passed', capacityBytes: module.HEAPU8.byteLength,
      bufferIdentity: bufferIdentity(module.HEAPU8.buffer) };
    owner = new SharedPageWasmSnapshotRing(module, {
      maxBytes: MAX_SNAPSHOT_BYTES,
      maxSnapshots: MAX_RING_STATES,
    });
    const ownerStats = owner.stats();
    report.ownerHeap = {
      scope: ownerStats.scope,
      capacityBytes: ownerStats.capacityBytes,
      pageBytes: ownerStats.pageBytes,
      pageCount: ownerStats.pageCount,
      maxRetainedPageBytes: ownerStats.maxRetainedPageBytes,
    };
    const checked = (label, call) => owner.call(() => {
      const value = call();
      if (value && typeof value.then === 'function') return value;
      if (value !== 1) throw new Error(`${label}: ${module.UTF8ToString(module._melee_web_snapshot_error())}`);
      return value;
    });
    const statePointer = module._melee_web_snapshot_observation();
    const stateSize = module._melee_web_snapshot_observation_size();
    const pcmPointer = module._melee_web_snapshot_pcm();
    const pcmSize = module._melee_web_snapshot_pcm_size();
    if (stateSize !== OBSERVATION_BYTES) throw new Error(`Runtime observation size ${stateSize} != ${OBSERVATION_BYTES}`);
    if (pcmSize !== PCM_BYTES) throw new Error(`Runtime PCM size ${pcmSize} != ${PCM_BYTES}`);
    const inputPointer = module._melee_web_snapshot_input();
    report.nativeObservation = {
      bytes: stateSize,
      pcmBytes: pcmSize,
      inputBytes: 44,
      fields: 'source frame/RNG/ready/ending/complete/pause/outcome/winner; stocks/motions/Fireball/object/process counts; PCM; raw PAD sample bytes',
    };
    const viewHeap = () => Buffer.from(module.HEAPU8.buffer, module.HEAPU8.byteOffset, module.HEAPU8.byteLength);
    const observe = ({ pcm = false, input = false, heapSha = false } = {}) => {
      const bytes = copyRange(module, statePointer, stateSize);
      const observed = {
        bytes,
        semantic: observationRecord(bytes),
        observationSha256: sha256(bytes),
      };
      if (pcm) {
        observed.pcm = copyRange(module, pcmPointer, pcmSize);
        observed.pcmSha256 = sha256(observed.pcm);
      }
      if (input) observed.inputHex = copyRange(module, inputPointer, 44).toString('hex');
      if (heapSha) {
        const memory = viewHeap();
        observed.heapSha256 = sha256(memory);
      }
      return observed;
    };
    const recordForward = (sample, observed, forwardStates) => {
      const record = {
        sample,
        semantic: observed.semantic,
        observationSha256: observed.observationSha256,
      };
      if (observed.pcmSha256) record.pcmSha256 = observed.pcmSha256;
      if (observed.inputHex) {
        record.inputHex = observed.inputHex;
        inputSamples.push({ sample, padHex: observed.inputHex });
      }
      if (observed.heapSha256) record.heapSha256 = observed.heapSha256;
      forwardStates.push(record);
    };
    const nativeStep = sample => {
      const stepStart = performance.now();
      checked(`snapshot_step(${sample})`, () => module._melee_web_snapshot_step(sample));
      return performance.now() - stepStart;
    };
    let sample = 1;
    forwardStates = [];
    let entry = observe({ pcm: true, input: true });
    recordForward(0, entry, forwardStates);
    while (!entry.semantic.ready && sample < 600) {
      nativeStep(sample);
      entry = observe({ pcm: true, input: true });
      recordForward(sample, entry, forwardStates);
      sample++;
    }
    if (!entry.semantic.ready) throw new Error('Original Ready did not finish within 600 samples');
    report.entrySamples = sample;
    for (const checkpoint of [300, 318, 420, 438, 500, 540, 558]) {
      while (sample < checkpoint) {
        nativeStep(sample);
        const observed = observe({ pcm: true, input: true });
        recordForward(sample, observed, forwardStates);
        sample++;
      }
      for (const depth of [1, 2, 4, 7]) {
        const initialObserved = observe();
        const cycle = {
          checkpoint,
          sample,
          depth,
          repeats: 3,
          result: 'incomplete',
          initial: { frame: initialObserved.semantic.frame, rng: initialObserved.semantic.rng },
          captureMs: null,
          baselineMs: null,
          restoreMs: [],
          replayMs: [],
          snapshotStats: null,
          firstDivergence: null,
        };
        report.cycles.push(cycle);
        const captureStart = performance.now();
        checked('snapshot_quiescent(cycle)', () => module._melee_web_snapshot_quiescent());
        const snapshot = owner.capture();
        cycle.captureMs = performance.now() - captureStart;
        cycle.snapshotStats = owner.stats();
        const baseline = [];
        const baselineStates = [];
        let baselineCost = 0;
        try {
          for (let offset = 0; offset < depth; offset++) {
            baselineCost += nativeStep(sample + offset);
            const observed = observe({ pcm: true, input: true });
            recordForward(sample + offset, observed, forwardStates);
            baseline.push({
              state: observed.bytes,
              pcm: observed.pcm,
            });
            baselineStates.push({
              sample: sample + offset,
              semantic: observed.semantic,
              observationSha256: observed.observationSha256,
              pcmSha256: observed.pcmSha256,
            });
          }
          cycle.baselineMs = baselineCost;
          // Own an immutable endpoint copy: Buffer.from(ArrayBuffer) is a live view.
          const baselineMemory = Buffer.from(viewHeap());
          cycle.finalMemoryBytes = baselineMemory.byteLength;
          cycle.finalMemorySha256 = sha256(baselineMemory);
          cycle.replayEndpointSha256 = [];
          for (let repeat = 0; repeat < cycle.repeats; repeat++) {
            const restoreStart = performance.now();
            snapshot.restore();
            cycle.restoreMs.push(performance.now() - restoreStart);
            let replayCost = 0;
            for (let offset = 0; offset < depth; offset++) {
              replayCost += nativeStep(sample + offset);
              const observed = observe({ pcm: true, input: true });
              if (!baseline[offset].state.equals(observed.bytes)) {
                cycle.firstDivergence = {
                  repeat, offset, sample: sample + offset, domain: 'observation',
                  ...firstDifference(baseline[offset].state, observed.bytes),
                };
                throw new Error(`Shared-page restore diverged at ${JSON.stringify(cycle.firstDivergence)}`);
              }
              if (!baseline[offset].pcm.equals(observed.pcm)) {
                cycle.firstDivergence = {
                  repeat, offset, sample: sample + offset, domain: 'pcm',
                  ...firstDifference(baseline[offset].pcm, observed.pcm),
                };
                throw new Error(`Shared-page PCM diverged at ${JSON.stringify(cycle.firstDivergence)}`);
              }
            }
            const replayMemory = viewHeap();
            const replaySha256 = sha256(replayMemory);
            cycle.replayEndpointSha256.push(replaySha256);
            if (!baselineMemory.equals(replayMemory)) {
              cycle.firstDivergence = {
                repeat, domain: 'entire-linear-memory',
                ...firstDifference(baselineMemory, replayMemory),
                expectedSha256: cycle.finalMemorySha256,
                actualSha256: replaySha256,
              };
              throw new Error(`Shared-page memory endpoint diverged at ${JSON.stringify(cycle.firstDivergence)}`);
            }
            cycle.replayMs.push(replayCost);
          }
          cycle.result = 'passed';
          writeExclusive(path.join(options['--out'], `baseline-${checkpoint}-${depth}.json`),
            JSON.stringify(baselineStates, null, 2) + '\n');
          sample += depth;
          writeReport();
        } finally {
          snapshot.release();
        }
      }
    }

    const ringStartSample = sample;
    const retained = [];
    const ringForward = [];
    const ringCaptureMs = [];
    const ringCaptureStats = [];
    for (let offset = 0; offset < MAX_RING_STATES; offset++) {
      const captureStart = performance.now();
      checked(`snapshot_quiescent(ring-${offset})`, () => module._melee_web_snapshot_quiescent());
      const snapshot = owner.capture();
      ringCaptureMs.push(performance.now() - captureStart);
      ringCaptureStats.push(owner.stats().lastCapture);
      retained.push({ offset, sample: ringStartSample + offset, snapshot });
      nativeStep(ringStartSample + offset);
      const observed = observe({ pcm: true, input: true, heapSha: true });
      recordForward(ringStartSample + offset, observed, forwardStates);
      ringForward.push({
        offset,
        sample: ringStartSample + offset,
        observation: observed.bytes,
        pcm: observed.pcm,
        observationSha256: observed.observationSha256,
        pcmSha256: observed.pcmSha256,
        heapSha256: observed.heapSha256,
      });
    }
    sample += MAX_RING_STATES;
    const retainedStats = owner.stats();
    const ring = { maxStates: MAX_RING_STATES, startSample: ringStartSample,
      retainedStats, captureMs: ringCaptureMs, captureStats: ringCaptureStats,
      restoreChecks: [], releaseInvalidation: null, closeInvalidation: null };
    report.sharedRing = ring;
    try {
      for (const retainedState of retained) {
        const expected = ringForward[retainedState.offset];
        const restoreStart = performance.now();
        retainedState.snapshot.restore();
        const restoreMs = performance.now() - restoreStart;
        nativeStep(expected.sample);
        const observed = observe({ pcm: true, input: true, heapSha: true });
        const check = {
          offset: retainedState.offset,
          sample: expected.sample,
          restoreMs,
          sameObservation: expected.observation.equals(observed.bytes),
          samePcm: expected.pcm.equals(observed.pcm),
          sameFullHeapSha256: expected.heapSha256 === observed.heapSha256,
        };
        ring.restoreChecks.push(check);
        if (!check.sameObservation || !check.samePcm || !check.sameFullHeapSha256) {
          throw new Error(`Retained ring state diverged at ${JSON.stringify(check)}`);
        }
      }
    } finally {
      const released = retained[0];
      for (const retainedState of retained) retainedState.snapshot.release();
      const releasedStats = owner.stats();
      ring.releasedStats = releasedStats;
      ring.releaseInvalidation = expectFailure(
        () => released.snapshot.restore(), 'restore-after-release');
      ring.releaseInvalidation.release = expectFailure(
        () => released.snapshot.release(), 'release-after-release');
    }
    if (ring.releasedStats.retainedSnapshots !== 0 || ring.releasedStats.uniquePageBytes !== 0) {
      throw new Error(`Shared-page release retained ownership: ${JSON.stringify(ring.releasedStats)}`);
    }
    const controlSnapshot = (() => {
      checked('snapshot_quiescent(negative-control)', () => module._melee_web_snapshot_quiescent());
      return owner.capture();
    })();
    try {
      const controlSample = sample;
      nativeStep(controlSample);
      const expectedControl = observe();
      controlSnapshot.restore();
      const rngPointer = module._melee_web_snapshot_rng_address();
      if (!Number.isSafeInteger(rngPointer) || rngPointer < 0 || rngPointer >= module.HEAPU8.byteLength) {
        throw new Error(`Snapshot RNG pointer is outside linear memory: ${rngPointer}`);
      }
      // Deliberately corrupt one restored source RNG bit only in this test target.
      module.HEAPU8[rngPointer] ^= 1;
      nativeStep(controlSample);
      const changedControl = observe();
      const firstControlDifference = firstDifference(expectedControl.bytes, changedControl.bytes);
      if (!firstControlDifference) throw new Error('Saved-RNG negative control was not detected');
      controlSnapshot.restore();
      nativeStep(controlSample);
      const recoveredControl = observe();
      if (!expectedControl.bytes.equals(recoveredControl.bytes)) {
        throw new Error(`Correct restoration failed after negative control at ${JSON.stringify(
          firstDifference(expectedControl.bytes, recoveredControl.bytes))}`);
      }
      report.negativeControl = {
        result: 'detected',
        sample: controlSample,
        perturbation: 'one restored source RNG bit',
        firstDivergence: firstControlDifference,
        recovery: 'passed',
      };
    } finally {
      controlSnapshot.release();
    }
    const semanticStates = forwardStates.map(row => row.semantic);
    report.coverage = {
      peakFireballs: Math.max(...semanticStates.map(row => row.fireballs)),
      minStocks: [0, 1].map(slot => Math.min(...semanticStates.map(row => row.stocks[slot]))),
      motions: [0, 1].map(slot => [...new Set(semanticStates.map(row => row.motions[slot]))].sort((a, b) => a - b)),
      objectCounts: [...new Set(semanticStates.map(row => row.objects))].sort((a, b) => a - b),
      processCounts: [...new Set(semanticStates.map(row => row.processes))].sort((a, b) => a - b),
      samples: semanticStates.length,
    };
    if (report.coverage.peakFireballs < 1) throw new Error('Snapshot scenario did not exercise a Mario fireball');
    if (report.coverage.minStocks.some(stocks => stocks >= 4)) {
      throw new Error('Snapshot scenario did not exercise both stock losses');
    }
    // Verify owner.close invalidates a still-retained snapshot and drains its
    // references. Native close occurs first, while the owner is still usable.
    checked('snapshot_quiescent(close-invalidation)', () => module._melee_web_snapshot_quiescent());
    const closeSnapshot = owner.capture();
    checked('snapshot_close', () => module._melee_web_snapshot_close());
    initialized = false;
    owner.close();
    ring.closeInvalidation = {
      restore: expectFailure(() => closeSnapshot.restore(), 'restore-after-owner-close'),
      release: expectFailure(() => closeSnapshot.release(), 'release-after-owner-close'),
    };
    ring.finalStats = { closed: true };
    report.sharedRing = ring;
    report.restoreReplayChecks = report.cycles.reduce((sum, cycle) => sum + cycle.repeats, 0);
    report.cycleCount = report.cycles.length;
    report.cleanup = 'source match/audio/world closed; shared-page owner closed; all retained pages released';
    report.result = 'passed';
    report.hostPeakRssBytes = process.resourceUsage().maxRSS * 1024;
    const inputJson = JSON.stringify(inputSamples, null, 2) + '\n';
    const forwardJson = JSON.stringify(forwardStates, null, 2) + '\n';
    writeExclusive(path.join(options['--out'], 'input-samples.json'), inputJson);
    writeExclusive(path.join(options['--out'], 'forward-states.json'), forwardJson);
    report.inputObservationTraceSha256 = sha256(Buffer.from(inputJson));
    report.forwardStatesSha256 = sha256(Buffer.from(forwardJson));
    writeReport();
    writeExclusive(path.join(options['--out'], 'native-host.log'), hostLog.join('\n') + '\n');
    process.stdout.write(JSON.stringify({ result: report.result, cycles: report.cycleCount,
      restoreReplayChecks: report.restoreReplayChecks, output: options['--out'] }) + '\n');
  } catch (error) {
    report.result = 'failed';
    report.failure = String(error?.stack || error);
    try {
      if ((initialized || initAttempted) && module) {
        const result = module._melee_web_snapshot_close();
        report.cleanup = result === 1 ? 'source match/audio/world closed after failure' : 'native close returned failure';
      }
    } catch (cleanupError) {
      report.cleanup = `native close threw after failure: ${String(cleanupError?.stack || cleanupError)}`;
    }
    try { if (owner) owner.close(); } catch (cleanupError) {
      report.cleanup = `${report.cleanup || ''}; shared-page owner close failed: ${String(cleanupError?.stack || cleanupError)}`;
    }
    report.hostPeakRssBytes = process.resourceUsage().maxRSS * 1024;
    try {
      const inputJson = JSON.stringify(inputSamples, null, 2) + '\n';
      const forwardJson = JSON.stringify(forwardStates, null, 2) + '\n';
      writeExclusive(path.join(options['--out'], 'input-samples.json'), inputJson);
      writeExclusive(path.join(options['--out'], 'forward-states.json'), forwardJson);
      report.inputObservationTraceSha256 = sha256(Buffer.from(inputJson));
      report.forwardStatesSha256 = sha256(Buffer.from(forwardJson));
    } catch (_) {}
    try { writeReport(); } catch (_) {}
    try { writeExclusive(path.join(options['--out'], 'failure.json'), JSON.stringify(report, null, 2) + '\n'); } catch (_) {}
    try { writeExclusive(path.join(options['--out'], 'native-host.log'), hostLog.join('\n') + '\n'); } catch (_) {}
    process.stderr.write(`${report.failure}\n`);
    process.exitCode = 1;
  }
}

main().catch(error => { process.stderr.write(`${error?.stack || error}\n`); process.exitCode = 1; });
