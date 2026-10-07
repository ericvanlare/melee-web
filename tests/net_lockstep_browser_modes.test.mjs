import assert from 'node:assert/strict';
import {test} from 'node:test';
import {validateLockstepBrowserMode} from '../scripts/net_lockstep_browser_modes.mjs';

const options = (overrides = {}) => ({scenario: 'positive', 'peer-owner': 'browser',
  'peer-transport': 'webrtc', 'webrtc-signaling': 'room-worker', ...overrides});

test('recorded full route uses page-owned room-signaled WebRTC', () => {
  assert.deepEqual(validateLockstepBrowserMode(options()), {browserOwned: true,
    peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true});
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
      /browser-owned input-sampling, positive, disconnect, or flip mode/);
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
    peerTransport: 'tcp-loopback', localWebRtc: false, roomWorkerSignaling: false});
  assert.deepEqual(validateLockstepBrowserMode(options({'peer-transport': undefined,
    'webrtc-signaling': 'memory', 'relay-url': 'ws://127.0.0.1:8788'})), {browserOwned: true,
    peerTransport: 'relay', localWebRtc: false, roomWorkerSignaling: false});
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
    browserOwned: true, peerTransport: 'webrtc', localWebRtc: true, roomWorkerSignaling: true});
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'flip',
    'webrtc-signaling': 'memory'})), /Flip WebRTC mode requires/);
  assert.throws(() => validateLockstepBrowserMode(options({scenario: 'flip',
    'peer-owner': 'node'})), /applies only to browser-owned peers/);
});
