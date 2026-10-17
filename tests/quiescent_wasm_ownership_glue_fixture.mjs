import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath, pathToFileURL} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
if (args.length !== 2) throw new Error('usage: node run_glue_fixture.mjs <generated.js> <fixture.wasm>');
const jsPath = path.resolve(args[0]);
const wasmPath = path.resolve(args[1]);
const generated = await readFile(jsPath, 'utf8');
const wasmBinary = await readFile(wasmPath);
const require = createRequire(jsPath);
const accessorPath = path.join(here, 'wasm_ownership_accessor_v1.mjs');
const snapshotPath = path.join(here, 'quiescent_wasm_snapshot.mjs');
const accessor = await import(pathToFileURL(accessorPath));
const {QuiescentWasmSnapshot} = await import(pathToFileURL(snapshotPath));

function makeModule({shape = 'none', installer = true, customInstantiate = false} = {}) {
  const trace = [];
  let context;
  let runtimeInitialized = false;
  const Module = {
    wasmBinary: Uint8Array.from(wasmBinary),
    locateFile: (name) => name.endsWith('.wasm') ? wasmPath : name,
    noInitialRun: false,
    print: () => {},
    printErr: (...values) => { throw new Error(values.map(String).join(' ')); },
    onAbort: (reason) => { throw new Error('generated glue aborted: ' + String(reason)); },
    onRuntimeInitialized: () => { runtimeInitialized = true; },
  };
  if (installer) {
    Module.__meleeWebWasmOwnershipInstallV1 = (options) =>
      accessor.installAsyncifyWriterCounterV1(options);
  }
  if (customInstantiate) Module.instantiateWasm = () => {};
  const prior = (name) => (module) => {
    assert.equal(module, context.Module);
    assert.equal(Module.__meleeWebWasmOwnershipPrivateV1.status().state,
      'waiting_for_prerun', 'existing preRun callbacks run before the appended installer');
    trace.push(name);
  };
  if (shape === 'function') Module.preRun = prior('prior-a');
  else if (shape === 'array') Module.preRun = [prior('prior-a'), prior('prior-b')];
  else if (shape !== 'none') throw new Error('unknown preRun shape');
  context = vm.createContext({
    Module, console, process, require, __dirname: path.dirname(jsPath),
    Buffer, WebAssembly, Uint8Array, Array, Map, Set, WeakMap, Object, Number,
    setTimeout, clearTimeout, setInterval, clearInterval, queueMicrotask,
    performance, TextEncoder, TextDecoder, URL, URLSearchParams,
  });
  return {Module, context, trace, get runtimeInitialized() { return runtimeInitialized; }};
}

async function oneCase(shape) {
  const fixture = makeModule({shape});
  const {Module, context, trace} = fixture;
  vm.runInContext(generated, context, {filename: jsPath, timeout: 10000});
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    if (Module._wasm_ownership_fixture_phase?.() === 2) break;
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.equal(fixture.runtimeInitialized, true, 'Emscripten runtime initialization callback ran');
  assert.equal(Module._wasm_ownership_fixture_phase(), 2, 'Asyncify continuation completed');
  assert.deepEqual(trace, shape === 'function' ? ['prior-a'] : ['prior-a', 'prior-b']);

  const privateApi = Module.__meleeWebWasmOwnershipPrivateV1;
  assert.equal(privateApi?.schema, 'melee-web-private-wasm-instance-v1');
  const status = privateApi.status();
  assert.equal(status.state, 'ready');
  assert.equal(status.failure, null);
  const bindings = privateApi.getBindings();
  assert.equal(bindings.module, Module);
  assert.ok(bindings.memory instanceof WebAssembly.Memory);
  assert.ok(bindings.table instanceof WebAssembly.Table);
  assert.equal(bindings.module.HEAPU8.buffer, bindings.memory.buffer);
  assert.ok(bindings.actualTableLength > 0);
  assert.ok(bindings.writerCounter.readCount() > 0,
    'native emscripten_sleep entered at least one wrapped Asyncify writer');
  bindings.writerCounter.validateBindings();

  const snapshotOwner = new QuiescentWasmSnapshot(Module);
  const heapHandle = snapshotOwner.capture();
  const ownerState = () => ({ready: true, pending: false, stopped: false, aborted: false});
  const boundary = accessor.captureWasmOwnershipBoundaryV1({
    ...bindings, expectedTableLength: bindings.actualTableLength,
    ownerState, quiescentSnapshot: heapHandle,
  });
  Module._wasm_ownership_fixture_ping();
  accessor.assertWasmOwnershipBoundaryV1(boundary);
  snapshotOwner.close();
  return {shape, trace, asyncifyWriterCalls: bindings.writerCounter.readCount(),
    tableLength: bindings.actualTableLength, heapBytes: Module.HEAPU8.byteLength,
    runtimeInitialized: fixture.runtimeInitialized};
}

function refusalCase(kind) {
  const fixture = makeModule({
    installer: kind !== 'missing-installer',
    customInstantiate: kind === 'custom-instantiate',
  });
  const {Module, context, trace} = fixture;
  assert.throws(() => vm.runInContext(generated, context, {filename: jsPath, timeout: 10000}),
    /Private Wasm ownership setup refused/);
  const status = Module.__meleeWebWasmOwnershipPrivateV1.status();
  assert.equal(status.state, 'refused');
  const expected = kind === 'missing-installer'
    ? 'configureModule did not provide the reviewed accessor installer'
    : 'custom Module.instantiateWasm is unsupported';
  assert.equal(status.failure, expected);
  assert.equal(fixture.runtimeInitialized, false, 'refusal occurs before runtime-ready');
  assert.notEqual(Module.__wasmOwnershipFixtureMainReached, true,
    'refusal occurs before C main');
  assert.deepEqual(trace, [], 'preRun callbacks do not run before the refusal');
  return {kind, state: status.state, failure: status.failure,
    runtimeInitialized: fixture.runtimeInitialized,
    mainReached: Module.__wasmOwnershipFixtureMainReached === true};
}

const reports = [];
for (const shape of ['function', 'array']) reports.push(await oneCase(shape));
const refusals = ['missing-installer', 'custom-instantiate'].map(refusalCase);
console.log(JSON.stringify({
  schema: 'melee-web-wasm-instance-ownership-glue-fixture-v2',
  reports, refusals,
}, null, 2));
