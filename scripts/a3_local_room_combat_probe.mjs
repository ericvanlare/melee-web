import assert from 'node:assert/strict';

function requireValue(condition, message) {
  if (!condition) throw Error(message);
}

function signedByte(value) {
  return value > 127 ? value - 256 : value;
}

function isNeutralPadRow(row) {
  return Array.isArray(row?.bytes) && row.bytes.length === 11 &&
    row.bytes.every(value => value === 0);
}

// Validate only literal local capture rows for the bounded P2 prefix. This
// does not infer movement from the key press or synthesize a neutral sample.
export function validateP2GameplayInputWindow({p2Rows, p1Rows, movementTick,
  releaseTick, direction}) {
  requireValue(Number.isSafeInteger(movementTick) && Number.isSafeInteger(releaseTick) &&
    releaseTick > movementTick, 'P2 input window has invalid source ticks');
  requireValue(direction === -1 || direction === 1,
    'P2 input window has invalid horizontal direction');

  const exactRows = (rows, port, label) => {
    const window = rows.filter(row => row?.input_tick >= movementTick &&
      row.input_tick <= releaseTick);
    assert.equal(window.length, releaseTick - movementTick + 1,
      `${label}: literal input capture window has a missing or duplicate tick`);
    for (let offset = 0; offset < window.length; offset++) {
      const row = window[offset];
      const tick = movementTick + offset;
      assert.equal(row.input_tick, tick, `${label}: input ticks are not in source order`);
      assert.equal(row.source_cursor, tick, `${label}: input capture is not source-cursor aligned`);
      assert.equal(row.local_port, port, `${label}: local input port changed`);
      assert(Array.isArray(row.bytes) && row.bytes.length === 11 &&
        row.bytes.every(value => Number.isInteger(value) && value >= 0 && value <= 255),
      `${label}: input row is not an exact 11-byte PAD record`);
    }
    return window;
  };

  const p2 = exactRows(p2Rows, 1, 'P2');
  const p1 = exactRows(p1Rows, 0, 'P1');
  const movement = p2[0];
  assert.equal(Math.sign(signedByte(movement.bytes[2])), direction,
    'P2 first captured sample does not match the requested horizontal direction');
  assert(movement.bytes.slice(0, 2).every(value => value === 0) &&
    movement.bytes.slice(3).every(value => value === 0),
  'P2 gameplay prefix contains an unexpected non-horizontal PAD input');
  for (const row of p2.slice(0, -1)) {
    assert.equal(Math.sign(signedByte(row.bytes[2])), direction,
      'P2 horizontal input changed before the observed neutral release');
    assert(row.bytes.slice(0, 2).every(value => value === 0) &&
      row.bytes.slice(3).every(value => value === 0),
    'P2 gameplay prefix contains an unexpected non-horizontal PAD input');
  }
  assert(isNeutralPadRow(p2.at(-1)), 'P2 release is not a literal all-zero PAD record');
  assert(p1.every(isNeutralPadRow), 'P1 was not literally neutral across the P2 input window');
  return {movement_input_tick: movementTick, neutral_release_input_tick: releaseTick,
    p1_neutral_capture_count: p1.length, p2_direction: direction};
}


// The approach stops at a bounded positive gap hypothesis. A new pulse is
// allowed only when the last measured settled displacement cannot cross P1.
export function decideP2ApproachTap({gap, tapCount, sourceTickStart, sourceTick,
  maxObservedInwardDisplacement = null, maxTaps = 16, maxSourceTicks = 480,
  targetMaxGap = 20}) {
  if (!Number.isFinite(gap) || !Number.isSafeInteger(tapCount) || tapCount < 0 ||
      !Number.isSafeInteger(sourceTickStart) || !Number.isSafeInteger(sourceTick) ||
      sourceTick < sourceTickStart || !Number.isSafeInteger(maxTaps) || maxTaps < 1 ||
      !Number.isSafeInteger(maxSourceTicks) || maxSourceTicks < 1 ||
      !Number.isFinite(targetMaxGap) || targetMaxGap <= 0 ||
      (maxObservedInwardDisplacement !== null &&
       (!Number.isFinite(maxObservedInwardDisplacement) || maxObservedInwardDisplacement <= 0)))
    throw Error('P2 approach decision has invalid source bounds or measured positions');
  const elapsedSourceTicks = sourceTick - sourceTickStart;
  if (gap <= 0) return {decision: 'stop', reason: 'crossed-initial-side', elapsed_source_ticks: elapsedSourceTicks};
  if (elapsedSourceTicks > maxSourceTicks)
    return {decision: 'stop', reason: 'source-cursor-budget-exhausted', elapsed_source_ticks: elapsedSourceTicks};
  if (gap <= targetMaxGap)
    return {decision: 'reached', reason: 'positive-gap-hypothesis', elapsed_source_ticks: elapsedSourceTicks};
  if (elapsedSourceTicks >= maxSourceTicks)
    return {decision: 'stop', reason: 'source-cursor-budget-exhausted', elapsed_source_ticks: elapsedSourceTicks};
  if (tapCount >= maxTaps) return {decision: 'stop', reason: 'tap-budget-exhausted', elapsed_source_ticks: elapsedSourceTicks};
  if (maxObservedInwardDisplacement !== null && gap <= maxObservedInwardDisplacement)
    return {decision: 'stop', reason: 'next-tap-could-cross-from-measured-displacement',
      elapsed_source_ticks: elapsedSourceTicks};
  return {decision: 'tap', reason: 'remaining-gap-exceeds-measured-step',
    elapsed_source_ticks: elapsedSourceTicks};
}

// The combat approach permits exactly one captured directional source row,
// followed by the literal neutral release and paired neutral input on P1.
export function validateSingleP2ApproachTapWindow({p2Rows, p1Rows, movementTick,
  releaseTick, direction, expectedMovementBytes}) {
  const p2Window = p2Rows.filter(row => row?.input_tick >= movementTick &&
    row.input_tick <= releaseTick);
  const p1Window = p1Rows.filter(row => row?.input_tick >= movementTick &&
    row.input_tick <= releaseTick);
  const validated = validateP2GameplayInputWindow({p2Rows: p2Window, p1Rows: p1Window,
    movementTick, releaseTick, direction});
  assert.equal(p2Window.length - 1, 1,
    'P2 combat approach tap did not resolve to exactly one captured active source row');
  assert(Array.isArray(expectedMovementBytes) && expectedMovementBytes.length === 11,
    'P2 combat approach lacks the retained keyboard PAD sample');
  assert.deepEqual(p2Window[0].bytes, expectedMovementBytes,
    'P2 combat approach capture differs from the retained movement sample');
  return {validated, p2Window, p1Window};
}

// Item frequency -1 is the source None setting. Preserve and compare the
// observed mask, but do not interpret an all-ones mask as disabled items.
export function validateCombatItemsNone(matches, expectedMaskHex = null) {
  if (!Array.isArray(matches) || matches.length !== 2)
    throw Error('Combat route requires two paired match-rule observations');
  const rules = matches.map(match => match?.rules);
  for (const [index, rule] of rules.entries()) {
    if (rule?.item_frequency !== -1)
      throw Error(`Combat peer ${index === 0 ? 'alpha' : 'beta'} did not observe Items None frequency -1`);
    if (typeof rule.item_mask_hex !== 'string' || !/^[a-f0-9]{16}$/.test(rule.item_mask_hex))
      throw Error(`Combat peer ${index === 0 ? 'alpha' : 'beta'} has no exact source item mask`);
  }
  if (rules[0].item_mask_hex !== rules[1].item_mask_hex ||
      (expectedMaskHex !== null && rules[0].item_mask_hex !== expectedMaskHex))
    throw Error('Combat peers changed or disagree on the observed source item mask');
  return {item_frequency: -1, item_mask_hex: rules[0].item_mask_hex};
}

// Require exact peer agreement for each combat player at an already-joined
// source cursor. These are the declared action fields, not all source state.
export function comparePairedCombatPlayers(matches) {
  if (!Array.isArray(matches) || matches.length !== 2 ||
      !matches.every(match => Array.isArray(match?.players) && match.players.length === 2))
    throw Error('Combat observation requires two paired native players');
  for (let port = 0; port < 2; ++port) {
    const left = matches[0].players[port];
    const right = matches[1].players[port];
    for (const field of ['source_port', 'motion', 'groundAir', 'stocks',
      'damage_percent', 'x', 'y', 'animation_frame']) {
      if (left?.[field] !== right?.[field])
        throw Error(`Combat peers disagree on P${port + 1} ${field} at one source cursor`);
    }
  }
  return true;
}

// Shield exchange fields are required only by the additive exchange scenario.
// This leaves the existing approach/jab probe compatible with its frozen
// observation schema while requiring exact finite same-cursor peer agreement
// where shield evidence is actually claimed.
export function comparePairedShieldObservation(matches) {
  comparePairedCombatPlayers(matches);
  for (let port = 0; port < 2; ++port) {
    const left = matches[0].players[port];
    const right = matches[1].players[port];
    for (const field of ['shield_health', 'facing_direction']) {
      if (!Number.isFinite(left?.[field]) || !Number.isFinite(right?.[field]))
        throw Error(`Combat exchange P${port + 1} ${field} is not finite on both peers`);
      if (left[field] !== right[field])
        throw Error(`Combat exchange peers disagree on P${port + 1} ${field} at one source cursor`);
    }
  }
  return true;
}

// The reverse-jab hypothesis requires P1 to remain left of P2 and face right
// on both peer snapshots at the retained actual baseline.
export function validateReverseJabSetup(matches, {maxGap = 20} = {}) {
  comparePairedShieldObservation(matches);
  if (!Number.isFinite(maxGap) || maxGap <= 0)
    throw Error('Reverse-jab setup has an invalid positive-gap bound');
  const signedGapByPeer = matches.map(match => match.players[1].x - match.players[0].x);
  const p1FacingByPeer = matches.map(match => match.players[0].facing_direction);
  if (!signedGapByPeer.every(gap => Number.isFinite(gap) && gap > 0 && gap <= maxGap))
    throw Error(`Reverse-jab setup requires signed P2x-P1x in (0, ${maxGap}] on both peers`);
  if (!p1FacingByPeer.every(direction => direction === 1))
    throw Error('Reverse-jab setup requires P1 facing_direction +1 on both peers');
  return {p1_x_by_peer: matches.map(match => match.players[0].x),
    p2_x_by_peer: matches.map(match => match.players[1].x),
    signed_gap_by_peer: signedGapByPeer,
    p1_facing_direction_by_peer: p1FacingByPeer};
}

// Keep the same victim-damage baseline through every paired shield-action
// sample and the final grounded-neutral recovery snapshot.
export function assertPairedVictimDamageUnchanged(matches,
  {initialVictimDamage, victimPort = 1} = {}) {
  if (!Number.isFinite(initialVictimDamage) || ![0, 1].includes(victimPort))
    throw Error('Combat victim-damage guard requires a finite baseline and valid port');
  comparePairedCombatPlayers(matches);
  const damageByPeer = matches.map(match => match.players[victimPort].damage_percent);
  if (!damageByPeer.every(Number.isFinite) ||
      !damageByPeer.every(value => value === initialVictimDamage))
    throw Error(`P${victimPort + 1} damage changed from ${initialVictimDamage}: ${JSON.stringify(damageByPeer)}`);
  return {victim_port: victimPort, baseline_damage_percent: initialVictimDamage,
    damage_percent_by_peer: damageByPeer, unchanged: true};
}

// A shield hit is supported only by the paired GuardSetOff state, unchanged
// defender damage, and shield loss measured from the paired guard-entry
// baseline. Passive decay or guard entry alone cannot satisfy this predicate.
export function observePairedShieldJab(matches, {initialVictimDamage,
  guardEntryShieldHealth, actorPort = 0, victimPort = 1} = {}) {
  if (!Number.isFinite(initialVictimDamage) || !Number.isFinite(guardEntryShieldHealth) ||
      ![0, 1].includes(actorPort) || ![0, 1].includes(victimPort) || actorPort === victimPort)
    throw Error('Combat shield-jab observation requires finite paired baselines and distinct ports');
  comparePairedShieldObservation(matches);
  const actorMotions = matches.map(match => match.players[actorPort].motion);
  const victimMotions = matches.map(match => match.players[victimPort].motion);
  const victimDamage = matches.map(match => match.players[victimPort].damage_percent);
  const victimShield = matches.map(match => match.players[victimPort].shield_health);
  if (!victimDamage.every(Number.isFinite) || !victimShield.every(Number.isFinite))
    throw Error('Combat shield-jab victim fields are not finite');
  const actorAttackObserved = actorMotions[0] === 44 && actorMotions[1] === 44;
  const guardSetOffObserved = victimMotions[0] === 181 && victimMotions[1] === 181;
  const damageUnchanged = victimDamage[0] === initialVictimDamage &&
    victimDamage[1] === initialVictimDamage;
  const shieldDecreased = victimShield.every(value => value < guardEntryShieldHealth);
  return {actor_motion_ids: actorMotions, actor_attack11_observed: actorAttackObserved,
    victim_motion_ids: victimMotions, guard_setoff_181_observed: guardSetOffObserved,
    victim_damage_percent: victimDamage, victim_damage_unchanged: damageUnchanged,
    victim_shield_health: victimShield,
    guard_entry_shield_health: guardEntryShieldHealth,
    victim_shield_decreased_from_guard_entry: shieldDecreased,
    hit_response_observed: guardSetOffObserved && damageUnchanged && shieldDecreased,
    shield_hit_response_observed: guardSetOffObserved && damageUnchanged && shieldDecreased};
}

// Validate exact same-cursor actor and defender fields. The action motion and
// victim reaction are retained at their own observed cursors; the observer does
// not assume their transition frames coincide.
export function observePairedCombatJab(matches, {initialVictimDamage,
  actorPort = 1, victimPort = 0} = {}) {
  if (!Number.isFinite(initialVictimDamage) || ![0, 1].includes(actorPort) ||
      ![0, 1].includes(victimPort) || actorPort === victimPort)
    throw Error('Combat jab observation requires paired players and a finite victim baseline');
  comparePairedCombatPlayers(matches);
  const actorMotions = matches.map(match => match.players[actorPort].motion);
  const victimMotions = matches.map(match => match.players[victimPort].motion);
  const victimDamage = matches.map(match => match.players[victimPort].damage_percent);
  if (!victimDamage.every(Number.isFinite))
    throw Error('Combat victim damage observation is not finite');
  const actorAttackObserved = actorMotions[0] === 44 && actorMotions[1] === 44;
  const victimDamageMotionObserved = victimMotions[0] === victimMotions[1] &&
    Number.isSafeInteger(victimMotions[0]) && victimMotions[0] >= 75 && victimMotions[0] <= 91;
  const victimDamageIncreased = victimDamage[0] === victimDamage[1] &&
    victimDamage[0] > initialVictimDamage + 0.001;
  return {actor_motion_ids: actorMotions, actor_attack11_observed: actorAttackObserved,
    victim_motion_ids: victimMotions, victim_damage_motion_observed: victimDamageMotionObserved,
    victim_damage_percent: victimDamage, victim_damage_increased: victimDamageIncreased,
    hit_response_observed: victimDamageMotionObserved && victimDamageIncreased};
}


// Retain approach readbacks before the cursor policy rejects an over-budget
// sample, so a stopping boundary still has its observed native state and PAD.
export function retainCombatApproachState({evidenceSink, state, sourceCursorLimit}) {
  const captureCount = state?.owner_health?.peer?.localInputCapture?.captured_count ?? null;
  const nativeCursor = state?.native_status?.cursor ?? null;
  evidenceSink.last_p1_state_readback = {native_cursor: nativeCursor,
    captured_count: captureCount, runtime_error: state?.runtime_error ?? null,
    owner_failure: state?.owner_health?.failure ?? null,
    peer_failure: state?.owner_health?.peer?.failure ?? null,
    local_capture_failure: state?.native_status?.local_capture_failure ?? null};
  if (sourceCursorLimit !== null)
    requireValue(Number.isSafeInteger(nativeCursor) && nativeCursor <= sourceCursorLimit,
      'P1 state acquisition exceeded the P2 approach source-cursor budget');
  return captureCount;
}

export function retainCombatApproachInputReadback({evidenceSink, p2Readback,
  p1Readback, movementTick, sourceCursorLimit}) {
  const p2Captures = p2Readback?.health?.peer?.localInputCapture?.captures_since;
  const p1Captures = p1Readback?.health?.peer?.localInputCapture?.captures_since;
  evidenceSink.last_suffix_readback = {
    p2_native_cursor: p2Readback?.native_status?.cursor ?? null,
    p1_native_cursor: p1Readback?.native_status?.cursor ?? null,
    p2_runtime_error: p2Readback?.runtime_error ?? null,
    p1_runtime_error: p1Readback?.runtime_error ?? null,
    p2_owner_failure: p2Readback?.health?.failure ?? null,
    p1_owner_failure: p1Readback?.health?.failure ?? null,
    p2_peer_failure: p2Readback?.health?.peer?.failure ?? null,
    p1_peer_failure: p1Readback?.health?.peer?.failure ?? null,
    p2_local_capture_failure: p2Readback?.native_status?.local_capture_failure ?? null,
    p1_local_capture_failure: p1Readback?.native_status?.local_capture_failure ?? null};
  if (Array.isArray(p2Captures) && Array.isArray(p1Captures)) {
    const p2Rows = p2Captures.filter(row => row.input_tick >= movementTick);
    const observedEnd = p2Rows.at(-1)?.input_tick ?? null;
    evidenceSink.p2_literal_rows = p2Rows;
    evidenceSink.p1_literal_rows = p1Captures.filter(row =>
      row.input_tick >= movementTick && (observedEnd === null || row.input_tick <= observedEnd));
    evidenceSink.observed_capture_end_tick = observedEnd;
  }
  if (sourceCursorLimit !== null)
    requireValue([p2Readback, p1Readback].every(snapshot =>
      Number.isSafeInteger(snapshot?.native_status?.cursor) &&
      snapshot.native_status.cursor <= sourceCursorLimit),
      'P2 input suffix acquisition exceeded the approach source-cursor budget');
  return {p2Captures, p1Captures};
}


// Keep the complete last aligned native pair in the report before running any
// classifier or guard that may reject the observed action boundary.
export function inspectCombatPairRetainingRawRows(record, rows, inspect = sourceRows => sourceRows) {
  record.last_inspected_pair = {source_tick: rows?.[0]?.source_tick ?? null, peers: rows};
  return inspect(rows);
}

// Check both actual native cursors on each checksum poll. The latest poll and
// first over-limit pair are retained before the caller refuses the boundary.
export function retainCombatChecksumCursorPoll({evidenceSink, states, sourceCursorLimit}) {
  if (!evidenceSink || typeof evidenceSink !== 'object' || !Array.isArray(states) ||
      states.length !== 2 || !Number.isSafeInteger(sourceCursorLimit))
    throw Error('Combat checksum poll requires two states, an evidence sink, and a source limit');
  const peers = states.map((state, index) => ({
    peer: index === 0 ? 'alpha' : 'beta',
    source_cursor: Number.isSafeInteger(state?.native_status?.cursor)
      ? state.native_status.cursor : null,
  }));
  const poll = {source_cursor_limit: sourceCursorLimit, peers};
  evidenceSink.checksum_cursor_poll_count =
    (evidenceSink.checksum_cursor_poll_count ?? 0) + 1;
  evidenceSink.last_checksum_cursor_poll = poll;
  const overLimit = peers.find(peer => peer.source_cursor === null ||
    peer.source_cursor > sourceCursorLimit);
  if (overLimit) {
    evidenceSink.checksum_cursor_budget_failure = poll;
    throw Error(`Approach checksum poll exceeded source-cursor limit for ${overLimit.peer}: ` +
      `${overLimit.source_cursor} > ${sourceCursorLimit}`);
  }
  return poll;
}
