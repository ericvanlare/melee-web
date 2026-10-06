import {compareStagingRingByteCaptures} from './staging_ring_byte_capture.mjs';
import {classifyStagingByteReplayCompletion} from './staging_ring_replay_completion.mjs';
const cleanDiff = 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855';
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
function require(condition, message) { if (!condition) throw Error(message); }
export function compareStagingRingReports(two, four, {artifactNames, sourceDifference,
  recoverMissingRing2PostMap = false} = {}) {
  require(Array.isArray(artifactNames) && artifactNames.length === 31,
    'Comparison requires the complete 31-file runtime artifact inventory');
  const completion = {};
  for (const [label, report, slots] of [['ring2', two, 2], ['ring4', four, 4]]) {
    completion[label] = classifyStagingByteReplayCompletion(report?.native_replay_report);
    require(report.requested_frame_slots === slots && report.ring_status?.frame_slots === slots &&
      report.ring_status?.staging_buffers === slots && report.ring_status?.selection?.byte_hash_enabled === true,
    `${label} requested/observed ring identity is invalid`);
    require(report.recipe_header?.version === 4 && report.recipe_header.frames === 600 &&
      report.recipe_header.profile_characters_hex === '07ff' && report.recipe_header.profile_stages_hex === '01c0',
    `${label} fixture is not the declared bounded synthetic MWRCv4`);
    require(report.capture_start?.cap === 600 && report.capture_start.start_cursor === 0 &&
      report.capture_start.source_frame === 0, `${label} first completed-input draw was not armed`);
    require(Array.isArray(report.browser_errors) && report.browser_errors.length === 0 &&
      !report.close_error && !report.capture_read_error && !report.screenshot_error,
    `${label} browser/capture cleanup contains errors`);
    require(report.source?.tracked_diff_sha256 === cleanDiff, `${label} capture source was not clean`);
    if (report.result !== 'captured') {
      require(report.result === 'fail' && typeof report.failure === 'string', `${label} capture result is unsupported`);
      const firstLine = report.failure.split('\n')[0];
      const prefix = 'Error: Native replay has blocking failures: ';
      require(firstLine.startsWith(prefix), `${label} failure is not a retained performance classification`);
      let reasons;
      try { reasons = JSON.parse(firstLine.slice(prefix.length)); } catch {}
      require(Array.isArray(reasons) && reasons.length && reasons.every(reason =>
        completion[label].excluded_performance_failures.includes(reason)),
      `${label} retained failure is not an excluded performance observation`);
    } else require(!report.failure, `${label} captured result has an unexpected failure`);
  }
  for (const key of ['disc', 'recipe', 'recipe_sidecar_sha256'])
    require(two.inputs?.[key] !== undefined && same(two.inputs[key], four.inputs?.[key]), `${key} identity differs`);
  require(same(two.recipe_header, four.recipe_header), 'Recipe seed/profile identity differs');
  require(two.source.aurora_patch_sha256 === four.source.aurora_patch_sha256,
    'Aurora patch identity differs');
  require(sourceDifference?.verified === true && sourceDifference.ring2_commit === two.source.commit &&
    sourceDifference.ring4_commit === four.source.commit && sourceDifference.ring2_tree === two.source.tree &&
    sourceDifference.ring4_tree === four.source.tree && Array.isArray(sourceDifference.changed_paths) &&
    sourceDifference.changed_paths.every(name => /^tests\/[^\n]+\.mjs$/.test(name)),
  'Capture source difference is not a verified harness-only change');
  const maps = [['ring2.before', two.build_artifacts_before], ['ring4.before', four.build_artifacts_before],
    ['ring4.after', four.build_artifacts_after]];
  let artifactBinding = 'complete observed pre/post manifests';
  if (two.build_artifacts_after) maps.push(['ring2.after', two.build_artifacts_after]);
  else {
    require(recoverMissingRing2PostMap, 'ring2 post-capture artifact map is missing; explicit recovery required');
    artifactBinding = 'conditional artifact binding: missing ring2 post-map; ring4 matching later before/after maps are subsequent unchanged-build witnesses';
  }
  const baseline = two.build_artifacts_before;
  for (const [label, map] of maps) {
    require(map && same(Object.keys(map).sort(), artifactNames.slice().sort()), `${label} artifact inventory is incomplete`);
    for (const name of artifactNames) {
      require(Number.isSafeInteger(map[name]?.bytes) && map[name].bytes > 0 &&
        /^[0-9a-f]{64}$/.test(map[name].sha256), `${label} artifact identity is invalid: ${name}`);
      require(same(map[name], baseline[name]), `${label} frozen artifact identity changed: ${name}`);
    }
  }
  return {schema: 'melee-web-staging-ring-byte-comparison-v2', result: 'equal',
    claim: 'functional submitted-byte SHA-256/length/order/offset equality only; no performance, pixel or retail match identity claim',
    artifact_binding: artifactBinding, missing_ring2_post_map: !two.build_artifacts_after,
    observed_artifact_maps: Object.fromEntries(maps), input: two.inputs, recipe_header: two.recipe_header,
    source_identities: {ring2: two.source, ring4: four.source}, source_difference: sourceDifference,
    native_completion: completion, original_results: {ring2: two.result, ring4: four.result},
    ring_status: {ring2: two.ring_status, ring4: four.ring_status},
    byte_comparison: compareStagingRingByteCaptures(two.capture, four.capture),
    memory_observations: {scope: 'Wasm/native allocator/JS snapshots; GPU reservation is not observed physical GPU memory',
      ring2: {before: two.memory_before, after: two.memory_after},
      ring4: {before: four.memory_before, after: four.memory_after}}};
}
