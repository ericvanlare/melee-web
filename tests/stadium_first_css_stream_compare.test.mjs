import assert from 'node:assert/strict';
import test from 'node:test';
import {createFirstCssStreamComparator, createFirstCssFinalDrawComparator, requestSynchronousApproval}
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
