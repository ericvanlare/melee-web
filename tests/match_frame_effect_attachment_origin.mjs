import {isDeepStrictEqual} from 'node:util';
import {firstDrawObservationFailures} from './match_frame_effect_first_draw_driver.mjs';

const PACKET = ['renderer', 'events', 'handles', 'resources', 'binding_entries',
  'layout_entries', 'provenance', 'attachment_facts', 'draws'];
// These rows cannot establish texture contents. Buffer effects remain excluded.
const BOOKKEEPING = new Set([1, 10, 12, 16, 17, 18, 19, 20, 21, 22, 24]);
const OPAQUE = new Map([[5, 'palette'], [7, 'custom_draw'], [8, 'encoder_task'],
  [9, 'texture_copy'], [11, 'texture_upload'], [13, 'resolve'],
  [14, 'color_snapshot'], [15, 'depth_snapshot'], [23, 'depth_read']]);

function alias(viewId, aspect, resources, provenance) {
  const view = resources.get(viewId), owner = provenance.get(viewId);
  const texture = resources.get(view?.owner_id), parent = provenance.get(view?.owner_id);
  if (view?.kind !== 1 || view.word_count !== 10 || texture?.kind !== 2 ||
      texture.word_count < 10 || !owner || !parent ||
      owner.device_id !== parent.device_id || owner.renderer_epoch !== parent.renderer_epoch)
    return null;
  const v = view.words, t = texture.words;
  const encodedParent = (BigInt(v[0]) | BigInt(v[1]) << 32n).toString();
  const mips = v[5] === 0xffffffff ? t[5] - v[4] : v[5];
  const layers = v[7] === 0xffffffff ? t[2] - v[6] : v[7];
  // Smallest scope: authored persistent 2D attachments, default all-aspect view,
  // one exact mip/layer. No format reinterpretation, stencil, arrays or 3D.
  if (parent.role !== 15 || owner.role !== 16 || owner.slot !== parent.slot ||
      !((parent.slot === 0 && aspect === 1) || (parent.slot === 2 && aspect === 2)) ||
      encodedParent !== view.owner_id || t[4] !== 2 || v[3] !== 2 ||
      (v[2] !== 0 && v[2] !== t[3]) || v[8] !== 0 ||
      !t[0] || !t[1] || !t[2] || !t[5] || !t[6] ||
      mips !== 1 || layers !== 1 || v[4] >= t[5] || v[6] >= t[2]) return null;
  return {texture_id: view.owner_id, view_id: viewId, device_id: parent.device_id,
    texture_serial: parent.creation_serial, view_serial: owner.creation_serial,
    renderer_epoch: parent.renderer_epoch, mip: v[4], layer: v[6], aspect,
    format: t[3], view_format: v[2], dimension: t[4], view_dimension: v[3],
    width: Math.max(1, Math.floor(t[0] / 2 ** v[4])),
    height: Math.max(1, Math.floor(t[1] / 2 ** v[4])), samples: t[6]};
}

// Pure dependency projection, not a packet validator or admission API. The
// public classifier below gates it with unchanged strict first-draw/owner checks.
// Reduced synthetic controls exercise this projection without claiming GPU work.
export function projectAttachmentCandidates(receipt) {
  const resources = new Map(receipt.resources.map(r => [r.capture_id, r]));
  const provenance = new Map(receipt.provenance.map(r => [r.capture_id, r]));
  const facts = new Map(receipt.attachment_facts.map(f => [f.event_index, f]));
  const origins = new Map(), attachments = [], candidates = [], unresolved = [];
  let pass = null, active = new Set(), barrier = null;
  const refuse = (reason, index) => { unresolved.push({reason, event_index: index}); };
  const halt = (reason, index) => {
    refuse(reason, index);
    barrier ??= index;
    origins.clear();
  };
  for (const [index, event] of receipt.events.entries()) {
    if (event.kind === 2) { pass = event; active = new Set(); continue; }
    if (event.kind === 6) {
      for (const key of active) {
        const origin = origins.get(key);
        if (origin) origin.possible_draw_writes++;
      }
      continue;
    }
    if (event.kind !== 3 && event.kind !== 4) {
      if (!BOOKKEEPING.has(event.kind))
        halt(`${OPAQUE.get(event.kind) ?? 'unknown_operation'}_contents_unresolved`, index);
      continue;
    }
    const fact = facts.get(index), aspect = event.kind === 3 ? 1 : 2;
    const subresource = alias(event.handle_a, aspect, resources, provenance);
    const load = event.value0, store = event.value1;
    const clearBits = event.kind === 3 ? fact?.color_clear_bits?.slice() : fact?.depth_clear_bits;
    const row = {event_index: index, pass_index: event.op_index, subresource,
      load_op: load, store_op: store, clear_bits: clearBits,
      stencil_clear_value: fact?.stencil_clear_value ?? null,
      extent: subresource ? [subresource.width, subresource.height] : null,
      progression: barrier === null ? 'candidate_only' : 'blocked'};
    attachments.push(row);
    if (!pass || pass.op_index !== event.op_index || !subresource ||
        fact?.aspects !== aspect || fact.view_id !== event.handle_a ||
        fact.resolve_view_id !== '0' || ![1, 2].includes(load) || ![1, 2].includes(store) ||
        (event.kind === 3 && (event.value2 !== subresource.width || event.value3 !== subresource.height)) ||
        (event.kind === 4 && (event.value2 !== 0 || event.value3 !== 0))) {
      row.progression = 'refused';
      halt('attachment_alias_extent_or_operation_unresolved', index); continue;
    }
    if (barrier !== null) continue; // Never restart after unresolved content work.
    const key = [subresource.texture_id, subresource.texture_serial,
      subresource.mip, subresource.layer, aspect].join(':');
    if (active.has(key)) { halt('overlapping_attachment_alias', index); continue; }
    active.add(key);
    if (load === 2) {
      origins.set(key, {clear_event_index: index, clear_view_id: event.handle_a,
        clear_bits: clearBits, possible_draw_writes: 0});
    } else {
      const origin = origins.get(key);
      if (origin) candidates.push({...origin, load_event_index: index, subresource});
      else refuse('load_without_stored_clear_origin', index);
    }
    if (store === 2) {
      origins.delete(key);
      refuse('attachment_store_discards_origin', index);
    }
  }
  return {attachments, clear_load_candidates: candidates, unresolved,
    progression_stopped_at: barrier};
}

export function classifyFrameEffectAttachmentOrigins(report) {
  const observation = report?.competitiveMatchStart?.frame_effect_observation;
  const current = observation?.renderer_status, retired = observation?.retirement?.status;
  const result = {schema: 'melee-web-attachment-origin-component-v1', status: 'refused',
    scope: 'attachment_clear_load_candidates_only', restoreEligible: false,
    compositeEligible: false, packetContentQualified: false,
    baseline: {first_blocker: current?.renderer?.first_blocker ?? null,
      finding_mask: current?.renderer?.finding_mask ?? null},
    strict_failures: [], attachments: [], clear_load_candidates: [], unresolved: [],
    progression_stopped_at: null,
    exclusions: ['sampled_input_contents_and_pair_lifetime', 'buffer_contents',
      'conversion_and_other_output_contents', 'native_refs_device_futures', 'restore_repeat']};
  // firstDrawObservationFailures already calls rendererOwnerObservationFailures.
  // No duplicated event/pass/draw validation framework is added here.
  try {
    for (const [name, receipt] of [['current', current], ['retired', retired]])
      result.strict_failures.push(...firstDrawObservationFailures(receipt).map(f => `${name}:${f}`));
  } catch { result.strict_failures.push('strict_decoder_exception'); }
  if (result.strict_failures.length) return result;
  if (current.schema_version !== 6 || retired.schema_version !== 6) {
    result.unresolved.push({reason: 'exact_renderer5_owner_facts_required', event_index: null});
    return result;
  }
  const differing = PACKET.find(key => !isDeepStrictEqual(current[key], retired[key]));
  if (differing) {
    result.unresolved.push({reason: `packet_copy_disagreement:${differing}`, event_index: null});
    return result;
  }
  return Object.assign(result, projectAttachmentCandidates(current));
}
