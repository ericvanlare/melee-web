import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const source = fs.readFileSync(new URL('../web/runtime-development.mjs', import.meta.url), 'utf8');
const helpers = source.split('\n').filter(line => /^window\.menuObserveFighter(?:Port)?=/.test(line)).join('\n');
assert.equal(helpers.split('\n').length, 2);
// Match melee_web_css_observe_port's ids[14], geometry[8] declaration.
const nativeCssPortIds = 14;
const nativeCssGeometry = 8;
function snapshotBlock(path) {
  const harness = fs.readFileSync(new URL(path, import.meta.url), 'utf8');
  const begin = harness.indexOf('// CSS_PORT_OBSERVER_ABI_BEGIN');
  const contentStart = harness.indexOf('\n', begin) + 1;
  const end = harness.indexOf('// CSS_PORT_OBSERVER_ABI_END', contentStart);
  assert(begin >= 0 && contentStart > 0 && end > contentStart, `${path} snapshot ABI block is present`);
  return harness.slice(contentStart, end);
}
function snapshotFixture(path, {failAllocation = 0, nativeResult = 1, phase = 1} = {}) {
  const heap = new ArrayBuffer(8192), bytes = new Uint8Array(heap);
  const Module = {HEAP32: new Int32Array(heap), HEAPF32: new Float32Array(heap)};
  const allocations = new Map(), guards = new Map();
  let next = 256, allocationCalls = 0, nativeCalls = 0, freeCalls = 0;
  const nativeArguments = [];
  const guardBytes = 16, guardValue = 0xa5;
  const checkGuard = ptr => {
    const size = allocations.get(ptr), expected = guards.get(ptr);
    assert.deepEqual(Array.from(bytes.slice(ptr + size, ptr + size + guardBytes)), expected,
      'native writes stay within the allocation boundary');
  };
  Module._malloc = size => {
    if (++allocationCalls === failAllocation) return 0;
    const ptr = next;
    next += size + guardBytes + 16;
    allocations.set(ptr, size);
    bytes.fill(guardValue, ptr + size, ptr + size + guardBytes);
    guards.set(ptr, Array(guardBytes).fill(guardValue));
    return ptr;
  };
  Module._free = ptr => {
    assert(allocations.has(ptr), 'only a live allocation is freed');
    checkGuard(ptr);
    allocations.delete(ptr); guards.delete(ptr); freeCalls++;
  };
  Module._melee_web_css_observe_port = (port, kind, ids, geometry) => {
    nativeCalls++;
    nativeArguments.push([port, kind]);
    assert(allocations.get(ids) >= nativeCssPortIds * Int32Array.BYTES_PER_ELEMENT,
      'native int[14] output has complete storage');
    assert(allocations.get(geometry) >= nativeCssGeometry * Float32Array.BYTES_PER_ELEMENT,
      'native float[8] output has complete storage');
    for (let i = 0; i < nativeCssPortIds; ++i)
      Module.HEAP32[(ids >> 2) + i] = 100 + i;
    for (let i = 0; i < nativeCssGeometry; ++i)
      Module.HEAPF32[(geometry >> 2) + i] = i + .5;
    for (const ptr of allocations.keys()) checkGuard(ptr);
    return nativeResult;
  };
  const invoke = vm.runInNewContext(`(module, phase, captureCss = true) => { let css = null;\n${snapshotBlock(path)}\nreturn css; }`);
  return {run: () => invoke(Module, phase), allocations, nativeCalls: () => nativeCalls,
    nativeArguments,
    freeCalls: () => freeCalls};
}
for (const path of ['../scripts/capture_whole_session_browser.mjs', './character_reference_browser_test.mjs']) {
  test(`${path} snapshot executes the complete native CSS port observer ABI`, () => {
    const f = snapshotFixture(path);
    const value = JSON.parse(JSON.stringify(f.run()));
    assert.deepEqual(value, Array(4).fill({ids: [100, 101, 102, 103],
      geometry: [.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5]}));
    assert.equal(f.nativeCalls(), 4); assert.equal(f.allocations.size, 0); assert.equal(f.freeCalls(), 2);
    assert.deepEqual(f.nativeArguments, [[0, 8], [1, 8], [2, 8], [3, 8]]);
  });
  test(`${path} snapshot frees a surviving CSS observer allocation on either OOM`, () => {
    for (const failAllocation of [1, 2]) {
      const f = snapshotFixture(path, {failAllocation});
      assert.throws(f.run, /CSS observation allocation failed/);
      assert.equal(f.nativeCalls(), 0); assert.equal(f.allocations.size, 0); assert.equal(f.freeCalls(), 1);
    }
  });
  test(`${path} snapshot preserves four-port nulls and skips observation outside CSS`, () => {
    const noObservation = snapshotFixture(path, {nativeResult: 0});
    assert.deepEqual(JSON.parse(JSON.stringify(noObservation.run())), [null, null, null, null]);
    assert.equal(noObservation.nativeCalls(), 4);
    assert.equal(noObservation.allocations.size, 0); assert.equal(noObservation.freeCalls(), 2);
    const outsideCss = snapshotFixture(path, {phase: 7});
    assert.equal(outsideCss.run(), null); assert.equal(outsideCss.nativeCalls(), 0);
    assert.equal(outsideCss.allocations.size, 0); assert.equal(outsideCss.freeCalls(), 0);
  });
}
function fixture({failAllocation = 0, nativeResult = 1, phase = 1} = {}) {
  const heap = new ArrayBuffer(4096), allocations = new Map();
  let next = 128, allocationCalls = 0, nativeCalls = 0;
  const Module = {
    HEAP32: new Int32Array(heap), HEAPF32: new Float32Array(heap),
    _malloc(size) { if (++allocationCalls === failAllocation) return 0;
      const ptr = next; next += size; allocations.set(ptr, size); return ptr; },
    _free(ptr) { assert.ok(allocations.delete(ptr)); },
    _melee_web_native_menu_phase: () => phase,
  };
  const observe = (ids, geometry) => {
    nativeCalls++;
    // Actual exported CSS contract: ids[14], geometry[8], including writes to 13/7.
    assert.equal(allocations.get(ids), 56); assert.equal(allocations.get(geometry), 32);
    for (let i = 0; i < 14; i++) Module.HEAP32[(ids >> 2) + i] = 100 + i;
    for (let i = 0; i < 8; i++) Module.HEAPF32[(geometry >> 2) + i] = i + .5;
    return nativeResult;
  };
  Module._melee_web_css_observe = (kind, ids, geometry) => observe(ids, geometry);
  Module._melee_web_css_observe_port = (port, kind, ids, geometry) => observe(ids, geometry);
  const window = {};
  vm.runInNewContext(helpers, {window, Module});
  return {window, allocations, nativeCalls: () => nativeCalls};
}
for (const name of ['menuObserveFighter', 'menuObserveFighterPort']) {
  const call = f => name.endsWith('Port') ? f.window[name](2, 8) : f.window[name](8);
  test(`${name} actual development source accepts complete native writes and preserves return shape`, () => {
    const f = fixture(), value = call(f);
    assert.deepEqual(Array.from(value.ids), [100, 101, 102, 103]);
    assert.deepEqual(Array.from(value.geometry), [.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5]);
    assert.equal(f.nativeCalls(), 1); assert.equal(f.allocations.size, 0);
  });
  test(`${name} skips native writes and frees surviving allocation on either OOM`, () => {
    for (const failAllocation of [1, 2]) {
      const f = fixture({failAllocation}); assert.equal(call(f), null);
      assert.equal(f.nativeCalls(), 0); assert.equal(f.allocations.size, 0);
    }
  });
  test(`${name} frees full storage when native observation is unavailable`, () => {
    const f = fixture({nativeResult: 0}); assert.equal(call(f), null);
    assert.equal(f.allocations.size, 0);
  });
}
