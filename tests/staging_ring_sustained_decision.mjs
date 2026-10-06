import crypto from 'node:crypto';
import path from 'node:path';

export const SUSTAINED_WINDOW_MS = 2000;
export const PREPARATION_REASON = 7;
export const SIMULATION_DEBT_REASON = 1;
// Fixed before the first attempt: up to 100 ms for browser observation and
// synchronous readback scheduling after the requested 2 s exposure.
export const MAX_OBSERVED_GPU_WINDOW_MS = SUSTAINED_WINDOW_MS + 100;

const finite = value => typeof value === 'number' && Number.isFinite(value);

/** Stable digest for a named browser-artifact inventory, independent of JSON
 * object insertion order. Each value is the captured {bytes, sha256} pair. */
export function artifactMapDigest(artifacts) {
  if (!artifacts || typeof artifacts !== 'object' || Array.isArray(artifacts)) return null;
  const entries = Object.entries(artifacts).sort(([left], [right]) => left.localeCompare(right));
  if (!entries.length || entries.some(([name, value]) => !name ||
      !Number.isSafeInteger(value?.bytes) || value.bytes < 0 ||
      typeof value?.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(value.sha256))) return null;
  return crypto.createHash('sha256').update(JSON.stringify(Object.fromEntries(entries))).digest('hex');
}

/** Compare two named inventories and retain exact identities on changed files. */
export function compareArtifactMaps(expected, actual) {
  const expectedMap = expected && typeof expected === 'object' && !Array.isArray(expected) ? expected : {};
  const actualMap = actual && typeof actual === 'object' && !Array.isArray(actual) ? actual : {};
  const differences = [];
  for (const name of [...new Set([...Object.keys(expectedMap), ...Object.keys(actualMap)])].sort()) {
    const before = expectedMap[name] ?? null;
    const after = actualMap[name] ?? null;
    if (JSON.stringify(before) !== JSON.stringify(after)) differences.push({name, expected: before, actual: after});
  }
  return {matches: differences.length === 0, differences};
}

/** Verify an authorized prospective source/build identity before the runner
 * accepts any served bytes. This does not assert historical equivalence. */
export function validateProspectiveBuildManifest(manifest, {source, buildDirectory,
  localArtifacts, expectedNames} = {}) {
  const problems = [];
  if (manifest?.schema !== 'melee-web-h1-prospective-build-manifest-v1' ||
      manifest?.binding_type !== 'prospective_local_build_inventory')
    problems.push('manifest_schema_invalid');
  if (!source || manifest?.source?.commit !== source.commit || manifest?.source?.tree !== source.tree)
    problems.push('source_identity_mismatch');
  if (buildDirectory && pathResolve(manifest?.build_directory) !== pathResolve(buildDirectory))
    problems.push('build_directory_mismatch');
  const requiredNames = Array.isArray(expectedNames) ? expectedNames :
    (localArtifacts && typeof localArtifacts === 'object' ? Object.keys(localArtifacts) : []);
  const requiredNameSet = new Set(requiredNames);
  if (!requiredNames.length || requiredNames.some(name => typeof name !== 'string' || !name) ||
      requiredNameSet.size !== requiredNames.length)
    problems.push('expected_artifact_names_invalid');
  const inventory = manifest?.inventory;
  const actualCount = inventory?.artifacts && typeof inventory.artifacts === 'object' &&
    !Array.isArray(inventory.artifacts) ? Object.keys(inventory.artifacts).length : 0;
  if (!inventory || inventory.artifact_count !== actualCount || actualCount !== requiredNames.length)
    problems.push('manifest_artifact_count_invalid');
  const actualNames = inventory?.artifacts && typeof inventory.artifacts === 'object' &&
    !Array.isArray(inventory.artifacts) ? Object.keys(inventory.artifacts) : [];
  if (requiredNameSet.size === requiredNames.length &&
      (actualNames.length !== requiredNames.length ||
       [...requiredNameSet].some(name => !Object.hasOwn(inventory?.artifacts ?? {}, name))))
    problems.push('manifest_artifact_names_mismatch');
  if (localArtifacts && (Object.keys(localArtifacts).length !== requiredNames.length ||
      [...requiredNameSet].some(name => !Object.hasOwn(localArtifacts, name))))
    problems.push('local_artifact_names_mismatch');
  const declaredDigest = artifactMapDigest(inventory?.artifacts);
  if (!declaredDigest || inventory?.artifact_map_sha256 !== declaredDigest)
    problems.push('manifest_artifact_digest_invalid');
  const localComparison = compareArtifactMaps(inventory?.artifacts, localArtifacts);
  if (!localComparison.matches) problems.push('local_artifact_map_mismatch');
  return {valid: problems.length === 0, problems,
    expected_artifact_count: requiredNames.length,
    manifest_artifact_map_sha256: inventory?.artifact_map_sha256 ?? null,
    local_artifact_map_sha256: artifactMapDigest(localArtifacts),
    local_artifact_differences: localComparison.differences};
}

function pathResolve(value) {
  return typeof value === 'string' && value.length ? path.resolve(value) : null;
}

/** Summarize the actual registration-to-delivery batch samples retained by
 * pause_trace_perturb.mjs. This is wall duration, not pure GPU execution time. */
export function summarizeObservedBatchDurations(samples) {
  if (!Array.isArray(samples) || samples.length === 0 ||
      samples.some(value => !finite(value) || value <= 0)) return null;
  const sorted = [...samples].sort((a, b) => a - b);
  const sum = sorted.reduce((total, value) => total + value, 0);
  const pick = fraction => sorted[Math.min(sorted.length - 1, Math.floor(fraction * sorted.length))];
  return {count: sorted.length, min_ms: sorted[0], mean_ms: sum / sorted.length,
    p50_ms: pick(0.5), p95_ms: pick(0.95), max_ms: sorted.at(-1)};
}

/** Qualify one active-play interval from compact rows sampled at the existing
 * timing callback. This is evidence validation only; it does not alter runtime
 * thresholds or pause/resume behavior. */
export function assessSustainedWindow({rows, incidents, startMs, endMs,
  durationMs = SUSTAINED_WINDOW_MS, captureErrors = 0, dropped = 0,
  incidentOverflow = 0, browserErrors = [], pauseTerminal = false} = {}) {
  const problems = [];
  if (!finite(startMs) || !finite(endMs) || endMs < startMs) problems.push('invalid_window_bounds');
  else if (endMs - startMs < durationMs) problems.push('window_shorter_than_required');
  if (!Array.isArray(rows)) problems.push('missing_callback_rows');
  if (!Array.isArray(incidents)) problems.push('missing_incident_events');
  if (captureErrors !== 0 || dropped !== 0 || incidentOverflow !== 0) problems.push('capture_loss_or_error');
  if (!Array.isArray(browserErrors) || browserErrors.length !== 0) problems.push('browser_error');

  const measuredRows = Array.isArray(rows) ? rows.filter(row =>
    finite(row?.hook_at_ms) && row.hook_at_ms >= startMs && row.hook_at_ms <= endMs) : [];
  const pauseCallback = pauseTerminal && measuredRows.at(-1)?.sample_running === 0
    ? measuredRows.at(-1) : null;
  const activeRows = pauseCallback ? measuredRows.slice(0, -1) : measuredRows;
  if (activeRows.length < 2) problems.push('insufficient_callback_rows');
  const required = ['hook_at_ms', 'total_ms', 'preparation_ms', 'staging_slot_wait_ms',
    'simulation_audio_ms', 'sample_pending_ticks', 'sample_running',
    'sample_source_frame', 'sample_replay_cursor'];
  for (const row of measuredRows) {
    if (required.some(name => !finite(row[name]))) {
      problems.push('missing_callback_telemetry');
      break;
    }
  }
  if (activeRows.some(row => row.sample_running !== 1 || row.sample_source_frame <= 0))
    problems.push('gameplay_not_ready_throughout_window');
  if (measuredRows.some(row => row.preparation_ms !== 0)) problems.push('preparation_duration_observed');
  const prepEvents = Array.isArray(incidents) ? incidents.filter(event =>
    event?.reason === PREPARATION_REASON && event.source_frame > 0 &&
    finite(event.at_ms) && event.at_ms >= startMs && event.at_ms <= endMs) : [];
  if (prepEvents.length) problems.push('preparation_event_observed');

  const first = activeRows[0] ?? null;
  const last = activeRows.at(-1) ?? null;
  if (first && last && (!(last.sample_replay_cursor > first.sample_replay_cursor) ||
      !(last.sample_source_frame > first.sample_source_frame)))
    problems.push('source_progress_not_observed');
  return {
    status: problems.length ? 'inconclusive' : 'clean',
    problems: [...new Set(problems)],
    start_ms: startMs ?? null,
    end_ms: endMs ?? null,
    elapsed_ms: finite(startMs) && finite(endMs) ? endMs - startMs : null,
    callback_rows: activeRows.length,
    measured_rows: measuredRows.length,
    first_input_cursor: first?.sample_replay_cursor ?? null,
    last_input_cursor: last?.sample_replay_cursor ?? null,
    first_original_frame: first?.sample_source_frame ?? null,
    last_original_frame: last?.sample_source_frame ?? null,
    preparation_events: prepEvents,
    rows: measuredRows,
    active_rows: activeRows,
    pause_callback_row: pauseCallback,
  };
}

function hasGenuineStagingIncident(recorder) {
  return Array.isArray(recorder?.retained_records) && recorder.retained_records.some(record =>
    record?.incident?.staging !== null && record?.incident?.staging !== undefined &&
    (record.incident.reason === 'simulation_debt' || record.incident.reason === SIMULATION_DEBT_REASON));
}

/** Apply the frozen decision rule to already captured evidence. */
export function classifySustainedAttempt({control, treatment, terminal,
  recorder, gpuLoad, stimulusValid = true} = {}) {
  const inconclusive = (reason, detail = {}) => ({result: 'inconclusive', decision: 'inspect_immediately', reason, ...detail});
  if (!stimulusValid) return inconclusive('invalid_stimulus');
  if (!control || control.status !== 'clean')
    return inconclusive('300_control_not_qualified', {control_problems: control?.problems ?? ['missing_control']});
  if (!['timing_pause', 'window_complete'].includes(terminal?.outcome))
    return inconclusive('unexpected_terminal_outcome', {terminal: terminal ?? null});
  if (!treatment || treatment.status !== 'clean')
    return inconclusive('600_treatment_not_qualified', {treatment_problems: treatment?.problems ?? ['missing_treatment']});
  const loadObservation = gpuLoad?.gpu_observation;
  const batchDurations = gpuLoad?.batch_durations_ms;
  const batchDurationStats = summarizeObservedBatchDurations(batchDurations);
  if (gpuLoad?.requested_iterations !== 600 || gpuLoad?.requested_gpu_load_window_ms !== SUSTAINED_WINDOW_MS ||
      !finite(gpuLoad.actual_gpu_load_window_ms) || gpuLoad.actual_gpu_load_window_ms <= 0 ||
      gpuLoad.actual_gpu_load_window_ms > MAX_OBSERVED_GPU_WINDOW_MS ||
      !batchDurationStats ||
      loadObservation?.window_batches !== batchDurations.length ||
      !Array.isArray(loadObservation.errors) || loadObservation.errors.length !== 0 || loadObservation.on !== false)
    return inconclusive('measured_600_gpu_window_invalid', {actual_gpu_load_window_ms:
      gpuLoad?.actual_gpu_load_window_ms ?? null, observed_batches: loadObservation?.window_batches ?? null,
      retained_batch_durations: Array.isArray(batchDurations) ? batchDurations.length : null,
      gpu_errors: loadObservation?.errors ?? null,
      maximum_observed_gpu_window_ms: MAX_OBSERVED_GPU_WINDOW_MS});

  if (terminal?.outcome === 'timing_pause') {
    if (terminal.pause_reason !== SIMULATION_DEBT_REASON)
      return inconclusive('pause_reason_not_simulation_debt', {pause_reason: terminal.pause_reason ?? null});
    if (!hasGenuineStagingIncident(recorder))
      return inconclusive('genuine_staging_incident_not_exported');
    const rows = (treatment.active_rows ?? treatment.rows).filter(row =>
      row.sample_running === 1 && row.sample_source_frame > 0);
    const debtTail = rows.slice(-3).map(row => row.sample_pending_ticks);
    const risingDebt = debtTail.length === 3 && debtTail[1] > debtTail[0] && debtTail[2] > debtTail[1];
    const pausedRow = treatment.pause_callback_row;
    const last = pausedRow ?? rows.at(-1);
    if (!last || last.sample_source_frame <= 0 || !finite(last.total_ms) ||
        !finite(last.staging_slot_wait_ms))
      return inconclusive('pause_callback_staging_telemetry_missing');
    const stagingDominant = !!last && finite(last.total_ms) && finite(last.staging_slot_wait_ms) &&
      last.total_ms > 0 && last.staging_slot_wait_ms > 0 && last.staging_slot_wait_ms <= last.total_ms &&
      last.staging_slot_wait_ms > last.total_ms - last.staging_slot_wait_ms;
    const evidence = {debt_tail_ticks: debtTail, rising_callback_debt: risingDebt,
      pause_callback_total_ms: last?.total_ms ?? null,
      pause_callback_staging_wait_ms: last?.staging_slot_wait_ms ?? null,
      staging_wait_dominant: stagingDominant,
      observed_batch_duration_stats_ms: batchDurationStats,
      requested_gpu_load_window_ms: gpuLoad.requested_gpu_load_window_ms,
      actual_gpu_load_window_ms: gpuLoad.actual_gpu_load_window_ms,
      gpu_load_actual_minus_requested_ms: gpuLoad.actual_gpu_load_window_ms - gpuLoad.requested_gpu_load_window_ms,
      gpu_load_observer_overshoot_ms: Math.max(0,
        gpuLoad.actual_gpu_load_window_ms - gpuLoad.requested_gpu_load_window_ms),
      gpu_load_early_stop_ms: Math.max(0,
        gpuLoad.requested_gpu_load_window_ms - gpuLoad.actual_gpu_load_window_ms),
      genuine_recorder_staging_incident: true};
    if (risingDebt && stagingDominant && evidence.genuine_recorder_staging_incident) {
      return {result: 'positive_control', decision: 'propose_one_equivalent_ring_four_comparison', ...evidence};
    }
    return {result: 'classified_pause', decision: 'park_ring_four',
      classification: stagingDominant ? 'staging_wait_without_rising_callback_debt'
        : 'cpu_or_callback_gap_dominated', ...evidence};
  }

  if (terminal?.outcome === 'window_complete')
    return {result: 'clean_nonreproduction', decision: 'park_ring_four',
      observed_batch_duration_stats_ms: batchDurationStats,
      requested_gpu_load_window_ms: gpuLoad.requested_gpu_load_window_ms,
      actual_gpu_load_window_ms: gpuLoad.actual_gpu_load_window_ms,
      gpu_load_actual_minus_requested_ms: gpuLoad.actual_gpu_load_window_ms - gpuLoad.requested_gpu_load_window_ms,
      gpu_load_observer_overshoot_ms: Math.max(0,
        gpuLoad.actual_gpu_load_window_ms - gpuLoad.requested_gpu_load_window_ms),
      gpu_load_early_stop_ms: Math.max(0,
        gpuLoad.requested_gpu_load_window_ms - gpuLoad.actual_gpu_load_window_ms)};
  return inconclusive('unexpected_terminal_outcome', {terminal: terminal ?? null});
}

/** Cleanup and postcheck integrity has priority over any plausible in-window
 * classification. The runner uses this as its single final decision gate. */
export function resolveSustainedAttempt({integrityErrors = [], ...evidence} = {}) {
  if (!Array.isArray(integrityErrors) || integrityErrors.length)
    return {result: 'inconclusive', decision: 'inspect_immediately',
      reason: 'capture_or_cleanup_integrity_failed', integrity_errors: Array.isArray(integrityErrors)
        ? integrityErrors : ['integrity_error_list_invalid']};
  return classifySustainedAttempt(evidence);
}
