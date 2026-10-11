import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {describeLockstepTransport, describeLockstepTransportAttempt} from '../scripts/net_lockstep_transport.mjs';

const source = await readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8');
function section(startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start);
  assert(start >= 0 && end > start);
  return source.slice(start, end);
}
function sectionThrough(startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start);
  assert(start >= 0 && end > start);
  return source.slice(start, end + endMarker.length);
}

for (const peerTransport of ['relay', 'webrtc']) {
  test(`runtime ${peerTransport} metadata uses the endpoint configured from its actual Worker`, async () => {
    const configured = [];
    let workerStarts = 0;
    const transport = peerTransport === 'relay'
      ? {type: 'room-websocket', peer_limit: 2}
      : {type: 'webrtc-datachannel', reliable: true, ordered: true};
    const context = {roomRuntime: null, runtimeRelayUrl: undefined, output: '/unused',
      path: {join: (...parts) => parts.join('/')}, createRoomId: () => 'test-room',
      startRoomRelayRuntime: async () => {
        ++workerStarts;
        return {base: 'http://127.0.0.1:61725', port: 61725,
          runtimeIdentity: {worker: 'fixture'}, hashes: {worker: 'a'.repeat(64)}};
      },
      describeLockstepTransportAttempt,
      instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {
        async configureRuntimeLockstep(options) { configured.push(options); },
        async installSourceAccounting() {}, async freezeLoadedWasmIdentity() { return {sha256: 'b'.repeat(64)}; },
      }])), peerTransport, stallMs: 1000, deadline: Date.now() + 10000,
      localWebRtc: peerTransport === 'webrtc', roomWorkerSignaling: true, explicitRuntimeRelay: false,
      runtimeOwned: true, values: {}, instanceRows: {alpha: {}, beta: {}},
      relay: {transport}, pairResults: {}, describeLockstepTransport};
    await vm.runInNewContext(`(async()=>{${section(
      '  if (runtimeOwned) {\n    const roomId = createRoomId();',
      '    // Observe the live native timing callback')}\n  }\n})()`, context);
    const call = section('    pairResults.transport = describeLockstepTransport(relay, {relayUrl: runtimeRelayUrl});',
      '    if (localWebRtc) {');
    vm.runInNewContext(call, context);
    assert.equal(workerStarts, 1, 'the no-URL path keeps the current local Worker default');
    assert.equal(configured.length, 2);
    assert(configured.every(row => row.url === 'ws://127.0.0.1:61725'));
    assert.equal(context.pairResults.transport.type, transport.type);
    if (peerTransport === 'relay') {
      assert.equal(context.pairResults.transport.endpoint_origin, configured[0].url);
      assert.equal(Object.hasOwn(context.pairResults.transport, 'alpha_to_beta_bytes'), false);
    } else assert.deepEqual(context.pairResults.transport, transport);
  });
}

test('runtime explicit relay URL bypasses local Worker setup and keeps the selected endpoint in metadata', async () => {
  const relayUrl = 'wss://rooms.example';
  const configured = [];
  let workerStarts = 0;
  const context = {roomRuntime: null, runtimeRelayUrl: undefined, output: '/unused',
    path: {join: (...parts) => parts.join('/')}, createRoomId: () => 'test-room',
    startRoomRelayRuntime: async () => { ++workerStarts; throw Error('must not start Miniflare'); },
    describeLockstepTransportAttempt,
    instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {
      async configureRuntimeLockstep(options) { configured.push(options); },
      async installSourceAccounting() {}, async freezeLoadedWasmIdentity() { return {sha256: 'b'.repeat(64)}; },
    }])), peerTransport: 'relay', stallMs: 1000, deadline: Date.now() + 10000,
    localWebRtc: false, roomWorkerSignaling: false, explicitRuntimeRelay: true, runtimeOwned: true,
    values: {'relay-url': relayUrl}, instanceRows: {alpha: {}, beta: {}},
    relay: {transport: {type: 'room-websocket', peer_limit: 2}}, pairResults: {}, describeLockstepTransport};
  await vm.runInNewContext(`(async()=>{${section(
    '  if (runtimeOwned) {\n    const roomId = createRoomId();',
    '    // Observe the live native timing callback')}\n  }\n})()`, context);
  assert.equal(workerStarts, 0);
  assert.equal(context.roomRuntime, null);
  assert.equal(context.runtimeRelayUrl, relayUrl, 'no local endpoint substitution occurs');
  assert.deepEqual(configured.map(row => [row.role, row.url, row.transport]), [
    ['alpha', relayUrl, 'relay'], ['beta', relayUrl, 'relay'],
  ]);
  assert.deepEqual(JSON.parse(JSON.stringify(context.pairResults.runtime_session_start.relay_endpoint)),
    {selection: '--relay-url', endpoint_origin: 'wss://rooms.example'});

  vm.runInNewContext(section('    pairResults.transport = describeLockstepTransport(relay, {relayUrl: runtimeRelayUrl});',
    '    if (localWebRtc) {'), context);
  assert.equal(context.pairResults.transport.type, 'room-websocket');
  assert.equal(context.pairResults.transport.endpoint_origin, 'wss://rooms.example');
  assert.equal(Object.hasOwn(context.pairResults.transport, 'alpha_to_beta_bytes'), false);

  context.peers = {alpha: {transport: {type: 'room-websocket'}}, beta: {transport: {type: 'room-websocket'}}};
  vm.runInNewContext(sectionThrough('      pairResults.runtime_room_relay = {selection: explicitRuntimeRelay ?',
    '        relay_payloads_exposed_to_node: false};'), context);
  assert.equal(context.pairResults.runtime_room_relay.selection,
    'explicit --relay-url RoomRelay WebSocket endpoint');
  assert.equal(context.pairResults.runtime_room_relay.relay_endpoint.endpoint_origin, 'wss://rooms.example');
});

test('runtime explicit relay configuration failure propagates without local Worker fallback', async () => {
  const relayUrl = 'wss://rooms.example';
  const failure = Error('explicit relay configuration rejected');
  const configured = [];
  let workerStarts = 0;
  const context = {roomRuntime: null, runtimeRelayUrl: undefined, output: '/unused',
    path: {join: (...parts) => parts.join('/')}, createRoomId: () => 'test-room',
    startRoomRelayRuntime: async () => { ++workerStarts; throw Error('must not start Miniflare'); },
    describeLockstepTransportAttempt,
    instances: Object.fromEntries(['alpha', 'beta'].map(role => [role, {
      async configureRuntimeLockstep(options) {
        configured.push(options);
        if (role === 'alpha') throw failure;
      },
      async installSourceAccounting() {}, async freezeLoadedWasmIdentity() { return {sha256: 'b'.repeat(64)}; },
    }])), peerTransport: 'relay', stallMs: 1000, deadline: Date.now() + 10000,
    localWebRtc: false, roomWorkerSignaling: false, explicitRuntimeRelay: true, runtimeOwned: true,
    values: {'relay-url': relayUrl}, instanceRows: {alpha: {}, beta: {}},
    relay: {transport: {type: 'room-websocket', peer_limit: 2}}, pairResults: {}, describeLockstepTransport};
  await assert.rejects(vm.runInNewContext(`(async()=>{${section(
    '  if (runtimeOwned) {\n    const roomId = createRoomId();',
    '    // Observe the live native timing callback')}\n  }\n})()`, context), error => error === failure);
  assert.equal(workerStarts, 0, 'a failed explicit endpoint never starts the local Worker');
  assert.equal(context.roomRuntime, null);
  assert.equal(context.runtimeRelayUrl, relayUrl, 'the failed URL is not replaced by a local endpoint');
  assert.deepEqual(configured.map(row => [row.role, row.url, row.transport]), [
    ['alpha', relayUrl, 'relay'], ['beta', relayUrl, 'relay'],
  ]);
});

test('failed run retains Worker passed=false even when disposal and process cleanup complete', async () => {
  let closeRequested;
  const context = {runPassed: false, closeNotes: [], pairResults: {outcome: 'fail', first_error: 'sentinel'},
    roomRuntime: {async close(passed) {
      closeRequested = passed;
      return {passed: false, ready: true, disposed: true, groupAlive: false, fallbackUsed: false,
        ownedTempPathsClean: true, exit: {code: 0, signal: null}, processClose: {code: 0, signal: null}};
    }}};
  await vm.runInNewContext(`(async()=>{${section('  if (roomRuntime) {\n    try {\n      const cleanup =',
    '  // TCP close events may admit callbacks')}\n})()`, context);
  assert.equal(closeRequested, false);
  assert.equal(context.pairResults.room_worker_cleanup.passed, false);
  assert.equal(context.pairResults.room_worker_cleanup.disposed, true);
  assert.equal(context.pairResults.room_worker_cleanup.group_alive, false);
  assert.equal(context.pairResults.room_worker_cleanup.owned_temp_paths_clean, true);
  assert.equal(context.pairResults.first_error, 'sentinel');
  assert.equal(context.closeNotes.length, 1);
});
