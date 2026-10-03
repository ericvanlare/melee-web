#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Source-only pause/mask boundary control. Raw PAD is the sole input.
// This checks the already compiled probe; it does not write pause, mask,
// callback, RNG, or game state and makes no browser/rollback claim.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';

const OBS_SIZE = 1216;
const TRACE_SIZE = 32;
const TRANSFER_SIZE = 40;
const INITIALIZER_SIZE = 15 * 4;
const INPUT_BYTES = 44;
const PROFILE_OFFSET = 0x1234;
const NATIVE_SEED = 4660;
const STAGE_KIND = 37;
const WARMUP_STEPS = 124;
const CONTROL_STEPS = 17;
const SCHEMA = 'melee-web-source-pause-mask-check-v1';
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const sha256File = file => sha256(fs.readFileSync(file));
const fail = message => { throw new Error(message); };
const requireValue = (value, message) => { if (!value) fail(message); return value; };

function parseSha(value, label) {
  requireValue(typeof value === 'string' && /^[0-9a-f]{64}$/i.test(value), `${label} must be a SHA-256 digest`);
  return value.toLowerCase();
}
function parseArgs(argv) {
  if (argv.length === 1 && argv[0] === '--self-test') return {selfTest: true};
  if (argv.length % 2) fail('options must be key/value pairs');
  const allowed = new Set([
    '--runtime', '--assets', '--out', '--probe-sha256', '--cmake-sha256',
    '--profile-helper-sha256', '--stage-kind-bridge-sha256', '--runtime-sha256',
    '--wasm-sha256', '--profile-offset',
  ]);
  const raw = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i]; const value = argv[i + 1];
    if (!allowed.has(key) || !value || raw[key]) fail('malformed options');
    raw[key] = value;
  }
  for (const key of ['--runtime', '--assets', '--out', '--probe-sha256', '--cmake-sha256',
    '--profile-helper-sha256', '--stage-kind-bridge-sha256', '--runtime-sha256', '--wasm-sha256'])
    requireValue(raw[key], `missing ${key}`);
  requireValue(Number(raw['--profile-offset'] || PROFILE_OFFSET) === PROFILE_OFFSET, 'profile offset must be 0x1234');
  const options = {
    runtime: path.resolve(raw['--runtime']), assets: path.resolve(raw['--assets']), out: path.resolve(raw['--out']),
    probeSha: parseSha(raw['--probe-sha256'], 'probe SHA'), cmakeSha: parseSha(raw['--cmake-sha256'], 'CMake SHA'),
    helperSha: parseSha(raw['--profile-helper-sha256'], 'profile helper SHA'),
    bridgeSha: parseSha(raw['--stage-kind-bridge-sha256'], 'stage-kind bridge SHA'),
    runtimeSha: parseSha(raw['--runtime-sha256'], 'runtime JS SHA'), wasmSha: parseSha(raw['--wasm-sha256'], 'runtime Wasm SHA'),
  };
  options.wasm = options.runtime.replace(/\.js$/, '.wasm');
  requireValue(fs.existsSync(options.assets) && fs.statSync(options.assets).isDirectory(), 'assets directory is missing');
  requireValue(!fs.existsSync(options.out), 'output directory must be fresh');
  return options;
}
function copyBytes(module, pointer, size, label) {
  requireValue(Number.isInteger(pointer) && pointer >= 0, `${label} pointer is invalid`);
  const bytes = Buffer.from(module.HEAPU8.slice(pointer, pointer + size));
  requireValue(bytes.length === size, `${label} ABI size is ${bytes.length}, expected ${size}`);
  return bytes;
}
function u32(view, offset) { return view.getUint32(offset, true); }
function i32(view, offset) { return view.getInt32(offset, true); }
function decodeObservation(bytes) {
  requireValue(bytes.length === OBS_SIZE, 'observation ABI mismatch');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {sha256: sha256(bytes), battle_frame: u32(view, 0), ready: u32(view, 8),
    ending: u32(view, 12), complete: u32(view, 16), paused: u32(view, 20),
    stocks: [i32(view, 40), i32(view, 44)]};
}
function decodeTrace(bytes) {
  requireValue(bytes.length === TRACE_SIZE, 'trace ABI mismatch');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {sha256: sha256(bytes), enabled: u32(view, 0), offset: u32(view, 4), callback_count: u32(view, 8),
    callback_frame: u32(view, 12), callback_seed: u32(view, 16), source_global_frame: u32(view, 20),
    constructor_requested_seed: u32(view, 24), constructor_initial_seed: u32(view, 28)};
}
function decodeTransfer(bytes) {
  requireValue(bytes.length === TRANSFER_SIZE, 'transfer ABI mismatch');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {sha256: sha256(bytes), sample: u32(view, 0), before_tick_drive: i32(view, 4), before_tick_source: i32(view, 8),
    before_tick_bank: i32(view, 12), after_tick_drive: i32(view, 16), after_tick_source: i32(view, 20),
    after_tick_bank: i32(view, 24), after_audio_drive: i32(view, 28), after_audio_source: i32(view, 32), after_audio_bank: i32(view, 36)};
}
function parseU64(value, label) {
  requireValue(typeof value === 'string' && /^(0|[1-9][0-9]*)$/.test(value), `${label} must be uint64 decimal`);
  const result = BigInt(value); requireValue(result >= 0n && result <= 0xffffffffffffffffn, `${label} out of range`); return result;
}
function decodeMask(module, allowUninitialized = false) {
  const raw = JSON.parse(module.UTF8ToString(module._melee_web_snapshot_ending_mask_diagnostic()));
  for (const field of ['mask_before', 'scheduler_mask', 'mask_after', 'mask_pointer']) parseU64(raw[field], `mask ${field}`);
  requireValue(raw.valid === 1 || (allowUninitialized && raw.valid === 0), 'mask diagnostic validity mismatch');
  requireValue(Number.isInteger(raw.profile_link) && raw.profile_link >= 0 && raw.profile_link < 64, 'mask profile link invalid');
  return {...raw, initialized: raw.valid === 1};
}
function decodeInitializer(module) {
  const bytes = copyBytes(module, module._melee_web_snapshot_initializer_diagnostic(), INITIALIZER_SIZE, 'initializer diagnostic');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const players = [0, 1].map(index => ({port: u32(view, 20 + index * 4), controller: u32(view, 28 + index * 4),
    fighter: u32(view, 36 + index * 4), color: u32(view, 44 + index * 4), stocks: u32(view, 52 + index * 4)}));
  const binary = {valid: u32(view, 0), profile: u32(view, 4), seed: u32(view, 8), stage_kind: u32(view, 12),
    player_count: u32(view, 16), players, bytes_sha256: sha256(bytes)};
  const json = JSON.parse(module.UTF8ToString(module._melee_web_snapshot_initializer_json()));
  requireValue(binary.valid === 1 && binary.profile === 1 && binary.seed === NATIVE_SEED && binary.stage_kind === STAGE_KIND && binary.player_count === 2,
    'native initializer binary contract mismatch');
  requireValue(JSON.stringify(json.players) === JSON.stringify(players) && json.valid === 1 && json.profile === 1 &&
    json.seed === NATIVE_SEED && json.stage_kind === STAGE_KIND && json.player_count === 2,
    'native initializer JSON disagrees with binary ABI');
  requireValue(players.every(player => player.port === 1 || player.port === 2) && JSON.stringify(players.map(p => p.port)) === JSON.stringify([1, 2]), 'native ports mismatch');
  requireValue(JSON.stringify(players.map(p => p.controller)) === JSON.stringify([0, 1]), 'native controllers mismatch');
  requireValue(players.every(player => player.fighter === 8 && player.color === 0 && player.stocks === 4), 'native Mario initializer fields mismatch');
  return {binary, json};
}
function writePort(bytes, port, buttons, error = 0) { const offset = port * 11; bytes.writeUInt16BE(buttons, offset); bytes[offset + 10] = error & 0xff; }
function pad(buttonPort = 0) {
  const bytes = Buffer.alloc(INPUT_BYTES);
  writePort(bytes, 0, buttonPort === 1 ? 0x1000 : 0); writePort(bytes, 1, buttonPort === 2 ? 0x1000 : 0);
  writePort(bytes, 2, 0, 0xff); writePort(bytes, 3, 0, 0xff); return bytes;
}
function selfTest() {
  const p1 = pad(1); const p2 = pad(2); const neutral = pad(0);
  requireValue(p1.length === INPUT_BYTES && p1.readUInt16BE(0) === 0x1000 && p2.readUInt16BE(11) === 0x1000, 'START PAD encoding failed');
  requireValue(neutral.readUInt16BE(0) === 0 && neutral.readUInt16BE(11) === 0 && neutral[32] === 0xff && neutral[43] === 0xff, 'neutral/disabled PAD encoding failed');
  requireValue(WARMUP_STEPS === 124 && CONTROL_STEPS === 17, 'fixed bounded recipe changed');
  const before = {ending_mask: {initialized: true, mask_after: '0', mask_pointer: '16'},
    trace: {callback_count: 3, callback_frame: 2, source_global_frame: 3}};
  const after = {input_hex: neutral.toString('hex'), ending_mask: {initialized: true,
    scheduler_mask: '0', mask_after: '0', mask_before: '0', mask_pointer: '16',
    profile_link: 7, expected_callback_delta: 1, callback_delta: 1,
    callback_count_before: 3, callback_count_after: 4, source_global_frame_before: 3,
    source_global_frame_after: 4, callback_frame: 3},
    trace: {callback_count: 4, callback_frame: 3, source_global_frame: 4}};
  requireValue(compareStep(before, after, neutral, 1).length === 0, 'valid callback control rejected');
  const changed = structuredClone(after); changed.trace.callback_count++;
  requireValue(compareStep(before, changed, neutral, 1).some(row => row.field === 'callback_count'),
    'callback mutation escaped comparison');
  changed.trace = {...after.trace}; changed.ending_mask.scheduler_mask = '128';
  requireValue(compareStep(before, changed, neutral, 1).some(row => row.field === 'prepared_callback_delta'),
    'prepared mask mutation escaped comparison');
  return {result: 'passed', controls: ['ordinary-start-pad-encoding', 'disabled-pad-encoding',
    'fixed-124-plus-17-recipe', 'callback-count-mutation-detected', 'prepared-mask-mutation-detected']};
}
function readError(module) { try { return module.UTF8ToString(module._melee_web_snapshot_error()) || null; } catch (error) { return String(error?.stack || error); } }
function capture(module, pointers, allowUninitialized = false) {
  return {observation: decodeObservation(copyBytes(module, pointers.observation, OBS_SIZE, 'observation')),
    trace: decodeTrace(copyBytes(module, pointers.trace, TRACE_SIZE, 'trace')),
    transfer: decodeTransfer(copyBytes(module, pointers.transfer, TRANSFER_SIZE, 'transfer')),
    input_hex: copyBytes(module, pointers.input, INPUT_BYTES, 'input').toString('hex'),
    ending_mask: decodeMask(module, allowUninitialized)};
}
function quiescence(module) { try { const result = module._melee_web_snapshot_quiescent(); return {result, success: result === 1, error: readError(module)}; } catch (error) { return {result: null, success: false, error: String(error?.stack || error)}; } }
function compareStep(before, after, rawPad, stepResult) {
  const mismatches = [];
  if (stepResult !== 1) mismatches.push({field: 'source_step', error: 'step returned failure'});
  if (after.input_hex !== rawPad.toString('hex')) mismatches.push({field: 'raw_pad_echo'});
  requireValue(after.ending_mask.initialized, 'successful source step did not publish a valid prepared mask');
  const expectedDelta = (parseU64(after.ending_mask.scheduler_mask, 'scheduler mask') & (1n << BigInt(after.ending_mask.profile_link))) === 0n ? 1 : 0;
  if (after.ending_mask.mask_after !== after.ending_mask.scheduler_mask) mismatches.push({field: 'mask_after_scheduler_mask'});
  if (before.ending_mask.initialized && after.ending_mask.mask_before !== before.ending_mask.mask_after) mismatches.push({field: 'mask_before_previous_mask_after'});
  if (before.ending_mask.initialized && after.ending_mask.mask_pointer !== before.ending_mask.mask_pointer) mismatches.push({field: 'mask_pointer'});
  if (after.ending_mask.expected_callback_delta !== expectedDelta) mismatches.push({field: 'prepared_callback_delta'});
  if (after.ending_mask.callback_delta !== expectedDelta) mismatches.push({field: 'callback_delta'});
  if (after.trace.callback_count !== before.trace.callback_count + expectedDelta) mismatches.push({field: 'callback_count'});
  if (after.trace.source_global_frame !== before.trace.source_global_frame + 1) mismatches.push({field: 'source_global_frame'});
  if (after.trace.callback_frame !== (expectedDelta ? before.trace.source_global_frame : before.trace.callback_frame)) mismatches.push({field: 'callback_frame'});
  for (const [field, expected] of [
    ['callback_count_before', before.trace.callback_count], ['callback_count_after', after.trace.callback_count],
    ['source_global_frame_before', before.trace.source_global_frame], ['source_global_frame_after', after.trace.source_global_frame],
    ['callback_frame', after.trace.callback_frame],
  ]) if (after.ending_mask[field] !== expected) mismatches.push({field: `diagnostic_${field}`});
  return mismatches;
}
async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.selfTest) { process.stdout.write(`${JSON.stringify(selfTest())}\n`); return; }
  fs.mkdirSync(options.out);
  const report = {schema: SCHEMA, result: 'failed', started_at_utc: new Date().toISOString(), finished_at_utc: null,
    boundary: 'Source-only native initializer/pause-mask control. 124 ordinary raw neutral steps precede 17 ordinary PAD controls.',
    source_scope: {raw_pad_only: true, native_initializer_seed: NATIVE_SEED, stage_kind: STAGE_KIND, no_state_writes: true},
    exclusions: ['draw/GPU', 'WebAudio/PCM rollback', 'browser gameplay', 'rollback admission', 'long recorded replay', 'postgame/Results'],
    source: {probe_sha256: options.probeSha, cmake_sha256: options.cmakeSha, profile_helper_sha256: options.helperSha, stage_kind_bridge_sha256: options.bridgeSha},
    runtime: {js_sha256: options.runtimeSha, wasm_sha256: options.wasmSha}, sequence: [], cleanup: {attempted: false, success: false}, first_failure: null};
  let module = null; let initAttempted = false; let closeAttempted = false;
  try {
    requireValue(sha256File(options.runtime) === options.runtimeSha, 'runtime JS SHA differs');
    requireValue(sha256File(options.wasm) === options.wasmSha, 'runtime Wasm SHA differs');
    const factory = createRequire(import.meta.url)(options.runtime);
    const control = await factory({print: () => {}, printErr: () => {}});
    requireValue(control._melee_web_snapshot_configure_rng_profile(PROFILE_OFFSET ^ 1) === 0, 'invalid profile offset accepted');
    module = await factory({print: () => {}, printErr: () => {}});
    for (const name of ['_melee_web_snapshot_configure_rng_profile', '_melee_web_snapshot_configure_native_initializer', '_melee_web_snapshot_init',
      '_melee_web_snapshot_step_raw', '_melee_web_snapshot_error', '_melee_web_snapshot_source_identity', '_melee_web_snapshot_stage_kind_bridge_identity',
      '_melee_web_snapshot_rng_profile_identity', '_melee_web_snapshot_initializer_diagnostic', '_melee_web_snapshot_initializer_diagnostic_size',
      '_melee_web_snapshot_initializer_json', '_melee_web_snapshot_ending_mask_diagnostic', '_melee_web_snapshot_observation',
      '_melee_web_snapshot_rng_profile_observation', '_melee_web_snapshot_transfer_diagnostic', '_melee_web_snapshot_input',
      '_melee_web_snapshot_quiescent', '_melee_web_snapshot_close']) requireValue(typeof module[name] === 'function', `runtime ABI lacks ${name}`);
    requireValue(module._melee_web_snapshot_observation_size() === OBS_SIZE && module._melee_web_snapshot_rng_profile_observation_size() === TRACE_SIZE &&
      module._melee_web_snapshot_transfer_diagnostic_size() === TRANSFER_SIZE && module._melee_web_snapshot_initializer_diagnostic_size() === INITIALIZER_SIZE, 'runtime ABI size mismatch');
    requireValue(module.UTF8ToString(module._melee_web_snapshot_source_identity()) === options.probeSha, 'bare source probe identity differs');
    requireValue(module.UTF8ToString(module._melee_web_snapshot_stage_kind_bridge_identity()) === options.bridgeSha, 'stage-kind bridge identity differs');
    requireValue(module.UTF8ToString(module._melee_web_snapshot_rng_profile_identity()) === options.helperSha, 'profile helper identity differs');
    requireValue(module._melee_web_snapshot_configure_native_initializer() === 1, `native initializer configuration failed: ${readError(module)}`);
    requireValue(module._melee_web_snapshot_configure_rng_profile(PROFILE_OFFSET) === 1, `profile configuration failed: ${readError(module)}`);
    initAttempted = true;
    requireValue(module.ccall('melee_web_snapshot_init', 'number', ['string'], [options.assets]) === 1, `source init failed: ${readError(module)}`);
    const pointers = {observation: module._melee_web_snapshot_observation(), trace: module._melee_web_snapshot_rng_profile_observation(),
      transfer: module._melee_web_snapshot_transfer_diagnostic(), input: module._melee_web_snapshot_input()};
    const initial = capture(module, pointers, true); report.initial = initial; report.initializer = decodeInitializer(module);
    requireValue(initial.trace.enabled === 1 && initial.trace.offset === PROFILE_OFFSET && initial.trace.callback_count === 0 &&
      initial.trace.constructor_requested_seed === NATIVE_SEED, 'native initializer/profile trace mismatch');
    const runStep = (index, label, rawPad) => {
      const before = capture(module, pointers, report.sequence.length === 0);
      module.HEAPU8.set(rawPad, pointers.input);
      const preStep = copyBytes(module, pointers.input, INPUT_BYTES, 'pre-step input');
      let stepResult = null; let stepError = null;
      try { stepResult = module._melee_web_snapshot_step_raw(); if (stepResult !== 1) stepError = readError(module); }
      catch (error) { stepError = String(error?.stack || error); }
      const after = capture(module, pointers);
      const mismatches = compareStep(before, after, rawPad, stepResult);
      if (!preStep.equals(rawPad)) mismatches.push({field: 'pre_step_input'});
      const row = {index, label, raw_pad_hex: rawPad.toString('hex'), pre_step_input_hex: preStep.toString('hex'),
        step_result: stepResult, step_error: stepError, before: {observation: before.observation, trace: before.trace, mask: before.ending_mask},
        after: {observation: after.observation, trace: after.trace, mask: after.ending_mask}, transfer: after.transfer,
        input_echo_hex: after.input_hex, input_echo_exact: after.input_hex === rawPad.toString('hex'), quiescence: quiescence(module), mismatches};
      report.sequence.push(row);
      if (stepResult !== 1 || mismatches.length) { report.first_failure = {index, label, step_result: stepResult, error: stepError, mismatches}; return false; }
      return true;
    };
    for (let index = 0; index < WARMUP_STEPS && !report.first_failure; index += 1) if (!runStep(index, `warmup-neutral-${index}`, pad(0))) break;
    report.ready_boundary = {warmup_steps: WARMUP_STEPS, ready: report.sequence.at(-1)?.after.observation.ready === 1};
    requireValue(report.ready_boundary.ready, 'source did not publish HUD-ready=1 after exactly 124 warmup steps');
    const recipe = [
      ['neutral-before-start', pad(0)], ['start-p1-pulse', pad(1)], ['start-p1-release', pad(0)], ['start-p1-early', pad(1)], ['start-p1-early-release', pad(0)],
      ...Array.from({length: 7}, (_, index) => [`pause-neutral-${index}`, pad(0)]), ['start-p2-pulse', pad(2)], ['start-p2-release', pad(0)],
      ['start-p1-resume', pad(1)], ['start-p1-resume-release', pad(0)], ['resumed-neutral', pad(0)],
    ];
    requireValue(recipe.length === CONTROL_STEPS, 'control recipe length changed');
    for (const [label, rawPad] of recipe) { if (report.first_failure || !runStep(report.sequence.length, label, rawPad)) break; }
    if (!report.first_failure) {
      const row = label => report.sequence.find(item => item.label === label);
      requireValue(row('start-p1-pulse').after.observation.paused === 1, 'P1 pause START did not pause');
      requireValue(row('start-p1-early').after.observation.paused === 1, 'early P1 START was not refused');
      requireValue(row('start-p2-pulse').after.observation.paused === 1, 'P2 START was not refused');
      requireValue(row('start-p1-resume').before.trace.source_global_frame === 138 && row('start-p1-resume').after.observation.paused === 0, 'P1 resume did not occur at source global 138');
      requireValue(row('start-p1-resume-release').before.trace.source_global_frame === 139, 'post-resume next tick was not observed');
      const paused = report.sequence.slice(WARMUP_STEPS).filter(item => item.after.observation.paused === 1);
      requireValue(paused.length === 13, `expected 13 paused advances, observed ${paused.length}`);
      for (const item of paused) requireValue(item.after.observation.battle_frame === item.before.observation.battle_frame &&
        item.after.trace.callback_count === item.before.trace.callback_count, `paused state advanced at ${item.label}`);
      report.pause_resume = {paused_advances: paused.length, early_p1_refused: true, p2_refused: true,
        resume_source_global_frame: row('start-p1-resume').before.trace.source_global_frame, next_tick_source_global_frame: row('start-p1-resume-release').before.trace.source_global_frame};
    }
    report.result = report.first_failure ? 'failed' : 'passed';
  } catch (error) { report.first_failure ??= {index: report.sequence.length, label: 'preflight-or-initialization', error: String(error?.stack || error), mismatches: []}; report.result = 'failed'; }
  finally {
    if (module && initAttempted && !closeAttempted) { closeAttempted = true; try { const result = module._melee_web_snapshot_close(); report.cleanup = {attempted: true, result, success: result === 1, error: result === 1 ? null : readError(module)}; } catch (error) { report.cleanup = {attempted: true, result: null, success: false, error: String(error?.stack || error)}; } }
    report.finished_at_utc = new Date().toISOString(); if (!report.cleanup.success && report.result === 'passed') { report.result = 'failed'; report.first_failure = {label: 'source-close', error: report.cleanup.error || 'source close failed', mismatches: []}; }
    fs.writeFileSync(path.join(options.out, report.result === 'passed' ? 'evidence.json' : 'failure.json'), `${JSON.stringify(report, null, 2)}\n`);
  }
  if (report.result !== 'passed') process.exitCode = 1;
}
main().catch(error => { process.stderr.write(`${error?.stack || error}\n`); process.exitCode = 1; });
