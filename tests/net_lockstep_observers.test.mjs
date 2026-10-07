import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import test from 'node:test';
import {readyRenderEvent, renderEventSignatures, verifyFirstChecksumMismatch,
  verifyTerminalHold, WasmResponseIdentityObserver} from '../scripts/net_lockstep_observers.mjs';

function callback(data, kind = 'Native callback') {
  return `${kind} ${JSON.stringify(data)}`;
}

const readyDraw = extra => ({began: 1, drawn: 1, frame: 17, preparation_ms: 0,
  draw_calls: 90, source_draws: 1, draw_suppressed: 0, source: 'Original stage select', ...extra});

test('route readiness requires a new same-phase positive source draw with no preparation', () => {
  const oldDraw = callback(readyDraw({frame: 16}));
  const newDraw = callback(readyDraw({frame: 17}));
  const prior = renderEventSignatures(oldDraw);
  const diagnostics = {phase: 3, running: 1, status: 'Original stage select',
    log: `${oldDraw}\n${newDraw}`};
  assert.deepEqual(readyRenderEvent(diagnostics, 3, prior), {
    phase: 3, kind: 'Native callback', frame: 17, draw_calls: 90,
    source_draws: 1, draw_suppressed: 0, source: 'Original stage select', signature: newDraw,
  });
  assert.equal(readyRenderEvent({...diagnostics, log: oldDraw}, 3, prior), null,
    'a positive draw already present when the boundary watcher starts is stale evidence');
  assert.equal(readyRenderEvent({...diagnostics, running: 0}, 3, new Set()), null);
  assert.equal(readyRenderEvent({...diagnostics, phase: 1}, 3, new Set()), null);
  assert.equal(readyRenderEvent({...diagnostics, status: 'Preparing original next scene...'}, 3, new Set()), null);
  for (const data of [
    readyDraw({draw_suppressed: 1}), readyDraw({draw_calls: 0}),
    readyDraw({source_draws: 0}), readyDraw({source: 'match preparing · ready: 0'}),
    readyDraw({preparation_ms: 0.005}), readyDraw({preparation_ms: undefined}),
    readyDraw({began: 0}),
  ]) assert.equal(readyRenderEvent({...diagnostics, log: callback(data)}, 3, new Set()), null);
});

function nativeStatus({cursor = 14, pushed = 20, kind = 1, tick = 12, channel = 1,
  active = 1, blocker = 'terminal'} = {}) {
  return {active, cursor, pushed, blocker, terminal: {kind, tick, channel}};
}

test('native terminal hold requires both peers terminal and source cursor/pushed to stay fixed', () => {
  const before = {alpha: nativeStatus(), beta: nativeStatus({cursor: 15, pushed: 20})};
  const after = {alpha: nativeStatus(), beta: nativeStatus({cursor: 15, pushed: 20})};
  assert.deepEqual(verifyTerminalHold(before, after, 1, {expectedTick: 12, expectedChannel: 1}), {
    minimum_hold_ms: 120, no_fallback: true,
    peers: {
      alpha: {kind: 1, tick: 12, channel: 1, active: 1, blocker: 'terminal', cursor_before: 14,
        cursor_after: 14, pushed_before: 20, pushed_after: 20},
      beta: {kind: 1, tick: 12, channel: 1, active: 1, blocker: 'terminal', cursor_before: 15,
        cursor_after: 15, pushed_before: 20, pushed_after: 20},
    },
  });
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({cursor: 16, pushed: 20})}, 1), /source advanced/);
  assert.throws(() => verifyTerminalHold(before, {...after, alpha: nativeStatus({active: 0})}, 1), /hold is incomplete/);
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({blocker: 'remote_input'})}, 1), /hold is incomplete/);
  assert.throws(() => verifyTerminalHold(before, {...after, beta: nativeStatus({kind: 2})}, 1), /hold is incomplete/);
});

function checksum(tick, {input = 1, scene = 2} = {}) {
  const bytes = Buffer.alloc(64);
  bytes.writeUInt32LE(tick, 0);
  bytes.writeUInt32LE(scene, 4);
  bytes.writeBigUInt64LE(BigInt(input), 24);
  return bytes;
}

test('raw native checksums retain an equal prefix and first changed PAD channel', () => {
  const alpha = Buffer.concat([checksum(0), checksum(1), checksum(2, {input: 5})]);
  const beta = Buffer.concat([checksum(0), checksum(1), checksum(2, {input: 6})]);
  assert.deepEqual(verifyFirstChecksumMismatch(alpha, beta, 2, 1), {
    equal_prefix_through_tick: 1, first_mismatch_tick: 2, first_mismatch_channel: 1,
  });
  assert.throws(() => verifyFirstChecksumMismatch(
    Buffer.concat([checksum(0), checksum(1, {input: 3}), checksum(2, {input: 5})]), beta, 2, 1),
  /diverged before expected tick/);
  assert.throws(() => verifyFirstChecksumMismatch(
    Buffer.concat([checksum(0), checksum(1), checksum(2, {scene: 3})]), beta, 2, 1),
  /expected channel 1/);
  assert.throws(() => verifyFirstChecksumMismatch(alpha.subarray(0, 128), beta.subarray(0, 128), 2, 1),
    /missing expected mismatch tick/);
});

function fakeResponse(body, {url = 'http://127.0.0.1:18943/gameplay_menu_browser.wasm', status = 200} = {}) {
  return {url: () => url, status: () => status, request: () => ({resourceType: () => 'fetch'}),
    body: () => typeof body === 'function' ? body() : Promise.resolve(body)};
}

test('loaded Wasm identity rejects missing, failed, unreadable, and conflicting response sets', async () => {
  await assert.rejects(new WasmResponseIdentityObserver().freeze(), /No runtime Wasm load response/);
  const failedStatus = new WasmResponseIdentityObserver();
  failedStatus.observe(fakeResponse(Buffer.from('bad'), {status: 503}));
  await assert.rejects(failedStatus.freeze(), /not usable/);
  const failedBody = new WasmResponseIdentityObserver();
  failedBody.observe(fakeResponse(() => Promise.reject(Error('body unavailable'))));
  await assert.rejects(failedBody.freeze(), /body unavailable/);
  const conflicts = new WasmResponseIdentityObserver();
  conflicts.observe(fakeResponse(Buffer.from('wasm A')));
  conflicts.observe(fakeResponse(Buffer.from('wasm B')));
  await assert.rejects(conflicts.freeze(), /conflicting body identities/);
});

test('loaded Wasm response set freezes before the later handshake fresh fetch', async () => {
  let resolveInitial, bodyReadStarted = false;
  const observer = new WasmResponseIdentityObserver();
  observer.observe(fakeResponse(() => {
    bodyReadStarted = true;
    return new Promise(resolve => { resolveInitial = resolve; });
  }));
  assert.equal(bodyReadStarted, true, 'response body capture starts synchronously in the response event');
  const frozen = observer.freeze();
  observer.observe(fakeResponse(Buffer.from('handshake fresh fetch')));
  resolveInitial(Buffer.from('runtime load body'));
  const identity = await frozen;
  assert.equal(identity.sha256, createHash('sha256').update('runtime load body').digest('hex'));
  assert.equal(identity.byte_length, Buffer.byteLength('runtime load body'));
  assert.equal(identity.response_count, 1);
  assert.equal(identity.response_set_frozen_before_handshake_fetch, true);
  assert.equal(identity.response_hashes.length, 1);
});
