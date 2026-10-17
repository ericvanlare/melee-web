import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {spawnSync} from 'node:child_process';
import {trackMatchedSetupCfExplicitStep, matchedSetupCfNativeAccessAllowed,
  guardMatchedSetupCfNativeCleanup} from './stadium_matched_setup_cf_runner_gate.mjs';

const [emccPath, scratchPath] = process.argv.slice(2);
assert.ok(emccPath, 'pass the pinned emcc executable as argv[2]');
assert.ok(scratchPath, 'pass a fresh external scratch directory as argv[3]');
const scratch = path.resolve(scratchPath);
await fs.mkdir(scratch, {recursive: false});
const source = new URL('./stadium_explicit_asyncify_yield_fixture.c', import.meta.url);
const output = path.join(scratch, 'stadium-explicit-asyncify-fixture.mjs');
const compilerVersion = spawnSync(emccPath, ['--version'], {encoding: 'utf8'});
if (compilerVersion.status !== 0)
  throw Error(`Pinned Emscripten version query failed: ${compilerVersion.stderr || compilerVersion.stdout}`);
const compile = spawnSync(emccPath, [
  source.pathname,
  '-O2',
  '-sASYNCIFY=1',
  '-sENVIRONMENT=node',
  '-sEXPORT_ES6=1',
  '-sMODULARIZE=1',
  '-sEXPORTED_FUNCTIONS=_stadium_asyncify_fixture_install,_stadium_asyncify_fixture_retire,_stadium_asyncify_fixture_retirement_status',
  '-sEXPORTED_RUNTIME_METHODS=ccall',
  '-o', output,
], {encoding: 'utf8'});
if (compile.status !== 0)
  throw Error(`Pinned Emscripten fixture compilation failed: ${compile.stderr || compile.stdout}`);

let successful = false;
try {
  const moduleUrl = pathToFileURL(output).href;
  const {default: createModule} = await import(moduleUrl);
  const Module = await createModule();
  Module._stadium_asyncify_fixture_install();
  assert.equal(typeof Module.stadiumAsyncifySnapshot, 'function');
  assert.equal(typeof Module.stadiumMatchedSetupNativeIdle, 'function');

  const call = Module.ccall('stadium_asyncify_fixture_retire', 'number', [], [],
    {async: true});
  assert.equal(typeof call?.then, 'function',
    'a generated yielding retirement ccall must return its Asyncify promise');
  const tracked = trackMatchedSetupCfExplicitStep(Promise.resolve(call).then(result => ({
    result, asyncify_idle: Module.stadiumMatchedSetupNativeIdle(),
  })), state => {
    state.asyncifyIdle = state.value?.asyncify_idle === true;
  });
  tracked.requiresAsyncifyIdle = true;
  const during = Module.stadiumAsyncifySnapshot();
  assert.deepEqual(during, {normal: true, curr_data_present: true, aborted: false},
    'the generated yielding export must retain Asyncify data before settlement');
  assert.equal(Module.stadiumMatchedSetupNativeIdle(), false);

  let nativeGetterCalls = 0, unloadCalls = 0;
  const guardedNativeStatus = () => {
    if (!matchedSetupCfNativeAccessAllowed(tracked)) return null;
    ++nativeGetterCalls;
    return Module._stadium_asyncify_fixture_retirement_status();
  };
  const cleanupGate = await guardMatchedSetupCfNativeCleanup({
    explicitStep: true,
    pendingStep: tracked,
    checkNativeIdle: async () => Module.stadiumMatchedSetupNativeIdle(),
    onNativeAccessAllowed: async () => {
      ++unloadCalls;
      guardedNativeStatus();
    },
    onNativeAccessBlocked: async () => {},
  });
  assert.equal(cleanupGate.allowed, false,
    'cleanup must remain closed while the real Asyncify ccall is unsettled');
  assert.equal(guardedNativeStatus(), null);
  assert.equal(nativeGetterCalls, 0,
    'no native status getter may run while the yielded retirement is pending');
  assert.equal(unloadCalls, 0, 'no unload may race a yielded retirement');

  const result = await tracked.promise;
  assert.equal(result.result, 1);
  assert.equal(result.asyncify_idle, true);
  assert.equal(Module.stadiumMatchedSetupNativeIdle(), true);
  assert.equal(matchedSetupCfNativeAccessAllowed(tracked), true);
  assert.equal(guardedNativeStatus(), 1,
    'the native retirement receipt is readable only after ccall settlement and positive idle');
  assert.equal(nativeGetterCalls, 1);
  const after = Module.stadiumAsyncifySnapshot();
  assert.deepEqual(after, {normal: true, curr_data_present: false, aborted: false});
  successful = true;
  process.stdout.write(JSON.stringify({
    control: 'generated-emscripten-yielded-retirement-no-native-getter',
    node_version: process.version,
    emcc_version: compilerVersion.stdout.trim().split(/\r?\n/, 1)[0],
    result: result.result,
    during,
    after,
    cleanup_allowed_while_pending: cleanupGate.allowed,
    native_getter_calls_while_pending: 0,
    native_getter_calls_after_settlement: nativeGetterCalls,
    unload_calls_while_pending: unloadCalls,
  }) + '\n');
} finally {
  if (successful) await fs.rm(scratch, {recursive: true, force: true});
}
