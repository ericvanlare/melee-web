// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only Wasm-memory write census for one warmed, actually drawn Slippi
// fixture sample. This records net byte-value deltas, not stores or a restore.
import crypto from 'node:crypto';
import {execFileSync} from 'node:child_process';
import {validateCpuRestoreConfiguration,runBrowserCpuRestore} from './cpu_restore_runner.mjs';
import {runBrowserCpuTwoTick,validateCpuTwoTickFinalEvidence} from './cpu_two_tick_runner.mjs';
import {runBrowserCpuSevenTick,validateCpuSevenTickFinalEvidence,CPU_SEVEN_TICK_SAMPLES} from './cpu_seven_tick_runner.mjs';
import {runBrowserCpuCorrectedSeven} from './cpu_corrected_runner.mjs';
import {runBrowserCpuCorrectedTwoWindow} from './cpu_corrected_two_window_runner.mjs';
import {runBrowserCpuCorrectedLoop} from './cpu_corrected_loop_runner.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {browserLaunchOptions, loadBrowserTools} from '../../../scripts/browser_tools.mjs';
import {
  applyCleanupVerdict, assertDrawDiagnosticsClear, checkedAsyncCcall, inspectInteractiveObservation,
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
const LOGICAL_FRAME_V6_PROFILE = 'slippi-render-dependencies-logical-frame-v6';
const LOBJ_PRESENTATION_V7_PROFILE = 'slippi-render-dependencies-logical-frame-lobj-v7';
// The historical TObj option now identifies stateless canonical matrix presentation.
const LOBJ_CURRENT_SET_V15_PROFILE = 'slippi-render-dependencies-logical-frame-lobj-tobj-current-light-set-v15';
const TEV_STAGE_SCRATCH_V19_PROFILE = 'slippi-render-dependencies-logical-frame-lobj-tobj-tev-stage-scratch-v19';

export function assembleCompletedCensusExecution(report, {prefixSamples, censusSample,
  captureHypothesis}) {
  const admittedPayload = report.execution?.payloadAdmissionDryRun;
  const execution = {status: 'completed', prefixSamples, censusSample,
    sampleInteractive: true,
    sourceStep: 'existing deterministic snapshot_step input at sample 300',
    captureHypothesis,
    draw: 'existing actual source draw followed by the existing Aurora pending-packet/staging/worker quiescence wait',
    restorePerformed: false};
  if (admittedPayload?.enabled === true) execution.payloadAdmissionDryRun = admittedPayload;
  report.execution = execution;
  report.result = 'incomplete';
  return report;
}

export function recordFailedCensusExecution(report, error) {
  const partial = error?.payloadAdmissionPartial;
  if (partial) {
    if (partial.payloadAdmission) {
      report.execution.payloadAdmissionDryRun = {...partial.payloadAdmission,
        enabled: true, restoreCalled: false, completeGameState: false};
    }
    report.execution.payloadAdmissionPartial = {
      status: partial.status,
      checkpoint: partial.checkpoint,
      commandSequence: partial.commandSequence,
      boundaries: partial.boundaries,
      sourceStep: partial.sourceStep,
      actualDraw: partial.actualDraw,
      sample300: partial.sample300,
      heapCensus: partial.heapCensus,
      failure: partial.failure,
    };
  }
  if (error?.payloadAdmissionPartialReadError) {
    report.execution.payloadAdmissionPartialReadError = error.payloadAdmissionPartialReadError;
  }
  report.result = 'failed';
  report.execution.status = 'failed';
  report.failure = {message: String(error), stack: error?.stack || null};
  return report;
}

export async function recordCensusFailurePageState(report, page) {
  if (!page) return;
  try {
    // Read the existing JS log buffer even when native initialization aborted.
    // Do not call the failed Wasm instance or advance the source to diagnose it.
    report.browserPageState = await page.evaluate(() => ({
      title: document.title,
      logs: window.__slippiSynctest?.logs ?? null,
    }));
  } catch (error) {
    report.browserPageStateError = String(error);
  }
}

export async function recordClassPieceSlabTrace(report, page) {
  let trace = {status: 'unavailable', reason: 'page unavailable'};
  if (page) {
    try {
      // Native close may emit EARLY_CLOSE. Read its existing stdout buffer after
      // close, without calling Wasm or turning diagnostics into browser errors.
      trace = await page.evaluate(() => {
        const logs = window.__slippiSynctest?.logs;
        if (!Array.isArray(logs)) return {status: 'unavailable', reason: 'native log buffer unavailable'};
        const prefix = 'MELEE_CLASS_PIECE_SLAB_TRACE ';
        const lineLimit = 64, perLineLimit = 4096;
        const lines = [];
        let matchedLineCount = 0, truncated = false;
        for (const row of logs) {
          if (row?.stream !== 'stdout' || typeof row.line !== 'string' || !row.line.startsWith(prefix)) continue;
          matchedLineCount++;
          if (lines.length === lineLimit) { truncated = true; continue; }
          if (row.line.length > perLineLimit) truncated = true;
          lines.push(row.line.slice(0, perLineLimit));
        }
        return {status: truncated ? 'truncated' : lines.length ? 'retained' : 'no-output',
          matchedLineCount, lineLimit, perLineLimit, lines};
      });
    } catch (error) { trace = {status: 'unavailable', reason: String(error)}; }
  }
  // Retention alone does not establish a complete or joined native trace.
  report.nativeDiagnostics = {...report.nativeDiagnostics, classPieceSlabTrace: trace};
}

export function selectTobjFailureStatics(manifest) {
  // V9 keeps only bounded diagnostic scalars; never follow former owner arrays.
  return [['first_failure', 4], ['first_failure_line', 4],
    ['first_failure_detail', 4], ['owner_world', 8], ['active_token', 8]].map(([name, size]) => {
    const rows = manifest.mutable_input_sections.filter(row =>
      row.source === 'src/slippi_profile/tobj_presentation_cache.c' &&
      row.input_object === 'libhsd_native_runtime.a(tobj_presentation_cache.c.o)' &&
      row.input_section === `.bss.${name}`);
    if (rows.length !== 1 || rows[0].size !== size ||
        !Number.isSafeInteger(rows[0].address) || rows[0].address <= 0 ||
        rows[0].address + size > 0x100000000) {
      throw new Error(`V9 stateless TObj failure diagnostic manifest row refused: ${name}`);
    }
    return {name, address: rows[0].address, size};
  });
}

export async function recordTobjFailureStatics(report, page, rows) {
  if (!page || !rows) return;
  try {
    // The caller has matched this manifest/map to the exact linked Wasm.
    // Copy only V9's named scalar bytes; never follow pointers or call native code.
    report.tobjFailureStatics = await page.evaluate(selected => {
      const heap = window.__slippiSynctest?.module?.HEAPU8;
      if (!(heap instanceof Uint8Array)) throw new Error('Wasm memory unavailable after failure');
      return selected.map(row => {
        if (row.address + row.size > heap.byteLength) {
          throw new Error(`V9 stateless TObj diagnostic outside current Wasm memory: ${row.name}`);
        }
        return {...row, bytes: Array.from(heap.slice(row.address, row.address + row.size))};
      });
    }, rows);
  } catch (error) {
    report.tobjFailureStaticsError = String(error);
  }
}

// Read the first refused effect before teardown changes the interval owner.
// This is diagnostic only: no native operation or memory mutation is performed.
export function selectCpuIntervalFailureStatics(manifest) {
  return [['failure',4],['epoch',8]].map(([name,size])=>{
    const rows=manifest.mutable_input_sections.filter(row=>
      row.source==='src/gameplay_cpu_interval.c'&&
      row.input_object==='libgameplay_runtime.a(gameplay_cpu_interval.c.o)'&&
      row.input_section===`.bss.${name}`);
    if(rows.length!==1||rows[0].size!==size||
       !Number.isSafeInteger(rows[0].address)||rows[0].address<=0||
       rows[0].address+size>0x100000000)
      throw new Error(`CPU interval failure diagnostic manifest row refused: ${name}`);
    return {name,address:rows[0].address,size};
  });
}

export async function recordCpuIntervalFailureStatics(report,page,rows,lane) {
  if(!page||!rows)return;
  report.cpuIntervalFailureStatics??={};
  try {
    report.cpuIntervalFailureStatics[lane]=await page.evaluate(selected=>{
      const heap=window.__slippiSynctest?.module?.HEAPU8;
      if(!(heap instanceof Uint8Array))throw new Error('Wasm memory unavailable after interval failure');
      return selected.map(row=>{
        if(row.address+row.size>heap.byteLength)
          throw new Error(`CPU interval diagnostic outside current Wasm memory: ${row.name}`);
        return {...row,bytes:Array.from(heap.slice(row.address,row.address+row.size))};
      });
    },rows);
  }catch(error){report.cpuIntervalFailureStatics[lane]={error:String(error)};}
}

export function validateCpuRestoreRouteSelection(values) {
  const oneTick = values['--aggregate-cpu-drawn-reference'] === true;
  const twoTick = values['--aggregate-cpu-drawn-reference-two-tick'] === true;
  const sevenTick = values['--aggregate-cpu-drawn-reference-seven-tick'] === true;
  const corrected = values['--correct-predicted-branch'] === true;
  const correctedTwoWindow = values['--correct-predicted-branch-two-window'] === true;
  const correctedLoop = values['--correct-predicted-branch-loop'] === true ||
    values['--correct-predicted-branch-loop-packets'] === true;
  const correctedLoopPackets = values['--correct-predicted-branch-loop-packets'] === true;
  const particle = values['--slippi-particle-presentation-cache'] === true;
  const fixedCpuRouteCount = Number(oneTick) + Number(twoTick) + Number(sevenTick);
  if (fixedCpuRouteCount > 1) throw new Error('Fixed one-, two-, and seven-tick drawn-reference routes are mutually exclusive');
  const multiTick = twoTick || sevenTick;
  if (sevenTick && !particle) {
    throw new Error('The seven-tick route requires the private v19 TEV stage scratch profile');
  }
  if ((corrected || correctedTwoWindow || correctedLoop) && !sevenTick) throw new Error('Corrected prediction requires the fixed seven-tick-capable preparation route');
  if(values['--correct-predicted-branch-loop']===true&&correctedLoopPackets)
    throw new Error('Local and packet-backed reusable-loop routes are mutually exclusive');
  if (Number(corrected)+Number(correctedTwoWindow)+Number(correctedLoop)>1) throw new Error('One-window, two-window and reusable-loop correction routes are mutually exclusive');
  if (particle && (values['--slippi-tobj-presentation-cache'] !== true ||
      values['--slippi-lobj-presentation-cache'] !== true || !multiTick)) {
    throw new Error('The v19 TEV stage scratch lane requires v15 TObj, v7 LObj, and a fixed two- or seven-tick route');
  }
  if (values['--slippi-tobj-presentation-cache'] === true &&
      (values['--slippi-lobj-presentation-cache'] !== true || !multiTick)) {
    throw new Error('The v15 current light set lane requires v7 and a fixed two- or seven-tick route');
  }
  if (values['--slippi-lobj-presentation-cache'] === true && !multiTick) {
    throw new Error('The v7 light owner requires a fixed two- or seven-tick route');
  }
  if ((oneTick || twoTick || sevenTick) && !values['--aggregate-cpu-restore-config']) {
    throw new Error('Drawn reference requires CPU configuration');
  }
  return correctedLoopPackets ? 'independent-browser-correction-loop-packets-v1' : correctedLoop ? 'independent-browser-correction-loop-v1' : correctedTwoWindow ? 'independent-browser-correction-two-window-v1' : corrected ? 'independent-browser-correction-v1' : sevenTick ? 'drawn-reference-no-draw-seven-tick-v1' : twoTick ? 'drawn-reference-no-draw-two-tick-v1' : oneTick ? 'drawn-reference-one-tick-v1' :
    values['--aggregate-cpu-restore-config'] ? 'cpu-only-v1' : 'census-v1';
}

export function validateBrowserProfileForRoute(init, twoTickDrawnReference, twoTickExports = null,
  lobjPresentationCache = false, tobjPresentationCache = false, particlePresentationCache = false,
  sevenTickDrawnReference = false, sevenTickExports = null, reuseExports = null) {
  const multiTick = twoTickDrawnReference || sevenTickDrawnReference;
  if (lobjPresentationCache && !multiTick) throw new Error('The v7 light owner requires two or seven ticks');
  if(tobjPresentationCache && (!lobjPresentationCache || !multiTick)) throw new Error('The v15 current light set lane requires v7 and two or seven ticks');
  if (particlePresentationCache && (!tobjPresentationCache || !lobjPresentationCache || !multiTick)) {
    throw new Error('The v19 TEV stage scratch lane requires v15 TObj, v7 LObj, and two or seven ticks');
  }
  if (sevenTickDrawnReference && !particlePresentationCache) {
    throw new Error('The seven-tick route requires the private v19 TEV stage scratch profile');
  }
  const expectedProfile = particlePresentationCache ? TEV_STAGE_SCRATCH_V19_PROFILE :
    tobjPresentationCache ? LOBJ_CURRENT_SET_V15_PROFILE : lobjPresentationCache ? LOBJ_PRESENTATION_V7_PROFILE :
    twoTickDrawnReference ? LOGICAL_FRAME_V6_PROFILE : SYNCTEST_PROFILE;
  if (init?.profile !== expectedProfile) throw new Error(`Unexpected profile identity: ${init?.profile}`);
  if (twoTickDrawnReference && (!twoTickExports ||
      twoTickExports.evidence !== true || twoTickExports.evidenceBytes !== true ||
      twoTickExports.payloadWitness !== true || twoTickExports.payloadWitnessBytes !== true)) {
    throw new Error('Two-tick evidence exports are unavailable for the linked v6 profile');
  }
  if (sevenTickDrawnReference && (!sevenTickExports ||
      sevenTickExports.evidence !== true || sevenTickExports.evidenceBytes !== true)) {
    throw new Error('Seven-tick evidence exports are unavailable for the linked v19 profile');
  }
  if (reuseExports !== null && (!sevenTickDrawnReference || reuseExports.enable !== true ||
      reuseExports.evidence !== true || reuseExports.evidenceBytes !== true)) {
    throw new Error('Two-window reuse exports are unavailable for the linked v19 profile');
  }
  return expectedProfile;
}

export function parseArgs(argv) {
  const values = {};
  const allowed = new Set(['--browser-runtime', '--assets', '--manifest', '--out', '--playwright','--aggregate-cpu-restore-config']);
  const flags = new Set(['--aurora-staging-shadow-inventory', '--stage-arena-owner-inventory',
    '--source-payload-admission-dry-run','--aggregate-cpu-drawn-reference',
    '--aggregate-cpu-drawn-reference-two-tick','--aggregate-cpu-drawn-reference-seven-tick', '--slippi-lobj-presentation-cache',
    '--slippi-tobj-presentation-cache', '--slippi-particle-presentation-cache', '--correct-predicted-branch',
    '--correct-predicted-branch-two-window','--correct-predicted-branch-loop',
    '--correct-predicted-branch-loop-packets']);
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
      throw new Error('Usage: run_write_census.mjs --browser-runtime FILE --assets DIR --manifest FILE --out NEW_DIR [--playwright PACKAGE_DIR] [--aurora-staging-shadow-inventory] [--stage-arena-owner-inventory] [--aggregate-cpu-drawn-reference-two-tick | --aggregate-cpu-drawn-reference-seven-tick] [--correct-predicted-branch | --correct-predicted-branch-two-window | --correct-predicted-branch-loop | --correct-predicted-branch-loop-packets] [--slippi-lobj-presentation-cache --slippi-tobj-presentation-cache --slippi-particle-presentation-cache]');
    }
    values[key] = value;
    i += 2;
  }
  for (const key of ['--browser-runtime', '--assets', '--manifest', '--out']) {
    if (!values[key]) throw new Error(`Missing ${key}`);
    values[key] = path.resolve(values[key]);
  }
  if (values['--playwright']) values['--playwright'] = path.resolve(values['--playwright']);
  validateCpuRestoreRouteSelection(values);
  if(values['--aggregate-cpu-restore-config']){
    values['--aggregate-cpu-restore-config']=path.resolve(values['--aggregate-cpu-restore-config']);
    if(values['--source-payload-admission-dry-run']||!values['--aurora-staging-shadow-inventory'])
      throw new Error('CPU restore is mutually exclusive with payload dry run and requires the private Aurora diagnostic');
    if(!fs.statSync(values['--aggregate-cpu-restore-config']).isFile())throw new Error('CPU configuration file required');
  }
  if (values['--source-payload-admission-dry-run'] &&
      (!values['--aurora-staging-shadow-inventory'] || !values['--stage-arena-owner-inventory'])) {
    throw new Error('Source-payload admission dry run requires both exact private Aurora and stage owner diagnostics');
  }
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

// Each interval is copied once, before native retirement. A partial file is
// retained on failure; the final name becomes visible only after a complete
// write. Exclusive linking refuses to replace prior evidence at that index.
export function createCpuIntervalEvidenceSink(outputDirectory) {
  const directory=path.join(outputDirectory,'intervals');
  fs.mkdirSync(directory);
  return async (index,interval) => {
    if(!Number.isSafeInteger(index)||index<0||index>0xffff_ffff)
      throw new Error('CPU interval evidence index is invalid');
    const name=`${String(index).padStart(4,'0')}-evidence.json`;
    const final=path.join(directory,name),partial=`${final}.partial`;
    const bytes=Buffer.from(`${JSON.stringify(interval)}\n`);
    const fd=fs.openSync(partial,'wx');
    try { fs.writeFileSync(fd,bytes);fs.fsyncSync(fd); } finally { fs.closeSync(fd); }
    fs.linkSync(partial,final);
    fs.unlinkSync(partial);
    return {path:`intervals/${name}`,sha256:sha256(bytes),bytes:bytes.length};
  };
}

export function validateCpuLoopExports(value) {
  if(!value||value.enable!==true||value.selectSpan!==true||
     value.evidence!==true||value.evidenceBytes!==true)
    throw new Error('Reusable-loop exports are unavailable for the linked private profile');
  return value;
}

function readCpuLoopExportsInPage() {
  const {module}=window.__slippiSynctest;
  return {enable:typeof module._melee_web_synctest_cpu_corrected_loop_enable==='function',
    selectSpan:typeof module._melee_web_synctest_cpu_corrected_loop_select_span==='function',
    evidence:typeof module._melee_web_synctest_cpu_corrected_loop_evidence==='function',
    evidenceBytes:typeof module._melee_web_synctest_cpu_corrected_loop_evidence_bytes==='function'};
}
function addPrefixSignature(hash, sample, result) {
  const index=Buffer.alloc(4);index.writeUInt32LE(sample,0);hash.update(index);
  for(const key of ['observation','pcm','pad','drawDiagnostics']){
    const bytes=Buffer.from(result[key],'base64');
    const length=Buffer.alloc(4);length.writeUInt32LE(bytes.length,0);hash.update(length).update(bytes);
  }
}

function sourceHashes({packetInput = false} = {}) {
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
    'tests/test_gameplay_cpu_checkpoint.py',
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
    'src/slippi_profile/tests/cpu_restore_runner.mjs',
    'src/slippi_profile/tests/cpu_two_tick_runner.mjs',
    'src/slippi_profile/tests/test_cpu_two_tick_runner.mjs',
    'src/slippi_profile/tests/cpu_seven_tick_runner.mjs',
    'src/slippi_profile/tests/test_cpu_seven_tick_runner.mjs',
    'src/slippi_profile/tests/cpu_restore_adapter.inc',
    'src/slippi_profile/tests/cpu_corrected_adapter.inc',
    'src/slippi_profile/tests/cpu_corrected_runner.mjs',
    'src/slippi_profile/tests/cpu_corrected_two_window_runner.mjs',
    'src/slippi_profile/tests/cpu_corrected_loop_runner.mjs',
    'src/slippi_profile/tests/test_cpu_loop_census.mjs',
    'src/slippi_profile/tests/test_cpu_corrected_two_window_runner.mjs',
    'src/slippi_profile/tests/cpu_endpoint.mjs',
    'src/slippi_profile/tests/test_cpu_endpoint.mjs',
    'src/gameplay_cpu_endpoint.cpp','src/gameplay_cpu_endpoint.hpp',
    'scripts/net_rollback_input_timeline.mjs',
    'scripts/net_lockstep_core.mjs',
    'src/gameplay_cpu_checkpoint.cpp','src/gameplay_cpu_commit.cpp','src/gameplay_cpu_statics.cpp','src/gameplay_cpu_interval.c',
  ];
  if(packetInput)files.push(
    'src/slippi_profile/tests/cpu_corrected_loop_packet_input.mjs',
    'scripts/net_lockstep_protocol.mjs',
    'scripts/net_lockstep_transport.mjs',
    'scripts/net_lockstep_relay.mjs',
    'scripts/net_lockstep_websocket_relay.mjs',
    'scripts/net_room_relay_runtime_owner.mjs',
    'scripts/net_room_relay_direct_runtime.mjs',
    'scripts/net_room_relay_miniflare_options.mjs');
  return Object.fromEntries(files.map(name => [name, fileSha(path.join(ROOT, name))]));
}

// Reuse the drawn-reference policy: Aurora frame progress through the warm
// prefix (which includes entry/Ready), then positive selected-fighter callback
// and normal-model dispatch deltas at interactive sample 300. No exact count is
// assumed. Status and counter regressions fail at every prefix boundary too.
export function validateDrawSampleBoundary(result, sample, beforeDiagnostics, previousFrame) {
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
  if (Buffer.from(result.pad, 'base64').byteLength !== 44) {
    throw new Error(`Browser sample ${sample} did not return 44 PAD bytes`);
  }
  return {frame, diagnostics};
}

export function validateCensusDrawSample(result, sample, beforeDiagnostics,
  previousFrame, expectedInputMapping) {
  const {frame, diagnostics}=validateDrawSampleBoundary(result,sample,beforeDiagnostics,previousFrame);
  let fighters = null;
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

// Save the complete returned native sequence before outer validation or screenshot.
export function retainCpuRestoreSample(report,sample) {
  report.cpuRawSample=sample;
  report.execution.cpuRestore=sample.cpuRestore;
  const twoTick=sample.cpuRestore?.mode==='drawn-reference-no-draw-two-tick-v1';
  const completeEvidence=twoTick ? sample.cpuRestore?.nativeEvidence?.cpuEquality===1&&sample.cpuRestore?.nativeEvidence?.drawnEquality===1 :
    sample.cpuRestore?.nativeEvidence?.equality===1;
  const completionCommand=sample.cpuRestore?.mode==='drawn-reference-one-tick-v1'?7:twoTick?13:2;
  report.execution.restorePerformed=completeEvidence&&
    sample.cpuRestore?.commands?.find(record=>record.command===completionCommand)?.result===0 ? true : 'not-proven';
  report.execution.cpuOuterValidation='pending';
  return report;
}
export function recordCpuRestoreFailure(report,error) {
  if(error.cpuRestorePartial)report.execution.cpuRestore=error.cpuRestorePartial;
  if(error.cpuRestorePartialReadError)report.execution.cpuRestorePartialReadError=error.cpuRestorePartialReadError;
  if(error.cpuSevenTickPartial)report.execution.cpuSevenTick=error.cpuSevenTickPartial;
  if(error.cpuSevenTickPartialReadError)report.execution.cpuSevenTickPartialReadError=error.cpuSevenTickPartialReadError;
  if(error.cpuSevenTickPredictedPartial)report.execution.predictedInstanceProgress=error.cpuSevenTickPredictedPartial;
  if(error.cpuSevenTickPredictedPartialReadError)report.execution.predictedInstanceProgressReadError=error.cpuSevenTickPredictedPartialReadError;
  if(error.cpuCorrectedPartial)report.execution.cpuCorrected=error.cpuCorrectedPartial;
  if(error.cpuCorrectedTwoWindowPartial)report.execution.correctedTwoWindow=error.cpuCorrectedTwoWindowPartial;
  if(error.cpuCorrectedLoopPartial)report.execution.correctedLoop=error.cpuCorrectedLoopPartial;
  if(!report.cpuRawSample)report.execution.restorePerformed='unknown-on-failure; inspect native evidence and command records';
  report.execution.cpuOuterValidation='failed';
  return report;
}
export function retainCpuSevenTickSample(report,sample) {
  report.cpuRawSample=sample;
  report.execution.cpuSevenTick=sample.cpuSevenTick;
  report.execution.restorePerformed=sample.cpuSevenTick?.nativeEvidence?.state===5&&
    sample.cpuSevenTick?.commands?.find(record=>record.command===31)?.result===0 ? true : 'not-proven';
  report.execution.cpuOuterValidation='pending';
  return report;
}
export function validateCpuRestoreDrawSample(sample,beforeDiagnostics,previousFrame,expectedInputMapping) {
  if(sample.cpuRestore?.mode==='drawn-reference-one-tick-v1')return validateDrawnRestoreSample(sample,beforeDiagnostics,previousFrame,expectedInputMapping);
  const labels=['before-cpu-baseline-and-replay','after-cpu-capture-before-step',
    'after-cpu-replay-before-final-present','after-final-source-present-and-quiescence'];
  const boundaries=sample.cpuRestore?.drawBoundaries;
  if(!Array.isArray(boundaries)||boundaries.length!==labels.length||
     boundaries.some((row,i)=>row.label!==labels[i]||row.bytes!==132||Buffer.from(row.base64||'','base64').length!==132))
    throw new Error('CPU draw boundary evidence missing or malformed');
  const raw=boundaries.map(row=>Buffer.from(row.base64,'base64'));
  if(!raw[0].equals(raw[1])||!raw[0].equals(raw[2]))throw new Error('CPU capture/baseline/replay unexpectedly changed draw diagnostics');
  if(JSON.stringify(decodeDrawDiagnostics(raw[0]))!==JSON.stringify(beforeDiagnostics))throw new Error('CPU pre-boundary draw diagnostics drift');
  if(!raw[3].equals(Buffer.from(sample.drawDiagnostics,'base64')))throw new Error('Final present draw evidence drift');
  const {frame,diagnostics}=validateDrawSampleBoundary(sample,SAMPLE,decodeDrawDiagnostics(raw[2]),previousFrame);
  const fighters=validateSelectedDrawPath(decodeDrawDiagnostics(raw[2]),diagnostics,sample.boundary?.players,expectedInputMapping);
  if(!sample.boundary?.interactive)throw new Error('CPU final present interactive boundary refused');
  if(sample.pad!=='AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=')throw new Error('CPU sample300 literal PAD drift');
  return {frame,diagnostics,fighters};
}

export function validateCpuTwoTickDrawSample(sample,beforeDiagnostics,previousFrame,expectedInputMapping) {
  const labels=['capture-before-baseline','after-reference-cpu300-before-draw',
    'after-baseline-draw300-native-idle','after-reference-cpu301-before-draw',
    'after-baseline-draw301-native-idle','after-correction-cpu300','after-correction-cpu301',
    'after-final-present-native-idle'];
  const rows=sample.cpuRestore?.drawBoundaries;
  if(!Array.isArray(rows)||rows.length!==labels.length||rows.some((row,index)=>row.label!==labels[index]||
     row.bytes!==132||Buffer.from(row.base64||'','base64').length!==132))
    throw new Error('Two-tick draw boundary evidence missing or malformed');
  const raw=rows.map(row=>Buffer.from(row.base64,'base64'));
  if(!raw[0].equals(raw[1])||!raw[2].equals(raw[3])||!raw[4].equals(raw[5])||!raw[4].equals(raw[6]))
    throw new Error('A CPU tick or no-draw correction changed the wrong draw boundary');
  if(JSON.stringify(decodeDrawDiagnostics(raw[0]))!==JSON.stringify(beforeDiagnostics))
    throw new Error('Two-tick capture/pre-draw diagnostic identity drift');
  const checkpointSourceFrame=sample.cpuRestore.checkpointSourceFrame;
  validateCpuTwoTickFinalEvidence(sample.cpuRestore.nativeEvidence,checkpointSourceFrame);
  const pair300=sample.cpuRestore.nativeEvidence.pair300;
  const pair301=sample.cpuRestore.nativeEvidence.pair301;
  const baseline300={status:sample.cpuRestore.baselineGpuStatus300,drawDiagnostics:raw[2].toString('base64'),
    pad:pair300.baselinePad,boundary:rows[2].boundary};
  const first=validateDrawSampleBoundary(baseline300,300,beforeDiagnostics,previousFrame);
  const fighters300=validateSelectedDrawPath(beforeDiagnostics,first.diagnostics,rows[2].boundary?.players,expectedInputMapping);
  const baseline301={status:sample.cpuRestore.baselineGpuStatus301,drawDiagnostics:raw[4].toString('base64'),
    pad:pair301.baselinePad,boundary:rows[4].boundary};
  const second=validateDrawSampleBoundary(baseline301,301,first.diagnostics,first.frame);
  const fighters301=validateSelectedDrawPath(first.diagnostics,second.diagnostics,rows[4].boundary?.players,expectedInputMapping);
  const final={...sample,drawDiagnostics:raw[7].toString('base64')};
  const completed=validateDrawSampleBoundary(final,301,second.diagnostics,second.frame);
  const fightersFinal=validateSelectedDrawPath(second.diagnostics,completed.diagnostics,sample.boundary?.players,expectedInputMapping);
  for(const boundary of [rows[2].boundary,rows[4].boundary,sample.boundary])
    if(!boundary?.interactive)throw new Error('Two-tick source draw interactive boundary refused');
  if(rows[2].boundary.sourceFrame!==checkpointSourceFrame+1||rows[4].boundary.sourceFrame!==checkpointSourceFrame+2||
     sample.boundary.sourceFrame!==checkpointSourceFrame+2||sample.pad!=='AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=')
    throw new Error('Two-tick source frame or literal sample301 PAD drift');
  if(pair300.baselinePad!=='AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8='||
     pair301.baselinePad!=='AABQAAAAAAAAAAAAALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=')
    throw new Error('Two-tick literal source PAD sequence drift');
  return {frame:completed.frame,diagnostics:completed.diagnostics,fighters:fightersFinal,
    baselineFrames:[first.frame,second.frame],baselineFighters:[fighters300,fighters301]};
}

export function validateCpuSevenTickDrawSample(sample,beforeDiagnostics,previousFrame,expectedInputMapping) {
  const seven=sample.cpuSevenTick;
  if(!seven||seven.route!=='drawn-reference-no-draw-seven-tick-v1'||seven.commands?.length!==18||
     seven.timeline?.maxPredictionTicks!==7||seven.timeline?.predictionLimit?.tick!==307||
     seven.timeline?.predictionLimit?.reason!=='prediction_limit'||seven.timeline?.earliestChangedTick!==300||
     seven.timeline?.remoteContiguousThrough!==306||seven.timeline?.dirtyFromAfterCommit!==null||
     seven.timeline?.originalLocalPadsPreserved!==true||!Array.isArray(seven.baselineRows)||seven.baselineRows.length!==7||
     !Array.isArray(seven.predictedRows)||seven.predictedRows.length!==7||
     !Array.isArray(seven.correctedRows)||seven.correctedRows.length!==7)
    throw new Error('Seven-tick timeline or command-sequence evidence missing');
  const checkpointSourceFrame=seven.checkpointSourceFrame;
  validateCpuSevenTickFinalEvidence(seven.nativeEvidence,checkpointSourceFrame);
  if(seven.nativeEvidence.tickPairs.some((pair,index)=>pair.baselinePad!==seven.timeline.replayFrames[index]?.frameBase64||
      pair.replayPad!==seven.timeline.replayFrames[index]?.frameBase64))
    throw new Error('Seven-tick actual/corrected PAD frames differ from the committed timeline plan');
  const baselineFrames=[],baselineFighters=[];
  let frame=previousFrame,diagnostics=beforeDiagnostics;
  let previousDrawBytes=null;
  for(let index=0;index<7;index++){
    const row=seven.baselineRows[index],sampleNumber=CPU_SEVEN_TICK_SAMPLES[index];
    if(row?.sample!==sampleNumber||row.boundary?.sourceFrame!==checkpointSourceFrame+index+1||
       row.pad44Base64!==seven.nativeEvidence.tickPairs[index].baselinePad||!row.boundary?.interactive)
      throw new Error(`Seven-tick baseline source boundary drift at ${sampleNumber}`);
    const before=Buffer.from(row.drawBeforeBase64||'','base64');
    if(before.length!==132||!before.equals(Buffer.from(seven.nativeEvidence.diagnostics[index].base64,'base64'))||
       (index===0 ? JSON.stringify(decodeDrawDiagnostics(before))!==JSON.stringify(beforeDiagnostics) :
         !before.equals(previousDrawBytes)))
      throw new Error(`Seven-tick baseline pre-draw diagnostics drift at ${sampleNumber}`);
    const drawn={status:row.gpuStatus,drawDiagnostics:row.drawAfterBase64,pad:row.pad44Base64,boundary:row.boundary};
    const validated=validateDrawSampleBoundary(drawn,sampleNumber,diagnostics,frame);
    const fighters=validateSelectedDrawPath(diagnostics,validated.diagnostics,row.boundary.players,expectedInputMapping);
    if(JSON.stringify(fighters)!==JSON.stringify(row.selectedDraw))
      throw new Error(`Seven-tick selected draw boundary changed at ${sampleNumber}`);
    baselineFrames.push(validated.frame);baselineFighters.push(fighters);
    previousDrawBytes=Buffer.from(row.drawAfterBase64,'base64');
    frame=validated.frame;diagnostics=validated.diagnostics;
  }
  const finalValidation=validateDrawSampleBoundary(sample,306,diagnostics,frame);
  const finalFighters=validateSelectedDrawPath(diagnostics,finalValidation.diagnostics,
    sample.boundary?.players,expectedInputMapping);
  if(!sample.boundary?.interactive||sample.boundary.sourceFrame!==checkpointSourceFrame+7||
     sample.pad!==seven.nativeEvidence.drawnPair.replayPad)
    throw new Error('Seven-tick final-present source boundary or raw PAD mismatch');
  const remoteMotion=seven.timeline.remoteMotionByTick;
  if(!Array.isArray(remoteMotion)||remoteMotion.length!==7||
     !remoteMotion.some(row=>row.differs===true&&Number.isInteger(row.predicted)&&Number.isInteger(row.authoritative)))
    throw new Error('Seven-tick prediction did not establish nonvacuous remote fighter divergence');
  return {frame:finalValidation.frame,diagnostics:finalValidation.diagnostics,fighters:finalFighters,
    baselineFrames,baselineFighters,remoteMotion};
}

export function validateDrawnRestoreSample(sample,beforeDiagnostics,previousFrame,expectedInputMapping) {
  const labels=['before-cpu-baseline-and-replay','after-cpu-capture-before-step','after-cpu-reference-before-baseline-draw',
    'after-baseline-draw-and-quiescence','after-correction-before-final-present','after-final-source-present-and-quiescence'];
  const rows=sample.cpuRestore.drawBoundaries;
  if(!Array.isArray(rows)||rows.length!==6||rows.some((r,i)=>r.label!==labels[i]||r.bytes!==132||Buffer.from(r.base64||'','base64').length!==132))
    throw new Error('Drawn reference boundary evidence missing');
  const raw=rows.map(r=>Buffer.from(r.base64,'base64'));
  if(!raw[0].equals(raw[1])||!raw[0].equals(raw[2])||!raw[3].equals(raw[4]))throw new Error('Drawn correction interval unexpectedly drew');
  if(JSON.stringify(decodeDrawDiagnostics(raw[0]))!==JSON.stringify(beforeDiagnostics)||!raw[5].equals(Buffer.from(sample.drawDiagnostics,'base64')))
    throw new Error('Drawn boundary raw evidence drift');
  const baseline={...sample,status:sample.cpuRestore.baselineGpuStatus,drawDiagnostics:rows[3].base64};
  const baselineFrame=validateDrawSampleBoundary(baseline,SAMPLE,beforeDiagnostics,previousFrame).frame;
  const referenceFighters=validateSelectedDrawPath(beforeDiagnostics,decodeDrawDiagnostics(raw[3]),sample.boundary?.players,expectedInputMapping);
  const {frame,diagnostics}=validateDrawSampleBoundary(sample,SAMPLE,decodeDrawDiagnostics(raw[4]),baselineFrame);
  const fighters=validateSelectedDrawPath(decodeDrawDiagnostics(raw[4]),diagnostics,sample.boundary?.players,expectedInputMapping);
  if(!sample.boundary?.interactive||sample.pad!=='AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=')throw new Error('Drawn interactive/PAD guard refused');
  const d=sample.cpuRestore.drawnEvidence;
  if(d?.stage!==4||d.mode!==1||d.pair?.equality!==1||d.pair.firstFailure!==0||d.pair.checkedDomains!==0x3fe||
     d.pair.matchedDomains!==0x3fe||d.pair.comparison?.status!==0)throw new Error('Drawn typed endpoint evidence missing');
  return {frame,diagnostics,fighters,baselineFrame,referenceFighters};
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
  const twoTickDrawnReference=options['--aggregate-cpu-drawn-reference-two-tick']===true;
  const sevenTickDrawnReference=options['--aggregate-cpu-drawn-reference-seven-tick']===true;
  const correctedPrediction=options['--correct-predicted-branch']===true;
  const correctedTwoWindow=options['--correct-predicted-branch-two-window']===true;
  const correctedLoopLocal=options['--correct-predicted-branch-loop']===true;
  const correctedLoopPackets=options['--correct-predicted-branch-loop-packets']===true;
  const correctedLoop=correctedLoopLocal||correctedLoopPackets;
  const correctedRoute=correctedPrediction||correctedTwoWindow||correctedLoop;
  const lobjPresentationCache=options['--slippi-lobj-presentation-cache']===true;
  const tobjPresentationCache=options['--slippi-tobj-presentation-cache']===true;
  const particlePresentationCache=options['--slippi-particle-presentation-cache']===true;
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
  const tobjFailureStatics = tobjPresentationCache ? selectTobjFailureStatics(manifest) : null;
  const cpuIntervalFailureStatics=correctedLoop?selectCpuIntervalFailureStatics(manifest):null;
  const runtimeHashes = Object.fromEntries([runtime, wasm, data].map(file => [path.basename(file), fileSha(file)]));
  if (manifestInventory.wasmSha256 !== runtimeHashes[path.basename(wasm)]) {
    throw new Error('Source-state manifest does not describe the supplied final browser Wasm');
  }

  let cpuConfiguration=null,cpuWire=null;
  if(options['--aggregate-cpu-restore-config']){
    const file=options['--aggregate-cpu-restore-config'];cpuConfiguration=JSON.parse(fs.readFileSync(file,'utf8'));
    const policyFile=path.resolve(path.dirname(file),cpuConfiguration.policyFile);
    const policy=JSON.parse(fs.readFileSync(policyFile,'utf8'));
    const identities={wasmSha256:fileSha(wasm),mapSha256:fileSha(wasm.replace(/\.wasm$/,'.map')),
      manifestSha256:fileSha(options['--manifest']),policySha256:fileSha(policyFile)};
    if(manifest.configuration_identity?.link_map_sha256!==identities.mapSha256)throw new Error('CPU map does not match final source-state manifest');
    cpuWire=validateCpuRestoreConfiguration(cpuConfiguration,policy,manifest,identities);
    cpuConfiguration={...identities,configurationSha256:fileSha(file),wireRows:cpuWire.length,policyFile};
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
      payloadAdmissionDryRun: {enabled: Boolean(options['--source-payload-admission-dry-run']),
        status: 'not-started', restoreCalled: false, completeGameState: false,
        scope: 'one existing source-main-heap payload capture/admit across actual sample-300 tick and draw; never a browser restore'},
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
      seed: SOURCE_SEED, name: SYNCTEST_PROFILE,
      sourceTextureMatrixWriterDiagnostic: tobjPresentationCache
        ? 'NOT_APPLICABLE_STATELESS_TOBJ_PRESENTATION_V15'
        : 'SOURCE_WRITE_OCCURRENCE_ONLY_CAPTURE_TIME_JOIN',
      sourceLvecWriterDiagnostic: lobjPresentationCache
        ? 'NOT_APPLICABLE_SOURCE_CACHE_RELOCATED_V7'
        : 'SOURCE_WRITE_OCCURRENCE_ONLY_CAPTURE_TIME_JOIN'},
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
    harnessSha256: sourceHashes({packetInput: correctedLoopPackets}),
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
  if(cpuWire){
    report.schema='melee-web-same-browser-cpu-restore-replay-present-component-v1';
    report.statement='One fixed sample300 baseline CPU+PCM without draw, typed CPU restore, exact no-draw replay, then one source present. Component evidence only; no renderer rewind or whole-Wasm restore.';
    report.execution={status:'incomplete',prefixSamples:300,sample:300,cpuRestore:{enabled:true,status:'not-started'},restorePerformed:'not-yet-observed'};
    if(twoTickDrawnReference){
      report.schema='melee-web-same-browser-drawn-reference-no-draw-two-tick-component-v1';
      report.statement='Fixed source samples300/301 drawn-reference, one typed CPU restore, two no-draw correction ticks and one phase-matched final present. Private component evidence only; no renderer rewind.';
      report.execution={status:'incomplete',prefixSamples:300,samples:[300,301],cpuRestore:{enabled:true,status:'not-started'},restorePerformed:'not-yet-observed'};
    }else if(sevenTickDrawnReference){
      report.schema='melee-web-two-instance-seven-tick-delayed-remote-input-component-v1';
      report.statement='Two identically warmed private browser instances exercise seven withheld-remote predictions for non-vacuity and a separate seven-tick authoritative-input branch with typed CPU restore, corrected-input replay, per-tick CPU evidence, and one phase-matched final present. The authoritative branch is corrected from authoritative inputs; the predicted branch is not restored. Component evidence only; no live packet delivery, renderer rewind, repeated rollback, or whole-Wasm restore.';
      report.execution={status:'incomplete',prefixSamples:300,samples:CPU_SEVEN_TICK_SAMPLES,
        cpuSevenTick:{enabled:true,status:'not-started'},restorePerformed:'not-yet-observed'};
    }else if(options['--aggregate-cpu-drawn-reference']){
      report.schema='melee-web-same-browser-drawn-reference-no-draw-one-tick-component-v1';
      report.statement='Fixed sample300 drawn-reference, typed CPU restore, same no-draw correction and phase-matched finalpresent. Incomplete private profile only; no renderer rewind.';
    }
    report.cpuConfiguration=cpuConfiguration;
    report.exclusions=['No #341 milestone closure',...((twoTickDrawnReference||sevenTickDrawnReference||options['--aggregate-cpu-drawn-reference'])?['No whole-profile or vanilla original acceptance']:['No drawn-original versus no-draw coverage claim']),
      ...(sevenTickDrawnReference?['No H1 or whole-session acceptance']:['No seven-tick/H1/whole-session acceptance']),
      ...(sevenTickDrawnReference?['No restore from the predicted instance; no changed-action, live-packet, or repeated rollback claim']:[]),
      'No renderer/host allocator rewind','No #339 nonzero-history/drop workaround'];
    if(correctedPrediction){
      report.schema='melee-web-independent-browser-correction-component-v1';
      report.statement='Two identically prepared private browsers compare complete declared PRE state, then a wrong-input seven-tick drawn branch is restored and re-simulated without draw against the independent authoritative browser. Exact declared CPU_END/raw pairs and final DRAW_END are separate checks. Component evidence only; no repeated rollback, live relay or profile acceptance.';
      report.execution={status:'incomplete',prefixSamples:300,samples:CPU_SEVEN_TICK_SAMPLES,
        cpuCorrected:{enabled:true,status:'not-started'},restorePerformed:'not-yet-observed'};
      report.exclusions=['No #341 milestone closure','No repeated correction or rolling snapshot ring',
        'No live packet delivery or relay match','No whole-profile, vanilla, H1 or whole-session acceptance',
        'No renderer/host allocator rewind','No #339 nonzero-history/drop workaround'];
    }
    if(correctedTwoWindow){
      report.schema='melee-web-independent-browser-correction-two-window-component-v1';
      report.statement='Two identically prepared private browsers compare two independent wrong-input seven-tick windows on one persistent input timeline. Each window has an independently compared PRE, corrected CPU_END, DRAW_END, and five raw lanes; a bounded literal-neutral source step/draw bridge separates checked capsule retirement. Each restore starts from that window’s pre-capture capsule, never from the speculative after-state. Component evidence only; no live relay, full-world restore, or profile acceptance.';
      report.execution={status:'incomplete',prefixSamples:300,windows:2,
        correctedReuse:{enabled:true,status:'not-started'},bridge:{maximumTicks:60,status:'not-started'},
        restorePerformed:'not-yet-observed'};
      report.exclusions=['No #341 milestone closure','No live packet delivery or relay match',
        'No whole-profile, vanilla, H1 or whole-session acceptance','No restoration from a speculative post-window state',
        'No renderer/host allocator rewind','No #339 nonzero-history/drop workaround'];
    }
  }
  if(correctedLoop){
    report.schema='melee-web-independent-browser-correction-loop-component-v1';
    report.statement=correctedLoopPackets?
      'One continuing private match/input timeline uses confirmed and speculative intervals of one through seven ticks. A Node-held pair of existing local A2 peers supplies all 44 remote PAD rows through the owned loopback Worker relay; speculative batches are released with the predicted command-40 final draw request and accepted only after complete protocol receive. Each interval retains exact PRE/CPU/DRAW and raw-lane checks, with evidence saved before retirement. Component evidence only; browser pages own no sockets, and this does not establish rolling checkpoints, whole-match rollback, or official Slippi interoperability.':
      'One continuing private match/input timeline uses confirmed and speculative intervals of one through seven ticks. Each interval compares all declared PRE/CPU/DRAW domains and raw lanes against an independent drawn instance; exact interval evidence is saved before confirmed retirement. This is bounded reusable ownership, not asynchronous rolling netcode or public-profile admission.';
    report.execution={status:'incomplete',prefixSamples:300,correctedLoop:{status:'not-started'},
      intervalEvidence:[],restorePerformed:'not-yet-observed'};
    report.exclusions=correctedLoopPackets?
      ['No #341 milestone closure','No browser-owned socket or original browser input-path integration',
      'No rolling/multiple simultaneous checkpoints or whole-match rollback',
      'No official Slippi/public interoperability or whole-profile acceptance',
      'No renderer/host allocator rewind','No #339 nonzero-history/drop workaround']:
      ['No #341 milestone closure','No asynchronous live packet delivery or relay match',
      'No multiple simultaneous checkpoints or repeated restoration within one capsule',
      'No whole-profile, vanilla, H1 or whole-session acceptance','No renderer/host allocator rewind',
      'No #339 nonzero-history/drop workaround'];
  }
  const writeReport = () => fs.writeFileSync(outFile, `${JSON.stringify(report, null, 2)}\n`);
  const intervalSink=correctedLoop?createCpuIntervalEvidenceSink(options['--out']):null;
  const onIntervalEvidence=correctedLoop?async(index,interval)=>{
    const receipt=await intervalSink(index,interval);
    report.execution.intervalEvidence.push({index,...receipt});
    writeReport();return receipt;
  }:null;
  writeReport();

  let server = null, browserServer = null, browserProcess = null;
  let browser = null, browserCdp = null, context = null, page = null, predictedPage = null;
  let relayRuntime=null,packetInput=null,packetRunPassed=false;
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
    if (twoTickDrawnReference && tobjPresentationCache) {
      // Retain original callers for the exact-target joint diagnostic. This is
      // confined to this private browser page; native event/text caps still apply.
      report.jointWriterStackDiagnostic = await page.evaluate(() => {
        const priorLimit = Error.stackTraceLimit;
        Error.stackTraceLimit = 64;
        return {priorLimit, limit: Error.stackTraceLimit,
          scope: 'private two-tick diagnostic; no simulation or timing claim'};
      });
      if (report.jointWriterStackDiagnostic.limit !== 64) {
        throw new Error('Private joint diagnostic stack depth unavailable');
      }
    }
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
    report.profile = {...report.profile, identity: init.profile, sourceIdentity: init.source,
      rngHelper: init.rngHelper, stageBridge: init.stageBridge, seed: SOURCE_SEED,
      name: particlePresentationCache ? TEV_STAGE_SCRATCH_V19_PROFILE :
        tobjPresentationCache ? LOBJ_CURRENT_SET_V15_PROFILE : lobjPresentationCache ? LOBJ_PRESENTATION_V7_PROFILE :
        twoTickDrawnReference ? LOGICAL_FRAME_V6_PROFILE : SYNCTEST_PROFILE,
      lobjPresentationCache, tobjPresentationCache, particlePresentationCache};
    const twoTickExports = twoTickDrawnReference ? await page.evaluate(() => {
      const {module} = window.__slippiSynctest;
      return {evidence: typeof module._melee_web_synctest_cpu_two_tick_evidence === 'function',
        evidenceBytes: typeof module._melee_web_synctest_cpu_two_tick_evidence_bytes === 'function',
        payloadWitness: typeof module._melee_web_synctest_cpu_payload_failure_witness === 'function',
        payloadWitnessBytes: typeof module._melee_web_synctest_cpu_payload_failure_witness_bytes === 'function'};
    }) : null;
    const sevenTickExports = sevenTickDrawnReference ? await page.evaluate(() => {
      const {module} = window.__slippiSynctest;
      return {evidence: typeof module._melee_web_synctest_cpu_seven_tick_evidence === 'function',
        evidenceBytes: typeof module._melee_web_synctest_cpu_seven_tick_evidence_bytes === 'function'};
    }) : null;
    const correctedReuseExports = correctedTwoWindow ? await page.evaluate(() => {
      const {module} = window.__slippiSynctest;
      return {enable:typeof module._melee_web_synctest_cpu_corrected_reuse_enable==='function',
        evidence:typeof module._melee_web_synctest_cpu_corrected_reuse_evidence==='function',
        evidenceBytes:typeof module._melee_web_synctest_cpu_corrected_reuse_evidence_bytes==='function'};
    }) : null;
    validateBrowserProfileForRoute(init, twoTickDrawnReference, twoTickExports,
      lobjPresentationCache, tobjPresentationCache, particlePresentationCache,
      sevenTickDrawnReference, sevenTickExports, correctedReuseExports);
    if (twoTickDrawnReference) report.profile.twoTickEvidenceExports = twoTickExports;
    if (sevenTickDrawnReference) report.profile.sevenTickEvidenceExports = sevenTickExports;
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
    let predictedInit=null,predictedDiagnostics=null,predictedFrame=null,predictedInputMapping=null;
    if(sevenTickDrawnReference){
      predictedPage=await context.newPage();
      predictedPage.setDefaultTimeout(120000);
      predictedPage.on('pageerror',error=>pageErrors.push(`predicted instance: ${String(error)}`));
      predictedPage.on('console',message=>{if(message.type()==='error')pageErrors.push(`predicted instance: ${message.text()}`);});
      await predictedPage.goto(serving.origin,{waitUntil:'domcontentloaded'});
      predictedInit=await runBrowserInit(predictedPage,serving.origin,path.basename(runtime));
      predictedDiagnostics=decodeDrawDiagnostics(Buffer.from(predictedInit.drawDiagnostics,'base64'));
      assertDrawDiagnosticsClear(predictedDiagnostics,'predicted instance initialization');
      const predictedInitializer=JSON.parse(predictedInit.initializer);
      predictedInputMapping=validateSourceInputMapping(predictedInitializer.players.map((player,sourceSlot)=>({
        sourceSlot,initializerPort:player.port,rawController:player.controller})),predictedDiagnostics.slotCount);
      if(predictedInit.profile!==init.profile||predictedInit.source!==init.source||
         predictedInit.rngHelper!==init.rngHelper||predictedInit.stageBridge!==init.stageBridge||
         predictedInit.initializer!==init.initializer||JSON.stringify(predictedInputMapping)!==JSON.stringify(expectedInputMapping)||
         JSON.stringify(predictedDiagnostics)!==JSON.stringify(drawDiagnostics)||
         predictedInit.backend.backend!==init.backend.backend||predictedInit.featureAvailable!==init.featureAvailable||
         JSON.stringify(predictedInit.initial)!==JSON.stringify(init.initial))
        throw new Error('Seven-tick predicted instance did not share the authoritative profile, initializer, mapping, and initial source signature');
      predictedFrame={beginFrameId:predictedInit.backend.lastBeginFrameId||0,endFrameId:predictedInit.backend.lastEndFrameId||0};
      report.warmPrefix.predictedInstance={profile:predictedInit.profile,source:predictedInit.source,
        rngHelper:predictedInit.rngHelper,stageBridge:predictedInit.stageBridge,seed:SOURCE_SEED,
        initializerSha256:sha256(Buffer.from(predictedInit.initializer)),inputMapping:predictedInputMapping,
        initialObservationSha256:sha256(Buffer.from(predictedInit.initial.observation,'base64')),
        initialPcmSha256:sha256(Buffer.from(predictedInit.initial.pcm,'base64')),
        initialPadSha256:sha256(Buffer.from(predictedInit.initial.pad,'base64'))};
      report.execution.predictedInstance={status:'initialized',profile:predictedInit.profile,
        source:predictedInit.source,initializerSha256:sha256(Buffer.from(predictedInit.initializer))};
    }
    if(correctedTwoWindow){
      // The seven-tick setup above creates and initializes predictedPage.
      // Check its reuse exports only after that shared startup boundary.
      const predictedReuseExports=await predictedPage.evaluate(()=>{
        const {module}=window.__slippiSynctest;
        return {enable:typeof module._melee_web_synctest_cpu_corrected_reuse_enable==='function',
          evidence:typeof module._melee_web_synctest_cpu_corrected_reuse_evidence==='function',
          evidenceBytes:typeof module._melee_web_synctest_cpu_corrected_reuse_evidence_bytes==='function'};
      });
      validateBrowserProfileForRoute(predictedInit,twoTickDrawnReference,twoTickExports,
        lobjPresentationCache,tobjPresentationCache,particlePresentationCache,
        sevenTickDrawnReference,sevenTickExports,predictedReuseExports);
      report.profile.correctedReuseExports={reference:correctedReuseExports,predicted:predictedReuseExports};
    }
    if(correctedLoop){
      report.profile.correctedLoopExports={
        reference:validateCpuLoopExports(await page.evaluate(readCpuLoopExportsInPage)),
        predicted:validateCpuLoopExports(await predictedPage.evaluate(readCpuLoopExportsInPage))};
    }
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
    const authoritativePrefixSignature=crypto.createHash('sha256');
    const predictedPrefixSignature=sevenTickDrawnReference?crypto.createHash('sha256'):null;
    let predictedPreviousDiagnostics=predictedDiagnostics;
    let checkpoint = null;
    for (let sample = 0; sample < PREFIX_SAMPLES; sample++) {
      const result = await runBrowserSample(page, sample, expectedInputMapping, expectedStocks);
      const validated = validateCensusDrawSample(result, sample, previousDiagnostics,
        previousFrame, expectedInputMapping);
      addPrefixSignature(authoritativePrefixSignature,sample,result);
      previousFrame = validated.frame;
      previousDiagnostics = validated.diagnostics;
      report.warmPrefix.drawSamples.push({sample,
        sourceFrame: result.boundary.sourceFrame, drawDiagnostics: validated.diagnostics,
        gpuFrame: validated.frame});
      const consumedPad = Buffer.from(result.pad, 'base64');
      report.warmPrefix.consumedInputs.push({sample, rawPad44Base64: result.pad,
        rawPad44Sha256: sha256(consumedPad)});
      let predictedResult=null;
      if(predictedPage){
        predictedResult=await runBrowserSample(predictedPage,sample,predictedInputMapping,expectedStocks);
        const predictedValidated=validateDrawSampleBoundary(predictedResult,sample,
          predictedPreviousDiagnostics,predictedFrame);
        for(const key of ['observation','pcm','pad','drawDiagnostics'])
          if(!Buffer.from(result[key],'base64').equals(Buffer.from(predictedResult[key],'base64')))
            throw new Error(`Seven-tick two-instance warm prefix differs at sample ${sample}: ${key}`);
        addPrefixSignature(predictedPrefixSignature,sample,predictedResult);
        predictedFrame=predictedValidated.frame;predictedPreviousDiagnostics=predictedValidated.diagnostics;
        report.warmPrefix.predictedSamplesCompleted=sample+1;
        report.warmPrefix.predictedDrawSamples=report.warmPrefix.predictedDrawSamples||[];
        report.warmPrefix.predictedDrawSamples.push({sample,sourceFrame:predictedResult.boundary.sourceFrame,
          drawDiagnostics:predictedValidated.diagnostics,gpuFrame:predictedValidated.frame});
        if(sample===PREFIX_SAMPLES-1){
          if(JSON.stringify(predictedResult.boundary)!==JSON.stringify(result.boundary))
            throw new Error('Seven-tick two-instance checkpoint interactive boundary differs');
          const authSig=authoritativePrefixSignature.copy().digest('hex');
          const predictedSig=predictedPrefixSignature.copy().digest('hex');
          if(authSig!==predictedSig)throw new Error('Seven-tick two-instance warm-prefix signatures differ');
          report.warmPrefix.authoritativePrefixSignatureSha256=authSig;
          report.warmPrefix.predictedPrefixSignatureSha256=predictedSig;
          report.warmPrefix.sameWarmPrefixSignature=true;
          report.warmPrefix.predictedCheckpointSample299=rawLaneRecord(browserSampleLane(predictedResult));
          report.warmPrefix.predictedCheckpointBoundary=predictedResult.boundary;
        }
      }
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

    if(cpuWire){
      const helpers={checkedSource:checkedAsyncCcall.toString(),inputMappingSource:validateSourceInputMapping.toString(),
        inspectSource:inspectInteractiveObservation.toString(),selectedDrawSource:validateSelectedDrawPath.toString(),
        decodeDrawSource:decodeDrawDiagnostics.toString()};
      if(sevenTickDrawnReference){
        if(!predictedPage)throw new Error('Seven-tick predicted instance is unavailable');
        if(correctedLoop){
          if(correctedLoopPackets){
            const relayTools=process.env.MELEE_A3_ROOM_RELAY_TOOLS;
            if(!relayTools||!path.isAbsolute(relayTools)||
               !['wrangler/package.json','miniflare/package.json','workerd/package.json']
                 .every(file=>fs.existsSync(path.join(relayTools,file))))
              throw new Error('Packet-backed loop requires the existing pinned Wrangler, Miniflare, and workerd packages in an absolute MELEE_A3_ROOM_RELAY_TOOLS path');
            const {startRoomRelayRuntime}=await import('../../../scripts/net_room_relay_runtime_owner.mjs');
            const packetModule=await import('./cpu_corrected_loop_packet_input.mjs');
            const producer={commit:execFileSync('git',['rev-parse','HEAD'],{cwd:ROOT,encoding:'utf8'}).trim(),
              tree:execFileSync('git',['rev-parse','HEAD^{tree}'],{cwd:ROOT,encoding:'utf8'}).trim()};
            relayRuntime=await startRoomRelayRuntime({producer,evidenceDir:path.join(options['--out'],'packet-worker-evidence'),
              diagnostic:message=>{(report.packetInputRuntimeLog??=[]).push(String(message));writeReport();}});
            const agreement=packetModule.buildCpuCorrectedLoopPacketAgreement({producer,profile:report.profile,
              initializer:init.initializer,inputMapping:expectedInputMapping,slotCount:drawDiagnostics.slotCount,
              sourceStateManifest:report.sourceStateManifest,
              runtimeSha256:report.runtimeSha256,harnessSha256:report.harnessSha256,
              worker:{identity:relayRuntime.runtimeIdentity,hashes:relayRuntime.hashes}});
            packetInput=await packetModule.openCpuCorrectedLoopPacketInput({runtime:relayRuntime,agreement});
            report.packetInput={status:'ready',scope:'Node-held A2 peers over the owned local Worker relay',
              browserOwnsSocket:false,agreementSha256:packetInput.startup.agreementSha256,
              runtime:{port:relayRuntime.port,identity:relayRuntime.runtimeIdentity,hashes:relayRuntime.hashes},
              peerReadyTimeoutMs:packetInput.startup.peerReadyTimeoutMs,
              neutralPreroll:packetInput.startup.neutralPreroll};
            writeReport();
          }
          const result=await runBrowserCpuCorrectedLoop(predictedPage,page,cpuWire,checkpoint,
            expectedInputMapping,expectedStocks,previousFrame,previousDiagnostics,onIntervalEvidence,
            packetInput?{packetInput}:{});
          packetRunPassed=result.status==='passed';
          report.execution={...report.execution,status:'completed',correctedLoop:result,restorePerformed:true,
            scope:packetInput?
              'bounded variable-span confirmed/speculative ownership, Node-held A2 input for 44 rows, one capsule and a persistent timeline':
              'bounded variable-span confirmed/speculative ownership with one capsule and a persistent timeline'};
          report.cpuResult={referenceScreenshot:await screenshotRecord(page,options['--out'],'reference-final.png'),
            correctedScreenshot:await screenshotRecord(predictedPage,options['--out'],'corrected-final.png')};
          report.inventory={status:'fixed-reviewed-domains-only',completeGameState:false,
            sourceStateManifestComplete:false,wasmAndMapBound:true,staticRows:cpuWire.length};
          if(pageErrors.length)throw new Error(`Browser reported an error: ${pageErrors[0]}`);
          if(packetInput)report.packetInput.finalSummary=packetInput.summary();
          report.result='passed';writeReport();return;
        }
        if(correctedTwoWindow){
          const result=await runBrowserCpuCorrectedTwoWindow(predictedPage,page,cpuWire,checkpoint,
            expectedInputMapping,expectedStocks,previousFrame,previousDiagnostics);
          report.execution={status:'completed',prefixSamples:300,correctedTwoWindow:result,
            windowCount:result.windows.length,bridgeStatus:result.bridge.status,
            bridgeTicks:result.bridge.ticks.length,restorePerformed:true,
            scope:'two independent same-match input-correction windows with the same timeline and checked one-capsule retirement'};
          report.cpuResult={referenceScreenshot:await screenshotRecord(page,options['--out'],'reference-final.png'),
            correctedScreenshot:await screenshotRecord(predictedPage,options['--out'],'corrected-final.png')};
          report.inventory={status:'fixed-reviewed-domains-only',completeGameState:false,
            sourceStateManifestComplete:false,wasmAndMapBound:true,staticRows:cpuWire.length};
          if(pageErrors.length)throw new Error(`Browser reported an error: ${pageErrors[0]}`);
          report.result='passed';writeReport();return;
        }
        if(correctedPrediction){
          const result=await runBrowserCpuCorrectedSeven(predictedPage,page,cpuWire,checkpoint,
            expectedInputMapping,expectedStocks,previousFrame,previousDiagnostics);
          report.execution={status:'completed',prefixSamples:300,samples:CPU_SEVEN_TICK_SAMPLES,
            cpuCorrected:result,restorePerformed:true,
            scope:'one independent-reference wrong-input browser correction with a final present'};
          report.cpuResult={comparisons:result.comparisons,
            referenceScreenshot:await screenshotRecord(page,options['--out'],'reference-final.png'),
            correctedScreenshot:await screenshotRecord(predictedPage,options['--out'],'corrected-final.png')};
          report.inventory={status:'fixed-reviewed-domains-only',completeGameState:false,
            sourceStateManifestComplete:false,wasmAndMapBound:true,staticRows:cpuWire.length};
          if(pageErrors.length)throw new Error(`Browser reported an error: ${pageErrors[0]}`);
          report.result='passed';writeReport();return;
        }
        const sample=await runBrowserCpuSevenTick(predictedPage,page,cpuWire,checkpoint,
          expectedInputMapping,expectedStocks,previousFrame,previousDiagnostics,helpers);
        retainCpuSevenTickSample(report,sample);writeReport();
        const validated=validateCpuSevenTickDrawSample(sample,previousDiagnostics,previousFrame,expectedInputMapping);
        const seven=sample.cpuSevenTick;
        report.execution={status:'completed',prefixSamples:300,samples:CPU_SEVEN_TICK_SAMPLES,
          sample:306,cpuSevenTick:seven,restorePerformed:true,
          scope:'two-instance seven-tick input-prediction and authoritative-input typed-restore component only',
          predictionInstance:'seven native no-draw ticks acknowledged one at a time; not restored',
          authoritativeInstance:'seven drawn-reference ticks, typed CPU restore, seven corrected-input no-draw ticks, one final present',
          cpuOuterValidation:'passed'};
        report.input={initialRemotePadBase64:seven.timeline.initialRemotePad,
          predictionLimit:seven.timeline.predictionLimit,
          samples:seven.timeline.replayFrames.map((row,index)=>{
            const bytes=Buffer.from(row.frameBase64,'base64');
            return {sample:CPU_SEVEN_TICK_SAMPLES[index],rawPad44Base64:row.frameBase64,
              rawPad44Sha256:sha256(bytes),predictedPad44Base64:seven.predictedRows[index].pad44Base64,
              predictedBoundaryAcknowledged:seven.predictedRows[index].timelineAcknowledged===true,
              authoritativeRemotePadBase64:Buffer.from(seven.baselineRows[index].pad44Base64,'base64')
                .subarray(seven.timeline.remotePort*11,(seven.timeline.remotePort+1)*11).toString('base64'),
              correctedPad44Base64:seven.correctedRows[index].pad44Base64};
          }),
          earliestChangedTick:seven.timeline.earliestChangedTick,
          originalLocalPadsPreserved:seven.timeline.originalLocalPadsPreserved,
          sourceSchedule:'literal samples300..306 only; tick307 is a prediction-limit query and no native source step is issued'};
        report.cpuResult={observation:rawLaneRecord(browserSampleLane(sample)),boundary:sample.boundary,
          gpuStatus:sample.status,finalFrame:validated.frame,drawDiagnostics:validated.diagnostics,
          selectedFighterPaths:validated.fighters,baselineFrames:validated.baselineFrames,
          baselineFighterPaths:validated.baselineFighters,remoteMotionByTick:validated.remoteMotion,
          screenshot:await screenshotRecord(page,options['--out'],'browser-cpu-seven-tick-sample-306.png')};
        report.inventory={status:'fixed-reviewed-domains-only',completeGameState:false,
          sourceStateManifestComplete:false,wasmAndMapBound:true,staticRows:cpuWire.length};
        if(pageErrors.length)throw new Error(`Browser reported an error: ${pageErrors[0]}`);
        report.result='passed';writeReport();return;
      }
      const sample=twoTickDrawnReference?
        await runBrowserCpuTwoTick(page,cpuWire,checkpoint,expectedInputMapping,expectedStocks,helpers):
        await runBrowserCpuRestore(page,cpuWire,checkpoint,expectedInputMapping,expectedStocks,{
          drawnReference:Boolean(options['--aggregate-cpu-drawn-reference']),checkedSource:helpers.checkedSource,
          inputMappingSource:helpers.inputMappingSource,inspectSource:helpers.inspectSource});
      retainCpuRestoreSample(report,sample);writeReport();
      const validated=twoTickDrawnReference?
        validateCpuTwoTickDrawSample(sample,previousDiagnostics,previousFrame,expectedInputMapping):
        validateCpuRestoreDrawSample(sample,previousDiagnostics,previousFrame,expectedInputMapping);
      const {frame,diagnostics,fighters}=validated;
      if(twoTickDrawnReference){
        report.execution={status:'completed',prefixSamples:300,samples:[300,301],sample:301,cpuRestore:sample.cpuRestore,
          restorePerformed:true,scope:'same-browser drawn-reference/no-draw-two-tick COMPONENT only',cpuOuterValidation:'passed'};
        report.input={samples:[300,301].map(sampleNumber=>{
          const raw=Buffer.from(sample.cpuRestore.rawSamples[`sample${sampleNumber}`].pad,'base64');
          return {sample:sampleNumber,rawPad44Base64:raw.toString('base64'),rawPad44Sha256:sha256(raw)};
        }),sourceSchedule:'two fixed literal inputs at source samples300 and301; no synthetic PAD or additional source step'};
      }else{
        report.execution={status:'completed',prefixSamples:300,sample:300,cpuRestore:sample.cpuRestore,restorePerformed:true,
          scope:options['--aggregate-cpu-drawn-reference']?'same-browser drawn-reference/no-draw-one-tick COMPONENT only':'same-browser CPU restore/replay + one present COMPONENT only',cpuOuterValidation:'passed'};
      }
      report.cpuResult={observation:rawLaneRecord(browserSampleLane(sample)),boundary:sample.boundary,gpuStatus:sample.status,
        finalFrame:frame,drawDiagnostics:diagnostics,selectedFighterPaths:fighters,
        ...(twoTickDrawnReference?{baselineFrames:validated.baselineFrames,baselineFighterPaths:validated.baselineFighters,
          tickSamples:Object.fromEntries([300,301].map(sampleNumber=>{
            const row=sample.cpuRestore.rawSamples[`sample${sampleNumber}`];
            return [sampleNumber,rawLaneRecord({observation:Buffer.from(row.observation,'base64'),pcm:Buffer.from(row.pcm,'base64'),pad:Buffer.from(row.pad,'base64')})];
          }))}:{}),
        screenshot:await screenshotRecord(page,options['--out'],twoTickDrawnReference?'browser-cpu-two-tick-sample-301.png':'browser-cpu-restore-sample-300.png')};
      report.inventory={status:'fixed-reviewed-domains-only',completeGameState:false,sourceStateManifestComplete:false,
        wasmAndMapBound:true,staticRows:cpuWire.length};
      if(pageErrors.length)throw new Error(`Browser reported an error: ${pageErrors[0]}`);
      report.result='passed';writeReport();return;
    }
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
    const payloadAdmissionPlan = options['--source-payload-admission-dry-run'] ? {
      checkpointSample: PREFIX_SAMPLES - 1,
      checkpointSourceFrame: checkpoint.boundary.sourceFrame,
      checkpointRawPad44Base64: checkpoint.pad,
      expectedSample300Pad44Base64: 'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
      expectedSample300Pad44Sha256: '46e14bb3286ecad56fd0c8c45e405b64f645f971bfbd35e9ddd8432db3c4e288',
    } : null;
    const sample = await runBrowserSample(page, SAMPLE, expectedInputMapping, expectedStocks,
      censusPlan, payloadAdmissionPlan);
    if (payloadAdmissionPlan) {
      report.execution.payloadAdmissionDryRun = {...sample.payloadAdmission,
        enabled: true, restoreCalled: false, completeGameState: false};
    }
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
    assembleCompletedCensusExecution(report, {prefixSamples: PREFIX_SAMPLES,
      censusSample: SAMPLE, captureHypothesis});
    if (pageErrors.length) throw new Error(`Browser reported an error: ${pageErrors[0]}`);
    report.browser.pageErrors = pageErrors;
    writeReport();
  } catch (error) {
    packetRunPassed=false;
    recordFailedCensusExecution(report, error);
    if(packetInput&&report.packetInput)report.packetInput.partialSummary=packetInput.summary();
    if(cpuWire){
      recordCpuRestoreFailure(report,error);
    }
    await recordCensusFailurePageState(report, page);
    await recordTobjFailureStatics(report, page, tobjFailureStatics);
    await recordCpuIntervalFailureStatics(report,page,cpuIntervalFailureStatics,'reference');
    await recordCpuIntervalFailureStatics(report,predictedPage,cpuIntervalFailureStatics,'predicted');
    if (page) {
      try { report.failureScreenshot = await screenshotRecord(page, options['--out'], 'failure.png'); }
      catch (screenshotError) { report.failureScreenshotError = String(screenshotError); }
    }
    writeReport();
    throw error;
  } finally {
    const cleanupFailures = [];
    if(predictedPage){
      try{
        const closed=await closeBrowser(predictedPage,false,correctedRoute);
        if(!closed?.closed)cleanupFailures.push({step:'predicted-renderer-close',error:JSON.stringify(closed)});
        report.cleanup={...(report.cleanup||{}),predictedRenderer:closed};
      }catch(error){cleanupFailures.push({step:'predicted-renderer-close',error:String(error)});}
    }
    if (page) {
      try {
        const closed = await closeBrowser(page,
          Boolean(options['--source-payload-admission-dry-run']),Boolean(cpuWire));
        if (!closed?.closed) cleanupFailures.push({step: 'renderer-close', error: JSON.stringify(closed)});
        report.cleanup = {...(report.cleanup || {}), renderer: closed};
      } catch (error) { cleanupFailures.push({step: 'renderer-close', error: String(error)}); }
    }
    await recordClassPieceSlabTrace(report, page);
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
    if(packetInput){
      try{
        const closed=await packetInput.close();
        report.packetInput={...(report.packetInput||{}),closed:true,closeSummary:closed.summary};
      }catch(error){
        cleanupFailures.push({step:'packet-input-close',error:String(error?.stack||error)});
        report.packetInput={...(report.packetInput||{}),closed:false,closeError:String(error),
          finalSummary:packetInput.summary()};
      }
    }
    if(relayRuntime){
      try{
        const closed=await relayRuntime.close(Boolean(packetRunPassed&&report.result==='passed'&&cleanupFailures.length===0));
        report.packetInputRuntimeCleanup=closed;
      }catch(error){cleanupFailures.push({step:'packet-worker-runtime-close',error:String(error?.stack||error)});}
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
