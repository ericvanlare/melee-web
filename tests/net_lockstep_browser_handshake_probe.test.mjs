import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {EventEmitter} from 'node:events';
import {test} from 'node:test';
import {runInNewContext} from 'node:vm';
import {
  assertFinalObservation,
  assertNeutralBatch,
  assertPreCloseObservation,
  assertReadySnapshot,
  armBrowserBeforeNodeClose,
  awaitOwnedAcquisition,
  captureBrowserResponse,
  capturePreCloseObservation,
  createTerminationRequest,
  EXPECTED_AGREEMENT_JSON,
  FIXTURE_HTML,
  runCleanupStages,
  validateAgreement,
  validateBrowserStartOptions,
  validateBrowserProcessInfo,
  validateBrowserResponseUrl,
  validateExpectedNeutralFrames,
  validateResponseRecord,
  validateServedHashInventory,
} from '../scripts/net_lockstep_browser_handshake_probe.mjs';

const agreement = {schema: 'a3-browser-owned-handshake-v1', source_ticks: 2,
  input_ticks: 0, delay: 2, seed: 201};
const agreementHash = '04e0100de1e0f847136cb6148aa1615b7dc528175edb0e34a37b82fb15d09519';

function readySnapshot(role) {
  const remoteRole = role === 'alpha' ? 'beta' : 'alpha';
  const localPort = role === 'alpha' ? 0 : 1;
  const remotePort = role === 'alpha' ? 1 : 0;
  return {
    role, ready: true, localHello: {version: 1, role, local_port: localPort,
      remote_port: remotePort, agreement},
    remoteHello: {version: 1, role: remoteRole, local_port: remotePort,
      remote_port: localPort, agreement}, agreementHash, terminal: null,
    peerSummary: {ready: true, start_identity_hash: agreementHash,
      local_input_ticks: 0, local_checksum_ticks: 0, next_source_frame: 2},
    terminals: [], unexpectedTerminalBeforeClose: false,
    endpointErrors: [], endpointErrorsFromApi: [], sentTypes: ['hello'], receivedTypes: ['hello'],
    frameBatches: 1, framesEmitted: 2, nativeFramesExecuted: 0,
    inputCalls: 0, checksumCalls: 0, nodeGlobalsAbsent: true,
  };
}

test('probe freezes the pre-portable two-frame contract and exact start identity', () => {
  assert.equal(validateExpectedNeutralFrames(2, 2), 2);
  assert.throws(() => validateExpectedNeutralFrames(1, 1), /exactly 2 source frames/);
  assert.throws(() => validateExpectedNeutralFrames(2, 1), /exactly 2 source frames/);
  assert.throws(() => validateExpectedNeutralFrames(3, 3), /exactly 2 source frames/);
  assert.doesNotThrow(() => validateAgreement(agreement));
  assert.throws(() => validateAgreement({...agreement, seed: 202}), /agreement or independently frozen/);
  assert.throws(() => validateAgreement(agreement, '0'.repeat(64)), /agreement or independently frozen/);
  assert.deepEqual(validateBrowserStartOptions({expectedFrames: 2, agreement}, 2, EXPECTED_AGREEMENT_JSON),
    {expectedFrames: 2, agreement});
  assert.throws(() => validateBrowserStartOptions({agreement}, 2, EXPECTED_AGREEMENT_JSON), /before allocating/);
  assert.throws(() => validateBrowserStartOptions({expectedFrames: 3, agreement}, 2, EXPECTED_AGREEMENT_JSON),
    /before allocating/);
});

test('neutral callback validator checks the exact two-frame PAD bytes', () => {
  const zeroPad = new Uint8Array(11);
  const noControllerPad = new Uint8Array([...new Uint8Array(10), 0xff]);
  const expected = new Uint8Array(88);
  for (let frame = 0; frame < 2; ++frame) {
    const base = frame * 44;
    expected.set(zeroPad, base);
    expected.set(zeroPad, base + 11);
    expected.set(noControllerPad, base + 22);
    expected.set(noControllerPad, base + 33);
  }
  assert.doesNotThrow(() => assertNeutralBatch(0, expected, 2));
  assert.throws(() => assertNeutralBatch(1, expected, 2), /Unexpected neutral prefix boundary/);
  const altered = expected.slice();
  altered[43] = 0;
  assert.throws(() => assertNeutralBatch(0, altered, 2), /No-controller PAD byte differs/);
  assert.throws(() => assertNeutralBatch(0, expected.subarray(0, 44), 2), /Unexpected neutral prefix boundary/);
});

test('browser fixture rejects missing or wrong frame counts before peer or endpoint allocation', async () => {
  const moduleScript = FIXTURE_HTML.match(/<script type="module">([\s\S]*?)<\/script>/)?.[1];
  assert.ok(moduleScript, 'fixture must contain one executable module script');
  const source = moduleScript.replace(/^\s*import .*;\s*$/gm, '');
  let peerAllocations = 0;
  let endpointAllocations = 0;
  const sandbox = {
    window: {},
    document: {querySelector: () => ({textContent: ''})},
    lockstepConstants: {delay: 2, neutralPad: '0000000000000000000000',
      noControllerPad: '00000000000000000000ff'},
    LockstepPeer: class { constructor() { ++peerAllocations; } },
    createRoomRelayPeerEndpoint: () => { ++endpointAllocations; },
  };
  runInNewContext(source, sandbox);
  await assert.rejects(sandbox.window.beginBrowserAlpha({agreement, relayBase: 'ws://127.0.0.1:1',
    roomId: '0123456789abcdef', timeoutMs: 1000}), /before allocating/);
  await assert.rejects(sandbox.window.beginBrowserAlpha({agreement, expectedFrames: 1,
    relayBase: 'ws://127.0.0.1:1', roomId: '0123456789abcdef', timeoutMs: 1000}), /before allocating/);
  assert.equal(peerAllocations, 0);
  assert.equal(endpointAllocations, 0);
});

test('served manifest requires every staged file hash and rejects response drift', () => {
  const served = {files: ['net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs', 'probe.html'],
    hashes: { 'net_lockstep_core.mjs': 'a'.repeat(64),
      'net_lockstep_websocket_relay.mjs': 'b'.repeat(64), 'probe.html': 'c'.repeat(64) }};
  assert.doesNotThrow(() => validateServedHashInventory(served));
  assert.throws(() => validateServedHashInventory({...served, hashes: {}}), /exactly these served files/);
  assert.throws(() => validateServedHashInventory({...served, hashes: {...served.hashes, extra: 'd'.repeat(64)}}),
    /exactly these served files/);
  const row = {url: 'http://127.0.0.1:8787/probe.html', status: 200,
    headers: {'cross-origin-opener-policy': 'same-origin',
      'cross-origin-embedder-policy': 'require-corp'}, bodyHash: 'c'.repeat(64)};
  assert.doesNotThrow(() => validateResponseRecord(row, row.url, 'c'.repeat(64)));
  assert.throws(() => validateResponseRecord({...row, url: 'https://example.invalid/probe.html'},
    row.url, 'c'.repeat(64)), /identity or bytes differ/);
  assert.throws(() => validateResponseRecord({...row, bodyHash: 'd'.repeat(64)},
    row.url, 'c'.repeat(64)), /identity or bytes differ/);
  assert.throws(() => validateResponseRecord({...row, headers: {...row.headers,
    'cross-origin-embedder-policy': null}}, row.url, 'c'.repeat(64)), /missing required/);
  assert.equal(validateBrowserResponseUrl(row.url, 'http://127.0.0.1:8787/'), 'fixture');
  assert.equal(validateBrowserResponseUrl('ws://127.0.0.1:49152/v1/socket',
    'http://127.0.0.1:8787/'), 'loopback-websocket');
  assert.equal(validateBrowserResponseUrl('data:,', 'http://127.0.0.1:8787/'), 'non-network');
  assert.throws(() => validateBrowserResponseUrl('https://example.invalid/module.mjs',
    'http://127.0.0.1:8787/'), /outside the frozen loopback origins/);
  assert.throws(() => validateBrowserResponseUrl('http://127.0.0.1:8788/other',
    'http://127.0.0.1:8787/'), /outside the frozen loopback origins/);
  assert.deepEqual(validateBrowserProcessInfo([{id: 12, type: 'browser'}, {id: 13, type: 'renderer'}]),
    [{id: 12, type: 'browser'}, {id: 13, type: 'renderer'}]);
  assert.throws(() => validateBrowserProcessInfo([]), /nonempty typed positive PID rows/);
  assert.throws(() => validateBrowserProcessInfo([{id: 0, type: 'browser'}]), /nonempty typed positive PID rows/);
  assert.throws(() => validateBrowserProcessInfo([{id: 12, type: ''}]), /nonempty typed positive PID rows/);
  assert.throws(() => validateBrowserProcessInfo([{id: 12, type: 'browser'},
    {id: 12, type: 'renderer'}]), /duplicate PIDs/);
});

test('real browser response capture reads fixture bytes and headers and rejects resource drift', async () => {
  const origin = 'http://127.0.0.1:8787/';
  for (const name of ['probe.html', 'net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs']) {
    const url = new URL(name, origin).href;
    const body = Buffer.from(`exact fixture bytes for ${name}\n`);
    const bodyHash = createHash('sha256').update(body).digest('hex');
    const calls = [];
    const response = {
      url: () => url,
      status: () => 200,
      body: async () => { calls.push('body'); return body; },
      allHeaders: async () => {
        calls.push('headers');
        return {'Cross-Origin-Opener-Policy': 'same-origin',
          'Cross-Origin-Embedder-Policy': 'require-corp'};
      },
    };
    const row = await captureBrowserResponse(response, origin);
    assert.deepEqual(row, {url, status: 200, headers: {
      'cross-origin-opener-policy': 'same-origin',
      'cross-origin-embedder-policy': 'require-corp'}, bytes: body.byteLength, bodyHash});
    assert.deepEqual(calls, ['body', 'headers']);
    assert.doesNotThrow(() => validateResponseRecord(row, url, bodyHash));
    const changedBytes = await captureBrowserResponse({...response,
      body: async () => Buffer.from('changed bytes')}, origin);
    assert.throws(() => validateResponseRecord(changedBytes, url, bodyHash), /identity or bytes differ/);
    const missingHeader = await captureBrowserResponse({...response,
      allHeaders: async () => ({'Cross-Origin-Opener-Policy': 'same-origin'})}, origin);
    assert.throws(() => validateResponseRecord(missingHeader, url, bodyHash), /missing required/);
    const badStatus = await captureBrowserResponse({...response, status: () => 404}, origin);
    assert.throws(() => validateResponseRecord(badStatus, url, bodyHash), /identity or bytes differ/);
    const queryDrift = await captureBrowserResponse({...response, url: () => `${url}?different=1`}, origin);
    assert.throws(() => validateResponseRecord(queryDrift, url, bodyHash), /identity or bytes differ/);
    await assert.rejects(captureBrowserResponse({...response,
      body: async () => { throw Error('response body unavailable'); }}, origin), /body unavailable/);
  }
  const unexpected = {url: () => `${origin}unexpected.mjs`,
    body: async () => { throw Error('must not read unexpected body'); }};
  await assert.rejects(captureBrowserResponse(unexpected, origin), /unexpected fixture resource/);
  await assert.rejects(captureBrowserResponse({...unexpected,
    url: () => 'https://example.invalid/probe.html'}, origin), /outside the frozen loopback origins/);
  assert.equal(await captureBrowserResponse({...unexpected, url: () => 'data:,'}, origin), null);
  assert.equal(await captureBrowserResponse({...unexpected,
    url: () => 'ws://127.0.0.1:49152/v1/socket'}, origin), null);
});

test('READY snapshots require complementary pinned hellos and reject late packets or batches', async () => {
  const alpha = readySnapshot('alpha');
  const beta = readySnapshot('beta');
  assert.doesNotThrow(() => assertPreCloseObservation({browserSnapshot: alpha,
    nodeSnapshot: beta, pageErrors: [], expectedFrames: 2}));
  assert.throws(() => assertReadySnapshot({...alpha, remoteHello: {...alpha.remoteHello,
    agreement: {...agreement, seed: 200}}}, 'alpha', 'beta', 2), /exact complementary/);
  assert.throws(() => assertReadySnapshot({...alpha, receivedTypes: ['hello', 'state']},
    'alpha', 'beta', 2), /non-hello A2 packet/);
  assert.throws(() => assertFinalObservation({...alpha, frameBatches: 2}, 'alpha', 2),
    /neutral-prefix observation/);

  const screenshotErrors = [];
  await assert.rejects(capturePreCloseObservation({
    screenshot: async () => { screenshotErrors.push('console error during screenshot'); },
    readBrowserSnapshot: async () => alpha,
    readNodeSnapshot: async () => beta,
    pageErrors: screenshotErrors,
    expectedFrames: 2,
  }), /errors before intentional close/);

  await assert.rejects(capturePreCloseObservation({
    screenshot: async () => {},
    readBrowserSnapshot: async () => ({...alpha, sentTypes: ['hello', 'state']}),
    readNodeSnapshot: async () => beta,
    pageErrors: [],
    expectedFrames: 2,
  }), /non-hello A2 packet/);
});

test('SIGTERM during an owned acquisition keeps and assigns the late resource before failing', async () => {
  const emitter = new EventEmitter();
  const events = [];
  const termination = createTerminationRequest(() => events.push('termination-requested'), emitter);
  let resolveAcquisition;
  const acquisition = new Promise(resolve => { resolveAcquisition = resolve; });
  let owned = null;
  const acquisitionResult = awaitOwnedAcquisition(acquisition, resource => { owned = resource; }, termination);
  const rejected = assert.rejects(acquisitionResult, /cooperative SIGTERM cleanup/);
  emitter.emit('SIGTERM');
  const resource = {close() { events.push('resource-closed'); }};
  resolveAcquisition(resource);
  await rejected;
  assert.equal(owned, resource);
  assert.deepEqual(events, ['termination-requested']);
  assert.throws(() => termination.throwIfRequested(), /cooperative SIGTERM cleanup/);
  termination.close();
  assert.equal(emitter.listenerCount('SIGTERM'), 0);
});

test('non-acquiring waits are interrupted by the cooperative termination request', async () => {
  const emitter = new EventEmitter();
  const termination = createTerminationRequest(() => {}, emitter);
  const waiting = termination.wait(new Promise(() => {}), 10000, 'stub wait');
  const rejected = assert.rejects(waiting, /cooperative SIGTERM cleanup/);
  emitter.emit('SIGTERM');
  await rejected;
  termination.close();
});

test('browser close arm is acknowledged before Node intent and verification failures remain visible', async () => {
  const events = [];
  const result = await armBrowserBeforeNodeClose({
    verifyBeforeArm: async () => { events.push('verify-snapshot'); throw Error('late protocol packet'); },
    armBrowser: async () => { events.push('browser-ack'); return {intentionalClose: true}; },
    markNodeCloseIntentional: () => events.push('node-intent'),
    onAcknowledged: () => events.push('ack-recorded'),
  });
  assert.equal(result.acknowledged, true);
  assert.match(result.verificationError.message, /late protocol packet/);
  assert.equal(result.armError, null);
  assert.deepEqual(events, ['verify-snapshot', 'browser-ack', 'node-intent', 'ack-recorded']);

  const failedArmEvents = [];
  const failedArm = await armBrowserBeforeNodeClose({
    verifyBeforeArm: async () => failedArmEvents.push('verify'),
    armBrowser: async () => { failedArmEvents.push('arm-failed'); throw Error('no browser ack'); },
    markNodeCloseIntentional: () => failedArmEvents.push('node-intent'),
  });
  assert.equal(failedArm.acknowledged, false);
  assert.match(failedArm.armError.message, /no browser ack/);
  assert.deepEqual(failedArmEvents, ['verify', 'arm-failed']);
});

test('cleanup stages run in order and aggregate failures without skipping later owners', async () => {
  const events = [];
  const errors = await runCleanupStages([
    {name: 'endpoint', run: async () => { events.push('endpoint'); throw Error('close rejected'); }},
    {name: 'page', run: async () => { events.push('page'); }},
    {name: 'Worker', run: async () => { events.push('Worker'); }},
  ]);
  assert.deepEqual(events, ['endpoint', 'page', 'Worker']);
  assert.equal(errors.length, 1);
  assert.match(errors[0], /^endpoint: Error: close rejected/);
});
