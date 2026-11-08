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
  RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_KEYS,
  RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_RESPAWN_PEER_SCOPE,
  RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_MATCH_FINAL_PEER_SCOPE,
  RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,
  RUNTIME_MATCH_HISTORY_STREAM_PEER_SCOPE,
} from '../src/slippi_profile/runtime_match_history_peer.mjs';
import {LOCKSTEP_VERSION, lockstepConstants} from '../scripts/net_lockstep_core.mjs';
import {installRuntimePeerHistoryProbe} from '../src/slippi_profile/tests/runtime_peer_history_probe.mjs';
import {waitForLateRemotePairAndResume}
  from '../src/slippi_profile/tests/runtime_paced_history_probe.mjs';
import {resolveRoomRelayMode} from '../scripts/a3_public_relay_preflight.mjs';
import {validateRoomRelayBaseUrl} from '../scripts/net_lockstep_websocket_relay.mjs';

const loadedIdentity = Object.freeze({
  runtime_sha256: '1'.repeat(64),
  asset_identity_sha256: '2'.repeat(64),
  save_profile_sha256: '3'.repeat(64),
  profile_identity: RUNTIME_MATCH_HISTORY_PEER_PROFILE,
});

const pairBrowserSource=await readFile(new URL('../scripts/runtime_match_history_pair_browser.mjs',import.meta.url),'utf8');
const peerRelayAdapterSource=pairBrowserSource.match(/^const peerRelayOwnerAdapter=async\([\s\S]*?^};$/m)?.[0];
assert(peerRelayAdapterSource,'Browser harness must retain its small peer-relay ownership adapter');
const peerRelayOwnerAdapter=new Function('validateRoomRelayBaseUrl',
  `${peerRelayAdapterSource};return peerRelayOwnerAdapter;`)(validateRoomRelayBaseUrl);

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

test('browser peer relay adapter preserves local ownership and leaves a validated external Worker untouched', async () => {
  const relayUrl='wss://relay.staging.example.invalid';
  const clientOrigin='http://127.0.0.1:8787';
  const preflight={
    schema:'melee-web-a3-public-relay-preflight-v1',version:1,
    scope:'read-only-public-staging-worker-binding',observed_at:'2026-11-07T12:00:00Z',
    runtime:{relay_url:relayUrl,relay_origin:relayUrl},
    source:{worker_name:'melee-web-room-relay-staging',environment:'staging',
      deployment_id:'pinned-deployment-1',script_sha256:'a'.repeat(64),config_sha256:'b'.repeat(64)},
    client:{origin:clientOrigin,allowed_origins:[clientOrigin]},
    evidence:{deployment_receipt:{file:'deployment.json',read_only:true,sha256:'c'.repeat(64)},
      origin_policy_receipt:{file:'origin-policy.json',read_only:true,sha256:'d'.repeat(64)}},
  };
  assert.match(pairBrowserSource,/clientOrigin:baseUrl\.origin/,
    'external mode must bind the supplied preflight to the actual browser page origin');
  const mode=resolveRoomRelayMode({relayUrl,preflight,clientOrigin});
  assert.throws(()=>resolveRoomRelayMode({relayUrl,preflight,clientOrigin:'https://other.example.invalid'}),
    /does not match/);
  let localStarts=0;
  const external=await peerRelayOwnerAdapter({relayMode:mode,roomId:'R'.repeat(24),
    producer:{wasmSha256:'1'.repeat(64),profile:'profile'},evidenceDir:'/evidence',
    diagnostic:()=>{},startLocalWorker:async()=>{++localStarts;throw Error('external mode started a local Worker');},
    preflight:{file:'preflight.json',sha256:'2'.repeat(64),binding:mode,
      independent_live_provider_verification:false}});
  assert.equal(localStarts,0);
  assert.equal(external.owner,null);
  assert.equal(external.relayUrl,relayUrl);
  assert.equal(external.report.mode,'external');
  assert.equal(external.report.external_worker.owned,false);
  assert.equal(external.report.external_worker.dispose_requested,false);
  assert.equal(external.report.external_worker.idle_status,'unknown');
  assert.equal(external.report.preflight.binding.client_origin,clientOrigin);
  const expectedSocket=`${relayUrl}/v1/rooms/${'R'.repeat(24)}/socket`;
  const closed=external.cleanupReceipt([
    {role:'alpha',websockets:[expectedSocket],peerClosed:true},
    {role:'beta',websockets:[expectedSocket],peerClosed:true},
  ]);
  assert.equal(closed.external_worker.idle_status,'unknown; external service state was not queried');
  assert.equal(closed.all_expected_browser_peer_sockets_closed,true);
  assert.equal(closed.expected_browser_peer_sockets.length,2);
  assert(closed.expected_browser_peer_sockets.every(socket=>socket.expected_socket_closed));
  const missing=external.cleanupReceipt([
    {role:'alpha',websockets:[],peerClosed:true},
    {role:'beta',websockets:[expectedSocket],peerClosed:true},
  ]);
  assert.equal(missing.expected_browser_peer_sockets[0].missing_expected_socket,true);
  assert.equal(missing.expected_browser_peer_sockets[0].expected_socket_closed,false,
    'peerClose cannot be attributed without the exact expected room socket');
  assert.equal(missing.all_expected_browser_peer_sockets_closed,false);
  const duplicate=external.cleanupReceipt([
    {role:'alpha',websockets:[expectedSocket,expectedSocket],peerClosed:true},
    {role:'beta',websockets:[expectedSocket],peerClosed:true},
  ]);
  assert.equal(duplicate.expected_browser_peer_sockets[0].duplicate_expected_sockets,true);
  assert.equal(duplicate.expected_browser_peer_sockets[0].expected_socket_closed,false,
    'a duplicate socket receipt cannot attribute one peerClose to one socket');
  assert.equal(duplicate.all_expected_browser_peer_sockets_closed,false);
  const unrelated=external.cleanupReceipt([
    {role:'alpha',websockets:[expectedSocket,'wss://other.example.invalid/unrelated'],peerClosed:true},
    {role:'beta',websockets:[expectedSocket],peerClosed:true},
  ]);
  assert.deepEqual(unrelated.expected_browser_peer_sockets[0].other_observed_urls,
    ['wss://other.example.invalid/unrelated']);
  assert.equal(unrelated.all_expected_browser_peer_sockets_closed,false,
    'peerClose must not be applied to an unrelated observed page WebSocket');
  const unclosed=external.cleanupReceipt([
    {role:'alpha',websockets:[expectedSocket],peerClosed:false},
    {role:'beta',websockets:[expectedSocket],peerClosed:true},
  ]);
  assert.equal(unclosed.expected_browser_peer_sockets[0].expected_socket_closed,false);
  assert.equal(unclosed.all_expected_browser_peer_sockets_closed,false);

  const local=await peerRelayOwnerAdapter({relayMode:{mode:'local'},roomId:'L'.repeat(24),
    producer:{wasmSha256:'3'.repeat(64),profile:'profile'},evidenceDir:'/evidence',
    diagnostic:()=>{},startLocalWorker:async options=>{
      ++localStarts;assert.equal(options.evidenceDir,'/evidence');
      return {base:'http://127.0.0.1:8787',port:8787,ownerProcess:{pid:123},
        runtimeIdentity:{node:'test'},hashes:{worker:'4'.repeat(64)}};
    },preflight:null});
  assert.equal(localStarts,1,'omitted external flags retain the owned local Worker');
  assert.equal(local.relayUrl,'ws://127.0.0.1:8787');
  assert.equal(local.owner.port,8787);
  assert.equal(local.cleanupReceipt,null);
});

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
  sampleTimeoutMs = timeoutMs,matchFinal = false,streaming = false,
  deferredLocalTick = null,deferredLocalTicks = null} = {}) {
  return createRuntimeMatchHistoryPeer({
    role, relayUrl: 'ws://local.test/room', roomId: 'R'.repeat(24), timeoutMs, sampleTimeoutMs,
    sourceIdentityJson: sourceJson, loadedIdentity: identity, sourceFrame, inputTicks, matchFinal, streaming,
    validateNativePad,deferredLocalTick,deferredLocalTicks,createEndpoint: bus.createEndpoint,
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

test('final checksum capability changes agreement identity only when explicitly enabled', () => {
  const base={sourceIdentityJson:identityText(120),loadedIdentity,sourceFrame:120,inputTicks:4};
  const ordinary=buildRuntimeMatchHistoryPeerAgreement(base);
  const final=buildRuntimeMatchHistoryPeerAgreement({...base,matchFinal:true});
  assert.equal(Object.hasOwn(ordinary,'match_final'),false,
    'default dense lockstep agreement retains its original fields');
  assert.equal(final.match_final,1);
  assert.notDeepEqual(final,ordinary);
  assert.throws(()=>buildRuntimeMatchHistoryPeerAgreement({...base,matchFinal:1}),/must be a boolean/);
  const matchFinalScope=buildRuntimeMatchHistoryPeerAgreement({...base,
    inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,matchFinal:true});
  assert.equal(matchFinalScope.scope,RUNTIME_MATCH_HISTORY_MATCH_FINAL_PEER_SCOPE);
  assert.equal(matchFinalScope.input_ticks,3600);
  assert.equal(matchFinalScope.source_ticks,3602);
  assert.equal(matchFinalScope.match_final,1);
  assert.throws(()=>buildRuntimeMatchHistoryPeerAgreement({...base,
    inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS}),/requires match_final opt-in/);
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

test('explicit deferred peer sample releases once with its original poll identity and PAD bytes', async () => {
  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks:2}),beta=makePeer(bus,'beta',{inputTicks:2,deferredLocalTick:0});
  try{
    await Promise.all([alpha.start(),beta.start()]);
    const pad=validPad(73),original=[...pad];
    await assert.rejects(alpha.publishLocal({port:0,tick:0,pollSerial:'1',pad,deferSend:true}),
      /differs from its explicitly configured tick/);
    await assert.rejects(beta.publishLocal({port:1,tick:0,pollSerial:'2',pad}),
      /differs from its explicitly configured tick/);
    await assert.rejects(beta.releaseDeferredLocal(1),
      /differs from its explicitly configured tick/);
    const held=await beta.publishLocal({port:1,tick:0,pollSerial:'2601',pad,deferSend:true});
    pad.fill(0);
    assert.deepEqual(held,{accepted_by:'native-validated-local-poll',local_port:1,
      protocol_input_tick:0,poll_serial:'2601',pad:original});
    assert.equal(alpha.remoteSnapshot(0),null,'deferred actual sample escaped before release');
    await assert.rejects(beta.publishLocal({port:1,tick:0,pollSerial:'2601',pad:original,deferSend:true}),
      /accepts local PAD publications once/);
    const releaseAttempts=await Promise.allSettled([
      beta.releaseDeferredLocal(0),beta.releaseDeferredLocal(0),
    ]);
    assert.equal(releaseAttempts[0].status,'fulfilled');
    assert.equal(releaseAttempts[1].status,'rejected');
    assert.match(String(releaseAttempts[1].reason),/requires its retained original publication/);
    assert.equal(bus.sent.beta.filter(text=>JSON.parse(text).type==='state').length,1,
      'concurrent release emitted more than one original packet');
    const released=releaseAttempts[0].value;
    assert.deepEqual(released,held,'release changed the original native poll receipt');
    const remote=await alpha.waitForRemoteSample({tick:0,timeoutMs:100});
    assert.deepEqual(remote.pad,original);
    await assert.rejects(beta.releaseDeferredLocal(0),
      /requires its retained original publication/);
  }finally{
    await Promise.allSettled([alpha.close(),beta.close()]);
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
    const priorFailure = failed.alpha.failure;
    const pendingFailure = await pending.catch(error => error);
    assert.equal(pendingFailure,priorFailure,'the first unexpected disconnect failure remains authoritative');
    assert.match(pendingFailure.message,/relay disconnected/);
    assert.notEqual(pendingFailure.code,'RUNTIME_MATCH_PEER_CLOSED');
    await failed.alpha.close();
    const afterFailure = await failed.alpha.waitForRemoteSample().catch(error => error);
    assert.equal(afterFailure,priorFailure,'explicit cleanup cannot reclassify an earlier relay failure');
  } finally {
    await failed.alpha.close().catch(() => {});
  }

  const closed = await readyWithRemoteSample();
  try {
    const pending = closed.alpha.waitForRemoteSample();
    const closing = closed.alpha.close();
    const pendingFailure = await pending.catch(error => error);
    assert.match(pendingFailure.message,/closed before remote PAD acceptance/);
    assert.equal(pendingFailure.code,'RUNTIME_MATCH_PEER_CLOSED');
    await closing;
    const afterClose = await closed.alpha.waitForRemoteSample().catch(error => error);
    assert.match(afterClose.message,/closed before remote PAD acceptance/);
    assert.equal(afterClose.code,'RUNTIME_MATCH_PEER_CLOSED');
  } finally {
    await closed.alpha.close().catch(() => {});
  }

  const neverStarted = makePeer(createEndpointBus(),'alpha');
  await neverStarted.close();
  const startAfterClose = await neverStarted.start().catch(error => error);
  assert.match(startAfterClose.message,/peer is closed/);
  assert.equal(startAfterClose.code,'RUNTIME_MATCH_PEER_CLOSED');
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
  assert.match(source, /import \{LockstepPeer, LOCKSTEP_VERSION, LOCKSTEP_WINDOW, LOCKSTEP_STREAM_MAX_INPUT_TICKS, lockstepConstants,\s+parseNetChecksum, validateMatchFinalOutcome\}\s+from '\.\.\/\.\.\/scripts\/net_lockstep_core\.mjs'/);
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

test('eight actual contributions keep one peer transport across the episode boundary',async()=>{
  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks:8}),beta=makePeer(bus,'beta',{inputTicks:8});
  try{
    const [started]=await Promise.all([alpha.start(),beta.start()]);
    assert.equal(started.scope,'melee-web-runtime-two-episode-arena-history-peer-v1');
    for(let tick=0;tick<8;++tick){
      await alpha.publishLocal({port:0,tick,pollSerial:String(400+tick),pad:validPad(10+tick)});
      await beta.publishLocal({port:1,tick,pollSerial:String(900+tick),pad:validPad(30+tick)});
      const actual=await alpha.waitForRemoteSample({tick});
      assert.deepEqual(actual.pad,[...validPad(30+tick)]);
    }
    await beta.waitForRemoteSample({tick:7});
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    for(const peer of [alpha,beta]){
      const snapshot=peer.snapshot();
      assert.equal(snapshot.agreement_hash,started.agreement_hash);
      assert.deepEqual(snapshot.protocol_only_frames.map(row=>row.protocol_input_tick),
        [null,null,0,1,2,3,4,5,6,7]);
    }
    await assert.rejects(alpha.publishLocal({port:0,tick:8,pollSerial:'408',pad:validPad(18)}),
      /contiguous index order/);
  }finally{
    await Promise.allSettled([alpha.close(),beta.close()]);
  }
});

test('finite 120-input peer session preserves its protocol pre-roll and refuses tick 120',async()=>{
  assert.equal(RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS,120);
  const bus=createEndpointBus();
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:119}),/supports one, two, four, eight, 120, 600, or 3600/);
  const alpha=makePeer(bus,'alpha',{inputTicks:RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS});
  const beta=makePeer(bus,'beta',{inputTicks:RUNTIME_MATCH_HISTORY_PACED_INPUT_TICKS});
  try{
    const [startedAlpha,startedBeta]=await Promise.all([alpha.start(),beta.start()]);
    for(const started of [startedAlpha,startedBeta]){
      assert.equal(started.input_ticks_protocol_only,120);
      assert.equal(started.source_ticks_protocol_only,122);
      assert.equal(started.scope,'melee-web-runtime-paced-120-inputs-history-peer-v1');
    }
    assert.equal(startedAlpha.agreement_hash,startedBeta.agreement_hash);
    for(let tick=0;tick<120;++tick){
      const alphaPad=validPad(10+tick),betaPad=validPad(30+tick);
      await Promise.all([
        alpha.publishLocal({port:0,tick,pollSerial:String(400+tick),pad:alphaPad}),
        beta.publishLocal({port:1,tick,pollSerial:String(900+tick),pad:betaPad}),
      ]);
      const [alphaRemote,betaRemote]=await Promise.all([
        alpha.waitForRemoteSample({tick}),beta.waitForRemoteSample({tick}),
      ]);
      assert.deepEqual(alphaRemote.pad,[...betaPad]);
      assert.deepEqual(betaRemote.pad,[...alphaPad]);
    }
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    for(const peer of [alpha,beta]){
      const snapshot=peer.snapshot();
      assert.equal(snapshot.input_ticks_protocol_only,120);
      assert.deepEqual(snapshot.protocol_only_frames.map(row=>row.protocol_input_tick),
        [null,null,...Array.from({length:120},(_,tick)=>tick)]);
    }
    await assert.rejects(alpha.publishLocal({port:0,tick:120,pollSerial:'520',pad:validPad(90)}),
      /contiguous index order/);
  }finally{
    await Promise.allSettled([alpha.close(),beta.close()]);
  }
});

test('finite 600-input peer session releases only the fixed directional deferred source polls once',async()=>{
  assert.equal(RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS,600);
  assert.deepEqual(RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS,[26,226,426]);
  assert.deepEqual(RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_KEYS,
    ['ArrowRight','ArrowLeft','ArrowRight']);
  const bus=createEndpointBus();
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:600,
    deferredLocalTicks:[26,226,426,526]}),/fixed ticks 26, 226, and 426/);
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:120,
    deferredLocalTicks:[26,226,426]}),/fixed ticks 26, 226, and 426/);
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:600,deferredLocalTick:26,
    deferredLocalTicks:[26,226,426]}),/cannot mix single and multiple/);
  const alpha=makePeer(bus,'alpha',{inputTicks:RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS});
  const beta=makePeer(bus,'beta',{inputTicks:RUNTIME_MATCH_HISTORY_RESPAWN_INPUT_TICKS,
    deferredLocalTicks:[...RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS]});
  try{
    const [startedAlpha,startedBeta]=await Promise.all([alpha.start(),beta.start()]);
    for(const started of [startedAlpha,startedBeta]){
      assert.equal(started.input_ticks_protocol_only,600);
      assert.equal(started.source_ticks_protocol_only,602);
      assert.equal(started.scope,RUNTIME_MATCH_HISTORY_RESPAWN_PEER_SCOPE);
    }
    assert.equal(startedAlpha.agreement_hash,startedBeta.agreement_hash);
    for(let tick=0;tick<600;++tick){
      const alphaPad=validPad(10+tick),betaPad=validPad(30+tick);
      await alpha.publishLocal({port:0,tick,pollSerial:String(400+tick),pad:alphaPad});
      const deferred=RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS.includes(tick);
      await beta.publishLocal({port:1,tick,pollSerial:String(900+tick),pad:betaPad,
        deferSend:deferred});
      if(deferred){
        assert.equal(alpha.remoteSnapshot(tick),null,
          `Beta deferred tick ${tick} escaped before release`);
        const released=await beta.releaseDeferredLocal(tick);
        assert.deepEqual(released.pad,[...betaPad]);
        assert.equal(released.protocol_input_tick,tick);
        assert.equal(released.poll_serial,String(900+tick));
        assert.equal(released.local_port,1);
        await assert.rejects(beta.releaseDeferredLocal(tick),
          /requires its retained original publication/);
      }
      const [alphaRemote,betaRemote]=await Promise.all([
        alpha.waitForRemoteSample({tick}),beta.waitForRemoteSample({tick}),
      ]);
      assert.deepEqual(alphaRemote.pad,[...betaPad]);
      assert.deepEqual(betaRemote.pad,[...alphaPad]);
      assert.equal(alphaRemote.protocol_input_tick,tick);
      assert.equal(betaRemote.protocol_input_tick,tick);
    }
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    for(const peer of [alpha,beta]){
      const snapshot=peer.snapshot();
      assert.equal(snapshot.input_ticks_protocol_only,600);
      assert.deepEqual(snapshot.protocol_only_frames.map(row=>row.protocol_input_tick),
        [null,null,...Array.from({length:600},(_,tick)=>tick)]);
    }
    await assert.rejects(alpha.publishLocal({port:0,tick:600,pollSerial:'1000',pad:validPad(90)}),
      /contiguous index order/);
    await assert.rejects(beta.publishLocal({port:1,tick:600,pollSerial:'1500',pad:validPad(91)}),
      /contiguous index order/);
  }finally{
    await Promise.allSettled([alpha.close(),beta.close()]);
  }
});

test('four-input page timeline retains its legacy four-tick lookahead',async t=>{
  t.after(()=>{
    delete globalThis.__meleeRuntimePeerHistoryProbe;
    delete globalThis.__meleeRuntimeMatchHistory;
  });
  globalThis.__meleeRuntimeMatchHistory={validatePad:()=>true};
  installRuntimePeerHistoryProbe({role:'alpha',relayUrl:'ws://127.0.0.1:8787',
    roomId:'a'.repeat(24),sourceIdentityJson:identityText(0),loadedIdentity,inputTicks:4,
    initialRemotePad:[...Buffer.from(lockstepConstants.neutralPad,'hex')]});
  const timeline=await globalThis.__meleeRuntimePeerHistoryProbe.call('episodeTimeline');
  assert.equal(timeline.max_history_ticks,8);
  assert.equal(timeline.max_lookahead_ticks,4);
});

test('fixed 3600-input full-match page uses the existing eight-tick timeline and requires final agreement',async t=>{
  t.after(()=>{
    delete globalThis.__meleeRuntimePeerHistoryProbe;
    delete globalThis.__meleeRuntimeMatchHistory;
  });
  globalThis.__meleeRuntimeMatchHistory={validatePad:()=>true};
  const options={role:'alpha',relayUrl:'ws://127.0.0.1:8787',roomId:'b'.repeat(24),
    sourceIdentityJson:identityText(0),loadedIdentity,inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,
    initialRemotePad:[...Buffer.from(lockstepConstants.neutralPad,'hex')]};
  assert.throws(()=>installRuntimePeerHistoryProbe(options),/requires final-checksum agreement/);
  installRuntimePeerHistoryProbe({...options,matchFinal:true});
  const timeline=await globalThis.__meleeRuntimePeerHistoryProbe.call('episodeTimeline');
  assert.equal(timeline.max_history_ticks,8);
  assert.equal(timeline.max_lookahead_ticks,8);
  assert.equal(timeline.simulated_through,-1);
  assert.equal(timeline.remote_contiguous_through,-1);
});

test('arena bridge refuses rebase outside the fully confirmed page timeline',async t=>{
  t.after(()=>{
    delete globalThis.__meleeRuntimePeerHistoryProbe;
    delete globalThis.__meleeRuntimeMatchHistory;
  });
  let commands=0;
  globalThis.__meleeRuntimeMatchHistory={
    enableArenaHistory:()=>true,validatePad:()=>true,
    status:()=>({expectedFrame:0,metadata:{preFrame:0}}),
    command(){++commands;throw Error('Unexpected native command');},
  };
  installRuntimePeerHistoryProbe({role:'alpha',relayUrl:'ws://127.0.0.1:8787',
    roomId:'a'.repeat(24),sourceIdentityJson:identityText(0),loadedIdentity,inputTicks:8,
    initialRemotePad:[...Buffer.from(lockstepConstants.neutralPad,'hex')]});
  const bridge=globalThis.__meleeRuntimePeerHistoryProbe;
  await bridge.call('arenaEnable');
  await assert.rejects(bridge.call('rebase',4),/page-owned timeline/);
  await assert.rejects(bridge.call('episodeRebase',4),/fully confirmed selected END/);
  assert.equal(commands,0);
  assert.equal((await bridge.call('episodeTimeline')).confirmed_through,-1);
});

function actualBetaPoll(tick=26){
  const raw=new Array(44).fill(0);
  raw.splice(11,11,0,0,127,0,0,0,0,0,0,0,0);
  return {sourceFrame:tick,localPort:1,remotePort:0,pollSerial:'492',raw};
}

function remotePadReceipt(tick,pad){
  return {accepted_by:'lockstep-protocol',sender_role:'beta',remote_port:1,
    protocol_input_tick:tick,pad:[...pad]};
}

test('late packet handoff arms target and neutral receipts before resuming alpha',async()=>{
  const sourcePoll=actualBetaPoll(),actualPad=sourcePoll.raw.slice(11,22),neutral=new Array(11).fill(0);
  const pending=new Map(),started=[];let resumed=0;
  const handoff=waitForLateRemotePairAndResume({tick:26,sourcePoll,requireFollowingNeutral:true,
    waitForRemoteSample(tick){
      started.push(tick);
      return new Promise(resolve=>pending.set(tick,resolve));
    },
    resumeLateFrontier(receipt){++resumed;assert.deepEqual(receipt,remotePadReceipt(26,actualPad));},
  });
  await Promise.resolve();
  assert.deepEqual(started,[26,27],'both remote waits are armed before the first receipt resolves');
  pending.get(26)(remotePadReceipt(26,actualPad));
  await Promise.resolve();
  assert.equal(resumed,0,'the target receipt alone cannot release alpha');
  pending.get(27)(remotePadReceipt(27,neutral));
  const result=await handoff;
  assert.equal(resumed,1);
  assert.deepEqual(result.target,remotePadReceipt(26,actualPad));
  assert.deepEqual(result.followingNeutral,remotePadReceipt(27,neutral));
});

test('late packet handoff refuses an incorrect target before resuming alpha',async()=>{
  const sourcePoll=actualBetaPoll(),neutral=new Array(11).fill(0);let resumed=0;
  await assert.rejects(waitForLateRemotePairAndResume({tick:26,sourcePoll,requireFollowingNeutral:true,
    waitForRemoteSample:async tick=>remotePadReceipt(tick,neutral),
    resumeLateFrontier(){++resumed;},
  }),/Late target receipt differs/);
  assert.equal(resumed,0);
});

test('late packet handoff refuses a non-neutral following receipt before resuming alpha',async()=>{
  const sourcePoll=actualBetaPoll(),actualPad=sourcePoll.raw.slice(11,22);let resumed=0;
  await assert.rejects(waitForLateRemotePairAndResume({tick:26,sourcePoll,requireFollowingNeutral:true,
    waitForRemoteSample:async tick=>remotePadReceipt(tick,tick===26?actualPad:[0,0,1,0,0,0,0,0,0,0,0]),
    resumeLateFrontier(){++resumed;},
  }),/Following beta receipt is not the actual neutral source poll/);
  assert.equal(resumed,0);
});

test('actual paced gate abort settles both pre-entry and paused owners',async()=>{
  const source=await readFile(new URL(
    '../src/slippi_profile/runtime_match_session_loop.mjs',import.meta.url),'utf8');
  const start=source.indexOf('function pauseGate('),end=source.indexOf('\nfunction matchesBytes(',start);
  assert(start>=0&&end>start);
  const gate=new Function(source.slice(start,end)+'\nreturn pauseGate;')();
  const future=gate('future');
  const waiting=assert.rejects(future.wait(),/aborted before entry/);
  future.abort();
  await waiting;
  assert.throws(()=>future.pause('late'),/entered more than once/);

  const entered=gate('entered');
  const paused=assert.rejects(entered.pause('ready'),/gate was aborted/);
  assert.equal(await entered.wait(),'ready');
  entered.abort();
  await paused;
  assert.throws(()=>entered.release(),/stale or duplicated/);
});

test('actual final recovery guard refuses invalidated or changed platform samples',async()=>{
  const source=await readFile(new URL(
    '../scripts/runtime_match_history_pair_browser.mjs',import.meta.url),'utf8');
  const start=source.indexOf('    const betaPlatform=betaSamples.get(');
  const end=source.indexOf('    const paired=',start);
  assert(start>=0&&end>start);
  const validate=new Function('assert','recovery','alphaSamples','betaSamples',source.slice(start,end));
  const sample={frame:340,players:[{stocks:3,fighter:0,motion:13,groundAir:1,facing_direction:-1,x:0,y:9}]};
  const recovery={platform_sample:sample};
  assert.throws(()=>validate(assert,recovery,new Map(),new Map()),/invalidated/);
  const changed={...sample,players:[{...sample.players[0],motion:29}]};
  assert.throws(()=>validate(assert,recovery,new Map([[340,changed]]),new Map()),/differs from the retained/);
  assert.doesNotThrow(()=>validate(assert,recovery,new Map([[340,structuredClone(sample)]]),new Map()));
  assert.equal(recovery.beta_same_frame_sample,null);
});

function finalChecksum(inputOrdinal,sourceFrame,salt=7){
  const bytes=Uint8Array.from({length:64},(_,index)=>(salt+index*19)&0xff);
  const view=new DataView(bytes.buffer);
  view.setUint32(0,inputOrdinal,true);
  view.setUint32(4,3,true);
  view.setUint32(8,0x12345678,true);
  view.setUint32(12,sourceFrame,true);
  return bytes;
}

const finalOutcome=Object.freeze({outcome:2,winner:1});

function finalPacket(snapshot,role,checksum,inputOrdinal,sourceFrame,type='match_final'){
  return {type,version:LOCKSTEP_VERSION,role,scope:snapshot.scope,
    agreement_hash:snapshot.agreement_hash,input_ordinal:inputOrdinal,
    source_frame:sourceFrame,...finalOutcome,checksum:Buffer.from(checksum).toString('base64')};
}

async function startFinalPeers({inputTicks=4,through=1,timeoutMs=500,matchFinal=true}={}){
  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks,timeoutMs,matchFinal});
  const beta=makePeer(bus,'beta',{inputTicks,timeoutMs,matchFinal});
  const [startedAlpha,startedBeta]=await Promise.all([alpha.start(),beta.start()]);
  assert.equal(startedAlpha.agreement_hash,startedBeta.agreement_hash);
  for(let tick=0;tick<=through;++tick){
    await Promise.all([
      alpha.publishLocal({port:0,tick,pollSerial:String(400+tick),pad:validPad(10+tick)}),
      beta.publishLocal({port:1,tick,pollSerial:String(900+tick),pad:validPad(30+tick)}),
    ]);
    await Promise.all([alpha.waitForRemoteSample({tick}),beta.waitForRemoteSample({tick})]);
  }
  return {bus,alpha,beta,startedAlpha,startedBeta};
}

test('3600-input match-final scope is fixed and permits an actual early terminal record without padding',async()=>{
  assert.equal(RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,3600);
  const bus=createEndpointBus();
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:3599,matchFinal:true}),
    /supports one, two, four, eight, 120, 600, or 3600/);
  assert.throws(()=>makePeer(bus,'alpha',{inputTicks:3600}),/requires match_final opt-in/);
  const {alpha,beta,startedAlpha,startedBeta}=await startFinalPeers({
    inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,through:1});
  const inputOrdinal=1,sourceFrame=602,checksum=finalChecksum(inputOrdinal,sourceFrame);
  try{
    for(const started of [startedAlpha,startedBeta]){
      assert.equal(started.scope,RUNTIME_MATCH_HISTORY_MATCH_FINAL_PEER_SCOPE);
      assert.equal(started.input_ticks_protocol_only,3600);
      assert.equal(started.source_ticks_protocol_only,3602);
    }
    const [a,b]=await Promise.all([
      alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),
      beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),
    ]);
    assert.deepEqual({...a},{accepted_by:'lockstep-match-final',role:'alpha',
      input_ordinal:1,source_frame:602,agreement_hash:startedAlpha.agreement_hash,...finalOutcome,matched_bytes:64});
    assert.deepEqual({...b},{...a,role:'beta'});
    for(const peer of [alpha,beta]){
      const snapshot=peer.snapshot();
      assert.equal(snapshot.input_ticks_protocol_only,3600);
      assert.equal(snapshot.local_contributions.length,2);
      assert.equal(snapshot.remote_contributions.length,2);
      assert.deepEqual(snapshot.protocol_only_frames.map(row=>row.protocol_input_tick),[null,null,0,1]);
    }
    await assert.rejects(alpha.publishLocal({port:0,tick:2,pollSerial:'402',pad:validPad(12)}),
      /final checksum was published/);
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('3600 full-match peer holds only the three fixed actual directional correction packets',async()=>{
  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,
    matchFinal:true});
  const beta=makePeer(bus,'beta',{inputTicks:RUNTIME_MATCH_HISTORY_MATCH_FINAL_INPUT_TICKS,
    matchFinal:true,deferredLocalTicks:[...RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS]});
  try{
    const [startedAlpha,startedBeta]=await Promise.all([alpha.start(),beta.start()]);
    assert.equal(startedAlpha.scope,RUNTIME_MATCH_HISTORY_MATCH_FINAL_PEER_SCOPE);
    assert.equal(startedBeta.agreement_hash,startedAlpha.agreement_hash);
    for(let tick=0;tick<=426;++tick){
      const alphaPad=validPad(10+tick),betaPad=validPad(30+tick);
      const deferred=RUNTIME_MATCH_HISTORY_RESPAWN_DEFERRED_LOCAL_TICKS.includes(tick);
      await alpha.publishLocal({port:0,tick,pollSerial:String(400+tick),pad:alphaPad});
      await beta.publishLocal({port:1,tick,pollSerial:String(900+tick),pad:betaPad,deferSend:deferred});
      if(deferred){
        assert.equal(alpha.remoteSnapshot(tick),null);
        const release=await beta.releaseDeferredLocal(tick);
        assert.equal(release.protocol_input_tick,tick);
        assert.equal(release.poll_serial,String(900+tick));
        assert.deepEqual(release.pad,[...betaPad]);
        await assert.rejects(beta.releaseDeferredLocal(tick),/requires its retained original publication/);
      }
      const [remoteA,remoteB]=await Promise.all([
        alpha.waitForRemoteSample({tick}),beta.waitForRemoteSample({tick})]);
      assert.deepEqual(remoteA.pad,[...betaPad]);
      assert.deepEqual(remoteB.pad,[...alphaPad]);
    }
    const snapshot=beta.snapshot();
    assert.equal(snapshot.input_ticks_protocol_only,3600);
    assert.equal(snapshot.local_contributions.length,427);
    assert.equal(snapshot.remote_contributions.length,427);
    assert.deepEqual(snapshot.protocol_only_frames.slice(-3).map(row=>row.protocol_input_tick),
      [424,425,426]);
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('opt-in peers exchange simultaneous exact final records and tolerate retransmission',async()=>{
  const {bus,alpha,beta,startedAlpha}=await startFinalPeers({through:1});
  const inputOrdinal=1,sourceFrame=602,checksum=finalChecksum(inputOrdinal,sourceFrame);
  try{
    const [a,b]=await Promise.all([
      alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),
      beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),
    ]);
    assert.deepEqual({...a},{accepted_by:'lockstep-match-final',role:'alpha',
      input_ordinal:1,source_frame:602,agreement_hash:startedAlpha.agreement_hash,...finalOutcome,matched_bytes:64});
    assert.deepEqual({...b},{...a,role:'beta'});
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    for(const role of ['alpha','beta']){
      const packets=bus.sent[role].map(text=>JSON.parse(text));
      const finals=packets.filter(packet=>packet.type==='match_final');
      const acks=packets.filter(packet=>packet.type==='match_final_ack');
      assert.equal(finals.length,1);
      assert.equal(acks.length,1);
      assert.equal(finals[0].input_ordinal,1,
        'ending ordinal is an actual consumed input and need not equal the four-sample capacity');
      assert.equal(finals[0].source_frame,602);
      assert.equal(finals[0].agreement_hash,startedAlpha.agreement_hash);
      assert.equal(finals[0].checksum,Buffer.from(checksum).toString('base64'));
      assert.deepEqual(acks[0],{...finals[0],type:'match_final_ack'});
    }
    await alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    assert.equal(bus.sent.alpha.map(text=>JSON.parse(text)).filter(p=>p.type==='match_final').length,2,
      'an exact retry republishes the same immutable local record');
    assert.equal(alpha.snapshot().terminal,null);
    await assert.rejects(alpha.publishLocal({port:0,tick:2,pollSerial:'402',pad:validPad(12)}),
      /final checksum was published/);
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('early remote final is buffered until local final and exact duplicate is idempotent',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const inputOrdinal=0,sourceFrame=500,checksum=finalChecksum(inputOrdinal,sourceFrame);
  try{
    const betaDone=beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
    await bus.flush('alpha');
    assert.equal(bus.sent.alpha.map(text=>JSON.parse(text)).filter(p=>p.type==='match_final_ack').length,0,
      'the early remote record is buffered without ACK until the local final send settles');
    const remote=finalPacket(alpha.snapshot(),'beta',checksum,inputOrdinal,sourceFrame);
    await bus.inject('alpha',remote);
    assert.equal(alpha.snapshot().terminal,null,'an exact remote final duplicate is idempotent');
    const [a,b]=await Promise.all([
      alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),betaDone,
    ]);
    assert.equal(a.input_ordinal,0);assert.equal(b.input_ordinal,0);
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('final handshake rejects malformed scope and a changed duplicate record',async()=>{
  for(const mutate of [
    packet=>{packet.scope='other-scope';},
    packet=>{packet.checksum='AA==';},
  ]){
    const {bus,alpha,beta}=await startFinalPeers({through:0});
    const checksum=finalChecksum(0,501);
    const packet=finalPacket(alpha.snapshot(),'beta',checksum,0,501);
    mutate(packet);
    try{
      await assert.rejects(bus.inject('alpha',packet));
      assert.equal(alpha.snapshot().terminal?.kind,'protocol');
    }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
  }
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const checksum=finalChecksum(0,502);
  try{
    const packet=finalPacket(alpha.snapshot(),'beta',checksum,0,502);
    await bus.inject('alpha',packet);
    await bus.inject('alpha',packet);
    const changed=finalChecksum(0,502);changed[56]^=1;
    await assert.rejects(bus.inject('alpha',finalPacket(alpha.snapshot(),'beta',changed,0,502)));
    assert.equal(alpha.snapshot().terminal?.kind,'protocol');
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('valid final records that differ outside the aggregate checksum use the desync latch',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const a=finalChecksum(0,503,2),b=finalChecksum(0,503,2);b[56]^=1;
  try{
    await Promise.allSettled([
      alpha.publishMatchFinal({...finalOutcome,inputOrdinal:0,sourceFrame:503,checksum:a}),
      beta.publishMatchFinal({...finalOutcome,inputOrdinal:0,sourceFrame:503,checksum:b}),
    ]);
    await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
    assert.equal(alpha.snapshot().terminal?.kind,'desync');
    assert.equal(alpha.snapshot().terminal?.channel,4,
      'all 64 checksum bytes participate even when the total field differs');
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('final outcome and winner are independently matched with identical checksum bytes',async()=>{
  for(const [remote,reason] of [
    [{outcome:1,winner:1},'final native outcome differs'],
    [{outcome:2,winner:0},'final native winner differs'],
  ]){
    const {bus,alpha,beta}=await startFinalPeers({through:0});
    const inputOrdinal=0,sourceFrame=506,checksum=finalChecksum(inputOrdinal,sourceFrame);
    try{
      const result=await Promise.allSettled([
        alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}),
        beta.publishMatchFinal({...remote,inputOrdinal,sourceFrame,checksum}),
      ]);
      assert(result.every(row=>row.status==='rejected'));
      await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
      assert.equal(alpha.snapshot().terminal?.kind,'desync');
      assert.equal(alpha.snapshot().terminal?.reason,reason);
      assert.equal(beta.snapshot().terminal?.kind,'desync');
    }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
  }
});

test('final outcome fields are exact integers and an ACK must echo both',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const inputOrdinal=0,sourceFrame=509,checksum=finalChecksum(inputOrdinal,sourceFrame);
  const record={...finalOutcome,inputOrdinal,sourceFrame,checksum};
  try{
    for(const invalid of [{outcome:0},{outcome:'2'},{winner:4},{outcome:7,winner:1}]){
      await assert.rejects(alpha.publishMatchFinal({...record,...invalid}),/exact native outcome and winner/);
    }
    assert.equal(alpha.snapshot().terminal,null);
    const betaEndpoint=bus.endpoints.get('beta');
    const send=betaEndpoint.send.bind(betaEndpoint);
    betaEndpoint.send=async text=>{
      if(JSON.parse(text).type!=='match_final_ack')return send(text);
    };
    const pending=Promise.allSettled([alpha.publishMatchFinal(record)]);
    await beta.publishMatchFinal(record);
    const packet=finalPacket(alpha.snapshot(),'beta',checksum,inputOrdinal,sourceFrame);
    await assert.rejects(bus.inject('alpha',{...packet,type:'match_final_ack',winner:0}),
      /does not echo the exact matched record/);
    assert.equal((await pending)[0].status,'rejected');
    assert.equal(alpha.snapshot().terminal?.kind,'protocol');
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('an exact remote ACK may arrive during local send but cannot complete before send settles',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const inputOrdinal=0,sourceFrame=507,checksum=finalChecksum(inputOrdinal,sourceFrame);
  const alphaEndpoint=bus.endpoints.get('alpha'),sendAlpha=alphaEndpoint.send.bind(alphaEndpoint);
  const sendStarted=deferredForTest(),releaseSend=deferredForTest();
  alphaEndpoint.send=async text=>{
    if(JSON.parse(text).type==='match_final'){
      sendStarted.resolve();
      await sendAlpha(text);
      await releaseSend.promise;
      return;
    }
    return sendAlpha(text);
  };
  let alphaSettled=false;
  const alphaDone=alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}).then(
    value=>{alphaSettled=true;return {value};},error=>{alphaSettled=true;return {error};});
  try{
    await sendStarted.promise;
    const betaDone=beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
    await bus.flush('beta');
    await bus.flush('alpha');
    assert.equal(alpha.snapshot().terminal,null,
      'a valid ACK after send start is buffered while the send promise remains pending');
    assert.equal(alphaSettled,false,'an incoming ACK cannot substitute for local send settlement');
    releaseSend.resolve();
    const [a,b]=await Promise.all([alphaDone,betaDone]);
    assert(a.value);assert.equal(a.value.input_ordinal,0);assert.equal(b.input_ordinal,0);
  }finally{releaseSend.resolve();await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('an exact duplicate final during an in-flight ACK schedules one retry after the send settles',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({through:0});
  const inputOrdinal=0,sourceFrame=508,checksum=finalChecksum(inputOrdinal,sourceFrame);
  const alphaEndpoint=bus.endpoints.get('alpha'),sendAlpha=alphaEndpoint.send.bind(alphaEndpoint);
  const firstAckStarted=deferredForTest(),releaseFirstAck=deferredForTest(),retryAckStarted=deferredForTest();
  let alphaAckSends=0;
  alphaEndpoint.send=async text=>{
    if(JSON.parse(text).type==='match_final_ack'){
      ++alphaAckSends;
      await sendAlpha(text);
      if(alphaAckSends===1){firstAckStarted.resolve();await releaseFirstAck.promise;}
      else if(alphaAckSends===2)retryAckStarted.resolve();
      return;
    }
    return sendAlpha(text);
  };
  try{
    const alphaDone=alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
    const betaDone=beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
    await firstAckStarted.promise;
    await bus.inject('alpha',finalPacket(alpha.snapshot(),'beta',checksum,inputOrdinal,sourceFrame));
    releaseFirstAck.resolve();
    await retryAckStarted.promise;
    await Promise.all([alphaDone,betaDone]);
    assert.equal(alphaAckSends,2,
      'an exact duplicate received in flight is re-ACKed after the current ACK send marker clears');
  }finally{releaseFirstAck.resolve();await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('final ACK requires the exact record, and close before ACK cannot report success',async()=>{
  {
    const {bus,alpha,beta}=await startFinalPeers({through:0});
    const checksum=finalChecksum(0,504);
    try{
      await assert.rejects(bus.inject('alpha',finalPacket(alpha.snapshot(),'beta',checksum,0,504,'match_final_ack')));
      assert.equal(alpha.snapshot().terminal?.kind,'protocol','an ACK before local final publication is a protocol failure');
    }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
  }
  {
    const {bus,alpha,beta}=await startFinalPeers({through:0});
    const inputOrdinal=0,sourceFrame=505,checksum=finalChecksum(inputOrdinal,sourceFrame);
    try{
      const alphaDone=alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}).then(
        value=>({value}),error=>({error}));
      await bus.flush('beta');
      await bus.inject('alpha',finalPacket(alpha.snapshot(),'beta',checksum,inputOrdinal,sourceFrame));
      const changed=finalChecksum(inputOrdinal,sourceFrame);changed[56]^=2;
      await assert.rejects(bus.inject('alpha',finalPacket(alpha.snapshot(),'beta',changed,inputOrdinal,sourceFrame,'match_final_ack')),
        /exact matched record/);
      assert.equal(alpha.snapshot().terminal?.kind,'protocol');
      assert((await alphaDone).error);
    }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
  }
  {
    const {bus,alpha,beta}=await startFinalPeers({through:0});
    const inputOrdinal=0,sourceFrame=506,checksum=finalChecksum(inputOrdinal,sourceFrame);
    const betaEndpoint=bus.endpoints.get('beta');
    const send=betaEndpoint.send.bind(betaEndpoint);
    betaEndpoint.send=async text=>{
      if(JSON.parse(text).type==='match_final_ack')return;
      return send(text);
    };
    let alphaSettled=false;
    const alphaDone=alpha.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum}).then(
      value=>{alphaSettled=true;return {value};},error=>{alphaSettled=true;return {error};});
    try{
      await beta.publishMatchFinal({...finalOutcome,inputOrdinal,sourceFrame,checksum});
      assert.equal(alphaSettled,false,'the missing exact ACK keeps the final handshake incomplete');
      await alpha.close();
      const result=await alphaDone;
      assert(result.error,'closing before ACK rejects instead of completing the handshake');
      assert.equal(alpha.snapshot().failure!==null,true);
    }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
  }
});

test('default peer wire remains dense and does not send final control packets',async()=>{
  const {bus,alpha,beta}=await startFinalPeers({inputTicks:1,through:0,matchFinal:false});
  try{
    for(const role of ['alpha','beta']){
      const hello=bus.sent[role].map(text=>JSON.parse(text)).find(packet=>packet.type==='hello');
      assert.equal(Object.hasOwn(hello.agreement,'match_final'),false);
    }
    for(const peer of [alpha,beta])
      assert.equal(peer.snapshot().terminal,null);
    for(const role of ['alpha','beta'])
      assert.equal(bus.sent[role].map(text=>JSON.parse(text)).some(packet=>
        packet.type==='match_final'||packet.type==='match_final_ack'),false);
  }finally{await Promise.allSettled([alpha.close(),beta.close()]);}
});

test('released late waits exclude only actual missing receipts in admitted input order',async()=>{
  const source=await readFile(new URL(
    '../src/slippi_profile/runtime_match_session_bridge.mjs',import.meta.url),'utf8');
  const start=source.indexOf("        case 'episodeAwaitNetworkInput': {");
  const end=source.indexOf("        case 'episodeTimeline':",start);
  assert(start>=0&&end>start);
  const run=new Function('networkBackpressure','arenaEnabled','timeline','inputTicks',
    'terminalTailPublished','peer','owner','localPort','performance',
    'return async value=>{switch("episodeAwaitNetworkInput"){'+source.slice(start,end)+'}}');
  const sourcePoll=actualBetaPoll(),pad=sourcePoll.raw.slice(11,22),neutral=new Array(11).fill(0);
  const receipts=new Map([[26,remotePadReceipt(26,pad)],[27,remotePadReceipt(27,neutral)]]);
  const ready=new Set([27]); // Following neutral arrived before the required target.
  const events=[];let through=25,time=100,releaseTarget;
  const timeline={status:()=>({remote_contiguous_through:through})};
  const peer={remoteSnapshot:tick=>ready.has(tick)?receipts.get(tick):null,
    waitForRemoteSample:({tick})=>{
      assert.equal(tick,26);
      events.push(['wait',tick]);
      return new Promise(resolve=>{releaseTarget=()=>{ready.add(tick);resolve(receipts.get(tick));};});
    }};
  const owner={pacingWaitBegin:(kind,tick)=>events.push(['begin',kind,tick,time]),
    pacingWaitEnd:(kind,tick)=>{events.push(['end',kind,tick,time]);time+=3;return {excludedMs:39};}};
  const wait=run(true,true,timeline,3600,null,peer,owner,0,{now:()=>time});
  await assert.rejects(wait({kind:1,tick:27,requiredThrough:27}),/earliest required/);
  assert.deepEqual(events,[]);
  // The harness completed its deliberate hold and beta release before starting alpha's wait.
  time=140;
  const pacedSource=await readFile(new URL(
    '../src/slippi_profile/runtime_match_session_loop.mjs',import.meta.url),'utf8');
  const callbackStart=pacedSource.indexOf('async sourceTick=>{');
  const callbackEnd=pacedSource.indexOf('},\n        ...(networkBackpressure?',callbackStart);
  assert(callbackStart>=0&&callbackEnd>callbackStart);
  const recorded=[];
  const waitForRemoteSample=new Function('networkBackpressure','call','tick','recordNetworkWait',
    'return '+pacedSource.slice(callbackStart,callbackEnd+1))(
    true,(_operation,value)=>wait(value),26,event=>recorded.push(event));
  const handoff=waitForLateRemotePairAndResume({tick:26,sourcePoll,requireFollowingNeutral:true,
    waitForRemoteSample,
    admitRemoteSample(tick){assert.equal(tick,++through);events.push(['admit',tick]);},
    resumeLateFrontier(){events.push(['resume']);},
  });
  assert.deepEqual(events,[['begin',1,26,140],['wait',26]]);
  time=180;releaseTarget();
  const result=await handoff;
  assert.deepEqual(events,[['begin',1,26,140],['wait',26],['end',1,26,180],
    ['admit',26],['admit',27],['resume']]);
  assert.deepEqual(recorded,[{kind:'released-late-input',input_ordinal:26,
    required_input_through:27,started_at_ms:140,observed_at_ms:180,ended_at_ms:183,
    elapsed_ms:40,native_excluded_ms:39,interrupted:false}]);
  assert.deepEqual(result.networkWaits,[
    {tick:26,startedAt:140,observedAt:180,endedAt:183,excludedMs:39},
    {tick:27,startedAt:183,observedAt:183,endedAt:183,excludedMs:0},
  ]);
});

test('terminal streaming agreement retires confirmed receipts and still ACKs the final row',async()=>{
  const base={sourceIdentityJson:identityText(120),loadedIdentity,sourceFrame:120,
    inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS};
  const agreement=buildRuntimeMatchHistoryPeerAgreement({...base,matchFinal:true,streaming:true});
  assert.equal(agreement.scope,RUNTIME_MATCH_HISTORY_STREAM_PEER_SCOPE);
  assert.equal(agreement.input_ticks,215998);
  assert.equal(agreement.source_ticks,216000);
  assert.equal(agreement.match_final,1);
  assert.equal(agreement.streaming,1);
  assert.throws(()=>buildRuntimeMatchHistoryPeerAgreement({...base,streaming:true}),/requires the final/);
  assert.throws(()=>buildRuntimeMatchHistoryPeerAgreement({...base,inputTicks:3600,
    matchFinal:true,streaming:true}),/supports one, two, four, eight, 120, 600, or 3600/);

  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,
    matchFinal:true,streaming:true});
  const beta=makePeer(bus,'beta',{inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,
    matchFinal:true,streaming:true});
  const [startedAlpha,startedBeta]=await Promise.all([alpha.start(),beta.start()]);
  assert.equal(startedAlpha.agreement_hash,startedBeta.agreement_hash);
  for(const role of ['alpha','beta']){
    const hello=bus.sent[role].map(text=>JSON.parse(text)).find(packet=>packet.type==='hello');
    assert.equal(hello.agreement.streaming,1);
    assert.equal(hello.agreement.match_final,1);
    assert.equal((role==='alpha'?alpha:beta).snapshot().protocol_only_frames.length,0,
      'streaming mode keeps the preroll count without an unbounded frame journal');
  }

  const flush=async()=>{
    for(let round=0;round<12;++round){
      const before=bus.sent.alpha.length+':'+bus.sent.beta.length;
      await Promise.all([bus.flush('alpha'),bus.flush('beta')]);
      if(before===bus.sent.alpha.length+':'+bus.sent.beta.length)return;
    }
    assert.fail('stream peer state ACKs did not settle in the bounded fixture');
  };
  for(let tick=0;tick<32;++tick){
    await Promise.all([
      alpha.publishLocal({port:0,tick,pollSerial:String(1000+tick),pad:validPad(tick+1)}),
      beta.publishLocal({port:1,tick,pollSerial:String(2000+tick),pad:validPad(tick+101)}),
    ]);
    await Promise.all([alpha.waitForRemoteSample({tick}),beta.waitForRemoteSample({tick})]);
    await flush();
    for(const peer of [alpha,beta]){
      const result=peer.prune({confirmedThrough:tick,retainedFrom:Math.max(0,tick-7),
        nativeConsumedThrough:tick});
      assert.equal(result.frozen,false);
      const snapshot=peer.snapshot();
      assert.equal(snapshot.protocol_frame_count,tick+3);
      assert(snapshot.retained_local_receipts<=8);
      assert(snapshot.retained_remote_receipts<=8);
    }
  }
  for(const peer of [alpha,beta]){
    assert.equal(peer.remoteSnapshot(23),null);
    assert.equal(peer.remoteSnapshot(24).protocol_input_tick,24);
    assert.equal(peer.remoteSnapshot(31).protocol_input_tick,31);
    await assert.rejects(peer.waitForRemoteSample({tick:23}),/older than the retained stream window/);
    const frozen=peer.prune({confirmedThrough:31,retainedFrom:32,
      nativeConsumedThrough:31,planFrom:24});
    assert.equal(frozen.frozen,true,'an outstanding plan freezes transport receipt pruning');
  }

  const inputOrdinal=31,sourceFrame=602,checksum=finalChecksum(inputOrdinal,sourceFrame);
  const [finalAlpha,finalBeta]=await Promise.all([
    alpha.publishMatchFinal({inputOrdinal,sourceFrame,checksum,...finalOutcome}),
    beta.publishMatchFinal({inputOrdinal,sourceFrame,checksum,...finalOutcome}),
  ]);
  assert.deepEqual({...finalAlpha,role:undefined},{...finalBeta,role:undefined});
  assert.equal(finalAlpha.input_ordinal,inputOrdinal);
  assert.equal(finalAlpha.matched_bytes,64);
  assert.equal(alpha.snapshot().retained_local_receipts,8);
  assert.equal(alpha.snapshot().retained_remote_receipts,8);
  await Promise.all([alpha.close(),beta.close()]);
});

test('terminal streaming refuses receipt growth when its owner omits safe pruning',async()=>{
  const bus=createEndpointBus();
  const alpha=makePeer(bus,'alpha',{inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,
    matchFinal:true,streaming:true});
  const beta=makePeer(bus,'beta',{inputTicks:RUNTIME_MATCH_HISTORY_STREAM_INPUT_TICKS,
    matchFinal:true,streaming:true});
  await Promise.all([alpha.start(),beta.start()]);
  for(let tick=0;tick<32;++tick){
    await alpha.publishLocal({port:0,tick,pollSerial:String(3000+tick),pad:validPad(tick+3)});
    await beta.waitForRemoteSample({tick});
    await bus.flush('alpha');
    await bus.flush('beta');
  }
  assert.equal(alpha.snapshot().retained_local_receipts,32);
  assert.equal(beta.snapshot().retained_remote_receipts,32);
  await assert.rejects(alpha.publishLocal({port:0,tick:32,pollSerial:'3032',pad:validPad(35)}),
    /stream_local_receipt_window_exhausted/);
  assert.equal(alpha.snapshot().retained_local_receipts,32,
    'the overflow refusal does not retain a 33rd local receipt');
  await Promise.allSettled([alpha.close(),beta.close()]);
});
