// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict';
import {test} from 'node:test';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';
import {decodeDrawDiagnostics} from './runner_contract.mjs';
import {validateCensusDrawSample} from './run_write_census.mjs';
import {
  classifyHeapRanges, diffHeapRanges, validateCensusInventory, validateSourceStateManifest,
  runBrowserSample,
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

// Execute the real page.evaluate census branch against a small service fixture.
// This exercises orchestration/guards only; it is not GPU or gameplay evidence.
async function censusServiceFixture({failedQuiescence = 0, rendererError = 0} = {}) {
  const context = vm.createContext({crypto: webcrypto, setTimeout, performance,
    btoa: value => Buffer.from(value, 'binary').toString('base64')});
  vm.runInContext(`
    const heap = new Uint8Array(16384);
    const view = new DataView(heap.buffer);
    const calls = [];
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
        if (identifier === 'melee_web_snapshot_step') {
          if (args.length !== 1 || args[0] !== 300) throw new Error('wrong census input sample');
          heap[15000] = 7;
          heap[6400] = 9;
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
  const baseline = decodeDrawDiagnostics(Buffer.from(vm.runInContext('heap.slice(6500, 6632)', context)));
  const page = {evaluate: (callback, arguments_) => {
    context.arguments_ = arguments_;
    return vm.runInContext(`(${callback.toString()})(arguments_)`, context);
  }};
  const result = await runBrowserSample(page, 300, censusMapping, [4, 4], {
    arenaRange: {base: 14000, bytes: 100},
    manifestRows: [row(15000, 1, 'source-c-static-candidate', 'service-fixture')],
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
    'melee_web_snapshot_quiescent', 'melee_web_snapshot_step',
    'melee_web_snapshot_quiescent', 'melee_web_synctest_draw_present',
    'melee_web_snapshot_quiescent',
  ]);
  assert.deepEqual(fixture.calls[1].args, [300]);
  const census = fixture.result.heapCensus;
  assert.equal(census.sourceStep.beforeHeapSha256, census.beforeStepSha256);
  assert.equal(census.sourceStep.afterHeapSha256, census.afterStepSha256);
  assert.equal(census.actualDrawAndQuiescence.beforeHeapSha256, census.afterStepSha256);
  assert.equal(census.actualDrawAndQuiescence.afterHeapSha256, census.afterDrawSha256);
  assert.notEqual(census.beforeStepSha256, census.afterStepSha256);
  assert.notEqual(census.afterStepSha256, census.afterDrawSha256);
  assert.ok(census.sourceStep.changedRanges.some(range => range.start === 15000 &&
    range.provenance === 'source-c-static-candidate'));
  assert.ok(census.actualDrawAndQuiescence.changedRanges.some(range => range.start === 14000 &&
    range.provenance === 'gameplay-arena'));
  assert.ok([...census.sourceStep.changedRanges, ...census.actualDrawAndQuiescence.changedRanges]
    .every(range => range.ownerRestoreAdmission === false));
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
    /three-boundary heap census/);
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
