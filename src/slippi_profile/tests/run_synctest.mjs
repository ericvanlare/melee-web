// SPDX-License-Identifier: GPL-3.0-or-later
// Bounded drawn-reference versus no-draw restoration check. The browser module
// owns a live Aurora WebGPU context and is never restored; only the separate
// Node source-only module captures/restores its Wasm linear memory.
import crypto from 'node:crypto';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {browserLaunchOptions, loadBrowserTools} from '../../../scripts/browser_tools.mjs';
import {QuiescentWasmSnapshot} from '../../../tests/quiescent_wasm_snapshot.mjs';
import {
  applyCleanupVerdict, checkedAsyncCcall, chromeUserDataDirectoryFromSpawnArgs,
  launchOwnedChromeServer, ownedProcessPresent, recordBrowserProcessInfo,
  validateAuroraFrame,
} from './runner_contract.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const OBSERVATION_BYTES = 1280;
const PCM_BYTES = 1068 * 4;
const PAD_BYTES = 44;
const SOURCE_SEED = 0x13579bdf;
const PREFIX_SAMPLES = 300;
const INTERVAL_SAMPLE = 300;
const SUBMISSION_TIMEOUT_MS = 5000;
const POLL_MS = 10;
const MAX_HEAP_BYTES = 512 * 1024 * 1024;

function args(argv) {
  const values = {};
  const allowed = new Set(['--node-runtime', '--browser-runtime', '--assets', '--out', '--playwright']);
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i], value = argv[i + 1];
    if (!allowed.has(key) || !value || values[key]) {
      throw new Error('Usage: run_synctest.mjs --node-runtime FILE --browser-runtime FILE --assets DIR --out NEW_DIR [--playwright PACKAGE_DIR]');
    }
    values[key] = value;
  }
  for (const key of ['--node-runtime', '--browser-runtime', '--assets', '--out']) {
    if (!values[key]) throw new Error(`Missing ${key}`);
    values[key] = path.resolve(values[key]);
  }
  if (values['--playwright']) values['--playwright'] = path.resolve(values['--playwright']);
  if (!fs.statSync(values['--assets']).isDirectory()) throw new Error('Assets path is not a directory');
  for (const key of ['--node-runtime', '--browser-runtime']) {
    if (!fs.statSync(values[key]).isFile()) throw new Error(`Runtime is not a file: ${values[key]}`);
  }
  if (fs.existsSync(values['--out'])) throw new Error(`Output path already exists: ${values['--out']}`);
  if (!fs.statSync(path.dirname(values['--out'])).isDirectory()) {
    throw new Error('Output parent directory must already exist');
  }
  return values;
}

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileSha = file => sha256(fs.readFileSync(file));
const cstring = (module, pointer) => module.UTF8ToString(pointer);

function copyBytes(module, pointer, size) {
  if (!Number.isInteger(pointer) || pointer < 0 || pointer + size > module.HEAPU8.byteLength) {
    throw new Error(`Wasm byte range is outside memory: pointer=${pointer}, size=${size}`);
  }
  return Buffer.from(module.HEAPU8.subarray(pointer, pointer + size));
}

function laneBytes(module) {
  const observationSize = module._melee_web_snapshot_observation_size();
  const pcmSize = module._melee_web_snapshot_pcm_size();
  if (observationSize !== OBSERVATION_BYTES) {
    throw new Error(`Observation v2 schema requires ${OBSERVATION_BYTES} bytes; runtime reports ${observationSize}`);
  }
  if (pcmSize !== PCM_BYTES) throw new Error(`PCM schema requires ${PCM_BYTES} bytes; runtime reports ${pcmSize}`);
  return {
    observation: copyBytes(module, module._melee_web_snapshot_observation(), observationSize),
    pcm: copyBytes(module, module._melee_web_snapshot_pcm(), pcmSize),
    pad: copyBytes(module, module._melee_web_snapshot_input(), PAD_BYTES),
  };
}

function firstDifference(expected, actual) {
  const limit = Math.min(expected.length, actual.length);
  for (let offset = 0; offset < limit; offset++) {
    if (expected[offset] !== actual[offset]) {
      return {offset, expected: expected[offset], actual: actual[offset]};
    }
  }
  return expected.length === actual.length ? null : {
    offset: limit, expectedLength: expected.length, actualLength: actual.length,
  };
}

function checksumRecord(sample, lane) {
  return {
    sample,
    observationSha256: sha256(lane.observation),
    pcmSha256: sha256(lane.pcm),
    rawPad44Sha256: sha256(lane.pad),
  };
}

function compareLane(sample, domain, expected, actual) {
  for (const key of ['observation', 'pcm', 'pad']) {
    if (!expected[key].equals(actual[key])) {
      const diff = firstDifference(expected[key], actual[key]);
      const failure = {sample, domain, field: key, ...diff,
        expectedSha256: sha256(expected[key]), actualSha256: sha256(actual[key])};
      throw Object.assign(new Error(`First ${domain} mismatch: ${JSON.stringify(failure)}`), {synctestFailure: failure});
    }
  }
}

function initializerBytes(module) {
  const size = module._melee_web_snapshot_initializer_diagnostic_size();
  return copyBytes(module, module._melee_web_snapshot_initializer_diagnostic(), size);
}

function machine() {
  return {
    cpu: os.cpus()[0]?.model || 'unknown',
    arch: os.arch(),
    os: os.type(),
    release: os.release(),
    node: process.version,
    nodeExecutable: process.execPath,
  };
}

function flatAssetHashes(root) {
  return Object.fromEntries(fs.readdirSync(root).sort().flatMap(name => {
    const full = path.join(root, name);
    return fs.statSync(full).isFile() ? [[name, fileSha(full)]] : [];
  }));
}

function reportSourceHashes() {
  const files = [
    'tests/gameplay_snapshot_probe.cpp',
    'tests/quiescent_wasm_snapshot.mjs',
    'src/slippi_profile/profile.h',
    'src/slippi_profile/profile.c',
    'src/slippi_profile/offscreen_damage.h',
    'src/slippi_profile/offscreen_damage.c',
    'src/slippi_profile/dynamics_fix.h',
    'src/slippi_profile/dynamics_fix.c',
    'src/slippi_profile/deadupfall_fix.h',
    'src/slippi_profile/deadupfall_fix.c',
    'src/slippi_profile/patches/melee-profile-hooks.patch',
    'src/slippi_profile/tests/synctest_node.cpp',
    'src/slippi_profile/tests/synctest_browser.cpp',
    'src/slippi_profile/tests/run_synctest.mjs',
    'src/slippi_profile/tests/runner_contract.mjs',
    'cmake/FighterRuntime.cmake',
    'CMakeLists.txt',
    'scripts/build.py',
    'scripts/gameplay_sources.py',
  ];
  return Object.fromEntries(files.map(name => [name, fileSha(path.join(ROOT, name))]));
}

function serveBuild(buildRoot, fileNames) {
  const page = `<!doctype html><html><head><meta charset="utf-8"><link rel="icon" href="data:,"><title>Slippi-compatible rules (unofficial) — dependency diagnostic</title></head><body><p>Slippi-compatible rules (unofficial) — private dependency diagnostic only; no player settings or service connection. Not affiliated with or endorsed by Project Slippi.</p><canvas id="canvas" width="640" height="480"></canvas></body></html>`;
  const allowed = new Map(fileNames.map(name => [`/${name}`, path.join(buildRoot, name)]));
  const server = http.createServer((request, response) => {
    response.setHeader('cross-origin-opener-policy', 'same-origin');
    response.setHeader('cross-origin-embedder-policy', 'require-corp');
    const pathname = new URL(request.url || '/', 'http://localhost').pathname;
    if (pathname === '/') {
      response.writeHead(200, {'content-type': 'text/html; charset=utf-8', 'cache-control': 'no-store'});
      response.end(page);
      return;
    }
    const file = allowed.get(pathname);
    if (!file) {
      response.writeHead(404); response.end('not found'); return;
    }
    const mime = pathname.endsWith('.wasm') ? 'application/wasm' :
      pathname.endsWith('.data') ? 'application/octet-stream' : 'text/javascript; charset=utf-8';
    response.writeHead(200, {'content-type': mime, 'cache-control': 'no-store'});
    fs.createReadStream(file).pipe(response);
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      server.removeListener('error', reject);
      resolve({server, origin: `http://127.0.0.1:${server.address().port}/`});
    });
  });
}

function checkedNode(module, label, call) {
  const result = call();
  if (result !== 1) throw new Error(`${label}: ${cstring(module, module._melee_web_snapshot_error())}`);
}

async function runBrowserInit(page, origin, runtimeName) {
  return page.evaluate(async ({origin, runtimeName, seed, checkedSource}) => {
    const encodeBase64 = bytes => {
      let binary = '';
      for (let start = 0; start < bytes.length; start += 0x6000) {
        binary += String.fromCharCode(...bytes.subarray(start, Math.min(start + 0x6000, bytes.length)));
      }
      return btoa(binary);
    };
    const imported = await import(new URL(runtimeName, origin).href);
    const create = imported.default || imported.createGameplaySlippiSynctestBrowser ||
      Object.values(imported).find(value => typeof value === 'function');
    if (typeof create !== 'function') throw new Error('Browser runtime did not export its module factory');
    const logs = [];
    const module = await create({
      canvas: document.querySelector('#canvas'),
      locateFile: name => new URL(name, origin).href,
      print: line => logs.push({stream: 'stdout', line: String(line)}),
      printErr: line => logs.push({stream: 'stderr', line: String(line)}),
    });
    window.__slippiSynctest = {module, logs};
    const checkedAsyncCcall = new Function(`return (${checkedSource})`)();
    const checked = (label, identifier, argumentTypes = [], args = []) =>
      checkedAsyncCcall(module, label, identifier, argumentTypes, args);
    const featureAvailable = Boolean(navigator.gpu);
    if (!featureAvailable) throw new Error('Headless Chrome does not expose WebGPU to this page');
    // Match the shared runtime's prerequisite even without persistent storage.
    module.FS.mkdirTree('/melee-render-cache');
    await checked('Aurora WebGPU initialization', 'melee_web_synctest_renderer_initialize');
    const backend = JSON.parse(module.UTF8ToString(module._melee_web_synctest_submission_json()));
    if (!backend.initialized || backend.backend !== 7) {
      throw new Error(`Aurora did not retain its WebGPU backend: ${JSON.stringify(backend)}`);
    }
    if (backend.rendererErrorCount !== 0) {
      throw new Error(`Aurora renderer logged native errors during initialization: ${JSON.stringify(backend)}`);
    }
    await checked('configure source seed', 'melee_web_snapshot_configure_seed', ['number'], [seed]);
    await checked('snapshot init', 'melee_web_snapshot_init', ['string'], ['/assets']);
    const read = () => ({
      observation: module.HEAPU8.slice(module._melee_web_snapshot_observation(),
        module._melee_web_snapshot_observation() + module._melee_web_snapshot_observation_size()),
      pcm: module.HEAPU8.slice(module._melee_web_snapshot_pcm(),
        module._melee_web_snapshot_pcm() + module._melee_web_snapshot_pcm_size()),
      pad: module.HEAPU8.slice(module._melee_web_snapshot_input(),
        module._melee_web_snapshot_input() + 44),
    });
    const init = {
      featureAvailable,
      backend,
      profile: module.UTF8ToString(module._melee_web_synctest_profile_identity()),
      source: module.UTF8ToString(module._melee_web_snapshot_source_identity()),
      rngHelper: module.UTF8ToString(module._melee_web_snapshot_rng_profile_identity()),
      stageBridge: module.UTF8ToString(module._melee_web_snapshot_stage_kind_bridge_identity()),
      initializer: module.UTF8ToString(module._melee_web_snapshot_initializer_json()),
      initializerBytes: encodeBase64(module.HEAPU8.slice(
        module._melee_web_snapshot_initializer_diagnostic(),
        module._melee_web_snapshot_initializer_diagnostic() +
          module._melee_web_snapshot_initializer_diagnostic_size())),
      initial: Object.fromEntries(Object.entries(read()).map(([key, bytes]) => [key, encodeBase64(bytes)])),
      logs,
    };
    return init;
  }, {origin, runtimeName, seed: SOURCE_SEED, checkedSource: checkedAsyncCcall.toString()});
}

async function runBrowserSample(page, sample) {
  return page.evaluate(async ({sample, checkedSource}) => {
    const {module} = window.__slippiSynctest;
    const checkedAsyncCcall = new Function(`return (${checkedSource})`)();
    const checked = (label, identifier, argumentTypes = [], args = []) =>
      checkedAsyncCcall(module, label, identifier, argumentTypes, args);
    const decision = module._melee_web_synctest_draw_decision(0);
    if (decision !== 1) throw new Error(`present tick draw policy returned ${decision}`);
    await checked(`draw-reference source tick ${sample}`,
      'melee_web_snapshot_step', ['number'], [sample]);
    await checked(`actual source draw ${sample}`,
      'melee_web_synctest_draw_present');
    if (sample === 0) {
      await checked('draw-reference native quiescence', 'melee_web_snapshot_quiescent');
    }
    const deadline = performance.now() + 5000;
    let status;
    for (;;) {
      status = JSON.parse(module.UTF8ToString(module._melee_web_synctest_submission_json()));
      if (status.rendererErrorCount !== 0) {
        throw new Error(`Aurora renderer logged native errors at sample ${sample}: ${JSON.stringify(status)}`);
      }
      if (status.pendingFramePackets === 0 && status.pendingStagingBuffers === 0 &&
          status.workerBusy === 0) break;
      if (performance.now() >= deadline) {
        throw new Error(`Aurora submission did not quiesce for sample ${sample}: ${JSON.stringify(status)}`);
      }
      await new Promise(resolve => setTimeout(resolve, 10));
    }
    const read = (pointer, size) => module.HEAPU8.slice(pointer, pointer + size);
    const observationSize = module._melee_web_snapshot_observation_size();
    const pcmSize = module._melee_web_snapshot_pcm_size();
    if (observationSize !== 1280 || pcmSize !== 4272) {
      throw new Error(`Unsupported observation/PCM ABI: ${observationSize}/${pcmSize}`);
    }
    const observation = read(module._melee_web_snapshot_observation(), observationSize);
    const pcm = read(module._melee_web_snapshot_pcm(), pcmSize);
    const pad = read(module._melee_web_snapshot_input(), 44);
    const encode = bytes => {
      let binary = '';
      for (let i = 0; i < bytes.length; i += 0x6000) {
        binary += String.fromCharCode(...bytes.subarray(i, Math.min(i + 0x6000, bytes.length)));
      }
      return btoa(binary);
    };
    return {observation: encode(observation), pcm: encode(pcm), pad: encode(pad), status};
  }, {sample, checkedSource: checkedAsyncCcall.toString()});
}

async function closeBrowser(page) {
  if (!page) return null;
  return page.evaluate(async checkedSource => {
    const state = window.__slippiSynctest;
    if (!state) return {closed: true, renderer: false};
    const {module} = state;
    const checkedAsyncCcall = new Function(`return (${checkedSource})`)();
    const errors = [];
    try {
      const status = JSON.parse(module.UTF8ToString(module._melee_web_synctest_submission_json()));
      if (status.pendingFramePackets !== 0 || status.pendingStagingBuffers !== 0 || status.workerBusy !== 0) {
        errors.push(`renderer was not quiescent at shutdown: ${JSON.stringify(status)}`);
      }
      if (status.rendererErrorCount !== 0) {
        errors.push(`renderer logged errors before shutdown: ${JSON.stringify(status)}`);
      }
    } catch (error) { errors.push(`renderer quiescence check failed: ${String(error)}`); }
    try {
      await checkedAsyncCcall(module, 'snapshot close', 'melee_web_snapshot_close');
    } catch (error) { errors.push(String(error)); }
    try {
      await checkedAsyncCcall(module, 'renderer shutdown', 'melee_web_synctest_renderer_shutdown');
    } catch (error) { errors.push(String(error)); }
    return {closed: errors.length === 0, renderer: true, errors};
  }, checkedAsyncCcall.toString());
}

async function main() {
  const options = args(process.argv.slice(2));
  fs.mkdirSync(options['--out']);
  const outFile = path.join(options['--out'], 'evidence.json');
  const report = {
    schema: 'melee-web-slippi-drawn-reference-no-draw-synctest-v1',
    result: 'incomplete',
    profile: 'slippi-render-dependencies-v2',
    statement: 'Unofficial Slippi-compatible dependency diagnostic; not a complete Slippi or rollback profile.',
    scenario: 'two human Mario players / Final Destination / four stocks; deterministic fixture inputs',
    interval: {checkpointInputSample: PREFIX_SAMPLES, inputSample: INTERVAL_SAMPLE,
      depth: 1, prefixSamples: PREFIX_SAMPLES},
    schedule: {
      drawnReference: 'source tick → source PCM → actual GameplayMatchSession::draw() inside Aurora begin/end frame → wait until Aurora frame packets, staging buffers, and worker are idle → refresh v2 observation',
      correction: 'source tick → source PCM → capture v2/PCM/raw PAD; source draw policy must return SKIP',
      initialEqualityBeforeSnapshot: true,
    },
    comparisonContract: {
      observation: {schema: 'melee-web-source-snapshot-observation-v2', bytes: OBSERVATION_BYTES},
      pcmBytes: PCM_BYTES,
      rawPadBytes: PAD_BYTES,
      exactBytesAndSha256: true,
      fullHeap: 'Node no-draw lane checkpoint restore integrity and endpoint replay only; never compared with drawn browser heap',
    },
    sourceDraw: {drawnLane: true, correctionLane: false, calls: 0, completedFrames: 0},
    snapshotScope: 'The Node no-draw lane alone snapshots its complete Wasm linear memory. Aurora WebGPU, Chrome, OS/GPU caches, browser surface, pending submissions, DOM, Node process state, audio output, filesystem, and external allocator state are host-owned and are not snapshotted.',
    audio: 'Native PCM bytes are compared after each source tick; fixture PCM is not sent to Web Audio or host speakers.',
    controls: {drawPolicy: 'native focused test', offscreenAndMatrices: 'native focused tests; not claimed exercised by this fixed interval'},
    licenses: {
      profileBehavior: 'GPL-3.0-or-later',
      sourceFixture: 'tests/gameplay_snapshot_probe.cpp',
      legacyRngHelper: 'GPL-3.0-only pinned helper remains linked for fixture symbol references; profile callback/reset is never enabled',
    },
    machine: machine(),
    browser: null,
    browser_process_info: [],
    browser_process_info_source: null,
    assets: flatAssetHashes(options['--assets']),
    runtimes: {},
    sourceSha256: reportSourceHashes(),
    initialization: null,
    samples: [],
    nodeRestore: null,
    negatives: [],
    cleanup: null,
    failure: null,
  };
  const writeReport = () => fs.writeFileSync(outFile, `${JSON.stringify(report, null, 2)}\n`);
  writeReport();
  let nodeModule = null, nodeSnapshot = null, nodeInitialized = false;
  let server = null, browserServer = null, browserProcess = null;
  let browser = null, context = null, page = null;
  let browserCdp = null, pageErrors = [], browserProcessPid = null, profileDirectory = null;
  const ownedChromePids = new Set();
  let browserInit = null;
  try {
    const nodeRuntime = options['--node-runtime'];
    const browserRuntime = options['--browser-runtime'];
    const nodeWasm = nodeRuntime.replace(/\.js$/, '.wasm');
    const browserWasm = browserRuntime.replace(/\.mjs$/, '.wasm');
    const browserData = browserRuntime.replace(/\.mjs$/, '.data');
    for (const file of [nodeWasm, browserWasm, browserData]) {
      if (!fs.statSync(file).isFile()) throw new Error(`Missing synctest runtime companion: ${file}`);
    }
    report.runtimes = Object.fromEntries([nodeRuntime, nodeWasm, browserRuntime,
      browserWasm, browserData].map(file => [path.basename(file), fileSha(file)]));

    const require = createRequire(import.meta.url);
    const factory = require(nodeRuntime);
    nodeModule = await factory({print: () => {}, printErr: line => process.stderr.write(`${line}\n`)});
    const checkN = (label, call) => checkedNode(nodeModule, label, call);
    checkN('configure source seed', () => nodeModule._melee_web_snapshot_configure_seed(SOURCE_SEED));
    checkN('no-draw snapshot init', () => nodeModule.ccall(
      'melee_web_snapshot_init', 'number', ['string'], [options['--assets']]));
    nodeInitialized = true;
    const initialNode = laneBytes(nodeModule);
    const nodeInit = {
      profile: cstring(nodeModule, nodeModule._melee_web_synctest_profile_identity()),
      source: cstring(nodeModule, nodeModule._melee_web_snapshot_source_identity()),
      rngHelper: cstring(nodeModule, nodeModule._melee_web_snapshot_rng_profile_identity()),
      stageBridge: cstring(nodeModule, nodeModule._melee_web_snapshot_stage_kind_bridge_identity()),
      initializer: JSON.parse(cstring(nodeModule, nodeModule._melee_web_snapshot_initializer_json())),
      initializerBytes: initializerBytes(nodeModule),
      initial: initialNode,
    };
    const buildRoot = path.dirname(browserRuntime);
    const files = [path.basename(browserRuntime), path.basename(browserWasm), path.basename(browserData)];
    const serving = await serveBuild(buildRoot, files);
    server = serving.server;
    const tools = await loadBrowserTools(options['--playwright']);
    const launch = browserLaunchOptions({executablePath: tools.browserPath}, {headed: false, audible: false, timeout: 30000});
    const ownedBrowser = await launchOwnedChromeServer(tools.chromium, launch);
    browserServer = ownedBrowser.server;
    browserProcess = ownedBrowser.child;
    browserProcessPid = browserProcess.pid;
    ownedChromePids.add(browserProcessPid);
    report.browser_process_info = [{id: browserProcessPid, type: 'browser'}];
    report.browser_process_info_source = 'Playwright-owned Chrome child process (provisional until CDP inventory)';
    report.browser = {
      ...(report.browser || {}),
      name: 'headless installed Chrome through scripts/browser_tools.mjs',
      executable: tools.browserPath,
      ownedProcessPid: browserProcessPid,
      userDataDirectory: null,
      commandLine: browserProcess.spawnargs,
    };
    writeReport();
    profileDirectory = path.resolve(chromeUserDataDirectoryFromSpawnArgs(browserProcess.spawnargs));
    const tempRoot = process.env.TMPDIR ? path.resolve(process.env.TMPDIR) : null;
    if (!tempRoot || !fs.statSync(tempRoot).isDirectory()) {
      throw new Error('TMPDIR must name the task-owned external scratch directory for Chrome profile data');
    }
    const profileRelativeToTemp = path.relative(tempRoot, profileDirectory);
    if (profileRelativeToTemp === '..' || profileRelativeToTemp.startsWith(`..${path.sep}`) ||
        path.isAbsolute(profileRelativeToTemp)) {
      throw new Error(`Playwright Chrome profile is outside task-owned TMPDIR: ${profileDirectory}`);
    }
    report.browser.userDataDirectory = profileDirectory;
    report.browser.profileTempRoot = tempRoot;
    writeReport();
    browser = await tools.chromium.connect(browserServer.wsEndpoint());
    browserCdp = await browser.newBrowserCDPSession();
    const processInfo = await browserCdp.send('SystemInfo.getProcessInfo');
    const attributed = recordBrowserProcessInfo(report, processInfo.processInfo);
    for (const pid of attributed.processIds) ownedChromePids.add(pid);
    writeReport();
    context = await browser.newContext();
    page = await context.newPage();
    page.setDefaultTimeout(120000);
    page.on('pageerror', error => pageErrors.push(String(error)));
    page.on('console', message => {
      if (message.type() === 'error') pageErrors.push(message.text());
    });
    await page.goto(serving.origin, {waitUntil: 'domcontentloaded'});
    browserInit = await runBrowserInit(page, serving.origin, path.basename(browserRuntime));
    report.browser = {
      ...(report.browser || {}),
      name: 'headless installed Chrome through scripts/browser_tools.mjs',
      executable: tools.browserPath,
      version: browser.version(),
      webgpuAvailable: browserInit.featureAvailable,
      backend: browserInit.backend.backend,
      canvas: '640x480 browser surface',
      audioOutput: 'muted; native PCM is not connected to Web Audio',
      ownedProcessPid: browserProcessPid,
      userDataDirectory: profileDirectory,
      profileTempRoot: tempRoot,
      commandLine: browserProcess.spawnargs,
      cdpProcessInventory: report.browser_process_info,
    };
    const initialBrowser = Object.fromEntries(Object.entries(browserInit.initial).map(
      ([key, base64]) => [key, Buffer.from(base64, 'base64')]));
    const browserInitRecord = {
      profile: browserInit.profile,
      source: browserInit.source,
      rngHelper: browserInit.rngHelper,
      stageBridge: browserInit.stageBridge,
      initializer: JSON.parse(browserInit.initializer),
      initializerBytes: Buffer.from(browserInit.initializerBytes, 'base64'),
      initial: initialBrowser,
    };
    const identityFields = ['profile', 'source', 'rngHelper', 'stageBridge'];
    for (const field of identityFields) {
      if (nodeInit[field] !== browserInitRecord[field]) {
        throw new Error(`Initial ${field} identity differs: Node=${nodeInit[field]} browser=${browserInitRecord[field]}`);
      }
    }
    if (nodeInit.profile !== 'slippi-render-dependencies-v2') {
      throw new Error(`Unexpected profile identity: ${nodeInit.profile}`);
    }
    const expectedProbeHash = report.sourceSha256['tests/gameplay_snapshot_probe.cpp'];
    if (nodeInit.source !== expectedProbeHash) {
      throw new Error(`Runtime fixture identity differs from current source: runtime=${nodeInit.source} current=${expectedProbeHash}`);
    }
    if (nodeInit.initializer.profile !== 0 || browserInitRecord.initializer.profile !== 0 ||
        nodeInit.initializer.seed !== SOURCE_SEED || browserInitRecord.initializer.seed !== SOURCE_SEED) {
      throw new Error('Initial source seed/profile differs before any snapshot');
    }
    if (!nodeInit.initializerBytes.equals(browserInitRecord.initializerBytes)) {
      throw new Error(`Initial profile/seed state differs before snapshot: ${JSON.stringify(firstDifference(nodeInit.initializerBytes, browserInitRecord.initializerBytes))}`);
    }
    compareLane(-1, 'initialization', nodeInit.initial, initialBrowser);
    report.initialization = {
      exactProfileSeedAndInitializer: true,
      sourceSeed: SOURCE_SEED,
      profileSeedResetOrCallback: false,
      profileIdentity: nodeInit.profile,
      sourceIdentity: nodeInit.source,
      oldRngHelperIdentity: nodeInit.rngHelper,
      oldRngHelperEnabled: false,
      initializer: nodeInit.initializer,
      observationSha256: sha256(initialNode.observation),
      pcmSha256: sha256(initialNode.pcm),
      rawPad44Sha256: sha256(initialNode.pad),
    };
    writeReport();

    let previousBrowserFrame = {
      beginFrameId: browserInit.backend.lastBeginFrameId || 0,
      endFrameId: browserInit.backend.lastEndFrameId || 0,
    };
    if (nodeModule._melee_web_synctest_draw_decision(1) !== 0) {
      throw new Error('Correction lane draw policy did not skip drawing');
    }
    const nodePrefix = [];
    for (let sample = 0; sample < PREFIX_SAMPLES; sample++) {
      const decision = nodeModule._melee_web_synctest_draw_decision(1);
      if (decision !== 0) throw new Error(`Correction draw policy was ${decision} at sample ${sample}`);
      checkN(`no-draw source step ${sample}`, () => nodeModule._melee_web_snapshot_step(sample));
      if (sample === 0) checkN('no-draw native quiescence', () => nodeModule._melee_web_snapshot_quiescent());
      const noDraw = laneBytes(nodeModule);
      const drawn = await runBrowserSample(page, sample);
      previousBrowserFrame = validateAuroraFrame(drawn.status,
        previousBrowserFrame.beginFrameId, previousBrowserFrame.endFrameId);
      if (sample === 0) {
        const screenshot = path.join(options['--out'], 'drawn-reference-first-frame.png');
        await page.screenshot({path: screenshot, fullPage: true});
        const screenshotBytes = fs.readFileSync(screenshot);
        if (screenshotBytes.length < 100 || !screenshotBytes.subarray(0, 8).equals(
          Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
          throw new Error('Headless Chrome did not retain a valid first-frame PNG');
        }
        report.browser.screenshot = {path: path.basename(screenshot), sha256: sha256(screenshotBytes), bytes: screenshotBytes.length};
      }
      const drawRef = Object.fromEntries(Object.entries(drawn).filter(([key]) => key !== 'status')
        .map(([key, base64]) => [key, Buffer.from(base64, 'base64')]));
      compareLane(sample, 'drawn-reference versus correction-prefix', noDraw, drawRef);
      const record = {
        ...checksumRecord(sample, noDraw),
        comparison: 'exact-v2-observation/PCM/raw-PAD bytes passed',
        drawCallCount: drawn.status.drawCallCount,
        createdPipelines: drawn.status.createdPipelines,
        queuedPipelines: drawn.status.queuedPipelines,
      };
      report.samples.push(record);
      report.sourceDraw.calls++;
      report.sourceDraw.completedFrames++;
      nodePrefix.push(noDraw);
      writeReport();
    }

    checkN('no-draw checkpoint quiescence', () => nodeModule._melee_web_snapshot_quiescent());
    nodeSnapshot = new QuiescentWasmSnapshot(nodeModule, MAX_HEAP_BYTES);
    const checkpoint = nodeSnapshot.capture();
    const checkpointHeap = Buffer.from(nodeModule.HEAPU8);
    const checkpointSha = sha256(checkpointHeap);
    report.nodeRestore = {
      checkpointInputSample: PREFIX_SAMPLES,
      checkpointHeapBytes: checkpointHeap.byteLength,
      checkpointHeapSha256: checkpointSha,
      endpointHeapSha256: null,
      restoredCheckpointHeapSha256: null,
      replayEndpointHeapSha256: null,
      originalEndpointVsReplayExact: null,
      restoredCheckpointExact: null,
      result: 'incomplete',
    };
    const correctionStep = sample => {
      const decision = nodeModule._melee_web_synctest_draw_decision(1);
      if (decision !== 0) throw new Error(`Correction draw policy was ${decision} at sample ${sample}`);
      checkN(`correction interval step ${sample}`, () => nodeModule._melee_web_snapshot_step(sample));
      return laneBytes(nodeModule);
    };
    const noDrawEndpoint = correctionStep(INTERVAL_SAMPLE);
    const endpointHeap = Buffer.from(nodeModule.HEAPU8);
    const endpointSha = sha256(endpointHeap);
    report.nodeRestore.endpointHeapSha256 = endpointSha;
    const drawnEndpointBase64 = await runBrowserSample(page, INTERVAL_SAMPLE);
    previousBrowserFrame = validateAuroraFrame(drawnEndpointBase64.status,
      previousBrowserFrame.beginFrameId, previousBrowserFrame.endFrameId);
    const drawnEndpoint = Object.fromEntries(Object.entries(drawnEndpointBase64)
      .filter(([key]) => key !== 'status').map(([key, base64]) => [key, Buffer.from(base64, 'base64')]));
    compareLane(INTERVAL_SAMPLE, 'drawn-reference versus no-draw interval',
      noDrawEndpoint, drawnEndpoint);
    report.interval.drawnReference = checksumRecord(INTERVAL_SAMPLE, drawnEndpoint);
    report.interval.noDrawBaseline = checksumRecord(INTERVAL_SAMPLE, noDrawEndpoint);
    report.interval.drawnReferenceDrawCalls = drawnEndpointBase64.status.drawCallCount;
    report.sourceDraw.calls++;
    report.sourceDraw.completedFrames++;
    writeReport();

    checkpoint.restore();
    const restoredCheckpointHeap = Buffer.from(nodeModule.HEAPU8);
    const restoredCheckpointSha = sha256(restoredCheckpointHeap);
    if (!checkpointHeap.equals(restoredCheckpointHeap)) {
      throw new Error(`No-draw full-heap restore did not reproduce checkpoint: ${JSON.stringify(firstDifference(checkpointHeap, restoredCheckpointHeap))}`);
    }
    report.nodeRestore.restoredCheckpointHeapSha256 = restoredCheckpointSha;
    report.nodeRestore.restoredCheckpointExact = true;

    const rngPointer = nodeModule._melee_web_snapshot_rng_address();
    if (!Number.isInteger(rngPointer) || rngPointer < 0 || rngPointer + 4 > nodeModule.HEAPU8.byteLength) {
      throw new Error(`Source RNG pointer is outside no-draw Wasm memory: ${rngPointer}`);
    }
    const rngBefore = nodeModule.HEAPU8[rngPointer];
    nodeModule.HEAPU8[rngPointer] ^= 1;
    const corrupted = correctionStep(INTERVAL_SAMPLE);
    const rngMismatch = firstDifference(noDrawEndpoint.observation, corrupted.observation);
    if (!rngMismatch) throw new Error('One-bit source RNG corruption was not detected by observation bytes');
    report.negatives.push({
      kind: 'source-RNG-bit corruption', result: 'detected', byteOffset: rngPointer,
      originalLowByte: rngBefore, corruptedLowByte: nodeModule.HEAPU8[rngPointer],
      firstObservationDifference: rngMismatch,
      expectedObservationSha256: sha256(noDrawEndpoint.observation),
      actualObservationSha256: sha256(corrupted.observation),
    });
    writeReport();

    checkpoint.restore();
    const postCorruptionRestoreSha = sha256(nodeModule.HEAPU8);
    if (postCorruptionRestoreSha !== checkpointSha) {
      throw new Error(`Corruption recovery restore differs from checkpoint: ${postCorruptionRestoreSha}`);
    }
    const replay = correctionStep(INTERVAL_SAMPLE);
    const replayHeap = Buffer.from(nodeModule.HEAPU8);
    const replaySha = sha256(replayHeap);
    compareLane(INTERVAL_SAMPLE, 'no-draw restored replay', noDrawEndpoint, replay);
    if (!endpointHeap.equals(replayHeap)) {
      throw new Error(`No-draw endpoint heap differs after replay: ${JSON.stringify(firstDifference(endpointHeap, replayHeap))}`);
    }
    report.nodeRestore.replayEndpointHeapSha256 = replaySha;
    report.nodeRestore.originalEndpointVsReplayExact = true;

    checkpoint.restore();
    if (sha256(nodeModule.HEAPU8) !== checkpointSha) {
      throw new Error('Checkpoint heap changed before raw-PAD negative control');
    }
    const expectedPad = Buffer.from(noDrawEndpoint.pad);
    if (expectedPad[2] !== 0) throw new Error(`PAD mutation control expected neutral stickX at offset 2; got ${expectedPad[2]}`);
    const changedPad = Buffer.from(expectedPad);
    changedPad[2] ^= 0x01;
    const padPointer = nodeModule._melee_web_snapshot_input();
    nodeModule.HEAPU8.set(changedPad, padPointer);
    if (!copyBytes(nodeModule, padPointer, PAD_BYTES).equals(changedPad)) {
      throw new Error('Raw PAD test mutation did not write the declared 44-byte sample');
    }
    if (nodeModule._melee_web_synctest_draw_decision(1) !== 0) {
      throw new Error('Raw PAD negative control unexpectedly permitted a source draw');
    }
    checkN('raw-PAD negative control step', () => nodeModule._melee_web_snapshot_step_raw());
    const changedPadObservation = laneBytes(nodeModule);
    const padDifference = firstDifference(expectedPad, changedPadObservation.pad);
    if (!padDifference || padDifference.offset !== 2 || padDifference.actual !== (padDifference.expected ^ 1)) {
      throw new Error(`Raw-PAD checksum did not detect the single-bit input mutation: ${JSON.stringify(padDifference)}`);
    }
    report.negatives.push({kind: 'raw PAD byte mutation', result: 'detected',
      padBytes: PAD_BYTES, changedByteOffset: padDifference.offset,
      expectedPadSha256: sha256(expectedPad), actualPadSha256: sha256(changedPadObservation.pad),
      firstDifference: padDifference, changedStateSha256: sha256(changedPadObservation.observation)});
    writeReport();

    checkpoint.restore();
    if (sha256(nodeModule.HEAPU8) !== checkpointSha) {
      throw new Error('Raw-PAD negative-control recovery did not restore full checkpoint heap');
    }
    const finalRecovery = correctionStep(INTERVAL_SAMPLE);
    const finalRecoveryHeapSha = sha256(nodeModule.HEAPU8);
    compareLane(INTERVAL_SAMPLE, 'post-negative-control recovery', noDrawEndpoint, finalRecovery);
    if (finalRecoveryHeapSha !== endpointSha) {
      throw new Error('Post-negative-control endpoint heap did not recover exactly');
    }
    report.nodeRestore.result = 'passed';
    report.nodeRestore.restoredCheckpointExact = true;
    report.nodeRestore.finalRecoveryEndpointHeapSha256 = finalRecoveryHeapSha;
    report.nodeRestore.fullHeapComparedAcrossLanes = false;
    report.result = 'passed';
    report.browser.pageErrors = pageErrors;
    const nativeDiagnostics = await page.evaluate(() => {
      const {module, logs} = window.__slippiSynctest;
      return {
        stderr: logs.filter(row => row?.stream === 'stderr'),
        rendererStatus: JSON.parse(module.UTF8ToString(module._melee_web_synctest_submission_json())),
      };
    });
    report.browser.nativeDiagnostics = nativeDiagnostics;
    if (nativeDiagnostics.rendererStatus.rendererErrorCount !== 0) {
      throw new Error(`Aurora renderer reported native errors: ${JSON.stringify(nativeDiagnostics.rendererStatus)}`);
    }
    if (pageErrors.length) throw new Error(`Browser reported page errors: ${pageErrors[0]}`);
    writeReport();
  } catch (error) {
    report.result = 'failed';
    report.failure = error.synctestFailure || {message: String(error?.stack || error)};
    if (page) {
      try {
        const screenshot = path.join(options['--out'], 'failure.png');
        await page.screenshot({path: screenshot, fullPage: true});
        const screenshotBytes = fs.readFileSync(screenshot);
        report.failureScreenshot = {path: path.basename(screenshot), sha256: sha256(screenshotBytes), bytes: screenshotBytes.length};
      } catch (screenshotError) {
        report.failureScreenshotError = String(screenshotError);
      }
      try { report.browserPageState = await page.evaluate(() => ({
        title: document.title,
        logs: window.__slippiSynctest?.logs || [],
      })); } catch {}
    }
    writeReport();
    throw error;
  } finally {
    const cleanupFailures = [];
    if (nodeInitialized && nodeModule) {
      try { checkedNode(nodeModule, 'no-draw fixture close', () => nodeModule._melee_web_snapshot_close()); }
      catch (error) { cleanupFailures.push({step: 'node-fixture-close', error: String(error)}); }
    }
    if (page) {
      try {
        const closed = await closeBrowser(page);
        if (!closed?.closed) cleanupFailures.push({step: 'renderer-close', error: JSON.stringify(closed)});
        report.cleanup = {...(report.cleanup || {}), renderer: closed};
      } catch (error) {
        cleanupFailures.push({step: 'renderer-close', error: String(error)});
      }
    }
    if (browserCdp) {
      try {
        const finalInventory = await browserCdp.send('SystemInfo.getProcessInfo');
        const attributed = recordBrowserProcessInfo(report, finalInventory.processInfo);
        for (const pid of attributed.processIds) ownedChromePids.add(pid);
        report.browser = {...(report.browser || {}), finalCdpProcessInventory: report.browser_process_info};
        writeReport();
      } catch (error) {
        cleanupFailures.push({step: 'chrome-final-process-inventory', error: String(error)});
      }
      try { await browserCdp.detach(); }
      catch (error) { cleanupFailures.push({step: 'chrome-cdp-detach', error: String(error)}); }
    }
    if (context) {
      try { await context.close(); }
      catch (error) { cleanupFailures.push({step: 'browser-context-close', error: String(error)}); }
    }
    if (browser) {
      try { await browser.close(); }
      catch (error) { cleanupFailures.push({step: 'browser-connection-close', error: String(error)}); }
    }
    if (browserServer) {
      try { await browserServer.close(); }
      catch (error) {
        cleanupFailures.push({step: 'owned-chrome-close', error: String(error)});
        try { await browserServer.kill(); }
        catch (killError) { cleanupFailures.push({step: 'owned-chrome-kill', error: String(killError)}); }
      }
      const processStates = {};
      for (const pid of [...ownedChromePids].sort((a, b) => a - b)) {
        try {
          processStates[pid] = ownedProcessPresent(pid);
          if (processStates[pid]) {
            cleanupFailures.push({step: 'owned-chrome-process-absence', error: `owned PID ${pid} still appears present`});
          }
        } catch (error) {
          processStates[pid] = null;
          cleanupFailures.push({step: 'owned-chrome-process-check', pid, error: String(error)});
        }
      }
      report.cleanup = {...(report.cleanup || {}), chrome: {
        pid: browserProcessPid,
        exitCode: browserProcess?.exitCode ?? null,
        signalCode: browserProcess?.signalCode ?? null,
        ownedPidAbsenceAfterClose: processStates,
        userDataDirectory: profileDirectory,
      }};
    }
    if (server) {
      try {
        await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve()));
        report.cleanup = {...(report.cleanup || {}), httpServer: 'closed'};
      } catch (error) { cleanupFailures.push({step: 'http-server-close', error: String(error)}); }
    }
    if (applyCleanupVerdict(report, cleanupFailures) === 'failed') process.exitCode = 1;
    writeReport();
  }
}

main().catch(error => {
  process.stderr.write(`Slippi synctest failed: ${error?.stack || error}\n`);
  process.exitCode = 1;
});
