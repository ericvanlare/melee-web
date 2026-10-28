import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import test from 'node:test';
import {NET_FRAME_BYTES, PAD_BYTES} from '../scripts/net_lockstep_core.mjs';
import {MatchInputTimeline, ROLLBACK_MAX_PREDICTION_TICKS} from '../scripts/net_rollback_input_timeline.mjs';

const pad = value => new Uint8Array([0, value & 0xff, (value ^ 0x5a) & 0xff,
  0, 0, 0, 0, 0, 0, 0, value & 1 ? 0x80 : 0]);

function timeline(options = {}) {
  return new MatchInputTimeline({localPort: 0, remotePort: 1, initialRemotePad: pad(0), ...options});
}

function slot(frame, port) {
  return frame.slice(port * PAD_BYTES, (port + 1) * PAD_BYTES);
}

function commitForward(timeline, stateByTick, frameByTick, plan) {
  assert.equal(plan.kind, 'forward');
  const previousState = plan.tick === 0 ? 0x12345678 : stateByTick.get(plan.tick - 1);
  assert.notEqual(previousState, undefined);
  const nextState = toyStep(previousState, plan.tick, slot(plan.frame, 0), slot(plan.frame, 1));
  stateByTick.set(plan.tick, nextState);
  frameByTick.set(plan.tick, plan.frame);
  timeline.commitForward(plan);
}

function toyStep(state, tick, leftPad, rightPad) {
  return (Math.imul(state ^ tick, 0x01000193) ^ Math.imul(leftPad[1] + 1, 31) ^
    Math.imul(rightPad[2] + 1, 37)) >>> 0;
}

function commitReplay(timeline, stateByTick, frameByTick, plan) {
  assert.equal(plan.kind, 'replay');
  let state = plan.from === 0 ? 0x12345678 : stateByTick.get(plan.from - 1);
  assert.notEqual(state, undefined);
  for (const row of plan.frames) {
    state = toyStep(state, row.tick, slot(row.frame, 0), slot(row.frame, 1));
    stateByTick.set(row.tick, state);
    frameByTick.set(row.tick, row.frame);
  }
  timeline.commitReplay(plan);
}

function applyRemote(timeline, stateByTick, frameByTick, tick, value) {
  const result = timeline.acceptRemote(tick, value);
  if (timeline.status().dirty_from !== null)
    commitReplay(timeline, stateByTick, frameByTick, timeline.planReplay());
  return result;
}

test('selected ports preserve exact PAD bytes and inactive ports retain no-controller semantics', () => {
  assert.throws(() => new MatchInputTimeline({localPort: 0, remotePort: 1}), /Agreed initial remote PAD/);
  const local = pad(0x35), remote = new Uint8Array([0, 0x91, 0, 0, 0, 0, 0, 0, 0, 0, 0xff]);
  const value = timeline({localPort: 2, remotePort: 0, initialRemotePad: pad(0x11)});
  value.recordLocal(0, local);
  value.acceptRemote(0, remote);
  const plan = value.planForward();
  assert.equal(plan.frame.length, NET_FRAME_BYTES);
  assert.deepEqual(slot(plan.frame, 2), [...local]);
  assert.deepEqual(slot(plan.frame, 0), [...remote]);
  assert.deepEqual(slot(plan.frame, 1), [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff]);
  assert.deepEqual(slot(plan.frame, 3), [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff]);
  assert.equal(value.status().simulated_through, -1, 'planning alone does not commit a tick');
  assert.throws(() => { plan.frame[0] = 99; }, TypeError);
  value.commitForward(plan);
  assert.equal(value.status().confirmed_through, 0);
});

test('prediction stops at seven ticks even when a future actual sample arrives out of order', () => {
  const value = timeline({maxHistoryTicks: 8});
  for (let tick = 0; tick < ROLLBACK_MAX_PREDICTION_TICKS; ++tick) {
    value.recordLocal(tick, pad(tick + 1));
    const plan = value.planForward();
    assert.equal(plan.kind, 'forward');
    assert.equal(plan.speculative_ticks, tick + 1);
    value.commitForward(plan);
  }
  value.recordLocal(7, pad(8));
  assert.equal(value.planForward().reason, 'prediction_limit');
  value.acceptRemote(7, pad(99));
  assert.equal(value.planForward().reason, 'prediction_limit',
    'an actual future sample cannot move the contiguous confirmation edge');
  value.acceptRemote(0, pad(0));
  assert.equal(value.status().remote_contiguous_through, 0);
  const resumed = value.planForward();
  assert.equal(resumed.tick, 7);
  assert.equal(resumed.remote_source, 'actual');
  value.commitForward(resumed);
  assert.equal(value.status().simulated_through, 7);
  assert.equal(value.status().confirmed_through, 0);
});

test('a changed remote sample dirties its complete predicted descendant suffix and preserves local samples', () => {
  const value = timeline({maxHistoryTicks: 8});
  const localSamples = Array.from({length: 5}, (_, tick) => pad(30 + tick));
  const states = new Map(), frames = new Map();
  for (let tick = 0; tick < localSamples.length; ++tick) {
    value.recordLocal(tick, localSamples[tick]);
    commitForward(value, states, frames, value.planForward());
  }
  assert.equal(value.status().confirmed_through, -1);
  const accepted = value.acceptRemote(2, pad(200));
  assert.equal(accepted.earliest_changed_tick, 2);
  assert.equal(value.status().dirty_from, 2);
  assert.equal(value.status().confirmed_through, -1,
    'the earlier missing remote prefix prevents confirming even the clean pre-dirty ticks');
  assert.throws(() => value.planForward(), /replay must be acknowledged/);
  const replay = value.planReplay();
  assert.equal(replay.from, 2);
  assert.equal(replay.through, 4);
  assert.deepEqual(replay.frames.map(row => row.tick), [2, 3, 4]);
  for (const row of replay.frames) {
    assert.deepEqual(row.local_pad, [...localSamples[row.tick]]);
    assert.deepEqual(slot(row.frame, 0), [...localSamples[row.tick]]);
    assert.deepEqual(slot(row.frame, 1), [...pad(200)]);
  }
  commitReplay(value, states, frames, replay);
  assert.equal(value.status().dirty_from, null);
  assert.equal(value.status().confirmed_through, -1,
    'replay does not make samples authoritative or confirm a gap');
  value.acceptRemote(0, pad(0));
  value.acceptRemote(1, pad(0));
  assert.equal(value.status().remote_contiguous_through, 2);
  assert.equal(value.status().confirmed_through, 2);
});

test('remote arrivals invalidate outstanding plans; invalid data is atomic', () => {
  const value = timeline();
  value.recordLocal(0, pad(4));
  const pending = value.planForward();
  assert.equal(value.remoteInputs, undefined, 'mutable input maps are private');
  assert.equal(value.simulatedThrough, undefined, 'simulation cursor is exposed only through a snapshot');
  assert.throws(() => value.recordLocal(1, pad(5)), /plan is outstanding/);
  const before = value.status();
  assert.throws(() => value.acceptRemote(0, new Uint8Array(PAD_BYTES - 1)), /11 bytes/);
  assert.equal(value.status().plan_outstanding, true);
  assert.equal(value.status().remote_input_count, before.remote_input_count);
  value.acceptRemote(0, pad(7));
  assert.equal(value.status().plan_outstanding, false);
  assert.throws(() => value.commitForward(pending), /stale/);
  const fresh = value.planForward();
  assert.equal(fresh.remote_source, 'actual');
  value.commitForward(fresh);
  assert.equal(value.status().confirmed_through, 0);

  value.acceptRemote(1, pad(8));
  value.recordLocal(1, pad(5));
  const second = value.planForward();
  assert.equal(second.tick, 1);
  assert.throws(() => value.acceptRemote(1, pad(9)), /Conflicting remote PAD/);
  assert.equal(value.status().plan_outstanding, true, 'conflict leaves the plan and sample store untouched');
  value.commitForward(second);
});

test('replay acknowledgment is versioned and a new arrival makes its plan stale', () => {
  const value = timeline();
  const states = new Map(), frames = new Map();
  for (let tick = 0; tick < 4; ++tick) {
    value.recordLocal(tick, pad(tick + 10));
    commitForward(value, states, frames, value.planForward());
  }
  value.acceptRemote(2, pad(55));
  const stale = value.planReplay();
  value.acceptRemote(0, pad(0));
  assert.equal(value.status().plan_outstanding, false);
  assert.throws(() => value.commitReplay(stale), /stale/);
  const current = value.planReplay();
  assert.equal(current.from, 2);
  commitReplay(value, states, frames, current);
  assert.equal(value.status().dirty_from, null);
});

test('duplicates, conflicts, stale packets, and out-of-order packets have bounded effects', () => {
  const value = timeline({maxHistoryTicks: 8, maxLookaheadTicks: 4});
  assert.equal(value.acceptRemote(3, pad(3)).status, 'accepted');
  assert.equal(value.status().remote_contiguous_through, -1);
  assert.equal(value.acceptRemote(3, pad(3)).status, 'duplicate');
  assert.throws(() => value.acceptRemote(3, pad(4)), /Conflicting/);
  assert.throws(() => value.acceptRemote(4, pad(4)), /lookahead/);
  assert.throws(() => value.acceptRemote(Number.MAX_SAFE_INTEGER + 1, pad(0)), /safe integer/);
  assert.throws(() => value.acceptRemote(-1, pad(0)), /safe integer/);
  for (let tick = 0; tick < 3; ++tick) value.acceptRemote(tick, pad(tick));
  assert.equal(value.status().remote_contiguous_through, 3);
  for (let tick = 0; tick < 12; ++tick) {
    value.recordLocal(tick, pad(tick + 20));
    value.acceptRemote(tick, pad(tick));
    const plan = value.planForward();
    assert.equal(plan.kind, 'forward');
    value.commitForward(plan);
  }
  assert.ok(value.status().local_input_count <= 8);
  assert.ok(value.status().executed_input_count <= 8);
  assert.ok(value.status().remote_input_count <= 8 + 4);
  assert.equal(value.acceptRemote(0, pad(88)).status, 'stale');
  assert.equal(value.status().remote_contiguous_through, 11);
});

test('Buffer aliases and caller-owned byte arrays cannot mutate retained or planned samples', () => {
  const value = timeline();
  const localBuffer = Buffer.from(pad(21));
  const localView = localBuffer.subarray(0, PAD_BYTES);
  const remoteBuffer = Buffer.from(pad(34));
  value.recordLocal(0, localView);
  value.acceptRemote(0, remoteBuffer);
  localBuffer.fill(99);
  remoteBuffer.fill(88);
  const plan = value.planForward();
  assert.deepEqual(slot(plan.frame, 0), [...pad(21)]);
  assert.deepEqual(slot(plan.frame, 1), [...pad(34)]);
  assert.ok(Object.isFrozen(plan.frame));
  value.commitForward(plan);
  const sparse = Array(PAD_BYTES);
  sparse[0] = 1;
  assert.throws(() => value.recordLocal(1, sparse), /unsigned bytes/);
});

test('disconnect is terminal and dispose releases all retained history', () => {
  const value = timeline();
  value.recordLocal(0, pad(1));
  const pending = value.planForward();
  value.disconnect('peer closed');
  assert.equal(value.status().terminal.kind, 'disconnect');
  assert.equal(value.status().plan_outstanding, false);
  assert.throws(() => value.commitForward(pending), /terminal/);
  assert.throws(() => value.acceptRemote(0, pad(0)), /terminal/);
  const disposed = value.dispose();
  assert.equal(disposed.disposed, true);
  assert.equal(disposed.local_input_count, 0);
  assert.equal(disposed.remote_input_count, 0);
  assert.equal(disposed.executed_input_count, 0);
  assert.equal(value.dispose().disposed, true);
  assert.throws(() => value.planForward(), /disposed/);
});

test('the maximum safe tick is usable once without unsafe cursor arithmetic', () => {
  const tick = Number.MAX_SAFE_INTEGER;
  const value = timeline({startTick: tick, maxHistoryTicks: 8});
  value.recordLocal(tick, pad(7));
  const forward = value.planForward();
  assert.equal(forward.tick, tick);
  value.commitForward(forward);
  assert.equal(value.planForward().reason, 'safe_tick_limit');
  assert.equal(value.acceptRemote(tick, pad(9)).earliest_changed_tick, tick);
  const replay = value.planReplay();
  assert.equal(replay.from, tick);
  assert.equal(replay.through, tick);
  assert.deepEqual(replay.frames.map(row => row.tick), [tick]);
  value.commitReplay(replay);
  assert.equal(value.status().confirmed_through, tick);
});

test('paired timelines converge with delayed, crossing, out-of-order remote samples and a toy-state oracle', () => {
  const count = 18;
  const agreedInitialRemotePad = pad(77);
  const localA = Array.from({length: count}, (_, tick) => pad(20 + tick * 3));
  const localB = Array.from({length: count}, (_, tick) => pad(120 + tick * 5));
  const alpha = timeline({localPort: 0, remotePort: 1, initialRemotePad: agreedInitialRemotePad,
    maxHistoryTicks: 32});
  const beta = timeline({localPort: 1, remotePort: 0, initialRemotePad: agreedInitialRemotePad,
    maxHistoryTicks: 32});
  const alphaStates = new Map(), betaStates = new Map();
  const alphaFrames = new Map(), betaFrames = new Map();

  for (let tick = 0; tick < 7; ++tick) {
    alpha.recordLocal(tick, localA[tick]);
    beta.recordLocal(tick, localB[tick]);
    commitForward(alpha, alphaStates, alphaFrames, alpha.planForward());
    commitForward(beta, betaStates, betaFrames, beta.planForward());
  }
  alpha.recordLocal(7, localA[7]);
  beta.recordLocal(7, localB[7]);
  assert.equal(alpha.planForward().reason, 'prediction_limit');
  assert.equal(beta.planForward().reason, 'prediction_limit');

  const alphaFirst = [5, 2, 4, 0, 3, 1];
  const betaFirst = [4, 1, 5, 2, 0, 3];
  for (let index = 0; index < alphaFirst.length; ++index) {
    applyRemote(alpha, alphaStates, alphaFrames, alphaFirst[index], localB[alphaFirst[index]]);
    applyRemote(beta, betaStates, betaFrames, betaFirst[index], localA[betaFirst[index]]);
  }
  assert.equal(alpha.status().remote_contiguous_through, 5);
  assert.equal(beta.status().remote_contiguous_through, 5);

  const advance = (side, target) => {
    const [peer, locals, states, frames] = side;
    while (peer.status().simulated_through < target) {
      const tick = peer.status().simulated_through + 1;
      peer.recordLocal(tick, locals[tick]);
      const plan = peer.planForward();
      if (plan.status === 'blocked') return plan;
      commitForward(peer, states, frames, plan);
    }
    return null;
  };
  const alphaSide = [alpha, localA, alphaStates, alphaFrames];
  const betaSide = [beta, localB, betaStates, betaFrames];
  assert.equal(advance(alphaSide, 12), null);
  assert.equal(advance(betaSide, 12), null);

  for (const tick of [9, 6, 8, 7, 10]) {
    applyRemote(alpha, alphaStates, alphaFrames, tick, localB[tick]);
    applyRemote(beta, betaStates, betaFrames, tick, localA[tick]);
  }
  assert.equal(alpha.status().remote_contiguous_through, 10);
  assert.equal(beta.status().remote_contiguous_through, 10);
  assert.equal(advance(alphaSide, 17), null);
  assert.equal(advance(betaSide, 17), null);

  for (const tick of [15, 11, 17, 13, 16, 12, 14]) {
    applyRemote(alpha, alphaStates, alphaFrames, tick, localB[tick]);
    applyRemote(beta, betaStates, betaFrames, tick, localA[tick]);
  }
  assert.equal(alpha.status().dirty_from, null);
  assert.equal(beta.status().dirty_from, null);
  assert.equal(alpha.status().confirmed_through, 17);
  assert.equal(beta.status().confirmed_through, 17);

  let oracle = 0x12345678;
  for (let tick = 0; tick < count; ++tick) {
    const expectedFrame = Array.from({length: NET_FRAME_BYTES}, (_, index) =>
      index % PAD_BYTES === PAD_BYTES - 1 && Math.floor(index / PAD_BYTES) >= 2 ? 0xff : 0);
    expectedFrame.splice(0, PAD_BYTES, ...localA[tick]);
    expectedFrame.splice(PAD_BYTES, PAD_BYTES, ...localB[tick]);
    const expected = Object.freeze(expectedFrame);
    if (tick <= 17) {
      assert.deepEqual(alphaFrames.get(tick), expected, `alpha final input tick ${tick}`);
      assert.deepEqual(betaFrames.get(tick), expected, `beta final input tick ${tick}`);
    }
    oracle = toyStep(oracle, tick, localA[tick], localB[tick]);
  }
  assert.equal(alphaStates.get(17), oracle);
  assert.equal(betaStates.get(17), oracle);
});

test('module imports with the Node Buffer global unavailable', async () => {
  const source = await readFile(new URL('../scripts/net_rollback_input_timeline.mjs', import.meta.url), 'utf8');
  assert.doesNotMatch(source, /^\s*import\s+.*(?:node:|Buffer|process)/m);
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'Buffer');
  try {
    Object.defineProperty(globalThis, 'Buffer', {configurable: true, value: undefined});
    await import('../scripts/net_rollback_input_timeline.mjs?without-buffer');
  } finally {
    if (previous) Object.defineProperty(globalThis, 'Buffer', previous);
    else delete globalThis.Buffer;
  }
});
