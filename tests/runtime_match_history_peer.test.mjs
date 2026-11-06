// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {webcrypto} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {
  buildRuntimeMatchHistoryPeerAgreement,
  createRuntimeMatchHistoryPeer,
  RUNTIME_MATCH_HISTORY_PEER_PROFILE,
  RUNTIME_MATCH_HISTORY_PEER_SCOPE,
  RUNTIME_MATCH_HISTORY_PROTOCOL_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_PROTOCOL_SOURCE_TICKS,
} from '../src/slippi_profile/runtime_match_history_peer.mjs';
import {LOCKSTEP_VERSION, lockstepConstants} from '../scripts/net_lockstep_core.mjs';
import {installRuntimePeerHistoryProbe} from '../src/slippi_profile/tests/runtime_peer_history_probe.mjs';

const loadedIdentity = Object.freeze({
  runtime_sha256: '1'.repeat(64),
  asset_identity_sha256: '2'.repeat(64),
  save_profile_sha256: '3'.repeat(64),
  profile_identity: RUNTIME_MATCH_HISTORY_PEER_PROFILE,
});

test('actual page bridge routes arena enable through the serialized episode owner', async t => {
  assert.equal(globalThis.__meleeRuntimePeerHistoryProbe, undefined);
  assert.equal(globalThis.__meleeRuntimeMatchHistory, undefined);
  t.after(() => {
    delete globalThis.__meleeRuntimePeerHistoryProbe;
    delete globalThis.__meleeRuntimeMatchHistory;
  });
  let enables = 0;
  globalThis.__meleeRuntimeMatchHistory = {
    enableArenaHistory() { ++enables; return {enabled: true}; },
    enableEpisode() { throw Error('Wrong episode enable path'); },
    validatePad() { return true; },
  };
  const options = {role: 'alpha', relayUrl: 'ws://127.0.0.1:8787',
    roomId: 'a'.repeat(24), sourceIdentityJson: identityText(0), loadedIdentity,
    initialRemotePad: [...Buffer.from(lockstepConstants.neutralPad, 'hex')]};
  installRuntimePeerHistoryProbe({...options, inputTicks: 4});
  assert.deepEqual(await globalThis.__meleeRuntimePeerHistoryProbe.call('arenaEnable'),
    {enabled: true});
  assert.equal(enables, 1);
  delete globalThis.__meleeRuntimePeerHistoryProbe;
  installRuntimePeerHistoryProbe({...options, inputTicks: 1});
  await assert.rejects(globalThis.__meleeRuntimePeerHistoryProbe.call('arenaEnable'),
    /Episode timeline owner unavailable/);
  assert.equal(enables, 1);
});

function sourceIdentity(sourceFrame = 120, overrides = {}) {
  const rules = Object.fromEntries([
    'on_unpause_override', 'on_pause_override', 'check_for_pauser_override',
    'on_match_start', 'on_frame_start', 'on_frame_end', 'on_match_end',
    'x54_pointer', 'x58_pointer',
  ].map(name => [name, 'null']));
  return {
    schema: 'melee-web-runtime-match-identity-v1',
    ...loadedIdentity,
    source_frame: sourceFrame,
    initial_selection_seed: '01234567',
    current_rng: '89abcdef',
    selection: {
      player_count: 2, opening_demo: 0, sudden_death: 0,
      controller_mapping: [
        {source_slot: 0, source_port: 0, raw_controller: 0},
        {source_slot: 1, source_port: 1, raw_controller: 1},
      ],
      start: {rules, players: [{}, {}, {}, {}, {}, {}]},
    },
    ...overrides,
  };
}

function identityText(sourceFrame = 120, overrides = {}) {
  return JSON.stringify(sourceIdentity(sourceFrame, overrides));
}

function validPad(seed) {
  return Uint8Array.from({length: 11}, (_, index) => (seed + index * 13) & 0xff);
}

function createEndpointBus() {
  const endpoints = new Map();
  const sent = {alpha: [], beta: []};
  const opposite = {alpha: 'beta', beta: 'alpha'};
  function createEndpoint(options) {
    const {role} = options;
    const endpoint = {
      role,
      options,
      errors: [],
      closed: false,
      ready: Promise.resolve(),
      transport: {type: 'fake-room-endpoint-for-control'},
      inbound: Promise.resolve(),
      async send(text) {
        if (endpoint.closed) throw Error('fake room endpoint is closed');
        sent[role].push(text);
        const remote = endpoints.get(opposite[role]);
        if (remote) {
          const delivery = remote.deliver(text);
          delivery.catch(() => {});
        }
      },
      deliver(text) {
        if (endpoint.closed) return Promise.reject(Error('fake remote endpoint is closed'));
        const operation = endpoint.inbound.then(() => options.onMessage(text));
        const tracked = operation.catch(error => {
          endpoint.errors.push(error);
          try { options.onEndpointError?.(role, error); } catch {}
          throw error;
        });
        endpoint.inbound = tracked.catch(() => {});
        return tracked;
      },
      async drainInbound() {
        await endpoint.inbound;
        if (endpoint.errors.length)
          throw new AggregateError([...endpoint.errors], 'fake endpoint inbound callback failed');
      },
      async close() {
        if (endpoint.closed) {
          await endpoint.drainInbound();
          return;
        }
        endpoint.closed = true;
        const failures = [];
        try { await endpoint.drainInbound(); } catch (error) { failures.push(error); }
        try { await options.onDisconnect?.(role, '1000: control endpoint closed'); }
        catch (error) { failures.push(error); }
        if (failures.length) throw new AggregateError(failures, 'fake endpoint close failed');
      },
      destroy() {
        if (endpoint.closed) return;
        endpoint.closed = true;
        void options.onDisconnect?.(role, '4001: control endpoint destroyed');
      },
    };
    endpoints.set(role, endpoint);
    return endpoint;
  }
  return {
    createEndpoint,
    endpoints,
    sent,
    inject(role, packet) {
      const endpoint = endpoints.get(role);
      assert.ok(endpoint, `${role} endpoint exists`);
      return endpoint.deliver(typeof packet === 'string' ? packet : JSON.stringify(packet));
    },
    async disconnect(role) {
      const endpoint = endpoints.get(role);
      assert.ok(endpoint, `${role} endpoint exists`);
      if (endpoint.closed) return;
      endpoint.closed = true;
      await endpoint.drainInbound();
      await endpoint.options?.onDisconnect?.(role, '1006: remote socket ended');
    },
    async flush(role) { await endpoints.get(role)?.drainInbound(); },
  };
}

function makePeer(bus, role, {sourceFrame = 120, sourceJson = identityText(sourceFrame),
  identity = loadedIdentity, validateNativePad = () => true, inputTicks = 1, timeoutMs = 1000,
  sampleTimeoutMs = timeoutMs} = {}) {
  return createRuntimeMatchHistoryPeer({
    role, relayUrl: 'ws://local.test/room', roomId: 'R'.repeat(24), timeoutMs, sampleTimeoutMs,
    sourceIdentityJson: sourceJson, loadedIdentity: identity, sourceFrame, inputTicks,
    validateNativePad, createEndpoint: bus.createEndpoint,
  });
}

function statePacket({sequence = 0, pad = validPad(9), checksums = []} = {}) {
  return {
    type: 'state', version: LOCKSTEP_VERSION, role: 'beta', sequence,
    ack_sequence: null, ack_input: null, ack_checksum: null,
    unacknowledged: [{tick: 0, pad: Buffer.from(pad).toString('base64')}],
    delayed_checksums: checksums,
  };
}

function frameBytesHex({localPort, local, remote}) {
  const frame = new Uint8Array(44);
  frame.set(local, localPort * 11);
  frame.set(remote, (1 - localPort) * 11);
  const noController = Uint8Array.from(lockstepConstants.noControllerPad.match(/../g), byte => Number.parseInt(byte, 16));
  frame.set(noController, 22);
  frame.set(noController, 33);
  return [...frame].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

test('agreement binds exact source identity JSON, loaded hashes, active profile, and source frame', () => {
  const raw = identityText(120);
  const agreement = buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: raw, loadedIdentity, sourceFrame: 120,
  });
  assert.equal(agreement.scope, RUNTIME_MATCH_HISTORY_PEER_SCOPE);
  assert.equal(agreement.source_match_identity_json, raw,
    'the source serializer bytes remain part of the LockstepPeer hello agreement');
  assert.equal(agreement.source_frame, 120);
  assert.equal(agreement.runtime_sha256, loadedIdentity.runtime_sha256);
  assert.equal(agreement.asset_identity_sha256, loadedIdentity.asset_identity_sha256);
  assert.equal(agreement.save_profile_sha256, loadedIdentity.save_profile_sha256);
  assert.equal(agreement.profile_identity, loadedIdentity.profile_identity);
  assert.equal(agreement.source_ticks, 3);
  assert.equal(agreement.input_ticks, 1);
  assert.throws(() => buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: raw, loadedIdentity, sourceFrame: 121,
  }), /agreed source frame/);
  assert.throws(() => buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: raw.replace(loadedIdentity.asset_identity_sha256, 'f'.repeat(64)),
    loadedIdentity, sourceFrame: 120,
  }), /asset_identity_sha256 differs/);
  assert.throws(() => buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: identityText(120, {profile_identity: 'slippi-render-dependencies-logical-frame-lobj-tobj-tev-stage-scratch-v19'}),
    loadedIdentity, sourceFrame: 120,
  }), /camera-history V20/);
  const wrongMapping = sourceIdentity(120);
  wrongMapping.selection.controller_mapping[1].raw_controller = 0;
  assert.throws(() => buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: JSON.stringify(wrongMapping), loadedIdentity, sourceFrame: 120,
  }), /source slots and raw controllers/);
  const pointerBearing = sourceIdentity(120);
  pointerBearing.selection.start.rules.x58_pointer = 'unresolved_nonnull';
  assert.throws(() => buildRuntimeMatchHistoryPeerAgreement({
    sourceIdentityJson: JSON.stringify(pointerBearing), loadedIdentity, sourceFrame: 120,
  }), /pointer-bearing/);
});

test('two peers retain one literal sample each and all three assembled frames as protocol-only evidence', async () => {
  const bus = createEndpointBus();
  const validatorCalls = {alpha: [], beta: []};
  const make = role => makePeer(bus, role, {
    validateNativePad(bytes, details) {
      validatorCalls[role].push({bytes: [...bytes], ...details});
      return bytes.length === 11 && details.pollSerial !== '0';
    },
  });
  const alpha = make('alpha'), beta = make('beta');
  try {
    const [startedAlpha, startedBeta] = await Promise.all([alpha.start(), beta.start()]);
    assert.equal(startedAlpha.ready, true);
    assert.equal(startedBeta.ready, true);
    assert.deepEqual(startedAlpha.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1]);
    assert.deepEqual(startedBeta.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1]);
    assert(startedAlpha.protocol_only_frames.every(row => row.protocol_preroll_neutral &&
      row.match_source_frame === null && row.not_for_gameplay));
    assert.equal(startedAlpha.agreement_hash, startedBeta.agreement_hash);

    const alphaPad = validPad(11), betaPad = validPad(91);
    const alphaOriginal = new Uint8Array(alphaPad), betaOriginal = new Uint8Array(betaPad);
    const [alphaReceipt, betaReceipt] = await Promise.all([
      alpha.publishLocal({port: 0, tick: 0, pollSerial: '41', pad: alphaPad}),
      beta.publishLocal({port: 1, tick: 0, pollSerial: 52n, pad: betaPad}),
    ]);
    alphaPad.fill(0);
    betaPad.fill(0);
    assert.deepEqual(alphaReceipt.pad, [...alphaOriginal]);
    assert.deepEqual(betaReceipt.pad, [...betaOriginal]);
    assert.equal(alphaReceipt.poll_serial, '41');
    assert.equal(betaReceipt.poll_serial, '52');
    assert.deepEqual(validatorCalls.alpha[0], {
      bytes: [...alphaOriginal], port: 0, tick: 0, pollSerial: '41',
    });
    assert.deepEqual(validatorCalls.beta[0], {
      bytes: [...betaOriginal], port: 1, tick: 0, pollSerial: '52',
    });

    const [remoteAtAlpha, remoteAtBeta] = await Promise.all([
      alpha.waitForRemoteSample({timeoutMs: 500}), beta.waitForRemoteSample({timeoutMs: 500}),
    ]);
    assert.deepEqual(remoteAtAlpha.pad, [...betaOriginal]);
    assert.deepEqual(remoteAtBeta.pad, [...alphaOriginal]);
    assert.equal(remoteAtAlpha.accepted_by, 'lockstep-protocol',
      'canonical peer admission is recorded separately from native/game semantic admission');
    assert.equal(remoteAtAlpha.remote_port, 1);
    assert.equal(remoteAtAlpha.sender_role, 'beta');
    assert.equal(Object.isFrozen(remoteAtAlpha.pad), true);

    const finalAlpha = alpha.snapshot(), finalBeta = beta.snapshot();
    for (const [summary, localPort, local, remote] of [
      [finalAlpha, 0, alphaOriginal, betaOriginal],
      [finalBeta, 1, betaOriginal, alphaOriginal],
    ]) {
      assert.equal(summary.module_native_game_input_calls, 0);
      assert.equal(summary.protocol_only_frames.length, 3);
      assert.deepEqual(summary.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1, 2]);
      assert(summary.protocol_only_frames.every(row => row.match_source_frame === null && row.not_for_gameplay));
      assert.equal(summary.protocol_only_frames[2].protocol_input_tick, 0);
      assert.equal(summary.protocol_only_frames[2].bytes_hex,
        frameBytesHex({localPort, local, remote}));
      assert.equal(summary.protocol_only_frames[0].bytes_hex.length, 88);
      assert.equal(summary.protocol_only_frames[1].bytes_hex.length, 88);
    }
    await Promise.all([alpha.close(), beta.close()]);
    assert.equal(alpha.snapshot().closed, true);
    assert.equal(beta.snapshot().closed, true);
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

test('local refusal is nonpublishing, sample deadlines are finite, and a successful sample cannot be replaced', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha', {validateNativePad: bytes => bytes[0] === 7,
    sampleTimeoutMs: 20});
  const beta = makePeer(bus, 'beta');
  try {
    await Promise.all([alpha.start(), beta.start()]);
    await assert.rejects(alpha.waitForRemoteSample(), /did not arrive within 20 ms/);
    await assert.rejects(alpha.publishLocal({port: 1, tick: 0, pollSerial: 1, pad: validPad(7)}),
      /this peer port/);
    await assert.rejects(alpha.publishLocal({port: 0, tick: 0, pollSerial: 0, pad: validPad(7)}),
      /nonzero safe integer/);
    for (const pollSerial of ['0', '01', '18446744073709551616'])
      await assert.rejects(alpha.publishLocal({port: 0, tick: 0, pollSerial, pad: validPad(7)}),
        /canonical nonzero uint64 decimal string|nonzero uint64/);
    await assert.rejects(alpha.publishLocal({port: 0, tick: 0, pollSerial: 2, pad: validPad(8)}),
      /Native pure PAD validator refused/);
    assert.equal(alpha.snapshot().local_contribution, null);
    assert.equal(JSON.parse(bus.sent.alpha[0]).type, 'hello');
    assert.equal(bus.sent.alpha.some(text => JSON.parse(text).type === 'state'), false,
      'rejected and missing local samples publish no PAD state packet');

    const accepted = await alpha.publishLocal({port: 0, tick: 0, pollSerial: 3, pad: validPad(7)});
    await assert.rejects(alpha.publishLocal({port: 0, tick: 0, pollSerial: 4, pad: validPad(7)}),
      /contiguous index order/);
    assert.deepEqual(alpha.snapshot().local_contribution.pad, accepted.pad);
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

test('malformed packet tail publishes no partial remote contribution or frame', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha');
  const beta = makePeer(bus, 'beta');
  try {
    await Promise.all([alpha.start(), beta.start()]);
    await assert.rejects(bus.inject('alpha', statePacket({checksums: [{tick: 0, record: 'AA=='}]})),
      /invalid delayed checksum record/);
    assert.equal(alpha.snapshot().remote_contribution, null);
    assert.deepEqual(alpha.snapshot().protocol_only_frames.map(row => row.protocol_source_tick), [0, 1]);
    assert.equal(alpha.snapshot().terminal?.kind, 'protocol');
    assert.equal(bus.sent.alpha.some(text => JSON.parse(text).type === 'state'), false,
      'the rejected state packet did not receive an ACK');
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

test('conflicting duplicate input fails closed and leaves the first literal receipt unchanged', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha');
  try {
    const starting = alpha.start();
    const agreement = buildRuntimeMatchHistoryPeerAgreement({
      sourceIdentityJson: identityText(), loadedIdentity, sourceFrame: 120,
    });
    await bus.inject('alpha', {type: 'hello', version: LOCKSTEP_VERSION, role: 'beta',
      local_port: 1, remote_port: 0, agreement});
    await starting;
    const first = validPad(21), conflict = validPad(22);
    await bus.inject('alpha', statePacket({sequence: 0, pad: first}));
    const receipt = await alpha.waitForRemoteSample({timeoutMs: 100});
    assert.deepEqual(receipt.pad, [...first]);
    await bus.inject('alpha', statePacket({sequence: 0, pad: first}));
    assert.deepEqual(alpha.remoteSnapshot().pad, [...first],
      'an identical retransmission does not publish a second receipt');
    await assert.rejects(bus.inject('alpha', statePacket({sequence: 1, pad: conflict})),
      /conflicting remote PAD contribution/);
    assert.deepEqual(alpha.remoteSnapshot().pad, [...first]);
    assert.equal(alpha.snapshot().terminal?.kind, 'protocol');
  } finally {
    await alpha.close().catch(() => {});
  }
});

test('close joins an in-flight core receive callback and prevents late remote admission', async () => {
  const previousCrypto = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
  const entered = deferredForTest(), release = deferredForTest();
  const originalDigest = webcrypto.subtle.digest.bind(webcrypto.subtle);
  Object.defineProperty(globalThis, 'crypto', {configurable: true, value: {subtle: {
    async digest(algorithm, data) {
      const bytes = new Uint8Array(data.buffer ?? data, data.byteOffset ?? 0, data.byteLength);
      const text = new TextDecoder().decode(bytes);
      if (text.startsWith('{"type":"state"')) {
        entered.resolve();
        await release.promise;
      }
      return originalDigest(algorithm, data);
    },
  }}});
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha');
  const beta = makePeer(bus, 'beta');
  try {
    await Promise.all([alpha.start(), beta.start()]);
    const receive = bus.inject('alpha', statePacket()).catch(error => error);
    await entered.promise;
    let closeSettled = false;
    const closing = alpha.close().finally(() => { closeSettled = true; });
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(closeSettled, false, 'close waits for the endpoint and LockstepPeer receive path');
    release.resolve();
    await receive;
    await closing;
    assert.equal(alpha.snapshot().remote_contribution, null,
      'a receive still awaiting validation cannot publish after close begins');
    await beta.close();
  } finally {
    release.resolve();
    await Promise.allSettled([alpha.close(), beta.close()]);
    if (previousCrypto) Object.defineProperty(globalThis, 'crypto', previousCrypto);
    else delete globalThis.crypto;
  }
});

test('unexpected relay close wakes remote waiters and cleanup remains joinable', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha');
  const beta = makePeer(bus, 'beta');
  await Promise.all([alpha.start(), beta.start()]);
  const waiting = alpha.waitForRemoteSample({timeoutMs: 500});
  const endpoint = bus.endpoints.get('alpha');
  endpoint.closed = true;
  await endpoint.options?.onDisconnect?.('alpha', '1006: peer ended early');
  await assert.rejects(waiting, /relay disconnected/);
  await Promise.allSettled([alpha.close(), beta.close()]);
  assert.equal(alpha.closed, true);
  assert.equal(beta.closed, true);
});

test('remote wait rejects a previously held sample after terminal failure or close', async () => {
  async function readyWithRemoteSample() {
    const bus = createEndpointBus();
    const alpha = makePeer(bus, 'alpha');
    const starting = alpha.start();
    const agreement = buildRuntimeMatchHistoryPeerAgreement({
      sourceIdentityJson: identityText(), loadedIdentity, sourceFrame: 120,
    });
    await bus.inject('alpha', {type: 'hello', version: LOCKSTEP_VERSION, role: 'beta',
      local_port: 1, remote_port: 0, agreement});
    await starting;
    await bus.inject('alpha', statePacket());
    assert.ok(await alpha.waitForRemoteSample({timeoutMs: 100}));
    return {alpha, bus};
  }

  const failed = await readyWithRemoteSample();
  try {
    const pending = failed.alpha.waitForRemoteSample();
    const endpoint = failed.bus.endpoints.get('alpha');
    await endpoint.options.onDisconnect('alpha', '1006: peer ended after contribution');
    await assert.rejects(pending, /relay disconnected/);
    await assert.rejects(failed.alpha.waitForRemoteSample(), /relay disconnected/);
  } finally {
    await failed.alpha.close().catch(() => {});
  }

  const closed = await readyWithRemoteSample();
  try {
    const pending = closed.alpha.waitForRemoteSample();
    const closing = closed.alpha.close();
    await assert.rejects(pending, /closed before remote PAD acceptance/);
    await closing;
    await assert.rejects(closed.alpha.waitForRemoteSample(), /closed before remote PAD acceptance/);
  } finally {
    await closed.alpha.close().catch(() => {});
  }
});

test('missing remote hello fails the readiness deadline and closes the owned endpoint', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha', {timeoutMs: 20});
  await assert.rejects(alpha.start(), /peer identity agreement exceeded 20 ms/);
  assert.equal(alpha.snapshot().closed, true);
  assert.equal(bus.endpoints.get('alpha').closed, true);
  await alpha.close();
});

test('production bridge uses page WebSocket owner and has no Node or native/game input dependency', async () => {
  const source = await readFile(new URL('../src/slippi_profile/runtime_match_history_peer.mjs', import.meta.url), 'utf8');
  assert.match(source, /import \{LockstepPeer, LOCKSTEP_VERSION, lockstepConstants\} from '\.\.\/\.\.\/scripts\/net_lockstep_core\.mjs'/);
  assert.match(source, /import \{createRoomRelayPeerEndpoint\} from '\.\.\/\.\.\/scripts\/net_lockstep_websocket_relay\.mjs'/);
  assert.doesNotMatch(source, /from ['"]node:|\b(?:Buffer|process|require)\b/);
  assert.doesNotMatch(source, /pushIndexed|melee_web_net_push_indexed|Module\.ccall/);
  assert.equal(RUNTIME_MATCH_HISTORY_PEER_PROFILE,
    'slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20');
});

test('explicit two-window peer keeps two indexed native polls on the same page socket', async () => {
  assert.equal(RUNTIME_MATCH_HISTORY_PROTOCOL_INPUT_TICKS, 1);
  assert.equal(RUNTIME_MATCH_HISTORY_PROTOCOL_SOURCE_TICKS, 3);
  assert.equal(RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS, 2);
  const bus = createEndpointBus(), validatorCalls = {alpha: [], beta: []};
  const make = role => makePeer(bus, role, {inputTicks: 2,
    validateNativePad(bytes, details) {
      validatorCalls[role].push({bytes: [...bytes], ...details});
      return bytes.length === 11 && details.pollSerial !== '0';
    },
  });
  const alpha = make('alpha'), beta = make('beta');
  try {
    const [startedAlpha, startedBeta] = await Promise.all([alpha.start(), beta.start()]);
    for (const started of [startedAlpha, startedBeta]) {
      assert.equal(started.input_ticks_protocol_only, 2);
      assert.equal(started.source_ticks_protocol_only, 4);
      assert.deepEqual(started.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1]);
      assert(started.protocol_only_frames.every(row => row.protocol_preroll_neutral &&
        row.not_for_gameplay && row.match_source_frame === null));
    }
    assert.equal(startedAlpha.scope, 'melee-web-runtime-two-window-history-peer-v1');
    assert.equal(startedAlpha.agreement_hash, startedBeta.agreement_hash);

    const accepted = [];
    for (const tick of [0, 1]) {
      const alphaPad = validPad(11 + tick), betaPad = validPad(71 + tick);
      const [alphaLocal, betaLocal] = await Promise.all([
        alpha.publishLocal({port: 0, tick, pollSerial: String(41 + tick), pad: alphaPad}),
        beta.publishLocal({port: 1, tick, pollSerial: String(51 + tick), pad: betaPad}),
      ]);
      assert.equal(alphaLocal.protocol_input_tick, tick);
      assert.equal(betaLocal.protocol_input_tick, tick);
      const [alphaRemote, betaRemote] = await Promise.all([
        alpha.waitForRemoteSample({tick}), beta.waitForRemoteSample({tick}),
      ]);
      assert.deepEqual(alphaRemote.pad, [...betaPad]);
      assert.deepEqual(betaRemote.pad, [...alphaPad]);
      accepted.push({tick, alphaPad, betaPad});
    }
    assert.deepEqual(validatorCalls.alpha.map(row => [row.tick, row.pollSerial]), [[0, '41'], [1, '42']]);
    assert.deepEqual(validatorCalls.beta.map(row => [row.tick, row.pollSerial]), [[0, '51'], [1, '52']]);
    for (const [peer, localPort] of [[alpha, 0], [beta, 1]]) {
      const snapshot = peer.snapshot();
      assert.equal(snapshot.socket_owner, 'browser-page');
      assert.equal(snapshot.socket_ready, true);
      assert.deepEqual(snapshot.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1, 2, 3]);
      assert.deepEqual(snapshot.protocol_only_frames.map(row => row.protocol_input_tick), [null, null, 0, 1]);
      assert(snapshot.protocol_only_frames.every(row => row.not_for_gameplay && row.match_source_frame === null));
      assert.deepEqual(snapshot.local_contributions.map(row => row.protocol_input_tick), [0, 1]);
      assert.deepEqual(snapshot.remote_contributions.map(row => row.protocol_input_tick), [0, 1]);
      for (const row of accepted) {
        const local = localPort === 0 ? row.alphaPad : row.betaPad;
        const remote = localPort === 0 ? row.betaPad : row.alphaPad;
        assert.equal(snapshot.protocol_only_frames[row.tick + 2].bytes_hex,
          frameBytesHex({localPort, local, remote}));
      }
    }
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

test('default peer remains one input and refuses a second input index', async () => {
  const bus = createEndpointBus(), alpha = makePeer(bus, 'alpha'), beta = makePeer(bus, 'beta');
  try {
    const [snapshot] = await Promise.all([alpha.start(), beta.start()]);
    assert.equal(snapshot.scope, RUNTIME_MATCH_HISTORY_PEER_SCOPE);
    assert.equal(snapshot.input_ticks_protocol_only, 1);
    assert.equal(snapshot.source_ticks_protocol_only, 3);
    await assert.rejects(alpha.publishLocal({port: 0, tick: 1, pollSerial: '61', pad: validPad(61)}),
      /contiguous index order/);
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

test('two-input peer refuses skipped, duplicate, third and out-of-bound remote indexes', async () => {
  const bus = createEndpointBus(), calls = [];
  const alpha = makePeer(bus, 'alpha', {inputTicks: 2,
    validateNativePad(bytes, details) { calls.push(details); return bytes.length === 11; },
  });
  const beta = makePeer(bus, 'beta', {inputTicks: 2});
  try {
    await Promise.all([alpha.start(), beta.start()]);
    for (const tick of [1, 2])
      await assert.rejects(alpha.publishLocal({port: 0, tick, pollSerial: String(70 + tick), pad: validPad(70)}),
        /contiguous index order/);
    assert.equal(calls.length, 0, 'skipped and third indexes never reach native PAD validation');
    await alpha.publishLocal({port: 0, tick: 0, pollSerial: '80', pad: validPad(80)});
    await assert.rejects(alpha.publishLocal({port: 0, tick: 0, pollSerial: '81', pad: validPad(81)}),
      /contiguous index order/);

    const invalid = statePacket();
    invalid.unacknowledged = [{tick: RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS,
      pad: Buffer.from(validPad(90)).toString('base64')}];
    await assert.rejects(bus.inject('alpha', invalid), /remote input tick/);
    assert.equal(alpha.snapshot().terminal?.kind, 'protocol');
    assert.equal(alpha.remoteSnapshot(1), null);
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});

function deferredForTest() {
  let resolve;
  const promise = new Promise(resolvePromise => { resolve = resolvePromise; });
  return {promise, resolve};
}

test('four-tick peer keeps later actual input unpublished across speculative continuation', async () => {
  const bus = createEndpointBus();
  const alpha = makePeer(bus, 'alpha', {inputTicks: 4});
  const beta = makePeer(bus, 'beta', {inputTicks: 4});
  const publish = (peer, port, tick) => peer.publishLocal({port, tick,
    pollSerial: String(100 + tick), pad: validPad(10 + port + tick)});
  try {
    const [started] = await Promise.all([alpha.start(), beta.start()]);
    assert.equal(started.scope, 'melee-web-runtime-four-tick-history-peer-v1');
    assert.deepEqual(started.protocol_only_frames.map(row => row.protocol_source_tick), [0, 1]);
    for (const tick of [0, 1, 2, 3]) await publish(alpha, 0, tick);
    for (const tick of [0, 1]) await publish(beta, 1, tick);
    await alpha.waitForRemoteSample({tick: 1});
    assert.equal(alpha.remoteSnapshot(2), null);
    assert.deepEqual(alpha.snapshot().remote_contributions.map(row => row.protocol_input_tick), [0, 1]);
    for (const tick of [2, 3]) await publish(beta, 1, tick);
    const received = await alpha.waitForRemoteSample({tick: 3});
    assert.deepEqual(received.pad, [...validPad(14)]);
    await beta.waitForRemoteSample({tick: 3});
    for (const peer of [alpha, beta]) {
      const rows = peer.snapshot().protocol_only_frames;
      assert.deepEqual(rows.map(row => row.protocol_input_tick), [null, null, 0, 1, 2, 3]);
      assert(rows.every(row => row.match_source_frame === null && row.not_for_gameplay));
    }
    await assert.rejects(publish(beta, 1, 4), /contiguous index order/);
  } finally {
    await Promise.allSettled([alpha.close(), beta.close()]);
  }
});
