export const WEBRTC_MAX_PACKET_BYTES = 1024 * 1024;
export const WEBRTC_MAX_BUFFERED_BYTES = WEBRTC_MAX_PACKET_BYTES;
export const WEBRTC_MAX_PENDING_SEND_MESSAGES = 256;
export const WEBRTC_MAX_INBOUND_PENDING_BYTES = WEBRTC_MAX_PACKET_BYTES;
export const WEBRTC_MAX_INBOUND_PENDING_MESSAGES = 256;

const utf8 = new TextEncoder();

function waitForPromise(promise, timeoutMs, description) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(Error(`${description} did not complete within ${timeoutMs} ms`)), timeoutMs);
    promise.then(value => { clearTimeout(timeout); resolve(value); }, error => {
      clearTimeout(timeout);
      reject(error);
    });
  });
}

function waitForClose(channel, timeoutMs, role) {
  if (channel.readyState === 'closed') return Promise.resolve();
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      clearTimeout(timeout);
      channel.removeEventListener('close', closed);
    };
    const closed = () => { cleanup(); resolve(); };
    const timeout = setTimeout(() => {
      cleanup();
      reject(Error(`WebRTC data channel ${role} did not close within ${timeoutMs} ms`));
    }, timeoutMs);
    channel.addEventListener('close', closed, {once: true});
    if (channel.readyState === 'closed') closed();
  });
}

function waitForBufferedDrain(channel, timeoutMs) {
  if (channel.readyState !== 'open')
    return Promise.reject(Error('WebRTC data channel closed before its send buffer drained'));
  if (channel.bufferedAmount === 0) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      clearTimeout(timeout);
      channel.removeEventListener('bufferedamountlow', drained);
      channel.removeEventListener('close', closed);
      channel.removeEventListener('error', failed);
    };
    const drained = () => {
      cleanup();
      if (channel.readyState !== 'open')
        return reject(Error('WebRTC data channel closed before its send buffer drained'));
      if (channel.bufferedAmount !== 0)
        return reject(Error('WebRTC data channel reported a low buffer before it drained'));
      resolve();
    };
    const closed = () => { cleanup(); reject(Error('WebRTC data channel closed before its send buffer drained')); };
    const failed = () => { cleanup(); reject(Error('WebRTC data channel failed before its send buffer drained')); };
    const timeout = setTimeout(() => {
      cleanup();
      reject(Error(`WebRTC data channel send buffer did not drain within ${timeoutMs} ms`));
    }, timeoutMs);
    channel.addEventListener('bufferedamountlow', drained);
    channel.addEventListener('close', closed, {once: true});
    channel.addEventListener('error', failed, {once: true});
    channel.bufferedAmountLowThreshold = 0;
    if (channel.bufferedAmount === 0) drained();
  });
}

function metadata() {
  return {type: 'webrtc-datachannel', peer_limit: 2, ordered: true, reliable: true,
    packet_limit_bytes: WEBRTC_MAX_PACKET_BYTES,
    buffered_queue_limit_bytes: WEBRTC_MAX_BUFFERED_BYTES,
    pending_send_queue_limit_messages: WEBRTC_MAX_PENDING_SEND_MESSAGES,
    inbound_callback_queue_limit_bytes: WEBRTC_MAX_INBOUND_PENDING_BYTES,
    inbound_callback_queue_limit_messages: WEBRTC_MAX_INBOUND_PENDING_MESSAGES};
}

/**
 * Adapts an already-created RTCDataChannel to the A2 peer endpoint contract.
 * Callers must attach immediately when the channel is created or received;
 * events before attachment are outside this endpoint's observation boundary.
 * The first message receiver is installed synchronously before `ready` can
 * resolve. `flush()` waits for the local SCTP send buffer only; it is not a
 * remote-peer acknowledgement.
 */
export function createDataChannelEndpoint({channel, role, timeoutMs = 5000,
  onMessage, onDisconnect = () => {}, onEndpointError = () => {}} = {}) {
  if (role !== 'alpha' && role !== 'beta') throw Error('WebRTC role must be alpha or beta');
  if (!channel || typeof channel.addEventListener !== 'function' ||
      typeof channel.removeEventListener !== 'function' || typeof channel.send !== 'function' ||
      typeof channel.close !== 'function')
    throw Error('WebRTC data channel must provide the browser RTCDataChannel API');
  if (channel.ordered !== true || channel.maxRetransmits !== null || channel.maxPacketLifeTime !== null)
    throw Error('WebRTC lockstep data channel must be reliable and ordered');
  if (typeof onMessage !== 'function')
    throw Error('WebRTC data channel endpoint requires its first message receiver');
  if (typeof onDisconnect !== 'function' || typeof onEndpointError !== 'function')
    throw Error('WebRTC endpoint callbacks must be functions');
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
    throw Error('WebRTC data channel timeout must be a positive integer');
  if (!['connecting', 'open', 'closing', 'closed'].includes(channel.readyState))
    throw Error('WebRTC data channel has an invalid readyState');

  const listeners = new Set([onMessage]);
  const errors = [];
  const inboundFailures = [];
  let closed = channel.readyState === 'closed';
  let acceptingMessages = !closed;
  let callbackCalled = false;
  let terminalError = null;
  let firstFailureCount = 0;
  let inboundPendingBytes = 0;
  let inboundPendingMessages = 0;
  let pendingSendBytes = 0;
  let pendingSendMessages = 0;
  let inboundChain = Promise.resolve();
  let errorCallbackChain = Promise.resolve();
  let outboundChain = Promise.resolve();
  let closePromise = null;
  let readySettled = false;
  let timeout;
  let api;
  let resolveReady;
  let rejectReady;
  const ready = new Promise((resolve, reject) => {
    resolveReady = resolve;
    rejectReady = reject;
  });
  ready.catch(() => {});

  const recordError = error => {
    errors.push({role, message: String(error?.stack || error?.message || error)});
    inboundFailures.push(error);
  };
  const report = error => {
    recordError(error);
    errorCallbackChain = errorCallbackChain.then(async () => {
      try { await onEndpointError(role, error); }
      catch (callbackError) { recordError(callbackError); }
    });
  };
  const enqueue = (callback, {bytes = 0, account = true} = {}) => {
    if (account) {
      ++inboundPendingMessages;
      inboundPendingBytes += bytes;
    }
    inboundChain = inboundChain.then(async () => {
      try { await callback(); }
      catch (error) { report(error); }
    }).finally(() => {
      if (account) {
        --inboundPendingMessages;
        inboundPendingBytes -= bytes;
      }
    });
  };
  const settleReady = (error = null) => {
    if (readySettled) return;
    readySettled = true;
    clearTimeout(timeout);
    if (error) rejectReady(error);
    else resolveReady(api);
  };
  const failClosed = (error, closeReason = 'WebRTC data channel failed') => {
    if (terminalError) return terminalError;
    terminalError = error;
    acceptingMessages = false;
    settleReady(error);
    report(error);
    if (channel.readyState === 'open' || channel.readyState === 'connecting') {
      try { channel.close(); }
      catch (closeError) { report(closeError); }
    }
    return error;
  };
  const notifyDisconnect = reason => {
    if (callbackCalled) return;
    callbackCalled = true;
    enqueue(() => onDisconnect(role, reason), {account: false});
  };

  const onMessageEvent = event => {
    if (!acceptingMessages) return;
    if (typeof event.data !== 'string') {
      failClosed(Error('WebRTC data channel delivered a non-text A2 packet'),
        'non-text A2 packet received');
      return;
    }
    const bytes = utf8.encode(event.data).byteLength;
    if (inboundPendingMessages >= WEBRTC_MAX_INBOUND_PENDING_MESSAGES ||
        inboundPendingBytes + bytes > WEBRTC_MAX_INBOUND_PENDING_BYTES) {
      failClosed(Error('WebRTC inbound callback queue exceeded its bound'),
        'inbound callback queue exceeded limit');
      return;
    }
    const callbacks = [...listeners];
    enqueue(async () => {
      for (const listener of callbacks) await listener(event.data);
    }, {bytes});
  };
  const onOpen = () => settleReady();
  const onClose = () => {
    closed = true;
    acceptingMessages = false;
    if (!readySettled) settleReady(Error('WebRTC data channel closed before opening'));
    notifyDisconnect('WebRTC data channel closed');
  };
  const onError = event => failClosed(event?.error instanceof Error ? event.error :
    Error('WebRTC data channel transport failed'), 'data channel transport failed');

  api = {
    role,
    ready,
    transport: metadata(),
    onMessage(listener) {
      if (typeof listener !== 'function') throw Error('WebRTC message listener must be a function');
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    send(text) {
      if (typeof text !== 'string' || !text.length)
        return Promise.reject(Error('A2 data-channel packet must be non-empty text no larger than 1 MiB'));
      const packetBytes = utf8.encode(text).byteLength;
      if (packetBytes > WEBRTC_MAX_PACKET_BYTES)
        return Promise.reject(Error('A2 data-channel packet must be non-empty text no larger than 1 MiB'));
      if (!acceptingMessages || (channel.readyState !== 'connecting' && channel.readyState !== 'open'))
        return Promise.reject(Error('WebRTC data channel is closed'));
      if (pendingSendMessages >= WEBRTC_MAX_PENDING_SEND_MESSAGES ||
          channel.bufferedAmount + pendingSendBytes + packetBytes > WEBRTC_MAX_BUFFERED_BYTES) {
        const error = failClosed(Error('WebRTC queued send bytes would exceed 1 MiB'),
          'outbound callback queue exceeded limit');
        return Promise.reject(error);
      }
      ++pendingSendMessages;
      pendingSendBytes += packetBytes;
      const operation = outboundChain.then(async () => {
        await ready;
        if (!acceptingMessages || channel.readyState !== 'open')
          throw Error('WebRTC data channel is closed');
        if (channel.bufferedAmount + pendingSendBytes > WEBRTC_MAX_BUFFERED_BYTES)
          throw Error('WebRTC data channel send buffer exceeded its bound');
        try { channel.send(text); }
        catch (error) {
          failClosed(error, 'data channel send failed');
          throw error;
        }
      });
      const tracked = operation.finally(() => {
        --pendingSendMessages;
        pendingSendBytes -= packetBytes;
      });
      outboundChain = tracked.catch(() => {});
      return tracked;
    },
    get errors() { return [...errors]; },
    async flush(flushTimeoutMs = timeoutMs) {
      if (!Number.isSafeInteger(flushTimeoutMs) || flushTimeoutMs <= 0)
        throw Error('WebRTC flush timeout must be a positive integer');
      await waitForPromise(outboundChain, flushTimeoutMs, 'WebRTC pending sends');
      await waitForBufferedDrain(channel, flushTimeoutMs);
    },
    async drainInbound(drainTimeoutMs = timeoutMs) {
      if (!Number.isSafeInteger(drainTimeoutMs) || drainTimeoutMs <= 0)
        throw Error('WebRTC inbound drain timeout must be a positive integer');
      const inboundAtCall = inboundChain;
      const pending = (async () => {
        await inboundAtCall;
        await errorCallbackChain;
      })();
      await waitForPromise(pending, drainTimeoutMs, 'WebRTC inbound callbacks');
      const failures = inboundFailures.slice(firstFailureCount);
      firstFailureCount = inboundFailures.length;
      if (failures.length)
        throw new AggregateError(failures, `WebRTC ${role} inbound callback failed`);
    },
    close() {
      if (closePromise) return closePromise;
      closePromise = (async () => {
        const failures = [];
        if (channel.readyState === 'open' || channel.readyState === 'connecting') {
          try { channel.close(); }
          catch (error) { failures.push(error); report(error); }
        }
        try { await waitForClose(channel, timeoutMs, role); }
        catch (error) { failures.push(error); }
        try { await api.drainInbound(timeoutMs); }
        catch (error) { failures.push(error); }
        try { await outboundChain; }
        catch (error) { failures.push(error); }
        closed = channel.readyState === 'closed';
        if (failures.length)
          throw new AggregateError(failures, 'WebRTC endpoint did not close cleanly');
      })();
      return closePromise;
    },
    destroy() {
      if (channel.readyState === 'open' || channel.readyState === 'connecting') channel.close();
    },
    get closed() { return closed || channel.readyState === 'closed'; },
  };

  // Install all receive/error/close listeners before observing readyState.
  // A caller supplies onMessage at construction, so a packet at open cannot
  // outrun the first A2 receiver.
  channel.addEventListener('message', onMessageEvent);
  channel.addEventListener('close', onClose);
  channel.addEventListener('error', onError);
  channel.addEventListener('open', onOpen, {once: true});
  timeout = setTimeout(() => failClosed(Error('WebRTC data channel open timed out'),
    'data channel open timed out'), timeoutMs);
  if (channel.readyState === 'open') settleReady();
  else if (channel.readyState === 'closed') onClose();
  else if (channel.readyState === 'closing') settleReady(Error('WebRTC data channel closed before opening'));
  return api;
}
