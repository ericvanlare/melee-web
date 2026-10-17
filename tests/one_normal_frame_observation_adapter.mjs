// One-shot adapter around the current private native frame-effect driver.
// All native operations are delegated to the already-reviewed driver helpers.
import {createHash} from 'node:crypto';

export const NORMAL_MATCH_FRAME_WINDOW = Object.freeze({minimum: 180, maximum: 240});

function jsonObject(value, label) {
  if (!value || typeof value !== 'object' || Array.isArray(value))
    throw Error(`${label} must be an object`);
  const encoded = JSON.stringify(value);
  if (!encoded || encoded === '{}') throw Error(`${label} must not be empty`);
  return JSON.parse(encoded);
}

const FRAME_EFFECT_PRODUCER_SCHEMA = 'melee-web-normal-frame-served-runtime-v1';
const FRAME_EFFECT_REQUIRED_FILES = Object.freeze({
  'gameplay_menu_browser.js': 'javascript',
  'gameplay_menu_browser.wasm': 'wasm',
  'gameplay_menu_browser.data': 'data',
});

export function validateFrameEffectMode({
  competitiveMatchStartOnly = false,
  frameEffectObservationOnly = false,
  firstDrawObservationOnly = false,
  otherRouteSelected = false,
  producerBindingPath = null,
} = {}) {
  if (frameEffectObservationOnly && firstDrawObservationOnly)
    throw Error('single-tick and first-draw observation modes are mutually exclusive');
  if (firstDrawObservationOnly && otherRouteSelected)
    throw Error('first-draw observation requires the exclusive competitive match-start route');
  const observationOnly = frameEffectObservationOnly || firstDrawObservationOnly;
  if (observationOnly && !competitiveMatchStartOnly)
    throw Error(`${firstDrawObservationOnly ? '--first-draw-observation-only' : '--frame-effect-observation-only'} requires --competitive-match-start-only`);
  if (observationOnly !== Boolean(producerBindingPath))
    throw Error(`--frame-effect-producer-binding is required only with ${firstDrawObservationOnly ? '--first-draw-observation-only' : '--frame-effect-observation-only'}`);
  return observationOnly;
}

export function validateServedRuntimeBinding(value) {
  const binding = jsonObject(value, 'frame-effect producer binding');
  if (binding.schema !== FRAME_EFFECT_PRODUCER_SCHEMA)
    throw Error(`frame-effect producer binding schema must be ${FRAME_EFFECT_PRODUCER_SCHEMA}`);
  if (!/^[a-f0-9]{40}$/.test(binding.source || ''))
    throw Error('frame-effect producer source must be a 40-character lowercase commit id');
  if (!/^[a-f0-9]{64}$/.test(binding.source_patch_sha256 || ''))
    throw Error('frame-effect producer source_patch_sha256 must be a lowercase SHA-256 digest');
  if (!['runtime-match-frame-effect Release','runtime-match-frame-effect Release --first-draw-components-diagnostic','runtime-match-frame-effect Release --first-draw-components-diagnostic --first-pair-audio-file-diagnostic'].includes(binding.profile))
    throw Error('frame-effect producer profile must be runtime-match-frame-effect Release');
  if (!binding.files || typeof binding.files !== 'object' || Array.isArray(binding.files))
    throw Error('frame-effect producer files must be an object');
  for (const name of Object.keys(FRAME_EFFECT_REQUIRED_FILES)) {
    const file = binding.files[name];
    if (!file || typeof file !== 'object' || Array.isArray(file) ||
        !Number.isSafeInteger(file.bytes) || file.bytes <= 0 ||
        !/^[a-f0-9]{64}$/.test(file.sha256 || ''))
      throw Error(`frame-effect producer files.${name} must contain positive bytes and a lowercase SHA-256 digest`);
  }
  return binding;
}

export async function verifyServedRuntimeAssets({binding, baseUrl, fetchImpl = globalThis.fetch}) {
  const producer = validateServedRuntimeBinding(binding);
  if (typeof fetchImpl !== 'function') throw Error('served-runtime fetch implementation is unavailable');
  const base = new URL(baseUrl);
  const observed = {};
  for (const [name, role] of Object.entries(FRAME_EFFECT_REQUIRED_FILES)) {
    const url = new URL(name, base);
    const response = await fetchImpl(url);
    if (response?.status !== 200) throw Error(`served ${role} returned HTTP ${response?.status ?? 'unknown'}`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    const sha256 = createHash('sha256').update(bytes).digest('hex');
    const expected = producer.files[name];
    if (bytes.byteLength !== expected.bytes || sha256 !== expected.sha256)
      throw Error(`served ${role} differs from its frozen producer binding`);
    observed[name] = {status: response.status, bytes: bytes.byteLength, sha256};
  }
  return {
    schema: 'melee-web-frame-effect-served-runtime-preflight-v1',
    status: 'passed',
    producer: {source: producer.source, source_patch_sha256: producer.source_patch_sha256,
      profile: producer.profile},
    files: observed,
  };
}

export function buildOneFrameRecorderBinding({outputName, discSha256, producerBinding, observationScope = 'single_tick'}) {
  if (!['single_tick', 'first_draw'].includes(observationScope))
    throw Error('Unsupported frame-effect recorder observation scope');
  if (typeof outputName !== 'string' || !outputName || outputName.includes('/') || outputName.includes('\\'))
    throw Error('outputName must be one directory name');
  if (!/^[a-f0-9]{64}$/.test(discSha256 || '')) throw Error('discSha256 must be a SHA-256 digest');
  const producer = validateServedRuntimeBinding(producerBinding);
  return {
    identity: {
      scenario: observationScope === 'first_draw'
        ? 'one-first-draw-effect-observation-v1' : 'one-normal-frame-effect-observation-v1',
      output_directory: outputName,
      disc_sha256: discSha256,
      producer_binding: producer,
    },
    identityScope: {
      source_phases: [7],
      callbacks: ['menuDiagnosticSample', 'menuDiagnosticIncident'],
      sample_ring: {capacity: 100, interval_ms: 100, lossy: true},
      incident_ring: {capacity: 24, dropped_records_fail: true},
      source_steps: 'counted at callback publication phase; route includes setup and the observed match callback',
    },
  };
}

export function oneFrameRuntimeCaptureFailures(capture, {identity, identityScope} = {}) {
  const failures = [];
  const expect = (label, actual, expected) => {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) failures.push(label);
  };
  expect('runtime_recorder_schema', capture?.schema, 'melee-web-runtime-callback-capture-v1');
  expect('runtime_recorder_status', capture?.status, 'installed');
  expect('runtime_recorder_identity', capture?.identity, identity);
  expect('runtime_recorder_identity_scope', capture?.identity_scope, identityScope);
  expect('runtime_sample_ring_capacity', capture?.max_samples, 100);
  expect('runtime_incident_ring_capacity', capture?.max_incidents, 24);
  if (!Array.isArray(capture?.samples) || capture.samples.length > 100)
    failures.push('runtime_samples_missing_or_oversized');
  if (!Array.isArray(capture?.incidents) || capture.incidents.length > 24)
    failures.push('runtime_incidents_missing_or_oversized');
  for (const key of ['callback_count', 'dropped_samples', 'dropped_incidents',
    'unknown_reason_count', 'dropped_unknown_reason_count', 'invalid_preparation_count',
    'dropped_invalid_preparation_count', 'invalid_phase_steps']) {
    if (!Number.isSafeInteger(capture?.[key]) || capture[key] < 0)
      failures.push(`runtime_${key}_invalid`);
  }
  const samples = Array.isArray(capture?.samples) ? capture.samples : [];
  if (Number.isSafeInteger(capture?.callback_count) && capture.callback_count < 1)
    failures.push('runtime_no_callbacks_observed');
  if (Number.isSafeInteger(capture?.callback_count) && Number.isSafeInteger(capture?.dropped_samples) &&
      capture.callback_count !== samples.length + capture.dropped_samples)
    failures.push('runtime_sample_accounting');
  for (const [index, row] of samples.entries()) {
    if (!Number.isFinite(row?.at_ms) || !Number.isInteger(row?.source_frame) || row.source_frame < -1 ||
        !Number.isInteger(row?.scene) || row.scene < 0 || !Number.isFinite(row?.source_steps) || row.source_steps < 0 ||
        !Number.isFinite(row?.source_draws) || row.source_draws < 0 ||
        !Number.isFinite(row?.callback_ms) || row.callback_ms < 0 ||
        !(row?.running === null || typeof row.running === 'boolean'))
      failures.push(`runtime_sample_${index}_invalid`);
  }
  const steps = capture?.phase_source_steps;
  if (!Array.isArray(steps) || steps.length !== 16 ||
      steps.some(value => !Number.isSafeInteger(value) || value < 0))
    failures.push('runtime_phase_steps_invalid');
  else if (steps[7] < 1) failures.push('runtime_no_match_source_step');
  if (capture?.invalid_phase_steps !== 0) failures.push('runtime_invalid_phase_steps');
  const reasons = capture?.reason_counts;
  const droppedReasons = capture?.dropped_reason_counts;
  const validReasons = value => Array.isArray(value) && value.length === 10 &&
    value.every(count => Number.isSafeInteger(count) && count >= 0);
  if (!validReasons(reasons)) failures.push('runtime_reason_counts_invalid');
  if (!validReasons(droppedReasons)) failures.push('runtime_dropped_reason_counts_invalid');
  if (capture?.dropped_incidents !== 0) failures.push('runtime_incidents_dropped');
  if (capture?.unknown_reason_count !== 0 || capture?.dropped_unknown_reason_count !== 0)
    failures.push('runtime_unknown_incident_reason');
  if (capture?.invalid_preparation_count !== 0 || capture?.dropped_invalid_preparation_count !== 0)
    failures.push('runtime_invalid_preparation_incident');
  if (validReasons(reasons) && reasons.some((count, reason) => reason !== 7 && count !== 0))
    failures.push('runtime_non_preparation_incident');
  if (validReasons(droppedReasons) && droppedReasons.some(count => count !== 0))
    failures.push('runtime_incident_ring_loss');
  const incidents = Array.isArray(capture?.incidents) ? capture.incidents : [];
  if (validReasons(reasons) && reasons.reduce((sum, count) => sum + count, 0) !== incidents.length)
    failures.push('runtime_incident_accounting');
  for (const [index, row] of incidents.entries()) {
    if (!Number.isFinite(row?.at_ms) || !Number.isFinite(row?.callback_ms) || row.callback_ms < 0 ||
        row?.reason_code !== 7 || row.value !== 0 || row.threshold !== 0 || row.clock_owner_code !== 0 ||
        !Number.isInteger(row.source_frame) || row.source_frame < -1 ||
        !Number.isInteger(row.scene) || row.scene < 0)
      failures.push(`runtime_incident_${index}_invalid`);
  }
  return failures;
}

function validHelpers(driver) {
  return driver && ['armNativeFrameEffect', 'readNativeFrameEffect',
    'retireNativeFrameEffect', 'frameEffectObservationFailures'].every(name =>
      typeof driver[name] === 'function');
}

function callbackReturnedPredicate() {
  const module = globalThis.Module;
  return module?.matchFrameEffectNativeIdle?.() === true &&
    module.matchFrameEffectCallbackReturned === true;
}

function validateRouteGate({normalizedMatch, normalizedMatchFailures, producerBinding}) {
  const failures = [];
  if (!normalizedMatch || normalizedMatch.ready !== true || normalizedMatch.paused !== false ||
      normalizedMatch.ending !== false || normalizedMatch.complete !== false ||
      !Number.isInteger(normalizedMatch.frame) ||
      normalizedMatch.frame < NORMAL_MATCH_FRAME_WINDOW.minimum ||
      normalizedMatch.frame > NORMAL_MATCH_FRAME_WINDOW.maximum)
    failures.push('normalized_match_not_live_in_180_240_window');
  if (!Array.isArray(normalizedMatchFailures) || normalizedMatchFailures.length !== 0)
    failures.push('competitive_normalization_gate_failed');
  try { jsonObject(producerBinding, 'producerBinding'); }
  catch { failures.push('frozen_producer_binding_missing'); }
  return failures;
}

async function retainRuntimeCaptureBestEffort(result, page, reader, identity, identityScope) {
  if (result.runtime_callback_capture_attempted || !page || typeof reader !== 'function') return;
  result.runtime_callback_capture_attempted = true;
  let timer;
  try {
    const capture = await Promise.race([
      Promise.resolve().then(() => reader(page)),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(Error('JS-only callback capture deadline expired')), 1000);
      }),
    ]);
    result.runtime_callback_capture = capture;
    result.runtime_callback_capture_failures = oneFrameRuntimeCaptureFailures(capture, {
      identity, identityScope,
    });
  } catch (error) {
    result.runtime_callback_capture_failures = ['runtime_callback_capture_unavailable'];
    result.runtime_callback_capture_error = String(error?.message || error);
  } finally {
    clearTimeout(timer);
  }
}

export async function captureOneFrameRuntimeRingBestEffort({page, readRuntimeCapture, identity, identityScope}) {
  const result = {};
  await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture, identity, identityScope);
  return {
    capture: result.runtime_callback_capture ?? null,
    failures: result.runtime_callback_capture_failures ?? ['runtime_callback_capture_unavailable'],
    error: result.runtime_callback_capture_error ?? null,
  };
}

// A host timeout never cancels a native continuation or permits another native
// call. The caller retains JS-only evidence and closes only its browser owner.
async function boundedTransport(operation, timeoutMs, label) {
  let timer;
  try {
    return await Promise.race([
      Promise.resolve().then(operation),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(Error(`${label} transport deadline expired`)), timeoutMs);
      }),
    ]);
  } finally {
    clearTimeout(timer);
  }
}

// Failure policy is explicit for the existing route's catch/finally blocks.
// After a dispatched private arm, uncertain callback/native promise state may
// only be cleaned up by closing the owned page/context/browser.
export async function observeOneNormalFrame({
  page,
  normalizedMatch,
  normalizedMatchFailures,
  producerBinding,
  outputName,
  discSha256,
  driver,
  readRuntimeCapture,
  observationScope = 'single_tick',
  armTimeoutMs = 15000,
  callbackTimeoutMs = 15000,
  retirementTimeoutMs = 15000,
  fenceTimeoutMs = 15000,
  fencePollMs = 25,
}) {
  const result = {
    schema: 'melee-web-one-normal-frame-observation-v1',
    scope: 'one_normal_callback_observation_only',
    status: 'refused',
    preflight_source_frame: Number.isInteger(normalizedMatch?.frame) ? normalizedMatch.frame : null,
    requested_source_frame: null,
    producer_binding: null,
    arm_attempt: 'not_started',
    callback_state: 'not_started',
    renderer_status: null,
    runtime_callback_capture: null,
    failures: [],
    cleanup_policy: 'ordinary_owner_cleanup_allowed',
    native_retired: false,
  };
  const gateFailures = validateRouteGate({normalizedMatch, normalizedMatchFailures, producerBinding});
  if (gateFailures.length) {
    result.failures.push(...gateFailures);
    return result;
  }
  if (!page || typeof page.evaluate !== 'function' || typeof page.waitForFunction !== 'function' ||
      !validHelpers(driver) || typeof readRuntimeCapture !== 'function') {
    result.failures.push('adapter_dependencies_missing');
    return result;
  }
  if (![armTimeoutMs, callbackTimeoutMs, fenceTimeoutMs, retirementTimeoutMs].every(value => Number.isSafeInteger(value) && value > 0 && value <= 60000) ||
      !Number.isSafeInteger(fencePollMs) || fencePollMs < 1 || fencePollMs > 1000) {
    result.failures.push('invalid_adapter_deadline');
    return result;
  }
  const binding = buildOneFrameRecorderBinding({outputName, discSha256, producerBinding, observationScope});
  result.producer_binding = binding.identity.producer_binding;
  result.runtime_callback_identity = binding.identity;
  result.runtime_callback_identity_scope = binding.identityScope;

  // From dispatch until a successful returned arm response is known, losing the
  // Playwright result means the private arm may have taken effect.
  result.arm_attempt = 'dispatched';
  result.cleanup_policy = 'browser_owner_only';
  const components=typeof driver.readFrameComponents==='function';
  const hostPoint=driver.hostOwnerPointRead==='first-draw-host-owners-v1';
  const joinedProfile=producerBinding.profile==='runtime-match-frame-effect Release --first-draw-components-diagnostic --first-pair-audio-file-diagnostic';
  if(joinedProfile!==hostPoint||hostPoint&&(!components||driver.naturalOneEligibility!==true||typeof driver.observeSelectedTextureInterval!=='function')){
    result.status='failed';result.failures.push('host_owner_point_profile_driver_mismatch');result.arm_attempt='not_dispatched';return result;
  }
  if(components&&(observationScope!=='first_draw'||typeof driver.armFrameComponents!=='function'||typeof driver.componentObservationFailures!=='function'||
      producerBinding.profile!==(hostPoint?'runtime-match-frame-effect Release --first-draw-components-diagnostic --first-pair-audio-file-diagnostic':'runtime-match-frame-effect Release --first-draw-components-diagnostic'))){
    result.status='failed';result.failures.push('component_private_profile_required');result.arm_attempt='not_dispatched';return result;
  }
  if(driver.naturalOneEligibility===true&&(typeof driver.observeSelectedTextureInterval!=='function'||typeof driver.naturalOneEnrollmentFailures!=='function')){
    result.status='failed';result.failures.push('natural_one_private_interval_required');result.arm_attempt='not_dispatched';return result;
  }
  if(typeof driver.observeSelectedTextureInterval==='function'&&!components){
    result.status='failed';result.failures.push('selected_texture_interval_requires_components');result.arm_attempt='not_dispatched';return result;
  }
  if(!components&&producerBinding.profile!=='runtime-match-frame-effect Release'){
    result.status='failed';result.failures.push('component_driver_required');result.arm_attempt='not_dispatched';return result;
  }
  let arm;
  try {
    arm = await boundedTransport(() => page.evaluate(driver.armNativeFrameEffect, {
      minimumFrame: NORMAL_MATCH_FRAME_WINDOW.minimum,
      maximumFrame: NORMAL_MATCH_FRAME_WINDOW.maximum,
      componentEligibility:components,
      ...(driver.naturalOneEligibility===true?{naturalOneEligibility:true}:{}),
    }), armTimeoutMs, 'arm');
  } catch (error) {
    result.status = 'failed';
    result.failures.push('arm_evaluation_response_lost_or_failed');
    result.error = String(error?.message || error);
    result.callback_state = 'uncertain';
    await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
      result.runtime_callback_identity, result.runtime_callback_identity_scope);
    return result;
  }
  result.arm_response = arm;
  if (Number.isInteger(arm?.match?.frame)) result.requested_source_frame = arm.match.frame;
  if (arm?.armed !== true) {
    // These reasons return before the private export is called. A native refusal
    // means the export ran but explicitly made no request; no callback is armed.
    const beforeNative = new Set(['native_call_pending', 'private_exports_missing',
      'source_frame_outside_declared_window']);
    result.arm_attempt = beforeNative.has(arm?.reason) ? 'not_dispatched' : 'returned_refusal';
    result.cleanup_policy = arm?.reason === 'native_call_pending' ? 'browser_owner_only'
      : beforeNative.has(arm?.reason) || arm?.reason === 'native_arm_refused'
        ? 'ordinary_owner_cleanup_allowed' : 'browser_owner_only';
    result.status = 'failed';
    result.failures.push(`native_arm:${arm?.reason || 'invalid_response'}`);
    result.callback_state = 'not_armed';
    await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
      result.runtime_callback_identity, result.runtime_callback_identity_scope);
    return result;
  }
  if(driver.naturalOneEligibility===true&&(arm.selection_pending!==true||arm.enrollment_mode!=='naturally_due_one_tick')){
    result.status='failed';result.failures.push('natural_one_request_response_malformed');return result;
  }
  result.callback_state = 'waiting_for_callback_return';
  try {
    await boundedTransport(async () => {
      const handle = await page.waitForFunction(callbackReturnedPredicate, null,
        {timeout: callbackTimeoutMs, polling: 10});
      try { await handle.jsonValue(); }
      finally { await handle.dispose(); }
    }, callbackTimeoutMs, 'callback return');
  } catch (error) {
    result.status = 'failed';
    result.failures.push('callback_return_not_confirmed');
    result.error = String(error?.message || error);
    result.callback_state = 'uncertain';
    await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
      binding.identity, binding.identityScope);
    return result;
  }
  result.callback_state = 'returned_waiting_for_normal_fence';

  const fenceDeadline = Date.now() + fenceTimeoutMs;
  let observation = null;
  while (Date.now() < fenceDeadline) {
    try {
      observation = await boundedTransport(() => page.evaluate(driver.readNativeFrameEffect,driver.naturalOneEligibility===true?{naturalOneEligibility:true}:undefined),
        Math.max(1, fenceDeadline - Date.now()), 'normal fence');
    }
    catch (error) {
      result.status = 'failed';
      result.failures.push('native_status_response_lost_or_failed');
      result.error = String(error?.message || error);
      result.callback_state = 'uncertain';
      await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
        binding.identity, binding.identityScope);
      return result;
    }
    if (observation?.settled !== true) {
      result.status = 'failed';
      result.failures.push('native_status_not_settled');
      result.callback_state = 'uncertain';
      await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
        binding.identity, binding.identityScope);
      return result;
    }
    if (observation.completed === true) break;
    await new Promise(resolve => setTimeout(resolve, fencePollMs));
  }
  if (observation?.completed !== true) {
    result.status = 'failed';
    result.failures.push('normal_queue_fence_not_observed_before_deadline');
    result.renderer_status = observation?.status ?? null;
    result.callback_state = 'fence_pending';
    await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
      binding.identity, binding.identityScope);
    return result;
  }
  result.renderer_status = observation.status;
  result.callback_state = 'fenced';
  const nativeReceipt = observation.status;
  result.observation_failures = driver.frameEffectObservationFailures(nativeReceipt);
  if (!Number.isInteger(nativeReceipt?.requested_source_frame) ||
      nativeReceipt.requested_source_frame < NORMAL_MATCH_FRAME_WINDOW.minimum ||
      nativeReceipt.requested_source_frame > NORMAL_MATCH_FRAME_WINDOW.maximum)
    result.observation_failures.push('armed_source_frame_outside_window');
  if(driver.naturalOneEligibility===true){
    result.selection_origin_frame=result.requested_source_frame;
    result.selection_enrollment=observation.enrollment;
    result.selection_enrollment_failures=driver.naturalOneEnrollmentFailures(observation.enrollment,nativeReceipt,{originFrame:result.selection_origin_frame});
    result.observation_failures.push(...result.selection_enrollment_failures);
    if(!result.selection_enrollment_failures.length)result.requested_source_frame=observation.enrollment.selected_frame;
  }else if (!Number.isInteger(result.requested_source_frame) ||
      nativeReceipt?.requested_source_frame !== result.requested_source_frame)
    result.observation_failures.push('armed_source_frame_mismatch');

  // This bounded JS-only reader never calls a native export; failure paths also
  // use it to retain any callback rows already published before a stall.
  await retainRuntimeCaptureBestEffort(result, page, readRuntimeCapture,
    binding.identity, binding.identityScope);

  if(components){
    const exactOne=['planned_steps','callback_steps','callback_draws','tick_returns','draw_returns'].every(k=>nativeReceipt?.[k]===1);
    if(!exactOne||result.observation_failures.length||result.runtime_callback_capture_failures?.length){
      result.status='failed';result.failures.push('component_requires_valid_exact_one_tick_draw');return result;
    }
    try{
      result.component_read=await boundedTransport(()=>page.evaluate(driver.readFrameComponents),retirementTimeoutMs,'component read');
      result.component_failures=driver.componentObservationFailures(result.component_read,{stage:'read'});
      if(result.component_failures.length){result.status='failed';result.failures.push(...result.component_failures);return result;}
      // No diagnostic mutation until this exact read receipt passes the host decoder.
      result.component_arm=await boundedTransport(()=>page.evaluate(driver.armFrameComponents,
        {expectedDepthSequence:result.component_read.depth_before.sequence}),retirementTimeoutMs,'component arm');
      const armed=result.component_arm;
      if(armed?.schema!=='melee-web-first-draw-components-arm-v1'||armed.status!=='armed'||armed.same_module!==true||armed.returned_idle!==true||armed.composite_eligible!==false){
        result.status='failed';result.failures.push('component_arm_refused_or_malformed');return result;
      }
      result.components={...result.component_read,schema:'melee-web-first-draw-components-v1',status:'observed',depth_after:armed.depth_after};
      result.component_failures=driver.componentObservationFailures(result.components);
      if(result.component_failures.length){result.status='failed';result.failures.push(...result.component_failures);return result;}
    }catch(error){result.status='failed';result.failures.push('component_response_lost_or_failed');result.error=String(error?.message||error);return result;}
  }

  if(typeof driver.observeSelectedTextureInterval==='function'){
    try{
      result.selected_texture_interval=await driver.observeSelectedTextureInterval({components:result.components,
        invoke:(fn,arg)=>boundedTransport(()=>page.evaluate(fn,arg),retirementTimeoutMs,'selected texture '+fn.name)});
      const interval=result.selected_texture_interval;
      if(interval?.schema!=='melee-web-selected-texture-interval-observation-v1'||interval.status!=='closed'||
         interval.composite_eligible!==false||interval.started?.status!=='active'||interval.ended?.status!=='closed'||
         !Array.isArray(interval.failures)||interval.failures.length){
        result.status='failed';result.failures.push('selected_texture_interval_refused_or_malformed');return result;
      }
    }catch(error){result.status='failed';result.failures.push('selected_texture_interval_response_lost_or_failed');result.error=String(error?.message||error);return result;}
  }

  // The private checked retirement is the only permitted native shutdown after
  // arm. A lost response or refused retirement never falls through to unload.
  result.callback_state = 'retiring';
  try {
    const retirement = await boundedTransport(() => page.evaluate(driver.retireNativeFrameEffect),
      retirementTimeoutMs, 'retirement');
    result.retirement = retirement;
    result.native_retired = retirement?.retired === true;
    if (!result.native_retired) {
      result.status = 'failed';
      result.failures.push('checked_native_retirement_failed');
      result.callback_state = 'retirement_failed';
      return result;
    }
  } catch (error) {
    result.status = 'failed';
    result.failures.push('retirement_response_lost_or_failed');
    result.error = String(error?.message || error);
    result.callback_state = 'uncertain';
    return result;
  }
  result.cleanup_policy = 'native_retired_browser_owner_cleanup';
  result.callback_state = 'retired';
  result.failures.push(...(result.observation_failures || []));
  result.failures.push(...(result.runtime_callback_capture_failures || []));
  result.status = result.failures.length ? 'failed' : 'pass';
  return result;
}

export function frameEffectMustSkipOrdinaryNativeCleanup(observation) {
  return observation?.cleanup_policy === 'browser_owner_only' ||
    observation?.cleanup_policy === 'native_retired_browser_owner_cleanup';
}
