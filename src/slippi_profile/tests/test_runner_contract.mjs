// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {test} from 'node:test';
import {
  applyCleanupVerdict, checkedAsyncCcall, chromeUserDataDirectoryFromSpawnArgs,
  launchOwnedChromeServer, ownedProcessPresent, recordBrowserProcessInfo,
  validateAuroraFrame,
} from './runner_contract.mjs';

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
