export const ROOM_RELAY_MAX_PACKET_BYTES = 1024 * 1024;
export const ROOM_RELAY_MAX_BUFFERED_BYTES = ROOM_RELAY_MAX_PACKET_BYTES;
export const ROOM_RELAY_MAX_PENDING_SEND_BYTES = ROOM_RELAY_MAX_BUFFERED_BYTES;
export const ROOM_RELAY_MAX_PENDING_SEND_MESSAGES = 256;
export const ROOM_RELAY_MAX_INBOUND_PENDING_BYTES = ROOM_RELAY_MAX_PACKET_BYTES;
export const ROOM_RELAY_MAX_INBOUND_PENDING_MESSAGES = 256;
const READY_PACKET = JSON.stringify({relay: 1, event: 'ready'});

export function createRoomId() {
  const cryptoApi = globalThis.crypto;
  if (typeof cryptoApi?.getRandomValues !== 'function')
    throw Error('Secure random room IDs require Web Crypto');
  const bytes = cryptoApi.getRandomValues(new Uint8Array(24));
  const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
  let encoded = '';
  for (let offset = 0; offset < bytes.length; offset += 3) {
    const value = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2];
    encoded += alphabet[(value >>> 18) & 63] + alphabet[(value >>> 12) & 63] +
      alphabet[(value >>> 6) & 63] + alphabet[value & 63];
  }
  return encoded;
}

function roomEndpointUrl(url, roomId) {
  if (typeof url !== 'string' || !url) throw Error('A room relay URL is required');
  if (typeof roomId !== 'string' || !/^[A-Za-z0-9_-]{22,64}$/.test(roomId))
    throw Error('Room relay ID must be a 22..64 character base64url token');
  const base = new URL(url);
  if (base.protocol !== 'ws:' && base.protocol !== 'wss:') throw Error('Room relay URL must use ws or wss');
  if (base.username || base.password || base.search || base.hash)
    throw Error('Room relay URL cannot contain credentials, a query, or a fragment');
  return `${base.origin}/v1/rooms/${roomId}/socket`;
}

function waitForOpen(socket, timeoutMs) {
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      clearTimeout(timeout);
      socket.removeEventListener('open', opened);
      socket.removeEventListener('error', failed);
      socket.removeEventListener('close', closed);
    };
    const finish = callback => value => { cleanup(); callback(value); };
    const opened = finish(resolve);
    const failed = finish(() => reject(Error('Room relay WebSocket connection failed')));
    const closed = finish(() => reject(Error('Room relay WebSocket closed before opening')));
    const timeout = setTimeout(finish(() => reject(Error('Room relay WebSocket open timed out'))), timeoutMs);
    socket.addEventListener('open', opened, {once: true});
    socket.addEventListener('error', failed, {once: true});
    socket.addEventListener('close', closed, {once: true});
  });
}

function waitForReady(socket, timeoutMs) {
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      clearTimeout(timeout);
      socket.removeEventListener('message', message);
      socket.removeEventListener('close', closed);
      socket.removeEventListener('error', failed);
    };
    const finish = callback => value => { cleanup(); callback(value); };
    const message = event => {
      if (typeof event.data === 'string' && event.data === READY_PACKET) finish(resolve)();
    };
    const closed = finish(() => reject(Error('Room relay closed before both peers joined')));
    const failed = finish(() => reject(Error('Room relay failed before both peers joined')));
    const timeout = setTimeout(finish(() => reject(Error('Room relay is still waiting for its second peer'))), timeoutMs);
    socket.addEventListener('message', message);
    socket.addEventListener('close', closed, {once: true});
    socket.addEventListener('error', failed, {once: true});
  });
}

function waitForSocketClose(socket, timeoutMs, role) {
  if (socket.readyState === WebSocket.CLOSED) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      clearTimeout(timeout);
      socket.removeEventListener('close', closed);
    };
    const closed = () => { cleanup(); resolve(); };
    const timeout = setTimeout(() => {
      cleanup();
      reject(Error(`Room relay ${role} WebSocket did not close within ${timeoutMs} ms`));
    }, timeoutMs);
    socket.addEventListener('close', closed, {once: true});
    if (socket.readyState === WebSocket.CLOSED) closed();
  });
}

function waitForBufferedDrain(socket, timeoutMs = 5000) {
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
    return Promise.reject(Error('Room relay flush timeout must be a positive integer'));
  const deadline = Date.now() + timeoutMs;
  return new Promise((resolve, reject) => {
    const poll = () => {
      if (socket.readyState !== WebSocket.OPEN) {
        reject(Error('Room relay transport closed before its send buffer drained'));
        return;
      }
      if (socket.bufferedAmount === 0) {
        resolve();
        return;
      }
      if (Date.now() >= deadline) {
        reject(Error(`Room relay send buffer did not drain within ${timeoutMs} ms`));
        return;
      }
      setTimeout(poll, Math.min(10, deadline - Date.now()));
    };
    poll();
  });
}

function waitForPromise(promise, timeoutMs, description) {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(Error(`${description} did not complete within ${timeoutMs} ms`)), timeoutMs);
    promise.then(value => { clearTimeout(timeout); resolve(value); }, error => {
      clearTimeout(timeout); reject(error);
    });
  });
}

function endpoint(socket, role, {onMessage, onDisconnect, onEndpointError, readyPromise, timeoutMs}) {
  const listeners = new Set();
  let firstListener = null;
  let releaseFirstListener;
  const firstListenerPromise = new Promise(resolve => { releaseFirstListener = resolve; });
  if (typeof onMessage === 'function') {
    firstListener = onMessage;
    listeners.add(onMessage);
    releaseFirstListener(onMessage);
  }
  const errors = [];
  let closed = false;
  let callbackCalled = false;
  let inboundChain = Promise.resolve();
  let errorCallbackChain = Promise.resolve();
  let inboundPendingBytes = 0;
  let inboundPendingMessages = 0;
  let drainedFailureCount = 0;
  const inboundFailures = [];
  let acceptingMessages = true;
  let terminalInboundError = null;
  let pendingSendBytes = 0;
  let pendingSendMessages = 0;
  let outboundChain = Promise.resolve();
  const recordError = error => {
    const row = {role, message: String(error?.stack || error?.message || error)};
    errors.push(row);
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
  const failInboundOnce = (error, closeReason, {closeSocket = true} = {}) => {
    if (terminalInboundError) return;
    terminalInboundError = error;
    acceptingMessages = false;
    if (!firstListener) releaseFirstListener(null);
    report(error);
    if (closeSocket && socket.readyState === WebSocket.OPEN) socket.close(4003, closeReason);
  };
  const messageBytes = data => {
    if (typeof data === 'string') return new TextEncoder().encode(data).byteLength;
    if (data instanceof ArrayBuffer) return data.byteLength;
    if (ArrayBuffer.isView(data)) return data.byteLength;
    if (typeof Blob !== 'undefined' && data instanceof Blob) return data.size;
    return ROOM_RELAY_MAX_INBOUND_PENDING_BYTES + 1;
  };
  socket.addEventListener('message', event => {
    if (!acceptingMessages) return;
    if (typeof event.data === 'string' && event.data === READY_PACKET) return;
    const bytes = messageBytes(event.data);
    if (inboundPendingMessages >= ROOM_RELAY_MAX_INBOUND_PENDING_MESSAGES ||
        inboundPendingBytes + bytes > ROOM_RELAY_MAX_INBOUND_PENDING_BYTES) {
      failInboundOnce(Error('Room relay inbound callback queue exceeded its bound'),
        'inbound callback queue exceeded limit');
      return;
    }
    if (typeof event.data !== 'string') {
      failInboundOnce(Error('Room relay delivered a non-text A2 packet'),
        'non-text A2 packet received');
      return;
    }
    const callbacks = [...listeners];
    const waitsForInitialListener = callbacks.length === 0 && !firstListener;
    enqueue(async () => {
      const initial = waitsForInitialListener ? await firstListenerPromise : null;
      const delivery = callbacks.length ? callbacks : (initial ? [initial] : []);
      if (!delivery.length) return;
      for (const listener of delivery) {
        try { await listener(event.data); }
        catch (error) { report(error); }
      }
    }, {bytes});
  });
  socket.addEventListener('close', event => {
    closed = true;
    acceptingMessages = false;
    if (!firstListener && inboundPendingMessages > 0 && !terminalInboundError)
      failInboundOnce(Error('Room relay closed before a message listener was registered'), '', {closeSocket: false});
    if (!firstListener) releaseFirstListener(null);
    if (callbackCalled) return;
    callbackCalled = true;
    // One close notification has a reserved queue slot, so a full data backlog
    // cannot discard the owned disconnect callback.
    enqueue(() => onDisconnect(role, `${event.code}: ${event.reason || 'room relay closed'}`),
      {account: false});
  });
  socket.addEventListener('error', () => failInboundOnce(
    Error('Room relay WebSocket transport failed'), 'WebSocket transport failed'));

  let closePromise = null;
  const api = {
    role,
    onMessage(listener) {
      if (typeof listener !== 'function') throw Error('Room relay message listener must be a function');
      if (!firstListener) {
        firstListener = listener;
        releaseFirstListener(listener);
      }
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    send(text) {
      if (typeof text !== 'string' || !text.length)
        return Promise.reject(Error('A2 relay packet must be non-empty text no larger than 1 MiB'));
      const packetBytes = new TextEncoder().encode(text).byteLength;
      if (packetBytes > ROOM_RELAY_MAX_PACKET_BYTES)
        return Promise.reject(Error('A2 relay packet must be non-empty text no larger than 1 MiB'));
      if (socket.readyState !== WebSocket.OPEN && socket.readyState !== WebSocket.CONNECTING)
        return Promise.reject(Error('Room relay WebSocket is closed'));
      if (pendingSendMessages >= ROOM_RELAY_MAX_PENDING_SEND_MESSAGES ||
          socket.bufferedAmount + pendingSendBytes + packetBytes > ROOM_RELAY_MAX_PENDING_SEND_BYTES)
        return Promise.reject(Error('Room relay WebSocket queued send bytes would exceed 1 MiB'));
      ++pendingSendMessages;
      pendingSendBytes += packetBytes;
      const operation = outboundChain.then(async () => {
        await readyPromise;
        if (socket.readyState !== WebSocket.OPEN) throw Error('Room relay WebSocket is closed');
        if (socket.bufferedAmount + pendingSendBytes > ROOM_RELAY_MAX_BUFFERED_BYTES)
          throw Error('Room relay WebSocket queued send bytes would exceed 1 MiB');
        try { socket.send(text); }
        catch (error) {
          failInboundOnce(error, 'WebSocket send failed');
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
    // This drains the local WebSocket send queue only; it is not a peer ACK.
    async flush(flushTimeoutMs = 5000) {
      if (!Number.isSafeInteger(flushTimeoutMs) || flushTimeoutMs <= 0)
        throw Error('Room relay flush timeout must be a positive integer');
      await waitForPromise(outboundChain, flushTimeoutMs, 'Room relay pending sends');
      await waitForBufferedDrain(socket, flushTimeoutMs);
    },
    // Waits for callbacks already delivered to this endpoint; it is separate
    // from send-buffer drain and does not imply that the peer acknowledged data.
    async drainInbound(timeoutMs = 5000) {
      if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
        throw Error('Room relay inbound drain timeout must be a positive integer');
      const deadline = Date.now() + timeoutMs;
      const inboundAtCall = inboundChain;
      const pending = (async () => {
        await inboundAtCall;
        await errorCallbackChain;
      })();
      let timeout;
      try {
        await Promise.race([pending, new Promise((_, reject) => {
          timeout = setTimeout(() => reject(Error(`Room relay inbound callbacks did not drain within ${timeoutMs} ms`)),
            Math.max(0, deadline - Date.now()));
        })]);
      } finally { clearTimeout(timeout); }
      const failures = inboundFailures.slice(drainedFailureCount);
      drainedFailureCount = inboundFailures.length;
      if (failures.length)
        throw new AggregateError(failures, `Room relay ${role} inbound callback failed`);
    },
    close() {
      if (closePromise) return closePromise;
      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
        socket.close(1000, 'client closed');
      closePromise = (async () => {
        await waitForSocketClose(socket, timeoutMs, role);
        await api.drainInbound(timeoutMs);
        await outboundChain;
      })();
      return closePromise;
    },
    destroy() {
      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
        socket.close(4001, 'client stopped');
    },
    get closed() { return closed || socket.readyState === WebSocket.CLOSED; },
  };
  return api;
}

function createRoomRelayPeerHandle({url, roomId = createRoomId(), role,
  timeoutMs = 5000, onMessage, onDisconnect = () => {}, onEndpointError = () => {}},
requireInitialReceiver) {
  if (typeof WebSocket !== 'function') throw Error('This runtime does not provide WebSocket');
  if (role !== 'alpha' && role !== 'beta') throw Error('Room relay role must be alpha or beta');
  if (requireInitialReceiver && typeof onMessage !== 'function')
    throw Error('Room relay endpoint requires its initial message receiver');
  if (onMessage !== undefined && typeof onMessage !== 'function')
    throw Error('Room relay initial message receiver must be a function');
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
    throw Error('Room relay timeout must be a positive integer');
  const endpointUrl = roomEndpointUrl(url, roomId);
  const socket = new WebSocket(endpointUrl);
  const readyGate = {};
  readyGate.promise = new Promise((resolve, reject) => {
    readyGate.resolve = resolve;
    readyGate.reject = reject;
  });
  readyGate.promise.catch(() => {});
  const result = endpoint(socket, role, {
    onMessage, onDisconnect, onEndpointError, readyPromise: readyGate.promise, timeoutMs,
  });
  const opened = waitForOpen(socket, timeoutMs);
  const readyPacket = waitForReady(socket, timeoutMs);
  const ready = Promise.all([opened, readyPacket]).then(() => {
    readyGate.resolve();
    return result;
  }, error => {
    readyGate.reject(error);
    try { result.destroy(); }
    catch (closeError) {
      throw new AggregateError([error, closeError], 'Room relay readiness failed and socket close failed');
    }
    throw error;
  });
  ready.catch(() => {});
  result.ready = ready;
  result.roomId = roomId;
  result.transport = transportMetadata();
  return result;
}

/** Creates one bounded room endpoint and installs its first A2 receiver before
 * returning or waiting for room readiness. READY is transport control only. */
export function createRoomRelayPeerEndpoint(options = {}) {
  return createRoomRelayPeerHandle(options, true);
}

function transportMetadata() {
  return {type: 'room-websocket', peer_limit: 2,
    packet_limit_bytes: ROOM_RELAY_MAX_PACKET_BYTES,
    buffered_queue_limit_bytes: ROOM_RELAY_MAX_BUFFERED_BYTES,
    pending_send_queue_limit_bytes: ROOM_RELAY_MAX_PENDING_SEND_BYTES,
    pending_send_queue_limit_messages: ROOM_RELAY_MAX_PENDING_SEND_MESSAGES,
    inbound_callback_queue_limit_bytes: ROOM_RELAY_MAX_INBOUND_PENDING_BYTES,
    inbound_callback_queue_limit_messages: ROOM_RELAY_MAX_INBOUND_PENDING_MESSAGES};
}

/** Compatibility composition for callers that host both room roles together.
 * It opens two one-peer handles; early packets remain in the same bounded
 * serialized receive lane until each caller registers its message listener. */
export async function openRoomRelayPeerPair({url, roomId = createRoomId(), timeoutMs = 5000,
  onDisconnect = () => {}, onEndpointError = () => {}} = {}) {
  if (typeof WebSocket !== 'function') throw Error('This runtime does not provide WebSocket');
  if (!Number.isSafeInteger(timeoutMs) || timeoutMs <= 0)
    throw Error('Room relay timeout must be a positive integer');
  let intentionalClose = false;
  let closePromise = null;
  const disconnect = (role, reason) => intentionalClose ? undefined : onDisconnect(role, reason);
  const endpoints = [];
  const roles = ['alpha', 'beta'];
  try {
    for (const role of roles)
      endpoints.push(createRoomRelayPeerHandle({url, roomId, role, timeoutMs,
        onDisconnect: disconnect, onEndpointError,
      }, false));
    await Promise.all(endpoints.map(peer => peer.ready));
  } catch (error) {
    intentionalClose = true;
    for (const peer of endpoints) peer.destroy();
    const cleanupResults = await Promise.allSettled(endpoints.map(peer => peer.close()));
    const cleanupError = cleanupResults.find(result => result.status === 'rejected');
    if (cleanupError) throw new AggregateError([error, cleanupError.reason],
      `Room relay setup failed (${String(error?.message || error)}) and endpoint cleanup also failed`);
    throw error;
  }

  return {
    alpha: endpoints[0], beta: endpoints[1],
    transport: transportMetadata(),
    close() {
      if (closePromise) return closePromise;
      closePromise = (async () => {
        if (endpoints.every(peer => peer.closed)) {
          const drains = await Promise.allSettled(endpoints.map(peer => peer.drainInbound(timeoutMs)));
          const failures = drains.filter(result => result.status === 'rejected').map(result => result.reason);
          if (failures.length) throw new AggregateError(failures, 'Room relay pair did not close cleanly');
          return;
        }
        intentionalClose = true;
        const first = await Promise.allSettled([endpoints[0].close()]);
        const second = await Promise.allSettled([endpoints[1].closed
          ? endpoints[1].drainInbound(timeoutMs) : endpoints[1].close()]);
        const failures = [...first, ...second]
          .filter(result => result.status === 'rejected').map(result => result.reason);
        if (failures.length) throw new AggregateError(failures, 'Room relay pair did not close cleanly');
      })();
      return closePromise;
    },
  };
}
