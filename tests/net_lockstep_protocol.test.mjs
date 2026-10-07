import assert from 'node:assert/strict';
import test from 'node:test';
import {
  LockstepPeer, LOCKSTEP_DELAY, RelayDecoder, relayFrame,
} from '../scripts/net_lockstep_protocol.mjs';
import {openLoopbackPeerPair} from '../scripts/net_lockstep_relay.mjs';

function harness({sourceTicks = 6, inputTicks = sourceTicks - LOCKSTEP_DELAY, agreement = {build: 'same'}} = {}) {
  const queues = {alpha: [], beta: []}, frames = {alpha: [], beta: []}, terminals = {alpha: [], beta: []};
  const receiveErrors = [];
  const make = role => new LockstepPeer({role, sourceTicks, inputTicks,
    pushFrame: async (tick, bytes) => {
      for (let offset = 0; offset < bytes.length; offset += 44)
        frames[role].push({tick: tick + offset / 44, bytes: bytes.subarray(offset, offset + 44)});
    },
    onTerminal: async terminal => terminals[role].push(terminal)});
  const alpha = make('alpha'), beta = make('beta');
  alpha.attach(async raw => queues.alpha.push(raw));
  beta.attach(async raw => queues.beta.push(raw));
  const flush = async () => {
    let changed = true, rounds = 0;
    while (changed) {
      if (++rounds > 40) throw Error('test transport did not settle');
      changed = false;
      for (const [from, to] of [['alpha', beta], ['beta', alpha]]) {
        const queue = queues[from];
        while (queue.length) {
          changed = true;
          try { await to.receive(queue.shift()); }
          catch (error) { receiveErrors.push(String(error.message || error)); }
        }
      }
    }
  };
  return {alpha, beta, queues, frames, terminals, receiveErrors, flush, agreement};
}

const sample = value => Buffer.from([0, value & 0xff, value, 0, 0, 0, 0, 0, 0, 0, 0]);
const record = (tick, input) => {
  const bytes = Buffer.alloc(64); bytes.writeUInt32LE(tick); bytes.writeBigUInt64LE(BigInt(input), 24); return bytes;
};

test('identity agreement precedes tick0 and complementary ports own separate local input', async () => {
  const h = harness();
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  assert.equal(h.alpha.ready, true); assert.equal(h.beta.ready, true);
  assert.deepEqual(h.frames.alpha.map(row => row.tick), [0, 1]);
  assert.deepEqual(h.frames.beta.map(row => row.tick), [0, 1]);
  await h.alpha.addLocalInput(0, sample(1));
  await h.beta.addLocalInput(0, sample(2));
  await h.flush();
  for (const role of ['alpha', 'beta']) {
    const frame = h.frames[role].find(row => row.tick === 2).bytes;
    assert.deepEqual(frame.subarray(0, 11), sample(1));
    assert.deepEqual(frame.subarray(11, 22), sample(2));
    assert.equal(frame.readUInt8(32), 0xff);
    assert.equal(frame.readUInt8(43), 0xff);
  }
});

test('duplicate contributions are idempotent and out-of-order input stays inside its bounded window', async () => {
  const h = harness();
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  await h.alpha.addLocalInput(0, sample(1));
  // Queue two identical state envelopes before the relay delivers either.
  await h.beta.addLocalInput(0, sample(2), {repeat: true});
  await h.flush();
  assert(h.alpha.inputDuplicates >= 1, 'retransmitted identical local contributions are idempotent');
  await h.beta.addLocalInput(2, sample(4));
  await h.flush();
  assert(h.alpha.outOfOrderInputs >= 1, 'future input is buffered until the missing contribution arrives');
  assert.equal(h.alpha.remoteContiguousInput, 0);
  await h.beta.addLocalInput(1, sample(3));
  await h.alpha.addLocalInput(1, sample(5));
  await h.flush();
  assert.equal(h.alpha.remoteContiguousInput, 2);
  assert.equal(h.beta.remoteContiguousInput, 1);
  assert.equal(h.terminals.alpha.length, 0);
  assert.equal(h.terminals.beta.length, 0);
});

test('deferred input survives real TCP ACK sends until an idempotent explicit release', async () => {
  const errors = [], frames = {alpha: [], beta: []}, sent = {alpha: [], beta: []};
  const relay = await openLoopbackPeerPair({onEndpointError: (role, error) => errors.push({role, error})});
  const make = role => new LockstepPeer({role, sourceTicks: 6, inputTicks: 4,
    pushFrame: async (first, bytes) => {
      for (let offset = 0; offset < bytes.length; offset += 44)
        frames[role].push({tick: first + offset / 44, bytes: Buffer.from(bytes.subarray(offset, offset + 44))});
    }});
  const alpha = make('alpha'), beta = make('beta');
  const until = async predicate => {
    const deadline = Date.now() + 2000;
    while (!predicate() && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 2));
    assert.equal(errors.length, 0, 'TCP peer callbacks must succeed');
    assert(predicate(), 'real TCP peer progress must meet the bound');
  };
  try {
    for (const peer of [alpha, beta]) {
      relay[peer.role].onMessage(raw => peer.receive(raw));
      peer.attach(async raw => { sent[peer.role].push(JSON.parse(raw)); await relay[peer.role].send(raw); });
    }
    await alpha.start({build: 'same'}); await beta.start({build: 'same'});
    await until(() => alpha.ready && beta.ready);
    await beta.addLocalInput(0, sample(2));
    await beta.addLocalInput(2, sample(4));
    await beta.addLocalInput(1, sample(3), {deferSend: true});
    const sendsBeforeAck = sent.beta.length;
    await alpha.addLocalInputs([[0, sample(10)], [1, sample(11)], [2, sample(12)], [3, sample(13)]]);
    await until(() => beta.remoteContiguousInput === 3 && alpha.remoteAckInput === 3 && alpha.remote.has(2));
    assert(sent.beta.length > sendsBeforeAck, 'incoming PAD traffic forced a real ACK state send');
    assert.equal(beta.local.has(1), true, 'held input remains locally available');
    assert.equal(alpha.remote.has(1), false, 'ACK retransmission must not leak held input1');
    assert.equal(alpha.remoteContiguousInput, 0);
    assert.equal(alpha.nextSourceFrame, 3, 'missing input1 holds the delayed source cursor3');
    assert.equal(sent.beta.some(packet => packet.unacknowledged?.some(row => row.tick === 1)), false);
    assert.deepEqual(beta.summary().deferred_input_ticks, [1]);

    // Delayed-checksum traffic also calls the same retransmission path.
    await beta.addChecksum(record(0, 9));
    await beta.setNativeProgress(3);
    await until(() => alpha.remoteChecksums.has(0));
    assert.equal(alpha.remote.has(1), false);
    assert.equal(alpha.nextSourceFrame, 3);

    await beta.addLocalInput(1, sample(3), {repeat: true});
    await until(() => alpha.remoteContiguousInput === 2 && alpha.nextSourceFrame === 5);
    assert.deepEqual(beta.summary().deferred_input_ticks, []);
    assert(alpha.inputDuplicates > 0);
    const before = frames.alpha.map(row => ({tick: row.tick, bytes: row.bytes.toString('hex')}));
    await beta.addLocalInput(1, sample(3), {repeat: true});
    await until(() => beta.remoteAckInput === 2);
    await relay.alpha.flush(); await relay.beta.flush();
    assert.deepEqual(frames.alpha.map(row => ({tick: row.tick, bytes: row.bytes.toString('hex')})), before,
      'releasing identical input again cannot push duplicate source frames');
    assert.equal(alpha.terminal, null); assert.equal(beta.terminal, null);
  } finally { await relay.close(); }
});

test('packet reordering holds state by sequence then acknowledges contiguous inputs and delayed checksums', async () => {
  const h = harness();
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  await h.beta.addLocalInput(2, sample(3));
  await h.beta.addLocalInput(1, sample(2));
  const [sequence0, sequence1] = h.queues.beta.splice(0);
  await h.alpha.receive(sequence1);
  assert.equal(h.alpha.remoteContiguousInput, -1, 'sequence 1 is buffered while sequence 0 is missing');
  assert.equal(h.alpha.remote.has(1), false, 'buffered packet has not contributed PAD data yet');
  await h.alpha.receive(sequence0);
  assert.equal(h.alpha.remote.has(1), true);
  await h.beta.addLocalInput(0, sample(1));
  await h.alpha.addLocalInputs([[0, sample(4)], [1, sample(5)], [2, sample(6)]]);
  await h.flush();
  assert.equal(h.alpha.remoteContiguousInput, 2);
  assert.equal(h.alpha.remoteAckInput, 2);
  assert.equal(h.beta.remoteAckInput, 2);
  assert(h.alpha.acknowledgedInputs >= 3);
  assert(h.beta.acknowledgedInputs >= 3);

  for (const tick of [2, 1, 0]) {
    await h.alpha.addChecksum(record(tick, tick + 10));
    await h.beta.addChecksum(record(tick, tick + 10));
  }
  await h.alpha.setNativeProgress(5); await h.beta.setNativeProgress(5); await h.flush();
  assert.equal(h.terminals.alpha.length, 0);
  assert.equal(h.terminals.beta.length, 0);
  assert.equal(h.alpha.nextChecksumCompare, 3);
  assert.equal(h.beta.nextChecksumCompare, 3);
  assert.equal(h.alpha.remoteAckChecksum, 2);
  assert.equal(h.beta.remoteAckChecksum, 2);
  assert(h.alpha.acknowledgedChecksums >= 3);
  assert(h.beta.acknowledgedChecksums >= 3);
});

test('conflicting start identity and input outside the reorder bound fail closed', async () => {
  const mismatch = harness({sourceTicks: 100, inputTicks: 98});
  await mismatch.alpha.start({build: 'a'}); await mismatch.beta.start({build: 'b'}); await mismatch.flush();
  assert.equal(mismatch.alpha.ready, false);
  assert.equal(mismatch.terminals.alpha[0]?.kind, 'protocol');
  assert.match(mismatch.receiveErrors[0], /start identity/);

  const h = harness({sourceTicks: 100, inputTicks: 98});
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  // A bounded future packet must not permit an unbounded receive-buffer jump.
  await h.beta.addLocalInput(50, sample(1)); await h.flush();
  assert.equal(h.terminals.alpha[0]?.kind, 'protocol');
  assert.match(h.terminals.alpha[0]?.reason ?? '', /reorder window/);
});

test('delayed checksums stop on the first changed declared channel', async () => {
  const h = harness();
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  await h.alpha.addChecksum(record(0, 1)); await h.beta.addChecksum(record(0, 2));
  // Checksum 0 is not sent until two subsequent source ticks have passed.
  await h.alpha.setNativeProgress(2); await h.beta.setNativeProgress(2); await h.flush();
  assert.equal(h.terminals.alpha.length, 0); assert.equal(h.terminals.beta.length, 0);
  await h.alpha.setNativeProgress(3); await h.beta.setNativeProgress(3); await h.flush();
  const terminal = [...h.terminals.alpha, ...h.terminals.beta][0];
  assert.equal(terminal?.kind, 'desync');
  assert.deepEqual({tick: terminal.tick, channel: terminal.channel}, {tick: 0, channel: 1});
});

test('out-of-order checksum envelopes report the earliest contiguous mismatch', async () => {
  const h = harness();
  await h.alpha.start(h.agreement); await h.beta.start(h.agreement); await h.flush();
  // Insert checksum records in reverse tick order. Tick 1 and tick 2 differ;
  // tick 1 must be reported even though tick 2 entered the Map first.
  for (const tick of [2, 1, 0]) {
    await h.alpha.addChecksum(record(tick, tick === 0 ? 0 : tick * 10 + 1));
    await h.beta.addChecksum(record(tick, tick === 0 ? 0 : tick * 10 + 2));
  }
  await h.alpha.setNativeProgress(5); await h.beta.setNativeProgress(5); await h.flush();
  const mismatch = [...h.alpha.checksumMismatches, ...h.beta.checksumMismatches]
    .sort((left, right) => left.tick - right.tick)[0];
  assert.deepEqual(mismatch, {tick: 1, channel: 1});
  assert.equal(h.terminals.alpha[0]?.tick, 1);
});

test('loopback length framing handles partial and multiple envelopes without parsing their payloads', () => {
  const first = relayFrame('{"opaque":1}'), second = relayFrame('{"opaque":2}');
  const decoder = new RelayDecoder();
  assert.deepEqual(decoder.push(first.subarray(0, 2)), []);
  assert.deepEqual(decoder.push(Buffer.concat([first.subarray(2), second])), ['{"opaque":1}', '{"opaque":2}']);
  assert.throws(() => decoder.push(Buffer.from([0, 0, 0, 0])), /Invalid loopback packet length/);
});

test('the local relay forwards opaque payloads between two real TCP peer sockets', async () => {
  let resolveMessage;
  const received = new Promise(resolve => { resolveMessage = resolve; });
  const relay = await openLoopbackPeerPair();
  try {
    relay.alpha.onMessage(resolveMessage);
    await relay.beta.send('{"type":"probe","contents":[1,2,3]}');
    assert.equal(await Promise.race([received, new Promise((_, reject) => setTimeout(() => reject(Error('relay timeout')), 1000))]),
      '{"type":"probe","contents":[1,2,3]}');
    assert(relay.traffic.beta_to_alpha_bytes > 0);
    assert.equal(relay.traffic.alpha_to_beta_bytes, 0);
  } finally { await relay.close(); }
});

test('the real relay reports rejected peer callbacks and keeps its message chain observed', async () => {
  const errors = [];
  const relay = await openLoopbackPeerPair({onEndpointError: (role, error) => {
    errors.push({role, message: String(error.message || error)});
  }});
  try {
    relay.alpha.onMessage(async () => { throw Error('intentional callback failure'); });
    relay.beta.onMessage(() => {});
    await relay.beta.send('{"type":"failure-probe"}');
    const deadline = Date.now() + 1000;
    while (!errors.length && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 5));
    assert.deepEqual(errors, [{role: 'alpha', message: 'intentional callback failure'}]);
    await relay.alpha.flush();
    await relay.beta.send('{"type":"chain-still-live"}');
    await new Promise(resolve => setTimeout(resolve, 25));
    await relay.alpha.flush();
    assert.equal(relay.alpha.errors.length, 2);
  } finally { await relay.close(); }
});


test('transport close reaches both terminal callbacks without sending to either TCP endpoint', async () => {
  let peers, intentionalClose = false, resolveDisconnected;
  const disconnected = new Promise(resolve => { resolveDisconnected = resolve; });
  const nativeTerminals = [], sends = [], endpointErrors = [];
  const relay = await openLoopbackPeerPair({
    onEndpointError: (role, error) => endpointErrors.push({role, message: error.message}),
    onDisconnect: (role, reason) => {
      if (intentionalClose) return;
      intentionalClose = true;
      void Promise.allSettled(['alpha', 'beta'].map(name => peers[name].disconnect(`${role}: ${reason}`)))
        .then(resolveDisconnected);
    },
  });
  try {
    peers = Object.fromEntries(['alpha', 'beta'].map(role => {
      const peer = new LockstepPeer({role, sourceTicks: 6, inputTicks: 4,
        pushFrame: async () => { throw Error('terminal transport must not push source input'); },
        onTerminal: async terminal => nativeTerminals.push({role, kind: terminal.kind}),
      });
      peer.attach(raw => { sends.push(role); return relay[role].send(raw); });
      return [role, peer];
    }));
    relay.beta.close();
    let timer;
    const results = await Promise.race([disconnected, new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error('TCP disconnect callback did not settle')), 1000);
    })]).finally(() => clearTimeout(timer));
    assert.deepEqual(results.map(row => row.status), ['fulfilled', 'fulfilled'],
      'closed TCP transport must not become a terminal notification failure');
    assert.deepEqual(sends, [], 'transport-close terminals never attempt peer notification');
    assert.deepEqual(nativeTerminals.sort((a, b) => a.role.localeCompare(b.role)), [
      {role: 'alpha', kind: 'disconnect'}, {role: 'beta', kind: 'disconnect'},
    ]);
    assert.equal(peers.alpha.terminal.kind, 'disconnect');
    assert.equal(peers.beta.terminal.kind, 'disconnect');
    assert.deepEqual(endpointErrors, []);
  } finally { intentionalClose = true; relay.alpha.destroy(); relay.beta.destroy(); await relay.close(); }
});

test('transport-close disconnect preserves native terminal callback rejection', async () => {
  let sendAttempts = 0;
  const peer = new LockstepPeer({role: 'beta', sourceTicks: 6, inputTicks: 4,
    pushFrame: async () => {},
    onTerminal: async () => { throw Error('native terminal callback rejected sentinel'); },
  });
  peer.attach(async () => { ++sendAttempts; throw Error('closed socket must not be used'); });
  await assert.rejects(peer.disconnect('TCP close'), /native terminal callback rejected sentinel/);
  assert.equal(peer.terminal.kind, 'disconnect');
  assert.equal(sendAttempts, 0);
});
