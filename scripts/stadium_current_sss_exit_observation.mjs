import {compareCurrentSssExitCommonFields} from '../tests/stadium_first_css_stream_compare.mjs';

export const CURRENT_SSS_EXIT_REFUSAL = 'Original VS entry produced an unsupported Stadium diagnostic payload';
const own = (v, keys) => v && typeof v === 'object' && !Array.isArray(v) &&
  Object.keys(v).sort().join('|') === [...keys].sort().join('|');
const hex = (v, bytes) => typeof v === 'string' && new RegExp(`^[0-9a-f]{${bytes * 2}}$`).test(v);
const requireValue = (v, message) => { if (!v) throw Error(message); };

export function continuePreparedStartInPage(scope, acceptedReceipt) {
  return scope.continuePreparedStart(acceptedReceipt);
}

// Passed as the actual page function; deliberately has no module-scope closure.
// Controls use a synthetic Module solely for call ordering/ownership semantics.
export function installCurrentSssExitReplayArm({recipeBytes, observationProfile = 0, preparedStartProbe = false, laterPreparedSetup = false}) {
  if (laterPreparedSetup && (![2,3].includes(observationProfile) || !preparedStartProbe))
    throw Error('Later prepared Setup requires its explicit profile2 prepared-Start probe');
  if (observationProfile === 3 && (!laterPreparedSetup || !preparedStartProbe))
    throw Error('Private profile3 new later reference requires the checked Setup stop');
  const module = globalThis.Module;
  const key = '_melee_web_native_menu_replay';
  const original = Object.getOwnPropertyDescriptor(module || {}, key);
  if (observationProfile !== 0 && observationProfile !== 1 && observationProfile !== 2 && observationProfile !== 3) throw Error('Unknown private SSS observation profile');
  const armKey = observationProfile !== 0 ? '_melee_web_native_menu_stadium_current_sss_exit_arm_profile' : '_melee_web_native_menu_stadium_current_sss_exit_arm';
  const arm = module?.[armKey];
  const observe = module?._melee_web_native_menu_stadium_current_sss_exit_observe;
  const preparedStartObserve = ([1,2,3].includes(observationProfile)) && preparedStartProbe
    ? module?._melee_web_native_menu_stadium_current_sss_prepared_start_observe : null;
  const preparedSetupBegin = (observationProfile === 1 || laterPreparedSetup) && preparedStartProbe
    ? module?._melee_web_native_menu_stadium_current_sss_prepared_setup_begin : null;
  const idle = module?.stadiumMatchedSetupNativeIdle;
  if (!original || !Object.hasOwn(original, 'value') ||
      typeof original.value !== 'function' || !original.writable ||
      typeof arm !== 'function' || typeof observe !== 'function' ||
      (preparedStartProbe && (![1,2,3].includes(observationProfile) || typeof preparedStartObserve !== 'function')) ||
      (laterPreparedSetup && typeof preparedSetupBegin !== 'function') ||
      typeof idle !== 'function' || typeof module.UTF8ToString !== 'function' ||
      !Array.isArray(recipeBytes) || recipeBytes.length < 328 ||
      recipeBytes.length > 128 * 1024 * 1024 ||
      recipeBytes.some(v => !Number.isInteger(v) || v < 0 || v > 255))
    throw Error('Current SSS diagnostic requires owned replay/private exports and literal recipe bytes');
  const bytes = Uint8Array.from(recipeBytes);
  const state = {calls: 0, armed: false, disposed: false, failure: null};
  let installed;
  const owns = () => {
    const now = Object.getOwnPropertyDescriptor(module, key);
    return globalThis.Module === module && now && now.value === installed.value &&
      now.writable === installed.writable && now.enumerable === installed.enumerable &&
      now.configurable === installed.configurable &&
      module[armKey] === arm &&
      module._melee_web_native_menu_stadium_current_sss_exit_observe === observe &&
      (!preparedStartProbe ||
        module._melee_web_native_menu_stadium_current_sss_prepared_start_observe === preparedStartObserve) &&
      (!preparedSetupBegin ||
        module._melee_web_native_menu_stadium_current_sss_prepared_setup_begin === preparedSetupBegin) &&
      module.stadiumMatchedSetupNativeIdle === idle;
  };
  const stableJson = value => {
    if (Array.isArray(value)) return `[${value.map(stableJson).join(',')}]`;
    if (value && typeof value === 'object') return `{${Object.keys(value).sort().map(key =>
      `${JSON.stringify(key)}:${stableJson(value[key])}`).join(',')}}`;
    return JSON.stringify(value);
  };
  let preparedSetupAttempted = false;
  let gate2Attempted = false, gate2Token = null, gate2Finalized = false, gate2ApprovalCalls = 0;
  let gate2AcceptedJson = null;
  const ordinaryObservation = () => {
    if (gate2Attempted) throw Error('Gate2 reservation forbids ordinary current SSS native access');
  };
  const gate2Owned = () => {
    if (!gate2Token || gate2Finalized || state.disposed || !owns())
      throw Error('Gate2 adapter token is stale, finalized or foreign');
  };
  function wrapped(...args) {
    if (state.disposed || state.calls || !owns())
      throw Error('Current SSS replay callback ownership/reuse refused');
    ++state.calls;
    // Never catch/rewrite the original error or alter this/arguments/return.
    const result = Reflect.apply(original.value, this, args);
    if (result !== 1) return result;
    try {
      if (!owns() || args.length !== 3 || args[2] !== 1 ||
          !Number.isSafeInteger(args[0]) || args[0] < 0 || args[1] !== bytes.length ||
          !(module.HEAPU8 instanceof Uint8Array) || args[0] + bytes.length > module.HEAPU8.length ||
          !bytes.every((v, i) => module.HEAPU8[args[0] + i] === v) || idle() !== true)
        throw Error('Current SSS diagnostic parsed a foreign recipe or lost its returned idle owner');
      if (Reflect.apply(arm, module, observationProfile !== 0 ? [observationProfile] : []) !== 1)
        throw Error('Current SSS diagnostic native arm refused; ordinary launch suppressed');
      if (!owns()) throw Error('Current SSS native arm changed replay callback ownership');
      state.armed = true;
      return result;
    } catch (error) {
      state.failure = String(error.message || error);
      throw error; // Diagnostic failure explicitly aborts UI before launch.
    }
  }
  installed = {...original, value: wrapped};
  Object.defineProperty(module, key, installed);
  return {
    state() { return {...state, owned: owns()}; },
    observe() {
      ordinaryObservation();
      if (state.disposed || !owns()) throw Error('Current SSS observation lost its retained Module/callback');
      if (idle() !== true) return {busy: true};
      const pointer = Reflect.apply(observe, module, []);
      if (!pointer) throw Error('Current SSS native observation pointer is absent');
      return {busy: false, value: JSON.parse(module.UTF8ToString(pointer))};
    },
    observePreparedStart() {
      ordinaryObservation();
      if (!preparedStartProbe || ![1,2,3].includes(observationProfile) || !preparedStartObserve)
        throw Error('Prepared Start observation is available only to an explicit checked prepared-Start profile');
      if (!state.armed || state.disposed || !owns())
        throw Error('Prepared Start observation lost its armed retained Module/callback');
      if (idle() !== true) return {busy: true};
      const pointer = Reflect.apply(preparedStartObserve, module, []);
      if (!pointer) throw Error('Prepared Start native observation pointer is absent');
      return {busy: false, value: JSON.parse(module.UTF8ToString(pointer))};
    },
    continuePreparedStart(acceptedReceipt) {
      ordinaryObservation();
      if (!preparedStartProbe || !preparedStartObserve || typeof preparedSetupBegin !== 'function' ||
          preparedSetupAttempted || state.disposed || !state.armed || !owns() || idle() !== true)
        throw Error('Prepared Setup continuation lacks its same-run one-use owner');
      preparedSetupAttempted = true;
      const pointer = Reflect.apply(preparedStartObserve, module, []);
      if (!pointer || stableJson(JSON.parse(module.UTF8ToString(pointer))) !== stableJson(acceptedReceipt))
        throw Error('Prepared Setup continuation receipt differs from the accepted comparison input');
      const approvalName = '__meleeWebStadiumCurrentSssPreparedStartApproved';
      if (Object.hasOwn(globalThis, approvalName))
        throw Error('Prepared Setup approval callback already exists');
      const acceptedJson = stableJson(acceptedReceipt);
      let approvalCalls = 0;
      const approved = () => {
        if (++approvalCalls !== 1 || state.disposed || !owns() || globalThis.Module !== module ||
            idle() !== true) return false;
        const current = Reflect.apply(preparedStartObserve, module, []);
        return !!current && stableJson(JSON.parse(module.UTF8ToString(current))) === acceptedJson;
      };
      Object.defineProperty(globalThis, approvalName, {value: approved, configurable: true});
      try {
        if (!owns() || globalThis[approvalName] !== approved)
          throw Error('Prepared Setup approval lost its current SSS Module owner');
        const result = Reflect.apply(preparedSetupBegin, module, []);
        if (result !== 1) throw Error('Native prepared Setup handoff refused');
        return {result, accepted_receipt_rechecked: true};
      } finally {
        if (globalThis[approvalName] === approved) delete globalThis[approvalName];
      }
    },
    reserveLaterBTransfer(acceptedObservation, request) {
      if (observationProfile !== 3 || !laterPreparedSetup || !preparedSetupAttempted ||
          gate2Attempted || state.disposed || !state.armed || !owns())
        throw Error('Gate2 reservation requires the one-use profile3 completed Setup owner');
      gate2Attempted = true; // Even a lost reservation response closes ordinary getters.
      if (idle() !== true) throw Error('Gate2 reservation requires settled Asyncify');
      const pointer = Reflect.apply(observe, module, []);
      const actual = pointer ? JSON.parse(module.UTF8ToString(pointer)) : null;
      if (!actual || stableJson(actual) !== stableJson(acceptedObservation) ||
          actual.reference_descriptor !== 'original-b-2013-943-v1' ||
          actual.prepared_to_setup?.setup?.complete !== true ||
          actual.prepared_to_setup.setup.captured !== true ||
          actual.prepared_to_setup.setup.stadium_setup_state?.world_generation !== request?.world_generation ||
          actual.prepared_to_setup.setup.actual?.random_seed_hex !== request?.setup_seed_hex ||
          actual.notification_coordinates?.order !== request?.notification_order ||
          actual.notification_coordinates?.replay_cursor !== 943 || request?.cursor !== 943)
        throw Error('Gate2 reservation changed the approved actual Setup/context');
      gate2AcceptedJson = stableJson(actual.prepared_to_setup.setup);
      if (typeof window.menuReplayReserveLaterBTransfer !== 'function')
        throw Error('Gate2 shell reservation service is unavailable');
      gate2Token = window.menuReplayReserveLaterBTransfer(request);
      if (!gate2Token || typeof gate2Token.id !== 'string')
        throw Error('Gate2 shell reservation returned no opaque same-page token');
      return {id: gate2Token.id}; // The opaque object itself never crosses page serialization.
    },
    gate2CanSettle() {
      gate2Owned();
      const evidence = window.menuReplayReadLaterBTransferEvidence(gate2Token);
      return evidence.uncertain !== true && evidence.core?.phase === 'pending';
    },
    gate2Pending(kind) { gate2Owned(); window.menuReplayLaterBCommandPending(gate2Token, kind); },
    gate2Settled() { gate2Owned(); window.menuReplayLaterBCommandSettled(gate2Token); },
    gate2Uncertain(reason) { gate2Owned(); window.menuReplayLaterBCommandUncertain(gate2Token, reason); },
    gate2ApproveSetup() {
      gate2Owned();
      return ++gate2ApprovalCalls === 1 && gate2AcceptedJson !== null &&
        typeof gate2Token.id === 'string'; // Pure approved-receipt vote; native independently checks current owner.
    },
    gate2Evidence() { gate2Owned(); return window.menuReplayReadLaterBTransferEvidence(gate2Token); },
    gate2Record(evidence) { gate2Owned(); window.menuReplayRecordLaterBAction(gate2Token, evidence); },
    async gate2Finalize() {
      gate2Owned();
      const evidence = window.menuReplayReadLaterBTransferEvidence(gate2Token);
      await window.menuReplayFinalizeLaterBTransfer(gate2Token);
      gate2Finalized = true;
      return {finalized: true, evidence, replay_report: window.lastRetailReplayReport};
    },
    dispose() {
      if (gate2Attempted && !gate2Finalized)
        throw Error('Gate2 callback restoration requires acknowledged finalized native close');
      if (state.disposed || !owns()) throw Error('Current SSS callback disposal refuses foreign replacement');
      Object.defineProperty(module, key, original);
      state.disposed = true;
      return true;
    },
  };
}

export function validateCurrentSssExitExpected(v) {
  requireValue(v?.schema === 'melee-web-current-sss-exit-host-expectation-v1' &&
    v.menu_draw_reference === 'UNKNOWN' && v.Entry === false && v.C3 === false &&
    v.expected_runtime_refusal === CURRENT_SSS_EXIT_REFUSAL &&
    v.recipe?.version === 8 && v.recipe.frames === 2013 &&
    Number.isSafeInteger(v.recipe.bytes) && v.recipe.bytes >= 328 && hex(v.recipe.sha256, 32) &&
    Number.isSafeInteger(v.notification_cursor) && v.notification_cursor > 0 &&
    Array.isArray(v.literal_inputs) && v.literal_inputs.length === v.notification_cursor,
  'Current SSS expectation scope/recipe/input shape differs');
  return validateSssExitExpectedFields(v);
}

export function validateSssExitExpectedFields(v, laterOccurrence = false, referenceSpans = null) {
  // The caller for laterOccurrence is the explicit finite occurrence1 validator.
  const spans = laterOccurrence ? (referenceSpans || [[0,144,1],[145,269,2],[270,461,3],[462,674,4],
    [675,806,1],[807,939,2]]) : null;
  for (const [index, row] of v.literal_inputs.entries())
    requireValue(row.index === index && (laterOccurrence
      ? spans.some(([first,last,scene]) => index >= first && index <= last && row.scene === scene)
      : [1, 2].includes(row.scene)) &&
      Array.isArray(row.pads) && row.pads.length === 4 && row.pads.every(p => hex(p, 11)),
    'Current SSS literal four-port input is missing/reordered/malformed');
  requireValue(v.literal_inputs[0].scene === 1 && v.literal_inputs.at(-1).scene === 2 &&
    v.source?.boundary === 'sss_exit' && v.source.pc === 0x8025bbd0 && v.source.match_index === (laterOccurrence ? 1 : 0) &&
    hex(v.source.raw_start_melee_hex, 240) && hex(v.primary_sha256, 32) &&
    v.expected?.phase === 'sss_exit' && v.expected.scene_kind === 9 &&
    Number.isSafeInteger(v.expected.scene_frame) && v.expected.scene_frame >= 0 &&
    hex(v.expected.random_seed_hex, 4) && hex(v.expected.pad_state_hex, 822) &&
    v.expected.sss?.header?.start_game === 1,
  'Current SSS expected source endpoint is malformed');
  return v;
}

export function compareCurrentSssExitObservation(actual, expected) {
  validateCurrentSssExitExpected(expected);
  return compareSssExitObservationFields(actual, expected);
}

export function compareSssExitObservationFields(actual, expected) {
  const later = (expected.schema === 'melee-web-stock4-none-later-sss-exit-host-expectation-v1' &&
    expected.observation_profile === 2) || (expected.schema === 'melee-web-stock4-none-later-sss-reference-expectation-v1' && expected.observation_profile === 3);
  requireValue(actual?.scope === 'passive_sss_onexit_return_only' &&
    actual.armed === true && actual.notified === true && actual.failed === false &&
    actual.error === null && actual.notification_received === true && actual.observation_state === 2 &&
    actual.menu_draw_reference === 'UNKNOWN' && actual.runtime_admission === false &&
    actual.Entry === false && actual.C3 === false,
  'Current SSS passive receipt did not capture its checked boundary');
  const c = actual.notification_coordinates;
  requireValue(c?.replay_cursor === expected.notification_cursor &&
    ['callback_source_steps', 'callback_source_draws', 'order'].every(k => Number.isSafeInteger(c[k]) && c[k] >= 0) &&
    ['in_flight_not_returned', 'not_in_host_tick'].includes(c.enclosing_host_tick_status) &&
    typeof c.last_host_tick_call_returned === 'boolean' &&
    (c.enclosing_host_tick_status !== 'in_flight_not_returned' || c.last_host_tick_call_returned === false),
  'Current SSS notification cursor/actual callback coordinates differ');
  const note = actual.note, want = expected.expected;
  requireValue(note?.phase === 'sss_exit' && note.host_entered === true &&
    note.session_phase === 3 && note.source_scene === 2 &&
    note.scene_kind === want.scene_kind && note.scene_frame === want.scene_frame &&
    note.random_seed_hex === want.random_seed_hex && note.pad_state_hex === want.pad_state_hex &&
    Number.isSafeInteger(note.session_ticks) && note.session_ticks === (later
      ? expected.menu_session_ticks : expected.notification_cursor) &&
    ['world_generation', 'audio_generation'].every(k => Number.isSafeInteger(note[k]) && note[k] > 0),
  'Current SSS scalar/PAD/seed/lifetime note differs');
  const ownerKeys = ['host', 'session', 'world', 'audio', 'vs_mode', 'scene_info', 'payload', 'seed'];
  requireValue(own(note.owners, ownerKeys) && ownerKeys.every(k => note.owners[k] === true),
    'Current SSS native owner check is absent/refused');
  requireValue(note.sss?.header?.start_game === want.sss.header.start_game &&
    actual.native_sss_bytes_scope === 'native_ABI_only_not_original_raw_equivalence' &&
    typeof actual.native_sss_bytes_hex === 'string' && /^(?:[0-9a-f]{2})+$/.test(actual.native_sss_bytes_hex),
  'Current SSS start-game/native ABI receipt differs');
  const difference = compareCurrentSssExitCommonFields(note, want);
  requireValue(!difference, `Current SSS source field differs: ${JSON.stringify(difference)}`);
  return {pass: true, scope: later ? 'retained_occurrence1_sss_exit_common_fields_only' : 'retained_first_sss_exit_common_fields_only',
    original_coordinate: {sequence: expected.source.sequence, source_tick: expected.source.source_tick,
      draw_ordinal: expected.source.draw_ordinal}, notification_coordinates: c,
    menu_draw_reference: 'UNKNOWN', unpaired: expected.unpaired, Entry: false, C3: false};
}

export function validateCurrentSssExitTrace(text, expected, actual) {
  validateCurrentSssExitExpected(expected);
  return validateSssExitTraceFields(text, expected, actual);
}

export function validateSssExitTraceFields(text, expected, actual) {
  const later = (expected.schema === 'melee-web-stock4-none-later-sss-exit-host-expectation-v1' &&
    expected.observation_profile === 2) || (expected.schema === 'melee-web-stock4-none-later-sss-reference-expectation-v1' && expected.observation_profile === 3);
  const rows = text.trim().split('\n').map(line => JSON.parse(line));
  requireValue(rows.every(r => ['header', 'session_frame', 'current_sss_exit_order',
    ...(later ? ['session_match_enter_complete'] : [])].includes(r.record)),
    'Current SSS trace contains an unexpected native record');
  const headers = rows.filter(r => r.record === 'header');
  requireValue(headers.length === 1 && headers[0].schema === 'melee-web-port-session-diagnostic' &&
    headers[0].version === 1 && headers[0].frames_requested === expected.recipe.frames &&
    headers[0].draw_state === 'not_captured' && headers[0].comparison === 'not_run',
    'Current SSS native trace header differs');
  const frames = rows.filter(r => r.record === 'session_frame');
  requireValue(frames.length === expected.notification_cursor, 'Current SSS trace input timeline is incomplete/extra');
  for (const [i, row] of frames.entries()) {
    const want = expected.literal_inputs[i];
    requireValue(row.index === i && row.scene === want.scene && hex(row.pad_state_hex, 822) &&
      Number.isInteger(row.rng) && row.rng >= 0 && row.rng <= 0xffffffff &&
      JSON.stringify(row.supplied_inputs) === JSON.stringify(want.pads),
    'Current SSS actual replay input order/scene/four-port bytes differ');
  }
  const order = rows.filter(r => r.record === 'current_sss_exit_order');
  requireValue(order.length === actual.notification_coordinates.order && order.every((r, i) =>
    r.order === i && r.menu_draw_reference === 'UNKNOWN' &&
    ['tick_return', 'draw_aurora_return'].includes(r.kind) &&
    Number.isSafeInteger(r.replay_cursor) && r.replay_cursor >= 0 &&
    r.replay_cursor <= expected.notification_cursor &&
    Number.isSafeInteger(r.source_steps) && r.source_steps >= 0 &&
    Number.isSafeInteger(r.source_draws) && r.source_draws >= 0),
  'Current SSS actual callback order receipt is missing/reordered');
  const ticks = order.filter(r => r.kind === 'tick_return');
  requireValue(ticks.length === frames.length && ticks.every((r, i) => r.replay_cursor === i &&
    rows.indexOf(r) > rows.indexOf(frames[i])), 'Current SSS tick-return order differs from actual supplied input rows');
  return {pass: true, literal_supplied_inputs: frames.length, local_order_rows: order.length,
    original_menu_draw_parity: 'UNKNOWN'};
}

export function validateCurrentSssExitTerminal(replayReport, finalSnapshot, expected) {
  const required = ['whole-session final CSS was not entered', CURRENT_SSS_EXIT_REFUSAL,
    'incomplete input timeline', 'source tick/draw count mismatch'];
  requireValue(replayReport?.pass === false && replayReport.complete === false &&
    replayReport.frames === expected.recipe.frames &&
    replayReport.mode === 'state_capture' && replayReport.scheduling_equivalence === 'not_evaluated' &&
    replayReport.instrumented_timing_resumes === 0 &&
    replayReport.metrics?.sourceFrames === expected.notification_cursor &&
    replayReport.metrics.sourceSteps === expected.notification_cursor &&
    Number.isSafeInteger(replayReport.metrics.sourceDraws) && replayReport.metrics.sourceDraws >= 0 &&
    own(replayReport.source_match, ['complete', 'outcome', 'winner']) &&
    replayReport.source_match.complete === false && replayReport.source_match.outcome === null &&
    replayReport.source_match.winner === null && replayReport.final_scene === null &&
    Array.isArray(replayReport.failures) && replayReport.failures.length === required.length &&
    required.every((v, i) => replayReport.failures[i] === v) &&
    finalSnapshot?.phase === 0 && finalSnapshot.running === 0,
  'Current SSS terminal is not the unchanged TIME refusal with checked unload');
  return {pass: true, runtime_replay: 'FAIL_INCOMPLETE_EXPECTED_TIME_REFUSAL',
    checked_unload: true, runtime_admission: false, Entry: false, C3: false};
}

// Additive component classification only. The generic whole-replay finalizer
// and its retained first_error/failures remain unchanged.
export function validateCurrentSssExitFinalization(report, expected) {
  validateCurrentSssExitExpected(expected);
  const first = report.first_error;
  const expectedFirst = first?.kind === 'runtime' &&
    first.message === CURRENT_SSS_EXIT_REFUSAL && first.phase === 'current-sss-exit-component';
  requireValue(!report.failure && !report.first_mismatch && (!first || expectedFirst),
    'Current SSS finalization contains an unexpected fatal diagnostic');
  // The production snapshot store stops appending at 2048. Saturation or
  // missing history cannot establish absence of a later different diagnostic.
  requireValue(Array.isArray(report.snapshots) && report.snapshots.length > 0 &&
    report.snapshots.length < 2048 && report.cleanup?.browser_close_returned === true,
    'Current SSS snapshot history is unknown/saturated or browser cleanup incomplete');
  const snapshots = [...report.snapshots, report.last_successful_snapshot, report.final_snapshot]
    .filter(value => value != null);
  requireValue(snapshots.length > 0 && snapshots.every(value => !value.snapshot_error &&
    (!value.runtime_error || value.runtime_error === CURRENT_SSS_EXIT_REFUSAL)) &&
    (!expectedFirst || snapshots.some(value => value.runtime_error === CURRENT_SSS_EXIT_REFUSAL)),
    'Current SSS finalization contains a missing or later different runtime observation');
  requireValue(Array.isArray(report.phases) && report.phases.length > 0 &&
    report.phases.every(value => value.result === 'pass') &&
    report.phases.some(value => value.name === 'current-sss-exit-component'),
    'Current SSS component phase did not finish cleanly');
  validateCurrentSssExitTerminal(report.browser_report, report.final_snapshot, expected);
  const failures = ['Whole-session replay did not complete successfully',
    'Browser replay report is incomplete or failed'];
  if (expectedFirst) failures.push('A fatal harness diagnostic was recorded');
  requireValue(report.original_replay_capture_result === 'fail' &&
    JSON.stringify(report.finalization_failures) === JSON.stringify(failures),
    'Current SSS capture has additional standard finalization failures');
  return {pass: true, scope: 'expected_TIME_refusal_component_only',
    expected_runtime_diagnostic_retained: expectedFirst, original_replay_capture_result: 'fail',
    runtime_admission: false, Entry: false, C3: false};
}
