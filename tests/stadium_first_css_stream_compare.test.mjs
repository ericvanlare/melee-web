import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import test from 'node:test';
import {createFirstCssStreamComparator, createFirstCssFinalDrawComparator,
  createFirstSssConstructorPairComparator, createFirstSssConsumedPadTickComparator,
  requestSynchronousApproval}
  from './stadium_first_css_stream_compare.mjs';

const COUNT = 148;
const route = Object.freeze({current_game_mode: 2, previous_game_mode: 1,
  current_scene_index: 0, previous_scene_index: 0});

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
      rules, players: [...expected.sss.vs.start.players.map(player => ({...player})),
        expectedSssPlayer(4), expectedSssPlayer(5)],
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
