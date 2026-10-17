import {createHash} from 'node:crypto';
import {validateSssExitExpectedFields, compareSssExitObservationFields,
  validateSssExitTraceFields} from './stadium_current_sss_exit_observation.mjs';

export const STOCK4_SSS_STOP = 'Manual unload stopped the replay';
const requireValue = (v, message) => { if (!v) throw Error(message); };
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
