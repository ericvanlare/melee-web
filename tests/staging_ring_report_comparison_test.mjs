import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {compareStagingRingReports as compare} from './staging_ring_report_comparison.mjs';
const artifactNames = JSON.parse(fs.readFileSync(new URL('../tools/browser_build_artifacts.json', import.meta.url)));
function fixtures() {
  const source = {commit: 'a'.repeat(40), tree: 'b'.repeat(40),
    tracked_diff_sha256: 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855',
    aurora_patch_sha256: 'c'.repeat(64)};
  const map = Object.fromEntries(artifactNames.map(name => [name, {bytes: 16, sha256: 'd'.repeat(64)}]));
  const report = slots => {
    const ring = {frame_slots: slots, staging_buffers: slots, selection: {byte_hash_enabled: true}};
    return {result: 'captured', requested_frame_slots: slots, ring_status: ring, browser_errors: [], source: structuredClone(source),
      inputs: {disc: {sha256: 'e'.repeat(64)}, recipe: {sha256: 'f'.repeat(64)}, recipe_sidecar_sha256: '1'.repeat(64)},
      recipe_header: {version: 4, frames: 600, profile_characters_hex: '07ff', profile_stages_hex: '01c0'},
      capture_start: {cap: 600, start_cursor: 0, source_frame: 0},
      native_replay_report: {complete: true, frames: 600, instrumented_timing_resumes: 0, pass: false,
        failures: ['livePipelinesCreated'], metrics: {sourceFrames: 600, sourceSteps: 600, sourceDraws: 600, focusLost: false}},
      build_artifacts_before: structuredClone(map), build_artifacts_after: structuredClone(map),
      capture: {schema: 'melee-web-staging-byte-hashes-v1', status: 'complete', frame_count: 600,
        pending_frame_count: 0, expected_cursor: 600, overflow: false, errors: [], requested_slots: slots, ring_status: ring,
        frames: Array.from({length: 600}, (_, index) => ({gameplay_ordinal: index + 1, source_cursor: index,
          source_frame: Math.max(0, index - 120), source_draw_ordinal: index + 1, writes: []}))}};
  };
  return {two: report(2), four: report(4), options: {artifactNames, sourceDifference: {verified: true,
    ring2_commit: source.commit, ring4_commit: source.commit, ring2_tree: source.tree, ring4_tree: source.tree,
    changed_paths: []}}};
}
test('missing post-map rejects by default; explicit recovery binds subsequent complete maps conditionally', () => {
  const {two, four, options} = fixtures(); delete two.build_artifacts_after;
  assert.throws(() => compare(two, four, options), /explicit recovery required/);
  const result = compare(two, four, {...options, recoverMissingRing2PostMap: true});
  assert.equal(result.missing_ring2_post_map, true);
  assert.match(result.artifact_binding, /conditional artifact binding/);
});
test('rejects absent completion, wrong rings, mutation, structural failure and cleanup errors', () => {
  for (const change of [
    f => delete f.two.native_replay_report,
    f => f.four.ring_status.frame_slots = 2,
    f => f.four.build_artifacts_after[artifactNames[0]].sha256 = '2'.repeat(64),
    f => delete f.four.build_artifacts_before[artifactNames[1]],
    f => f.two.native_replay_report.failures.push('teardown incomplete'),
    f => f.two.close_error = 'cleanup failed',
    f => f.two.browser_errors.push('runtime error'),
    f => f.two.source.aurora_patch_sha256 = '9'.repeat(64),
    f => f.options.sourceDifference.changed_paths.push('web/melee-runtime.mjs'),
  ]) {
    const f = fixtures(); change(f); assert.throws(() => compare(f.two, f.four, f.options));
  }
});
test('retains original failed performance result only with exact validated completion and known failure', () => {
  const {two, four, options} = fixtures();
  two.result = 'fail'; two.failure = 'Error: Native replay has blocking failures: ["livePipelinesCreated"]\n at capture';
  assert.equal(compare(two, four, options).original_results.ring2, 'fail');
  two.failure = 'Error: Runtime preparation failed';
  assert.throws(() => compare(two, four, options), /retained performance classification/);
});
