import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
const source = fs.readFileSync(new URL('../web/runtime-development.mjs', import.meta.url), 'utf8');
const helpers = source.split('\n').filter(line => /^window\.menuObserveFighter(?:Port)?=/.test(line)).join('\n');
assert.equal(helpers.split('\n').length, 2);
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
