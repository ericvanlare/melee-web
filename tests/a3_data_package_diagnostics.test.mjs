import test from 'node:test';
import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import {createHash, webcrypto} from 'node:crypto';
import {observeDataPackageLifecycle, readInitialPipelineSeedInPage} from '../scripts/a3_data_package_diagnostics.mjs';
const url = 'http://127.0.0.1:8787/runtime.html';
const dataUrl = new URL('gameplay_menu_browser.data', url).href;
const request = (requestUrl = dataUrl, failure = null) => ({url: () => requestUrl,
  method: () => 'GET', resourceType: () => 'fetch', failure: () => failure ? {errorText: failure} : null,
  timing: () => ({startTime: 123, responseEnd: failure ? -1 : 10})});
const response = (req, bytes, declared = bytes.length) => ({request: () => req,
  status: () => 200, allHeaders: async () => ({'content-length': String(declared)}),
  body: async () => bytes});

test('separate actual request objects retain successful package and later aborted request independently', async () => {
  const page = new EventEmitter();
  const observer = observeDataPackageLifecycle(page, url);
  const body = Buffer.from('pipeline seed fixture'), first = request(), aborted = request(dataUrl, 'net::ERR_ABORTED');
  page.emit('request', first); page.emit('response', response(first, body));
  page.emit('requestfinished', first);
  page.emit('request', aborted); page.emit('requestfailed', aborted);
  const snapshot = await observer.finish();
  assert.equal(snapshot.requests.length, 2);
  const [good, bad] = snapshot.requests;
  assert.notEqual(good.request_id, bad.request_id);
  assert.equal(good.failure, null);
  assert.equal(good.response.body.sha256, createHash('sha256').update(body).digest('hex'));
  assert(good.finished_at_monotonic_ms >= good.requested_at_monotonic_ms);
  assert.equal(bad.failure, 'net::ERR_ABORTED');
  assert.equal(bad.response, null);
  assert(bad.failed_at_monotonic_ms >= bad.requested_at_monotonic_ms);
  assert.equal(snapshot.pending_response_bodies, 0);
  assert.equal(page.listenerCount('requestfailed'), 0);
  // Observation is not an acceptance rule or fabricated successful request.
  assert.equal(snapshot.decision, undefined);
});

test('wrong path ignored, duplicate event does not invent request and capacity refusal remains explicit', async () => {
  const page = new EventEmitter(), observer = observeDataPackageLifecycle(page, url);
  page.emit('request', request('http://127.0.0.1:8787/unrelated.data'));
  const first = request(); page.emit('request', first); page.emit('request', first);
  for (let i = 0; i < 20; i++) page.emit('request', request());
  const snapshot = await observer.finish();
  assert.equal(snapshot.requests.length, 16);
  assert.equal(snapshot.overflow, true);
  assert.equal(new Set(snapshot.requests.map(row => row.request_id)).size, 16);
});

test('body mismatch and aborted body read remain failures rather than successful preload proof', async () => {
  const page = new EventEmitter(), observer = observeDataPackageLifecycle(page, url);
  const first = request(), second = request(dataUrl, 'net::ERR_ABORTED');
  page.emit('request', first); page.emit('response', response(first, Buffer.from('one'), 4));
  page.emit('request', second);
  page.emit('response', {...response(second, Buffer.from('two')), body: async () => { throw Error('body unavailable'); }});
  page.emit('requestfailed', second);
  const snapshot = await observer.finish();
  assert(snapshot.requests.every(row => row.response.body === null && row.response.body_error));
  assert.equal(snapshot.requests[1].failure, 'net::ERR_ABORTED');
});

test('read-only seed snapshot hashes actual filesystem bytes and retains preload/download status', async () => {
  const priorModule = globalThis.Module, priorDocument = globalThis.document;
  const priorCrypto = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
  const bytes = new Uint8Array([4, 7, 9]);
  let reads = 0;
  try {
    Object.defineProperty(globalThis, 'crypto', {configurable: true, value: webcrypto});
    globalThis.document = {querySelector: () => ({dataset: {runtimeError: ''}})};
    globalThis.Module = {FS: {readFile: path => { assert.equal(path, '/initial_pipeline_cache.db'); reads++; return bytes; }},
      calledRun: true, preloadResults: {'gameplay_menu_browser.data': {fromCache: false}},
      dataFileDownloads: {[dataUrl]: {loaded: 3, total: 3}}, expectedDataFileDownloads: 1,
      _melee_web_native_menu_phase: () => 0};
    const snapshot = await readInitialPipelineSeedInPage();
    assert.equal(reads, 1);
    assert.equal(snapshot.sha256, createHash('sha256').update(bytes).digest('hex'));
    assert.deepEqual(snapshot.data_file_downloads[dataUrl], {loaded: 3, total: 3});
    assert.equal(snapshot.called_run, true);
    assert.equal(snapshot.native_phase, 0);
  } finally {
    if (priorModule === undefined) delete globalThis.Module; else globalThis.Module = priorModule;
    if (priorDocument === undefined) delete globalThis.document; else globalThis.document = priorDocument;
    if (priorCrypto) Object.defineProperty(globalThis, 'crypto', priorCrypto); else delete globalThis.crypto;
  }
});
