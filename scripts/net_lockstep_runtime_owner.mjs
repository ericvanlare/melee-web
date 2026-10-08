import {createNetLockstepNativeAdapter} from './net_lockstep_native_adapter.mjs';
import {BROWSER_CHECKSUM_EXPORT_LIMIT, createBrowserNativePeer} from './net_lockstep_browser_peer.mjs';
import {createDataChannelEndpoint} from './net_lockstep_webrtc.mjs';
import {createRoomWebRtcSignaler} from './net_lockstep_webrtc_signaling.mjs';
import {LOCKSTEP_DELAY, NET_RECORD_BYTES} from './net_lockstep_core.mjs';

const ROOM_ID_PATTERN = /^[A-Za-z0-9_-]{22,64}$/;

function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  promise.catch(() => {});
  return {promise, resolve, reject};
}

function createDeferredChannelEndpoint({channel, createEndpoint, role, timeoutMs, closeState}) {
  let endpoint = null, resolveReady, rejectReady;
  let readinessSettled = false, cancelled = false, closePromise = null;
  const ready = new Promise((resolve, reject) => { resolveReady = resolve; rejectReady = reject; });
  ready.catch(() => {});
  const settleReady = (error, value) => {
    if (readinessSettled) return false;
    readinessSettled = true;
    if (error) rejectReady(error);
    else resolveReady(value);
    return true;
  };
  const attach = value => {
    if (closeState.closed || cancelled) {
      try { value.close(); } catch {}
      return;
    }
    if (endpoint) throw Error('Local WebRTC data channel was attached more than once');
    endpoint = createEndpoint({channel: value, role, timeoutMs,
      onMessage: channel.onMessage, onDisconnect: channel.onDisconnect,
      onEndpointError: channel.onEndpointError});
    endpoint.ready.then(value => settleReady(null, value), error => settleReady(error));
  };
  if (channel.value) attach(channel.value);
  else channel.attach = attach;
  return {
    ready,
    get closed() { return closeState.closed || Boolean(endpoint?.closed); },
    get errors() { return endpoint?.errors ?? []; },
    get transport() { return endpoint?.transport ?? {type: 'webrtc-datachannel'}; },
    async send(text) { await ready; return endpoint.send(text); },
    async drainInbound(timeout = timeoutMs) {
      if (endpoint) return endpoint.drainInbound(timeout);
    },
    cancelPendingStart() {
      if (readinessSettled || cancelled) return false;
      cancelled = true;
      const error = Error('Local WebRTC endpoint readiness cancelled by runtime close');
      let closeError = null;
      try { endpoint?.destroy?.(); } catch (cause) { closeError = cause; }
      settleReady(error);
      if (closeError) throw closeError;
      return true;
    },
    async close() {
      if (closePromise) return closePromise;
      closeState.closed = true;
      if (!readinessSettled) settleReady(Error('Local WebRTC endpoint closed before channel attachment'));
      closePromise = (async () => {
        if (endpoint) await endpoint.close();
      })();
      return closePromise;
    },
  };
}

export function createRoomTransport({url, roomId, role, timeoutMs, createEndpoint = createDataChannelEndpoint,
  createPeerConnection = options => new RTCPeerConnection(options)} = {}) {
  const pc = createPeerConnection({iceServers: []});
  const channelState = {value: null, attach: null, closed: false, cancelPendingStart: null};
  const signalState = {signaler: null, channel: null};
  const pendingTransportWaits = new Set();
  let closed = false, cleanupSnapshot = null, channelFailure = null;
  const rememberChannel = (channel, source) => {
    if (closed) { try { channel.close(); } catch {} return; }
    if (channelState.value) {
      if (channel !== channelState.value) { try { channel.close(); } catch {} }
      throw Error('Local WebRTC data channel was announced more than once');
    }
    channelState.value = channel;
    signalState.channel = {source, ready_state: channel.readyState, ordered: channel.ordered,
      max_retransmits: channel.maxRetransmits, max_packet_lifetime: channel.maxPacketLifeTime};
    channelState.attach?.(channel);
  };
  const receivedChannel = event => {
    try { rememberChannel(event.channel, 'datachannel'); }
    catch (error) { channelFailure ??= error; }
  };
  pc.addEventListener('datachannel', receivedChannel);
  if (role === 'alpha') rememberChannel(pc.createDataChannel('a3-native-input', {ordered: true}), 'createDataChannel');

  const waitForIce = async () => {
    if (closed) throw Error('Room WebRTC transport closed during ICE gathering');
    if (pc.iceGatheringState === 'complete') return;
    await new Promise((resolve, reject) => {
      let timer;
      const cleanup = () => {
        clearTimeout(timer);
        pc.removeEventListener('icegatheringstatechange', changed);
        pendingTransportWaits.delete(cancel);
      };
      const finish = error => { cleanup(); if (error) reject(error); else resolve(); };
      const changed = () => { if (pc.iceGatheringState === 'complete') finish(); };
      const cancel = () => finish(Error('Room WebRTC ICE gathering cancelled by runtime close'));
      pendingTransportWaits.add(cancel);
      timer = setTimeout(() => finish(Error('ice-gathering-timeout')), timeoutMs);
      pc.addEventListener('icegatheringstatechange', changed);
      changed();
    });
  };
  const candidateTypes = description => description?.sdp?.split(/\r?\n/)
    .filter(line => line.startsWith('a=candidate:')).map(line => line.trim().split(/\s+/)[7]) ?? [];
  const checkHostCandidates = description => {
    const types = candidateTypes(description);
    if (!types.length || types.some(type => type !== 'host'))
      throw Error('Room WebRTC signaling requires host-only ICE candidates');
    return types;
  };
  const waitForConnected = async () => {
    await new Promise((resolve, reject) => {
      let timer, channel = null, settled = false;
      const cleanup = () => {
        clearTimeout(timer);
        pc.removeEventListener('connectionstatechange', changed);
        pc.removeEventListener('iceconnectionstatechange', changed);
        pc.removeEventListener('datachannel', changed);
        channel?.removeEventListener('open', changed);
        channel?.removeEventListener('close', changed);
        channel?.removeEventListener('error', failed);
        pendingTransportWaits.delete(failed);
      };
      const finish = error => {
        if (settled) return;
        settled = true; cleanup();
        if (error) reject(error); else resolve();
      };
      const failed = () => finish(Error('Local WebRTC data channel connection failed'));
      pendingTransportWaits.add(failed);
      const changed = () => {
        if (settled) return;
        if (channelFailure) { finish(channelFailure); return; }
        if (!channel && channelState.value) {
          channel = channelState.value;
          channel.addEventListener('open', changed);
          channel.addEventListener('close', changed);
          channel.addEventListener('error', failed);
        }
        if (pc.connectionState === 'connected' &&
            ['connected', 'completed'].includes(pc.iceConnectionState) && channel?.readyState === 'open') {
          finish();
        } else if (closed || pc.connectionState === 'failed' || pc.connectionState === 'closed' ||
            pc.iceConnectionState === 'failed' || pc.iceConnectionState === 'closed' ||
            channel?.readyState === 'closed' || channel?.readyState === 'closing') failed();
      };
      // Signaling may finish before beta's datachannel event. This one existing
      // deadline covers attachment, channel open and PC/ICE connection together.
      timer = setTimeout(() => finish(Error('Local WebRTC data channel connection timed out')), timeoutMs);
      pc.addEventListener('connectionstatechange', changed);
      pc.addEventListener('iceconnectionstatechange', changed);
      pc.addEventListener('datachannel', changed);
      changed();
    });
  };
  const cancelPendingStart = () => {
    const failures = [];
    let cancelled = false;
    try { cancelled = channelState.cancelPendingStart?.() ?? false; } catch (error) { failures.push(error); }
    for (const cancel of [...pendingTransportWaits]) {
      try { cancel(); cancelled = true; } catch (error) { failures.push(error); }
    }
    if (failures.length) throw new AggregateError(failures, 'Room WebRTC startup cancellation failed');
    return cancelled;
  };
  const makeEndpoint = options => {
    const deferredEndpoint = createDeferredChannelEndpoint({channel: {
      get value() { return channelState.value; },
      set attach(value) { channelState.attach = value; },
      onMessage: options.onMessage, onDisconnect: options.onDisconnect,
      onEndpointError: options.onEndpointError,
    }, createEndpoint, role, timeoutMs, closeState: channelState});
    channelState.cancelPendingStart = deferredEndpoint.cancelPendingStart;
    return deferredEndpoint;
  };

  return {
    createEndpoint: makeEndpoint,
    cancelPendingStart,
    async start() {
      if (closed) throw Error('Room WebRTC transport is closed');
      if (signalState.signaler) throw Error('Room WebRTC signaling already started');
      const signaler = createRoomWebRtcSignaler({url, roomId, role, timeoutMs,
        createOffer: async () => {
          const offer = await pc.createOffer();
          await pc.setLocalDescription(offer);
          await waitForIce();
          checkHostCandidates(pc.localDescription);
          return {type: pc.localDescription.type, sdp: pc.localDescription.sdp};
        },
        acceptOffer: async offer => {
          await pc.setRemoteDescription(offer);
          checkHostCandidates(pc.remoteDescription);
          const answer = await pc.createAnswer();
          await pc.setLocalDescription(answer);
          await waitForIce();
          checkHostCandidates(pc.localDescription);
          return {type: pc.localDescription.type, sdp: pc.localDescription.sdp};
        },
        acceptAnswer: async answer => {
          await pc.setRemoteDescription(answer);
          checkHostCandidates(pc.remoteDescription);
        }});
      signalState.signaler = signaler;
      await signaler.negotiated;
      signaler.assertHealthy();
      await waitForConnected();
      return {signaling: signaler.snapshot(), local_webrtc: {role, attach_source: signalState.channel.source,
        attach_error: null, ready_state_at_attach: signalState.channel.ready_state,
        ready_state: channelState.value.readyState, ordered: channelState.value.ordered,
        max_retransmits: channelState.value.maxRetransmits,
        max_packet_lifetime: channelState.value.maxPacketLifeTime,
        connection_state: pc.connectionState, ice_connection_state: pc.iceConnectionState,
        local_candidate_types: candidateTypes(pc.localDescription),
        remote_candidate_types: candidateTypes(pc.remoteDescription)}};
    },
    async close() {
      if (closed) return;
      const failures = [];
      try { cancelPendingStart(); } catch (error) { failures.push(error); }
      closed = true;
      channelState.closed = true;
      pc.removeEventListener('datachannel', receivedChannel);
      try { await signalState.signaler?.close(); } catch (error) { failures.push(error); }
      try { pc.close(); } catch (error) { failures.push(error); }
      cleanupSnapshot = {connection_state: pc.connectionState,
        channel_state: channelState.value?.readyState ?? null,
        signaling: signalState.signaler?.snapshot() ?? null};
      if (failures.length) throw new AggregateError(failures, 'Room WebRTC transport cleanup failed');
      return cleanupSnapshot;
    },
    get cleanupSnapshot() { return cleanupSnapshot; },
    signalingSnapshot() { return signalState.signaler?.snapshot() ?? null; },
  };
}

/** One runtime-owned native adapter, browser peer, and frame-driven progress subscription. */
export function createRuntimeLockstepSession({Module, role, sourceTicks, inputTicks, url, roomId,
  timeoutMs = 5000, createAdapter = createNetLockstepNativeAdapter,
  createPeer = createBrowserNativePeer, createTransport = createRoomTransport} = {}) {
  if (!Module || typeof Module !== 'object') throw Error('Runtime lockstep session requires the loaded native Module');
  if (role !== 'alpha' && role !== 'beta') throw Error('Runtime lockstep role must be alpha or beta');
  if (!Number.isSafeInteger(sourceTicks) || sourceTicks < LOCKSTEP_DELAY ||
      !Number.isSafeInteger(inputTicks) || inputTicks < 0 || inputTicks + LOCKSTEP_DELAY !== sourceTicks)
    throw Error('Runtime lockstep source and input tick bounds are incompatible');
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0) throw Error('Runtime lockstep timeout must be positive');
  if (typeof url !== 'string' || !/^wss?:\/\//.test(url) || typeof roomId !== 'string' || !ROOM_ID_PATTERN.test(roomId))
    throw Error('Runtime lockstep Room URL or room ID is invalid');

  const adapter = createAdapter(Module);
  const progressListeners = new Set();
  const checksumRecords = [];
  let armed = false, closing = false, closeMode = null, closeOperation = null;
  let agreementForStart = null, transport = null, transportInfo = null, transportCleanup = null,
    peer = null, peerReady = null, failure = null;
  let readyResolve, readyReject;
  const ready = new Promise((resolve, reject) => { readyResolve = resolve; readyReject = reject; });
  ready.catch(() => {});
  const remember = error => { failure ??= error; return error; };
  const failStart = error => {
    const cause = remember(error);
    readyReject(cause);
    return cause;
  };
  const native = Object.freeze({
    pushIndexed: (...args) => adapter.pushIndexed(...args),
    configureLocalInputCapture: (...args) => adapter.configureLocalInputCapture(...args),
    confirmStart: (...args) => adapter.confirmStart(...args),
    terminate: (...args) => adapter.terminate(...args),
    status: (...args) => adapter.status(...args),
    subscribeProgress(callback) {
      if (closing) throw Error('Runtime lockstep progress owner is closing');
      if (typeof callback !== 'function') throw Error('Runtime lockstep progress callback must be a function');
      if (progressListeners.has(callback)) throw Error('Runtime lockstep progress callback already has an owner');
      progressListeners.add(callback);
      let active = true;
      return () => { if (!active) return false; active = false; progressListeners.delete(callback); return true; };
    },
    drain(maxRecords) {
      const result = adapter.drain(maxRecords), records = [];
      if (result.bytes.length !== result.count * NET_RECORD_BYTES)
        throw Error('Runtime native checksum byte count differs from its record count');
      for (let offset = 0; offset < result.bytes.length; offset += NET_RECORD_BYTES)
        records.push(Array.from(result.bytes.subarray(offset, offset + NET_RECORD_BYTES)));
      return records;
    },
  });

  async function createPeerForStart(start) {
    if (peer || closing) return;
    if (start?.recorded !== 1 || start?.required !== 1 || start?.capture_failed)
      throw Error('Runtime lockstep native start identity is not ready');
    const agreement = agreementForStart(start);
    if (!agreement || typeof agreement !== 'object' || Array.isArray(agreement))
      throw Error('Runtime lockstep agreement builder returned an invalid value');
    transport = createTransport({url, roomId, role, timeoutMs, agreement, sourceTicks, inputTicks});
    peer = createPeer({role, sourceTicks, inputTicks, timeoutMs, agreement, native,
      inputCapture: {mode: 'live'}, autonomousPump: true,
      checksumConsumer(records) {
        if (!Array.isArray(records) || checksumRecords.length + records.length > BROWSER_CHECKSUM_EXPORT_LIMIT)
          throw Error('Runtime lockstep checksum evidence exceeds its bounded page retention');
        for (const record of records) checksumRecords.push(Object.freeze(Array.from(record)));
        return true;
      }}, {createEndpoint: transport.createEndpoint});
    const transportReady = Promise.resolve().then(() => transport.start());
    const protocolReady = Promise.resolve().then(() => peer.rpc('start'));
    peerReady = Promise.all([transportReady, protocolReady, peer.waitForReady()]).then(([transportSnapshot]) => {
      const snapshot = peer.snapshot();
      transportInfo = transportSnapshot;
      if (snapshot?.protocol?.ready !== true)
        throw Error('Runtime lockstep peer did not confirm the native start identity');
      readyResolve(snapshot);
      return snapshot;
    }).catch(error => { throw failStart(error); });
    peerReady.catch(() => {});
  }

  function onFrame() {
    if (!armed || closing || failure) return;
    try {
      if (!peer) {
        const status = adapter.status();
        if (status.start?.capture_failed) throw Error('Native lockstep start identity capture failed');
        if (status.start?.recorded === 1 && status.start?.required === 1)
          void createPeerForStart(status.start).catch(error => { failStart(error); });
      }
      for (const callback of [...progressListeners]) callback();
    } catch (error) { failStart(error); }
  }

  function start(buildAgreement) {
    if (armed) return Promise.reject(Error('Runtime lockstep session was already started'));
    if (closing) return Promise.reject(Error('Runtime lockstep session is closing'));
    if (typeof buildAgreement !== 'function') return Promise.reject(Error('Runtime lockstep agreement builder must be a function'));
    armed = true;
    agreementForStart = buildAgreement;
    return ready;
  }

  function close({mode = 'normal'} = {}) {
    if (!['normal', 'fatal'].includes(mode)) return Promise.reject(Error('Runtime lockstep close mode must be normal or fatal'));
    if (closeOperation) {
      if (mode === 'fatal' && closeMode !== 'fatal') {
        closeMode = 'fatal';
        try { peer?.close({mode: 'fatal'}); } catch (error) { remember(error); }
      }
      return closeOperation;
    }
    closing = true;
    closeMode = mode;
    if (!peer) readyReject(Error('Runtime lockstep session closed before native start identity'));
    closeOperation = (async () => {
      const failures = [];
      let peerClose = null;
      if (peer) {
        try { peerClose = peer.close({mode}); } catch (error) { failures.push(error); }
      }
      // Cancel only unresolved data-channel readiness after the peer's
      // synchronous mutation fence, so its queued start RPC can join before
      // native scratch or the Room transport is retired.
      try { transport?.cancelPendingStart?.(); } catch (error) { failures.push(error); }
      if (peerClose) { try { await peerClose; } catch (error) { failures.push(error); } }
      if (transport) { try { transportCleanup = await transport.close(); } catch (error) { failures.push(error); } }
      if (peerReady) { try { await peerReady; } catch (error) { failures.push(error); } }
      try { adapter.dispose(); } catch (error) { failures.push(error); }
      progressListeners.clear();
      if (closeMode === 'fatal' && failure) failures.push(failure);
      if (failures.length) throw new AggregateError([...new Set(failures)], 'Runtime lockstep session cleanup failed');
      return Object.freeze({closed: true, mode: closeMode,
        native_quiescence: peer?.snapshot()?.nativePump?.native_quiescence ?? 'not-run',
        transport_cleanup: transportCleanup});
    })();
    closeOperation.catch(() => {});
    return closeOperation;
  }

  return Object.freeze({start, onFrame, close, snapshot: () => Object.freeze({armed, closing, close_mode: closeMode,
    peer: peer?.snapshot() ?? null,
    transport: transportInfo ? {...transportInfo,
      signaling: transport?.signalingSnapshot?.() ?? transportInfo.signaling} : null,
    transport_cleanup: transportCleanup,
    checksums: Object.freeze(checksumRecords.map(record => Object.freeze(record.slice()))),
    failure: failure ? String(failure?.message || failure) : null})});
}
