// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {test} from 'node:test';
import {runBrowserInit, runBrowserSample, compareLane, rawLaneRecord} from './run_synctest.mjs';
import {
  applyCleanupVerdict, assertDrawDiagnosticsClear, checkedAsyncCcall,
  chromeUserDataDirectoryFromSpawnArgs, decodeDrawDiagnostics,
  DRAW_MOTION_BOUNDARY, inspectInteractiveObservation, requireDrawDiagnosticSize,
  validateSelectedDrawPath, validateSourceInputMapping,
  launchOwnedChromeServer, ownedProcessPresent, recordBrowserProcessInfo,
  validateAuroraFrame,
} from './runner_contract.mjs';

function observationFixture({ready = 1, ending = 0, complete = 0, paused = 0,
  motions = [20, 20], kinds = [42, 42], stocks = [4, 4], slots = [0, 1]} = {}) {
  const bytes = new Uint8Array(1280);
  const view = new DataView(bytes.buffer);
  view.setUint32(8, ready, true);
  view.setUint32(12, ending, true);
  view.setUint32(16, complete, true);
  view.setUint32(20, paused, true);
  for (let player = 0; player < 2; player++) {
    const base = 72 + 192 * player;
    view.setInt32(base + 20, motions[player], true);
    view.setUint32(base + 136, slots[player], true);
    view.setInt32(base + 148, stocks[player], true);
    view.setInt32(base + 152, kinds[player], true);
  }
  return bytes;
}

const copiedInputMapping = [
  {sourceSlot: 0, initializerPort: 0, rawController: 0},
  {sourceSlot: 1, initializerPort: 0, rawController: 1},
];

function diagnosticFixture({status = 0, callbacks = [], modelTransforms = [],
  dispatches = [], processedPadSlots = [], kinds = [], metadataSeenMask = 0} = {}) {
  const slotCount = 6;
  const bytes = new Uint8Array(12 + 20 * slotCount);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, slotCount, true);
  view.setUint32(4, status, true);
  const write = (values, offset, signed = false) => values.forEach((value, i) => {
    if (signed) view.setInt32(offset + 4 * i, value, true);
    else view.setUint32(offset + 4 * i, value, true);
  });
  write(callbacks, 8);
  write(modelTransforms, 8 + 4 * slotCount);
  write(dispatches, 8 + 8 * slotCount);
  write(processedPadSlots, 8 + 12 * slotCount, true);
  write(kinds, 8 + 16 * slotCount, true);
  view.setUint32(8 + 20 * slotCount, metadataSeenMask, true);
  return decodeDrawDiagnostics(bytes);
}

test('the captured SDL window assignment requires an exported writable runtime helper', () => {
  const fixture = JSON.parse(fs.readFileSync(
    new URL('./fixtures/sdl_module_descriptor.json', import.meta.url), 'utf8'));
  const makeContext = exported => {
    const calls = [];
    const Module = exported ? {requestFullscreen() {
      throw new Error('SDL must replace the exported helper');
    }} : {};
    const context = vm.createContext({Module, calls,
      abort(message) { throw new Error(message); },
      isExportedByForceFilesystem() { return false; },
      _requestFullscreenThroughSDL(pointer) { calls.push(pointer); }});
    vm.runInContext('"use strict"; ' + fixture.unexportedRuntimeSymbol +
      '; unexportedRuntimeSymbol("requestFullscreen");', context);
    return {Module, calls, context};
  };
  const missing = makeContext(false);
  assert.equal(typeof Object.getOwnPropertyDescriptor(missing.Module, 'requestFullscreen').get, 'function');
  assert.throws(() => vm.runInContext('"use strict"; (' +
    fixture.createWindowAssignment + ')(42);', missing.context),
  /Cannot set property requestFullscreen.*only a getter/);

  const exported = makeContext(true);
  vm.runInContext('"use strict"; (' + fixture.createWindowAssignment +
    ')(42); Module.requestFullscreen(false, false);', exported.context);
  assert.deepEqual(exported.calls, [42]);
  assert.equal(Object.getOwnPropertyDescriptor(exported.Module, 'requestFullscreen').writable, true);
});

test('Aurora draw validation requires positive per-frame work and new frame identities', () => {
  assert.deepEqual(validateAuroraFrame({drawCallCount: 2, lastBeginFrameId: 9, lastEndFrameId: 9}, 8, 8),
    {beginFrameId: 9, endFrameId: 9});
  assert.throws(() => validateAuroraFrame({drawCallCount: 0, lastBeginFrameId: 9, lastEndFrameId: 9}, 8, 8),
    /no Aurora draw calls/);
  assert.throws(() => validateAuroraFrame({drawCallCount: 2, lastBeginFrameId: 8, lastEndFrameId: 9}, 8, 8),
    /begin-frame identity/);
  assert.throws(() => validateAuroraFrame({drawCallCount: 2, lastBeginFrameId: 9, lastEndFrameId: 8}, 8, 8),
    /end-frame identity/);
});

test('observation boundary separates noninteractive states from tick comparison', () => {
  const classifyMotion = motion => ({101: 1, 102: 2, 103: 3}[motion] ??
    (motion < 0 ? 4 : 0));
  const inspect = bytes => inspectInteractiveObservation(bytes, {
    classifyMotion, isMarioKind: kind => kind === 42,
    slotCount: 6, expectedInputMapping: copiedInputMapping, expectedStocks: [4, 4],
  });
  const active = inspect(observationFixture());
  assert.equal(active.interactive, true);
  assert.deepEqual(active.refusalReasons, []);
  assert.deepEqual(active.players.map(player => player.sourceSlot), [0, 1]);
  assert.equal(inspect(observationFixture({ready: 0})).refusalReasons.includes('not-ready'), true);
  assert.equal(inspect(observationFixture({paused: 1})).refusalReasons.includes('paused'), true);
  assert.equal(inspect(observationFixture({ending: 1})).refusalReasons.includes('terminal'), true);
  assert.equal(inspect(observationFixture({motions: [101, 20]})).refusalReasons
    .includes('player-0-motion-entry'), true);
  assert.equal(inspect(observationFixture({motions: [102, 20]})).refusalReasons
    .includes('player-0-motion-death'), true);
  assert.equal(inspect(observationFixture({motions: [103, 20]})).refusalReasons
    .includes('player-0-motion-respawn'), true);
  assert.equal(inspect(observationFixture({motions: [-1, 20]})).refusalReasons
    .includes('player-0-motion-unknown'), true);
  assert.equal(inspect(observationFixture({kinds: [41, 42]})).refusalReasons
    .includes('player-0-unexpected-fighter-kind'), true);
  assert.equal(inspect(observationFixture({stocks: [3, 4]})).refusalReasons
    .includes('player-0-stocks-differ-from-initializer'), true);
  assert.equal(inspect(observationFixture({stocks: [0, 4]})).refusalReasons
    .includes('player-0-no-live-stock'), true);
  assert.throws(() => inspectInteractiveObservation(new Uint8Array(1279), {
    classifyMotion, isMarioKind: () => true, slotCount: 6,
    expectedInputMapping: copiedInputMapping, expectedStocks: [4, 4],
  }), /requires 1280 bytes/);
  const wrongOrder = inspect(observationFixture({slots: [1, 0]}));
  assert.equal(wrongOrder.interactive, false);
  assert.ok(wrongOrder.refusalReasons.includes('player-0-source-slot-differs-from-initializer'));
});

test('actual serialized init and sample callbacks execute in one fresh browser realm', async () => {
  const context = vm.createContext({URL, observationBytes: Array.from(observationFixture()),
    btoa: value => Buffer.from(value, 'binary').toString('base64'),
    performance: {now: () => 0}, setTimeout});
  vm.runInContext(`
    const heap = new Uint8Array(6400);
    heap.set(observationBytes, 16);
    const view = new DataView(heap.buffer);
    view.setUint32(5800, 6, true);
    const calls = [];
    const module = {
      HEAPU8: heap, FS: {mkdirTree: path => calls.push(path)},
      ccall: async name => {
        calls.push(name);
        if (name === 'melee_web_synctest_draw_present') {
          view.setUint32(5808, 1, true); view.setUint32(5812, 1, true);
          view.setUint32(5856, 1, true); view.setUint32(5860, 1, true);
          view.setInt32(5880, 0, true); view.setInt32(5884, 1, true);
          view.setInt32(5904, 42, true); view.setInt32(5908, 42, true);
          view.setUint32(5928, 3, true);
        }
        return 1;
      },
      UTF8ToString: pointer => pointer === 1 ? JSON.stringify({initialized:true,
        backend:7,rendererErrorCount:0,pendingFramePackets:0,pendingStagingBuffers:0,
        workerBusy:0,drawCallCount:2,lastBeginFrameId:1,lastEndFrameId:1}) :
        pointer === 3 ? JSON.stringify({players:[{controller:0,stocks:4},{controller:1,stocks:4}]}) : 'fixture',
      _melee_web_synctest_submission_json: () => 1,
      _melee_web_synctest_profile_identity: () => 2,
      _melee_web_snapshot_source_identity: () => 2,
      _melee_web_snapshot_rng_profile_identity: () => 2,
      _melee_web_snapshot_stage_kind_bridge_identity: () => 2,
      _melee_web_snapshot_initializer_json: () => 3,
      _melee_web_snapshot_initializer_diagnostic: () => 6000,
      _melee_web_snapshot_initializer_diagnostic_size: () => 16,
      _melee_web_snapshot_observation: () => 16,
      _melee_web_snapshot_observation_size: () => 1280,
      _melee_web_snapshot_pcm: () => 1400,
      _melee_web_snapshot_pcm_size: () => 4272,
      _melee_web_snapshot_input: () => 5680,
      _melee_web_snapshot_refresh_observation: () => 1,
      _melee_web_snapshot_observe: () => 1,
      _melee_web_synctest_draw_diagnostics: () => 5800,
      _melee_web_synctest_draw_diagnostics_size: () => 132,
      _melee_web_synctest_motion_boundary: () => 0,
      _melee_web_synctest_is_mario_kind: kind => kind === 42 ? 1 : 0,
      _melee_web_synctest_draw_decision: () => 1,
    };
    const window = {};
    const document = {querySelector: () => module};
    const navigator = {gpu: {}};
  `, context);
  const page = {evaluate: async (callback, arguments_) => {
    context.arguments_ = arguments_;
    return vm.runInContext(`(${callback.toString()})(arguments_)`, context, {
      importModuleDynamically: vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER,
    });
  }};
  // The factory returns a realm-local service fixture supplied as the canvas.
  // No application/GPU runtime is initialized; actual callback code is unchanged.
  const runtimeName = 'data:text/javascript,' + encodeURIComponent(
    'export default async options => options.canvas;');
  const init = await runBrowserInit(page, 'https://fixture.invalid/', runtimeName);
  assert.equal(Buffer.from(init.drawDiagnostics, 'base64').length, 132);
  const sample = await runBrowserSample(page, 300, copiedInputMapping, [4, 4]);
  assert.equal(sample.boundary.interactive, true);
  assert.deepEqual(Array.from(sample.boundary.players, row => row.stocks), [4, 4]);
  assert.equal(Buffer.from(sample.observation, 'base64').length, 1280);
  assert.equal(Buffer.from(sample.pcm, 'base64').length, 4272);
  assert.equal(Buffer.from(sample.pad, 'base64').length, 44);
  assert.deepEqual(decodeDrawDiagnostics(Buffer.from(sample.drawDiagnostics, 'base64'))
    .normalModelDispatches.slice(0, 2), [1, 1]);
  const missingPayload = {evaluate: (callback, arguments_) => {
    delete arguments_.drawDiagnosticBytes;
    return page.evaluate(callback, arguments_);
  }};
  await assert.rejects(runBrowserInit(missingPayload, 'https://fixture.invalid/', runtimeName),
    /Unsupported draw diagnostic ABI size/);
  await assert.rejects(runBrowserSample(missingPayload, 301, copiedInputMapping, [4, 4]),
    /Unsupported draw diagnostic ABI size/);
  const wrongStocks = await runBrowserSample(page, 301, copiedInputMapping, [5, 4]);
  assert.equal(wrongStocks.boundary.interactive, false);
  assert.ok(wrongStocks.boundary.refusalReasons.includes('invalid-stock-selection'));
});

test('diagnostic bytes and both selected per-tick draw paths are checked independently', () => {
  assert.deepEqual(validateSourceInputMapping(copiedInputMapping, 6), copiedInputMapping);
  assert.deepEqual(validateSourceInputMapping([
    {sourceSlot: 0, initializerPort: 0, rawController: 0},
    {sourceSlot: 1, initializerPort: 2, rawController: 1},
  ], 6), [
    {sourceSlot: 0, initializerPort: 0, rawController: 0},
    {sourceSlot: 1, initializerPort: 2, rawController: 1},
  ]);
  assert.throws(() => validateSourceInputMapping([
    {sourceSlot: 0, initializerPort: 0, rawController: 0},
    {sourceSlot: 1, initializerPort: 2, rawController: 0},
  ], 6), /initializer formula/);
  assert.throws(() => validateSourceInputMapping([
    {sourceSlot: 0, initializerPort: 0, rawController: 0},
    {sourceSlot: 1, initializerPort: 1, rawController: 0},
  ], 6), /does not preserve source slot/);
  const before = diagnosticFixture();
  assertDrawDiagnosticsClear(before, 'no-draw fixture');
  const after = diagnosticFixture({callbacks: [1, 1], modelTransforms: [0, 1],
    dispatches: [1, 1], processedPadSlots: [0, 1], kinds: [42, 42], metadataSeenMask: 3});
  const players = observationFixture();
  const state = inspectInteractiveObservation(players, {
    classifyMotion: () => 0, isMarioKind: kind => kind === 42,
    slotCount: after.slotCount, expectedInputMapping: copiedInputMapping, expectedStocks: [4, 4],
  });
  assert.deepEqual(validateSelectedDrawPath(before, after, state.players, copiedInputMapping), [
    {sourceSlot: 0, processedPadSlot: 0, rawController: 0, initializerPort: 0,
      fighterKind: 42, callbackDelta: 1,
      normalModelDispatchDelta: 1, modelTransformDelta: 0},
    {sourceSlot: 1, processedPadSlot: 1, rawController: 1, initializerPort: 0,
      fighterKind: 42, callbackDelta: 1,
      normalModelDispatchDelta: 1, modelTransformDelta: 1},
  ]);
  assert.equal(after.processedPadSlots[0], 0);
  assert.equal(after.processedPadSlots[1], 1);
  assert.throws(() => validateSourceInputMapping([
    {sourceSlot: 0, initializerPort: 0, rawController: 1},
    {sourceSlot: 1, initializerPort: 0, rawController: 0},
  ], 6), /initializer formula/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 1], dispatches: [1, 1],
      processedPadSlots: [1, 0], kinds: [42, 42], metadataSeenMask: 3}),
    state.players, copiedInputMapping), /Processed PAD slot does not match source slot/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 1], dispatches: [1, 1],
      processedPadSlots: [0, 0], kinds: [42, 42], metadataSeenMask: 3}),
    state.players, copiedInputMapping), /Processed PAD slot does not match source slot/);
  assert.throws(() => decodeDrawDiagnostics(new Uint8Array(131)), /ABI is invalid/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 1], dispatches: [1, 0], processedPadSlots: [0, 1],
      kinds: [42, 42], metadataSeenMask: 3}), state.players, copiedInputMapping),
  /Normal fighter model dispatch/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 0], dispatches: [1, 1], processedPadSlots: [0, 1],
      kinds: [42, 42], metadataSeenMask: 3}), state.players, copiedInputMapping),
  /Fighter callback entry/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 1], dispatches: [1, 1], processedPadSlots: [0, 1],
      kinds: [42, 42], metadataSeenMask: 3}), state.players,
    [{sourceSlot: 0, initializerPort: 0, rawController: 1},
      {sourceSlot: 1, initializerPort: 0, rawController: 0}]),
  /initializer formula/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({callbacks: [1, 1], dispatches: [1, 1], processedPadSlots: [0, 1],
      kinds: [42, 42], metadataSeenMask: 1}), state.players, copiedInputMapping),
  /metadata for selected slot 1/);
  assert.throws(() => validateSelectedDrawPath(before,
    diagnosticFixture({status: 1, callbacks: [1, 1], dispatches: [1, 1],
      processedPadSlots: [0, 1], kinds: [42, 42], metadataSeenMask: 3}), state.players, copiedInputMapping),
  /status is nonzero/);
  assert.throws(() => assertDrawDiagnosticsClear(after, 'no-draw lane'),
    /unexpectedly recorded a source draw/);
});

test('cleanup failures change a passing live-run verdict to failed', () => {
  const clean = {result: 'passed'};
  assert.equal(applyCleanupVerdict(clean, []), 'passed');
  const failed = {result: 'passed'};
  assert.equal(applyCleanupVerdict(failed, [{step: 'chrome-close'}]), 'failed');
  assert.deepEqual(failed.failure.cleanupFailures, [{step: 'chrome-close'}]);
});

test('browser native call uses Asyncify ccall and waits for completion', async () => {
  let completed = false;
  const module = {
    ccall(identifier, returnType, argumentTypes, arguments_, options) {
      assert.equal(identifier, 'melee_web_synctest_draw_present');
      assert.equal(returnType, 'number');
      assert.deepEqual(argumentTypes, []);
      assert.deepEqual(arguments_, []);
      assert.deepEqual(options, {async: true});
      return new Promise(resolve => setImmediate(() => { completed = true; resolve(1); }));
    },
    UTF8ToString: () => 'native failure',
    _melee_web_snapshot_error: () => 1,
  };
  const call = checkedAsyncCcall(module, 'draw', 'melee_web_synctest_draw_present');
  assert.equal(completed, false, 'The async native call completed before the runner awaited it');
  assert.equal(await call, 1);
  assert.equal(completed, true, 'The runner returned before native completion');
});

test('owned-process query recognizes this exact running process', () => {
  assert.equal(ownedProcessPresent(process.pid), true);
  assert.throws(() => ownedProcessPresent(-1), /Invalid owned process PID/);
});

test('owned Chrome launch uses Playwright launchServer without an unsupported profile override', async () => {
  let receivedOptions = null;
  const child = {pid: 4101, spawnargs: ['/Chrome', '--headless', '--user-data-dir=/tmp/owned-profile']};
  const server = {process: () => child, close: async () => {}};
  const chromium = {
    async launchServer(options) { receivedOptions = options; return server; },
    async launchPersistentContext() { throw new Error('persistent-context fallback should not run'); },
  };
  const launchOptions = {headless: true, args: ['--mute-audio']};
  assert.deepEqual(await launchOwnedChromeServer(chromium, launchOptions), {server, child});
  assert.equal(receivedOptions, launchOptions);
  assert.equal(chromeUserDataDirectoryFromSpawnArgs(child.spawnargs), '/tmp/owned-profile');
});

test('the automatic Chrome profile is read from the owned child command line', () => {
  assert.equal(chromeUserDataDirectoryFromSpawnArgs([
    '/Applications/Google Chrome', '--user-data-dir=/Volumes/scratch/chrome-profile',
  ]), '/Volumes/scratch/chrome-profile');
  assert.equal(chromeUserDataDirectoryFromSpawnArgs([
    '/Applications/Google Chrome', '--user-data-dir', '/Volumes/scratch/second-profile',
  ]), '/Volumes/scratch/second-profile');
  assert.throws(() => chromeUserDataDirectoryFromSpawnArgs(['/Applications/Google Chrome']), /no --user-data-dir/);
});

test('CDP process attribution is stored in the capture-owner top-level field', () => {
  const rows = [{id: 4101, type: 'browser'}, {id: 4102, type: 'renderer'}];
  const report = {browser_process_info: []};
  assert.deepEqual(recordBrowserProcessInfo(report, rows), {browserPid: 4101, processIds: [4101, 4102]});
  assert.deepEqual(report.browser_process_info, rows);
  assert.equal(report.browser_process_info_source, 'Chrome CDP SystemInfo.getProcessInfo');
  assert.throws(() => recordBrowserProcessInfo({}, []), /empty or malformed/);
  assert.throws(() => recordBrowserProcessInfo({}, [{id: 4101, type: 'renderer'}]), /no browser process/);
});

test('owned launch rejects an explicit user-data-dir before starting Chrome', async () => {
  let launched = false;
  await assert.rejects(launchOwnedChromeServer({
    async launchServer() { launched = true; throw new Error('unexpected launch'); },
  }, {args: ['--user-data-dir=/tmp/unsupported']}), /Do not pass --user-data-dir/);
  assert.equal(launched, false);
});


test('actual lane comparator refuses either interval sample and preserves exact raw domains', () => {
  const expected = {observation: Buffer.from(observationFixture()),
    pcm: Buffer.alloc(4272), pad: Buffer.alloc(44)};
  expected.observation.writeUInt32LE(533, 32);
  expected.observation.writeUInt32LE(20, 36);
  const raw = rawLaneRecord(expected);
  assert.equal(raw.pcmFrames, 533);
  assert.equal(raw.audioPhase, 20);
  for (const [field, encoded] of [['observation', 'observationBase64'],
    ['pcm', 'pcmBase64'], ['pad', 'rawPad44Base64']]) {
    assert.ok(Buffer.from(raw[encoded], 'base64').equals(expected[field]));
  }
  for (const sample of [300, 301]) {
    compareLane(sample, 'control', expected, expected);
    for (const field of ['observation', 'pcm', 'pad']) {
      const changed = {...expected, [field]: Buffer.from(expected[field])};
      changed[field][changed[field].length - 1] ^= 1;
      assert.throws(() => compareLane(sample, 'control', expected, changed), error =>
        error.synctestFailure.sample === sample && error.synctestFailure.field === field);
    }
  }
  // Raw evidence still exists when a later frame-count/boundary guard refuses.
  expected.observation.writeUInt32LE(535, 32);
  assert.equal(rawLaneRecord(expected).pcmFrames, 535);
});
