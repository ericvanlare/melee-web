// Test-only Node diagnostic implementation for bounded full-linear-memory
// snapshots. It has no browser/player API and no renderer, audio-sink, or
// source-runtime behavior. The caller must use call() around one synchronous
// exported source call, then capture() only after that call returns, matching
// QuiescentWasmSnapshot.
import crypto from 'node:crypto';

const DEFAULT_MAX_BYTES = 512 * 1024 * 1024;
const WASM_PAGE_BYTES = 64 * 1024;
const DEFAULT_MAX_SNAPSHOTS = 8;

function digest(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

class ImmutablePage {
  #bytes;
  #sha256;

  constructor(view, sha256) {
    this.#bytes = Buffer.from(view);
    this.#sha256 = sha256 || digest(this.#bytes);
  }

  get byteLength() { return this.#bytes.byteLength; }
  get sha256() { return this.#sha256; }
  equals(view) {
    const candidate = Buffer.from(view.buffer, view.byteOffset, view.byteLength);
    return this.#bytes.equals(candidate);
  }
  copyInto(heap, offset) { heap.set(this.#bytes, offset); }
}

class PageEntry {
  #page;
  #references = 0;

  constructor(page) { this.#page = page; }
  get page() { return this.#page; }
  get references() { return this.#references; }
  retain() { this.#references += 1; }
  release() {
    if (this.#references < 1) throw new Error('Shared page reference underflow');
    this.#references -= 1;
  }
}

export class SharedPageWasmSnapshotRing {
  #module;
  #busy = false;
  #closed = false;
  #limit;
  #pageBytes;
  #maxSnapshots;
  #buffer;
  #capacity;
  #maxRetainedPageBytes;
  #retainedPageBytes = 0;
  #pool = new Map();
  #snapshots = new Set();
  #lastCapture = null;

  constructor(module, {
    maxBytes = DEFAULT_MAX_BYTES,
    pageBytes = WASM_PAGE_BYTES,
    maxSnapshots = DEFAULT_MAX_SNAPSHOTS,
    maxRetainedPageBytes = 256 * 1024 * 1024,
  } = {}) {
    if (!(module?.HEAPU8 instanceof Uint8Array) ||
        typeof module.stackSave !== 'function' ||
        typeof module.stackRestore !== 'function') {
      throw new Error('Missing diagnostic Wasm memory/stack exports');
    }
    if (typeof SharedArrayBuffer !== 'undefined' &&
        module.HEAPU8.buffer instanceof SharedArrayBuffer) {
      throw new Error('Concurrent Wasm memory is outside the diagnostic boundary');
    }
    if (!Number.isSafeInteger(maxBytes) || maxBytes <= 0 ||
        !Number.isSafeInteger(pageBytes) || pageBytes <= 0 ||
        !Number.isSafeInteger(maxSnapshots) || maxSnapshots < 1 ||
        maxSnapshots > DEFAULT_MAX_SNAPSHOTS ||
        !Number.isSafeInteger(maxRetainedPageBytes) || maxRetainedPageBytes <= 0) {
      throw new Error('Invalid shared-page snapshot limits');
    }
    this.#module = module;
    this.#limit = maxBytes;
    this.#pageBytes = pageBytes;
    this.#maxSnapshots = maxSnapshots;
    this.#maxRetainedPageBytes = maxRetainedPageBytes;
    this.#buffer = module.HEAPU8.buffer;
    this.#capacity = module.HEAPU8.byteLength;
    this.#validateMemory();
    if (this.#capacity > this.#limit || this.#capacity % this.#pageBytes !== 0) {
      throw new Error('Wasm memory capacity is outside the shared-page snapshot budget');
    }
  }

  #validateOpen() {
    if (this.#closed) throw new Error('Shared-page snapshot owner is closed');
  }

  #validateMemory() {
    this.#validateOpen();
    const heap = this.#module.HEAPU8;
    if (!(heap instanceof Uint8Array) || heap.buffer !== this.#buffer ||
        heap.byteOffset !== 0 || heap.byteLength !== heap.buffer.byteLength ||
        heap.byteLength !== this.#capacity ||
        (typeof SharedArrayBuffer !== 'undefined' &&
         heap.buffer instanceof SharedArrayBuffer)) {
      throw new Error('Diagnostic Wasm memory identity, view, or capacity changed');
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
    } finally {
      if (!asyncCall) this.#busy = false;
    }
  }

  #mostRecentSnapshot() {
    let latest = null;
    for (const record of this.#snapshots) latest = record;
    return latest;
  }

  #findOrCreate(view, hintEntry, metrics) {
    if (hintEntry) {
      metrics.hintComparisons += 1;
      if (hintEntry.page.equals(view)) {
        metrics.hintHits += 1;
        hintEntry.retain();
        return hintEntry;
      }
    }
    metrics.hashedPages += 1;
    const sha256 = digest(view);
    const candidates = this.#pool.get(sha256) || [];
    for (const entry of candidates) {
      if (entry.page.equals(view)) {
        entry.retain();
        return entry;
      }
    }
    if (this.#retainedPageBytes + view.byteLength > this.#maxRetainedPageBytes) {
      throw new Error('Shared-page retained payload exceeds its memory budget');
    }
    const entry = new PageEntry(new ImmutablePage(view, sha256));
    this.#retainedPageBytes += view.byteLength;
    entry.retain();
    candidates.push(entry);
    this.#pool.set(sha256, candidates);
    return entry;
  }

  #dropEntry(entry) {
    if (entry.references !== 0) return;
    this.#retainedPageBytes -= entry.page.byteLength;
    const hash = entry.page.sha256;
    const candidates = this.#pool.get(hash) || [];
    const remaining = candidates.filter(candidate => candidate !== entry);
    if (remaining.length) this.#pool.set(hash, remaining);
    else this.#pool.delete(hash);
  }

  #releaseRecord(record) {
    if (record.released) throw new Error('Shared-page snapshot is already released');
    for (const entry of record.entries) {
      entry.release();
      this.#dropEntry(entry);
    }
    // Released handles may remain in caller history. They must not keep page
    // payloads alive after the pool's last reference is released.
    record.entries = [];
    record.released = true;
    this.#snapshots.delete(record);
    if (this.#snapshots.size === 0) this.#lastCapture = null;
  }

  capture() {
    this.#validateMemory();
    if (this.#busy) throw new Error('Snapshot requires an exported-call return');
    if (this.#snapshots.size >= this.#maxSnapshots) {
      throw new Error(`Shared-page snapshot limit is ${this.#maxSnapshots}; release one first`);
    }
    const heap = this.#module.HEAPU8;
    const entries = [];
    let stack;
    const hintRecord = this.#mostRecentSnapshot();
    const metrics = {
      hintedSnapshot: hintRecord !== null,
      pageCount: 0,
      hintComparisons: 0,
      hintHits: 0,
      hashedPages: 0,
    };
    try {
      // Keep stackSave inside the rollback region: a malformed/failed export
      // must not leave pages retained without a live snapshot record.
      stack = this.#module.stackSave();
      for (let offset = 0; offset < this.#capacity; offset += this.#pageBytes) {
        metrics.pageCount += 1;
        const hintEntry = hintRecord?.entries[offset / this.#pageBytes] || null;
        entries.push(this.#findOrCreate(
          heap.subarray(offset, offset + this.#pageBytes), hintEntry, metrics));
      }
    } catch (error) {
      for (const entry of entries) {
        entry.release();
        this.#dropEntry(entry);
      }
      throw error;
    }
    const record = { entries, stack, released: false };
    this.#snapshots.add(record);
    this.#lastCapture = Object.freeze(metrics);
    const owner = this;
    return Object.freeze({
      byteLength: this.#capacity,
      pageBytes: this.#pageBytes,
      pageCount: entries.length,
      restore() { owner.#restoreRecord(record); },
      release() { owner.#releaseRecord(record); },
    });
  }

  #restoreRecord(record) {
    if (this.#busy) throw new Error('Restore requires an exported-call return');
    this.#validateMemory();
    if (record.released || !this.#snapshots.has(record)) {
      throw new Error('Shared-page snapshot is invalid or released');
    }
    if (this.#module.HEAPU8.byteLength !== record.entries.length * this.#pageBytes) {
      throw new Error('Wasm memory capacity changed; shared-page restoration is unsupported');
    }
    if (this.#module.stackSave() !== record.stack) {
      throw new Error('Wasm stack is not at the captured boundary');
    }
    const heap = this.#module.HEAPU8;
    for (let index = 0; index < record.entries.length; index++) {
      record.entries[index].page.copyInto(heap, index * this.#pageBytes);
    }
    this.#module.stackRestore(record.stack);
  }

  stats() {
    this.#validateMemory();
    let uniquePageBytes = 0;
    let uniquePageCount = 0;
    for (const candidates of this.#pool.values()) {
      for (const entry of candidates) {
        uniquePageCount += 1;
        uniquePageBytes += entry.page.byteLength;
      }
    }
    return {
      closed: false,
      scope: 'all bytes of every full HEAPU8 page; no excluded ranges',
      capacityBytes: this.#capacity,
      pageBytes: this.#pageBytes,
      pageCount: this.#capacity / this.#pageBytes,
      maxSnapshots: this.#maxSnapshots,
      retainedSnapshots: this.#snapshots.size,
      retainedLogicalBytes: this.#snapshots.size * this.#capacity,
      uniquePageCount,
      uniquePageBytes,
      maxRetainedPageBytes: this.#maxRetainedPageBytes,
      uniqueOwnedPageBytes: uniquePageBytes,
      deduplicatedBytes: this.#snapshots.size * this.#capacity - uniquePageBytes,
      lastCapture: this.#lastCapture ? { ...this.#lastCapture } : null,
    };
  }

  close() {
    if (this.#busy) throw new Error('Cannot close an active diagnostic call');
    if (this.#closed) return;
    for (const record of [...this.#snapshots]) this.#releaseRecord(record);
    this.#closed = true;
    this.#pool.clear();
  }
}

export const sharedPageSnapshotConstants = Object.freeze({
  wasmPageBytes: WASM_PAGE_BYTES,
  defaultMaxSnapshots: DEFAULT_MAX_SNAPSHOTS,
  defaultMaxBytes: DEFAULT_MAX_BYTES,
});
