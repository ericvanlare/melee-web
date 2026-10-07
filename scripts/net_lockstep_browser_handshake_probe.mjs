#!/usr/bin/env node
/*
 * Issue 201: one browser-owned A2 peer and one Node-owned peer through the
 * actual local room Worker. --prepare stages only the fixture and exact
 * portable modules in external scratch; --run is reserved for a reviewed
 * capture packet and is not an alternative to the full gameplay harness.
 */
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {createReadStream} from 'node:fs';
import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {parseArgs} from 'node:util';
import {browserLaunchOptions, resolveBrowserTools, loadBrowserTools} from './browser_tools.mjs';
import {LockstepPeer, lockstepConstants} from './net_lockstep_protocol.mjs';
import {createRoomId, createRoomRelayPeerEndpoint} from './net_lockstep_websocket_relay.mjs';
import {startRoomRelayRuntime} from './net_room_relay_runtime_owner.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const NODE_FACADE = path.join(ROOT, 'scripts/net_lockstep_protocol.mjs');
const CORE = path.join(ROOT, 'scripts/net_lockstep_core.mjs');
const ADAPTER = path.join(ROOT, 'scripts/net_lockstep_websocket_relay.mjs');
const MINIFLARE_OPTIONS = path.join(ROOT, 'scripts/net_room_relay_miniflare_options.mjs');
const SERVE = path.join(ROOT, 'scripts/serve.py');
const PRE_PORTABLE_FIXTURE = path.join(ROOT, 'tests/fixtures/net_lockstep_pre_portable_wire_v1.json');
const SITE_NAMES = ['net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs', 'probe.html'];
const EXPECTED_NEUTRAL_PAD_HEX = '0000000000000000000000';
const EXPECTED_NO_CONTROLLER_PAD_HEX = '00000000000000000000ff';
const EXPECTED_NEUTRAL_FRAMES = lockstepConstants.delay;
const EXPECTED_AGREEMENT = Object.freeze({schema: 'a3-browser-owned-handshake-v1',
  source_ticks: EXPECTED_NEUTRAL_FRAMES, input_ticks: 0, delay: EXPECTED_NEUTRAL_FRAMES, seed: 201});
export const EXPECTED_AGREEMENT_JSON = JSON.stringify(EXPECTED_AGREEMENT);
const EXPECTED_AGREEMENT_SHA256 = '04e0100de1e0f847136cb6148aa1615b7dc528175edb0e34a37b82fb15d09519';
const REQUIRED_HEADERS = Object.freeze({
  'cross-origin-opener-policy': 'same-origin',
  'cross-origin-embedder-policy': 'require-corp',
});

export function validateBrowserStartOptions(options, delay, agreementJson) {
  const expectedFrames = options?.expectedFrames;
  const agreement = options?.agreement;
  if (delay !== 2 || expectedFrames !== delay || agreement?.source_ticks !== delay ||
      agreement?.input_ticks !== 0 || agreement?.delay !== delay ||
      JSON.stringify(agreement) !== agreementJson)
    throw Error('browser alpha requires the exact two-frame, input-free frozen agreement before allocating a peer or endpoint');
  return {expectedFrames, agreement};
}

export const FIXTURE_HTML = String.raw`<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:,">
<title>A3 browser-owned relay handshake</title>
<body>
  <main><h1>A3 browser-owned relay handshake</h1><output id="status">fixture loaded</output></main>
  <script type="module">
    import {LockstepPeer, lockstepConstants} from './net_lockstep_core.mjs';
    import {createRoomRelayPeerEndpoint} from './net_lockstep_websocket_relay.mjs';
    ${validateBrowserStartOptions.toString()}
    const EXPECTED_BROWSER_AGREEMENT_JSON = ${JSON.stringify(EXPECTED_AGREEMENT_JSON)};
    if (typeof process !== 'undefined' || typeof Buffer !== 'undefined')
      throw Error('browser portable modules exposed a Node-only global');

    const probe = {peer: null, endpoint: null, intentionalClose: false, frameBatches: 0,
      framesEmitted: 0, nativeFramesExecuted: 0,
      inputCalls: 0, checksumCalls: 0, sentTypes: [], receivedTypes: [], terminals: [],
      endpointErrors: [], disconnects: [], unexpectedTerminalBeforeClose: false};
    const packetType = text => {
      try { return JSON.parse(text)?.type ?? null; }
      catch { return null; }
    };
    function assertNeutralBatch(firstTick, bytes, expectedFrames) {
      if (firstTick !== 0 || !Number.isSafeInteger(expectedFrames) || expectedFrames < 1 ||
          bytes?.byteLength !== expectedFrames * 44)
        throw Error('Unexpected neutral prefix boundary: ' + firstTick + '/' + bytes?.byteLength + '/' + expectedFrames);
      if (lockstepConstants.neutralPad !== '0000000000000000000000' ||
          lockstepConstants.noControllerPad !== '00000000000000000000ff')
        throw Error('Portable core PAD constants differ from the frozen neutral-prefix contract');
      const view = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      for (let frame = 0; frame < expectedFrames; ++frame) {
        const base = frame * 44;
        for (const padOffset of [0, 11]) {
          for (let byte = 0; byte < 11; ++byte)
            if (view[base + padOffset + byte] !== 0)
              throw Error('Unexpected neutral PAD byte at ' + frame + ':' + (padOffset + byte));
        }
        for (const padOffset of [22, 33]) {
          for (let byte = 0; byte < 11; ++byte) {
            const expected = byte === 10 ? 0xff : 0;
            if (view[base + padOffset + byte] !== expected)
              throw Error('Unexpected no-controller PAD byte at ' + frame + ':' + (padOffset + byte));
          }
        }
      }
    }
    function snapshot() {
      const peer = probe.peer;
      return {
        role: 'alpha', ready: Boolean(peer?.ready), localHello: peer?.localHello,
        remoteHello: peer?.remoteHello, agreementHash: peer?.agreementHash ?? null,
        peerSummary: peer?.summary(), terminal: peer?.terminal ?? null,
        sentTypes: [...probe.sentTypes], receivedTypes: [...probe.receivedTypes],
        frameBatches: probe.frameBatches, framesEmitted: probe.framesEmitted,
        nativeFramesExecuted: probe.nativeFramesExecuted, inputCalls: probe.inputCalls,
        checksumCalls: probe.checksumCalls, terminals: [...probe.terminals],
        unexpectedTerminalBeforeClose: probe.unexpectedTerminalBeforeClose,
        endpointErrors: [...probe.endpointErrors], disconnects: [...probe.disconnects],
        endpointErrorsFromApi: probe.endpoint?.errors ?? [],
        endpointClosed: Boolean(probe.endpoint?.closed),
        nodeGlobalsAbsent: typeof process === 'undefined' && typeof Buffer === 'undefined',
      };
    }
    window.beginBrowserAlpha = async options => {
      const {expectedFrames, agreement} = validateBrowserStartOptions(
        options, lockstepConstants.delay, EXPECTED_BROWSER_AGREEMENT_JSON);
      const {relayBase, roomId, timeoutMs} = options;
      if (probe.peer || probe.endpoint) throw Error('browser alpha probe already started');
      let markReady;
      const protocolReady = new Promise(resolve => { markReady = resolve; });
      const peer = new LockstepPeer({role: 'alpha', sourceTicks: expectedFrames, inputTicks: 0,
        pushFrame: async (firstTick, bytes) => {
          if (probe.frameBatches !== 0) throw Error('Unexpected second source-frame batch at callback entry');
          assertNeutralBatch(firstTick, bytes, expectedFrames);
          ++probe.frameBatches;
          probe.framesEmitted += bytes.byteLength / 44;
        },
        onReady: async () => { markReady(); },
        onTerminal: async terminal => {
          if (!probe.intentionalClose) probe.unexpectedTerminalBeforeClose = true;
          probe.terminals.push(terminal);
        }});
      probe.peer = peer;
      for (const name of ['addLocalInput', 'addLocalInputs']) {
        const method = peer[name].bind(peer);
        peer[name] = (...args) => { ++probe.inputCalls; return method(...args); };
      }
      const checksum = peer.addChecksum.bind(peer);
      peer.addChecksum = (...args) => { ++probe.checksumCalls; return checksum(...args); };
      const endpoint = createRoomRelayPeerEndpoint({url: relayBase, roomId, role: 'alpha', timeoutMs,
        onMessage: async text => {
          probe.receivedTypes.push(packetType(text));
          await peer.receive(text);
        },
        onDisconnect: async (role, reason) => {
          probe.disconnects.push({role, reason});
          await peer.disconnect(reason);
        },
        onEndpointError: async (role, error) => {
          probe.endpointErrors.push({role, message: String(error?.stack || error?.message || error)});
        }});
      probe.endpoint = endpoint;
      peer.attach(async text => {
        probe.sentTypes.push(packetType(text));
        await endpoint.send(text);
      });
      // This call starts WebCrypto agreement preparation synchronously, before
      // waiting for READY; the installed receiver can retain an early hello.
      const start = peer.start(agreement);
      start.catch(() => {});
      let timer;
      try {
        await Promise.race([Promise.all([endpoint.ready, start, protocolReady]), new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error('browser alpha peer identity timed out')), timeoutMs);
        })]);
      } finally { clearTimeout(timer); }
      await endpoint.drainInbound(timeoutMs);
      document.querySelector('#status').textContent = 'alpha READY; one hello received';
      return snapshot();
    };
    window.armBrowserAlphaClose = () => {
      probe.intentionalClose = true;
      return {intentionalClose: true, endpointStarted: Boolean(probe.endpoint)};
    };
    window.closeBrowserAlpha = async () => {
      if (!probe.endpoint) return snapshot();
      probe.intentionalClose = true;
      await probe.endpoint.close();
      await probe.endpoint.drainInbound();
      return snapshot();
    };
    window.browserAlphaSnapshot = snapshot;
    document.querySelector('#status').textContent = 'portable modules loaded';
  </script>
</body>
</html>
`;

const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');
function packetType(text) {
  try { return JSON.parse(text)?.type ?? null; }
  catch { return null; }
}
export function assertNeutralBatch(firstTick, bytes, expectedFrames) {
  if (firstTick !== 0 || !Number.isSafeInteger(expectedFrames) || expectedFrames < 1 ||
      bytes?.byteLength !== expectedFrames * 44)
    throw Error(`Unexpected neutral prefix boundary: firstTick=${firstTick}, bytes=${bytes?.byteLength}, frames=${expectedFrames}`);
  if (lockstepConstants.neutralPad !== EXPECTED_NEUTRAL_PAD_HEX ||
      lockstepConstants.noControllerPad !== EXPECTED_NO_CONTROLLER_PAD_HEX)
    throw Error('Node facade PAD constants differ from the frozen neutral-prefix contract');
  const view = new Uint8Array(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let frame = 0; frame < expectedFrames; ++frame) {
    const base = frame * 44;
    for (const padOffset of [0, 11]) {
      for (let byte = 0; byte < 11; ++byte)
        if (view[base + padOffset + byte] !== 0)
          throw Error(`Neutral PAD byte differs at frame ${frame}, offset ${padOffset + byte}`);
    }
    for (const padOffset of [22, 33]) {
      for (let byte = 0; byte < 11; ++byte) {
        const expected = byte === 10 ? 0xff : 0;
        if (view[base + padOffset + byte] !== expected)
          throw Error(`No-controller PAD byte differs at frame ${frame}, offset ${padOffset + byte}`);
      }
    }
  }
}
const bounded = async (promise, timeoutMs, description) => {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(Error(`${description} exceeded ${timeoutMs} ms`)), timeoutMs);
    })]);
  } finally { clearTimeout(timer); }
};
const CLI_OPTIONS = {
  prepare: {type: 'boolean'}, run: {type: 'boolean'}, out: {type: 'string'},
  playwright: {type: 'string'},
};
let output = null;
let manifestPath = null;
let site = null;
let values = null;

export function validateExpectedNeutralFrames(alphaFrames, betaFrames, delay = EXPECTED_NEUTRAL_FRAMES) {
  if (delay !== 2 || alphaFrames !== delay || betaFrames !== delay)
    throw Error(`Frozen handshake probe requires exactly ${delay} source frames per role, matching the protocol delay`);
  return delay;
}

export function validateAgreement(agreement, expectedHash = EXPECTED_AGREEMENT_SHA256) {
  if (JSON.stringify(agreement) !== EXPECTED_AGREEMENT_JSON ||
      sha256(Buffer.from(EXPECTED_AGREEMENT_JSON, 'utf8')) !== EXPECTED_AGREEMENT_SHA256 ||
      expectedHash !== EXPECTED_AGREEMENT_SHA256)
    throw Error('Handshake agreement or independently frozen agreement hash differs from the probe contract');
}

export function validateServedHashInventory(served) {
  const expectedKeys = [...SITE_NAMES].sort();
  if (!served || !Array.isArray(served.files) ||
      JSON.stringify(served.files.slice().sort()) !== JSON.stringify(expectedKeys) ||
      !served.hashes || typeof served.hashes !== 'object' || Array.isArray(served.hashes) ||
      JSON.stringify(Object.keys(served.hashes).sort()) !== JSON.stringify(expectedKeys) ||
      Object.values(served.hashes).some(hash => !/^[0-9a-f]{64}$/.test(hash)))
    throw Error(`Probe manifest must bind exactly these served files and SHA-256 values: ${expectedKeys.join(', ')}`);
}

function assertObservationCounters(snapshot, side, expectedFrames) {
  if (snapshot.frameBatches !== 1 || snapshot.framesEmitted !== expectedFrames ||
      snapshot.nativeFramesExecuted !== 0 || snapshot.inputCalls !== 0 || snapshot.checksumCalls !== 0 ||
      snapshot.peerSummary?.local_input_ticks !== 0 || snapshot.peerSummary?.local_checksum_ticks !== 0 ||
      snapshot.peerSummary?.next_source_frame !== expectedFrames)
    throw Error(`${side} neutral-prefix observation or no-native-work contract failed`);
  packetTypesOnlyHello(snapshot, side);
}

export function assertReadySnapshot(snapshot, side, remoteRole, expectedFrames,
  expectedAgreementHash = EXPECTED_AGREEMENT_SHA256) {
  const localPort = side === 'alpha' ? 0 : 1;
  const remotePort = side === 'alpha' ? 1 : 0;
  if (snapshot.role !== side || snapshot.ready !== true || snapshot.peerSummary?.ready !== true)
    throw Error(`${side} protocol peer did not become READY`);
  if (snapshot.localHello?.role !== side || snapshot.remoteHello?.role !== remoteRole ||
      snapshot.localHello?.version !== 1 || snapshot.remoteHello?.version !== 1 ||
      snapshot.localHello?.local_port !== localPort || snapshot.localHello?.remote_port !== remotePort ||
      snapshot.remoteHello?.local_port !== remotePort || snapshot.remoteHello?.remote_port !== localPort ||
      JSON.stringify(snapshot.localHello?.agreement) !== EXPECTED_AGREEMENT_JSON ||
      JSON.stringify(snapshot.remoteHello?.agreement) !== EXPECTED_AGREEMENT_JSON)
    throw Error(`${side} protocol peer did not observe the exact complementary version-1 hello identities`);
  if (snapshot.agreementHash !== expectedAgreementHash ||
      snapshot.agreementHash !== snapshot.peerSummary?.start_identity_hash)
    throw Error(`${side} peer agreement hash was absent or differed from the independently frozen hash`);
  if (snapshot.terminal !== null || snapshot.terminals.length || snapshot.unexpectedTerminalBeforeClose)
    throw Error(`${side} observed a terminal before intentional close`);
  if (snapshot.endpointErrors.length || snapshot.endpointErrorsFromApi.length)
    throw Error(`${side} endpoint recorded an error before intentional close`);
  assertObservationCounters(snapshot, side, expectedFrames);
}

export function assertPreCloseObservation({browserSnapshot, nodeSnapshot, pageErrors, expectedFrames}) {
  const frozenErrors = Object.freeze([...pageErrors]);
  assertReadySnapshot(nodeSnapshot, 'beta', 'alpha', expectedFrames);
  assertReadySnapshot(browserSnapshot, 'alpha', 'beta', expectedFrames);
  if (!browserSnapshot.nodeGlobalsAbsent)
    throw Error('Browser fixture did not remain free of Node-only globals');
  if (browserSnapshot.agreementHash !== nodeSnapshot.agreementHash)
    throw Error('Browser alpha and Node beta computed different agreement hashes');
  if (frozenErrors.length)
    throw Error(`Browser fixture reported errors before intentional close: ${JSON.stringify(frozenErrors)}`);
  return frozenErrors;
}

export async function capturePreCloseObservation({screenshot, readBrowserSnapshot,
  readNodeSnapshot, pageErrors, expectedFrames}) {
  await screenshot();
  const browserSnapshot = await readBrowserSnapshot();
  const nodeSnapshot = await readNodeSnapshot();
  const frozenPageErrors = assertPreCloseObservation({browserSnapshot, nodeSnapshot,
    pageErrors, expectedFrames});
  return {browserSnapshot, nodeSnapshot, pageErrors: frozenPageErrors};
}

export function assertFinalObservation(snapshot, side, expectedFrames) {
  assertObservationCounters(snapshot, side, expectedFrames);
}

export function validateResponseRecord({url, status, headers, bodyHash}, expectedUrl, expectedHash) {
  const parsed = new URL(url);
  const target = new URL(expectedUrl);
  if (parsed.origin !== 'http://127.0.0.1:8787' || parsed.href !== target.href || status !== 200 ||
      bodyHash !== expectedHash)
    throw Error(`Served response identity or bytes differ for ${expectedUrl}`);
  for (const [name, value] of Object.entries(REQUIRED_HEADERS))
    if (headers?.[name] !== value)
      throw Error(`${expectedUrl} is missing required ${name}: ${value}`);
}

export function validateBrowserResponseUrl(url, origin) {
  const parsed = new URL(url);
  const root = new URL(origin);
  if (parsed.origin === root.origin) return 'fixture';
  if (parsed.protocol === 'ws:' && parsed.hostname === '127.0.0.1' &&
      !parsed.username && !parsed.password)
    return 'loopback-websocket';
  if (parsed.protocol === 'data:' || parsed.protocol === 'about:') return 'non-network';
  throw Error(`Browser requested a resource outside the frozen loopback origins: ${url}`);
}

export function validateBrowserProcessInfo(rows) {
  if (!Array.isArray(rows) || rows.length === 0 || rows.some(row =>
      !row || !Number.isSafeInteger(row.id) || row.id <= 0 ||
      typeof row.type !== 'string' || row.type.length === 0))
    throw Error('Chrome CDP process inventory must contain nonempty typed positive PID rows');
  if (new Set(rows.map(row => row.id)).size !== rows.length)
    throw Error('Chrome CDP process inventory contains duplicate PIDs');
  return rows.map(({id, type}) => ({id, type}));
}

export function createTerminationRequest(onRequest, emitter = process) {
  let requested = false;
  let resolveRequest;
  const promise = new Promise(resolve => { resolveRequest = resolve; });
  const handler = () => {
    if (requested) return;
    requested = true;
    onRequest();
    resolveRequest();
  };
  emitter.once('SIGTERM', handler);
  return {
    get requested() { return requested; },
    promise,
    close() { emitter.removeListener('SIGTERM', handler); },
    throwIfRequested() {
      if (requested) throw Error('Capture owner requested cooperative SIGTERM cleanup; probe is failed');
    },
    async wait(promiseToWait, timeoutMs, description) {
      let timer;
      try {
        return await Promise.race([promiseToWait, new Promise((_, reject) => {
          timer = setTimeout(() => reject(Error(`${description} exceeded ${timeoutMs} ms`)), timeoutMs);
        }), promise.then(() => { throw Error('Capture owner requested cooperative SIGTERM cleanup; probe is failed'); })]);
      } finally { clearTimeout(timer); }
    },
  };
}

export async function awaitOwnedAcquisition(acquisition, assign, termination) {
  const resource = await acquisition;
  assign(resource);
  termination.throwIfRequested();
  return resource;
}

export async function armBrowserBeforeNodeClose({verifyBeforeArm, armBrowser,
  markNodeCloseIntentional, onAcknowledged = () => {}}) {
  let verificationError = null;
  let armError = null;
  let acknowledgement = null;
  let acknowledged = false;
  try { await verifyBeforeArm(); }
  catch (error) { verificationError = error; }
  try {
    acknowledgement = await armBrowser();
    if (acknowledgement?.intentionalClose !== true)
      throw Error(`Browser did not acknowledge intentional close: ${JSON.stringify(acknowledgement)}`);
    acknowledged = true;
    markNodeCloseIntentional();
    onAcknowledged(acknowledgement);
  } catch (error) { armError = error; }
  return {acknowledged,
    acknowledgement, verificationError, armError};
}

export async function runCleanupStages(stages, errors = []) {
  for (const stage of stages) {
    try { await stage.run(); }
    catch (error) { errors.push(`${stage.name}: ${String(error?.stack || error)}`); }
  }
  return errors;
}

async function browserProfileDirectories(tempDirectory) {
  const entries = await fs.readdir(tempDirectory, {withFileTypes: true});
  return entries.filter(entry => entry.isDirectory() &&
    /^playwright_chromiumdev_profile-[A-Za-z0-9_-]+$/.test(entry.name))
    .map(entry => path.join(tempDirectory, entry.name));
}

async function waitForDirectoryEmpty(directory, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  let entries = await fs.readdir(directory);
  while (entries.length && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 25));
    entries = await fs.readdir(directory);
  }
  return entries;
}

async function fileSha256(file) {
  const hash = createHash('sha256');
  const stream = createReadStream(file);
  for await (const chunk of stream) hash.update(chunk);
  return hash.digest('hex');
}

async function directorySha256(directory) {
  const hash = createHash('sha256');
  async function add(dir, prefix = '') {
    const entries = await fs.readdir(dir, {withFileTypes: true});
    entries.sort((a, b) => a.name < b.name ? -1 : a.name > b.name ? 1 : 0);
    for (const entry of entries) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      const full = path.join(dir, entry.name);
      if (entry.isSymbolicLink()) throw Error(`Refusing a symlink in frozen Playwright package: ${relative}`);
      if (entry.isDirectory()) {
        hash.update(`directory\0${relative}\0`);
        await add(full, relative);
      } else if (entry.isFile()) {
        hash.update(`file\0${relative}\0`);
        const fileHash = await fileSha256(full);
        hash.update(fileHash);
        hash.update('\0');
      } else throw Error(`Refusing a non-regular Playwright package entry: ${relative}`);
    }
  }
  await add(directory);
  return hash.digest('hex');
}

async function chromeIdentity(browserPath) {
  const resolved = await fs.realpath(browserPath);
  const marker = `${path.sep}Contents${path.sep}MacOS${path.sep}`;
  const markerIndex = resolved.lastIndexOf(marker);
  if (process.platform !== 'darwin' || markerIndex < 0)
    throw Error('Probe requires the selected installed macOS Chrome bundle so its browser version is verifiable');
  const bundleContents = resolved.slice(0, markerIndex + `${path.sep}Contents`.length);
  const infoPlist = path.join(bundleContents, 'Info.plist');
  const version = execFileSync('/usr/bin/plutil', ['-extract', 'CFBundleShortVersionString', 'raw', '-o', '-', infoPlist],
    {encoding: 'utf8'}).trim();
  if (!/^\d+(?:\.\d+){2,3}$/.test(version)) throw Error(`Unrecognized Chrome bundle version: ${version}`);
  return {path: resolved, executableSha256: await fileSha256(resolved),
    infoPlistPath: infoPlist, infoPlistSha256: await fileSha256(infoPlist), version};
}

async function validateNodeIdentity(manifest) {
  const executable = await fs.realpath(process.execPath);
  if (process.version !== 'v24.19.0' || manifest.tools.node !== process.version ||
      manifest.tools.nodeExecutable !== executable ||
      !process.execPath.includes(`${path.sep}.deps${path.sep}emsdk${path.sep}node${path.sep}24.19.0_64bit${path.sep}bin${path.sep}node`))
    throw Error('Probe must use the frozen project SDK Node 24.19.0 executable');
}

async function ensureExternalRunDirectory() {
  const configuredRoot = process.env.MELEE_A3_PROBE_RUNS_ROOT;
  if (!configuredRoot) throw Error('Set MELEE_A3_PROBE_RUNS_ROOT to the verified external NVMe runs directory');
  const externalRoot = await fs.realpath(path.resolve(configuredRoot));
  const actual = await fs.realpath(output);
  if (!actual.startsWith(`${externalRoot}${path.sep}`))
    throw Error('Probe output must be a task-owned directory beneath the configured external NVMe runs root');
  const info = await fs.lstat(output);
  if (!info.isDirectory() || info.isSymbolicLink()) throw Error('Probe output must be a real directory');
}

async function ensureExternalBrowserTempDirectory() {
  const configuredRoot = process.env.MELEE_A3_PROBE_RUNS_ROOT;
  const configuredTemp = process.env.TMPDIR;
  if (!configuredRoot || !configuredTemp)
    throw Error('Set TMPDIR to a fresh owned directory on the external probe volume');
  const externalRoot = await fs.realpath(path.resolve(configuredRoot));
  const tempPath = path.resolve(configuredTemp);
  const tempReal = await fs.realpath(tempPath);
  if (!tempReal.startsWith(`${externalRoot}${path.sep}`))
    throw Error('Browser temporary profile directory must be beneath the configured external NVMe runs root');
  const info = await fs.lstat(tempPath);
  if (!info.isDirectory() || info.isSymbolicLink())
    throw Error('Browser temporary profile directory must be a real directory');
  if ((await fs.readdir(tempPath)).length)
    throw Error('Browser temporary profile directory must be fresh and empty');
  return tempReal;
}

async function prepare() {
  await ensureExternalRunDirectory();
  if ((await fs.readdir(output)).length)
    throw Error('Probe preparation requires a new, empty task-owned external output directory');
  for (const file of [site, manifestPath, path.join(output, 'server-spec.json')]) {
    try { await fs.lstat(file); throw Error(`Probe preparation refuses existing output ${file}`); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  const browserTools = await resolveBrowserTools(values.playwright);
  const sourceStatus = execFileSync('git', ['status', '--porcelain'], {cwd: ROOT, encoding: 'utf8'});
  if (sourceStatus.trim()) throw Error('Freeze the probe harness in a clean producer before staging fixtures');
  if (process.version !== 'v24.19.0' || !process.execPath.includes(`${path.sep}.deps${path.sep}emsdk${path.sep}node${path.sep}24.19.0_64bit${path.sep}bin${path.sep}node`))
    throw Error('Probe preparation must run under the project SDK Node 24.19.0 executable');
  const pythonExecutable = await fs.realpath(execFileSync('which', ['python3'], {encoding: 'utf8'}).trim());
  const pythonVersion = execFileSync(pythonExecutable, ['--version'], {encoding: 'utf8'}).trim();
  const prePortableFixtureBytes = await fs.readFile(PRE_PORTABLE_FIXTURE);
  const prePortableFixture = JSON.parse(prePortableFixtureBytes.toString('utf8'));
  const neutralPrefixFrames = validateExpectedNeutralFrames(
    prePortableFixture.peers?.alpha?.source_frames_before_input,
    prePortableFixture.peers?.beta?.source_frames_before_input);
  await fs.mkdir(site, {recursive: false});
  const [core, adapter] = await Promise.all([fs.readFile(CORE), fs.readFile(ADAPTER)]);
  await Promise.all([
    fs.writeFile(path.join(site, 'net_lockstep_core.mjs'), core, {flag: 'wx'}),
    fs.writeFile(path.join(site, 'net_lockstep_websocket_relay.mjs'), adapter, {flag: 'wx'}),
    fs.writeFile(path.join(site, 'probe.html'), FIXTURE_HTML, {flag: 'wx'}),
  ]);
  const producerCommit = execFileSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const producerTree = execFileSync('git', ['rev-parse', 'HEAD^{tree}'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const playwrightPackage = JSON.parse(await fs.readFile(path.join(browserTools.playwrightPath, 'package.json'), 'utf8'));
  const browserIdentity = await chromeIdentity(browserTools.browserPath);
  const nodeExecutable = await fs.realpath(process.execPath);
  const nodeExecutableSha256 = await fileSha256(nodeExecutable);
  const pythonExecutableSha256 = await fileSha256(pythonExecutable);
  const playwrightSha256 = await directorySha256(browserTools.playwrightPath);
  const hashes = {};
  for (const name of SITE_NAMES) hashes[name] = sha256(await fs.readFile(path.join(site, name)));
  const manifest = {
    schema: 'melee-web-a3-browser-owned-handshake-probe-v2',
    producer: {commit: producerCommit, tree: producerTree},
    sources: {
      portableCore: {path: 'scripts/net_lockstep_core.mjs', sha256: sha256(core)},
      nodeFacade: {path: 'scripts/net_lockstep_protocol.mjs', sha256: sha256(await fs.readFile(NODE_FACADE))},
      websocketEndpoint: {path: 'scripts/net_lockstep_websocket_relay.mjs', sha256: sha256(adapter)},
      runtimeOwner: {path: 'scripts/net_room_relay_runtime_owner.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/net_room_relay_runtime_owner.mjs')))},
      directRuntime: {path: 'scripts/net_room_relay_direct_runtime.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/net_room_relay_direct_runtime.mjs')))},
      miniflareOptions: {path: 'scripts/net_room_relay_miniflare_options.mjs', sha256: sha256(await fs.readFile(MINIFLARE_OPTIONS))},
      worker: {path: 'online/relay/worker.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'online/relay/worker.mjs')))},
      workerConfig: {path: 'online/relay/wrangler.jsonc', sha256: sha256(await fs.readFile(path.join(ROOT, 'online/relay/wrangler.jsonc')))},
      dependenciesLock: {path: 'dependencies.lock.json', sha256: sha256(await fs.readFile(path.join(ROOT, 'dependencies.lock.json')))},
      browserTools: {path: 'scripts/browser_tools.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/browser_tools.mjs')))},
      captureOwner: {path: 'scripts/capture_owner_deadline.py', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/capture_owner_deadline.py')))},
      serve: {path: 'scripts/serve.py', sha256: sha256(await fs.readFile(SERVE))},
      coordinator: {path: 'scripts/net_lockstep_browser_handshake_probe.mjs', sha256: sha256(await fs.readFile(fileURLToPath(import.meta.url)))},
      prePortableFixture: {path: 'tests/fixtures/net_lockstep_pre_portable_wire_v1.json', sha256: sha256(prePortableFixtureBytes)},
    },
    served: {directory: 'site', files: SITE_NAMES, hashes},
    tools: {node: process.version, nodeExecutable, nodeExecutableSha256,
      pythonExecutable, pythonExecutableSha256, pythonVersion,
      playwrightPath: browserTools.playwrightPath, playwrightVersion: playwrightPackage.version,
      playwrightPackageSha256: playwrightSha256, browserPath: browserIdentity.path,
      browserExecutableSha256: browserIdentity.executableSha256,
      browserInfoPlistPath: browserIdentity.infoPlistPath,
      browserInfoPlistSha256: browserIdentity.infoPlistSha256,
      browserBundleVersion: browserIdentity.version},
    probe: {origin: 'http://127.0.0.1:8787', room_roles: {browser: 'alpha', node: 'beta'},
      source_ticks: neutralPrefixFrames, neutral_prefix_frames: neutralPrefixFrames,
      neutral_pad_hex: EXPECTED_NEUTRAL_PAD_HEX, no_controller_pad_hex: EXPECTED_NO_CONTROLLER_PAD_HEX,
      agreement: EXPECTED_AGREEMENT, agreement_json_utf8: EXPECTED_AGREEMENT_JSON,
      agreement_sha256_hex: EXPECTED_AGREEMENT_SHA256,
      input_ticks: 0, peer_ready_timeout_ms: 20000,
      relay_url_is_private: true},
  };
  await fs.writeFile(manifestPath, JSON.stringify(manifest, null, 2) + '\n', {flag: 'wx'});
  const serverSpec = {
    command: [pythonExecutable, SERVE, '--directory', site, '--port', '8787'],
    ready_url: 'http://127.0.0.1:8787/probe.html', ready_timeout_ms: 15000,
  };
  await fs.writeFile(path.join(output, 'server-spec.json'), JSON.stringify(serverSpec, null, 2) + '\n', {flag: 'wx'});
  process.stdout.write(JSON.stringify({prepared: true, output, producer: manifest.producer,
    served: manifest.served, tools: manifest.tools}, null, 2) + '\n');
}

function packetTypesOnlyHello(record, side) {
  const types = [...record.sentTypes, ...record.receivedTypes];
  if (!types.length || types.some(type => type !== 'hello'))
    throw Error(`${side} sent or received a non-hello A2 packet before intentional close: ${JSON.stringify(types)}`);
  if (record.sentTypes.filter(type => type === 'hello').length !== 1 ||
      record.receivedTypes.filter(type => type === 'hello').length !== 1)
    throw Error(`${side} did not exchange exactly one hello in each direction`);
}

export function assertClosedSnapshot(snapshot, side) {
  const disconnectReason = snapshot.disconnects?.[0]?.reason;
  const allowedCloseCode = typeof disconnectReason === 'string' &&
    /^(?:1000|4001): .+$/.test(disconnectReason);
  if (snapshot.endpointClosed !== true || snapshot.disconnects.length !== 1 ||
      !allowedCloseCode ||
      snapshot.terminal?.kind !== 'disconnect' || snapshot.terminals.length !== 1 ||
      snapshot.terminals[0]?.kind !== 'disconnect' || snapshot.unexpectedTerminalBeforeClose ||
      snapshot.endpointErrors.length ||
      snapshot.endpointErrorsFromApi.length)
    throw Error(`${side} endpoint did not finish one allowed-code intentional disconnect: ${JSON.stringify({
      endpointClosed: snapshot.endpointClosed, disconnects: snapshot.disconnects,
      terminal: snapshot.terminal, errors: snapshot.endpointErrors,
      apiErrors: snapshot.endpointErrorsFromApi})}`);
}

function responseHeaders(headers) {
  return Object.fromEntries(Object.keys(REQUIRED_HEADERS).map(name => [name, headers.get(name)]));
}

async function verifyHttpServedFiles(manifest) {
  const origin = new URL(manifest.probe.origin);
  if (origin.protocol !== 'http:' || origin.hostname !== '127.0.0.1' || origin.port !== '8787' ||
      origin.username || origin.password || origin.pathname !== '/' || origin.search || origin.hash)
    throw Error('Probe origin must be the exact allowlisted loopback HTTP origin');
  const rows = [];
  for (const name of SITE_NAMES) {
    const url = new URL(name, origin).href;
    const response = await fetch(url, {redirect: 'manual', signal: AbortSignal.timeout(5000)});
    const body = new Uint8Array(await response.arrayBuffer());
    const row = {url: response.url || url, status: response.status,
      headers: responseHeaders(response.headers), bytes: body.byteLength, bodyHash: sha256(body)};
    validateResponseRecord(row, url, manifest.served.hashes[name]);
    rows.push(row);
  }
  return rows;
}

export async function captureBrowserResponse(response, origin) {
  const url = response.url();
  if (validateBrowserResponseUrl(url, origin) !== 'fixture') return null;
  const parsed = new URL(url);
  const name = parsed.pathname.replace(/^\//, '');
  if (!SITE_NAMES.includes(name)) throw Error(`Browser requested an unexpected fixture resource: ${url}`);
  const body = await response.body();
  return {url, status: response.status(), headers: responseHeaders(new Headers(await response.allHeaders())),
    bytes: body.byteLength, bodyHash: sha256(body)};
}

function verifyBrowserResponseSet(rows, manifest) {
  for (const name of SITE_NAMES) {
    const expectedUrl = new URL(name, manifest.probe.origin).href;
    const matching = rows.filter(row => row.url === expectedUrl);
    if (matching.length !== 1)
      throw Error(`Browser did not load exactly one frozen HTTP resource for ${name}: ${matching.length}`);
    validateResponseRecord(matching[0], expectedUrl, manifest.served.hashes[name]);
  }
}

async function run() {
  await ensureExternalRunDirectory();
  const browserTempDirectory = await ensureExternalBrowserTempDirectory();
  for (const name of ['result.json', 'browser-alpha-ready.png', 'worker-evidence']) {
    try { await fs.lstat(path.join(output, name)); throw Error(`Probe run refuses prior output ${name}`); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  if ((await fs.readdir(output)).some(name => !['manifest.json', 'server-spec.json', 'site', 'capture-owner'].includes(name)))
    throw Error('Probe run found unexpected data beside the frozen fixture and capture-owner outputs');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (manifest.schema !== 'melee-web-a3-browser-owned-handshake-probe-v2') throw Error('Unknown probe manifest schema');
  const servedFiles = (await fs.readdir(site)).sort();
  if (JSON.stringify(servedFiles) !== JSON.stringify([...SITE_NAMES].sort()))
    throw Error(`Served fixture directory must contain exactly ${SITE_NAMES.join(', ')}`);
  validateServedHashInventory(manifest.served);
  const serverSpec = JSON.parse(await fs.readFile(path.join(output, 'server-spec.json'), 'utf8'));
  if (JSON.stringify(serverSpec.command) !== JSON.stringify([manifest.tools.pythonExecutable, SERVE, '--directory', site, '--port', '8787']) ||
      serverSpec.ready_url !== `${manifest.probe.origin}/probe.html`)
    throw Error('Probe server spec must serve only the frozen fixture directory at the allowlisted loopback origin');
  for (const name of SITE_NAMES) {
    const actual = sha256(await fs.readFile(path.join(site, name)));
    if (actual !== manifest.served.hashes[name]) throw Error(`Served fixture hash changed for ${name}`);
  }
  const currentCommit = execFileSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const currentTree = execFileSync('git', ['rev-parse', 'HEAD^{tree}'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const currentStatus = execFileSync('git', ['status', '--porcelain'], {cwd: ROOT, encoding: 'utf8'}).trim();
  if (currentCommit !== manifest.producer.commit || currentTree !== manifest.producer.tree || currentStatus)
    throw Error('Probe producer identity or clean-worktree state differs from the reviewed manifest');
  const sourcePaths = {core: CORE, nodeFacade: NODE_FACADE, adapter: ADAPTER,
    runtimeOwner: path.join(ROOT, 'scripts/net_room_relay_runtime_owner.mjs'),
    directRuntime: path.join(ROOT, 'scripts/net_room_relay_direct_runtime.mjs'),
    miniflareOptions: MINIFLARE_OPTIONS,
    worker: path.join(ROOT, 'online/relay/worker.mjs'),
    workerConfig: path.join(ROOT, 'online/relay/wrangler.jsonc'),
    dependenciesLock: path.join(ROOT, 'dependencies.lock.json'),
    browserTools: path.join(ROOT, 'scripts/browser_tools.mjs'),
    captureOwner: path.join(ROOT, 'scripts/capture_owner_deadline.py'), serve: SERVE,
    prePortableFixture: PRE_PORTABLE_FIXTURE,
    coordinator: fileURLToPath(import.meta.url)};
  for (const [key, file] of Object.entries(sourcePaths)) {
    const record = Object.values(manifest.sources).find(row => row.path === path.relative(ROOT, file));
    if (!record || sha256(await fs.readFile(file)) !== record.sha256)
      throw Error(`Probe source identity changed for ${key}; regenerate packet and request review`);
  }
  const prePortableFixture = JSON.parse(await fs.readFile(PRE_PORTABLE_FIXTURE, 'utf8'));
  const expectedNeutralFrames = validateExpectedNeutralFrames(
    prePortableFixture.peers?.alpha?.source_frames_before_input,
    prePortableFixture.peers?.beta?.source_frames_before_input);
  validateAgreement(manifest.probe.agreement, manifest.probe.agreement_sha256_hex);
  if (manifest.probe.agreement_json_utf8 !== EXPECTED_AGREEMENT_JSON ||
      manifest.probe.neutral_prefix_frames !== expectedNeutralFrames ||
      manifest.probe.source_ticks !== expectedNeutralFrames ||
      manifest.probe.neutral_pad_hex !== EXPECTED_NEUTRAL_PAD_HEX ||
      manifest.probe.no_controller_pad_hex !== EXPECTED_NO_CONTROLLER_PAD_HEX ||
      manifest.probe.input_ticks !== 0)
    throw Error('Frozen neutral-prefix contract differs from the pre-portable fixture or exact PAD bytes');
  await validateNodeIdentity(manifest);
  if (await fileSha256(process.execPath) !== manifest.tools.nodeExecutableSha256)
    throw Error('Node executable bytes differ from the reviewed manifest');
  if (await fs.realpath(manifest.tools.pythonExecutable) !== manifest.tools.pythonExecutable ||
      await fileSha256(manifest.tools.pythonExecutable) !== manifest.tools.pythonExecutableSha256 ||
      execFileSync(manifest.tools.pythonExecutable, ['--version'], {encoding: 'utf8'}).trim() !== manifest.tools.pythonVersion)
    throw Error('Python interpreter version differs from the reviewed manifest');
  if (process.env.MELEE_BROWSER_PATH &&
      await fs.realpath(path.resolve(process.env.MELEE_BROWSER_PATH)) !== manifest.tools.browserPath)
    throw Error('Capture owner selected a different browser executable than the reviewed manifest');
  const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(manifest.tools.playwrightPath);
  if (browserPath !== manifest.tools.browserPath || playwrightPath !== manifest.tools.playwrightPath)
    throw Error('Installed browser or Playwright identity differs from the reviewed manifest');
  const playwrightPackage = JSON.parse(await fs.readFile(path.join(playwrightPath, 'package.json'), 'utf8'));
  if (playwrightPackage.version !== manifest.tools.playwrightVersion ||
      await directorySha256(playwrightPath) !== manifest.tools.playwrightPackageSha256)
    throw Error('Playwright package version differs from the reviewed manifest');
  const currentBrowserIdentity = await chromeIdentity(browserPath);
  if (currentBrowserIdentity.executableSha256 !== manifest.tools.browserExecutableSha256 ||
      currentBrowserIdentity.infoPlistPath !== manifest.tools.browserInfoPlistPath ||
      currentBrowserIdentity.infoPlistSha256 !== manifest.tools.browserInfoPlistSha256 ||
      currentBrowserIdentity.version !== manifest.tools.browserBundleVersion)
    throw Error('Installed Chrome executable or bundle version identity differs from the reviewed manifest');
  const httpResponses = await verifyHttpServedFiles(manifest);

  const report = {schema: 'melee-web-a3-browser-owned-handshake-result-v2', outcome: 'fail',
    producer: manifest.producer, sourceHashes: Object.fromEntries(Object.entries(manifest.sources)
      .map(([key, value]) => [key, value.sha256])), served: manifest.served,
    runtime: null, browser_process_info: [], httpResponses, browser: {path: browserPath, playwrightPath,
      tempDirectory: browserTempDirectory,
      executableSha256: currentBrowserIdentity.executableSha256,
      infoPlistSha256: currentBrowserIdentity.infoPlistSha256,
      bundleVersion: currentBrowserIdentity.version, pageErrors: [], loadedResponses: []},
    roles: {browser: 'alpha', node: 'beta'}, contract: manifest.probe,
    events: [], firstError: null, cleanup: {browserContextClosed: false, browserClosed: false,
      browserCdpDetached: false, browserProfileAbsent: false, globalTempDirectoryEmpty: false,
      endpointsClosed: false, workerDisposed: false}};
  const recordEvent = event => report.events.push({event, monotonicNs: process.hrtime.bigint().toString(), at: new Date().toISOString()});
  const termination = createTerminationRequest(() => {
    report.terminationRequested = true;
    recordEvent('capture-owner-sigterm-requested');
  });
  let runtime = null, browser = null, context = null, page = null, browserCdp = null;
  let endpoint = null, peer = null, startPromise = null, intentionalClose = false;
  let browserStartInvoked = false;
  let probePassed = false;
  let cleanupErrors = [];
  let pageErrors = [];
  let browserResponseTasks = [];
  let browserResponseErrors = [];
  let browserResponseRows = [];
  let nodeCounters = {frameBatches: 0, framesEmitted: 0, nativeFramesExecuted: 0,
    inputCalls: 0, checksumCalls: 0,
    sentTypes: [], receivedTypes: [], terminals: [], endpointErrors: [], disconnects: [],
    unexpectedTerminalBeforeClose: false};
  const snapshotNode = () => ({
    role: 'beta', ready: peer?.ready, localHello: peer?.localHello, remoteHello: peer?.remoteHello,
    agreementHash: peer?.agreementHash, peerSummary: peer?.summary(), terminal: peer?.terminal ?? null,
    sentTypes: [...nodeCounters.sentTypes], receivedTypes: [...nodeCounters.receivedTypes],
    frameBatches: nodeCounters.frameBatches, framesEmitted: nodeCounters.framesEmitted,
    nativeFramesExecuted: nodeCounters.nativeFramesExecuted, inputCalls: nodeCounters.inputCalls,
    checksumCalls: nodeCounters.checksumCalls, terminals: [...nodeCounters.terminals],
    unexpectedTerminalBeforeClose: nodeCounters.unexpectedTerminalBeforeClose,
    endpointErrors: [...nodeCounters.endpointErrors], disconnects: [...nodeCounters.disconnects],
    endpointErrorsFromApi: endpoint?.errors || [],
  });
  try {
    const {page: fixtureUrl, origin} = {page: 'probe.html', origin: manifest.probe.origin};
    await awaitOwnedAcquisition(chromium.launch(browserLaunchOptions(launchOptions,
      {headed: false, audible: false, timeout: 15000})), value => { browser = value; }, termination);
    const activeProfiles = await browserProfileDirectories(browserTempDirectory);
    if (activeProfiles.length !== 1)
      throw Error(`Expected exactly one owned Playwright Chrome profile, found ${activeProfiles.length}`);
    report.browser.profileDirectory = activeProfiles[0];
    const actualBrowserVersion = browser.version();
    report.browser.actualVersion = actualBrowserVersion;
    const actualBrowserVersionNumber = actualBrowserVersion.match(/\d+(?:\.\d+){2,3}/)?.[0];
    if (actualBrowserVersionNumber !== manifest.tools.browserBundleVersion)
      throw Error(`Launched Chrome version ${actualBrowserVersion} differs from frozen bundle ${manifest.tools.browserBundleVersion}`);
    await awaitOwnedAcquisition(browser.newContext({viewport: {width: 1100, height: 700}}),
      value => { context = value; }, termination);
    await awaitOwnedAcquisition(context.newPage(), value => { page = value; }, termination);
    page.on('pageerror', error => pageErrors.push(String(error?.stack || error)));
    page.on('console', message => { if (message.type() === 'error') pageErrors.push(message.text()); });
    page.on('response', response => {
      const task = captureBrowserResponse(response, origin).then(row => {
        if (row) browserResponseRows.push(row);
      }).catch(error => { browserResponseErrors.push(String(error?.stack || error)); });
      browserResponseTasks.push(task);
    });
    await termination.wait(page.goto(`${origin}/${fixtureUrl}`, {waitUntil: 'load', timeout: 15000}),
      15000, 'browser fixture load');
    termination.throwIfRequested();
    await termination.wait(Promise.all(browserResponseTasks), 3000,
      'initial browser resource response capture');
    if (browserResponseErrors.length)
      throw Error(`Browser fixture response capture failed: ${JSON.stringify(browserResponseErrors)}`);
    verifyBrowserResponseSet(browserResponseRows, manifest);
    report.browser.loadedResponses = [...browserResponseRows];

    await awaitOwnedAcquisition(browser.newBrowserCDPSession(),
      value => { browserCdp = value; }, termination);
    const browserProcessResult = await termination.wait(
      browserCdp.send('SystemInfo.getProcessInfo'), 3000, 'Chrome CDP process inventory');
    report.browser_process_info = validateBrowserProcessInfo(browserProcessResult?.processInfo);

    recordEvent('worker-start-begin');
    await awaitOwnedAcquisition(startRoomRelayRuntime({evidenceDir: path.join(output, 'worker-evidence'),
      producer: manifest.producer, diagnostic: message => recordEvent(`worker:${message}`)}),
    value => {
      runtime = value;
      report.runtime = {base: runtime.base, port: runtime.port, ownerProcess: runtime.ownerProcess,
        identity: runtime.runtimeIdentity, hashes: runtime.hashes};
    }, termination);
    recordEvent('worker-started');

    const roomId = createRoomId();
    const agreement = {...EXPECTED_AGREEMENT};
    validateAgreement(agreement);
    let markNodeReady;
    const nodeProtocolReady = new Promise(resolve => { markNodeReady = resolve; });
    const onTerminal = async terminal => {
      if (!intentionalClose) nodeCounters.unexpectedTerminalBeforeClose = true;
      nodeCounters.terminals.push(terminal);
    };
    peer = new LockstepPeer({role: 'beta', sourceTicks: expectedNeutralFrames, inputTicks: 0,
      pushFrame: async (firstTick, bytes) => {
        if (nodeCounters.frameBatches !== 0) throw Error('Unexpected second source-frame batch at callback entry');
        assertNeutralBatch(firstTick, bytes, expectedNeutralFrames);
        ++nodeCounters.frameBatches;
        nodeCounters.framesEmitted += bytes.byteLength / 44;
      }, onTerminal, onReady: async () => { markNodeReady(); }});
    for (const name of ['addLocalInput', 'addLocalInputs']) {
      const method = peer[name].bind(peer);
      peer[name] = (...args) => { ++nodeCounters.inputCalls; return method(...args); };
    }
    const checksum = peer.addChecksum.bind(peer);
    peer.addChecksum = (...args) => { ++nodeCounters.checksumCalls; return checksum(...args); };
    endpoint = createRoomRelayPeerEndpoint({url: runtime.base.replace(/^http:/, 'ws:'), roomId,
      role: 'beta', timeoutMs: 20000,
      onMessage: async text => { nodeCounters.receivedTypes.push(packetType(text)); await peer.receive(text); },
      onDisconnect: async (role, reason) => {
        nodeCounters.disconnects.push({role, reason});
        await peer.disconnect(reason);
      },
      onEndpointError: async (role, error) => {
        nodeCounters.endpointErrors.push({role, message: String(error?.stack || error?.message || error)});
      }});
    peer.attach(async text => { nodeCounters.sentTypes.push(packetType(text)); await endpoint.send(text); });
    // Start agreement preparation immediately after installing the receiver;
    // do not await READY or the browser connection before this call.
    startPromise = peer.start(agreement);
    startPromise.catch(() => {});
    recordEvent('node-beta-started-before-ready');
    termination.throwIfRequested();

    const browserStart = page.evaluate(({relayBase, roomId, agreement, timeoutMs, expectedFrames}) =>
      window.beginBrowserAlpha({relayBase, roomId, agreement, timeoutMs, expectedFrames}),
      {relayBase: runtime.base.replace(/^http:/, 'ws:'), roomId, agreement, timeoutMs: 20000,
        expectedFrames: expectedNeutralFrames});
    browserStartInvoked = true;
    const [nodeReady, browserReady] = await termination.wait(Promise.all([
      Promise.all([endpoint.ready, startPromise, nodeProtocolReady]).then(() => true), browserStart,
    ]), 25000, 'both browser-owned handshake peers becoming READY');
    termination.throwIfRequested();
    await endpoint.drainInbound(5000);
    report.browserHandshake = browserReady;
    report.nodeHandshake = snapshotNode();
    if (!nodeReady) throw Error('Node beta did not become ready');
    assertReadySnapshot(report.nodeHandshake, 'beta', 'alpha', expectedNeutralFrames);
    assertReadySnapshot(report.browserHandshake, 'alpha', 'beta', expectedNeutralFrames);
    const screenshotPath = path.join(output, 'browser-alpha-ready.png');
    const screenshot = () => termination.wait(
      page.screenshot({path: screenshotPath, fullPage: true, timeout: 5000}),
      5000, 'browser readiness screenshot');
    const beforeClose = await capturePreCloseObservation({
      screenshot,
      readBrowserSnapshot: () => termination.wait(
        page.evaluate(() => window.browserAlphaSnapshot()), 3000, 'browser alpha post-screenshot snapshot'),
      readNodeSnapshot: async () => snapshotNode(),
      pageErrors, expectedFrames: expectedNeutralFrames});
    report.browserScreenshot = {path: path.basename(screenshotPath), sha256: sha256(await fs.readFile(screenshotPath))};
    report.browser.pageErrorsBeforeClose = [...beforeClose.pageErrors];
    report.browser.loadedResponses = [...browserResponseRows];
    verifyBrowserResponseSet(browserResponseRows, manifest);
    if (browserResponseErrors.length)
      throw Error(`Browser fixture response capture failed: ${JSON.stringify(browserResponseErrors)}`);
    report.browserHandshake = beforeClose.browserSnapshot;
    report.nodeHandshake = beforeClose.nodeSnapshot;
    probePassed = true;
    recordEvent('both-peers-ready-and-asserted');
  } catch (error) {
    report.firstError = {name: error?.name || 'Error', message: String(error?.stack || error?.message || error)};
    recordEvent('probe-error');
  } finally {
    if (page && browserStartInvoked) {
      const closeArm = await armBrowserBeforeNodeClose({
        verifyBeforeArm: async () => {
          const browserBeforeClose = await bounded(page.evaluate(() => window.browserAlphaSnapshot()), 3000,
            'browser alpha pre-close snapshot');
          const nodeBeforeClose = snapshotNode();
          report.browserBeforeClose = browserBeforeClose;
          report.nodeBeforeClose = nodeBeforeClose;
          report.browser.pageErrorsBeforeClose = assertPreCloseObservation({
            browserSnapshot: browserBeforeClose, nodeSnapshot: nodeBeforeClose,
            pageErrors, expectedFrames: expectedNeutralFrames});
        },
        armBrowser: () => bounded(page.evaluate(() => window.armBrowserAlphaClose()), 3000,
          'browser alpha intentional-close arm acknowledgment'),
        markNodeCloseIntentional: () => { intentionalClose = true; },
        onAcknowledged: arm => {
          report.browserCloseArm = arm;
          report.browser.pageErrorsAfterCloseArm = [...pageErrors];
          if (pageErrors.length) {
            probePassed = false;
            cleanupErrors.push(`browser close arm: page errors before close completion: ${JSON.stringify(pageErrors)}`);
          }
          recordEvent('browser-alpha-intentional-close-armed');
        },
      });
      if (closeArm.verificationError) {
        probePassed = false;
        report.preCloseCheckError = String(closeArm.verificationError?.stack || closeArm.verificationError);
        if (!report.firstError)
          report.firstError = {name: closeArm.verificationError?.name || 'Error', message: report.preCloseCheckError};
        recordEvent('pre-close-check-failed');
      }
      if (closeArm.armError) cleanupErrors.push(`browser close arm: ${String(closeArm.armError?.stack || closeArm.armError)}`);
    }

    const cleanupStages = [];
    if (endpoint || page) cleanupStages.push({name: 'endpoint close and inbound drain', run: async () => {
      const closures = [];
      if (endpoint) closures.push((async () => {
        await endpoint.close();
        await endpoint.drainInbound();
      })());
      if (page && browserStartInvoked) closures.push(page.evaluate(() => window.closeBrowserAlpha()).then(value => {
        report.browserAfterClose = value;
      }));
      const settled = await bounded(Promise.allSettled(closures), 25000,
        'intentional endpoint close and inbound drain');
      const rejected = settled.filter(row => row.status === 'rejected').map(row => row.reason);
      if (rejected.length) throw new AggregateError(rejected, 'One or more owned endpoint closes failed');
      if (report.browserAfterClose) {
        assertFinalObservation(report.browserAfterClose, 'alpha', expectedNeutralFrames);
        assertClosedSnapshot(report.browserAfterClose, 'browser alpha');
      }
      if (endpoint && peer) {
        const nodeAfterClose = {
          ...snapshotNode(), endpointClosed: endpoint.closed,
          disconnects: [...nodeCounters.disconnects],
        };
        report.nodeAfterClose = nodeAfterClose;
        assertFinalObservation(nodeAfterClose, 'beta', expectedNeutralFrames);
        assertClosedSnapshot(nodeAfterClose, 'Node beta');
      }
      report.cleanup.endpointsClosed = true;
      recordEvent('endpoints-closed-and-drained');
    }});
    if (page) cleanupStages.push({name: 'browser page close', run: async () => {
      await bounded(page.close(), 3000, 'browser page close');
    }});
    cleanupStages.push({name: 'browser response capture drain', run: async () => {
      await bounded(Promise.allSettled(browserResponseTasks), 3000, 'browser response capture drain');
    }});
    if (browserCdp) cleanupStages.push({name: 'browser CDP detach', run: async () => {
      await bounded(browserCdp.detach(), 1000, 'browser CDP detach');
      browserCdp = null;
      report.cleanup.browserCdpDetached = true;
    }});
    if (context) cleanupStages.push({name: 'browser context close', run: async () => {
      await bounded(context.close(), 5000, 'browser context close');
      report.cleanup.browserContextClosed = true;
    }});
    if (browser) cleanupStages.push({name: 'browser close', run: async () => {
      await bounded(browser.close(), 5000, 'browser close');
      report.cleanup.browserClosed = true;
    }});
    cleanupStages.push({name: 'browser profile cleanup', run: async () => {
      const remainingProfiles = await browserProfileDirectories(browserTempDirectory);
      if (remainingProfiles.length)
        throw Error(`Playwright Chrome profiles remain after browser close: ${remainingProfiles.join(', ')}`);
      report.cleanup.browserProfileAbsent = true;
    }});
    if (runtime) cleanupStages.push({name: 'Worker disposal', run: async () => {
      const disposal = await runtime.close(Boolean(probePassed && cleanupErrors.length === 0 && !termination.requested));
      report.runtime.disposal = disposal;
      report.cleanup.workerDisposed = disposal?.ready === true && disposal?.disposed === true &&
        disposal?.exit?.code === 0 && !disposal?.exit?.signal && disposal?.processClose !== null &&
        disposal?.processClose?.code === 0 && !disposal?.processClose?.signal &&
        disposal?.groupAlive === false && disposal?.fallbackUsed === false &&
        !(disposal?.runtimeErrors?.length) && !(disposal?.structuredErrors?.length) &&
        disposal?.stderrErrors === false && !(disposal?.cleanupEvents || []).some(row => row.error);
      if (!report.cleanup.workerDisposed)
        throw Error('Worker disposal returned without complete clean-runtime evidence');
    }});
    cleanupStages.push({name: 'external temporary directory cleanup', run: async () => {
      const remaining = await waitForDirectoryEmpty(browserTempDirectory, 2000);
      if (remaining.length)
        throw Error(`External TMPDIR retained entries after browser and Worker disposal: ${remaining.join(', ')}`);
      report.cleanup.globalTempDirectoryEmpty = true;
    }});
    await runCleanupStages(cleanupStages, cleanupErrors);
    report.browser.pageErrors = [...pageErrors];
    if (pageErrors.length) probePassed = false;
    report.browser.loadedResponses = [...browserResponseRows];
    if (browserResponseErrors.length) {
      probePassed = false;
      report.browser.responseErrors = [...browserResponseErrors];
    }
  }

  report.cleanup.errors = cleanupErrors;
  report.outcome = probePassed && !termination.requested && report.cleanup.endpointsClosed && report.cleanup.browserClosed &&
    report.cleanup.browserContextClosed && report.cleanup.browserCdpDetached &&
    report.cleanup.browserProfileAbsent && report.cleanup.globalTempDirectoryEmpty &&
    report.cleanup.workerDisposed && cleanupErrors.length === 0
    ? 'pass' : 'fail';
  report.finishedAt = new Date().toISOString();
  const reportPath = path.join(output, 'result.json');
  await fs.writeFile(reportPath, JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
  if (termination.requested && report.outcome !== 'fail') {
    report.outcome = 'fail';
    report.finishedAt = new Date().toISOString();
    const temporary = `${reportPath}.tmp`;
    await fs.writeFile(temporary, JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
    await fs.rename(temporary, reportPath);
  }
  termination.close();
  process.stdout.write(JSON.stringify({outcome: report.outcome, result: reportPath,
    agreementHash: report.nodeHandshake?.agreementHash || null, cleanup: report.cleanup,
    firstError: report.firstError}, null, 2) + '\n');
  if (report.outcome !== 'pass') process.exitCode = 1;
}

async function main(argv = process.argv.slice(2)) {
  values = parseArgs({args: argv, options: CLI_OPTIONS}).values;
  if (values.prepare === values.run || !values.out)
    throw Error('Choose exactly one of --prepare or --run, and provide --out EXTERNAL_RUN_DIRECTORY');
  output = path.resolve(values.out);
  manifestPath = path.join(output, 'manifest.json');
  site = path.join(output, 'site');
  if (values.prepare) await prepare();
  else await run();
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  await main();
