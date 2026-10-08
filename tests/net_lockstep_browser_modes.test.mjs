import assert from 'node:assert/strict';
import {test} from 'node:test';
import {spawnSync} from 'node:child_process';
import {mkdtemp, rm} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import {validateLockstepBrowserMode} from '../scripts/net_lockstep_browser_modes.mjs';

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
      /browser-owned input-sampling, positive, disconnect, flip, or native-pump mode/);
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
