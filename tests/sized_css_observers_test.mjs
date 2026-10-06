import assert from 'node:assert/strict';
import {installSizedCssObservers} from './pause_trace_capture.mjs';
const previous = {window: globalThis.window, Module: globalThis.Module};
try {
  const heap = new ArrayBuffer(4096), allocations = new Map();
  let next = 128, freed = 0, calls = 0;
  const observe = (ids, geometry) => {
    // Native export declares ids[14]/geometry[8] and writes ids[13]/geometry[7].
    assert.equal(allocations.get(ids), 56);
    assert.equal(allocations.get(geometry), 32);
    for (let i = 0; i < 14; i++) Module.HEAP32[(ids >> 2) + i] = 100 + i;
    for (let i = 0; i < 8; i++) Module.HEAPF32[(geometry >> 2) + i] = i + .5;
    calls++; return 1;
  };
  globalThis.Module = {
    HEAP32: new Int32Array(heap), HEAPF32: new Float32Array(heap),
    _malloc(size) { const ptr = next; next += size; allocations.set(ptr, size); return ptr; },
    _free(ptr) { assert.ok(allocations.delete(ptr)); freed++; },
    _melee_web_native_menu_phase: () => 1,
    _melee_web_css_observe_port: (port, kind, ids, geometry) => observe(ids, geometry),
    _melee_web_css_observe: (kind, ids, geometry) => observe(ids, geometry),
  };
  globalThis.window = {menuObserveFighterPort() {}, menuObserveFighter() {}};
  const installed = await installSizedCssObservers({evaluate: async fn => fn()});
  assert.equal(installed.ids_bytes, 56); assert.equal(installed.geometry_bytes, 32);
  for (const value of [window.menuObserveFighterPort(2, 8), window.menuObserveFighter(8)]) {
    assert.deepEqual(value.ids, [100, 101, 102, 103]);
    assert.deepEqual(value.geometry, [.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5]);
  }
  assert.equal(calls, 2); assert.equal(freed, 4); assert.equal(allocations.size, 0);
} finally {
  if (previous.window === undefined) delete globalThis.window; else globalThis.window = previous.window;
  if (previous.Module === undefined) delete globalThis.Module; else globalThis.Module = previous.Module;
}
console.log('CSS observer native-sized storage contract passed');
