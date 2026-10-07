import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {existsSync} from 'node:fs';
import {lstat, mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const TOOLS = path.resolve(process.env.MELEE_A3_ROOM_RELAY_TOOLS ||
  path.join(ROOT, 'work/a3-room-relay-tools/node_modules'));
const WRANGLER_PACKAGE = path.join(TOOLS, 'wrangler/package.json');
const MINIFLARE_PACKAGE = path.join(TOOLS, 'miniflare/package.json');
const WORKERD_PACKAGE = path.join(TOOLS, 'workerd/package.json');
const STARTUP_TIMEOUT_MS = 20000;
const DISPOSE_TIMEOUT_MS = 5000;
const FALLBACK_TERM_TIMEOUT_MS = 3000;
const FALLBACK_KILL_TIMEOUT_MS = 3000;
const CLOSE_TIMEOUT_MS = 1000;
const FINAL_GROUP_KILL_TIMEOUT_MS = 3000;
const EVIDENCE_LOG_LIMIT_BYTES = 4 * 1024 * 1024;

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

export function roomRelayRuntimeAvailable() {
  return [WRANGLER_PACKAGE, MINIFLARE_PACKAGE, WORKERD_PACKAGE].every(existsSync);
}

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const port = server.address().port;
  await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
  return port;
}

function bounded(promise, timeoutMs) {
  let timer;
  return Promise.race([promise, new Promise(resolve => {
    timer = setTimeout(() => resolve(null), timeoutMs);
  })]).finally(() => clearTimeout(timer));
}

function appendBounded(current, chunk) {
  const next = current + chunk.toString();
  return next.length <= EVIDENCE_LOG_LIMIT_BYTES ? next : next.slice(-EVIDENCE_LOG_LIMIT_BYTES);
}

function groupAlive(pid) {
  if (!pid) return false;
  try { process.kill(-pid, 0); return true; }
  catch (error) { return error.code !== 'ESRCH'; }
}

function sendSignal(pid, signal, record) {
  try { process.kill(-pid, signal); }
  catch (error) {
    if (error.code !== 'ESRCH') record(`${signal.toLowerCase()}-error`, {error: String(error)});
  }
}

/** Start the pinned direct Miniflare Worker and return its owned local endpoint.
 * The caller must call close(passed) exactly once, even after its own failure.
 * A passing return requires clean IPC disposal and a gone process group.
 */
export async function startRoomRelayRuntime({diagnostic = () => {}, evidenceDir = null,
  producer = null} = {}) {
  if (!roomRelayRuntimeAvailable())
    throw Error(`Pinned direct-runtime packages are unavailable under ${TOOLS}`);

  const lock = JSON.parse(await readFile(path.join(ROOT, 'dependencies.lock.json'), 'utf8'));
  const [wrangler, miniflare, workerd] = await Promise.all([
    readFile(WRANGLER_PACKAGE), readFile(MINIFLARE_PACKAGE), readFile(WORKERD_PACKAGE),
  ]).then(rows => rows.map(bytes => JSON.parse(bytes.toString('utf8'))));
  if (wrangler.version !== lock.deployment_tools.wrangler.version ||
      miniflare.version !== lock.deployment_tools.miniflare.version ||
      workerd.version !== lock.deployment_tools.workerd.version ||
      wrangler.dependencies.miniflare !== miniflare.version ||
      wrangler.dependencies.workerd !== workerd.version ||
      miniflare.dependencies.workerd !== workerd.version)
    throw Error('Direct Worker runtime packages do not match the pinned Wrangler/Miniflare/workerd dependency graph');
  if (typeof globalThis.WebSocket !== 'function')
    throw Error('Direct Worker runtime owner requires Node global WebSocket');
  if (Number(process.versions.node.split('.')[0]) < 22)
    throw Error('Pinned Wrangler requires Node 22 or newer');

  const workRoot = path.join(ROOT, 'work');
  await mkdir(workRoot, {recursive: true});
  const workInfo = await lstat(workRoot);
  if (!workInfo.isDirectory() || workInfo.isSymbolicLink())
    throw Error('Direct Worker scratch requires the real checkout work directory');
  const scratch = await mkdtemp(path.join(workRoot, 'a3-room-relay-direct-'));
  const persist = path.join(scratch, 'state');
  const resourceTmp = path.join(scratch, 'resource-tmp');
  const runtimeTmp = path.join(scratch, 'runtime-tmp');
  await Promise.all([mkdir(persist), mkdir(resourceTmp), mkdir(runtimeTmp)]);
  const port = await freePort();
  const inspectorPort = await freePort();
  const runtimeScript = path.join(ROOT, 'scripts/net_room_relay_direct_runtime.mjs');
  const wranglerCli = path.join(TOOLS, 'wrangler/wrangler-dist/cli.js');
  const miniflareEntry = path.join(TOOLS, 'miniflare/dist/src/index.js');
  const configPath = path.join(ROOT, 'online/relay/wrangler.jsonc');
  const hashes = {
    worker: sha256(await readFile(path.join(ROOT, 'online/relay/worker.mjs'))),
    config: sha256(await readFile(configPath)),
    adapter: sha256(await readFile(path.join(ROOT, 'scripts/net_lockstep_websocket_relay.mjs'))),
    protocol: sha256(await readFile(path.join(ROOT, 'scripts/net_lockstep_protocol.mjs'))),
    directRuntime: sha256(await readFile(runtimeScript)),
    miniflareOptionsBuilder: sha256(await readFile(path.join(ROOT, 'scripts/net_room_relay_miniflare_options.mjs'))),
    dependenciesLock: sha256(await readFile(path.join(ROOT, 'dependencies.lock.json'))),
    ownerModule: sha256(await readFile(fileURLToPath(import.meta.url))),
    wranglerPackage: sha256(await readFile(WRANGLER_PACKAGE)),
    miniflarePackage: sha256(await readFile(MINIFLARE_PACKAGE)),
    workerdPackage: sha256(await readFile(WORKERD_PACKAGE)),
  };
  const env = {...process.env,
    MELEE_ROOM_RELAY_ROOT: ROOT,
    MELEE_ROOM_RELAY_CONFIG: configPath,
    MELEE_ROOM_RELAY_WRANGLER_CLI: wranglerCli,
    MELEE_ROOM_RELAY_MINIFLARE_ENTRY: miniflareEntry,
    MELEE_ROOM_RELAY_SCRATCH: scratch,
    MELEE_ROOM_RELAY_PERSIST: persist,
    MELEE_ROOM_RELAY_RESOURCE_TMP: resourceTmp,
    MELEE_ROOM_RELAY_RUNTIME_TMP: runtimeTmp,
    MELEE_ROOM_RELAY_PORT: String(port),
    MELEE_ROOM_RELAY_INSPECTOR_PORT: String(inspectorPort),
    TMPDIR: runtimeTmp,
  };
  for (const key of ['CLOUDFLARE_API_TOKEN', 'CLOUDFLARE_API_KEY', 'CLOUDFLARE_EMAIL', 'CLOUDFLARE_ACCOUNT_ID'])
    delete env[key];

  const child = spawn(process.execPath, [runtimeScript], {
    cwd: ROOT, env, stdio: ['ignore', 'pipe', 'pipe', 'ipc'], detached: true,
  });
  let stdout = '';
  let stderr = '';
  child.stdout.on('data', chunk => { stdout = appendBounded(stdout, chunk); });
  child.stderr.on('data', chunk => { stderr = appendBounded(stderr, chunk); });
  const exitPromise = new Promise(resolve => child.once('exit', (code, signal) => resolve({code, signal})));
  const closePromise = new Promise(resolve => child.once('close', (code, signal) => resolve({code, signal})));
  const base = `http://127.0.0.1:${port}`;
  let cleanupPromise = null;

  const close = passed => {
    if (cleanupPromise) return cleanupPromise;
    cleanupPromise = (async () => {
      const cleanupEvents = [];
      const record = (event, details = {}) => cleanupEvents.push({
        event, monotonicNs: process.hrtime.bigint().toString(), wallTime: new Date().toISOString(), ...details,
      });
      let exit = child.exitCode === null ? null : {code: child.exitCode, signal: child.signalCode};
      if (!exit && child.connected) {
        record('owner-dispose-request');
        await new Promise(resolve => child.send({type: 'dispose'}, error => {
          record('owner-dispose-send-result', {error: error ? String(error.stack || error) : null});
          resolve();
        }));
        exit = await bounded(exitPromise, DISPOSE_TIMEOUT_MS);
      }
      if (!exit && child.pid) {
        record('fallback-sigterm-request', {pid: child.pid, timeoutMs: FALLBACK_TERM_TIMEOUT_MS});
        sendSignal(child.pid, 'SIGTERM', record);
        exit = await bounded(exitPromise, FALLBACK_TERM_TIMEOUT_MS);
      }
      if (!exit && child.pid) {
        record('fallback-sigkill-request', {pid: child.pid, timeoutMs: FALLBACK_KILL_TIMEOUT_MS});
        sendSignal(child.pid, 'SIGKILL', record);
        exit = await bounded(exitPromise, FALLBACK_KILL_TIMEOUT_MS);
      }
      const processClose = exit ? await bounded(closePromise, CLOSE_TIMEOUT_MS) : null;
      let alive = groupAlive(child.pid);
      record('owner-process-group-check', {pid: child.pid || null, alive});
      if (alive && child.pid) {
        record('cleanup-sigkill-request', {pid: child.pid, timeoutMs: FINAL_GROUP_KILL_TIMEOUT_MS});
        sendSignal(child.pid, 'SIGKILL', record);
        await bounded(closePromise, FINAL_GROUP_KILL_TIMEOUT_MS);
        alive = groupAlive(child.pid);
      }

      const records = stdout.split(/\r?\n/).flatMap(line => {
        const prefix = 'MELEE_ROOM_RELAY_RUNTIME ';
        if (!line.startsWith(prefix)) return [];
        try { return [JSON.parse(line.slice(prefix.length))]; }
        catch { return [{event: 'runtime-record-parse-error', line}]; }
      });
      const runtimeErrors = records.filter(row => [
        'runtime-uncaught-error', 'bootstrap-error', 'dispose-error', 'host-unhandledRejection',
        'host-uncaughtException', 'runtime-record-parse-error',
      ].includes(row.event));
      const structuredErrors = records.filter(row => row.event === 'structured-log' &&
        (String(row.log?.level || '').toLowerCase() === 'error' ||
          /Uncaught Error|Network connection lost|exception outcome/i.test(String(row.log?.message || ''))));
      const stderrErrors = /\bERROR\b|Uncaught Error|Network connection lost|exception outcome/i.test(stderr);
      const ready = records.some(row => row.event === 'direct-miniflare-ready');
      const readyRecord = records.find(row => row.event === 'direct-miniflare-ready');
      const disposeRecord = records.find(row => row.event === 'dispose-complete');
      const disposed = Boolean(disposeRecord);
      const ownedTempPathsClean = disposeRecord?.runtimeTmpEmpty === true &&
        disposeRecord?.resourceTmpEmpty === true;
      const fallbackUsed = cleanupEvents.some(row => row.event.startsWith('fallback-') ||
        row.event.startsWith('cleanup-sigkill'));
      const runtimeClean = ready && disposed && exit?.code === 0 && !exit?.signal && processClose && !alive &&
        !fallbackUsed && !runtimeErrors.length && !structuredErrors.length && !stderrErrors &&
        ownedTempPathsClean && !cleanupEvents.some(row => row.error);
      const summary = {
        passed: Boolean(passed && runtimeClean),
        producer,
        ownerProcess: {pid: child.pid || null, processGroupId: child.pid || null},
        deadlinesMs: {startup: STARTUP_TIMEOUT_MS, dispose: DISPOSE_TIMEOUT_MS,
          fallbackSigterm: FALLBACK_TERM_TIMEOUT_MS, fallbackSigkill: FALLBACK_KILL_TIMEOUT_MS,
          closeEvent: CLOSE_TIMEOUT_MS, finalGroupKill: FINAL_GROUP_KILL_TIMEOUT_MS},
        runtimeIdentity: {
          node: process.version, wrangler: wrangler.version, miniflare: miniflare.version, workerd: workerd.version,
        },
        ports: {worker: port, inspector: inspectorPort},
        paths: {worker: 'online/relay/worker.mjs', config: 'online/relay/wrangler.jsonc',
          ownerScratch: scratch, resourcePersistencePath: persist,
          resourceTmpPath: resourceTmp, nodeTmpPath: runtimeTmp},
        hashes,
        ready, disposed, effectiveOptions: readyRecord?.effectiveOptions || null,
        runtimeTmpDirectories: readyRecord?.runtimeTemporaryDirectories || [],
        ownedTempPathsClean, runtimeErrors, structuredErrors, stderrErrors,
        exit, processClose, groupAlive: alive, fallbackUsed, cleanupEvents, runtimeRecords: records, stderr,
      };
      await writeFile(path.join(scratch, 'receipt.json'), JSON.stringify(summary, null, 2) + '\n');
      await writeFile(path.join(scratch, 'runtime.stdout.log'), stdout);
      await writeFile(path.join(scratch, 'runtime.stderr.log'), stderr);
      if (evidenceDir) {
        const evidenceRoot = path.resolve(evidenceDir);
        const scratchRoot = path.resolve(scratch);
        if (evidenceRoot === scratchRoot || evidenceRoot.startsWith(`${scratchRoot}${path.sep}`))
          throw Error('Retained Worker evidence must be outside disposable runtime scratch');
        await mkdir(evidenceRoot, {recursive: true});
        const prefix = `direct-miniflare-${path.basename(scratch)}`;
        await writeFile(path.join(evidenceRoot, `${prefix}.receipt.json`), JSON.stringify(summary, null, 2) + '\n');
        await writeFile(path.join(evidenceRoot, `${prefix}.stdout.log`), stdout);
        await writeFile(path.join(evidenceRoot, `${prefix}.stderr.log`), stderr);
      }
      if (passed && runtimeClean) {
        await rm(scratch, {recursive: true});
        diagnostic(`Direct Miniflare cleanup verified: dispose complete, exit ${exit.code}, process group absent; disposable scratch removed.`);
      } else {
        diagnostic(`Direct Miniflare evidence retained at ${scratch}`);
      }
      if (!runtimeClean) throw Error(`Direct Miniflare cleanup/runtime outcome failed; retained ${scratch}`);
      return summary;
    })();
    return cleanupPromise;
  };

  const startupDeadline = Date.now() + STARTUP_TIMEOUT_MS;
  let startupResponse = null;
  try {
    while (Date.now() < startupDeadline && child.exitCode === null && child.signalCode === null) {
      try {
        const remainingMs = Math.max(1, startupDeadline - Date.now());
        startupResponse = await fetch(`${base}/v1/rooms/too-short/socket`, {
          signal: AbortSignal.timeout(remainingMs),
        });
        if (startupResponse.status === 400) {
          await startupResponse.body?.cancel();
          break;
        }
        await startupResponse.body?.cancel();
      } catch {}
      await wait(50);
    }
    if (startupResponse?.status !== 400)
      throw Error('Actual Worker invalid-room HTTP route did not become ready with status 400');
  } catch (error) {
    try { await close(false); }
    catch (cleanupError) { error.cleanupError = cleanupError; }
    throw error;
  }

  return {base, port, inspectorPort, close, hashes,
    ownerProcess: {pid: child.pid || null, processGroupId: child.pid || null},
    runtimeIdentity: {node: process.version, wrangler: wrangler.version,
      miniflare: miniflare.version, workerd: workerd.version}};
}
