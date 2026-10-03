#!/usr/bin/env node
// Real source-runtime restoration experiment; no browser/drawn-state claim.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { performance } from 'node:perf_hooks';
import { QuiescentWasmSnapshot } from '../tests/quiescent_wasm_snapshot.mjs';

const args = process.argv.slice(2);
const options = {};
for (let i = 0; i < args.length; i += 2) {
  if (!['--runtime', '--assets', '--out'].includes(args[i]) || !args[i + 1]) {
    throw new Error('Usage: check_source_snapshot.mjs --runtime FILE --assets DIR --out NEW_DIR');
  }
  if (options[args[i]]) throw new Error(`Duplicate option ${args[i]}`);
  options[args[i]] = path.resolve(args[i + 1]);
}
for (const key of ['--runtime', '--assets', '--out']) if (!options[key]) throw new Error(`Missing ${key}`);
const output = options['--out'];
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.mkdirSync(output); // Never overwrite a previous run or failed reproducer.
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const sourceRoot = path.resolve(import.meta.dirname, '..');
const report = {
  schema: 'melee-web-source-snapshot-feasibility-v1', result: 'incomplete',
  boundary: 'synchronous-source-only-exported-call-return',
  scenario: 'two human Mario players / Final Destination / four stocks',
  sourceDrawing: false, browserGameplay: false, rollbackAdmission: false,
  machine: { cpu: os.cpus()[0].model, arch: os.arch(), os: os.type(), release: os.release(), node: process.version },
  runtime: null, sourceSha256: null, assets: null,
  cycles: [], negativeControl: null, cleanup: null,
  exclusions: ['source draw traversal and hidden draw-dependent simulation', 'GPU and JS renderer ownership',
    'browser input/clock/queue state', 'Web Audio and externally committed sound', 'persistent saves',
    'memory growth after snapshot', 'asynchronous calls, workers and mutable function tables',
    'retail or Slippi semantic equivalence', 'foreground timing, physical input and Internet connectivity'],
};
const writeReport = () => fs.writeFileSync(path.join(output, 'evidence.json'), JSON.stringify(report, null, 2) + '\n');
writeReport();
const hostLog = [];
const inputLog = [];
const forwardStates = [];
let module;
let initialized = false;
try {
  report.runtime = { jsSha256: digest(fs.readFileSync(options['--runtime'])),
    wasmSha256: digest(fs.readFileSync(options['--runtime'].replace(/\.js$/, '.wasm'))) };
  report.sourceSha256 = Object.fromEntries(['tests/gameplay_snapshot_probe.cpp', 'tests/quiescent_wasm_snapshot.mjs',
    'scripts/check_source_snapshot.mjs', 'scripts/build.py', 'cmake/FighterRuntime.cmake', 'dependencies.lock.json']
    .map(name => [name, digest(fs.readFileSync(path.join(sourceRoot, name)))]));
  report.assets = Object.fromEntries(fs.readdirSync(options['--assets']).filter(name =>
    fs.statSync(path.join(options['--assets'], name)).isFile()).sort()
    .map(name => [name, digest(fs.readFileSync(path.join(options['--assets'], name)))]));
  const factory = createRequire(import.meta.url)(options['--runtime']);
  module = await factory({ print: line => hostLog.push(line), printErr: line => hostLog.push(line) });
  if (module.UTF8ToString(module._melee_web_snapshot_source_identity()) !==
      report.sourceSha256['tests/gameplay_snapshot_probe.cpp']) {
    throw new Error('Snapshot runtime was built from a different native fixture; rebuild the target');
  }
  const snapshots = new QuiescentWasmSnapshot(module);
  const checked = fn => snapshots.call(() => {
    const ok = fn();
    if (ok && typeof ok.then === 'function') return ok;
    if (ok !== 1) throw new Error(module.UTF8ToString(module._melee_web_snapshot_error()));
    return ok;
  });
  initialized = true;
  checked(() => module.ccall('melee_web_snapshot_init', 'number', ['string'], [options['--assets']]));
  const capture = () => {
    checked(() => module._melee_web_snapshot_quiescent());
    return snapshots.capture();
  };
  const statePointer = module._melee_web_snapshot_observation();
  const stateSize = module._melee_web_snapshot_observation_size();
  const pcmPointer = module._melee_web_snapshot_pcm();
  const pcmSize = module._melee_web_snapshot_pcm_size();
  const inputPointer = module._melee_web_snapshot_input();
  report.nativeObservation = { bytes: stateSize, pcmBytes: pcmSize,
    fields: 'source frame/RNG/ready/ending/complete/pause/outcome/winner; stocks/motions/Fireball count/object/process counts; both full MeleeWebMatchStats; all semantic PAD history; PCM' };
  const copyRange = (pointer, size) => Buffer.from(module.HEAPU8.slice(pointer, pointer + size));
  const viewHeap = () => Buffer.from(module.HEAPU8.buffer, module.HEAPU8.byteOffset, module.HEAPU8.byteLength);
  const state = () => {
    const bytes = copyRange(statePointer, stateSize);
    return { frame: bytes.readUInt32LE(0), rng: bytes.readUInt32LE(4), ready: bytes.readUInt32LE(8),
      ending: bytes.readUInt32LE(12), complete: bytes.readUInt32LE(16),
      paused: bytes.readUInt32LE(20), pcmFrames: bytes.readUInt32LE(32), audioPhase: bytes.readUInt32LE(36),
      outcome: bytes.readUInt32LE(24), winner: bytes.readInt32LE(28),
      stocks: [bytes.readInt32LE(40), bytes.readInt32LE(44)],
      motions: [bytes.readInt32LE(48), bytes.readInt32LE(52)],
      fireballs: bytes.readInt32LE(56), objects: bytes.readUInt32LE(60), processes: bytes.readUInt32LE(64), bytes };
  };
  const firstDifference = (expected, actual) => {
    const limit = Math.min(expected.length, actual.length);
    for (let i = 0; i < limit; i++) if (expected[i] !== actual[i]) {
      return { offset: i, expected: expected[i], actual: actual[i] };
    }
    return expected.length === actual.length ? null : { offset: limit, expectedLength: expected.length, actualLength: actual.length };
  };
  let sample = 0;
  const recordForward = index => {
    const observed = state();
    inputLog.push({ sample: index, padHex: copyRange(inputPointer, 44).toString('hex') });
    forwardStates.push({ sample: index, ...observed, bytes: undefined,
      observationSha256: digest(observed.bytes), pcmSha256: digest(copyRange(pcmPointer, pcmSize)) });
  };
  const step = (index, record = false) => {
    checked(() => module._melee_web_snapshot_step(index));
    if (record) {
      recordForward(index);
    }
  };
  // Entry/Ready runs naturally through source callbacks. No warm-up is hidden.
  while (!state().ready && sample < 600) step(sample++, true);
  if (!state().ready) throw new Error('Original Ready did not finish within 600 samples');
  report.entrySamples = sample;
  // Different checkpoints exercise movement, combat, jump, guard and Articles.
  for (const checkpoint of [300, 318, 420, 438, 500, 540, 558]) {
    while (sample < checkpoint) step(sample++, true);
    for (const depth of [1, 2, 4, 7]) {
      const cycle = { checkpoint, sample, depth, repeats: 3, result: 'incomplete',
        initial: { frame: state().frame, rng: state().rng }, restoreMs: [], replayMs: [],
        firstDivergence: null };
      report.cycles.push(cycle);
      const begin = performance.now();
      const snapshot = capture();
      cycle.captureMs = performance.now() - begin;
      cycle.snapshotBytes = snapshot.byteLength;
      const baseline = [];
      let baselineCost = 0;
      for (let offset = 0; offset < depth; offset++) {
        const baselineStart = performance.now();step(sample + offset);
        baselineCost += performance.now() - baselineStart;
        recordForward(sample + offset);
        baseline.push({ state: copyRange(statePointer, stateSize), pcm: copyRange(pcmPointer, pcmSize) });
      }
      cycle.baselineMs = baselineCost;
      const baselineMemory = Buffer.from(viewHeap());
      cycle.finalMemorySha256 = digest(baselineMemory);
      cycle.finalState = { ...state(), bytes: undefined };
      fs.writeFileSync(path.join(output, `baseline-${checkpoint}-${depth}.json`), JSON.stringify(
        baseline.map((row, offset) => ({ sample: sample + offset,
          stateHex: row.state.toString('hex'), pcmSha256: digest(row.pcm) })), null, 2) + '\n');
      for (let repeat = 0; repeat < 3; repeat++) {
        const restoreStart = performance.now();snapshot.restore();
        cycle.restoreMs.push(performance.now() - restoreStart);
        let replayCost = 0;
        for (let offset = 0; offset < depth; offset++) {
          const replayStart = performance.now();step(sample + offset);
          replayCost += performance.now() - replayStart;
          for (const [domain, pointer, size] of [['state', statePointer, stateSize], ['pcm', pcmPointer, pcmSize]]) {
            const actual = copyRange(pointer, size);
            if (!baseline[offset][domain].equals(actual)) {
              cycle.firstDivergence = { repeat, offset, sample: sample + offset, domain,
                ...firstDifference(baseline[offset][domain], actual) };
              throw new Error('Restore/replay diverged at ' + JSON.stringify(cycle.firstDivergence));
            }
          }
        }
        cycle.replayMs.push(replayCost);
        if (!baselineMemory.equals(viewHeap())) {
          cycle.firstDivergence = { repeat, domain: 'entire-linear-memory', ...firstDifference(baselineMemory, viewHeap()) };
          throw new Error('Restore/replay memory diverged at ' + JSON.stringify(cycle.firstDivergence));
        }
      }
      cycle.result = 'passed';
      sample += depth;
      writeReport();
    }
  }
  const controlSnapshot = capture();
  const rngPointer = module._melee_web_snapshot_rng_address();
  step(sample);
  const expectedControl = copyRange(statePointer, stateSize);
  controlSnapshot.restore();
  // Deliberately corrupt one restored source RNG bit only in this test target.
  module.HEAPU8[rngPointer] ^= 1;
  step(sample);
  const changedControl = copyRange(statePointer, stateSize);
  if (expectedControl.equals(changedControl)) throw new Error('Saved-RNG negative control was not detected');
  report.negativeControl = { result: 'detected', sample, perturbation: 'one restored source RNG bit',
    firstDivergence: firstDifference(expectedControl, changedControl) };
  controlSnapshot.restore();step(sample, true);
  if (!expectedControl.equals(copyRange(statePointer, stateSize))) throw new Error('Correct restoration failed after negative control');
  report.finalState = { ...state(), bytes: undefined };
  report.coverage = { peakFireballs: Math.max(...forwardStates.map(row => row.fireballs)),
    minStocks: [0, 1].map(slot => Math.min(...forwardStates.map(row => row.stocks[slot]))),
    motions: [0, 1].map(slot => [...new Set(forwardStates.map(row => row.motions[slot]))].sort((a, b) => a - b)),
    objectCounts: [...new Set(forwardStates.map(row => row.objects))].sort((a, b) => a - b),
    processCounts: [...new Set(forwardStates.map(row => row.processes))].sort((a, b) => a - b) };
  if (report.coverage.peakFireballs < 1) throw new Error('Snapshot scenario did not exercise a Mario fireball');
  if (report.coverage.minStocks.some(stocks => stocks >= 4)) throw new Error('Snapshot scenario did not exercise both stock losses');
  checked(() => module._melee_web_snapshot_close());initialized = false;
  snapshots.close();
  if (module.ccall('melee_web_snapshot_init', 'number', ['string'], [options['--assets']]) !== 0 ||
      module._melee_web_snapshot_quiescent() !== 0) {
    throw new Error('Closed one-shot fixture accepted initialization or a snapshot boundary');
  }
  report.cleanup = 'source match/audio/world closed';
  report.result = 'passed';
} catch (error) {
  report.result = 'failed';report.failure = String(error.stack || error);
  if (module && initialized) {
    try { report.cleanup = module._melee_web_snapshot_close() === 1 ? 'source match/audio/world closed after failure' : module.UTF8ToString(module._melee_web_snapshot_error()); }
    catch (cleanupError) { report.cleanup = String(cleanupError); }
  }
  process.exitCode = 1;
} finally {
  report.comparison = 'exact observation and PCM every replayed sample; entire linear memory at each depth endpoint; observation/PCM hashes for every forward sample';
  report.timing = 'capture includes native quiescence check and memory copy; restore is memory/stack restoration; baseline/replay are synchronous checked source steps, excluding host comparison/log copies';
  report.inputScriptSha256 = digest(JSON.stringify(inputLog));
  fs.writeFileSync(path.join(output, 'input-samples.json'), JSON.stringify(inputLog, null, 2) + '\n');
  fs.writeFileSync(path.join(output, 'forward-states.json'), JSON.stringify(forwardStates, null, 2) + '\n');
  report.hostPeakRssBytes = process.resourceUsage().maxRSS * 1024;
  fs.writeFileSync(path.join(output, 'native-host.log'), hostLog.join('\n') + '\n');
  writeReport();
  console.log(JSON.stringify({ result: report.result, cycles: report.cycles.length, output, failure: report.failure }));
}
