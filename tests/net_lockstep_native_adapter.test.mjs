import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {createNetLockstepNativeAdapter} from '../scripts/net_lockstep_native_adapter.mjs';
import {NET_FRAME_BYTES, NET_RECORD_BYTES, PAD_BYTES} from '../scripts/net_lockstep_core.mjs';

function fakeModule(options = {}) {
  let memory = new Uint8Array(options.initialHeap ?? 64), cursor = 8;
  let mallocCalls = 0;
  const allocations = new Map(), freed = [], pushed = [], indexed = [], configured = [], terminals = [];
  const records = options.records ?? [Uint8Array.from({length: NET_RECORD_BYTES}, (_, i) => i)];
  const grow = minimum => {
    if (memory.length >= minimum) return;
    const next = new Uint8Array(Math.max(minimum, memory.length * 2));
    next.set(memory);
    memory = next;
  };
  const Module = {
    get HEAPU8() { return memory; },
    _malloc(size) {
      ++mallocCalls;
      if (options.allocationFails) return 0;
      const ptr = options.outOfBoundsAllocation ? memory.length - 1 : cursor;
      cursor += size + 8;
      grow(cursor);
      allocations.set(ptr, size);
      return ptr;
    },
    _free(ptr) { freed.push(ptr); allocations.delete(ptr); if (options.freeFailure) throw options.freeFailure; },
    _melee_web_net_push(ptr, count) {
      pushed.push({count, bytes: memory.slice(ptr, ptr + count * NET_FRAME_BYTES)});
      return options.pushResult ?? 1;
    },
    _melee_web_net_push_indexed(tick, ptr, count) {
      indexed.push({tick, count, bytes: memory.slice(ptr, ptr + count * NET_FRAME_BYTES)});
      return 1;
    },
    _melee_web_net_enable_local_input_capture(port, ticks) {
      configured.push({port, ticks});
      return 1;
    },
    _melee_web_net_confirm_start() { return 1; },
    _melee_web_net_terminate(kind, tick, channel) { terminals.push({kind, tick, channel}); },
    _melee_web_net_checksum_drain(ptr, max) {
      if (options.growOnDrain) grow(memory.length * 2);
      const count = options.drainCount ?? Math.min(max, records.length);
      for (let index = 0; index < Math.min(count, records.length); ++index)
        memory.set(records[index], ptr + index * NET_RECORD_BYTES);
      return count;
    },
    _melee_web_net_status() { return options.statusPointer ?? 4; },
    UTF8ToString(pointer) { assert.equal(pointer, 4); return '{"active":1,"cursor":0}'; },
  };
  return {Module, allocations, freed, pushed, indexed, configured, terminals,
    heap: () => memory, grow, mallocCalls: () => mallocCalls};
}

test('one adapter preserves A1 sequential and A3 indexed PAD envelopes through the same scratch owner', () => {
  const native = fakeModule();
  const adapter = createNetLockstepNativeAdapter(native.Module);
  const frames = Uint8Array.from({length: NET_FRAME_BYTES * 2}, (_, index) => index & 0xff);
  assert.equal(adapter.push(frames), true);
  assert.equal(adapter.pushIndexed(7, frames), true);
  assert.equal(native.pushed.length, 1);
  assert.equal(native.pushed[0].count, 2);
  assert.deepEqual(native.pushed[0].bytes, frames);
  assert.deepEqual(native.indexed, [{tick: 7, count: 2, bytes: frames}]);
  assert.equal(native.allocations.size, 1, 'sequential and indexed publishers share one scratch allocation');
  assert.equal(adapter.recordBytes, NET_RECORD_BYTES);
});

test('adapter refreshes the heap after allocation and native drain, and returns independent record bytes', () => {
  const native = fakeModule({initialHeap: 32, growOnDrain: true,
    records: [Uint8Array.from({length: NET_RECORD_BYTES}, (_, i) => 255 - i)]});
  const adapter = createNetLockstepNativeAdapter(native.Module);
  const input = Uint8Array.from({length: NET_FRAME_BYTES}, (_, i) => (i * 3) & 0xff);
  assert.equal(adapter.push(input), true);
  assert.deepEqual(native.pushed[0].bytes, input);
  const drained = adapter.drain(2);
  assert.equal(drained.count, 1);
  assert.equal(drained.bytes.length, NET_RECORD_BYTES);
  assert.deepEqual(drained.bytes, Uint8Array.from({length: NET_RECORD_BYTES}, (_, i) => 255 - i));
  const saved = new Uint8Array(drained.bytes);
  native.heap().fill(0, 0, native.heap().length);
  assert.deepEqual(drained.bytes, saved, 'drained evidence does not alias Wasm memory');
});

test('adapter copies HEAP-backed input before real Wasm memory growth detaches the source view', () => {
  const memory = new WebAssembly.Memory({initial: 1, maximum: 2});
  let pushed;
  const Module = {
    get HEAPU8() { return new Uint8Array(memory.buffer); },
    _malloc(size) { assert.equal(size, NET_FRAME_BYTES); memory.grow(1); return 8; },
    _free() {},
    _melee_web_net_push(pointer, count) {
      pushed = {count, bytes: Module.HEAPU8.slice(pointer, pointer + count * NET_FRAME_BYTES)};
      return 1;
    },
    _melee_web_net_push_indexed() { return 1; },
    _melee_web_net_enable_local_input_capture() { return 1; },
    _melee_web_net_confirm_start() { return 1; },
    _melee_web_net_terminate() {},
    _melee_web_net_checksum_drain() { return 0; },
    _melee_web_net_status() { return 4; },
    UTF8ToString() { return '{}'; },
  };
  const adapter = createNetLockstepNativeAdapter(Module);
  const input = Module.HEAPU8.subarray(128, 128 + NET_FRAME_BYTES);
  input.forEach((_, index) => { input[index] = (index * 7) & 0xff; });
  const expected = Uint8Array.from(input);

  assert.equal(adapter.push(input), true);
  assert.equal(input.byteLength, 0, 'real memory.grow detached the original caller view');
  assert.deepEqual(pushed, {count: 1, bytes: expected});
});

test('adapter validates ABI shapes, native result/count bounds and unsigned input cursors', () => {
  const native = fakeModule();
  const adapter = createNetLockstepNativeAdapter(native.Module);
  assert.throws(() => adapter.push(new Uint8Array(PAD_BYTES)), /complete 44-byte frames/);
  assert.throws(() => adapter.pushIndexed(-1, new Uint8Array(NET_FRAME_BYTES)), /unsigned 32-bit/);
  assert.throws(() => adapter.pushIndexed(0x100000000, new Uint8Array(NET_FRAME_BYTES)), /unsigned 32-bit/);
  assert.throws(() => adapter.configureLocalInputCapture(2, 4), /port or input tick/);
  assert.throws(() => adapter.configureLocalInputCapture(0, 0), /port or input tick/);
  assert.equal(adapter.configureLocalInputCapture(1, 4), true);
  assert.deepEqual(native.configured, [{port: 1, ticks: 4}]);
  assert.equal(adapter.confirmStart(), true);
  adapter.terminate(3, 12, 1);
  assert.deepEqual(native.terminals, [{kind: 3, tick: 12, channel: 1}]);
  assert.throws(() => adapter.terminate(3, -1, 1), /unsigned 32-bit/);
  assert.deepEqual(adapter.status(), {active: 1, cursor: 0});

  const oddPush = fakeModule({pushResult: 2});
  assert.throws(() => createNetLockstepNativeAdapter(oddPush.Module).push(new Uint8Array(NET_FRAME_BYTES)),
    /invalid result/);
  const badCount = fakeModule({drainCount: 3});
  assert.throws(() => createNetLockstepNativeAdapter(badCount.Module).drain(2), /invalid record count/);
  const abiOverflow = fakeModule();
  const abiOverflowAdapter = createNetLockstepNativeAdapter(abiOverflow.Module);
  assert.throws(() => abiOverflowAdapter.drain(Math.floor(0xffffffff / NET_RECORD_BYTES) + 1),
    /32-bit Wasm ABI/);
  assert.equal(abiOverflow.mallocCalls(), 0, 'an ABI-overflowing drain is rejected before allocation');
  const missingExport = fakeModule().Module;
  delete missingExport._melee_web_net_push_indexed;
  assert.throws(() => createNetLockstepNativeAdapter(missingExport), /complete browser ABI/);
  const invalidStatusPointer = fakeModule({statusPointer: 64});
  assert.throws(() => createNetLockstepNativeAdapter(invalidStatusPointer.Module).status(),
    /status pointer is outside/);
  const unterminatedStatus = fakeModule({statusPointer: 3});
  unterminatedStatus.heap().fill(0xff);
  assert.throws(() => createNetLockstepNativeAdapter(unterminatedStatus.Module).status(), /not terminated/);
});

test('adapter reports allocation and heap extent failures before writing native bytes', () => {
  const failed = fakeModule({allocationFails: true});
  assert.throws(() => createNetLockstepNativeAdapter(failed.Module).push(new Uint8Array(NET_FRAME_BYTES)),
    /scratch allocation failed/);
  const outside = fakeModule({outOfBoundsAllocation: true});
  assert.throws(() => createNetLockstepNativeAdapter(outside.Module).push(new Uint8Array(NET_FRAME_BYTES)),
    /outside the current Wasm heap/);
  assert.equal(outside.pushed.length, 0);
});

test('adapter disposal is idempotent, frees its owned scratch once, and rejects later native use', () => {
  const native = fakeModule();
  const adapter = createNetLockstepNativeAdapter(native.Module);
  adapter.push(new Uint8Array(NET_FRAME_BYTES));
  assert.equal(adapter.dispose(), true);
  assert.equal(adapter.dispose(), false);
  assert.equal(native.freed.length, 1);
  assert.throws(() => adapter.pushIndexed(0, new Uint8Array(NET_FRAME_BYTES)), /adapter is disposed/);
  assert.throws(() => adapter.drain(1), /adapter is disposed/);
  assert.throws(() => adapter.status(), /adapter is disposed/);
  assert.throws(() => adapter.drain(0), /adapter is disposed/);
});

test('progress adapter owns one subscription and silences retained callbacks after unsubscribe', () => {
  const withoutOwner = createNetLockstepNativeAdapter(fakeModule().Module);
  assert.throws(() => withoutOwner.subscribeProgress(() => {}), /progress subscription is unavailable/);

  const native = fakeModule();
  let captured, unsubscribeCalls = 0, notifications = 0;
  const adapter = createNetLockstepNativeAdapter(native.Module, {subscribeProgress(callback) {
    captured = callback;
    return () => { ++unsubscribeCalls; };
  }});
  const unsubscribe = adapter.subscribeProgress(() => { ++notifications; });
  assert.throws(() => adapter.subscribeProgress(() => {}), /already has an owner/);
  captured(null); assert.equal(notifications, 1);
  assert.equal(unsubscribe(), true);
  captured(null); assert.equal(notifications, 1, 'a callback retained by an observer is inert after unsubscribe');
  assert.equal(unsubscribe(), false);
  assert.equal(unsubscribeCalls, 1);
});

test('adapter disposal cancels progress and aggregates subscription and scratch cleanup failures', () => {
  const unsubscribeFailure = Error('progress unsubscribe failed');
  const freeFailure = Error('scratch free failed');
  const native = fakeModule({freeFailure});
  let captured, notifications = 0, unsubscribeCalls = 0;
  const adapter = createNetLockstepNativeAdapter(native.Module, {subscribeProgress(callback) {
    captured = callback;
    return () => { ++unsubscribeCalls; throw unsubscribeFailure; };
  }});
  adapter.push(new Uint8Array(NET_FRAME_BYTES));
  adapter.subscribeProgress(() => { ++notifications; });
  assert.throws(() => adapter.dispose(), error => {
    assert.equal(error instanceof AggregateError, true);
    assert(error.errors.includes(unsubscribeFailure));
    assert(error.errors.includes(freeFailure));
    return true;
  });
  captured(null);
  assert.equal(notifications, 0, 'dispose silences late progress callbacks before attempting cleanup');
  assert.equal(unsubscribeCalls, 1);
  assert.equal(native.freed.length, 1);
  assert.equal(adapter.dispose(), false);
});

test('ordinary runtime stages shared modules while exact A3 response inventory binds them', async () => {
  const [cmake, artifacts, harness, session] = await Promise.all([
    readFile(new URL('../cmake/FighterRuntime.cmake', import.meta.url), 'utf8'),
    readFile(new URL('../tools/browser_build_artifacts.json', import.meta.url), 'utf8'),
    readFile(new URL('../scripts/net_lockstep_browser.mjs', import.meta.url), 'utf8'),
    readFile(new URL('../scripts/net_session_instance.mjs', import.meta.url), 'utf8'),
  ]);
  const names = JSON.parse(artifacts);
  assert(names.includes('net_lockstep_core.mjs'));
  assert(names.includes('net_lockstep_native_adapter.mjs'));
  assert.match(cmake, /if\(NOT MELEE_WEB_PUBLIC_RUNTIME AND NOT MELEE_WEB_AUDIO_PREVIEW_RUNTIME\)[\s\S]*configure_file\(scripts\/net_lockstep_core\.mjs net_lockstep_core\.mjs COPYONLY\)[\s\S]*configure_file\(scripts\/net_lockstep_native_adapter\.mjs net_lockstep_native_adapter\.mjs COPYONLY\)[\s\S]*endif\(\)/);
  assert.match(harness, /'net_lockstep_native_adapter\.mjs'/);
  assert.match(session, /import\('\.\/net_lockstep_native_adapter\.mjs'\)/);
  assert.match(session, /window\.__meleeWebNetNativeAdapter\?\.dispose\(\)/);
});
