import {LATER_REFERENCE_CONTEXTS, OBSERVED_B_REFERENCE, LONG_B_REFERENCE, laterReferenceContext} from './stadium_later_reference_contexts.mjs';
// Node-side finite occurrence1 admission. The page arm remains the shared closure-free helper.
import {createHash} from 'node:crypto';
import {isDeepStrictEqual} from 'node:util';
import {STOCK4_SSS_STOP, validateLaterStock4SssExitExpectedFields, validateObservedLaterReferenceFields, validateLongLaterReferenceFields, comparePreparedStartReceiptFields,
  comparePreparedSetupObservationFields, validateStock4PreparedSetupUnload} from './stadium_stock4_sss_exit_observation.mjs';
import {compareSssExitObservationFields, validateSssExitTraceFields} from './stadium_current_sss_exit_observation.mjs';
const requireValue = (value, message) => { if (!value) throw Error(message); };
export function validateLaterSssExitExpected(v) {
  const d=laterReferenceContext(v), observed=d===OBSERVED_B_REFERENCE, long=d===LONG_B_REFERENCE;
  if(observed)validateObservedLaterReferenceFields(v);else if(long)validateLongLaterReferenceFields(v);
  else validateLaterStock4SssExitExpectedFields(v);
  const spans=d.first.slice(0,6).map((first,i)=>[LATER_REFERENCE_CONTEXTS.scenes[i],first,d.last[i]]);
  requireValue(v.selected_occurrence === 1 && v.notification_cursor === d.first[6] &&
    v.source.sequence === d.sss_sequence && v.source.source_tick === d.sss_tick && v.source.draw_ordinal === d.sss_draw &&
    v.prepared_profile.entry.sequence === d.entry_sequence && v.prepared_profile.setup.sequence === d.setup_sequence &&
    v.prepared_profile.entry.source_tick === d.sss_tick && v.prepared_profile.entry.draw_ordinal === 0 &&
    v.prepared_profile.entry.rng?.value_hex === d.entry_seed &&
    isDeepStrictEqual(v.source.rng, {pointer_slot_address:0x804d5f94,pointer_hex:'804d5f90',
      value_address:0x804d5f90,value_hex:d.entry_seed}) &&
    isDeepStrictEqual(v.source.rng, v.prepared_profile.entry.rng) &&
    v.menu_session_ticks === v.literal_inputs.filter(row => [1,2].includes(row.scene)).length &&
    v.menu_session_ticks === d.menu_ticks && Array.isArray(v.preceding_spans) &&
    isDeepStrictEqual(v.preceding_spans.map(s => [s.scene,s.first_frame,s.last_frame]), spans) &&
    v.expected.random_seed_hex === v.source.rng.value_hex &&
    v.original_B_constructor_GO === d.original_B_constructor_GO,
  'Later SSS occurrence/global source context differs');
  const a = v.preceding_profile;
  requireValue(a?.match_index === 0 && a.entry?.match_index === 0 && a.setup?.match_index === 0 &&
    a.entry.sequence === d.a_entry_sequence && a.entry.rng?.value_hex === LATER_REFERENCE_CONTEXTS.a_entry_seed &&
    a.entry.sequence < a.setup.sequence && a.setup.sequence < v.source.sequence &&
    a.setup.rng?.value_hex === LATER_REFERENCE_CONTEXTS.a_setup_seed && a.entry.start_melee_hex === a.setup.start_melee_hex &&
    a.entry.start_melee_hex === v.prepared_profile.entry.start_melee_hex &&
    createHash('sha256').update(Buffer.from(a.entry.start_melee_hex, 'hex')).digest('hex') ===
      a.entry.start_melee_sha256 &&
    a.entry.start_melee_sha256 === LATER_REFERENCE_CONTEXTS.start_sha256 &&
    a.entry.start_melee_sha256 === v.prepared_profile.entry.start_melee_sha256,
  'Later SSS preceding A source identity differs');
  if(observed||long)requireValue(v.recipe.sha256===d.recipe_sha256 && v.recipe.bytes===d.recipe_bytes &&
    a.setup.sequence===d.a_setup_sequence && a.entry.source_tick===d.a_tick && a.setup.source_tick===d.a_tick &&
    v.prepared_profile.setup.source_tick===d.sss_tick && v.prepared_profile.setup.draw_ordinal===0 &&
    v.prepared_profile.setup.rng.value_hex===d.setup_seed &&
    v.reference_order_contract==='captured_native_complete_trace_not_original_draw_ordinal',
    'New later reference recipe/phase witnesses differ');
  return v;
}
// Fixed v8 transport from whole_session_replay._encode_whole_session and
// gameplay_retail_recipe.cpp: context, Setup, initial PAD, inputs, then spans.
export function validateLaterReferenceRecipeInputs(bytes,expected){
  validateLaterSssExitExpected(expected);
  const d=laterReferenceContext(expected);
  requireValue((d===OBSERVED_B_REFERENCE||d===LONG_B_REFERENCE) && Buffer.isBuffer(bytes) && bytes.length===d.recipe_bytes,
    'New later reference raw recipe transport size differs');
  const headerBytes=20, contextHeaderBytes=8;
  const contextBytes=0x18+0x55e8+0x148+6, setupBytes=0x138, initialPadBytes=822;
  const frameBytes=4*11, spanBytes=12;
  const first=headerBytes+contextHeaderBytes+contextBytes+setupBytes+initialPadBytes;
  const trailer=first+d.frames*frameBytes;
  requireValue(bytes.subarray(0,4).toString('ascii')==='MWRC' && bytes.readUInt32BE(4)===8 &&
    bytes.readUInt32BE(12)===d.frames && bytes.readUInt16BE(headerBytes)===2 &&
    bytes.readUInt16BE(headerBytes+2)===0 && bytes.readUInt32BE(headerBytes+4)===contextBytes &&
    trailer+2<=bytes.length,
    'New later reference v8 header/context/count differs');
  const count=bytes.readUInt16BE(trailer);
  requireValue(count===d.first.length && count>=1 && count<=32 &&
    bytes.length===trailer+2+count*spanBytes,
    'New later reference v8 span trailer size/count differs');
  for(let i=0,next=0;i<count;++i){
    const at=trailer+2+i*spanBytes, firstFrame=bytes.readUInt32BE(at+4), lastFrame=bytes.readUInt32BE(at+8);
    requireValue(bytes[at]===LATER_REFERENCE_CONTEXTS.scenes[i] && bytes[at+1]===0 &&
      bytes.readUInt16BE(at+2)===0 && firstFrame===next && firstFrame===d.first[i] &&
      lastFrame===d.last[i] && lastFrame>=firstFrame && lastFrame<d.frames &&
      (i!==count-1 || lastFrame+1===d.frames),
      'New later reference v8 span flags/topology/coverage differs');
    next=lastFrame+1;
  }
  requireValue(createHash('sha256').update(bytes).digest('hex')===d.recipe_sha256,
    'New later reference raw recipe identity differs');
  for(const row of expected.literal_inputs)
    requireValue(bytes.subarray(first+row.index*frameBytes,first+(row.index+1)*frameBytes).toString('hex')===row.pads.join(''),
      'New later reference literal four-port prefix differs from exact recipe');
  return true;
}
function checkedLaterOrder(actual,expected){
  const d=laterReferenceContext(expected), order=actual?.notification_coordinates?.order;
  if(d===OBSERVED_B_REFERENCE||d===LONG_B_REFERENCE)requireValue(actual?.reference_descriptor===d.id &&
    Number.isSafeInteger(order)&&order>=0&&actual.notification_order_count===order+1,
    'New later reference notification owner/order receipt differs');
  else return d.notification_order; // Existing legacy consumers retain their own fixed-order checks.
  return order;
}
export function validateLaterSssExitStop(actual, expected, unloaded = false) {
  validateLaterSssExitExpected(expected);
  const d=laterReferenceContext(expected);
  checkedLaterOrder(actual,expected);
  const t = actual?.terminal, c = actual?.notification_coordinates;
  requireValue(actual.observation_profile === d.profile && actual.selected_occurrence === 1 &&
    actual.preceding_lifecycle_stage === 4 && actual.original_B_constructor_GO === d.original_B_constructor_GO &&
    t?.complete === true && t.host_call_returned === true && t.cursor === d.first[6] &&
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
    preceding_A_state_comparison: 'NOT_RUN', selected_B_Entry: false, original_B_constructor_GO: laterReferenceContext(expected).original_B_constructor_GO};
}
export function validateLaterSssExitTrace(text, expected, actual) {
  validateLaterSssExitExpected(expected);
  const result = validateSssExitTraceFields(text, expected, actual);
  const rows = text.trim().split('\n').map(line => JSON.parse(line));
  const d=laterReferenceContext(expected);
  if(d===OBSERVED_B_REFERENCE||d===LONG_B_REFERENCE){
    checkedLaterOrder(actual,expected);
    const order=rows.filter(row=>row.record==='current_sss_exit_order');
    requireValue(order.every((row,i)=>i===0||row.replay_cursor>=order[i-1].replay_cursor),
      'New later reference actual event cursors are stale/reordered');
    const frames=rows.filter(row=>row.record==='session_frame');
    const ticks=order.filter(row=>row.kind==='tick_return');
    requireValue(ticks.every((row,i)=>rows.indexOf(frames[i])<rows.indexOf(row) &&
      (i+1===frames.length || rows.indexOf(row)<rows.indexOf(frames[i+1]))),
      'New later reference frame/tick-return brackets are missing/reordered');
  }
  const entries = rows.filter(row => row.record === 'session_match_enter_complete');
  const frame = index => rows.find(row => row.record === 'session_frame' && row.index === index);
  requireValue(entries.length === 1 && rows.indexOf(entries[0]) > rows.indexOf(frame(d.first[2]-1)) &&
    rows.indexOf(entries[0]) < rows.indexOf(frame(d.first[2])) &&
    entries[0].rng === parseInt(expected.preceding_profile.setup.rng.value_hex, 16) &&
    /^[0-9a-f]{1644}$/.test(entries[0].pad_state_hex),
  'Later SSS actual preceding match-entry event is missing/reordered or seed differs');
  return {...result, preceding_match_entries: 1, original_A_setup_seed: true,
    preceding_A_state_comparison: 'NOT_RUN', original_menu_draw_parity: 'UNKNOWN'};
}

// Later mode stops before B asset requests; the original 2006-frame replay remains incomplete.
export function validateLaterSssExitTerminal(report, snapshot, expected, actual, preparedSetupUnload = null) {
  const d=laterReferenceContext(expected);
  const unloaded = preparedSetupUnload || actual;
  if (preparedSetupUnload) {
    compareLaterSssExitObservation(actual, expected, false);
    validateStock4PreparedSetupUnload(actual, unloaded);
  } else compareLaterSssExitObservation(unloaded, expected, true);
  requireValue((preparedSetupUnload || !Object.hasOwn(unloaded, 'prepared_to_setup')) &&
    report?.pass === false && report.complete === false &&
    report.recipe_sha256 === expected.recipe.sha256 && report.frames === expected.recipe.frames &&
    report.mode === 'state_capture' && report.scheduling_equivalence === 'not_evaluated' &&
    report.instrumented_timing_resumes === 0 && report.metrics?.sourceFrames === d.first[6] &&
    report.metrics.sourceSteps === d.first[6] && Number.isSafeInteger(report.metrics.sourceDraws) &&
    report.metrics.sourceDraws >= 0 &&
    isDeepStrictEqual(report.source_match, {complete:false,outcome:null,winner:null}) &&
    report.final_scene === null && isDeepStrictEqual(report.failures, [
      'whole-session final CSS was not entered', STOCK4_SSS_STOP,
      'incomplete input timeline', 'source tick/draw count mismatch']) &&
    snapshot?.phase === 0 && snapshot.running === 0 && !snapshot.runtime_error,
  'Later SSS terminal differs from the explicit global940 stop with normal unload');
  return {pass:true, runtime_replay:'FAIL_INCOMPLETE_EXPLICIT_SSS_STOP', checked_unload:true,
    literal_input_cursor:d.first[6], menu_session_ticks:d.menu_ticks, runtime_admission:false, Entry:false, C3:false};
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
      !exit.prepared_start_comparison) &&
    (report.later_sss_prepared_setup === true ?
      (report.later_sss_prepared_start === true && exit.prepared_setup_begin?.result === 1 &&
       exit.prepared_setup_begin.accepted_receipt_rechecked === true &&
       exit.prepared_setup_comparison?.pass === true && !exit.prepared_setup_failure) :
      (!exit.prepared_setup_begin && !exit.prepared_setup_comparison)) &&
    report.original_replay_capture_result === 'fail' &&
    isDeepStrictEqual(report.finalization_failures, [
      'Whole-session replay did not complete successfully','Browser replay report is incomplete or failed']),
  'Later SSS capture contains extra failure, prepared continuation, or lacks its explicit stop');
  if(report.later_sss_prepared_start === true)
    compareLaterPreparedStartObservation(exit.prepared_start_observation,expected,exit.observation);
  compareLaterSssExitObservation(exit.observation,expected,false);
  if(report.later_sss_prepared_setup === true)
    compareLaterPreparedSetupObservation(exit.prepared_setup_observation,expected);
  validateLaterSssExitTerminal(report.browser_report,report.final_snapshot,expected,
    report.later_sss_prepared_setup === true ? exit.observation : exit.unloaded_observation,
    report.later_sss_prepared_setup === true ? exit.unloaded_observation : null);
  requireValue(isDeepStrictEqual(exit.observation.note,exit.unloaded_observation.note) &&
    isDeepStrictEqual(exit.observation.notification_coordinates,exit.unloaded_observation.notification_coordinates),
  'Later SSS unload changed the retained boundary receipt');
  return {pass:true,scope:'explicit_later_SSS_stop_component_only',original_replay_capture_result:'fail',
    runtime_admission:false,Entry:false,C3:false};
}

export function compareLaterPreparedStartObservation(actual, expected, stoppedObservation = null) {
  validateLaterSssExitExpected(expected);
  const d=laterReferenceContext(expected);
  const entry=expected.prepared_profile.entry;
  const observed=d===OBSERVED_B_REFERENCE||d===LONG_B_REFERENCE;
  const order=observed ? checkedLaterOrder(stoppedObservation,expected) : d.notification_order;
  if(observed)compareLaterSssExitObservation(stoppedObservation,expected,false);
  const receipt=comparePreparedStartReceiptFields(actual,entry.rng,entry.declared_setup,
    entry.start_melee_hex,{profile:d.profile,cursor:d.first[6],order});
  return {...receipt,scope:'later_sss_prepared_start_common_fields_only',
    original_entry_sequence:d.entry_sequence,Setup:false,original_B_constructor_GO:d.original_B_constructor_GO};
}

export function compareLaterPreparedSetupObservation(actual, expected) {
  validateLaterSssExitExpected(expected);
  compareSssExitObservationFields(actual, expected);
  const d=laterReferenceContext(expected);
  const order=checkedLaterOrder(actual,expected);
  requireValue(actual?.observation_profile === d.profile && actual.selected_occurrence === 1 &&
    actual.preceding_lifecycle_stage === 4 && actual.terminal?.cursor === d.first[6] &&
    actual.notification_coordinates?.replay_cursor === d.first[6] &&
    actual.notification_coordinates?.order === order && actual.note?.session_ticks === d.menu_ticks &&
    actual.note.world_generation === actual.prepared_to_setup?.setup?.prior_world_generation &&
    actual.notification_coordinates?.callback_source_steps === 0 &&
    actual.notification_coordinates?.callback_source_draws === 0,
  'Later prepared Setup lost its occurrence1 source boundary');
  const result = comparePreparedSetupObservationFields(actual, expected, d.handoff_origin);
  return {...result, scope:'later_sss_zero_action_setup_common_fields_and_seed_only',
    original_setup_sequence:d.setup_sequence, original_B_constructor_GO:d.original_B_constructor_GO};
}
