import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {fileURLToPath, pathToFileURL} from 'node:url';
import {createFirstCssStreamComparator, createFirstCssFinalDrawComparator,
  createFirstSssConstructorPairComparator, createFirstSssConsumedPadTickComparator,
  createFirstSssDrawComparator, createFirstSssPrefixComparator, createFirstSssVsEntryComparator,
  createFirstSssVsSetupComparator,
  decodeFirstSssPrefixInputBundle, firstSssAudioAccounting,
  requestSynchronousApproval}
  from './stadium_first_css_stream_compare.mjs';

const COUNT = 148;
const route = Object.freeze({current_game_mode: 2, previous_game_mode: 1,
  current_scene_index: 0, previous_scene_index: 0});
const testDirectory = path.dirname(fileURLToPath(import.meta.url));
const candidateRoot = path.resolve(testDirectory, '..');
const freshSssExpectedPair = JSON.parse(await fs.readFile(
  path.join(testDirectory, 'fixtures/fresh-sss-expected-pair.json'), 'utf8'));


function firstSssDrawExpectedFixture() {
  const inventory = [
    [17, 0, 0x80479d30, 6], [19, 0, 0x804d5f94, 4],
    [20, 0, 0x804d5f90, 4], [21, 0, 0x804c1f84, 0x358],
    [30, 0, 0x80479d58, 4], [36, 0, 0x8045bf28, 2],
    [37, 0, 0x8045bf2a, 2], [40, 0, 0x803dd9c4, 1],
    [2, 0, 0x804c1f78, 0x0c],
  ].map(([tag, flags, address, size]) => ({tag, flags, address, size}));
  const raw = Object.fromEntries(inventory.map(({tag, flags, size}) =>
    [`${tag}:${flags}`, '00'.repeat(size)]));
  raw['17:0'] = '020201010000'; raw['19:0'] = '804d5f90';
  raw['20:0'] = '3bb84c53'; raw['30:0'] = '00000001'; raw['40:0'] = '09';
  const note = phase => ({phase, scene_frame: 1, scene_kind: 9,
    pad_state_hex: '00'.repeat(822), random_seed_hex: '3bb84c53',
    scene_routing_getters: {current_game_mode: 2, previous_game_mode: 1,
      current_scene_index: 1, previous_scene_index: 0}});
  const source = (sequence, boundary, pc) => ({event: 'boundary', sequence,
    source_tick: 1, draw_ordinal: 149, boundary, phase: null, pc, word: null,
    lr: 0x801a5048, argument: null, source_slice_inventory: inventory,
    source_slices_hex: raw, scene_routing_raw_hex: raw['17:0'],
    rng_pointer_hex: raw['19:0']});
  return {schema: 'melee-web-stadium-first-sss-draw-diagnostic', version: 1,
    scope: 'original first SSS DrawEnter1609 through DrawReturn1610 only',
    source_admission: false, whole_session_equivalent: false,
    provenance: {observer_bytes: 4397889,
      observer_sha256: '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778',
      observer_status_bytes: 515,
      observer_status_sha256: '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f',
      draw_enter_sequence: 1609, draw_return_sequence: 1610, source_tick: 1,
      draw_ordinal: 149, scene_frame: 1, scene_kind: 9,
      setup_profile_verified_by_observer: false},
    expected_draw_enter: note('draw_enter'), expected_draw_return: note('draw_return'),
    source_draw_enter: source(1609, 'draw_enter', 0x80390fc0),
    source_draw_return: source(1610, 'draw_return', 0x80391040),
    comparison_fields: ['scene_frame', 'scene_kind', 'pad_state_hex',
      'random_seed_hex', 'scene_routing_getters'],
    native_protocol_requirements: {host_draw_calls: 1, host_tick_calls: 0,
      aurora_begin_calls: 1, aurora_end_calls: 1, frame_end_returned: true,
      all_owners_true: true, audio_render_calls_delta: 0,
      audio_render_frames_delta: 0, audio_phase_unchanged: true}};
}

function firstSssPrerequisites({worldGeneration = 7, audioGeneration = 3} = {}) {
  return {
    pair: {complete: true, compared: true, failed: false, host_entered: true,
      session_phase: 3, sss_host_tick_calls: 0, sss_host_draw_calls: 0,
      world_generation: worldGeneration, audio_generation: audioGeneration,
      entry: {session_ticks: 149}, returned: {session_ticks: 149}},
    tick: {complete: true, compared: true, captured: true, failed: false,
      host_tick_calls: 1, host_draw_calls: 0, clock_post_succeeded: true,
      tick_result: 1, transition_requested: false, post_host_frame_captured: true,
      post_host_frame: 1},
  };
}

function expectedPairs() {
  return Array.from({length: COUNT}, (_, index) => {
    const tick = index + 1;
    const draw = index + 2;
    return {
      index,
      consumed_pad_sequence: 839 + index * 5,
      source_tick_sequence: 840 + index * 5,
      draw_enter_sequence: 841 + index * 5,
      draw_return_sequence: 842 + index * 5,
      expected_post_tick: {source_tick_value: tick, source_draw_ordinal: tick,
        native_post_host_tick_frame: draw, pad_state_hex: 'aa'.repeat(822),
        random_seed_hex: '312151c3', scene_routing_getters: route},
      expected_draw_return: {source_tick: draw, draw_ordinal: tick,
        scene_frame: draw, pad_state_hex: 'aa'.repeat(822),
        random_seed_hex: '312151c3', scene_routing_getters: route},
    };
  });
}

function actualTick(index) {
  return {source_scene: 1, menu_phase: 1, scene_kind: 8,
    world_generation_stable: true, scene_owner_stable: true, seed_owner_stable: true,
    source_tick: index + 1, draw_ordinal: index + 1, scene_frame: index + 2,
    pad_state_hex: 'aa'.repeat(822), random_seed_hex: '312151c3',
    scene_routing_getters: route};
}

function actualDraw(index) {
  return {source_scene: 1, menu_phase: 1, scene_kind: 8,
    world_generation_stable: true, scene_owner_stable: true, seed_owner_stable: true,
    source_tick: index + 2, draw_ordinal: index + 1, scene_frame: index + 2,
    pad_state_hex: 'aa'.repeat(822), random_seed_hex: '312151c3',
    scene_routing_getters: route};
}

test('strict CSS stream comparator approves one tick then its paired draw only', () => {
  const comparator = createFirstCssStreamComparator(expectedPairs());
  assert.equal(comparator.compare('tick', JSON.stringify(actualTick(0))), true);
  assert.deepEqual(comparator.status(), {next_index: 0, pending_draw: true,
    terminal: false, failed: false, outcome: null, terminal_observation: null});
  assert.equal(comparator.compare('draw', JSON.stringify(actualDraw(0))), true);
  assert.equal(comparator.status().next_index, 1);
  assert.equal(comparator.status().pending_draw, false);
});

test('mismatch and out-of-order callbacks fail closed without advancing the prefix', () => {
  const beforeTick = createFirstCssStreamComparator(expectedPairs());
  assert.equal(beforeTick.compare('draw', JSON.stringify(actualDraw(0))), false);
  assert.equal(beforeTick.status().failed, true);
  assert.equal(beforeTick.status().next_index, 0);

  const badTick = createFirstCssStreamComparator(expectedPairs());
  const changed = {...actualTick(0), random_seed_hex: '312151c4'};
  assert.equal(badTick.compare('tick', JSON.stringify(changed)), false);
  assert.equal(badTick.status().next_index, 0);

  const badDraw = createFirstCssStreamComparator(expectedPairs());
  assert.equal(badDraw.compare('tick', JSON.stringify(actualTick(0))), true);
  assert.equal(badDraw.compare('draw', JSON.stringify({...actualDraw(0), scene_frame: 9})), false);
  assert.equal(badDraw.status().next_index, 0);
});

function transition(index) {
  return {source_tick: index + 1, draw_ordinal: index + 1,
    scene_frame: index + 2,
    tick_result: 3, executed_host_ticks: index + 2,
    stream_input_ordinal: index + 1,
    consumed_pad_sequence: 839 + index * 5,
    terminal_transition: true, source_scene: 1, menu_phase: 1, scene_kind: 8,
    world_generation_stable: true, scene_owner_stable: true, seed_owner_stable: true,
    random_seed_hex: '312151c3', pad_state_hex: 'aa'.repeat(822),
    scene_routing_getters: route};
}

test('an early transition is retained as a failed terminal partial prefix', () => {
  const comparator = createFirstCssStreamComparator(expectedPairs());
  assert.equal(comparator.compare('transition', JSON.stringify(transition(0))), false);
  assert.equal(comparator.status().outcome, 'early_source_transition_before_raw_pair_cap');
  assert.equal(comparator.status().failed, true);
  assert.equal(comparator.status().terminal, true);
  assert.equal(comparator.status().next_index, 0);
  assert.equal(comparator.status().terminal_observation.tick_result, 3);
  assert.equal(comparator.compare('tick', JSON.stringify(actualTick(0))), false);
});

test('the final retained transition is a bounded stop with the final draw unpaired', () => {
  const comparator = createFirstCssStreamComparator(expectedPairs());
  for (let index = 0; index < COUNT - 1; index += 1) {
    assert.equal(comparator.compare('tick', JSON.stringify(actualTick(index))), true);
    assert.equal(comparator.compare('draw', JSON.stringify(actualDraw(index))), true);
  }
  assert.deepEqual(comparator.status(), {next_index: COUNT - 1,
    pending_draw: false, terminal: false, failed: false, outcome: null,
    terminal_observation: null});
  assert.equal(comparator.compare('transition', JSON.stringify(transition(COUNT - 1))), false);
  assert.deepEqual(comparator.status(), {next_index: COUNT - 1,
    pending_draw: false, terminal: true, failed: false,
    outcome: 'stop_after_last_input_request',
    terminal_observation: transition(COUNT - 1)});
  assert.equal(comparator.compare('draw', JSON.stringify(actualDraw(COUNT - 1))), false,
    'No approval is issued for the raw final draw after transition');
  assert.equal(comparator.status().next_index, COUNT - 1,
    'The bounded stop retains 147 approved pairs rather than claiming all 148');
});

test('only synchronous boolean true can approve a boundary', () => {
  assert.equal(requestSynchronousApproval(() => true, 'tick', '{}'), true);
  assert.equal(requestSynchronousApproval(() => 1, 'tick', '{}'), false);
  assert.equal(requestSynchronousApproval(() => Promise.resolve(true), 'tick', '{}'), false);
  assert.equal(requestSynchronousApproval(() => { throw Error('refuse'); }, 'tick', '{}'), false);
  assert.equal(requestSynchronousApproval(null, 'tick', '{}'), false);
});

test('SSS audio servicing preserves the existing 32000/60 carry and counts only approved ticks', () => {
  const firstDraw = firstSssAudioAccounting({audio_render_calls_before: 19,
    audio_render_calls_after: 19, audio_render_frames_before: 10133,
    audio_render_frames_after: 10133, audio_phase_before: 17, audio_phase_after: 17}, 0);
  assert.equal(firstDraw.valid, true, firstDraw.error);
  assert.equal(firstDraw.calls_delta, 0);
  assert.equal(firstDraw.frames_delta, 0);

  const beforePhase = 59, ticks = 124;
  const total = beforePhase + 32000 * ticks;
  const prefix = firstSssAudioAccounting({audio_render_calls_before: 4,
    audio_render_calls_after: 4 + ticks, audio_render_frames_before: 7,
    audio_render_frames_after: 7 + Math.floor(total / 60),
    audio_phase_before: beforePhase, audio_phase_after: total % 60}, ticks);
  assert.equal(prefix.valid, true, prefix.error);
  assert.equal(prefix.calls_delta, 124);
  assert.equal(prefix.frames_delta, Math.floor(total / 60));
  assert.equal(prefix.after.phase, total % 60);

  const refusedTick = firstSssAudioAccounting({audio_render_calls_before: 4,
    audio_render_calls_after: 4 + 7, audio_render_frames_before: 7,
    audio_render_frames_after: 7 + Math.floor((11 + 32000 * 7) / 60),
    audio_phase_before: 11, audio_phase_after: (11 + 32000 * 7) % 60}, 7);
  assert.equal(refusedTick.valid, true,
    'A refused later tick contributes no audio render beyond seven approved ticks');
});

test('SSS audio policy rejects render-after-refusal and lost fractional carry', () => {
  const extraCall = firstSssAudioAccounting({audio_render_calls_before: 0,
    audio_render_calls_after: 2, audio_render_frames_before: 0,
    audio_render_frames_after: 1066, audio_phase_before: 0, audio_phase_after: 8}, 1);
  assert.equal(extraCall.valid, false);
  assert.match(extraCall.error, /call delta/);

  const resetCarry = firstSssAudioAccounting({audio_render_calls_before: 0,
    audio_render_calls_after: 1, audio_render_frames_before: 0,
    audio_render_frames_after: 533, audio_phase_before: 59, audio_phase_after: 20}, 1);
  assert.equal(resetCarry.valid, false);
  assert.match(resetCarry.error, /frame delta|preserved carry/);
});

test('first SSS DrawEnter mismatch is retained and stops the comparator', () => {
  const expected = firstSssDrawExpectedFixture();
  const {pair, tick} = firstSssPrerequisites();
  const comparator = createFirstSssDrawComparator(expected, pair, tick);
  const wanted = expected.expected_draw_enter;
  const actual = {phase: 'draw_enter', source_scene: 2, scene_kind: wanted.scene_kind,
    scene_frame: 0, random_seed_hex: wanted.random_seed_hex,
    pad_state_hex: wanted.pad_state_hex,
    scene_routing_getters: {...wanted.scene_routing_getters},
    owners: {host: true, session: true, world: true, audio: true, vs_mode: true,
      scene_info: true, payload: true, seed: true},
    world_generation: pair.world_generation, audio_generation: pair.audio_generation};
  assert.equal(comparator.compare('draw_enter', JSON.stringify(actual)), false);
  const status = comparator.status();
  assert.equal(status.failed, true);
  assert.equal(status.approved, false);
  assert.equal(status.first_mismatch.path, 'draw.draw_enter.scene_frame');
  assert.deepEqual(status.rows[0].actual, actual,
    'The first actual snapshot is retained on comparison failure');
  assert.equal(comparator.compare('draw_return', JSON.stringify({...actual,
    phase: 'draw_return', scene_frame: 1})), false,
  'A failed first phase cannot advance to DrawReturn');
});


test('first SSS prefix comparator rejects malformed retained expectations before callbacks', () => {
  const {pair, tick} = firstSssPrerequisites();
  const draw = {complete: true, compared: true, failed: false,
    host_draw_calls: 1, host_tick_calls: 0, aurora_begin_calls: 1,
    aurora_end_calls: 1, frame_end_returned: true};
  assert.throws(() => createFirstSssPrefixComparator({}, [], pair, tick, draw),
    /exact retained rows1609–2427/);
});

const retainedPrefixExpectedPath = process.env.MELEE_WEB_STADIUM_FIRST_SSS_PREFIX_EXPECTED;
const retainedPrefixInputPath = process.env.MELEE_WEB_STADIUM_FIRST_SSS_PREFIX_INPUT;
const retainedPrefixSerializerLogPath = process.env.MELEE_WEB_STADIUM_FIRST_SSS_PREFIX_SERIALIZER_LOG;
function actualPrefixPhase(record, phase, pair) {
  const expected = record[phase];
  return {phase, source_scene: 2, scene_kind: expected.scene_kind,
    scene_frame: expected.scene_frame, random_seed_hex: expected.random_seed_hex,
    pad_state_hex: expected.pad_state_hex,
    scene_routing_getters: {...expected.scene_routing_getters},
    owners: {host: true, session: true, world: true, audio: true, vs_mode: true,
      scene_info: true, payload: true, seed: true},
    world_generation: pair.world_generation, audio_generation: pair.audio_generation,
    input_index: record.input_index, input_ordinal: record.input_ordinal,
    consumed_pad_sequence: record.consumed_pad_sequence,
    ...(phase === 'scheduler_end' ? {consumed_pad_hex: expected.consumed_pad_hex} : {})};
}

function actualPrefixExit(expected, sessionTicks, pair, serializedExit = null) {
  const note = serializedExit ? structuredClone(serializedExit) :
    nativeSssNote(expected.exit_note.expected, 'sss_exit');
  return {...note, world_generation: pair.world_generation,
    audio_generation: pair.audio_generation, session_ticks: sessionTicks};
}

async function retainedPrefixFixture() {
  const expected = JSON.parse(await fs.readFile(retainedPrefixExpectedPath, 'utf8'));
  const input = new Uint8Array(await fs.readFile(retainedPrefixInputPath));
  const serializerRows = retainedPrefixSerializerLogPath ?
    (await fs.readFile(retainedPrefixSerializerLogPath, 'utf8')).trim().split('\n').map(JSON.parse) : null;
  const prerequisitesPath = retainedPrefixSerializerLogPath ?
    path.join(path.dirname(retainedPrefixSerializerLogPath), 'prerequisites.json') : null;
  const prerequisites = prerequisitesPath ? JSON.parse(await fs.readFile(prerequisitesPath, 'utf8')) : null;
  if (serializerRows) {
    assert.equal(serializerRows.length, 8);
    assert.equal(serializerRows[6].phase, 'sss_exit');
    assert.equal(serializerRows[6].session_ticks, 125,
      'the retained writer fixture predates the nonzero lifetime base');
    assert.deepEqual(serializerRows[7].selected_stage_note, {index: 18, kind: 3});
    assert.equal(prerequisites.first_sss_constructor_pair.entry.session_ticks, 149);
    assert.equal(prerequisites.first_sss_constructor_pair.returned.session_ticks, 149);
  }
  return {expected, inputRows: decodeFirstSssPrefixInputBundle(input),
    serializedExit: serializerRows?.[6] ?? null,
    serializedSelection: serializerRows?.[7]?.selected_stage_note ?? null,
    prerequisites, serializedDrawObservation: serializerRows?.[2] ?? null};
}

function feedRetainedPrefixSamples(comparator, expected, pair) {
  for (const record of expected.records) {
    for (const phase of ['scheduler_end', 'draw_enter', 'draw_return']) {
      const actual = actualPrefixPhase(record, phase, pair);
      assert.equal(comparator.compare(phase, JSON.stringify(actual)), true,
        `retained source-shaped ${phase} sample ${record.input_index} is accepted`);
    }
  }
}

test('retained prefix drives the ordered comparator through lifetime exit ticks 274',
  {skip: !(retainedPrefixExpectedPath && retainedPrefixInputPath)}, async () => {
    // This is an actual-comparator control, not a native run: each callback is
    // shaped from the pinned expected row and input identity. It exercises all
    // 372 ordered input/tick/draw callbacks before the real exit comparison.
    const {expected, inputRows, serializedExit, serializedSelection, prerequisites,
      serializedDrawObservation} = await retainedPrefixFixture();
    const fallback = firstSssPrerequisites();
    const pair = prerequisites?.first_sss_constructor_pair ?? fallback.pair;
    const tick = prerequisites?.first_sss_consumed_tick ?? fallback.tick;
    const draw = serializedDrawObservation ?? {complete: true, compared: true, failed: false,
      host_draw_calls: 1, host_tick_calls: 0, aurora_begin_calls: 1,
      aurora_end_calls: 1, frame_end_returned: true};
    assert.throws(() => createFirstSssPrefixComparator(expected, inputRows,
      {...pair, returned: {...pair.returned, session_ticks: 148}}, tick, draw),
    /approved constructor, tick and first-draw prerequisites/,
    'unequal constructor-note counters cannot authorize a derived exit count');
    const unsafeBase = Number.MAX_SAFE_INTEGER - 124;
    assert.throws(() => createFirstSssPrefixComparator(expected, inputRows,
      {...pair, entry: {...pair.entry, session_ticks: unsafeBase},
        returned: {...pair.returned, session_ticks: unsafeBase}}, tick, draw),
    /approved constructor, tick and first-draw prerequisites/,
    'overflowing lifetime-counter derivation is refused');
    const comparator = createFirstSssPrefixComparator(expected, inputRows, pair, tick, draw);
    feedRetainedPrefixSamples(comparator, expected, pair);
    const exit = actualPrefixExit(expected, 274, pair, serializedExit);
    assert.equal(exit.scene_frame, 125, 'source frame remains independent of lifetime ticks');
    assert.equal(comparator.compare('sss_exit', JSON.stringify(exit)), true);
    assert.equal(comparator.compare('selected_stage', JSON.stringify(
      serializedSelection ?? expected.selected_stage_source_witness.selected_stage)), true);
    const status = comparator.status();
    assert.deepEqual({attempted: status.attempted, approved: status.approved,
      failed: status.failed, next_phase: status.next_phase, first_mismatch: status.first_mismatch},
    {attempted: 374, approved: true, failed: false, next_phase: null, first_mismatch: null});

    for (const rejectedTicks of [125, 273]) {
      const rejectedComparator = createFirstSssPrefixComparator(expected, inputRows, pair, tick, draw);
      feedRetainedPrefixSamples(rejectedComparator, expected, pair);
      const rejectedExit = actualPrefixExit(expected, rejectedTicks, pair, serializedExit);
      assert.equal(rejectedExit.scene_frame, 125);
      assert.equal(rejectedComparator.compare('sss_exit', JSON.stringify(rejectedExit)), false,
        `stale or drifted lifetime counter ${rejectedTicks} is refused`);
      const rejectedStatus = rejectedComparator.status();
      assert.equal(rejectedStatus.approved, false);
      assert.equal(rejectedStatus.first_mismatch.path, 'sss_exit.session');
      assert.equal(rejectedStatus.rows[372].actual.session_ticks, rejectedTicks,
        'the rejected actual exit sample remains retained');
      assert.equal(rejectedStatus.rows[372].actual.scene_frame, 125,
        'the source frame remains intact in the rejected sample');
    }
  });

test('retained first SSS prefix mismatch preserves the actual scheduler sample and first path',
  {skip: !(retainedPrefixExpectedPath && retainedPrefixInputPath)}, async () => {
    // These optional files are retained private source extraction outputs. CI
    // uses the synthetic draw failure above; the external retained-source
    // control supplies this full 124-record prefix fixture without a new capture.
    const expected = JSON.parse(await fs.readFile(retainedPrefixExpectedPath, 'utf8'));
    const input = new Uint8Array(await fs.readFile(retainedPrefixInputPath));
    const inputRows = decodeFirstSssPrefixInputBundle(input);
    const {pair, tick} = firstSssPrerequisites();
    const draw = {complete: true, compared: true, failed: false,
      host_draw_calls: 1, host_tick_calls: 0, aurora_begin_calls: 1,
      aurora_end_calls: 1, frame_end_returned: true};
    const comparator = createFirstSssPrefixComparator(expected, inputRows, pair, tick, draw);
    const record = expected.records[0];
    const actual = {phase: 'scheduler_end', source_scene: 2,
      scene_kind: record.scheduler_end.scene_kind, scene_frame: 99,
      random_seed_hex: record.scheduler_end.random_seed_hex,
      pad_state_hex: record.scheduler_end.pad_state_hex,
      scene_routing_getters: {...record.scheduler_end.scene_routing_getters},
      owners: {host: true, session: true, world: true, audio: true, vs_mode: true,
        scene_info: true, payload: true, seed: true},
      world_generation: pair.world_generation, audio_generation: pair.audio_generation,
      input_index: record.input_index, input_ordinal: record.input_ordinal,
      consumed_pad_sequence: record.consumed_pad_sequence,
      consumed_pad_hex: record.consumed_pad_status_hex.join('')};
    assert.equal(comparator.compare('scheduler_end', JSON.stringify(actual)), false);
    const status = comparator.status();
    assert.equal(status.failed, true);
    assert.equal(status.approved, false);
    assert.equal(status.first_mismatch.path, 'prefix[0].scheduler_end.scene_frame');
    assert.deepEqual(status.rows[0].actual, actual,
      'The first native-shaped sample remains available after mismatch');
  });

test('the actual browser runner recognizes SSS modes and rejects conflicting/orphan inputs before startup', () => {
  const runner = path.join(candidateRoot, 'tests/stadium_c1a_browser_test.mjs');
  const conflict = spawnSync(process.execPath,
    [runner, '--first-sss-draw', '--first-sss-prefix'],
    {encoding: 'utf8', timeout: 10000});
  assert.notEqual(conflict.status, 0);
  assert.match(conflict.stderr, /Choose one private C1a diagnostic route/);

  const orphan = spawnSync(process.execPath,
    [runner, '--first-sss-draw', '--first-sss-draw-expected', 'draw.json',
      '--first-sss-prefix-expected', 'prefix.json', '--first-sss-prefix-input', 'prefix.mwst'],
    {encoding: 'utf8', timeout: 10000});
  assert.notEqual(orphan.status, 0);
  assert.match(orphan.stderr, /Choose one private C1a diagnostic route/);

  const accepted = spawnSync(process.execPath,
    [runner, '--first-sss-prefix', '--first-sss-pair-expected', 'pair.json',
      '--first-sss-tick-expected', 'tick.json', '--first-sss-tick-input', 'tick.mwst',
      '--first-sss-draw-expected', 'draw.json', '--first-sss-prefix-expected', 'prefix.json',
      '--first-sss-prefix-input', 'prefix.mwst'],
    {encoding: 'utf8', timeout: 10000});
  assert.notEqual(accepted.status, 0);
  assert.match(accepted.stderr, /Missing --build/,
    'All legacy SSS options parsed and route validation passed before required runtime setup');

  const freshArgs = ['--first-css-fresh-finite', '--first-css-finite-receipt', 'receipt.json',
    '--first-css-constructor-manifest', 'manifest.json', '--first-sss-constructor-pair-fresh',
    '--first-sss-pair-expected', 'pair.json'];
  const fresh = spawnSync(process.execPath, [runner, ...freshArgs],
    {encoding: 'utf8', timeout: 10000});
  assert.notEqual(fresh.status, 0);
  assert.match(fresh.stderr, /Missing --build/,
    'The explicit fresh finite-CSS pair route passes option validation before runtime setup');
  for (const values of [
    ['--first-sss-constructor-pair-fresh', '--first-sss-pair-expected', 'pair.json'],
    [...freshArgs, '--first-sss-constructor-pair'],
    [...freshArgs, '--first-sss-tick-expected', 'tick.json', '--first-sss-tick-input', 'tick.mwst'],
    [...freshArgs, '--first-sss-draw-expected', 'draw.json'],
    [...freshArgs, '--first-sss-prefix-expected', 'prefix.json', '--first-sss-prefix-input', 'prefix.mwst'],
  ]) {
    const refused = spawnSync(process.execPath, [runner, ...values],
      {encoding: 'utf8', timeout: 10000});
    assert.notEqual(refused.status, 0);
    assert.match(refused.stderr, /Fresh SSS pair is an explicit finite-CSS constructor-only continuation/);
  }
});

test('the actual first-SSS expected loader checks the exact expected and comparator bindings', async () => {
  const runnerPath = path.join(candidateRoot, 'tests/stadium_c1a_browser_test.mjs');
  const runnerSource = await fs.readFile(runnerPath, 'utf8');
  const match = runnerSource.match(/async function loadBoundFirstSssExpected\([^\n]*\) \{[\s\S]*?\n\}/);
  assert.ok(match, 'Actual pre-server expected loader is present');
  const loaderFactory = new Function('assert', 'path', 'fs', 'digest', 'errorText',
    'pathToFileURL', 'ROOT', `return (values, preflight) => { ${match[0]}; return loadBoundFirstSssExpected; };`)(
      assert, path, fs,
      bytes => createHash('sha256').update(bytes).digest('hex'), String,
      pathToFileURL, candidateRoot);
  const loadBound = (values, preflight, ...args) =>
    loaderFactory(values, preflight)(...args);
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'melee-sss-loader-test-'));
  try {
    const expected = firstSssDrawExpectedFixture();
    const expectedBytes = Buffer.from(`${JSON.stringify(expected)}\n`);
    const expectedPath = path.join(dir, 'first-draw.json');
    await fs.writeFile(expectedPath, expectedBytes);
    const comparatorPath = path.join(candidateRoot, 'tests/stadium_first_css_stream_compare.mjs');
    const comparatorBytes = await fs.readFile(comparatorPath);
    const digest = bytes => createHash('sha256').update(bytes).digest('hex');
    const bindings = {
      first_sss_draw: {expected_json: {path: expectedPath,
        basename: path.basename(expectedPath), bytes: expectedBytes.byteLength,
        sha256: digest(expectedBytes)},
      comparator: {path: comparatorPath, bytes: comparatorBytes.byteLength,
        sha256: digest(comparatorBytes)}},
    };
    const values = {'first-sss-draw-expected': expectedPath};
    const loaded = await loadBound(values, bindings, 'first_sss_draw',
      'first-sss-draw-expected', 'melee-web-stadium-first-sss-draw-diagnostic');
    assert.equal(loaded.identity.sha256, digest(expectedBytes));
    assert.equal(loaded.comparator.sha256, digest(comparatorBytes));

    const mutatedBindings = structuredClone(bindings);
    mutatedBindings.first_sss_draw.comparator.sha256 = '00'.repeat(32);
    await assert.rejects(loadBound(values, mutatedBindings, 'first_sss_draw',
      'first-sss-draw-expected', 'melee-web-stadium-first-sss-draw-diagnostic'),
    /comparator hash differs from preflight/);
  } finally {
    await fs.rm(dir, {recursive: true, force: true});
  }
});

test('the actual prefix loader checks the exact expected, input, and comparator bindings',
  {skip: !(retainedPrefixExpectedPath && retainedPrefixInputPath)}, async () => {
    // Full prefix rows are retained private source evidence; this optional
    // pre-server control validates the real loader without launching a server.
    const runnerPath = path.join(candidateRoot, 'tests/stadium_c1a_browser_test.mjs');
    const runnerSource = await fs.readFile(runnerPath, 'utf8');
    const boundMatch = runnerSource.match(/async function loadBoundFirstSssExpected\([^\n]*\) \{[\s\S]*?\n\}/);
    const prefixMatch = runnerSource.match(/async function loadFirstSssPrefixInputs\([^\n]*\) \{[\s\S]*?\n\}/);
    assert.ok(boundMatch && prefixMatch, 'Actual SSS expected and prefix loaders are present');
    const digest = bytes => createHash('sha256').update(bytes).digest('hex');
    const boundFactory = new Function('assert', 'path', 'fs', 'digest', 'errorText',
      'pathToFileURL', 'ROOT', `return (values, preflight) => { ${boundMatch[0]}; return loadBoundFirstSssExpected; };`)(
        assert, path, fs, digest, String, pathToFileURL, candidateRoot);
    const expectedBytes = await fs.readFile(retainedPrefixExpectedPath);
    const inputBytes = await fs.readFile(retainedPrefixInputPath);
    const expected = JSON.parse(expectedBytes.toString('utf8'));
    const comparatorPath = path.join(candidateRoot, 'tests/stadium_first_css_stream_compare.mjs');
    const comparatorBytes = await fs.readFile(comparatorPath);
    const preflight = {first_sss_prefix: {
      expected_json: {path: retainedPrefixExpectedPath,
        basename: path.basename(retainedPrefixExpectedPath), bytes: expectedBytes.byteLength,
        sha256: digest(expectedBytes)},
      comparator: {path: comparatorPath, bytes: comparatorBytes.byteLength,
        sha256: digest(comparatorBytes)},
      input_bundle: {path: retainedPrefixInputPath,
        basename: path.basename(retainedPrefixInputPath), bytes: inputBytes.byteLength,
        sha256: digest(inputBytes)},
    }};
    const values = {'first-sss-prefix-expected': retainedPrefixExpectedPath,
      'first-sss-prefix-input': retainedPrefixInputPath};
    const loadBound = boundFactory(values, preflight);
    const prefixFactory = new Function('assert', 'path', 'fs', 'digest',
      'pathToFileURL', 'ROOT', `return (values, preflight, firstSssPrefix, firstSssDrawInputs, loadBoundFirstSssExpected) => { ${prefixMatch[0]}; return loadFirstSssPrefixInputs; };`)(
        assert, path, fs, digest, pathToFileURL, candidateRoot);
    const drawInputs = {expected: {provenance: {observer_sha256:
      expected.provenance.observer_sha256}}};
    const loadPrefix = prefixFactory(values, preflight, true, drawInputs,
      (...args) => loadBound(...args));
    const loaded = await loadPrefix();
    assert.equal(loaded.expected.records.length, 124);
    assert.equal(loaded.inputIdentity.sha256, digest(inputBytes));
    assert.equal(loaded.inputRows.length, 124);
    assert.equal(loaded.expected.input_bundle.contains_expected_state, false);

    const changed = structuredClone(preflight);
    changed.first_sss_prefix.input_bundle.sha256 = '00'.repeat(32);
    const rejected = prefixFactory(values, changed, true, drawInputs,
      (...args) => boundFactory(values, changed)(...args));
    await assert.rejects(rejected(), /SSS prefix input hash differs from preflight/);
  });

test('malformed callbacks and unexpected routing fields are terminal refusals', () => {
  const malformed = createFirstCssStreamComparator(expectedPairs());
  assert.equal(malformed.compare(null, '{}'), false);
  assert.equal(malformed.status().failed, true);
  assert.equal(malformed.compare('tick', JSON.stringify(actualTick(0))), false);

  const routeExtra = createFirstCssStreamComparator(expectedPairs());
  assert.equal(routeExtra.compare('tick', JSON.stringify({...actualTick(0),
    scene_routing_getters: {...route, pending_mode: 0}})), false);
  assert.equal(routeExtra.status().failed, true);
});


function finalFixture() {
  const pair = expectedPairs()[147];
  pair.expected_draw_return.scene_kind = 8;
  const owner = {...transition(147), world_generation: 1};
  const actual = {...actualDraw(147), world_generation: 1};
  return {pair, owner, actual};
}

test('separate final CSS draw approves exact row1577 once without reviving the stream', () => {
  const {pair, owner, actual} = finalFixture();
  const comparator = createFirstCssFinalDrawComparator(pair, owner);
  assert.equal(comparator.compare('final_draw', JSON.stringify(actual)), true);
  assert.deepEqual(comparator.status(), {attempted: true, approved: true, failed: false});
  assert.equal(comparator.compare('final_draw', JSON.stringify(actual)), false);
  assert.equal(comparator.status().failed, true);
  const stream = createFirstCssStreamComparator(expectedPairs());
  for (let i = 0; i < 147; ++i) {
    assert.equal(stream.compare('tick', JSON.stringify(actualTick(i))), true);
    assert.equal(stream.compare('draw', JSON.stringify(actualDraw(i))), true);
  }
  assert.equal(stream.compare('transition', JSON.stringify(owner)), false);
  const stopped = stream.status();
  assert.equal(stream.compare('draw', JSON.stringify(actual)), false);
  assert.deepEqual(stream.status(), stopped);
});

test('final CSS draw rejects every changed authority field and foreign or missing owner', () => {
  const {pair, owner, actual} = finalFixture();
  const changes = {source_tick: 148, draw_ordinal: 147, scene_frame: 150,
    pad_state_hex: 'bb'.repeat(822), random_seed_hex: '312151c4', scene_kind: 9,
    scene_routing_getters: {...route, current_scene_index: 1},
    source_scene: 2, menu_phase: 2, world_generation: 2,
    world_generation_stable: false, scene_owner_stable: false, seed_owner_stable: false};
  for (const [key, value] of Object.entries(changes)) {
    const comparator = createFirstCssFinalDrawComparator(pair, owner);
    assert.equal(comparator.compare('final_draw', JSON.stringify({...actual, [key]: value})), false, key);
    assert.equal(comparator.status().failed, true, key);
    const missing = {...actual}; delete missing[key];
    assert.equal(createFirstCssFinalDrawComparator(pair, owner)
      .compare('final_draw', JSON.stringify(missing)), false, 'missing '+key);
  }
  assert.equal(createFirstCssFinalDrawComparator(pair, owner).compare('final_draw',
    JSON.stringify({...actual, scene_routing_getters: {...route, pending_mode: 0}})), false);
});

test('final CSS draw malformed phase/data and foreign raw provenance cannot authorize', () => {
  const {pair, owner, actual} = finalFixture();
  for (const [phase, data] of [['draw', JSON.stringify(actual)], ['final_draw', '{'],
    ['final_draw', null], ['final_draw', 'null'], ['final_draw', '[]']]) {
    const comparator = createFirstCssFinalDrawComparator(pair, owner);
    assert.equal(comparator.compare(phase, data), false);
    assert.equal(comparator.compare('final_draw', JSON.stringify(actual)), false);
    assert.equal(comparator.status().failed, true);
  }
  for (const [key, value] of Object.entries({index: 146, consumed_pad_sequence: 1569,
    source_tick_sequence: 1570, draw_enter_sequence: 1571, draw_return_sequence: 1572}))
    assert.throws(() => createFirstCssFinalDrawComparator({...pair, [key]: value}, owner), /exact original row1577/);
  for (const [key, value] of Object.entries({world_generation: 0, source_tick: 147,
    consumed_pad_sequence: 1569, executed_host_ticks: 148, tick_result: 1,
    terminal_transition: false, scene_owner_stable: false}))
    assert.throws(() => createFirstCssFinalDrawComparator(pair, {...owner, [key]: value}), /terminal owner/);
});

test('final CSS draw callback absence, exception, Promise and nontrue results fail closed', () => {
  const {pair, owner, actual} = finalFixture();
  const data = JSON.stringify(actual);
  for (const callback of [undefined, null, () => {throw Error('refuse');},
    () => Promise.resolve(true), () => 1, () => 'true', () => false])
    assert.equal(requestSynchronousApproval(callback, 'final_draw', data), false);
  const comparator = createFirstCssFinalDrawComparator(pair, owner);
  assert.equal(requestSynchronousApproval(comparator.compare, 'final_draw', data), true);
});

const SSS_RULES = Object.freeze({
  match_kind: 1, hud_layout: 2, timer_enabled: false, timer_counts_up: false,
  friendly_fire: false, is_stock: true, single_button: false, disable_pausing: false,
  is_vs: true, is_teams: 0, item_frequency: 0, stage_kind: 3, time_limit: 0,
  item_mask: '0000000000000000', damage_ratio_bits: '3f800000',
  game_speed_bits: '3f800000',
});
const SSS_PLAYER_KEYS = [
  'ckind', 'slot_type', 'stocks', 'color', 'slot', 'spawn', 'spawn_direction',
  'sub_color', 'handicap', 'team', 'nametag', 'flags_c', 'flags_d', 'cpu_kind',
  'cpu_level', 'damage_10', 'damage_12', 'hp', 'attack_ratio_bits',
  'defense_ratio_bits', 'model_scale_bits',
];

function expectedSssPlayer(index) {
  return {ckind: 0, slot_type: 3, stocks: 4, color: index, slot: index,
    spawn: 0, spawn_direction: 1, sub_color: 0, handicap: 9, team: 0,
    nametag: 0, flags_c: 0, flags_d: 0, cpu_kind: 0, cpu_level: 0,
    damage_10: 0, damage_12: 0, hp: 0, attack_ratio_bits: '3f800000',
    defense_ratio_bits: '3f800000', model_scale_bits: '3f800000'};
}

function nativeSssPlayer(index) {
  return {...expectedSssPlayer(index), xB: 0};
}

function expectedSssNote(phase) {
  return {phase, scene_frame: 149, scene_kind: 9, random_seed_hex: '3bb84c53',
    pad_state_hex: '00'.repeat(822),
    scene_routing_getters: {current_game_mode: 2, previous_game_mode: 1,
      current_scene_index: 0, previous_scene_index: 3},
    sss: {header: {start_game: 1}, vs: {start: {
      rules: {...SSS_RULES}, players: Array.from({length: 4}, (_, i) => expectedSssPlayer(i)),
    }}}};
}

function sourceSssRow(expected, phase) {
  const slices = {
    '32:0': '00'.repeat(0xF0), '35:0': expected.sss.header.start_game.toString(16).padStart(2, '0'),
    '33:0': '00'.repeat(0x40), '34:0': '00000000',
    '36:0': '0000', '37:0': '0000', '21:0': '00'.repeat(0x358),
    '17:0': '020001000300', '30:0': '00000095', '40:0': '09',
    '19:0': '804d5f90', '20:0': expected.random_seed_hex,
  };
  const inventory = [
    {tag: 32, flags: 0, address: 0x80480678, size: 0xF0},
    {tag: 35, flags: 0, address: 0x8048066C, size: 1},
    {tag: 33, flags: 0, address: 0x803BB300, size: 0x40},
    {tag: 34, flags: 0, address: 0x804D6038, size: 4},
    {tag: 36, flags: 0, address: 0x8045BF28, size: 2},
    {tag: 37, flags: 0, address: 0x8045BF2A, size: 2},
    {tag: 21, flags: 0, address: 0x804C1F84, size: 0x358},
    {tag: 17, flags: 0, address: 0x80479D30, size: 6},
    {tag: 30, flags: 0, address: 0x80479D58, size: 4},
    {tag: 40, flags: 0, address: 0x803DD9C4, size: 1},
    {tag: 19, flags: 0, address: 0x804D5F94, size: 4},
    {tag: 20, flags: 0, address: 0x804D5F90, size: 4},
  ];
  return {sequence: phase === 'sss_entry' ? 1579 : 1603,
    source_tick: 149, draw_ordinal: 149,
    pc: phase === 'sss_entry' ? '0x8025a998' : '0x8025b84c',
    word: phase === 'sss_entry' ? '0x7c0802a6' : '0x4e800020',
    lr: '0x801a40e8', argument: phase === 'sss_entry' ? '0x80480668' : '0x00000000',
    setup_receipt_sha256: 'e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda',
    setup_profile_verified_by_observer: false,
    source_slice_inventory: inventory,
    source_slices_hex: slices, scene_routing_raw_hex: slices['17:0'],
    rng_pointer_hex: '804d5f90'};
}

function sssExpectedPair() {
  const expectedEntry = expectedSssNote('sss_entry');
  const expectedReturn = expectedSssNote('sss_return');
  return {schema: 'melee-web-stadium-first-sss-constructor-pair-diagnostic',
    version: 1, source_admission: false, whole_session_equivalent: false,
    provenance: {observer_bytes: 4397889,
      observer_sha256: '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778',
      observer_status_bytes: 515,
      observer_status_sha256: '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f',
      entry_sequence: 1579, entry_pc: '0x8025a998', entry_word: '0x7c0802a6',
      return_sequence: 1603, return_pc: '0x8025b84c', return_word: '0x4e800020',
      entry_lr: '0x801a40e8', return_lr: '0x801a40e8',
      source_tick: 149, draw_ordinal: 149, scene_frame: 149, source_scene_kind: 9},
    source_entry: {sequence: 1579, pc: '0x8025a998', word: '0x7c0802a6',
      ...sourceSssRow(expectedEntry, 'sss_entry')},
    source_return: {sequence: 1603, pc: '0x8025b84c', word: '0x4e800020',
      ...sourceSssRow(expectedReturn, 'sss_return')},
    expected_entry: expectedEntry, expected_return: expectedReturn};
}

function nativeSssNote(expected, phase) {
  const rules = {...expected.sss.vs.start.rules,
    x0_3: expected.sss.vs.start.rules.hud_layout,
    xB: expected.sss.vs.start.rules.item_frequency};
  delete rules.hud_layout;
  delete rules.item_frequency;
  for (const key of ['timer_enabled', 'timer_counts_up', 'friendly_fire', 'is_stock',
    'single_button', 'disable_pausing', 'is_vs'])
    rules[key] = expected.sss.vs.start.rules[key] ? 1 : 0;
  return {phase, host_entered: false, session_phase: 2, source_scene: 2,
    scene_kind: expected.scene_kind, scene_frame: expected.scene_frame,
    random_seed_hex: expected.random_seed_hex, pad_state_hex: expected.pad_state_hex,
    scene_routing_getters: {...expected.scene_routing_getters},
    owners: {host: true, session: true, world: true, audio: true, vs_mode: true,
      scene_info: true, payload: true, seed: true},
    world_generation: 7, audio_generation: 3, session_ticks: 149,
    sss: {header: {unk_stage: 0, x1: 0, no_lras: 0, force_stage_id: 0,
      start_game: expected.sss.header.start_game},
    vs: {loser: -1, ordered_stage_index: -1, winner: -1, start: {
      rules, players: [...expected.sss.vs.start.players.map(player => ({...player, xB: 0})),
        nativeSssPlayer(4), nativeSssPlayer(5)],
    }}}};
}

function actualSssPair(expected = sssExpectedPair()) {
  return {status: 'captured', armed: true, kicked: true, attempted: true,
    captured: true, compared: false, complete: false, failed: false, error: null,
    host_entered: true, session_phase: 3, world_generation: 7,
    audio_generation: 3, audio_owner_live: true,
    audio_render_calls_before_rebuild: 10, audio_render_frames_before_rebuild: 100,
    audio_render_calls_after_sss_enter: 11, audio_render_frames_after_sss_enter: 101,
    sss_host_tick_calls: 0, sss_host_draw_calls: 0,
    entry: nativeSssNote(expected.expected_entry, 'sss_entry'),
    returned: nativeSssNote(expected.expected_return, 'sss_return')};
}

test('original SSS constructor pair compares exact entry then return semantics once', () => {
  const expected = sssExpectedPair();
  const actual = actualSssPair(expected);
  const comparator = createFirstSssConstructorPairComparator(expected);
  assert.equal(requestSynchronousApproval(comparator.compare, 'sss_pair', JSON.stringify(actual)), true);
  const status = comparator.status();
  assert.deepEqual({attempted: status.attempted, approved: status.approved,
    failed: status.failed, first_mismatch: status.first_mismatch},
  {attempted: true, approved: true, failed: false, first_mismatch: null});
  assert.deepEqual(status.actual_pair, actual, 'actual source-side evidence is retained before approval');
  assert.equal(comparator.compare('sss_pair', JSON.stringify(actual)), false,
    'The native comparison callback is exactly once');
  assert.equal(comparator.status().failed, true);
});

test('SSS pair reports the first entry mismatch before later return mismatches', () => {
  const expected = sssExpectedPair();
  const actual = actualSssPair(expected);
  actual.entry.scene_frame = 148;
  actual.returned.random_seed_hex = '00000000';
  const comparator = createFirstSssConstructorPairComparator(expected);
  assert.equal(comparator.compare('sss_pair', JSON.stringify(actual)), false);
  assert.equal(comparator.status().first_mismatch.path, 'entry.scene_frame');
  assert.equal(comparator.status().actual_pair.returned.random_seed_hex, '00000000');
});

test('SSS pair maps only decoder rules and first four players with strict native bools', () => {
  const expected = sssExpectedPair();
  for (const [path, value] of [
    ['rules-bool', 'true'], ['hud', 3], ['player', 8], ['seed', '3bb84c52'],
  ]) {
    const actual = actualSssPair(expected);
    if (path === 'rules-bool') actual.entry.sss.vs.start.rules.is_stock = true;
    if (path === 'hud') actual.entry.sss.vs.start.rules.x0_3 = value;
    if (path === 'player') actual.entry.sss.vs.start.players[2].slot = value;
    if (path === 'seed') actual.returned.random_seed_hex = value;
    const comparator = createFirstSssConstructorPairComparator(expected);
    assert.equal(comparator.compare('sss_pair', JSON.stringify(actual)), false, path);
    assert.equal(comparator.status().failed, true, path);
  }
  const routeExtra = actualSssPair(expected);
  routeExtra.entry.scene_routing_getters.pending_mode = 1;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(routeExtra)), false);
});

test('SSS native player xB is byte-validated but remains an explicit unpaired field', () => {
  const expected = sssExpectedPair();
  const accepted = actualSssPair(expected);
  accepted.entry.sss.vs.start.players[0].xB = 255;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(accepted)), true);

  for (const mutation of [
    player => { delete player.xB; },
    player => { player.xB = -1; },
    player => { player.xB = 256; },
    player => { player.xB = '0'; },
    player => { player.unrecognized = 0; },
  ]) {
    const actual = actualSssPair(expected);
    mutation(actual.entry.sss.vs.start.players[0]);
    const comparator = createFirstSssConstructorPairComparator(expected);
    assert.equal(comparator.compare('sss_pair', JSON.stringify(actual)), false);
    assert.equal(comparator.status().failed, true);
  }

  const semantic = actualSssPair(expected);
  semantic.entry.sss.vs.start.players[0].slot += 1;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(semantic)), false,
  'allowing the named native byte does not relax source-backed player comparisons');
});

test('SSS pair requires retained ownership and zero host SSS ticks/draws', () => {
  const expected = sssExpectedPair();
  for (const [key, value] of [['sss_host_tick_calls', 1], ['sss_host_draw_calls', 1],
    ['audio_owner_live', false], ['world_generation', 8], ['status', 'failed']]) {
    const actual = actualSssPair(expected);
    actual[key] = value;
    const comparator = createFirstSssConstructorPairComparator(expected);
    assert.equal(comparator.compare('sss_pair', JSON.stringify(actual)), false, key);
    assert.equal(comparator.status().failed, true, key);
  }
  const owner = actualSssPair(expected);
  owner.returned.owners.seed = false;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(owner)), false);
  const phaseCross = actualSssPair(expected);
  phaseCross.returned.session_ticks += 1;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(phaseCross)), false);
});

test('SSS pair ignores unpaired header, pointer, padding and last-two-player fields', () => {
  const expected = sssExpectedPair();
  const actual = actualSssPair(expected);
  actual.entry.sss.header.unk_stage = 99;
  actual.entry.sss.vs.start.rules.on_match_start = 'unresolved_nonnull';
  actual.entry.sss.vs.start.players[4].slot = 9;
  actual.entry.sss.vs.start.players[5].slot = 8;
  assert.equal(createFirstSssConstructorPairComparator(expected)
    .compare('sss_pair', JSON.stringify(actual)), true);
});

test('SSS expected source hashes and argument identity cannot drift', () => {
  const expected = sssExpectedPair();
  for (const mutation of [
    value => { value.source_return.argument = '0x80480668'; },
    value => { value.source_return.setup_profile_verified_by_observer = true; },
    value => { value.provenance.return_sequence += 1; },
    value => { value.provenance.observer_sha256 = '00'.repeat(32); },
  ]) {
    const changed = structuredClone(expected);
    mutation(changed);
    assert.throws(() => createFirstSssConstructorPairComparator(changed), /exact retained source/);
  }
});

async function loadBrowserPostCaptureAssertion() {
  const source = await fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const declaration = source.match(/function assertFirstSssPostCaptureState\(evidence, baseline\) \{[\s\S]*?\n\}/);
  assert.ok(declaration, 'Browser harness postcapture assertion function is absent');
  return new Function('assert', `${declaration[0]}; return assertFirstSssPostCaptureState;`)(assert);
}

function postCaptureFixture() {
  const baseline = {
    entry: {scene_frame: 0, typed_rules: {timer_enabled: 1}},
    tick: {scene_frame: 1, random_seed_hex: '312151c3'},
    draw: {scene_frame: 1, draw_ordinal: 0},
    postdraw_stream: {next_index: 147, outcome: 'stop_after_last_input_request'},
    final_pending_css_draw: {sequence: 1577, compared: true, frame_end_returned: true},
  };
  const native = structuredClone(baseline);
  native.running = 0;
  native.first_sss_constructor_pair = {captured: true, host_entered: true,
    sss_host_tick_calls: 0, sss_host_draw_calls: 0,
    failed: true, error: 'entry rules differ'};
  const evidence = {native, host: {status: {first_mismatch: {path: 'entry.sss.vs.start.rules'}}}};
  return {evidence, baseline};
}

test('browser postcapture assertion checks stop and retained baselines before SSS outcome', async () => {
  const check = await loadBrowserPostCaptureAssertion();
  const {evidence, baseline} = postCaptureFixture();
  assert.doesNotThrow(() => check(evidence, baseline),
    'An expected semantic mismatch still passes the independent stop/baseline gate');
  assert.equal(evidence.native.first_sss_constructor_pair.failed, true,
    'The gate must preserve the native mismatch state');
  assert.deepEqual(evidence.host.status.first_mismatch,
    {path: 'entry.sss.vs.start.rules'}, 'The gate must preserve the entry-first mismatch');

  for (const [path, mutate, message] of [
    ['SSS tick', value => { value.native.first_sss_constructor_pair.sss_host_tick_calls = 1; },
      /SSS host tick ran/],
    ['SSS draw', value => { value.native.first_sss_constructor_pair.sss_host_draw_calls = 1; },
      /SSS host draw ran/],
    ['running', value => { value.native.running = 1; }, /Host time continued/],
  ]) {
    const changed = structuredClone(evidence);
    mutate(changed);
    assert.throws(() => check(changed, baseline), message, path);
  }

  for (const [key, message] of [
    ['entry', /typed CSS entry changed/], ['tick', /typed CSS tick changed/],
    ['draw', /typed CSS draw changed/], ['postdraw_stream', /postdraw stream changed/],
    ['final_pending_css_draw', /final CSS draw changed/],
  ]) {
    const changed = structuredClone(evidence);
    changed.native[key].mutated_after_capture = true;
    assert.throws(() => check(changed, baseline), message, key);
  }
});

function firstSssTickExpected() {
  const inventory = [
    [2, 0, 0x804C1F78, 0x0C], [17, 0, 0x80479D30, 6],
    [19, 0, 0x804D5F94, 4], [20, 0, 0x804D5F90, 4],
    [21, 0, 0x804C1F84, 0x358], [30, 0, 0x80479D58, 4],
    [36, 0, 0x8045BF28, 2], [37, 0, 0x8045BF2A, 2],
    [40, 0, 0x803DD9C4, 1],
  ].map(([tag, flags, address, size]) => ({tag, flags, address, size}));
  const raw = Object.fromEntries(inventory.map(({tag, flags, size}) =>
    [`${tag}:${flags}`, '00'.repeat(size)]));
  raw['17:0'] = '020201010000';
  raw['19:0'] = '804d5f90';
  raw['20:0'] = '3bb84c53';
  raw['30:0'] = '00000000';
  raw['40:0'] = '09';
  return {
    schema: 'melee-web-stadium-first-sss-consumed-pad-tick-diagnostic', version: 1,
    scope: 'one consumed SSS PAD and its scheduler-end SourceTick only',
    provenance: {observer_bytes: 4397889,
      observer_sha256: '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778',
      observer_status_bytes: 515,
      observer_status_sha256: '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f',
      consumed_pad_sequence: 1604, scheduler_end_sequence: 1608,
      scheduler_end_pc: '0x80390eb4', source_tick: 0, draw_ordinal: 149,
      original_source_frame: 0},
    source_scheduler_end: {source_slice_inventory: inventory, source_slices_hex: raw,
      scene_routing_raw_hex: '020201010000', rng_pointer_hex: '804d5f90',
      setup_profile_verified_by_observer: false,
      expected: {scene_frame: 0, scene_kind: 9, pad_state_hex: 'aa'.repeat(822),
        random_seed_hex: '3bb84c53',
        scene_routing_getters: {current_game_mode: 2, previous_game_mode: 1,
          current_scene_index: 1, previous_scene_index: 0},
        consumed_pad_status_hex: ['0000000000000000000000', '0000000000000000000000',
          '00000000000000000000ff', '00000000000000000000ff'],
        required_owners: {host: true, session: true, world: true, audio: true,
          vs_mode: true, scene_info: true, payload: true, seed: true}}},
    input_bundle: {magic_hex: '5354433153535331', version: 1, bytes: 96,
      sha256: '00'.repeat(32), contains_expected_state: false,
      port_status_hex: ['0000000000000000000000', '0000000000000000000000',
        '00000000000000000000ff', '00000000000000000000ff']},
    comparison_fields: ['scheduler_end.scene_frame', 'scheduler_end.scene_kind',
      'scheduler_end.pad_state_hex', 'scheduler_end.random_seed_hex',
      'scheduler_end.scene_routing_getters', 'consumed_pad_status_hex'],
    native_protocol_requirements: {scheduler_sample_scene_frame: 0,
      post_host_frame_after_clock_post: 1, clock_post_succeeded: true, tick_result: 1,
      transition_requested: false, host_tick_calls: 1, host_draw_calls: 0,
      all_owners_true: true},
    whole_session_equivalent: false, source_admission: false,
  };
}

function firstSssTickPair() {
  return {complete: true, compared: true, failed: false, host_entered: true,
    session_phase: 3, sss_host_tick_calls: 0, sss_host_draw_calls: 0,
    world_generation: 7, audio_generation: 3};
}

function firstSssTickActual(expected = firstSssTickExpected()) {
  const wanted = expected.source_scheduler_end.expected;
  return {phase: 'scheduler_end', source_scene: 2, scene_kind: wanted.scene_kind,
    scene_frame: wanted.scene_frame, random_seed_hex: wanted.random_seed_hex,
    pad_state_hex: wanted.pad_state_hex,
    scene_routing_getters: {...wanted.scene_routing_getters},
    owners: {...wanted.required_owners}, world_generation: 7, audio_generation: 3,
    consumed_pad_hex: wanted.consumed_pad_status_hex.join('')};
}

test('one SSS scheduler-end sample compares directly with original frame-zero row', () => {
  const expected = firstSssTickExpected();
  const actual = firstSssTickActual(expected);
  const comparator = createFirstSssConsumedPadTickComparator(expected, firstSssTickPair());
  assert.equal(requestSynchronousApproval(comparator.compare, 'scheduler_end',
    JSON.stringify(actual)), true);
  assert.deepEqual(comparator.status(), {attempted: true, approved: true, failed: false,
    first_mismatch: null, actual, error: null});
  assert.equal(comparator.compare('scheduler_end', JSON.stringify(actual)), false,
    'The scheduler-end sample is compared exactly once');
  assert.equal(comparator.status().failed, true);
});

test('SSS tick keeps frame-zero comparison distinct and reports the first actual mismatch', () => {
  const expected = firstSssTickExpected();
  for (const [field, value, path] of [
    ['scene_frame', 1, 'scheduler_end.scene_frame'],
    ['scene_kind', 8, 'scheduler_end.scene_kind'],
    ['pad_state_hex', 'bb'.repeat(822), 'scheduler_end.pad_state_hex'],
    ['random_seed_hex', '3bb84c54', 'scheduler_end.random_seed_hex'],
    ['consumed_pad_hex', '00'.repeat(44), 'scheduler_end.consumed_pad_hex'],
  ]) {
    const actual = firstSssTickActual(expected);
    actual[field] = value;
    const comparator = createFirstSssConsumedPadTickComparator(expected, firstSssTickPair());
    assert.equal(comparator.compare('scheduler_end', JSON.stringify(actual)), false, field);
    assert.equal(comparator.status().first_mismatch.path, path, field);
  }
  const route = firstSssTickActual(expected);
  route.scene_routing_getters.current_scene_index = 0;
  assert.equal(createFirstSssConsumedPadTickComparator(expected, firstSssTickPair())
    .compare('scheduler_end', JSON.stringify(route)), false);
});

test('SSS tick requires completed constructor owners and exact source expected identity', () => {
  const expected = firstSssTickExpected();
  for (const key of ['complete', 'compared', 'host_entered']) {
    const pair = firstSssTickPair();
    pair[key] = false;
    assert.throws(() => createFirstSssConsumedPadTickComparator(expected, pair),
      /approved completed constructor pair/);
  }
  const tickOwner = firstSssTickActual(expected);
  tickOwner.audio_generation += 1;
  assert.equal(createFirstSssConsumedPadTickComparator(expected, firstSssTickPair())
    .compare('scheduler_end', JSON.stringify(tickOwner)), false);
  const changed = structuredClone(expected);
  changed.provenance.scheduler_end_sequence += 1;
  assert.throws(() => createFirstSssConsumedPadTickComparator(changed, firstSssTickPair()),
    /exact retained row1604\/1608/);
});

async function loadBrowserFirstSssTickAssertions() {
  const source = await fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const names = ['assertFirstSssConsumedTickStopState',
    'retainFirstSssConsumedTickEvidence', 'assertFirstSssConsumedTickCompared'];
  const declarations = names.map(name => {
    const expression = new RegExp(`function ${name}\\([^\\n]*\\) \\{[\\s\\S]*?\\n\\}`);
    const match = source.match(expression);
    assert.ok(match, `Actual browser ${name} function is absent`);
    return match[0];
  });
  return new Function('assert', `${declarations.join('\n')}; return {${names.join(',')}};`)(assert);
}

test('browser harness persists a semantic tick mismatch before rejecting success', async () => {
  const {assertFirstSssConsumedTickStopState, retainFirstSssConsumedTickEvidence,
    assertFirstSssConsumedTickCompared} = await loadBrowserFirstSssTickAssertions();
  const pair = firstSssTickPair();
  const baseline = {entry: {scene_frame: 0}, tick: {scene_frame: 1},
    draw: {scene_frame: 1}, postdraw_stream: {next_index: 147},
    final_pending_css_draw: {sequence: 1577, compared: true}};
  const actual = firstSssTickActual();
  actual.scene_frame = 1;
  const mismatch = {path: 'scheduler_end.scene_frame', expected: 0, actual: 1};
  const evidence = {native: {running: 0, first_sss_constructor_pair: pair,
    first_sss_consumed_tick: {attempted: true, captured: true, failed: true,
      error: 'scheduler_end.scene_frame differs', host_tick_calls: 1, host_draw_calls: 0,
      post_host_frame_captured: true, post_host_frame: 1},
    entry: baseline.entry, tick: baseline.tick, draw: baseline.draw,
    postdraw_stream: baseline.postdraw_stream,
    final_pending_css_draw: baseline.final_pending_css_draw},
  host: {hook_error: null, status: {attempted: true, approved: false, failed: true,
    first_mismatch: mismatch, actual}, rows: [{phase: 'scheduler_end', actual, approved: false}]}};
  const scenario = {};
  const kick = {result: 0, message: 'strict SSS source comparison rejected the sample'};

  retainFirstSssConsumedTickEvidence(scenario, evidence, kick);
  assert.deepEqual(scenario.observation, evidence,
    'The full native sample and first mismatch are retained before success assertions');
  assert.deepEqual(scenario.kick_result, kick);
  assert.match(scenario.claim, /comparison and protocol gates are pending/);
  assert.doesNotThrow(() => assertFirstSssConsumedTickStopState(evidence, baseline, pair),
    'A semantic mismatch must still pass the independent stop and old-CSS invariance gate');
  assert.throws(() => assertFirstSssConsumedTickCompared(evidence, kick),
    /strict SSS source comparison rejected/,
    'Expected mismatch remains a failing browser report');
  assert.equal(scenario.observation.host.status.first_mismatch.path,
    'scheduler_end.scene_frame');
});

function vsEntryExpectedFixture() {
  const start = structuredClone(expectedSssNote('sss_entry').sss.vs.start);
  start.rules.is_stock = true; start.rules.is_vs = true;
  const inventory = [[4,0x80480530,0x138],[19,0x804d5f94,4],[20,0x804d5f90,4],
    [21,0x804c1f84,0x358],[36,0x8045bf28,2],[37,0x8045bf2a,2]]
    .map(([tag,address,size]) => ({tag, flags: 0, address, size}));
  return {schema: 'melee-web-stadium-first-sss-vs-entry-endpoint', version: 1,
    scope: 'original ordinary VS Entry row2468 StartMeleeData and RNG scalar only',
    source_admission: false, whole_session_equivalent: false,
    provenance: {observer_bytes: 4397889,
      observer_sha256: '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778',
      observer_status_bytes: 515,
      observer_status_sha256: '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f',
      sequence: 2468, pc: '0x8016e934', source_tick: 125, draw_ordinal: 0,
      setup_profile_verified_by_observer: false},
    expected: {start, random_seed_hex: '3bb84c53'},
    comparison_fields: ['random_seed_hex', 'start.rules', 'start.players[0:4]'],
    source_entry: {sequence: 2468, pc: 0x8016e934, event: 'boundary', boundary: 'entry',
      source_tick: 125, draw_ordinal: 0, start_slice_address: 0x80480530,
      setup_profile_verified_by_observer: false, random_seed_hex: '3bb84c53',
      rng_pointer_hex: '804d5f90', source_slice_inventory: inventory,
      source_slices_hex: Object.fromEntries(inventory.map(row => [`${row.tag}:0`,
        row.tag === 19 ? '804d5f90' : row.tag === 20 ? '3bb84c53' : '00'.repeat(row.size)]))}};
}
function vsEntryPrefixFixture() {
  return {complete: true, failed: false, current_host_entered: false,
    current_source_scene: 0, current_session_phase: 5,
    host_tick_calls: 124, host_draw_calls: 124, matched_ticks: 124,
    matched_draw_enters: 124, matched_draw_returns: 124,
    last_tick_result: 3, transition_requested: true,
    audio_render_calls_before: 150, audio_render_calls_after: 274,
    audio_render_frames_before: 80000, audio_render_frames_after: 146133,
    audio_phase_before: 0, audio_phase_after: 20};
}
function vsEntryActualFixture(expected = vsEntryExpectedFixture()) {
  const start = nativeSssNote({sss: {vs: {start: expected.expected.start}, header: {start_game: 1}}},
    'vs_entry').sss.vs.start;
  return {attempted: true, captured: true, complete: true, failed: false, error: null,
    getter_calls: 1, audio_render_calls_before: 274, audio_render_calls_after: 274,
    audio_render_frames_before: 146133, audio_render_frames_after: 146133,
    audio_phase_before: 20, audio_phase_after: 20,
    actual: {phase: 'vs_entry', start, random_seed_hex: '3bb84c53'}};
}

function vsSetupExpectedFixture() {
  const entry = vsEntryExpectedFixture();
  const inventory = [[4,0x80480530,0x138],[19,0x804d5f94,4],
    [20,0x804d5f90,4],[21,0x804c1f84,0x358]]
    .map(([tag,address,size]) => ({tag, flags: 0, address, size}));
  return {schema: 'melee-web-stadium-first-sss-vs-setup-endpoint', version: 1,
    scope: 'original ordinary VS Setup row2503 StartMeleeData and RNG scalar only',
    provenance: {observer_bytes: 4397889,
      observer_sha256: '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778',
      observer_status_bytes: 515,
      observer_status_sha256: '05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f',
      sequence: 2503, pc: '0x8016e9c4', source_tick: 125, draw_ordinal: 0,
      setup_profile_verified_by_observer: false},
    expected: {phase: 'vs_setup', start: entry.expected.start,
      random_seed_hex: '35a455b9'},
    comparison_fields: ['phase', 'random_seed_hex', 'start.rules', 'start.players[0:4]'],
    unpaired_fields: [
      'source tick/draw ordinal (provenance only; not native frame equality)',
      'source pointer/address', 'StartMeleeData ABI padding/unknown bytes',
      'native player slots4-5', 'native player xB',
      'original Setup-row PAD snapshot (provenance only)',
      'prepared_output_unpaired persistent VsModeData',
      'other Setup-row slices outside the selected typed Start and RNG'],
    source_setup: {sequence: 2503, pc: 0x8016e9c4, event: 'boundary', boundary: 'setup',
      source_tick: 125, draw_ordinal: 0, start_slice_address: 0x80480530,
      start_sha256: '918a23d68a3b5024f94baf1f5e26e6c0db56c5fbbbffdc79c5e620e82af3b728',
      setup_profile_verified_by_observer: false, random_seed_hex: '35a455b9',
      rng_pointer_hex: '804d5f90', source_slice_inventory: inventory,
      source_capture_inventory: inventory,
      source_slices_hex: Object.fromEntries(inventory.map(row => [`${row.tag}:0`,
        row.tag === 19 ? '804d5f90' : row.tag === 20 ? '35a455b9' : '00'.repeat(row.size)]))},
    source_admission: false, whole_session_equivalent: false};
}
function vsSetupEntryStatus() {
  return {attempted: true, approved: true, failed: false, first_mismatch: null};
}
function vsSetupActualFixture(expected = vsSetupExpectedFixture()) {
  const start = nativeSssNote({sss: {vs: {start: expected.expected.start}, header: {start_game: 1}}},
    'vs_setup').sss.vs.start;
  return {attempted: true, captured: true, complete: true, failed: false, error: null,
    construction_complete: true, source_frames: 0, host_tick_calls: 0, host_draw_calls: 0,
    actual: {phase: 'vs_setup', start, random_seed_hex: '35a455b9'}};
}

test('VS Entry compares the checked match Start mapper once, preserving unpaired native bytes', () => {
  const expected = vsEntryExpectedFixture(), actual = vsEntryActualFixture(expected);
  actual.actual.start.players[0].xB = 255;
  const comparator = createFirstSssVsEntryComparator(expected, vsEntryPrefixFixture());
  assert.equal(comparator.compare(actual), true);
  assert.equal(comparator.status().approved, true);
  assert.equal(comparator.compare(actual), false);
  assert.equal(comparator.status().first_mismatch.path, 'entry.callback');
});

test('VS Entry refuses missing/refused capture, seed, declared fields and unexpected audio work', () => {
  const expected = vsEntryExpectedFixture();
  for (const change of [a => {a.captured = false;}, a => {a.failed = true; a.error = 'owner refused';},
    a => {a.actual.random_seed_hex = '3bb84c54';},
    a => {a.actual.start.rules.is_vs = 0;}, a => {a.actual.start.players[0].stocks += 1;},
    a => {delete a.actual.start.players[0].flags_c;},
    a => {a.audio_render_calls_after += 1;}]) {
    const actual = vsEntryActualFixture(expected);change(actual);
    const comparator = createFirstSssVsEntryComparator(expected, vsEntryPrefixFixture());
    assert.equal(comparator.compare(actual), false);
    assert.equal(comparator.status().failed, true);
    assert.ok(comparator.status().first_mismatch);
    assert.equal(comparator.compare(vsEntryActualFixture(expected)), false);
  }
});

test('VS Entry refuses incomplete prefix and wrong original provenance', () => {
  const expected = vsEntryExpectedFixture(), prefix = vsEntryPrefixFixture();
  for (const key of ['complete', 'transition_requested']) {
    const changed = structuredClone(prefix);changed[key] = false;
    assert.throws(() => createFirstSssVsEntryComparator(expected, changed), /completed checked SSS prefix/);
  }
  for (const change of [e => {e.provenance.sequence = 2503;},
    e => {e.expected.random_seed_hex = '35a455b9';},
    e => {e.source_entry.source_slices_hex['19:0'] = '804d5f94';}]) {
    const changed = structuredClone(expected);change(changed);
    assert.throws(() => createFirstSssVsEntryComparator(changed, prefix), /retained row2468/);
  }
});

test('VS Entry actual runner invariant catches extra source work outside its sibling', async () => {
  const source = await fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const declaration = source.match(/function assertFirstSssVsEntryUnchanged\([^\n]*\) \{[\s\S]*?\n\}/);
  assert.ok(declaration);
  const check = new Function('assert', `${declaration[0]}; return assertFirstSssVsEntryUnchanged;`)(assert);
  const before = {running: 0, native_phase: 0, first_sss_prefix: {host_tick_calls: 124},
    first_sss_consumed_tick: {post_host_frame: 1}, entry: {random_seed_hex: 'abcddcba'}};
  const after = {...structuredClone(before), first_sss_vs_entry: vsEntryActualFixture()};
  check(after, before);
  for (const mutate of [a => {a.running = 1;}, a => {a.first_sss_prefix.host_tick_calls++;},
    a => {a.first_sss_consumed_tick.post_host_frame++;}, a => {a.unexpected_source_work = true;}]) {
    const changed = structuredClone(after);mutate(changed);assert.throws(() => check(changed, before));
  }
});


test('VS Entry actual loader refuses a different diagnostic export and source binding', async () => {
  const source = await fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const declaration = source.match(/async function loadFirstSssVsEntryInputs\(\) \{[\s\S]*?\n\}/);
  assert.ok(declaration);
  const expected = vsEntryExpectedFixture();
  const helperPath = new URL('./stadium_first_css_stream_compare.mjs', import.meta.url).pathname;
  const {pathToFileURL} = await import('node:url');
  const run = async (binding, prefixExpected = expected) => {
    const loader = new Function('assert', 'firstSssVsEntry', 'loadBoundFirstSssExpected',
      'pathToFileURL', 'preflight', 'firstSssPrefixInputs',
      `${declaration[0]}; return loadFirstSssVsEntryInputs;`)(assert, true,
        async () => ({expected, helperPath}), pathToFileURL,
        {first_sss_vs_entry: binding}, {expected: prefixExpected});
    return loader();
  };
  const binding = {diagnostic_export: '_melee_web_native_menu_stadium_first_sss_vs_entry_kick',
    expected_state_host_only: true};
  assert.equal((await run(binding)).expected, expected);
  await assert.rejects(run({...binding, diagnostic_export: '_wrong_export'}), /diagnostic export/);
  await assert.rejects(run({...binding, expected_state_host_only: false}), /host-only/);
  const differentSource = structuredClone(expected); differentSource.provenance.observer_bytes++;
  await assert.rejects(run(binding, differentSource), /source provenance/);
});


test('VS Entry uses the actual prefix reader live phase/running shape before invariant checking', async () => {
  const source = await fs.readFile(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
  const definitions = ['readFirstSssPrefixObservation', 'readFirstSssVsEntryObservation',
    'assertFirstSssVsEntryUnchanged'].map(name => {
      const match = source.match(new RegExp(`(?:async )?function ${name}\\([^\\n]*\\) \\{[\\s\\S]*?\\n\\}`));
      assert.ok(match, name); return match[0];
    }).join('\n');
  let raw = {first_sss_prefix: {host_tick_calls: 124}};
  const originalModule = globalThis.Module;
  globalThis.Module = {_melee_web_native_menu_stadium_first_css_draw_observe: () => 1,
    UTF8ToString: () => JSON.stringify(raw), _melee_web_native_menu_phase: () => 5,
    _melee_web_native_menu_running: () => 0};
  try {
    const {prefix, entry, check} = new Function('assert', 'page',
      `const firstSssVsSetupCfExplicitStep = false;
       let matchedSetupCfExplicitAsyncifyUnsafe = false;
       ${definitions}; return {
      prefix:readFirstSssPrefixObservation,entry:readFirstSssVsEntryObservation,
      check:assertFirstSssVsEntryUnchanged};`)(assert, {evaluate: async fn => fn()});
    const before = (await prefix()).native;
    raw.first_sss_vs_entry = vsEntryActualFixture();
    const after = await entry();
    assert.equal(after.native_phase, 5); assert.equal(after.running, 0);
    check(after, before);
    raw.first_sss_prefix.host_tick_calls++;
    assert.throws(() => check({...after, first_sss_prefix: raw.first_sss_prefix}, before));
  } finally { globalThis.Module = originalModule; }
});


test('VS Entry actual CLI recognizes the new route and rejects missing/orphan/conflicting expected input', () => {
  const runner = path.join(candidateRoot, 'tests/stadium_c1a_browser_test.mjs');
  const args = ['--first-sss-vs-entry', '--first-sss-vs-entry-expected', 'entry.json',
    '--first-sss-pair-expected', 'pair.json', '--first-sss-tick-expected', 'tick.json',
    '--first-sss-tick-input', 'tick.mwst', '--first-sss-draw-expected', 'draw.json',
    '--first-sss-prefix-expected', 'prefix.json', '--first-sss-prefix-input', 'prefix.mwst'];
  const run = values => spawnSync(process.execPath, [runner, ...values],
    {encoding:'utf8', timeout:10000});
  const accepted = run(args);
  assert.notEqual(accepted.status, 0); assert.match(accepted.stderr, /Missing --build/);
  for (const values of [args.filter(value => !['--first-sss-vs-entry-expected', 'entry.json'].includes(value)),
    [...args, '--first-sss-prefix'], ['--first-sss-vs-entry-expected', 'entry.json']]) {
    const refused = run(values); assert.notEqual(refused.status, 0);
    assert.match(refused.stderr, /Choose one private C1a diagnostic route/);
  }
});

test('VS Setup compares completed deferred Session Start and seed after approved Entry', () => {
  const expected = vsSetupExpectedFixture(), actual = vsSetupActualFixture(expected);
  actual.actual.start.players[0].xB = 255;
  actual.actual.start.players[4] = {ignored_native_player_slot: true};
  const comparator = createFirstSssVsSetupComparator(
    expected, vsEntryPrefixFixture(), vsSetupEntryStatus());
  assert.equal(comparator.compare(actual), true);
  assert.equal(comparator.status().approved, true);
  assert.equal(comparator.compare(actual), false);
  assert.equal(comparator.status().first_mismatch.path, 'setup.callback');
});

test('VS Setup refuses construction, frame, continuation work and declared Start mismatches', () => {
  const expected = vsSetupExpectedFixture();
  for (const change of [
    a => {a.captured = false;},
    a => {a.failed = true; a.error = 'deferred owner refused';},
    a => {a.construction_complete = false;},
    a => {a.source_frames = 1;},
    a => {a.host_tick_calls = 1;},
    a => {a.host_draw_calls = 1;},
    a => {a.actual.phase = 'vs_entry';},
    a => {a.actual.random_seed_hex = '35a455ba';},
    a => {a.actual.start.rules.is_vs = 0;},
    a => {a.actual.start.players[0].stocks += 1;},
    a => {delete a.actual.start.players[0].flags_c;},
  ]) {
    const actual = vsSetupActualFixture(expected); change(actual);
    const comparator = createFirstSssVsSetupComparator(
      expected, vsEntryPrefixFixture(), vsSetupEntryStatus());
    assert.equal(comparator.compare(actual), false);
    assert.equal(comparator.status().failed, true);
    assert.ok(comparator.status().first_mismatch);
  }
});

test('VS Setup requires strict prefix, same-run Entry approval and exact source identity', () => {
  const expected = vsSetupExpectedFixture(), prefix = vsEntryPrefixFixture();
  for (const key of ['complete', 'transition_requested']) {
    const changed = structuredClone(prefix); changed[key] = false;
    assert.throws(() => createFirstSssVsSetupComparator(
      expected, changed, vsSetupEntryStatus()), /completed checked SSS prefix/);
  }
  for (const entry of [
    {attempted: false, approved: true, failed: false, first_mismatch: null},
    {attempted: true, approved: false, failed: true, first_mismatch: {path: 'entry'}},
  ]) assert.throws(() => createFirstSssVsSetupComparator(
    expected, prefix, entry), /approved same-run VS Entry/);
  for (const change of [
    e => {e.provenance.sequence = 2502;},
    e => {e.provenance.pc = '0x8016e934';},
    e => {e.expected.phase = 'vs_entry';},
    e => {e.expected.random_seed_hex = '3bb84c53';},
    e => {e.source_setup.source_slices_hex['19:0'] = '804d5f94';},
    e => {e.source_setup.setup_profile_verified_by_observer = true;},
  ]) {
    const changed = structuredClone(expected); change(changed);
    assert.throws(() => createFirstSssVsSetupComparator(
      changed, prefix, vsSetupEntryStatus()), /exact retained row2503/);
  }
});


test('fresh finite-CSS profile compares only its original SSS entry/return pair', () => {
  const actual = actualSssPair(freshSssExpectedPair);
  actual.entry.session_ticks = 142;
  actual.returned.session_ticks = 142;
  const comparator = createFirstSssConstructorPairComparator(freshSssExpectedPair);
  assert.equal(requestSynchronousApproval(comparator.compare, 'sss_pair',
    JSON.stringify(actual)), true);
  assert.equal(comparator.status().approved, true);
  assert.equal(comparator.status().failed, false);
  assert.deepEqual(comparator.status().actual_pair, actual);
});

test('fresh profile rejects source identity, raw slice and projected-field mutations', () => {
  const mutations = [
    value => { value.profile = 'legacy_tick_draw_v1'; },
    value => { value.provenance.observer_sha256 = '361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778'; },
    value => { value.provenance.entry_sequence += 1; },
    value => { value.source_entry.source_tick -= 1; },
    value => { value.source_entry.source_slice_inventory[0].size -= 1; },
    value => { value.source_entry.source_slices_hex['21:0'] = '01' + value.source_entry.source_slices_hex['21:0'].slice(2); },
    value => { value.source_return.source_slices_hex['32:0'] = '00' + value.source_return.source_slices_hex['32:0'].slice(2); },
    value => { value.expected_entry.scene_frame += 1; },
    value => { value.expected_return.sss.vs.start.rules.is_stock = true; },
    value => { value.expected_entry.sss.vs.start.players[0].attack_ratio_bits = '00000000'; },
  ];
  for (const mutate of mutations) {
    const expected = structuredClone(freshSssExpectedPair);
    mutate(expected);
    assert.throws(() => createFirstSssConstructorPairComparator(expected), TypeError);
  }
});

test('fresh expected projection refuses legacy-frame and altered actual constructor values', () => {
  const mutations = [
    pair => { pair.entry.scene_frame = 149; },
    pair => { pair.returned.random_seed_hex = 'e2624ac2'; },
    pair => { pair.entry.sss.vs.start.rules.is_stock = 1; },
    pair => { pair.returned.sss.vs.start.players[0].attack_ratio_bits = '00000000'; },
    pair => { pair.sss_host_tick_calls = 1; },
    pair => { pair.sss_host_draw_calls = 1; },
  ];
  for (const mutate of mutations) {
    const actual = actualSssPair(freshSssExpectedPair);
    actual.entry.session_ticks = actual.returned.session_ticks = 142;
    mutate(actual);
    const comparator = createFirstSssConstructorPairComparator(freshSssExpectedPair);
    assert.equal(requestSynchronousApproval(comparator.compare, 'sss_pair',
      JSON.stringify(actual)), false);
    assert.equal(comparator.status().failed, true);
    assert.ok(comparator.status().first_mismatch);
  }
});
