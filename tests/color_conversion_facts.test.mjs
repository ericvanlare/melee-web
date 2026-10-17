import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {readColorConversionDependencies} from './color_conversion_facts.mjs';

test('missing packet/companion never becomes content or restore admission', () => {
  const r = readColorConversionDependencies(null, null);
  assert.ok(r.failures.length); assert.equal(r.dependenciesObserved, false);
  assert.equal(r.packetContentQualified, false); assert.equal(r.restoreEligible, false);
  assert.equal(r.compositeEligible, false); assert.equal(r.internalGpuCreationOwnershipQualified, false);
});
const path = process.env.MELEE_FRAME_EFFECT_PACKET_REPORT;
const packetTest = (name, run) => test(name, {skip: !path}, run);
const packet = () => JSON.parse(readFileSync(path, 'utf8')).competitiveMatchStart.frame_effect_observation.renderer_status;
// Synthetic companion to exercise descriptor joins. These are NOT facts
// recovered from ba2b, and must never be saved as a purported original capture.
function syntheticCompanion(p) {
  const view = t => p.resources.find(r => r.kind === 1 && r.owner_id === t).capture_id;
  const uniform = p.provenance.find(r => r.role === 3).capture_id;
  return {schema: 'melee-web-color-conversion-facts-v1', packet_frame_id: p.renderer.frame_id,
    internal_gpu_creation_ownership_qualified: false, packet_content_qualified: false,
    restore_eligible: false, composite_eligible: false, overflow: false,
    facts: p.events.flatMap((e, event_index) => e.kind === 13 && (e.flags & 1) ? [{event_index,
      refusal: 0, returned: 1, gx_format: 4, filter: 0, draw_count: 3, load_op: 2, store_op: 1,
      uniform_offset: '0', uniform_size: '16', uniform_buffer: uniform,
      source_texture: e.handle_a, source_view: view(e.handle_a), destination_texture: e.handle_b,
      destination_view: view(e.handle_b), local_call_pipeline: '91', local_call_group: '92',
      local_call_sampler: '93', rect: [0, 0, e.value0, e.value1],
      uv_bits: [0x80000000, 0x7fc00001, 0x3ecccccd, 0x3f088889], clear_bits: ['0', '0', '0', '0']}] : [])};
}
packetTest('old ba2b has no companion and remains refused; synthetic joins retain original blockers', () => {
  const p = packet(); assert.deepEqual(readColorConversionDependencies(p, null).failures, ['missing_or_invalid_companion']);
  const r = readColorConversionDependencies(p, syntheticCompanion(p));
  assert.deepEqual(r.failures, []); assert.equal(r.dependenciesObserved, true);
  assert.deepEqual(r.baseline, {first_blocker: 7, finding_mask: '134217856'});
  assert.equal(r.conversions[0].reads.uv_bits[1], 0x7fc00001);
  assert.equal(r.internalGpuCreationOwnershipQualified, false); assert.equal(r.restoreEligible, false);
});
packetTest('synthetic missing End, alias, range, provenance, coverage and identity refusals', () => {
  for (const edit of [c => {c.facts[0].returned = 0;}, c => {c.facts[0].refusal = 6;},
    c => {c.facts[0].destination_texture = c.facts[0].source_texture;},
    c => {c.facts[0].uniform_offset = '18446744073709551615';},
    c => {c.facts[0].source_view = '0';}, c => {c.facts[0].local_call_pipeline = '0';},
    c => {c.facts.pop();}, c => {c.packet_frame_id = '0';},
    c => {c.facts[0].clear_bits[0] = '9223372036854775808';}]) {
    const p = packet(), c = syntheticCompanion(p); edit(c);
    assert.ok(readColorConversionDependencies(p, c).failures.length);
  }
});
