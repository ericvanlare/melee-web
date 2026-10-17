import assert from 'node:assert/strict';
import {
  assertWasmOwnershipBoundaryV1,
  captureWasmOwnershipBoundaryV1,
  installAsyncifyWriterCounterV1,
  restoreHeapAndTempRetV1,
} from './wasm_ownership_accessor_v1.mjs';

function fixture() {
  let aborted = false;
  let owner = { ready: true, pending: false, stopped: false, aborted: false };
  let stack = 0x1000;
  let tempRet = 0x76543210 | 0;
  let memory = new WebAssembly.Memory({ initial: 1, maximum: 2 });
  const calls = [];
  // A tiny synthetic module supplies branded Wasm function refs for the table;
  // it is not the game runtime or any project Wasm artifact.
  const tinyModule = new WebAssembly.Module(Uint8Array.from([
    0, 97, 115, 109, 1, 0, 0, 0,
    1, 5, 1, 96, 0, 1, 127,
    3, 2, 1, 0,
    7, 5, 1, 1, 102, 0, 0,
    10, 6, 1, 4, 0, 65, 0, 11,
  ]));
  const wasmFunction = new WebAssembly.Instance(tinyModule).exports.f;
  const asyncify = {
    State: { Normal: 0, Unwinding: 1 }, state: 0, currData: null,
    handleSleepReturnValue: 0, callStackId: 3, asyncPromiseHandlers: null,
    exportCallStack: [], sleepCallbacks: [], callstackFuncToId: new Map(),
    callStackIdToFunc: new Map(), funcWrappers: new Map(),
  };
  const aliases = {};
  const wasmExports = {};
  const originalKeys = [];
  const names = ['start_unwind', 'stop_unwind', 'start_rewind', 'stop_rewind'];
  const aliasValues = [];
  for (let i = 0; i < names.length; i += 1) {
    const original = (...args) => { calls.push([i, args]); return i + 10; };
    const wrapper = (...args) => {
      asyncify.exportCallStack.push(original);
      try { return Reflect.apply(original, undefined, args); }
      finally { asyncify.exportCallStack.pop(); }
    };
    // Deliberately opaque keys prove the installer binds by function identity.
    const exportKey = `x${String.fromCharCode(97 + i)}`;
    wasmExports[exportKey] = wrapper;
    asyncify.funcWrappers.set(original, wrapper);
    aliases[names[i]] = { get: () => aliasValues[i], set: (value) => { aliasValues[i] = value; } };
    originalKeys.push(original);
    aliasValues[i] = wrapper;
  }
  let table = new WebAssembly.Table({ element: 'anyfunc', initial: 3, maximum: 3 });
  table.set(0, wasmFunction);
  table.set(2, wasmFunction);
  const module = {
    HEAPU8: new Uint8Array(memory.buffer),
    stackSave: () => stack,
    stackRestore: (value) => { stack = value >>> 0; },
    getTempRet0: () => tempRet,
    setTempRet0: (value) => { tempRet = value | 0; },
    instantiateWasm: undefined,
  };
  const writerCounter = installAsyncifyWriterCounterV1({
    module, wasmExports, getWasmExports: () => wasmExports,
    asyncify, getAsyncify: () => asyncify, aliases, isAborted: () => aborted,
  });
  const context = () => ({
    module, memory, getMemory: () => memory,
    asyncify, getAsyncify: () => asyncify, table, getTable: () => table,
    expectedTableLength: 3, writerCounter, isAborted: () => aborted,
    ownerState: () => owner,
  });
  const setAborted = (value) => { aborted = value; };
  const setOwner = (value) => { owner = value; };
  const setStack = (value) => { stack = value >>> 0; };
  const setTableEntry = (index, value) => { table.set(index, value); };
  const replaceTable = () => {
    table = new WebAssembly.Table({ element: 'anyfunc', initial: 3, maximum: 3 });
    table.set(0, wasmFunction);
    table.set(2, wasmFunction);
  };
  const replaceMemory = () => {
    memory = new WebAssembly.Memory({ initial: 2, maximum: 2 });
    module.HEAPU8 = new Uint8Array(memory.buffer);
  };
  const growMemory = () => {
    memory.grow(1);
    module.HEAPU8 = new Uint8Array(memory.buffer);
  };
  const setTempRet = (value) => { tempRet = value | 0; };
  return {
    module, asyncify, aliases, wasmExports, writerCounter, context, calls, wasmFunction,
    originalKeys, setAborted, setOwner, setStack, setTableEntry, replaceTable,
    replaceMemory, growMemory, setTempRet,
  };
}

function capture(f, quiescentSnapshot = {
  byteLength: f.module.HEAPU8.byteLength,
  restore() { throw new Error('unused fixture snapshot'); },
}) {
  const context = f.context();
  context.quiescentSnapshot = quiescentSnapshot;
  return captureWasmOwnershipBoundaryV1(context);
}
function rejects(fn, pattern) { assert.throws(fn, (error) => error instanceof Error && pattern.test(error.message)); }

// A normal no-write endpoint accepts, and the boundary enumerates the full table.
{
  const f = fixture();
  const token = capture(f);
  assert.equal(f.writerCounter.readCount(), 0);
  assert.equal(assertWasmOwnershipBoundaryV1(token).tempRetBits, 0x76543210);
}

// Both exposed alias calls and Asyncify.doRewind's generic map route count.
for (const route of ['alias', 'rewind-map']) {
  const f = fixture();
  const token = capture(f);
  const originalKey = f.originalKeys[0];
  if (route === 'alias') f.aliases.start_unwind.get()(7);
  else {
    const mappedWrapper = f.asyncify.funcWrappers.get(originalKey);
    assert.equal(typeof mappedWrapper, 'function');
    mappedWrapper(8); // same dispatch shape as doRewind(original) -> funcWrappers.get(original)
  }
  assert.equal(f.writerCounter.readCount(), 1);
  assert.equal(f.asyncify.funcWrappers.has(originalKey), true);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /writer was entered/);
}

// The installer and boundary fail closed on alias, export, map, or table identity drift.
{
  const f = fixture(); const token = capture(f);
  f.aliases.start_unwind.set(() => 0);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /binding changed/);
}
{
  const f = fixture(); const token = capture(f);
  f.wasmExports.xa = () => 0;
  rejects(() => assertWasmOwnershipBoundaryV1(token), /binding changed/);
}
{
  const f = fixture(); const token = capture(f);
  f.asyncify.funcWrappers.set(f.originalKeys[0], () => 0);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /binding changed/);
}
{
  const f = fixture(); const token = capture(f);
  f.asyncify.funcWrappers = new Map(f.asyncify.funcWrappers);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /funcWrappers identity/);
}
for (const name of ['callstackFuncToId', 'callStackIdToFunc']) {
  const f = fixture(); const token = capture(f);
  f.asyncify[name].set(f.originalKeys[0], 9);
  rejects(() => assertWasmOwnershipBoundaryV1(token), new RegExp(name));
}
{
  const f = fixture(); const token = capture(f);
  f.setTableEntry(1, f.wasmFunction);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /table entry 1 changed/);
}
{
  const f = fixture(); const token = capture(f);
  f.replaceTable();
  rejects(() => assertWasmOwnershipBoundaryV1(token), /table identity or length changed/);
}
{
  const f = fixture(); const token = capture(f); f.replaceMemory();
  rejects(() => assertWasmOwnershipBoundaryV1(token), /WebAssembly.Memory identity changed/);
}
{
  const f = fixture(); const token = capture(f); f.growMemory();
  rejects(() => assertWasmOwnershipBoundaryV1(token), /memory identity or capacity changed/);
}

// tempRet0 is the explicitly supported independent i32 restore; Wasm heap bytes use
// the existing quiescent snapshot handle. No table/Asyncify state is restored.
{
  const f = fixture();
  f.module.HEAPU8[5] = 0x2a;
  const memoryToken = f.module.HEAPU8.slice();
  const fakeQuiescentSnapshot = {
    byteLength: f.module.HEAPU8.byteLength,
    restore() {
      f.module.HEAPU8.set(memoryToken);
      f.module.stackRestore(0x1000);
    },
  };
  const token = capture(f, fakeQuiescentSnapshot);
  f.module.HEAPU8[5] = 0x99;
  f.setTempRet(0xfedcba98);
  const restored = restoreHeapAndTempRetV1(token);
  assert.equal(restored.restoredTempRetBits, 0x76543210);
  assert.equal(f.module.getTempRet0() >>> 0, 0x76543210);
  assert.equal(f.module.HEAPU8[5], 0x2a);
}

for (const mutate of [
  (f) => { f.asyncify.state = f.asyncify.State.Unwinding; },
  (f) => { f.asyncify.currData = 12; },
  (f) => { f.asyncify.sleepCallbacks.push(() => {}); },
  (f) => { f.asyncify.handleSleepReturnValue = 1; },
]) {
  const f = fixture(); const token = capture(f); mutate(f);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /Asyncify/);
}
{
  const f = fixture(); const token = capture(f);
  f.setOwner({ ready: true, pending: true, stopped: false, aborted: false });
  rejects(() => assertWasmOwnershipBoundaryV1(token), /native owner/);
}
for (const field of ['stopped', 'aborted']) {
  const f = fixture(); const token = capture(f);
  f.setOwner({ ready: true, pending: false, stopped: false, aborted: false, [field]: true });
  rejects(() => assertWasmOwnershipBoundaryV1(token), /native owner/);
}
{
  const f = fixture(); const token = capture(f); f.setAborted(true);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /aborted/);
}
{
  const f = fixture(); const token = capture(f); f.setStack(0x1004);
  rejects(() => assertWasmOwnershipBoundaryV1(token), /stack boundary changed/);
}
for (const name of ['stackSave', 'stackRestore', 'getTempRet0', 'setTempRet0']) {
  const f = fixture();
  let heapRestoreInvoked = false;
  const token = capture(f, {
    byteLength: f.module.HEAPU8.byteLength,
    restore() { heapRestoreInvoked = true; },
  });
  const prior = f.module[name];
  let helperInvoked = false;
  f.module[name] = (...args) => { helperInvoked = true; return Reflect.apply(prior, f.module, args); };
  if (name === 'stackRestore' || name === 'setTempRet0') {
    rejects(() => restoreHeapAndTempRetV1(token), /helper identity changed/);
  } else {
    rejects(() => assertWasmOwnershipBoundaryV1(token), /helper identity changed/);
  }
  assert.equal(helperInvoked, false);
  assert.equal(heapRestoreInvoked, false);
}
{
  const f = fixture();
  const token = capture(f);
  const forgedCopy = Object.freeze({ ...token });
  rejects(() => restoreHeapAndTempRetV1(forgedCopy), /not created by this accessor/);
}
{
  const f = fixture(); f.module.instantiateWasm = () => {};
  rejects(() => installAsyncifyWriterCounterV1({
    module: f.module, wasmExports: f.wasmExports, getWasmExports: () => f.wasmExports,
    asyncify: f.asyncify, getAsyncify: () => f.asyncify, aliases: f.aliases, isAborted: () => false,
  }), /custom instantiateWasm/);
}
{
  const f = fixture();
  f.asyncify.state = f.asyncify.State.Unwinding;
  rejects(() => installAsyncifyWriterCounterV1({
    module: f.module, wasmExports: f.wasmExports, getWasmExports: () => f.wasmExports,
    asyncify: f.asyncify, getAsyncify: () => f.asyncify, aliases: f.aliases, isAborted: () => false,
  }), /Asyncify is not Normal/);
}
{
  const f = fixture();
  const oldAlias = f.aliases.stop_unwind.get();
  const oldExport = f.wasmExports.xb;
  const oldMapValue = f.asyncify.funcWrappers.get(f.originalKeys[1]);
  let failOnce = true;
  f.aliases.stop_unwind.set = (value) => {
    // Mutate first, then throw once, to cover rollback of a partial row.
    f.aliases.stop_unwind._value = value;
    if (failOnce && value !== oldAlias) {
      failOnce = false;
      throw new Error('synthetic setter failure after mutation');
    }
  };
  // Replace the getter too, so this isolated control can observe setter state.
  f.aliases.stop_unwind._value = oldAlias;
  f.aliases.stop_unwind.get = () => f.aliases.stop_unwind._value;
  assert.throws(() => installAsyncifyWriterCounterV1({
    module: f.module, wasmExports: f.wasmExports, getWasmExports: () => f.wasmExports,
    asyncify: f.asyncify, getAsyncify: () => f.asyncify, aliases: f.aliases, isAborted: () => false,
  }), /counter install failed/);
  assert.equal(f.aliases.stop_unwind.get(), oldAlias);
  assert.equal(f.wasmExports.xb, oldExport);
  assert.equal(f.asyncify.funcWrappers.get(f.originalKeys[1]), oldMapValue);
}
{
  const f = fixture(); const context = f.context();
  context.expectedTableLength = 4;
  context.quiescentSnapshot = { byteLength: f.module.HEAPU8.byteLength, restore() {} };
  rejects(() => captureWasmOwnershipBoundaryV1(context), /artifact-bound scan/);
}
{
  const f = fixture();
  const context = f.context();
  context.table = { length: 3, get: () => null };
  context.getTable = () => context.table;
  context.quiescentSnapshot = { byteLength: f.module.HEAPU8.byteLength, restore() {} };
  rejects(() => captureWasmOwnershipBoundaryV1(context), /artifact-bound scan/);
}
{
  const f = fixture();
  const context = f.context();
  context.memory = { buffer: f.module.HEAPU8.buffer };
  context.getMemory = () => context.memory;
  context.quiescentSnapshot = { byteLength: f.module.HEAPU8.byteLength, restore() {} };
  rejects(() => captureWasmOwnershipBoundaryV1(context), /actual WebAssembly.Memory/);
}

console.log('private Wasm ownership controls passed: alias/map routes and rollback; Asyncify, native-owner and abort refusals; branded table/memory identity/content/growth checks; pinned stack/tempRet helper identities; exact tempRet0 restore');
