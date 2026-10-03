#!/usr/bin/env node
// SPDX-License-Identifier: MIT
// Portable worker for tools/slippi_profile_prefix_check.py.  It intentionally
// consumes only the normalized 44-byte PAD records and compares observations
// at the source step boundary.  It is not a browser or rollback harness.

import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const OBS_SIZE = 1216;
const TRACE_SIZE = 32;
const PAD_TAIL_SIZE = 30 + 3 * 4 * 66;
const PAD_PORT_SIZE = 66;
const OBS_PLAYERS_OFFSET = 72;
const OBS_PLAYER_STRIDE = 160;
const OBS_PAD_OFFSET = OBS_PLAYERS_OFFSET + 2 * OBS_PLAYER_STRIDE;
const INPUT_BYTES = 44;
const PROFILE_OFFSET = 0x1234;
const DEFAULT_SEED = 0x13579bdf;
const NATIVE_INITIALIZER_PROFILE = 'native';
const DEFAULT_INITIALIZER_PROFILE = 'default';
const NATIVE_INITIALIZER_SEED = 4660;
const NATIVE_STAGE_KIND = 37;
const INITIALIZER_DIAGNOSTIC_BYTES = 15 * 4;
const SUPPORTED_SCENE_LASTS = new Set([110, 1341]);

function fail(message) { throw new Error(message); }
function requireValue(value, message) { if (!value) fail(message); return value; }
function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function sha256File(file) { return sha256(fs.readFileSync(file)); }
function requireSha(value, label) {
  if (typeof value !== 'string' || !/^[0-9a-f]{64}$/i.test(value)) fail(`${label} must be a SHA-256 hex digest`);
  return value.toLowerCase();
}
function asHex(value) { return `0x${value.toString(16).padStart(8, '0')}`; }
function utcNow() { return new Date().toISOString(); }

function parseArgs(argv) {
  const allowed = new Set(['--runtime', '--wasm', '--assets', '--inputs', '--probe-sha256',
    '--profile-helper-sha256', '--runtime-sha256', '--wasm-sha256', '--profile-offset',
    '--seed', '--initializer-profile', '--stage-kind-bridge-sha256', '--scene-last', '--out']);
  if (argv.length % 2) fail('worker options must be key/value pairs');
  const options = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i]; const value = argv[i + 1];
    if (!allowed.has(key) || !value || options[key]) fail('malformed worker options');
    options[key] = value;
  }
  const required = ['--runtime', '--wasm', '--assets', '--inputs', '--probe-sha256',
    '--profile-helper-sha256', '--runtime-sha256', '--wasm-sha256', '--profile-offset',
    '--seed', '--scene-last', '--out'];
  for (const key of required) if (!options[key]) fail(`missing ${key}`);
  return options;
}

// Keep option spelling explicit after parseArgs so a typo cannot silently
// become an identity or path field.
function normalizeOptions(raw) {
  const options = {
    runtime: raw['--runtime'], wasm: raw['--wasm'], assets: raw['--assets'],
    inputs: raw['--inputs'], probeSha: raw['--probe-sha256'],
    helperSha: raw['--profile-helper-sha256'], runtimeSha: raw['--runtime-sha256'],
    wasmSha: raw['--wasm-sha256'], profileOffset: Number(raw['--profile-offset']),
    seed: Number(raw['--seed']), initializerProfile: raw['--initializer-profile'] ?? DEFAULT_INITIALIZER_PROFILE,
    stageKindBridgeSha: raw['--stage-kind-bridge-sha256'] || null,
    sceneLast: Number(raw['--scene-last']), out: raw['--out'],
  };
  for (const name of ['runtime', 'wasm', 'assets', 'inputs', 'out'])
    options[name] = path.resolve(options[name]);
  if (options.profileOffset !== PROFILE_OFFSET) fail('profile offset must be 0x1234');
  if (!SUPPORTED_SCENE_LASTS.has(options.sceneLast)) fail('scene-last must be 110 or 1341');
  if (options.initializerProfile !== DEFAULT_INITIALIZER_PROFILE && options.initializerProfile !== NATIVE_INITIALIZER_PROFILE)
    fail('initializer-profile must be default or native');
  if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
    if (options.seed !== NATIVE_INITIALIZER_SEED) fail('native initializer requires seed 4660');
    options.stageKindBridgeSha = requireSha(options.stageKindBridgeSha, 'stage-kind bridge SHA-256');
  } else if (options.stageKindBridgeSha !== null) {
    fail('stage-kind bridge identity is only accepted with native initializer');
  }
  if (!Number.isInteger(options.seed) || options.seed < 0 || options.seed > 0xffffffff)
    fail('seed must be an unsigned 32-bit value');
  if (!fs.existsSync(options.assets) || !fs.statSync(options.assets).isDirectory())
    fail(`assets directory is missing: ${options.assets}`);
  return options;
}

function loadJson(file, label) {
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch (error) { fail(`cannot read ${label}: ${error.message || error}`); }
}

function copyBytes(module, pointer, size, label) {
  if (!Number.isInteger(pointer) || pointer < 0) fail(`${label} pointer is invalid`);
  const bytes = Buffer.from(module.HEAPU8.slice(pointer, pointer + size));
  if (bytes.length !== size) fail(`${label} has ${bytes.length} bytes, expected ${size}`);
  return bytes;
}

function trace(module, pointer) {
  const bytes = copyBytes(module, pointer, TRACE_SIZE, 'RNG profile trace');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    enabled: view.getUint32(0, true), offset: view.getUint32(4, true),
    callback_count: view.getUint32(8, true), callback_frame: view.getUint32(12, true),
    callback_seed: view.getUint32(16, true), source_global_frame: view.getUint32(20, true),
    constructor_requested_seed: view.getUint32(24, true),
    constructor_initial_seed: view.getUint32(28, true), bytes_hex: bytes.toString('hex'),
  };
}

function floatBits(view, offset, little = true) {
  return asHex(view.getUint32(offset, little));
}

function stats(view, offset) {
  return {
    motion_id: view.getInt32(offset + 20, true),
    ground_or_air: view.getInt32(offset + 24, true),
    position_x_bits: floatBits(view, offset + 92),
    position_y_bits: floatBits(view, offset + 96),
    facing_bits: floatBits(view, offset + 104),
    damage_bits: floatBits(view, offset + 140),
    shield_bits: floatBits(view, offset + 144),
    held_buttons: view.getUint32(offset + 124, true),
    stocks: view.getInt32(offset + 148, true),
    fighter_kind: view.getInt32(offset + 152, true),
    source_stick_bits: [floatBits(view, offset + 112), floatBits(view, offset + 116)],
    source_trigger_bits: floatBits(view, offset + 120),
  };
}

function normalizedPad(view, offset) {
  return {
    cstick_bits: [floatBits(view, offset + 40, false), floatBits(view, offset + 44, false)],
  };
}

function decodeObservationBytes(bytes) {
  if (bytes.length !== OBS_SIZE) fail(`observation has ${bytes.length} bytes, expected ${OBS_SIZE}`);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (OBS_PAD_OFFSET + PAD_TAIL_SIZE > bytes.length) fail('observation PAD tail exceeds ABI');
  const players = [stats(view, OBS_PLAYERS_OFFSET), stats(view, OBS_PLAYERS_OFFSET + OBS_PLAYER_STRIDE)];
  const gamePads = [];
  const gameBase = OBS_PAD_OFFSET + 30 + 2 * 4 * PAD_PORT_SIZE;
  for (let port = 0; port < 4; port += 1)
    gamePads.push(normalizedPad(view, gameBase + port * PAD_PORT_SIZE));
  return { bytes, sha256: sha256(bytes), players, gamePads };
}

function decodeInitializerBytes(bytes) {
  if (bytes.length !== INITIALIZER_DIAGNOSTIC_BYTES) fail(`initializer diagnostic has ${bytes.length} bytes, expected ${INITIALIZER_DIAGNOSTIC_BYTES}`);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const players = [0, 1].map(index => ({
    port: view.getUint32(20 + index * 4, true),
    controller: view.getUint32(28 + index * 4, true),
    fighter: view.getUint32(36 + index * 4, true),
    color: view.getUint32(44 + index * 4, true),
    stocks: view.getUint32(52 + index * 4, true),
  }));
  return {valid: view.getUint32(0, true), profile: view.getUint32(4, true),
    seed: view.getUint32(8, true), stage_kind: view.getUint32(12, true),
    player_count: view.getUint32(16, true), players, bytes_hex: bytes.toString('hex')};
}

function initializerContract(module, profile, seed = DEFAULT_SEED) {
  const binary = decodeInitializerBytes(copyBytes(module, module._melee_web_snapshot_initializer_diagnostic(),
    INITIALIZER_DIAGNOSTIC_BYTES, 'initializer diagnostic'));
  const json = JSON.parse(module.UTF8ToString(module._melee_web_snapshot_initializer_json()));
  requireValue(json && json.valid === 1, 'initializer JSON is not valid');
  requireValue(binary.valid === 1 && binary.player_count === 2, 'initializer binary ABI is invalid');
  requireValue(json.profile === binary.profile && json.seed === binary.seed &&
    json.stage_kind === binary.stage_kind && json.player_count === binary.player_count,
    'initializer JSON disagrees with the 60-byte diagnostic');
  requireValue(JSON.stringify(json.players) === JSON.stringify(binary.players),
    'initializer JSON players disagree with the 60-byte diagnostic');
  if (profile === NATIVE_INITIALIZER_PROFILE) {
    requireValue(binary.profile === 1 && binary.seed === NATIVE_INITIALIZER_SEED, 'native initializer profile/seed mismatch');
    requireValue(binary.stage_kind === NATIVE_STAGE_KIND, 'native initializer stage kind differs from actual ground kind 37');
    requireValue(JSON.stringify(binary.players.map(player => player.port)) === JSON.stringify([1, 2]), 'native initializer ports mismatch');
    requireValue(JSON.stringify(binary.players.map(player => player.controller)) === JSON.stringify([0, 1]), 'native initializer controllers mismatch');
    requireValue(binary.players.every(player => player.fighter === 8), 'native initializer fighter kind is not Mario (8)');
    requireValue(binary.players.every(player => player.color === 0 && player.stocks === 4), 'native initializer color/stocks mismatch');
  } else {
    requireValue(binary.profile === 0 && binary.seed === seed, 'default initializer profile/seed mismatch');
    requireValue(JSON.stringify(binary.players.map(player => player.port)) === JSON.stringify([0, 0]), 'default initializer ports changed');
    requireValue(JSON.stringify(binary.players.map(player => player.controller)) === JSON.stringify([0, 1]), 'default initializer controllers changed');
    requireValue(binary.players.every(player => player.fighter === 8), 'default initializer fighter kind is not Mario (8)');
    requireValue(JSON.stringify(binary.players.map(player => player.color)) === JSON.stringify([0, 1]), 'default initializer colors changed');
    requireValue(binary.players.every(player => player.stocks === 4), 'default initializer stocks changed');
  }
  return {binary, json};
}

function initializerContractSelfTest() {
  const bytes = Buffer.alloc(INITIALIZER_DIAGNOSTIC_BYTES);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  [1, 1, NATIVE_INITIALIZER_SEED, NATIVE_STAGE_KIND, 2, 1, 2, 0, 1, 8, 8, 0, 0, 4, 4]
    .forEach((value, index) => view.setUint32(index * 4, value, true));
  const heap = Buffer.alloc(128);
  let json;
  const module = {HEAPU8: heap, _melee_web_snapshot_initializer_diagnostic: () => 16,
    _melee_web_snapshot_initializer_json: () => 112, UTF8ToString: () => JSON.stringify(json)};
  const install = () => {
    heap.set(bytes, 16);
    const {bytes_hex, ...record} = decodeInitializerBytes(bytes);
    json = record;
  };
  const rejects = label => {
    let refused = false;
    try { initializerContract(module, NATIVE_INITIALIZER_PROFILE); } catch { refused = true; }
    requireValue(refused, label);
  };
  install(); initializerContract(module, NATIVE_INITIALIZER_PROFILE);
  view.setUint32(12, 32, true); install();
  rejects('wire stage 32 accepted as source ground kind');
  view.setUint32(12, NATIVE_STAGE_KIND, true); install();
  json.players[1].color = 1;
  rejects('initializer JSON mutation was not detected');
  view.setUint32(24, 1, true); install();
  rejects('duplicate native port was accepted');
  [1, 0, 123, NATIVE_STAGE_KIND, 2, 0, 0, 0, 1, 8, 8, 0, 1, 4, 4]
    .forEach((value, index) => view.setUint32(index * 4, value, true));
  install(); initializerContract(module, DEFAULT_INITIALIZER_PROFILE, 123);
  return true;
}

function observation(module, pointer) {
  return decodeObservationBytes(copyBytes(module, pointer, OBS_SIZE, 'observation'));
}

function transfer(module, pointer) {
  const bytes = copyBytes(module, pointer, 40, 'transfer diagnostic');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    sample: view.getUint32(0, true),
    before_tick_drive: view.getInt32(4, true),
    before_tick_source: view.getInt32(8, true),
    before_tick_bank: view.getInt32(12, true),
    after_tick_drive: view.getInt32(16, true),
    after_tick_source: view.getInt32(20, true),
    after_tick_bank: view.getInt32(24, true),
    after_audio_drive: view.getInt32(28, true),
    after_audio_source: view.getInt32(32, true),
    after_audio_bank: view.getInt32(36, true),
    bytes_hex: bytes.toString('hex'),
  };
}

function fieldDifferences(actual, expected, fields) {
  return fields.filter(field => actual[field] !== expected[field]).map(field => ({
    field, actual: actual[field], expected: expected[field],
  }));
}

function transferQuiescence(module, context) {
  let result = null; let error = null;
  try {
    result = module._melee_web_snapshot_quiescent();
    error = module.UTF8ToString(module._melee_web_snapshot_error()) || null;
  } catch (failure) { error = String(failure && (failure.stack || failure)); }
  return { context, result, success: result === 1, pending: result !== 1, error };
}

function readError(module) {
  try { return module.UTF8ToString(module._melee_web_snapshot_error()) || null; }
  catch (error) { return String(error && (error.stack || error)); }
}

function compareState(actual, expected) {
  const fields = ['fighter_kind', 'motion_id', 'position_x_bits', 'position_y_bits',
    'facing_bits', 'damage_bits', 'shield_bits', 'stocks', 'ground_or_air'];
  return fieldDifferences(actual, expected, fields);
}

function compareProcessed(actual, expected, pad) {
  const differences = [];
  if (JSON.stringify(actual.source_stick_bits) !== JSON.stringify(expected.processed.stick_bits))
    differences.push({ field: 'stick_bits', actual: actual.source_stick_bits, expected: expected.processed.stick_bits });
  if (actual.source_trigger_bits !== expected.processed.trigger_bits)
    differences.push({ field: 'trigger_bits', actual: actual.source_trigger_bits, expected: expected.processed.trigger_bits });
  if (actual.held_buttons !== expected.processed.buttons)
    differences.push({ field: 'buttons', actual: actual.held_buttons, expected: expected.processed.buttons });
  if (JSON.stringify(pad.cstick_bits) !== JSON.stringify(expected.processed.cstick_bits))
    differences.push({ field: 'cstick_bits', actual: pad.cstick_bits, expected: expected.processed.cstick_bits });
  return differences;
}

function decoderSelfTest() {
  const bytes = Buffer.alloc(OBS_SIZE);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const offset = OBS_PLAYERS_OFFSET;
  view.setInt32(offset + 20, 7, true);
  view.setInt32(offset + 24, 2, true);
  view.setUint32(offset + 92, 0x3f800000, true);
  view.setUint32(offset + 96, 0x40000000, true);
  view.setUint32(offset + 104, 0x3f800000, true);
  view.setUint32(offset + 140, 0, true); view.setUint32(offset + 144, 0x42c80000, true);
  view.setUint32(offset + 124, 0x10, true); view.setInt32(offset + 148, 4, true); view.setInt32(offset + 152, 8, true);
  view.setUint32(offset + 112, 0x3f800000, true); view.setUint32(offset + 116, 0, true);
  view.setUint32(offset + 120, 0x3f000000, true);
  const gameBase = OBS_PAD_OFFSET + 30 + 2 * 4 * PAD_PORT_SIZE;
  view.setUint32(gameBase + 40, 0x3e800000, false); view.setUint32(gameBase + 44, 0, false);
  const decoded = decodeObservationBytes(bytes);
  const expectedState = { fighter_kind: 8, motion_id: 7, position_x_bits: '0x3f800000',
    position_y_bits: '0x40000000', facing_bits: '0x3f800000', damage_bits: '0x00000000',
    shield_bits: '0x42c80000', stocks: 4, ground_or_air: 2 };
  requireValue(compareState(decoded.players[0], expectedState).length === 0, 'synthetic state decode failed');
  const expectedInput = { processed: { stick_bits: ['0x3f800000', '0x00000000'],
    trigger_bits: '0x3f000000', buttons: 0x10, cstick_bits: ['0x3e800000', '0x00000000'] } };
  requireValue(compareProcessed(decoded.players[0], expectedInput, decoded.gamePads[0]).length === 0,
    'synthetic processed decode failed');
  const stateMutation = { ...expectedState, motion_id: 8 };
  requireValue(compareState(decoded.players[0], stateMutation).length === 1,
    'synthetic state mutation was not detected');
  const inputMutation = { processed: { ...expectedInput.processed, buttons: 0x11 } };
  requireValue(compareProcessed(decoded.players[0], inputMutation, decoded.gamePads[0]).length === 1,
    'synthetic processed mutation was not detected');
  return true;
}

async function main() {
  const raw = parseArgs(process.argv.slice(2));
  const options = normalizeOptions(raw);
  const report = {
    schema: 'melee-web-source-slippi-profile-runtime-v1', result: 'incomplete',
    started_at_utc: utcNow(), claim_boundary:
      'Source-only bounded prefix. Raw 44-byte PAD records are the sole step inputs; callback, native state and declared fields are observations.',
    scene_range: [0, options.sceneLast],
    profile: { offset: options.profileOffset, seed: options.seed, initializer_profile: options.initializerProfile,
      source_algorithm: 'rotl16(global_frame)+offset' },
    rows: [], controls: {}, cleanup: { attempted: false, result: null, success: false, error: null },
  };
  let module = null; let initAttempted = false; let closeAttempted = false;
  let interrupted = false;
  process.on('SIGINT', () => { interrupted = true; });
  process.on('SIGTERM', () => { interrupted = true; });
  try {
    report.controls.synthetic_decoder_and_comparator = decoderSelfTest();
    report.controls.synthetic_initializer_abi_and_json_contract = initializerContractSelfTest();
    requireValue(sha256File(options.runtime) === options.runtimeSha, 'runtime JS SHA-256 differs');
    requireValue(sha256File(options.wasm) === options.wasmSha, 'runtime Wasm SHA-256 differs');
    const input = loadJson(options.inputs, 'normalized inputs');
    requireValue(input.schema === 'melee-web-source-slippi-profile-input-v1', 'input schema mismatch');
    requireValue(Array.isArray(input.frames) && input.frames.length === options.sceneLast + 1,
      `input must contain ${options.sceneLast + 1} frames`);
    requireValue(JSON.stringify(input.scene_range) === JSON.stringify([0, options.sceneLast]),
      'input scene range differs from --scene-last');
    for (let scene = 0; scene < input.frames.length; scene += 1) {
      const frame = input.frames[scene];
      requireValue(frame.scene_frame === scene, `input scene sequence differs at ${scene}`);
      requireValue(typeof frame.combined_pad_bytes_hex === 'string' && /^[0-9a-f]{88}$/i.test(frame.combined_pad_bytes_hex),
        `scene ${scene} PAD bytes are not exactly 44 bytes`);
      requireValue(frame.inputs?.length === 2, `scene ${scene} lacks two active inputs`);
    }
    report.runtime = { js_sha256: options.runtimeSha, wasm_sha256: options.wasmSha };
    report.source = { probe_sha256: options.probeSha, profile_helper_sha256: options.helperSha };
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) report.source.stage_kind_bridge_sha256 = options.stageKindBridgeSha;
    const factory = createRequire(import.meta.url)(options.runtime);

    // A fresh uninitialized module is the offset negative control.  It does
    // not call init or close, so it cannot own a source match or files.
    const control = await factory({ print: () => {}, printErr: () => {} });
    requireValue(control._melee_web_snapshot_configure_rng_profile(options.profileOffset ^ 1) === 0,
      'invalid profile offset was accepted');
    report.controls.invalid_offset_rejected = true;
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
      requireValue(control._melee_web_snapshot_configure_native_initializer() === 1,
        'native initializer control configure failed');
      requireValue(control._melee_web_snapshot_configure_seed(NATIVE_INITIALIZER_SEED ^ 1) === 0,
        'native initializer accepted a different seed');
      report.controls.native_seed_mismatch_rejected = true;
    }

    module = await factory({ print: () => {}, printErr: () => {} });
    const required = [
      '_melee_web_snapshot_configure_rng_profile', '_melee_web_snapshot_configure_seed',
      '_melee_web_snapshot_init', '_melee_web_snapshot_step_raw', '_melee_web_snapshot_error',
      '_melee_web_snapshot_source_identity', '_melee_web_snapshot_rng_profile_identity',
      '_melee_web_snapshot_rng_profile_observation', '_melee_web_snapshot_rng_profile_observation_size',
      '_melee_web_snapshot_observation', '_melee_web_snapshot_observation_size',
      '_melee_web_snapshot_transfer_diagnostic', '_melee_web_snapshot_transfer_diagnostic_size',
      '_melee_web_snapshot_pcm', '_melee_web_snapshot_pcm_size',
      '_melee_web_snapshot_input', '_melee_web_snapshot_quiescent', '_melee_web_snapshot_close',
    ];
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
      required.push('_melee_web_snapshot_stage_kind_bridge_identity',
        '_melee_web_snapshot_configure_native_initializer',
        '_melee_web_snapshot_initializer_diagnostic',
        '_melee_web_snapshot_initializer_diagnostic_size',
        '_melee_web_snapshot_initializer_json');
    }
    for (const name of required) requireValue(typeof module[name] === 'function', `runtime ABI lacks ${name}`);
    requireValue(module._melee_web_snapshot_observation_size() === OBS_SIZE, 'observation ABI is not 1216 bytes');
    requireValue(module._melee_web_snapshot_rng_profile_observation_size() === TRACE_SIZE, 'trace ABI is not 32 bytes');
    requireValue(module._melee_web_snapshot_transfer_diagnostic_size() === 40, 'transfer ABI is not 40 bytes');
    requireValue(module._melee_web_snapshot_pcm_size() === 4272, 'PCM ABI is not 4272 bytes');
    requireValue(module.UTF8ToString(module._melee_web_snapshot_source_identity()) === options.probeSha,
      'runtime source probe identity differs');
    requireValue(module.UTF8ToString(module._melee_web_snapshot_rng_profile_identity()) === options.helperSha,
      'runtime profile helper identity differs');
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
      requireValue(module.UTF8ToString(module._melee_web_snapshot_stage_kind_bridge_identity()) === options.stageKindBridgeSha,
        'runtime stage-kind bridge identity differs');
      requireValue(module._melee_web_snapshot_initializer_diagnostic_size() === INITIALIZER_DIAGNOSTIC_BYTES,
        'initializer diagnostic ABI is not 15 uint32 fields / 60 bytes');
      requireValue(module._melee_web_snapshot_configure_native_initializer() === 1,
        `native initializer configure failed: ${readError(module)}`);
    }
    requireValue(module._melee_web_snapshot_configure_rng_profile(options.profileOffset) === 1,
      `profile configure failed: ${readError(module)}`);
    if (options.initializerProfile === DEFAULT_INITIALIZER_PROFILE) {
      requireValue(module._melee_web_snapshot_configure_seed(options.seed) === 1,
        `seed configure failed: ${readError(module)}`);
    }
    initAttempted = true;
    requireValue(module.ccall('melee_web_snapshot_init', 'number', ['string'], [options.assets]) === 1,
      `init failed: ${readError(module)}`);
    report.controls.invalid_offset_rejected_after_fresh_factory = true;
    requireValue(module._melee_web_snapshot_configure_rng_profile(options.profileOffset) === 0,
      'profile reconfiguration after init was accepted');
    requireValue(module._melee_web_snapshot_configure_seed(options.seed) === 0,
      'seed reconfiguration after init was accepted');
    report.controls.configuration_after_init_rejected = true;
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
      requireValue(module._melee_web_snapshot_configure_native_initializer() === 0,
        'native initializer reconfiguration after init was accepted');
      report.controls.native_initializer_after_init_rejected = true;
    }

    const tracePointer = module._melee_web_snapshot_rng_profile_observation();
    const observationPointer = module._melee_web_snapshot_observation();
    const inputPointer = module._melee_web_snapshot_input();
    const initialTrace = trace(module, tracePointer);
    requireValue(initialTrace.enabled === 1 && initialTrace.offset === options.profileOffset, 'profile trace is disabled');
    requireValue(initialTrace.callback_count === 0, 'profile callback ran during init');
    requireValue(initialTrace.constructor_requested_seed === options.seed, 'constructor seed trace differs');
    if (options.initializerProfile === NATIVE_INITIALIZER_PROFILE) {
      report.initializer = initializerContract(module, options.initializerProfile);
    }
    const initializationQuiescence = transferQuiescence(module, 'initialization');
    // The measured candidate reports a pending source transfer at init.  This
    // is an observation-only prefix boundary: record it as ineligible for a
    // snapshot, retain the strict native guard, and still feed scene 0 once.
    report.initial_quiescence = {
      ...initializationQuiescence,
      snapshot_eligible: initializationQuiescence.success,
    };
    report.initial_trace = initialTrace;
    const rowsPath = path.join(options.out, 'rows.jsonl');
    fs.writeFileSync(rowsPath, '', { flag: 'wx' });
    for (const frame of input.frames) {
      if (interrupted) fail('worker received supervisor shutdown signal');
      const before = trace(module, tracePointer);
      const pad = Buffer.from(frame.combined_pad_bytes_hex, 'hex');
      module.HEAPU8.set(pad, inputPointer);
      let stepResult = null; let stepError = null;
      try { stepResult = module._melee_web_snapshot_step_raw(); if (stepResult !== 1) stepError = readError(module); }
      catch (error) { stepError = String(error && (error.stack || error)); }
      const after = trace(module, tracePointer);
      const observed = observation(module, observationPointer);
      const pcm = copyBytes(module, module._melee_web_snapshot_pcm(), 4272, 'PCM');
      const transferDiagnostic = transfer(module, module._melee_web_snapshot_transfer_diagnostic());
      const mismatches = [];
      if (stepResult !== 1) mismatches.push('raw_step_failed');
      if (after.callback_count !== before.callback_count + 1) mismatches.push('callback_count');
      if (after.callback_frame !== frame.scene_frame) mismatches.push('callback_frame');
      if (after.source_global_frame !== frame.scene_frame + 1) mismatches.push('source_global_frame');
      if (after.callback_seed !== frame.start_random_seed) mismatches.push('callback_rng');
      const declared = [];
      const processed = [];
      for (const expectedInput of frame.inputs) {
        const port = expectedInput.port - 1;
        const stateDifferences = compareState(observed.players[port], expectedInput.expected);
        const processedDifferences = compareProcessed(observed.players[port], expectedInput, observed.gamePads[port]);
        if (stateDifferences.length) declared.push({ port: expectedInput.port, fields: stateDifferences });
        if (processedDifferences.length) processed.push({ port: expectedInput.port, fields: processedDifferences });
      }
      if (declared.length) mismatches.push('declared_fighter_fields');
      if (processed.length) mismatches.push('processed_input_fields');
      const echo = Buffer.from(module.HEAPU8.slice(inputPointer, inputPointer + INPUT_BYTES));
      if (!echo.equals(pad)) mismatches.push('raw_input_buffer_echo');
      const quiescence = transferQuiescence(module, `post_step:${frame.scene_frame}`);
      const row = {
        scene_frame: frame.scene_frame, recording_frame: frame.recording_frame,
        source_step: frame.scene_frame + 1,
        start_random_seed: frame.start_random_seed, callback_before: before,
        callback_after: after, callback_matches_start_rng: after.callback_seed === frame.start_random_seed,
        declared_fighter_fields_exact: declared.length === 0,
        processed_input_fields_exact: processed.length === 0,
        declared_differences: declared, processed_differences: processed,
        raw_input_echo_exact: echo.equals(pad), observation_sha256: observed.sha256,
        observation_hex: observed.bytes.toString('hex'),
        pcm_sha256: sha256(pcm), transfer_diagnostic: transferDiagnostic,
        source_global_frame_observed: after.source_global_frame,
        native_quiescence: quiescence, step_result: stepResult, step_error: stepError,
        mismatches,
      };
      report.rows.push(row);
      fs.appendFileSync(rowsPath, JSON.stringify(row) + '\n');
      if (mismatches.length) fail(`first divergence at scene ${frame.scene_frame}: ${mismatches.join(',')}`);
    }
    closeAttempted = true;
    const closeResult = module._melee_web_snapshot_close();
    report.cleanup = { attempted: true, result: closeResult, success: closeResult === 1,
      error: closeResult === 1 ? null : readError(module) };
    requireValue(closeResult === 1, `close failed: ${report.cleanup.error}`);
    report.rows_jsonl_sha256 = sha256File(rowsPath);
    report.result = 'passed'; report.finished_at_utc = utcNow();
    fs.writeFileSync(path.join(options.out, 'report.json'), JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
    process.stdout.write(JSON.stringify({ result: report.result, rows: report.rows.length }) + '\n');
  } catch (error) {
    report.result = 'failed'; report.failure = String(error && (error.stack || error));
    if (module && initAttempted && !closeAttempted) {
      closeAttempted = true;
      try {
        const closeResult = module._melee_web_snapshot_close();
        report.cleanup = { attempted: true, result: closeResult, success: closeResult === 1,
          error: closeResult === 1 ? null : readError(module) };
      } catch (closeError) {
        report.cleanup = { attempted: true, result: null, success: false,
          error: String(closeError && (closeError.stack || closeError)) };
      }
    } else if (!report.cleanup.attempted) {
      report.cleanup = { attempted: false, result: null, success: false, error: null };
    }
    const rowsPath = path.join(options.out, 'rows.jsonl');
    if (fs.existsSync(rowsPath)) report.rows_jsonl_sha256 = sha256File(rowsPath);
    report.finished_at_utc = utcNow();
    fs.writeFileSync(path.join(options.out, 'failure.json'), JSON.stringify(report, null, 2) + '\n', { flag: 'wx' });
    process.stderr.write(`${report.failure}\n`);
    process.exitCode = 1;
  }
}

if (process.argv.length === 3 && process.argv[2] === '--self-test') {
  try {
    decoderSelfTest();
    initializerContractSelfTest();
    process.stdout.write(JSON.stringify({ result: 'passed', controls: [
      'synthetic-1216-byte-decoder', 'synthetic-state-mutation',
      'synthetic-processed-mutation', 'synthetic-60-byte-initializer-mutation'
    ] }) + '\n');
  } catch (error) {
    process.stderr.write(`${error.stack || error}\n`);
    process.exitCode = 1;
  }
} else {
  main().catch(error => { process.stderr.write(`${error.stack || error}\n`); process.exitCode = 1; });
}
