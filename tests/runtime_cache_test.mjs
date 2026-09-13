import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../web/runtime-cache.js', import.meta.url), 'utf8');
const waitTurn = () => new Promise(resolve => setTimeout(resolve, 0));

function loadInstaller() {
  const context = { console, Promise, Error, TypeError, setTimeout, clearTimeout, queueMicrotask };
  context.globalThis = context;
  vm.runInNewContext(source, context, { filename: 'runtime-cache.js' });
  return context.installRuntimeCache;
}

function fakeModule({populateError = null, files = {"pipeline_cache.db": 4096}} = {}) {
  const calls = [];
  const mountPaths = [];
  const saveCallbacks = [];
  const syncEvents = [];
  let mountedType = null;
  let targetMount = null;
  let mounted = null;
  let dependencies = 0;
  const module = {
    preRun: [() => calls.push('existing-pre-run')],
    FS: {
      mkdirTree(path) { calls.push(['mkdirTree', path]); },
      mount(_type, _options, path) {
        mountPaths.push(path); calls.push(['mount', path]);
        mountedType = {
          syncfs(receiver, populate, callback) {
            syncEvents.push({populate, receiver, thisValue: this});
            if (populate) queueMicrotask(() => callback(populateError));
            else saveCallbacks.push(callback);
            return 'syncfs-return';
          },
        };
        targetMount = {type: mountedType};
        mounted = {mount: targetMount};
        return mounted;
      },
      syncfs(populate, callback) {
        calls.push(['syncfs', populate]);
        return targetMount.type.syncfs(targetMount, populate, callback);
      },
      lookupPath() { return {node: {mount: targetMount}}; },
      readdir() { return ['.', '..', ...Object.keys(files)]; },
      stat(path) {
        const name = path.split('/').at(-1);
        if (!(name in files)) throw new Error(`missing ${name}`);
        return {mode: 0, size: files[name]};
      },
      isDir() { return false; },
      unlink(path) {
        const name = path.split('/').at(-1);
        calls.push(['unlink', path]);
        delete files[name];
      },
    },
    IDBFS: {},
    addRunDependency(id) { ++dependencies; calls.push(['add', id]); },
    removeRunDependency(id) { --dependencies; calls.push(['remove', id]); },
  };
  return { module, calls, mountPaths, saveCallbacks, syncEvents, get mountedType() { return mountedType; }, get dependencies() { return dependencies; } };
}

async function main() {
  const installRuntimeCache = loadInstaller();
  const successful = fakeModule();
  const reports = [];
  const state = installRuntimeCache(successful.module, report => reports.push(report));
  assert.equal(successful.module.preRun.length, 2);
  successful.module.preRun[0]();
  successful.module.preRun[1]();
  await waitTurn();
  assert.equal(state.state, 'ready');
  assert.equal(state.populated, true);
  assert.equal(state.fileBytes, 4096);
  assert.equal(successful.dependencies, 0);
  assert.deepEqual(successful.mountPaths, ['/melee-render-cache']);
  assert.deepEqual(successful.calls.slice(0, 5), [
    'existing-pre-run',
    ['mkdirTree', '/melee-render-cache'],
    ['mount', '/melee-render-cache'],
    ['add', 'melee-render-cache-populate'],
    ['syncfs', true],
  ]);

  const first = successful.module.saveRuntimeCache();
  const second = successful.module.saveRuntimeCache();
  await waitTurn();
  assert.equal(successful.saveCallbacks.length, 1, 'saves must not overlap');
  successful.saveCallbacks.shift()(null);
  assert.equal(await first, true);
  await waitTurn();
  assert.equal(successful.saveCallbacks.length, 1, 'queued save should start after the first');
  successful.saveCallbacks.shift()(null);
  assert.equal(await second, true);
  assert.equal(state.saves, 2);
  assert.equal(state.fileBytes, 4096);

  assert.equal(successful.module.markRuntimeCacheDirty(), true);
  assert.equal(successful.module.markRuntimeCacheDirty(), true);
  assert.equal(state.dirty, true);
  await waitTurn();
  await waitTurn();
  assert.equal(successful.saveCallbacks.length, 0, 'pipeline discovery must not persist during gameplay');
  const deferredSave = successful.module.saveRuntimeCache();
  await waitTurn();
  assert.equal(successful.saveCallbacks.length, 1, 'explicit lifecycle teardown starts the cache save');
  successful.saveCallbacks.shift()(null);
  assert.equal(await deferredSave, true);
  assert.equal(state.saves, 3);
  assert.equal(state.dirty, false);
  assert.equal(typeof state.lastSaveMs, 'number');
  assert.equal(successful.module.scheduleRuntimeCacheSave, undefined);

  // Causal mode wraps only the mounted IDBFS type and preserves native direct
  // calls plus callback receiver/arguments/return values.
  let clock = 10;
  const instrumented = fakeModule();
  const syncReports = [];
  const instrumentedState = installRuntimeCache(instrumented.module, () => {}, {
    syncDiagnostics: true,
    now: () => clock,
    onSync: event => syncReports.push(event),
  });
  instrumented.module.preRun.at(-1)();
  await waitTurn();
  assert.equal(instrumentedState.syncDiagnostics.enabled, true);
  assert.equal(instrumentedState.syncDiagnostics.installed, true);
  assert.deepEqual(syncReports.slice(0, 2).map(event => [event.phase, event.source, event.operation]), [
    ['start', 'explicit', 'populate'],
    ['completion', 'explicit', 'populate'],
  ]);
  const originalType = instrumented.mountedType;
  const nativeReceiver = instrumented.module.FS.lookupPath('/melee-render-cache').node.mount.type;
  assert.notEqual(nativeReceiver, originalType, 'Only the target mount gets an adapter');
  assert.equal(originalType.syncfs.__meleeRenderCacheSyncInstrumentation, undefined);
  let nativeCallbackThis = null;
  let nativeCallbackArgs = null;
  clock = 20;
  const nativeReturn = nativeReceiver.syncfs(instrumented.module.FS.lookupPath('/melee-render-cache').node.mount, false, function (...args) {
    nativeCallbackThis = this;
    nativeCallbackArgs = args;
    return 'callback-return';
  });
  assert.equal(nativeReturn, 'syncfs-return');
  clock = 26;
  const nativeError = new Error('native sync failed');
  const nativeCallbackReturn = instrumented.saveCallbacks.shift().call(nativeReceiver, nativeError);
  assert.equal(nativeCallbackReturn, 'callback-return');
  assert.equal(nativeCallbackThis, nativeReceiver);
  assert.deepEqual(nativeCallbackArgs, [nativeError]);
  assert.equal(instrumented.syncEvents.at(-1).thisValue, originalType);
  // The shared IDBFS type can serve another mount; its call stays entirely
  // invisible to the render-cache instrumentation.
  const beforeUnrelated = syncReports.length;
  const unrelatedMount = {type: originalType};
  originalType.syncfs(unrelatedMount, false, () => {});
  assert.equal(syncReports.length, beforeUnrelated);
  instrumented.saveCallbacks.shift()(null);
  const nativeEvents = syncReports.filter(event => event.id.startsWith('cache-sync-2'));
  assert.deepEqual(nativeEvents.map(event => [event.phase, event.source, event.operation, event.status]), [
    ['start', 'unknown', 'unknown', 'pending'],
    ['completion', 'unknown', 'unknown', 'error'],
  ]);
  assert.equal(instrumentedState.syncDiagnostics.errors, 1);
  clock = 30;
  const explicitSave = instrumented.module.saveRuntimeCache();
  await waitTurn();
  assert.equal(instrumented.saveCallbacks.length, 1);
  clock = 35;
  instrumented.saveCallbacks.shift()(null);
  assert.equal(await explicitSave, true);
  const saveEvents = syncReports.filter(event => event.operation === 'save');
  assert.deepEqual(saveEvents.map(event => [event.phase, event.source, event.status]), [
    ['start', 'explicit', 'pending'],
    ['completion', 'explicit', 'completed'],
  ]);

  const observedBeforeDisable = syncReports.length;
  instrumented.module.setRuntimeCacheSyncDiagnostics(false);
  const targetMount = instrumented.module.FS.lookupPath('/melee-render-cache').node.mount;
  assert.equal(targetMount.type, originalType, 'Disabling restores only the target mount');
  targetMount.type.syncfs(targetMount, false, () => {});
  instrumented.saveCallbacks.shift()(null);
  assert.equal(syncReports.length, observedBeforeDisable);
  instrumented.module.setRuntimeCacheSyncDiagnostics(true);
  assert.notEqual(targetMount.type, originalType);
  assert.equal(originalType.syncfs.__meleeRenderCacheSyncInstrumentation, undefined);

  const failedSave = successful.module.saveRuntimeCache();
  await waitTurn();
  successful.saveCallbacks.shift()(new Error('quota exceeded'));
  assert.equal(await failedSave, false);
  assert.equal(state.state, 'unavailable');
  assert.match(state.message, /quota exceeded/);

  const populateFailure = fakeModule({populateError: new Error('private mode')});
  const failureReports = [];
  const failedState = installRuntimeCache(populateFailure.module, report => failureReports.push(report), {
    syncDiagnostics: true, now: () => clock,
  });
  populateFailure.module.preRun.at(-1)();
  await waitTurn();
  assert.equal(failedState.state, 'unavailable');
  assert.equal(populateFailure.dependencies, 0, 'failed populate must release its run dependency');
  assert.match(failureReports.at(-1).message, /private mode/);
  assert.equal(await populateFailure.module.saveRuntimeCache(), false);
  assert.equal(failedState.syncDiagnostics.installed, false, 'Unavailable cache cannot advertise a usable causal probe');
  const failedMount = populateFailure.module.FS.lookupPath('/melee-render-cache').node.mount;
  assert.equal(failedMount.type, populateFailure.mountedType, 'Populate failure restores the original mount type');
  populateFailure.module.setRuntimeCacheSyncDiagnostics(false);
  populateFailure.module.setRuntimeCacheSyncDiagnostics(true);
  assert.equal(failedState.syncDiagnostics.installed, false, 'Reenabling diagnostics cannot revive an unavailable cache');
  assert.equal(failureReports.at(-1).sync_diagnostics.installed, false);

  const staleReports = [];
  const staleModule = {};
  const staleState = installRuntimeCache(staleModule, report => staleReports.push(report));
  staleModule.preRun[0]();
  assert.equal(staleState.state, 'unavailable');
  assert.match(staleReports.at(-1).message, /unavailable/);
  assert.equal(await staleModule.saveRuntimeCache(), false);

  const cleared = fakeModule({files: {"pipeline_cache.db": 8192, "pipeline_cache.db-journal": 512}});
  const clearReports = [];
  const clearState = installRuntimeCache(cleared.module, report => clearReports.push(report), {clearOnLoad: true});
  cleared.module.preRun.at(-1)();
  await waitTurn();
  assert.deepEqual(cleared.calls.filter(call => Array.isArray(call) && call[0] === 'unlink'), [
    ['unlink', '/melee-render-cache/pipeline_cache.db'],
    ['unlink', '/melee-render-cache/pipeline_cache.db-journal'],
  ]);
  assert.equal(cleared.saveCallbacks.length, 1, 'cleared IDBFS contents must be persisted before startup');
  assert.equal(cleared.dependencies, 1, 'renderer startup stays blocked until the reset is persisted');
  cleared.saveCallbacks.shift()(null);
  await waitTurn();
  assert.equal(clearState.state, 'cleared');
  assert.equal(clearState.clears, 1);
  assert.equal(clearState.fileBytes, 0);
  assert.equal(cleared.dependencies, 0);
  assert.match(clearReports.at(-1).message, /driver cache unchanged/);
  console.log('Runtime cache mount, populate, reset-before-startup, dependency, serialization and failure checks passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
