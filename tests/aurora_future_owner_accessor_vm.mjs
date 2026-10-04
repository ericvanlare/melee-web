#!/usr/bin/env node
/*
 * Focused VM controls for the generated Aurora JS-library accessor.
 *
 * This is intentionally a host-store fixture: it executes the exact pinned
 * library body after bootstrap, but does not create a WebGPU device, submit
 * work, enroll callbacks, or claim Wasm/browser coverage.  The browser probe
 * is the later real-HTTP boundary for the ABI-only target.
 */
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const explicitLibrary = process.argv.indexOf('--library');
const libraryPath = explicitLibrary >= 0
  ? path.resolve(process.argv[explicitLibrary + 1] || '')
  : path.resolve(here, '..', '.deps', 'aurora', 'lib', 'gfx',
    'aurora_browser_future_owner_accessor_library.js');

function fail(message) {
  throw new Error(message);
}

function equal(actual, expected, label) {
  if (actual !== expected) fail(`${label}: expected ${expected}, got ${actual}`);
}

function unchanged(before, after, label) {
  if (before.length !== after.length) fail(`${label}: length changed`);
  for (let i = 0; i < before.length; i += 1) {
    if (before[i] !== after[i]) fail(`${label}: byte ${i} changed`);
  }
}

function makeRuntime() {
  if (!fs.existsSync(libraryPath)) {
    fail(`Aurora is not bootstrapped: ${libraryPath}`);
  }
  const source = fs.readFileSync(libraryPath, 'utf8');
  let library = null;
  const context = vm.createContext({
    addToLibrary(value) { library = value; },
    console,
  });
  vm.runInContext(source, context, { filename: libraryPath });
  if (!library || typeof library.$AuroraBrowserFutureOwnerV5__postset !== 'string') {
    fail('library did not register the module-local postset');
  }
  const heap = new Uint8Array(4096);
  context.HEAPU8 = heap;
  context.HEAPU32 = new Uint32Array(heap.buffer);
  context.WebGPU = { Internals: { futures: [], jsObjects: [] } };
  vm.runInContext(library.$AuroraBrowserFutureOwnerV5__postset, context,
    { filename: `${libraryPath}:postset` });
  if (!context.AuroraBrowserFutureOwnerV5) fail('postset did not install accessor');
  return { context, heap, accessor: context.AuroraBrowserFutureOwnerV5 };
}

function readU32(heap, offset) {
  return new DataView(heap.buffer).getUint32(offset, true);
}

function readU64(heap, offset) {
  return new DataView(heap.buffer).getBigUint64(offset, true);
}

function assertSummary(runtime, expectedRows, label, expectedFutureRows = expectedRows) {
  const { accessor, heap } = runtime;
  equal(accessor.summary(8, 96), 1, `${label} summary`);
  equal(readU32(heap, 8), 2, `${label} schema`);
  equal(Number(readU64(heap, 8 + 80)), expectedFutureRows, `${label} future rows`);
  equal(accessor.rowCount(), expectedRows, `${label} row count`);
}

function sparseAndIdentityControls() {
  const runtime = makeRuntime();
  const { accessor, context, heap } = runtime;
  const first = { role: 'future' };
  context.WebGPU.Internals.futures[3] = first;
  context.WebGPU.Internals.futures[7] = undefined;
  context.WebGPU.Internals.jsObjects[5] = { role: 'object' };
  assertSummary(runtime, 3, 'sparse', 2);
  equal(accessor.row(16, 32, 0), 1, 'first row');
  equal(Number(readU64(heap, 16 + 16)), 3, 'first sparse ordinal');

  const before = heap.slice(0);
  equal(accessor.summary(9, 96), -2, 'unaligned output refusal');
  unchanged(before, heap, 'unaligned output no-write');
  equal(accessor.row(16, 32, -1), -7, 'negative ordinal refusal');
  unchanged(before, heap, 'negative ordinal no-write');

  let getterCalls = 0;
  Object.defineProperty(context.WebGPU.Internals.futures, '4', {
    configurable: true,
    enumerable: true,
    get() { getterCalls += 1; return first; },
  });
  equal(accessor.summary(8, 96), -5, 'getter refusal');
  equal(getterCalls, 0, 'getter was not invoked');
  equal(accessor.rowCount(), 0, 'failed capture invalidates rows');
  delete context.WebGPU.Internals.futures[4];

  equal(accessor.dispose(), 1, 'dispose');
  context.WebGPU.Internals.futures = [first];
  context.WebGPU.Internals.jsObjects = [];
  assertSummary(runtime, 1, 'fresh after dispose');
  context.WebGPU.Internals.futures[0] = { replaced: true };
  equal(accessor.summary(8, 96), -4, 'entry replacement refusal');
  equal(accessor.rowCount(), 0, 'replacement invalidates rows');
}

function budgetAndIndependentContextControls() {
  const refBudget = makeRuntime();
  const refs = Array.from({ length: 4095 }, (_, index) => ({ index }));
  refBudget.context.WebGPU.Internals.futures = refs;
  const before = refBudget.heap.slice(0);
  equal(refBudget.accessor.summary(8, 96), -6, 'retained-reference budget');
  unchanged(before, refBudget.heap, 'retained-reference budget no-write');
  equal(refBudget.accessor.rowCount(), 0, 'reference budget invalidates rows');
  equal(refBudget.accessor.dispose(), 1, 'reference-budget dispose');
  refBudget.context.WebGPU.Internals.futures = [{ fresh: true }];
  refBudget.context.WebGPU.Internals.jsObjects = [];
  assertSummary(refBudget, 1, 'fresh after reference budget');

  const entryBudget = makeRuntime();
  entryBudget.context.WebGPU.Internals.futures = Array.from(
    { length: 65537 }, () => undefined,
  );
  const entryBefore = entryBudget.heap.slice(0);
  equal(entryBudget.accessor.summary(8, 96), -6, 'entry budget');
  unchanged(entryBefore, entryBudget.heap, 'entry budget no-write');
  equal(entryBudget.accessor.rowCount(), 0, 'entry budget invalidates rows');

  const other = makeRuntime();
  other.context.WebGPU.Internals.futures[2] = { owner: 'other' };
  assertSummary(other, 1, 'independent context');
  equal(refBudget.accessor.rowCount(), 1, 'contexts retain independent state');
}

sparseAndIdentityControls();
budgetAndIndependentContextControls();
console.log(JSON.stringify({
  status: 'pass',
  scope: 'module-local read-only accessor VM controls',
  cases: [
    'sparse holes and own undefined',
    'descriptor getter refusal without invocation',
    'identity replacement and dispose reset',
    'invalid pointer/ordinal zero-write refusal',
    'retained-reference and tracked-entry budgets',
    'independent module contexts',
  ],
  browser: false,
  webgpuDevice: false,
}));
