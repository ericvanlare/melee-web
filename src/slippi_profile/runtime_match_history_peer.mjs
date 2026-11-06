// SPDX-License-Identifier: GPL-3.0-or-later
// Page-owned two-input peer transport for the private ordinary-runtime
// retained-history diagnostic. This module has no native/game input path.
import {LockstepPeer, LOCKSTEP_VERSION, lockstepConstants} from '../../scripts/net_lockstep_core.mjs';
import {createRoomRelayPeerEndpoint} from '../../scripts/net_lockstep_websocket_relay.mjs';

export const RUNTIME_MATCH_HISTORY_PEER_SCOPE = 'melee-web-runtime-one-sample-history-peer-v1';
export const RUNTIME_MATCH_HISTORY_TWO_WINDOW_PEER_SCOPE = 'melee-web-runtime-two-window-history-peer-v1';
export const RUNTIME_MATCH_HISTORY_PEER_PROFILE =
  'slippi-render-dependencies-logical-frame-lobj-tobj-camera-history-v20';
export const RUNTIME_MATCH_HISTORY_PROTOCOL_SOURCE_TICKS = 3;
export const RUNTIME_MATCH_HISTORY_PROTOCOL_INPUT_TICKS = 1;
export const RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS = 2;
export const RUNTIME_MATCH_HISTORY_PEER_TIMEOUT_MS = 5000;
const PAD_BYTES = 11;
const FRAME_BYTES = 44;
const MAX_SOURCE_IDENTITY_BYTES = 64 * 1024;
const POINTER_FIELDS = Object.freeze([
  'on_unpause_override', 'on_pause_override', 'check_for_pauser_override',
  'on_match_start', 'on_frame_start', 'on_frame_end', 'on_match_end',
  'x54_pointer', 'x58_pointer',
]);

function requireCondition(condition, message) {
  if (!condition) throw Error(message);
}

function timeoutValue(value, label) {
  requireCondition(Number.isSafeInteger(value) && value > 0,
    `${label} must be a positive safe integer`);
  return value;
}

function lowerSha256(value) {
  return typeof value === 'string' && /^[0-9a-f]{64}$/.test(value);
}

function canonicalPollSerial(value) {
  if (typeof value === 'bigint') {
    requireCondition(value > 0n && value <= 0xffffffffffffffffn,
      'Native PAD poll serial must be a nonzero uint64');
    return value.toString(10);
  }
  if (typeof value === 'string') {
    requireCondition(/^[1-9][0-9]{0,19}$/.test(value),
      'Native PAD poll serial must be a canonical nonzero uint64 decimal string');
    requireCondition(BigInt(value) <= 0xffffffffffffffffn,
      'Native PAD poll serial must be a nonzero uint64');
    return value;
  }
  requireCondition(Number.isSafeInteger(value) && value > 0,
    'Native PAD poll serial must be a nonzero safe integer');
  return String(value);
}

function copyPad(value) {
  let bytes;
  if (value instanceof Uint8Array) bytes = new Uint8Array(value);
  else if (value instanceof ArrayBuffer) bytes = new Uint8Array(value.slice(0));
  else if (ArrayBuffer.isView(value) && value.BYTES_PER_ELEMENT === 1)
    bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
  else if (Array.isArray(value) && value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255))
    bytes = Uint8Array.from(value);
  else throw Error('Native PAD sample must be an explicit byte view');
  requireCondition(bytes.length === PAD_BYTES,
    'Native PAD sample must contain exactly 11 bytes');
  return bytes;
}

function toHex(bytes) {
  let value = '';
  for (const byte of bytes) value += byte.toString(16).padStart(2, '0');
  return value;
}

function fromHex(value) {
  if (typeof value !== 'string' || value.length % 2 || !/^[0-9a-f]*$/.test(value))
    throw Error('Lockstep constant is not canonical hexadecimal');
  const bytes = new Uint8Array(value.length / 2);
  for (let index = 0; index < bytes.length; ++index)
    bytes[index] = Number.parseInt(value.slice(index * 2, index * 2 + 2), 16);
  return bytes;
}

function frozenReceipt(receipt) {
  if (receipt === null) return null;
  return Object.freeze({...receipt, pad: Object.freeze([...receipt.pad])});
}

function validateSourceIdentity(sourceIdentityJson, loadedIdentity, sourceFrame) {
  requireCondition(typeof sourceIdentityJson === 'string' && sourceIdentityJson.length > 0 &&
    new TextEncoder().encode(sourceIdentityJson).byteLength <= MAX_SOURCE_IDENTITY_BYTES,
    'Runtime history peer requires bounded exact source match identity JSON');
  let identity;
  try { identity = JSON.parse(sourceIdentityJson); }
  catch { throw Error('Runtime history source match identity is not valid JSON'); }
  requireCondition(identity && typeof identity === 'object' && !Array.isArray(identity) &&
    identity.schema === 'melee-web-runtime-match-identity-v1',
    'Runtime history peer requires the exact source match identity schema');
  for (const field of ['runtime_sha256', 'asset_identity_sha256', 'save_profile_sha256']) {
    requireCondition(lowerSha256(loadedIdentity?.[field]),
      `Loaded runtime identity requires lowercase ${field}`);
    requireCondition(identity[field] === loadedIdentity[field],
      `Source match identity ${field} differs from the loaded runtime`);
  }
  requireCondition(loadedIdentity?.profile_identity === RUNTIME_MATCH_HISTORY_PEER_PROFILE &&
    identity.profile_identity === loadedIdentity.profile_identity,
    'Source match identity differs from the active camera-history V20 profile');
  requireCondition(Number.isSafeInteger(sourceFrame) && sourceFrame >= 0 &&
    sourceFrame <= 0xffffffff && identity.source_frame === sourceFrame,
    'Source match identity differs from the agreed source frame');
  const selection = identity.selection;
  requireCondition(selection && selection.player_count === 2 &&
    selection.opening_demo === 0 && selection.sudden_death === 0,
    'Source match identity is not the supported ordinary two-player selection');
  const mapping = selection.controller_mapping;
  requireCondition(Array.isArray(mapping) && mapping.length === 2 &&
    mapping.every((row, index) => row && row.source_slot === index &&
      row.source_port === index && row.raw_controller === index),
    'Source match identity does not bind source slots and raw controllers zero and one');
  const start = selection.start;
  requireCondition(start && start.rules && Array.isArray(start.players) && start.players.length === 6,
    'Source match identity is missing the complete pointer-free source start');
  requireCondition(POINTER_FIELDS.every(field => start.rules[field] === 'null'),
    'Source match identity contains unresolved pointer-bearing start data');
  requireCondition(/^[0-9a-f]{8}$/.test(identity.initial_selection_seed || '') &&
    /^[0-9a-f]{8}$/.test(identity.current_rng || ''),
    'Source match identity must keep the initial selection seed and current RNG distinct');
  return identity;
}

/** Build a fixed transport agreement around the exact pointer-free source JSON.
 * Loaded hashes are supplied by the ordinary-runtime host and are cross-checked
 * against the source serializer output; this function does not establish their
 * provenance. */
export function buildRuntimeMatchHistoryPeerAgreement({sourceIdentityJson,
  loadedIdentity, sourceFrame, inputTicks = RUNTIME_MATCH_HISTORY_PROTOCOL_INPUT_TICKS} = {}) {
  requireCondition(Number.isSafeInteger(inputTicks) && inputTicks >= 1 &&
    inputTicks <= RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS,
    'Runtime history peer supports one or two indexed input samples');
  validateSourceIdentity(sourceIdentityJson, loadedIdentity, sourceFrame);
  return Object.freeze({
    protocol: 'melee-web-local-lockstep-a2-v1',
    scope: inputTicks === 1 ? RUNTIME_MATCH_HISTORY_PEER_SCOPE : RUNTIME_MATCH_HISTORY_TWO_WINDOW_PEER_SCOPE,
    source_ticks: inputTicks + 2,
    input_ticks: inputTicks,
    source_frame: sourceFrame,
    runtime_sha256: loadedIdentity.runtime_sha256,
    asset_identity_sha256: loadedIdentity.asset_identity_sha256,
    save_profile_sha256: loadedIdentity.save_profile_sha256,
    profile_identity: loadedIdentity.profile_identity,
    source_match_identity_json: sourceIdentityJson,
  });
}

function deferred() {
  let resolve, reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  promise.catch(() => {});
  return {promise, resolve, reject};
}

function withDeadline(promise, timeoutMs, label) {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(Error(`${label} exceeded ${timeoutMs} ms`)), timeoutMs);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

/** Own one page WebSocket endpoint and LockstepPeer for at most two indexed
 * native-validated samples. The two neutral frames and the two sample frames
 * are retained as protocol evidence only; this owner never forwards them to a
 * game or native input API. */
export function createRuntimeMatchHistoryPeer({role, relayUrl, roomId, timeoutMs = RUNTIME_MATCH_HISTORY_PEER_TIMEOUT_MS,
  sampleTimeoutMs = timeoutMs, sourceIdentityJson, loadedIdentity, sourceFrame,
  inputTicks = RUNTIME_MATCH_HISTORY_PROTOCOL_INPUT_TICKS,
  validateNativePad, createEndpoint = createRoomRelayPeerEndpoint} = {}) {
  timeoutMs = timeoutValue(timeoutMs, 'Runtime history peer timeout');
  sampleTimeoutMs = timeoutValue(sampleTimeoutMs, 'Runtime history remote sample timeout');
  requireCondition(role === 'alpha' || role === 'beta',
    'Runtime history peer role must be alpha or beta');
  requireCondition(typeof relayUrl === 'string' && /^wss?:\/\//.test(relayUrl),
    'Runtime history peer requires an explicit room WebSocket URL');
  requireCondition(typeof roomId === 'string' && /^[A-Za-z0-9_-]{24,64}$/.test(roomId),
    'Runtime history peer requires a bounded room ID');
  requireCondition(typeof validateNativePad === 'function',
    'Runtime history peer requires the loaded runtime native pure PAD validator');
  requireCondition(typeof createEndpoint === 'function',
    'Runtime history peer requires the existing room relay endpoint factory');

  requireCondition(Number.isSafeInteger(inputTicks) && inputTicks >= 1 &&
    inputTicks <= RUNTIME_MATCH_HISTORY_PROTOCOL_MAX_INPUT_TICKS,
    'Runtime history peer supports one or two indexed input samples');
  const sourceTicks = inputTicks + 2;
  const agreement = buildRuntimeMatchHistoryPeerAgreement({sourceIdentityJson, loadedIdentity, sourceFrame, inputTicks});
  const localPort = role === 'alpha' ? 0 : 1;
  const remotePort = role === 'alpha' ? 1 : 0;
  const remoteRole = role === 'alpha' ? 'beta' : 'alpha';
  const readyGate = deferred();
  const protocolFrameRows = [];
  const neutralPad = fromHex(lockstepConstants.neutralPad);
  const noControllerPad = fromHex(lockstepConstants.noControllerPad);
  let peer = null, endpoint = null, startOperation = null, closeOperation = null;
  let failure = null, intentionalClose = false, closed = false;
  const localPads = new Map(), localReceipts = new Map(), remoteReceipts = new Map();
  const localPublishPending = new Set();
  const remoteWaiters = new Set();

  const wakeRemoteWaiters = () => {
    for (const waiter of [...remoteWaiters]) waiter.wake();
  };
  const latch = error => {
    failure ??= error instanceof Error ? error : Error(String(error));
    readyGate.reject(failure);
    wakeRemoteWaiters();
    return failure;
  };
  const throwIfFailed = () => { if (failure) throw failure; };
  const expectedProtocolFrame = protocolTick => {
    const expected = new Uint8Array(FRAME_BYTES);
    if (protocolTick < 2) {
      expected.set(neutralPad, 0);
      expected.set(neutralPad, PAD_BYTES);
    } else {
      const inputTick = protocolTick - 2;
      const localPad = localPads.get(inputTick), remoteReceipt = remoteReceipts.get(inputTick);
      requireCondition(localPad && remoteReceipt,
        `Lockstep protocol frame ${protocolTick} requires both indexed input ${inputTick} contributions`);
      expected.set(localPad, localPort * PAD_BYTES);
      expected.set(remoteReceipt.pad, remotePort * PAD_BYTES);
    }
    expected.set(noControllerPad, 2 * PAD_BYTES);
    expected.set(noControllerPad, 3 * PAD_BYTES);
    return expected;
  };
  const captureProtocolFrames = async (firstTick, input) => {
    const bytes = copyFrameBytes(input);
    requireCondition(Number.isSafeInteger(firstTick) && firstTick === protocolFrameRows.length &&
      bytes.length > 0 && bytes.length % FRAME_BYTES === 0,
      'Lockstep protocol frame callback is not a contiguous 44-byte sequence');
    const count = bytes.length / FRAME_BYTES;
      requireCondition(firstTick + count <= sourceTicks,
      `Lockstep protocol exceeded its ${sourceTicks}-frame diagnostic bound`);
    const staged = [];
    for (let offset = 0; offset < count; ++offset) {
      const protocolTick = firstTick + offset;
      const frame = bytes.slice(offset * FRAME_BYTES, (offset + 1) * FRAME_BYTES);
      const expected = expectedProtocolFrame(protocolTick);
      requireCondition(equalBytes(frame, expected),
        `Lockstep protocol frame ${protocolTick} differs from its PAD ownership`);
      staged.push(Object.freeze({
        protocol_source_tick: protocolTick,
        protocol_input_tick: protocolTick < 2 ? null : protocolTick - 2,
        protocol_preroll_neutral: protocolTick < 2,
        match_source_frame: null,
        not_for_gameplay: true,
        bytes_hex: toHex(frame),
      }));
    }
    protocolFrameRows.push(...staged);
  };

  peer = new LockstepPeer({
    role,
    sourceTicks,
    inputTicks,
    pushFrame: captureProtocolFrames,
    onReady: () => readyGate.resolve(),
    onTerminal: terminal => {
      if (intentionalClose) return;
      const error = Error(`Runtime history peer reached ${terminal.kind}: ${JSON.stringify(terminal)}`);
      latch(error);
    },
    onRemoteContributionBatch: batch => {
      requireCondition(!closed && !failure,
        'Remote PAD arrived after runtime history peer failure or close');
      requireCondition(batch?.protocolVersion === LOCKSTEP_VERSION &&
        batch.agreementHash === peer.agreementHash && batch.senderRole === remoteRole &&
        batch.remotePort === remotePort && Number.isSafeInteger(batch.packetSequence) &&
        batch.packetSequence >= 0,
        'Accepted remote PAD batch differs from agreed peer, role, port, or packet identity');
      requireCondition(Array.isArray(batch.inputs) && batch.inputs.length > 0 &&
        batch.inputs.length <= inputTicks - remoteReceipts.size,
        `Runtime history peer remote PAD batch exceeded its ${inputTicks}-input bound`);
      let nextTick = remoteReceipts.size;
      const staged = [];
      for (const input of batch.inputs) {
        requireCondition(input?.tick === nextTick && nextTick < inputTicks,
          `Runtime history peer accepts remote PAD inputs once, in contiguous index order 0..${inputTicks-1}`);
        const pad = copyPad(input.pad);
        staged.push(Object.freeze({
          accepted_by: 'lockstep-protocol',
          sender_role: batch.senderRole,
          remote_port: batch.remotePort,
          protocol_input_tick: nextTick,
          packet_sequence: batch.packetSequence,
          agreement_hash: batch.agreementHash,
          pad: Object.freeze(Array.from(pad)),
        }));
        ++nextTick;
      }
      for (const receipt of staged) remoteReceipts.set(receipt.protocol_input_tick, receipt);
      wakeRemoteWaiters();
    },
  });

  async function receiveMessage(text) {
    if (!peer) throw Error('Runtime history peer receive arrived before LockstepPeer ownership');
    return peer.receive(text);
  }
  async function onDisconnect(disconnectedRole, reason) {
    if (intentionalClose) return;
    const error = Error(`Runtime history ${disconnectedRole} relay disconnected: ${reason}`);
    latch(error);
    try { await peer.disconnect(String(reason || 'room relay closed')); }
    catch (terminalError) { latch(terminalError); }
  }
  function onEndpointError(errorRole, error) {
    if (intentionalClose) return;
    const cause = error instanceof Error ? error : Error(String(error));
    latch(Error(`Runtime history ${errorRole} room endpoint failed: ${cause.message}`, {cause}));
  }

  function start() {
    if (startOperation) return startOperation;
    if (closed) return Promise.reject(Error('Runtime history peer is closed'));
    if (failure) return Promise.reject(failure);
    startOperation = (async () => {
      try {
        endpoint = createEndpoint({url: relayUrl, roomId, role, timeoutMs,
          onMessage: receiveMessage, onDisconnect, onEndpointError});
        if (!endpoint || typeof endpoint.send !== 'function' || !endpoint.ready ||
            typeof endpoint.close !== 'function' || typeof endpoint.drainInbound !== 'function')
          throw Error('Room relay endpoint is missing its bounded send/receive/close contract');
        peer.attach(text => endpoint.send(text));
        const helloSent = peer.start(agreement);
        helloSent.catch(error => latch(error));
        await withDeadline(Promise.all([endpoint.ready, helloSent, readyGate.promise]), timeoutMs,
          'Runtime history peer identity agreement');
        await withDeadline(peer.pump, timeoutMs, 'Runtime history peer preroll assembly');
        throwIfFailed();
        requireCondition(protocolFrameRows.length === 2 &&
          protocolFrameRows.every((row, index) => row.protocol_source_tick === index &&
          row.protocol_preroll_neutral && row.match_source_frame === null && row.not_for_gameplay),
          'Runtime history peer did not retain exactly two neutral protocol preroll frames');
        return snapshot();
      } catch (error) {
        const cause = latch(error);
        try { await close(); }
        catch (cleanupError) {
          throw new AggregateError([cause, cleanupError],
            'Runtime history peer start failed and transport cleanup also failed');
        }
        throw cause;
      }
    })();
    startOperation.catch(() => {});
    return startOperation;
  }

  async function publishLocal({port, tick, pollSerial, pad} = {}) {
    throwIfFailed();
    requireCondition(!closed && peer?.ready,
      'Runtime history local PAD requires a ready, open peer');
    requireCondition(Number.isSafeInteger(tick) && tick >= 0 &&
      tick < inputTicks && tick === localReceipts.size &&
      localPublishPending.size === 0 &&
      !localReceipts.has(tick) && !localPublishPending.has(tick),
      `Runtime history peer accepts local PAD publications once, in contiguous index order 0..${inputTicks-1}`);
    requireCondition(port === localPort,
      'Runtime history local PAD must use this peer port');
    const serial = canonicalPollSerial(pollSerial);
    const bytes = copyPad(pad);
    let valid = false;
    try { valid = validateNativePad(new Uint8Array(bytes), {port, tick, pollSerial: serial}); }
    catch (error) { throw Error(`Native pure PAD validator failed: ${String(error?.message || error)}`, {cause: error}); }
    requireCondition(valid === true,
      'Native pure PAD validator refused the retained local sample');
    localPublishPending.add(tick);
    localPads.set(tick, new Uint8Array(bytes));
    try {
      await peer.addLocalInput(tick, new Uint8Array(bytes), {
        nativeSample: new Uint8Array(bytes),
      });
      throwIfFailed();
      const receipt = Object.freeze({
        accepted_by: 'native-validated-local-poll',
        local_port: localPort,
        protocol_input_tick: tick,
        poll_serial: serial,
        pad: Object.freeze(Array.from(bytes)),
      });
      localReceipts.set(tick, receipt);
      return frozenReceipt(receipt);
    } catch (error) {
      latch(error);
      throw failure;
    } finally {
      localPublishPending.delete(tick);
    }
  }

  function inputTick(value) {
    requireCondition(Number.isSafeInteger(value) && value >= 0 &&
      value < inputTicks,
      `Runtime history peer input tick is outside the ${inputTicks}-sample bound`);
    return value;
  }

  function remoteSnapshot(tick = 0) {
    return frozenReceipt(remoteReceipts.get(inputTick(tick)) ?? null);
  }

  async function waitForRemoteSample({tick = 0, timeoutMs: waitTimeoutMs = sampleTimeoutMs} = {}) {
    tick = inputTick(tick);
    timeoutValue(waitTimeoutMs, 'Runtime history remote sample wait');
    throwIfFailed();
    if (closed) throw Error('Runtime history peer closed before remote PAD acceptance');
    if (remoteReceipts.has(tick)) {
      const receipt = remoteSnapshot(tick);
      await Promise.resolve();
      throwIfFailed();
      if (closed) throw Error('Runtime history peer closed before remote PAD acceptance');
      return receipt;
    }
    let resolve, reject, timer;
    const promise = new Promise((resolvePromise, rejectPromise) => {
      resolve = resolvePromise;
      reject = rejectPromise;
    });
    const waiter = {wake: () => {
      if (failure) reject(failure);
      else if (closed) reject(Error('Runtime history peer closed before remote PAD acceptance'));
      else if (remoteReceipts.has(tick)) resolve(remoteSnapshot(tick));
    }};
    remoteWaiters.add(waiter);
    timer = setTimeout(() => reject(Error(`Runtime history remote PAD did not arrive within ${waitTimeoutMs} ms`)),
      waitTimeoutMs);
    try {
      waiter.wake();
      const receipt = await promise;
      throwIfFailed();
      if (closed) throw Error('Runtime history peer closed before remote PAD acceptance');
      return receipt;
    } finally {
      clearTimeout(timer);
      remoteWaiters.delete(waiter);
    }
  }

  function snapshot() {
    return Object.freeze({
      schema: 'melee-web-runtime-match-history-peer-snapshot-v1',
      scope: agreement.scope,
      execution_scope: 'page-peer-transport-only; module code is not execution evidence',
      role, local_port: localPort, remote_port: remotePort,
      identity_source_frame: sourceFrame,
      source_ticks_protocol_only: sourceTicks,
      input_ticks_protocol_only: inputTicks,
      protocol_version: LOCKSTEP_VERSION,
      agreement_hash: peer?.agreementHash ?? null,
      ready: peer?.ready === true,
      terminal: peer?.terminal ? Object.freeze({...peer.terminal}) : null,
      local_contribution: frozenReceipt(localReceipts.get(0) ?? null),
      remote_contribution: remoteSnapshot(0),
      local_contributions: Object.freeze([...localReceipts.values()].map(frozenReceipt)),
      remote_contributions: Object.freeze([...remoteReceipts.values()].map(frozenReceipt)),
      protocol_only_frames: Object.freeze(protocolFrameRows.map(row => Object.freeze({...row}))),
      socket_owner: endpoint ? 'browser-page' : null,
      socket_ready: endpoint?.closed === false && peer?.ready === true,
      module_native_game_input_calls: 0,
      failure: failure ? String(failure.message || failure) : null,
      closed,
      transport_type: endpoint?.transport?.type ?? 'room-websocket',
    });
  }

  function close() {
    if (closeOperation) return closeOperation;
    intentionalClose = true;
    closed = true;
    wakeRemoteWaiters();
    closeOperation = (async () => {
      const cleanupErrors = [];
      try {
        if (peer && !peer.terminal) await peer.disconnect('runtime history peer closed');
      } catch (error) { cleanupErrors.push(error); }
      if (endpoint) {
        try { await withDeadline(endpoint.close(), timeoutMs, 'Runtime history relay close'); }
        catch (error) { cleanupErrors.push(error); }
        try { await withDeadline(endpoint.drainInbound(timeoutMs), timeoutMs,
          'Runtime history relay inbound drain'); }
        catch (error) { cleanupErrors.push(error); }
      }
      if (peer) {
        try { await withDeadline(peer.receiveQueue, timeoutMs, 'Runtime history protocol receive queue'); }
        catch (error) { cleanupErrors.push(error); }
        try { await withDeadline(peer.pump, timeoutMs, 'Runtime history protocol frame queue'); }
        catch (error) { cleanupErrors.push(error); }
      }
      if (endpoint?.errors?.length) cleanupErrors.push(...endpoint.errors);
      if (cleanupErrors.length)
        throw new AggregateError(cleanupErrors, 'Runtime history peer cleanup failed');
      return snapshot();
    })();
    closeOperation.catch(() => {});
    return closeOperation;
  }

  return Object.freeze({start, publishLocal, remoteSnapshot, waitForRemoteSample, snapshot, close,
    get failure() { return failure; }, get closed() { return closed; }});
}

function copyFrameBytes(value) {
  if (value instanceof Uint8Array) return new Uint8Array(value);
  if (ArrayBuffer.isView(value) && value.BYTES_PER_ELEMENT === 1)
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength).slice();
  if (value instanceof ArrayBuffer) return new Uint8Array(value.slice(0));
  throw Error('Lockstep protocol frame batch must be a byte view');
}

function equalBytes(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; ++index)
    if (left[index] !== right[index]) return false;
  return true;
}
