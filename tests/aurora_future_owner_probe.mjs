/* Real Emdawn/WebGPU populated-future probe. It calls only the fixture's
 * native C WebGPU path: create instance -> request adapter/device -> submit an
 * empty command buffer -> register Queue.OnSubmittedWorkDone. The JS accessor
 * remains read-only and is captured synchronously before and after the actual
 * completion callback. No browser WebGPU internal store is installed or replaced. */

function factoryFromModule(imported) {
  const candidate = imported.default ?? imported.FutureOwnerAbiFixture ?? imported;
  if (typeof candidate !== 'function') throw new Error('generated ES6 factory export missing');
  return candidate;
}

function readBytes(module, pointer, count) {
  return new Uint8Array(module.HEAPU8.buffer, pointer, count).slice();
}

function requireEqual(actual, expected, label) {
  if (actual !== expected) throw new Error(`${label}: expected ${expected}, got ${actual}`);
}

function readU32(bytes, offset) {
  return new DataView(Uint8Array.from(bytes).buffer).getUint32(offset, true);
}

function readU64(bytes, offset) {
  return new DataView(Uint8Array.from(bytes).buffer).getBigUint64(offset, true);
}

function decodeRows(snapshot, label) {
  const futures = [];
  const objects = [];
  for (const row of snapshot.rows) {
    if (!Array.isArray(row.bytes) || row.bytes.length !== 32)
      throw new Error(`${label}: malformed 32-byte row`);
    const kind = readU32(row.bytes, 0);
    const valueKind = readU32(row.bytes, 8);
    const index = readU64(row.bytes, 16);
    const identity = readU64(row.bytes, 24);
    if (kind !== 1 && kind !== 2) throw new Error(`${label}: unknown row kind ${kind}`);
    if (valueKind !== 2 || identity === 0n)
      throw new Error(`${label}: row ${row.ordinal} is not an object-like identity`);
    if (kind === 1) {
      if (index === 0n) throw new Error(`${label}: Future row has zero native index`);
      futures.push({ordinal: row.ordinal, index: index.toString(), identity: identity.toString()});
    } else {
      objects.push({ordinal: row.ordinal, identity: identity.toString()});
    }
  }
  requireEqual(futures.length, snapshot.summary_future_row_count ?? futures.length,
    `${label}: decoded future row count`);
  return {futures, objects};
}

function assertFutureRows(snapshot, ids, label, requireWork = false) {
  const rows = new Map(snapshot.decoded.futures.map(row => [row.index, row]));
  for (const [name, value] of Object.entries(ids)) {
    if (BigInt(value) <= 0n) continue;
    const row = rows.get(value);
    if (!row) throw new Error(`${label}: native ${name} Future ${value} missing from synchronous rows`);
  }
  if (requireWork) {
    const work = rows.get(ids.work);
    if (!work) throw new Error(`${label}: submitted work Future was not synchronously retained`);
    return work.identity;
  }
  return null;
}

const STATE = Object.freeze({
  STARTED: 1 << 0,
  ADAPTER_REQUESTED: 1 << 1,
  ADAPTER_READY: 1 << 2,
  DEVICE_REQUESTED: 1 << 3,
  DEVICE_READY: 1 << 4,
  QUEUE_READY: 1 << 5,
  SUBMITTED: 1 << 6,
  WORK_REQUESTED: 1 << 7,
  WORK_COMPLETED: 1 << 8,
  ERROR: 1 << 9,
  CALLBACK_BEFORE_FUTURE: 1 << 10,
  WORK_SUCCEEDED: 1 << 11,
});

function delay() {
  return new Promise(resolve => setTimeout(resolve, 0));
}

function snapshot(module, label) {
  const summaryBytes = Number(module._future_owner_summary_bytes());
  const rowBytes = Number(module._future_owner_row_bytes());
  requireEqual(summaryBytes, 96, 'summary ABI size');
  requireEqual(rowBytes, 32, 'row ABI size');
  const summary = module._malloc(summaryBytes);
  if (!summary) throw new Error(`${label}: summary allocation failed`);
  let row = 0;
  try {
    module.HEAPU8.fill(0, summary, summary + summaryBytes);
    const result = module._future_owner_summary(summary, summaryBytes);
    requireEqual(result, 1, `${label}: summary call`);
    const summaryView = new DataView(module.HEAPU8.buffer, summary, summaryBytes);
    requireEqual(summaryView.getUint32(0, true), 2, `${label}: wire schema`);
    const summaryBytesHex = Array.from(readBytes(module, summary, summaryBytes));
    const rowCount = Number(module._future_owner_row_count());
    if (!Number.isSafeInteger(rowCount) || rowCount < 0 || rowCount > 65536)
      throw new Error(`${label}: row count outside bound ${rowCount}`);
    const rows = [];
    if (rowCount > 0) {
      row = module._malloc(rowBytes);
      if (!row) throw new Error(`${label}: row allocation failed`);
      for (let ordinal = 0; ordinal < rowCount; ordinal += 1) {
        module.HEAPU8.fill(0, row, row + rowBytes);
        requireEqual(module._future_owner_row(row, rowBytes, ordinal), 1,
          `${label}: row ${ordinal}`);
        rows.push({ordinal, bytes: Array.from(readBytes(module, row, rowBytes))});
      }
    }
    const snapshot = {label, summary_result: result, summary_bytes: summaryBytes,
      row_bytes: rowBytes, row_count: rowCount, summary_bytes_hex: summaryBytesHex,
      rows};
    const summaryViewCopy = new DataView(Uint8Array.from(summaryBytesHex).buffer);
    snapshot.summary_future_row_count = Number(summaryViewCopy.getBigUint64(80, true));
    snapshot.summary_js_object_row_count = Number(summaryViewCopy.getBigUint64(88, true));
    snapshot.decoded = decodeRows(snapshot, label);
    if (snapshot.decoded.futures.length !== snapshot.summary_future_row_count ||
        snapshot.decoded.objects.length !== snapshot.summary_js_object_row_count)
      throw new Error(`${label}: row kind counts disagree with summary`);
    return snapshot;
  } finally {
    if (row) module._free(row);
    module._free(summary);
  }
}

async function waitFor(module, predicate, label, attempts = 200) {
  for (let attempt = 0; attempt < attempts; attempt += 1) {
    module._future_owner_real_poll();
    const state = Number(module._future_owner_real_state());
    if (predicate(state)) return {attempts: attempt + 1, state};
    await delay();
  }
  throw new Error(`${label}: timed out waiting for native callback, state=${module._future_owner_real_state()}`);
}

function readFutureIds(module) {
  const bytes = Number(module._future_owner_real_ids_bytes());
  requireEqual(bytes, 24, 'future ID ABI size');
  const pointer = module._malloc(bytes);
  if (!pointer) throw new Error('future ID allocation failed');
  try {
    module.HEAPU8.fill(0, pointer, pointer + bytes);
    requireEqual(module._future_owner_real_ids(pointer, bytes), 1, 'future ID read');
    const view = new DataView(module.HEAPU8.buffer, pointer, bytes);
    return {
      adapter: view.getBigUint64(0, true).toString(),
      device: view.getBigUint64(8, true).toString(),
      work: view.getBigUint64(16, true).toString(),
    };
  } finally {
    module._free(pointer);
  }
}

function readObserverDiagnostics(module) {
  const bytes = Number(module._future_owner_real_observer_bytes());
  requireEqual(bytes, 16, 'observer diagnostic ABI size');
  const pointer = module._malloc(bytes);
  if (!pointer) throw new Error('observer diagnostic allocation failed');
  try {
    module.HEAPU8.fill(0, pointer, pointer + bytes);
    requireEqual(module._future_owner_real_observer(pointer, bytes), 1,
      'observer diagnostic read');
    const view = new DataView(module.HEAPU8.buffer, pointer, bytes);
    return {
      device_lost_reason: view.getUint32(0, true),
      device_lost_callbacks: view.getUint32(4, true),
      uncaptured_error_callbacks: view.getUint32(8, true),
      unexpected_callbacks: view.getUint32(12, true),
    };
  } finally {
    module._free(pointer);
  }
}

export async function collect(moduleURL) {
  const imported = await import(moduleURL);
  const factory = factoryFromModule(imported);
  const moduleURLObject = new URL(moduleURL, window.location.href);
  const module = await factory({locateFile: name => new URL(name, moduleURLObject).href});
  let pendingError = null;
  let disposed = false;
  let realCleaned = false;
  const result = {
    evidence: 'ABI-only-real-webgpu-populated-future-observation',
    host_setup: 'none',
    webgpu_path: 'native-emdawn-c-api',
    queue_submission: 'empty-command-buffer',
    settlement: 'observed-before-and-after-queue-future',
    graph_restore: 'unsupported',
  };
  try {
    result.emdawn_link_anchor = module._future_owner_emdawn_link_anchor();
    requireEqual(result.emdawn_link_anchor, 1, 'Emscripten Dawn link anchor');
    result.accessor_capability = module._future_owner_capability();
    requireEqual(result.accessor_capability, 1, 'single-threaded accessor capability');
    requireEqual(module._future_owner_real_start(), 1, 'native WebGPU start');
    const beforeAdapterState = Number(module._future_owner_real_state());
    result.before_adapter_state = beforeAdapterState;
    result.before_adapter = snapshot(module, 'before_adapter_settlement');
    result.before_adapter_ids = readFutureIds(module);
    assertFutureRows(result.before_adapter, result.before_adapter_ids,
      'before_adapter_settlement');
    if ((beforeAdapterState & STATE.STARTED) === 0 ||
        (beforeAdapterState & STATE.ADAPTER_REQUESTED) === 0)
      throw new Error(`native adapter request state missing: ${beforeAdapterState}`);

    const deviceReady = await waitFor(module,
      state => (state & STATE.DEVICE_READY) !== 0 || (state & STATE.ERROR) !== 0,
      'device request');
    result.device_ready_wait = deviceReady;
    if ((deviceReady.state & STATE.DEVICE_READY) === 0)
      throw new Error(`native adapter/device request failed: state=${deviceReady.state}`);

    result.before_submit_ids = readFutureIds(module);
    assertFutureRows(result.before_adapter, {adapter: result.before_submit_ids.adapter},
      'before_adapter_settlement adapter identity');

    requireEqual(module._future_owner_real_submit(), 1, 'native queue submission');
    result.before_work_state = Number(module._future_owner_real_state());
    result.before_work = snapshot(module, 'before_work_future_settlement');
    result.before_work_ids = readFutureIds(module);
    result.before_work_identity = assertFutureRows(result.before_work, result.before_work_ids,
      'before_work_future_settlement', true);
    if ((result.before_work_state & STATE.WORK_REQUESTED) === 0 ||
        (result.before_work_state & STATE.SUBMITTED) === 0 ||
        (result.before_work_state & (STATE.WORK_COMPLETED | STATE.WORK_SUCCEEDED)) !== 0)
      throw new Error(`queue submission state missing: ${result.before_work_state}`);

    const workDone = await waitFor(module,
      state => (state & STATE.WORK_COMPLETED) !== 0 || (state & STATE.ERROR) !== 0,
      'queue completion');
    result.work_done_wait = workDone;
    if ((workDone.state & STATE.WORK_SUCCEEDED) === 0)
      throw new Error(`queue completion failed: state=${workDone.state}`);
    result.after_work_state = Number(module._future_owner_real_state());
    result.after_work = snapshot(module, 'after_work_future_settlement');
    result.after_work_ids = readFutureIds(module);
    result.future_ids = result.after_work_ids;
    for (const [name, value] of Object.entries(result.future_ids)) {
      if (BigInt(value) <= 0n) throw new Error(`native ${name} Future id was not published`);
    }
    const afterWorkIdentity = assertFutureRows(result.after_work, result.after_work_ids,
      'after_work_future_settlement', true);
    requireEqual(afterWorkIdentity, result.before_work_identity,
      'work Future Promise identity across native completion');
    result.disposed = false;
  } catch (error) {
    pendingError = error;
  } finally {
    try {
      realCleaned = module._future_owner_real_cleanup() === 1;
    } catch (error) {
      pendingError ??= error;
    }
    try {
      disposed = module._future_owner_dispose() === 1;
    } catch (error) {
      pendingError ??= error;
    }
    await delay();
    result.cleanup_state = Number(module._future_owner_real_state());
    result.cleanup_pending_callbacks = Number(module._future_owner_real_pending_callbacks());
    result.cleanup_lifecycle = Number(module._future_owner_real_lifecycle());
    result.cleanup_late_callback = Number(module._future_owner_real_late_callback());
    try {
      result.cleanup_observer = readObserverDiagnostics(module);
    } catch (error) {
      result.cleanup_observer_error = String(error?.stack ?? error);
      pendingError ??= error;
    }
    if (result.cleanup_late_callback !== 0)
      pendingError ??= new Error('late WebGPU callback observed after closed lifecycle');
  }
  result.real_cleanup = realCleaned;
  result.disposed = disposed;
  if (pendingError !== null) {
    result.result = 'failed';
    result.failure = String(pendingError?.stack ?? pendingError);
    return result;
  }
  if (!realCleaned) {
    result.result = 'failed';
    result.failure = 'native WebGPU cleanup failed';
    return result;
  }
  if (!disposed) {
    result.result = 'failed';
    result.failure = 'future-owner accessor dispose failed';
    return result;
  }
  if (result.cleanup_lifecycle !== 3 || result.cleanup_pending_callbacks !== 0 ||
      result.cleanup_late_callback !== 0 || (result.cleanup_state & (1 << 14)) === 0) {
    result.result = 'failed';
    result.failure = 'native WebGPU cleanup lifecycle was not closed and settled';
    return result;
  }
  if (!result.cleanup_observer || result.cleanup_observer.device_lost_reason !== 2 ||
      result.cleanup_observer.device_lost_callbacks !== 1 ||
      result.cleanup_observer.uncaptured_error_callbacks !== 0 ||
      result.cleanup_observer.unexpected_callbacks !== 0 ||
      (result.cleanup_state & (1 << 18)) === 0) {
    result.result = 'failed';
    result.failure = 'native device-lost Destroyed callback was not the exact owned close event';
    return result;
  }
  result.result = 'passed';
  return result;
}
