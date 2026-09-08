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

function fakeModule({populateError = null} = {}) {
  const calls = [];
  const mountPaths = [];
  const saveCallbacks = [];
  let dependencies = 0;
  const module = {
    preRun: [() => calls.push('existing-pre-run')],
    FS: {
      mkdirTree(path) { calls.push(['mkdirTree', path]); },
      mount(_type, _options, path) { mountPaths.push(path); calls.push(['mount', path]); },
      syncfs(populate, callback) {
        calls.push(['syncfs', populate]);
        if (populate) queueMicrotask(() => callback(populateError));
        else saveCallbacks.push(callback);
      },
    },
    IDBFS: {},
    addRunDependency(id) { ++dependencies; calls.push(['add', id]); },
    removeRunDependency(id) { --dependencies; calls.push(['remove', id]); },
  };
  return { module, calls, mountPaths, saveCallbacks, get dependencies() { return dependencies; } };
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

  const failedSave = successful.module.saveRuntimeCache();
  await waitTurn();
  successful.saveCallbacks.shift()(new Error('quota exceeded'));
  assert.equal(await failedSave, false);
  assert.equal(state.state, 'unavailable');
  assert.match(state.message, /quota exceeded/);

  const populateFailure = fakeModule({populateError: new Error('private mode')});
  const failureReports = [];
  const failedState = installRuntimeCache(populateFailure.module, report => failureReports.push(report));
  populateFailure.module.preRun.at(-1)();
  await waitTurn();
  assert.equal(failedState.state, 'unavailable');
  assert.equal(populateFailure.dependencies, 0, 'failed populate must release its run dependency');
  assert.match(failureReports.at(-1).message, /private mode/);
  assert.equal(await populateFailure.module.saveRuntimeCache(), false);

  const staleReports = [];
  const staleModule = {};
  const staleState = installRuntimeCache(staleModule, report => staleReports.push(report));
  staleModule.preRun[0]();
  assert.equal(staleState.state, 'unavailable');
  assert.match(staleReports.at(-1).message, /unavailable/);
  assert.equal(await staleModule.saveRuntimeCache(), false);
  console.log('Runtime cache mount, populate, dependency, serialization and failure checks passed');
}

main().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
