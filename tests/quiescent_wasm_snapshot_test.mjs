import assert from 'node:assert/strict';
import { QuiescentWasmSnapshot } from './quiescent_wasm_snapshot.mjs';

function fixture() {
  let stack = 64;
  const module = { HEAPU8: new Uint8Array(128), stackSave: () => stack,
    stackRestore: value => { stack = value; } };
  return { module, setStack: value => { stack = value; } };
}
{
  const { module } = fixture();
  const boundary = new QuiescentWasmSnapshot(module);
  module.HEAPU8[20] = 7;
  const snapshot = boundary.capture();
  for (let repeat = 0; repeat < 3; repeat++) {
    module.HEAPU8.fill(91);
    snapshot.restore();
    assert.equal(module.HEAPU8[20], 7);
    assert.equal(module.HEAPU8[21], 0);
  }
  boundary.call(() => {
    assert.throws(() => boundary.capture(), /exported-call return/);
    assert.throws(() => snapshot.restore(), /exported-call return/);
    assert.throws(() => boundary.call(() => 1), /not reentrant/);
  });
  assert.throws(() => boundary.call(() => { throw new Error('native failure'); }), /native failure/);
  snapshot.restore(); // A failed call must release the host call guard.
}
{
  const { module, setStack } = fixture();
  const snapshot = new QuiescentWasmSnapshot(module).capture();
  module.HEAPU8[20] = 9;
  setStack(60);
  assert.throws(() => snapshot.restore(), /stack/);
  assert.equal(module.HEAPU8[20], 9); // Refusal must precede any copying.
  setStack(64);
  module.HEAPU8 = new Uint8Array(256);
  module.HEAPU8[20] = 11;
  assert.throws(() => snapshot.restore(), /memory identity/);
  assert.equal(module.HEAPU8[20], 11);
}
{
  const { module } = fixture();
  const boundary = new QuiescentWasmSnapshot(module);
  const snapshot = boundary.capture();
  module.HEAPU8 = new Uint8Array(128);
  module.HEAPU8[20] = 12;
  assert.throws(() => snapshot.restore(), /memory identity/);
  assert.equal(module.HEAPU8[20], 12);
  const partial = { ...module, HEAPU8: new Uint8Array(new ArrayBuffer(256), 64, 128) };
  assert.throws(() => new QuiescentWasmSnapshot(partial), /full-buffer/);
}
{
  const boundary = new QuiescentWasmSnapshot(fixture().module);
  const snapshot = boundary.capture();
  assert.throws(() => boundary.call(() => Promise.resolve(1)), /poisoned/);
  assert.throws(() => snapshot.restore(), /exported-call/);
  assert.throws(() => boundary.capture(), /exported-call/);
}
{
  const boundary = new QuiescentWasmSnapshot(fixture().module);
  const snapshot = boundary.capture();
  boundary.close();
  assert.throws(() => snapshot.restore(), /closed/);
  assert.throws(() => boundary.call(() => 1), /closed/);
  assert.throws(() => boundary.capture(), /closed/);
}
{
  const first = fixture().module, second = fixture().module;
  first.HEAPU8[20] = 3;second.HEAPU8[20] = 5;
  const snapshot = new QuiescentWasmSnapshot(first).capture();
  first.HEAPU8[20] = 8;snapshot.restore();
  assert.equal(first.HEAPU8[20], 3);
  assert.equal(second.HEAPU8[20], 5);
  assert.throws(() => new QuiescentWasmSnapshot(first, 64).capture(), /memory budget/);
  assert.throws(() => new QuiescentWasmSnapshot({}), /Missing/);
  if (typeof SharedArrayBuffer !== 'undefined') {
    const shared = { ...first, HEAPU8: new Uint8Array(new SharedArrayBuffer(128)) };
    assert.throws(() => new QuiescentWasmSnapshot(shared), /Concurrent/);
  }
}
console.log('Quiescent snapshot boundary controls passed');
