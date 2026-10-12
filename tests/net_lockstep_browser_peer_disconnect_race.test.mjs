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

async function startServer({includeRemoteInput = false} = {}) {
  const sockets = new Set();
  const fixtureErrors = [];
  const peers = new Set();
  const server = createServer((_request, response) => response.writeHead(404).end());
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
    const packets = [READY, hello, ...(includeRemoteInput ? [stateWithRemoteInput()] : [])];
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
    closeRemote(code = 4001) {
      const payload = Buffer.alloc(2);
      payload.writeUInt16BE(code);
      for (const socket of sockets) socket.write(frame(8, payload));
      for (const socket of sockets) socket.end();
    },
    destroyRemote() { for (const socket of peers) socket.destroy(); },
    sendPacket(packet) {
      for (const socket of peers) socket.write(frame(1, packet));
    },
    async close() {
      for (const socket of sockets) socket.destroy();
      await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
    },
  };
}

function createPeer(server, {holdStateSend = false, holdEmptyState = false,
  holdSourceTickTwo = false, failStateSend = false, checksumTickZero = false} = {}) {
  const disconnectEntered = deferred();
  const releaseDisconnect = deferred();
  const stateSendEntered = deferred();
  const stateSendSettled = deferred();
  const releaseStateSend = deferred();
  const sourceTickTwoEntered = deferred();
  const releaseSourceTickTwo = deferred();
  let endpoint;
  const pushed = [];
  const checksums = [];
  if (checksumTickZero) {
    const record = new Uint8Array(64);
    new DataView(record.buffer).setUint32(0, 0, true);
    record[4] = 0x5a;
    checksums.push(record);
  }
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
    status() { return {active: 1, cursor: 0,
      blocker: nativeTerminal ? 'terminal' : 'wait', terminal: nativeTerminal ?? {kind: 0}}; },
    drain() { return checksums.splice(0); },
    dispose() {},
  };
  const controller = createBrowserNativePeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    relayUrl: server.url, roomId: createRoomId(), timeoutMs: 1200, agreement, native,
    inputCapture: {mode: 'live'}}, {createEndpoint(options) {
      endpoint = createRoomRelayPeerEndpoint({...options, onDisconnect: async (...args) => {
        disconnectEntered.resolve();
        await releaseDisconnect.promise;
      return options.onDisconnect(...args);
    }});
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
    nativeTerminalObserved,
    get disconnectCallbackReleased() { return disconnectCallbackReleased; },
    stateSendEntered, stateSendSettled, releaseStateSend, sourceTickTwoEntered, releaseSourceTickTwo,
    get endpoint() { return endpoint; }};
}

async function startCase({includeRemoteInput = false, holdStateSend = false,
  holdEmptyState = false, holdSourceTickTwo = false, failStateSend = false,
  checksumTickZero = false} = {}) {
  const server = await startServer({includeRemoteInput});
  const peer = createPeer(server, {holdStateSend, holdEmptyState, holdSourceTickTwo,
    failStateSend, checksumTickZero});
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
