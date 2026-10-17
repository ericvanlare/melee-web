/*
 * External private-profile candidate. Not wired into NET and not a public
 * browser API. It installs a JS-only counter around the four existing
 * Asyncify wrappers and snapshots instance state for fail-closed comparison.
 */
const WRITER_NAMES = Object.freeze([
  'start_unwind', 'stop_unwind', 'start_rewind', 'stop_rewind',
]);
const PRIVATE_BOUNDARIES = new WeakMap();

function refuse(message) {
  throw new Error(`Wasm ownership refused: ${message}`);
}

function asU32(value, label) {
  if (!Number.isInteger(value) || value < -0x80000000 || value > 0xffffffff) {
    refuse(`${label} is not a 32-bit integer`);
  }
  return value >>> 0;
}

function copyExactArray(array, label) {
  if (!Array.isArray(array)) refuse(`${label} is not an array`);
  const lengthDescriptor = Object.getOwnPropertyDescriptor(array, 'length');
  if (!lengthDescriptor || !Object.prototype.hasOwnProperty.call(lengthDescriptor, 'value')) {
    refuse(`${label} has no data length`);
  }
  const entries = [];
  for (const key of Reflect.ownKeys(array)) {
    if (key === 'length') continue;
    if (typeof key !== 'string' || !/^(0|[1-9][0-9]*)$/.test(key)) {
      refuse(`${label} has an unclassified property`);
    }
    const descriptor = Object.getOwnPropertyDescriptor(array, key);
    if (!descriptor || !Object.prototype.hasOwnProperty.call(descriptor, 'value')) {
      refuse(`${label} has an accessor entry`);
    }
    entries.push([key, descriptor.value, descriptor.writable, descriptor.enumerable, descriptor.configurable]);
  }
  return Object.freeze({ ref: array, length: array.length, lengthWritable: lengthDescriptor.writable,
    entries: Object.freeze(entries) });
}

function sameExactArray(snapshot, current, label) {
  if (current !== snapshot.ref || !Array.isArray(current) || current.length !== snapshot.length ||
      Object.getOwnPropertyDescriptor(current, 'length')?.writable !== snapshot.lengthWritable) {
    refuse(`${label} identity or length changed`);
  }
  const keys = Reflect.ownKeys(current).filter((key) => key !== 'length');
  if (keys.length !== snapshot.entries.length) refuse(`${label} entries changed`);
  for (let i = 0; i < keys.length; i += 1) {
    const [oldKey, oldValue, writable, enumerable, configurable] = snapshot.entries[i];
    const descriptor = Object.getOwnPropertyDescriptor(current, oldKey);
    if (keys[i] !== oldKey || !descriptor || !Object.prototype.hasOwnProperty.call(descriptor, 'value') ||
        descriptor.value !== oldValue || descriptor.writable !== writable ||
        descriptor.enumerable !== enumerable || descriptor.configurable !== configurable) {
      refuse(`${label} entry changed`);
    }
  }
}

function copyMap(map, label) {
  if (!(map instanceof Map)) refuse(`${label} is not a Map`);
  const entries = Object.freeze(Array.from(map, ([key, value]) => Object.freeze([key, value])));
  return Object.freeze({ ref: map, entries });
}

function sameMap(snapshot, current, label) {
  if (current !== snapshot.ref || !(current instanceof Map) || current.size !== snapshot.entries.length) {
    refuse(`${label} identity or size changed`);
  }
  let index = 0;
  for (const [key, value] of current) {
    const [oldKey, oldValue] = snapshot.entries[index++];
    if (key !== oldKey || value !== oldValue) refuse(`${label} entries changed`);
  }
}

function requireOwner(ownerState) {
  if (typeof ownerState !== 'function') refuse('native owner evidence is unavailable');
  const owner = ownerState();
  if (!owner || owner.ready !== true || owner.pending !== false ||
      owner.stopped !== false || owner.aborted !== false) {
    refuse('native owner is pending, stopped, aborted, or not ready');
  }
}

function requireIdle(asyncify, isAborted) {
  if (typeof isAborted !== 'function' || isAborted() !== false) refuse('runtime is aborted or abort status is unavailable');
  if (!asyncify || !asyncify.State || asyncify.state !== asyncify.State.Normal || asyncify.currData !== null) {
    refuse('Asyncify is not Normal with null currData');
  }
  if (asyncify.asyncPromiseHandlers !== null) refuse('Asyncify has a pending promise handler');
  if (!Array.isArray(asyncify.exportCallStack) || asyncify.exportCallStack.length !== 0) {
    refuse('Asyncify export call stack is not empty');
  }
  if (!Array.isArray(asyncify.sleepCallbacks) || asyncify.sleepCallbacks.length !== 0) {
    refuse('Asyncify sleep callbacks are pending');
  }
}

function readFullHeap(module, memory) {
  if (typeof WebAssembly === 'undefined' || typeof WebAssembly.Memory !== 'function' ||
      !(memory instanceof WebAssembly.Memory)) {
    refuse('live binding is not the actual WebAssembly.Memory object');
  }
  const heap = module?.HEAPU8;
  if (!(heap instanceof Uint8Array) || heap.byteOffset !== 0 ||
      heap.byteLength !== heap.buffer.byteLength || heap.buffer !== memory.buffer ||
      (typeof SharedArrayBuffer !== 'undefined' && heap.buffer instanceof SharedArrayBuffer)) {
    refuse('Wasm memory is not one full non-shared HEAPU8 view');
  }
  return { heap, memory, buffer: memory.buffer, bytes: heap.byteLength };
}

function snapshotTable(table, expectedTableLength) {
  if (!Number.isSafeInteger(expectedTableLength) || expectedTableLength <= 0 ||
      typeof WebAssembly === 'undefined' || typeof WebAssembly.Table !== 'function' ||
      !(table instanceof WebAssembly.Table) ||
      table.length !== expectedTableLength) {
    refuse('table identity/length does not match the artifact-bound scan');
  }
  const entries = new Array(expectedTableLength);
  for (let i = 0; i < expectedTableLength; i += 1) entries[i] = table.get(i);
  return Object.freeze({ table, length: expectedTableLength, entries: Object.freeze(entries) });
}

function compareTable(snapshot, table) {
  if (table !== snapshot.table || typeof WebAssembly === 'undefined' ||
      typeof WebAssembly.Table !== 'function' ||
      !(table instanceof WebAssembly.Table) || table.length !== snapshot.length) {
    refuse('table identity or length changed');
  }
  for (let i = 0; i < snapshot.length; i += 1) {
    if (table.get(i) !== snapshot.entries[i]) refuse(`table entry ${i} changed`);
  }
}

/*
 * Rebind by identity, never by minified export key or function-table index.
 * Each actual alias, transformed export property, and funcWrappers value must
 * agree exactly once. Original Wasm function keys in funcWrappers are retained.
 */
export function installAsyncifyWriterCounterV1({ module, wasmExports, getWasmExports, asyncify, getAsyncify, aliases, isAborted }) {
  if (!module || module.instantiateWasm ||
      !wasmExports || typeof wasmExports !== 'object' || typeof getWasmExports !== 'function' ||
      !asyncify || typeof getAsyncify !== 'function' || typeof isAborted !== 'function' ||
      !(asyncify.funcWrappers instanceof Map) || !aliases) {
    refuse('module export/Asyncify surface is incomplete or uses a custom instantiateWasm hook');
  }
  if (getWasmExports() !== wasmExports || getAsyncify() !== asyncify) {
    refuse('instance or Asyncify binding is replaced at private preRun');
  }
  requireIdle(asyncify, isAborted);
  const seenWrappers = new Set();
  const seenExportKeys = new Set();
  const seenOriginalKeys = new Set();
  const rows = [];
  for (const name of WRITER_NAMES) {
    const binding = aliases[name];
    if (!binding || typeof binding.get !== 'function' || typeof binding.set !== 'function') {
      refuse(`Asyncify alias ${name} is unavailable`);
    }
    const oldWrapper = binding.get();
    if (typeof oldWrapper !== 'function' || seenWrappers.has(oldWrapper)) {
      refuse(`Asyncify alias ${name} is missing or duplicated`);
    }
    const exportKeys = Object.keys(wasmExports).filter((key) => wasmExports[key] === oldWrapper);
    const mapRows = Array.from(asyncify.funcWrappers.entries()).filter(([, value]) => value === oldWrapper);
    if (exportKeys.length !== 1 || mapRows.length !== 1) {
      refuse(`Asyncify alias ${name} lacks one exact export property and wrapper-map value`);
    }
    const exportKey = exportKeys[0];
    const originalKey = mapRows[0][0];
    const descriptor = Object.getOwnPropertyDescriptor(wasmExports, exportKey);
    if (!descriptor || !Object.prototype.hasOwnProperty.call(descriptor, 'value') || !descriptor.writable) {
      refuse(`Asyncify export property for ${name} is not a writable data property`);
    }
    if (seenExportKeys.has(exportKey) || seenOriginalKeys.has(originalKey)) {
      refuse('Asyncify writer bindings are not one-to-one');
    }
    seenWrappers.add(oldWrapper);
    seenExportKeys.add(exportKey);
    seenOriginalKeys.add(originalKey);
    rows.push({ name, binding, oldWrapper, exportKey, originalKey, nextWrapper: null });
  }

  let entries = 0;
  for (const row of rows) {
    const previous = row.oldWrapper;
    row.nextWrapper = function countedAsyncifyWriter(...args) {
      if (entries >= Number.MAX_SAFE_INTEGER) refuse('writer counter exhausted');
      entries += 1;
      return Reflect.apply(previous, this, args);
    };
  }

  const changed = [];
  try {
    for (const row of rows) {
      // Record before the first write so rollback also covers a setter/property
      // operation that mutates and then throws.
      changed.push(row);
      asyncify.funcWrappers.set(row.originalKey, row.nextWrapper);
      wasmExports[row.exportKey] = row.nextWrapper;
      row.binding.set(row.nextWrapper);
    }
  } catch (error) {
    let rollbackFailed = false;
    for (const row of changed.reverse()) {
      try {
        row.binding.set(row.oldWrapper);
        wasmExports[row.exportKey] = row.oldWrapper;
        asyncify.funcWrappers.set(row.originalKey, row.oldWrapper);
      } catch { rollbackFailed = true; }
    }
    refuse(rollbackFailed ? 'counter install and rollback both failed' : `counter install failed: ${String(error)}`);
  }

  const validateBindings = () => {
    if (getWasmExports() !== wasmExports || getAsyncify() !== asyncify ||
        module.instantiateWasm) {
      refuse('module exports, Asyncify instance, or instantiate hook changed');
    }
    for (const row of rows) {
      if (row.binding.get() !== row.nextWrapper ||
          wasmExports[row.exportKey] !== row.nextWrapper ||
          asyncify.funcWrappers.get(row.originalKey) !== row.nextWrapper) {
        refuse(`Asyncify writer ${row.name} alias/export/map binding changed`);
      }
    }
  };
  validateBindings();
  return Object.freeze({ readCount: () => entries, validateBindings });
}

export function captureWasmOwnershipBoundaryV1({
  module, memory, getMemory, asyncify, getAsyncify, table, getTable, expectedTableLength,
  writerCounter, isAborted, ownerState, quiescentSnapshot,
}) {
  requireOwner(ownerState);
  requireIdle(asyncify, isAborted);
  if (!module || typeof module.stackSave !== 'function' ||
      typeof module.stackRestore !== 'function' || typeof module.getTempRet0 !== 'function' ||
      typeof module.setTempRet0 !== 'function') {
    refuse('private stack/tempRet runtime helpers are not retained');
  }
  if (typeof getMemory !== 'function' || getMemory() !== memory) {
    refuse('live WebAssembly.Memory binding is unavailable');
  }
  const helpers = Object.freeze({
    stackSave: module.stackSave,
    stackRestore: module.stackRestore,
    getTempRet0: module.getTempRet0,
    setTempRet0: module.setTempRet0,
  });
  if (typeof WebAssembly === 'undefined' || typeof WebAssembly.Memory !== 'function' ||
      !(memory instanceof WebAssembly.Memory)) {
    refuse('live binding is not the actual WebAssembly.Memory object');
  }
  if (!writerCounter || typeof writerCounter.readCount !== 'function' ||
      typeof writerCounter.validateBindings !== 'function') {
    refuse('Asyncify writer counter is unavailable');
  }
  writerCounter.validateBindings();
  const heap = readFullHeap(module, memory);
  if (!quiescentSnapshot || typeof quiescentSnapshot.restore !== 'function' ||
      quiescentSnapshot.byteLength !== heap.bytes) {
    refuse('matching existing QuiescentWasmSnapshot handle is unavailable');
  }
  const stackBits = asU32(Reflect.apply(helpers.stackSave, module, []), 'stackSave result');
  const tempRetBits = asU32(Reflect.apply(helpers.getTempRet0, module, []), 'getTempRet0 result');
  if (typeof getAsyncify !== 'function' || getAsyncify() !== asyncify ||
      typeof getTable !== 'function' || getTable() !== table) {
    refuse('live Asyncify/table bindings are unavailable');
  }
  const tableSnapshot = snapshotTable(getTable(), expectedTableLength);
  const state = Object.freeze({
    handleSleepReturnValue: asyncify.handleSleepReturnValue,
    callStackId: asyncify.callStackId,
    exportCallStack: copyExactArray(asyncify.exportCallStack, 'Asyncify exportCallStack'),
    sleepCallbacks: copyExactArray(asyncify.sleepCallbacks, 'Asyncify sleepCallbacks'),
    callstackFuncToId: copyMap(asyncify.callstackFuncToId, 'Asyncify callstackFuncToId'),
    callStackIdToFunc: copyMap(asyncify.callStackIdToFunc, 'Asyncify callStackIdToFunc'),
    funcWrappers: copyMap(asyncify.funcWrappers, 'Asyncify funcWrappers'),
  });
  const snapshot = Object.freeze({
    module, memory, getMemory, helpers, asyncify, heapBuffer: heap.buffer, heapBytes: heap.bytes,
    stackBits, tempRetBits,
    tableSnapshot, writerCount: writerCounter.readCount(), writerCounter,
    isAborted, ownerState, getAsyncify, getTable, state,
  });
  PRIVATE_BOUNDARIES.set(snapshot, Object.freeze({ quiescentSnapshot }));
  return snapshot;
}

export function assertWasmOwnershipBoundaryV1(snapshot, { compareTempRet = false } = {}) {
  if (!snapshot || !snapshot.module || !snapshot.asyncify || !PRIVATE_BOUNDARIES.has(snapshot)) {
    refuse('boundary snapshot is missing or was not created by this accessor');
  }
  if (snapshot.module.stackSave !== snapshot.helpers.stackSave ||
      snapshot.module.stackRestore !== snapshot.helpers.stackRestore ||
      snapshot.module.getTempRet0 !== snapshot.helpers.getTempRet0 ||
      snapshot.module.setTempRet0 !== snapshot.helpers.setTempRet0) {
    refuse('private stack/tempRet helper identity changed');
  }
  if (snapshot.getMemory() !== snapshot.memory) refuse('live WebAssembly.Memory identity changed');
  requireOwner(snapshot.ownerState);
  const asyncify = snapshot.getAsyncify();
  if (asyncify !== snapshot.asyncify) refuse('live Asyncify instance changed');
  requireIdle(asyncify, snapshot.isAborted);
  snapshot.writerCounter.validateBindings();
  if (snapshot.writerCounter.readCount() !== snapshot.writerCount) refuse('an Asyncify writer was entered');
  const heap = readFullHeap(snapshot.module, snapshot.memory);
  if (heap.memory !== snapshot.memory || heap.buffer !== snapshot.heapBuffer || heap.bytes !== snapshot.heapBytes) {
    refuse('Wasm memory identity or capacity changed');
  }
  if (asU32(Reflect.apply(snapshot.helpers.stackSave, snapshot.module, []), 'stackSave result') !== snapshot.stackBits) {
    refuse('Wasm stack boundary changed');
  }
  compareTable(snapshot.tableSnapshot, snapshot.getTable());
  const a = asyncify;
  const s = snapshot.state;
  if (a.handleSleepReturnValue !== s.handleSleepReturnValue || a.callStackId !== s.callStackId) {
    refuse('Asyncify scalar bookkeeping changed');
  }
  sameExactArray(s.exportCallStack, a.exportCallStack, 'Asyncify exportCallStack');
  sameExactArray(s.sleepCallbacks, a.sleepCallbacks, 'Asyncify sleepCallbacks');
  sameMap(s.callstackFuncToId, a.callstackFuncToId, 'Asyncify callstackFuncToId');
  sameMap(s.callStackIdToFunc, a.callStackIdToFunc, 'Asyncify callStackIdToFunc');
  sameMap(s.funcWrappers, a.funcWrappers, 'Asyncify funcWrappers');
  const currentTempRetBits = asU32(Reflect.apply(snapshot.helpers.getTempRet0, snapshot.module, []), 'getTempRet0 result');
  if (compareTempRet && currentTempRetBits !== snapshot.tempRetBits) refuse('tempRet0 bits changed');
  return Object.freeze({ tempRetBits: currentTempRetBits });
}

/*
 * Existing QuiescentWasmSnapshot owns the full heap copy and refuses async,
 * stack drift, memory replacement, and growth. tempRet0 is an independent
 * i32 global, so restore it with Emscripten's existing helper after heap copy.
 */
export function restoreHeapAndTempRetV1(snapshot) {
  assertWasmOwnershipBoundaryV1(snapshot);
  PRIVATE_BOUNDARIES.get(snapshot).quiescentSnapshot.restore();
  Reflect.apply(snapshot.helpers.setTempRet0, snapshot.module, [snapshot.tempRetBits | 0]);
  const restored = asU32(Reflect.apply(snapshot.helpers.getTempRet0, snapshot.module, []), 'getTempRet0 result');
  if (restored !== snapshot.tempRetBits) refuse('tempRet0 restore did not preserve exact bits');
  assertWasmOwnershipBoundaryV1(snapshot, { compareTempRet: true });
  return Object.freeze({ restoredTempRetBits: restored, stackBits: snapshot.stackBits });
}
