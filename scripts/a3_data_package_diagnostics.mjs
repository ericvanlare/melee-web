import {createHash} from 'node:crypto';
import {performance} from 'node:perf_hooks';

/** Passive Task13 startup/retirement diagnostics. Never intercepts or cancels requests. */
export function observeDataPackageLifecycle(page, runtimeUrl) {
  const expectedUrl = new URL('gameplay_menu_browser.data', runtimeUrl).href;
  const ids = new WeakMap(), requests = [], pending = new Set();
  const errors = [];
  let overflow = false, finished = false;
  const stamp = () => performance.now();
  const errorText = error => String(error?.stack || error?.message || error);
  const find = request => requests.find(row => row.request_id === ids.get(request));
  const requestStarted = request => {
    if (request.url() !== expectedUrl || ids.has(request)) return;
    if (requests.length >= 16) { overflow = true; return; }
    const request_id = `page-request-${requests.length + 1}`;
    ids.set(request, request_id);
    requests.push({request_id, identity_scope: 'actual Playwright Request object within this profile',
      url: request.url(), method: request.method(), resource_type: request.resourceType(),
      requested_at_monotonic_ms: stamp(), response: null, finished_at_monotonic_ms: null,
      failure: null, failed_at_monotonic_ms: null});
  };
  const responded = response => {
    const row = find(response.request());
    if (!row) return;
    const task = (async () => {
      try {
        const headers = await response.allHeaders();
        row.response = {status: response.status(), observed_at_monotonic_ms: stamp(),
          content_length: headers['content-length'] ?? null, body: null, body_error: null};
        const length = Number(headers['content-length']);
        if (!Number.isSafeInteger(length) || length < 0 || length > 8 * 1024 * 1024) {
          row.response.body_error = 'Missing, invalid or oversized declared package body'; return;
        }
        const body = await response.body();
        if (body.length !== length) {
          row.response.body_error = 'Package body length differs from Content-Length'; return;
        }
        row.response.body = {bytes: body.length,
          sha256: createHash('sha256').update(body).digest('hex')};
      } catch (error) {
        if (row.response) row.response.body_error = errorText(error);
        else errors.push(errorText(error));
      }
    })();
    pending.add(task);
    task.finally(() => pending.delete(task));
  };
  const requestFinished = request => {
    const row = find(request);
    if (row) { row.finished_at_monotonic_ms = stamp(); row.browser_timing = request.timing(); }
  };
  const requestFailed = request => {
    const row = find(request);
    if (row) {
      row.failure = request.failure()?.errorText ?? null;
      row.failed_at_monotonic_ms = stamp(); row.browser_timing = request.timing();
    }
  };
  page.on('request', requestStarted); page.on('response', responded);
  page.on('requestfinished', requestFinished); page.on('requestfailed', requestFailed);
  const snapshot = () => structuredClone({schema: 'melee-web-task13-data-package-lifecycle-v1',
    expected_url: expectedUrl, requests, errors, overflow, finished,
    pending_response_bodies: pending.size,
    scope: 'passive request identity/timing and response body hash; no abort/fetch behavior change'});
  return {snapshot, async finish() {
    // Called after context retirement, within the caller's unchanged cleanup budget.
    let timer;
    try {
      await Promise.race([Promise.allSettled([...pending]),
        new Promise(resolve => { timer = setTimeout(resolve, 1000); })]);
    } finally {
      clearTimeout(timer);
      page.off('request', requestStarted); page.off('response', responded);
      page.off('requestfinished', requestFinished); page.off('requestfailed', requestFailed);
      finished = true;
    }
    return snapshot();
  }};
}

/** Executed read-only in the already-ready owned page. No loader or file writes. */
export async function readInitialPipelineSeedInPage() {
  const module = globalThis.Module;
  const path = '/initial_pipeline_cache.db';
  const bytes = module?.FS?.readFile(path);
  if (!(bytes instanceof Uint8Array) || bytes.length > 8 * 1024 * 1024)
    throw Error('Task13 initial pipeline seed read is unavailable or exceeds its diagnostic bound');
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return {schema: 'melee-web-task13-preloaded-seed-observation-v1', path,
    bytes: bytes.length, sha256: Array.from(digest, byte => byte.toString(16).padStart(2, '0')).join(''),
    observed_at_page_monotonic_ms: performance.now(), called_run: module.calledRun === true,
    preload_results: structuredClone(module.preloadResults ?? null),
    data_file_downloads: structuredClone(module.dataFileDownloads ?? null),
    expected_data_file_downloads: module.expectedDataFileDownloads ?? null,
    native_phase: module._melee_web_native_menu_phase?.() ?? null,
    runtime_error: document.querySelector('#status')?.dataset.runtimeError || null};
}
