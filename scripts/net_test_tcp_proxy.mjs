import {createHash} from 'node:crypto';
import net from 'node:net';

const DEFAULT_MAX_CONNECTIONS = 32;
const DEFAULT_MAX_PENDING_BYTES = 1024 * 1024;
const MIN_MAX_PENDING_BYTES = 16 * 1024;
const MAX_MAX_PENDING_BYTES = 64 * 1024 * 1024;
const MAX_CONNECTIONS = 256;
const MAX_PROFILE_DELAY_MS = 60_000;
const MAX_BANDWIDTH_BYTES_PER_SECOND = 1_000_000_000;
const MAX_READ_CHUNK_BYTES = 16 * 1024;
const DEFAULT_EVENT_LIMIT = 4096;
const MAX_EVENT_LIMIT = 65_536;
const LOOPBACK_HOSTS = new Set(['127.0.0.1', '::1']);
const DIRECTION_NAMES = ['clientToWorker', 'workerToClient'];
const DIRECTION_KEYS = new Set([
  'seed', 'delayMs', 'jitterMs', 'bytesPerSecond', 'stallAfterBytes',
  'stallDurationMs', 'cutAfterBytes',
]);

const nowNs = () => process.hrtime.bigint();
const nsMs = value => Number(value) / 1_000_000;
const laterNs = (left, right) => left > right ? left : right;

function requireInteger(value, label, minimum, maximum) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum)
    throw new TypeError(`${label} must be an integer from ${minimum} through ${maximum}`);
  return value;
}

function requirePlainObject(value, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value) ||
      (Object.getPrototypeOf(value) !== Object.prototype && Object.getPrototypeOf(value) !== null))
    throw new TypeError(`${label} must be a plain object`);
  return value;
}

function rejectUnknownKeys(value, allowed, label) {
  const unknown = Object.keys(value).filter(key => !allowed.has(key));
  if (unknown.length) throw new TypeError(`${label} has unknown field(s): ${unknown.join(', ')}`);
}

function normalizeDirection(value, name) {
  const input = requirePlainObject(value, `profile.${name}`);
  rejectUnknownKeys(input, DIRECTION_KEYS, `profile.${name}`);
  if (!Object.hasOwn(input, 'seed')) throw new TypeError(`profile.${name}.seed is required`);
  const seed = requireInteger(input.seed, `profile.${name}.seed`, 1, 0xffffffff);
  const delayMs = requireInteger(input.delayMs ?? 0, `profile.${name}.delayMs`, 0,
    MAX_PROFILE_DELAY_MS);
  const jitterMs = requireInteger(input.jitterMs ?? 0, `profile.${name}.jitterMs`, 0,
    MAX_PROFILE_DELAY_MS);
  const bytesPerSecond = input.bytesPerSecond === undefined ? null :
    requireInteger(input.bytesPerSecond, `profile.${name}.bytesPerSecond`, 1,
      MAX_BANDWIDTH_BYTES_PER_SECOND);
  const stallAfterBytes = input.stallAfterBytes === undefined ? null :
    requireInteger(input.stallAfterBytes, `profile.${name}.stallAfterBytes`, 0,
      Number.MAX_SAFE_INTEGER);
  const stallDurationMs = requireInteger(input.stallDurationMs ?? 0,
    `profile.${name}.stallDurationMs`, 0, MAX_PROFILE_DELAY_MS);
  if ((stallAfterBytes === null) !== (stallDurationMs === 0))
    throw new TypeError(`profile.${name} must pair stallAfterBytes with a positive stallDurationMs`);
  const cutAfterBytes = input.cutAfterBytes === undefined ? null :
    requireInteger(input.cutAfterBytes, `profile.${name}.cutAfterBytes`, 1,
      Number.MAX_SAFE_INTEGER);
  return {seed, delayMs, jitterMs, bytesPerSecond, stallAfterBytes,
    stallDurationMs, cutAfterBytes};
}

function normalizeProfile(profile) {
  if (profile === undefined) {
    return {
      clientToWorker: {seed: 1, delayMs: 0, jitterMs: 0, bytesPerSecond: null,
        stallAfterBytes: null, stallDurationMs: 0, cutAfterBytes: null},
      workerToClient: {seed: 2, delayMs: 0, jitterMs: 0, bytesPerSecond: null,
        stallAfterBytes: null, stallDurationMs: 0, cutAfterBytes: null},
    };
  }
  const input = requirePlainObject(profile, 'profile');
  rejectUnknownKeys(input, new Set(DIRECTION_NAMES), 'profile');
  for (const name of DIRECTION_NAMES) {
    if (!Object.hasOwn(input, name)) throw new TypeError(`profile.${name} is required`);
  }
  return Object.fromEntries(DIRECTION_NAMES.map(name =>
    [name, normalizeDirection(input[name], name)]));
}

function xorshift32(seed, connectionId) {
  let state = (seed ^ Math.imul(connectionId, 0x9e3779b9)) >>> 0;
  if (state === 0) state = seed;
  return () => {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    state >>>= 0;
    return state / 0x1_0000_0000;
  };
}

function directionCounters() {
  return {
    chunksEnqueued: 0,
    chunksReleased: 0,
    bytesEnqueued: 0,
    bytesReleased: 0,
    bytesCutAtThreshold: 0,
    bytesDroppedOnCut: 0,
    bytesDroppedOnStop: 0,
    bytesDroppedOnFailure: 0,
    maxPendingBytes: 0,
    queuePressureEpisodes: 0,
    jitterSamples: 0,
    sampledJitterMsTotal: 0,
    sampledJitterMsMax: 0,
    stallEvents: 0,
    stallDurationMsTotal: 0,
    serializationDelayMsTotal: 0,
    enqueueToWriteCallbackMsTotal: 0,
    enqueueToWriteCallbackMsMax: 0,
    writeBackpressureEvents: 0,
  };
}

// Closed connections fold into bounded cumulative counters. Active connections
// keep independent counters so thresholds and integrity receipts are per socket.
const MAXIMUM_COUNTERS = new Set([
  'maxPendingBytes', 'sampledJitterMsMax', 'enqueueToWriteCallbackMsMax',
]);
function mergeCounters(total, metrics) {
  for (const key of Object.keys(total))
    total[key] = MAXIMUM_COUNTERS.has(key) ? Math.max(total[key], metrics[key]) :
      total[key] + metrics[key];
  return total;
}

function hashHex(hash) {
  return hash.copy().digest('hex');
}

/**
 * Start a byte-transparent, loopback TCP proxy for a local Worker endpoint.
 * The returned endpoint accepts ordinary HTTP and WebSocket TCP traffic; the
 * proxy never parses or rewrites application bytes.
 *
 * The default profile adds no impairment. A supplied profile must contain both
 * direction objects and a nonzero 32-bit seed for each direction. Delay and
 * seeded jitter are per client-to-Worker or Worker-to-client leg, not RTT.
 */
export async function startTcpProxy({targetHost = '127.0.0.1', targetPort,
  listenHost = '127.0.0.1', listenPort = 0, profile, maxConnections = DEFAULT_MAX_CONNECTIONS,
  maxPendingBytesPerDirection = DEFAULT_MAX_PENDING_BYTES, eventLimit = DEFAULT_EVENT_LIMIT} = {}) {
  for (const [label, host] of [['targetHost', targetHost], ['listenHost', listenHost]]) {
    if (typeof host !== 'string' || !LOOPBACK_HOSTS.has(host))
      throw new TypeError(`${label} must be the literal loopback address 127.0.0.1 or ::1`);
  }
  requireInteger(targetPort, 'targetPort', 1, 65535);
  requireInteger(listenPort, 'listenPort', 0, 65535);
  requireInteger(maxConnections, 'maxConnections', 1, MAX_CONNECTIONS);
  requireInteger(maxPendingBytesPerDirection, 'maxPendingBytesPerDirection',
    MIN_MAX_PENDING_BYTES, MAX_MAX_PENDING_BYTES);
  requireInteger(eventLimit, 'eventLimit', 0, MAX_EVENT_LIMIT);
  const normalizedProfile = normalizeProfile(profile);

  const directions = Object.fromEntries(DIRECTION_NAMES.map(name => [name, directionCounters()]));
  const state = {
    startedNs: nowNs(),
    stoppedNs: null,
    stopping: false,
    listenerClosed: false,
    activeConnections: 0,
    acceptedConnections: 0,
    rejectedConnections: 0,
    closedConnections: 0,
    cleanConnections: 0,
    failedConnections: 0,
    cutConnections: 0,
    stopClosedConnections: 0,
    socketErrors: 0,
    timers: 0,
    integrityFailure: false,
    eventLimit,
    events: [],
    eventsOmitted: 0,
    connectionResults: [],
    connectionResultsOmitted: 0,
    directions,
  };
  const pairs = new Map();
  let nextConnectionId = 0;
  let stopPromise = null;
  let address = null;
  let serverClosePromise = null;

  const record = (event, details = {}) => {
    if (state.events.length >= state.eventLimit) {
      state.eventsOmitted++;
      return;
    }
    state.events.push({event, monotonicNs: nowNs().toString(), ...details});
  };

  const noteFailure = pair => {
    if (pair.failureCounted) return;
    pair.failureCounted = true;
    state.integrityFailure = true;
    state.failedConnections++;
    if (pair.closeReason === 'cut') state.cutConnections++;
    if (pair.closeReason === 'proxy-stop') state.stopClosedConnections++;
  };

  const clearTimer = direction => {
    if (!direction.timer) return;
    clearTimeout(direction.timer);
    direction.timer = null;
    state.timers--;
  };

  const noteDroppedBytes = (direction, amount, reason) => {
    if (!amount) return;
    const key = reason === 'cut' ? 'bytesDroppedOnCut' :
      reason === 'proxy-stop' ? 'bytesDroppedOnStop' : 'bytesDroppedOnFailure';
    direction.metrics[key] += amount;
  };

  const discardQueued = (direction, reason) => {
    clearTimer(direction);
    let dropped = 0;
    for (const item of direction.queue) dropped += item.buffer.length;
    direction.queue = [];
    direction.pendingBytes -= dropped;
    noteDroppedBytes(direction, dropped, reason);
    if (dropped) record('queued-bytes-dropped', {connectionId: direction.pair.id,
      direction: direction.name, reason, bytes: dropped});
  };

  const closePair = (pair, reason, error = null) => {
    if (pair.closeReason) return;
    pair.closeReason = reason;
    pair.closeError = error ? String(error.stack || error) : null;
    noteFailure(pair);
    record('connection-closing', {connectionId: pair.id, reason,
      error: pair.closeError});
    for (const direction of Object.values(pair.directions)) {
      discardQueued(direction, reason);
      const buffered = direction.source.readableLength || 0;
      if (buffered) {
        noteDroppedBytes(direction, buffered, reason);
        record('socket-buffer-dropped', {connectionId: pair.id,
          direction: direction.name, reason, bytes: buffered});
      }
    }
    pair.client.destroy();
    pair.worker.destroy();
  };

  const finishPair = pair => {
    if (pair.finalized || pair.closeEvents < 2) return;
    pair.finalized = true;
    for (const direction of Object.values(pair.directions)) {
      clearTimer(direction);
      discardQueued(direction, pair.closeReason || 'socket-close');
      if (direction.inFlight && !direction.inFlight.settled) {
        const item = direction.inFlight;
        item.settled = true;
        direction.pendingBytes -= item.buffer.length;
        noteDroppedBytes(direction, item.buffer.length, pair.closeReason || 'socket-close');
        direction.inFlight = null;
      }
      if (direction.pendingBytes < 0) direction.pendingBytes = 0;
    }
    const hashesMatch = Object.values(pair.directions).every(direction =>
      direction.metrics.bytesEnqueued === direction.metrics.bytesReleased &&
      hashHex(direction.enqueuedHash) === hashHex(direction.releasedHash));
    const gracefullyClosed = !pair.closeReason && pair.bothDirectionsEnded && hashesMatch &&
      Object.values(pair.directions).every(direction => direction.pendingBytes === 0);
    if (gracefullyClosed) state.cleanConnections++;
    else if (!pair.failureCounted) {
      pair.closeReason = pair.closeReason || 'unexpected-close';
      noteFailure(pair);
    }
    state.closedConnections++;
    state.activeConnections--;
    const connectionResult = {connectionId: pair.id, clean: gracefullyClosed,
      reason: pair.closeReason, hashesMatch,
      directions: Object.fromEntries(Object.entries(pair.directions).map(([name, direction]) =>
        [name, {bytesEnqueued: direction.metrics.bytesEnqueued,
          bytesReleased: direction.metrics.bytesReleased,
          bytesDroppedOnCut: direction.metrics.bytesDroppedOnCut,
          bytesDroppedOnStop: direction.metrics.bytesDroppedOnStop,
          bytesDroppedOnFailure: direction.metrics.bytesDroppedOnFailure,
          enqueuedSha256: hashHex(direction.enqueuedHash),
          releasedSha256: hashHex(direction.releasedHash)}]))};
    if (state.connectionResults.length < state.eventLimit)
      state.connectionResults.push(connectionResult);
    else
      state.connectionResultsOmitted++;
    record('connection-closed', connectionResult);
    for (const name of DIRECTION_NAMES)
      mergeCounters(directions[name], pair.directions[name].metrics);
    pairs.delete(pair.id);
    pair.resolveClose();
  };

  const finishWrite = (direction, item, error = null) => {
    if (item.settled) return;
    if (error) {
      item.settled = true;
      if (direction.inFlight === item) direction.inFlight = null;
      direction.pendingBytes -= item.buffer.length;
      noteDroppedBytes(direction, item.buffer.length, direction.pair.closeReason || 'socket-error');
      if (direction.pendingBytes < 0) direction.pendingBytes = 0;
      closePair(direction.pair, direction.pair.closeReason || 'socket-error', error);
      return;
    }
    item.callbackDone = true;
    if (!item.needsDrain || item.drainDone) completeWrite(direction, item);
  };

  const completeWrite = (direction, item) => {
    if (item.settled) return;
    item.settled = true;
    if (direction.inFlight === item) direction.inFlight = null;
    direction.pendingBytes -= item.buffer.length;
    if (direction.pendingBytes < 0) direction.pendingBytes = 0;
    const releasedNs = nowNs();
    const elapsedMs = nsMs(releasedNs - item.enqueuedNs);
    direction.metrics.bytesReleased += item.buffer.length;
    direction.metrics.chunksReleased++;
    direction.metrics.enqueueToWriteCallbackMsTotal += elapsedMs;
    direction.metrics.enqueueToWriteCallbackMsMax = Math.max(
      direction.metrics.enqueueToWriteCallbackMsMax, elapsedMs);
    direction.releasedHash.update(item.buffer);
    record('release', {connectionId: direction.pair.id, direction: direction.name,
      sequence: item.sequence, bytes: item.buffer.length,
      enqueuedNs: item.enqueuedNs.toString(), scheduledReleaseNs: item.scheduledNs.toString(),
      writeCallbackNs: releasedNs.toString(), sampledDelayMs: item.delayMs,
      jitterMs: item.jitterMs, stallMs: item.stallMs,
      serializationDelayMs: item.serializationMs, enqueueToWriteCallbackMs: elapsedMs});
    if (direction.profile.cutAfterBytes !== null &&
        direction.metrics.bytesReleased >= direction.profile.cutAfterBytes) {
      closePair(direction.pair, 'cut');
      return;
    }
    if (direction.queuePressure && direction.pendingBytes <=
        Math.floor(maxPendingBytesPerDirection / 2)) {
      direction.queuePressure = false;
      record('backpressure-resume', {connectionId: direction.pair.id,
        direction: direction.name, pendingBytes: direction.pendingBytes});
    }
    schedule(direction);
    pump(direction);
    maybeHalfClose(direction);
  };

  const schedule = direction => {
    if (direction.timer || direction.inFlight || !direction.queue.length ||
        direction.pair.closeReason || !direction.pair.connected) return;
    const item = direction.queue[0];
    const remainingNs = item.scheduledNs - nowNs();
    const waitMs = Math.max(0, nsMs(remainingNs));
    state.timers++;
    direction.timer = setTimeout(() => {
      direction.timer = null;
      state.timers--;
      if (direction.pair.closeReason || direction.inFlight || !direction.queue.length) return;
      const next = direction.queue.shift();
      direction.inFlight = next;
      next.drainDone = false;
      next.callbackDone = false;
      try {
        const accepted = direction.destination.write(next.buffer, error =>
          finishWrite(direction, next, error));
        next.needsDrain = !accepted;
        if (next.needsDrain) {
          direction.metrics.writeBackpressureEvents++;
          record('destination-backpressure', {connectionId: direction.pair.id,
            direction: direction.name, sequence: next.sequence,
            writableLength: direction.destination.writableLength});
        } else {
          next.drainDone = true;
        }
        if (next.callbackDone && (!next.needsDrain || next.drainDone))
          completeWrite(direction, next);
      } catch (error) {
        finishWrite(direction, next, error);
      }
    }, Math.min(Math.ceil(waitMs), 2_147_483_647));
  };

  const maybeHalfClose = direction => {
    if (!direction.sourceEnded || direction.destHalfClosed || direction.queue.length ||
        direction.inFlight || !direction.pair.connected || direction.pair.closeReason) return;
    direction.destHalfClosed = true;
    record('half-close-forwarded', {connectionId: direction.pair.id,
      direction: direction.name});
    direction.destination.end();
    direction.pair.bothDirectionsEnded = Object.values(direction.pair.directions)
      .every(row => row.sourceEnded && row.destHalfClosed);
  };

  const enqueue = (direction, raw) => {
    if (direction.pair.closeReason || !raw.length) return;
    const before = direction.metrics.bytesEnqueued;
    const cutLimit = direction.profile.cutAfterBytes;
    const remainingBeforeCut = cutLimit === null ? raw.length :
      Math.max(0, cutLimit - before);
    const keptBytes = Math.min(raw.length, remainingBeforeCut);
    if (keptBytes < raw.length) {
      const dropped = raw.length - keptBytes;
      direction.metrics.bytesCutAtThreshold += dropped;
      direction.pendingCutBytes += dropped;
      record('cut-threshold-trim', {connectionId: direction.pair.id,
        direction: direction.name, bytes: dropped,
        cutAfterBytes: cutLimit});
    }
    if (!keptBytes) {
      direction.cutArmed = true;
      return;
    }
    const buffer = raw.subarray(0, keptBytes);
    const enqueuedNs = nowNs();
    const jitterMs = direction.profile.jitterMs ?
      Math.floor(direction.random() * (direction.profile.jitterMs + 1)) : 0;
    const beforeThreshold = direction.metrics.bytesEnqueued;
    const afterThreshold = beforeThreshold + buffer.length;
    let stallMs = 0;
    const stallAt = direction.profile.stallAfterBytes;
    if (!direction.stallTriggered && stallAt !== null &&
        ((stallAt === 0 && beforeThreshold === 0) || afterThreshold >= stallAt)) {
      direction.stallTriggered = true;
      stallMs = direction.profile.stallDurationMs;
      direction.metrics.stallEvents++;
      direction.metrics.stallDurationMsTotal += stallMs;
      record('stall-scheduled', {connectionId: direction.pair.id,
        direction: direction.name, afterBytes: stallAt, durationMs: stallMs});
    }
    const delayMs = direction.profile.delayMs;
    const serializationMs = direction.profile.bytesPerSecond === null ? 0 :
      Math.ceil(buffer.length * 1000 / direction.profile.bytesPerSecond);
    const eligibleNs = enqueuedNs + BigInt(Math.ceil((delayMs + jitterMs + stallMs) * 1_000_000));
    const startNs = laterNs(eligibleNs, direction.nextReleaseNs);
    const scheduledNs = startNs + BigInt(serializationMs) * 1_000_000n;
    direction.nextReleaseNs = scheduledNs;
    direction.metrics.bytesEnqueued += buffer.length;
    direction.metrics.chunksEnqueued++;
    direction.metrics.jitterSamples += direction.profile.jitterMs ? 1 : 0;
    direction.metrics.sampledJitterMsTotal += jitterMs;
    direction.metrics.sampledJitterMsMax = Math.max(direction.metrics.sampledJitterMsMax, jitterMs);
    direction.metrics.serializationDelayMsTotal += serializationMs;
    direction.pendingBytes += buffer.length;
    direction.metrics.maxPendingBytes = Math.max(direction.metrics.maxPendingBytes,
      direction.pendingBytes);
    direction.enqueuedHash.update(buffer);
    const item = {buffer, sequence: direction.nextSequence++, enqueuedNs,
      scheduledNs, jitterMs, delayMs, stallMs, serializationMs, settled: false};
    direction.queue.push(item);
    record('enqueue', {connectionId: direction.pair.id, direction: direction.name,
      sequence: item.sequence, bytes: buffer.length,
      enqueuedNs: enqueuedNs.toString(), scheduledReleaseNs: scheduledNs.toString(),
      sampledDelayMs: delayMs + jitterMs + stallMs, baseDelayMs: delayMs,
      jitterMs, stallMs, serializationDelayMs: serializationMs,
      pendingBytes: direction.pendingBytes});
    if (direction.pendingBytes >= maxPendingBytesPerDirection && !direction.queuePressure) {
      direction.queuePressure = true;
      direction.metrics.queuePressureEpisodes++;
      record('backpressure-pause', {connectionId: direction.pair.id,
        direction: direction.name, pendingBytes: direction.pendingBytes});
    }
    if (cutLimit !== null && direction.metrics.bytesEnqueued >= cutLimit)
      direction.cutArmed = true;
    schedule(direction);
  };

  const pump = direction => {
    if (direction.pair.closeReason || direction.sourceEnded || direction.cutArmed) return;
    for (;;) {
      const room = maxPendingBytesPerDirection - direction.pendingBytes;
      if (room <= 0) {
        if (!direction.queuePressure) {
          direction.queuePressure = true;
          direction.metrics.queuePressureEpisodes++;
          record('backpressure-pause', {connectionId: direction.pair.id,
            direction: direction.name, pendingBytes: direction.pendingBytes});
        }
        return;
      }
      const readSize = Math.min(room, MAX_READ_CHUNK_BYTES,
        direction.source.readableLength);
      if (readSize <= 0) {
        // In explicit-read mode, consume the stream's EOF signal after the last
        // buffered byte so Node can emit `end` and we can forward the FIN.
        direction.source.read(0);
        return;
      }
      const chunk = direction.source.read(readSize);
      if (chunk === null) return;
      if (!Buffer.isBuffer(chunk)) {
        closePair(direction.pair, 'socket-error', Error('TCP socket produced a non-Buffer chunk'));
        return;
      }
      if (chunk.length > room) {
        closePair(direction.pair, 'socket-error', Error('TCP source exceeded its pending-byte bound'));
        return;
      }
      enqueue(direction, chunk);
      if (direction.cutArmed) return;
    }
  };

  const onReadableEnd = direction => {
    if (direction.sourceEnded) return;
    direction.sourceEnded = true;
    record('source-half-closed', {connectionId: direction.pair.id,
      direction: direction.name, pendingBytes: direction.pendingBytes});
    maybeHalfClose(direction);
  };

  const makeDirection = (pair, name, source, destination, profile) => {
    const metrics = directionCounters();
    return {
      pair, name, source, destination, profile, metrics,
      random: xorshift32(profile.seed, pair.id),
      queue: [], pendingBytes: 0, pendingCutBytes: 0,
      nextSequence: 0, nextReleaseNs: 0n, timer: null, inFlight: null,
      sourceEnded: false, destHalfClosed: false, queuePressure: false,
      stallTriggered: false, cutArmed: false,
      enqueuedHash: createHash('sha256'), releasedHash: createHash('sha256'),
    };
  };

  const onSocketClose = (pair, socket, hadError) => {
    pair.closeEvents++;
    const gracefulHalfClose = socket === pair.client ?
      pair.directions.clientToWorker.sourceEnded &&
        pair.directions.workerToClient.destHalfClosed :
      pair.directions.workerToClient.sourceEnded &&
        pair.directions.clientToWorker.destHalfClosed;
    record('socket-closed', {connectionId: pair.id,
      endpoint: socket === pair.client ? 'client' : 'worker', hadError, gracefulHalfClose});
    if (hadError && !pair.closeReason)
      closePair(pair, 'socket-error', Error('TCP socket closed with an error'));
    else if (!pair.closeReason && !gracefulHalfClose)
      closePair(pair, 'unexpected-close');
    finishPair(pair);
  };

  const onSocketError = (pair, error) => {
    state.socketErrors++;
    if (!pair.closeReason) closePair(pair, 'socket-error', error);
  };

  const onClient = client => {
    client.setNoDelay(true);
    if (state.stopping || state.activeConnections >= maxConnections) {
      state.rejectedConnections++;
      record('connection-rejected', {reason: state.stopping ? 'proxy-stopping' : 'connection-limit'});
      client.destroy();
      return;
    }
    const id = ++nextConnectionId;
    const worker = new net.Socket({allowHalfOpen: true});
    worker.setNoDelay(true);
    let resolveClose;
    const closePromise = new Promise(resolve => { resolveClose = resolve; });
    const pair = {id, client, worker, connected: false, closeEvents: 0,
      closeReason: null, closeError: null, failureCounted: false,
      bothDirectionsEnded: false, finalized: false, pendingCutBytes: 0,
      closePromise, resolveClose};
    pair.directions = {
      clientToWorker: makeDirection(pair, 'clientToWorker', client, worker,
        normalizedProfile.clientToWorker),
      workerToClient: makeDirection(pair, 'workerToClient', worker, client,
        normalizedProfile.workerToClient),
    };
    pairs.set(id, pair);
    state.activeConnections++;
    state.acceptedConnections++;
    record('connection-opened', {connectionId: id,
      clientAddress: client.remoteAddress || null, clientPort: client.remotePort || null});
    for (const socket of [client, worker]) {
      socket.on('error', error => onSocketError(pair, error));
      socket.on('close', hadError => onSocketClose(pair, socket, hadError));
    }
    const clientToWorker = pair.directions.clientToWorker;
    const workerToClient = pair.directions.workerToClient;
    client.on('readable', () => {
      record('source-readable', {connectionId: id, direction: clientToWorker.name,
        readableBytes: client.readableLength});
      pump(clientToWorker);
    });
    worker.on('readable', () => {
      record('source-readable', {connectionId: id, direction: workerToClient.name,
        readableBytes: worker.readableLength});
      pump(workerToClient);
    });
    client.on('end', () => onReadableEnd(clientToWorker));
    worker.on('end', () => onReadableEnd(workerToClient));
    workerToClient.destination.on('drain', () => {
      const item = workerToClient.inFlight;
      if (!item || !item.needsDrain) return;
      item.drainDone = true;
      if (item.callbackDone) completeWrite(workerToClient, item);
    });
    clientToWorker.destination.on('drain', () => {
      const item = clientToWorker.inFlight;
      if (!item || !item.needsDrain) return;
      item.drainDone = true;
      if (item.callbackDone) completeWrite(clientToWorker, item);
    });
    worker.once('connect', () => {
      pair.connected = true;
      record('target-connected', {connectionId: id, targetHost, targetPort});
      pump(clientToWorker);
      pump(workerToClient);
      schedule(clientToWorker);
      schedule(workerToClient);
      maybeHalfClose(clientToWorker);
      maybeHalfClose(workerToClient);
    });
    worker.connect({host: targetHost, port: targetPort});
  };

  // Installing `readable` handlers in onClient puts each accepted socket into
  // explicit-read mode. Do not also pause on accept: a write sent after the
  // target connects would otherwise sit unread without another readable event.
  const server = net.createServer({allowHalfOpen: true}, onClient);
  const startup = new Promise((resolve, reject) => {
    const onListening = () => {
      server.off('error', onError);
      resolve();
    };
    const onError = error => {
      server.off('listening', onListening);
      reject(error);
    };
    server.once('listening', onListening);
    server.once('error', onError);
  });
  server.listen({host: listenHost, port: listenPort});
  try {
    await startup;
  } catch (error) {
    server.close();
    throw error;
  }
  const bound = server.address();
  address = {host: bound.address, port: bound.port, family: bound.family};
  server.on('error', error => {
    state.socketErrors++;
    state.integrityFailure = true;
    record('listener-error', {error: String(error.stack || error)});
  });
  record('proxy-listening', {address, targetHost, targetPort});

  // Controls can affect only sockets owned by this proxy. Their separate,
  // bounded receipts survive diagnostic event-ring truncation.
  const controlledFaults = [];
  const requireControlledPair = connectionId => {
    requireInteger(connectionId, 'connectionId', 1, Number.MAX_SAFE_INTEGER);
    const pair = pairs.get(connectionId);
    if (state.stopping || !pair || !pair.connected || pair.finalized || pair.closeReason)
      throw Error('Controlled fault requires an active connected owned socket pair');
    if (controlledFaults.length >= maxConnections * 3)
      throw Error('Controlled fault receipt capacity is exhausted');
    return pair;
  };
  const cutConnection = connectionId => {
    const pair = requireControlledPair(connectionId);
    const receipt = Object.freeze({kind: 'cut', connectionId, activatedNs: nowNs().toString(),
      scope: 'one opaque owned TCP connection; no inferred player role'});
    controlledFaults.push(receipt);
    record('controlled-cut', receipt);
    closePair(pair, 'cut');
    return receipt;
  };
  const stallConnection = (connectionId, directionName, durationMs) => {
    if (!DIRECTION_NAMES.includes(directionName)) throw Error('Unknown stall direction');
    requireInteger(durationMs, 'durationMs', 1, MAX_PROFILE_DELAY_MS);
    const pair = requireControlledPair(connectionId);
    const direction = pair.directions[directionName];
    if (direction.controlledStall) throw Error('Only one controlled stall per connection direction');
    if (direction.sourceEnded && !direction.queue.length)
      throw Error('Cannot stall an ended direction without queued bytes');
    const activatedNs = nowNs();
    const delayNs = BigInt(durationMs) * 1_000_000n;
    direction.controlledStall = true;
    clearTimer(direction);
    // Shift queued eligibility together so FIFO/bandwidth spacing is preserved.
    // An already submitted socket write is irrevocable and is not held back.
    const firstScheduledNs = direction.queue[0]?.scheduledNs ?? activatedNs;
    const shiftNs = delayNs + (activatedNs > firstScheduledNs ? activatedNs - firstScheduledNs : 0n);
    for (const item of direction.queue) item.scheduledNs += shiftNs;
    direction.nextReleaseNs = laterNs(direction.nextReleaseNs + shiftNs, activatedNs + delayNs);
    direction.metrics.stallEvents++;
    direction.metrics.stallDurationMsTotal += durationMs;
    const receipt = Object.freeze({kind: 'stall', connectionId, direction: directionName,
      durationMs, activatedNs: activatedNs.toString(),
      nextReleaseNs: direction.nextReleaseNs.toString(), queuedChunks: direction.queue.length,
      queuedBytes: direction.queue.reduce((sum, item) => sum + item.buffer.length, 0),
      inFlightBytesNotStalled: direction.inFlight?.buffer.length ?? 0,
      scope: 'delays queued and subsequent writes in this direction; cannot recall in-flight bytes'});
    controlledFaults.push(receipt);
    record('controlled-stall', receipt);
    schedule(direction);
    return receipt;
  };

  const snapshot = () => {
    const aggregateDirections = Object.fromEntries(DIRECTION_NAMES.map(name => {
      const total = {...directions[name]};
      for (const pair of pairs.values()) mergeCounters(total, pair.directions[name].metrics);
      return [name, total];
    }));
    const allQueuesDrained = Object.values(aggregateDirections).every(row =>
      row.bytesEnqueued === row.bytesReleased && row.bytesDroppedOnCut === 0 &&
      row.bytesDroppedOnStop === 0 && row.bytesDroppedOnFailure === 0);
    const dataPathPass = state.integrityFailure ? false :
      state.activeConnections > 0 ? null : state.cleanConnections > 0 ? allQueuesDrained : null;
    const estimatedPerConnectionBufferedBytes = 2 * maxPendingBytesPerDirection +
      2 * MAX_READ_CHUNK_BYTES + 2 * 64 * 1024;
    return {
      schema: 'melee-web-tcp-test-proxy-snapshot-v1',
      address,
      target: {host: targetHost, port: targetPort},
      profile: normalizedProfile,
      limits: {maxConnections, maxPendingBytesPerDirection,
        readChunkBytes: MAX_READ_CHUNK_BYTES,
        estimatedMaxBufferedPayloadBytes: maxConnections * estimatedPerConnectionBufferedBytes},
      lifecycle: {stopping: state.stopping, listenerClosed: state.listenerClosed,
        activeConnections: state.activeConnections, acceptedConnections: state.acceptedConnections,
        rejectedConnections: state.rejectedConnections, closedConnections: state.closedConnections,
        cleanConnections: state.cleanConnections, failedConnections: state.failedConnections,
        cutConnections: state.cutConnections, stopClosedConnections: state.stopClosedConnections,
        activeTimers: state.timers, cleanShutdown: state.stoppedNs !== null &&
          state.listenerClosed && state.activeConnections === 0 && state.timers === 0},
      dataPathPass,
      controlledFaults: controlledFaults.slice(),
      directions: aggregateDirections,
      connections: {closed: state.connectionResults.slice(),
        closedOmitted: state.connectionResultsOmitted,
        active: [...pairs.values()].map(pair => {
          const pairDirections = Object.fromEntries(
            Object.entries(pair.directions).map(([name, direction]) => [name, {
              bytesEnqueued: direction.metrics.bytesEnqueued,
              bytesReleased: direction.metrics.bytesReleased,
              queuedBytes: direction.queue.reduce((sum, item) => sum + item.buffer.length, 0),
              inFlightBytes: direction.inFlight?.buffer.length ?? 0,
              pendingBytes: direction.pendingBytes,
              enqueuedSha256: hashHex(direction.enqueuedHash),
              releasedSha256: hashHex(direction.releasedHash),
            }]),
          );
          return {connectionId: pair.id, directions: pairDirections};
        })},
      diagnostics: {events: state.events.slice(), eventsOmitted: state.eventsOmitted,
        eventLimit: state.eventLimit, socketErrors: state.socketErrors,
        integrityFailure: state.integrityFailure,
        startedNs: state.startedNs.toString(), stoppedNs: state.stoppedNs?.toString() || null},
    };
  };

  const stop = () => {
    if (stopPromise) return stopPromise;
    stopPromise = (async () => {
      state.stopping = true;
      record('proxy-stop-start');
      const pendingPairs = [...pairs.values()];
      serverClosePromise = new Promise((resolve, reject) => {
        if (!server.listening) {
          state.listenerClosed = true;
          resolve();
          return;
        }
        server.close(error => {
          if (error) { reject(error); return; }
          state.listenerClosed = true;
          record('listener-closed');
          resolve();
        });
      });
      for (const pair of pendingPairs) closePair(pair, 'proxy-stop');
      await Promise.all([serverClosePromise, ...pendingPairs.map(pair => pair.closePromise)]);
      state.stoppedNs = nowNs();
      record('proxy-stop-complete', {activeConnections: state.activeConnections,
        activeTimers: state.timers});
      const result = snapshot();
      if (!result.lifecycle.cleanShutdown)
        throw Error('TCP proxy cleanup did not close its listener, sockets, and timers');
      return result;
    })();
    return stopPromise;
  };

  return {address, snapshot, stop, cutConnection, stallConnection};
}
