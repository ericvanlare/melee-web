// Diagnostic-only full-memory snapshots for gameplay_snapshot_probe. This is
// deliberately not a snapshot API for the browser player: no JS/GPU/audio sink
// or async host state is restored. Only the synchronous source-only target may
// use it, at an exported-call return with the same stack and memory capacity.
export class QuiescentWasmSnapshot {
  #module;
  #busy = false;
  #closed = false;
  #limit;
  #buffer;
  constructor(module, maxBytes = 512 * 1024 * 1024) {
    if (!(module.HEAPU8 instanceof Uint8Array) ||
        typeof module.stackSave !== 'function' || typeof module.stackRestore !== 'function') {
      throw new Error('Missing diagnostic Wasm memory/stack exports');
    }
    if (typeof SharedArrayBuffer !== 'undefined' && module.HEAPU8.buffer instanceof SharedArrayBuffer) {
      throw new Error('Concurrent Wasm memory is outside the diagnostic boundary');
    }
    this.#module = module;
    this.#limit = maxBytes;
    this.#buffer = module.HEAPU8.buffer;
    this.#validateMemory();
  }
  #validateMemory() {
    const heap = this.#module.HEAPU8;
    if (this.#closed) throw new Error('Diagnostic snapshot owner is closed');
    if (!(heap instanceof Uint8Array) || heap.buffer !== this.#buffer ||
        heap.byteOffset !== 0 || heap.byteLength !== heap.buffer.byteLength ||
        (typeof SharedArrayBuffer !== 'undefined' && heap.buffer instanceof SharedArrayBuffer)) {
      throw new Error('Diagnostic Wasm memory identity or full-buffer view changed');
    }
  }
  call(fn) {
    this.#validateMemory();
    if (this.#busy) throw new Error('Diagnostic Wasm call is not reentrant');
    this.#busy = true;
    let asyncCall = false;
    try {
      const result = fn();
      if (result && typeof result.then === 'function') {
        asyncCall = true;
        throw new Error('Async Wasm call poisoned the diagnostic boundary');
      }
      return result;
    } finally { if (!asyncCall) this.#busy = false; }
  }
  close() {
    if (this.#busy) throw new Error('Cannot close an active diagnostic call');
    this.#closed = true;
  }
  capture() {
    this.#validateMemory();
    if (this.#busy) throw new Error('Snapshot requires an exported-call return');
    const heap = this.#module.HEAPU8;
    if (heap.byteLength > this.#limit) throw new Error('Snapshot exceeds the diagnostic memory budget');
    const owner = this;
    const bytes = heap.slice();
    const stack = this.#module.stackSave();
    return Object.freeze({ byteLength: bytes.byteLength, restore: () => {
      if (owner.#busy) throw new Error('Restore requires an exported-call return');
      owner.#validateMemory();
      if (owner.#module.HEAPU8.byteLength !== bytes.byteLength) {
        throw new Error('Wasm memory capacity changed; diagnostic restoration is unsupported');
      }
      if (owner.#module.stackSave() !== stack) throw new Error('Wasm stack is not at its captured boundary');
      owner.#module.HEAPU8.set(bytes);
      owner.#module.stackRestore(stack);
    } });
  }
}
