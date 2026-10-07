import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawn} from 'node:child_process';
import {createServer, request as httpRequest} from 'node:http';
import {existsSync} from 'node:fs';
import {lstat, mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import net from 'node:net';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import test from 'node:test';
import {LockstepPeer, LOCKSTEP_DELAY} from '../scripts/net_lockstep_protocol.mjs';
import {
  createRoomId, openRoomRelayPeerPair,
} from '../scripts/net_lockstep_websocket_relay.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const TOOLS = path.resolve(process.env.MELEE_A3_ROOM_RELAY_TOOLS ||
  path.join(ROOT, 'work/a3-room-relay-tools/node_modules'));
const WRANGLER_PACKAGE = path.join(TOOLS, 'wrangler/package.json');
const MINIFLARE_PACKAGE = path.join(TOOLS, 'miniflare/package.json');
const WORKERD_PACKAGE = path.join(TOOLS, 'workerd/package.json');
const HAS_RUNTIME_TOOLS = [WRANGLER_PACKAGE, MINIFLARE_PACKAGE, WORKERD_PACKAGE]
  .every(existsSync);
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
let directRuntimeEvidenceIndex = 0;
async function bounded(promise, timeoutMs) {
  let timer;
  try {
    return await Promise.race([promise, new Promise(resolve => {
      timer = setTimeout(() => resolve(null), timeoutMs);
    })]);
  } finally { clearTimeout(timer); }
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

function rawUpgradeStatus(urlText, extraHeaders = {}) {
  const url = new URL(urlText);
  const key = Buffer.from('room-relay-test-key').toString('base64');
  return new Promise((resolve, reject) => {
    const req = httpRequest({
      hostname: url.hostname,
      port: url.port,
      path: url.pathname,
      method: 'GET',
      headers: {
        connection: 'Upgrade', upgrade: 'websocket',
        'sec-websocket-version': '13', 'sec-websocket-key': key,
        ...extraHeaders,
      },
    }, response => {
      const chunks = [];
      response.on('data', chunk => chunks.push(chunk));
      response.on('end', () => resolve({status: response.statusCode,
        body: Buffer.concat(chunks).toString('utf8')}));
    });
    req.once('upgrade', (response, socket) => {
      socket.destroy();
      resolve({status: response.statusCode, body: ''});
    });
    req.once('error', reject);
    req.end();
  });
}

function serverFrame(opcode, payload) {
  const body = Buffer.from(payload);
  if (body.length >= 126) throw Error('test server frame helper only supports short payloads');
  return Buffer.concat([Buffer.from([0x80 | opcode, body.length]), body]);
}

function rawClient(url) {
  const socket = new WebSocket(url);
  const messages = [];
  const waiters = [];
  let closeResult = null;
  socket.addEventListener('message', event => {
    messages.push(event.data);
    for (let index = waiters.length - 1; index >= 0; --index) {
      const waiter = waiters[index];
      if (!waiter.predicate(event.data)) continue;
      waiters.splice(index, 1);
      clearTimeout(waiter.timeout);
      waiter.resolve(event.data);
    }
  });
  socket.addEventListener('close', event => {
    closeResult = {code: event.code, reason: event.reason, wasClean: event.wasClean};
  });
  const opened = new Promise((resolve, reject) => {
    const cleanup = () => {
      socket.removeEventListener('open', onOpen);
      socket.removeEventListener('error', onError);
      socket.removeEventListener('close', onClose);
    };
    const onOpen = () => { cleanup(); resolve(); };
    const onError = () => { cleanup(); reject(Error('raw room relay WebSocket failed before opening')); };
    const onClose = () => { cleanup(); reject(Error('raw room relay WebSocket closed before opening')); };
    socket.addEventListener('open', onOpen, {once: true});
    socket.addEventListener('error', onError, {once: true});
    socket.addEventListener('close', onClose, {once: true});
  });
  return {
    socket, messages, opened,
    waitFor(predicate, timeoutMs = 3000) {
      const found = messages.find(predicate);
      if (found !== undefined) return Promise.resolve(found);
      return new Promise((resolve, reject) => {
        const waiter = {predicate, resolve, reject, timeout: null};
        waiter.timeout = setTimeout(() => {
          const index = waiters.indexOf(waiter);
          if (index >= 0) waiters.splice(index, 1);
          reject(Error('timed out waiting for room relay message'));
        }, timeoutMs);
        waiters.push(waiter);
      });
    },
    waitForClose(timeoutMs = 3000, label = 'room relay') {
      if (closeResult) return Promise.resolve(closeResult);
      return new Promise((resolve, reject) => {
        const onClose = event => {
          clearTimeout(timeout);
          socket.removeEventListener('close', onClose);
          const outcome = {code: event.code, reason: event.reason, wasClean: event.wasClean};
          closeResult = outcome;
          resolve(outcome);
        };
        const timeout = setTimeout(() => {
          socket.removeEventListener('close', onClose);
          reject(Error(`timed out waiting for ${label} close; readyState=${socket.readyState}`));
        }, timeoutMs);
        socket.addEventListener('close', onClose, {once: true});
      });
    },
    get closeResult() { return closeResult; },
  };
}

async function waitFor(predicate, label, timeoutMs = 3000) {
  const deadline = Date.now() + timeoutMs;
  while (!predicate() && Date.now() < deadline) await wait(5);
  assert(predicate(), `timed out waiting for ${label}`);
}

function listen(endpoint) {
  const messages = [];
  const waiters = [];
  endpoint.onMessage(text => {
    messages.push(text);
    for (let index = waiters.length - 1; index >= 0; --index) {
      const waiter = waiters[index];
      if (!waiter.predicate(text)) continue;
      waiters.splice(index, 1);
      clearTimeout(waiter.timeout);
      waiter.resolve(text);
    }
  });
  return {
    messages,
    next(predicate, timeoutMs = 3000) {
      const found = messages.find(predicate);
      if (found !== undefined) return Promise.resolve(found);
      return new Promise((resolve, reject) => {
        const waiter = {predicate, resolve, timeout: null};
        waiter.timeout = setTimeout(() => {
          const index = waiters.indexOf(waiter);
          if (index >= 0) waiters.splice(index, 1);
          reject(Error('timed out waiting for forwarded A2 packet'));
        }, timeoutMs);
        waiters.push(waiter);
      });
    },
  };
}

async function startWorker(t) {
  if (!HAS_RUNTIME_TOOLS) t.skip(`install the pinned local runtime under work or set MELEE_A3_ROOM_RELAY_TOOLS; looked in ${TOOLS}`);
  const lock = JSON.parse(await readFile(path.join(ROOT, 'dependencies.lock.json'), 'utf8'));
  const [wrangler, miniflare, workerd] = await Promise.all([
    readFile(WRANGLER_PACKAGE), readFile(MINIFLARE_PACKAGE), readFile(WORKERD_PACKAGE),
  ]).then(rows => rows.map(bytes => JSON.parse(bytes.toString('utf8'))));
  assert.equal(wrangler.version, lock.deployment_tools.wrangler.version);
  assert.equal(miniflare.version, lock.deployment_tools.miniflare.version);
  assert.equal(workerd.version, lock.deployment_tools.workerd.version);
  assert.equal(wrangler.dependencies.miniflare, miniflare.version,
    'direct Miniflare must match the package Wrangler declares');
  assert.equal(wrangler.dependencies.workerd, workerd.version,
    'direct workerd must match the package Wrangler declares');
  assert.equal(miniflare.dependencies.workerd, workerd.version,
    'Miniflare must use the pinned workerd runtime');
  assert.equal(typeof globalThis.WebSocket, 'function', 'test requires Node global WebSocket');
  assert(Number(process.versions.node.split('.')[0]) >= 22, 'pinned Wrangler requires Node 22 or newer');

  const workRoot = path.join(ROOT, 'work');
  await mkdir(workRoot, {recursive: true});
  const workInfo = await lstat(workRoot);
  assert(workInfo.isDirectory() && !workInfo.isSymbolicLink(), 'test scratch requires the real checkout work directory');
  const scratch = await mkdtemp(path.join(workRoot, 'a3-room-relay-direct-'));
  const persist = path.join(scratch, 'state');
  await mkdir(persist);
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
    dependenciesLock: sha256(await readFile(path.join(ROOT, 'dependencies.lock.json'))),
    testHarness: sha256(await readFile(fileURLToPath(import.meta.url))),
    contractWrapper: sha256(await readFile(path.join(ROOT, 'tests/test_net_lockstep_contracts.py'))),
    wranglerPackage: sha256(await readFile(WRANGLER_PACKAGE)),
    miniflarePackage: sha256(await readFile(MINIFLARE_PACKAGE)),
    workerdPackage: sha256(await readFile(WORKERD_PACKAGE)),
  };
  const env = {...process.env,
    MELEE_ROOM_RELAY_ROOT: ROOT,
    MELEE_ROOM_RELAY_CONFIG: configPath,
    MELEE_ROOM_RELAY_WRANGLER_CLI: wranglerCli,
    MELEE_ROOM_RELAY_MINIFLARE_ENTRY: miniflareEntry,
    MELEE_ROOM_RELAY_PERSIST: persist,
    MELEE_ROOM_RELAY_PORT: String(port),
    MELEE_ROOM_RELAY_INSPECTOR_PORT: String(inspectorPort),
  };
  for (const key of ['CLOUDFLARE_API_TOKEN', 'CLOUDFLARE_API_KEY', 'CLOUDFLARE_EMAIL', 'CLOUDFLARE_ACCOUNT_ID'])
    delete env[key];
  const child = spawn(process.execPath, [runtimeScript], {
    cwd: ROOT, env, stdio: ['ignore', 'pipe', 'pipe', 'ipc'], detached: true,
  });
  let stdout = '';
  let stderr = '';
  const append = (current, chunk) => {
    const next = current + chunk.toString();
    return next.length <= 4 * 1024 * 1024 ? next : next.slice(-4 * 1024 * 1024);
  };
  child.stdout.on('data', chunk => { stdout = append(stdout, chunk); });
  child.stderr.on('data', chunk => { stderr = append(stderr, chunk); });
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
        exit = await bounded(exitPromise, 5000);
      }
      if (!exit && child.pid) {
        record('fallback-sigterm-request', {pid: child.pid});
        try { process.kill(-child.pid, 'SIGTERM'); } catch (error) { record('fallback-sigterm-error', {error: String(error)}); }
        exit = await bounded(exitPromise, 3000);
      }
      if (!exit && child.pid) {
        record('fallback-sigkill-request', {pid: child.pid});
        try { process.kill(-child.pid, 'SIGKILL'); } catch (error) { record('fallback-sigkill-error', {error: String(error)}); }
        exit = await bounded(exitPromise, 3000);
      }
      if (exit) await bounded(closePromise, 1000);
      let groupAlive = false;
      if (child.pid) {
        try { process.kill(-child.pid, 0); groupAlive = true; }
        catch (error) { if (error.code !== 'ESRCH') groupAlive = true; }
      }
      record('owner-process-group-check', {pid: child.pid || null, alive: groupAlive});
      if (groupAlive && child.pid) {
        record('cleanup-sigkill-request', {pid: child.pid});
        try { process.kill(-child.pid, 'SIGKILL'); } catch (error) { record('cleanup-sigkill-error', {error: String(error)}); }
        await bounded(closePromise, 3000);
        try { process.kill(-child.pid, 0); groupAlive = true; }
        catch (error) { groupAlive = error.code !== 'ESRCH'; }
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
      const disposed = records.some(row => row.event === 'dispose-complete');
      const fallbackUsed = cleanupEvents.some(row => row.event.startsWith('fallback-') ||
        row.event.startsWith('cleanup-sigkill'));
      const runtimeClean = ready && disposed && exit?.code === 0 && !exit?.signal && !groupAlive &&
        !fallbackUsed && !runtimeErrors.length && !structuredErrors.length && !stderrErrors &&
        !cleanupEvents.some(row => row.error);
      const summary = {
        passed: Boolean(passed && runtimeClean),
        producer: {
          commit: process.env.MELEE_A3_ROOM_RELAY_PRODUCER_COMMIT || null,
          tree: process.env.MELEE_A3_ROOM_RELAY_PRODUCER_TREE || null,
        },
        runtimeIdentity: {
          node: process.version, wrangler: wrangler.version, miniflare: miniflare.version, workerd: workerd.version,
        },
        paths: {worker: 'online/relay/worker.mjs', config: 'online/relay/wrangler.jsonc'},
        hashes,
        ready, disposed, runtimeErrors, structuredErrors, stderrErrors,
        exit, groupAlive, fallbackUsed, cleanupEvents, runtimeRecords: records, stderr,
      };
      await writeFile(path.join(scratch, 'receipt.json'), JSON.stringify(summary, null, 2) + '\n');
      await writeFile(path.join(scratch, 'runtime.stdout.log'), stdout);
      await writeFile(path.join(scratch, 'runtime.stderr.log'), stderr);
      const evidenceDir = process.env.MELEE_A3_ROOM_RELAY_EVIDENCE_DIR;
      if (evidenceDir) {
        const evidenceRoot = path.resolve(evidenceDir);
        const scratchRoot = path.resolve(scratch);
        assert(evidenceRoot !== scratchRoot && !evidenceRoot.startsWith(`${scratchRoot}${path.sep}`),
          'retained Worker evidence must be outside disposable runtime scratch');
        await mkdir(evidenceRoot, {recursive: true});
        const runName = `direct-miniflare-${String(++directRuntimeEvidenceIndex).padStart(2, '0')}`;
        await writeFile(path.join(evidenceRoot, `${runName}.receipt.json`), JSON.stringify(summary, null, 2) + '\n');
        await writeFile(path.join(evidenceRoot, `${runName}.stdout.log`), stdout);
        await writeFile(path.join(evidenceRoot, `${runName}.stderr.log`), stderr);
      }
      if (passed && runtimeClean) {
        await rm(scratch, {recursive: true});
        t.diagnostic(`Direct Miniflare cleanup verified: dispose complete, exit ${exit.code}, process group absent; disposable scratch removed.`);
      } else {
        t.diagnostic(`Direct Miniflare evidence retained at ${scratch}`);
      }
      if (!runtimeClean) throw Error(`direct Miniflare cleanup/runtime outcome failed; retained ${scratch}`);
      return summary;
    })();
    return cleanupPromise;
  };

  const startupDeadline = Date.now() + 20000;
  let startupResponse = null;
  try {
    while (Date.now() < startupDeadline && child.exitCode === null && child.signalCode === null) {
      try {
        startupResponse = await fetch(`${base}/v1/rooms/too-short/socket`);
        if (startupResponse.status === 400) break;
      } catch {}
      await wait(50);
    }
    assert.equal(startupResponse?.status, 400, 'actual Worker invalid-room HTTP route did not become ready');
  } catch (error) {
    await close(false).catch(cleanupError => { error.cleanupError = cleanupError; });
    throw error;
  }
  return {base, close, hashes, runtimeIdentity: {node: process.version,
    wrangler: wrangler.version, miniflare: miniflare.version, workerd: workerd.version}};
}

function makeChecksum(tick, inputHash = tick) {
  const bytes = Buffer.alloc(64);
  bytes.writeUInt32LE(tick, 0);
  bytes.writeBigUInt64LE(BigInt(inputHash), 24);
  return bytes;
}

function makePad(value) { return Buffer.from([0, value, value, 0, 0, 0, 0, 0, 0, 0, 0]); }

function attachLockstep(pair, sourceTicks, {disconnects, frames, sent, received, terminals}) {
  const inputTicks = sourceTicks - LOCKSTEP_DELAY;
  const peers = {};
  for (const role of ['alpha', 'beta']) {
    peers[role] = new LockstepPeer({role, sourceTicks, inputTicks,
      pushFrame: async (firstTick, bytes) => {
        for (let offset = 0; offset < bytes.length; offset += 44)
          frames[role].push({tick: firstTick + offset / 44, bytes: Buffer.from(bytes.subarray(offset, offset + 44))});
      },
      onTerminal: async value => terminals[role].push(value),
    });
    pair[role].onMessage(async raw => {
      received[role].push(raw);
      try { await peers[role].receive(raw); }
      catch (error) { disconnects.push({role, error: String(error.message || error)}); }
    });
    peers[role].attach(async raw => {
      sent[role].push(raw);
      await pair[role].send(raw);
    });
  }
  return peers;
}

test('room adapter reports a close-handshake timeout instead of claiming closure', async () => {
  const server = createServer();
  const sockets = [];
  const ready = Buffer.from(JSON.stringify({relay: 1, event: 'ready'}));
  server.on('upgrade', (request, socket) => {
    const accept = createHash('sha1')
      .update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sockets.push(socket);
    if (sockets.length === 2) {
      const frame = Buffer.concat([Buffer.from([0x81, ready.length]), ready]);
      for (const peer of sockets) peer.write(frame);
    }
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const {port} = server.address();
  let pair;
  try {
    pair = await openRoomRelayPeerPair({url: `ws://127.0.0.1:${port}`, timeoutMs: 100});
    await assert.rejects(pair.close(), error => error instanceof AggregateError &&
      error.errors.some(row => /did not close within 100 ms/.test(String(row?.message || row))));
  } finally {
    for (const socket of sockets) socket.destroy();
    const deadline = Date.now() + 1000;
    while (pair && (!pair.alpha.closed || !pair.beta.closed) && Date.now() < deadline) await wait(5);
    await new Promise(resolve => server.close(resolve));
  }
  assert(pair.alpha.closed && pair.beta.closed, 'owned client sockets closed after the deliberately failed close gate');
});

test('room adapter bounds all inbound messages once and preserves the disconnect callback', async () => {
  const server = createServer();
  const sockets = [];
  const closeStates = new Map();
  const fixtureErrors = [];
  // This fixture receives only client Close frames. Finish TCP after both
  // directions exchange Close; a browser client waits for the server's FIN.
  const sendClose = socket => {
    const state = closeStates.get(socket);
    state.sent = true;
    socket.write(serverFrame(8, closePayload));
    if (state.received) socket.end();
  };
  const ready = Buffer.from(JSON.stringify({relay: 1, event: 'ready'}));
  const closePayload = Buffer.alloc(2 + Buffer.byteLength('test close'));
  closePayload.writeUInt16BE(4003, 0);
  closePayload.write('test close', 2);
  server.on('upgrade', (request, socket) => {
    const accept = createHash('sha1')
      .update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest('base64');
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
    sockets.push(socket);
    const state = {sent: false, received: false, bytes: Buffer.alloc(0)};
    closeStates.set(socket, state);
    socket.on('data', chunk => {
      if (state.bytes.length + chunk.length > 131) {
        fixtureErrors.push('client Close frame exceeded its control-frame bound');
        socket.destroy();
        return;
      }
      state.bytes = Buffer.concat([state.bytes, chunk]);
      if (state.bytes.length < 2) return;
      const length = state.bytes[1] & 0x7f;
      if (state.bytes[0] !== 0x88 || !(state.bytes[1] & 0x80) || length < 2 || length > 125) {
        fixtureErrors.push('expected one masked client Close frame');
        socket.destroy();
        return;
      }
      if (state.bytes.length < 6 + length) return;
      const payload = Buffer.from(state.bytes.subarray(6, 6 + length));
      for (let index = 0; index < length; ++index)
        payload[index] ^= state.bytes[2 + index % 4];
      if (state.bytes.length !== 6 + length || payload.readUInt16BE(0) !== 4003) {
        fixtureErrors.push('client Close frame was extra or had the wrong code');
        socket.destroy();
        return;
      }
      state.received = true;
      if (state.sent) socket.end();
    });
    if (sockets.length === 2)
      for (const peer of sockets) peer.write(serverFrame(1, ready));
  });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const {port} = server.address();
  let pair;
  const delivered = [];
  const disconnects = [];
  const endpointErrors = [];
  try {
    pair = await openRoomRelayPeerPair({url: `ws://127.0.0.1:${port}`, timeoutMs: 1000,
      onDisconnect: async (role, reason) => { disconnects.push({role, reason}); },
      onEndpointError: (role, error) => endpointErrors.push({role, error: String(error?.message || error)}),
    });
    pair.alpha.onMessage(async text => { await wait(2); delivered.push(text); });
    const flood = [...Array(257)].map(() => serverFrame(1, Buffer.from('x')));
    sockets[0].write(Buffer.concat(flood));
    await waitFor(() => endpointErrors.length === 1, 'one-time inbound queue overflow notification');
    sendClose(sockets[0]);
    sendClose(sockets[1]);
    await waitFor(() => disconnects.length === 2, 'both reserved disconnect notifications');
    await assert.rejects(pair.alpha.drainInbound(5000), error => error instanceof AggregateError &&
      error.errors.some(row => /inbound callback queue exceeded its bound/.test(String(row))));
    await pair.beta.drainInbound(5000);
    assert.deepEqual(fixtureErrors, []);
    assert([...closeStates.values()].every(state => state.sent && state.received),
      'both raw servers exchange Close frames before TCP shutdown');
    assert(disconnects.every(row => row.reason === '4003: test close'));
    assert.equal(delivered.length, 256, 'the data callback queue admits no more than its declared message bound');
    assert.equal(pair.alpha.errors.length, 1, 'terminal queue overflow is reported once');
    assert.equal(endpointErrors.length, 1);
    assert.equal(endpointErrors[0].role, 'alpha');
    assert.deepEqual(disconnects.map(row => row.role).sort(), ['alpha', 'beta']);
    await pair.close();
    assert(pair.alpha.closed && pair.beta.closed);
  } finally {
    for (const socket of sockets) socket.destroy();
    await new Promise(resolve => server.close(resolve));
  }
});

test('real Worker HTTP/WebSocket room contract and serialized async callbacks', {
  skip: !HAS_RUNTIME_TOOLS && `pinned direct-runtime packages not present under ${TOOLS}`,
}, async t => {
  const worker = await startWorker(t);
  const pairs = [];
  const rawClients = [];
  let passed = false;
  let cleanupError = null;
  try {
    const base = worker.base;
    const wsBase = base.replace(/^http:/, 'ws:');
    const readyText = JSON.stringify({relay: 1, event: 'ready'});
    const room1 = createRoomId(), room2 = createRoomId();
    const room1Url = `${wsBase}/v1/rooms/${room1}/socket`;

    const invalidRoom = await fetch(`${base}/v1/rooms/too-short/socket`);
    assert.equal(invalidRoom.status, 400);
    assert.deepEqual(await invalidRoom.json(), {error: 'invalid room id'});
    const wrongMethod = await fetch(`${base}/v1/rooms/${room1}/socket`, {method: 'POST'});
    assert.equal(wrongMethod.status, 405);
    assert.deepEqual(await wrongMethod.json(), {error: 'WebSocket room requires GET'});
    const noUpgrade = await fetch(`${base}/v1/rooms/${room1}/socket`);
    assert.equal(noUpgrade.status, 426);
    assert.deepEqual(await noUpgrade.json(), {error: 'WebSocket upgrade required'});
    const unknownRoute = await fetch(`${base}/health`);
    assert.equal(unknownRoute.status, 404);
    const rejectedOrigin = await rawUpgradeStatus(room1Url, {origin: 'https://untrusted.invalid'});
    assert.equal(rejectedOrigin.status, 403);
    assert.deepEqual(JSON.parse(rejectedOrigin.body), {error: 'origin is not allowed'});

    const early = rawClient(`${wsBase}/v1/rooms/${createRoomId()}/socket`);
    rawClients.push(early);
    await early.opened;
    await wait(30);
    assert.deepEqual(early.messages, [], 'a lone accepted peer receives no ready packet or buffered data');
    const earlyClose = early.waitForClose(3000, 'pre-ready peer');
    early.socket.send('opaque A2 packet before the second peer');
    assert.deepEqual(await earlyClose, {code: 4002, reason: 'peer is not ready', wasClean: true});
    assert.deepEqual(early.messages, [], 'pre-ready A2 packets are rejected, never queued or replayed');

    const reportedErrors = [];
    const pair1 = await openRoomRelayPeerPair({url: wsBase, roomId: room1,
      onEndpointError: (role, error) => reportedErrors.push({role, message: String(error?.message || error)}),
    });
    pairs.push(pair1);
    const pair2 = await openRoomRelayPeerPair({url: wsBase, roomId: room2});
    pairs.push(pair2);
    const side1 = {alpha: listen(pair1.alpha), beta: listen(pair1.beta)};
    const side2 = {alpha: listen(pair2.alpha), beta: listen(pair2.beta)};
    const marker = JSON.stringify({type: 'hello', version: 1, opaque: 'A2-byte-opacity-雪'});
    await pair1.alpha.send(marker);
    assert.equal(await side1.beta.next(text => text === marker), marker);
    await pair1.beta.drainInbound();
    await wait(30);
    assert.deepEqual(side2.alpha.messages, []);
    assert.deepEqual(side2.beta.messages, [], 'messages remain isolated to their room ID');

    const third = await rawUpgradeStatus(room1Url);
    assert.equal(third.status, 409);
    assert.deepEqual(JSON.parse(third.body), {error: 'room already has two peers'});

    const order = [];
    const expectedCallbacks = 6;
    pair1.beta.onMessage(async text => {
      if (text === 'first') await wait(30);
      order.push(`slow:${text}`);
      if (text === 'reject') throw Error('intentional async listener rejection');
    });
    pair1.beta.onMessage(async text => { order.push(`next:${text}`); });
    for (const text of ['first', 'second', 'reject']) await pair1.alpha.send(text);
    await waitFor(() => order.length === expectedCallbacks, 'serialized asynchronous WebSocket callbacks');
    assert.deepEqual(order, [
      'slow:first', 'next:first', 'slow:second', 'next:second', 'slow:reject', 'next:reject',
    ]);
    await assert.rejects(pair1.beta.drainInbound(), error =>
      error instanceof AggregateError &&
      error.errors.some(row => /intentional async listener rejection/.test(String(row))));
    assert.equal(reportedErrors.length, 1);
    assert.equal(reportedErrors[0].role, 'beta');

    const asyncDisconnects = [];
    const disconnectErrors = [];
    const disconnectPair = await openRoomRelayPeerPair({url: wsBase,
      onDisconnect: async (role, reason) => {
        await wait(20);
        asyncDisconnects.push({role, reason});
        if (role === 'alpha') throw Error('intentional async disconnect rejection');
      },
      onEndpointError: (role, error) => disconnectErrors.push({role, message: String(error?.message || error)}),
    });
    pairs.push(disconnectPair);
    disconnectPair.alpha.destroy();
    await waitFor(() => asyncDisconnects.length === 2, 'awaited async disconnect callbacks');
    await assert.rejects(disconnectPair.alpha.drainInbound(), error => error instanceof AggregateError &&
      error.errors.some(row => /intentional async disconnect rejection/.test(String(row))));
    await disconnectPair.beta.drainInbound();
    assert.deepEqual(asyncDisconnects.map(row => row.role).sort(), ['alpha', 'beta']);
    assert.deepEqual(disconnectErrors, [{role: 'alpha', message: 'intentional async disconnect rejection'}]);

    const closeRoom = createRoomId();
    const closeUrl = `${wsBase}/v1/rooms/${closeRoom}/socket`;
    const closeA = rawClient(closeUrl), closeB = rawClient(closeUrl);
    rawClients.push(closeA, closeB);
    await Promise.all([closeA.opened, closeB.opened]);
    await Promise.all([closeA.waitFor(text => text === readyText), closeB.waitFor(text => text === readyText)]);
    const senderClose = closeA.waitForClose(3000, 'sender');
    const partnerClose = closeB.waitForClose(3000, 'partner');
    closeA.socket.close(4009, 'explicit close reason');
    assert.deepEqual(await senderClose, {code: 4009, reason: 'explicit close reason', wasClean: true});
    assert.deepEqual(await partnerClose, {code: 4001, reason: 'peer disconnected', wasClean: true});

    const binaryRoom = createRoomId();
    const binaryA = rawClient(`${wsBase}/v1/rooms/${binaryRoom}/socket`);
    const binaryB = rawClient(`${wsBase}/v1/rooms/${binaryRoom}/socket`);
    rawClients.push(binaryA, binaryB);
    await Promise.all([binaryA.opened, binaryB.opened]);
    await Promise.all([binaryA.waitFor(text => text === readyText), binaryB.waitFor(text => text === readyText)]);
    const binaryCloseA = binaryA.waitForClose(), binaryCloseB = binaryB.waitForClose();
    binaryA.socket.send(new Uint8Array([1, 2]));
    assert.deepEqual(await binaryCloseA, {code: 4003, reason: 'A2 relay accepts text packets only', wasClean: true});
    assert.deepEqual(await binaryCloseB, {code: 4001, reason: 'peer disconnected', wasClean: true});

    const oversizedRoom = createRoomId();
    const oversizedA = rawClient(`${wsBase}/v1/rooms/${oversizedRoom}/socket`);
    const oversizedB = rawClient(`${wsBase}/v1/rooms/${oversizedRoom}/socket`);
    rawClients.push(oversizedA, oversizedB);
    await Promise.all([oversizedA.opened, oversizedB.opened]);
    await Promise.all([oversizedA.waitFor(text => text === readyText), oversizedB.waitFor(text => text === readyText)]);
    const oversizedCloseA = oversizedA.waitForClose(), oversizedCloseB = oversizedB.waitForClose();
    oversizedA.socket.send('x'.repeat(1024 * 1024 + 1));
    assert.deepEqual(await oversizedCloseA, {code: 4003, reason: 'A2 packet exceeds 1 MiB', wasClean: true});
    assert.deepEqual(await oversizedCloseB, {code: 4001, reason: 'peer disconnected', wasClean: true});

    await pair1.close();
    await pair2.close();
    await assert.rejects(pair1.alpha.send('after-close'), /WebSocket is closed/);
    await assert.rejects(pair1.alpha.flush(), /transport closed before its send buffer drained/);
    await assert.deepEqual(pair1.transport, {
      type: 'room-websocket', peer_limit: 2, packet_limit_bytes: 1024 * 1024,
      buffered_queue_limit_bytes: 1024 * 1024,
      inbound_callback_queue_limit_bytes: 1024 * 1024,
      inbound_callback_queue_limit_messages: 256,
    });
    passed = true;
    t.diagnostic(`Actual Worker/DO HTTP-WebSocket contract passed with Wrangler ${worker.runtimeIdentity.wrangler}, Miniflare ${worker.runtimeIdentity.miniflare}, workerd ${worker.runtimeIdentity.workerd}; Worker/config SHA-256 ${worker.hashes.worker}/${worker.hashes.config}.`);
  } finally {
    const cleanupFailures = [];
    for (const pair of pairs) {
      try { await pair.close(); } catch (error) { cleanupFailures.push(error); }
    }
    const openRaw = rawClients.filter(client => client.socket.readyState === WebSocket.OPEN ||
      client.socket.readyState === WebSocket.CONNECTING);
    for (const client of openRaw) client.socket.close(1000, 'test cleanup');
    const rawCloses = await Promise.allSettled(rawClients.map(client => client.waitForClose(3000, 'test client')));
    cleanupFailures.push(...rawCloses.filter(result => result.status === 'rejected').map(result => result.reason));
    try { await worker.close(passed && cleanupFailures.length === 0); }
    catch (error) { cleanupFailures.push(error); }
    cleanupError = cleanupFailures.length ? new AggregateError(cleanupFailures, 'room relay integration cleanup failed') : null;
    if (cleanupError) throw cleanupError;
  }
});

test('A2 lockstep identity, input/ACK, checksum desync, and disconnect remain unchanged over the Worker relay', {
  skip: !HAS_RUNTIME_TOOLS && `pinned direct-runtime packages not present under ${TOOLS}`,
}, async t => {
  const worker = await startWorker(t);
  const pairs = [];
  let passed = false;
  try {
    const base = worker.base.replace(/^http:/, 'ws:');
    const agreement = {protocol: 'melee-web-local-lockstep-a2-v1', test: 'real-worker-relay', marker: '雪'};
    const pair = await openRoomRelayPeerPair({url: base});
    pairs.push(pair);
    const frames = {alpha: [], beta: []};
    const sent = {alpha: [], beta: []};
    const received = {alpha: [], beta: []};
    const terminals = {alpha: [], beta: []};
    const protocolErrors = [];
    const peers = attachLockstep(pair, 6, {disconnects: protocolErrors, frames, sent, received, terminals});
    await peers.alpha.start(agreement);
    await peers.beta.start(agreement);
    await waitFor(() => peers.alpha.ready && peers.beta.ready, 'A2 identity handshake');
    await Promise.all([pair.alpha.drainInbound(), pair.beta.drainInbound()]);
    assert.deepEqual(Buffer.from(received.beta[0]), Buffer.from(sent.alpha[0]), 'relay preserves A2 hello bytes');
    assert.deepEqual(Buffer.from(received.alpha[0]), Buffer.from(sent.beta[0]), 'reverse A2 hello bytes are unchanged');

    await peers.alpha.addLocalInput(0, makePad(0x31));
    await waitFor(() => peers.alpha.remoteAckInput === 0 && peers.beta.remote.has(0), 'ACK while beta input is absent');
    assert.equal(peers.alpha.nextSourceFrame, LOCKSTEP_DELAY, 'missing remote PAD holds the source input seam');
    await peers.beta.addLocalInput(0, makePad(0x72));
    await waitFor(() => peers.alpha.nextSourceFrame === 3 && peers.beta.nextSourceFrame === 3, 'delayed input release');
    for (let tick = 1; tick < 4; ++tick) {
      await peers.alpha.addLocalInput(tick, makePad(0x31 + tick));
      await peers.beta.addLocalInput(tick, makePad(0x72 + tick));
    }
    await waitFor(() => peers.alpha.nextSourceFrame === 6 && peers.beta.nextSourceFrame === 6,
      'all six indexed source frames');
    assert.deepEqual(frames.alpha, frames.beta, 'both protocol peers construct the same four-port source frames');

    for (let tick = 0; tick < 6; ++tick) {
      await peers.alpha.addChecksum(makeChecksum(tick));
      await peers.beta.addChecksum(makeChecksum(tick, tick === 2 ? tick + 1 : tick));
    }
    await peers.alpha.setNativeProgress(6, {flushFinal: true});
    await peers.beta.setNativeProgress(6, {flushFinal: true});
    await waitFor(() => peers.alpha.terminal?.kind === 'desync' && peers.beta.terminal?.kind === 'desync',
      'A2 first checksum desync');
    assert.deepEqual(peers.alpha.checksumMismatches[0], {tick: 2, channel: 1});
    assert.equal(peers.beta.terminal.tick, 2);
    assert.equal(peers.beta.terminal.channel, 1);
    assert.deepEqual(protocolErrors, []);
    assert(terminals.alpha.some(row => row.kind === 'desync'));
    assert(terminals.beta.some(row => row.kind === 'desync'));
    await Promise.all([pair.alpha.drainInbound(), pair.beta.drainInbound()]);

    const mismatchPair = await openRoomRelayPeerPair({url: base});
    pairs.push(mismatchPair);
    const mismatchFrames = {alpha: [], beta: []};
    const mismatchSent = {alpha: [], beta: []};
    const mismatchReceived = {alpha: [], beta: []};
    const mismatchErrors = [];
    const mismatchTerminals = {alpha: [], beta: []};
    const mismatchPeers = attachLockstep(mismatchPair, 6, {disconnects: mismatchErrors,
      frames: mismatchFrames, sent: mismatchSent, received: mismatchReceived, terminals: mismatchTerminals});
    await mismatchPeers.alpha.start({protocol: 'melee-web-local-lockstep-a2-v1', marker: 'alpha'});
    await mismatchPeers.beta.start({protocol: 'melee-web-local-lockstep-a2-v1', marker: 'beta'});
    await waitFor(() => mismatchPeers.alpha.terminal?.kind === 'protocol' &&
      mismatchPeers.beta.terminal?.kind === 'protocol', 'A2 start identity mismatch');
    await Promise.all([mismatchPair.alpha.drainInbound(), mismatchPair.beta.drainInbound()]);
    assert.equal(mismatchPeers.alpha.ready, false);
    assert.equal(mismatchPeers.beta.ready, false);
    assert.equal(mismatchErrors.length, 2);
    assert(mismatchErrors.every(row => /start identity/.test(row.error)));
    assert.equal(mismatchTerminals.alpha[0].kind, 'protocol');
    assert.equal(mismatchTerminals.beta[0].kind, 'protocol');

    let disconnectPeers;
    const disconnectPair = await openRoomRelayPeerPair({url: base,
      onDisconnect: async (role, reason) => { await disconnectPeers?.[role]?.disconnect(reason); },
    });
    pairs.push(disconnectPair);
    const disconnectFrames = {alpha: [], beta: []};
    const disconnectSent = {alpha: [], beta: []};
    const disconnectReceived = {alpha: [], beta: []};
    const disconnectErrors = [];
    const disconnectTerminals = {alpha: [], beta: []};
    disconnectPeers = attachLockstep(disconnectPair, 6, {disconnects: disconnectErrors,
      frames: disconnectFrames, sent: disconnectSent, received: disconnectReceived,
      terminals: disconnectTerminals});
    await disconnectPeers.alpha.start(agreement);
    await disconnectPeers.beta.start(agreement);
    await waitFor(() => disconnectPeers.alpha.ready && disconnectPeers.beta.ready, 'second room handshake');
    disconnectPair.alpha.destroy();
    await waitFor(() => disconnectPeers.alpha.terminal?.kind === 'disconnect' &&
      disconnectPeers.beta.terminal?.kind === 'disconnect', 'A2 transport disconnect on both peers');
    await Promise.all([disconnectPair.alpha.drainInbound(), disconnectPair.beta.drainInbound()]);
    assert.deepEqual(disconnectErrors, []);
    assert.equal(disconnectTerminals.alpha[0].kind, 'disconnect');
    assert.equal(disconnectTerminals.beta[0].kind, 'disconnect');

    await pair.close();
    await assert.rejects(pair.alpha.flush(), /transport closed before its send buffer drained/);
    passed = true;
    t.diagnostic(`Existing A2 protocol tests passed through the actual Worker/DO; Wrangler ${worker.runtimeIdentity.wrangler}, Miniflare ${worker.runtimeIdentity.miniflare}, workerd ${worker.runtimeIdentity.workerd}.`);
  } finally {
    const cleanupFailures = [];
    for (const pair of pairs) {
      try { await pair.close(); } catch (error) { cleanupFailures.push(error); }
    }
    try { await worker.close(passed && cleanupFailures.length === 0); }
    catch (error) { cleanupFailures.push(error); }
    if (cleanupFailures.length) throw new AggregateError(cleanupFailures, 'A2 Worker integration cleanup failed');
  }
});
