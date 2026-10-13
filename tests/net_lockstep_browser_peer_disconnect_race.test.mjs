import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import test from 'node:test';
import {createBrowserNativePeer} from '../scripts/net_lockstep_browser_peer.mjs';
import {createRoomId, createRoomRelayPeerEndpoint} from '../scripts/net_lockstep_websocket_relay.mjs';
import {LOCKSTEP_VERSION, TERMINAL} from '../scripts/net_lockstep_core.mjs';

const READY = JSON.stringify({relay: 1, event: 'ready'});
const agreement = {build: 'disconnect-race-fixture-v1'};
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}

async function waitFor(predicate, label, timeoutMs = 1500) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate() && Date.now() < deadline) await wait(2);
  assert(predicate(), `timed out waiting for ${label}`);
}

function frame(opcode, payload) {
  const body = Buffer.from(payload);
  if (body.length <= 125) return Buffer.concat([Buffer.from([0x80 | opcode, body.length]), body]);
  const header = Buffer.alloc(4);
  header[0] = 0x80 | opcode;
  header[1] = 126;
  header.writeUInt16BE(body.length, 2);
  return Buffer.concat([header, body]);
}

function stateWithRemoteInput() {
  return JSON.stringify({type: 'state', version: LOCKSTEP_VERSION, role: 'beta', sequence: 0,
    ack_sequence: null, ack_input: null, ack_checksum: null,
    unacknowledged: [{tick: 0, pad: Buffer.alloc(11).toString('base64')}], delayed_checksums: []});
}

async function startServer({includeRemoteInput = false, omitReady = false, holdRemoteFin = false} = {}) {
  const sockets = new Set();
  const fixtureErrors = [];
  const peers = new Set();
  const server = createServer({allowHalfOpen: holdRemoteFin}, (_request, response) => response.writeHead(404).end());
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key'];
    if (typeof key !== 'string') { fixtureErrors.push('missing WebSocket key'); socket.destroy(); return; }
    const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sockets.add(socket);
    peers.add(socket);
    socket.on('close', () => sockets.delete(socket));
    const hello = JSON.stringify({type: 'hello', version: LOCKSTEP_VERSION, role: 'beta',
      local_port: 1, remote_port: 0, agreement});
    const packets = omitReady ? [] : [READY, hello, ...(includeRemoteInput ? [stateWithRemoteInput()] : [])];
    socket.write(Buffer.concat(packets.map(packet => frame(1, packet))));
    socket.on('error', error => fixtureErrors.push(String(error?.message || error)));
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  return {
    url: `ws://127.0.0.1:${server.address().port}`,
    fixtureErrors,
    closeRemote(code = 4001, {holdFin = false} = {}) {
      const payload = Buffer.alloc(2);
      payload.writeUInt16BE(code);
      for (const socket of sockets) socket.write(frame(8, payload));
      if (!holdFin) for (const socket of sockets) socket.end();
    },
    finishRemoteClose() { for (const socket of sockets) socket.end(); },
    destroyRemote() { for (const socket of peers) socket.destroy(); },
    sendPacket(packet) {
      for (const socket of peers) socket.write(frame(1, packet));
    },
    async close() {
      for (const socket of sockets) socket.destroy();
      await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
      return {listener_closed: !server.listening, all_sockets_destroyed: [...peers].every(socket => socket.destroyed)};
    },
  };
}

function createPeer(server, {holdStateSend = false, holdEmptyState = false,
  holdSourceTickTwo = false, failStateSend = false, checksumTickZero = false, checksumConsumer = null} = {}) {
  const disconnectEntered = deferred();
  const releaseDisconnect = deferred();
  const stateSendEntered = deferred();
  const stateSendSettled = deferred();
  const releaseStateSend = deferred();
  const sourceTickTwoEntered = deferred();
  const releaseSourceTickTwo = deferred();
  let endpoint, endpointOptions;
  const injectedEndpointErrors = [];
  const pushed = [];
  const checksums = [];
  if (checksumTickZero) {
    const record = new Uint8Array(64);
    new DataView(record.buffer).setUint32(0, 0, true);
    record[4] = 0x5a;
    checksums.push(record);
  }
  let settlementGate = false;
  const settlementEntered = deferred(), releaseSettlement = deferred();
  let statusGate = null;
  const statusEntered = deferred(), releaseStatus = deferred();
  let nativeTerminal = null;
  const nativeTerminalObserved = deferred();
  let disconnectCallbackReleased = false;
  const openDisconnect = () => {
    disconnectCallbackReleased = true;
    releaseDisconnect.resolve();
  };
  const native = {
    async confirmStart() { return true; },
    configureLocalInputCapture(port, ticks) { return port === 0 && ticks === 2; },
    terminate(kind, tick, channel) {
      nativeTerminal = {kind, tick, channel};
      nativeTerminalObserved.resolve(nativeTerminal);
    },
    async pushIndexed(firstTick, bytes) {
      pushed.push({firstTick, byteLength: bytes.length});
      if (holdSourceTickTwo && firstTick === 2 && !sourceTickTwoEntered.done) {
        sourceTickTwoEntered.done = true;
        sourceTickTwoEntered.resolve();
        await releaseSourceTickTwo.promise;
      }
      return true;
    },
    async status() {
      if (statusGate) { statusGate = null; statusEntered.resolve(); await releaseStatus.promise; }
      return {active: 1, cursor: 0,
      blocker: nativeTerminal ? 'terminal' : 'wait', terminal: nativeTerminal ?? {kind: 0}}; },
    drain() { return checksums.splice(0); },
    dispose() {},
  };
  const controller = createBrowserNativePeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    relayUrl: server.url, roomId: createRoomId(), timeoutMs: 1200, agreement, native,
    inputCapture: {mode: 'live'}, checksumConsumer}, {createEndpoint(options) {
      endpointOptions = options;
    endpoint = createRoomRelayPeerEndpoint({...options, onDisconnect: async (...args) => {
        disconnectEntered.resolve();
        await releaseDisconnect.promise;
      return options.onDisconnect(...args);
    }});
    const actualErrors = Object.getOwnPropertyDescriptor(endpoint, 'errors').get;
    Object.defineProperty(endpoint, 'errors', {get() { return [...actualErrors.call(endpoint), ...injectedEndpointErrors]; }});
    const drainInbound = endpoint.drainInbound.bind(endpoint);
    endpoint.drainInbound = async (...args) => {
      if (settlementGate && controller.snapshot().checksumConsumer.pending_batch?.callback_started === false) {
        settlementGate = false; settlementEntered.resolve(); await releaseSettlement.promise;
      }
      return drainInbound(...args);
    };
    const send = endpoint.send.bind(endpoint);
    endpoint.send = text => {
      const packet = JSON.parse(text);
      if (holdStateSend && packet.type === 'state' &&
          (holdEmptyState || packet.unacknowledged?.length) && !stateSendEntered.done) {
        stateSendEntered.done = true;
        stateSendEntered.resolve();
        return releaseStateSend.promise.then(() => send(text)).then(value => {
          stateSendSettled.resolve({status: 'fulfilled'});
          return value;
        }, error => {
          stateSendSettled.resolve({status: 'rejected', name: error?.name,
            message: String(error?.message || error)});
          throw error;
        });
      }
      if (failStateSend && packet.type === 'state' && !stateSendEntered.done) {
        stateSendEntered.resolve();
        const error = Error('fixture unexpected state send failure');
        stateSendSettled.resolve({status: 'rejected', name: error.name, message: error.message});
        return Promise.reject(error);
      }
      return send(text);
    };
    return endpoint;
  }});
  return {controller, native, pushed, disconnectEntered, releaseDisconnect, openDisconnect,
    async injectEndpointError(error) { injectedEndpointErrors.push(error); await endpointOptions.onEndpointError('alpha', error); },
    statusEntered, releaseStatus, holdNextStatus() { statusGate = true; },
    settlementEntered, releaseSettlement, holdConsumerSettlement() { settlementGate = true; },
    enqueueChecksum(tick) { const row = new Uint8Array(64); new DataView(row.buffer).setUint32(0, tick, true); row[4] = 0x5a; checksums.push(row); return Array.from(row); },
    nativeTerminalObserved,
    get disconnectCallbackReleased() { return disconnectCallbackReleased; },
    stateSendEntered, stateSendSettled, releaseStateSend, sourceTickTwoEntered, releaseSourceTickTwo,
    get endpoint() { return endpoint; }};
}

async function startCase({includeRemoteInput = false, holdStateSend = false, holdRemoteFin = false,
  holdEmptyState = false, holdSourceTickTwo = false, failStateSend = false,
  checksumTickZero = false, checksumConsumer = null} = {}) {
  const server = await startServer({includeRemoteInput, holdRemoteFin});
  const peer = createPeer(server, {holdStateSend, holdEmptyState, holdSourceTickTwo,
    failStateSend, checksumTickZero, checksumConsumer});
  await peer.controller.rpc('start', [agreement]);
  return {server, ...peer, capture: globalThis.__meleeWebNetLocalInputCapture};
}

function pad() { return Uint8Array.from([0x80, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]); }

test('remote close racing an in-flight live publication is a cancelled disconnect, not protocol failure',
  async () => {
    let run;
    try {
      run = await startCase({holdStateSend: true});
      assert.equal(run.capture(0, 0, 1, pad()), true);
      assert.equal(run.capture(1, 0, 2, pad()), true, 'second sample queues behind the held publication');
      await run.stateSendEntered.promise;
      run.server.closeRemote();
      await waitFor(() => run.endpoint.closed, 'real endpoint close state');
      await run.disconnectEntered.promise;
      run.releaseStateSend.resolve();
      const sendOutcome = await run.stateSendSettled.promise;
      assert.equal(run.disconnectCallbackReleased, false,
        'the real endpoint send settles while its serialized disconnect callback remains held');
      assert.equal(sendOutcome.status, 'rejected');
      assert.equal(sendOutcome.name, 'RoomRelayClosedSendError');
      run.openDisconnect();
      let queueJoin = null, queueJoinError = null;
      try { queueJoin = await run.controller.rpc('snapshot'); }
      catch (error) { queueJoinError = String(error?.message || error); }
      let closeResult = null, closeError = null;
      try { closeResult = await run.controller.close({mode: 'normal'}); }
      catch (error) { closeError = String(error?.message || error); }
      const observed = run.controller.snapshot();
      console.log('RACE_SEND_RESULT ' + JSON.stringify({endpointClosed: observed.endpointClosed,
        sendOutcome, terminal: observed.protocol.terminal, failure: observed.failure?.split('\n')[0],
        queueJoinError, queueJoinResolved: queueJoin !== null, closeError, normalCloseResolved: closeResult !== null,
        captures: observed.localInputCapture.captures.map(row => row.source_cursor),
        localInputTicks: observed.protocol.local_input_ticks}));
      assert.equal(closeError, null, 'normal cleanup should join the proven disconnect and cancelled input');
      assert.equal(observed.failure, null,
        'only a typed send cancellation with endpoint-closed proof should be consumed');
      assert.equal(observed.protocol.terminal?.kind, 'disconnect');
      assert.deepEqual(observed.localInputCapture.captures.map(row => row.source_cursor), [0, 1]);
      assert.deepEqual(observed.localInputCapture.cancelled_publications.map(row => ({
        source_cursor: row.source_cursor, input_tick: row.input_tick, local_port: row.local_port,
        poll_serial: row.poll_serial, disposition: row.disposition, reason: row.reason, bytes: row.bytes,
        publication_scope: row.publication_scope,
      })), [
        {source_cursor: 0, input_tick: 0, local_port: 0, poll_serial: 1,
          disposition: 'cancelled_unpublished', reason: 'closed_transport_send', bytes: [...pad()],
          publication_scope: 'this_state_packet_attempt_only'},
        {source_cursor: 1, input_tick: 1, local_port: 0, poll_serial: 2,
          disposition: 'cancelled_before_publication', reason: 'remote_disconnect_before_queue_execution', bytes: [...pad()],
          publication_scope: 'no_state_packet_attempt'},
      ]);
      assert.equal(observed.protocol.local_input_ticks, 1,
        'core local insertion remains visible only for the sample accepted before send cancellation');
      assert.equal(observed.cancelledOutboundSends.length, 1);
      assert.equal(observed.cancelledOutboundSends[0].context, 'local_input');
      assert.equal(observed.cancelledOutboundSends[0].sequence, 0);
      assert.equal(observed.cancelledOutboundSends[0].publication_scope, 'this_state_packet_attempt_only');
      assert.deepEqual(run.server.fixtureErrors, []);
    } finally {
      if (run) {
        run.openDisconnect();
        run.releaseStateSend.resolve();
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('closed live-input send after committed disconnect remains unpublished without protocol failure',
  async () => {
    let run;
    try {
      run = await startCase({holdStateSend: true});
      assert.equal(run.capture(0, 0, 1, pad()), true);
      await run.stateSendEntered.promise;
      run.server.closeRemote();
      await waitFor(() => run.endpoint.closed, 'real endpoint close state before committed disconnect');
      await run.disconnectEntered.promise;
      run.openDisconnect();
      await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
        'committed disconnect before releasing the pending state send');
      run.releaseStateSend.resolve();
      const sendOutcome = await run.stateSendSettled.promise;
      assert.equal(sendOutcome.status, 'rejected');
      assert.equal(sendOutcome.name, 'RoomRelayClosedSendError');
      const queueJoin = await run.controller.rpc('snapshot');
      const closeResult = await run.controller.close({mode: 'normal'});
      const observed = run.controller.snapshot();
      assert.equal(closeResult.protocol.terminal?.kind, 'disconnect');
      assert.equal(queueJoin.failure, null);
      assert.equal(observed.failure, null);
      assert.deepEqual(observed.localInputCapture.cancelled_publications.map(row => ({
        source_cursor: row.source_cursor, disposition: row.disposition, reason: row.reason,
        poll_serial: row.poll_serial, bytes: row.bytes, publication_scope: row.publication_scope,
      })), [{source_cursor: 0, disposition: 'cancelled_unpublished',
        reason: 'closed_transport_send', poll_serial: 1, bytes: [...pad()],
        publication_scope: 'this_state_packet_attempt_only'}]);
      assert.deepEqual(observed.cancelledOutboundSends.map(row => ({context: row.context,
        reason: row.reason, sequence: row.sequence, input_ticks: row.input_ticks,
        publication_scope: row.publication_scope})), [{
        context: 'local_input', reason: 'closed_transport_send', sequence: 0, input_ticks: [0],
        publication_scope: 'this_state_packet_attempt_only'}]);
      assert.deepEqual(run.server.fixtureErrors, []);
    } finally {
      if (run) {
        run.openDisconnect();
        run.releaseStateSend.resolve();
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('native progress state-send cancellation is explicit and does not replace transport close',
  async () => {
    let run;
    try {
      run = await startCase({holdStateSend: true, holdEmptyState: true, checksumTickZero: true});
      await run.controller.rpc('snapshot');
      const progress = run.controller.rpc('setNativeProgress', [3]);
      await run.stateSendEntered.promise;
      run.server.closeRemote();
      await waitFor(() => run.endpoint.closed, 'real endpoint close during progress state send');
      await run.disconnectEntered.promise;
      run.openDisconnect();
      await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
        'committed disconnect before releasing progress state send');
      run.releaseStateSend.resolve();
      const sendOutcome = await run.stateSendSettled.promise;
      assert.equal(sendOutcome.status, 'rejected');
      assert.equal(sendOutcome.name, 'RoomRelayClosedSendError');
      const progressResult = await progress;
      const closeResult = await run.controller.close({mode: 'normal'});
      const observed = run.controller.snapshot();
      assert.equal(progressResult.failure, null);
      assert.equal(closeResult.protocol.terminal?.kind, 'disconnect');
      assert.equal(observed.failure, null);
      assert.deepEqual(observed.localInputCapture.cancelled_publications, []);
      assert.deepEqual(observed.cancelledOutboundSends.map(row => ({context: row.context,
        reason: row.reason, sequence: row.sequence, source_cursor: row.source_cursor,
        flush_final: row.flush_final, checksum_ticks: row.checksum_ticks,
        publication_scope: row.publication_scope})), [{context: 'native_progress',
        reason: 'closed_transport_send', sequence: 0, source_cursor: 3, flush_final: false,
        checksum_ticks: [0], publication_scope: 'this_state_packet_attempt_only'}]);
      assert.deepEqual(run.server.fixtureErrors, []);
    } finally {
      if (run) {
        run.openDisconnect();
        run.releaseStateSend.resolve();
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('remote input ACK send closed during inbound delivery remains a transport disconnect', async () => {
  let run;
  try {
    run = await startCase({holdStateSend: true, holdEmptyState: true});
    run.server.sendPacket(stateWithRemoteInput());
    await run.stateSendEntered.promise;
    run.server.closeRemote();
    await waitFor(() => run.endpoint.closed, 'real endpoint close state during inbound ACK send');
    run.releaseStateSend.resolve();
    const sendOutcome = await run.stateSendSettled.promise;
    assert.equal(sendOutcome.status, 'rejected');
    assert.equal(sendOutcome.name, 'RoomRelayClosedSendError');
    await run.disconnectEntered.promise;
    assert.equal(run.disconnectCallbackReleased, false,
      'the inbound receive callback settles before the reserved disconnect callback is released');
    run.openDisconnect();
    await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
      'inbound ACK disconnect terminal');
    let closeResult = null, closeError = null;
    try { closeResult = await run.controller.close({mode: 'normal'}); }
    catch (error) { closeError = String(error?.message || error); }
    const observed = run.controller.snapshot();
    console.log('RACE_INBOUND_ACK_RESULT ' + JSON.stringify({endpointClosed: observed.endpointClosed,
      sendOutcome, terminal: observed.protocol.terminal, failure: observed.failure?.split('\n')[0],
      closeError, normalCloseResolved: closeResult !== null,
      remoteInputTicks: observed.protocol.remote_input_ticks}));
    assert.equal(closeError, null, 'normal cleanup should join the inbound callback and disconnect');
    assert.equal(observed.failure, null,
      'a typed send cancellation proven by endpoint closure must not become receive protocol failure');
    assert.equal(observed.protocol.terminal?.kind, 'disconnect');
    assert.equal(observed.cancelledOutboundSends.length, 1);
    assert.equal(observed.cancelledOutboundSends[0].context, 'inbound_state_ack');
    assert.equal(observed.cancelledOutboundSends[0].reason, 'closed_transport_send');
    assert.equal(observed.cancelledOutboundSends[0].publication_scope, 'this_state_packet_attempt_only');
    assert.equal(observed.protocol.remote_input_ticks, 1,
      'the valid inbound PAD is retained even though its ACK state was not sent');
    assert.deepEqual(run.server.fixtureErrors, []);
  } finally {
    if (run) {
      run.openDisconnect();
      run.releaseStateSend.resolve();
      try { await run.controller.close({mode: 'fatal'}); } catch {}
      await run.server.close();
    }
  }
});

test('queued live publication after serialized disconnect is cancelled without poisoning the peer',
  async () => {
    let run;
    try {
      run = await startCase({includeRemoteInput: true, holdSourceTickTwo: true});
      assert.equal(run.controller.snapshot().protocol.remote_input_ticks, 1,
        'the real endpoint delivered the remote prerequisite input');
      assert.equal(run.capture(0, 0, 1, pad()), true);
      assert.equal(run.capture(1, 0, 2, pad()), true, 'second sample queues behind native frame publication');
      await run.sourceTickTwoEntered.promise;
      run.server.closeRemote();
      await waitFor(() => run.endpoint.closed, 'real endpoint close state');
      await run.disconnectEntered.promise;
      run.openDisconnect();
      await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
        'serialized disconnect terminal');
      run.releaseSourceTickTwo.resolve();
      let queueJoin = null, queueJoinError = null;
      try { queueJoin = await run.controller.rpc('snapshot'); }
      catch (error) { queueJoinError = String(error?.message || error); }
      let closeResult = null, closeError = null;
      try { closeResult = await run.controller.close({mode: 'normal'}); }
      catch (error) { closeError = String(error?.message || error); }
      const observed = run.controller.snapshot();
      console.log('RACE_QUEUED_RESULT ' + JSON.stringify({endpointClosed: observed.endpointClosed,
        terminal: observed.protocol.terminal, failure: observed.failure?.split('\n')[0],
        queueJoinError, queueJoinResolved: queueJoin !== null, closeError,
        normalCloseResolved: closeResult !== null,
        captures: observed.localInputCapture.captures.map(row => row.source_cursor),
        localInputTicks: observed.protocol.local_input_ticks, pushed: run.pushed}));
      assert.equal(closeError, null, 'normal cleanup should join the proven disconnect and cancelled input');
      assert.equal(observed.failure, null,
        'queued work fenced by the proven disconnect must be recorded as cancelled, not sticky failure');
      assert.equal(observed.protocol.terminal?.kind, 'disconnect');
      assert.deepEqual(observed.localInputCapture.captures.map(row => row.source_cursor), [0, 1]);
      assert.deepEqual(observed.localInputCapture.cancelled_publications.map(row => ({
        source_cursor: row.source_cursor, disposition: row.disposition, reason: row.reason,
        poll_serial: row.poll_serial, bytes: row.bytes, publication_scope: row.publication_scope,
      })), [
        {source_cursor: 0, disposition: 'cancelled_before_publication',
          reason: 'peer_disconnected_before_state_send', poll_serial: 1, bytes: [...pad()],
          publication_scope: 'no_state_packet_attempt'},
        {source_cursor: 1, disposition: 'cancelled_before_publication',
          reason: 'remote_disconnect_before_queue_execution', poll_serial: 2, bytes: [...pad()],
          publication_scope: 'no_state_packet_attempt'},
      ]);
      assert.deepEqual(observed.cancelledOutboundSends.map(row => ({context: row.context,
        reason: row.reason, sequence: row.sequence, input_ticks: row.input_ticks,
        packet_input_ticks: row.packet_input_ticks, publication_scope: row.publication_scope})), [{context: 'local_input',
        reason: 'peer_disconnected_before_state_send', sequence: null,
        input_ticks: [0], packet_input_ticks: [], publication_scope: 'no_state_packet_attempt'}]);
      assert.deepEqual(run.server.fixtureErrors, []);
    } finally {
      if (run) {
        run.openDisconnect();
        run.releaseSourceTickTwo.resolve();
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('unexpected state send errors stay sticky even when they are not closed-send cancellations',
  async () => {
    let run;
    try {
      run = await startCase({failStateSend: true});
      assert.equal(run.capture(0, 0, 1, pad()), true);
      await run.stateSendEntered.promise;
      const nativeTerminal = await run.nativeTerminalObserved.promise;
      const observed = run.controller.snapshot();
      assert.equal(nativeTerminal.kind, TERMINAL.protocol);
      assert.match(observed.failure, /fixture unexpected state send failure/);
      assert.deepEqual(observed.localInputCapture.cancelled_publications, []);
      assert.deepEqual(observed.cancelledOutboundSends, []);
      await assert.rejects(run.controller.close({mode: 'normal'}), /Browser native peer close failed/);
    } finally {
      if (run) {
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('invalid live PAD rows remain sticky and are never recast as disconnect cancellations',
  async () => {
    let run;
    try {
      run = await startCase();
      assert.equal(run.capture(0, 0, 1, new Uint8Array(10)), false);
      const nativeTerminal = await run.nativeTerminalObserved.promise;
      const observed = run.controller.snapshot();
      assert.equal(nativeTerminal.kind, TERMINAL.protocol);
      assert.match(observed.failure, /cursor, port, serial or PAD bytes are invalid/);
      assert.deepEqual(observed.localInputCapture.captures, []);
      assert.deepEqual(observed.localInputCapture.cancelled_publications, []);
      await assert.rejects(run.controller.close({mode: 'normal'}), /Browser native peer close failed/);
    } finally {
      if (run) {
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
    }
  });

test('transport error remains sticky after the real socket also closes', async () => {
  let run;
  try {
    run = await startCase();
    run.server.destroyRemote();
    await waitFor(() => run.endpoint.errors.length > 0, 'real endpoint transport error event');
    await waitFor(() => run.endpoint.closed, 'real endpoint closure after transport error');
    const nativeTerminal = await run.nativeTerminalObserved.promise;
    const observed = run.controller.snapshot();
    assert.equal(nativeTerminal.kind, TERMINAL.protocol);
    assert.equal(observed.endpointClosed, true);
    assert.equal(observed.endpointErrors.length > 0, true);
    assert.match(observed.failure, /transport receive failed|WebSocket transport failed/);
    assert.deepEqual(observed.cancelledOutboundSends, []);
    await assert.rejects(run.controller.close({mode: 'normal'}), /Browser native peer close failed/);
  } finally {
    if (run) {
      try { await run.controller.close({mode: 'fatal'}); } catch {}
      await run.server.close();
    }
  }
});


test('CLOSING state publication waits for physical close, not inbound disconnect settlement', async () => {
  const OriginalWebSocket = globalThis.WebSocket;
  let socket;
  globalThis.WebSocket = class extends OriginalWebSocket {
    constructor(...args) { super(...args); socket = this; }
  };
  let run;
  try {
    run = await startCase({holdStateSend: true, holdRemoteFin: true});
    assert.equal(run.capture(0, 0, 1, pad()), true);
    await run.stateSendEntered.promise;
    run.server.closeRemote(4001, {holdFin: true});
    await waitFor(() => socket.readyState === OriginalWebSocket.CLOSING,
      'real WebSocket CLOSING interval with remote FIN held');
    assert.equal(run.endpoint.closed, false);
    let settled = false;
    run.stateSendSettled.promise.then(() => { settled = true; });
    run.releaseStateSend.resolve();
    await wait(20);
    console.log('CLOSING_BEFORE_FIN ' + JSON.stringify({readyState: socket.readyState,
      endpointClosed: run.endpoint.closed, settled}));
    assert.equal(settled, false, 'send cancellation requires actual physical close proof');
    run.server.finishRemoteClose();
    const sendOutcome = await run.stateSendSettled.promise;
    assert.equal(socket.readyState, OriginalWebSocket.CLOSED);
    assert.equal(sendOutcome.name, 'RoomRelayClosedSendError');
    assert.equal(run.disconnectCallbackReleased, false);
    run.openDisconnect();
    await run.controller.rpc('snapshot');
    const result = await run.controller.close({mode: 'normal'});
    assert.equal(result.failure, null);
    assert.equal(result.protocol.terminal.kind, 'disconnect');
    assert.equal(result.cancelledOutboundSends.length, 1);
    assert.deepEqual(result.localInputCapture.captures[0].bytes, [...pad()]);
  } finally {
    if (run) {
      run.server.finishRemoteClose();
      run.openDisconnect();
      run.releaseStateSend.resolve();
      try { await run.controller.close({mode: 'fatal'}); } catch {}
      await run.server.close();
    }
    globalThis.WebSocket = OriginalWebSocket;
  }
});


for (const failureMode of ['timeout', 'transport-error']) {
  test(`CLOSING publication keeps ${failureMode} failure sticky`, async () => {
    const OriginalWebSocket = globalThis.WebSocket;
    let socket;
    globalThis.WebSocket = class extends OriginalWebSocket {
      constructor(...args) { super(...args); socket = this; }
    };
    let run;
    try {
      run = await startCase({holdStateSend: true, holdRemoteFin: true});
      assert.equal(run.capture(0, 0, 1, pad()), true);
      await run.stateSendEntered.promise;
      run.server.closeRemote(4001, {holdFin: true});
      await waitFor(() => socket.readyState === OriginalWebSocket.CLOSING, 'held physical close');
      const closingWaitStarted = performance.now();
      run.releaseStateSend.resolve();
      await wait(10);
      const additionalOutcome = failureMode === 'timeout'
        ? run.endpoint.send(JSON.stringify({type: 'state', fixture: 'second pending attempt'})).then(
          () => ({fulfilled: true}), error => ({name: error.name, message: error.message}))
        : null;
      if (failureMode === 'transport-error') {
        // Inject the endpoint's documented error event on the actual socket.
        // This tests classification, not a claim about a TCP error's origin.
        socket.dispatchEvent(new Event('error'));
        run.server.finishRemoteClose();
      }
      const sendOutcome = await run.stateSendSettled.promise;
      const terminal = await run.nativeTerminalObserved.promise;
      assert.equal(terminal.kind, TERMINAL.protocol);
      if (failureMode === 'timeout') {
        assert.equal(sendOutcome.name, 'Error');
        assert.match(sendOutcome.message, /did not close within 1200 ms/);
        const additional = await additionalOutcome;
        assert.equal(additional.name, 'Error');
        assert.match(additional.message, /did not close within 1200 ms/);
        assert(performance.now() - closingWaitStarted < 1500,
          'queued sends must share one physical-close timeout, not restart it');
        assert.equal(socket.readyState, OriginalWebSocket.CLOSING);
        run.server.finishRemoteClose();
      } else {
        assert(run.endpoint.errors.length > 0);
      }
      run.openDisconnect();
      await waitFor(() => run.endpoint.closed, 'eventual close after negative control');
      const observed = run.controller.snapshot();
      assert(observed.failure);
      assert.deepEqual(observed.cancelledOutboundSends, []);
      assert.deepEqual(observed.localInputCapture.cancelled_publications, []);
      await assert.rejects(run.controller.close({mode: 'normal'}), /Browser native peer close failed/);
    } finally {
      if (run) {
        run.server.finishRemoteClose();
        run.openDisconnect();
        run.releaseStateSend.resolve();
        try { await run.controller.close({mode: 'fatal'}); } catch {}
        await run.server.close();
      }
      globalThis.WebSocket = OriginalWebSocket;
    }
  });
}

test('CLOSING before relay readiness is an ordinary startup failure', async () => {
  const OriginalWebSocket = globalThis.WebSocket;
  let socket, server, endpoint;
  globalThis.WebSocket = class extends OriginalWebSocket {
    constructor(...args) { super(...args); socket = this; }
  };
  try {
    server = await startServer({omitReady: true, holdRemoteFin: true});
    endpoint = createRoomRelayPeerEndpoint({url: server.url, roomId: createRoomId(),
      role: 'alpha', timeoutMs: 1200, onMessage: () => {}});
    await waitFor(() => socket.readyState === OriginalWebSocket.OPEN, 'socket open before missing READY');
    server.closeRemote(4001, {holdFin: true});
    await waitFor(() => socket.readyState === OriginalWebSocket.CLOSING, 'startup CLOSING interval');
    await assert.rejects(endpoint.send('startup fixture'), error =>
      error.name === 'Error' && /WebSocket is closed/.test(error.message));
    server.finishRemoteClose();
    await assert.rejects(endpoint.ready);
  } finally {
    server?.finishRemoteClose();
    try { await endpoint?.close(); } catch {}
    await server?.close();
    globalThis.WebSocket = OriginalWebSocket;
  }
});


test('never-started consumer batch clean real disconnect reducer', {timeout: 5000}, async () => {
  let run, calls = 0, operation;
  try {
    run = await startCase({checksumConsumer: () => { ++calls; return true; }});
    run.holdNextStatus();
    const raw = run.enqueueChecksum(0);
    operation = run.controller.rpc('snapshot').then(value => ({value}), error => ({error}));
    await run.statusEntered.promise;
    const before = run.controller.snapshot().checksumConsumer.pending_batch;
    assert.equal(before.count, 1);
    assert.equal(before.callback_started, false);
    assert.equal(calls, 0);
    assert.deepEqual(before.records, [raw]);
    console.log('MILESTONE_BATCH_RETAINED_BEFORE_CALLBACK', JSON.stringify(before));
    run.server.closeRemote(4001);
    await waitFor(() => run.endpoint.closed, 'real physical endpoint close');
    await run.disconnectEntered.promise;
    run.openDisconnect();
    await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
      'owned disconnect committed while native status is held');
    // Native terminate queues after status: do not join it until releasing this gate.
    run.releaseStatus.resolve();
    const outcome = await operation;
    const close = await run.controller.close({mode: 'normal'}).then(value => ({value}), error => ({error}));
    const state = run.controller.snapshot();
    console.log('MILESTONE_DISCONNECT_CONSUMER_OUTCOME', JSON.stringify({
      operation_error: outcome.error?.message ?? null, close_error: close.error?.message ?? null,
      terminal: state.protocol.terminal, endpoint_errors: state.endpointErrors,
      endpoint_closed: state.endpointClosed, consumer: state.checksumConsumer, calls}));
    assert.equal(calls, 0);
    assert.deepEqual(state.checksumConsumer.retained, [raw]);
    assert.equal(state.checksumConsumer.accepted_records, 0);
    assert.equal(state.checksumConsumer.pending_batch.callback_started, false);
    assert.equal(state.checksumConsumer.pending_batch.accepted, false);
    assert.equal(state.checksumConsumer.pending_batch.joined, true);
    assert.equal(state.checksumConsumer.pending_batch.unjoined, false);
    assert.equal(state.checksumConsumer.pending_batch.disposition,
      'cancelled_before_checksum_consumer_callback_by_disconnect');
    assert.equal(state.failure, null);
    assert.deepEqual(state.endpointErrors, []);
    assert.equal(state.endpointClosed, true);
    assert.equal(outcome.error, undefined, 'clean disconnect before any callback must not become a checksum consumer failure');
    assert.equal(close.error, undefined, 'same proven cancellation must not make normal close fail');
  } finally {
    if (run) {
      run.releaseStatus.resolve(); run.openDisconnect(); run.releaseStateSend.resolve();
      if (operation) await operation;
      try { await run.controller.close({mode:'fatal'}); } catch {}
      const cleanup = await run.server.close();
      console.log('FIXTURE_CLEANUP', JSON.stringify({...cleanup, endpoint_closed: run.endpoint.closed,
        fixture_errors: run.server.fixtureErrors}));
      assert.equal(cleanup.listener_closed, true);
      assert.equal(cleanup.all_sockets_destroyed, true);
      assert.equal(run.endpoint.closed, true);
      assert.deepEqual(run.server.fixtureErrors, []);
    }
  }
});

test('never-started consumer historical settlement disconnect reducer', {timeout: 5000}, async () => {
  let run, calls = 0, operation;
  try {
    run = await startCase({checksumConsumer: () => { ++calls; return true; }});
    run.holdConsumerSettlement();
    const raw = run.enqueueChecksum(0);
    operation = run.controller.rpc('snapshot').then(value => ({value}), error => ({error}));
    await run.settlementEntered.promise;
    const before = run.controller.snapshot().checksumConsumer.pending_batch;
    assert.equal(before.count, 1);
    assert.equal(before.callback_started, false);
    assert.equal(calls, 0);
    assert.deepEqual(before.records, [raw]);
    console.log('MILESTONE_BATCH_RETAINED_BEFORE_CALLBACK', JSON.stringify(before));
    run.server.closeRemote(4001);
    await waitFor(() => run.endpoint.closed, 'real physical endpoint close');
    await run.disconnectEntered.promise;
    run.openDisconnect();
    await waitFor(() => run.controller.snapshot().protocol.terminal?.kind === 'disconnect',
      'owned disconnect committed while pre-consumer settlement is held');
    // This gate is outside endpoint inbound ordering; physical close/terminal can settle first.
    run.releaseSettlement.resolve();
    const outcome = await operation;
    assert.equal(outcome.error, undefined, 'historical consumer guard cancellation must be typed before tail drain');
    const tail = run.enqueueChecksum(1);
    await run.controller.rpc('snapshot');
    const close = await run.controller.close({mode: 'normal'}).then(value => ({value}), error => ({error}));
    const state = run.controller.snapshot();
    console.log('MILESTONE_DISCONNECT_CONSUMER_OUTCOME', JSON.stringify({
      operation_error: outcome.error?.message ?? null, close_error: close.error?.message ?? null,
      terminal: state.protocol.terminal, endpoint_errors: state.endpointErrors,
      endpoint_closed: state.endpointClosed, consumer: state.checksumConsumer, calls}));
    assert.equal(calls, 0);
    assert.deepEqual(state.checksumConsumer.retained, [raw, tail]);
    assert.deepEqual(state.checksumConsumer.pending_batch.records, [raw]);
    assert.equal(state.checksumOwnership.post_terminal_native_evidence_records, 1);
    assert.equal(state.checksumConsumer.accepted_records, 0);
    assert.equal(state.checksumConsumer.pending_batch.callback_started, false);
    assert.equal(state.checksumConsumer.pending_batch.accepted, false);
    assert.equal(state.checksumConsumer.pending_batch.joined, true);
    assert.equal(state.checksumConsumer.pending_batch.unjoined, false);
    assert.equal(state.checksumConsumer.pending_batch.disposition,
      'cancelled_before_checksum_consumer_callback_by_disconnect');
    assert.equal(state.failure, null);
    assert.deepEqual(state.endpointErrors, []);
    assert.equal(state.endpointClosed, true);
    assert.equal(outcome.error, undefined, 'clean disconnect before any callback must not become a checksum consumer failure');
    assert.equal(close.error, undefined, 'same proven cancellation must not make normal close fail');
  } finally {
    if (run) {
      run.releaseSettlement.resolve(); run.openDisconnect(); run.releaseStateSend.resolve();
      if (operation) await operation;
      try { await run.controller.close({mode:'fatal'}); } catch {}
      const cleanup = await run.server.close();
      console.log('FIXTURE_CLEANUP', JSON.stringify({...cleanup, endpoint_closed: run.endpoint.closed,
        fixture_errors: run.server.fixtureErrors}));
      assert.equal(cleanup.listener_closed, true);
      assert.equal(cleanup.all_sockets_destroyed, true);
      assert.equal(run.endpoint.closed, true);
      assert.deepEqual(run.server.fixtureErrors, []);
    }
  }
});


for (const kind of ['protocol', 'disconnect']) {
  test(`never-started consumer ${kind} without physical close remains fatal`, {timeout:5000}, async () => {
    let run, operation, calls=0;
    try {
      run=await startCase({checksumConsumer:()=>{++calls;return true;}});
      run.holdConsumerSettlement();const raw=run.enqueueChecksum(0);
      operation=run.controller.rpc('snapshot').then(value=>({value}),error=>({error}));
      await run.settlementEntered.promise;
      run.server.sendPacket(JSON.stringify({type:'terminal',version:LOCKSTEP_VERSION,role:'beta',
        kind,details:{reason:'fixture terminal without physical close'}}));
      await waitFor(()=>run.controller.snapshot().protocol.terminal?.kind===kind,'actual remote terminal packet');
      assert.equal(run.endpoint.closed,false);
      run.releaseSettlement.resolve();const outcome=await operation;
      assert(outcome.error);
      await assert.rejects(run.controller.close({mode:'normal'}),/close failed/);
      const state=run.controller.snapshot();assert.equal(calls,0);
      assert.equal(state.checksumConsumer.accepted_records,0);
      assert.deepEqual(state.checksumConsumer.retained,[raw]);
      assert.equal(state.checksumConsumer.pending_batch.disposition,null);
      assert(state.failure);
    } finally {
      if(run){run.releaseSettlement.resolve();run.openDisconnect();run.releaseStateSend.resolve();
        if(operation)await operation;try{await run.controller.close({mode:'fatal'});}catch{}
        const clean=await run.server.close();assert(clean.listener_closed&&clean.all_sockets_destroyed);}
    }
  });
}

test('never-started clean disconnect with separate endpoint error remains fatal',{timeout:5000},async()=>{
  let run,operation;
  try{
    run=await startCase({checksumConsumer:()=>true});run.holdConsumerSettlement();const raw=run.enqueueChecksum(0);
    operation=run.controller.rpc('snapshot').then(value=>({value}),error=>({error}));
    await run.settlementEntered.promise;run.server.closeRemote();
    await waitFor(()=>run.endpoint.closed,'physical close');run.openDisconnect();
    await waitFor(()=>run.controller.snapshot().protocol.terminal?.kind==='disconnect','committed disconnect');
    await run.injectEndpointError(Error('fixture explicit endpoint error'));
    run.releaseSettlement.resolve();const outcome=await operation;assert(outcome.error);
    await assert.rejects(run.controller.close({mode:'normal'}),/close failed/);
    const state=run.controller.snapshot();assert.equal(state.checksumConsumer.accepted_records,0);
    assert.deepEqual(state.checksumConsumer.retained,[raw]);
    assert.equal(state.checksumConsumer.pending_batch.disposition,null);
    assert.match(state.failure,/fixture explicit endpoint error/);
  }finally{if(run){run.releaseSettlement.resolve();run.openDisconnect();run.releaseStateSend.resolve();
    if(operation)await operation;try{await run.controller.close({mode:'fatal'});}catch{}
    const clean=await run.server.close();assert(clean.listener_closed&&clean.all_sockets_destroyed);}}
});

test('started checksum consumer disconnect stays fatal even after true acknowledgment',{timeout:5000},async()=>{
  const entered=deferred();let run,operation,calls=0;
  try{
    run=await startCase({checksumConsumer:(_batch,{signal})=>{++calls;entered.resolve();
      return new Promise(resolve=>signal.addEventListener('abort',()=>resolve(true),{once:true}));}});
    const raw=run.enqueueChecksum(0);
    operation=run.controller.rpc('snapshot').then(value=>({value}),error=>({error}));
    await entered.promise;run.server.closeRemote();await waitFor(()=>run.endpoint.closed,'physical close');
    run.openDisconnect();const outcome=await operation;assert(outcome.error);
    await assert.rejects(run.controller.close({mode:'normal'}),/close failed/);
    const state=run.controller.snapshot();assert.equal(calls,1);
    assert.equal(state.checksumConsumer.pending_batch.callback_started,true);
    assert.equal(state.checksumConsumer.pending_batch.accepted,false);
    assert.equal(state.checksumConsumer.pending_batch.disposition,null);
    assert.equal(state.checksumConsumer.accepted_records,0);
    assert.deepEqual(state.checksumConsumer.retained,[raw]);
  }finally{if(run){run.releaseSettlement.resolve();run.openDisconnect();run.releaseStateSend.resolve();
    if(operation)await operation;try{await run.controller.close({mode:'fatal'});}catch{}
    const clean=await run.server.close();assert(clean.listener_closed&&clean.all_sockets_destroyed);}}
});
