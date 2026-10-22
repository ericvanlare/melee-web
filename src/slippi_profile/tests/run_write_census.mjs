// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only Wasm-memory write census for one warmed, actually drawn Slippi
// fixture sample. This records net byte-value deltas, not stores or a restore.
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {browserLaunchOptions, loadBrowserTools} from '../../../scripts/browser_tools.mjs';
import {
  applyCleanupVerdict, assertDrawDiagnosticsClear,
  chromeUserDataDirectoryFromSpawnArgs, launchOwnedChromeServer,
  ownedProcessPresent, recordBrowserProcessInfo,
  validateAuroraFrame, validateSelectedDrawPath, validateSourceInputMapping, decodeDrawDiagnostics,
} from './runner_contract.mjs';
import {
  PREFIX_SAMPLES, SOURCE_SEED, SYNCTEST_PROFILE,
  closeBrowser, flatAssetHashes,
  rawLaneRecord, runBrowserInit, runBrowserSample, serveBuild,
  validateCensusInventory, validateSourceStateManifest,
} from './run_synctest.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const SAMPLE = PREFIX_SAMPLES;

function parseArgs(argv) {
  const values = {};
  const allowed = new Set(['--browser-runtime', '--assets', '--manifest', '--out', '--playwright']);
  const flags = new Set(['--aurora-staging-shadow-inventory', '--stage-arena-owner-inventory']);
  for (let i = 0; i < argv.length;) {
    const key = argv[i];
    if (flags.has(key)) {
      if (values[key]) throw new Error(`Duplicate ${key}`);
      values[key] = true;
      i++;
      continue;
    }
    const value = argv[i + 1];
    if (!allowed.has(key) || !value || values[key]) {
      throw new Error('Usage: run_write_census.mjs --browser-runtime FILE --assets DIR --manifest FILE --out NEW_DIR [--playwright PACKAGE_DIR] [--aurora-staging-shadow-inventory] [--stage-arena-owner-inventory]');
    }
    values[key] = value;
    i += 2;
  }
  for (const key of ['--browser-runtime', '--assets', '--manifest', '--out']) {
    if (!values[key]) throw new Error(`Missing ${key}`);
    values[key] = path.resolve(values[key]);
  }
  if (values['--playwright']) values['--playwright'] = path.resolve(values['--playwright']);
  if (!fs.statSync(values['--assets']).isDirectory()) throw new Error('Assets path is not a directory');
  for (const key of ['--browser-runtime', '--manifest']) {
    if (!fs.statSync(values[key]).isFile()) throw new Error(`Input is not a file: ${values[key]}`);
  }
  if (fs.existsSync(values['--out'])) throw new Error(`Output path already exists: ${values['--out']}`);
  if (!fs.statSync(path.dirname(values['--out'])).isDirectory()) {
    throw new Error('Output parent directory must already exist');
  }
  return values;
}

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileSha = file => sha256(fs.readFileSync(file));

function sourceHashes() {
  const files = [
    'src/slippi_profile/tests/run_write_census.mjs',
    'src/slippi_profile/tests/run_synctest.mjs',
    'src/slippi_profile/tests/runner_contract.mjs',
    'src/slippi_profile/tests/synctest_browser.cpp',
    'src/native_dat.hpp',
    'src/native_dat.cpp',
    'src/gameplay_world.hpp',
    'src/gameplay_world.cpp',
    'src/gameplay_match_session.hpp',
    'src/gameplay_match_session.cpp',
    'src/slippi_profile/tests/test_write_census_contract.mjs',
    'tests/native_dat_checkpoint_test.cpp',
    'tests/test_native_dat_checkpoint.py',
    'tests/gameplay_snapshot_probe.cpp',
    'tests/gameplay_match_context_trace.c',
    'src/gameplay_match_context.h',
    'src/gameplay_match_context.c',
    'src/gameplay_match_session.hpp',
    'src/gameplay_match_session.cpp',
    'src/gameplay_audio.h',
    'src/gameplay_audio.c',
    'src/gameplay_audio_fx.h',
    'src/gameplay_audio_fx.c',
    'src/gameplay_bootstrap.c',
    'src/gameplay_bootstrap.h',
    'cmake/FighterRuntime.cmake',
    'patches/aurora-browser.patch',
    'scripts/build.py',
    'scripts/browser_tools.mjs',
    'scripts/source_state_manifest.py',
  ];
  return Object.fromEntries(files.map(name => [name, fileSha(path.join(ROOT, name))]));
}

// Reuse the drawn-reference policy: Aurora frame progress through the warm
// prefix (which includes entry/Ready), then positive selected-fighter callback
// and normal-model dispatch deltas at interactive sample 300. No exact count is
// assumed. Status and counter regressions fail at every prefix boundary too.
export function validateCensusDrawSample(result, sample, beforeDiagnostics,
  previousFrame, expectedInputMapping) {
  const frame = validateAuroraFrame(result.status,
    previousFrame.beginFrameId, previousFrame.endFrameId);
  const diagnostics = decodeDrawDiagnostics(Buffer.from(result.drawDiagnostics, 'base64'));
  if (diagnostics.slotCount !== beforeDiagnostics.slotCount || diagnostics.status !== 0) {
    throw new Error('Draw diagnostic slot identity changed or status is nonzero');
  }
  for (const field of ['callbacks', 'normalModelDispatches', 'modelTransforms']) {
    if (diagnostics[field].some((value, slot) => value < beforeDiagnostics[field][slot])) {
      throw new Error(`Draw diagnostic counter regressed: ${field}`);
    }
  }
  let fighters = null;
  if (Buffer.from(result.pad, 'base64').byteLength !== 44) {
    throw new Error(`Browser sample ${sample} did not return 44 PAD bytes`);
  }
  if (sample === SAMPLE) {
    fighters = validateSelectedDrawPath(beforeDiagnostics, diagnostics,
      result.boundary?.players, expectedInputMapping);
    if (!result.boundary?.interactive) {
      throw new Error(`Existing sample ${sample} did not reach the interactive boundary: ${JSON.stringify(result.boundary)}`);
    }
    if (!result.heapCensus || result.heapCensus.sample !== sample ||
        result.heapCensus.audioOwnerInventory?.boundaryOrder?.join('|') !==
          'before-source-step|after-source-step|after-actual-draw-and-Aurora-quiescence' ||
        result.heapCensus.audioOwnerInventory?.boundaries?.length !== 3 ||
        result.heapCensus.audioOwnerInventory.boundaries.some(boundary =>
          ![6, 9].includes(boundary.count) || boundary.rows?.length !== boundary.count) ||
        result.heapCensus.audioOwnerInventory?.stableTopologyValidated !== true ||
        result.heapCensus.audioOwnerInventory?.ownerRestoreAdmission !== false ||
        result.heapCensus.matchContextOwnerInventory?.boundaryOrder?.join('|') !==
          'before-source-step|after-source-step|after-actual-draw-and-Aurora-quiescence' ||
        result.heapCensus.matchContextOwnerInventory?.boundaries?.length !== 3 ||
        result.heapCensus.matchContextOwnerInventory.boundaries.some(boundary =>
          boundary.count !== 1 || boundary.rows?.length !== 1) ||
        result.heapCensus.matchContextOwnerInventory?.stableOwnerLifetimeValidated !== true ||
        result.heapCensus.matchContextOwnerInventory?.ownerRestoreAdmission !== false ||
        result.heapCensus.nativeOutputStorage?.diagnosticOnly !== true ||
        result.heapCensus.nativeOutputStorage?.ownerRestoreAdmission !== false) {
      throw new Error('The requested three-boundary audio owner inventory did not return');
    }
    if (result.heapCensus.requireStageArenaOwnerInventory === true &&
        (result.heapCensus.stageArenaOwnerInventory?.boundaryOrder?.join('|') !==
          'before-source-step|after-source-step|after-actual-draw-and-Aurora-quiescence' ||
         result.heapCensus.stageArenaOwnerInventory?.boundaries?.length !== 3 ||
         result.heapCensus.stageArenaOwnerInventory?.stableOwnerLifetimeValidated !== true ||
         result.heapCensus.stageArenaOwnerInventory?.ownerRestoreAdmission !== false ||
         result.heapCensus.stageArenaOwnerInventory?.completeGameState !== false ||
         result.heapCensus.stageArenaOwnerInventory.boundaries.some(boundary =>
           boundary.rows?.length !== boundary.metadata?.allocationCount ||
           boundary.ownerRestoreAdmission !== false || boundary.completeGameState !== false ||
           boundary.diagnosticOutputStorage?.diagnosticOnly !== true ||
           boundary.diagnosticOutputStorage?.ownerRestoreAdmission !== false))) {
      throw new Error('The requested three-boundary stage-arena owner inventory did not return');
    }
    if (result.heapCensus.requireAuroraStagingShadowInventory === true &&
        (result.heapCensus.auroraStagingShadowOwnerInventory?.boundaryOrder?.join('|') !==
          'before-source-step|after-source-step|after-actual-draw-and-Aurora-quiescence' ||
         result.heapCensus.auroraStagingShadowOwnerInventory?.boundaries?.length !== 3 ||
         result.heapCensus.auroraStagingShadowOwnerInventory?.version !== 5 ||
         result.heapCensus.auroraStagingShadowOwnerInventory?.stableOwnerLifetimeValidated !== true ||
         result.heapCensus.auroraStagingShadowOwnerInventory?.ownerRestoreAdmission !== false ||
         result.heapCensus.auroraStagingShadowOwnerInventory.boundaries.some(boundary =>
           boundary.version !== 5 || !Array.isArray(boundary.rows) ||
           boundary.rows.length !== boundary.frameSlotCount * 5 ||
           !Array.isArray(boundary.ranges) ||
           boundary.rows.some((row, index) => row.frameSlot !== Math.floor(index / 5) ||
             row.streamIndex !== index % 5 || row.kind !== index % 5 + 1 || row.owned !== 1) ||
           boundary.ownerRestoreAdmission !== false ||
           boundary.diagnosticOutputStorage?.diagnosticOnly !== true ||
           boundary.diagnosticOutputStorage?.ownerRestoreAdmission !== false))) {
      throw new Error('The requested three-boundary Aurora staging-shadow inventory did not return');
    }
    const checks = result.heapCensus.nativeTransferQuiescence;
    if (!checks || checks.beforeStep !== 'passed' || checks.afterStep !== 'passed' ||
        checks.afterActualDraw !== 'passed') {
      throw new Error(`Native transfer boundary checks did not all pass: ${JSON.stringify(checks)}`);
    }
  }
  return {frame, diagnostics, fighters};
}

function browserSampleLane(sample) {
  return Object.fromEntries(['observation', 'pcm', 'pad'].map(key =>
    [key, Buffer.from(sample[key], 'base64')]));
}

function screenshotRecord(page, out, name) {
  return page.screenshot({path: path.join(out, name), fullPage: true}).then(bytes => {
    if (bytes.length < 100 || !bytes.subarray(0, 8).equals(
      Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
      throw new Error(`Headless Chrome did not retain a valid ${name} PNG`);
    }
    return {path: name, sha256: sha256(bytes), bytes: bytes.length};
  });
}

function changedSummary(transitions) {
  const summary = {};
  for (const transition of transitions) {
    for (const range of transition.changedRanges) {
      const entry = summary[range.provenance] || {ranges: 0, bytes: 0};
      entry.ranges++;
      entry.bytes += range.bytes;
      summary[range.provenance] = entry;
    }
  }
  return summary;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const requireAuroraStagingShadowInventory =
    options['--aurora-staging-shadow-inventory'] === true;
  const requireStageArenaOwnerInventory =
    options['--stage-arena-owner-inventory'] === true;
  const captureHypothesis = requireStageArenaOwnerInventory ?
    'Attribute source-step changes from exact live audio, match-context, and stage_arena allocation ranges; source-draw changes remain separately classified; no same-browser restore is attempted.' :
    requireAuroraStagingShadowInventory ?
      'Attribute source-step changes from exact live audio, match-context, and Aurora staging-shadow owner ranges, and source-draw changes from the staging-shadow capacities; no same-browser restore is attempted.' :
      'Attribute source-step changes from exact live audio and match-context owner ranges; no same-browser restore is attempted.';
  const runtime = options['--browser-runtime'];
  const wasm = runtime.replace(/\.mjs$/, '.wasm');
  const data = runtime.replace(/\.mjs$/, '.data');
  for (const file of [wasm, data]) {
    if (!fs.statSync(file).isFile()) throw new Error(`Missing browser runtime companion: ${file}`);
  }
  const manifest = JSON.parse(fs.readFileSync(options['--manifest'], 'utf8'));
  const manifestInventory = validateSourceStateManifest(manifest);
  const runtimeHashes = Object.fromEntries([runtime, wasm, data].map(file => [path.basename(file), fileSha(file)]));
  if (manifestInventory.wasmSha256 !== runtimeHashes[path.basename(wasm)]) {
    throw new Error('Source-state manifest does not describe the supplied final browser Wasm');
  }

  fs.mkdirSync(options['--out']);
  const outFile = path.join(options['--out'], 'evidence.json');
  const report = {
    schema: 'melee-web-slippi-browser-write-census-v1',
    result: 'incomplete',
    statement: requireStageArenaOwnerInventory ?
      'One warmed actual browser sample net-byte-delta census with read-only audio, match-context, and single stage_arena allocation provenance at three boundaries; same-value stores and transient changes that return to their prior value before a boundary are not observable. Every range remains provenance only, all changed bytes remain unadmitted, and no memory is restored.' :
      requireAuroraStagingShadowInventory ?
      'One warmed actual browser sample net-byte-delta census with read-only audio, match-context, and Aurora CPU staging-shadow provenance at three boundaries; same-value stores and transient changes that return to their prior value before a boundary are not observable. These ranges and diagnostic output storage are provenance only, not complete game-state ownership or restore admission; every changed range remains unadmitted and no memory is restored.' :
      'One warmed actual browser sample net-byte-delta census with read-only audio and match-context owner-range provenance at three boundaries; same-value stores and transient changes that return to their prior value before a boundary are not observable. These owner ranges and native PCM output storage are diagnostic provenance only, not complete game-state ownership or restore admission; every changed range remains unadmitted and no memory is restored.',
    execution: {status: 'incomplete', prefixSamples: PREFIX_SAMPLES, censusSample: SAMPLE,
      sourceStep: 'existing deterministic snapshot_step input at sample 300',
      captureHypothesis,
      draw: 'existing actual source draw followed by the existing Aurora pending-packet/staging/worker quiescence wait'},
    inventory: {status: 'incomplete', completeGameState: manifestInventory.completeGameState,
      sourceStateManifestComplete: false,
      staticManifestValidation: {status: 'passed', target: manifestInventory.target,
        mutableInputSections: manifestInventory.mutableInputSections,
        sourceCStaticCandidates: manifestInventory.sourceCStaticCandidates,
        excludedMutableInputSections: manifestInventory.excludedMutableInputSections},
      ownerRestoreAdmission: false, arenaInternalOwnershipProven: false,
      audioOwnerRangeInventory: null, nativeOutputStorage: null,
      matchContextOwnerRangeInventory: null,
      auroraStagingShadowOwnerRangeInventory: null,
      stageArenaOwnerRangeInventory: null,
      outsideArenaBytes: null, unadmittedChangedBytes: null,
      unclassifiedChanges: [], classificationSummary: {}},
    profile: {identity: null, sourceIdentity: null, rngHelper: null, stageBridge: null,
      seed: SOURCE_SEED, name: SYNCTEST_PROFILE},
    input: {sample: SAMPLE, rawPad44Base64: null, rawPad44Sha256: null,
      sourceSchedule: 'tests/gameplay_snapshot_probe.cpp::melee_web_snapshot_step; samples 0–299 warm the fixture, sample 300 is the first non-neutral deterministic input'},
    runtimeSha256: runtimeHashes,
    sourceStateManifest: {path: path.basename(options['--manifest']), sha256: fileSha(options['--manifest']),
      configurationIdentitySha256: manifestInventory.configurationIdentitySha256,
      wasmSha256: manifestInventory.wasmSha256,
      rowCount: manifestInventory.mutableInputSections,
      sourceCStaticCandidates: manifestInventory.sourceCStaticCandidates,
      excludedMutableInputSections: manifestInventory.excludedMutableInputSections,
      completeGameState: manifestInventory.completeGameState},
    harnessSha256: sourceHashes(),
    assetSha256: flatAssetHashes(options['--assets']),
    browser: null,
    browser_process_info: [],
    browser_process_info_source: null,
    initialization: null,
    warmPrefix: {samplesCompleted: 0, consumedInputs: [],
      checkpointSample299: null, screenshotSample299: null, drawSamples: []},
    census: null,
    cleanup: null,
    failure: null,
  };
  const writeReport = () => fs.writeFileSync(outFile, `${JSON.stringify(report, null, 2)}\n`);
  writeReport();

  let server = null, browserServer = null, browserProcess = null;
  let browser = null, browserCdp = null, context = null, page = null;
  let browserProcessPid = null, profileDirectory = null;
  const ownedChromePids = new Set();
  const pageErrors = [];
  try {
    const files = [path.basename(runtime), path.basename(wasm), path.basename(data)];
    const serving = await serveBuild(path.dirname(runtime), files);
    server = serving.server;
    const tools = await loadBrowserTools(options['--playwright']);
    const launch = browserLaunchOptions({executablePath: tools.browserPath},
      {headed: false, audible: false, timeout: 30000});
    const ownedBrowser = await launchOwnedChromeServer(tools.chromium, launch);
    browserServer = ownedBrowser.server;
    browserProcess = ownedBrowser.child;
    browserProcessPid = browserProcess.pid;
    ownedChromePids.add(browserProcessPid);
    report.browser_process_info = [{id: browserProcessPid, type: 'browser'}];
    report.browser_process_info_source = 'Playwright-owned Chrome child process (provisional until CDP inventory)';
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
    browser = await tools.chromium.connect(browserServer.wsEndpoint());
    browserCdp = await browser.newBrowserCDPSession();
    const processInfo = await browserCdp.send('SystemInfo.getProcessInfo');
    const attributed = recordBrowserProcessInfo(report, processInfo.processInfo);
    for (const pid of attributed.processIds) ownedChromePids.add(pid);
    context = await browser.newContext();
    page = await context.newPage();
    page.setDefaultTimeout(120000);
    page.on('pageerror', error => pageErrors.push(String(error)));
    page.on('console', message => {
      if (message.type() === 'error') pageErrors.push(message.text());
    });
    await page.goto(serving.origin, {waitUntil: 'domcontentloaded'});
    const init = await runBrowserInit(page, serving.origin, path.basename(runtime));
    report.browser = {
      name: 'headless installed Chrome through scripts/browser_tools.mjs',
      executable: tools.browserPath,
      version: browser.version(),
      webgpuAvailable: init.featureAvailable,
      backend: init.backend.backend,
      canvas: '640x480 browser surface',
      audioOutput: 'muted; native PCM is not connected to Web Audio',
      ownedProcessPid: browserProcessPid,
      userDataDirectory: profileDirectory,
      profileTempRoot: tempRoot,
      commandLine: browserProcess.spawnargs,
      cdpProcessInventory: report.browser_process_info,
    };
    const initializer = JSON.parse(init.initializer);
    const drawDiagnostics = decodeDrawDiagnostics(Buffer.from(init.drawDiagnostics, 'base64'));
    assertDrawDiagnosticsClear(drawDiagnostics, 'browser initialization');
    report.profile = {identity: init.profile, sourceIdentity: init.source,
      rngHelper: init.rngHelper, stageBridge: init.stageBridge, seed: SOURCE_SEED,
      name: SYNCTEST_PROFILE};
    if (init.profile !== SYNCTEST_PROFILE) throw new Error(`Unexpected profile identity: ${init.profile}`);
    if (init.source !== fileSha(path.join(ROOT, 'tests/gameplay_snapshot_probe.cpp'))) {
      throw new Error('Runtime fixture identity differs from current gameplay_snapshot_probe.cpp');
    }
    if (initializer.profile !== 0 || initializer.seed !== SOURCE_SEED ||
        !Array.isArray(initializer.players) || initializer.players.length !== 2 ||
        initializer.players.some(player => player.stocks !== 4)) {
      throw new Error(`Unexpected fixture initializer: ${JSON.stringify(initializer)}`);
    }
    const expectedInputMapping = validateSourceInputMapping(
      initializer.players.map((player, sourceSlot) => ({
        sourceSlot, initializerPort: player.port, rawController: player.controller,
      })), drawDiagnostics.slotCount);
    const expectedStocks = initializer.players.map(player => player.stocks);
    report.initialization = {
      exactProfileSeedAndInitializer: true,
      initializer,
      sourceInputMapping: expectedInputMapping,
      sourceInputMappingEvidence: 'same mapping validator as run_synctest.mjs, applied to copied start_data initializer',
      drawDiagnostics,
      browserBackend: init.backend,
      initialObservationSha256: sha256(Buffer.from(init.initial.observation, 'base64')),
      initialPcmSha256: sha256(Buffer.from(init.initial.pcm, 'base64')),
      initialRawPad44Sha256: sha256(Buffer.from(init.initial.pad, 'base64')),
    };
    writeReport();

    let previousFrame = {beginFrameId: init.backend.lastBeginFrameId || 0,
      endFrameId: init.backend.lastEndFrameId || 0};
    let previousDiagnostics = drawDiagnostics;
    let checkpoint = null;
    for (let sample = 0; sample < PREFIX_SAMPLES; sample++) {
      const result = await runBrowserSample(page, sample, expectedInputMapping, expectedStocks);
      const validated = validateCensusDrawSample(result, sample, previousDiagnostics,
        previousFrame, expectedInputMapping);
      previousFrame = validated.frame;
      previousDiagnostics = validated.diagnostics;
      report.warmPrefix.drawSamples.push({sample,
        sourceFrame: result.boundary.sourceFrame, drawDiagnostics: validated.diagnostics,
        gpuFrame: validated.frame});
      const consumedPad = Buffer.from(result.pad, 'base64');
      report.warmPrefix.consumedInputs.push({sample, rawPad44Base64: result.pad,
        rawPad44Sha256: sha256(consumedPad)});
      report.warmPrefix.samplesCompleted = sample + 1;
      writeReport();
      if (sample === PREFIX_SAMPLES - 1) {
        checkpoint = result;
        report.warmPrefix.checkpointSample299 = rawLaneRecord(browserSampleLane(result));
        report.warmPrefix.checkpointBoundary = result.boundary;
        report.warmPrefix.checkpointGpuStatus = result.status;
        report.warmPrefix.screenshotSample299 = await screenshotRecord(
          page, options['--out'], 'browser-warm-prefix-sample-299.png');
        writeReport();
      }
    }
    if (!checkpoint) throw new Error('The existing 300-sample browser warm prefix did not complete');

    const arenaRange = await page.evaluate(() => {
      const {module} = window.__slippiSynctest;
      return {base: module._melee_web_synctest_gameplay_arena_base(),
        bytes: module._melee_web_synctest_gameplay_arena_bytes()};
    });
    const inventoryCounts = validateCensusInventory(manifest, arenaRange);
    report.inventory.liveArenaValidation = {
      status: 'passed', base: arenaRange.base, bytes: arenaRange.bytes,
      disjointFromManifest: true,
    };
    writeReport();
    const censusPlan = {
      arenaRange,
      manifestRows: manifest.mutable_input_sections,
      requireAudioInventory: true,
      requireMatchContextInventory: true,
      requireAuroraStagingShadowInventory,
      requireStageArenaOwnerInventory,
    };
    const sample = await runBrowserSample(page, SAMPLE, expectedInputMapping, expectedStocks, censusPlan);
    const censusDraw = validateCensusDrawSample(sample, SAMPLE, previousDiagnostics,
      previousFrame, expectedInputMapping);
    report.input = {sample: SAMPLE,
      rawPad44Base64: sample.pad,
      rawPad44Sha256: sha256(Buffer.from(sample.pad, 'base64')),
      sourceSchedule: 'tests/gameplay_snapshot_probe.cpp::melee_web_snapshot_step; samples 0–299 warm the fixture, sample 300 is the first non-neutral deterministic input'};
    const transitions = [sample.heapCensus.sourceStep, sample.heapCensus.actualDrawAndQuiescence];
    const summary = changedSummary(transitions);
    const unclassified = transitions.flatMap(transition => transition.changedRanges
      .filter(range => range.provenance === 'unmapped' || range.provenance === 'ambiguous-overlap')
      .map(range => ({transition: transition === transitions[0] ? 'source-step' : 'actual-draw-and-quiescence', ...range})));
    const outsideArenaBytes = transitions.reduce((total, transition) => total +
      transition.changedRanges.filter(range => range.provenance !== 'gameplay-arena')
        .reduce((sum, range) => sum + range.bytes, 0), 0);
    const unadmittedChangedBytes = transitions.reduce((total, transition) => total +
      transition.changedRanges.reduce((sum, range) => sum + range.bytes, 0), 0);
    report.census = {...sample.heapCensus,
      drawDiagnostics: censusDraw.diagnostics,
      selectedFighterPaths: censusDraw.fighters,
      inventoryCounts,
      transitionOrder: ['before-source-step', 'after-source-step', 'after-actual-draw-and-Aurora-quiescence'],
      transitionByteCountBasis: 'sum of net-changed bytes per transition; the same address can count in both transitions',
      nativeTransferQuiescenceContract: {
        api: 'melee_web_snapshot_quiescent',
        condition: 'DVDGetDriveStatus() == DVD_STATE_END',
        transientTransferLimitation: 'Passing boundary checks do not exclude a transfer that began and completed between sampled boundaries.',
      },
      classificationSummary: summary,
      ownerRestoreAdmission: false,
      arenaInternalOwnershipProven: false,
      unclassifiedChanges: unclassified,
      unadmittedChanges: transitions.flatMap(transition => transition.changedRanges
        .map(range => ({transition: transition === transitions[0] ? 'source-step' : 'actual-draw-and-quiescence', ...range}))),
      outsideArenaChanges: transitions.flatMap(transition => transition.changedRanges
        .filter(range => range.provenance !== 'gameplay-arena')
        .map(range => ({transition: transition === transitions[0] ? 'source-step' : 'actual-draw-and-quiescence', ...range}))),
      observation: rawLaneRecord(browserSampleLane(sample)),
      boundary: sample.boundary,
      gpuStatus: sample.status,
      screenshot: await screenshotRecord(page, options['--out'], 'browser-census-sample-300.png'),
    };
    report.inventory = {
      status: 'incomplete',
      completeGameState: false,
      sourceStateManifestComplete: false,
      ownerRestoreAdmission: false,
      arenaInternalOwnershipProven: false,
      audioOwnerRangeInventory: sample.heapCensus.audioOwnerInventory,
      matchContextOwnerRangeInventory: sample.heapCensus.matchContextOwnerInventory,
      auroraStagingShadowOwnerRangeInventory:
        sample.heapCensus.auroraStagingShadowOwnerInventory,
      stageArenaOwnerRangeInventory: sample.heapCensus.stageArenaOwnerInventory,
      nativeOutputStorage: sample.heapCensus.nativeOutputStorage,
      outsideArenaBytes,
      unadmittedChangedBytes,
      unclassifiedBytes: unclassified.reduce((sum, range) => sum + range.bytes, 0),
      unclassifiedChanges: unclassified,
      classificationSummary: summary,
      incompleteReasons: [
        'The source-state manifest covers generated source C static candidates only.',
        'Source-static candidates, excluded sections and unmapped writes have no owner restore contract established by this census.',
        'The live gameplay arena export proves only its address extent; its internal object/state ownership and restore admission remain unproven.',
        'The audio owner-range inventory attributes exact allocations to one live audio lifetime; it is provenance only and does not establish complete audio or game-state restore admission.',
        'The match-context owner range attributes one exact live allocation within its world generation and owner epoch; it is provenance only and does not establish complete game-state restore admission.',
        ...(requireStageArenaOwnerInventory ? [
          'The exact direct stage_arena NativeDat ledger is provenance only and does not imply source-read coverage or restore admission.',
          'The 96-byte observer is an explicitly excluded diagnostic allocation; no observer bytes are restored.',
        ] : []),
        ...(requireAuroraStagingShadowInventory ? [
          'Aurora staging-shadow ranges attribute exact CPU backing capacities across one unchanged renderer generation; this does not establish source-read independence or restore admission.',
          'Aurora descriptor output storage is retained as a diagnostic and excluded range.',
        ] : []),
        'Native PCM output storage is retained as an explicitly diagnostic and excluded range.',
        'Native DVD STATE_END checks passed at each sampled boundary but cannot exclude a transfer that completed between boundaries.',
      ],
    };
    report.execution = {status: 'completed', prefixSamples: PREFIX_SAMPLES,
      censusSample: SAMPLE, sampleInteractive: true,
      sourceStep: 'existing deterministic snapshot_step input at sample 300',
      captureHypothesis,
      draw: 'existing actual source draw followed by the existing Aurora pending-packet/staging/worker quiescence wait',
      restorePerformed: false};
    report.result = 'incomplete';
    if (pageErrors.length) throw new Error(`Browser reported an error: ${pageErrors[0]}`);
    report.browser.pageErrors = pageErrors;
    writeReport();
  } catch (error) {
    report.result = 'failed';
    report.execution.status = 'failed';
    report.failure = {message: String(error), stack: error?.stack || null};
    writeReport();
    throw error;
  } finally {
    const cleanupFailures = [];
    if (page) {
      try {
        const closed = await closeBrowser(page);
        if (!closed?.closed) cleanupFailures.push({step: 'renderer-close', error: JSON.stringify(closed)});
        report.cleanup = {...(report.cleanup || {}), renderer: closed};
      } catch (error) { cleanupFailures.push({step: 'renderer-close', error: String(error)}); }
    }
    if (browserCdp) {
      try {
        const finalInventory = await browserCdp.send('SystemInfo.getProcessInfo');
        const attributed = recordBrowserProcessInfo(report, finalInventory.processInfo);
        for (const pid of attributed.processIds) ownedChromePids.add(pid);
        report.browser = {...(report.browser || {}), finalCdpProcessInventory: report.browser_process_info};
        writeReport();
      } catch (error) { cleanupFailures.push({step: 'chrome-final-process-inventory', error: String(error)}); }
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
          if (processStates[pid]) cleanupFailures.push({step: 'owned-chrome-process-absence', error: `owned PID ${pid} still appears present`});
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
      try { await new Promise((resolve, reject) => server.close(error => error ? reject(error) : resolve())); }
      catch (error) { cleanupFailures.push({step: 'http-server-close', error: String(error)}); }
    }
    if (applyCleanupVerdict(report, cleanupFailures) === 'failed') process.exitCode = 1;
    writeReport();
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    process.stderr.write(`Slippi browser write census failed: ${error?.stack || error}\n`);
    process.exitCode = 1;
  });
}
