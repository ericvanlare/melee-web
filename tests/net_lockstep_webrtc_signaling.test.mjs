import assert from 'node:assert/strict';
import test from 'node:test';
import {createRoomWebRtcSignaler} from '../scripts/net_lockstep_webrtc_signaling.mjs';

const ROOM = 'a'.repeat(32);
const offer = {type: 'offer', sdp: 'offer-sdp'};
const answer = {type: 'answer', sdp: 'answer-sdp'};

function fakeRoom({heldReady = [], closeError = null} = {}) {
  const handles = new Map();
  const createEndpoint = options => {
    let resolveReady, rejectReady;
    const ready = new Promise((resolve, reject) => { resolveReady = resolve; rejectReady = reject; });
    ready.catch(() => {});
    const endpoint = {
      options,
      ready,
      closed: false,
      transport: {packet_limit_bytes: 1024 * 1024},
      async send(text) {
        await ready;
        const recipient = handles.get(options.role === 'alpha' ? 'beta' : 'alpha');
        if (!recipient) return;
        queueMicrotask(() => {
          Promise.resolve(recipient.options.onMessage(text)).catch(error =>
            recipient.options.onEndpointError(recipient.options.role, error));
        });
      },
      async close() {
        if (closeError) throw closeError;
        endpoint.closed = true;
        options.onDisconnect(options.role, '1000: client closed');
      },
      destroy() { endpoint.closed = true; },
      resolveReady,
      rejectReady,
    };
    handles.set(options.role, endpoint);
    if (!heldReady.includes(options.role)) resolveReady(endpoint);
    return endpoint;
  };
  return {createEndpoint, handles};
}

function create(room, role, callbacks = {}, timeoutMs = 500) {
  return createRoomWebRtcSignaler({url: 'ws://127.0.0.1:8787', roomId: ROOM, role,
    timeoutMs, createEndpoint: room.createEndpoint,
    createOffer: callbacks.createOffer ?? (async () => offer),
    acceptOffer: callbacks.acceptOffer ?? (async () => answer),
    acceptAnswer: callbacks.acceptAnswer ?? (async () => {})});
}

async function until(predicate) {
  const deadline = Date.now() + 500;
  while (!predicate()) {
    if (Date.now() >= deadline) throw Error('condition timed out');
    await new Promise(resolve => setTimeout(resolve, 0));
  }
}

test('beta installs waiting-offer before READY and completes an offer delivered while READY is pending', async () => {
  const room = fakeRoom({heldReady: ['beta']});
  let accepted = 0;
  const beta = create(room, 'beta', {acceptOffer: async value => {
    ++accepted;
    assert.deepEqual(value, offer);
    return answer;
  }});
  assert.equal(beta.snapshot().phase, 'waiting-offer');
  const alpha = create(room, 'alpha');
  await until(() => beta.snapshot().phase === 'answer-sending');
  assert.equal(accepted, 1);
  assert.equal(room.handles.get('beta').ready instanceof Promise, true);
  room.handles.get('beta').resolveReady(room.handles.get('beta'));
  await Promise.all([alpha.negotiated, beta.negotiated]);
  assert.equal(alpha.snapshot().phase, 'negotiated');
  assert.equal(beta.snapshot().phase, 'negotiated');
  assert.equal(JSON.stringify(alpha.snapshot()).includes('sdp'), false);
  assert.equal(JSON.stringify(beta.snapshot()).includes('sdp'), false);
});

test('rejects malformed, wrong-room, wrong-role, and out-of-order messages without exposing SDP', async t => {
  const cases = [
    ['malformed JSON', '{', 'invalid-signal-json'],
    ['unsupported version', JSON.stringify({protocol: 'melee-local-webrtc', version: 2, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: offer}), 'invalid-signal-envelope'],
    ['unknown envelope field', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: offer, extra: true}), 'invalid-signal-envelope'],
    ['wrong room', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: 'b'.repeat(32),
      from: 'alpha', to: 'beta', kind: 'offer', description: offer}), 'invalid-signal-envelope'],
    ['wrong role', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'beta', to: 'beta', kind: 'offer', description: offer}), 'invalid-signal-envelope'],
    ['out-of-order answer', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'answer', description: answer}), 'unexpected-offer-state'],
    ['description with wrong type', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: answer}), 'invalid-session-description'],
    ['description without SDP', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: {type: 'offer'}}), 'invalid-session-description'],
    ['description with empty SDP', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: {type: 'offer', sdp: ''}}), 'invalid-session-description'],
    ['description with unknown field', JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: {...offer, extra: true}}), 'invalid-session-description'],
  ];
  for (const [name, text, code] of cases) await t.test(name, async () => {
    const room = fakeRoom();
    const beta = create(room, 'beta');
    await assert.rejects(room.handles.get('beta').options.onMessage(text), new RegExp(code));
    assert.equal(beta.snapshot().failure_code, code);
    assert.equal(JSON.stringify(beta.snapshot()).includes('sdp'), false);
    assert.throws(() => beta.assertHealthy(), new RegExp(code));
  });
});

test('rejects duplicate offers after beta accepts the first one', async () => {
  const room = fakeRoom();
  const beta = create(room, 'beta');
  const text = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
    from: 'alpha', to: 'beta', kind: 'offer', description: offer});
  await room.handles.get('beta').options.onMessage(text);
  assert.equal(beta.snapshot().phase, 'negotiated');
  await assert.rejects(room.handles.get('beta').options.onMessage(text), /unexpected-offer-state/);
  assert.equal(beta.snapshot().failure_code, 'unexpected-offer-state');
});

test('rejects duplicate answers after alpha accepts the first one', async () => {
  const room = fakeRoom();
  const alpha = create(room, 'alpha');
  await until(() => alpha.snapshot().phase === 'offer-sent');
  const text = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
    from: 'beta', to: 'alpha', kind: 'answer', description: answer});
  await room.handles.get('alpha').options.onMessage(text);
  await alpha.negotiated;
  assert.equal(alpha.snapshot().phase, 'negotiated');
  await assert.rejects(room.handles.get('alpha').options.onMessage(text), /unexpected-answer-state/);
  assert.equal(alpha.snapshot().failure_code, 'unexpected-answer-state');
});

test('offer and answer callback rejections become bounded sticky failures', async t => {
  await t.test('createOffer rejection', async () => {
    const room = fakeRoom();
    const alpha = create(room, 'alpha', {createOffer: async () => { throw Error('private offer detail'); }});
    await assert.rejects(alpha.negotiated, /offer-creation-failed/);
    assert.equal(alpha.snapshot().failure_code, 'offer-creation-failed');
    assert.equal(JSON.stringify(alpha.snapshot()).includes('private offer detail'), false);
  });

  await t.test('acceptOffer rejection', async () => {
    const room = fakeRoom();
    const beta = create(room, 'beta', {acceptOffer: async () => { throw Error('private offer detail'); }});
    const text = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: offer});
    await assert.rejects(room.handles.get('beta').options.onMessage(text), /offer-application-failed/);
    assert.equal(beta.snapshot().failure_code, 'offer-application-failed');
    assert.equal(JSON.stringify(beta.snapshot()).includes('private offer detail'), false);
  });

  await t.test('acceptAnswer rejection', async () => {
    const room = fakeRoom();
    const alpha = create(room, 'alpha', {acceptAnswer: async () => { throw Error('private answer detail'); }});
    await until(() => alpha.snapshot().phase === 'offer-sent');
    const text = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'beta', to: 'alpha', kind: 'answer', description: answer});
    await assert.rejects(room.handles.get('alpha').options.onMessage(text), /answer-application-failed/);
    assert.equal(alpha.snapshot().failure_code, 'answer-application-failed');
    assert.equal(JSON.stringify(alpha.snapshot()).includes('private answer detail'), false);
  });
});

test('endpoint failure and unexpected post-negotiation close remain sticky control-plane errors', async () => {
  const room = fakeRoom();
  const alpha = create(room, 'alpha');
  await until(() => alpha.snapshot().phase === 'offer-sent');
  await room.handles.get('alpha').options.onMessage(JSON.stringify({protocol: 'melee-local-webrtc', version: 1,
    roomId: ROOM, from: 'beta', to: 'alpha', kind: 'answer', description: answer}));
  await alpha.negotiated;
  room.handles.get('alpha').options.onDisconnect('alpha', '1006: peer disconnected');
  assert.equal(alpha.snapshot().failure_code, 'signaling-disconnected');
  assert.throws(() => alpha.assertHealthy(), /signaling-disconnected/);
});

test('armed endpoint close notifications are expected and close failures remain visible', async t => {
  const room = fakeRoom();
  const beta = create(room, 'beta');
  const text = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
    from: 'alpha', to: 'beta', kind: 'offer', description: offer});
  await room.handles.get('beta').options.onMessage(text);
  await beta.negotiated;
  beta.armClose();
  await beta.close();
  assert.equal(beta.snapshot().failure_code, null);
  assert.equal(beta.snapshot().phase, 'closed');
  assert.equal(beta.snapshot().endpoint_closed, true);

  await t.test('repeated close keeps the original cleanup rejection', async () => {
    const closeError = Error('endpoint close failed');
    const failingRoom = fakeRoom({closeError});
    const signaler = create(failingRoom, 'beta');
    const incomingOffer = JSON.stringify({protocol: 'melee-local-webrtc', version: 1, roomId: ROOM,
      from: 'alpha', to: 'beta', kind: 'offer', description: offer});
    await failingRoom.handles.get('beta').options.onMessage(incomingOffer);
    await signaler.negotiated;
    await assert.rejects(signaler.close(), /endpoint close failed/);
    await assert.rejects(signaler.close(), /endpoint close failed/);
  });
});

test('negotiation timeout and endpoint readiness failure are explicit', async t => {
  const room = fakeRoom({heldReady: ['beta']});
  const beta = create(room, 'beta', {}, 20);
  await assert.rejects(beta.negotiated, /signaling-negotiation-timeout/);
  assert.equal(beta.snapshot().failure_code, 'signaling-negotiation-timeout');

  await t.test('READY rejection', async () => {
    const failingRoom = fakeRoom({heldReady: ['beta']});
    const signaler = create(failingRoom, 'beta');
    failingRoom.handles.get('beta').rejectReady(Error('private transport detail'));
    await assert.rejects(signaler.negotiated, /room-ready-failed/);
    assert.equal(signaler.snapshot().failure_code, 'room-ready-failed');
    assert.equal(JSON.stringify(signaler.snapshot()).includes('private transport detail'), false);
  });
});
