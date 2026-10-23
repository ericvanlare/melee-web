// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {test} from 'node:test';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import {decodeDrawDiagnostics} from './runner_contract.mjs';
import {assembleCompletedCensusExecution, recordFailedCensusExecution,
  validateCensusDrawSample, validateCpuRestoreRouteSelection, validateBrowserProfileForRoute} from './run_write_census.mjs';
import {
  classifyHeapRanges, decodeAudioOwnerInventory, decodeMatchContextRangeInventory,
  decodeAuroraStagingShadowInventory, diffHeapRanges, requireStableAudioOwnerInventory,
  requireStableMatchContextRangeInventory, requireStableAuroraStagingShadowInventory,
  validateAudioOwnerRangeDisjointness, validateAuroraStagingShadowRangeDisjointness,
  validateMatchContextRangeDisjointness,
  decodeStageArenaOwnerWire, validateStageArenaOwnerCatalog,
  requireStableStageArenaOwnerInventory,
  validateCensusInventory, validateSourceStateManifest, runBrowserSample, closeBrowser,
} from './run_synctest.mjs';

function manifest(rows) {
  return {
    schema: 'melee-web-source-state-manifest-v1',
    target: 'gameplay_slippi_synctest_browser',
    complete_game_state: false,
    configuration_identity_sha256: 'a'.repeat(64),
    configuration_identity: {
      target: 'gameplay_slippi_synctest_browser',
      wasm_sha256: 'b'.repeat(64),
    },
    counts: {
      mutable_input_sections: rows.length,
      source_c_candidates: rows.filter(item => item.decision === 'source-c-static-candidate').length,
      excluded_mutable_input_sections: rows.filter(item => item.decision === 'excluded').length,
    },
    mutable_input_sections: rows,
  };
}

function row(address, size, decision, suffix) {
  return {
    address, size, decision,
    input_object: `libsource.a(${suffix}.o)`,
    input_section: `.data.${suffix}`,
    source: `src/${suffix}.c`,
    exclusion_reason: decision === 'excluded' ? 'outside-generated-source-root' : null,
  };
}

function audioWire({effects = false, addressMutation = null} = {}) {
  const heap = new Uint8Array(16384);
  const pointer = 8000;
  const view = new DataView(heap.buffer);
  const owner = 9000;
  const generationLow = 0x89abcdef;
  const generationHigh = 0x12345678;
  const fxGenerationLow = 0x76543210;
  const fxGenerationHigh = 0x0fedcba9;
  const addresses = [owner, 9200, 9300, 9400, 9500, 9600, 9700, 10000, 10100];
  const extents = [128, 64, 32, 64, 32, 16, 32, 16, 16];
  if (addressMutation) addresses[addressMutation.index] = addressMutation.address;
  const count = effects ? 9 : 6;
  for (const [offset, value] of [[0, 1], [4, 252], [8, count], [12, 24],
    [16, 9], [20, 1], [24, owner], [28, generationLow], [32, generationHigh]]) {
    view.setUint32(pointer + offset, value, true);
  }
  for (let index = 0; index < count; index++) {
    const offset = pointer + 36 + index * 24;
    const ownerIdentity = index < 6 ? owner : 9700;
    const rowGenerationLow = index < 6 ? generationLow : fxGenerationLow;
    const rowGenerationHigh = index < 6 ? generationHigh : fxGenerationHigh;
    for (const [field, value] of [[0, index + 1], [4, addresses[index]],
      [8, extents[index]], [12, ownerIdentity], [16, rowGenerationLow],
      [20, rowGenerationHigh]]) {
      view.setUint32(offset + field, value, true);
    }
  }
  return {heap, pointer};
}

function matchContextWire({address = 12000, owner = address, extent = 64,
  worldGenerationLow = 0x76543210, worldGenerationHigh = 0xabcdef01,
  ownerEpochLow = 0x11223344, ownerEpochHigh = 0x55667788,
  version = 1, size = 56, count = 1, rowSize = 32, capacity = 1,
  status = 1, kind = 1, pointer = 11000} = {}) {
  const heap = new Uint8Array(16384);
  const view = new DataView(heap.buffer);
  for (const [offset, value] of [[0, version], [4, size], [8, count],
    [12, rowSize], [16, capacity], [20, status], [24, kind], [28, address],
    [32, extent], [36, owner], [40, worldGenerationLow],
    [44, worldGenerationHigh], [48, ownerEpochLow], [52, ownerEpochHigh]]) {
    view.setUint32(pointer + offset, value, true);
  }
  return {heap, pointer};
}

function auroraStorageRow(address = 6800) {
  return {address, size: 2200, decision: 'excluded',
    exclusion_reason: 'archive-outside-generated-source-producers',
    input_object: '.deps/aurora/libaurora_gx.a(frame.cpp.o)',
    input_section: '.bss.aurora_browser_quiescence_observer_storage'};
}

function stageArenaDiagnosticRow(address = 11800) {
  return {address, size: 96, decision: 'excluded',
    exclusion_reason: 'not-a-known-source-archive-member',
    input_object: 'CMakeFiles/gameplay_slippi_synctest_browser.dir/src/slippi_profile/tests/synctest_browser.cpp.o',
    input_section: '.bss.melee_web_synctest_stage_arena_diagnostic_storage'};
}

function pcmStaticManifestRow(address = 854624) {
  return {address, size: 4272, decision: 'excluded',
    exclusion_reason: 'not-a-known-source-archive-member',
    input_object: 'CMakeFiles/gameplay_slippi_synctest_browser.dir/src/slippi_profile/tests/synctest_browser.cpp.o',
    input_section: '.bss._ZN12_GLOBAL__N_13pcmE'};
}

function stageArenaWire({pointer = 11832, ordinal = 0xffffffff, address = 0,
  extent = 0, worldGenerationLow = 0x33445566, worldGenerationHigh = 0x12345678,
  worldStorage = 800, arenaStorage = 900, identityToken = 1000,
  allocationCount = 2, totalBytes = 40, mutation = null} = {}) {
  const bytes = new Uint8Array(64), view = new DataView(bytes.buffer);
  const values = [1, 64, 1, 1, worldGenerationLow, worldGenerationHigh,
    worldStorage, arenaStorage, identityToken, allocationCount, totalBytes,
    ordinal, address, extent, 0, 0];
  if (mutation === 'version') values[0]++;
  if (mutation === 'size') values[1]++;
  if (mutation === 'status') values[2] = 0;
  if (mutation === 'kind') values[3] = 2;
  if (mutation === 'zero-generation') values[4] = values[5] = 0;
  if (mutation === 'zero-identity') values[8] = 0;
  if (mutation === 'max-count') values[9] = 65537;
  if (mutation === 'max-total') values[10] = 33554433;
  if (mutation === 'flags') values[14] = 1;
  if (mutation === 'reserved') values[15] = 1;
  if (mutation === 'metadata-row') values[12] = 1;
  if (mutation === 'row-zero-extent' && ordinal !== 0xffffffff) values[13] = 0;
  if (mutation === 'row-overflow' && ordinal !== 0xffffffff) { values[12] = 0xfffffff0; values[13] = 32; }
  for (let index = 0; index < values.length; index++) view.setUint32(index * 4, values[index] >>> 0, true);
  return {bytes, pointer};
}

function decodedStageWire(options) {
  const wire = stageArenaWire(options);
  return decodeStageArenaOwnerWire(wire.bytes, wire.pointer);
}

function writeU64(view, offset, low, high = 0) {
  view.setUint32(offset, low >>> 0, true);
  view.setUint32(offset + 4, high >>> 0, true);
}

function auroraWire({pointer = 6800, ordinal = 0, mutation = null} = {}) {
  const heap = new Uint8Array(16384);
  const view = new DataView(heap.buffer);
  const frameCount = 2, stagingCount = 2;
  const frameOffset = 120, frameStride = 464;
  const submissionOffset = frameOffset + frameCount * frameStride, submissionStride = 56;
  const byteSize = submissionOffset + stagingCount * submissionStride;
  const drawn = ordinal >= 2;
  let generationLow = 0x89abcdef, generationHigh = 0x12345678;
  if (mutation === 'generation' && drawn) generationLow++;
  if (mutation === 'generation-high' && drawn) generationHigh++;
  const writeHeader = (offset, value) => view.setUint32(pointer + offset, value >>> 0, true);
  writeHeader(0, mutation === 'version' ? 4 : 5);
  writeHeader(4, byteSize);
  writeHeader(8, frameCount);
  writeHeader(12, stagingCount);
  writeHeader(16, mutation === 'offset' ? frameOffset + 4 : frameOffset);
  writeHeader(20, mutation === 'stride' ? frameStride - 1 : frameStride);
  writeHeader(24, mutation === 'frame-count' ? frameCount - 1 : frameCount);
  writeHeader(28, submissionOffset);
  writeHeader(32, submissionStride);
  writeHeader(36, mutation === 'submission-count' ? stagingCount - 1 : stagingCount);
  for (const [offset, value] of [[40, mutation === 'pending-frame' ? 1 : 0],
    [44, mutation === 'pending-staging' ? 1 : 0],
    [48, mutation === 'worker-busy' ? 1 : 0],
    [52, mutation === 'pipeline-not-ready' ? 1 : 0],
    [56, mutation === 'queued-pipeline' ? 1 : 0], [60, 0], [64, 0]]) {
    writeHeader(offset, value);
  }
  writeHeader(68, mutation === 'current-frame-invalid' ? 0xffffffff : 1);
  writeU64(view, pointer + 72,
    mutation === 'zero-generation' ? 0 : generationLow,
    mutation === 'zero-generation' ? 0 : generationHigh);
  writeU64(view, pointer + 80, 300 + Number(drawn), 0);
  writeU64(view, pointer + 88, 300 + Number(drawn), 0);
  writeU64(view, pointer + 96, Number(drawn), 0);
  writeU64(view, pointer + 104, Number(drawn), 0);
  writeU64(view, pointer + 112, 0, 0);

  for (let slot = 0; slot < frameCount; slot++) {
    const base = pointer + frameOffset + slot * frameStride;
    view.setUint32(base, slot, true);
    view.setUint32(base + 4, mutation === 'packet-active' ? 1 : 0, true);
    view.setUint32(base + 8, 0xffffffff, true);
    view.setUint32(base + 12, 0, true);
    writeU64(view, base + 16, mutation === 'packet-active' ? 1 : 0, 0);
    for (let stream = 0; stream < 5; stream++) {
      const prior = slot === 0 && stream === 0 ?
        (mutation === 'row-topology' && drawn ? 0 : (drawn ? 32 : 16)) : 0;
      writeU64(view, base + 24 + stream * 8, prior, 0);
      const packet = base + 64 + stream * 40;
      writeU64(view, packet, 0, 0);
      writeU64(view, packet + 8, 0, 0);
      writeU64(view, packet + 16, 0, 0);
      writeU64(view, packet + 24, 0xffffffff, 0);
      view.setUint32(packet + 32, 1, true);
      view.setUint32(packet + 36, 0, true);

      const shadow = base + 264 + stream * 40;
      let address = slot === 0 && stream === 0 ? 13000 : 0;
      let length = slot === 0 && stream === 0 ? (drawn ? 32 : 16) : 0;
      let capacity = slot === 0 && stream === 0 ? 64 : 0;
      let maxCapacity = stream === 4 ? 0xffffffff : 128;
      let owned = 1;
      if (slot === 0 && stream === 0 && mutation === 'static-overlap') address = 15000;
      if (slot === 0 && stream === 0 && mutation === 'arena-overlap') address = 14000;
      if (slot === 0 && stream === 0 && mutation === 'native-overlap') address = 3000;
      if (slot === 0 && stream === 0 && mutation === 'audio-overlap') address = 9000;
      if (slot === 0 && stream === 0 && mutation === 'context-overlap') address = 12000;
      if (slot === 0 && stream === 0 && mutation === 'output-overlap') address = pointer + 16;
      if (slot === 0 && stream === 0 && mutation === 'overflow') address = 0xfffffff0;
      if (slot === 0 && stream === 0 && mutation === 'pointer' && drawn) address = 13008;
      if (slot === 0 && stream === 0 && mutation === 'capacity' && drawn) capacity = 96;
      if (slot === 0 && stream === 0 && mutation === 'max-capacity' && drawn) maxCapacity = 192;
      if (slot === 0 && stream === 0 && mutation === 'owned' && drawn) owned = 0;
      if (slot === 0 && stream === 0 && mutation === 'row-topology' && drawn) {
        address = 0; length = 0; capacity = 0;
      }
      if (slot === 0 && stream === 0 && mutation === 'high-word') {
        writeU64(view, shadow, address, 1);
      } else {
        writeU64(view, shadow, address, 0);
      }
      writeU64(view, shadow + 8, length, 0);
      writeU64(view, shadow + 16, capacity, 0);
      writeU64(view, shadow + 24, maxCapacity,
        mutation === 'max-high-word' && slot === 0 && stream === 0 ? 1 : 0);
      view.setUint32(shadow + 32, owned, true);
      view.setUint32(shadow + 36, 0, true);
      if (mutation === 'overlap' && slot === 0 && stream === 1) {
        writeU64(view, shadow, 13032, 0);
        writeU64(view, shadow + 16, 64, 0);
        writeU64(view, shadow + 24, 128, 0);
      }
      if (mutation === 'empty-pointer' && slot === 0 && stream === 1) {
        writeU64(view, shadow, 13100, 0);
      }
      if (mutation === 'packet-alias' && slot === 0 && stream === 0) {
        const packet = base + 64;
        writeU64(view, packet, 13000, 0);
        writeU64(view, packet + 16, 64, 0);
      }
    }
  }
  for (let slot = 0; slot < stagingCount; slot++) {
    const base = pointer + submissionOffset + slot * submissionStride;
    view.setUint32(base, slot, true);
    view.setUint32(base + 4, mutation === 'submission-state' ? 6 :
      mutation === 'pending-callback' ? 1 : 0, true);
    for (let offset = 8; offset < submissionStride; offset += 8) writeU64(view, base + offset, 0, 0);
  }
  return {heap, pointer, byteSize};
}

test('heap diff reports exact contiguous changed bytes and memory growth', () => {
  const before = Uint8Array.from([0, 1, 2, 3, 4, 5, 6, 7]);
  const after = Uint8Array.from([0, 9, 2, 8, 9, 5, 6, 7, 10, 11]);
  assert.deepEqual(diffHeapRanges(before, after), {
    changedBytes: 5,
    ranges: [
      {start: 1, end: 2, bytes: 1},
      {start: 3, end: 5, bytes: 2},
      {start: 8, end: 10, bytes: 2, kind: 'memory-growth'},
    ],
  });
});

test('changed bytes split across generated source, excluded input, arena and unmapped memory', () => {
  const rows = [row(0x104, 4, 'source-c-static-candidate', 'owned'),
    row(0x10a, 2, 'excluded', 'foreign')];
  const ranges = classifyHeapRanges([{start: 0x100, end: 0x130, bytes: 0x30}], rows,
    {base: 0x110, bytes: 8});
  assert.deepEqual(ranges.map(({start, end, provenance}) => [start, end, provenance]), [
    [0x100, 0x104, 'unmapped'],
    [0x104, 0x108, 'source-c-static-candidate'],
    [0x108, 0x10a, 'unmapped'],
    [0x10a, 0x10c, 'excluded-mutable-input-section'],
    [0x10c, 0x110, 'unmapped'],
    [0x110, 0x118, 'gameplay-arena'],
    [0x118, 0x130, 'unmapped'],
  ]);
  assert.ok(ranges.every(range => range.ownerRestoreAdmission === false));
  assert.equal(ranges.reduce((sum, item) => sum + item.bytes, 0), 0x30);
});

test('overlapping provenance is explicit ambiguity and never source-owner success', () => {
  const ranges = classifyHeapRanges([{start: 0x204, end: 0x206}], [
    row(0x200, 8, 'source-c-static-candidate', 'first'),
    row(0x204, 8, 'source-c-static-candidate', 'second'),
  ], {base: 0x300, bytes: 0x20});
  assert.deepEqual(ranges, [{start: 0x204, end: 0x206, bytes: 2,
    provenance: 'ambiguous-overlap', ownerRestoreAdmission: false}]);
});

test('inventory validation binds the partial manifest shape and rejects overlaps', () => {
  const validRows = [row(0x100, 4, 'source-c-static-candidate', 'owned'),
    row(0x108, 2, 'excluded', 'foreign')];
  assert.deepEqual(validateCensusInventory(manifest(validRows), {base: 0x200, bytes: 0x40}), {
    mutableInputSections: 2,
    sourceCStaticCandidates: 1,
    excludedMutableInputSections: 1,
    completeGameState: false,
  });
  assert.throws(() => validateCensusInventory({...manifest(validRows), complete_game_state: true},
    {base: 0x200, bytes: 0x40}), /malformed/);
  assert.throws(() => validateCensusInventory(manifest([
    row(0x100, 8, 'source-c-static-candidate', 'first'),
    row(0x104, 8, 'excluded', 'duplicate'),
  ]), {base: 0x200, bytes: 0x40}), /overlap/);
  assert.throws(() => validateCensusInventory(manifest(validRows), {base: 0x102, bytes: 0x20}),
    /arena overlaps/);
  assert.throws(() => validateCensusInventory(manifest([row(0xfffffff0, 0x20,
    'source-c-static-candidate', 'out-of-range')]), {base: 0x100, bytes: 0x20}), /invalid mutable input section/);
});

test('static prelaunch validation rejects malformed metadata and inconsistent count fields', () => {
  const valid = manifest([row(0x100, 4, 'source-c-static-candidate', 'owned'),
    row(0x108, 2, 'excluded', 'foreign')]);
  assert.deepEqual(validateSourceStateManifest(valid), {
    target: 'gameplay_slippi_synctest_browser',
    completeGameState: false,
    configurationIdentitySha256: 'a'.repeat(64),
    wasmSha256: 'b'.repeat(64),
    mutableInputSections: 2,
    sourceCStaticCandidates: 1,
    excludedMutableInputSections: 1,
  });
  assert.throws(() => validateSourceStateManifest({...valid, schema: 'wrong'}), /malformed/);
  assert.throws(() => validateSourceStateManifest({...valid, target: 'wrong'}), /malformed/);
  assert.throws(() => validateSourceStateManifest({...valid, complete_game_state: true}), /malformed/);
  assert.throws(() => validateSourceStateManifest({...valid,
    configuration_identity: {...valid.configuration_identity, target: 'wrong'}}), /malformed/);
  assert.throws(() => validateSourceStateManifest({...valid, configuration_identity_sha256: 'not-a-hash'}), /malformed/);
  assert.throws(() => validateSourceStateManifest({...valid,
    counts: {...valid.counts, mutable_input_sections: 3}}), /row count/);
  assert.throws(() => validateSourceStateManifest({...valid,
    counts: {...valid.counts, source_c_candidates: 2}}), /decision counts/);
  assert.throws(() => validateSourceStateManifest({...valid,
    counts: {...valid.counts, excluded_mutable_input_sections: 0}}), /decision counts/);
});

test('malformed changed ranges and non-byte inputs are refused', () => {
  assert.throws(() => diffHeapRanges([1, 2], Uint8Array.from([1, 3])), /Uint8Array/);
  assert.throws(() => classifyHeapRanges([{start: 4, end: 4}], [], {base: 8, bytes: 4}), /invalid changed range/);
});

test('audio wire preserves the full lifetime tag and names the FX-disabled topology', () => {
  const {heap, pointer} = audioWire();
  const inventory = decodeAudioOwnerInventory(heap, pointer);
  assert.equal(inventory.version, 1);
  assert.equal(inventory.size, 252);
  assert.equal(inventory.count, 6);
  assert.equal(inventory.effectsEnabled, false);
  assert.equal(inventory.generationHigh, 0x12345678);
  assert.equal(inventory.generationLow, 0x89abcdef);
  assert.deepEqual(inventory.rows.map(item => item.kindName), [
    'audio-owner', 'audio-entries', 'audio-bindings', 'audio-programs',
    'audio-words', 'audio-starts',
  ]);
  const ranges = validateAudioOwnerRangeDisjointness(inventory, [],
    {base: 14000, bytes: 100}, {base: 2048, bytes: 4272}, heap.byteLength);
  assert.deepEqual(ranges, {base: 2048, bytes: 4272,
    diagnosticOnly: true, ownerRestoreAdmission: false});
  assert.equal(requireStableAudioOwnerInventory([inventory, inventory, inventory]).length, 6);
  assert.throws(() => decodeAudioOwnerInventory(heap, pointer + 1), /wire pointer or wasm bounds/);
  assert.throws(() => decodeAudioOwnerInventory(heap, heap.byteLength - 4), /wire pointer or wasm bounds/);
  assert.throws(() => validateAudioOwnerRangeDisjointness(inventory, [row(9000, 128,
    'source-c-static-candidate', 'audio-overlap')], {base: 14000, bytes: 100},
  {base: 2048, bytes: 4272}, heap.byteLength), /static manifest range/);
  assert.throws(() => validateAudioOwnerRangeDisjointness(inventory, [],
    {base: 9000, bytes: 128}, {base: 2048, bytes: 4272}, heap.byteLength), /gameplay arena/);
  assert.throws(() => validateAudioOwnerRangeDisjointness(inventory, [],
    {base: 14000, bytes: 100}, {base: 9000, bytes: 128}, heap.byteLength), /native PCM output/);
});

test('audio inventory accepts delegated FX ranges and rejects unsupported cardinality or overlap', () => {
  const {heap, pointer} = audioWire({effects: true});
  const inventory = decodeAudioOwnerInventory(heap, pointer);
  assert.equal(inventory.effectsEnabled, true);
  assert.deepEqual(inventory.rows.slice(6).map(item => item.kindName), [
    'effects-owner', 'effects-reverb-heap', 'effects-delay-heap',
  ]);
  const {heap: overlappingHeap, pointer: overlappingPointer} = audioWire({
    addressMutation: {index: 1, address: 9064},
  });
  assert.throws(() => decodeAudioOwnerInventory(overlappingHeap, overlappingPointer), /overlap each other/);
  const {heap: wrongKindHeap, pointer: wrongKindPointer} = audioWire();
  new DataView(wrongKindHeap.buffer).setUint32(wrongKindPointer + 36, 8, true);
  assert.throws(() => decodeAudioOwnerInventory(wrongKindHeap, wrongKindPointer), /invalid kind/);
  const {heap: mismatchedFxHeap, pointer: mismatchedFxPointer} = audioWire({effects: true});
  new DataView(mismatchedFxHeap.buffer).setUint32(mismatchedFxPointer + 36 + 7 * 24 + 16,
    0x76543211, true);
  assert.throws(() => decodeAudioOwnerInventory(mismatchedFxHeap, mismatchedFxPointer),
    /FX audio ranges disagree on their delegated owner identity or lifetime tag/);
  const {heap: zeroFxHeap, pointer: zeroFxPointer} = audioWire({effects: true});
  const zeroFxView = new DataView(zeroFxHeap.buffer);
  zeroFxView.setUint32(zeroFxPointer + 36 + 6 * 24 + 16, 0, true);
  zeroFxView.setUint32(zeroFxPointer + 36 + 6 * 24 + 20, 0, true);
  assert.throws(() => decodeAudioOwnerInventory(zeroFxHeap, zeroFxPointer),
    /FX owner inventory row 6 has a zero lifetime tag/);
});

test('match-context wire retains exact one-owner extent and both full lifetime tags', () => {
  const {heap, pointer} = matchContextWire();
  const inventory = decodeMatchContextRangeInventory(heap, pointer);
  assert.deepEqual([inventory.version, inventory.size, inventory.count,
    inventory.rowSize, inventory.capacity, inventory.status], [1, 56, 1, 32, 1, 1]);
  assert.equal(inventory.ownerRestoreAdmission, false);
  assert.deepEqual([inventory.worldGenerationHigh, inventory.worldGenerationLow],
    [0xabcdef01, 0x76543210]);
  assert.deepEqual([inventory.ownerEpochHigh, inventory.ownerEpochLow],
    [0x55667788, 0x11223344]);
  assert.deepEqual(inventory.rows[0], {
    kind: 1, address: 12000, extent: 64, owner: 12000,
    worldGenerationLow: 0x76543210, worldGenerationHigh: 0xabcdef01,
    ownerEpochLow: 0x11223344, ownerEpochHigh: 0x55667788,
    kindName: 'match-context-owner',
  });
  const audioFixture = audioWire();
  const audio = decodeAudioOwnerInventory(audioFixture.heap, audioFixture.pointer);
  const excluded = validateMatchContextRangeDisjointness(inventory, [],
    {base: 14000, bytes: 100}, {base: 2048, bytes: 4272}, heap.byteLength, audio);
  assert.deepEqual(excluded, {base: 12000, bytes: 64, owner: 12000,
    diagnosticOnly: true, ownerRestoreAdmission: false});
  const boundaries = ['before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence'].map(boundary => ({...inventory, boundary}));
  assert.equal(requireStableMatchContextRangeInventory(boundaries).extent, 64);
  const changedRange = classifyHeapRanges([{start: 12004, end: 12005}], [],
    {base: 14000, bytes: 100}, null, {base: 2048, bytes: 4272}, {
      before: boundaries[0], after: boundaries[1],
    });
  assert.deepEqual(changedRange, [{start: 12004, end: 12005, bytes: 1,
    provenance: 'match-context-owner-range', ownerRestoreAdmission: false,
    matchContextOwnerAddress: 12000,
    matchContextWorldGenerationTag: 'abcdef0176543210',
    matchContextOwnerEpoch: '5566778811223344'}]);
});

test('match-context decoder and owner intervals reject malformed layout, lifetime, and overlap', () => {
  for (const options of [
    {version: 2}, {size: 55}, {count: 0}, {rowSize: 31}, {capacity: 2},
    {status: 0}, {kind: 2}, {owner: 12001},
    {worldGenerationLow: 0, worldGenerationHigh: 0},
    {ownerEpochLow: 0, ownerEpochHigh: 0},
    {address: 16380, owner: 16380, extent: 64},
  ]) {
    const {heap, pointer} = matchContextWire(options);
    assert.throws(() => decodeMatchContextRangeInventory(heap, pointer),
      /wire layout\/status|invalid kind, owner, extent, lifetime, or wasm bounds/);
  }
  const {heap, pointer} = matchContextWire();
  assert.throws(() => decodeMatchContextRangeInventory(heap, pointer + 1), /wire pointer or wasm bounds/);
  const context = decodeMatchContextRangeInventory(heap, pointer);
  const audioFixture = audioWire();
  const audio = decodeAudioOwnerInventory(audioFixture.heap, audioFixture.pointer);
  const {heap: audioOverlapHeap, pointer: audioOverlapPointer} = matchContextWire({
    address: 9000, owner: 9000,
  });
  assert.throws(() => validateMatchContextRangeDisjointness(
    decodeMatchContextRangeInventory(audioOverlapHeap, audioOverlapPointer), [],
    {base: 14000, bytes: 100}, {base: 2048, bytes: 4272},
    audioOverlapHeap.byteLength, audio), /overlaps an audio owner range/);
  assert.throws(() => validateMatchContextRangeDisjointness(context,
    [row(12032, 4, 'source-c-static-candidate', 'match-context-overlap')],
    {base: 14000, bytes: 100}, {base: 2048, bytes: 4272}, heap.byteLength,
    audio), /static manifest range/);
  assert.throws(() => validateMatchContextRangeDisjointness(context, [],
    {base: 12032, bytes: 100}, {base: 2048, bytes: 4272}, heap.byteLength,
    audio), /gameplay arena/);
  assert.throws(() => validateMatchContextRangeDisjointness(context, [],
    {base: 14000, bytes: 100}, {base: 12032, bytes: 100}, heap.byteLength,
    audio), /native PCM output storage/);
  const stable = ['before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence'].map(boundary => ({...context, boundary}));
  assert.throws(() => requireStableMatchContextRangeInventory(stable.map((item, index) =>
    index === 2 ? {...item, owner: item.owner + 4} : item)), /owner address changed/);
  assert.throws(() => requireStableMatchContextRangeInventory(stable.map((item, index) =>
    index === 2 ? {...item, worldGenerationLow: item.worldGenerationLow + 1} : item)),
  /world generation changed/);
  assert.throws(() => requireStableMatchContextRangeInventory(stable.map((item, index) =>
    index === 2 ? {...item, ownerEpochHigh: item.ownerEpochHigh + 1} : item)),
  /owner epoch changed/);
  assert.throws(() => requireStableMatchContextRangeInventory(stable.map((item, index) =>
    index === 2 ? {...item, rows: [{...item.rows[0], extent: 65}]} : item)),
  /topology changed/);
});

test('Aurora staging-shadow wire validates schema5 rows, full generation and exact capacities', () => {
  const {heap, pointer} = auroraWire();
  const inventory = decodeAuroraStagingShadowInventory(heap, pointer);
  assert.deepEqual([inventory.version, inventory.size, inventory.frameSlotCount,
    inventory.stagingSlotCount, inventory.frameDescriptorCount,
    inventory.submissionDescriptorCount], [5, 1160, 2, 2, 2, 2]);
  assert.deepEqual([inventory.stagingGenerationHigh, inventory.stagingGenerationLow],
    [0x12345678, 0x89abcdef]);
  assert.equal(inventory.rows.length, 10);
  assert.deepEqual(inventory.rows.map(row => [row.frameSlot, row.streamIndex, row.kindName]), [
    [0, 0, 'aurora-staging-shadow-verts'],
    [0, 1, 'aurora-staging-shadow-uniforms'],
    [0, 2, 'aurora-staging-shadow-indices'],
    [0, 3, 'aurora-staging-shadow-storage'],
    [0, 4, 'aurora-staging-shadow-textureUpload'],
    [1, 0, 'aurora-staging-shadow-verts'],
    [1, 1, 'aurora-staging-shadow-uniforms'],
    [1, 2, 'aurora-staging-shadow-indices'],
    [1, 3, 'aurora-staging-shadow-storage'],
    [1, 4, 'aurora-staging-shadow-textureUpload'],
  ]);
  assert.equal(inventory.ranges.length, 1);
  assert.deepEqual(inventory.ranges[0], {
    kind: 1, kindName: 'aurora-staging-shadow-verts', frameSlot: 0,
    streamIndex: 0, streamName: 'verts', address: 13000, owner: 13000,
    ownerAddress: 13000, length: 16, extent: 64, capacity: 64,
    maxCapacity: 128, previousUsed: 16, owned: 1,
    generationLow: 0x89abcdef, generationHigh: 0x12345678,
  });
  const emptyTextureStream = inventory.rows.find(row => row.frameSlot === 0 && row.streamIndex === 4);
  assert.deepEqual([emptyTextureStream.address, emptyTextureStream.length,
    emptyTextureStream.capacity, emptyTextureStream.maxCapacity, emptyTextureStream.owned],
  [0, 0, 0, 0xffffffff, 1]);
  assert.deepEqual(inventory.diagnosticOutputRange, {base: pointer, bytes: 1160});
  assert.equal(inventory.ownerRestoreAdmission, false);
  assert.throws(() => decodeAuroraStagingShadowInventory(heap, pointer + 4),
    /wire pointer or wasm bounds/);
  assert.throws(() => decodeAuroraStagingShadowInventory(heap, heap.byteLength - 4),
    /wire pointer or wasm bounds/);
  for (const mutation of ['version', 'offset', 'stride', 'current-frame-invalid', 'frame-count',
    'submission-count', 'pending-frame', 'pending-staging', 'worker-busy',
    'pipeline-not-ready', 'queued-pipeline', 'zero-generation', 'packet-active',
    'packet-alias', 'submission-state', 'pending-callback', 'high-word',
    'max-high-word', 'overflow', 'owned', 'empty-pointer', 'overlap']) {
    const malformed = auroraWire({mutation, ordinal: 2});
    assert.throws(() => decodeAuroraStagingShadowInventory(malformed.heap, malformed.pointer),
      /Aurora (?:empty )?staging-shadow/, mutation);
  }
});

test('Aurora staging-shadow provenance rejects aliases and validates stable owner identity only', () => {
  const aurora = auroraWire();
  const inventory = decodeAuroraStagingShadowInventory(aurora.heap, aurora.pointer);
  const audioFixture = audioWire();
  const audio = decodeAudioOwnerInventory(audioFixture.heap, audioFixture.pointer);
  const contextFixture = matchContextWire();
  const matchContext = decodeMatchContextRangeInventory(contextFixture.heap,
    contextFixture.pointer);
  const manifestRows = [auroraStorageRow(), row(15000, 1, 'source-c-static-candidate', 'aurora-disjointness')];
  const nativeOutput = {base: 2048, bytes: 4272};
  const arena = {base: 14000, bytes: 100};
  assert.deepEqual(validateAuroraStagingShadowRangeDisjointness(inventory,
    manifestRows, arena, nativeOutput, aurora.heap.byteLength, audio, matchContext), {
    rangeCount: 1, diagnosticOnly: true,
    diagnosticOutputStorage: {base: 6800, bytes: 2200}, ownerRestoreAdmission: false,
  });
  for (const broken of [
    {...auroraStorageRow(), input_section: '.bss.other'},
    {...auroraStorageRow(), input_object: 'wrong(frame.cpp.o)'},
    {...auroraStorageRow(), decision: 'source-c-static-candidate'},
    {...auroraStorageRow(), exclusion_reason: 'wrong'},
    {...auroraStorageRow(), address: 6808},
    {...auroraStorageRow(), size: 1160},
  ]) assert.throws(() => validateAuroraStagingShadowRangeDisjointness(inventory,
    [broken], arena, nativeOutput, aurora.heap.byteLength, audio, matchContext), /exact excluded manifest identity/);
  assert.throws(() => validateAuroraStagingShadowRangeDisjointness(inventory,
    [auroraStorageRow(), auroraStorageRow()], arena, nativeOutput, aurora.heap.byteLength, audio, matchContext),
  /exact excluded manifest identity/);
  assert.throws(() => validateAuroraStagingShadowRangeDisjointness(inventory,
    [...manifestRows, row(6804, 4, 'source-c-static-candidate', 'alien-overlap')],
    arena, nativeOutput, aurora.heap.byteLength, audio, matchContext), /diagnostic output overlaps static manifest range/);
  for (const [mutation, message] of [
    ['static-overlap', /static manifest range/],
    ['arena-overlap', /gameplay arena/],
    ['native-overlap', /native PCM output storage/],
    ['audio-overlap', /audio owner range/],
    ['context-overlap', /match-context owner range/],
    ['output-overlap', /overlaps its diagnostic output/],
  ]) {
    const alias = auroraWire({mutation});
    const decoded = decodeAuroraStagingShadowInventory(alias.heap, alias.pointer);
    assert.throws(() => validateAuroraStagingShadowRangeDisjointness(decoded,
      manifestRows, arena, nativeOutput, alias.heap.byteLength, audio, matchContext), message);
  }
  const outputAlias = auroraWire({pointer: 9000});
  const outputInventory = decodeAuroraStagingShadowInventory(outputAlias.heap,
    outputAlias.pointer);
  assert.throws(() => validateAuroraStagingShadowRangeDisjointness(outputInventory,
    [auroraStorageRow(outputAlias.pointer)], arena, nativeOutput, outputAlias.heap.byteLength, audio, matchContext),
  /diagnostic output overlaps audio owner range/);

  const boundaries = ['before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence'].map((boundary, ordinal) => {
    const sample = auroraWire({ordinal});
    return {...decodeAuroraStagingShadowInventory(sample.heap, sample.pointer), boundary};
  });
  assert.equal(requireStableAuroraStagingShadowInventory(boundaries)[0].address, 13000);
  assert.throws(() => requireStableAuroraStagingShadowInventory(boundaries.map((item, index) =>
    index === 2 ? {...item, stagingGenerationLow: item.stagingGenerationLow + 1} : item)),
  /lifetime generation changed/);
  assert.throws(() => requireStableAuroraStagingShadowInventory(boundaries.map((item, index) =>
    index === 2 ? {...item, stagingGenerationHigh: item.stagingGenerationHigh + 1} : item)),
  /lifetime generation changed/);
  for (const field of ['address', 'extent', 'maxCapacity', 'owned', 'kind', 'streamIndex']) {
    const changed = boundaries.map((item, index) => index === 2 ? {
      ...item, rows: item.rows.map((row, rowIndex) => rowIndex === 0 ? {
        ...row,
        [field]: field === 'owned' ? 0 : field === 'address' ? row.address + 4 :
          field === 'extent' ? row.extent + 4 : field === 'maxCapacity' ? row.maxCapacity + 4 : row[field] + 1,
      } : row),
      ranges: item.ranges.map((row, rowIndex) => rowIndex === 0 ? {
        ...row,
        [field]: field === 'owned' ? 0 : field === 'address' ? row.address + 4 :
          field === 'extent' ? row.extent + 4 : field === 'maxCapacity' ? row.maxCapacity + 4 : row[field] + 1,
      } : row),
    } : item);
    assert.throws(() => requireStableAuroraStagingShadowInventory(changed),
      /owner topology or capacity changed/, field);
  }
  const changedRangeCount = boundaries.map((item, index) => index === 2 ?
    {...item, ranges: []} : item);
  assert.throws(() => requireStableAuroraStagingShadowInventory(changedRangeCount),
    /owner topology or capacity changed/);
});

// Execute the real page.evaluate census branch against a small service fixture.
// This exercises orchestration/guards only; it is not GPU or gameplay evidence.
async function censusServiceFixture({failedQuiescence = 0, rendererError = 0,
  payloadAdmissionPlan = null, payloadCommandFailure = 0,
  sharedMemory = false, returnFailure = false,
  inventoryFailureAt = 0, inventoryMutation = null, missingInventoryExport = false,
  matchContextInventoryFailureAt = 0, matchContextInventoryMutation = null,
  missingMatchContextInventoryExport = false, requireAuroraStagingShadowInventory = false,
  auroraInventoryFailureAt = 0, auroraInventoryMutation = null,
  missingAuroraStagingShadowExport = false,
  submissionStatuses = null,
  requireStageArenaOwnerInventory = false, stageArenaInventoryFailureAt = 0,
  stageArenaInventoryMutation = null, missingStageArenaInventoryExport = false} = {}) {
  const context = vm.createContext({crypto: webcrypto, setTimeout, performance, SharedArrayBuffer,
    btoa: value => Buffer.from(value, 'binary').toString('base64')});
  context.__checkpointPad = Uint8Array.from(Buffer.from(
    'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=', 'base64'));
  context.__sample300Pad = Uint8Array.from(Buffer.from(
    'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=', 'base64'));
  vm.runInContext(`
    const heap = new Uint8Array(${sharedMemory ? 'new SharedArrayBuffer(16384)' : '16384'});
    const view = new DataView(heap.buffer);
    const calls = [];
    let directNativeReadCount = 0;
    const inventoryPointer = 8000;
    const matchContextInventoryPointer = 11000;
    const auroraInventoryPointer = 6800;
    const stageArenaWirePointer = 11832;
    const submissionStatuses = ${JSON.stringify(submissionStatuses)};
    let submissionStatusQueryCount = 0;
    let stageArenaInventoryQueryCount = 0;
    let inventoryQueryCount = 0;
    let matchContextInventoryQueryCount = 0;
    let auroraInventoryQueryCount = 0;
    function publishAudioInventory() {
      const ordinal = inventoryQueryCount;
      let owner = 9000;
      let generationLow = 0x89abcdef;
      const generationHigh = 0x12345678;
      let fxGenerationLow = 0x76543210;
      const fxGenerationHigh = 0x0fedcba9;
      let count = ${inventoryMutation === 'effects' || inventoryMutation === 'fx-generation' ? 9 : 6};
      const addresses = [owner, 9200, 9300, 9400, 9500, 9600, 9700, 10000, 10100];
      const extents = [128, 64, 32, 64, 32, 16, 32, 16, 16];
      if ('${inventoryMutation || ''}' === 'owner' && ordinal >= 2) owner = 9800;
      if ('${inventoryMutation || ''}' === 'generation' && ordinal >= 2) generationLow++;
      if ('${inventoryMutation || ''}' === 'fx-generation' && ordinal >= 2) fxGenerationLow++;
      if ('${inventoryMutation || ''}' === 'topology' && ordinal >= 2) addresses[1] = 9220;
      if ('${inventoryMutation || ''}' === 'overlap') addresses[1] = owner + 64;
      if ('${inventoryMutation || ''}' === 'static-overlap') owner = addresses[0] = 15000;
      if ('${inventoryMutation || ''}' === 'arena-overlap') owner = addresses[0] = 14000;
      if ('${inventoryMutation || ''}' === 'output-overlap') addresses[1] = 3000;
      if (owner !== addresses[0] && '${inventoryMutation || ''}' !== 'topology' &&
          '${inventoryMutation || ''}' !== 'overlap' && '${inventoryMutation || ''}' !== 'output-overlap') {
        addresses[0] = owner;
      }
      const view = new DataView(heap.buffer);
      view.setUint32(inventoryPointer + 0, '${inventoryMutation || ''}' === 'malformed' ? 7 : 1, true);
      view.setUint32(inventoryPointer + 4, '${inventoryMutation || ''}' === 'layout' ? 251 : 252, true);
      view.setUint32(inventoryPointer + 8, count, true);
      view.setUint32(inventoryPointer + 12, 24, true);
      view.setUint32(inventoryPointer + 16, '${inventoryMutation || ''}' === 'capacity' ? 8 : 9, true);
      view.setUint32(inventoryPointer + 20, 1, true);
      view.setUint32(inventoryPointer + 24, owner, true);
      view.setUint32(inventoryPointer + 28, generationLow, true);
      view.setUint32(inventoryPointer + 32, generationHigh, true);
      for (let index = 0; index < count; index++) {
        const offset = inventoryPointer + 36 + index * 24;
        const rowOwner = index < 6 ? owner : 9700;
        const rowGenerationLow = index < 6 ? generationLow : fxGenerationLow;
        const rowGenerationHigh = index < 6 ? generationHigh : fxGenerationHigh;
        view.setUint32(offset + 0, index + 1, true);
        view.setUint32(offset + 4, addresses[index], true);
        view.setUint32(offset + 8, extents[index], true);
        view.setUint32(offset + 12, rowOwner, true);
        view.setUint32(offset + 16, rowGenerationLow, true);
        view.setUint32(offset + 20, rowGenerationHigh, true);
      }
    }
    function publishMatchContextInventory() {
      const ordinal = matchContextInventoryQueryCount;
      let address = 12000, owner = address, extent = 64;
      let worldGenerationLow = 0x76543210, worldGenerationHigh = 0xabcdef01;
      let ownerEpochLow = 0x11223344, ownerEpochHigh = 0x55667788;
      if ('${matchContextInventoryMutation || ''}' === 'owner' && ordinal >= 2) {
        address += 4; owner = address;
      }
      if ('${matchContextInventoryMutation || ''}' === 'world-generation' && ordinal >= 2) {
        worldGenerationLow++;
      }
      if ('${matchContextInventoryMutation || ''}' === 'owner-epoch' && ordinal >= 2) {
        ownerEpochLow++;
      }
      if ('${matchContextInventoryMutation || ''}' === 'extent' && ordinal >= 2) extent++;
      if ('${matchContextInventoryMutation || ''}' === 'audio-overlap') address = owner = 9000;
      if ('${matchContextInventoryMutation || ''}' === 'static-overlap') address = owner = 15000;
      if ('${matchContextInventoryMutation || ''}' === 'arena-overlap') address = owner = 14000;
      if ('${matchContextInventoryMutation || ''}' === 'output-overlap') address = owner = 3000;
      if ('${matchContextInventoryMutation || ''}' === 'bounds') address = owner = 16380;
      if ('${matchContextInventoryMutation || ''}' === 'wrong-owner') owner++;
      const view = new DataView(heap.buffer);
      view.setUint32(matchContextInventoryPointer + 0,
        '${matchContextInventoryMutation || ''}' === 'malformed' ? 2 : 1, true);
      view.setUint32(matchContextInventoryPointer + 4,
        '${matchContextInventoryMutation || ''}' === 'layout' ? 55 : 56, true);
      view.setUint32(matchContextInventoryPointer + 8,
        '${matchContextInventoryMutation || ''}' === 'count' ? 0 : 1, true);
      view.setUint32(matchContextInventoryPointer + 12,
        '${matchContextInventoryMutation || ''}' === 'layout-row' ? 31 : 32, true);
      view.setUint32(matchContextInventoryPointer + 16,
        '${matchContextInventoryMutation || ''}' === 'capacity' ? 2 : 1, true);
      view.setUint32(matchContextInventoryPointer + 20,
        '${matchContextInventoryMutation || ''}' === 'status' ? 0 : 1, true);
      const offset = matchContextInventoryPointer + 24;
      view.setUint32(offset + 0,
        '${matchContextInventoryMutation || ''}' === 'kind' ? 2 : 1, true);
      view.setUint32(offset + 4, address, true);
      view.setUint32(offset + 8, extent, true);
      view.setUint32(offset + 12, owner, true);
      view.setUint32(offset + 16, worldGenerationLow, true);
      view.setUint32(offset + 20, worldGenerationHigh, true);
      view.setUint32(offset + 24, ownerEpochLow, true);
      view.setUint32(offset + 28, ownerEpochHigh, true);
    }
    function publishStageArenaWire(ordinal) {
      const queryCount = stageArenaInventoryQueryCount;
      let worldGenerationLow = 0x33445566, worldStorage = 800, arenaStorage = 900,
        identityToken = 1000, allocationCount = 2, totalBytes = 40;
      let address = ordinal === 0 ? ${requireAuroraStagingShadowInventory ? 13400 : 13000} :
        ${requireAuroraStagingShadowInventory ? 13600 : 13200};
      let extent = ordinal === 0 ? 16 : 24;
      if ('${stageArenaInventoryMutation || ''}' === 'generation' && queryCount >= 5) worldGenerationLow++;
      if ('${stageArenaInventoryMutation || ''}' === 'world' && queryCount >= 5) worldStorage++;
      if ('${stageArenaInventoryMutation || ''}' === 'arena' && queryCount >= 5) arenaStorage++;
      if ('${stageArenaInventoryMutation || ''}' === 'token' && queryCount >= 5) identityToken++;
      if ('${stageArenaInventoryMutation || ''}' === 'count' && queryCount >= 5) allocationCount++;
      if ('${stageArenaInventoryMutation || ''}' === 'total' && queryCount >= 5) totalBytes++;
      if ('${stageArenaInventoryMutation || ''}' === 'row-topology' && queryCount >= 5 && ordinal === 1) address++;
      if ('${stageArenaInventoryMutation || ''}' === 'row-overlap' && ordinal === 1) address = 13008;
      if ('${stageArenaInventoryMutation || ''}' === 'static-overlap' && ordinal === 0) address = 15000;
      if ('${stageArenaInventoryMutation || ''}' === 'arena-overlap' && ordinal === 0) address = 14000;
      if ('${stageArenaInventoryMutation || ''}' === 'audio-overlap' && ordinal === 0) address = 9000;
      if ('${stageArenaInventoryMutation || ''}' === 'context-overlap' && ordinal === 0) address = 12000;
      if ('${stageArenaInventoryMutation || ''}' === 'observer-overlap' && ordinal === 0) address = 11832;
      if ('${stageArenaInventoryMutation || ''}' === 'output-overlap' && ordinal === 0) address = 2048;
      if (ordinal === 0xffffffff) { address = 0; extent = 0; }
      const values = [1, 64, 1, 1, worldGenerationLow, 0x12345678,
        worldStorage, arenaStorage, identityToken, allocationCount, totalBytes,
        ordinal, address, extent, 0, 0];
      if ('${stageArenaInventoryMutation || ''}' === 'malformed' && queryCount === 1) values[0] = 2;
      const view = new DataView(heap.buffer);
      for (let index = 0; index < values.length; index++) {
        view.setUint32(stageArenaWirePointer + index * 4, values[index] >>> 0, true);
      }
    }
    view.setUint32(8, 1, true);
    for (let slot = 0; slot < 2; slot++) {
      const base = 72 + 192 * slot;
      view.setUint32(base + 136, slot, true);
      view.setInt32(base + 148, 4, true);
      view.setInt32(base + 152, 42, true);
    }
    view.setUint32(6500, 6, true);
    let quiescenceCount = 0;
    const module = {
      HEAPU8: heap,
      _melee_web_synctest_gameplay_arena_base: () => { directNativeReadCount++; return 14000; },
      _melee_web_synctest_gameplay_arena_bytes: () => { directNativeReadCount++; return 100; },
      _melee_web_synctest_audio_range_inventory_query: () => { directNativeReadCount++; return 1; },
      _melee_web_synctest_audio_range_inventory: () => { directNativeReadCount++; return inventoryPointer; },
      _melee_web_synctest_match_context_range_inventory_query: () => { directNativeReadCount++; return 1; },
      _melee_web_synctest_match_context_range_inventory: () => { directNativeReadCount++; return matchContextInventoryPointer; },
      _melee_web_synctest_aurora_staging_shadow_inventory_query: () => { directNativeReadCount++; return auroraInventoryPointer; },
      _melee_web_synctest_stage_arena_inventory_query(ordinal) {
        directNativeReadCount++;
        stageArenaInventoryQueryCount++;
        calls.push({identifier: 'stage-arena-query:' + ordinal});
        if (stageArenaInventoryQueryCount === ${stageArenaInventoryFailureAt}) return 0;
        publishStageArenaWire(ordinal);
        return stageArenaWirePointer;
      },
      _melee_web_synctest_draw_decision: () => { directNativeReadCount++; return 1; },
      _melee_web_snapshot_observation_size: () => { directNativeReadCount++; return 1280; },
      _melee_web_snapshot_pcm_size: () => { directNativeReadCount++; return 4272; },
      _melee_web_snapshot_observation: () => { directNativeReadCount++; return 0; },
      _melee_web_synctest_payload_admission_command: () => { directNativeReadCount++; return 1; },
      _melee_web_snapshot_pcm: () => { directNativeReadCount++; return 2048; },
      _melee_web_snapshot_input: () => { directNativeReadCount++; return 6400; },
      _melee_web_synctest_draw_diagnostics_size: () => { directNativeReadCount++; return 132; },
      _melee_web_synctest_draw_diagnostics: () => { directNativeReadCount++; return 6500; },
      _melee_web_synctest_motion_boundary: () => { directNativeReadCount++; return 0; },
      _melee_web_synctest_is_mario_kind: kind => { directNativeReadCount++; return kind === 42 ? 1 : 0; },
      _melee_web_synctest_submission_json: () => {
        directNativeReadCount++;
        const ordinal = submissionStatusQueryCount++;
        if (submissionStatuses) calls.push({identifier: 'submission-status-poll', ordinal: ordinal + 1});
        return 1;
      },
      _melee_web_snapshot_error: () => { directNativeReadCount++; return 2; },
      UTF8ToString: pointer => pointer === 2 ? 'synthetic native transfer failure' :
        submissionStatuses ?
          JSON.stringify(submissionStatuses[Math.min(submissionStatusQueryCount - 1,
            submissionStatuses.length - 1)]) :
          JSON.stringify({initialized: true, backend: 7, rendererErrorCount: ${rendererError},
            pendingFramePackets: 0, pendingStagingBuffers: 0, workerBusy: 0, drawCallCount: 4,
            lastBeginFrameId: 301, lastEndFrameId: 301}),
      async ccall(identifier, returnType, argumentTypes, args, options) {
        directNativeReadCount++;
        if (returnType !== 'number' || options.async !== true) throw new Error('incorrect native call');
        calls.push({identifier, args});
        if (identifier === 'melee_web_snapshot_quiescent') {
          quiescenceCount++;
          return quiescenceCount === ${failedQuiescence} ? 0 : 1;
        }
        if (identifier === 'melee_web_synctest_audio_range_inventory_query') {
          inventoryQueryCount++;
          if (inventoryQueryCount === ${inventoryFailureAt}) return 0;
          publishAudioInventory();
          return 1;
        }
        if (identifier === 'melee_web_synctest_match_context_range_inventory_query') {
          matchContextInventoryQueryCount++;
          if (matchContextInventoryQueryCount === ${matchContextInventoryFailureAt}) return 0;
          publishMatchContextInventory();
          return 1;
        }
        if (identifier === 'melee_web_synctest_aurora_staging_shadow_inventory_query') {
          auroraInventoryQueryCount++;
          if (auroraInventoryQueryCount === ${auroraInventoryFailureAt}) return 0;
          globalThis.__writeAuroraInventory(auroraInventoryQueryCount - 1);
          return auroraInventoryPointer;
        }
        if (identifier === 'melee_web_synctest_payload_admission_command') {
          if (args.length !== 1 || ![1, 2, 3, 4, 5].includes(args[0])) {
            throw new Error('wrong source-payload command ABI');
          }
          if (args[0] === 2) {
            if (args[0] !== 2) throw new Error('wrong payload step command');
            heap.set(globalThis.__sample300Pad, 6400);
            view.setUint32(0, 1, true);
            heap[15000] = 7; heap[9004] = 11;
            heap[12004] = 15; heap[13004] = 17; heap[2050] = 13;
          }
          if (args[0] === 3) {
            heap[14000] = 8; heap[13004] = 19;
            for (let slot = 0; slot < 2; slot++) {
              view.setUint32(6508 + 4 * slot, 5, true);
              view.setUint32(6556 + 4 * slot, 1, true);
              view.setInt32(6580 + 4 * slot, slot, true);
              view.setInt32(6604 + 4 * slot, 42, true);
            }
            view.setUint32(6628, 3, true);
          }
          return args[0] === ${payloadCommandFailure} ? 1 : 0;
        }
        if (identifier === 'melee_web_snapshot_step') {
          if (args.length !== 1 || args[0] !== 300) throw new Error('wrong census input sample');
          heap[15000] = 7;
          heap[6400] = 9;
          heap[9004] = 11;
          heap[12004] = 15;
          if (${requireStageArenaOwnerInventory ? 'true' : 'false'} &&
              '${stageArenaInventoryMutation || ''}' !== 'zero-source-step') heap[13004] = 17;
          heap[2050] = 13;
          return 1;
        }
        if (identifier === 'melee_web_synctest_draw_present') {
          heap[14000] = 8;
          if (${requireAuroraStagingShadowInventory ? 'true' : 'false'}) heap[13004] = 19;
          for (let slot = 0; slot < 2; slot++) {
            view.setUint32(6508 + 4 * slot, 5, true);
            view.setUint32(6556 + 4 * slot, 1, true);
            view.setInt32(6580 + 4 * slot, slot, true);
            view.setInt32(6604 + 4 * slot, 42, true);
          }
          view.setUint32(6628, 3, true);
          return 1;
        }
        throw new Error('unexpected service ' + identifier);
      },
    };
    const window = {__slippiSynctest: {module}};
  `, context);
  if (missingInventoryExport) {
    vm.runInContext('delete window.__slippiSynctest.module._melee_web_synctest_audio_range_inventory_query', context);
  }
  if (missingMatchContextInventoryExport) {
    vm.runInContext('delete window.__slippiSynctest.module._melee_web_synctest_match_context_range_inventory_query', context);
  }
  if (missingAuroraStagingShadowExport) {
    vm.runInContext('delete window.__slippiSynctest.module._melee_web_synctest_aurora_staging_shadow_inventory_query', context);
  }
  if (missingStageArenaInventoryExport) {
    vm.runInContext('delete window.__slippiSynctest.module._melee_web_synctest_stage_arena_inventory_query', context);
  }
  context.__writeAuroraInventory = ordinal => {
    const wire = auroraWire({ordinal, mutation: auroraInventoryMutation});
    const target = vm.runInContext('heap', context);
    target.set(wire.heap.subarray(wire.pointer, wire.pointer + wire.byteSize), wire.pointer);
  };
  vm.runInContext('heap.set(__checkpointPad, 6400)', context);
  const baseline = decodeDrawDiagnostics(Buffer.from(vm.runInContext('heap.slice(6500, 6632)', context)));
  const page = {evaluate: (callback, arguments_) => {
    context.arguments_ = arguments_;
    return vm.runInContext(`(${callback.toString()})(arguments_)`, context);
  }};
  let result;
  try {
    result = await runBrowserSample(page, 300, censusMapping, [4, 4], {
      arenaRange: {base: 14000, bytes: 100},
      manifestRows: [auroraStorageRow(), stageArenaDiagnosticRow(), row(15000, 1, 'source-c-static-candidate', 'service-fixture')],
      requireAudioInventory: true,
      requireMatchContextInventory: true,
      requireAuroraStagingShadowInventory,
      requireStageArenaOwnerInventory,
    }, payloadAdmissionPlan);
  } catch (error) {
    if (!returnFailure) throw error;
    return {error, calls: JSON.parse(vm.runInContext('JSON.stringify(calls)', context)),
      directNativeReadCount: vm.runInContext('directNativeReadCount', context)};
  }
  return {result, baseline, calls: JSON.parse(vm.runInContext('JSON.stringify(calls)', context))};
}

const censusMapping = [
  {sourceSlot: 0, initializerPort: 0, rawController: 0},
  {sourceSlot: 1, initializerPort: 2, rawController: 1},
];

const payloadAdmissionPlan = {
  checkpointSample: 299, checkpointSourceFrame: 0,
  checkpointRawPad44Base64: 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  expectedSample300Pad44Base64: 'AgAAAAAAAAAAAAABALAAAAAAAAAAAAAAAAAAAAAAAAD/AAAAAAAAAAAAAP8=',
  expectedSample300Pad44Sha256: '46e14bb3286ecad56fd0c8c45e405b64f645f971bfbd35e9ddd8432db3c4e288',
};

test('source-payload Asyncify predicate admits only a normal empty runtime state', () => {
  const source = readFileSync(new URL('./synctest_browser.cpp', import.meta.url), 'utf8');
  const marker = 'EM_JS(int, melee_web_payload_admission_asyncify_idle, (), {';
  const start = source.indexOf(marker);
  assert.notEqual(start, -1, 'the production predicate must exist once in the adapter');
  assert.equal(source.indexOf(marker, start + marker.length), -1, 'the predicate must have one source');
  const bodyStart = start + marker.length;
  const bodyEnd = source.indexOf('\n});', bodyStart);
  assert.ok(bodyEnd > bodyStart, 'the complete production EM_JS body must be extractable');
  const idle = new Function('Asyncify', source.slice(bodyStart, bodyEnd));
  assert.equal(idle(undefined), 0, 'missing Asyncify fails closed');
  assert.equal(idle(null), 0, 'null Asyncify fails closed');
  const normal = Object.freeze({State: {Normal: 0, Unwinding: 1, Rewinding: 2, Disabled: 3},
    state: 0, currData: null});
  assert.equal(idle(normal), 1);
  assert.deepEqual(normal, {State: {Normal: 0, Unwinding: 1, Rewinding: 2, Disabled: 3},
    state: 0, currData: null}, 'predicate does not mutate Asyncify state');
  assert.equal(idle({State: normal.State, state: 1, currData: {}}), 0, 'Unwinding refuses');
  assert.equal(idle({State: normal.State, state: 2, currData: {}}), 0, 'Rewinding refuses');
  assert.equal(idle({State: normal.State, state: 3, currData: null}), 0, 'Disabled refuses');
  assert.equal(idle({State: normal.State, state: 0, currData: {}}), 0, 'normal with live continuation refuses');
  assert.equal(idle({State: null, state: 0, currData: null}), 0, 'malformed state refuses');
  assert.equal(idle(new Proxy({}, {get() { throw new Error('unavailable'); }})), 0);
});

test('source-payload runner preserves the exact capture/tick/draw/post-draw command order', async () => {
  const {result, calls} = await censusServiceFixture({payloadAdmissionPlan,
    requireAuroraStagingShadowInventory: true, requireStageArenaOwnerInventory: true});
  const commands = calls.filter(item => item.identifier === 'melee_web_synctest_payload_admission_command');
  assert.deepEqual(commands.map(item => item.args[0]), [1, 2, 3, 4]);
  assert.equal(calls.some(item => item.identifier === 'melee_web_snapshot_step'), false);
  assert.equal(calls.some(item => item.identifier === 'melee_web_synctest_draw_present'), false);
  const firstOwnerQuery = calls.findIndex(item => item.identifier === 'melee_web_synctest_audio_range_inventory_query');
  assert.ok(calls.findIndex(item => item.identifier === 'melee_web_synctest_payload_admission_command') < firstOwnerQuery);
  assert.equal(result.payloadAdmission.status, 'owner-local-payload-admission-complete');
  assert.equal(result.payloadAdmission.restoreCalled, false);
  assert.deepEqual(Array.from(result.payloadAdmission.sequence, item => item.command), [1, 2, 3, 4]);
  assert.deepEqual(Array.from(result.payloadAdmission.sequence, item => item.result), [0, 0, 0, 0]);
  const report = {result: 'incomplete', execution: {payloadAdmissionDryRun: {
    ...result.payloadAdmission, enabled: true, restoreCalled: false, completeGameState: false,
  }}};
  assembleCompletedCensusExecution(report, {prefixSamples: 300, censusSample: 300,
    captureHypothesis: 'fixture hypothesis'});
  assert.equal(report.execution.payloadAdmissionDryRun.status,
    'owner-local-payload-admission-complete');
  assert.deepEqual(Array.from(report.execution.payloadAdmissionDryRun.sequence,
    item => item.result), [0, 0, 0, 0], 'final report assembly retains all command statuses');
  assert.equal(result.pad, payloadAdmissionPlan.expectedSample300Pad44Base64);
});

test('completed report assembly leaves the disabled payload path unchanged', () => {
  const report = {result: 'incomplete', execution: {payloadAdmissionDryRun: {
    enabled: false, status: 'not-started', restoreCalled: false, completeGameState: false,
  }}};
  assembleCompletedCensusExecution(report, {prefixSamples: 300, censusSample: 300,
    captureHypothesis: 'fixture hypothesis'});
  assert.equal(Object.hasOwn(report.execution, 'payloadAdmissionDryRun'), false);
  assert.equal(report.execution.restorePerformed, false);
});

test('source-payload draw submits while busy and admits only after the existing idle poll', async () => {
  const busy = {rendererErrorCount: 0, pendingFramePackets: 1, pendingStagingBuffers: 1,
    workerBusy: 1, drawCallCount: 4, lastBeginFrameId: 301, lastEndFrameId: 300};
  const idle = {...busy, pendingFramePackets: 0, pendingStagingBuffers: 0,
    workerBusy: 0, lastEndFrameId: 301};
  const {result, calls} = await censusServiceFixture({payloadAdmissionPlan,
    submissionStatuses: [busy, idle], requireAuroraStagingShadowInventory: true,
    requireStageArenaOwnerInventory: true});
  const commands = calls.filter(item => item.identifier === 'melee_web_synctest_payload_admission_command');
  assert.deepEqual(commands.map(item => item.args[0]), [1, 2, 3, 4]);
  const polls = calls.filter(item => item.identifier === 'submission-status-poll');
  assert.deepEqual(polls.map(item => item.ordinal), [1, 2], 'the runner observed busy then idle status');
  const command4 = calls.findIndex(item => item.identifier === 'melee_web_synctest_payload_admission_command' &&
    item.args[0] === 4);
  const idlePoll = calls.findIndex(item => item.identifier === 'submission-status-poll' && item.ordinal === 2);
  const postDrawTransfer = calls.findLastIndex(item => item.identifier === 'melee_web_snapshot_quiescent');
  assert.ok(idlePoll < postDrawTransfer && postDrawTransfer < command4,
    'POSTDRAW_ADMIT follows the observed idle status and post-draw transfer check');
  assert.equal(result.payloadAdmission.status, 'owner-local-payload-admission-complete');
});

test('source-payload runner rejects a nonzero command status instead of using the checked-1 convention', async () => {
  await assert.rejects(censusServiceFixture({payloadAdmissionPlan, payloadCommandFailure: 1,
    requireAuroraStagingShadowInventory: true, requireStageArenaOwnerInventory: true}),
  /source-payload command returned 1/);
});

test('first payload refusal keeps completed prefix, command sequence, and observed pre-step boundary in failed report', async () => {
  const {error, calls} = await censusServiceFixture({payloadAdmissionPlan,
    payloadCommandFailure: 2, returnFailure: true,
    requireAuroraStagingShadowInventory: true, requireStageArenaOwnerInventory: true});
  assert.match(error.message, /STEP_AND_ADMIT\(sample 300\): source-payload command returned 1/);
  const partial = error.payloadAdmissionPartial;
  assert.ok(partial, 'the page-local sequence was read back after the refused command');
  assert.equal(partial.status, 'refused');
  assert.equal(partial.checkpoint.sample, 299);
  assert.equal(partial.checkpoint.rawPad44Base64, payloadAdmissionPlan.checkpointRawPad44Base64);
  assert.deepEqual(Array.from(partial.commandSequence, item => item.command), [1, 2]);
  assert.deepEqual(Array.from(partial.commandSequence, item => item.result), [0, 1]);
  assert.equal(partial.commandSequence[1].outcome, 'returned-nonzero');
  assert.equal(partial.boundaries.audioOwnerInventory.length, 1);
  assert.equal(partial.boundaries.audioOwnerInventory[0].boundary, 'before-source-step');
  assert.equal(partial.sourceStep, null, 'the refused tick is not reported as a completed boundary');
  assert.equal(partial.sample300, null, 'no sample-300 observation is fabricated after refusal');
  assert.deepEqual(calls.filter(item => item.identifier === 'melee_web_synctest_payload_admission_command')
    .map(item => item.args[0]), [1, 2]);
  const report = {result: 'incomplete', execution: {status: 'incomplete',
    payloadAdmissionDryRun: {enabled: true, status: 'not-started', restoreCalled: false,
      completeGameState: false}}};
  recordFailedCensusExecution(report, error);
  assert.equal(report.result, 'failed');
  assert.equal(report.execution.status, 'failed');
  assert.equal(report.execution.payloadAdmissionDryRun.status, 'refused');
  assert.deepEqual(Array.from(report.execution.payloadAdmissionDryRun.sequence,
    item => item.result), [0, 1]);
  assert.equal(report.execution.payloadAdmissionPartial.boundaries.audioOwnerInventory.length, 1);
});

test('unexpected boundary failure preserves partial commands as interrupted and remains failed', async () => {
  const {error, calls} = await censusServiceFixture({payloadAdmissionPlan,
    inventoryFailureAt: 1, returnFailure: true,
    requireAuroraStagingShadowInventory: true, requireStageArenaOwnerInventory: true});
  assert.match(error.message, /audio owner inventory before-source-step: synthetic native transfer failure/);
  const report = {result: 'incomplete', execution: {status: 'incomplete',
    payloadAdmissionDryRun: {enabled: true, status: 'not-started', restoreCalled: false,
      completeGameState: false}}};
  recordFailedCensusExecution(report, error);
  assert.equal(report.result, 'failed');
  assert.equal(report.execution.payloadAdmissionDryRun.status, 'interrupted');
  assert.deepEqual(Array.from(report.execution.payloadAdmissionDryRun.sequence,
    item => item.result), [0]);
  assert.equal(report.execution.payloadAdmissionPartial.status, 'interrupted');
  assert.equal(report.execution.payloadAdmissionPartial.failure.kind, 'runner-error');
  assert.deepEqual(calls.filter(item => item.identifier === 'melee_web_synctest_payload_admission_command')
    .map(item => item.args[0]), [1]);
});

test('source-payload runner binds the live sample-299 PAD before calling native', async () => {
  await assert.rejects(censusServiceFixture({payloadAdmissionPlan: {...payloadAdmissionPlan,
    checkpointRawPad44Base64: 'A'.repeat(60)}, requireAuroraStagingShadowInventory: true,
    requireStageArenaOwnerInventory: true}), /sample-299 warm checkpoint/);
});

test('source-payload runner refuses shared WebAssembly memory before native reads', async () => {
  const {error, calls, directNativeReadCount} = await censusServiceFixture({payloadAdmissionPlan,
    sharedMemory: true, returnFailure: true, requireAuroraStagingShadowInventory: true,
    requireStageArenaOwnerInventory: true});
  assert.match(error.message, /refuses shared WebAssembly memory/);
  assert.deepEqual(calls, [], 'no native ccall was attempted');
  assert.equal(directNativeReadCount, 0, 'no direct native owner or source export was read');
});
async function closeBrowserFixture({payloadClose = 7, snapshotClose = 0,
  rendererShutdown = 1} = {}) {
  const calls = [];
  const module = {
    _melee_web_synctest_submission_json: () => 1,
    _melee_web_snapshot_error: () => 2,
    UTF8ToString: pointer => pointer === 1 ? JSON.stringify({pendingFramePackets: 0,
      pendingStagingBuffers: 0, workerBusy: 0, rendererErrorCount: 0}) : 'synthetic close refusal',
    async ccall(identifier, returnType, argumentTypes, args, options) {
      calls.push({identifier, args, options});
      if (identifier === 'melee_web_synctest_payload_admission_command') return payloadClose;
      if (identifier === 'melee_web_snapshot_close') return snapshotClose;
      if (identifier === 'melee_web_synctest_renderer_shutdown') return rendererShutdown;
      throw new Error(`unexpected close service ${identifier}`);
    },
  };
  const context = vm.createContext({window: {__slippiSynctest: {module}}});
  const page = {evaluate: (callback, arguments_) => {
    context.arguments_ = arguments_;
    return vm.runInContext(`(${callback.toString()})(arguments_)`, context);
  }};
  return {result: await closeBrowser(page, true), calls};
}

test('failed payload CLOSE stays failed while snapshot and renderer shutdown outcomes remain separate', async () => {
  const {result, calls} = await closeBrowserFixture({payloadClose: 7,
    snapshotClose: 0, rendererShutdown: 1});
  assert.equal(result.closed, false);
  assert.equal(result.payloadAdmissionClose.status, 'returned-nonzero');
  assert.equal(result.payloadAdmissionClose.result, 7);
  assert.equal(result.snapshotClose.status, 'checked-call-failed');
  assert.match(result.snapshotClose.error, /snapshot close: synthetic close refusal/);
  assert.equal(result.rendererShutdown.status, 'checked-return-one');
  assert.equal(result.rendererShutdown.result, 1);
  assert.equal(Object.hasOwn(result, 'snapshotDestroyed'), false,
    'return outcomes do not claim snapshot destruction');
  assert.deepEqual(calls.map(item => item.identifier), [
    'melee_web_synctest_payload_admission_command',
    'melee_web_snapshot_close',
    'melee_web_synctest_renderer_shutdown',
  ]);
  const successful = await closeBrowserFixture({payloadClose: 0,
    snapshotClose: 1, rendererShutdown: 1});
  assert.equal(successful.result.closed, true);
  assert.equal(successful.result.payloadAdmissionClose.status, 'returned-zero');
  assert.equal(successful.result.snapshotClose.status, 'checked-return-one');
  assert.equal(successful.result.rendererShutdown.status, 'checked-return-one');
  assert.equal(Object.hasOwn(successful.result, 'snapshotDestroyed'), false,
    'even successful return codes report outcomes, not inferred destruction');
});

const previousFrame = {beginFrameId: 300, endFrameId: 300};
const validate300 = ({result, baseline}) =>
  validateCensusDrawSample(result, 300, baseline, previousFrame, censusMapping);

function mutateDiagnostics(result, mutate) {
  const bytes = Buffer.from(result.drawDiagnostics, 'base64');
  mutate(new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength));
  return {...result, drawDiagnostics: bytes.toString('base64')};
}

function validStageArenaCatalog({mutateRow = null, mutateFinal = null, manifestRows = null,
  rowAddresses = [13000, 13200], audioRows = [], matchRows = [], auroraRanges = [],
  nativeOutputRange = {base: 2048, bytes: 4272}, heapBytes = 16384} = {}) {
  const metadata = decodedStageWire({ordinal: 0xffffffff});
  const rows = rowAddresses.map((address, ordinal) => decodedStageWire({ordinal, address,
    extent: ordinal === 0 ? 16 : 24,
    ...(mutateRow === ordinal ? {identityToken: 1001} : {})}));
  const finalMetadata = decodedStageWire({ordinal: 0xffffffff,
    ...(mutateFinal ? {worldGenerationLow: 0x33445567} : {})});
  const catalog = validateStageArenaOwnerCatalog(metadata, rows, finalMetadata, 11832,
    manifestRows || [stageArenaDiagnosticRow(), row(15000, 1, 'source-c-static-candidate', 'fixture')],
    {base: 14000, bytes: 100}, nativeOutputRange, heapBytes,
    {rows: audioRows}, {rows: matchRows}, {ranges: auroraRanges});
  return {metadata, rows, finalMetadata, catalog};
}

test('stage-arena owner wire validates a complete zero-row catalog and one exact-owner fixture catalog', () => {
  const emptyMetadata = decodedStageWire({ordinal: 0xffffffff, allocationCount: 0,
    totalBytes: 0});
  const empty = validateStageArenaOwnerCatalog(emptyMetadata, [], emptyMetadata, 11832,
    [stageArenaDiagnosticRow()], {base: 14000, bytes: 100}, {base: 2048, bytes: 4272},
    16384, {rows: []}, {rows: []});
  assert.equal(empty.rows.length, 0);
  assert.equal(empty.ownerRestoreAdmission, false);
  assert.equal(empty.completeGameState, false);

  const {metadata, rows, finalMetadata, catalog} = validStageArenaCatalog();
  assert.equal(metadata.metadata, true);
  assert.deepEqual(rows.map(item => [item.ordinal, item.address, item.extent]),
    [[0, 13000, 16], [1, 13200, 24]]);
  assert.equal(finalMetadata.metadata, true);
  assert.equal(catalog.diagnosticOutputStorage.wirePointer, 11832);
  assert.equal(catalog.diagnosticOutputStorage.bytes, 96);
  assert.equal(catalog.ownerRestoreAdmission, false);
});

test('stage-arena catalog permits PCM provenance alias but rejects stage and observer overlap with either view', () => {
  const pcmManifest = pcmStaticManifestRow();
  const manifestRows = [stageArenaDiagnosticRow(), pcmManifest,
    row(15000, 1, 'source-c-static-candidate', 'fixture')];
  const descriptor = {base: 854624, bytes: 4272};
  const accepted = validStageArenaCatalog({manifestRows, nativeOutputRange: descriptor,
    heapBytes: 1000000});
  assert.equal(accepted.catalog.rows.length, 2);
  assert.equal(accepted.catalog.diagnosticOutputStorage.bytes, 96);
  assert.equal(accepted.catalog.ownerRestoreAdmission, false);
  assert.equal(accepted.catalog.completeGameState, false);

  // Keep each negative isolated so it proves the new stage/observer range is
  // still checked against both independent PCM provenance descriptions.
  assert.throws(() => validStageArenaCatalog({
    manifestRows: [stageArenaDiagnosticRow(), pcmStaticManifestRow(13000)],
    nativeOutputRange: descriptor, heapBytes: 1000000,
  }), /overlaps static manifest range/);
  assert.throws(() => validStageArenaCatalog({
    manifestRows: [stageArenaDiagnosticRow(), pcmManifest],
    nativeOutputRange: {base: 13000, bytes: 4272}, heapBytes: 1000000,
  }), /overlaps native PCM diagnostic storage/);
  assert.throws(() => validStageArenaCatalog({
    manifestRows: [stageArenaDiagnosticRow(), pcmStaticManifestRow(8000)],
    nativeOutputRange: descriptor, heapBytes: 1000000,
  }), /diagnostic storage overlaps another owner: stage-arena diagnostic storage overlaps static manifest range/);
  assert.throws(() => validStageArenaCatalog({
    manifestRows: [stageArenaDiagnosticRow(), pcmManifest],
    nativeOutputRange: {base: 8000, bytes: 4272}, heapBytes: 1000000,
  }), /diagnostic storage overlaps another owner: stage-arena diagnostic storage overlaps native PCM diagnostic storage/);
});

test('stage-arena owner wire and catalog refuse malformed, partial, overlapping, or aliased rows', () => {
  for (const mutation of ['version','size','status','kind','zero-generation','zero-identity',
    'max-count','max-total','flags','reserved','metadata-row']) {
    assert.throws(() => decodedStageWire({ordinal: 0xffffffff, mutation}), /Stage-arena owner wire/,
      mutation);
  }
  assert.throws(() => decodedStageWire({ordinal: 0, address: 12000, extent: 16,
    mutation: 'row-zero-extent'}), /ordinal or extent/);
  assert.throws(() => decodedStageWire({ordinal: 0, address: 0xfffffff0, extent: 32,
    mutation: 'row-overflow'}), /ordinal or extent/);
  assert.throws(() => validateStageArenaOwnerCatalog(
    decodedStageWire({ordinal: 0xffffffff}), [], decodedStageWire({ordinal: 0xffffffff}),
    11832, [stageArenaDiagnosticRow()], {base: 14000, bytes: 100},
    {base: 2048, bytes: 4272}, 16384, {rows: []}, {rows: []}), /catalog metadata, count/);
  assert.throws(() => validStageArenaCatalog({mutateRow: 0}), /catalog row 0/);
  assert.throws(() => validStageArenaCatalog({mutateFinal: true}), /catalog metadata/);
  assert.throws(() => validStageArenaCatalog({rowAddresses: [13000]}), /catalog metadata, count/);
  assert.throws(() => validStageArenaCatalog({rowAddresses: [13000, 13008]}), /overlap each other/);

  for (const [manifestRows, message] of [
    [[{...stageArenaDiagnosticRow(), input_section: '.bss.other'}], /one exact diagnostic manifest identity/],
    [[{...stageArenaDiagnosticRow(), input_object: 'wrong.o'}], /one exact diagnostic manifest identity/],
    [[stageArenaDiagnosticRow(), stageArenaDiagnosticRow(11900)], /one exact diagnostic manifest identity/],
    [[{...stageArenaDiagnosticRow(), size: 95}], /diagnostic manifest identity/],
    [[{...stageArenaDiagnosticRow(), address: 11801}], /diagnostic manifest identity/],
    [[{...stageArenaDiagnosticRow(), decision: 'source-c-static-candidate'}], /diagnostic manifest identity/],
    [[{...stageArenaDiagnosticRow(), exclusion_reason: 'wrong'}], /diagnostic manifest identity/],
  ]) {
    assert.throws(() => validStageArenaCatalog({manifestRows}), message);
  }
  for (const [rowAddresses, message] of [
    [[15000, 13200], /overlaps static manifest range/],
    [[14000, 13200], /overlaps gameplay arena/],
    [[11832, 13200], /overlaps the diagnostic storage/],
    [[2048, 13200], /overlaps native PCM diagnostic storage/],
  ]) {
    assert.throws(() => validStageArenaCatalog({rowAddresses}), message,
      `row-address overlap control failed for ${JSON.stringify(rowAddresses)}`);
  }
  assert.throws(() => validStageArenaCatalog({rowAddresses: [9000, 13200],
    audioRows: [{address: 9000, extent: 64}]}), /overlaps audio owner range/);
  assert.throws(() => validStageArenaCatalog({rowAddresses: [12000, 13200],
    matchRows: [{address: 12000, extent: 64}]}), /overlaps match-context owner range/);
  assert.throws(() => validStageArenaCatalog({rowAddresses: [13200, 13300],
    auroraRanges: [{address: 13200, extent: 64}]}), /overlaps Aurora staging-shadow range/);
  assert.doesNotThrow(() => validStageArenaCatalog({rowAddresses: [13000, 13200],
    audioRows: [{address: 9000, extent: 128}],
    matchRows: [{address: 12000, extent: 64}]}));
});

test('stage-arena attribution requires all three identical catalogs and leaves every range unadmitted', () => {
  const valid = validStageArenaCatalog();
  const order = ['before-source-step','after-source-step','after-actual-draw-and-Aurora-quiescence'];
  const boundaries = order.map(boundary => ({boundary, metadata: valid.metadata,
    rows: valid.catalog.rows, diagnosticOutputStorage: valid.catalog.diagnosticOutputStorage}));
  assert.deepEqual(requireStableStageArenaOwnerInventory(boundaries).map(row => row.ordinal), [0, 1]);
  for (const mutation of ['generation','world','arena','token','extent','ordinal']) {
    const changed = boundaries.map((item, index) => index === 2 ? {...item,
      metadata: mutation === 'generation' ? {...item.metadata, worldGenerationLow: item.metadata.worldGenerationLow + 1} : item.metadata,
      rows: mutation === 'world' ? item.rows.map((row, rowIndex) => rowIndex === 0 ? {...row, worldStorage: row.worldStorage + 1} : row) :
        mutation === 'arena' ? item.rows.map((row, rowIndex) => rowIndex === 0 ? {...row, arenaStorage: row.arenaStorage + 1} : row) :
        mutation === 'token' ? item.rows.map((row, rowIndex) => rowIndex === 0 ? {...row, identityToken: row.identityToken + 1} : row) :
        mutation === 'extent' ? item.rows.map((row, rowIndex) => rowIndex === 0 ? {...row, extent: row.extent + 1} : row) :
        mutation === 'ordinal' ? item.rows.map((row, rowIndex) => rowIndex === 0 ? {...row, ordinal: row.ordinal + 1} : row) : item.rows,
    } : item);
    assert.throws(() => requireStableStageArenaOwnerInventory(changed), /lifetime or complete ordered topology/,
      mutation);
  }
  assert.throws(() => requireStableStageArenaOwnerInventory(boundaries.slice(0, 2)), /three complete ordered/);
  const before = boundaries[0], after = boundaries[1];
  const changedStage = classifyHeapRanges([{start: 13004, end: 13005}], [],
    {base: 14000, bytes: 100}, null, null, null, null, {before, after});
  assert.equal(changedStage[0].provenance, 'stage-arena-owner-range');
  assert.equal(changedStage[0].ownerRestoreAdmission, false);
  const changedDiagnostic = classifyHeapRanges([{start: 11835, end: 11836}],
    [stageArenaDiagnosticRow()], {base: 14000, bytes: 100}, null, null, null, null,
    {before, after});
  assert.equal(changedDiagnostic[0].provenance, 'stage-arena-descriptor-diagnostic-excluded');
  assert.equal(changedDiagnostic[0].ownerRestoreAdmission, false);
  assert.throws(() => classifyHeapRanges([{start: 13004, end: 13005}], [],
    {base: 14000, bytes: 100}, null, null, null, null, {before,
      after: {...after, metadata: {...after.metadata, worldGenerationLow: 77}}}),
  /same complete owner/);
  assert.throws(() => classifyHeapRanges([{start: 13004, end: 13005}], [],
    {base: 14000, bytes: 100}, null, null, null, null, {before,
      after: {...after, rows: after.rows.map((item, index) => index === 0 ?
        {...item, arenaStorage: item.arenaStorage + 1} : item)}}), /same complete owner/);
});

test('opt-in stage-arena census uses synchronous full catalogs at all three boundaries', async () => {
  await assert.rejects(censusServiceFixture({requireStageArenaOwnerInventory: true,
    missingStageArenaInventoryExport: true}), /Required stage-arena owner inventory export is unavailable/);
  for (const [stageArenaInventoryFailureAt, boundary] of [[1, 'metadata'], [2, 'row 0'],
    [5, 'metadata'], [9, 'metadata']]) {
    await assert.rejects(censusServiceFixture({requireStageArenaOwnerInventory: true,
      stageArenaInventoryFailureAt}), new RegExp(`Stage-arena owner inventory .* ${boundary} refused`));
  }
  const fixture = await censusServiceFixture({requireStageArenaOwnerInventory: true});
  const census = fixture.result.heapCensus;
  assert.equal(census.requireStageArenaOwnerInventory, true);
  assert.equal(census.stageArenaOwnerInventory.ownerRestoreAdmission, false);
  assert.equal(census.stageArenaOwnerInventory.completeGameState, false);
  assert.equal(census.stageArenaOwnerInventory.stableOwnerLifetimeValidated, true);
  assert.deepEqual([...census.stageArenaOwnerInventory.boundaryOrder], [
    'before-source-step','after-source-step','after-actual-draw-and-Aurora-quiescence']);
  assert.ok(census.stageArenaOwnerInventory.boundaries.every(boundary =>
    boundary.rows.length === 2 && boundary.metadata.allocationCount === 2 &&
    boundary.metadata.totalBytes === 40 && boundary.diagnosticOutputStorage.bytes === 96 &&
    boundary.ownerRestoreAdmission === false && boundary.completeGameState === false));
  assert.ok(census.sourceStep.changedRanges.some(range =>
    range.start === 13004 && range.provenance === 'stage-arena-owner-range' &&
    range.ownerRestoreAdmission === false));
  assert.ok([...census.sourceStep.changedRanges, ...census.actualDrawAndQuiescence.changedRanges]
    .every(range => range.ownerRestoreAdmission === false));
  const calls = fixture.calls.map(call => call.identifier);
  const firstQuery = calls.indexOf('stage-arena-query:4294967295');
  assert.deepEqual(calls.slice(firstQuery, firstQuery + 4), [
    'stage-arena-query:4294967295','stage-arena-query:0','stage-arena-query:1',
    'stage-arena-query:4294967295']);
  assert.equal(calls.filter(name => name === 'stage-arena-query:4294967295').length, 6);
  assert.equal(calls.filter(name => name === 'stage-arena-query:0').length, 3);
  assert.equal(calls.filter(name => name === 'stage-arena-query:1').length, 3);
});

test('stage-arena census rejects malformed rows, changed lifetime/topology, and cross-owner overlaps', async () => {
  for (const stageArenaInventoryMutation of ['malformed','generation','world','arena','token',
    'count','total','row-topology']) {
    await assert.rejects(censusServiceFixture({requireStageArenaOwnerInventory: true,
      stageArenaInventoryMutation}), /Stage-arena/);
  }
  for (const stageArenaInventoryMutation of ['row-overlap','static-overlap','arena-overlap',
    'audio-overlap','context-overlap','observer-overlap','output-overlap']) {
    await assert.rejects(censusServiceFixture({requireStageArenaOwnerInventory: true,
      stageArenaInventoryMutation}), /Stage-arena/, stageArenaInventoryMutation);
  }
  const ordinary = await censusServiceFixture({missingStageArenaInventoryExport: true});
  assert.equal(ordinary.calls.some(call => call.identifier.startsWith('stage-arena-query:')), false);
  assert.equal(ordinary.result.heapCensus.requireStageArenaOwnerInventory, false);
});

test('stable stage-arena inventory with zero source-step intersection is a valid provenance measurement', async () => {
  const fixture = await censusServiceFixture({requireStageArenaOwnerInventory: true,
    stageArenaInventoryMutation: 'zero-source-step'});
  const inventory = fixture.result.heapCensus.stageArenaOwnerInventory;
  assert.equal(inventory.stableOwnerLifetimeValidated, true);
  assert.equal(inventory.ownerRestoreAdmission, false);
  assert.equal(inventory.boundaries.length, 3);
  assert.ok(inventory.boundaries.every(boundary =>
    boundary.rows.length === boundary.metadata.allocationCount));
  assert.equal(fixture.result.heapCensus.sourceStep.changedRanges.some(range =>
    range.provenance === 'stage-arena-owner-range'), false);
  assert.ok(fixture.result.heapCensus.sourceStep.changedRanges.length > 0);
  assert.equal(fixture.result.heapCensus.actualDrawAndQuiescence.changedRanges.some(range =>
    range.provenance === 'stage-arena-owner-range'), false);
});

test('actual census evaluate branch retains three boundaries and positive sample-300 draw paths', async () => {
  const fixture = await censusServiceFixture();
  const validated = validate300(fixture);
  assert.deepEqual(validated.fighters.map(player => [player.callbackDelta, player.normalModelDispatchDelta]),
    [[5, 1], [5, 1]]);
  assert.equal(validated.fighters[1].initializerPort, 2);
  assert.equal(fixture.result.boundary.interactive, true);
  assert.deepEqual(fixture.calls.map(call => call.identifier), [
    'melee_web_snapshot_quiescent', 'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query', 'melee_web_snapshot_step',
    'melee_web_snapshot_quiescent', 'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query', 'melee_web_synctest_draw_present',
    'melee_web_snapshot_quiescent', 'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query',
  ]);
  assert.deepEqual(fixture.calls[3].args, [300]);
  const census = fixture.result.heapCensus;
  assert.equal(census.sourceStep.beforeHeapSha256, census.beforeStepSha256);
  assert.equal(census.sourceStep.afterHeapSha256, census.afterStepSha256);
  assert.equal(census.actualDrawAndQuiescence.beforeHeapSha256, census.afterStepSha256);
  assert.equal(census.actualDrawAndQuiescence.afterHeapSha256, census.afterDrawSha256);
  assert.notEqual(census.beforeStepSha256, census.afterStepSha256);
  assert.notEqual(census.afterStepSha256, census.afterDrawSha256);
  assert.ok(census.sourceStep.changedRanges.some(range => range.start === 15000 &&
    range.provenance === 'source-c-static-candidate'));
  const audioChange = census.sourceStep.changedRanges.find(range => range.start === 9004);
  assert.equal(audioChange.provenance, 'audio-owner-range');
  assert.equal(audioChange.audioRangeKind, 'audio-owner');
  assert.equal(audioChange.ownerAddress, census.audioOwnerInventory.boundaries[0].audioOwner);
  assert.equal(audioChange.ownerLifetimeTag, '1234567889abcdef');
  const contextChange = census.sourceStep.changedRanges.find(range => range.start === 12004);
  assert.equal(contextChange.provenance, 'match-context-owner-range');
  assert.equal(contextChange.matchContextOwnerAddress,
    census.matchContextOwnerInventory.boundaries[0].owner);
  assert.equal(contextChange.matchContextWorldGenerationTag, 'abcdef0176543210');
  assert.equal(contextChange.matchContextOwnerEpoch, '5566778811223344');
  assert.throws(() => classifyHeapRanges([{start: 9004, end: 9005}], [],
    {base: 14000, bytes: 100}, {
      before: census.audioOwnerInventory.boundaries[0],
      after: {...census.audioOwnerInventory.boundaries[1], audioOwner: 1},
    }, census.nativeOutputStorage), /same-range, same-owner/);
  assert.equal(census.audioOwnerInventory.effectsEnabled, false);
  assert.deepEqual([...census.audioOwnerInventory.boundaryOrder], [
    'before-source-step', 'after-source-step', 'after-actual-draw-and-Aurora-quiescence',
  ]);
  assert.ok(census.sourceStep.changedRanges.some(range => range.start === 2050 &&
    range.provenance === 'native-output-storage-diagnostic-excluded'));
  assert.ok(census.actualDrawAndQuiescence.changedRanges.some(range => range.start === 14000 &&
    range.provenance === 'gameplay-arena'));
  assert.ok([...census.sourceStep.changedRanges, ...census.actualDrawAndQuiescence.changedRanges]
    .every(range => range.ownerRestoreAdmission === false));
  assert.equal(census.audioOwnerInventory.ownerRestoreAdmission, false);
  assert.equal(census.matchContextOwnerInventory.ownerRestoreAdmission, false);
  assert.deepEqual([...census.matchContextOwnerInventory.boundaryOrder], [
    'before-source-step', 'after-source-step', 'after-actual-draw-and-Aurora-quiescence',
  ]);
  assert.equal(census.matchContextOwnerInventory.stableOwnerLifetimeValidated, true);
  assert.equal(census.nativeOutputStorage.diagnosticOnly, true);
});

test('census draw validator permits entry warm-prefix dispatch zero but rejects unobserved sample 300', async () => {
  const fixture = await censusServiceFixture();
  const entry = mutateDiagnostics(fixture.result, view => {
    view.setUint32(56, 0, true);
    view.setUint32(60, 0, true);
  });
  entry.boundary = {...entry.boundary, interactive: false};
  const validated = validateCensusDrawSample(entry, 0, fixture.baseline,
    previousFrame, censusMapping);
  assert.equal(validated.fighters, null);
  assert.deepEqual(validated.diagnostics.callbacks.slice(0, 2), [5, 5]);
  assert.throws(() => validate300({...fixture, result: entry}), /Normal fighter model dispatch/);
  for (const [offset, message] of [[8, /Fighter callback entry/], [56, /Normal fighter model dispatch/]]) {
    const unobserved = mutateDiagnostics(fixture.result, view => view.setUint32(offset, 0, true));
    assert.throws(() => validate300({...fixture, result: unobserved}), message);
  }
});

test('census sample-300 validator rejects bad status, metadata, mapping and counter regressions', async () => {
  const fixture = await censusServiceFixture();
  for (const [offset, value, message] of [
    [4, 1, /status is nonzero/],
    [128, 1, /metadata for selected slot 1/],
    [80, 1, /Processed PAD slot/],
    [104, 41, /fighter kind differs/],
  ]) {
    const result = mutateDiagnostics(fixture.result, view => view.setUint32(offset, value, true));
    assert.throws(() => validate300({...fixture, result}), message);
  }
  for (const field of ['callbacks', 'normalModelDispatches', 'modelTransforms']) {
    const baseline = {...fixture.baseline, [field]: [10, 0, 0, 0, 0, 0]};
    assert.throws(() => validate300({...fixture, baseline}), /counter regressed/);
    assert.throws(() => validateCensusDrawSample(fixture.result, 0, baseline,
      previousFrame, censusMapping), /counter regressed/);
  }
  assert.throws(() => validate300({...fixture, result: {...fixture.result,
    boundary: {...fixture.result.boundary, interactive: false}}}), /interactive boundary/);
  assert.throws(() => validate300({...fixture, result: {...fixture.result, heapCensus: null}}),
    /three-boundary audio owner inventory/);
  assert.throws(() => validate300({...fixture, result: {...fixture.result, pad: ''}}), /44 PAD bytes/);
  assert.throws(() => validate300({...fixture, result: {...fixture.result,
    status: {...fixture.result.status, drawCallCount: 0}}}), /no Aurora draw calls/);
  assert.throws(() => validate300({...fixture, result: {...fixture.result,
    status: {...fixture.result.status, lastBeginFrameId: 300}}}), /begin-frame identity/);
});

test('actual census branch fails at each native transfer boundary and on renderer errors', async () => {
  for (const [failedQuiescence, label] of [[1, /before census source step/],
    [2, /after census source step/], [3, /after census actual draw/]]) {
    await assert.rejects(censusServiceFixture({failedQuiescence}), label);
  }
  await assert.rejects(censusServiceFixture({rendererError: 1}), /Aurora renderer logged native errors/);
  const fixture = await censusServiceFixture();
  for (const boundary of ['beforeStep', 'afterStep', 'afterActualDraw']) {
    const result = {...fixture.result, heapCensus: {...fixture.result.heapCensus,
      nativeTransferQuiescence: {...fixture.result.heapCensus.nativeTransferQuiescence,
        [boundary]: null}}};
    assert.throws(() => validate300({...fixture, result}), /Native transfer boundary checks/);
  }
});

test('audio census requires the export and refuses stale lifetime, changed topology, and invalid ranges', async () => {
  await assert.rejects(censusServiceFixture({missingInventoryExport: true}),
    /Required audio owner inventory export is unavailable/);
  await assert.rejects(censusServiceFixture({inventoryFailureAt: 2}),
    /audio owner inventory after-source-step/);
  await assert.rejects(censusServiceFixture({inventoryMutation: 'owner'}),
    /Audio owner address changed/);
  await assert.rejects(censusServiceFixture({inventoryMutation: 'generation'}),
    /Audio owner lifetime generation changed/);
  await assert.rejects(censusServiceFixture({inventoryMutation: 'fx-generation'}),
    /FX owner lifetime generation changed across census boundaries/);
  await assert.rejects(censusServiceFixture({inventoryMutation: 'topology'}),
    /Audio owner inventory topology changed/);
  for (const [inventoryMutation, message] of [
    ['malformed', /wire layout\/status/], ['capacity', /wire layout\/status/],
    ['layout', /wire layout\/status/], ['overlap', /overlap each other/],
    ['static-overlap', /static manifest range/], ['arena-overlap', /gameplay arena/],
    ['output-overlap', /native PCM output storage/],
  ]) {
    await assert.rejects(censusServiceFixture({inventoryMutation}), message);
  }
});

test('match-context census requires its export and refuses stale owner, lifetime, topology, and overlap', async () => {
  await assert.rejects(censusServiceFixture({missingMatchContextInventoryExport: true}),
    /Required match-context owner inventory export is unavailable/);
  await assert.rejects(censusServiceFixture({matchContextInventoryFailureAt: 2}),
    /match-context owner inventory after-source-step/);
  for (const [matchContextInventoryMutation, message] of [
    ['owner', /owner address changed across census boundaries/],
    ['world-generation', /world generation changed across census boundaries/],
    ['owner-epoch', /owner epoch changed across census boundaries/],
    ['extent', /topology changed across census boundaries/],
  ]) {
    await assert.rejects(censusServiceFixture({matchContextInventoryMutation}), message);
  }
  for (const [matchContextInventoryMutation, message] of [
    ['malformed', /wire layout\/status/], ['layout', /wire layout\/status/],
    ['layout-row', /wire layout\/status/], ['capacity', /wire layout\/status/],
    ['count', /wire layout\/status/], ['status', /wire layout\/status/],
    ['kind', /invalid kind, owner, extent/], ['wrong-owner', /invalid kind, owner, extent/],
    ['bounds', /invalid kind, owner, extent/],
    ['audio-overlap', /overlaps an audio owner range/],
    ['static-overlap', /static manifest range/], ['arena-overlap', /gameplay arena/],
    ['output-overlap', /native PCM output storage/],
  ]) {
    await assert.rejects(censusServiceFixture({matchContextInventoryMutation}), message);
  }
});

test('opt-in Aurora census samples three ordered owner boundaries and keeps all rows unadmitted', async () => {
  await assert.rejects(censusServiceFixture({requireAuroraStagingShadowInventory: true,
    missingAuroraStagingShadowExport: true}),
  /Required Aurora staging-shadow inventory export is unavailable/);
  for (const [auroraInventoryFailureAt, boundary] of [[1, 'before-source-step'],
    [2, 'after-source-step'], [3, 'after-actual-draw-and-Aurora-quiescence']]) {
    await assert.rejects(censusServiceFixture({requireAuroraStagingShadowInventory: true,
      auroraInventoryFailureAt}), new RegExp(`Aurora staging-shadow inventory ${boundary}`));
  }
  const fixture = await censusServiceFixture({requireAuroraStagingShadowInventory: true});
  const validated = validate300(fixture);
  assert.equal(validated.fighters.length, 2);
  const expectedCalls = [
    'melee_web_snapshot_quiescent',
    'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query',
    'melee_web_synctest_aurora_staging_shadow_inventory_query',
    'melee_web_snapshot_step',
    'melee_web_snapshot_quiescent',
    'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query',
    'melee_web_synctest_aurora_staging_shadow_inventory_query',
    'melee_web_synctest_draw_present',
    'melee_web_snapshot_quiescent',
    'melee_web_synctest_audio_range_inventory_query',
    'melee_web_synctest_match_context_range_inventory_query',
    'melee_web_synctest_aurora_staging_shadow_inventory_query',
  ];
  assert.deepEqual(fixture.calls.map(call => call.identifier), expectedCalls);
  const census = fixture.result.heapCensus;
  assert.equal(census.requireAuroraStagingShadowInventory, true);
  assert.equal(census.auroraStagingShadowOwnerInventory.version, 5);
  assert.equal(census.auroraStagingShadowOwnerInventory.ownerRestoreAdmission, false);
  assert.equal(census.auroraStagingShadowOwnerInventory.stableOwnerLifetimeValidated, true);
  assert.deepEqual([...census.auroraStagingShadowOwnerInventory.boundaryOrder], [
    'before-source-step', 'after-source-step',
    'after-actual-draw-and-Aurora-quiescence',
  ]);
  assert.equal(census.auroraStagingShadowOwnerInventory.boundaries.length, 3);
  assert.ok(census.auroraStagingShadowOwnerInventory.boundaries.every(boundary =>
    boundary.rows.length === 10 && boundary.ranges.length === 1 &&
    boundary.ownerRestoreAdmission === false &&
    boundary.diagnosticOutputStorage.ownerRestoreAdmission === false));
  assert.equal(census.sourceStep.changedRanges.some(range =>
    range.provenance === 'aurora-staging-shadow-owner-range'), false);
  assert.ok(census.actualDrawAndQuiescence.changedRanges.some(range =>
    range.start === 13004 && range.provenance === 'aurora-staging-shadow-owner-range' &&
    range.ownerRestoreAdmission === false &&
    range.auroraStagingShadowKind === 'aurora-staging-shadow-verts'));
  assert.ok(census.actualDrawAndQuiescence.changedRanges.some(range =>
    range.provenance === 'aurora-staging-shadow-descriptor-diagnostic-excluded'));
  assert.ok([...census.sourceStep.changedRanges, ...census.actualDrawAndQuiescence.changedRanges]
    .every(range => range.ownerRestoreAdmission === false));
  const missing = {...fixture.result, heapCensus: {...census,
    auroraStagingShadowOwnerInventory: null}};
  assert.throws(() => validate300({...fixture, result: missing}),
    /three-boundary Aurora staging-shadow inventory/);
});

test('Aurora census keeps normal mode compatible and refuses changed lifetime or topology', async () => {
  const ordinary = await censusServiceFixture({missingAuroraStagingShadowExport: true});
  assert.equal(validate300(ordinary).fighters.length, 2);
  assert.equal(ordinary.calls.some(call => call.identifier ===
    'melee_web_synctest_aurora_staging_shadow_inventory_query'), false);

  for (const auroraInventoryMutation of ['generation', 'generation-high']) {
    await assert.rejects(censusServiceFixture({requireAuroraStagingShadowInventory: true,
      auroraInventoryMutation}), /lifetime generation changed/);
  }
  for (const auroraInventoryMutation of ['pointer', 'capacity', 'max-capacity', 'row-topology']) {
    await assert.rejects(censusServiceFixture({requireAuroraStagingShadowInventory: true,
      auroraInventoryMutation}), /owner topology or capacity changed/);
  }
  for (const [auroraInventoryMutation, message] of [
    ['version', /schema, count, offset, stride, or idle status/],
    ['offset', /schema, count, offset, stride, or idle status/],
    ['stride', /schema, count, offset, stride, or idle status/],
    ['current-frame-invalid', /schema, count, offset, stride, or idle status/],
    ['frame-count', /schema, count, offset, stride, or idle status/],
    ['submission-count', /schema, count, offset, stride, or idle status/],
    ['pending-frame', /schema, count, offset, stride, or idle status/],
    ['pending-staging', /schema, count, offset, stride, or idle status/],
    ['worker-busy', /schema, count, offset, stride, or idle status/],
    ['pipeline-not-ready', /schema, count, offset, stride, or idle status/],
    ['queued-pipeline', /schema, count, offset, stride, or idle status/],
    ['zero-generation', /schema, count, offset, stride, or idle status/],
    ['packet-active', /frame row is active/],
    ['packet-alias', /packet buffer is not empty/],
    ['submission-state', /submission row has invalid slot or pending callback state/],
    ['pending-callback', /submission row has invalid slot or pending callback state/],
    ['high-word', /owner row has invalid kind, lifetime extent, or wasm32 bounds/],
    ['max-high-word', /owner row has invalid kind, lifetime extent, or wasm32 bounds/],
    ['overflow', /capacity lies outside wasm32 memory/],
    ['owned', /owner row has invalid kind, lifetime extent, or wasm32 bounds/],
    ['empty-pointer', /empty staging-shadow row has a nonempty address or length/],
    ['overlap', /capacity ranges overlap each other/],
    ['static-overlap', /overlaps static manifest range/],
    ['arena-overlap', /overlaps gameplay arena/],
    ['native-overlap', /overlaps native PCM output storage/],
    ['audio-overlap', /overlaps audio owner range/],
    ['context-overlap', /overlaps match-context owner range/],
    ['output-overlap', /overlaps its diagnostic output/],
  ]) {
    await assert.rejects(censusServiceFixture({requireAuroraStagingShadowInventory: true,
      auroraInventoryMutation}), message);
  }
});

test('browser census records all delegated FX rows when the provider enables effects', async () => {
  const fixture = await censusServiceFixture({inventoryMutation: 'effects'});
  const inventory = fixture.result.heapCensus.audioOwnerInventory;
  assert.equal(inventory.effectsEnabled, true);
  assert.ok(inventory.boundaries.every(boundary => boundary.count === 9));
  assert.deepEqual([...inventory.boundaries[0].rows.slice(6).map(item => item.kindName)], [
    'effects-owner', 'effects-reverb-heap', 'effects-delay-heap',
  ]);
  const audioOwner = inventory.boundaries[0].rows[0];
  const [effectsOwner, reverbHeap, delayHeap] = inventory.boundaries[0].rows.slice(6);
  assert.notDeepEqual([effectsOwner.generationHigh, effectsOwner.generationLow],
    [audioOwner.generationHigh, audioOwner.generationLow]);
  assert.deepEqual([reverbHeap.generationHigh, reverbHeap.generationLow],
    [effectsOwner.generationHigh, effectsOwner.generationLow]);
  assert.deepEqual([delayHeap.generationHigh, delayHeap.generationLow],
    [effectsOwner.generationHigh, effectsOwner.generationLow]);
});


test('two-tick route is explicit, mutually exclusive, and preserves existing defaults', () => {
  assert.equal(validateCpuRestoreRouteSelection({}), 'census-v1');
  assert.equal(validateCpuRestoreRouteSelection({'--aggregate-cpu-restore-config':'config.json'}), 'cpu-only-v1');
  assert.equal(validateCpuRestoreRouteSelection({'--aggregate-cpu-drawn-reference':true,
    '--aggregate-cpu-restore-config':'config.json'}), 'drawn-reference-one-tick-v1');
  assert.equal(validateCpuRestoreRouteSelection({'--aggregate-cpu-drawn-reference-two-tick':true,
    '--aggregate-cpu-restore-config':'config.json'}), 'drawn-reference-no-draw-two-tick-v1');
  assert.throws(()=>validateCpuRestoreRouteSelection({'--aggregate-cpu-drawn-reference-two-tick':true}),
    /requires CPU configuration/);
  assert.throws(()=>validateCpuRestoreRouteSelection({'--aggregate-cpu-drawn-reference':true,
    '--aggregate-cpu-drawn-reference-two-tick':true,'--aggregate-cpu-restore-config':'config.json'}),
    /mutually exclusive/);
});


test('two-tick route checks exact linked v6 runtime identity and both new getters before the prefix', () => {
  const v6={profile:'slippi-render-dependencies-logical-frame-v6'};
  assert.equal(validateBrowserProfileForRoute(v6,true,{evidence:true,evidenceBytes:true}),
    'slippi-render-dependencies-logical-frame-v6');
  assert.throws(()=>validateBrowserProfileForRoute({profile:'slippi-render-dependencies-dbooc-shield-drop-v5'},true,
    {evidence:true,evidenceBytes:true}),/Unexpected profile identity/);
  assert.throws(()=>validateBrowserProfileForRoute(v6,true,{evidence:true,evidenceBytes:false}),
    /evidence exports are unavailable/);
  assert.equal(validateBrowserProfileForRoute({profile:'slippi-render-dependencies-dbooc-shield-drop-v5'},false),
    'slippi-render-dependencies-dbooc-shield-drop-v5');
});
