import {createRoomRelayPeerEndpoint} from './net_lockstep_websocket_relay.mjs';

const SIGNAL_PROTOCOL = 'melee-local-webrtc';
const SIGNAL_VERSION = 1;
const ROOM_ID_PATTERN = /^[A-Za-z0-9_-]{22,64}$/;

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  promise.catch(() => {});
  return {promise, resolve, reject};
}

function exactKeys(value, keys) {
  return value && typeof value === 'object' && !Array.isArray(value) &&
    Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value, key));
}

function description(value, expectedType, maxBytes) {
  if (!exactKeys(value, ['type', 'sdp']) || value.type !== expectedType ||
      typeof value.sdp !== 'string' || value.sdp.length === 0 ||
      new TextEncoder().encode(value.sdp).byteLength > maxBytes)
    throw Error('invalid-session-description');
  return {type: value.type, sdp: value.sdp};
}

/** Page-owned, one-off offer/answer exchange over the existing room endpoint.
 * The endpoint installs this receiver synchronously; neither signaling SDP nor
 * endpoint disconnects are forwarded to the native lockstep peer. */
export function createRoomWebRtcSignaler({url, roomId, role, timeoutMs = 5000,
  createOffer, acceptOffer, acceptAnswer, createEndpoint = createRoomRelayPeerEndpoint} = {}) {
  if (role !== 'alpha' && role !== 'beta') throw Error('WebRTC signaling role must be alpha or beta');
  if (typeof roomId !== 'string' || !ROOM_ID_PATTERN.test(roomId))
    throw Error('WebRTC signaling room ID is invalid');
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
    throw Error('WebRTC signaling timeout must be a positive integer');
  if (typeof createEndpoint !== 'function') throw Error('WebRTC signaling endpoint factory is required');
  if (role === 'alpha' && typeof createOffer !== 'function')
    throw Error('Alpha WebRTC signaling requires an offer creator');
  if (role === 'beta' && typeof acceptOffer !== 'function')
    throw Error('Beta WebRTC signaling requires an offer handler');
  if (role === 'alpha' && typeof acceptAnswer !== 'function')
    throw Error('Alpha WebRTC signaling requires an answer handler');

  const negotiated = deferred();
  let phase = role === 'alpha' ? 'ready' : 'waiting-offer';
  let failureCode = null;
  let closeArmed = false;
  let closePromise = null;
  let timer;
  let endpoint;

  const fail = code => {
    if (failureCode) return;
    failureCode = code;
    phase = 'failed';
    clearTimeout(timer);
    negotiated.reject(Error(code));
    try { endpoint?.destroy(); }
    catch { /* The owned close path reports cleanup failures separately. */ }
  };
  const assertHealthy = () => {
    if (failureCode) throw Error(failureCode);
  };
  const complete = () => {
    if (failureCode) return;
    phase = 'negotiated';
    clearTimeout(timer);
    negotiated.resolve(snapshot());
  };
  const peerRole = role === 'alpha' ? 'beta' : 'alpha';
  const packetLimit = () => endpoint?.transport?.packet_limit_bytes ?? 1024 * 1024;
  const encode = (kind, sessionDescription) => JSON.stringify({
    protocol: SIGNAL_PROTOCOL,
    version: SIGNAL_VERSION,
    roomId,
    from: role,
    to: peerRole,
    kind,
    description: sessionDescription,
  });
  const parse = text => {
    if (typeof text !== 'string' || new TextEncoder().encode(text).byteLength > packetLimit())
      throw Error('invalid-signal-size');
    let message;
    try { message = JSON.parse(text); }
    catch { throw Error('invalid-signal-json'); }
    if (!exactKeys(message, ['protocol', 'version', 'roomId', 'from', 'to', 'kind', 'description']) ||
        message.protocol !== SIGNAL_PROTOCOL || message.version !== SIGNAL_VERSION ||
        message.roomId !== roomId || message.from !== peerRole || message.to !== role)
      throw Error('invalid-signal-envelope');
    if (!['offer', 'answer'].includes(message.kind)) throw Error('invalid-signal-kind');
    return {...message, description: description(message.description, message.kind, packetLimit())};
  };
  const receive = async text => {
    try {
      assertHealthy();
      const message = parse(text);
      if (role === 'beta') {
        if (phase !== 'waiting-offer' || message.kind !== 'offer')
          throw Error('unexpected-offer-state');
        phase = 'applying-offer';
        let answer;
        try { answer = description(await acceptOffer(message.description), 'answer', packetLimit()); }
        catch { throw Error('offer-application-failed'); }
        assertHealthy();
        phase = 'answer-sending';
        try { await endpoint.send(encode('answer', answer)); }
        catch { throw Error('answer-send-failed'); }
        assertHealthy();
        complete();
        return;
      }
      if (phase !== 'offer-sent' || message.kind !== 'answer')
        throw Error('unexpected-answer-state');
      phase = 'applying-answer';
      try { await acceptAnswer(message.description); }
      catch { throw Error('answer-application-failed'); }
      assertHealthy();
      complete();
    } catch (error) {
      const code = /^[a-z][a-z0-9-]*$/.test(error?.message || '') ? error.message : 'invalid-signal';
      fail(code);
      throw Error(code);
    }
  };
  const onDisconnect = () => {
    if (!closeArmed) fail('signaling-disconnected');
  };
  const onEndpointError = () => fail('signaling-endpoint-error');

  // Beta is already in waiting-offer before endpoint construction. The
  // endpoint receives this callback at construction, before its READY promise.
  endpoint = createEndpoint({url, roomId, role, timeoutMs,
    onMessage: receive, onDisconnect, onEndpointError});
  if (!endpoint || !endpoint.ready || typeof endpoint.send !== 'function' ||
      typeof endpoint.close !== 'function' || typeof endpoint.destroy !== 'function')
    throw Error('WebRTC signaling endpoint is incomplete');
  endpoint.ready.catch(() => fail('room-ready-failed'));
  timer = setTimeout(() => fail('signaling-negotiation-timeout'), timeoutMs);

  if (role === 'alpha') {
    void (async () => {
      try {
        await endpoint.ready;
        assertHealthy();
        if (phase !== 'ready') throw Error('unexpected-offer-state');
        phase = 'creating-offer';
        let offer;
        try { offer = description(await createOffer(), 'offer', packetLimit()); }
        catch { throw Error('offer-creation-failed'); }
        assertHealthy();
        phase = 'offer-sent';
        try { await endpoint.send(encode('offer', offer)); }
        catch { throw Error('offer-send-failed'); }
      } catch (error) {
        const code = /^[a-z][a-z0-9-]*$/.test(error?.message || '') ? error.message : 'offer-send-failed';
        fail(code);
      }
    })();
  }

  function snapshot() {
    return {role, phase, failure_code: failureCode,
      endpoint_closed: Boolean(endpoint?.closed)};
  }
  return {
    negotiated: negotiated.promise,
    snapshot,
    assertHealthy,
    armClose() { closeArmed = true; },
    close() {
      if (closePromise) return closePromise;
      closeArmed = true;
      if (!failureCode && phase !== 'negotiated') fail('signaling-closed-before-negotiation');
      closePromise = (async () => {
        await endpoint.close();
        if (!failureCode) phase = 'closed';
        return snapshot();
      })();
      return closePromise;
    },
  };
}
