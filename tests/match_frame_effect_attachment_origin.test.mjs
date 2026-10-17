import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {classifyFrameEffectAttachmentOrigins, projectAttachmentCandidates} from './match_frame_effect_attachment_origin.mjs';

// Deliberately reduced projection fixture; never passed off as a strict packet.
function projection() {
  return {resources: [
    {capture_id: '8', owner_id: '1', kind: 2, word_count: 10,
      words: [640, 480, 1, 27, 2, 1, 1, 23, 0, 0]},
    ...['7', '17'].map(capture_id => ({capture_id, owner_id: '8', kind: 1, word_count: 10,
      words: [8, 0, 0, 2, 0, 0xffffffff, 0, 0xffffffff, 0, 0]}))],
    provenance: [{capture_id: '8', role: 15, slot: 0, device_id: '1', renderer_epoch: '1', creation_serial: '2'},
      ...['7', '17'].map((capture_id, i) => ({capture_id, role: 16, slot: 0,
        device_id: '1', renderer_epoch: '1', creation_serial: String(i + 3)}))],
    events: [{kind: 2, op_index: 0},
      {kind: 3, op_index: 0, handle_a: '7', value0: 2, value1: 1, value2: 640, value3: 480},
      {kind: 6, op_index: 0}, {kind: 2, op_index: 1},
      {kind: 3, op_index: 1, handle_a: '17', value0: 1, value1: 1, value2: 640, value3: 480}],
    attachment_facts: [1, 4].map((event_index, i) => ({event_index, aspects: 1,
      view_id: i ? '17' : '7', resolve_view_id: '0',
      color_clear_bits: ['9223372036854775808', '9221120237041090561', '0', '4607182418800017408'],
      stencil_clear_value: 0}))};
}
test('projected aliases join exact parent/subresource; draw remains a possible write', () => {
  const result = projectAttachmentCandidates(projection()), [relation] = result.clear_load_candidates;
  assert.equal(result.clear_load_candidates.length, 1);
  assert.equal(relation.clear_view_id, '7'); assert.equal(relation.subresource.view_id, '17');
  assert.equal(relation.subresource.texture_id, '8'); assert.equal(relation.possible_draw_writes, 1);
  assert.equal(relation.clear_bits[0], '9223372036854775808');
  assert.equal(relation.clear_bits[1], '9221120237041090561');
});
test('missing clear and discard cannot seed later LOAD', () => {
  for (const edit of [r => {r.events[1].value0 = 1;}, r => {r.events[1].value1 = 2;}]) {
    const raw = projection(); edit(raw);
    const result = projectAttachmentCandidates(raw);
    assert.equal(result.clear_load_candidates.length, 0);
    assert.ok(result.unresolved.some(r => r.reason === 'load_without_stored_clear_origin'));
  }
});
test('projected depth preserves integer clear bits and exact aspect', () => {
  const raw = projection();
  for (const owner of raw.provenance) owner.slot = 2;
  raw.resources[0].words[3] = 48;
  for (const index of [1, 4]) {
    raw.events[index].kind = 4;
    raw.events[index].value2 = raw.events[index].value3 = 0;
  }
  for (const fact of raw.attachment_facts) {
    fact.aspects = 2; fact.depth_clear_bits = 0x80000000;
    fact.color_clear_bits = ['0', '0', '0', '0'];
  }
  const [relation] = projectAttachmentCandidates(raw).clear_load_candidates;
  assert.equal(relation.clear_bits, 0x80000000);
  assert.equal(relation.subresource.aspect, 2);
});
test('alias bounds, parent encoding, aspect and exact extent refuse', () => {
  for (const edit of [r => {r.resources[2].words[0] = 9;},
    r => {r.resources[2].words[4] = 1;}, r => {r.resources[2].words[8] = 3;},
    r => {r.events[4].value2--;}, r => {r.attachment_facts[1].aspects = 2;}]) {
    const raw = projection(); edit(raw);
    assert.equal(projectAttachmentCandidates(raw).clear_load_candidates.length, 0);
    assert.ok(projectAttachmentCandidates(raw).progression_stopped_at !== null);
  }
});
test('distinct layer is not an alias; repeated same-pass alias refuses', () => {
  const raw = projection(); raw.resources[0].words[2] = 2;
  for (const view of raw.resources.slice(1)) view.words[7] = 1;
  raw.resources[2].words[6] = 1;
  assert.equal(projectAttachmentCandidates(raw).clear_load_candidates.length, 0);
  const overlap = projection(); overlap.events[3].op_index = 0;
  overlap.events[4].op_index = 0;
  // Remove the second pass start so both attachments share one active pass.
  overlap.events[3].kind = 19;
  assert.ok(projectAttachmentCandidates(overlap).unresolved.some(r => r.reason === 'overlapping_attachment_alias'));
});
test('unresolved resolve stops progression permanently, even through later clear', () => {
  const raw = projection(); raw.events[2] = {kind: 13, op_index: 0, flags: 1};
  raw.events[4].value0 = 2;
  const result = projectAttachmentCandidates(raw);
  assert.equal(result.progression_stopped_at, 2);
  assert.equal(result.clear_load_candidates.length, 0);
  assert.equal(result.attachments[1].progression, 'blocked');
});
test('malformed wrapper refuses before projection without a private report', () => {
  for (const raw of [null, {}, {competitiveMatchStart: {frame_effect_observation: {
    renderer_status: {}, retirement: {status: {}}}}}]) {
    const result = classifyFrameEffectAttachmentOrigins(raw);
    assert.equal(result.status, 'refused');
    assert.ok(result.strict_failures.length > 0);
    assert.equal(result.attachments.length, 0);
    assert.equal(result.clear_load_candidates.length, 0);
    assert.equal(result.restoreEligible, false);
    assert.equal(result.compositeEligible, false);
  }
});
const packetPath = process.env.MELEE_FRAME_EFFECT_PACKET_REPORT;
const packetTest = (name, run) => test(name, {skip: !packetPath}, run);
const report = () => JSON.parse(readFileSync(packetPath, 'utf8'));
packetTest('retained ba2b preserves raw findings and refuses at first conversion', () => {
  const result = classifyFrameEffectAttachmentOrigins(report());
  assert.deepEqual(result.strict_failures, []);
  assert.deepEqual(result.baseline, {first_blocker: 7, finding_mask: '134217856'});
  assert.equal(result.status, 'refused'); assert.equal(result.packetContentQualified, false);
  assert.equal(result.restoreEligible, false); assert.equal(result.compositeEligible, false);
  assert.equal(result.attachments.length, 6); assert.equal(result.progression_stopped_at, 121);
  assert.equal(result.clear_load_candidates.length, 0);
  assert.ok(result.unresolved.some(r => r.reason === 'resolve_contents_unresolved'));
});
packetTest('strict malformed owner fails before projection; valid copy mismatch is distinct', () => {
  const raw = report(), root = raw.competitiveMatchStart.frame_effect_observation;
  root.renderer_status.provenance[0].device_id = null;
  assert.ok(classifyFrameEffectAttachmentOrigins(raw).strict_failures.length > 0);
  const other = report();
  other.competitiveMatchStart.frame_effect_observation.retirement.status.attachment_facts[0].color_clear_bits[0] = '1';
  const result = classifyFrameEffectAttachmentOrigins(other);
  assert.deepEqual(result.strict_failures, []);
  assert.equal(result.unresolved[0].reason, 'packet_copy_disagreement:attachment_facts');
});
