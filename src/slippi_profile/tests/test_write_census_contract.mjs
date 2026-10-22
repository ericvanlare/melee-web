// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import {decodeDrawDiagnostics} from './runner_contract.mjs';
import {validateCensusDrawSample} from './run_write_census.mjs';
import {
  classifyHeapRanges, decodeAudioOwnerInventory, diffHeapRanges,
  requireStableAudioOwnerInventory, validateAudioOwnerRangeDisjointness,
  validateCensusInventory, validateSourceStateManifest, runBrowserSample,
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

// Execute the real page.evaluate census branch against a small service fixture.
// This exercises orchestration/guards only; it is not GPU or gameplay evidence.
async function censusServiceFixture({failedQuiescence = 0, rendererError = 0,
  inventoryFailureAt = 0, inventoryMutation = null, missingInventoryExport = false} = {}) {
  const context = vm.createContext({crypto: webcrypto, setTimeout, performance,
    btoa: value => Buffer.from(value, 'binary').toString('base64')});
  vm.runInContext(`
    const heap = new Uint8Array(16384);
    const view = new DataView(heap.buffer);
    const calls = [];
    const inventoryPointer = 8000;
    let inventoryQueryCount = 0;
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
      _melee_web_synctest_gameplay_arena_base: () => 14000,
      _melee_web_synctest_gameplay_arena_bytes: () => 100,
      _melee_web_synctest_audio_range_inventory_query: () => 1,
      _melee_web_synctest_audio_range_inventory: () => inventoryPointer,
      _melee_web_synctest_draw_decision: () => 1,
      _melee_web_snapshot_observation_size: () => 1280,
      _melee_web_snapshot_pcm_size: () => 4272,
      _melee_web_snapshot_observation: () => 0,
      _melee_web_snapshot_pcm: () => 2048,
      _melee_web_snapshot_input: () => 6400,
      _melee_web_synctest_draw_diagnostics_size: () => 132,
      _melee_web_synctest_draw_diagnostics: () => 6500,
      _melee_web_synctest_motion_boundary: () => 0,
      _melee_web_synctest_is_mario_kind: kind => kind === 42 ? 1 : 0,
      _melee_web_synctest_submission_json: () => 1,
      _melee_web_snapshot_error: () => 2,
      UTF8ToString: pointer => pointer === 2 ? 'synthetic native transfer failure' :
        JSON.stringify({rendererErrorCount: ${rendererError}, pendingFramePackets: 0,
          pendingStagingBuffers: 0, workerBusy: 0, drawCallCount: 4,
          lastBeginFrameId: 301, lastEndFrameId: 301}),
      async ccall(identifier, returnType, argumentTypes, args, options) {
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
        if (identifier === 'melee_web_snapshot_step') {
          if (args.length !== 1 || args[0] !== 300) throw new Error('wrong census input sample');
          heap[15000] = 7;
          heap[6400] = 9;
          heap[9004] = 11;
          heap[2050] = 13;
          return 1;
        }
        if (identifier === 'melee_web_synctest_draw_present') {
          heap[14000] = 8;
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
  const baseline = decodeDrawDiagnostics(Buffer.from(vm.runInContext('heap.slice(6500, 6632)', context)));
  const page = {evaluate: (callback, arguments_) => {
    context.arguments_ = arguments_;
    return vm.runInContext(`(${callback.toString()})(arguments_)`, context);
  }};
  const result = await runBrowserSample(page, 300, censusMapping, [4, 4], {
    arenaRange: {base: 14000, bytes: 100},
    manifestRows: [row(15000, 1, 'source-c-static-candidate', 'service-fixture')],
    requireAudioInventory: true,
  });
  return {result, baseline, calls: JSON.parse(vm.runInContext('JSON.stringify(calls)', context))};
}

const censusMapping = [
  {sourceSlot: 0, initializerPort: 0, rawController: 0},
  {sourceSlot: 1, initializerPort: 2, rawController: 1},
];
const previousFrame = {beginFrameId: 300, endFrameId: 300};
const validate300 = ({result, baseline}) =>
  validateCensusDrawSample(result, 300, baseline, previousFrame, censusMapping);

function mutateDiagnostics(result, mutate) {
  const bytes = Buffer.from(result.drawDiagnostics, 'base64');
  mutate(new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength));
  return {...result, drawDiagnostics: bytes.toString('base64')};
}

test('actual census evaluate branch retains three boundaries and positive sample-300 draw paths', async () => {
  const fixture = await censusServiceFixture();
  const validated = validate300(fixture);
  assert.deepEqual(validated.fighters.map(player => [player.callbackDelta, player.normalModelDispatchDelta]),
    [[5, 1], [5, 1]]);
  assert.equal(validated.fighters[1].initializerPort, 2);
  assert.equal(fixture.result.boundary.interactive, true);
  assert.deepEqual(fixture.calls.map(call => call.identifier), [
    'melee_web_snapshot_quiescent', 'melee_web_synctest_audio_range_inventory_query',
    'melee_web_snapshot_step', 'melee_web_snapshot_quiescent',
    'melee_web_synctest_audio_range_inventory_query', 'melee_web_synctest_draw_present',
    'melee_web_snapshot_quiescent', 'melee_web_synctest_audio_range_inventory_query',
  ]);
  assert.deepEqual(fixture.calls[2].args, [300]);
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
