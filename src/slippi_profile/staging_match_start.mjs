// SPDX-License-Identifier: GPL-3.0-or-later
// One-shot, page-owned original-menu agreement. This exchanges source-owned
// contexts; it never generates PAD, edits a selection, or advances a game tick.
import {createRoomRelayPeerEndpoint} from '../../scripts/net_lockstep_websocket_relay.mjs';
import {validateRuntimeMatchStartPrefix} from './runtime_match_history_controller.mjs';

const SCHEMA = 'melee-web-staging-match-session-v2';
const STAGES = ['offer', 'agreement', 'held'];
const LIMIT = 65536;
const encoder = new TextEncoder();
const identityFields = ['runtime_sha256', 'asset_identity_sha256', 'save_profile_sha256', 'profile_identity'];
const assert = (value, message) => { if (!value) throw Error(message); };
const exactKeys = (value, keys) => value && typeof value === 'object' && !Array.isArray(value) &&
  Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
export function validateStagingLobbyState(value) {
  assert(exactKeys(value, ['phase', 'characters', 'colors', 'stage']) &&
    ['css', 'sss', 'selected', 'preparing'].includes(value.phase) &&
    Array.isArray(value.characters) && value.characters.length === 2 &&
    value.characters.every(kind => Number.isInteger(kind) && kind >= -1 && kind <= 255) &&
    Array.isArray(value.colors) && value.colors.length === 2 &&
    value.colors.every(color => Number.isInteger(color) && color >= 0 && color <= 255) &&
    (value.stage === null || Number.isInteger(value.stage) && value.stage >= -1 && value.stage <= 255),
    'Invalid shared lobby state');
  return {phase:value.phase, characters:[...value.characters], colors:[...value.colors],stage:value.stage};
}
function boundedText(value, label) {
  assert(typeof value === 'string' && value.length && !value.includes('\0') &&
    encoder.encode(value).byteLength <= LIMIT, `${label} is not bounded source JSON`);
  return value;
}
function parseOffer(text, identity) {
  const value = JSON.parse(boundedText(text, 'Offer'));
  assert(exactKeys(value, ['schema', ...identityFields, 'local_menu_rng_seed_provenance', 'selection']) &&
    value.schema === 'melee-web-runtime-match-start-offer-v1', 'Unexpected native offer schema');
  for (const field of identityFields)
    assert(value[field] === identity[field], `Remote start ${field} differs from the loaded runtime`);
  for (const field of identityFields.slice(0, 3))
    assert(/^[0-9a-f]{64}$/.test(value[field]), `Invalid loaded ${field}`);
  assert(typeof value.profile_identity === 'string' && value.profile_identity.length > 0,
    'Missing loaded profile identity');
  assert(value.selection && typeof value.selection === 'object' && !Array.isArray(value.selection),
    'Source offer omitted its complete selection');
  assert(/^(?:0x)?[0-9a-f]{8}$/.test(value.selection.random_seed), 'Invalid source-selected seed');
  assert(/^(?:0x)?[0-9a-f]{8}$/.test(value.local_menu_rng_seed_provenance), 'Invalid menu seed provenance');
  return value;
}
function withoutSeed(offer) {
  const selection = {...offer.selection};
  delete selection.random_seed;
  // Both strings originate from the same pinned native serializer. Compare all
  // remaining fields and their order; never merge the players or their rules.
  return JSON.stringify(selection);
}
async function sha256(text) {
  const bytes = await globalThis.crypto.subtle.digest('SHA-256', encoder.encode(text));
  return [...new Uint8Array(bytes)].map(byte => byte.toString(16).padStart(2, '0')).join('');
}
export async function stagingHeldIdentity({identity, prefix, publication}) {
  boundedText(identity, 'Held identity');
  const value = validateRuntimeMatchStartPrefix(prefix, {publication, liveIdentity: identity});
  const compared = {};
  for (const field of ['schema', 'input_provenance', 'source_ticks', 'source_draws',
    'final_ready_tick', 'initial_resident_transfer_drains', 'final_resident_transfer_drains'])
    compared[field] = value[field];
  compared.rows = value.rows.map(row => {
    const result = {};
    for (const field of ['tick_before', 'tick_after', 'source_frame_before', 'source_frame_after',
      'hud_ready_before', 'hud_ready_after', 'pad_hex']) result[field] = row[field];
    return result;
  });
  return JSON.stringify({identity, prefix_sha256: await sha256(JSON.stringify(compared))});
}

/** Open the room before local menu selection. The native callbacks remain
 * owned by the page and are called once; only alpha's source seed is adopted.
 * The caller enables the native start owner before the local original menus
 * produce an offer. The initial slice requires the same complete Mario/FD
 * selection on both pages; no local-slot selection merge is implied.
 * A returned endpoint factory hands the same socket to the existing match peer.
 */
export function createStagingMatchStart({role, url, roomId, identity, native,
  timeoutMs = 120000, createEndpoint = createRoomRelayPeerEndpoint,
  onState = () => {}, onFailure = () => {}, onLobby = () => {}}) {
  assert(role === 'alpha' || role === 'beta', 'Unknown room role');
  assert(Number.isSafeInteger(timeoutMs) && timeoutMs > 0, 'Invalid room setup timeout');
  for (const method of ['offer', 'agreement', 'commit', 'held'])
    assert(typeof native?.[method] === 'function', `Missing native start ${method}`);
  const remoteRole = role === 'alpha' ? 'beta' : 'alpha';
  const messages = new Map(), waiters = new Map(), queuedMatch = [];
  let received = 0, failure = null, closed = false, negotiated = false, attached = false;
  let matchCallbacks = null, queuedBytes = 0, operation = null, closeOperation = null;
  let endpoint = null, teardownQueued = false;
  let matchChain = Promise.resolve();
  let sentLobbyRevision = 0, receivedLobbyRevision = 0, lastLobby = null;
  const abort = new AbortController();
  let rejectStopped;
  const stopped = new Promise((_, reject) => { rejectStopped = reject; });
  stopped.catch(() => {});
  function fail(error) {
    if (failure) return failure;
    failure = error instanceof Error ? error : Error(String(error));
    abort.abort(failure); rejectStopped(failure);
    for (const waiter of waiters.values()) waiter.reject(failure);
    waiters.clear();
    try { onFailure(failure); } catch {}
    if (endpoint && !teardownQueued) {
      teardownQueued = true;
      // Closing drains the endpoint's inbound callbacks. Start it after this
      // callback unwinds, never await its own drain from the receive callback.
      setTimeout(() => { close().catch(() => {}); }, 0);
    }
    return failure;
  }
  function check() {
    if (failure) throw failure;
    assert(!closed, 'Room setup is closed');
  }
  function receive(text) {
    try {
      check();
      const packet = JSON.parse(text);
      if (typeof packet?.schema === 'string' && packet.schema.startsWith('melee-web-staging-match-session-') &&
          packet.schema !== SCHEMA)
        throw Error('Online player versions differ. Reload both computers before joining again.');
      assert(exactKeys(packet, ['schema', 'role', 'channel', 'stage', 'payload']) &&
        packet.schema === SCHEMA && packet.role === remoteRole, 'Unexpected room session packet');
      if (packet.channel === 'lobby') {
        assert(!negotiated && received < STAGES.length && packet.stage === receivedLobbyRevision + 1 &&
          receivedLobbyRevision < 1024, 'Invalid shared lobby revision');
        const state = validateStagingLobbyState(JSON.parse(boundedText(packet.payload, 'Lobby state')));
        ++receivedLobbyRevision;
        onLobby({role:remoteRole, revision:receivedLobbyRevision, state});
        return;
      }
      if (packet.channel === 'setup') {
        assert(packet.stage === STAGES[received], 'Duplicate or out-of-order setup packet');
        boundedText(packet.payload, 'Setup payload');
        ++received;
        messages.set(packet.stage, packet.payload);
        const waiter = waiters.get(packet.stage);
        if (waiter) { waiters.delete(packet.stage); waiter.resolve(packet.payload); }
        return;
      }
      assert(packet.channel === 'match' && packet.stage === null && received === STAGES.length,
        'Match traffic preceded source setup');
      boundedText(packet.payload, 'Match payload');
      if (!matchCallbacks) {
        const bytes = encoder.encode(packet.payload).byteLength;
        assert(queuedMatch.length < 16 && queuedBytes + bytes <= LIMIT,
          'Match handoff queue exceeded its bound');
        queuedMatch.push(packet.payload); queuedBytes += bytes;
        return;
      }
      matchChain = matchChain.then(() => matchCallbacks.onMessage(packet.payload));
      matchChain.catch(fail);
      return matchChain;
    } catch (error) { throw fail(error); }
  }
  endpoint = createEndpoint({url, roomId, role, timeoutMs, onMessage: receive,
    onDisconnect: (side, reason) => {
      if (closed) return;
      let disposition;
      try { disposition = matchCallbacks?.onDisconnect(side, reason); }
      catch (error) { throw fail(error); }
      if (disposition === 'expected-final-disconnect') return;
      fail(Error(`Room disconnected: ${reason}`));
      return disposition;
    },
    onEndpointError: (side, error) => {
      if (closed) return;
      fail(error);
      return matchCallbacks?.onEndpointError(side, error);
    }});
  async function exchange(stage, payload, deadline) {
    check(); boundedText(payload, stage);
    await endpoint.send(JSON.stringify({schema: SCHEMA, role, channel: 'setup', stage, payload}));
    check();
    if (messages.has(stage)) return messages.get(stage);
    const remaining = deadline - performance.now();
    assert(remaining > 0, 'Room setup timed out');
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { waiters.delete(stage); reject(fail(Error('Room setup timed out'))); },
        remaining);
      waiters.set(stage, {
        resolve(value) { clearTimeout(timer); resolve(value); },
        reject(error) { clearTimeout(timer); reject(error); },
      });
    });
  }
  async function publishLobby(value) {
    check();
    assert(!negotiated, 'Lobby publication followed match agreement');
    const state = validateStagingLobbyState(value), payload = JSON.stringify(state);
    if (payload === lastLobby) return;
    assert(sentLobbyRevision < 1024, 'Shared lobby update bound exceeded');
    await endpoint.ready;
    check();
    assert(!negotiated, 'Lobby publication followed match agreement');
    assert(sentLobbyRevision < 1024, 'Shared lobby update bound exceeded');
    // Recheck after readiness: two callers may have waited for the same peer.
    if (payload === lastLobby) return;
    const revision = ++sentLobbyRevision;
    lastLobby = payload;
    await endpoint.send(JSON.stringify({schema:SCHEMA, role, channel:'lobby', stage:revision, payload}));
    onLobby({role, revision, state});
  }
  function negotiate() {
    if (operation) return operation;
    operation = (async () => {
      try {
        check();
        const deadline = performance.now() + timeoutMs;
        const timer = setTimeout(() => fail(Error('Room setup timed out')), timeoutMs);
        const bounded = value => Promise.race([Promise.resolve(value), stopped]);
        try {
        await bounded(endpoint.ready);
        onState('Connected. Both players choose Mario, then Final Destination.');
        const localText = boundedText(await bounded(native.offer({signal: abort.signal})), 'Local offer');
        const local = parseOffer(localText, identity);
        const remote = parseOffer(await exchange('offer', localText, deadline), identity);
        assert(withoutSeed(local) === withoutSeed(remote),
          'Players selected different characters, stage, rules, or ports');
        const host = role === 'alpha' ? local : remote;
        const seed = Number.parseInt(host.selection.random_seed.replace(/^0x/, ''), 16);
        const agreement = boundedText(await bounded(native.agreement(seed, {signal: abort.signal})), 'Native agreement');
        assert(agreement === await exchange('agreement', agreement, deadline),
          'Native match-start agreements differ');
        check();
        await bounded(native.commit(seed, agreement, {signal: abort.signal}));
        onState('Preparing the agreed match.');
        const held = await bounded(native.held({signal: abort.signal}));
        const proof = await stagingHeldIdentity(held);
        assert(proof === await exchange('held', proof, deadline),
          'Constructed match state or actual startup inputs differ');
        check(); negotiated = true;
        onState('Match ready.');
        return Object.freeze({seed, agreement, liveIdentity: held.identity,
          heldProof: proof, localPort: role === 'alpha' ? 0 : 1});
        } finally { clearTimeout(timer); }
      } catch (error) { throw fail(error); }
    })();
    operation.catch(() => {});
    return operation;
  }
  function createMatchEndpoint(options) {
    check();
    assert(negotiated && !attached && options.role === role &&
      options.url === url && options.roomId === roomId, 'Match socket takeover refused');
    for (const callback of ['onMessage', 'onDisconnect', 'onEndpointError'])
      assert(typeof options[callback] === 'function', `Missing match ${callback}`);
    attached = true; matchCallbacks = options;
    for (const text of queuedMatch.splice(0))
      matchChain = matchChain.then(() => matchCallbacks.onMessage(text));
    queuedBytes = 0; matchChain.catch(fail);
    return Object.freeze({role, ready: Promise.resolve(), transport: endpoint.transport,
      get closed() { return closed || endpoint.closed; },
      get errors() { return endpoint.errors; },
      send(payload) {
        check(); boundedText(payload, 'Match payload');
        return endpoint.send(JSON.stringify({schema: SCHEMA, role, channel: 'match', stage: null, payload}));
      },
      flush: (...args) => endpoint.flush(...args),
      async drainInbound(...args) { await endpoint.drainInbound(...args); await matchChain; },
      close, destroy() { closed = true; fail(Error('Room stopped')); endpoint.destroy(); },
    });
  }
  function close() {
    if (closeOperation) return closeOperation;
    closed = true;
    abort.abort(Error('Room closed')); rejectStopped(abort.signal.reason);
    for (const waiter of waiters.values()) waiter.reject(Error('Room closed'));
    waiters.clear();
    closeOperation = endpoint.close();
    return closeOperation;
  }
  return Object.freeze({negotiate, publishLobby, createMatchEndpoint, close,
    get failed() { return failure; }});
}
