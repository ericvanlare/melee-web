import {createHash} from 'node:crypto';
import {validateSssExitExpectedFields, compareSssExitObservationFields,
  validateSssExitTraceFields} from './stadium_current_sss_exit_observation.mjs';

export const STOCK4_SSS_STOP = 'Manual unload stopped the replay';
const requireValue = (v, message) => { if (!v) throw Error(message); };
const ORIGINAL_ENTRY = Object.freeze({sequence: 1640, source_tick: 125, draw_ordinal: 0,
  start_melee_sha256: '918a23d68a3b5024f94baf1f5e26e6c0db56c5fbbbffdc79c5e620e82af3b728',
  rng: Object.freeze({pointer_slot_address: 0x804d5f94, pointer_hex: '804d5f90',
    value_address: 0x804d5f90, value_hex: 'e2624ac3'})});
export function validateStock4SssExitExpected(v) {
  requireValue(v?.schema === 'melee-web-stock4-none-sss-exit-host-expectation-v1' &&
    v.observation_profile === 1 && v.menu_draw_reference === 'UNKNOWN' &&
    v.Entry === false && v.C3 === false && !Object.hasOwn(v, 'expected_runtime_refusal') &&
    v.expected_replay_stop === STOCK4_SSS_STOP && v.recipe?.version === 8 &&
    v.recipe.frames === 2006 && Number.isSafeInteger(v.recipe.bytes) && v.recipe.bytes >= 328 &&
    /^[0-9a-f]{64}$/.test(v.recipe.sha256) &&
    Number.isSafeInteger(v.notification_cursor) && v.notification_cursor > 0 &&
    Array.isArray(v.literal_inputs) && v.literal_inputs.length === v.notification_cursor,
  'Stock4 SSS expectation scope/recipe/input differs');
  validateSssExitExpectedFields(v);
  const p = v.prepared_profile, entry = p?.entry, setup = p?.setup;
  const valid = row => row && row.match_index === 0 &&
    ['sequence', 'source_tick', 'draw_ordinal', 'pc'].every(k => Number.isSafeInteger(row[k]) && row[k] >= 0) &&
    typeof row.start_melee_hex === 'string' && /^[0-9a-f]{624}$/.test(row.start_melee_hex) &&
    createHash('sha256').update(Buffer.from(row.start_melee_hex, 'hex')).digest('hex') === row.start_melee_sha256;
  requireValue(p?.scope === 'original_prepared_Entry_Setup_profile_not_SSS_payload' &&
    p.source_primary_sha256 === v.primary_sha256 && valid(entry) && valid(setup) &&
    entry.boundary === 'entry' && setup.boundary === 'setup' &&
    v.source.sequence < entry.sequence && entry.sequence < setup.sequence &&
    entry.start_melee_hex === setup.start_melee_hex &&
    JSON.stringify(entry.declared_setup) === JSON.stringify(setup.declared_setup),
  'Stock4 SSS prepared profile source identities/order differ');
  const r = entry.declared_setup;
  requireValue(r?.match_kind === 1 && r.is_stock === true && r.stage === 3 && r.item_frequency === -1 &&
    r.time_limit_seconds === 0 && r.timer_enabled === false && r.timer_counts_up === false &&
    r.disable_pausing === false && r.is_teams === false && Array.isArray(r.players) && r.players.length === 2 &&
    r.players.every((p, i) => p.port === i + 1 && p.character_kind === 8 && p.costume === (i === 0 ? 1 : 0) &&
      p.stocks === 4 && p.player_type === 0), 'Stock4 prepared Entry/Setup profile differs');
  // The SSS payload remains an earlier, independently compared raw projection.
  // Its template rules/zero stock counters are not the prepared match profile.
  return v;
}
export function validateStock4SssExitStop(actual, expected, unloaded = false) {
  validateStock4SssExitExpected(expected);
  const t = actual?.terminal, c = actual?.notification_coordinates;
  requireValue(actual.observation_profile === 1 && t?.complete === true &&
    t.host_call_returned === true && t.cursor === expected.notification_cursor &&
    t.callback_source_steps === 0 && t.callback_source_draws === 0 &&
    t.asset_requests_after_leave_boundary === 0 &&
    t.waiting_for_audio_retained_until_unload === true && t.native_unloaded === unloaded &&
    c?.enclosing_host_tick_status === 'not_in_host_tick' && c.last_host_tick_call_returned === true &&
    c.callback_source_steps === 0 && c.callback_source_draws === 0,
  'Stock4 SSS stop is not the accounted ordinary-leave/pre-assets boundary');
  return {pass: true, scope: 'checked_stock4_SSS_leave_stop_only',
    native_unloaded: unloaded, runtime_admission: false, Entry: false, C3: false};
}
export function compareStock4SssExitObservation(actual, expected) {
  validateStock4SssExitExpected(expected);
  validateStock4SssExitStop(actual, expected);
  return compareSssExitObservationFields(actual, expected);
}
export function compareStock4PreparedStartProjection(actualStart, declaredSetup, originalEntryStartHex) {
  const rules = actualStart?.rules, players = actualStart?.players;
  const bit = (value, expected) => typeof value === 'number' && value === Number(expected);
  requireValue(rules && Array.isArray(players) && players.length === 6 &&
    typeof originalEntryStartHex === 'string' && /^[0-9a-f]{624}$/.test(originalEntryStartHex) &&
    Array.isArray(declaredSetup?.players) && declaredSetup.players.length === 2,
  'Prepared Start compact projection/profile shape differs');
  const originalEntryStart = Buffer.from(originalEntryStartHex, 'hex');
  requireValue(rules.match_kind === declaredSetup.match_kind &&
    bit(rules.is_stock, declaredSetup.is_stock) && rules.stage_kind === declaredSetup.stage &&
    rules.xB === declaredSetup.item_frequency &&
    rules.time_limit === declaredSetup.time_limit_seconds * 60 &&
    bit(rules.timer_enabled, declaredSetup.timer_enabled) &&
    bit(rules.timer_counts_up, declaredSetup.timer_counts_up) &&
    bit(rules.disable_pausing, declaredSetup.disable_pausing) &&
    bit(rules.is_teams, declaredSetup.is_teams) &&
    rules.item_mask === declaredSetup.item_mask_hex &&
    rules.damage_ratio_bits === declaredSetup.damage_ratio_bits &&
    rules.game_speed_bits === declaredSetup.game_speed_bits &&
    rules.is_vs === 1 && rules.friendly_fire === 0 && rules.timer_shows_hours === 0 &&
    rules.x14 === 0,
  'Prepared Start compact rules differ from the source declared setup');
  for (let i = 0; i < 2; ++i) {
    const player = players[i], wanted = declaredSetup.players[i];
    requireValue(player && wanted && player.slot === originalEntryStart[0x60 + i * 0x24 + 4] &&
      player.slot_type === wanted.player_type && player.ckind === wanted.character_kind &&
      player.color === wanted.costume && player.stocks === wanted.stocks &&
      ((player.flags_c >>> 7) & 1) === Number(wanted.rumble_enabled),
    `Prepared Start compact player ${i + 1} differs from the source declared setup`);
  }
  requireValue(players.slice(2).every(player => player?.slot_type === 3),
    'Prepared Start compact projection contains an extra active source player');
  return {pass: true, scope: 'declared_source_common_fields_only',
    source_slot_bytes: 'compared_to_original_entry', raw_start_melee_bytes: 'not_compared',
    runtime_admission: false, Entry: false, C3: false};
}
export function compareStock4PreparedStartReceipt(actual, expectedRng, declaredSetup, originalEntryStartHex) {
  requireValue(actual && Object.keys(actual).sort().join('|') === [
    'schema','scope','phase','observation_profile','captured','source_scene','host_entered',
    'waiting_for_audio_retained_until_unload','replay_cursor','local_order_rows',
    'callback_source_steps','callback_source_draws','asset_requests_after_leave_boundary',
    'world_generation','hsd_pending_loads_at_return','bank_transport_active_at_return',
    'bank_transport_configured_at_return','bank_transport_busy_at_return','random_seed_hex','start',
    'menu_draw_reference','runtime_admission','Entry','C3'
  ].sort().join('|') &&
    actual.schema === 'melee-web-stadium-prepared-start-before-assets-v2' &&
    actual.scope === 'source_prepared_start_after_sss_leave_before_assets' &&
    actual.phase === 'prepared_entry' && actual.observation_profile === 1 && actual.captured === true &&
    actual.source_scene === 0 && actual.host_entered === false &&
    actual.waiting_for_audio_retained_until_unload === true &&
    actual.replay_cursor === 270 && actual.local_order_rows === 543 &&
    actual.callback_source_steps === 0 && actual.callback_source_draws === 0 &&
    actual.asset_requests_after_leave_boundary === 0 &&
    Number.isSafeInteger(actual.world_generation) && actual.world_generation > 0 &&
    actual.hsd_pending_loads_at_return === 0 &&
    actual.bank_transport_active_at_return === true &&
    actual.bank_transport_configured_at_return === true &&
    actual.bank_transport_busy_at_return === false &&
    actual.random_seed_hex === expectedRng?.value_hex && actual.menu_draw_reference === 'UNKNOWN' &&
    actual.runtime_admission === false && actual.Entry === false && actual.C3 === false,
  'Prepared Start receipt escaped the exact stopped stock4 SSS pre-assets boundary or Entry seed');
  const projection = compareStock4PreparedStartProjection(actual.start, declaredSetup, originalEntryStartHex);
  return {pass: true, scope: 'prepared_start_boundary_common_fields_and_announcer_service_drain', projection,
    announcer_service_completion: {
      basis: 'source_backed_synchronous_helper_return_and_drained_real_service',
      helper_return: 'C1A host READY follows helper success; the helper calls authored gm_LoadAnnouncer synchronously',
      hsd_pending_loads_at_return: 0, bank_transport_active_at_return: true,
      bank_transport_configured_at_return: true, bank_transport_busy_at_return: false,
      transfer_occurrence: 'UNOBSERVED'
    }, runtime_admission: false, Entry: false, C3: false};
}
export function compareStock4PreparedStartObservation(actual, expected) {
  validateStock4SssExitExpected(expected);
  const entry = expected.prepared_profile.entry, rng = entry.rng;
  requireValue(entry.boundary === 'entry' && entry.sequence === ORIGINAL_ENTRY.sequence &&
    entry.source_tick === ORIGINAL_ENTRY.source_tick && entry.draw_ordinal === ORIGINAL_ENTRY.draw_ordinal &&
    entry.start_melee_sha256 === ORIGINAL_ENTRY.start_melee_sha256 &&
    rng?.pointer_slot_address === ORIGINAL_ENTRY.rng.pointer_slot_address &&
    rng?.pointer_hex === ORIGINAL_ENTRY.rng.pointer_hex &&
    rng?.value_address === ORIGINAL_ENTRY.rng.value_address &&
    rng?.value_hex === ORIGINAL_ENTRY.rng.value_hex,
  'Prepared Start comparison is not bound to the retained original Entry/RNG identity');
  const receipt = compareStock4PreparedStartReceipt(actual, rng, entry.declared_setup, entry.start_melee_hex);
  return {pass: true, scope: 'prepared_start_common_fields_and_announcer_service_drain_against_original_entry_profile',
    original_entry: {sequence: entry.sequence, source_tick: entry.source_tick,
      draw_ordinal: entry.draw_ordinal, start_melee_sha256: entry.start_melee_sha256,
      rng_pointer_hex: rng.pointer_hex, rng_value_hex: rng.value_hex},
    projection: receipt.projection,
    announcer_service_completion: receipt.announcer_service_completion,
    raw_start_melee_bytes: 'not_compared',
    runtime_admission: false, Entry: false, C3: false};
}
export function validateStock4SssExitTrace(text, expected, actual) {
  validateStock4SssExitExpected(expected);
  return validateSssExitTraceFields(text, expected, actual);
}
export function validateStock4SssExitTerminal(report, snapshot, expected, actual) {
  validateStock4SssExitStop(actual, expected, true);
  const failures = ['whole-session final CSS was not entered', STOCK4_SSS_STOP,
    'incomplete input timeline', 'source tick/draw count mismatch'];
  requireValue(report?.pass === false && report.complete === false &&
    report.frames === expected.recipe.frames && report.mode === 'state_capture' &&
    report.scheduling_equivalence === 'not_evaluated' && report.instrumented_timing_resumes === 0 &&
    report.metrics?.sourceFrames === expected.notification_cursor &&
    report.metrics.sourceSteps === expected.notification_cursor &&
    Number.isSafeInteger(report.metrics.sourceDraws) && report.metrics.sourceDraws >= 0 &&
    JSON.stringify(report.source_match) === JSON.stringify({complete: false, outcome: null, winner: null}) &&
    report.final_scene === null && JSON.stringify(report.failures) === JSON.stringify(failures) &&
    snapshot?.phase === 0 && snapshot.running === 0 && !snapshot.runtime_error,
  'Stock4 SSS terminal is not the explicit bounded stop with normal unload');
  return {pass: true, runtime_replay: 'FAIL_INCOMPLETE_EXPLICIT_SSS_STOP',
    checked_unload: true, runtime_admission: false, Entry: false, C3: false};
}
export function validateStock4SssExitFinalization(report, expected) {
  validateStock4SssExitExpected(expected);
  requireValue(!report.failure && !report.first_mismatch && !report.first_error &&
    Array.isArray(report.snapshots) && report.snapshots.length > 0 && report.snapshots.length < 2048 &&
    report.cleanup?.browser_close_returned === true,
  'Stock4 SSS capture has unknown/saturated history or a fatal diagnostic');
  const snapshots = [...report.snapshots, report.last_successful_snapshot, report.final_snapshot]
    .filter(value => value != null);
  requireValue(snapshots.every(s => !s.snapshot_error && !s.runtime_error) &&
    Array.isArray(report.phases) && report.phases.every(p => p.result === 'pass') &&
    report.phases.some(p => p.name === 'current-sss-exit-component') &&
    report.current_sss_exit?.unload_requested === true &&
    report.original_replay_capture_result === 'fail' &&
    JSON.stringify(report.finalization_failures) === JSON.stringify([
      'Whole-session replay did not complete successfully', 'Browser replay report is incomplete or failed']),
  'Stock4 SSS capture contains additional failure or lacks its explicit stop');
  validateStock4SssExitTerminal(report.browser_report, report.final_snapshot, expected,
    report.current_sss_exit.unloaded_observation);
  return {pass: true, scope: 'explicit_stock4_SSS_stop_component_only',
    original_replay_capture_result: 'fail', runtime_admission: false, Entry: false, C3: false};
}
export function validateStock4PreparedStartFinalization(report, expected) {
  validateStock4SssExitExpected(expected);
  requireValue(report?.current_sss_exit?.prepared_start_comparison?.pass === true &&
    report.current_sss_exit.prepared_start_comparison.scope ===
      'prepared_start_common_fields_and_announcer_service_drain_against_original_entry_profile' &&
    !report.current_sss_exit.prepared_start_failure,
  'Prepared Start candidate is missing its separately enabled exact-boundary comparison');
  return {pass: true, scope: 'prepared_start_common_fields_and_announcer_service_drain_against_original_entry_profile',
    Entry: false, C3: false, runtime_admission: false};
}
