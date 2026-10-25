// Node-side finite occurrence1 admission. The page arm remains the shared closure-free helper.
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {STOCK4_SSS_STOP, validateLaterStock4SssExitExpectedFields, comparePreparedStartReceiptFields} from './stadium_stock4_sss_exit_observation.mjs';
import {compareSssExitObservationFields, validateSssExitTraceFields} from './stadium_current_sss_exit_observation.mjs';
const requireValue = (value, message) => { if (!value) throw Error(message); };
const spans = [[1,0,144],[2,145,269],[3,270,461],[4,462,674],[1,675,806],[2,807,939]];
export function validateLaterSssExitExpected(v) {
  validateLaterStock4SssExitExpectedFields(v);
  requireValue(v.selected_occurrence === 1 && v.notification_cursor === 940 &&
    v.source.sequence === 4234 && v.source.source_tick === 133 && v.source.draw_ordinal === 192 &&
    v.prepared_profile.entry.sequence === 4235 && v.prepared_profile.setup.sequence === 4313 &&
    v.prepared_profile.entry.source_tick === 133 && v.prepared_profile.entry.draw_ordinal === 0 &&
    v.prepared_profile.entry.rng?.value_hex === '7fc17cf1' &&
    isDeepStrictEqual(v.source.rng, {pointer_slot_address:0x804d5f94,pointer_hex:'804d5f90',
      value_address:0x804d5f90,value_hex:'7fc17cf1'}) &&
    isDeepStrictEqual(v.source.rng, v.prepared_profile.entry.rng) &&
    v.menu_session_ticks === v.literal_inputs.filter(row => [1,2].includes(row.scene)).length &&
    v.menu_session_ticks === 535 && Array.isArray(v.preceding_spans) &&
    isDeepStrictEqual(v.preceding_spans.map(s => [s.scene,s.first_frame,s.last_frame]), spans) &&
    v.expected.random_seed_hex === v.source.rng.value_hex &&
    v.original_B_constructor_GO === 'UNOBSERVED',
  'Later SSS occurrence/global source context differs');
  const a = v.preceding_profile;
  requireValue(a?.match_index === 0 && a.entry?.match_index === 0 && a.setup?.match_index === 0 &&
    a.entry.sequence === 1640 && a.entry.rng?.value_hex === 'e2624ac3' &&
    a.entry.sequence < a.setup.sequence && a.setup.sequence < v.source.sequence &&
    a.setup.rng?.value_hex === '083a62a9' && a.entry.start_melee_hex === a.setup.start_melee_hex &&
    a.entry.start_melee_hex === v.prepared_profile.entry.start_melee_hex &&
    createHash('sha256').update(Buffer.from(a.entry.start_melee_hex, 'hex')).digest('hex') ===
      a.entry.start_melee_sha256 &&
    a.entry.start_melee_sha256 === '918a23d68a3b5024f94baf1f5e26e6c0db56c5fbbbffdc79c5e620e82af3b728' &&
    a.entry.start_melee_sha256 === v.prepared_profile.entry.start_melee_sha256,
  'Later SSS preceding A source identity differs');
  return v;
}
export function validateLaterSssExitStop(actual, expected, unloaded = false) {
  validateLaterSssExitExpected(expected);
  const t = actual?.terminal, c = actual?.notification_coordinates;
  requireValue(actual.observation_profile === 2 && actual.selected_occurrence === 1 &&
    actual.preceding_lifecycle_stage === 4 && actual.original_B_constructor_GO === 'UNOBSERVED' &&
    t?.complete === true && t.host_call_returned === true && t.cursor === 940 &&
    t.callback_source_steps === 0 && t.callback_source_draws === 0 &&
    t.asset_requests_after_leave_boundary === 0 && t.asset_requests_at_terminal === 0 &&
    t.waiting_for_audio_retained_until_unload === true && t.native_unloaded === unloaded &&
    c?.enclosing_host_tick_status === 'not_in_host_tick' && c.last_host_tick_call_returned === true &&
    c.callback_source_steps === 0 && c.callback_source_draws === 0,
  'Later SSS stop lost its returned occurrence1 pre-assets owner');
  return {pass: true, native_unloaded: unloaded, Entry: false, C3: false};
}
export function compareLaterSssExitObservation(actual, expected, unloaded = false) {
  validateLaterSssExitStop(actual, expected, unloaded);
  return {...compareSssExitObservationFields(actual, expected),
    preceding_A_state_comparison: 'NOT_RUN', selected_B_Entry: false, original_B_constructor_GO: 'UNOBSERVED'};
}
export function validateLaterSssExitTrace(text, expected, actual) {
  validateLaterSssExitExpected(expected);
  const result = validateSssExitTraceFields(text, expected, actual);
  const rows = text.trim().split('\n').map(line => JSON.parse(line));
  const entries = rows.filter(row => row.record === 'session_match_enter_complete');
  const frame = index => rows.find(row => row.record === 'session_frame' && row.index === index);
  requireValue(entries.length === 1 && rows.indexOf(entries[0]) > rows.indexOf(frame(269)) &&
    rows.indexOf(entries[0]) < rows.indexOf(frame(270)) &&
    entries[0].rng === parseInt(expected.preceding_profile.setup.rng.value_hex, 16) &&
    /^[0-9a-f]{1644}$/.test(entries[0].pad_state_hex),
  'Later SSS actual preceding match-entry event is missing/reordered or seed differs');
  return {...result, preceding_match_entries: 1, original_A_setup_seed: true,
    preceding_A_state_comparison: 'NOT_RUN', original_menu_draw_parity: 'UNKNOWN'};
}

// Later mode stops before B asset requests; the original 2006-frame replay remains incomplete.
export function validateLaterSssExitTerminal(report, snapshot, expected, unloaded) {
  compareLaterSssExitObservation(unloaded, expected, true);
  requireValue(!Object.hasOwn(unloaded, 'prepared_to_setup') &&
    report?.pass === false && report.complete === false &&
    report.recipe_sha256 === expected.recipe.sha256 && report.frames === expected.recipe.frames &&
    report.mode === 'state_capture' && report.scheduling_equivalence === 'not_evaluated' &&
    report.instrumented_timing_resumes === 0 && report.metrics?.sourceFrames === 940 &&
    report.metrics.sourceSteps === 940 && Number.isSafeInteger(report.metrics.sourceDraws) &&
    report.metrics.sourceDraws >= 0 &&
    isDeepStrictEqual(report.source_match, {complete:false,outcome:null,winner:null}) &&
    report.final_scene === null && isDeepStrictEqual(report.failures, [
      'whole-session final CSS was not entered', STOCK4_SSS_STOP,
      'incomplete input timeline', 'source tick/draw count mismatch']) &&
    snapshot?.phase === 0 && snapshot.running === 0 && !snapshot.runtime_error,
  'Later SSS terminal differs from the explicit global940 stop with normal unload');
  return {pass:true, runtime_replay:'FAIL_INCOMPLETE_EXPLICIT_SSS_STOP', checked_unload:true,
    literal_input_cursor:940, menu_session_ticks:535, runtime_admission:false, Entry:false, C3:false};
}
export function validateLaterSssExitFinalization(report, expected) {
  validateLaterSssExitExpected(expected);
  requireValue(!report.failure && !report.first_mismatch && !report.first_error &&
    Array.isArray(report.snapshots) && report.snapshots.length > 0 && report.snapshots.length < 2048 &&
    report.cleanup?.browser_close_returned === true,
  'Later SSS capture has unknown/saturated history or a fatal diagnostic');
  const snapshots = [...report.snapshots,report.last_successful_snapshot,report.final_snapshot].filter(v=>v!=null);
  const exit = report.current_sss_exit;
  requireValue(snapshots.every(s=>!s.snapshot_error&&!s.runtime_error) &&
    Array.isArray(report.phases) && report.phases.every(p=>p.result==='pass') &&
    report.phases.some(p=>p.name==='current-sss-exit-component') && exit?.comparison?.pass === true &&
    exit.unload_requested === true &&
    (report.later_sss_prepared_start === true ?
      (exit.prepared_start_comparison?.pass === true && !exit.prepared_start_failure &&
       exit.prepared_start_comparison.scope === 'later_sss_prepared_start_common_fields_only') :
      !exit.prepared_start_comparison) && !exit.prepared_setup_begin &&
    !exit.prepared_setup_comparison && report.original_replay_capture_result === 'fail' &&
    isDeepStrictEqual(report.finalization_failures, [
      'Whole-session replay did not complete successfully','Browser replay report is incomplete or failed']),
  'Later SSS capture contains extra failure, prepared continuation, or lacks its explicit stop');
  if(report.later_sss_prepared_start === true)
    compareLaterPreparedStartObservation(exit.prepared_start_observation,expected);
  compareLaterSssExitObservation(exit.observation,expected,false);
  validateLaterSssExitTerminal(report.browser_report,report.final_snapshot,expected,exit.unloaded_observation);
  requireValue(isDeepStrictEqual(exit.observation.note,exit.unloaded_observation.note) &&
    isDeepStrictEqual(exit.observation.notification_coordinates,exit.unloaded_observation.notification_coordinates),
  'Later SSS unload changed the retained boundary receipt');
  return {pass:true,scope:'explicit_later_SSS_stop_component_only',original_replay_capture_result:'fail',
    runtime_admission:false,Entry:false,C3:false};
}

export function compareLaterPreparedStartObservation(actual, expected) {
  validateLaterSssExitExpected(expected);
  const entry=expected.prepared_profile.entry;
  const receipt=comparePreparedStartReceiptFields(actual,entry.rng,entry.declared_setup,
    entry.start_melee_hex,{profile:2,cursor:940,order:1887});
  return {...receipt,scope:'later_sss_prepared_start_common_fields_only',
    original_entry_sequence:4235,Setup:false,original_B_constructor_GO:'UNOBSERVED'};
}
