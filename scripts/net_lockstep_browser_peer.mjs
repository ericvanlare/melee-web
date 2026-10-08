/** Page-owned A3 protocol, room endpoint and native checksum boundary.
 * Node may publish recipe inputs and export evidence, but never handles packets
 * or submits native checksums to the protocol in this mode. */
import {LockstepPeer, NET_RECORD_BYTES, TERMINAL} from './net_lockstep_core.mjs';
import {createRoomRelayPeerEndpoint} from './net_lockstep_websocket_relay.mjs';

export const BROWSER_CHECKSUM_EXPORT_LIMIT = 512;

export function createBrowserNativePeer({role, sourceTicks, inputTicks, relayUrl, roomId,
  timeoutMs = 5000, agreement, native, inputCapture = null, autonomousPump = false},
{createEndpoint = createRoomRelayPeerEndpoint} = {}) {
  if (!agreement || typeof agreement !== 'object') throw Error('Browser peer requires its frozen start agreement');
  if (inputCapture !== null && (!inputCapture || typeof inputCapture !== 'object' ||
      !Array.isArray(inputCapture.deferSendTicks) || !Array.isArray(inputCapture.pattern) ||
      inputCapture.pattern.length !== inputTicks))
    throw Error('Browser local input capture requires a bounded pattern and deferred tick list');
  const localPort = role === 'alpha' ? 0 : role === 'beta' ? 1 : -1;
  const deferredTicks = new Set(inputCapture?.deferSendTicks ?? []);
  if ([...deferredTicks].some(tick => !Number.isSafeInteger(tick) || tick < 0 || tick >= inputTicks))
    throw Error('Browser local input capture deferred tick is outside its input bound');
  if (inputCapture && (localPort < 0 || typeof native.configureLocalInputCapture !== 'function' ||
      !native.configureLocalInputCapture(localPort, inputTicks)))
    throw Error('Native local input capture configuration was rejected');
  if (typeof autonomousPump !== 'boolean' || (autonomousPump && typeof native.subscribeProgress !== 'function'))
    throw Error('Autonomous native pump requires a progress subscription');
  let failure = null, intentionalClose = false, closed = false, closing = false;
  let wakeRequested = false, wakeQueued = false, wakeRuns = 0, rpcCalls = 0, unsubscribeProgress = null;
  let closeOperation = null, drainFailure = null;
  let nativeChain = Promise.resolve();
  const callNative = (name, ...args) => {
    if (!autonomousPump) return native[name](...args);
    const operation = nativeChain.then(() => {
      if (closed) throw Error('Native access after autonomous peer close');
      return native[name](...args);
    });
    nativeChain = operation.catch(remember);
    return operation;
  };
  let rpcChain = Promise.resolve(), activeNativeRecords = 0, postTerminalNativeRecords = 0;
  const exports = [], pending = new Set(), capturedLocalInputs = [];
  let localInputQueue = Promise.resolve(), lastCapturePollSerial = null, peer = null;
  const remember = error => { failure ??= error; return error; };
  const check = () => { if (failure) throw failure; };
  const track = operation => {
    const result = Promise.resolve().then(operation);
    const observed = result.catch(remember).finally(() => pending.delete(observed));
    pending.add(observed);
    return result;
  };
  peer = new LockstepPeer({role, sourceTicks, inputTicks,
    pushFrame: async (tick, bytes) => {
      if (!await callNative('pushIndexed', tick, bytes)) throw Error('Native indexed input queue rejected agreed frames');
    },
    onReady: async () => {
      if (!await callNative('confirmStart')) throw Error('Native rejected peer start identity confirmation');
    },
    onTerminal: terminal => callNative('terminate', TERMINAL[terminal.kind] ?? TERMINAL.protocol,
      Number.isInteger(terminal.tick) ? terminal.tick : 0,
      Number.isInteger(terminal.channel) ? terminal.channel : 0),
  });
  const endpoint = createEndpoint({url: relayUrl, roomId, role, timeoutMs,
    onMessage: text => track(() => peer.receive(text)),
    onDisconnect: (_role, reason) => intentionalClose ? Promise.resolve() : track(() => peer.disconnect(reason)),
    onEndpointError: (_role, error) => track(async () => {
      remember(error);
      await peer.fail('protocol', {reason: `transport receive failed: ${String(error?.message || error)}`});
    }),
  });
  peer.attach(text => endpoint.send(text));
  const captureLocalInput = (tick, port, pollSerial, bytes) => {
    const reject = reason => {
      const error = remember(Error(reason));
      if (peer && !peer.terminal)
        void peer.fail('protocol', {reason: error.message}).catch(remember);
      return false;
    };
    if (!inputCapture) return reject('Native local input capture was not enabled for this peer');
    if (!Number.isSafeInteger(tick) || tick < 0 || tick >= inputTicks ||
        tick !== capturedLocalInputs.length || port !== localPort ||
        !Number.isSafeInteger(pollSerial) ||
        (lastCapturePollSerial !== null && pollSerial <= lastCapturePollSerial) ||
        !(bytes instanceof Uint8Array) || bytes.length !== 11)
      return reject('Native local input capture cursor, port, serial or PAD bytes are invalid');
    const expectedState = inputCapture.pattern[tick];
    const fixtureState = globalThis.__meleeSyntheticPadState ?? null;
    if (fixtureState !== expectedState ||
        (inputCapture.pattern[tick + 1] !== undefined &&
         typeof globalThis.__meleeSyntheticPadTransition !== 'function'))
      return reject(`Synthetic Gamepad state differs at source cursor ${tick}`);
    const immutableBytes = new Uint8Array(bytes);
    const row = Object.freeze({source_cursor: tick, input_tick: tick, local_port: port,
      poll_serial: pollSerial, bytes: Object.freeze(Array.from(immutableBytes)),
      synthetic_state: fixtureState});
    capturedLocalInputs.push(row);
    lastCapturePollSerial = pollSerial;
    const deferSend = deferredTicks.has(tick);
    const operation = localInputQueue.then(() => peer.addLocalInput(tick, immutableBytes, {deferSend}));
    localInputQueue = operation.catch(async error => {
      const cause = remember(Error(`Asynchronous browser-local input ${tick} failed: ${String(error?.message || error)}`));
      if (!peer.terminal) {
        try { await peer.fail('protocol', {reason: cause.message, tick}); }
        catch (terminalError) { remember(terminalError); }
      }
    });
    track(() => operation);
    if (inputCapture.pattern[tick + 1] !== undefined) {
      try { globalThis.__meleeSyntheticPadTransition(inputCapture.pattern[tick + 1]); }
      catch (error) {
        return reject(`Synthetic Gamepad transition failed after source cursor ${tick}: ${String(error?.message || error)}`);
      }
    }
    return true;
  };
  if (inputCapture) globalThis.__meleeWebNetLocalInputCapture = captureLocalInput;
  // Start WebCrypto preparation before returning the allocated endpoint. An
  // early remote hello must join that same preparation, never precede it.
  const startup = peer.start(agreement);
  startup.catch(remember);
  endpoint.ready.catch(remember);

  async function settle() {
    await endpoint.drainInbound(timeoutMs);
    await localInputQueue;
    while (pending.size) await Promise.all([...pending]);
    await peer.receiveQueue;
    await peer.pump;
    check();
  }
  async function pump() {
    await settle();
    // One extra record detects capacity exhaustion; do not silently truncate or
    // stop draining and describe the remaining native records as exported.
    const records = await callNative('drain', BROWSER_CHECKSUM_EXPORT_LIMIT - exports.length + 1);
    if (!Array.isArray(records) || records.some(record =>
        (!Array.isArray(record) && !(record instanceof Uint8Array)) || record.length !== NET_RECORD_BYTES ||
        Array.from(record).some(byte => !Number.isInteger(byte) || byte < 0 || byte > 255))) {
      drainFailure = {kind: 'malformed_drain', returned_records: Array.isArray(records) ? records.length : null,
        retained_records: Array.isArray(records) ? records.slice(0, BROWSER_CHECKSUM_EXPORT_LIMIT + 1)
          .map(record => Array.isArray(record) || record instanceof Uint8Array ? Array.from(record).slice(0, NET_RECORD_BYTES + 1) : null) : []};
      throw Error('Native checksum drain returned malformed records');
    }
    if (records.length + exports.length > BROWSER_CHECKSUM_EXPORT_LIMIT) {
      const error = remember(Error('Browser native checksum export queue exceeded its 512-record bound'));
      drainFailure = {kind: 'export_overflow', returned_records: records.length,
        retained_records: records.slice(0, BROWSER_CHECKSUM_EXPORT_LIMIT + 1).map(record => Array.from(record))};
      await peer.fail('protocol', {reason: error.message});
      throw error;
    }
    for (const record of records) {
      const bytes = Uint8Array.from(record);
      if (!peer.terminal) { await peer.addChecksum(bytes); ++activeNativeRecords; }
      else ++postTerminalNativeRecords;
      exports.push(Array.from(bytes));
    }
    if (peer.ready && !peer.terminal) {
      const status = await callNative('status');
      const flushFinal = autonomousPump && status.active === 1 && status.blocker === 'complete' &&
        status.cursor === sourceTicks && status.terminal?.kind === 0;
      await peer.setNativeProgress(status.cursor, {flushFinal});
    }
    await settle();
  }
  const snapshot = () => ({protocol: peer.summary(), endpointErrors: endpoint.errors,
    endpointClosed: endpoint.closed, closed, exportRecords: exports.length,
    localInputCapture: inputCapture ? {enabled: true, input_ticks: inputTicks,
      captures: capturedLocalInputs.slice()} : {enabled: false},
    checksumOwnership: {owner: 'browser-page', export_limit_records: BROWSER_CHECKSUM_EXPORT_LIMIT,
      active_native_records_submitted_before_export: activeNativeRecords,
      post_terminal_native_evidence_records: postTerminalNativeRecords},
    nativePump: {enabled: autonomousPump, closing, wake_requested: wakeRequested,
      wake_queued: wakeQueued, completed_wakeups: wakeRuns, rpc_calls: rpcCalls, drain_failure: drainFailure},
    failure: failure ? String(failure?.stack || failure) : null, transport: endpoint.transport});

  // Native observations and RPCs join one owner; notifications never step source time.
  function serialize(operation) {
    const result = rpcChain.then(operation);
    rpcChain = result.catch(remember);
    return result;
  }
  function requestWakeup(error = null) {
    if (closing || closed) return;
    if (error) remember(error);
    if (failure && !error) return;
    wakeRequested = true;
    if (wakeQueued) return;
    wakeQueued = true;
    const operation = serialize(async () => {
      wakeRequested = false;
      try { check(); await pump(); ++wakeRuns; }
      catch (error) {
        remember(error);
        if (!peer.terminal) {
          try { await peer.fail('protocol', {reason: String(error?.message || error)}); }
          catch (terminalError) { remember(terminalError); }
        }
        throw error;
      } finally {
        wakeQueued = false;
        if (wakeRequested && !closing && !failure) requestWakeup();
      }
    });
    // A background error is sticky evidence, never an unhandled rejection.
    operation.catch(remember);
  }
  if (autonomousPump) {
    try {
      const unsubscribe = native.subscribeProgress(requestWakeup);
      if (typeof unsubscribe !== 'function')
        throw Error('Native progress subscription requires an unsubscribe owner');
      unsubscribeProgress = unsubscribe;
    } catch (error) {
      // Return the failed controller so its caller can still join startup and
      // endpoint cleanup. Setup failure must not orphan an allocated endpoint.
      remember(error);
    }
  }

  // Serialize RPCs so native scratch and evidence exports have one owner.
  function rpc(name, args = []) {
    ++rpcCalls;
    if (autonomousPump && closing && !closed)
      return Promise.reject(Error('Browser native peer is closing'));
    const operation = serialize(async () => {
      check();
      if (closed && !['snapshot', 'drain'].includes(name)) throw Error('Browser native peer is closed');
      if (name === 'start') { await Promise.all([startup, endpoint.ready]); await settle(); }
      else if (name === 'addLocalInput' || name === 'addLocalInputs') {
        if (inputCapture) {
          const error = Error('Recorded local input RPC cannot be mixed with browser-local sampling');
          await peer.fail('protocol', {reason: error.message});
          throw error;
        }
        if (name === 'addLocalInput') await peer.addLocalInput(...args);
        else await peer.addLocalInputs(...args);
      }
      else if (name === 'releaseCapturedInput') {
        if (!inputCapture) throw Error('Browser-local input capture is not enabled');
        const [tick] = args;
        if (!Number.isSafeInteger(tick) || !deferredTicks.has(tick) || tick >= capturedLocalInputs.length)
          throw Error('Requested deferred browser-local input has not been captured');
        const bytes = Uint8Array.from(capturedLocalInputs[tick].bytes);
        await localInputQueue;
        try { await peer.addLocalInput(tick, bytes); }
        catch (error) {
          const cause = remember(Error(`Deferred browser-local input ${tick} release failed: ${String(error?.message || error)}`));
          if (!peer.terminal) await peer.fail('protocol', {reason: cause.message, tick});
          throw cause;
        }
        deferredTicks.delete(tick);
      }
      else if (name === 'setNativeProgress') { if (!peer.terminal) await peer.setNativeProgress(...args); }
      else if (name === 'disconnect') await peer.disconnect(...args);
      else if (name === 'fail') await peer.fail(...args);
      else if (name !== 'snapshot' && name !== 'drain') throw Error(`Unknown browser peer RPC: ${name}`);
      if (!autonomousPump || !closed) await pump();
      const result = snapshot();
      if (name === 'drain') result.records = exports.splice(0);
      return result;
    });
    return operation;
  }
  function close(options = {}) {
    closeOperation ??= finishClose(options);
    return closeOperation;
  }
  async function finishClose({intentional = true} = {}) {
    closing = true;
    wakeRequested = false;
    intentionalClose = intentional;
    const failures = [];
    if (unsubscribeProgress) {
      try { unsubscribeProgress(); } catch (error) { failures.push(remember(error)); }
      unsubscribeProgress = null;
    }
    const finalNativeDrain = async () => {
      if (!autonomousPump) return;
      await settle();
      const before = await callNative('status');
      const quiescent = status => status?.active === 1 &&
        Number.isSafeInteger(status.cursor) && status.cursor >= 0 && status.cursor <= sourceTicks &&
        ((status.blocker === 'complete' && status.cursor === sourceTicks && status.terminal?.kind === 0) ||
         (status.blocker === 'terminal' && Number.isInteger(status.terminal?.kind) &&
          status.terminal.kind >= 1 && status.terminal.kind <= 4 &&
          Number.isSafeInteger(status.terminal.tick) && status.terminal.tick >= 0 &&
          status.terminal.tick < sourceTicks && Number.isSafeInteger(status.terminal.channel) &&
          status.terminal.channel >= 0 && status.terminal.channel <= 0xffffffff));
      if (!quiescent(before)) throw Error('Autonomous native pump close requires complete or terminal native quiescence');
      await pump();
      const after = await callNative('status');
      if (!quiescent(after) || after.cursor !== before.cursor ||
          JSON.stringify(after.terminal) !== JSON.stringify(before.terminal) || after.ring_pending !== 0)
        throw Error('Autonomous native pump close boundary changed or retained native records');
    };
    for (const operation of [() => rpcChain, finalNativeDrain, () => endpoint.close(), () => startup, () => settle()]) {
      try { await operation(); } catch (error) { failures.push(remember(error)); }
    }
    closed = endpoint.closed;
    if (inputCapture && globalThis.__meleeWebNetLocalInputCapture === captureLocalInput)
      globalThis.__meleeWebNetLocalInputCapture = null;
    if (failure) failures.push(failure);
    if (failures.length) throw new AggregateError([...new Set(failures)], 'Browser native peer close failed');
    return snapshot();
  }
  return {rpc, close, armClose() { intentionalClose = true; }, snapshot};
}
