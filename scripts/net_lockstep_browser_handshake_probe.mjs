#!/usr/bin/env node
/*
 * Issue 201: one browser-owned A2 peer and one Node-owned peer through the
 * actual local room Worker. --prepare stages only the fixture and exact
 * portable modules in external scratch; --run is reserved for a reviewed
 * capture packet and is not an alternative to the full gameplay harness.
 */
import {createHash} from 'node:crypto';
import {execFileSync} from 'node:child_process';
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
const SERVE = path.join(ROOT, 'scripts/serve.py');
const PRE_PORTABLE_FIXTURE = path.join(ROOT, 'tests/fixtures/net_lockstep_pre_portable_wire_v1.json');
const SITE_NAMES = ['net_lockstep_core.mjs', 'net_lockstep_websocket_relay.mjs', 'probe.html'];
const EXPECTED_NEUTRAL_PAD_HEX = '0000000000000000000000';
const EXPECTED_NO_CONTROLLER_PAD_HEX = '00000000000000000000ff';
const FIXTURE_HTML = String.raw`<!doctype html>
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
    window.beginBrowserAlpha = async ({relayBase, roomId, agreement, timeoutMs, expectedFrames}) => {
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
function assertNeutralBatch(firstTick, bytes, expectedFrames) {
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
const {values} = parseArgs({options: {
  prepare: {type: 'boolean'}, run: {type: 'boolean'}, out: {type: 'string'},
  playwright: {type: 'string'},
}});
if (values.prepare === values.run || !values.out)
  throw Error('Choose exactly one of --prepare or --run, and provide --out EXTERNAL_RUN_DIRECTORY');
const output = path.resolve(values.out);
const manifestPath = path.join(output, 'manifest.json');
const site = path.join(output, 'site');

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
  const pythonExecutable = execFileSync('which', ['python3'], {encoding: 'utf8'}).trim();
  const pythonVersion = execFileSync(pythonExecutable, ['--version'], {encoding: 'utf8'}).trim();
  const prePortableFixtureBytes = await fs.readFile(PRE_PORTABLE_FIXTURE);
  const prePortableFixture = JSON.parse(prePortableFixtureBytes.toString('utf8'));
  const neutralPrefixFrames = prePortableFixture.peers?.alpha?.source_frames_before_input;
  if (!Number.isSafeInteger(neutralPrefixFrames) || neutralPrefixFrames < 1 ||
      prePortableFixture.peers?.beta?.source_frames_before_input !== neutralPrefixFrames)
    throw Error('Pre-portable golden fixture does not declare one matching positive source-prefix count per role');
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
  const hashes = {};
  for (const name of SITE_NAMES) hashes[name] = sha256(await fs.readFile(path.join(site, name)));
  const manifest = {
    schema: 'melee-web-a3-browser-owned-handshake-probe-v1',
    producer: {commit: producerCommit, tree: producerTree},
    sources: {
      portableCore: {path: 'scripts/net_lockstep_core.mjs', sha256: sha256(core)},
      nodeFacade: {path: 'scripts/net_lockstep_protocol.mjs', sha256: sha256(await fs.readFile(NODE_FACADE))},
      websocketEndpoint: {path: 'scripts/net_lockstep_websocket_relay.mjs', sha256: sha256(adapter)},
      runtimeOwner: {path: 'scripts/net_room_relay_runtime_owner.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/net_room_relay_runtime_owner.mjs')))},
      directRuntime: {path: 'scripts/net_room_relay_direct_runtime.mjs', sha256: sha256(await fs.readFile(path.join(ROOT, 'scripts/net_room_relay_direct_runtime.mjs')))},
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
    tools: {node: process.version, nodeExecutable: process.execPath,
      pythonExecutable, pythonVersion, playwrightPath: browserTools.playwrightPath,
      playwrightVersion: playwrightPackage.version, browserPath: browserTools.browserPath},
    probe: {origin: 'http://127.0.0.1:8787', room_roles: {browser: 'alpha', node: 'beta'},
      source_ticks: neutralPrefixFrames, neutral_prefix_frames: neutralPrefixFrames,
      neutral_pad_hex: EXPECTED_NEUTRAL_PAD_HEX, no_controller_pad_hex: EXPECTED_NO_CONTROLLER_PAD_HEX,
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

function assertReadySnapshot(snapshot, side, remoteRole, expectedFrames) {
  if (snapshot.role !== side || snapshot.ready !== true || snapshot.peerSummary?.ready !== true)
    throw Error(`${side} protocol peer did not become READY`);
  if (snapshot.localHello?.role !== side || snapshot.remoteHello?.role !== remoteRole)
    throw Error(`${side} protocol peer did not observe complementary hello roles`);
  if (!snapshot.agreementHash || snapshot.agreementHash !== snapshot.peerSummary?.start_identity_hash)
    throw Error(`${side} peer agreement hash was absent or inconsistent`);
  if (snapshot.terminal !== null || snapshot.terminals.length || snapshot.unexpectedTerminalBeforeClose)
    throw Error(`${side} observed a terminal before intentional close`);
  if (snapshot.endpointErrors.length || snapshot.endpointErrorsFromApi.length)
    throw Error(`${side} endpoint recorded an error before intentional close`);
  if (snapshot.frameBatches !== 1 || snapshot.framesEmitted !== expectedFrames || snapshot.nativeFramesExecuted !== 0 ||
      snapshot.inputCalls !== 0 || snapshot.checksumCalls !== 0 ||
      snapshot.peerSummary.local_input_ticks !== 0 || snapshot.peerSummary.local_checksum_ticks !== 0 ||
      snapshot.peerSummary.next_source_frame !== expectedFrames)
    throw Error(`${side} neutral-prefix observation or no-native-work contract failed`);
  packetTypesOnlyHello(snapshot, side);
}

function assertClosedSnapshot(snapshot, side) {
  if (snapshot.endpointClosed !== true || snapshot.disconnects.length !== 1 ||
      snapshot.terminal?.kind !== 'disconnect' || snapshot.terminals.length !== 1 ||
      snapshot.terminals[0]?.kind !== 'disconnect' || snapshot.unexpectedTerminalBeforeClose ||
      snapshot.endpointErrors.length ||
      snapshot.endpointErrorsFromApi.length)
    throw Error(`${side} endpoint did not finish one clean intentional disconnect: ${JSON.stringify({
      endpointClosed: snapshot.endpointClosed, disconnects: snapshot.disconnects,
      terminal: snapshot.terminal, errors: snapshot.endpointErrors,
      apiErrors: snapshot.endpointErrorsFromApi})}`);
}

async function run() {
  await ensureExternalRunDirectory();
  for (const name of ['result.json', 'browser-alpha-ready.png', 'worker-evidence']) {
    try { await fs.lstat(path.join(output, name)); throw Error(`Probe run refuses prior output ${name}`); }
    catch (error) { if (error.code !== 'ENOENT') throw error; }
  }
  if ((await fs.readdir(output)).some(name => !['manifest.json', 'server-spec.json', 'site', 'capture-owner'].includes(name)))
    throw Error('Probe run found unexpected data beside the frozen fixture and capture-owner outputs');
  const manifest = JSON.parse(await fs.readFile(manifestPath, 'utf8'));
  if (manifest.schema !== 'melee-web-a3-browser-owned-handshake-probe-v1') throw Error('Unknown probe manifest schema');
  const servedFiles = (await fs.readdir(site)).sort();
  if (JSON.stringify(servedFiles) !== JSON.stringify([...SITE_NAMES].sort()))
    throw Error(`Served fixture directory must contain exactly ${SITE_NAMES.join(', ')}`);
  if (!Array.isArray(manifest.served.files) ||
      JSON.stringify(manifest.served.files.slice().sort()) !== JSON.stringify([...SITE_NAMES].sort()))
    throw Error('Probe manifest inventory differs from the exact served fixture contents');
  const serverSpec = JSON.parse(await fs.readFile(path.join(output, 'server-spec.json'), 'utf8'));
  if (JSON.stringify(serverSpec.command) !== JSON.stringify([manifest.tools.pythonExecutable, SERVE, '--directory', site, '--port', '8787']) ||
      serverSpec.ready_url !== `${manifest.probe.origin}/probe.html`)
    throw Error('Probe server spec must serve only the frozen fixture directory at the allowlisted loopback origin');
  for (const [name, expected] of Object.entries(manifest.served.hashes)) {
    const actual = sha256(await fs.readFile(path.join(site, name)));
    if (actual !== expected) throw Error(`Served fixture hash changed for ${name}`);
  }
  const currentCommit = execFileSync('git', ['rev-parse', 'HEAD'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const currentTree = execFileSync('git', ['rev-parse', 'HEAD^{tree}'], {cwd: ROOT, encoding: 'utf8'}).trim();
  const currentStatus = execFileSync('git', ['status', '--porcelain'], {cwd: ROOT, encoding: 'utf8'}).trim();
  if (currentCommit !== manifest.producer.commit || currentTree !== manifest.producer.tree || currentStatus)
    throw Error('Probe producer identity or clean-worktree state differs from the reviewed manifest');
  const sourcePaths = {core: CORE, nodeFacade: NODE_FACADE, adapter: ADAPTER,
    runtimeOwner: path.join(ROOT, 'scripts/net_room_relay_runtime_owner.mjs'),
    directRuntime: path.join(ROOT, 'scripts/net_room_relay_direct_runtime.mjs'),
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
  const expectedNeutralFrames = prePortableFixture.peers?.alpha?.source_frames_before_input;
  if (!Number.isSafeInteger(expectedNeutralFrames) || expectedNeutralFrames < 1 ||
      prePortableFixture.peers?.beta?.source_frames_before_input !== expectedNeutralFrames ||
      manifest.probe.neutral_prefix_frames !== expectedNeutralFrames ||
      manifest.probe.source_ticks !== expectedNeutralFrames ||
      manifest.probe.neutral_pad_hex !== EXPECTED_NEUTRAL_PAD_HEX ||
      manifest.probe.no_controller_pad_hex !== EXPECTED_NO_CONTROLLER_PAD_HEX)
    throw Error('Frozen neutral-prefix contract differs from the pre-portable fixture or exact PAD bytes');
  if (manifest.tools.node !== process.version)
    throw Error(`Probe Node changed: expected ${manifest.tools.node}, received ${process.version}`);
  if (manifest.tools.nodeExecutable !== process.execPath)
    throw Error('Probe Node executable path differs from the reviewed manifest');
  if (execFileSync(manifest.tools.pythonExecutable, ['--version'], {encoding: 'utf8'}).trim() !== manifest.tools.pythonVersion)
    throw Error('Python interpreter version differs from the reviewed manifest');
  if (process.env.MELEE_BROWSER_PATH && path.resolve(process.env.MELEE_BROWSER_PATH) !== manifest.tools.browserPath)
    throw Error('Capture owner selected a different browser executable than the reviewed manifest');
  const {chromium, browser: launchOptions, browserPath, playwrightPath} = await loadBrowserTools(manifest.tools.playwrightPath);
  if (browserPath !== manifest.tools.browserPath || playwrightPath !== manifest.tools.playwrightPath)
    throw Error('Installed browser or Playwright identity differs from the reviewed manifest');
  const playwrightPackage = JSON.parse(await fs.readFile(path.join(playwrightPath, 'package.json'), 'utf8'));
  if (playwrightPackage.version !== manifest.tools.playwrightVersion)
    throw Error('Playwright package version differs from the reviewed manifest');

  const report = {schema: 'melee-web-a3-browser-owned-handshake-result-v1', outcome: 'fail',
    producer: manifest.producer, sourceHashes: Object.fromEntries(Object.entries(manifest.sources)
      .map(([key, value]) => [key, value.sha256])), served: manifest.served,
    runtime: null, browser: {path: browserPath, playwrightPath},
    roles: {browser: 'alpha', node: 'beta'}, contract: manifest.probe,
    events: [], firstError: null, cleanup: {browserContextClosed: false, browserClosed: false,
      endpointsClosed: false, workerDisposed: false}};
  const recordEvent = event => report.events.push({event, monotonicNs: process.hrtime.bigint().toString(), at: new Date().toISOString()});
  let runtime = null, browser = null, context = null, page = null;
  let endpoint = null, peer = null, startPromise = null, intentionalClose = false;
  let browserStartInvoked = false;
  let probePassed = false;
  let cleanupErrors = [];
  let nodeCounters = {frameBatches: 0, framesEmitted: 0, nativeFramesExecuted: 0,
    inputCalls: 0, checksumCalls: 0,
    sentTypes: [], receivedTypes: [], terminals: [], endpointErrors: [], disconnects: [],
    unexpectedTerminalBeforeClose: false};
  try {
    recordEvent('worker-start-begin');
    runtime = await startRoomRelayRuntime({evidenceDir: path.join(output, 'worker-evidence'),
      producer: manifest.producer, diagnostic: message => recordEvent(`worker:${message}`)});
    report.runtime = {base: runtime.base, port: runtime.port, ownerProcess: runtime.ownerProcess,
      identity: runtime.runtimeIdentity, hashes: runtime.hashes};
    recordEvent('worker-started');

    const roomId = createRoomId();
    const agreement = {schema: 'a3-browser-owned-handshake-v1', source_ticks: expectedNeutralFrames,
      input_ticks: 0, delay: 2, seed: 201};
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

    const {page: fixtureUrl, origin} = {page: 'probe.html', origin: manifest.probe.origin};
    browser = await chromium.launch(browserLaunchOptions(launchOptions,
      {headed: false, audible: false, timeout: 15000}));
    context = await browser.newContext({viewport: {width: 1100, height: 700}});
    page = await context.newPage();
    const pageErrors = [];
    page.on('pageerror', error => pageErrors.push(String(error?.stack || error)));
    page.on('console', message => { if (message.type() === 'error') pageErrors.push(message.text()); });
    await page.goto(`${origin}/${fixtureUrl}`, {waitUntil: 'load', timeout: 15000});
    const browserStart = page.evaluate(({relayBase, roomId, agreement, timeoutMs}) =>
      window.beginBrowserAlpha({relayBase, roomId, agreement, timeoutMs}),
      {relayBase: runtime.base.replace(/^http:/, 'ws:'), roomId, agreement, timeoutMs: 20000,
        expectedFrames: expectedNeutralFrames});
    browserStartInvoked = true;
    const [nodeReady, browserReady] = await bounded(Promise.all([
      Promise.all([endpoint.ready, startPromise, nodeProtocolReady]).then(() => true), browserStart,
    ]), 25000, 'both browser-owned handshake peers becoming READY');
    await endpoint.drainInbound(5000);
    report.browserHandshake = browserReady;
    report.nodeHandshake = {
      role: 'beta', ready: peer.ready, localHello: peer.localHello, remoteHello: peer.remoteHello,
      agreementHash: peer.agreementHash, peerSummary: peer.summary(), terminal: peer.terminal,
      sentTypes: [...nodeCounters.sentTypes], receivedTypes: [...nodeCounters.receivedTypes],
      frameBatches: nodeCounters.frameBatches, framesEmitted: nodeCounters.framesEmitted,
      nativeFramesExecuted: nodeCounters.nativeFramesExecuted, inputCalls: nodeCounters.inputCalls,
      checksumCalls: nodeCounters.checksumCalls, terminals: [...nodeCounters.terminals],
      unexpectedTerminalBeforeClose: nodeCounters.unexpectedTerminalBeforeClose,
      endpointErrors: [...nodeCounters.endpointErrors], disconnects: [...nodeCounters.disconnects],
      endpointErrorsFromApi: endpoint.errors,
    };
    if (!nodeReady) throw Error('Node beta did not become ready');
    assertReadySnapshot(report.nodeHandshake, 'beta', 'alpha', expectedNeutralFrames);
    assertReadySnapshot(report.browserHandshake, 'alpha', 'beta', expectedNeutralFrames);
    if (!report.browserHandshake.nodeGlobalsAbsent)
      throw Error('Browser fixture did not remain free of Node-only globals');
    if (report.nodeHandshake.agreementHash !== report.browserHandshake.agreementHash)
      throw Error('Browser alpha and Node beta computed different agreement hashes');
    if (pageErrors.length) throw Error(`Browser fixture reported errors: ${JSON.stringify(pageErrors)}`);
    report.browser.pageErrorsBeforeClose = pageErrors;
    const screenshotPath = path.join(output, 'browser-alpha-ready.png');
    await page.screenshot({path: screenshotPath, fullPage: true, timeout: 5000});
    report.browserScreenshot = {path: path.basename(screenshotPath), sha256: sha256(await fs.readFile(screenshotPath))};
    probePassed = true;
    recordEvent('both-peers-ready-and-asserted');
  } catch (error) {
    report.firstError = {name: error?.name || 'Error', message: String(error?.stack || error?.message || error)};
    recordEvent('probe-error');
  } finally {
    if (endpoint || page) {
      if (page && browserStartInvoked) {
        try {
          const arm = await bounded(page.evaluate(() => window.armBrowserAlphaClose()), 3000,
            'browser alpha intentional-close arm acknowledgment');
          if (arm?.intentionalClose !== true)
            throw Error(`Browser alpha did not acknowledge intentional close arm: ${JSON.stringify(arm)}`);
          report.browserCloseArm = arm;
          recordEvent('browser-alpha-intentional-close-armed');
        } catch (error) { cleanupErrors.push(`browser close arm: ${String(error?.stack || error)}`); }
      }
      // The browser must acknowledge its close arm before Node marks its own
      // close intentional; this leaves all earlier disconnects observable.
      intentionalClose = true;
      try {
        const closures = [];
        if (endpoint) closures.push((async () => {
          await endpoint.close();
          await endpoint.drainInbound();
        })());
        if (page && browserStartInvoked) closures.push(page.evaluate(() => window.closeBrowserAlpha()).then(value => {
          report.browserAfterClose = value;
        }));
        await bounded(Promise.all(closures), 25000, 'intentional endpoint close and inbound drain');
        if (report.browserAfterClose) assertClosedSnapshot(report.browserAfterClose, 'browser alpha');
        if (endpoint && peer) {
          const nodeAfterClose = {
            role: 'beta', endpointClosed: endpoint.closed, disconnects: [...nodeCounters.disconnects],
            terminal: peer.terminal, terminals: [...nodeCounters.terminals],
            unexpectedTerminalBeforeClose: nodeCounters.unexpectedTerminalBeforeClose,
            endpointErrors: [...nodeCounters.endpointErrors],
            endpointErrorsFromApi: endpoint.errors,
          };
          report.nodeAfterClose = nodeAfterClose;
          assertClosedSnapshot(nodeAfterClose, 'Node beta');
        }
        report.cleanup.endpointsClosed = true;
        recordEvent('endpoints-closed-and-drained');
      } catch (error) { cleanupErrors.push(`endpoint close: ${String(error?.stack || error)}`); }
    }
    if (page) {
      try { await bounded(page.close(), 3000, 'browser page close'); }
      catch (error) { cleanupErrors.push(`page close: ${String(error?.stack || error)}`); }
    }
    if (context) {
      try { await bounded(context.close(), 5000, 'browser context close'); report.cleanup.browserContextClosed = true; }
      catch (error) { cleanupErrors.push(`browser context close: ${String(error?.stack || error)}`); }
    }
    if (browser) {
      try { await bounded(browser.close(), 5000, 'browser close'); report.cleanup.browserClosed = true; }
      catch (error) { cleanupErrors.push(`browser close: ${String(error?.stack || error)}`); }
    }
    if (runtime) {
      try {
        await runtime.close(Boolean(probePassed && !cleanupErrors.length));
        report.cleanup.workerDisposed = true;
      } catch (error) { cleanupErrors.push(`Worker disposal: ${String(error?.stack || error)}`); }
    }
  }

  report.cleanup.errors = cleanupErrors;
  report.outcome = probePassed && report.cleanup.endpointsClosed && report.cleanup.browserClosed &&
    report.cleanup.browserContextClosed && report.cleanup.workerDisposed && cleanupErrors.length === 0
    ? 'pass' : 'fail';
  report.finishedAt = new Date().toISOString();
  const reportPath = path.join(output, 'result.json');
  await fs.writeFile(reportPath, JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
  process.stdout.write(JSON.stringify({outcome: report.outcome, result: reportPath,
    agreementHash: report.nodeHandshake?.agreementHash || null, cleanup: report.cleanup,
    firstError: report.firstError}, null, 2) + '\n');
  if (report.outcome !== 'pass') process.exitCode = 1;
}

if (values.prepare) await prepare();
else await run();
