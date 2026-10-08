import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {createDevelopmentLockstepOwner} from '../web/net_lockstep_development_owner.mjs';

const config = Object.freeze({role: 'alpha', roomId: 'r'.repeat(32), timeoutMs: 500,
  url: 'ws://127.0.0.1:8787'});
const identity = Object.freeze({algorithm: 'sha256', wasm: 'a'.repeat(64), disc: {sha256: 'b'.repeat(64)}});
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
};

function fixture({context = {}, identityPromise = Promise.resolve(identity), startFailure = null,
  closeFailure = null} = {}) {
  const state = {ready: true, fatal: false, bundle: true, replayActive: false,
    ownerState: 'prepared', phase: 1, ...context};
  const events = [];
  let entry;
  const session = {
    start(buildAgreement) {
      events.push(['session-start']);
      const agreement = buildAgreement({recorded: 1, required: 1});
      events.push(['agreement', agreement]);
      return Promise.resolve({protocol: {ready: true}});
    },
    onFrame() { events.push(['frame']); },
    async close(options) { events.push(['session-close', options.mode]); if (closeFailure) throw closeFailure; },
    snapshot() { return {closing: false}; },
  };
  const owner = {
    attachNetworkSession(close) { events.push(['attach']); this.close = close; },
    stop(error) { events.push(['stop', error]); },
  };
  entry = createDevelopmentLockstepOwner({Module: {}, owner, inputDelay: 2, getContext: () => state,
    getIdentity: async () => { events.push(['identity']); return identityPromise; },
    createSession(options) { events.push(['create-session', options]); return session; },
    async runNativeStart(options) {
      events.push(['native-begin', options]);
      if (startFailure) throw startFailure;
      entry.onFrame();
    }});
  return {entry, events, state, owner};
}

test('development entry binds fresh CSS, identity, adapter session and native start in order', async () => {
  const run = fixture();
  assert.equal(run.entry.configure(config), true);
  assert.throws(() => run.entry.configure(config), /unavailable or already owned/);
  const ready = await run.entry.begin(17, 8);
  assert.deepEqual(run.events.map(row => row[0]), [
    'identity', 'create-session', 'attach', 'session-start', 'agreement', 'native-begin', 'frame',
  ]);
  assert.equal(ready.protocol.ready, true);
  assert.equal(run.events.find(row => row[0] === 'native-begin')[1].sourceTicks, 8);
  const agreement = run.events.find(row => row[0] === 'agreement')[1];
  assert.equal(agreement.protocol, 'melee-web-local-lockstep-a2-v1');
  assert.equal(agreement.input_delay, 2);
  assert.equal(agreement.input_source, 'browser-local-native-PADStatus');
  assert.equal(agreement.runtime_wasm_sha256, identity.wasm);
  assert.equal(run.entry.state().used, true);
});

test('development entry rejects invalid context before identity or native work', async () => {
  for (const context of [{ownerState: 'css'}, {phase: 3}, {fatal: true}, {replayActive: true}, {bundle: false}]) {
    const run = fixture({context});
    assert.throws(() => run.entry.configure(config), /Runtime lockstep/);
    assert.deepEqual(run.events, []);
  }
  const run = fixture();
  run.entry.configure(config);
  run.state.phase = 3;
  await assert.rejects(run.entry.begin(1, 8), /Runtime lockstep/);
  assert.deepEqual(run.events, []);
});

test('development entry synchronously reserves identity fetch and rechecks context before native mutation', async () => {
  const identityWait = deferred();
  const run = fixture({identityPromise: identityWait.promise});
  run.entry.configure(config);
  const first = run.entry.begin(1, 8);
  await assert.rejects(run.entry.begin(1, 8), /already owns or used/);
  run.state.phase = 3;
  identityWait.resolve(identity);
  await assert.rejects(first, /Runtime lockstep/);
  assert.deepEqual(run.events.map(row => row[0]), ['identity']);
  assert.equal(run.entry.state().starting, false);
});

test('development entry preserves start failure while joining fatal session cleanup', async () => {
  const first = Error('native launch failed'), cleanup = Error('peer cleanup failed');
  const run = fixture({startFailure: first, closeFailure: cleanup});
  run.entry.configure(config);
  await assert.rejects(run.entry.begin(9, 8), error => error === first);
  assert.deepEqual(run.events.filter(row => ['stop', 'session-close'].includes(row[0]))
    .map(row => row[0] === 'stop' ? ['stop', row[1]] : row), [['stop', first], ['session-close', 'fatal']]);
});

test('development runtime APIs use the guarded owner and only frame callbacks advance it', async () => {
  const source = await readFile(new URL('../web/runtime-development.mjs', import.meta.url), 'utf8');
  const apiStart = source.indexOf('window.meleeNetBeginLockstep=');
  const apiEnd = source.indexOf('\nwindow.meleeNetPeerIdentity=', apiStart);
  assert.ok(apiStart >= 0 && apiEnd > apiStart, 'runtime-owned APIs remain a contiguous development entry');
  const frameStart = source.indexOf('developmentHooks.frame=wasRunning=>{');
  const frameEnd = source.indexOf(' const now=performance.now();', frameStart);
  assert.ok(frameStart >= 0 && frameEnd > frameStart, 'frame entry calls the owner before ordinary frame work');

  const events = [], identityGate = deferred();
  const identityValue = {algorithm: 'sha256', wasm: 'c'.repeat(64), disc: {sha256: 'd'.repeat(64)}};
  const nativeCalls = [];
  const Module = {
    phase: 1,
    _melee_web_native_menu_phase() { return this.phase; },
    _melee_web_native_menu_net_begin_lockstep(seed, ticks) {
      nativeCalls.push(['begin', seed, ticks]); return 1;
    },
    _melee_web_native_menu_launch() { nativeCalls.push(['launch']); return 1; },
  };
  const owner = {
    stop(error) { events.push(['stop', error]); },
    attachNetworkSession(close) { events.push(['attach']); this.networkClose = close; },
    handle: {getState: () => ({state: 'prepared'})},
  };
  let session = null, identityCalls = 0, ownerFactories = 0;
  const window = {meleeNetPeerIdentity: async () => {
    ++identityCalls; events.push(['identity']);
    if (identityCalls === 1) return identityGate.promise;
    return identityValue;
  }};
  const scope = {
    window, developmentHooks: {}, owner, Module, ready: false, fatal: false, bundle: true,
    retailRun: null, replayLoading: false, inputDirty: false,
    LOCKSTEP_DELAY: 2,
    createDevelopmentLockstepOwner,
    createRuntimeLockstepSession(options) {
      ++ownerFactories; events.push(['create-session', options]);
      session = {
        start(buildAgreement) {
          events.push(['session-start']);
          const agreement = buildAgreement({recorded: 1, required: 1, capture_failed: false});
          events.push(['agreement', agreement]);
          return Promise.resolve({protocol: {ready: true}});
        },
        onFrame() { events.push(['frame']); },
        async close(options) { events.push(['session-close', options?.mode || 'normal']); return {closed: true}; },
        snapshot() { return {role: 'alpha'}; },
      };
      return session;
    },
    resetTiming() { events.push(['reset-timing']); },
    prepareAudio: async () => { events.push(['prepare-audio']); },
    pauseAudioForPreparation: async () => { events.push(['pause-audio']); },
    boundary: async operation => { events.push(['boundary']); return operation(); },
    check: value => { events.push(['check', value]); if (!value) throw Error('native call failed'); },
    waitForAudioRender: async () => { events.push(['audio-render']); },
    syncAudio: () => { events.push(['sync-audio']); },
    $: () => ({focus: () => events.push(['focus'])}),
  };
  vm.createContext(scope);
  vm.runInContext(`let runtimeLockstepEntry=null;\n${source.slice(apiStart, apiEnd)}`, scope,
    {filename: 'runtime-development.mjs'});
  vm.runInContext(`${source.slice(frameStart, frameEnd)}\n};`, scope,
    {filename: 'runtime-development.mjs'});

  const config = {role: 'alpha', roomId: 's'.repeat(32), timeoutMs: 500, url: 'ws://127.0.0.1:8787'};
  assert.throws(() => window.meleeNetConfigureRuntimeLockstep(config), /configuration is unavailable/);
  assert.equal(ownerFactories, 0, 'unready runtime context allocates no lockstep owner');
  scope.ready = true;
  assert.equal(window.meleeNetConfigureRuntimeLockstep(config), true);

  const guardedStart = window.meleeNetBeginLockstep(11, 8);
  await Promise.resolve();
  assert.equal(identityCalls, 1, 'identity read begins only after the runtime API guard');
  Module.phase = 3;
  identityGate.resolve(identityValue);
  await assert.rejects(guardedStart, /fresh original CSS native context/);
  assert.equal(ownerFactories, 0, 'context is rechecked before adapter/session creation');
  assert.equal(nativeCalls.length, 0, 'stale context cannot enter native lockstep or launch');

  Module.phase = 1;
  await window.meleeNetBeginLockstep(17, 8);
  assert.equal(ownerFactories, 1);
  assert.equal(window.meleeNetRuntimeLockstepSnapshot().role, 'alpha');
  assert.deepEqual(nativeCalls, [['begin', 17, 8], ['launch']]);
  assert.deepEqual(events.find(row => row[0] === 'create-session')[1], {
    Module, role: 'alpha', sourceTicks: 8, inputTicks: 6, url: config.url,
    roomId: config.roomId, timeoutMs: config.timeoutMs,
  });
  const agreement = events.find(row => row[0] === 'agreement')[1];
  assert.equal(agreement.input_delay, 2);
  assert.equal(agreement.native_start.recorded, 1);
  assert.deepEqual(events.filter(row => ['reset-timing', 'prepare-audio', 'pause-audio', 'audio-render', 'focus', 'sync-audio']
    .includes(row[0])).map(row => row[0]),
  ['reset-timing', 'prepare-audio', 'pause-audio', 'audio-render', 'focus', 'sync-audio']);

  scope.developmentHooks.frame(false);
  assert.equal(events.at(-1)[0], 'frame', 'the actual frame hook forwards progress once');
  scope.fatal = true;
  scope.developmentHooks.frame(false);
  assert.equal(events.filter(row => row[0] === 'frame').length, 1, 'fatal frames stop forwarding progress');
  scope.fatal = false;
  const frameFailure = Error('frame owner failed');
  session.onFrame = () => { throw frameFailure; };
  scope.developmentHooks.frame(false);
  assert.equal(events.at(-1)[0], 'stop');
  assert.equal(events.at(-1)[1], frameFailure, 'frame callback failures enter the shared runtime stop path');
  assert.deepEqual(await window.meleeNetCloseRuntimeLockstep('fatal'), {closed: true});
  assert.equal(events.at(-1)[0], 'session-close');
});
