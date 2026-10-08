import {LOCKSTEP_DELAY, NET_FRAME_BYTES, NET_RECORD_BYTES, PAD_BYTES} from './net_lockstep_core.mjs';

const UINT32_MAX = 0xffffffff;
const REQUIRED_EXPORTS = Object.freeze([
  '_malloc', '_free', '_melee_web_net_push', '_melee_web_net_push_indexed',
  '_melee_web_net_enable_local_input_capture', '_melee_web_net_confirm_start',
  '_melee_web_net_terminate', '_melee_web_net_checksum_drain', '_melee_web_net_status',
]);

function bytesOf(value, label) {
  if (value instanceof Uint8Array) return value;
  if (ArrayBuffer.isView(value) && value.BYTES_PER_ELEMENT === 1)
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  throw Error(`${label} must be a byte array`);
}

/** Development-only owner of the native network ABI scratch allocation. */
export function createNetLockstepNativeAdapter(Module, {subscribeProgress: subscribeProgressOwner = null} = {}) {
  if (!Module || typeof Module !== 'object' ||
      REQUIRED_EXPORTS.some(name => typeof Module[name] !== 'function') ||
      typeof Module.UTF8ToString !== 'function')
    throw Error('Native network adapter requires the complete browser ABI');
  if (subscribeProgressOwner !== null && typeof subscribeProgressOwner !== 'function')
    throw Error('Native network progress subscription must be a function');
  if (NET_FRAME_BYTES !== PAD_BYTES * 4 || !Number.isSafeInteger(LOCKSTEP_DELAY) || LOCKSTEP_DELAY < 0)
    throw Error('Native network adapter core byte and delay constants are inconsistent');

  let pointer = 0, capacity = 0, disposed = false, progressUnsubscribe = null;
  const assertActive = () => { if (disposed) throw Error('Native network adapter is disposed'); };
  const ensureCapacity = size => {
    assertActive();
    if (!Number.isSafeInteger(size) || size < 0) throw Error('Native network buffer size is invalid');
    if (size > UINT32_MAX) throw Error('Native network buffer exceeds the 32-bit Wasm ABI range');
    if (size <= capacity) return pointer;
    const next = Module._malloc(size);
    if (!Number.isSafeInteger(next) || next <= 0) {
      throw Error('Networked scratch allocation failed');
    }
    const heap = Module.HEAPU8;
    if (!(heap instanceof Uint8Array) || next > heap.length || size > heap.length - next) {
      Module._free(next);
      throw Error('Networked scratch allocation is outside the current Wasm heap');
    }
    const previous = pointer;
    pointer = next;
    capacity = size;
    if (previous) Module._free(previous);
    return pointer;
  };
  const copyInput = (value, label, frameBytes) => {
    const bytes = bytesOf(value, label);
    if (bytes.byteLength % frameBytes !== 0)
      throw Error(`${label} must contain complete ${frameBytes}-byte frames`);
    // _malloc may grow WebAssembly.Memory and detach a caller's HEAP-backed view.
    const input = Uint8Array.from(bytes);
    const ptr = ensureCapacity(input.byteLength);
    const heap = Module.HEAPU8;
    if (!(heap instanceof Uint8Array) || ptr > heap.length || input.byteLength > heap.length - ptr)
      throw Error('Networked scratch input is outside the current Wasm heap');
    heap.set(input, ptr);
    return {ptr, count: input.byteLength / frameBytes};
  };
  const accepted = (value, label) => {
    if (value !== 0 && value !== 1) throw Error(`Native ${label} returned an invalid result`);
    return value === 1;
  };
  const push = bytes => {
    const {ptr, count} = copyInput(bytes, 'Networked input', NET_FRAME_BYTES);
    return accepted(Module._melee_web_net_push(ptr, count), 'input push');
  };
  const pushIndexed = (firstTick, bytes) => {
    assertActive();
    if (!Number.isSafeInteger(firstTick) || firstTick < 0 || firstTick > UINT32_MAX)
      throw Error('Native indexed input tick must be an unsigned 32-bit integer');
    const {ptr, count} = copyInput(bytes, 'Lockstep input', NET_FRAME_BYTES);
    return accepted(Module._melee_web_net_push_indexed(firstTick, ptr, count), 'indexed input push');
  };
  const configureLocalInputCapture = (port, inputTicks) => {
    assertActive();
    if (!Number.isSafeInteger(port) || port < 0 || port > 1 ||
        !Number.isSafeInteger(inputTicks) || inputTicks <= 0 || inputTicks > UINT32_MAX - LOCKSTEP_DELAY)
      throw Error('Native local input capture port or input tick bound is invalid');
    return accepted(Module._melee_web_net_enable_local_input_capture(port, inputTicks),
      'local input capture configuration');
  };
  const drain = maxRecords => {
    assertActive();
    if (!Number.isSafeInteger(maxRecords) || maxRecords < 0 || maxRecords > UINT32_MAX)
      throw Error('Native checksum drain bound is invalid');
    if (!maxRecords) return {count: 0, bytes: new Uint8Array(0)};
    const bytesRequested = maxRecords * NET_RECORD_BYTES;
    if (!Number.isSafeInteger(bytesRequested) || bytesRequested > UINT32_MAX)
      throw Error('Native checksum drain byte bound exceeds the 32-bit Wasm ABI range');
    const ptr = ensureCapacity(bytesRequested);
    const count = Module._melee_web_net_checksum_drain(ptr, maxRecords);
    if (!Number.isSafeInteger(count) || count < 0 || count > maxRecords)
      throw Error('Native checksum drain returned an invalid record count');
    const heap = Module.HEAPU8;
    const byteLength = count * NET_RECORD_BYTES;
    if (!(heap instanceof Uint8Array) || ptr > heap.length || byteLength > heap.length - ptr)
      throw Error('Native checksum records are outside the current Wasm heap');
    return {count, bytes: heap.slice(ptr, ptr + byteLength)};
  };
  const subscribeProgress = callback => {
    assertActive();
    if (!subscribeProgressOwner) throw Error('Native progress subscription is unavailable');
    if (typeof callback !== 'function') throw Error('Native progress subscriber must be a function');
    if (progressUnsubscribe) throw Error('Native progress subscriber already has an owner');
    let active = true;
    const notify = (...args) => { if (active && !disposed) callback(...args); };
    let unsubscribeOwner;
    try { unsubscribeOwner = subscribeProgressOwner(notify); }
    catch (error) { active = false; throw error; }
    if (typeof unsubscribeOwner !== 'function') {
      active = false;
      throw Error('Native progress subscription requires an unsubscribe owner');
    }
    const unsubscribe = () => {
      if (!active) return false;
      active = false;
      if (progressUnsubscribe === unsubscribe) progressUnsubscribe = null;
      unsubscribeOwner();
      return true;
    };
    progressUnsubscribe = unsubscribe;
    return unsubscribe;
  };

  return Object.freeze({
    recordBytes: NET_RECORD_BYTES,
    push,
    pushIndexed,
    configureLocalInputCapture,
    confirmStart() {
      assertActive();
      return accepted(Module._melee_web_net_confirm_start(), 'start confirmation');
    },
    terminate(kind, tick, channel = 0) {
      assertActive();
      for (const value of [kind, tick, channel])
        if (!Number.isSafeInteger(value) || value < 0 || value > UINT32_MAX)
          throw Error('Native terminal fields must be unsigned 32-bit integers');
      Module._melee_web_net_terminate(kind, tick, channel);
    },
    status() {
      assertActive();
      const pointer = Module._melee_web_net_status();
      const heap = Module.HEAPU8;
      if (!Number.isSafeInteger(pointer) || pointer < 0 ||
          !(heap instanceof Uint8Array) || pointer >= heap.length)
        throw Error('Native network status pointer is outside the current Wasm heap');
      const end = heap.indexOf(0, pointer);
      if (end < 0) throw Error('Native network status is not terminated inside the current Wasm heap');
      return JSON.parse(Module.UTF8ToString(pointer, end - pointer));
    },
    drain,
    subscribeProgress(callback) {
      return subscribeProgress(callback);
    },
    dispose() {
      if (disposed) return false;
      disposed = true;
      const failures = [];
      if (progressUnsubscribe) {
        try { progressUnsubscribe(); } catch (error) { failures.push(error); }
        progressUnsubscribe = null;
      }
      const previous = pointer;
      pointer = 0;
      capacity = 0;
      if (previous) {
        try { Module._free(previous); } catch (error) { failures.push(error); }
      }
      if (failures.length) throw new AggregateError(failures, 'Native network adapter disposal failed');
      return true;
    },
  });
}
