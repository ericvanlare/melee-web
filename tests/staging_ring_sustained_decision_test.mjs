import assert from 'node:assert/strict';
import {assessSustainedWindow, classifySustainedAttempt, resolveSustainedAttempt,
  summarizeObservedBatchDurations, artifactMapDigest, compareArtifactMaps,
  validateProspectiveBuildManifest} from './staging_ring_sustained_decision.mjs';

function rowsForWindow(start, end, pending = [0, 1, 2], stagingMs = 12, totalMs = 20) {
  const count = pending.length;
  return pending.map((debt, index) => {
    const at = count === 1 ? start : start + (end - start) * index / (count - 1);
    return {row: index, hook_at_ms: at, total_ms: totalMs, preparation_ms: 0,
      staging_slot_wait_ms: stagingMs, simulation_audio_ms: 0.5,
      sample_pending_ticks: debt, sample_running: 1, sample_source_frame: 100 + index,
      sample_replay_cursor: 500 + index};
  });
}

const control = assessSustainedWindow({rows: rowsForWindow(0, 2000), incidents: [], startMs: 0, endMs: 2000});
assert.equal(control.status, 'clean');
assert.equal(control.first_input_cursor, 500);
assert.equal(control.last_original_frame, 102);

const preparation = assessSustainedWindow({rows: rowsForWindow(0, 2000).map((row, index) =>
  index === 1 ? {...row, preparation_ms: 0.25} : row),
incidents: [{reason: 7, source_frame: 101, at_ms: 1000}], startMs: 0, endMs: 2000});
assert.equal(preparation.status, 'inconclusive');
assert.ok(preparation.problems.includes('preparation_duration_observed'));
assert.ok(preparation.problems.includes('preparation_event_observed'));

const missingCursor = assessSustainedWindow({rows: rowsForWindow(0, 2000).map(row =>
  ({...row, sample_replay_cursor: null})), incidents: [], startMs: 0, endMs: 2000});
assert.ok(missingCursor.problems.includes('missing_callback_telemetry'));
const shortControl = assessSustainedWindow({rows: rowsForWindow(0, 1999), incidents: [], startMs: 0, endMs: 1999});
assert.ok(shortControl.problems.includes('window_shorter_than_required'));
const errorWindow = assessSustainedWindow({rows: rowsForWindow(0, 2000), incidents: [], startMs: 0, endMs: 2000,
  browserErrors: ['console error']});
assert.ok(errorWindow.problems.includes('browser_error'));

const recorder = {retained_records: [{incident: {reason: 'simulation_debt', staging: {wait_ms: 18}}}]};
const pauseRows = rowsForWindow(100, 300, [1, 3, 5], 12, 20);
pauseRows.push({...pauseRows.at(-1), row: 3, sample_running: 0, sample_replay_cursor: 503});
const positiveTreatment = assessSustainedWindow({rows: pauseRows,
  incidents: [], startMs: 100, endMs: 300, durationMs: 0, pauseTerminal: true});
assert.equal(positiveTreatment.status, 'clean');
assert.equal(positiveTreatment.callback_rows, 3);
assert.equal(positiveTreatment.pause_callback_row.sample_running, 0);
const gpuLoad = {requested_iterations: 600, requested_gpu_load_window_ms: 2000,
  actual_gpu_load_window_ms: 2001,
  batch_durations_ms: [2.1, 2.3],
  gpu_observation: {window_batches: 2, errors: [], on: false}};
const positive = classifySustainedAttempt({control, treatment: positiveTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad});
assert.equal(positive.result, 'positive_control');
assert.equal(positive.decision, 'propose_one_equivalent_ring_four_comparison');
assert.equal(positive.staging_wait_dominant, true);
assert.equal(positive.rising_callback_debt, true);
assert.deepEqual(positive.observed_batch_duration_stats_ms,
  {count: 2, min_ms: 2.1, mean_ms: 2.2, p50_ms: 2.3, p95_ms: 2.3, max_ms: 2.3});
assert.equal(positive.gpu_load_actual_minus_requested_ms, 1);
assert.equal(positive.gpu_load_observer_overshoot_ms, 1);

const cpuTreatment = assessSustainedWindow({rows: rowsForWindow(100, 300, [1, 3, 5], 4, 20),
  incidents: [], startMs: 100, endMs: 300, durationMs: 0});
const cpuPause = classifySustainedAttempt({control, treatment: cpuTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad});
assert.equal(cpuPause.result, 'classified_pause');
assert.equal(cpuPause.classification, 'cpu_or_callback_gap_dominated');
assert.equal(cpuPause.decision, 'park_ring_four');

const nonmonotonicPause = assessSustainedWindow({rows: rowsForWindow(100, 300, [1, 5, 2], 12, 20),
  incidents: [], startMs: 100, endMs: 300, durationMs: 0});
assert.equal(classifySustainedAttempt({control, treatment: nonmonotonicPause,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad}).classification,
'staging_wait_without_rising_callback_debt');
const invalidWait = assessSustainedWindow({rows: rowsForWindow(100, 300, [1, 3, 5], 25, 20),
  incidents: [], startMs: 100, endMs: 300, durationMs: 0});
assert.equal(classifySustainedAttempt({control, treatment: invalidWait,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad}).staging_wait_dominant, false);

const cleanTreatment = assessSustainedWindow({rows: rowsForWindow(100, 2100, [0, 1, 2], 2, 10),
  incidents: [], startMs: 100, endMs: 2100});
assert.deepEqual(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, recorder: {retained_records: []}, gpuLoad}),
{result: 'clean_nonreproduction', decision: 'park_ring_four',
  observed_batch_duration_stats_ms: {count: 2, min_ms: 2.1, mean_ms: 2.2, p50_ms: 2.3, p95_ms: 2.3, max_ms: 2.3},
  requested_gpu_load_window_ms: 2000, actual_gpu_load_window_ms: 2001,
  gpu_load_actual_minus_requested_ms: 1, gpu_load_observer_overshoot_ms: 1,
  gpu_load_early_stop_ms: 0});
assert.equal(classifySustainedAttempt({control, treatment: positiveTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder: {retained_records: []}, gpuLoad}).reason,
'genuine_staging_incident_not_exported');
assert.equal(classifySustainedAttempt({control, treatment: positiveTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 4}, recorder, gpuLoad}).reason,
'pause_reason_not_simulation_debt');
assert.equal(classifySustainedAttempt({control: preparation, treatment: positiveTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad}).decision,
'inspect_immediately');
assert.equal(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, stimulusValid: false, gpuLoad}).reason,
'invalid_stimulus');
assert.equal(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, gpuLoad: {...gpuLoad,
    gpu_observation: {...gpuLoad.gpu_observation, window_batches: 1}}}).reason,
'measured_600_gpu_window_invalid');
assert.equal(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, gpuLoad: {...gpuLoad,
    actual_gpu_load_window_ms: 2100}}).result, 'clean_nonreproduction');
assert.equal(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, gpuLoad: {...gpuLoad,
    actual_gpu_load_window_ms: 2101}}).reason, 'measured_600_gpu_window_invalid');
assert.equal(classifySustainedAttempt({control, treatment: cleanTreatment,
  terminal: {outcome: 'window_complete'}, gpuLoad: {...gpuLoad,
    batch_durations_ms: [2.1, 0]}}).reason, 'measured_600_gpu_window_invalid');
assert.equal(summarizeObservedBatchDurations([3, 1, 2]).mean_ms, 2);
assert.equal(summarizeObservedBatchDurations([]), null);

const teardownFailure = resolveSustainedAttempt({integrityErrors: ['artifact_postcheck_error'],
  control, treatment: positiveTreatment, terminal: {outcome: 'timing_pause', pause_reason: 1},
  recorder, gpuLoad});
assert.equal(teardownFailure.result, 'inconclusive');
assert.equal(teardownFailure.decision, 'inspect_immediately');
assert.equal(teardownFailure.reason, 'capture_or_cleanup_integrity_failed');
assert.deepEqual(teardownFailure.integrity_errors, ['artifact_postcheck_error']);
assert.equal(resolveSustainedAttempt({integrityErrors: [], control, treatment: positiveTreatment,
  terminal: {outcome: 'timing_pause', pause_reason: 1}, recorder, gpuLoad}).result, 'positive_control');

const localArtifacts = Object.fromEntries(Array.from({length: 31}, (_, index) => {
  const name = `browser-artifact-${String(index).padStart(2, '0')}`;
  return [name, {bytes: index + 1, sha256: String(index + 1).padStart(64, '0')}];
}));
const currentNames = Object.keys(localArtifacts);
const source = {commit: 'a'.repeat(40), tree: 'b'.repeat(40)};
assert.equal(artifactMapDigest(localArtifacts), artifactMapDigest(Object.fromEntries(
  Object.entries(localArtifacts).reverse())));
const prospectiveManifest = {schema: 'melee-web-h1-prospective-build-manifest-v1',
  binding_type: 'prospective_local_build_inventory', source,
  build_directory: '/frozen/release', inventory: {artifact_count: 31,
    artifact_map_sha256: artifactMapDigest(localArtifacts), artifacts: localArtifacts}};
assert.equal(validateProspectiveBuildManifest(prospectiveManifest, {source,
  buildDirectory: '/frozen/release', localArtifacts, expectedNames: currentNames}).valid, true);
const changedArtifacts = {...localArtifacts, 'browser-artifact-03':
  {bytes: 999, sha256: 'c'.repeat(64)}};
const changed = compareArtifactMaps(localArtifacts, changedArtifacts);
assert.equal(changed.matches, false);
assert.deepEqual(changed.differences.map(item => item.name), ['browser-artifact-03']);
assert.deepEqual(validateProspectiveBuildManifest(prospectiveManifest, {source,
  buildDirectory: '/frozen/release', localArtifacts: changedArtifacts, expectedNames: currentNames}).problems,
['local_artifact_map_mismatch']);
assert.ok(validateProspectiveBuildManifest(prospectiveManifest, {source: {...source, tree: 'd'.repeat(40)},
  buildDirectory: '/frozen/release', localArtifacts, expectedNames: currentNames}).problems.includes('source_identity_mismatch'));
assert.ok(validateProspectiveBuildManifest({...prospectiveManifest,
  inventory: {...prospectiveManifest.inventory, artifact_map_sha256: 'e'.repeat(64)}}, {source,
  buildDirectory: '/frozen/release', localArtifacts, expectedNames: currentNames}).problems.includes('manifest_artifact_digest_invalid'));
const artifact32 = {...localArtifacts, 'browser-artifact-31': {bytes: 32, sha256: '2'.repeat(64)}};
const names32 = [...currentNames, 'browser-artifact-31'];
const manifest32 = {...prospectiveManifest, inventory: {artifact_count: 32,
  artifact_map_sha256: artifactMapDigest(artifact32), artifacts: artifact32}};
assert.equal(validateProspectiveBuildManifest(manifest32, {source, buildDirectory: '/frozen/release',
  localArtifacts: artifact32, expectedNames: names32}).valid, true);
const missingOne = validateProspectiveBuildManifest(prospectiveManifest, {source,
  buildDirectory: '/frozen/release', localArtifacts: artifact32, expectedNames: names32});
assert.ok(missingOne.problems.includes('manifest_artifact_count_invalid'));
assert.ok(missingOne.problems.includes('manifest_artifact_names_mismatch'));
const unexpected = {...artifact32, 'browser-artifact-32': {bytes: 33, sha256: '3'.repeat(64)}};
const unexpectedManifest = {...prospectiveManifest, inventory: {artifact_count: 32,
  artifact_map_sha256: artifactMapDigest(unexpected), artifacts: unexpected}};
assert.ok(validateProspectiveBuildManifest(unexpectedManifest, {source,
  buildDirectory: '/frozen/release', localArtifacts: localArtifacts, expectedNames: currentNames})
  .problems.includes('manifest_artifact_names_mismatch'));
const oldHistoricalMap = {...localArtifacts,
  'browser-artifact-02': {bytes: 3, sha256: 'f'.repeat(64)},
  'browser-artifact-08': {bytes: 9, sha256: 'e'.repeat(64)}};
assert.deepEqual(compareArtifactMaps(oldHistoricalMap, localArtifacts).differences.map(item => item.name),
['browser-artifact-02', 'browser-artifact-08']);

console.log('Sustained staging-ring window qualification and frozen decision rules passed.');
