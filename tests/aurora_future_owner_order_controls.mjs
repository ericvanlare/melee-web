import assert from 'node:assert/strict';
import {assertNativeFutureOrder} from './aurora_future_owner_probe.mjs';
// State checkpoints observed in the first fresh real-Emdawn diagnostic.
// Flipping the native publication-order flag must invalidate each checkpoint.
const baseline = {before_adapter_state: 3, before_work_state: 255,
  after_work_state: 2559, cleanup_state: 322047};
assert.doesNotThrow(() => assertNativeFutureOrder(baseline));
for (const name of Object.keys(baseline)) {
  assert.throws(() => assertNativeFutureOrder({...baseline, [name]: baseline[name] | (1 << 10)}),
    /callback preceded Future publication/);
  for (const state of [undefined, -1, 0.5, 0x100000000])
    assert.throws(() => assertNativeFutureOrder({...baseline, [name]: state}), /invalid native state checkpoint/);
}
console.log('native Future publication-order controls passed');
