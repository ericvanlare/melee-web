import assert from 'node:assert/strict';
import test from 'node:test';
import {createHash} from 'node:crypto';
import {readFile} from 'node:fs/promises';
import {createBrowserNativePeer, BROWSER_CHECKSUM_EXPORT_LIMIT} from '../scripts/net_lockstep_browser_peer.mjs';
import {browserPeerFacade, createPeerModuleResponseObserver} from '../scripts/net_session_instance.mjs';

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return {promise, resolve};
}
function harness({terminalFailure = false, confirmFailure = false, sourceTicks = 8, readyGate = null} = {}) {
  const sent = [], nativeRecords = [], frames = [], terminals = [], pending = new Set();
  let callbacks, confirmations = 0;
  const endpoint = {ready: Promise.resolve(), closed: false, errors: [], transport: {type: 'room-websocket'},
    send: async text => { sent.push(JSON.parse(text)); },
    async drainInbound() { while (pending.size) await Promise.all([...pending]); },
    async close() { endpoint.closed = true; await callbacks.onDisconnect('alpha', 'closed'); },
  };
  const controller = createBrowserNativePeer({role: 'alpha', sourceTicks, inputTicks: sourceTicks - 2,
    relayUrl: 'ws://example.test', roomId: 'a'.repeat(32), agreement: {build: 'same'}, native: {
      async pushIndexed(tick, bytes) { frames.push({tick, bytes: Array.from(bytes)}); return true; },
      async confirmStart() { ++confirmations; if (readyGate) await readyGate.promise; return !confirmFailure; },
      terminate(...args) { terminals.push(args); if (terminalFailure) throw Error('terminal callback failure'); },
      status: () => ({cursor: nativeRecords.length}),
      drain: max => nativeRecords.splice(0, max),
    }}, {createEndpoint: options => { callbacks = options; return endpoint; }});
  async function deliver(packet) {
    const result = callbacks.onMessage(typeof packet === 'string' ? packet : JSON.stringify(packet));
    const observed = result.catch(() => {}).finally(() => pending.delete(observed));
    pending.add(observed);
    return result;
  }
  async function ready() {
    await controller.rpc('start', [{build: 'same'}]);
    await deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0, agreement: {build: 'same'}});
    await controller.rpc('snapshot');
  }
  const record = tick => {
    const bytes = new Uint8Array(64);
    new DataView(bytes.buffer).setUint32(0, tick, true);
    return Array.from(bytes);
  };
  return {controller, endpoint, sent, nativeRecords, frames, terminals, deliver, ready, record,
    get confirmations() { return confirmations; }};
}

test('page controller confirms start and gives native records to peer before exporting evidence', async () => {
  const run = harness();
  await run.ready();
  assert.equal(run.confirmations, 1);
  assert.equal(run.frames[0].tick, 0);
  assert.equal(run.frames[0].bytes.length, 88);
  run.nativeRecords.push(run.record(0), run.record(1));
  const row = await run.controller.rpc('drain');
  assert.equal(row.protocol.local_checksum_ticks, 2);
  assert.equal(row.checksumOwnership.active_native_records_submitted_before_export, 2);
  assert.equal(row.checksumOwnership.post_terminal_native_evidence_records, 0);
  assert.deepEqual(row.records, [run.record(0), run.record(1)]);
  assert.equal((await run.controller.rpc('drain')).records.length, 0);
  await run.controller.close();
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.terminals.length, 0);
});

test('fixed export capacity overflow remains sticky through snapshot, drain and close', async () => {
  const run = harness({sourceTicks: 516});
  await run.ready();
  run.nativeRecords.push(...Array.from({length: BROWSER_CHECKSUM_EXPORT_LIMIT + 1}, (_, tick) => run.record(tick)));
  await assert.rejects(run.controller.rpc('snapshot'), /512-record bound/);
  await assert.rejects(run.controller.rpc('drain'), /512-record bound/);
  await assert.rejects(run.controller.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
  assert.equal(run.terminals[0][0], 3);
});

test('late receive terminal callback failure is observed by settled RPC and close', async () => {
  const run = harness({terminalFailure: true});
  await run.ready();
  await assert.rejects(run.deliver('invalid JSON'), /terminal callback failure/);
  await assert.rejects(run.controller.rpc('snapshot'), /terminal callback failure/);
  await assert.rejects(run.controller.close(), /close failed/);
  assert.equal(run.endpoint.closed, true);
});

test('close joins a delayed inbound receive before returning', async () => {
  const run = harness();
  await run.ready();
  const gate = deferred();
  const original = run.endpoint.drainInbound;
  run.endpoint.drainInbound = async () => { await gate.promise; await original(); };
  let complete = false;
  const closing = run.controller.close().then(() => { complete = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(complete, false);
  gate.resolve();
  await closing;
  assert.equal(complete, true);
});

test('native start confirmation failure reaches the terminal and remains sticky', async () => {
  const run = harness({confirmFailure: true});
  await run.controller.rpc('start', [{build: 'same'}]);
  await assert.rejects(run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}}), /confirmation/);
  await assert.rejects(run.controller.rpc('snapshot'), /confirmation/);
  await assert.rejects(run.controller.close(), /close failed/);
});

test('facade refresh replaces stale ACK and terminal snapshot and serializes PAD byte views', async () => {
  const calls = [];
  const initial = {protocol: {remote_ack_input: -1, ready: false, terminal: null}, endpointErrors: []};
  const instance = {async peerRpc(name, args) {
    calls.push({name, args});
    return {protocol: {remote_ack_input: 5, ready: true, terminal: {kind: 'disconnect'}}, endpointErrors: []};
  }};
  const peer = browserPeerFacade(instance, initial);
  assert.equal(peer.remoteAckInput, -1);
  await peer.refresh();
  assert.equal(peer.remoteAckInput, 5);
  assert.equal(peer.terminal.kind, 'disconnect');
  await peer.addLocalInput(1, Uint8Array.of(1, 2), {nativeOverrides: [[1, Uint8Array.of(3)]]});
  assert.deepEqual(calls[1].args, [1, [1, 2], {nativeOverrides: [[1, [3]]]}]);
});


test('settled snapshot and close join a receive blocked inside native confirmation', async () => {
  const gate = deferred(), run = harness({readyGate: gate});
  await run.controller.rpc('start', [{build: 'same'}]);
  const receive = run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}});
  let snapshotDone = false, closeDone = false;
  const snapshot = run.controller.rpc('snapshot').then(() => { snapshotDone = true; });
  const close = run.controller.close().then(() => { closeDone = true; });
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(snapshotDone, false);
  assert.equal(closeDone, false);
  gate.resolve();
  await Promise.all([receive, snapshot, close]);
  assert.equal(run.confirmations, 1);
  assert.equal(closeDone, true);
});

const moduleNames = ['net_lockstep_browser_peer.mjs', 'net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs'];
const moduleBody = Buffer.from('module bytes');
const moduleHash = createHash('sha256').update(moduleBody).digest('hex');
function moduleObserver() {
  return createPeerModuleResponseObserver({url: 'http://127.0.0.1:8787/runtime.html',
    peerModuleHashes: Object.fromEntries(moduleNames.map(name => [name, moduleHash])),
    runtimeArtifactNames: ['runtime-development.mjs']});
}
function response(name, {body = moduleBody, coop = 'same-origin', status = 200} = {}) {
  return {url: () => new URL(name, 'http://127.0.0.1:8787/').href, status: () => status,
    allHeaders: async () => ({'cross-origin-opener-policy': coop, 'cross-origin-embedder-policy': 'require-corp'}),
    body: async () => body};
}

test('peer module observer records exact loaded bytes while admitting catalog runtime modules', async () => {
  const observer = moduleObserver();
  observer.observe(response('runtime-development.mjs'));
  for (const name of moduleNames) observer.observe(response(name));
  const rows = await observer.freeze();
  assert.equal(rows.length, 3);
  assert.ok(rows.every(row => row.sha256 === moduleHash && row.bytes === moduleBody.length));
});

test('peer module observer rejects unexpected paths, changed bytes, headers and duplicate/missing responses', async () => {
  for (const name of ['unexpected.mjs', 'subdir/net_lockstep_core.mjs', 'https://elsewhere.test/net_lockstep_core.mjs',
    'net_lockstep_core.mjs?extra=1']) {
    const observer = moduleObserver();
    for (const expected of moduleNames) observer.observe(response(expected));
    observer.observe(response(name));
    await assert.rejects(observer.freeze(), /Unexpected browser module import/);
  }
  for (const bad of [{body: Buffer.from('changed')}, {coop: 'unsafe-none'}, {status: 404}]) {
    const observer = moduleObserver();
    for (const name of moduleNames) observer.observe(response(name, name === moduleNames[0] ? bad : {}));
    await assert.rejects(observer.freeze(), /response identity differs/);
  }
  const duplicate = moduleObserver();
  for (const name of [...moduleNames, moduleNames[0]]) duplicate.observe(response(name));
  await assert.rejects(duplicate.freeze(), /incomplete or duplicated/);
  const missing = moduleObserver();
  missing.observe(response(moduleNames[0]));
  await assert.rejects(missing.freeze(), /incomplete or duplicated/);
});

test('page controller imports only unchanged portable core and endpoint', async () => {
  const source = await readFile(new URL('../scripts/net_lockstep_browser_peer.mjs', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /\b(?:Buffer|process|require|node:)\b/);
  const imports = [...source.matchAll(/^import .* from '([^']+)';$/gm)].map(match => match[1]);
  assert.deepEqual(imports, ['./net_lockstep_core.mjs', './net_lockstep_websocket_relay.mjs']);
});


test('an immediate remote hello joins startup before any facade start RPC', async () => {
  const run = harness();
  await run.deliver({type: 'hello', version: 1, role: 'beta', local_port: 1, remote_port: 0,
    agreement: {build: 'same'}});
  assert.equal(run.confirmations, 1);
  const row = await run.controller.rpc('start');
  assert.equal(row.protocol.ready, true);
  assert.equal(run.sent.filter(packet => packet.type === 'hello').length, 1);
  await run.controller.close();
});

test('module observer fails sticky on bounded events and rechecks late imports after early freeze', async () => {
  const observer = moduleObserver();
  for (const name of moduleNames) observer.observe(response(name));
  await observer.freeze();
  observer.observe(response('late-unexpected.mjs'));
  await assert.rejects(observer.freeze(), /Unexpected browser module import/);
  const bounded = moduleObserver();
  for (let i = 0; i < 5; ++i) bounded.observe(response('runtime-development.mjs'));
  await assert.rejects(bounded.freeze(), /event bound exceeded/);
});


test('post-terminal records remain separately labeled native evidence without touching closed protocol', async () => {
  const run = harness();
  await run.ready();
  await run.controller.rpc('disconnect', ['controlled disconnect']);
  run.nativeRecords.push(run.record(0));
  const row = await run.controller.rpc('drain');
  assert.equal(row.protocol.local_checksum_ticks, 0);
  assert.equal(row.checksumOwnership.active_native_records_submitted_before_export, 0);
  assert.equal(row.checksumOwnership.post_terminal_native_evidence_records, 1);
  assert.deepEqual(row.records, [run.record(0)]);
  await run.controller.close();
});
