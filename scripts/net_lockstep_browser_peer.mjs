/** Page-owned A3 protocol, room endpoint and native checksum boundary.
 * Node may publish recipe inputs and export evidence, but never handles packets
 * or submits native checksums to the protocol in this mode. */
import {LockstepPeer, NET_RECORD_BYTES, TERMINAL} from './net_lockstep_core.mjs';
import {createRoomRelayPeerEndpoint} from './net_lockstep_websocket_relay.mjs';

export const BROWSER_CHECKSUM_EXPORT_LIMIT = 512;

export function createBrowserNativePeer({role, sourceTicks, inputTicks, relayUrl, roomId,
  timeoutMs = 5000, agreement, native}, {createEndpoint = createRoomRelayPeerEndpoint} = {}) {
  if (!agreement || typeof agreement !== 'object') throw Error('Browser peer requires its frozen start agreement');
  let failure = null, intentionalClose = false, closed = false;
  let rpcChain = Promise.resolve(), activeNativeRecords = 0, postTerminalNativeRecords = 0;
  const exports = [], pending = new Set();
  const remember = error => { failure ??= error; return error; };
  const check = () => { if (failure) throw failure; };
  const track = operation => {
    const result = Promise.resolve().then(operation);
    const observed = result.catch(remember).finally(() => pending.delete(observed));
    pending.add(observed);
    return result;
  };
  const peer = new LockstepPeer({role, sourceTicks, inputTicks,
    pushFrame: async (tick, bytes) => {
      if (!await native.pushIndexed(tick, bytes)) throw Error('Native indexed input queue rejected agreed frames');
    },
    onReady: async () => {
      if (!await native.confirmStart()) throw Error('Native rejected peer start identity confirmation');
    },
    onTerminal: terminal => native.terminate(TERMINAL[terminal.kind] ?? TERMINAL.protocol,
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
  // Start WebCrypto preparation before returning the allocated endpoint. An
  // early remote hello must join that same preparation, never precede it.
  const startup = peer.start(agreement);
  startup.catch(remember);
  endpoint.ready.catch(remember);

  async function settle() {
    await endpoint.drainInbound(timeoutMs);
    while (pending.size) await Promise.all([...pending]);
    await peer.receiveQueue;
    await peer.pump;
    check();
  }
  async function pump() {
    await settle();
    // One extra record detects capacity exhaustion; do not silently truncate or
    // stop draining and describe the remaining native records as exported.
    const records = await native.drain(BROWSER_CHECKSUM_EXPORT_LIMIT - exports.length + 1);
    if (!Array.isArray(records) || records.some(record => record.length !== NET_RECORD_BYTES))
      throw Error('Native checksum drain returned malformed records');
    if (records.length + exports.length > BROWSER_CHECKSUM_EXPORT_LIMIT) {
      const error = remember(Error('Browser native checksum export queue exceeded its 512-record bound'));
      await peer.fail('protocol', {reason: error.message});
      throw error;
    }
    for (const record of records) {
      const bytes = Uint8Array.from(record);
      if (!peer.terminal) { await peer.addChecksum(bytes); ++activeNativeRecords; }
      else ++postTerminalNativeRecords;
      exports.push(Array.from(bytes));
    }
    if (peer.ready && !peer.terminal) await peer.setNativeProgress((await native.status()).cursor);
    await settle();
  }
  const snapshot = () => ({protocol: peer.summary(), endpointErrors: endpoint.errors,
    endpointClosed: endpoint.closed, closed, exportRecords: exports.length,
    checksumOwnership: {owner: 'browser-page', export_limit_records: BROWSER_CHECKSUM_EXPORT_LIMIT,
      active_native_records_submitted_before_export: activeNativeRecords,
      post_terminal_native_evidence_records: postTerminalNativeRecords},
    failure: failure ? String(failure?.stack || failure) : null, transport: endpoint.transport});

  // Serialize RPCs so native scratch and evidence exports have one owner.
  function rpc(name, args = []) {
    const operation = rpcChain.then(async () => {
      check();
      if (closed && !['snapshot', 'drain'].includes(name)) throw Error('Browser native peer is closed');
      if (name === 'start') { await Promise.all([startup, endpoint.ready]); await settle(); }
      else if (name === 'addLocalInput') await peer.addLocalInput(...args);
      else if (name === 'addLocalInputs') await peer.addLocalInputs(...args);
      else if (name === 'setNativeProgress') { if (!peer.terminal) await peer.setNativeProgress(...args); }
      else if (name === 'disconnect') await peer.disconnect(...args);
      else if (name === 'fail') await peer.fail(...args);
      else if (name !== 'snapshot' && name !== 'drain') throw Error(`Unknown browser peer RPC: ${name}`);
      await pump();
      const result = snapshot();
      if (name === 'drain') result.records = exports.splice(0);
      return result;
    });
    rpcChain = operation.catch(remember);
    return operation;
  }
  async function close({intentional = true} = {}) {
    intentionalClose = intentional;
    const failures = [];
    for (const operation of [() => rpcChain, () => endpoint.close(), () => startup, () => settle()]) {
      try { await operation(); } catch (error) { failures.push(remember(error)); }
    }
    closed = endpoint.closed;
    if (failure) failures.push(failure);
    if (failures.length) throw new AggregateError([...new Set(failures)], 'Browser native peer close failed');
    return snapshot();
  }
  return {rpc, close, armClose() { intentionalClose = true; }, snapshot};
}
