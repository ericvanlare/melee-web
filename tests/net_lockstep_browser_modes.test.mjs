import assert from 'node:assert/strict';
import {test} from 'node:test';
import {spawnSync} from 'node:child_process';
import {mkdtemp, readFile, rm} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import os from 'node:os';
import path from 'node:path';
import {buildRuntimeCssSssGamepadSamples, RUNTIME_CSS_SSS_GENERATOR_SHA256,
  RUNTIME_CSS_SSS_INPUT_TICKS, RUNTIME_CSS_SSS_SOURCE_TICKS,
  validateLockstepBrowserMode, validateRuntimeCssSssRecipe} from '../scripts/net_lockstep_browser_modes.mjs';

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
      /browser-owned input-sampling, positive, disconnect, flip, native-pump, or runtime-css-sss mode/);
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
  assert.throws(() => validateLockstepBrowserMode(options({'script-manifest': 'route.json'})), /only to runtime-css-sss/);
  assert.throws(() => validateLockstepBrowserMode(runtimeCssSssOptions({scenario: 'positive', 'script-manifest': undefined})), /Runtime-owned peers/);
});

function runtimeRecipeFixture() {
  const count = RUNTIME_CSS_SSS_INPUT_TICKS, body = Buffer.alloc(count * 44);
  const port = (tick, player, {buttons = 0, stickX = 0, stickY = 0} = {}) => {
    const offset = tick * 44 + player * 11;
    body.writeUInt16BE(buttons, offset); body.writeInt8(stickX, offset + 2); body.writeInt8(stickY, offset + 3);
  };
  port(0, 0, {stickX: 80, stickY: -40});
  port(1, 1, {stickX: -60, stickY: 20});
  for (let tick = 150; tick <= 152; ++tick) port(tick, 0, {buttons: 0x1000});
  port(153, 0, {buttons: 0x1000, stickX: 80}); // Outside the selected CSS prefix.
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
  const wrongFramesManifest = {...fixture.manifest, frames: fixture.manifest.frames + 1};
  for (const value of [
    {...fixture, seed: fixture.manifest.seed + 1},
    {...fixture, scriptBytes: badScript},
    {...fixture, scriptBytes: badHeader},
    {...fixture, manifestBytes: Buffer.from('{bad json')},
    {...fixture, manifestBytes: Buffer.from(JSON.stringify(wrongFramesManifest))},
    {...fixture, generatorBytes: Buffer.from('unreviewed generator')},
  ]) assert.throws(() => validateRuntimeCssSssRecipe({...value,
    generatorBytes: value.generatorBytes ?? generatorBytes,
    seed: value.seed ?? fixture.manifest.seed}));
  const wrongSeedManifest = {...fixture.manifest, seed: fixture.manifest.seed + 1};
  assert.throws(() => validateRuntimeCssSssRecipe({...fixture,
    manifestBytes: Buffer.from(JSON.stringify(wrongSeedManifest)), generatorBytes, seed: fixture.manifest.seed}), /manifest/);
});

test('runtime CSS-to-SSS Gamepad sample builder round-trips only the recipe prefix and neutral tail', () => {
  const {body} = runtimeRecipeFixture();
  const scriptFrames = Buffer.alloc(16 + body.length); body.copy(scriptFrames, 16);
  const samples = buildRuntimeCssSssGamepadSamples(scriptFrames.subarray(16));
  assert.equal(samples.alpha.length, RUNTIME_CSS_SSS_INPUT_TICKS);
  assert.equal(samples.beta.length, RUNTIME_CSS_SSS_INPUT_TICKS);
  assert.deepEqual(samples.alpha[0].bytes.slice(0, 4), [0, 0, 80, 216]);
  assert.equal(samples.alpha[150].bytes[0], 0x10);
  assert.equal(samples.alpha[153].bytes[0], 0);
  assert.deepEqual(samples.beta[1].bytes.slice(0, 4), [0, 0, 196, 20]);
  assert.deepEqual(samples.alpha[153].bytes, Array(11).fill(0));
  assert.deepEqual(samples.alpha[153].gamepad.axes, [0, 0, 0, 0]);
});
