import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {classifyStockObservation, matchPairedActorMotion, retainPreStartActionPair,
  validateButtonGameplayInputWindow} from '../scripts/a3_local_room_full_route.mjs';

const firstUseFixture = JSON.parse(await readFile(
  new URL('./fixtures/a3_first_use_render_wait_observation.json', import.meta.url), 'utf8'));

function padRow(inputTick, localPort, buttons = 0, tail = []) {
  const bytes = [(buttons >>> 8) & 0xff, buttons & 0xff, ...tail];
  while (bytes.length < 11) bytes.push(0);
  return {input_tick: inputTick, source_cursor: inputTick, local_port: localPort, bytes};
}

test('bounded action accepts only the existing healthy first-use render wait', () => {
  assert.equal(classifyStockObservation(firstUseFixture.beta, 'captured first-use wait').wait,
    'first-use-rendering');
  const brokenOwner = structuredClone(firstUseFixture.beta);
  brokenOwner.native_status.local_capture_failure = 'capture failed';
  assert.throws(() => classifyStockObservation(brokenOwner, 'failed first-use owner'));
});

test('paired action motion requires the same exact source motion ID', () => {
  assert.equal(matchPairedActorMotion([25, 25], [25, 26]), 25);
  assert.equal(matchPairedActorMotion([26, 26], [25, 26]), 26);
  assert.equal(matchPairedActorMotion([25, 26], [25, 26]), null);
});

test('P1 grounded-jab window keeps literal A press/release and neutral P2 rows', () => {
  const result = validateButtonGameplayInputWindow({
    actionRows: [padRow(100, 0, 0x0100), padRow(101, 0, 0x0100), padRow(102, 0)],
    otherPortRows: [padRow(100, 1), padRow(101, 1), padRow(102, 1)],
    pressTick: 100, releaseTick: 102, port: 0, button: 0x0100,
  });
  assert.equal(result.action_sample_count, 2);
  assert.equal(result.other_port_neutral_count, 3);
});

test('P2 jump window keeps literal X press/release and neutral P1 rows', () => {
  const result = validateButtonGameplayInputWindow({
    actionRows: [padRow(220, 1, 0x0400), padRow(221, 1, 0)],
    otherPortRows: [padRow(220, 0), padRow(221, 0)],
    pressTick: 220, releaseTick: 221, port: 1, button: 0x0400,
  });
  assert.equal(result.action_sample_count, 1);
  assert.equal(result.local_port, 1);
});

test('button-window controls reject extra input, wrong ports, gaps and missing release', () => {
  const validAction = [padRow(10, 0, 0x0100), padRow(11, 0)];
  const validOther = [padRow(10, 1), padRow(11, 1)];
  assert.throws(() => validateButtonGameplayInputWindow({
    actionRows: [padRow(10, 0, 0x0100, [1]), validAction[1]],
    otherPortRows: validOther, pressTick: 10, releaseTick: 11, port: 0, button: 0x0100,
  }), /unexpected axis/);
  assert.throws(() => validateButtonGameplayInputWindow({
    actionRows: validAction, otherPortRows: [padRow(10, 0), padRow(11, 0)],
    pressTick: 10, releaseTick: 11, port: 0, button: 0x0100,
  }), /local input port changed/);
  assert.throws(() => validateButtonGameplayInputWindow({
    actionRows: [validAction[0], padRow(10, 0)],
    otherPortRows: validOther, pressTick: 10, releaseTick: 11, port: 0, button: 0x0100,
  }), /input ticks are not in source order/);
  assert.throws(() => validateButtonGameplayInputWindow({
    actionRows: [padRow(10, 0), padRow(11, 0)],
    otherPortRows: validOther, pressTick: 10, releaseTick: 11, port: 0, button: 0x0100,
  }), /contains no literal held sample/);
});



test('pre-start buffer retains same-cursor render WAIT and active versions, ignoring counter-only polls', () => {
  const buffer = [];
  // Synthetic pair for buffer policy: only beta is a full captured observer row.
  // This does not invent a matching peer observation as browser evidence.
  const waiting = [structuredClone(firstUseFixture.beta), structuredClone(firstUseFixture.beta)];
  waiting[0].running = 1;
  waiting[0].message = 'synthetic active buffer fixture';
  const cursor = waiting[0].source_tick;
  assert.equal(waiting[1].source_tick, cursor);
  assert.equal(retainPreStartActionPair(buffer, waiting), true);
  const countersOnly = structuredClone(waiting);
  countersOnly.forEach(row => {
    // Synthetic ignored metadata: the retained fixture need not expose these counters.
    row.native_status = {wait_callbacks: 100, network_wait: {callbacks: 100}};
    row.health = {poll_count: 999};
  });
  assert.equal(retainPreStartActionPair(buffer, countersOnly), false);
  assert.equal(buffer.length, 1);
  const active = structuredClone(waiting);
  active[1].running = 1;
  active[1].message = active[0].message;
  assert.equal(retainPreStartActionPair(buffer, active), true);
  assert.equal(buffer.length, 2);
  assert.equal(buffer[0].rows[1].running, 0);
  assert.equal(buffer[1].rows[1].running, 1);
  assert.equal(buffer[0].rows[0].source_tick, buffer[1].rows[0].source_tick);
  const distinctMotion = structuredClone(active);
  distinctMotion.forEach(row => { row.match.players[0].motion = 44; });
  assert.equal(retainPreStartActionPair(buffer, distinctMotion), true);
  assert.equal(buffer.length, 3);
});

test('pre-start buffer stays bounded and refuses an unpaired cursor', () => {
  const buffer = [];
  for (let cursor = 0; cursor < 130; ++cursor) {
    const rows = [structuredClone(firstUseFixture.alpha), structuredClone(firstUseFixture.beta)];
    rows.forEach(row => { row.source_tick = cursor; });
    retainPreStartActionPair(buffer, rows);
  }
  assert.equal(buffer.length, 128);
  assert.equal(buffer[0].rows[0].source_tick, 2);
  assert.equal(buffer.at(-1).rows[0].source_tick, 129);
  const bad = structuredClone(buffer.at(-1).rows);
  bad[1].source_tick++;
  assert.throws(() => retainPreStartActionPair(buffer, bad), /common-source rows/);
  assert.equal(buffer.length, 128);
});
