/* Pure acceptance predicates for the bounded C1a CSS -> SSS browser reducer. */
// Source PAD ABI: .deps/melee/extern/dolphin/include/dolphin/pad.h:36.
export const SOURCE_PAD_BUTTON_START = 0x1000;
export const CSS_SSS_TRANSITION_LIMITS = Object.freeze({
  readinessFrames: 180,
  transitionMs: 20000,
  sampleCapHz: 144,
});

export function createCssSssTransitionCapture(scope) {
  return {
    scope,
    max_readiness_frames: CSS_SSS_TRANSITION_LIMITS.readinessFrames,
    max_post_start_frames: Math.ceil(CSS_SSS_TRANSITION_LIMITS.transitionMs *
      CSS_SSS_TRANSITION_LIMITS.sampleCapHz / 1000),
    post_start_sample_cap_hz: CSS_SSS_TRANSITION_LIMITS.sampleCapHz,
    post_start_transition_deadline_ms: CSS_SSS_TRANSITION_LIMITS.transitionMs,
    observer_contract: {css_ids_length: 14, cooldown_index: 5, pending_scene_index: 7,
      start_ready_index: 8, callback_count_index: 10, last_start_trigger_index: 11,
      last_start_ready_index: 12, last_start_pending_index: 13},
    canvas_focused_before_observation: false,
    samples: [],
    readiness: null,
    input_before_start: null,
    input_after_start: null,
    start_key: null,
    last_source_start: null,
    transition: null,
    outcome: null,
  };
}

export function hasCssMenuReadiness(sample) {
  const ids = sample?.css?.ids;
  const input = sample?.input;
  const p1 = input?.pads?.[0];
  return sample?.phase === 1 && sample?.running === 1 &&
    Array.isArray(ids) && ids.length === 14 &&
    ids[5] === 0 && ids[7] === 0 && ids[8] > 0 &&
    input?.ready === 1 && input.focused === 1 && input.visible === 1 && input.active === 1 &&
    input.keyboard_layout === 1 && input.keyboard_requested_mask === 1 &&
    input.keyboard_active_mask === 1 && input.physical_mask === 0 &&
    p1?.source === 'keyboard' && p1.err === 0 && input?.pads?.[1]?.source === 'none';
}

export function cssReadinessEvidence(samples) {
  const valid = samples.filter(sample => Array.isArray(sample?.css?.ids) &&
    sample.css.ids.length === 14 && sample.input && Number.isInteger(sample.input.samples));
  const first = valid[0];
  const last = valid.at(-1);
  const inputProgressed = !!first && !!last && last.input.samples > first.input.samples;
  const callbackProgressed = !!first && !!last && last.css.ids[10] !== first.css.ids[10];
  const readyIndex = samples.findIndex(hasCssMenuReadiness);
  return {input_progressed: inputProgressed, source_callback_progressed: callbackProgressed,
    readiness_observed: readyIndex >= 0, ready_sample_index: readyIndex};
}

export function rawKeyboardStartDelivered(sample, baselineSamples) {
  const last = sample?.input?.last_non_neutral;
  return !!last && last.port === 0 && last.source === 'keyboard' && last.err === 0 &&
    last.buttons === SOURCE_PAD_BUTTON_START && Number.isInteger(last.sample) &&
    last.sample > baselineSamples &&
    sample?.input?.keyboard_requested_mask === 1 && sample?.input?.keyboard_active_mask === 1 &&
    sample?.input?.pads?.[0]?.source === 'keyboard' && sample?.input?.pads?.[1]?.source === 'none';
}

export function cssSssOutcome(samples, finalSample, baselineSamples) {
  const readiness = cssReadinessEvidence(samples);
  const deliveryIndex = samples.findIndex(sample => rawKeyboardStartDelivered(sample, baselineSamples));
  const inputDelivered = deliveryIndex >= 0;
  const phaseAccepted = finalSample?.phase === 3 && finalSample?.running === 1;
  let failureBoundary = null;
  if (!readiness.readiness_observed) failureBoundary = 'css-readiness-not-observed';
  else if (!readiness.input_progressed || !readiness.source_callback_progressed)
    failureBoundary = 'css-source-or-input-progress-not-observed';
  else if (!inputDelivered) failureBoundary = 'keyboard-start-not-observed-on-p1';
  else if (!phaseAccepted) failureBoundary = 'source-start-not-accepted-into-sss';
  return {readiness, input_start_delivered: inputDelivered,
    input_start_sample_index: inputDelivered ? deliveryIndex : null,
    phase_3_running: phaseAccepted, accepted: failureBoundary === null,
    failure_boundary: failureBoundary};
}
