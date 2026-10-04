#!/usr/bin/env node
/* Real HTTP/headless Chrome driver for the read-only populated-Future owner
 * diagnostic. It never installs a WebGPU store or mutates a producer. Browser
 * and server close are bounded here; an outer ProcessSupervisor remains the
 * owner of kill/reap if a browser child ignores its close request. */
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import {pathToFileURL} from 'node:url';

function parseArgs(argv) {
  const values = {};
  for (let index = 2; index < argv.length; index += 1) {
    const key = argv[index];
    if (!key.startsWith('--') || index + 1 >= argv.length) throw new Error(`invalid argument ${key}`);
    values[key.slice(2)] = argv[++index];
  }
  for (const key of ['site', 'out', 'browser-tools', 'playwright-dir', 'timeout-ms']) {
    if (!values[key]) throw new Error(`missing --${key}`);
  }
  values.timeoutMs = Number(values['timeout-ms']);
  if (!Number.isInteger(values.timeoutMs) || values.timeoutMs <= 0) throw new Error('invalid timeout-ms');
  return values;
}

function remaining(deadline, label) {
  const value = deadline - Date.now();
  if (value <= 0) throw new Error(`${label} deadline expired`);
  return value;
}

async function boundedCleanup(label, deadline, operation, report) {
  const budget = deadline - Date.now();
  if (budget <= 0) {
    report.cleanup_timeouts.push(label);
    report.failure ??= `cleanup deadline expired before ${label}`;
    return false;
  }
  let timer;
  try {
    await Promise.race([
      operation(),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`${label} exceeded cleanup budget`)), budget);
      }),
    ]);
    return true;
  } catch (error) {
    report.cleanup_errors.push({label, message: String(error?.stack ?? error)});
    report.failure ??= `${label} failed: ${String(error?.message ?? error)}`;
    if (String(error?.message ?? error).includes('cleanup budget')) report.cleanup_timeouts.push(label);
    return false;
  } finally {
    clearTimeout(timer);
  }
}

function contentType(file) {
  if (file.endsWith('.html')) return 'text/html; charset=utf-8';
  if (file.endsWith('.mjs') || file.endsWith('.js')) return 'text/javascript; charset=utf-8';
  if (file.endsWith('.wasm')) return 'application/wasm';
  return 'application/octet-stream';
}

async function startServer(root) {
  const absoluteRoot = path.resolve(root);
  const server = http.createServer(async (request, response) => {
    try {
      const requestURL = new URL(request.url ?? '/', 'http://127.0.0.1');
      const relative = requestURL.pathname === '/' ? 'aurora_future_owner_probe.html' : requestURL.pathname.replace(/^\/+/, '');
      const file = path.resolve(absoluteRoot, decodeURIComponent(relative));
      if (file !== absoluteRoot && !file.startsWith(`${absoluteRoot}${path.sep}`)) {
        response.writeHead(403); response.end('forbidden'); return;
      }
      const bytes = await fs.readFile(file);
      response.writeHead(200, {'content-type': contentType(file), 'cache-control': 'no-store'});
      response.end(bytes);
    } catch (error) {
      response.writeHead(error?.code === 'ENOENT' ? 404 : 500);
      response.end(String(error));
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('HTTP server did not expose a TCP port');
  return {server, port: address.port};
}

async function closeServer(server) {
  if (!server || !server.listening) return;
  server.closeIdleConnections?.();
  await new Promise((resolve, reject) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (!settled) { settled = true; reject(new Error('HTTP server close timed out')); }
    }, 3000);
    server.close((error) => {
      if (settled) return;
      settled = true; clearTimeout(timer);
      if (error && error.code !== 'ERR_SERVER_NOT_RUNNING') reject(error);
      else resolve();
    });
  });
}

function strictProbeResult(result) {
  if (!result || result.evidence !== 'ABI-only-real-webgpu-populated-future-observation')
    throw new Error('populated WebGPU evidence label mismatch');
  if (result.result !== 'passed') throw new Error(`source probe failed: ${result.failure ?? 'unknown failure'}`);
  if (result.host_setup !== 'none' || result.webgpu_path !== 'native-emdawn-c-api' ||
      result.settlement !== 'observed-before-and-after-queue-future' ||
      result.graph_restore !== 'unsupported')
    throw new Error('populated WebGPU probe scope changed');
  if (result.emdawn_link_anchor !== 1) throw new Error('Emscripten Dawn link anchor contract failed');
  if (result.accessor_capability !== 1) throw new Error('single-threaded accessor capability contract failed');
  if (result.real_cleanup !== true || result.disposed !== true)
    throw new Error('real WebGPU/accessor cleanup was not reported');
  for (const name of ['before_adapter', 'before_work', 'after_work']) {
    const snapshot = result[name];
    if (!snapshot || snapshot.summary_result !== 1 || snapshot.summary_bytes !== 96 ||
        snapshot.row_bytes !== 32 || !Number.isInteger(snapshot.row_count) || snapshot.row_count < 1 ||
        !Array.isArray(snapshot.rows) || snapshot.rows.length !== snapshot.row_count ||
        !snapshot.decoded || !Array.isArray(snapshot.decoded.futures) ||
        !Array.isArray(snapshot.decoded.objects) || snapshot.decoded.futures.length < 1)
      throw new Error(`populated future snapshot contract failed: ${name}`);
  }
  if ((result.before_adapter_state & 3) !== 3)
    throw new Error('real instance/adapter request state missing');
  if ((result.before_work_state & 192) !== 192)
    throw new Error('real queue submission/future request state missing');
  if ((result.before_work_state & (1 << 8 | 1 << 11)) !== 0)
    throw new Error('work completion was already reported at synchronous submission capture');
  if ((result.after_work_state & 2304) !== 2304)
    throw new Error('real queue completion state missing');
  if (!result.future_ids || Object.values(result.future_ids).some(value => BigInt(value) <= 0n))
    throw new Error('real WebGPU Future IDs missing');
  if (!result.before_adapter_ids || !result.before_work_ids || !result.after_work_ids ||
      !result.before_work_identity)
    throw new Error('real native Future identity checkpoints missing');
  const rowsByIndex = snapshot => new Map(snapshot.decoded.futures.map(row => [row.index, row.identity]));
  for (const [label, snapshot, ids] of [
    ['before_adapter', result.before_adapter, result.before_adapter_ids],
    ['before_work', result.before_work, result.before_work_ids],
    ['after_work', result.after_work, result.after_work_ids],
  ]) {
    const rows = rowsByIndex(snapshot);
    for (const [name, id] of Object.entries(ids)) {
      if (BigInt(id) > 0n && !rows.has(id)) throw new Error(`${label} missing native ${name} Future row`);
    }
  }
  const beforeWorkIdentity = rowsByIndex(result.before_work).get(result.before_work_ids.work);
  const afterWorkIdentity = rowsByIndex(result.after_work).get(result.after_work_ids.work);
  if (!beforeWorkIdentity || beforeWorkIdentity !== afterWorkIdentity ||
      beforeWorkIdentity !== result.before_work_identity)
    throw new Error('work Future Promise identity changed across completion');
  if (result.cleanup_lifecycle !== 3 || result.cleanup_pending_callbacks !== 0 ||
      result.cleanup_late_callback !== 0 || (result.cleanup_state & (1 << 14)) === 0)
    throw new Error('closed native lifecycle/late callback guard failed');
  if (!result.cleanup_observer || result.cleanup_observer.device_lost_reason !== 2 ||
      result.cleanup_observer.device_lost_callbacks !== 1 ||
      result.cleanup_observer.uncaptured_error_callbacks !== 0 ||
      result.cleanup_observer.unexpected_callbacks !== 0 ||
      (result.cleanup_state & (1 << 18)) === 0)
    throw new Error('exact owned Destroyed device-lost close callback missing');
}

async function recordCdpProcessInfo(cdp, report, key) {
  const info = await cdp.send('SystemInfo.getProcessInfo');
  report[key] = info;
  for (const row of info?.processInfo ?? []) {
    if (Number.isInteger(row.id)) report.owned_pids.push(row.id);
    if (Number.isInteger(row.processId)) report.owned_pids.push(row.processId);
  }
  return info;
}

async function main() {
  const args = parseArgs(process.argv);
  const started = Date.now();
  const cleanupReserveMs = 10000;
  if (args.timeoutMs <= cleanupReserveMs) throw Error('--timeout-ms must exceed the 10-second cleanup reserve');
  const deadline = started + args.timeoutMs;
  const workDeadline = deadline - cleanupReserveMs;
  const site = path.resolve(args.site);
  const out = path.resolve(args.out);
  await fs.mkdir(out, {recursive: false});
  const report = {
    schema: 'melee-web-aurora-future-owner-browser-v1',
    diagnostic: 'read-only Emscripten JS-library Future/object ownership observation',
    result: 'failed',
    started_at_utc: new Date().toISOString(),
    timeout_ms: args.timeoutMs,
    work_timeout_ms: args.timeoutMs - cleanupReserveMs,
    cleanup_reserve_ms: cleanupReserveMs,
    node_pid: process.pid,
    site,
    out,
    server_closed: false,
    screenshot: null,
    console: [],
    page_errors: [],
    owned_pids: [process.pid],
    process_supervisor_required_on_cleanup_timeout: true,
    cleanup_timeouts: [],
    cleanup_errors: [],
    failure: null,
  };
  let server = null;
  let browser = null;
  let context = null;
  let page = null;
  let cdp = null;
  try {
    const startedServer = await startServer(site);
    server = startedServer.server;
    report.port = startedServer.port;
    report.url = `http://127.0.0.1:${startedServer.port}/aurora_future_owner_probe.html`;

    const browserToolsURL = pathToFileURL(path.resolve(args['browser-tools'])).href;
    const {loadBrowserTools, browserLaunchOptions} = await import(browserToolsURL);
    const config = await loadBrowserTools(path.resolve(args['playwright-dir']));
    report.browser_path = config.browserPath;
    report.playwright_path = config.playwrightPath;
    browser = await config.chromium.launch(browserLaunchOptions(config.browser, {
      headed: false, audible: false, timeout: remaining(workDeadline, 'browser launch'),
    }));
    cdp = await browser.newBrowserCDPSession();
    try {
      await recordCdpProcessInfo(cdp, report, 'cdp_process_info_initial');
    } catch (error) {
      report.cdp_diagnostics_error = String(error);
      throw error;
    }
    context = await browser.newContext({viewport: {width: 800, height: 300}, deviceScaleFactor: 1});
    page = await context.newPage();
    page.on('console', message => report.console.push({type: message.type(), text: message.text()}));
    page.on('pageerror', error => report.page_errors.push(String(error?.stack ?? error)));
    await page.goto(report.url, {waitUntil: 'load', timeout: remaining(workDeadline, 'page load')});
    await page.waitForFunction(() => window.__abiProbeDone === true,
      undefined, {timeout: remaining(workDeadline, 'ABI probe')});
    report.browser_scope = await page.evaluate(() => ({
      navigator_gpu: typeof navigator.gpu,
      driver_adapter_requested: false,
      driver_device_requested: false,
      probe_native_requests: true,
      probe_scope: 'native Emdawn C API requests adapter/device/queue inside the frozen Wasm fixture',
    }));
    report.probe_result = await page.evaluate(() => window.__abiProbeResult ?? null);
    report.probe_error = await page.evaluate(() => window.__abiProbeError ?? null);
    strictProbeResult(report.probe_result);
    if (report.page_errors.length) throw new Error('pageerror observed');
    if (report.console.some(row => row.type === 'error')) throw new Error('console error observed');
  } catch (error) {
    report.failure = String(error?.stack ?? error);
  } finally {
    if (cdp) {
      await boundedCleanup('final CDP process snapshot', deadline,
        () => recordCdpProcessInfo(cdp, report, 'cdp_process_info_final'), report);
    }
    if (page) {
      const screenshot = path.join(out, 'abi-probe-800x300.png');
      await boundedCleanup('diagnostic screenshot', deadline, async () => {
        await page.screenshot({path: screenshot, fullPage: false});
        report.screenshot = {path: screenshot, bytes: (await fs.stat(screenshot)).size};
      }, report);
    }
    if (cdp) await boundedCleanup('CDP detach', deadline, () => cdp.detach(), report);
    if (context) await boundedCleanup('browser context close', deadline, () => context.close(), report);
    if (browser) await boundedCleanup('browser close', deadline, () => browser.close(), report);
    if (server) {
      if (await boundedCleanup('HTTP server close', deadline, () => closeServer(server), report))
        report.server_closed = true;
    }
  }
  report.owned_pids = [...new Set(report.owned_pids)];
  report.finished_at_utc = new Date().toISOString();
  report.elapsed_ms = Date.now() - started;
  if (!report.failure && report.server_closed && report.screenshot && report.cleanup_errors.length === 0)
    report.result = 'future_owner_diagnostic_pass';
  await fs.writeFile(path.join(out, 'browser-report.json'), `${JSON.stringify(report, null, 2)}\n`);
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
  return report.result === 'future_owner_diagnostic_pass' ? 0 : 1;
}

process.on('unhandledRejection', error => { console.error(error); process.exitCode = 1; });
main().then(code => { process.exitCode = code; }).catch(error => {
  console.error(error?.stack ?? error); process.exitCode = 1;
});
