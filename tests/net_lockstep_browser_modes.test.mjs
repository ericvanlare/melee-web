import assert from 'node:assert/strict';
import {test} from 'node:test';
import {spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {buildRuntimeCssMatchGamepadSamples, buildRuntimeCssSssGamepadSamples,
  RUNTIME_CSS_MATCH_INPUT_TICKS, RUNTIME_CSS_MATCH_SOURCE_TICKS, RUNTIME_CSS_MATCH_STIMULUS_FRAMES,
  RUNTIME_CSS_SSS_GENERATOR_SHA256, RUNTIME_CSS_SSS_INPUT_TICKS, RUNTIME_CSS_SSS_SOURCE_TICKS,
  RUNTIME_FULL_ROUTE_BODY_SHA256, RUNTIME_FULL_ROUTE_GENERATOR_SHA256, RUNTIME_FULL_ROUTE_INPUT_TICKS,
  RUNTIME_FULL_ROUTE_MANIFEST_SHA256, RUNTIME_FULL_ROUTE_SCRIPT_SHA256, RUNTIME_FULL_ROUTE_SEED,
  RUNTIME_FULL_ROUTE_SOURCE_TICKS, buildRuntimeFullRouteGamepadSamples, buildRuntimeFullRouteProfile,
  validateLockstepBrowserMode, validateRuntimeCssMatchRecipe, validateRuntimeCssSssRecipe,
  validateRuntimeFullRouteRecipe} from '../scripts/net_lockstep_browser_modes.mjs';
import {LOCKSTEP_MAX_SOURCE_TICKS} from '../scripts/net_lockstep_core.mjs';

const options = (overrides = {}) => ({scenario: 'positive', 'peer-owner': 'browser',
  'peer-transport': 'webrtc', 'webrtc-signaling': 'room-worker', ...overrides});

test('recorded full route uses page-owned room-signaled WebRTC', () => {
  assert.deepEqual(validateLockstepBrowserMode(options()), {browserOwned: true,
    runtimeOwned: false, peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true});
});

test('input-sampling retains both existing WebRTC signaling modes', () => {
  for (const signaling of ['memory', 'room-worker']) {
    assert.equal(validateLockstepBrowserMode(options({scenario: 'input-sampling',
      'webrtc-signaling': signaling})).roomWorkerSignaling, signaling === 'room-worker');
  }
});

test('positive WebRTC cannot pass SDP through the Node memory coordinator', () => {
  assert.throws(() => validateLockstepBrowserMode(options({'webrtc-signaling': 'memory'})),
    /Positive WebRTC mode requires --webrtc-signaling room-worker/);
});

test('unsupported WebRTC scenarios and owners fail before acquisition', () => {
  for (const scenario of ['probe'])
    assert.throws(() => validateLockstepBrowserMode(options({scenario})),
      /browser-owned input-sampling, positive, disconnect, flip, native-pump, runtime-css-sss, runtime-css-match, or runtime-full-route mode/);
  assert.throws(() => validateLockstepBrowserMode(options({'peer-owner': 'node'})),
    /applies only to browser-owned peers/);
});

test('room signaling requires WebRTC and relay mode still requires a URL', () => {
  assert.throws(() => validateLockstepBrowserMode(options({'peer-transport': 'relay'})),
    /applies only to the local WebRTC transport/);
  assert.throws(() => validateLockstepBrowserMode(options({'peer-transport': 'relay',
    'webrtc-signaling': 'memory'})), /require --relay-url/);
});

test('ordinary positive TCP and browser relay modes keep their defaults', () => {
  assert.deepEqual(validateLockstepBrowserMode(options({'peer-owner': 'node',
    'peer-transport': undefined, 'webrtc-signaling': 'memory'})), {browserOwned: false,
    runtimeOwned: false, peerTransport: 'tcp-loopback', localWebRtc: false, roomWorkerSignaling: false});
  assert.deepEqual(validateLockstepBrowserMode(options({'peer-transport': undefined,
    'webrtc-signaling': 'memory', 'relay-url': 'ws://127.0.0.1:8788'})), {browserOwned: true,
    runtimeOwned: false, peerTransport: 'relay', localWebRtc: false, roomWorkerSignaling: false});
});

test('unknown mode values and Node input-sampling remain rejected', () => {
  for (const [field, value, message] of [['scenario', 'other', /--scenario/],
    ['peer-owner', 'other', /--peer-owner/], ['peer-transport', 'other', /--peer-transport/],
    ['webrtc-signaling', 'other', /--webrtc-signaling/]])
    assert.throws(() => validateLockstepBrowserMode(options({[field]: value})), message);
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'input-sampling',
    'peer-owner': 'node', 'peer-transport': undefined, 'webrtc-signaling': 'memory'})),
    /input-sampling scenario requires --peer-owner browser/);
});

test('disconnect WebRTC requires page-owned room signaling', () => {
  assert.equal(validateLockstepBrowserMode(options({scenario: 'disconnect'})).roomWorkerSignaling, true);
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'disconnect',
    'webrtc-signaling': 'memory'})), /Disconnect WebRTC mode requires/);
});

test('flip WebRTC reuses page-owned room signaling and rejects Node SDP coordination', () => {
  assert.deepEqual(validateLockstepBrowserMode(options({scenario: 'flip'})), {
    browserOwned: true, runtimeOwned: false, peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true});
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'flip',
    'webrtc-signaling': 'memory'})), /Flip WebRTC mode requires/);
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'flip',
    'peer-owner': 'node'})), /applies only to browser-owned peers/);
});

test('runtime-owned diagnostic path keeps protocol and live PAD ownership in the browser runtime', () => {
  const value = options({scenario: 'native-pump', 'peer-owner': 'runtime',
    'peer-transport': undefined, 'source-ticks': '8'});
  assert.deepEqual(validateLockstepBrowserMode(value), {browserOwned: true, runtimeOwned: true,
    peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true});
  for (const override of [{scenario: 'positive'}, {'peer-transport': 'relay'},
    {'webrtc-signaling': 'memory'}])
    assert.throws(() => validateLockstepBrowserMode({...value, ...override}), /Runtime-owned peers require/);
});


test('bounded diagnostic native pump admits only eight-tick page-owned room WebRTC', () => {
  const value = options({scenario: 'native-pump', 'source-ticks': '8'});
  assert.equal(validateLockstepBrowserMode(value).roomWorkerSignaling, true);
  for (const count of ['7', '9', undefined, 'bad'])
    assert.throws(() => validateLockstepBrowserMode({...value, 'source-ticks': count}), /exactly eight/);
  assert.throws(() => validateLockstepBrowserMode({...value, 'webrtc-signaling': 'memory'}), /room-worker/);
  assert.throws(() => validateLockstepBrowserMode({...value, 'peer-owner': 'node'}), /browser-owned/);
  assert.throws(() => validateLockstepBrowserMode({...value, 'peer-transport': 'relay'}), /local WebRTC/);
});

test('runtime-owned native-pump CLI reaches output creation without a synthetic script', async () => {
  const scratch = await mkdtemp(path.join(os.tmpdir(), 'melee-web-runtime-cli-'));
  const existingOutput = path.join(scratch, 'already-exists');
  const {mkdir} = await import('node:fs/promises');
  await mkdir(existingOutput);
  try {
    const result = spawnSync(process.execPath, [path.resolve('scripts/net_lockstep_browser.mjs'),
      '--url', 'http://127.0.0.1:8787/runtime.html', '--disc', path.join(scratch, 'unused.iso'),
      '--seed', '1', '--out', existingOutput, '--scenario', 'native-pump', '--peer-owner', 'runtime',
      '--peer-transport', 'webrtc', '--webrtc-signaling', 'room-worker', '--source-ticks', '8'],
    {cwd: process.cwd(), encoding: 'utf8', timeout: 5000});
    const output = `${result.stdout}\n${result.stderr}`;
    assert.notEqual(result.status, 0, 'existing output directory must stop this reducer before browser launch');
    assert.match(output, /EEXIST|already exists/);
    assert.doesNotMatch(output, /Requested local input workload exceeds the script/);
  } finally {
    await rm(scratch, {recursive: true, force: true});
  }
});


test('runtime nonneutral fixture is explicit and restricted to existing eight-CSS runtime scope', () => {
  const valid = options({scenario: 'native-pump', 'peer-owner': 'runtime', 'source-ticks': '8', 'runtime-input-fixture': 'neutral-a-release'});
  assert.equal(validateLockstepBrowserMode(valid).runtimeInputFixture, true);
  assert.equal(validateLockstepBrowserMode(valid).runtimeInputFixtureName, 'neutral-a-release');
  for (const overrides of [{'runtime-input-fixture': 'A'}, {'peer-owner': 'browser'}, {scenario: 'positive'}, {'source-ticks': '9'}])
    assert.throws(() => validateLockstepBrowserMode({...valid, ...overrides}));
  assert.equal(validateLockstepBrowserMode(options({scenario: 'native-pump', 'peer-owner': 'runtime', 'source-ticks': '8'})).runtimeInputFixture, undefined);
});

const runtimeCssSssOptions = (overrides = {}) => options({scenario: 'runtime-css-sss', 'peer-owner': 'runtime',
  'peer-transport': 'webrtc', 'webrtc-signaling': 'room-worker', 'source-ticks': '520',
  'runtime-input-fixture': 'css-start-to-sss', script: 'route.mwni', 'script-manifest': 'route.json', ...overrides});

test('runtime CSS-to-SSS mode admits only the explicit 520-tick Room Worker fixture contract', () => {
  assert.deepEqual(validateLockstepBrowserMode(runtimeCssSssOptions()), {browserOwned: true, runtimeOwned: true,
    peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true,
    runtimeInputFixture: true, runtimeInputFixtureName: 'css-start-to-sss'});
  for (const overrides of [
    {'source-ticks': '519'}, {'source-ticks': undefined}, {'peer-owner': 'browser'},
    {'peer-transport': 'relay'}, {'webrtc-signaling': 'memory'}, {'runtime-input-fixture': undefined},
    {'runtime-input-fixture': 'neutral-a-release'}, {script: undefined}, {'script-manifest': undefined},
  ]) assert.throws(() => validateLockstepBrowserMode(runtimeCssSssOptions(overrides)),
    /runtime-css-sss|runtime-input-fixture|Runtime-owned peers/);
  assert.throws(() => validateLockstepBrowserMode(options({'script-manifest': 'route.json'})), /only to runtime-css-sss, runtime-css-match or runtime-full-route/);
  assert.throws(() => validateLockstepBrowserMode(runtimeCssSssOptions({scenario: 'positive', 'script-manifest': undefined})), /Runtime-owned peers/);
});

const runtimeCssMatchOptions = (overrides = {}) => options({scenario: 'runtime-css-match', 'peer-owner': 'runtime',
  'peer-transport': 'webrtc', 'webrtc-signaling': 'room-worker', 'source-ticks': '520',
  'runtime-input-fixture': 'css-sss-to-match', script: 'route.mwni', 'script-manifest': 'route.json', ...overrides});

test('runtime CSS-to-match mode admits only the explicit 520-tick Room Worker fixture contract', () => {
  assert.deepEqual(validateLockstepBrowserMode(runtimeCssMatchOptions()), {browserOwned: true, runtimeOwned: true,
    peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true,
    runtimeInputFixture: true, runtimeInputFixtureName: 'css-sss-to-match'});
  for (const overrides of [
    {'source-ticks': '519'}, {'source-ticks': undefined}, {'peer-owner': 'browser'},
    {'peer-transport': 'relay'}, {'webrtc-signaling': 'memory'}, {'runtime-input-fixture': undefined},
    {'runtime-input-fixture': 'css-start-to-sss'}, {script: undefined}, {'script-manifest': undefined},
  ]) assert.throws(() => validateLockstepBrowserMode(runtimeCssMatchOptions(overrides)),
    /runtime-css-match|runtime-input-fixture|Runtime-owned peers/);
  assert.throws(() => validateLockstepBrowserMode(options({'script-manifest': 'route.json'})), /only to runtime-css-sss, runtime-css-match or runtime-full-route/);
  assert.throws(() => validateLockstepBrowserMode(runtimeCssMatchOptions({scenario: 'positive', 'script-manifest': undefined})), /Runtime-owned peers/);
});

function runtimeRecipeFixture() {
  const count = RUNTIME_CSS_SSS_INPUT_TICKS, body = Buffer.alloc(count * 44);
  for (let tick = 0; tick < count; ++tick) {
    body[tick * 44 + 32] = 0xff;
    body[tick * 44 + 43] = 0xff;
  }
  const port = (tick, player, {buttons = 0, stickX = 0, stickY = 0} = {}) => {
    const offset = tick * 44 + player * 11;
    body.writeUInt16BE(buttons, offset); body.writeInt8(stickX, offset + 2); body.writeInt8(stickY, offset + 3);
  };
  port(1, 0, {stickX: 80, stickY: -40});
  port(2, 1, {stickX: -60, stickY: 20});
  for (let tick = 150; tick <= 152; ++tick) port(tick, 0, {buttons: 0x1000});
  for (let tick = 183; tick < 197; ++tick) port(tick, 0, {stickX: 80});
  for (let tick = 197; tick < 202; ++tick) port(tick, 0, {stickY: 80});
  for (let tick = 202; tick < 206; ++tick) port(tick, 0, {stickX: -80, stickY: -80});
  for (let tick = 206; tick < 213; ++tick) port(tick, 0, {stickX: -80});
  for (let tick = 213; tick < 216; ++tick) port(tick, 0, {buttons: 0x0100});
  const header = Buffer.alloc(16); header.write('MWNI'); header.writeUInt32BE(1, 4);
  header.writeUInt32BE(count, 8); header.writeUInt32BE(0, 12);
  const scriptBytes = Buffer.concat([header, body]);
  const digest = bytes => createHash('sha256').update(bytes).digest('hex');
  const manifest = {schema: 'melee-web-net-input-script-v1', version: 1, seed: 305419896,
    frames: count, frame_bytes: 44, human_ports: [0, 1], frames_sha256: digest(body), script_sha256: digest(scriptBytes)};
  return {body, scriptBytes, manifestBytes: Buffer.from(JSON.stringify(manifest)), manifest};
}

test('runtime CSS-to-SSS recipe validation binds format, seed, generated bytes and reviewed generator identity', async () => {
  const fixture = runtimeRecipeFixture();
  const generatorBytes = await readFile(new URL('../tools/net_input_script.py', import.meta.url));
  assert.equal(createHash('sha256').update(generatorBytes).digest('hex'), RUNTIME_CSS_SSS_GENERATOR_SHA256);
  const identity = validateRuntimeCssSssRecipe({...fixture, generatorBytes, seed: fixture.manifest.seed});
  assert.equal(identity.script_sha256, fixture.manifest.script_sha256);
  assert.equal(identity.frame_count, RUNTIME_CSS_SSS_INPUT_TICKS);
  assert.equal(identity.stimulus_frames, 153);
  const badScript = Buffer.from(fixture.scriptBytes); badScript[16] = 1;
  const badHeader = Buffer.from(fixture.scriptBytes); badHeader.writeUInt32BE(RUNTIME_CSS_SSS_INPUT_TICKS - 1, 8);
  const sourceOverflow = Buffer.from(fixture.scriptBytes);
  sourceOverflow.writeUInt32BE(LOCKSTEP_MAX_SOURCE_TICKS + 1, 8);
  const wrongFramesManifest = {...fixture.manifest, frames: fixture.manifest.frames + 1};
  for (const value of [
    {...fixture, seed: fixture.manifest.seed + 1},
    {...fixture, scriptBytes: badScript},
    {...fixture, scriptBytes: badHeader},
    {...fixture, scriptBytes: sourceOverflow},
    {...fixture, manifestBytes: Buffer.from('{bad json')},
    {...fixture, manifestBytes: Buffer.from(JSON.stringify(wrongFramesManifest))},
    {...fixture, generatorBytes: Buffer.from('unreviewed generator')},
  ]) assert.throws(() => validateRuntimeCssSssRecipe({...value,
    generatorBytes: value.generatorBytes ?? generatorBytes,
    seed: value.seed ?? fixture.manifest.seed}));
  const wrongSeedManifest = {...fixture.manifest, seed: fixture.manifest.seed + 1};
  assert.throws(() => validateRuntimeCssSssRecipe({...fixture,
    manifestBytes: Buffer.from(JSON.stringify(wrongSeedManifest)), generatorBytes, seed: fixture.manifest.seed}), /manifest/);
  assert.throws(() => validateRuntimeCssSssRecipe({...fixture, scriptBytes: sourceOverflow, generatorBytes,
    seed: fixture.manifest.seed}), /exceeds the .* source bound/);
});

test('runtime CSS-to-SSS Gamepad sample builder round-trips only the recipe prefix and neutral tail', () => {
  const {body} = runtimeRecipeFixture();
  const scriptFrames = Buffer.alloc(16 + body.length); body.copy(scriptFrames, 16);
  const samples = buildRuntimeCssSssGamepadSamples(scriptFrames.subarray(16));
  assert.equal(samples.alpha.length, RUNTIME_CSS_SSS_INPUT_TICKS);
  assert.equal(samples.beta.length, RUNTIME_CSS_SSS_INPUT_TICKS);
  assert.deepEqual(samples.alpha[0].bytes, Array(11).fill(0));
  assert.deepEqual(samples.beta[0].bytes, Array(11).fill(0));
  assert.deepEqual(samples.alpha[1].bytes.slice(0, 4), [0, 0, 80, 216]);
  assert.equal(samples.alpha[150].bytes[0], 0x10);
  assert.equal(samples.alpha[153].bytes[0], 0);
  assert.deepEqual(samples.beta[2].bytes.slice(0, 4), [0, 0, 196, 20]);
  assert.deepEqual(samples.alpha[153].bytes, Array(11).fill(0));
  assert.deepEqual(samples.alpha[153].gamepad.axes, [0, 0, 0, 0]);
  const nonneutralAlphaFirst = Buffer.from(body); nonneutralAlphaFirst[2] = 1;
  const nonneutralBetaFirst = Buffer.from(body); nonneutralBetaFirst[11 + 2] = 1;
  const invalidPort2 = Buffer.from(body); invalidPort2[22 + 10] = 0;
  const invalidPort3 = Buffer.from(body); invalidPort3[33 + 10] = 0;
  assert.throws(() => buildRuntimeCssSssGamepadSamples(nonneutralAlphaFirst), /first selected sample for alpha must be neutral/);
  assert.throws(() => buildRuntimeCssSssGamepadSamples(nonneutralBetaFirst), /first selected sample for beta must be neutral/);
  assert.throws(() => buildRuntimeCssSssGamepadSamples(invalidPort2), /port 2 is not the canonical no-controller PAD/);
  assert.throws(() => buildRuntimeCssSssGamepadSamples(invalidPort3), /port 3 is not the canonical no-controller PAD/);
});

test('runtime CSS-to-match recipe validation binds the explicit MWNI manifest and reviewed generator', async () => {
  const fixture = runtimeRecipeFixture();
  const generatorBytes = await readFile(new URL('../tools/net_input_script.py', import.meta.url));
  const identity = validateRuntimeCssMatchRecipe({...fixture, generatorBytes, seed: fixture.manifest.seed});
  assert.equal(identity.script_sha256, fixture.manifest.script_sha256);
  assert.equal(identity.frame_count, RUNTIME_CSS_MATCH_INPUT_TICKS);
  assert.equal(identity.stimulus_frames, RUNTIME_CSS_MATCH_STIMULUS_FRAMES);
  for (const value of [
    {...fixture, seed: fixture.manifest.seed + 1},
    {...fixture, generatorBytes: Buffer.from('unreviewed generator')},
    {...fixture, manifestBytes: Buffer.from('{bad json')},
    {...fixture, scriptBytes: Buffer.from(fixture.scriptBytes).subarray(0, -44)},
  ]) assert.throws(() => validateRuntimeCssMatchRecipe({...value, generatorBytes: value.generatorBytes ?? generatorBytes,
    seed: value.seed ?? fixture.manifest.seed}));
});

test('runtime CSS-to-match converts only CSS/SSS recipe frames, maps Start/A and makes the match tail neutral', () => {
  const {body} = runtimeRecipeFixture();
  const samples = buildRuntimeCssMatchGamepadSamples(body);
  assert.equal(samples.alpha.length, RUNTIME_CSS_MATCH_INPUT_TICKS);
  assert.equal(samples.beta.length, RUNTIME_CSS_MATCH_INPUT_TICKS);
  assert.deepEqual(samples.alpha[0].bytes, Array(11).fill(0));
  assert.deepEqual(samples.beta[0].bytes, Array(11).fill(0));
  assert.equal(samples.alpha[150].bytes[0], 0x10);
  assert.equal(samples.alpha[150].gamepad.buttons[9].pressed, true);
  assert.equal(samples.alpha[150].gamepad.buttons[0].pressed, false);
  assert.deepEqual(samples.alpha[183].bytes.slice(2, 4), [80, 0]);
  assert.deepEqual(samples.alpha[197].bytes.slice(2, 4), [0, 80]);
  assert.equal(samples.alpha[213].bytes[1], 0);
  assert.equal(samples.alpha[213].bytes[0], 0x01);
  assert.equal(samples.alpha[213].gamepad.buttons[0].pressed, true);
  assert.equal(samples.alpha[213].gamepad.buttons[9].pressed, false);
  assert.equal(samples.beta[213].bytes[0], 0);
  assert.deepEqual(samples.alpha[304].bytes, Array(11).fill(0));
  assert.deepEqual(samples.alpha[304].gamepad.axes, [0, 0, 0, 0]);
  const invalidPort2 = Buffer.from(body); invalidPort2[303 * 44 + 22 + 10] = 0;
  const invalidPort3 = Buffer.from(body); invalidPort3[303 * 44 + 33 + 10] = 0;
  const unsupportedA = Buffer.from(body); unsupportedA[216 * 44] = 0x10; unsupportedA[216 * 44 + 1] = 0;
  assert.throws(() => buildRuntimeCssMatchGamepadSamples(invalidPort2), /input 303 port 2 is not the canonical no-controller PAD/);
  assert.throws(() => buildRuntimeCssMatchGamepadSamples(invalidPort3), /input 303 port 3 is not the canonical no-controller PAD/);
  assert.throws(() => buildRuntimeCssMatchGamepadSamples(unsupportedA), /input 216 port 0 has unsupported buttons/);
});

const runtimeFullRouteOptions = (overrides = {}) => options({scenario: 'runtime-full-route', 'peer-owner': 'runtime',
  'peer-transport': 'webrtc', 'webrtc-signaling': 'room-worker', 'source-ticks': '5084',
  'runtime-input-fixture': 'full-route', script: 'route1.mwni', 'script-manifest': 'route1.json', ...overrides});

test('runtime full-route admits only the fixed 5082-input/5084-source runtime owner contract', () => {
  assert.deepEqual(validateLockstepBrowserMode(runtimeFullRouteOptions()), {browserOwned: true, runtimeOwned: true,
    peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true,
    runtimeInputFixture: true, runtimeInputFixtureName: 'full-route'});
  for (const overrides of [
    {'source-ticks': '5083'}, {'source-ticks': undefined}, {'peer-owner': 'browser'},
    {'peer-transport': 'relay'}, {'webrtc-signaling': 'memory'}, {'runtime-input-fixture': undefined},
    {'runtime-input-fixture': 'css-sss-to-match'}, {script: undefined}, {'script-manifest': undefined},
  ]) assert.throws(() => validateLockstepBrowserMode(runtimeFullRouteOptions(overrides)),
    /runtime-full-route|runtime-input-fixture|Runtime-owned peers/);
  assert.throws(() => validateLockstepBrowserMode(runtimeFullRouteOptions({scenario: 'positive',
    'script-manifest': undefined, 'runtime-input-fixture': undefined})), /Runtime-owned peers/);
});

test('runtime full-route orchestration maps its CLI fixture name to the registered decoder kind', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
  const start = source.indexOf("  if (runtimeInputFixture) for (const role of ['alpha', 'beta'])");
  const end = source.indexOf("  await Promise.all(['alpha', 'beta'].map(role => instances[role].beginLockstep", start);
  assert(start >= 0 && end > start);
  const rows = {alpha: {}, beta: {}}, installed = [];
  const context = {runtimeInputFixture: true, runtimeFullRoute: true, runtimeLiveRoute: true,
    runtimeInputFixtureName: 'full-route', usedInputs: RUNTIME_FULL_ROUTE_INPUT_TICKS,
    runtimeGamepadSamples: {alpha: [], beta: []}, instanceRows: rows,
    instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {async installRuntimeInputFixture(options) {
      installed.push({role, ...options}); return {variant: options.variant};
    }}]))};
  await vm.runInNewContext(`(async()=>{${source.slice(start, end)}\n})()`, context);
  assert.deepEqual(installed.map(row => [row.role, row.variant]), [
    ['alpha', 'runtime-full-route'], ['beta', 'runtime-full-route']]);
  assert.equal(rows.alpha.runtime_input_fixture_start.variant, 'runtime-full-route');
  assert.equal(rows.beta.runtime_input_fixture_start.variant, 'runtime-full-route');
});

test('runtime full-route recipe validator rejects bad identity, size, trailing bytes and seed', async () => {
  const generatorBytes = await readFile(new URL('../tools/net_input_script.py', import.meta.url));
  assert.equal(createHash('sha256').update(generatorBytes).digest('hex'), RUNTIME_FULL_ROUTE_GENERATOR_SHA256);
  const header = Buffer.alloc(16); header.write('MWNI'); header.writeUInt32BE(1, 4);
  header.writeUInt32BE(RUNTIME_FULL_ROUTE_INPUT_TICKS, 8);
  const body = Buffer.alloc(RUNTIME_FULL_ROUTE_INPUT_TICKS * 44), scriptBytes = Buffer.concat([header, body]);
  const manifestBytes = Buffer.from('{}');
  const exact = (script, manifest = manifestBytes, generator = generatorBytes, seed = RUNTIME_FULL_ROUTE_SEED) =>
    validateRuntimeFullRouteRecipe({scriptBytes: script, manifestBytes: manifest, generatorBytes: generator, seed});
  assert.throws(() => exact(Buffer.concat([scriptBytes, Buffer.from([0])])), /body length|trailing-byte/);
  assert.throws(() => exact(scriptBytes.subarray(0, scriptBytes.length - 1)), /body length|trailing-byte/);
  const badHash = Buffer.from(scriptBytes); badHash[100] ^= 1;
  assert.throws(() => exact(badHash), /identity/);
  assert.throws(() => exact(scriptBytes, manifestBytes, Buffer.from('unreviewed')),
    /generator identity/);
  assert.throws(() => exact(scriptBytes, manifestBytes, generatorBytes, RUNTIME_FULL_ROUTE_SEED + 1),
    /fixed reviewed seed/);
  assert.equal(RUNTIME_FULL_ROUTE_SCRIPT_SHA256.length, 64);
  assert.equal(RUNTIME_FULL_ROUTE_MANIFEST_SHA256.length, 64);
  assert.equal(RUNTIME_FULL_ROUTE_BODY_SHA256.length, 64);
  assert.equal(RUNTIME_FULL_ROUTE_SOURCE_TICKS, RUNTIME_FULL_ROUTE_INPUT_TICKS + 2);
});

test('runtime full-route validator accepts only the exact retained generator output and manifest', async () => {
  const scratch = await mkdtemp(path.join(os.tmpdir(), 'melee-web-full-route-recipe-'));
  const scriptPath = path.join(scratch, 'route1.mwni'), manifestPath = path.join(scratch, 'route1.json');
  const generatorPath = path.resolve('tools/net_input_script.py');
  try {
    const generated = spawnSync('python3', [generatorPath, '--seed', String(RUNTIME_FULL_ROUTE_SEED),
      '--match-end-tick', '4290', '--out', scriptPath, '--manifest', manifestPath],
    {cwd: process.cwd(), encoding: 'utf8', timeout: 30000});
    assert.equal(generated.status, 0, `${generated.stdout}\n${generated.stderr}`);
    const [scriptBytes, manifestBytes, generatorBytes] = await Promise.all([
      readFile(scriptPath), readFile(manifestPath), readFile(generatorPath),
    ]);
    const identity = validateRuntimeFullRouteRecipe({scriptBytes, manifestBytes, generatorBytes,
      seed: RUNTIME_FULL_ROUTE_SEED});
    assert.equal(identity.script_sha256, RUNTIME_FULL_ROUTE_SCRIPT_SHA256);
    assert.equal(identity.manifest_sha256, RUNTIME_FULL_ROUTE_MANIFEST_SHA256);
    assert.equal(identity.body_sha256, RUNTIME_FULL_ROUTE_BODY_SHA256);
    assert.equal(identity.frame_count, RUNTIME_FULL_ROUTE_INPUT_TICKS);
    const mutatedManifest = Buffer.from(JSON.stringify({...JSON.parse(manifestBytes.toString('utf8')),
      seed: RUNTIME_FULL_ROUTE_SEED + 1}));
    assert.throws(() => validateRuntimeFullRouteRecipe({scriptBytes, manifestBytes: mutatedManifest,
      generatorBytes, seed: RUNTIME_FULL_ROUTE_SEED}), /manifest identity/);
  } finally {
    await rm(scratch, {recursive: true, force: true});
  }
});

test('full-route profile keeps independent L/R clicks separate from trigger pressure', () => {
  const profile = buildRuntimeFullRouteProfile();
  assert.equal(Object.isFrozen(profile), true);
  assert.equal(Object.isFrozen(profile.buttons), true);
  assert.equal(Object.isFrozen(profile.axes), true);
  assert.equal(profile.gamecube, true);
  assert.equal('calibrateTriggerOrigin' in profile, false);
  assert.deepEqual(profile.buttons.L, {kind: 'button', index: 4});
  assert.deepEqual(profile.buttons.R, {kind: 'button', index: 8});
  assert.deepEqual(profile.axes.triggerL, {kind: 'button', index: 6});
  assert.deepEqual(profile.axes.triggerR, {kind: 'button', index: 7});
});

function fullRouteBody() {
  const body = Buffer.alloc(RUNTIME_FULL_ROUTE_INPUT_TICKS * 44);
  for (let tick = 0; tick < RUNTIME_FULL_ROUTE_INPUT_TICKS; ++tick) {
    body[tick * 44 + 32] = 0xff;
    body[tick * 44 + 43] = 0xff;
  }
  return body;
}

function fullRoutePad(body, tick, port, {buttons = 0, stickX = 0, stickY = 0,
  cstickX = 0, cstickY = 0, triggerL = 0, triggerR = 0} = {}) {
  const offset = tick * 44 + port * 11;
  body.writeUInt16BE(buttons, offset);
  body.writeInt8(stickX, offset + 2); body.writeInt8(stickY, offset + 3);
  body.writeInt8(cstickX, offset + 4); body.writeInt8(cstickY, offset + 5);
  body[offset + 6] = triggerL; body[offset + 7] = triggerR;
}

test('full-route Gamepad conversion preserves every supported PAD field and independent axes', () => {
  const body = fullRouteBody();
  fullRoutePad(body, 1, 0, {stickX: -128});
  fullRoutePad(body, 2, 0, {stickY: 127});
  fullRoutePad(body, 3, 0, {cstickX: 85});
  fullRoutePad(body, 4, 0, {cstickY: -63});
  fullRoutePad(body, 5, 1, {buttons: 0x0120, stickX: 80, stickY: -40,
    cstickX: -70, cstickY: 30, triggerL: 51, triggerR: 70});
  const samples = buildRuntimeFullRouteGamepadSamples(body);
  assert.equal(samples.alpha.length, 5082);
  assert.equal(samples.beta.length, 5082);
  assert.deepEqual(samples.alpha[1].gamepad.axes, [-1, 0, 0, 0]);
  assert.deepEqual(samples.alpha[2].gamepad.axes, [0, -127 / 128, 0, 0]);
  assert.deepEqual(samples.alpha[3].gamepad.axes, [0, 0, 85 / 128, 0]);
  assert.deepEqual(samples.alpha[4].gamepad.axes, [0, 0, 0, 63 / 128]);
  assert.deepEqual(samples.beta[5].bytes, [0x01, 0x20, 80, 216, (-70) & 255, 30,
    51, 70, 0, 0, 0]);
  assert.equal(samples.beta[5].gamepad.buttons[4].pressed, false);
  assert.equal(samples.beta[5].gamepad.buttons[8].pressed, true);
  assert.equal(samples.beta[5].gamepad.buttons[6].value, 51 / 255);
  assert.equal(samples.beta[5].gamepad.buttons[7].value, 70 / 255);
  assert.throws(() => buildRuntimeFullRouteGamepadSamples(body.subarray(0, body.length - 44)), /exactly 5082/);
  assert.throws(() => buildRuntimeFullRouteGamepadSamples(Buffer.concat([body, Buffer.from([0])])), /exactly 5082/);
});

test('full-route conversion keeps digital trigger clicks independent at every pressure boundary', () => {
  const body = fullRouteBody(), pressures = [0, 70, 242, 243, 255];
  for (let side = 0; side < 2; ++side) for (let index = 0; index < pressures.length; ++index) {
    const tick = side * pressures.length * 2 + index * 2, pressure = pressures[index];
    const clickMask = side === 0 ? 0x0040 : 0x0020, pressureFields = side === 0 ?
      {triggerL: pressure} : {triggerR: pressure};
    fullRoutePad(body, tick, 0, pressureFields);
    fullRoutePad(body, tick + 1, 0, {buttons: clickMask, ...pressureFields});
  }
  const samples = buildRuntimeFullRouteGamepadSamples(body).alpha;
  for (let side = 0; side < 2; ++side) for (let index = 0; index < pressures.length; ++index) {
    const tick = side * pressures.length * 2 + index * 2;
    const clickButton = side === 0 ? 4 : 8, pressureButton = side === 0 ? 6 : 7;
    for (const click of [false, true]) {
      const sample = samples[tick + Number(click)].gamepad;
      assert.equal(sample.buttons[clickButton].pressed, click);
      assert.equal(sample.buttons[pressureButton].value, pressures[index] / 255);
      assert.equal(sample.buttons[side === 0 ? 8 : 4].pressed, false);
    }
  }
  const badMask = fullRouteBody(); badMask[1] = 0x80;
  assert.throws(() => buildRuntimeFullRouteGamepadSamples(badMask), /unsupported PAD fields/);
  const badInactive = fullRouteBody(); badInactive[32] = 0;
  assert.throws(() => buildRuntimeFullRouteGamepadSamples(badInactive), /canonical no-controller/);
});
