import assert from 'node:assert/strict';
import test from 'node:test';
import {cssReadinessEvidence, cssSssOutcome, hasCssMenuReadiness,
  rawKeyboardStartDelivered, SOURCE_PAD_BUTTON_START} from './stadium_c1a_css_sss_reducer.mjs';

function sample({phase = 1, running = 1, cooldown = 0, pending = 0, ready = 1,
  inputSamples = 10, callbackCount = 20, keyboardMask = 1, active = 1,
  focused = 1, visible = 1, inputReady = 1, inputButtons = 0, physicalMask = 0, last = null} = {}) {
  const ids = Array(14).fill(0);
  ids[5] = cooldown; ids[7] = pending; ids[8] = ready; ids[10] = callbackCount;
  return {phase, running, css: {ids}, input: {ready: inputReady, focused, visible, active,
    keyboard_layout: 1, keyboard_requested_mask: keyboardMask, keyboard_active_mask: keyboardMask,
    physical_mask: physicalMask, samples: inputSamples,
    pads: [{source: 'keyboard', err: 0, buttons: inputButtons}, {source: 'none', err: -1, buttons: 0}],
    last_non_neutral: last}};
}

test('C1a CSS readiness requires source cooldown/readiness and active P1 keyboard state', () => {
  const ready = sample();
  assert.equal(hasCssMenuReadiness(ready), true);
  for (const changed of [
    {cooldown: 1}, {ready: 0}, {pending: 1}, {phase: 3}, {running: 0},
    {keyboardMask: 0}, {keyboardMask: 3}, {physicalMask: 1}, {active: 0},
    {focused: 0}, {visible: 0}, {inputReady: 0},
  ]) assert.equal(hasCssMenuReadiness(sample(changed)), false, JSON.stringify(changed));
});

test('readiness evidence requires advancing source callbacks and input samples', () => {
  const first = sample({inputSamples: 10, callbackCount: 20});
  assert.deepEqual(cssReadinessEvidence([first, first]), {
    input_progressed: false, source_callback_progressed: false,
    readiness_observed: true, ready_sample_index: 0,
  });
  const next = sample({inputSamples: 11, callbackCount: 21});
  assert.deepEqual(cssReadinessEvidence([first, next]), {
    input_progressed: true, source_callback_progressed: true,
    readiness_observed: true, ready_sample_index: 0,
  });
});

test('raw Start delivery requires a new non-neutral P1 keyboard sample', () => {
  const start = {port: 0, source: 'keyboard', err: 0,
    buttons: SOURCE_PAD_BUTTON_START, sample: 11};
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12, last: start}), 10), true);
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12,
    last: {...start, port: 1}}), 10), false);
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12,
    last: {...start, source: 'gamepad'}}), 10), false);
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12,
    last: {...start, buttons: 0x0100}}), 10), false);
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12,
    last: {...start, err: -1}}), 10), false);
  assert.equal(rawKeyboardStartDelivered(sample({inputSamples: 12,
    last: {...start, sample: 10}}), 10), false);
});

test('CSS-to-SSS outcome distinguishes readiness, missing input, and source refusal', () => {
  const cold = [sample({cooldown: 1, ready: 0}), sample({cooldown: 1, ready: 0,
    inputSamples: 11, callbackCount: 21})];
  assert.equal(cssSssOutcome(cold, cold.at(-1), 10).failure_boundary,
    'css-readiness-not-observed');
  const stalled = [sample(), sample()];
  assert.equal(cssSssOutcome(stalled, stalled.at(-1), 10).failure_boundary,
    'css-source-or-input-progress-not-observed');
  const ready = [sample(), sample({inputSamples: 11, callbackCount: 21})];
  assert.equal(cssSssOutcome(ready, ready.at(-1), 10).failure_boundary,
    'keyboard-start-not-observed-on-p1');
  const delivered = sample({phase: 1, inputSamples: 12, callbackCount: 22,
    last: {port: 0, source: 'keyboard', err: 0, buttons: SOURCE_PAD_BUTTON_START, sample: 11}});
  const refused = [ready[0], ready[1], delivered];
  assert.equal(cssSssOutcome(refused, delivered, 10).failure_boundary,
    'source-start-not-accepted-into-sss');
  const accepted = {...sample({phase: 3, inputSamples: 13}), css: null};
  assert.deepEqual(cssSssOutcome([...ready, delivered, accepted], accepted, 10), {
    readiness: {input_progressed: true, source_callback_progressed: true,
      readiness_observed: true, ready_sample_index: 0},
    input_start_delivered: true, input_start_sample_index: 2, phase_3_running: true,
    accepted: true, failure_boundary: null,
  });
});
