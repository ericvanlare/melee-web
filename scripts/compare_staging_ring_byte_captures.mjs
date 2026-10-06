#!/usr/bin/env node
/** Compare separately captured ring2/ring4 reports under one frozen input/build. */
import fs from 'node:fs/promises';
import path from 'node:path';
import {parseArgs} from 'node:util';
import {compareStagingRingByteCaptures} from '../tests/staging_ring_byte_capture.mjs';

const {values, positionals} = parseArgs({allowPositionals: true, options: {out: {type: 'string'}}});
if (positionals.length !== 2 || !values.out) {
  throw new Error('Use node scripts/compare_staging_ring_byte_captures.mjs RING2_REPORT RING4_REPORT --out NEW_COMPARISON_JSON');
}
const [ring2Path, ring4Path] = positionals.map(value => path.resolve(value));
const outputPath = path.resolve(values.out);
await fs.access(path.dirname(outputPath));
try { await fs.access(outputPath); throw new Error('Comparison output must not already exist'); }
catch (error) { if (error?.code !== 'ENOENT') throw error; }
const ring2 = JSON.parse(await fs.readFile(ring2Path, 'utf8'));
const ring4 = JSON.parse(await fs.readFile(ring4Path, 'utf8'));
function requireCondition(condition, message) { if (!condition) throw new Error(message); }
for (const [label, report] of [['ring2', ring2], ['ring4', ring4]]) {
  requireCondition(report?.result === 'captured', `${label} run did not complete a capture`);
  requireCondition(report.recipe_header?.version === 4 && report.recipe_header.frames === 600 &&
    report.recipe_header.profile_characters_hex === '07ff' && report.recipe_header.profile_stages_hex === '01c0',
  `${label} report is not the declared 600-frame synthetic MWRC v4 four-Mario CPU9 FD fixture`);
  requireCondition(report.capture_start?.start_cursor === 0 && report.capture_start?.source_frame === 1,
    `${label} capture was not armed for the first gameplay source submission`);
}
for (const key of ['disc', 'recipe']) {
  requireCondition(JSON.stringify(ring2.inputs?.[key]) === JSON.stringify(ring4.inputs?.[key]),
    `${key} input identity differs between ring runs`);
}
requireCondition(JSON.stringify(ring2.recipe_header) === JSON.stringify(ring4.recipe_header),
  'MWRC v4 seed, frame count, or profile differs between ring runs');
requireCondition(JSON.stringify(ring2.source) === JSON.stringify(ring4.source),
  'source identity differs between ring runs');
requireCondition(JSON.stringify(ring2.build_artifacts_before) === JSON.stringify(ring4.build_artifacts_before) &&
  JSON.stringify(ring2.build_artifacts_after) === JSON.stringify(ring4.build_artifacts_after) &&
  JSON.stringify(ring2.build_artifacts_before) === JSON.stringify(ring2.build_artifacts_after) &&
  JSON.stringify(ring4.build_artifacts_before) === JSON.stringify(ring4.build_artifacts_after),
  'browser runtime artifact identity differs or changed during a capture');
const byteComparison = compareStagingRingByteCaptures(ring2.capture, ring4.capture);
const result = {
  schema: 'melee-web-staging-ring-byte-comparison-v1',
  result: 'equal',
  claim: 'submitted-byte SHA-256 equality only; no GPU pixel, output image, or retail match identity claim',
  input: ring2.inputs,
  recipe_header: ring2.recipe_header,
  source: ring2.source,
  build_artifacts: ring2.build_artifacts_before,
  ring2: {requested_frame_slots: ring2.requested_frame_slots,
    observed_frame_slots: ring2.ring_status?.frame_slots,
    reserved_gpu_buffer_bytes: ring2.ring_status?.reserved_gpu_buffer_bytes},
  ring4: {requested_frame_slots: ring4.requested_frame_slots,
    observed_frame_slots: ring4.ring_status?.frame_slots,
    reserved_gpu_buffer_bytes: ring4.ring_status?.reserved_gpu_buffer_bytes},
  memory_observations: {
    live_gpu_memory: 'not exposed by WebGPU; native allocator, Wasm heap and JS heap snapshots are retained per run',
    ring2: {before: ring2.memory_before, after: ring2.memory_after},
    ring4: {before: ring4.memory_before, after: ring4.memory_after},
  },
  byte_comparison: byteComparison,
};
await fs.writeFile(outputPath, JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({result: result.result, frames: byteComparison.frames,
  fingerprinted_write_count: byteComparison.fingerprinted_write_count,
  fixture_sha256: ring2.inputs.recipe.sha256}));
