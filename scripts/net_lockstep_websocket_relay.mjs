import {randomBytes} from 'node:crypto';

export const ROOM_RELAY_MAX_PACKET_BYTES = 1024 * 1024;
export const ROOM_RELAY_MAX_BUFFERED_BYTES = ROOM_RELAY_MAX_PACKET_BYTES;
export const ROOM_RELAY_MAX_INBOUND_PENDING_BYTES = ROOM_RELAY_MAX_PACKET_BYTES;
export const ROOM_RELAY_MAX_INBOUND_PENDING_MESSAGES = 256;
const READY_PACKET = JSON.stringify({relay: 1, event: 'ready'});

export function createRoomId() {
  return randomBytes(24).toString('base64url');
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

function endpoint(socket, role, {onDisconnect, onEndpointError}) {
  const listeners = new Set();
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
  const failInboundOnce = (error, closeReason) => {
    if (terminalInboundError) return;
    terminalInboundError = error;
    acceptingMessages = false;
    report(error);
    if (socket.readyState === WebSocket.OPEN) socket.close(4003, closeReason);
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
    if (callbacks.length) enqueue(async () => {
      for (const listener of callbacks) {
        try { await listener(event.data); }
        catch (error) { report(error); }
      }
    }, {bytes});
  });
  socket.addEventListener('close', event => {
    closed = true;
    acceptingMessages = false;
    if (callbackCalled) return;
    callbackCalled = true;
    // One close notification has a reserved queue slot, so a full data backlog
    // cannot discard the owned disconnect callback.
    enqueue(() => onDisconnect(role, `${event.code}: ${event.reason || 'room relay closed'}`),
      {account: false});
  });
  socket.addEventListener('error', () => failInboundOnce(
    Error('Room relay WebSocket transport failed'), 'WebSocket transport failed'));
  return {
    send(text) {
      if (typeof text !== 'string' || !text.length)
        return Promise.reject(Error('A2 relay packet must be non-empty text no larger than 1 MiB'));
      const packetBytes = new TextEncoder().encode(text).byteLength;
      if (packetBytes > ROOM_RELAY_MAX_PACKET_BYTES)
        return Promise.reject(Error('A2 relay packet must be non-empty text no larger than 1 MiB'));
      if (socket.readyState !== WebSocket.OPEN) return Promise.reject(Error('Room relay WebSocket is closed'));
      if (socket.bufferedAmount + packetBytes > ROOM_RELAY_MAX_BUFFERED_BYTES)
        return Promise.reject(Error('Room relay WebSocket queued send bytes would exceed 1 MiB'));
      try { socket.send(text); return Promise.resolve(); }
      catch (error) { return Promise.reject(error); }
    },
    onMessage(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    get errors() { return [...errors]; },
    // This drains the local WebSocket send queue only; it is not a peer ACK.
    async flush(timeoutMs = 5000) { await waitForBufferedDrain(socket, timeoutMs); },
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
      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
        socket.close(1000, 'client closed');
    },
    destroy() { if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) socket.close(4001, 'client stopped'); },
    get closed() { return closed || socket.readyState === WebSocket.CLOSED; },
  };
}

/** Opens two real WebSocket clients in one room. The relay control packet only
 * gates the clients until both are present; it is never delivered to A2. */
export async function openRoomRelayPeerPair({url, roomId = createRoomId(), timeoutMs = 5000,
  onDisconnect = () => {}, onEndpointError = () => {}} = {}) {
  if (typeof WebSocket !== 'function') throw Error('This Node runtime does not provide WebSocket');
  if (typeof url !== 'string' || !url) throw Error('A room relay URL is required');
  if (typeof roomId !== 'string' || !/^[A-Za-z0-9_-]{22,64}$/.test(roomId))
    throw Error('Room relay ID must be a 22..64 character base64url token');
  const base = new URL(url);
  if (base.protocol !== 'ws:' && base.protocol !== 'wss:') throw Error('Room relay URL must use ws or wss');
  if (base.username || base.password || base.search || base.hash) throw Error('Room relay URL cannot contain credentials, a query, or a fragment');
  const endpointUrl = `${base.origin}/v1/rooms/${roomId}/socket`;
  const sockets = [];
  const readyResults = [];
  const endpoints = [];
  let intentionalClose = false;
  let closePromise = null;
  const disconnect = (role, reason) => intentionalClose ? undefined : onDisconnect(role, reason);
  try {
    for (const role of ['alpha', 'beta']) {
      const socket = new WebSocket(endpointUrl);
      sockets.push(socket);
      const opened = waitForOpen(socket, timeoutMs);
      const readiness = waitForReady(socket, timeoutMs).then(() => null, error => error);
      readyResults.push(readiness);
      await opened;
      endpoints.push(endpoint(socket, role, {
        onDisconnect: disconnect, onEndpointError,
      }));
    }
    const readinessErrors = await Promise.all(readyResults);
    const readinessError = readinessErrors.find(Boolean);
    if (readinessError) throw readinessError;
  } catch (error) {
    intentionalClose = true;
    for (const socket of sockets)
      if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)
        socket.close(4001, 'pair setup failed');
    await Promise.all(readyResults);
    const cleanupResults = await Promise.allSettled(sockets.map((socket, index) =>
      waitForSocketClose(socket, timeoutMs, index === 0 ? 'alpha setup' : 'beta setup')));
    const cleanupError = cleanupResults.find(result => result.status === 'rejected');
    if (cleanupError) throw new AggregateError([error, cleanupError.reason], 'Room relay setup failed and socket cleanup timed out');
    throw error;
  }

  return {
    alpha: endpoints[0], beta: endpoints[1],
    transport: {type: 'room-websocket', peer_limit: 2, packet_limit_bytes: ROOM_RELAY_MAX_PACKET_BYTES,
      buffered_queue_limit_bytes: ROOM_RELAY_MAX_BUFFERED_BYTES,
      inbound_callback_queue_limit_bytes: ROOM_RELAY_MAX_INBOUND_PENDING_BYTES,
      inbound_callback_queue_limit_messages: ROOM_RELAY_MAX_INBOUND_PENDING_MESSAGES},
    async close() {
      if (closePromise) return closePromise;
      intentionalClose = true;
      endpoints[0].close();
      closePromise = (async () => {
        const results = await Promise.allSettled(sockets.map((socket, index) =>
          waitForSocketClose(socket, timeoutMs, index === 0 ? 'alpha' : 'beta')));
        const failures = results.filter(result => result.status === 'rejected').map(result => result.reason);
        const drains = await Promise.allSettled(endpoints.map(endpoint => endpoint.drainInbound(timeoutMs)));
        failures.push(...drains.filter(result => result.status === 'rejected').map(result => result.reason));
        if (failures.length) throw new AggregateError(failures, 'Room relay pair did not close cleanly');
      })();
      return closePromise;
    },
  };
}
