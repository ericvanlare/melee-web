import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {MatchInputTimeline} from '../scripts/net_rollback_input_timeline.mjs';

// The expected wire/hash/frame values were captured from pre-portable producer
// da1f3eaf9836f757380cfdfc7e9d2cafa97eeab1 (protocol module SHA-256
// d21dd7a5f81aa31a4b6a821bb3f6fa2b1f0b1b8a6c314b5c22af21bc1bc62aea).
const wireFixture = JSON.parse(await readFile(new URL('./fixtures/net_lockstep_pre_portable_wire_v1.json', import.meta.url), 'utf8'));
const checksumFixture = JSON.parse(await readFile(new URL('./fixtures/net_lockstep_pre_portable_checksum_v1.json', import.meta.url), 'utf8'));
let portableCore;

function deferred() {
  let resolve, reject;
  const promise = new Promise((resolvePromise, rejectPromise) => { resolve = resolvePromise; reject = rejectPromise; });
  return {promise, resolve, reject};
}

function installDigest(digest) {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
  Object.defineProperty(globalThis, 'crypto', {
    configurable: true, enumerable: true, writable: true, value: {subtle: {digest}},
  });
  return () => Object.defineProperty(globalThis, 'crypto', previous);
}

async function loadCoreWithoutBuffer(callback) {
  const source = await readFile(new URL('../scripts/net_lockstep_core.mjs', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /^\s*import\b/m, 'portable core must have no module imports');
  assert.doesNotMatch(source, /\b(?:Buffer|process|require|node:)\b/, 'portable core must not depend on Node globals or modules');
  const previousBuffer = Object.getOwnPropertyDescriptor(globalThis, 'Buffer');
  try {
    Object.defineProperty(globalThis, 'Buffer', {configurable: true, enumerable: true, writable: true, value: undefined});
    assert.equal(globalThis.Buffer, undefined);
    portableCore = await import('../scripts/net_lockstep_core.mjs?portable-no-buffer');
    return await callback(portableCore);
  } finally {
    if (previousBuffer) Object.defineProperty(globalThis, 'Buffer', previousBuffer);
    else delete globalThis.Buffer;
  }
}

const hex = bytes => [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join('');
const fromHex = text => Uint8Array.from(text.match(/../g), pair => Number.parseInt(pair, 16));
const hashText = async text => hex(new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', new TextEncoder().encode(text))));
const sample = value => new Uint8Array([0, value & 0xff, value, 0, 0, 0, 0, 0, 0, 0, 0]);

async function readyPair(core) {
  const wire = {alpha: [], beta: []}, frames = {alpha: [], beta: []}, terminals = {alpha: [], beta: []};
  const make = role => new core.LockstepPeer({role, sourceTicks: 4, inputTicks: 2,
    pushFrame: async (firstTick, bytes) => {
      for (let offset = 0; offset < bytes.length; offset += core.NET_FRAME_BYTES)
        frames[role].push({tick: firstTick + offset / core.NET_FRAME_BYTES,
          bytes: bytes.slice(offset, offset + core.NET_FRAME_BYTES)});
    },
    onTerminal: async terminal => terminals[role].push(terminal),
  });
  const peers = {alpha: make('alpha'), beta: make('beta')};
  for (const role of ['alpha', 'beta']) peers[role].attach(async text => wire[role].push(text));
  const agreement = {build: 'same'};
  await Promise.all([peers.alpha.start(agreement), peers.beta.start(agreement)]);
  await Promise.all([peers.alpha.receive(wire.beta[0]), peers.beta.receive(wire.alpha[0])]);
  return {peers, wire, frames, terminals, agreement};
}

async function readyAlpha(core, {onReady = () => {}, onTerminal = () => {}} = {}) {
  const peer = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    pushFrame: async () => {}, onReady, onTerminal});
  peer.attach(async () => {});
  const agreement = {build: 'same'};
  await peer.start(agreement);
  await peer.receive(JSON.stringify({type: 'hello', version: core.LOCKSTEP_VERSION,
    role: 'beta', local_port: 1, remote_port: 0, agreement}));
  return peer;
}

test('portable core imports and runs without Buffer, Node imports, or a transport', async () => {
  await loadCoreWithoutBuffer(async core => {
    const run = await readyPair(core);
    const {peers, wire, frames, terminals, agreement} = run;
    assert.equal(await hashText(JSON.stringify(agreement)), wireFixture.agreement_sha256_hex);
    assert.deepEqual(terminals, {alpha: [], beta: []});
    for (const role of ['alpha', 'beta']) {
      const expected = wireFixture.peers[role];
      assert.equal(wire[role][0], expected.hello_json_utf8);
      assert.equal(new TextEncoder().encode(wire[role][0]).byteLength, expected.hello_utf8_bytes);
      assert.equal(await hashText(wire[role][0]), expected.hello_sha256_hex);
      assert.equal(peers[role].agreementHash, expected.ready_callback_agreement_hash_hex);
      assert.equal(frames[role].length, expected.source_frames_before_input);
      assert.equal(frames[role][0].bytes.constructor, Uint8Array);
    }

    await Promise.all([peers.alpha.addLocalInput(0, sample(1)), peers.beta.addLocalInput(0, sample(2))]);
    assert.equal(wire.alpha[1], wireFixture.peers.alpha.first_state_json_utf8);
    assert.equal(wire.beta[1], wireFixture.peers.beta.first_state_json_utf8);
    assert.equal(new TextEncoder().encode(wire.alpha[1]).byteLength, wireFixture.peers.alpha.first_state_utf8_bytes);
    assert.equal(new TextEncoder().encode(wire.beta[1]).byteLength, wireFixture.peers.beta.first_state_utf8_bytes);
    await Promise.all([peers.alpha.receive(wire.beta[1]), peers.beta.receive(wire.alpha[1])]);
    for (const role of ['alpha', 'beta']) {
      const expected = wireFixture.peers[role];
      const receiver = peers[role === 'alpha' ? 'beta' : 'alpha'];
      assert.equal(receiver.lastAccepted.get(0), expected.first_state_raw_sha256_hex);
      const frame = frames[role].find(row => row.tick === 2).bytes;
      assert.equal(hex(frame), expected.source_frame_tick2_hex);
    }

    assert.deepEqual(core.parseNetChecksum(fromHex(checksumFixture.record_hex)), checksumFixture.parsed);
    assert.equal(core.lockstepConstants.noControllerPad, `${'00'.repeat(10)}ff`);
  });
});

test('indexed suffix reads touch only the new fixed-workload rows', async () => {
  await loadCoreWithoutBuffer(async core => {
    for (const history of [0, 3000, 5084]) {
      const rows = Array.from({length: history + 1}, (_, index) => Object.freeze({index}));
      const reads = [];
      const observed = new Proxy(rows, {get(target, key, receiver) {
        if (typeof key === 'string' && /^\d+$/.test(key)) reads.push(Number(key));
        return Reflect.get(target, key, receiver);
      }});
      const suffix = core.readOnlySuffix(observed, history, 'test history');
      assert.deepEqual(reads, [history], `history ${history} performed no prior-row reads`);
      assert.equal(suffix.length, 1);
      assert.equal(suffix[0], rows[history], 'immutable retained row is referenced without copying');
      assert(Object.isFrozen(suffix));
      assert.throws(() => core.readOnlySuffix(observed, -1), /offset/);
      assert.throws(() => core.readOnlySuffix(observed, history + 2), /offset/);
    }
  });
});

async function readyStreamingPair(core, inputTicks) {
  const wire = {alpha: [], beta: []}, frames = {alpha: [], beta: []};
  const make = role => new core.LockstepPeer({role, sourceTicks: inputTicks + 2,
    inputTicks, streaming: true,
    pushFrame: async (firstTick, bytes) => {
      for (let offset = 0; offset < bytes.length; offset += core.NET_FRAME_BYTES)
        frames[role].push({tick:firstTick+offset/core.NET_FRAME_BYTES,
          bytes:bytes.slice(offset,offset+core.NET_FRAME_BYTES)});
    }});
  const peers = {alpha:make('alpha'), beta:make('beta')};
  for (const role of ['alpha','beta']) peers[role].attach(async text=>wire[role].push(text));
  const agreement = {build:'same', scope:'stream-test', match_final:1, streaming:1};
  await Promise.all([peers.alpha.start(agreement),peers.beta.start(agreement)]);
  let alphaRead=0,betaRead=0;
  const exchange = async () => {
    for(let round=0;round<32;++round){
      const before=[alphaRead,betaRead,wire.alpha.length,wire.beta.length].join(':');
      while(alphaRead<wire.alpha.length) await peers.beta.receive(wire.alpha[alphaRead++]);
      while(betaRead<wire.beta.length) await peers.alpha.receive(wire.beta[betaRead++]);
      if(before===[alphaRead,betaRead,wire.alpha.length,wire.beta.length].join(':'))return;
    }
    assert.fail('stream peer-state exchange did not settle within its finite bound');
  };
  await exchange();
  return {peers,wire,frames,agreement,exchange};
}

test('opt-in streaming lockstep keeps transport storage bounded as prefixes retire', async()=>{
  const core=portableCore??await import('../scripts/net_lockstep_core.mjs');
  const {peers,frames,exchange}=await readyStreamingPair(core,96);
  for(let first=0;first<96;first+=8){
    for(let tick=first;tick<first+8;++tick) await Promise.all([
        peers.alpha.addLocalInput(tick,sample(tick+1)),
        peers.beta.addLocalInput(tick,sample(tick+101)),
      ]);
    for(const peer of [peers.alpha,peers.beta])
      assert(peer.nextSequence-(peer.remoteAckSequence+1)>=8,
        'each batch creates actual outstanding sends while the peer ACK trails');
    await exchange();
  }
  for(const role of ['alpha','beta']){
    const peer=peers[role],summary=peer.summary();
    assert.equal(peer.terminal,null);
    assert.equal(summary.streaming,true);
    assert.equal(summary.local_input_ticks,96);
    assert.equal(summary.remote_input_ticks,96);
    assert.equal(summary.remote_contiguous_input,95);
    assert.equal(summary.next_source_frame,98);
    assert.equal(summary.retained_local_input_rows,0);
    assert.equal(summary.retained_native_local_rows,0);
    assert.equal(summary.retained_remote_input_rows,0);
    assert(summary.remote_ack_advertised_input>=95,
      'a delayed acknowledgement retires its exact sent ACK watermark');
    assert(summary.retained_ack_watermarks<=core.LOCKSTEP_WINDOW,
      'unacknowledged packet watermarks remain bounded until a later peer packet');
    const expectedPendingSequences=Array.from({length:peer.nextSequence-peer.remoteAckSequence-1},
      (_,index)=>peer.remoteAckSequence+1+index);
    assert.deepEqual([...peer.ackAdvertisedBySequence.keys()],expectedPendingSequences,
      'retained ACK snapshots are exactly the sent sequences still awaiting acknowledgment');
    assert(frames[role].length<=98);
  }
  for(const peer of [peers.alpha,peers.beta]){
    await peer.disconnect('stream test teardown');
    assert.equal(peer.ackAdvertisedBySequence.size,0,
      'terminal teardown releases pending ACK snapshots');
  }
});

test('streaming capacity refuses the first extra input and a missing stream agreement', async()=>{
  const core=portableCore??await import('../scripts/net_lockstep_core.mjs');
  const peer=new core.LockstepPeer({role:'alpha',sourceTicks:3,inputTicks:1,streaming:true,
    pushFrame:async()=>{}});
  peer.attach(async()=>{});
  await assert.rejects(peer.start({build:'same'}),/explicitly agreed/);
  const invalidPeer=new core.LockstepPeer({role:'alpha',sourceTicks:3,inputTicks:1,streaming:false,
    pushFrame:async()=>{}});
  invalidPeer.attach(async()=>{});
  await assert.rejects(invalidPeer.start({build:'same',streaming:0}),/exactly 1 when present/);
  const {peers,exchange}=await readyStreamingPair(core,1);
  await assert.rejects(peers.alpha.addLocalInput(0,sample(8),{deferSend:true}),
    /does not permit deferred/);
  await Promise.all([peers.alpha.addLocalInput(0,sample(1)),peers.beta.addLocalInput(0,sample(2))]);
  await exchange();
  await assert.rejects(peers.alpha.addLocalInput(1,sample(3)),/capacity 1 was exhausted/);
  assert.equal(peers.alpha.terminal.kind,'protocol');
  assert.equal(peers.alpha.terminal.reason,'stream_input_capacity_exhausted');
});

test('streaming retention refuses progress past the existing 32-row transport window', async()=>{
  const core=portableCore??await import('../scripts/net_lockstep_core.mjs');
  const peer=new core.LockstepPeer({role:'alpha',sourceTicks:98,inputTicks:96,streaming:true,
    pushFrame:async()=>{}});
  const sent=[];
  peer.attach(async text=>sent.push(text));
  const agreement={build:'same',scope:'stream-test',match_final:1,streaming:1};
  await peer.start(agreement);
  await peer.receive(JSON.stringify({type:'hello',version:core.LOCKSTEP_VERSION,
    role:'beta',local_port:1,remote_port:0,agreement}));
  for(let tick=0;tick<32;++tick) await peer.addLocalInput(tick,sample(tick+1));
  assert.equal(peer.local.size,32);
  assert.equal(peer.nativeLocal.size,32);
  await assert.rejects(peer.addLocalInput(32,sample(33)),/32-row protocol window/);
  assert.equal(peer.terminal.kind,'protocol');
  assert.equal(peer.terminal.reason,'stream_local_retention_window_exhausted');
  assert.equal(peer.local.size,32,'refusal happens before another retained PAD row is written');
  assert.equal(peer.nativeLocal.size,32);
  assert.equal(sent.map(text=>JSON.parse(text)).filter(packet=>packet.type==='state').length,32,
    'the refused 33rd input is never published');
});

test('compact peer health matches summary counters after rejected and terminal input paths', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const peer = await readyAlpha(core);
  const assertCounters = () => {
    const full = peer.summary(), health = peer.health();
    for (const key of ['local_input_ticks', 'remote_input_ticks', 'remote_ack_input',
      'local_checksum_ticks', 'remote_checksum_ticks', 'next_checksum_compare',
      'remote_ack_checksum', 'local_contiguous_input', 'local_contiguous_checksum',
      'next_source_frame', 'native_local_divergences'])
      assert.equal(health[key], full[key], `compact ${key} remains summary-equivalent`);
    assert.equal(health.deferred_input_count, full.deferred_input_ticks.length);
    assert.equal(health.checksum_mismatch_count, full.checksum_mismatches.length);
  };
  await peer.addLocalInputs([[0, sample(4)]], {nativeOverrides: [[0, sample(7)]]});
  assert.equal(peer.health().native_local_divergences, peer.summary().native_local_divergences);
  await assert.rejects(peer.addLocalInputs([[1, sample(5)]],
    {nativeOverrides: [[1, new Uint8Array(10)]]}), /11 bytes/);
  assertCounters();
  await peer.addLocalInput(1, sample(5));
  assertCounters();
  await peer.addLocalInput(0, sample(4), {nativeSample: sample(8)});
  assert.equal(peer.health().terminal.kind, 'protocol');
  assertCounters();
});

test('browser-local input ticks 0 and 1 keep the neutral prefix and map to source ticks 2 and 3', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const {peers, wire, frames} = await readyPair(core);
  const portSamples = {alpha: [sample(5), sample(7)], beta: [sample(6), sample(8)]};
  let alphaRead = wire.alpha.length, betaRead = wire.beta.length;
  const exchangePendingStates = async () => {
    for (let round = 0; round < 16; ++round) {
      const before = [alphaRead, betaRead, wire.alpha.length, wire.beta.length].join(':');
      while (betaRead < wire.beta.length)
        await peers.alpha.receive(wire.beta[betaRead++]);
      while (alphaRead < wire.alpha.length)
        await peers.beta.receive(wire.alpha[alphaRead++]);
      if (before === [alphaRead, betaRead, wire.alpha.length, wire.beta.length].join(':')) return;
    }
    assert.fail('bounded peer-state exchange did not settle');
  };
  const neutralPad = new Uint8Array(core.NET_FRAME_BYTES);
  neutralPad.set(core.lockstepConstants.noControllerPad.match(/../g).map(byte => Number.parseInt(byte, 16)), 22);
  neutralPad.set(core.lockstepConstants.noControllerPad.match(/../g).map(byte => Number.parseInt(byte, 16)), 33);
  for (const role of ['alpha', 'beta']) {
    assert.equal(frames[role].length, 2);
    assert.deepEqual([...frames[role][0].bytes], [...neutralPad]);
    assert.deepEqual([...frames[role][1].bytes], [...neutralPad]);
  }
  await Promise.all(['alpha', 'beta'].map(role => peers[role].addLocalInput(0, portSamples[role][0])));
  await exchangePendingStates();
  for (const role of ['alpha', 'beta']) {
    const row = frames[role].find(frame => frame.tick === 2);
    assert.ok(row, `${role} input tick 0 should first be consumed at source tick 2`);
    const localPort = role === 'alpha' ? 0 : 1, remotePort = localPort === 0 ? 1 : 0;
    assert.deepEqual([...row.bytes.subarray(localPort * 11, localPort * 11 + 11)], [...portSamples[role][0]]);
    assert.deepEqual([...row.bytes.subarray(remotePort * 11, remotePort * 11 + 11)],
      [...portSamples[role === 'alpha' ? 'beta' : 'alpha'][0]]);
  }
  await Promise.all(['alpha', 'beta'].map(role => peers[role].addLocalInput(1, portSamples[role][1])));
  await exchangePendingStates();
  for (const role of ['alpha', 'beta']) {
    const row = frames[role].find(frame => frame.tick === 3);
    assert.ok(row, `${role} input tick 1 should first be consumed at source tick 3`);
    const localPort = role === 'alpha' ? 0 : 1, remotePort = localPort === 0 ? 1 : 0;
    assert.deepEqual([...row.bytes.subarray(localPort * 11, localPort * 11 + 11)], [...portSamples[role][1]]);
    assert.deepEqual([...row.bytes.subarray(remotePort * 11, remotePort * 11 + 11)],
      [...portSamples[role === 'alpha' ? 'beta' : 'alpha'][1]]);
  }
});

test('existing local publication boundary rejects changed bytes for a repeated input tick', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const peer = await readyAlpha(core);
  await peer.addLocalInput(0, sample(3));
  await peer.addLocalInput(0, sample(4));
  assert.equal(peer.terminal?.kind, 'protocol');
  assert.match(peer.terminal?.reason ?? '', /local input changed after publication/);
});

test('start identity preparation is atomic and a remote hello waits for WebCrypto', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const originalCrypto = globalThis.crypto;
  const started = deferred(), release = deferred();
  const restore = installDigest(async (algorithm, bytes) => {
    assert.equal(algorithm, 'SHA-256');
    started.resolve();
    return release.promise;
  });
  const sent = [], readySnapshots = [];
  const peer = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    pushFrame: async () => {}, onReady: async current => readySnapshots.push({
      ready: current.ready, localHello: current.localHello, hash: current.agreementHash,
    })});
  peer.attach(async text => sent.push(text));
  const agreement = {build: 'same'};
  let start, incoming, incomingSettled = false;
  try {
    start = peer.start(agreement);
    await started.promise;
    assert.equal(peer.localHello, null);
    assert.equal(peer.agreementHash, undefined);
    await assert.rejects(peer.start(agreement), /already sent/);

    incoming = peer.receive(JSON.stringify({type: 'hello', version: core.LOCKSTEP_VERSION,
      role: 'beta', local_port: 1, remote_port: 0, agreement}));
    incoming.then(() => { incomingSettled = true; }, () => { incomingSettled = true; });
    await Promise.resolve();
    assert.equal(incomingSettled, false, 'remote identity must wait for local preparation');
    assert.equal(peer.ready, false);
    const expected = await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(JSON.stringify(agreement)));
    release.resolve(expected);
    await Promise.all([start, incoming]);
    assert.equal(peer.ready, true);
    assert.equal(sent.length, 1);
    assert(readySnapshots[0].localHello);
    assert.equal(readySnapshots[0].hash, peer.agreementHash);
    assert.equal(readySnapshots[0].ready, true);
  } finally {
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(JSON.stringify(agreement))));
    restore();
    if (start && incoming) await Promise.allSettled([start, incoming]);
  }
});

test('agreement digest rejection is explicit and cannot publish readiness or hello', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const restore = installDigest(async () => { throw Error('controlled digest failure'); });
  const sent = [], terminals = [], ready = [];
  const peer = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
    pushFrame: async () => {}, onReady: async () => ready.push(true),
    onTerminal: async terminal => terminals.push(terminal)});
  peer.attach(async text => sent.push(text));
  try {
    await assert.rejects(peer.start({build: 'same'}), /start identity SHA-256 failed: controlled digest failure/);
    assert.equal(peer.localHello, null);
    assert.equal(peer.agreementHash, undefined);
    assert.equal(peer.ready, false);
    assert.equal(sent.length, 1);
    assert.equal(JSON.parse(sent[0]).type, 'terminal', 'digest failure is explicitly reported over an attached transport');
    assert.deepEqual(ready, []);
    assert.equal(terminals.length, 1);
    assert.equal(terminals[0].kind, 'protocol');
  } finally { restore(); }
});

test('state hashing and acceptance are serialized; raw-text duplicates stay exact', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const {peers, wire} = await readyPair(core);
  const {alpha, beta} = peers;
  await beta.addLocalInput(0, sample(2), {repeat: true});
  const state0 = wire.beta[1], state1 = wire.beta[2];
  assert.equal(JSON.parse(state0).sequence, 0);
  assert.equal(JSON.parse(state1).sequence, 1);

  const originalCrypto = globalThis.crypto;
  const started = deferred(), release = deferred();
  let digestCalls = 0;
  const restore = installDigest((algorithm, bytes) => {
    ++digestCalls;
    if (digestCalls === 1) { started.resolve(); return release.promise; }
    return originalCrypto.subtle.digest(algorithm, bytes);
  });
  let first, second;
  try {
    first = alpha.receive(state0);
    await started.promise;
    second = alpha.receive(state1);
    await Promise.resolve();
    assert.equal(digestCalls, 1, 'sequence 1 hashing cannot pass the held sequence 0 hash');
    assert.equal(alpha.expectedSequence, 0);
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state0)));
    await Promise.all([first, second]);
    assert.equal(digestCalls, 2);
    assert.equal(alpha.expectedSequence, 2);
    assert.equal(alpha.lastAccepted.get(0), await hashText(state0));
    assert.equal(alpha.lastAccepted.get(1), await hashText(state1));
    await alpha.receive(state0);
    await assert.rejects(alpha.receive(`${state0} `), /conflicting or expired duplicate state packet/);
    assert.equal(alpha.terminal.kind, 'protocol');
  } finally {
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state0)));
    restore();
    if (first && second) await Promise.allSettled([first, second]);
  }
});

test('a disconnect while state hashing is held prevents later acceptance or frame pumping', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const {peers, wire, frames} = await readyPair(core);
  const {alpha, beta} = peers;
  await beta.addLocalInput(0, sample(2));
  const state = wire.beta[1];
  const initialFrameCount = frames.alpha.length;
  const originalCrypto = globalThis.crypto;
  const started = deferred(), release = deferred();
  const restore = installDigest((algorithm, bytes) => { started.resolve(); return release.promise; });
  let receiving;
  try {
    receiving = alpha.receive(state);
    await started.promise;
    await alpha.disconnect('transport ended during state digest');
    assert.equal(alpha.terminal.kind, 'disconnect');
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state)));
    await receiving;
    assert.equal(alpha.expectedSequence, 0);
    assert.equal(alpha.remote.size, 0);
    assert.equal(frames.alpha.length, initialFrameCount);
  } finally {
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state)));
    restore();
    if (receiving) await Promise.allSettled([receiving]);
  }
});

test('state digest rejection fails explicitly without accepting or pumping the packet', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const {peers, wire, frames} = await readyPair(core);
  const {alpha, beta} = peers;
  await beta.addLocalInput(0, sample(2));
  const state = wire.beta[1];
  const initialFrameCount = frames.alpha.length;
  const restore = installDigest(async () => { throw Error('controlled state digest failure'); });
  try {
    await assert.rejects(alpha.receive(state), /controlled state digest failure/);
    assert.equal(alpha.terminal.kind, 'protocol');
    assert.match(alpha.terminal.reason, /controlled state digest failure/);
    assert.equal(alpha.expectedSequence, 0);
    assert.equal(alpha.lastAccepted.size, 0);
    assert.equal(alpha.remote.size, 0);
    assert.equal(frames.alpha.length, initialFrameCount);
    assert.equal(JSON.parse(wire.alpha.at(-1)).type, 'terminal');
  } finally { restore(); }
});

test('inbound receive backlog is bounded by message count and UTF-8 bytes', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const peer = await readyAlpha(core);
  const state = JSON.stringify({type: 'state', version: core.LOCKSTEP_VERSION, role: 'beta', sequence: 0,
    ack_sequence: null, ack_input: null, ack_checksum: null, unacknowledged: [], delayed_checksums: []});
  const originalCrypto = globalThis.crypto;
  const started = deferred(), release = deferred();
  const restore = installDigest((algorithm, bytes) => { started.resolve(); return release.promise; });
  let first;
  try {
    first = peer.receive(state);
    await started.promise;
    const queued = Array.from({length: core.LOCKSTEP_MAX_PENDING_RECEIVE_MESSAGES - 1}, () => peer.receive('{}'));
    assert.equal(peer.receiveQueueCount, core.LOCKSTEP_MAX_PENDING_RECEIVE_MESSAGES);
    await assert.rejects(peer.receive('{}'), /bounded capacity/);
    assert.equal(peer.terminal.kind, 'protocol');
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state)));
    await Promise.allSettled([first, ...queued]);
    assert.equal(peer.receiveQueueCount, 0);
  } finally {
    release.resolve(await originalCrypto.subtle.digest('SHA-256', new TextEncoder().encode(state)));
    restore();
    if (first) await Promise.allSettled([first]);
  }

  const oversized = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2, pushFrame: async () => {}});
  await assert.rejects(oversized.receive(' '.repeat(core.LOCKSTEP_MAX_PENDING_RECEIVE_BYTES + 1)), /1 MiB byte bound/);
  assert.equal(oversized.terminal.kind, 'protocol');

  const multibyte = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2, pushFrame: async () => {}});
  const threeByteCharacter = '\u0800';
  const multibyteText = threeByteCharacter.repeat(core.LOCKSTEP_MAX_PENDING_RECEIVE_BYTES / 2);
  assert(multibyteText.length <= core.LOCKSTEP_MAX_PENDING_RECEIVE_BYTES);
  assert(new TextEncoder().encode(multibyteText).byteLength > core.LOCKSTEP_MAX_PENDING_RECEIVE_BYTES);
  await assert.rejects(multibyte.receive(multibyteText), /1 MiB byte bound/);
  assert.equal(multibyte.terminal.kind, 'protocol');
});

test('foreign and malformed PAD/checksum state payloads fail closed', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const foreign = await readyAlpha(core);
  const badIdentity = {type: 'state', version: core.LOCKSTEP_VERSION, role: 'alpha', sequence: 0,
    ack_sequence: null, ack_input: null, ack_checksum: null, unacknowledged: [], delayed_checksums: []};
  await assert.rejects(foreign.receive(JSON.stringify(badIdentity)), /invalid state packet identity/);
  assert.equal(foreign.terminal.kind, 'protocol');

  const badPad = await readyAlpha(core);
  const malformedPad = {...badIdentity, role: 'beta', unacknowledged: [{tick: 0, pad: 'AA=='}]};
  await assert.rejects(badPad.receive(JSON.stringify(malformedPad)), /exactly 11 canonical bytes/);
  assert.equal(badPad.terminal.kind, 'protocol');

  const badChecksum = await readyAlpha(core);
  const malformedChecksum = {...badIdentity, role: 'beta', delayed_checksums: [{tick: 0, record: 'AA=='}]};
  await assert.rejects(badChecksum.receive(JSON.stringify(malformedChecksum)), /invalid delayed checksum record/);
  assert.equal(badChecksum.terminal.kind, 'protocol');
});

test('accepted remote PAD batches reach the timeline before a full native frame is available', async () => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const wire = {alpha: [], beta: []}, pushed = {alpha: [], beta: []}, batches = [];
  const agreement = {protocol: 'match-scoped-test-v1', match: 'same'};
  const makePeer = role => new core.LockstepPeer({role, sourceTicks: 8, inputTicks: 6,
    pushFrame: async (firstTick, bytes) => {
      for (let offset = 0; offset < bytes.length; offset += core.NET_FRAME_BYTES)
        pushed[role].push(firstTick + offset / core.NET_FRAME_BYTES);
    },
    onRemoteContributionBatch: role === 'alpha' ? async batch => {
      batches.push(batch);
      assert.equal(wire.alpha.length, 1, 'observer runs before the inbound state ACK is sent');
    } : undefined,
  });
  const peers = {alpha: makePeer('alpha'), beta: makePeer('beta')};
  for (const role of ['alpha', 'beta']) peers[role].attach(async text => wire[role].push(text));
  await Promise.all([peers.alpha.start(agreement), peers.beta.start(agreement)]);
  await Promise.all([peers.alpha.receive(wire.beta[0]), peers.beta.receive(wire.alpha[0])]);
  assert.deepEqual(pushed.alpha, [0, 1], 'readiness pushes only the existing neutral prefix');

  const timeline = new MatchInputTimeline({localPort: 0, remotePort: 1, initialRemotePad: sample(0)});
  for (let tick = 0; tick < 3; ++tick) {
    timeline.recordLocal(tick, sample(tick + 20));
    timeline.commitForward(timeline.planForward());
  }
  assert.equal(timeline.status().dirty_from, null);

  const remotePad = sample(7);
  await peers.beta.addLocalInput(0, remotePad);
  await peers.alpha.receive(wire.beta.at(-1));
  assert.equal(batches.length, 1);
  assert.deepEqual({
    protocolVersion: batches[0].protocolVersion,
    agreementHash: batches[0].agreementHash,
    senderRole: batches[0].senderRole,
    remotePort: batches[0].remotePort,
    packetSequence: batches[0].packetSequence,
  }, {
    protocolVersion: core.LOCKSTEP_VERSION,
    agreementHash: peers.alpha.agreementHash,
    senderRole: 'beta', remotePort: 1, packetSequence: 0,
  });
  assert.deepEqual(batches[0].inputs, [{tick: 0, pad: Array.from(remotePad)}]);
  assert.equal(Object.isFrozen(batches[0]), true);
  assert.equal(Object.isFrozen(batches[0].inputs), true);
  assert.equal(Object.isFrozen(batches[0].inputs[0].pad), true);
  assert.deepEqual(pushed.alpha, [0, 1], 'remote PAD is observed while the native frame still lacks local PAD');
  const accepted = timeline.acceptRemote(batches[0].inputs[0].tick, batches[0].inputs[0].pad);
  assert.equal(accepted.earliest_changed_tick, 0);
  assert.equal(timeline.status().dirty_from, 0, 'late literal PAD dirties the predicted suffix');
  assert.deepEqual(timeline.planReplay().frames.map(row => row.tick), [0, 1, 2]);

  await peers.alpha.addLocalInput(0, sample(31));
  assert.ok(pushed.alpha.includes(2), 'the unchanged native path pushes only after both literal inputs exist');
});

test('pending remote contribution admission blocks the frame pump and outbound acknowledgements', async t => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');

  for (const outcome of ['accepted', 'refused']) await t.test(outcome, async () => {
    const wire = {alpha: [], beta: []}, pushed = {alpha: [], beta: []};
    const observerEntered = deferred(), observerRelease = deferred();
    const terminalEntered = deferred(), terminalRelease = deferred();
    const refusal = Error('remote timeline refused contribution batch');
    const agreement = {protocol: 'match-scoped-test-v1', match: 'admission-race'};
    const makePeer = role => new core.LockstepPeer({role, sourceTicks: 4, inputTicks: 2,
      pushFrame: async (firstTick, bytes) => {
        for (let offset = 0; offset < bytes.length; offset += core.NET_FRAME_BYTES)
          pushed[role].push(firstTick + offset / core.NET_FRAME_BYTES);
      },
      onRemoteContributionBatch: role === 'alpha' ? async () => {
        observerEntered.resolve();
        await observerRelease.promise;
        if (outcome === 'refused') throw refusal;
      } : undefined,
      onTerminal: role === 'alpha' && outcome === 'refused' ? async () => {
        terminalEntered.resolve();
        await terminalRelease.promise;
      } : undefined,
    });
    const peers = {alpha: makePeer('alpha'), beta: makePeer('beta')};
    for (const role of ['alpha', 'beta']) peers[role].attach(async text => wire[role].push(text));
    await Promise.all([peers.alpha.start(agreement), peers.beta.start(agreement)]);
    await Promise.all([peers.alpha.receive(wire.beta[0]), peers.beta.receive(wire.alpha[0])]);
    assert.deepEqual(pushed.alpha, [0, 1]);

    await peers.beta.addLocalInput(0, sample(7));
    const remoteState = wire.beta.at(-1);
    const receivePromise = peers.alpha.receive(remoteState);
    await observerEntered.promise;

    // This outgoing publication can race the asynchronous page observer. It
    // must not let the pump consume the staged remote row or acknowledge it.
    await peers.alpha.addLocalInput(0, sample(31));
    assert.deepEqual(pushed.alpha, [0, 1], 'no source tick consumes an unadmitted remote PAD row');
    assert.equal(peers.alpha.remote.has(0), false, 'the received row remains staged');
    const concurrentState = wire.alpha.map(text => JSON.parse(text)).find(packet =>
      packet.type === 'state' && packet.unacknowledged.some(row => row.tick === 0));
    assert.equal(concurrentState.ack_sequence, null, 'pending packet sequence is not acknowledged');
    assert.equal(concurrentState.ack_input, null, 'pending remote input is not acknowledged');

    observerRelease.resolve();
    if (outcome === 'accepted') {
      await receivePromise;
      assert.deepEqual(pushed.alpha, [0, 1, 2]);
      assert.deepEqual(peers.alpha.remote.get(0), sample(7));
      const ack = wire.alpha.map(text => JSON.parse(text)).find(packet =>
        packet.type === 'state' && packet.ack_sequence === 0);
      assert.ok(ack, 'accepted packet receives its sequence acknowledgement');
      assert.equal(ack.ack_input, 0, 'accepted PAD row becomes acknowledgeable');
      return;
    }

    await terminalEntered.promise;
    assert.equal(peers.alpha.terminal.kind, 'protocol');
    assert.deepEqual(pushed.alpha, [0, 1], 'refusal cannot release the frame pump');
    assert.equal(peers.alpha.remote.has(0), false, 'refused row is never committed');
    assert.equal(wire.alpha.map(text => JSON.parse(text)).some(packet =>
      packet.type === 'state' && packet.ack_sequence === 0), false, 'refused packet is never acknowledged');
    terminalRelease.resolve();
    await assert.rejects(receivePromise, error => error === refusal);
    assert.equal(peers.alpha.terminal.kind, 'protocol');
  });
});

test('remote observer publishes neither a valid prefix of a malformed packet nor a checksum mismatch', async t => {
  const core = portableCore ?? await import('../scripts/net_lockstep_core.mjs');
  const agreement = {protocol: 'match-scoped-test-v1', match: 'same'};
  const hello = {type: 'hello', version: core.LOCKSTEP_VERSION, role: 'beta',
    local_port: 1, remote_port: 0, agreement};
  const state = (overrides = {}) => ({type: 'state', version: core.LOCKSTEP_VERSION,
    role: 'beta', sequence: 0, ack_sequence: null, ack_input: null, ack_checksum: null,
    unacknowledged: [{tick: 0, pad: Buffer.from(sample(7)).toString('base64')}],
    delayed_checksums: [], ...overrides});
  const make = async onRemoteContributionBatch => {
    const outbound = [];
    const peer = new core.LockstepPeer({role: 'alpha', sourceTicks: 4, inputTicks: 2,
      pushFrame: async () => {}, onRemoteContributionBatch});
    peer.attach(async text => outbound.push(JSON.parse(text)));
    await peer.start(agreement);
    await peer.receive(JSON.stringify(hello));
    outbound.length = 0;
    return {peer, outbound};
  };

  await t.test('malformed checksum tail', async () => {
    const observed = [];
    const run = await make(batch => observed.push(batch));
    await assert.rejects(run.peer.receive(JSON.stringify(state({delayed_checksums: [
      {tick: 0, record: 'AA=='},
    ]}))), /invalid delayed checksum record/);
    assert.equal(observed.length, 0);
    assert.equal(run.outbound.some(packet => packet.type === 'state'), false, 'no inbound ACK escaped');
    assert.equal(run.peer.terminal.kind, 'protocol');
  });

  await t.test('ready checksum mismatch', async () => {
    const observed = [];
    const run = await make(batch => observed.push(batch));
    const local = new Uint8Array(64);
    new DataView(local.buffer).setUint32(0, 0, true);
    await run.peer.addChecksum(local);
    const remote = new Uint8Array(local); remote[24] = 1;
    await run.peer.receive(JSON.stringify(state({delayed_checksums: [
      {tick: 0, record: Buffer.from(remote).toString('base64')},
    ]})));
    assert.equal(observed.length, 0, 'checksum mismatch prevents observation of the packet PAD row');
    assert.equal(run.outbound.some(packet => packet.type === 'state'), false, 'no inbound ACK escaped');
    assert.equal(run.peer.terminal.kind, 'desync');
  });

  await t.test('observer refusal terminalizes before ACK or frame pump', async () => {
    const refusal = Error('remote timeline refused contribution batch');
    const run = await make(() => { throw refusal; });
    await assert.rejects(run.peer.receive(JSON.stringify(state())), error => error === refusal);
    assert.equal(run.outbound.some(packet => packet.type === 'state'), false, 'no inbound ACK escaped');
    assert.equal(run.peer.terminal.kind, 'protocol');
  });
});
