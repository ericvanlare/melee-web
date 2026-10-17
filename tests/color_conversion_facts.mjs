import {firstDrawObservationFailures} from './match_frame_effect_first_draw_driver.mjs';

const exact = (v, keys) => v && typeof v === 'object' && !Array.isArray(v) &&
  Object.keys(v).length === keys.length && keys.every(k => Object.hasOwn(v, k));
const u32 = n => Number.isInteger(n) && n >= 0 && n <= 0xffffffff;
const u64 = n => typeof n === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(n) && BigInt(n) <= 0xffffffffffffffffn;
const ids = 'uniform_offset uniform_size source_texture source_view destination_texture destination_view uniform_buffer local_call_pipeline local_call_group local_call_sampler'.split(' ');
const numbers = 'event_index refusal returned gx_format filter draw_count load_op store_op'.split(' ');

// Additive companion only. No historical schema coercion and no change to the
// attachment candidate classifier. Local call handles are NOT creation tokens.
export function readColorConversionDependencies(packet, companion) {
  const out = {scope: 'returned_color_conversion_dependency_facts_only',
    dependenciesObserved: false, packetContentQualified: false, restoreEligible: false,
    compositeEligible: false, internalGpuCreationOwnershipQualified: false,
    failures: [], conversions: [], baseline: {
      first_blocker: packet?.renderer?.first_blocker ?? null,
      finding_mask: packet?.renderer?.finding_mask ?? null}};
  try { out.failures.push(...firstDrawObservationFailures(packet)); }
  catch { out.failures.push('strict_packet_decoder_exception'); }
  if (out.failures.length) return out;
  if (!exact(companion, ['schema', 'packet_frame_id', 'internal_gpu_creation_ownership_qualified',
    'packet_content_qualified', 'restore_eligible', 'composite_eligible', 'overflow', 'facts']) ||
      companion.schema !== 'melee-web-color-conversion-facts-v1' ||
      companion.packet_frame_id !== packet.renderer.frame_id || companion.overflow !== false ||
      ['internal_gpu_creation_ownership_qualified', 'packet_content_qualified', 'restore_eligible',
        'composite_eligible'].some(k => companion[k] !== false) || !Array.isArray(companion.facts)) {
    out.failures.push('missing_or_invalid_companion'); return out;
  }
  const expected = packet.events.flatMap((e, index) => e.kind === 13 && (e.flags & 1) ? [index] : []);
  if (companion.facts.length !== expected.length || !expected.length) {
    out.failures.push('conversion_fact_coverage'); return out;
  }
  const resources = new Map(packet.resources.map(r => [r.capture_id, r]));
  const owners = new Map(packet.provenance.map(r => [r.capture_id, r]));
  const viewParent = (viewId, textureId) => {
    const v = resources.get(viewId), t = resources.get(textureId);
    const vp = owners.get(viewId), tp = owners.get(textureId);
    if (v?.kind !== 1 || t?.kind !== 2 || !vp || !tp ||
        v.owner_id !== textureId || vp.device_id !== tp.device_id ||
        vp.renderer_epoch !== tp.renderer_epoch ||
        (BigInt(v.words[0]) | BigInt(v.words[1]) << 32n).toString() !== textureId) return null;
    if (t.words[4] !== 2 || t.words[2] !== 1 || t.words[5] !== 1 || t.words[6] !== 1 ||
        v.words[3] !== 2 || v.words[4] !== 0 || v.words[6] !== 0 || v.words[8] !== 0 ||
        ![1, 0xffffffff].includes(v.words[5]) || ![1, 0xffffffff].includes(v.words[7]) ||
        (v.words[2] !== 0 && v.words[2] !== t.words[3])) return null;
    return {texture_id: textureId, view_id: viewId, texture_serial: tp.creation_serial,
      view_serial: vp.creation_serial, device_id: tp.device_id, renderer_epoch: tp.renderer_epoch,
      descriptor: t.words.slice(0, t.word_count), view_descriptor: v.words.slice(0, v.word_count)};
  };
  for (const [ordinal, f] of companion.facts.entries()) {
    if (!exact(f, [...numbers, ...ids, 'rect', 'uv_bits', 'clear_bits']) ||
        !numbers.every(k => u32(f[k])) || !ids.every(k => u64(f[k])) ||
        !Array.isArray(f.rect) || f.rect.length !== 4 || !f.rect.every(n => Number.isInteger(n) && n >= 0 && n <= 0x7fffffff) ||
        !Array.isArray(f.uv_bits) || f.uv_bits.length !== 4 || !f.uv_bits.every(u32) ||
        !Array.isArray(f.clear_bits) || f.clear_bits.length !== 4 || !f.clear_bits.every(u64)) {
      out.failures.push('conversion_fact_shape'); continue;
    }
    const e = packet.events[expected[ordinal]];
    const source = viewParent(f.source_view, f.source_texture);
    const destination = viewParent(f.destination_view, f.destination_texture);
    const buffer = resources.get(f.uniform_buffer), bp = owners.get(f.uniform_buffer);
    if (f.event_index !== expected[ordinal] || e.flags !== 1 || f.refusal !== 0 || f.returned !== 1 ||
        f.filter !== 0 || f.draw_count !== 3 || f.load_op !== 2 || f.store_op !== 1 ||
        f.clear_bits.some(n => n !== '0') || f.uniform_size !== '16' ||
        [f.local_call_pipeline, f.local_call_group, f.local_call_sampler].some(n => n === '0') ||
        f.source_texture !== e.handle_a || f.destination_texture !== e.handle_b ||
        f.source_texture === f.destination_texture || !source || !destination ||
        source.device_id !== destination.device_id || buffer?.kind !== 3 || bp?.role !== 3 ||
        bp.device_id !== source.device_id || bp.renderer_epoch !== source.renderer_epoch ||
        BigInt(f.uniform_offset) + 16n > (BigInt(buffer.words[0]) | BigInt(buffer.words[1]) << 32n) ||
        f.rect[2] !== e.value0 || f.rect[3] !== e.value1 || !f.rect[2] || !f.rect[3] ||
        (source && (f.rect[0] + f.rect[2] > source.descriptor[0] || f.rect[1] + f.rect[3] > source.descriptor[1])) ||
        (destination && (f.rect[2] !== destination.descriptor[0] || f.rect[3] !== destination.descriptor[1]))) {
      out.failures.push(`conversion_refused:${f.event_index}`); continue;
    }
    out.conversions.push({event_index: f.event_index, authenticated_texture_owner: {source, destination},
      reads: {source_view: f.source_view, rect: f.rect.slice(), filter: f.filter,
        uniform_buffer: f.uniform_buffer, offset: f.uniform_offset, size: f.uniform_size, uv_bits: f.uv_bits.slice()},
      writes: {destination_view: f.destination_view, load_op: f.load_op, store_op: f.store_op,
        clear_bits: f.clear_bits.slice(), draw_count: f.draw_count, returned_end: true},
      gx_format: f.gx_format, local_call_identity: {
        pipeline: f.local_call_pipeline, group: f.local_call_group, sampler: f.local_call_sampler}});
  }
  out.dependenciesObserved = out.failures.length === 0;
  return out;
}
