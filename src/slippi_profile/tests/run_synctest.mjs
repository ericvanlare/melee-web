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
  applyCleanupVerdict, assertDrawDiagnosticsClear, checkedAsyncCcall,
  chromeUserDataDirectoryFromSpawnArgs, decodeDrawDiagnostics,
  inspectInteractiveObservation, launchOwnedChromeServer, ownedProcessPresent,
  requireDrawDiagnosticSize,
  recordBrowserProcessInfo, validateAuroraFrame, validateSelectedDrawPath,
  validateSourceInputMapping,
} from './runner_contract.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
export const OBSERVATION_BYTES = 1280;
export const PCM_BYTES = 1068 * 4;
export const PAD_BYTES = 44;
export const DRAW_DIAGNOSTIC_BYTES = 132;
export const SOURCE_SEED = 0x13579bdf;
export const PREFIX_SAMPLES = 300;
export const SYNCTEST_PROFILE = 'slippi-render-dependencies-dbooc-shield-drop-v5';
const INTERVAL_SAMPLES = Object.freeze([300, 301]);
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

function runtimeDrawDiagnostics(module) {
  const size = module._melee_web_synctest_draw_diagnostics_size();
  if (size !== DRAW_DIAGNOSTIC_BYTES) {
    throw new Error(`Draw diagnostic ABI size is invalid: ${size}`);
  }
  return decodeDrawDiagnostics(copyBytes(
    module, module._melee_web_synctest_draw_diagnostics(), size));
}

function nodeInteractiveState(module, lane, expectedInputMapping, expectedStocks) {
  const diagnostics = runtimeDrawDiagnostics(module);
  return inspectInteractiveObservation(lane.observation, {
    classifyMotion: motion => module._melee_web_synctest_motion_boundary(motion),
    isMarioKind: kind => module._melee_web_synctest_is_mario_kind(kind) === 1,
    slotCount: diagnostics.slotCount,
    expectedInputMapping, expectedStocks,
  });
}

function requireInteractive(state, lane, sample, phase) {
  if (!state.interactive) {
    const refusal = {kind: 'interactive-boundary-refusal', lane, sample, phase,
      reasons: state.refusalReasons, players: state.players};
    throw Object.assign(new Error(`Interactive boundary refused: ${JSON.stringify(refusal)}`),
      {interactiveBoundaryRefusal: refusal});
  }
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
  const observation = new DataView(lane.observation.buffer,
    lane.observation.byteOffset, lane.observation.byteLength);
  const pcmFrames = observation.getUint32(32, true);
  const audioPhase = observation.getUint32(36, true);
  if (pcmFrames > 534 || audioPhase >= 60) {
    throw new Error(`PCM observation metadata is outside source bounds: frames=${pcmFrames}, phase=${audioPhase}`);
  }
  return {
    sample,
    pcmFrames,
    audioPhase,
    observationSha256: sha256(lane.observation),
    pcmSha256: sha256(lane.pcm),
    rawPad44Sha256: sha256(lane.pad),
  };
}

export function rawLaneRecord(lane) {
  const view = new DataView(lane.observation.buffer,
    lane.observation.byteOffset, lane.observation.byteLength);
  // Preserve bytes even if the subsequent timing or boundary guard refuses.
  return {
    observationBase64: lane.observation.toString('base64'),
    pcmBase64: lane.pcm.toString('base64'),
    rawPad44Base64: lane.pad.toString('base64'),
    pcmFrames: view.getUint32(32, true), audioPhase: view.getUint32(36, true),
    observationSha256: sha256(lane.observation), pcmSha256: sha256(lane.pcm),
    rawPad44Sha256: sha256(lane.pad),
  };
}

function browserSampleLane(sample) {
  return Object.fromEntries(['observation', 'pcm', 'pad'].map(key =>
    [key, Buffer.from(sample[key], 'base64')]));
}

function browserSampleDiagnostics(sample) {
  return decodeDrawDiagnostics(Buffer.from(sample.drawDiagnostics, 'base64'));
}

export function compareLane(sample, domain, expected, actual) {
  for (const key of ['observation', 'pcm', 'pad']) {
    if (!expected[key].equals(actual[key])) {
      const diff = firstDifference(expected[key], actual[key]);
      const failure = {sample, domain, field: key, ...diff,
        expectedSha256: sha256(expected[key]), actualSha256: sha256(actual[key])};
      throw Object.assign(new Error(`First ${domain} mismatch: ${JSON.stringify(failure)}`), {synctestFailure: failure});
    }
  }
}

// Byte-level census helpers classify observed writes by static provenance,
// arena extent, and validated live audio ranges. These labels never admit a
// range for restore.
export function diffHeapRanges(before, after) {
  if (!(before instanceof Uint8Array) || !(after instanceof Uint8Array)) {
    throw new TypeError('Heap census snapshots must be Uint8Array values');
  }
  const shared = Math.min(before.length, after.length);
  const ranges = [];
  let start = -1;
  let changedBytes = 0;
  for (let offset = 0; offset < shared; offset++) {
    if (before[offset] !== after[offset]) {
      changedBytes++;
      if (start < 0) start = offset;
    } else if (start >= 0) {
      ranges.push({start, end: offset, bytes: offset - start});
      start = -1;
    }
  }
  if (start >= 0) ranges.push({start, end: shared, bytes: shared - start});
  if (after.length > shared) {
    ranges.push({start: shared, end: after.length, bytes: after.length - shared,
      kind: 'memory-growth'});
    changedBytes += after.length - shared;
  } else if (before.length > shared) {
    ranges.push({start: shared, end: before.length, bytes: before.length - shared,
      kind: 'memory-shrink'});
    changedBytes += before.length - shared;
  }
  return {ranges, changedBytes};
}

export function classifyHeapRanges(rawRanges, manifestRows, arenaRange,
  audioInventoryTransition = null, nativeOutputRange = null,
  matchContextInventoryTransition = null, auroraInventoryTransition = null) {
  if (!Array.isArray(rawRanges) || !Array.isArray(manifestRows) ||
      !arenaRange || !Number.isSafeInteger(arenaRange.base) ||
      !Number.isSafeInteger(arenaRange.bytes)) {
    throw new TypeError('Heap census classification inputs are malformed');
  }
  let audioRows = [];
  if (audioInventoryTransition !== null) {
    const {before, after} = audioInventoryTransition || {};
    if (!before || !after || !Array.isArray(before.rows) || !Array.isArray(after.rows) ||
        before.audioOwner !== after.audioOwner ||
        before.generationLow !== after.generationLow ||
        before.generationHigh !== after.generationHigh || before.count !== after.count ||
        before.effectsEnabled !== after.effectsEnabled || before.rows.length !== after.rows.length ||
        before.rows.some((row, index) => {
          const other = after.rows[index];
          return row.kind !== other.kind || row.address !== other.address ||
            row.extent !== other.extent || row.owner !== other.owner ||
            row.generationLow !== other.generationLow || row.generationHigh !== other.generationHigh;
        })) {
      throw new Error('Audio owner attribution requires same-range, same-owner rows at both boundaries');
    }
    audioRows = before.rows;
  }
  const arenaEnd = arenaRange.base + arenaRange.bytes;
  const rows = manifestRows.filter(row => row.size > 0).map(row => ({
    start: row.address, end: row.address + row.size, row,
  })).sort((a, b) => a.start - b.start || a.end - b.end);
  const prefixMaxEnd = [];
  for (let index = 0, maximum = 0; index < rows.length; index++) {
    maximum = Math.max(maximum, rows[index].end);
    prefixMaxEnd.push(maximum);
  }
  const ownedRanges = audioRows.map(row => ({
    start: row.address, end: row.address + row.extent, row, ownerType: 'audio',
  })).sort((a, b) => a.start - b.start || a.end - b.end);
  let matchContextRows = [];
  if (matchContextInventoryTransition !== null) {
    const {before, after} = matchContextInventoryTransition || {};
    if (!before || !after || !Array.isArray(before.rows) || !Array.isArray(after.rows) ||
        before.count !== 1 || after.count !== 1 || before.rows.length !== 1 ||
        after.rows.length !== 1 ||
        before.owner !== after.owner ||
        before.worldGenerationLow !== after.worldGenerationLow ||
        before.worldGenerationHigh !== after.worldGenerationHigh ||
        before.ownerEpochLow !== after.ownerEpochLow ||
        before.ownerEpochHigh !== after.ownerEpochHigh ||
        before.rows.some((row, index) => {
          const other = after.rows[index];
          return row.kind !== other.kind || row.address !== other.address ||
            row.extent !== other.extent || row.owner !== other.owner ||
            row.worldGenerationLow !== other.worldGenerationLow ||
            row.worldGenerationHigh !== other.worldGenerationHigh ||
            row.ownerEpochLow !== other.ownerEpochLow ||
            row.ownerEpochHigh !== other.ownerEpochHigh;
        })) {
      throw new Error('Match-context owner attribution requires same-range, same-owner, same-lifetime rows at both boundaries');
    }
    matchContextRows = before.rows;
    ownedRanges.push(...matchContextRows.map(row => ({
      start: row.address, end: row.address + row.extent, row, ownerType: 'match-context',
    })));
    ownedRanges.sort((a, b) => a.start - b.start || a.end - b.end);
  }
  let auroraShadowRows = [];
  let auroraDiagnosticOutput = null;
  if (auroraInventoryTransition !== null) {
    const {before, after} = auroraInventoryTransition || {};
    const sameIdentity = before && after &&
      before.version === after.version && before.size === after.size &&
      before.frameSlotCount === after.frameSlotCount && before.stagingSlotCount === after.stagingSlotCount &&
      before.stagingGenerationLow === after.stagingGenerationLow &&
      before.stagingGenerationHigh === after.stagingGenerationHigh &&
      before.diagnosticOutputRange?.base === after.diagnosticOutputRange?.base &&
      before.diagnosticOutputRange?.bytes === after.diagnosticOutputRange?.bytes &&
      before.rows?.length === after.rows?.length && before.ranges?.length === after.ranges?.length &&
      before.rows.every((row, index) => {
        const other = after.rows[index];
        return row.kind === other.kind && row.frameSlot === other.frameSlot &&
          row.streamIndex === other.streamIndex && row.address === other.address &&
          row.extent === other.extent && row.maxCapacity === other.maxCapacity &&
          row.owned === other.owned && row.ownerAddress === other.ownerAddress &&
          row.generationLow === other.generationLow && row.generationHigh === other.generationHigh;
      });
    if (!sameIdentity) {
      throw new Error('Aurora staging-shadow attribution requires same owner address, capacity, topology, and lifetime');
    }
    auroraShadowRows = before.ranges;
    auroraDiagnosticOutput = {start: before.diagnosticOutputRange.base,
      end: before.diagnosticOutputRange.base + before.diagnosticOutputRange.bytes};
    ownedRanges.push(...auroraShadowRows.map(row => ({
      start: row.address, end: row.address + row.extent, row,
      ownerType: 'aurora-staging-shadow',
    })));
    ownedRanges.sort((a, b) => a.start - b.start || a.end - b.end);
  }
  let nativeOutput = null;
  if (nativeOutputRange !== null) {
    if (!nativeOutputRange || !Number.isSafeInteger(nativeOutputRange.base) ||
        !Number.isSafeInteger(nativeOutputRange.bytes) || nativeOutputRange.base < 0 ||
        nativeOutputRange.bytes <= 0 || nativeOutputRange.base > 0x100000000 ||
        nativeOutputRange.bytes > 0x100000000 - nativeOutputRange.base) {
      throw new TypeError('Native PCM diagnostic range is malformed');
    }
    nativeOutput = {start: nativeOutputRange.base,
      end: nativeOutputRange.base + nativeOutputRange.bytes};
  }
  const classified = [];
  for (const range of rawRanges) {
    if (!range || !Number.isSafeInteger(range.start) || !Number.isSafeInteger(range.end) ||
        range.start < 0 || range.end <= range.start) {
      throw new TypeError('Heap census contains an invalid changed range');
    }
    let low = 0, high = prefixMaxEnd.length;
    while (low < high) {
      const middle = (low + high) >>> 1;
      if (prefixMaxEnd[middle] <= range.start) low = middle + 1;
      else high = middle;
    }
    const overlapping = [];
    for (let index = low; index < rows.length && rows[index].start < range.end; index++) {
      if (range.start < rows[index].end) overlapping.push(rows[index]);
    }
    const boundaries = new Set([range.start, range.end]);
    for (const item of overlapping) {
      boundaries.add(Math.max(range.start, item.start));
      boundaries.add(Math.min(range.end, item.end));
    }
    const overlappingOwners = ownedRanges.filter(item =>
      range.start < item.end && item.start < range.end);
    for (const item of overlappingOwners) {
      boundaries.add(Math.max(range.start, item.start));
      boundaries.add(Math.min(range.end, item.end));
    }
    if (nativeOutput && nativeOutput.start < range.end && range.start < nativeOutput.end) {
      boundaries.add(Math.max(range.start, nativeOutput.start));
      boundaries.add(Math.min(range.end, nativeOutput.end));
    }
    if (auroraDiagnosticOutput && auroraDiagnosticOutput.start < range.end &&
        range.start < auroraDiagnosticOutput.end) {
      boundaries.add(Math.max(range.start, auroraDiagnosticOutput.start));
      boundaries.add(Math.min(range.end, auroraDiagnosticOutput.end));
    }
    if (arenaRange.base < range.end && range.start < arenaEnd) {
      boundaries.add(Math.max(range.start, arenaRange.base));
      boundaries.add(Math.min(range.end, arenaEnd));
    }
    const ordered = [...boundaries].sort((a, b) => a - b);
    for (let index = 0; index + 1 < ordered.length; index++) {
      const start = ordered[index], end = ordered[index + 1];
      if (start === end) continue;
      const matches = overlapping.filter(item => item.start <= start && end <= item.end);
      const ownerMatches = overlappingOwners.filter(item => item.start <= start && end <= item.end);
      const inArena = arenaRange.base <= start && end <= arenaEnd;
      const inNativeOutput = nativeOutput && nativeOutput.start <= start && end <= nativeOutput.end;
      const inAuroraDiagnosticOutput = auroraDiagnosticOutput &&
        auroraDiagnosticOutput.start <= start && end <= auroraDiagnosticOutput.end;
      let provenance, details = {};
      if (inNativeOutput) {
        provenance = 'native-output-storage-diagnostic-excluded';
      } else if (inAuroraDiagnosticOutput) {
        provenance = 'aurora-staging-shadow-descriptor-diagnostic-excluded';
      } else if (matches.length > 1 || ownerMatches.length > 1 ||
          (matches.length && ownerMatches.length) ||
          (matches.length && inArena) || (ownerMatches.length && inArena)) {
        provenance = 'ambiguous-overlap';
      } else if (ownerMatches.length === 1) {
        const {row, ownerType} = ownerMatches[0];
        if (ownerType === 'match-context') {
          provenance = 'match-context-owner-range';
          details = {
            matchContextOwnerAddress: row.owner,
            matchContextWorldGenerationTag: `${row.worldGenerationHigh.toString(16).padStart(8, '0')}${row.worldGenerationLow.toString(16).padStart(8, '0')}`,
            matchContextOwnerEpoch: `${row.ownerEpochHigh.toString(16).padStart(8, '0')}${row.ownerEpochLow.toString(16).padStart(8, '0')}`,
          };
        } else if (ownerType === 'aurora-staging-shadow') {
          provenance = 'aurora-staging-shadow-owner-range';
          details = {
            auroraStagingShadowKind: row.kindName,
            auroraFrameSlot: row.frameSlot,
            auroraStreamIndex: row.streamIndex,
            ownerAddress: row.ownerAddress,
            ownerLifetimeTag: `${row.generationHigh.toString(16).padStart(8, '0')}${row.generationLow.toString(16).padStart(8, '0')}`,
            ownerCapacity: row.capacity,
            ownerMaxCapacity: row.maxCapacity,
          };
        } else {
          provenance = 'audio-owner-range';
          details = {
            audioRangeKind: row.kindName,
            audioRangeKindValue: row.kind,
            ownerAddress: row.owner,
            ownerLifetimeTag: `${row.generationHigh.toString(16).padStart(8, '0')}${row.generationLow.toString(16).padStart(8, '0')}`,
          };
        }
      } else if (matches.length === 1) {
        const {row} = matches[0];
        provenance = row.decision === 'source-c-static-candidate' ?
          'source-c-static-candidate' : 'excluded-mutable-input-section';
        details = {
          source: row.source,
          inputObject: row.input_object,
          inputSection: row.input_section,
          exclusionReason: row.exclusion_reason,
        };
      } else if (inArena) {
        provenance = 'gameplay-arena';
      } else {
        provenance = 'unmapped';
      }
      classified.push({start, end, bytes: end - start,
        ...(range.kind ? {kind: range.kind} : {}), provenance,
        ownerRestoreAdmission: false, ...details});
    }
  }
  return classified;
}

export function validateSourceStateManifest(manifest) {
  const limit = 0x100000000;
  if (!manifest || manifest.schema !== 'melee-web-source-state-manifest-v1' ||
      manifest.target !== 'gameplay_slippi_synctest_browser' ||
      manifest.complete_game_state !== false ||
      !/^[0-9a-f]{64}$/.test(manifest.configuration_identity_sha256 || '') ||
      !manifest.configuration_identity ||
      manifest.configuration_identity.target !== 'gameplay_slippi_synctest_browser' ||
      !/^[0-9a-f]{64}$/.test(manifest.configuration_identity.wasm_sha256 || '') ||
      !Array.isArray(manifest.mutable_input_sections)) {
    throw new Error('Source-state manifest is malformed');
  }
  const rows = manifest.mutable_input_sections;
  if (!manifest.counts || !Number.isSafeInteger(manifest.counts.mutable_input_sections) ||
      !Number.isSafeInteger(manifest.counts.source_c_candidates) ||
      !Number.isSafeInteger(manifest.counts.excluded_mutable_input_sections) ||
      manifest.counts.mutable_input_sections !== rows.length || rows.length === 0) {
    throw new Error('Source-state manifest row count is inconsistent');
  }
  const intervals = [];
  for (const row of rows) {
    if (!row || !Number.isSafeInteger(row.address) || !Number.isSafeInteger(row.size) ||
        row.address < 0 || row.size < 0 || row.address > limit || row.size > limit - row.address ||
        !['source-c-static-candidate', 'excluded'].includes(row.decision) ||
        typeof row.input_object !== 'string' || row.input_object.length === 0 ||
        typeof row.input_section !== 'string' || row.input_section.length === 0 ||
        !('source' in row) || !(row.source === null || typeof row.source === 'string') ||
        !('exclusion_reason' in row) ||
        (row.decision === 'source-c-static-candidate' ? row.exclusion_reason !== null :
          typeof row.exclusion_reason !== 'string' || row.exclusion_reason.length === 0)) {
      throw new Error('Source-state manifest contains an invalid mutable input section');
    }
    if (row.size) intervals.push({start: row.address, end: row.address + row.size});
  }
  const sourceCStaticCandidates = rows.filter(row => row.decision === 'source-c-static-candidate').length;
  const excludedMutableInputSections = rows.filter(row => row.decision === 'excluded').length;
  if (sourceCStaticCandidates !== manifest.counts?.source_c_candidates ||
      excludedMutableInputSections !== manifest.counts?.excluded_mutable_input_sections) {
    throw new Error('Source-state manifest decision counts are inconsistent');
  }
  intervals.sort((a, b) => a.start - b.start || a.end - b.end);
  for (let i = 1; i < intervals.length; i++) {
    if (intervals[i].start < intervals[i - 1].end) {
      throw new Error('Source-state manifest mutable sections overlap');
    }
  }
  return {
    target: manifest.target,
    completeGameState: manifest.complete_game_state,
    configurationIdentitySha256: manifest.configuration_identity_sha256,
    wasmSha256: manifest.configuration_identity.wasm_sha256,
    mutableInputSections: rows.length,
    sourceCStaticCandidates,
    excludedMutableInputSections,
  };
}

export function validateCensusArenaDisjointness(manifestRows, arenaRange) {
  const limit = 0x100000000;
  if (!Array.isArray(manifestRows) ||
      !Number.isSafeInteger(arenaRange?.base) || !Number.isSafeInteger(arenaRange?.bytes) ||
      arenaRange.base < 0 || arenaRange.bytes <= 0 ||
      arenaRange.base > limit || arenaRange.bytes > limit - arenaRange.base) {
    throw new Error('Live gameplay arena range is malformed');
  }
  const intervals = manifestRows.filter(row => row.size > 0).map(row => ({
    start: row.address, end: row.address + row.size,
  }));
  const arenaEnd = arenaRange.base + arenaRange.bytes;
  if (intervals.some(row => row.start < arenaEnd && arenaRange.base < row.end)) {
    throw new Error('Live gameplay arena overlaps a manifest input section');
  }
}

export function validateCensusInventory(manifest, arenaRange) {
  const validated = validateSourceStateManifest(manifest);
  validateCensusArenaDisjointness(manifest.mutable_input_sections, arenaRange);
  return {
    mutableInputSections: validated.mutableInputSections,
    sourceCStaticCandidates: validated.sourceCStaticCandidates,
    excludedMutableInputSections: validated.excludedMutableInputSections,
    completeGameState: validated.completeGameState,
  };
}

// The browser adapter exposes a fixed wasm32 record. Decode each integer as a
// word; generation stays as two words so the full uint64 lifetime tag is never
// rounded through JavaScript Number.
export function decodeAudioOwnerInventory(heap, pointer) {
  const wireVersion = 1, wireHeaderBytes = 36, wireRowBytes = 24, wireCapacity = 9;
  const wireBytes = wireHeaderBytes + wireRowBytes * wireCapacity;
  const kindNames = {
    1: 'audio-owner', 2: 'audio-entries', 3: 'audio-bindings',
    4: 'audio-programs', 5: 'audio-words', 6: 'audio-starts',
    7: 'effects-owner', 8: 'effects-reverb-heap', 9: 'effects-delay-heap',
  };
  const limit = 0x100000000;
  if (!(heap instanceof Uint8Array) || heap.byteLength > limit ||
      !Number.isSafeInteger(pointer) || pointer <= 0 || pointer % 4 !== 0 ||
      pointer > heap.byteLength || wireBytes > heap.byteLength - pointer) {
    throw new Error('Audio owner inventory wire pointer or wasm bounds are invalid');
  }
  const view = new DataView(heap.buffer, heap.byteOffset + pointer, wireBytes);
  const version = view.getUint32(0, true);
  const size = view.getUint32(4, true);
  const count = view.getUint32(8, true);
  const rowSize = view.getUint32(12, true);
  const capacity = view.getUint32(16, true);
  const status = view.getInt32(20, true);
  const audioOwner = view.getUint32(24, true);
  const generationLow = view.getUint32(28, true);
  const generationHigh = view.getUint32(32, true);
  if (version !== wireVersion || size !== wireBytes ||
      rowSize !== wireRowBytes || capacity !== wireCapacity ||
      (count !== 6 && count !== 9) || status !== 1) {
    throw new Error(`Audio owner inventory wire layout/status is invalid: version=${version}, size=${size}, rowSize=${rowSize}, count=${count}, capacity=${capacity}, status=${status}`);
  }
  if (!audioOwner || (generationLow === 0 && generationHigh === 0)) {
    throw new Error('Audio owner inventory has an invalid owner or zero lifetime tag');
  }
  const rows = [];
  const intervals = [];
  for (let index = 0; index < count; index++) {
    const offset = wireHeaderBytes + index * wireRowBytes;
    const row = {
      kind: view.getUint32(offset, true),
      address: view.getUint32(offset + 4, true),
      extent: view.getUint32(offset + 8, true),
      owner: view.getUint32(offset + 12, true),
      generationLow: view.getUint32(offset + 16, true),
      generationHigh: view.getUint32(offset + 20, true),
    };
    const expectedKind = index + 1;
    const audioLifetimeMismatch = index < 6 &&
      (row.generationLow !== generationLow || row.generationHigh !== generationHigh);
    if (row.kind !== expectedKind || !kindNames[row.kind] ||
        !row.address || !row.extent || !row.owner ||
        row.address > limit || row.extent > limit - row.address ||
        row.address + row.extent > heap.byteLength ||
        audioLifetimeMismatch) {
      throw new Error(`Audio owner inventory row ${index} has an invalid kind, extent, owner, lifetime, or wasm bound`);
    }
    if (index >= 6 && row.generationLow === 0 && row.generationHigh === 0) {
      throw new Error(`FX owner inventory row ${index} has a zero lifetime tag`);
    }
    if (index < 6 && row.owner !== audioOwner) {
      throw new Error(`Audio owner inventory row ${index} has the wrong owner identity`);
    }
    if (index === 0 && row.address !== audioOwner) {
      throw new Error('Audio owner inventory does not identify its owner allocation');
    }
    if (index === 6 && (row.address !== row.owner || row.owner === audioOwner)) {
      throw new Error('FX owner inventory does not identify a delegated effects allocation');
    }
    if (index > 6 && (row.owner !== rows[6].owner ||
        row.generationLow !== rows[6].generationLow ||
        row.generationHigh !== rows[6].generationHigh)) {
      throw new Error('FX audio ranges disagree on their delegated owner identity or lifetime tag');
    }
    row.kindName = kindNames[row.kind];
    intervals.push({start: row.address, end: row.address + row.extent});
    rows.push(row);
  }
  intervals.sort((a, b) => a.start - b.start || a.end - b.end);
  for (let index = 1; index < intervals.length; index++) {
    if (intervals[index].start < intervals[index - 1].end) {
      throw new Error('Audio owner inventory ranges overlap each other');
    }
  }
  return {version, size, rowSize, capacity, count, status, audioOwner,
    generationLow, generationHigh, effectsEnabled: count === 9,
    rows, ownerRestoreAdmission: false};
}

export function validateAudioOwnerRangeDisjointness(inventory, manifestRows,
  arenaRange, nativeOutputRange, heapBytes) {
  const limit = 0x100000000;
  if (!inventory || !Array.isArray(inventory.rows) || !Array.isArray(manifestRows) ||
      !Number.isSafeInteger(heapBytes) || heapBytes < 0 || heapBytes > limit ||
      !Number.isSafeInteger(arenaRange?.base) || !Number.isSafeInteger(arenaRange?.bytes) ||
      arenaRange.base < 0 || arenaRange.bytes <= 0 || arenaRange.base > limit ||
      arenaRange.bytes > limit - arenaRange.base ||
      arenaRange.base + arenaRange.bytes > heapBytes ||
      !Number.isSafeInteger(nativeOutputRange?.base) || !Number.isSafeInteger(nativeOutputRange?.bytes) ||
      nativeOutputRange.base <= 0 || nativeOutputRange.bytes <= 0 ||
      nativeOutputRange.base > limit || nativeOutputRange.bytes > limit - nativeOutputRange.base ||
      nativeOutputRange.base + nativeOutputRange.bytes > heapBytes) {
    throw new Error('Audio owner, arena, or native output inventory bounds are invalid');
  }
  const arenaEnd = arenaRange.base + arenaRange.bytes;
  const outputEnd = nativeOutputRange.base + nativeOutputRange.bytes;
  const staticIntervals = manifestRows.filter(row => row.size > 0).map(row => ({
    start: row.address, end: row.address + row.size,
  }));
  for (const row of inventory.rows) {
    const end = row.address + row.extent;
    if (staticIntervals.some(item => row.address < item.end && item.start < end)) {
      throw new Error(`Audio owner range ${row.kindName} overlaps a static manifest range`);
    }
    if (row.address < arenaEnd && arenaRange.base < end) {
      throw new Error(`Audio owner range ${row.kindName} overlaps the gameplay arena`);
    }
    if (row.address < outputEnd && nativeOutputRange.base < end) {
      throw new Error(`Audio owner range ${row.kindName} overlaps native PCM output storage`);
    }
  }
  if (nativeOutputRange.base < arenaEnd && arenaRange.base < outputEnd) {
    throw new Error('Native PCM output storage overlaps the gameplay arena');
  }
  return {base: nativeOutputRange.base, bytes: nativeOutputRange.bytes,
    diagnosticOnly: true, ownerRestoreAdmission: false};
}

export function requireStableAudioOwnerInventory(inventories) {
  if (!Array.isArray(inventories) || inventories.length !== 3 ||
      inventories.some(item => !item || !Array.isArray(item.rows))) {
    throw new Error('Audio owner inventory requires all three census boundaries');
  }
  const first = inventories[0];
  for (const inventory of inventories.slice(1)) {
    if (inventory.audioOwner !== first.audioOwner) {
      throw new Error('Audio owner address changed across census boundaries');
    }
    if (inventory.generationLow !== first.generationLow ||
        inventory.generationHigh !== first.generationHigh) {
      throw new Error('Audio owner lifetime generation changed across census boundaries');
    }
    if (first.count === 9 && inventory.count === 9 &&
        (inventory.rows[6].generationLow !== first.rows[6].generationLow ||
         inventory.rows[6].generationHigh !== first.rows[6].generationHigh)) {
      throw new Error('FX owner lifetime generation changed across census boundaries');
    }
    if (inventory.count !== first.count || inventory.effectsEnabled !== first.effectsEnabled ||
        inventory.rows.length !== first.rows.length ||
        inventory.rows.some((row, index) => {
          const prior = first.rows[index];
          return row.kind !== prior.kind || row.address !== prior.address ||
            row.extent !== prior.extent || row.owner !== prior.owner ||
            row.generationLow !== prior.generationLow || row.generationHigh !== prior.generationHigh;
        })) {
      throw new Error('Audio owner inventory topology changed across census boundaries');
    }
  }
  return first.rows.map(row => ({...row}));
}

// One fixed record describes only the live MeleeWebMatchContext allocation.
// Both 64-bit identities remain split into words so JS never rounds them.
export function decodeMatchContextRangeInventory(heap, pointer) {
  const wireVersion = 1, wireHeaderBytes = 24, wireRowBytes = 32,
    wireCapacity = 1, wireBytes = wireHeaderBytes + wireRowBytes;
  const limit = 0x100000000;
  if (!(heap instanceof Uint8Array) || heap.byteLength > limit ||
      !Number.isSafeInteger(pointer) || pointer <= 0 || pointer % 4 !== 0 ||
      pointer > heap.byteLength || wireBytes > heap.byteLength - pointer) {
    throw new Error('Match-context range inventory wire pointer or wasm bounds are invalid');
  }
  const view = new DataView(heap.buffer, heap.byteOffset + pointer, wireBytes);
  const version = view.getUint32(0, true), size = view.getUint32(4, true);
  const count = view.getUint32(8, true), rowSize = view.getUint32(12, true);
  const capacity = view.getUint32(16, true), status = view.getInt32(20, true);
  if (version !== wireVersion || size !== wireBytes || count !== 1 ||
      rowSize !== wireRowBytes || capacity !== wireCapacity || status !== 1) {
    throw new Error(`Match-context range inventory wire layout/status is invalid: version=${version}, size=${size}, count=${count}, rowSize=${rowSize}, capacity=${capacity}, status=${status}`);
  }
  const offset = wireHeaderBytes;
  const row = {
    kind: view.getUint32(offset, true),
    address: view.getUint32(offset + 4, true),
    extent: view.getUint32(offset + 8, true),
    owner: view.getUint32(offset + 12, true),
    worldGenerationLow: view.getUint32(offset + 16, true),
    worldGenerationHigh: view.getUint32(offset + 20, true),
    ownerEpochLow: view.getUint32(offset + 24, true),
    ownerEpochHigh: view.getUint32(offset + 28, true),
  };
  const worldGenerationIsZero = row.worldGenerationLow === 0 && row.worldGenerationHigh === 0;
  const ownerEpochIsZero = row.ownerEpochLow === 0 && row.ownerEpochHigh === 0;
  if (row.kind !== 1 || !row.address || !row.extent || row.owner !== row.address ||
      row.address > limit || row.extent > limit - row.address ||
      row.address + row.extent > heap.byteLength || worldGenerationIsZero || ownerEpochIsZero) {
    throw new Error('Match-context range inventory row has invalid kind, owner, extent, lifetime, or wasm bounds');
  }
  row.kindName = 'match-context-owner';
  return {version, size, count, rowSize, capacity, status,
    owner: row.owner,
    worldGenerationLow: row.worldGenerationLow,
    worldGenerationHigh: row.worldGenerationHigh,
    ownerEpochLow: row.ownerEpochLow,
    ownerEpochHigh: row.ownerEpochHigh,
    rows: [row], ownerRestoreAdmission: false};
}

export function validateMatchContextRangeDisjointness(inventory, manifestRows,
  arenaRange, nativeOutputRange, heapBytes, audioInventory) {
  const limit = 0x100000000;
  if (!inventory || !Array.isArray(inventory.rows) || inventory.rows.length !== 1 ||
      inventory.count !== 1 || !Array.isArray(manifestRows) ||
      !Number.isSafeInteger(heapBytes) || heapBytes < 0 || heapBytes > limit ||
      !Number.isSafeInteger(arenaRange?.base) || !Number.isSafeInteger(arenaRange?.bytes) ||
      arenaRange.base < 0 || arenaRange.bytes <= 0 || arenaRange.base > limit ||
      arenaRange.bytes > limit - arenaRange.base || arenaRange.base + arenaRange.bytes > heapBytes ||
      !Number.isSafeInteger(nativeOutputRange?.base) || !Number.isSafeInteger(nativeOutputRange?.bytes) ||
      nativeOutputRange.base <= 0 || nativeOutputRange.bytes <= 0 ||
      nativeOutputRange.base > limit || nativeOutputRange.bytes > limit - nativeOutputRange.base ||
      nativeOutputRange.base + nativeOutputRange.bytes > heapBytes ||
      !audioInventory || !Array.isArray(audioInventory.rows)) {
    throw new Error('Match-context owner, arena, or native output inventory bounds are invalid');
  }
  const [row] = inventory.rows;
  const end = row.address + row.extent;
  const arenaEnd = arenaRange.base + arenaRange.bytes;
  const outputEnd = nativeOutputRange.base + nativeOutputRange.bytes;
  if (row.address < arenaEnd && arenaRange.base < end) {
    throw new Error('Match-context owner range overlaps the gameplay arena');
  }
  if (row.address < outputEnd && nativeOutputRange.base < end) {
    throw new Error('Match-context owner range overlaps native PCM output storage');
  }
  for (const section of manifestRows) {
    if (section.size > 0 && row.address < section.address + section.size &&
        section.address < end) {
      throw new Error('Match-context owner range overlaps a static manifest range');
    }
  }
  for (const audioRow of audioInventory.rows) {
    const audioEnd = audioRow.address + audioRow.extent;
    if (row.address < audioEnd && audioRow.address < end) {
      throw new Error('Match-context owner range overlaps an audio owner range');
    }
  }
  return {base: row.address, bytes: row.extent, owner: row.owner,
    diagnosticOnly: true, ownerRestoreAdmission: false};
}

export function requireStableMatchContextRangeInventory(inventories) {
  const boundaryOrder = ['before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence'];
  if (!Array.isArray(inventories) || inventories.length !== boundaryOrder.length ||
      inventories.some((item, index) => !item || !Array.isArray(item.rows) ||
        item.rows.length !== 1 || item.count !== 1 || item.boundary !== boundaryOrder[index])) {
    throw new Error('Match-context owner inventory requires all three ordered census boundaries');
  }
  const first = inventories[0];
  for (const inventory of inventories.slice(1)) {
    if (inventory.owner !== first.owner || inventory.rows[0].owner !== first.rows[0].owner) {
      throw new Error('Match-context owner address changed across census boundaries');
    }
    if (inventory.worldGenerationLow !== first.worldGenerationLow ||
        inventory.worldGenerationHigh !== first.worldGenerationHigh) {
      throw new Error('Match-context world generation changed across census boundaries');
    }
    if (inventory.ownerEpochLow !== first.ownerEpochLow ||
        inventory.ownerEpochHigh !== first.ownerEpochHigh) {
      throw new Error('Match-context owner epoch changed across census boundaries');
    }
    if (inventory.version !== first.version || inventory.size !== first.size ||
        inventory.rowSize !== first.rowSize || inventory.capacity !== first.capacity ||
        inventory.status !== first.status ||
        inventory.rows.some((row, index) => {
          const prior = first.rows[index];
          return row.kind !== prior.kind || row.address !== prior.address ||
            row.extent !== prior.extent || row.owner !== prior.owner ||
            row.worldGenerationLow !== prior.worldGenerationLow ||
            row.worldGenerationHigh !== prior.worldGenerationHigh ||
            row.ownerEpochLow !== prior.ownerEpochLow ||
            row.ownerEpochHigh !== prior.ownerEpochHigh;
        })) {
      throw new Error('Match-context owner inventory topology changed across census boundaries');
    }
  }
  return {...first.rows[0]};
}

const AURORA_QUIESCENCE_SCHEMA_VERSION = 5;
const AURORA_FRAME_DESCRIPTOR_BYTES = 464;
const AURORA_SUBMISSION_DESCRIPTOR_BYTES = 56;
const AURORA_HEADER_BYTES = 120;
const AURORA_STREAM_NAMES = ['verts', 'uniforms', 'indices', 'storage', 'textureUpload'];
const WASM32_LIMIT = 0x100000000;

function u64Words(view, offset) {
  return {low: view.getUint32(offset, true), high: view.getUint32(offset + 4, true)};
}

function isZeroU64(value) {
  return value.low === 0 && value.high === 0;
}

export function decodeAuroraStagingShadowInventory(heap, pointer) {
  if (!(heap instanceof Uint8Array) || heap.byteLength > WASM32_LIMIT ||
      !Number.isSafeInteger(pointer) || pointer <= 0 || pointer % 8 !== 0 ||
      pointer > heap.byteLength || AURORA_HEADER_BYTES > heap.byteLength - pointer) {
    throw new Error('Aurora staging-shadow inventory wire pointer or wasm bounds are invalid');
  }
  const header = new DataView(heap.buffer, heap.byteOffset + pointer, AURORA_HEADER_BYTES);
  const version = header.getUint32(0, true), size = header.getUint32(4, true);
  const frameSlotCount = header.getUint32(8, true), stagingSlotCount = header.getUint32(12, true);
  const frameOffset = header.getUint32(16, true), frameStride = header.getUint32(20, true);
  const frameCount = header.getUint32(24, true), submissionOffset = header.getUint32(28, true);
  const submissionStride = header.getUint32(32, true), submissionCount = header.getUint32(36, true);
  const pendingFramePackets = header.getUint32(40, true);
  const pendingStagingBuffers = header.getUint32(44, true);
  const workerBusy = header.getUint32(48, true);
  const pipelineCacheStatus = header.getUint32(52, true);
  const queuedPipelines = header.getUint32(56, true);
  const createdPipelines = header.getUint32(60, true);
  const drawCallCount = header.getUint32(64, true);
  const currentFrameIndex = header.getUint32(68, true);
  const stagingGeneration = u64Words(header, 72);
  const lastBeginFrameId = u64Words(header, 80);
  const lastEndFrameId = u64Words(header, 88);
  const lastRegistrationSequence = u64Words(header, 96);
  const lastCallbackSequence = u64Words(header, 104);
  const staleCallbackCount = u64Words(header, 112);
  if (version !== AURORA_QUIESCENCE_SCHEMA_VERSION || frameSlotCount === 0 ||
      frameSlotCount > 4 || stagingSlotCount === 0 || stagingSlotCount > 4 ||
      frameCount !== frameSlotCount || submissionCount !== stagingSlotCount ||
      frameOffset !== AURORA_HEADER_BYTES || frameStride !== AURORA_FRAME_DESCRIPTOR_BYTES ||
      submissionStride !== AURORA_SUBMISSION_DESCRIPTOR_BYTES ||
      submissionOffset !== frameOffset + frameCount * frameStride ||
      size !== submissionOffset + submissionCount * submissionStride ||
      size > heap.byteLength - pointer || pendingFramePackets !== 0 ||
      pendingStagingBuffers !== 0 || workerBusy !== 0 || pipelineCacheStatus !== 0 ||
      queuedPipelines !== 0 || currentFrameIndex === 0xffffffff ||
      isZeroU64(stagingGeneration)) {
    throw new Error('Aurora staging-shadow inventory schema, count, offset, stride, or idle status is invalid');
  }
  const view = new DataView(heap.buffer, heap.byteOffset + pointer, size);
  const rows = [], ranges = [];
  const frameOwners = [];
  for (let frameIndex = 0; frameIndex < frameCount; frameIndex++) {
    const base = frameOffset + frameIndex * frameStride;
    const frameSlot = view.getUint32(base, true);
    const packetActive = view.getUint32(base + 4, true);
    const packetStagingBuffer = view.getUint32(base + 8, true);
    const reserved = view.getUint32(base + 12, true);
    const packetFrameId = u64Words(view, base + 16);
    if (frameSlot !== frameIndex || packetActive !== 0 || packetStagingBuffer !== 0xffffffff ||
        reserved !== 0 || !isZeroU64(packetFrameId)) {
      throw new Error('Aurora staging-shadow frame row is active or has invalid identity');
    }
    const previousUsed = AURORA_STREAM_NAMES.map((_, streamIndex) => {
      const value = u64Words(view, base + 24 + streamIndex * 8);
      if (value.high !== 0) throw new Error('Aurora staging-shadow previousUsed exceeds wasm32');
      return value.low;
    });
    const packetBuffers = [], shadowBuffers = [];
    const packetStart = base + 64;
    const shadowStart = packetStart + AURORA_STREAM_NAMES.length * 40;
    for (let streamIndex = 0; streamIndex < AURORA_STREAM_NAMES.length; streamIndex++) {
      const packetOffset = packetStart + streamIndex * 40;
      const packet = {
        dataAddress: u64Words(view, packetOffset), length: u64Words(view, packetOffset + 8),
        capacity: u64Words(view, packetOffset + 16), maxCapacity: u64Words(view, packetOffset + 24),
        owned: view.getUint32(packetOffset + 32, true), reserved: view.getUint32(packetOffset + 36, true),
      };
      if (!isZeroU64(packet.dataAddress) || !isZeroU64(packet.length) ||
          !isZeroU64(packet.capacity) || packet.maxCapacity.high !== 0 ||
          packet.maxCapacity.low >= WASM32_LIMIT || packet.owned !== 1 || packet.reserved !== 0) {
        throw new Error('Aurora staging-shadow packet buffer is not empty at the idle boundary');
      }
      packetBuffers.push(packet);

      const shadowOffset = shadowStart + streamIndex * 40;
      const address = u64Words(view, shadowOffset);
      const length = u64Words(view, shadowOffset + 8);
      const capacity = u64Words(view, shadowOffset + 16);
      const maxCapacity = u64Words(view, shadowOffset + 24);
      const owned = view.getUint32(shadowOffset + 32, true);
      const shadowReserved = view.getUint32(shadowOffset + 36, true);
      if (address.high !== 0 || length.high !== 0 || capacity.high !== 0 || maxCapacity.high !== 0 ||
          owned !== 1 || shadowReserved !== 0 || length.low > capacity.low ||
          capacity.low > maxCapacity.low || maxCapacity.low >= WASM32_LIMIT ||
          previousUsed[streamIndex] > capacity.low) {
        throw new Error('Aurora staging-shadow owner row has invalid kind, lifetime extent, or wasm32 bounds');
      }
      if (capacity.low === 0) {
        if (address.low !== 0 || length.low !== 0) {
          throw new Error('Aurora empty staging-shadow row has a nonempty address or length');
        }
      } else if (address.low === 0 || address.low > WASM32_LIMIT ||
                 capacity.low > WASM32_LIMIT - address.low ||
                 address.low + capacity.low > heap.byteLength) {
        throw new Error('Aurora staging-shadow capacity lies outside wasm32 memory');
      }
      const row = {
        kind: streamIndex + 1,
        kindName: `aurora-staging-shadow-${AURORA_STREAM_NAMES[streamIndex]}`,
        frameSlot, streamIndex, streamName: AURORA_STREAM_NAMES[streamIndex],
        address: address.low, owner: address.low, ownerAddress: address.low,
        length: length.low, extent: capacity.low, capacity: capacity.low,
        maxCapacity: maxCapacity.low, previousUsed: previousUsed[streamIndex],
        owned, generationLow: stagingGeneration.low, generationHigh: stagingGeneration.high,
      };
      shadowBuffers.push(row);
      if (row.extent > 0) {
        const end = row.address + row.extent;
        for (const prior of frameOwners) {
          if (row.address < prior.end && prior.start < end) {
            throw new Error('Aurora staging-shadow capacity ranges overlap each other');
          }
        }
        frameOwners.push({start: row.address, end});
        ranges.push(row);
      }
      rows.push(row);
    }
  }
  const submissionRows = [];
  for (let slot = 0; slot < submissionCount; slot++) {
    const base = submissionOffset + slot * submissionStride;
    const row = {
      stagingSlot: view.getUint32(base, true), state: view.getUint32(base + 4, true),
      futureId: u64Words(view, base + 8),
      registrationSequence: u64Words(view, base + 16),
      callbackSequence: u64Words(view, base + 24),
      capturedGeneration: u64Words(view, base + 32),
      queueHandle: u64Words(view, base + 40),
      encoderHandle: u64Words(view, base + 48),
    };
    if (row.stagingSlot !== slot || row.state > 5 || row.state === 1) {
      throw new Error('Aurora staging-shadow submission row has invalid slot or pending callback state');
    }
    submissionRows.push(row);
  }
  return {
    version, size, frameSlotCount, stagingSlotCount, frameDescriptorCount: frameCount,
    submissionDescriptorCount: submissionCount, frameStride, submissionStride,
    pendingFramePackets, pendingStagingBuffers, workerBusy, pipelineCacheStatus,
    queuedPipelines, createdPipelines, drawCallCount, currentFrameIndex,
    stagingGenerationLow: stagingGeneration.low, stagingGenerationHigh: stagingGeneration.high,
    lastBeginFrameId, lastEndFrameId, lastRegistrationSequence, lastCallbackSequence,
    staleCallbackCount, rows, ranges, submissionRows,
    diagnosticOutputRange: {base: pointer, bytes: size}, ownerRestoreAdmission: false,
  };
}

export function validateAuroraStagingShadowRangeDisjointness(inventory, manifestRows,
  arenaRange, nativeOutputRange, heapBytes, audioInventory, matchContextInventory) {
  if (!inventory || inventory.version !== AURORA_QUIESCENCE_SCHEMA_VERSION ||
      !Array.isArray(inventory.rows) || !Array.isArray(inventory.ranges) ||
      inventory.rows.length !== inventory.frameSlotCount * AURORA_STREAM_NAMES.length ||
      !Array.isArray(manifestRows) || !audioInventory || !Array.isArray(audioInventory.rows) ||
      !matchContextInventory || !Array.isArray(matchContextInventory.rows) ||
      !Number.isSafeInteger(heapBytes) || heapBytes < 0 || heapBytes > WASM32_LIMIT ||
      !Number.isSafeInteger(arenaRange?.base) || !Number.isSafeInteger(arenaRange?.bytes) ||
      arenaRange.base < 0 || arenaRange.bytes <= 0 || arenaRange.base > WASM32_LIMIT ||
      arenaRange.bytes > WASM32_LIMIT - arenaRange.base ||
      arenaRange.base + arenaRange.bytes > heapBytes ||
      !Number.isSafeInteger(nativeOutputRange?.base) || !Number.isSafeInteger(nativeOutputRange?.bytes) ||
      nativeOutputRange.base <= 0 || nativeOutputRange.bytes <= 0 ||
      nativeOutputRange.base > WASM32_LIMIT || nativeOutputRange.bytes > WASM32_LIMIT - nativeOutputRange.base ||
      nativeOutputRange.base + nativeOutputRange.bytes > heapBytes ||
      !Number.isSafeInteger(inventory.diagnosticOutputRange?.base) ||
      !Number.isSafeInteger(inventory.diagnosticOutputRange?.bytes) ||
      inventory.diagnosticOutputRange.base <= 0 || inventory.diagnosticOutputRange.bytes <= 0 ||
      inventory.diagnosticOutputRange.base > WASM32_LIMIT ||
      inventory.diagnosticOutputRange.bytes > WASM32_LIMIT - inventory.diagnosticOutputRange.base ||
      inventory.diagnosticOutputRange.base + inventory.diagnosticOutputRange.bytes > heapBytes) {
    throw new Error('Aurora staging-shadow, arena, or diagnostic output inventory bounds are invalid');
  }
  const overlaps = (leftBase, leftBytes, rightBase, rightBytes) =>
    leftBytes > 0 && rightBytes > 0 && leftBase < rightBase + rightBytes && rightBase < leftBase + leftBytes;
  // Bind precisely the one private producer-owned diagnostic allocation.
  // These maxima are the authored wasm32 FrameSlotCount/StagingBufferCount (4/4).
  const storageBytes = 120 + 4 * 464 + 4 * 56;
  const storageRows = manifestRows.filter(section =>
    section.input_object === '.deps/aurora/libaurora_gx.a(frame.cpp.o)' &&
    section.input_section === '.bss.aurora_browser_quiescence_observer_storage');
  if (storageRows.length !== 1 || storageRows[0].decision !== 'excluded' ||
      storageRows[0].exclusion_reason !== 'archive-outside-generated-source-producers' ||
      storageRows[0].address !== inventory.diagnosticOutputRange.base ||
      storageRows[0].size !== storageBytes || inventory.diagnosticOutputRange.bytes > storageBytes ||
      storageBytes > heapBytes - storageRows[0].address) {
    throw new Error('Aurora diagnostic storage lacks its exact excluded manifest identity');
  }
  const storageRow = storageRows[0];
  const diagnosticOutput = {base: storageRow.address, bytes: storageRow.size};
  const knownRanges = [
    {label: 'gameplay arena', base: arenaRange.base, bytes: arenaRange.bytes},
    {label: 'native PCM output storage', base: nativeOutputRange.base, bytes: nativeOutputRange.bytes},
  ];
  for (const section of manifestRows) {
    if (!Number.isSafeInteger(section.address) || !Number.isSafeInteger(section.size) ||
        section.address < 0 || section.size < 0 || section.address > WASM32_LIMIT ||
        section.size > WASM32_LIMIT - section.address || section.address + section.size > heapBytes) {
      throw new Error('Aurora staging-shadow static manifest range is malformed');
    }
    if (section !== storageRow && section.size > 0) knownRanges.push({label: 'static manifest range',
      base: section.address, bytes: section.size});
  }
  for (const row of audioInventory.rows) {
    if (!Number.isSafeInteger(row.address) || !Number.isSafeInteger(row.extent) ||
        row.address < 0 || row.extent <= 0 || row.address > WASM32_LIMIT ||
        row.extent > WASM32_LIMIT - row.address || row.address + row.extent > heapBytes) {
      throw new Error('Aurora staging-shadow audio owner range is malformed');
    }
    knownRanges.push({label: 'audio owner range', base: row.address, bytes: row.extent});
  }
  for (const row of matchContextInventory.rows) {
    if (!Number.isSafeInteger(row.address) || !Number.isSafeInteger(row.extent) ||
        row.address < 0 || row.extent <= 0 || row.address > WASM32_LIMIT ||
        row.extent > WASM32_LIMIT - row.address || row.address + row.extent > heapBytes) {
      throw new Error('Aurora staging-shadow match-context range is malformed');
    }
    knownRanges.push({label: 'match-context owner range', base: row.address, bytes: row.extent});
  }
  for (const known of knownRanges) {
    if (overlaps(diagnosticOutput.base, diagnosticOutput.bytes, known.base, known.bytes)) {
      throw new Error(`Aurora staging-shadow diagnostic output overlaps ${known.label}`);
    }
  }
  for (const section of manifestRows) {
    if (section !== storageRow && section.size > 0 && overlaps(diagnosticOutput.base, diagnosticOutput.bytes,
        section.address, section.size)) {
      throw new Error('Aurora staging-shadow diagnostic output overlaps static manifest range');
    }
  }
  for (const row of [...audioInventory.rows, ...matchContextInventory.rows]) {
    if (overlaps(diagnosticOutput.base, diagnosticOutput.bytes, row.address, row.extent)) {
      throw new Error('Aurora staging-shadow diagnostic output overlaps an existing owner range');
    }
  }
  for (let index = 0; index < inventory.ranges.length; index++) {
    const row = inventory.ranges[index];
    if (!row || !Number.isSafeInteger(row.address) || !Number.isSafeInteger(row.extent) ||
        row.address <= 0 || row.extent <= 0 || row.address > WASM32_LIMIT ||
        row.extent > WASM32_LIMIT - row.address || row.address + row.extent > heapBytes ||
        row.ownerAddress !== row.address || row.owned !== 1 ||
        row.generationLow !== inventory.stagingGenerationLow ||
        row.generationHigh !== inventory.stagingGenerationHigh) {
      throw new Error('Aurora staging-shadow owner range is malformed');
    }
    if (overlaps(row.address, row.extent, diagnosticOutput.base, diagnosticOutput.bytes)) {
      throw new Error('Aurora staging-shadow owner range overlaps its diagnostic output');
    }
    for (let prior = 0; prior < index; prior++) {
      if (overlaps(row.address, row.extent, inventory.ranges[prior].address,
          inventory.ranges[prior].extent)) {
        throw new Error('Aurora staging-shadow capacity ranges overlap each other');
      }
    }
    for (const known of knownRanges) {
      if (overlaps(row.address, row.extent, known.base, known.bytes)) {
        throw new Error(`Aurora staging-shadow owner range overlaps ${known.label}`);
      }
    }
  }
  return {rangeCount: inventory.ranges.length, diagnosticOnly: true,
    diagnosticOutputStorage: {...diagnosticOutput}, ownerRestoreAdmission: false};
}

export function requireStableAuroraStagingShadowInventory(inventories) {
  const boundaryOrder = ['before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence'];
  if (!Array.isArray(inventories) || inventories.length !== boundaryOrder.length ||
      inventories.some((item, index) => !item || !Array.isArray(item.rows) ||
        !Array.isArray(item.ranges) || item.boundary !== boundaryOrder[index])) {
    throw new Error('Aurora staging-shadow inventory requires all three ordered census boundaries');
  }
  const first = inventories[0];
  for (const inventory of inventories.slice(1)) {
    if (inventory.stagingGenerationLow !== first.stagingGenerationLow ||
        inventory.stagingGenerationHigh !== first.stagingGenerationHigh) {
      throw new Error('Aurora staging-shadow lifetime generation changed across census boundaries');
    }
    const sameIdentity = first.version === inventory.version && first.size === inventory.size &&
      first.frameSlotCount === inventory.frameSlotCount &&
      first.stagingSlotCount === inventory.stagingSlotCount &&
      first.frameDescriptorCount === inventory.frameDescriptorCount &&
      first.submissionDescriptorCount === inventory.submissionDescriptorCount &&
      first.stagingGenerationLow === inventory.stagingGenerationLow &&
      first.stagingGenerationHigh === inventory.stagingGenerationHigh &&
      first.diagnosticOutputRange?.base === inventory.diagnosticOutputRange?.base &&
      first.diagnosticOutputRange?.bytes === inventory.diagnosticOutputRange?.bytes &&
      first.rows.length === inventory.rows.length && first.ranges.length === inventory.ranges.length &&
      first.rows.every((row, index) => {
        const other = inventory.rows[index];
        return row.kind === other.kind && row.frameSlot === other.frameSlot &&
          row.streamIndex === other.streamIndex && row.address === other.address &&
          row.extent === other.extent && row.maxCapacity === other.maxCapacity &&
          row.owned === other.owned && row.ownerAddress === other.ownerAddress &&
          row.generationLow === other.generationLow && row.generationHigh === other.generationHigh;
      });
    if (!sameIdentity) {
      throw new Error('Aurora staging-shadow owner topology or capacity changed across census boundaries');
    }
  }
  return first.ranges.map(row => ({...row}));
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

export function flatAssetHashes(root) {
  return Object.fromEntries(fs.readdirSync(root).sort().flatMap(name => {
    const full = path.join(root, name);
    return fs.statSync(full).isFile() ? [[name, fileSha(full)]] : [];
  }));
}

export function reportSourceHashes() {
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
    'src/slippi_profile/draw_diagnostics.h',
    'src/slippi_profile/draw_diagnostics.c',
    'src/slippi_profile/whispy_direction_fix.h',
    'src/slippi_profile/whispy_direction_fix.c',
    'src/slippi_profile/ps_camera_independent_monitor_fix.h',
    'src/slippi_profile/ps_camera_independent_monitor_fix.c',
    'src/slippi_profile/ucf_dbooc_fix.h',
    'src/slippi_profile/ucf_dbooc_fix.c',
    'src/slippi_profile/ucf_shield_drop_fix.h',
    'src/slippi_profile/ucf_shield_drop_fix.c',
    'src/slippi_profile/ucf_shield_drop_source_guards.h',
    'src/gameplay_trig.h',
    '.deps/melee/src/melee/ft/kinds/ftMario/forward.h',
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

export function serveBuild(buildRoot, fileNames) {
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

export async function runBrowserInit(page, origin, runtimeName) {
  return page.evaluate(async ({origin, runtimeName, seed, checkedSource,
    diagnosticSizeSource, drawDiagnosticBytes}) => {
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
    const checkDrawDiagnosticSize = new Function(`return (${diagnosticSizeSource})`)();
    const diagnosticSize = checkDrawDiagnosticSize(
      module._melee_web_synctest_draw_diagnostics_size(), drawDiagnosticBytes);
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
      drawDiagnostics: encodeBase64(module.HEAPU8.slice(
        module._melee_web_synctest_draw_diagnostics(),
        module._melee_web_synctest_draw_diagnostics() + diagnosticSize)),
      initial: Object.fromEntries(Object.entries(read()).map(([key, bytes]) => [key, encodeBase64(bytes)])),
      logs,
    };
    return init;
  }, {origin, runtimeName, seed: SOURCE_SEED, checkedSource: checkedAsyncCcall.toString(),
    diagnosticSizeSource: requireDrawDiagnosticSize.toString(),
    drawDiagnosticBytes: DRAW_DIAGNOSTIC_BYTES});
}

export async function runBrowserSample(page, sample, expectedInputMapping, expectedStocks,
  writeCensus = null) {
  return page.evaluate(async ({sample, expectedInputMapping, expectedStocks, checkedSource, inspectSource,
    inputMappingSource, diagnosticSizeSource, drawDiagnosticBytes, writeCensus,
    diffHeapRangesSource, classifyHeapRangesSource, decodeAudioInventorySource,
    validateAudioOwnerRangesSource, requireStableAudioInventorySource,
    decodeMatchContextRangeInventorySource, validateMatchContextRangeDisjointnessSource,
    requireStableMatchContextRangeInventorySource, decodeAuroraStagingShadowInventorySource,
    validateAuroraStagingShadowRangeDisjointnessSource,
    requireStableAuroraStagingShadowInventorySource, maxHeapBytes}) => {
    const {module} = window.__slippiSynctest;
    const checkedAsyncCcall = new Function(`return (${checkedSource})`)();
    const checked = (label, identifier, argumentTypes = [], args = []) =>
      checkedAsyncCcall(module, label, identifier, argumentTypes, args);
    const decision = module._melee_web_synctest_draw_decision(0);
    if (decision !== 1) throw new Error(`present tick draw policy returned ${decision}`);
    let heapBefore = null, heapAfterStep = null, censusBeforeSha256 = null;
    let censusAfterStepSha256 = null, censusAfterDrawSha256 = null;
    let censusStepDiff = null, censusDrawDiff = null;
    let censusStepRaw = null, censusDrawRaw = null;
    const audioInventoryBoundaries = writeCensus ? [] : null;
    const matchContextInventoryBoundaries = writeCensus ? [] : null;
    const auroraStagingShadowInventoryBoundaries =
      writeCensus?.requireAuroraStagingShadowInventory === true ? [] : null;
    let nativeOutputRange = null;
    let queryOwnerInventories = null, requireStableInventory = null,
      requireStableMatchContextInventory = null, requireStableAuroraInventory = null;
    const censusNativeTransferQuiescence = writeCensus ? {
      beforeStep: null, afterStep: null, afterActualDraw: null,
    } : null;
    if (writeCensus) {
      if (writeCensus.requireAudioInventory !== true) {
        throw new Error('Write census runner must require the audio owner inventory');
      }
      if (writeCensus.requireMatchContextInventory !== true) {
        throw new Error('Write census runner must require the match-context owner inventory');
      }
      for (const name of ['_melee_web_synctest_audio_range_inventory_query',
        '_melee_web_synctest_audio_range_inventory',
        '_melee_web_synctest_match_context_range_inventory_query',
        '_melee_web_synctest_match_context_range_inventory',
        '_melee_web_snapshot_pcm', '_melee_web_snapshot_pcm_size']) {
        if (typeof module[name] !== 'function') {
          const ownerLabel = name.includes('match_context') ? 'match-context owner' : 'audio owner';
          throw new Error(`Required ${ownerLabel} inventory export is unavailable: ${name}`);
        }
      }
      if (writeCensus.requireAuroraStagingShadowInventory &&
          typeof module._melee_web_synctest_aurora_staging_shadow_inventory_query !== 'function') {
        throw new Error('Required Aurora staging-shadow inventory export is unavailable: _melee_web_synctest_aurora_staging_shadow_inventory_query');
      }
      const arenaBase = module._melee_web_synctest_gameplay_arena_base();
      const arenaBytes = module._melee_web_synctest_gameplay_arena_bytes();
      if (arenaBase !== writeCensus.arenaRange.base || arenaBytes !== writeCensus.arenaRange.bytes) {
        throw new Error(`Live gameplay arena changed before census: ${arenaBase}/${arenaBytes}`);
      }
      if (!globalThis.crypto?.subtle) throw new Error('Browser Web Crypto is unavailable for census hashes');
      await checked('native transfer quiescence before census source step', 'melee_web_snapshot_quiescent');
      censusNativeTransferQuiescence.beforeStep = 'passed';
      const decodeInventory = new Function('heap', 'pointer',
        `return (${decodeAudioInventorySource})(heap, pointer)`);
      const validateOwnerRanges = new Function('inventory', 'manifestRows', 'arenaRange',
        'nativeOutputRange', 'heapBytes',
        `return (${validateAudioOwnerRangesSource})(inventory, manifestRows, arenaRange, nativeOutputRange, heapBytes)`);
      const decodeMatchContextInventory = new Function('heap', 'pointer',
        `return (${decodeMatchContextRangeInventorySource})(heap, pointer)`);
      const validateMatchContextDisjointness = new Function('inventory', 'manifestRows',
        'arenaRange', 'nativeOutputRange', 'heapBytes', 'audioInventory',
        `return (${validateMatchContextRangeDisjointnessSource})(inventory, manifestRows, arenaRange, nativeOutputRange, heapBytes, audioInventory)`);
      requireStableInventory = new Function('inventories',
        `return (${requireStableAudioInventorySource})(inventories)`);
      requireStableMatchContextInventory = new Function('inventories',
        `return (${requireStableMatchContextRangeInventorySource})(inventories)`);
      const decodeAuroraInventory = writeCensus.requireAuroraStagingShadowInventory ?
        new Function('heap', 'pointer',
          `const AURORA_QUIESCENCE_SCHEMA_VERSION = 5;
` +
          `const AURORA_FRAME_DESCRIPTOR_BYTES = 464;
` +
          `const AURORA_SUBMISSION_DESCRIPTOR_BYTES = 56;
` +
          `const AURORA_HEADER_BYTES = 120;
` +
          `const AURORA_STREAM_NAMES = ['verts','uniforms','indices','storage','textureUpload'];
` +
          `const WASM32_LIMIT = 0x100000000;
` +
          `const u64Words = (view, offset) => ({low:view.getUint32(offset,true),high:view.getUint32(offset+4,true)});
` +
          `const isZeroU64 = value => value.low === 0 && value.high === 0;
` +
          `return (${decodeAuroraStagingShadowInventorySource})(heap, pointer);`) : null;
      const validateAuroraOwnerRanges = writeCensus.requireAuroraStagingShadowInventory ?
        new Function('inventory', 'manifestRows', 'arenaRange', 'nativeOutputRange',
          'heapBytes', 'audioInventory', 'matchContextInventory',
          `const AURORA_QUIESCENCE_SCHEMA_VERSION = 5;
` +
          `const AURORA_STREAM_NAMES = ['verts','uniforms','indices','storage','textureUpload'];
` +
          `const WASM32_LIMIT = 0x100000000;
` +
          `return (${validateAuroraStagingShadowRangeDisjointnessSource})(inventory, manifestRows, arenaRange, nativeOutputRange, heapBytes, audioInventory, matchContextInventory);`) : null;
      requireStableAuroraInventory = writeCensus.requireAuroraStagingShadowInventory ?
        new Function('inventories',
          `return (${requireStableAuroraStagingShadowInventorySource})(inventories)`) : null;
      queryOwnerInventories = async boundary => {
        await checked(`audio owner inventory ${boundary}`,
          'melee_web_synctest_audio_range_inventory_query');
        const pointer = module._melee_web_synctest_audio_range_inventory();
        const inventory = decodeInventory(module.HEAPU8, pointer);
        const currentNativeOutput = {
          base: module._melee_web_snapshot_pcm(),
          bytes: module._melee_web_snapshot_pcm_size(),
        };
        if (nativeOutputRange && (nativeOutputRange.base !== currentNativeOutput.base ||
            nativeOutputRange.bytes !== currentNativeOutput.bytes)) {
          throw new Error('Native PCM diagnostic storage changed across census boundaries');
        }
        nativeOutputRange ||= currentNativeOutput;
        const nativeOutput = validateOwnerRanges(inventory, writeCensus.manifestRows,
          writeCensus.arenaRange, nativeOutputRange, module.HEAPU8.byteLength);
        audioInventoryBoundaries.push({boundary, ...inventory});
        await checked(`match-context owner inventory ${boundary}`,
          'melee_web_synctest_match_context_range_inventory_query');
        const matchContextPointer = module._melee_web_synctest_match_context_range_inventory();
        const matchContextInventory = decodeMatchContextInventory(module.HEAPU8,
          matchContextPointer);
        validateMatchContextDisjointness(matchContextInventory, writeCensus.manifestRows,
          writeCensus.arenaRange, nativeOutputRange, module.HEAPU8.byteLength, inventory);
        const matchContextBoundary = {boundary, ...matchContextInventory,
          nativeOutputStorage: nativeOutput};
        matchContextInventoryBoundaries.push(matchContextBoundary);
        if (writeCensus.requireAuroraStagingShadowInventory) {
          const pointer = await module.ccall(
            'melee_web_synctest_aurora_staging_shadow_inventory_query',
            'number', [], [], {async: true});
          if (!Number.isSafeInteger(pointer) || pointer <= 0 || pointer >= 0x100000000) {
            throw new Error(`Aurora staging-shadow inventory ${boundary} refused or returned an invalid wire pointer`);
          }
          const auroraInventory = decodeAuroraInventory(module.HEAPU8, pointer);
          const diagnosticOutput = validateAuroraOwnerRanges(auroraInventory,
            writeCensus.manifestRows, writeCensus.arenaRange, nativeOutputRange,
            module.HEAPU8.byteLength, inventory, matchContextInventory);
          auroraStagingShadowInventoryBoundaries.push({boundary, ...auroraInventory,
            diagnosticOutputRange: diagnosticOutput.diagnosticOutputStorage,
            diagnosticOutputStorage: diagnosticOutput});
        }
      };
      await queryOwnerInventories('before-source-step');
      const heap = module.HEAPU8;
      if (!(heap instanceof Uint8Array) || heap.byteLength > maxHeapBytes) {
        throw new Error(`Unsupported census heap size: ${heap?.byteLength}`);
      }
      heapBefore = heap.slice();
      const digest = await globalThis.crypto.subtle.digest('SHA-256', heapBefore);
      censusBeforeSha256 = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
    }
    await checked(`draw-reference source tick ${sample}`,
      'melee_web_snapshot_step', ['number'], [sample]);
    if (writeCensus) {
      await checked('native transfer quiescence after census source step', 'melee_web_snapshot_quiescent');
      censusNativeTransferQuiescence.afterStep = 'passed';
      await queryOwnerInventories('after-source-step');
      heapAfterStep = module.HEAPU8.slice();
      if (heapAfterStep.byteLength > maxHeapBytes) {
        throw new Error(`Census heap grew beyond its bound: ${heapAfterStep.byteLength}`);
      }
      const digest = await globalThis.crypto.subtle.digest('SHA-256', heapAfterStep);
      censusAfterStepSha256 = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
      const makeDiff = new Function(`return (${diffHeapRangesSource})`)();
      const raw = makeDiff(heapBefore, heapAfterStep);
      censusStepRaw = {
        beforeHeapBytes: heapBefore.byteLength,
        afterHeapBytes: heapAfterStep.byteLength,
        beforeHeapSha256: censusBeforeSha256,
        afterHeapSha256: censusAfterStepSha256,
        changedBytes: raw.changedBytes,
        ranges: raw.ranges,
      };
      heapBefore = null;
    }
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
    if (writeCensus) {
      await checked('native transfer quiescence after census actual draw', 'melee_web_snapshot_quiescent');
      censusNativeTransferQuiescence.afterActualDraw = 'passed';
      await queryOwnerInventories('after-actual-draw-and-Aurora-quiescence');
      const heapAfterDraw = module.HEAPU8.slice();
      if (heapAfterDraw.byteLength > maxHeapBytes) {
        throw new Error(`Census heap grew beyond its bound: ${heapAfterDraw.byteLength}`);
      }
      const digest = await globalThis.crypto.subtle.digest('SHA-256', heapAfterDraw);
      censusAfterDrawSha256 = Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
      const makeDiff = new Function(`return (${diffHeapRangesSource})`)();
      requireStableInventory(audioInventoryBoundaries);
      requireStableMatchContextInventory(matchContextInventoryBoundaries);
      if (requireStableAuroraInventory) {
        requireStableAuroraInventory(auroraStagingShadowInventoryBoundaries);
      }
      const raw = makeDiff(heapAfterStep, heapAfterDraw);
      censusDrawRaw = {
        beforeHeapBytes: heapAfterStep.byteLength,
        afterHeapBytes: heapAfterDraw.byteLength,
        beforeHeapSha256: censusAfterStepSha256,
        afterHeapSha256: censusAfterDrawSha256,
        changedBytes: raw.changedBytes,
        ranges: raw.ranges,
      };
      const makeClassify = new Function('ranges', 'manifestRows', 'arenaRange',
        'audioInventoryTransition', 'nativeOutputRange', 'matchContextInventoryTransition',
        'auroraInventoryTransition',
        `return (${classifyHeapRangesSource})(ranges, manifestRows, arenaRange, audioInventoryTransition, nativeOutputRange, matchContextInventoryTransition, auroraInventoryTransition)`);
      censusStepDiff = {...censusStepRaw,
        changedRanges: makeClassify(censusStepRaw.ranges, writeCensus.manifestRows,
          writeCensus.arenaRange, {before: audioInventoryBoundaries[0],
            after: audioInventoryBoundaries[1]}, nativeOutputRange,
          {before: matchContextInventoryBoundaries[0],
            after: matchContextInventoryBoundaries[1]},
          auroraStagingShadowInventoryBoundaries ? {
            before: auroraStagingShadowInventoryBoundaries[0],
            after: auroraStagingShadowInventoryBoundaries[1],
          } : null)};
      censusDrawDiff = {...censusDrawRaw,
        changedRanges: makeClassify(censusDrawRaw.ranges, writeCensus.manifestRows,
          writeCensus.arenaRange, {before: audioInventoryBoundaries[1],
            after: audioInventoryBoundaries[2]}, nativeOutputRange,
          {before: matchContextInventoryBoundaries[1],
            after: matchContextInventoryBoundaries[2]},
          auroraStagingShadowInventoryBoundaries ? {
            before: auroraStagingShadowInventoryBoundaries[1],
            after: auroraStagingShadowInventoryBoundaries[2],
          } : null)};
      heapAfterStep = null;
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
    const checkDrawDiagnosticSize = new Function(`return (${diagnosticSizeSource})`)();
    const diagnosticSize = checkDrawDiagnosticSize(
      module._melee_web_synctest_draw_diagnostics_size(), drawDiagnosticBytes);
    const diagnostics = read(module._melee_web_synctest_draw_diagnostics(), diagnosticSize);
    const diagnosticView = new DataView(diagnostics.buffer, diagnostics.byteOffset, diagnostics.byteLength);
    const mappingValidator = new Function(`return (${inputMappingSource})`)();
    const makeInspector = new Function('DRAW_MOTION_BOUNDARY', 'validateSourceInputMapping',
      `return (${inspectSource})`);
    const inspectInteractiveObservation = makeInspector({
      interactive: 0, entry: 1, death: 2, respawn: 3, unknown: 4,
    }, mappingValidator);
    const boundary = inspectInteractiveObservation(observation, {
      classifyMotion: motion => module._melee_web_synctest_motion_boundary(motion),
      isMarioKind: kind => module._melee_web_synctest_is_mario_kind(kind) === 1,
      slotCount: diagnosticView.getUint32(0, true),
      expectedInputMapping, expectedStocks,
    });
    const encode = bytes => {
      let binary = '';
      for (let i = 0; i < bytes.length; i += 0x6000) {
        binary += String.fromCharCode(...bytes.subarray(i, Math.min(i + 0x6000, bytes.length)));
      }
      return btoa(binary);
    };
    const heapCensus = writeCensus ? {
      sample,
      arenaRange: writeCensus.arenaRange,
      audioOwnerInventory: {
        version: audioInventoryBoundaries[0].version,
        wireSize: audioInventoryBoundaries[0].size,
        rowSize: audioInventoryBoundaries[0].rowSize,
        boundaryOrder: audioInventoryBoundaries.map(item => item.boundary),
        boundaries: audioInventoryBoundaries,
        stableTopologyValidated: true,
        effectsEnabled: audioInventoryBoundaries[0].effectsEnabled,
        ownerRestoreAdmission: false,
      },
      matchContextOwnerInventory: {
        version: matchContextInventoryBoundaries[0].version,
        wireSize: matchContextInventoryBoundaries[0].size,
        rowSize: matchContextInventoryBoundaries[0].rowSize,
        boundaryOrder: matchContextInventoryBoundaries.map(item => item.boundary),
        boundaries: matchContextInventoryBoundaries,
        stableOwnerLifetimeValidated: true,
        ownerRestoreAdmission: false,
      },
      requireAuroraStagingShadowInventory: Boolean(auroraStagingShadowInventoryBoundaries),
      auroraStagingShadowOwnerInventory: auroraStagingShadowInventoryBoundaries ? {
        version: auroraStagingShadowInventoryBoundaries[0].version,
        wireSize: auroraStagingShadowInventoryBoundaries[0].size,
        frameSlotCount: auroraStagingShadowInventoryBoundaries[0].frameSlotCount,
        stagingSlotCount: auroraStagingShadowInventoryBoundaries[0].stagingSlotCount,
        frameDescriptorStride: auroraStagingShadowInventoryBoundaries[0].frameStride,
        submissionDescriptorStride: auroraStagingShadowInventoryBoundaries[0].submissionStride,
        boundaryOrder: auroraStagingShadowInventoryBoundaries.map(item => item.boundary),
        boundaries: auroraStagingShadowInventoryBoundaries,
        stableOwnerLifetimeValidated: true,
        ownerRestoreAdmission: false,
      } : null,
      nativeOutputStorage: {
        kind: 'fixture-native-pcm-output', ...nativeOutputRange,
        diagnosticOnly: true, ownerRestoreAdmission: false,
      },
      beforeStepSha256: censusBeforeSha256,
      afterStepSha256: censusAfterStepSha256,
      afterDrawSha256: censusAfterDrawSha256,
      nativeTransferQuiescence: censusNativeTransferQuiescence,
      sourceStep: censusStepDiff,
      actualDrawAndQuiescence: censusDrawDiff,
    } : null;
    return {observation: encode(observation), pcm: encode(pcm), pad: encode(pad),
      drawDiagnostics: encode(diagnostics), boundary, status, heapCensus};
  }, {sample, expectedInputMapping, expectedStocks, checkedSource: checkedAsyncCcall.toString(),
    inputMappingSource: validateSourceInputMapping.toString(),
    diagnosticSizeSource: requireDrawDiagnosticSize.toString(),
    inspectSource: inspectInteractiveObservation.toString(),
    drawDiagnosticBytes: DRAW_DIAGNOSTIC_BYTES, writeCensus,
    diffHeapRangesSource: diffHeapRanges.toString(),
    classifyHeapRangesSource: classifyHeapRanges.toString(),
    decodeAudioInventorySource: decodeAudioOwnerInventory.toString(),
    validateAudioOwnerRangesSource: validateAudioOwnerRangeDisjointness.toString(),
    requireStableAudioInventorySource: requireStableAudioOwnerInventory.toString(),
    decodeMatchContextRangeInventorySource: decodeMatchContextRangeInventory.toString(),
    validateMatchContextRangeDisjointnessSource: validateMatchContextRangeDisjointness.toString(),
    requireStableMatchContextRangeInventorySource: requireStableMatchContextRangeInventory.toString(),
    decodeAuroraStagingShadowInventorySource: decodeAuroraStagingShadowInventory.toString(),
    validateAuroraStagingShadowRangeDisjointnessSource: validateAuroraStagingShadowRangeDisjointness.toString(),
    requireStableAuroraStagingShadowInventorySource: requireStableAuroraStagingShadowInventory.toString(),
    maxHeapBytes: MAX_HEAP_BYTES});
}

export async function closeBrowser(page) {
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
    profile: 'slippi-render-dependencies-dbooc-shield-drop-v5',
    statement: 'Unofficial Slippi-compatible dependency diagnostic; not a complete Slippi or rollback profile.',
    scenario: 'two human Mario players / Final Destination / four stocks; deterministic fixture inputs',
    interval: {checkpointInputSample: PREFIX_SAMPLES, inputSamples: [...INTERVAL_SAMPLES],
      depth: INTERVAL_SAMPLES.length, prefixSamples: PREFIX_SAMPLES,
      checkpointRawBytes: null, comparedRawBytes: []},
    schedule: {
      drawnReference: 'source tick → source PCM → actual GameplayMatchSession::draw() inside Aurora begin/end frame → wait until Aurora frame packets, staging buffers, and worker are idle → refresh v2 observation',
      correction: 'source tick → source PCM → capture v2/PCM/raw PAD; source draw policy must return SKIP',
      initialEqualityBeforeSnapshot: true,
    },
    comparisonContract: {
      observation: {schema: 'melee-web-source-snapshot-observation-v2', bytes: OBSERVATION_BYTES},
      pcmBytes: PCM_BYTES,
      rawPadBytes: PAD_BYTES,
      drawDiagnosticBytes: DRAW_DIAGNOSTIC_BYTES,
      ticks: 'exact byte equality after each of input samples 300 and 301; each sample is checked independently',
      fighterPath: 'source callback entry and normal fighter-model dispatch-return counters must advance for both selected Mario fighters on each drawn sample; this is path reachability, not pixel acceptance',
      exactBytesAndSha256: true,
      fullHeap: 'Node no-draw lane checkpoint restore integrity and endpoint replay only; never compared with drawn browser heap',
    },
    sourceDraw: {drawnLane: true, correctionLane: false, calls: 0, completedFrames: 0,
      selectedFighterPathSamples: []},
    snapshotScope: 'The Node no-draw lane alone snapshots its complete Wasm linear memory. Aurora WebGPU, Chrome, OS/GPU caches, browser surface, pending submissions, DOM, Node process state, audio output, filesystem, and external allocator state are host-owned and are not snapshotted.',
      audio: 'All 4272 native PCM bytes are compared after each source tick, including the unused zero tail; the observation separately records actual pcmFrames (bounded to 534) and audioPhase (<60). Fixture PCM is not sent to Web Audio or host speakers.',
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
      drawDiagnostics: runtimeDrawDiagnostics(nodeModule),
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
      drawDiagnostics: decodeDrawDiagnostics(Buffer.from(browserInit.drawDiagnostics, 'base64')),
      initial: initialBrowser,
    };
    const identityFields = ['profile', 'source', 'rngHelper', 'stageBridge'];
    for (const field of identityFields) {
      if (nodeInit[field] !== browserInitRecord[field]) {
        throw new Error(`Initial ${field} identity differs: Node=${nodeInit[field]} browser=${browserInitRecord[field]}`);
      }
    }
    if (nodeInit.profile !== 'slippi-render-dependencies-dbooc-shield-drop-v5') {
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
    assertDrawDiagnosticsClear(nodeInit.drawDiagnostics, 'Node initialization');
    assertDrawDiagnosticsClear(browserInitRecord.drawDiagnostics, 'browser initialization');
    const initializerPlayers = nodeInit.initializer.players;
    if (!Array.isArray(initializerPlayers)) {
      throw new Error('Copied start-data initializer has no source-player mapping');
    }
    const expectedInputMapping = validateSourceInputMapping(
      initializerPlayers.map((player, sourceSlot) => ({
        sourceSlot, initializerPort: player.port, rawController: player.controller,
      })), nodeInit.drawDiagnostics.slotCount);
    const browserInputMapping = validateSourceInputMapping(
      browserInitRecord.initializer.players?.map((player, sourceSlot) => ({
        sourceSlot, initializerPort: player.port, rawController: player.controller,
      })), browserInitRecord.drawDiagnostics.slotCount);
    if (JSON.stringify(expectedInputMapping) !== JSON.stringify(browserInputMapping)) {
      throw new Error(`Copied start-data input maps differ by lane: node=${JSON.stringify(expectedInputMapping)} browser=${JSON.stringify(browserInputMapping)}`);
    }
    const expectedStocks = nodeInit.initializer.players?.map(player => player.stocks);
    if (!Array.isArray(expectedStocks) || expectedStocks.length !== 2 ||
        expectedStocks.some(stocks => stocks !== 4)) {
      throw new Error(`Fixture must initialize exactly four stocks: ${JSON.stringify(expectedStocks)}`);
    }
    compareLane(-1, 'initialization', nodeInit.initial, initialBrowser);
    report.initialization = {
      exactProfileSeedAndInitializer: true,
      sourceSeed: SOURCE_SEED,
      profileSeedResetOrCallback: false,
      profileIdentity: nodeInit.profile,
      sourceIdentity: nodeInit.source,
      sourceInputMapping: expectedInputMapping,
      sourceInputMappingEvidence: 'initializer JSON copied from start_data; rawController uses source.slot ? source.slot - 1 : sourceSlot, and match entry requires source_port == source_slot',
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
    let browserPrefixEndpoint = null;
    for (let sample = 0; sample < PREFIX_SAMPLES; sample++) {
      const decision = nodeModule._melee_web_synctest_draw_decision(1);
      if (decision !== 0) throw new Error(`Correction draw policy was ${decision} at sample ${sample}`);
      checkN(`no-draw source step ${sample}`, () => nodeModule._melee_web_snapshot_step(sample));
      if (sample === 0) checkN('no-draw native quiescence', () => nodeModule._melee_web_snapshot_quiescent());
      const noDraw = laneBytes(nodeModule);
      assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), `Node no-draw prefix sample ${sample}`);
      const drawn = await runBrowserSample(page, sample, expectedInputMapping, expectedStocks);
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
      const drawRef = browserSampleLane(drawn);
      if (sample === PREFIX_SAMPLES - 1) {
        report.interval.checkpointRawBytes = {sample,
          nodeNoDraw: rawLaneRecord(noDraw), browserDrawn: rawLaneRecord(drawRef),
          nodeBoundary: nodeInteractiveState(nodeModule, noDraw, expectedInputMapping, expectedStocks),
          browserBoundary: drawn.boundary, browserDrawDiagnostics: browserSampleDiagnostics(drawn)};
        writeReport();
      }
      if (sample === PREFIX_SAMPLES - 1) {
        const screenshot = path.join(options['--out'], 'drawn-reference-checkpoint-sample-299.png');
        await page.screenshot({path: screenshot, fullPage: true});
        const screenshotBytes = fs.readFileSync(screenshot);
        if (screenshotBytes.length < 100 || !screenshotBytes.subarray(0, 8).equals(
          Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
          throw new Error('Headless Chrome did not retain a valid sample-299 checkpoint PNG');
        }
        report.browser.screenshotCheckpointSample299 = {path: path.basename(screenshot),
          sha256: sha256(screenshotBytes), bytes: screenshotBytes.length};
      }
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
      browserPrefixEndpoint = drawn;

      writeReport();
    }

    const prefixCheckpoint = PREFIX_SAMPLES - 1;
    if (!browserPrefixEndpoint) throw new Error('Drawn reference did not reach the checkpoint input sample');
    const prefixNodeState = nodeInteractiveState(nodeModule, nodePrefix.at(-1), expectedInputMapping, expectedStocks);
    checksumRecord(prefixCheckpoint, nodePrefix.at(-1));
    checksumRecord(prefixCheckpoint, browserSampleLane(browserPrefixEndpoint));
    requireInteractive(prefixNodeState, 'node-no-draw', prefixCheckpoint, 'pre-interval-checkpoint');
    requireInteractive(browserPrefixEndpoint.boundary, 'browser-drawn-reference',
      prefixCheckpoint, 'pre-interval-checkpoint');
    const browserDrawBeforeInterval = browserSampleDiagnostics(browserPrefixEndpoint);
    if (browserDrawBeforeInterval.status !== 0) {
      throw new Error(`Draw diagnostics reported a status before interval: ${browserDrawBeforeInterval.status}`);
    }
    assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), 'Node checkpoint before snapshot');

    checkN('no-draw checkpoint quiescence', () => nodeModule._melee_web_snapshot_quiescent());
    nodeSnapshot = new QuiescentWasmSnapshot(nodeModule, MAX_HEAP_BYTES);
    const checkpoint = nodeSnapshot.capture();
    const checkpointHeap = Buffer.from(nodeModule.HEAPU8);
    const checkpointSha = sha256(checkpointHeap);
    report.nodeRestore = {
      checkpointInputSample: PREFIX_SAMPLES,
      intervalInputSamples: [...INTERVAL_SAMPLES],
      checkpointHeapBytes: checkpointHeap.byteLength,
      checkpointHeapSha256: checkpointSha,
      endpointHeapSha256: null,
      restoredCheckpointHeapSha256: null,
      replayEndpointHeapSha256: null,
      originalEndpointVsReplayExact: null,
      restoredCheckpointExact: null,
      result: 'incomplete',
    };
    const correctionStep = (sample, retain = () => {}) => {
      const decision = nodeModule._melee_web_synctest_draw_decision(1);
      if (decision !== 0) throw new Error(`Correction draw policy was ${decision} at sample ${sample}`);
      checkN(`correction interval step ${sample}`, () => nodeModule._melee_web_snapshot_step(sample));
      const lane = laneBytes(nodeModule);
      const state = nodeInteractiveState(nodeModule, lane, expectedInputMapping, expectedStocks);
      const diagnostics = runtimeDrawDiagnostics(nodeModule);
      report.nodeRestore.lastCorrectionSample = {sample,
        raw: rawLaneRecord(lane), boundary: state, drawDiagnostics: diagnostics};
      retain(lane, state, diagnostics);
      writeReport();
      checksumRecord(sample, lane); // Timing guard follows durable raw evidence.
      requireInteractive(state, 'node-no-draw', sample, 'interval-sample');
      assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), `Node no-draw interval sample ${sample}`);
      return lane;
    };
    const noDrawInterval = [];
    const drawnInterval = [];
    let browserDrawBaseline = browserDrawBeforeInterval;
    for (const sample of INTERVAL_SAMPLES) {
      const retained = {sample, comparisonStarted: false};
      report.interval.comparedRawBytes.push(retained);
      const noDrawStep = correctionStep(sample, (lane, state, diagnostics) => {
        retained.nodeNoDraw = rawLaneRecord(lane);
        retained.nodeBoundary = state;
        retained.nodeDrawDiagnostics = diagnostics;
        writeReport();
      });
      const drawnStep = await runBrowserSample(page, sample, expectedInputMapping, expectedStocks);
      const browserDrawCurrent = browserSampleDiagnostics(drawnStep);
      const drawnLane = browserSampleLane(drawnStep);
      retained.browserDrawn = rawLaneRecord(drawnLane);
      retained.browserBoundary = drawnStep.boundary;
      retained.browserDrawDiagnostics = browserDrawCurrent;
      writeReport();
      if (sample === INTERVAL_SAMPLES.at(-1)) {
        const screenshot = path.join(options['--out'], 'drawn-reference-sample-301.png');
        await page.screenshot({path: screenshot, fullPage: true});
        const screenshotBytes = fs.readFileSync(screenshot);
        if (screenshotBytes.length < 100 || !screenshotBytes.subarray(0, 8).equals(
          Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
          throw new Error('Headless Chrome did not retain a valid sample-301 PNG');
        }
        report.browser.screenshotSample301 = {path: path.basename(screenshot),
          sha256: sha256(screenshotBytes), bytes: screenshotBytes.length};
      }
      writeReport();
      previousBrowserFrame = validateAuroraFrame(drawnStep.status,
        previousBrowserFrame.beginFrameId, previousBrowserFrame.endFrameId);
      checksumRecord(sample, drawnLane);
      requireInteractive(drawnStep.boundary, 'browser-drawn-reference', sample, 'interval-sample');
      const selectedPaths = validateSelectedDrawPath(browserDrawBaseline,
        browserDrawCurrent, drawnStep.boundary.players, expectedInputMapping);
      retained.comparisonStarted = true;
      writeReport();
      compareLane(sample, 'drawn-reference versus no-draw interval', noDrawStep, drawnLane);
      report.interval.comparedRawBytes.at(-1).comparisonPassed = true;
      const pathRecord = {sample, sourceFrame: drawnStep.boundary.sourceFrame,
        fighters: selectedPaths};
      report.sourceDraw.selectedFighterPathSamples.push(pathRecord);
      noDrawInterval.push(noDrawStep);
      drawnInterval.push(drawnLane);
      const record = {
        ...checksumRecord(sample, noDrawStep),
        comparison: 'exact-v2-observation/PCM/raw-PAD bytes passed',
        drawCallCount: drawnStep.status.drawCallCount,
        createdPipelines: drawnStep.status.createdPipelines,
        queuedPipelines: drawnStep.status.queuedPipelines,
        selectedFighterPaths: selectedPaths,
      };
      report.samples.push(record);
      report.sourceDraw.calls++;
      report.sourceDraw.completedFrames++;
      browserDrawBaseline = browserDrawCurrent;

      writeReport();
    }

    const endpointHeap = Buffer.from(nodeModule.HEAPU8);
    const endpointSha = sha256(endpointHeap);
    report.nodeRestore.endpointHeapSha256 = endpointSha;
    report.interval.drawnReference = drawnInterval.map((lane, index) =>
      checksumRecord(INTERVAL_SAMPLES[index], lane));
    report.interval.noDrawBaseline = noDrawInterval.map((lane, index) =>
      checksumRecord(INTERVAL_SAMPLES[index], lane));
    report.interval.drawnReferenceAuroraFrames = report.samples
      .filter(record => INTERVAL_SAMPLES.includes(record.sample))
      .map(({sample, drawCallCount, createdPipelines, queuedPipelines}) =>
        ({sample, drawCallCount, createdPipelines, queuedPipelines}));
    writeReport();

    checkpoint.restore();
    const restoredCheckpointHeap = Buffer.from(nodeModule.HEAPU8);
    const restoredCheckpointSha = sha256(restoredCheckpointHeap);
    if (!checkpointHeap.equals(restoredCheckpointHeap)) {
      throw new Error(`No-draw full-heap restore did not reproduce checkpoint: ${JSON.stringify(firstDifference(checkpointHeap, restoredCheckpointHeap))}`);
    }
    assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), 'Node restored checkpoint');
    report.nodeRestore.restoredCheckpointHeapSha256 = restoredCheckpointSha;
    report.nodeRestore.restoredCheckpointExact = true;

    const rngPointer = nodeModule._melee_web_snapshot_rng_address();
    if (!Number.isInteger(rngPointer) || rngPointer < 0 || rngPointer + 4 > nodeModule.HEAPU8.byteLength) {
      throw new Error(`Source RNG pointer is outside no-draw Wasm memory: ${rngPointer}`);
    }
    const rngBefore = nodeModule.HEAPU8[rngPointer];
    nodeModule.HEAPU8[rngPointer] ^= 1;
    const corruptedSeedLowByte = nodeModule.HEAPU8[rngPointer];
    const corrupted = correctionStep(INTERVAL_SAMPLES[0]);
    assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), 'Node corrupted no-draw sample');
    const rngMismatch = firstDifference(noDrawInterval[0].observation, corrupted.observation);
    if (!rngMismatch) throw new Error('One-bit source RNG corruption was not detected by observation bytes');
    report.negatives.push({
      kind: 'source-RNG-bit corruption', result: 'detected', byteOffset: rngPointer,
      originalLowByteBeforeStep: rngBefore, corruptedLowByteBeforeStep: corruptedSeedLowByte,
      sourceLowByteAfterStep: nodeModule.HEAPU8[rngPointer],
      firstObservationDifference: rngMismatch,
      expectedObservationSha256: sha256(noDrawInterval[0].observation),
      actualObservationSha256: sha256(corrupted.observation),
    });
    writeReport();

    checkpoint.restore();
    const postCorruptionRestoreSha = sha256(nodeModule.HEAPU8);
    if (postCorruptionRestoreSha !== checkpointSha) {
      throw new Error(`Corruption recovery restore differs from checkpoint: ${postCorruptionRestoreSha}`);
    }
    assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), 'Node post-corruption restored checkpoint');
    const replayInterval = INTERVAL_SAMPLES.map(sample => correctionStep(sample));
    const replayHeap = Buffer.from(nodeModule.HEAPU8);
    const replaySha = sha256(replayHeap);
    for (let index = 0; index < INTERVAL_SAMPLES.length; index++) {
      compareLane(INTERVAL_SAMPLES[index], 'no-draw restored replay',
        noDrawInterval[index], replayInterval[index]);
    }
    if (!endpointHeap.equals(replayHeap)) {
      throw new Error(`No-draw endpoint heap differs after replay: ${JSON.stringify(firstDifference(endpointHeap, replayHeap))}`);
    }
    report.nodeRestore.replayEndpointHeapSha256 = replaySha;
    report.nodeRestore.originalEndpointVsReplayExact = true;

    checkpoint.restore();
    if (sha256(nodeModule.HEAPU8) !== checkpointSha) {
      throw new Error('Checkpoint heap changed before raw-PAD negative control');
    }
    const expectedPad = Buffer.from(noDrawInterval[0].pad);
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
    assertDrawDiagnosticsClear(runtimeDrawDiagnostics(nodeModule), 'Node raw-PAD negative control');
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
    const finalRecoveryInterval = INTERVAL_SAMPLES.map(sample => correctionStep(sample));
    const finalRecoveryHeapSha = sha256(nodeModule.HEAPU8);
    for (let index = 0; index < INTERVAL_SAMPLES.length; index++) {
      compareLane(INTERVAL_SAMPLES[index], 'post-negative-control recovery',
        noDrawInterval[index], finalRecoveryInterval[index]);
    }
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
    report.result = error.interactiveBoundaryRefusal ? 'refused' : 'failed';
    report.failure = error.synctestFailure || error.interactiveBoundaryRefusal ||
      {message: String(error?.stack || error)};
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

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => {
    process.stderr.write(`Slippi synctest failed: ${error?.stack || error}\n`);
    process.exitCode = 1;
  });
}
