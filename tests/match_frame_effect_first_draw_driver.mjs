import {rendererOwnerObservationFailures} from './match_frame_effect_renderer_owners.mjs';
// Explicit first-draw scope. The historical single-tick driver/validator is unchanged.
// Reuses the reviewed schema2 row/value validation with a distinct exact outer
// field allowlist and selected-map checks. No historical receipt/schema coercion.
export function armNativeFrameEffectFirstDraw({minimumFrame = 180, maximumFrame = 240} = {}) {
  const module = globalThis.Module;
  if (module?.matchFrameEffectNativeIdle?.() !== true)
    return {armed: false, reason: 'native_call_pending'};
  if (typeof module._melee_web_native_menu_frame_effect_arm_first_draw !== 'function' ||
      typeof module._melee_web_native_menu_frame_effect_status !== 'function')
    return {armed: false, reason: 'private_exports_missing'};
  const match = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
  if (match.ready !== true || !Number.isInteger(match.frame) ||
      match.frame < minimumFrame || match.frame > maximumFrame)
    return {armed: false, reason: 'source_frame_outside_declared_window', match};
  const armed = module._melee_web_native_menu_frame_effect_arm_first_draw(match.frame) === 1;
  return {armed, reason: armed ? null : 'native_arm_refused', match};
}

export function readNativeFrameEffectFirstDraw() {
  const module = globalThis.Module;
  // No native getter even on a timeout until the actual callback returns.
  if (module?.matchFrameEffectNativeIdle?.() !== true ||
      module.matchFrameEffectCallbackReturned !== true)
    return {settled: false};
  const status = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_frame_effect_status()));
  if (status.busy === true) return {settled: false};
  const renderer = status.renderer;
  const completed = (status.schema_version === 3 || status.schema_version === 4 || status.schema_version === 5 || status.schema_version === 6) && status.terminal === true &&
    status.selected_depth_owners?.pending === 0 && (status.error !== '' ||
    renderer?.state === 5 || renderer?.fence_count === 1);
  return {settled: true, completed, status};
}

export function firstDrawObservationFailures(receipt) {
  const textureOwners = receipt?.schema_version === 6;
  const attachmentOwners = receipt?.schema_version === 5 || textureOwners;
  const currentOwners = receipt?.schema_version === 4 || attachmentOwners;
  const failures = [];
  const uint32 = value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
  const uint64 = value => typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
    BigInt(value) <= 0xffffffffffffffffn;
  if (receipt?.scope !== 'first_draw_in_normal_callback_observation_only' || (receipt.schema_version !== 3 && !currentOwners))
    return ['wrong_scope_or_schema'];
  for (const key of ['requested', 'admitted', 'terminal', 'renderer_armed'])
    if (receipt[key] !== true) failures.push(key);
  if (receipt.resumable !== false) failures.push('resumable');
  if (receipt.error !== '') failures.push(`native:${receipt.error}`);
  // FixedTickClock::tick_with_budget has the authored eight-step ceiling.
  const total = receipt.planned_steps;
  if (!uint32(total) || total < 1 || total > 8) failures.push('planned_steps');
  for (const key of ['tick_returns', 'draw_returns', 'callback_steps', 'callback_draws'])
    if (receipt[key] !== total) failures.push(key);
  if (!['requested_source_frame', 'source_frame_before', 'source_frame_after', 'owner_generation',
        'selected_source_frame', 'unobserved_draws'].every(key => uint32(receipt[key])) ||
      receipt.source_frame_before !== receipt.requested_source_frame ||
      receipt.source_frame_after !== receipt.source_frame_before + total ||
      receipt.selected_source_frame !== receipt.source_frame_before + 1 ||
      receipt.unobserved_draws !== total - 1) failures.push('source_cursor');
  const owners = receipt.selected_depth_owners;
  const ownerKeys = ['registered', 'completed', 'pending', 'ignored_other_issuer', 'finalized'];
  if (!owners || typeof owners !== 'object' || Array.isArray(owners) ||
      Object.keys(owners).length !== ownerKeys.length || !ownerKeys.every(key => Object.hasOwn(owners, key)) ||
      !ownerKeys.slice(0, 4).every(key => uint32(owners[key])) || owners.registered > 64 ||
      owners.completed > owners.registered || owners.pending !== owners.registered - owners.completed ||
      owners.pending !== 0 || typeof owners.finalized !== 'boolean' ||
      (receipt.native_retired === true && owners.finalized !== true)) failures.push('selected_depth_owners');
  if (!Number.isInteger(receipt.planned_audio_steps) || receipt.planned_audio_steps < 0 ||
      receipt.planned_audio_steps > 8 || receipt.audio_returns !== receipt.planned_audio_steps)
    failures.push('audio_steps');
  if (!Number.isInteger(receipt.audio_phase_before) || receipt.audio_phase_before < 0 ||
      receipt.audio_phase_before >= 60 ||
      receipt.pcm_frames !== Math.floor((receipt.audio_phase_before + 32000 * receipt.audio_returns) / 60) ||
      receipt.audio_phase_after !== (receipt.audio_phase_before + 32000 * receipt.audio_returns) % 60)
    failures.push('actual_audio_cadence');
  // Aurora TARGET_PC PADStatus is 16 bytes (asserted by the producer). Keep
  // all four native records, including extButton/padding; net format is separate.
  if (!Array.isArray(receipt.raw_pad_bytes) || receipt.raw_pad_bytes.length !== 64 ||
      !receipt.raw_pad_bytes.every(value => Number.isInteger(value) && value >= 0 && value <= 255))
    failures.push('native_pad_records');
  const renderer = receipt.renderer;
  if (renderer?.schema_version !== (textureOwners ? 5 : attachmentOwners ? 4 : currentOwners ? 3 : 2) || renderer?.state !== 4 ||
      renderer.submit_count !== 1 || renderer.fence_count !== 1 || renderer.fence_success !== 1)
    failures.push('normal_submit_and_fence');
  if (!Array.isArray(receipt.events) || !Array.isArray(receipt.handles) ||
      !Number.isInteger(renderer?.event_count) || renderer.event_count < 1 || renderer.event_count > 8192 ||
      !Number.isInteger(renderer?.handle_count) || renderer.handle_count < 0 || renderer.handle_count > 6144 ||
      receipt.events?.length !== renderer?.event_count || receipt.handles?.length !== renderer?.handle_count)
    failures.push('incomplete_rows');
  if (!Number.isInteger(renderer?.first_blocker) || renderer.first_blocker < 0 || renderer.first_blocker > 27)
    failures.push('unknown_blocker');
  if (!uint32(renderer?.target_frame_index) || !uint32(renderer?.frame_index) ||
      renderer.target_frame_index !== renderer.frame_index || !uint64(renderer?.frame_id))
    failures.push('renderer_frame_identity');
  if (Array.isArray(receipt.events)) {
    const fields = ['kind', 'frame_index', 'op_index', 'item_index', 'flags', 'value0', 'value1', 'value2', 'value3'];
    if (receipt.events.some(row => !row || !fields.every(key => uint32(row[key])) ||
        row.kind < 1 || row.kind > 24 || row.frame_index !== renderer?.frame_index ||
        !uint64(row.handle_a) || !uint64(row.handle_b))) failures.push('event_rows');
    const submits = receipt.events.flatMap((row, index) => row?.kind === 17 ? [index] : []);
    const fences = receipt.events.flatMap((row, index) => row?.kind === 18 ? [index] : []);
    if (submits.length !== 1 || fences.length !== 1 || submits[0] >= fences[0] ||
        receipt.events[fences[0]]?.flags !== 1) failures.push('submit_fence_events');
  }
  if (Array.isArray(receipt.handles)) {
    if (receipt.handles.some((row, index) => !row || row.capture_id !== String(index + 1) ||
        !uint32(row.kind) || row.kind < 1 || row.kind > 9 || row.reserved !== 0))
      failures.push('handle_rows');
    const ids = new Set(['0', ...receipt.handles.map(row => row?.capture_id)]);
    // FRAME_OP uses these slots for upload high-water counters; CUSTOM_DRAW
    // and ENCODER_TASK use handle_a for an opaque registry type, not a resource.
    const resourceKinds = new Set([3, 4, 5, 9, 10, 11, 12, 13, 14, 15]);
    if (Array.isArray(receipt.events) && receipt.events.some(row =>
        resourceKinds.has(row?.kind) && (!ids.has(row.handle_a) || !ids.has(row.handle_b) ||
          (row.kind === 5 && !ids.has(String(row.value0)))))) failures.push('unlisted_handle');
  }
  // first_blocker records unresolved rollback ownership. A complete observation
  // may report a blocker; it never establishes safe restore or zero side effects.
  const exact = (row, fields) => row && typeof row === 'object' && !Array.isArray(row) &&
    Object.keys(row).length === fields.length && fields.every(key => Object.hasOwn(row, key));
  const vector = (row, size, predicate) => Array.isArray(row) && row.length === size && row.every(predicate);
  const rootFields = 'schema_version scope requested terminal native_retired admitted renderer_armed resumable requested_source_frame owner_generation source_frame_before source_frame_after tick_returns draw_returns audio_returns pcm_frames pre_tick_callbacks pre_tick_audio_returns pre_tick_pcm_frames planned_steps planned_audio_steps callback_steps callback_draws audio_phase_before audio_phase_after error raw_pad_bytes renderer events handles resources binding_entries draws rows_complete selected_source_frame unobserved_draws selected_depth_owners'.split(' ');
  if (currentOwners) rootFields.push('layout_entries', 'provenance');
  if (textureOwners) rootFields.push('attachment_facts');
  if (!exact(receipt, rootFields) || typeof receipt.native_retired !== 'boolean' || receipt.rows_complete !== true)
    failures.push('incomplete_schema2_serialization');
  for (const key of ['pre_tick_callbacks', 'pre_tick_audio_returns', 'pre_tick_pcm_frames'])
    if (!uint32(receipt[key])) failures.push(key);
  const counterFields = 'state first_blocker target_frame_index frame_index event_count handle_count op_count render_pass_count draw_command_count custom_draw_count encoder_task_count texture_copy_count texture_upload_count palette_conversion_count resolve_count snapshot_count staging_copy_count queue_write_count after_submit_callback_count submit_count fence_count fence_success command_record_count actual_draw_count resource_count binding_entry_count depth_trace_count depth_request_count'.split(' ');
  if (currentOwners) counterFields.push('layout_entry_count', 'provenance_count');
  if (textureOwners) counterFields.push('attachment_fact_count');
  const rendererFields = ['schema_version', 'frame_id', 'queue_write_bytes', 'finding_mask', ...counterFields];
  if (!exact(renderer, rendererFields) || !counterFields.every(key => uint32(renderer?.[key])) ||
      !uint64(renderer?.queue_write_bytes) || !uint64(renderer?.finding_mask)) failures.push('renderer_schema2_fields');
  const mask = uint64(renderer?.finding_mask) ? BigInt(renderer.finding_mask) : 0n;
  if ((mask >> 28n) !== 0n || (mask & 1n) !== 0n ||
      (renderer?.first_blocker === 0 ? mask !== 0n :
        !uint32(renderer?.first_blocker) || renderer.first_blocker > 27 ||
        (mask & (1n << BigInt(renderer.first_blocker))) === 0n)) failures.push('finding_identity');
  // The authored private observer marks any other retained profile shape as finding27.
  if ((renderer?.command_record_count !== 265 || renderer?.draw_command_count !== 246 ||
       renderer?.render_pass_count !== 3) && (mask & (1n << 27n)) === 0n)
    failures.push('unreported_command_shape');
  const arrays = [['resources', 'resource_count', 6144], ['binding_entries', 'binding_entry_count', 3955],
    ['draws', 'actual_draw_count', 246]];
  for (const [key, count, cap] of arrays)
    if (!Array.isArray(receipt[key]) || !uint32(renderer?.[count]) || renderer[count] > cap ||
        receipt[key].length !== renderer[count]) failures.push(`incomplete_${key}`);
  const handles = new Map(Array.isArray(receipt.handles) ? receipt.handles.map(h => [h?.capture_id, h?.kind]) : []);
  const listed = (id, kind = null) => uint64(id) && (id === '0' ||
    (handles.has(id) && (kind === null || handles.get(id) === kind)));
  const resourceFields = 'capture_id owner_id kind word_count words'.split(' ');
  const resources = new Map();
  for (const row of Array.isArray(receipt.resources) ? receipt.resources : []) {
    if (!exact(row, resourceFields) || !listed(row.capture_id, row.kind) || row.capture_id === '0' ||
        !listed(row.owner_id) || !uint32(row.kind) || row.kind < 1 || (row.kind > 6 && !(currentOwners && row.kind === 9)) ||
        !uint32(row.word_count) || row.word_count > 16 || !vector(row.words, 16, uint32) ||
        resources.has(row.capture_id)) failures.push('resource_rows');
    else {
      const validWords = row.kind === 2 ? row.word_count >= 10 && row.word_count <= 16 :
        ({1: [10], 3: [3, 5], 4: [10], 5: [5], 6: [2], 9: [1]})[row.kind]?.includes(row.word_count);
      if (!validWords || row.words.slice(row.word_count).some(word => word !== 0) ||
          (row.owner_id !== '0' && !(row.kind === 1 ? [2, 7] : [7]).includes(handles.get(row.owner_id))))
        failures.push('resource_descriptor_shape');
      resources.set(row.capture_id, row);
    }
  }
  const bindingFields = 'bind_group_id resource_id offset size binding kind'.split(' ');
  for (const row of Array.isArray(receipt.binding_entries) ? receipt.binding_entries : [])
    if (!exact(row, bindingFields) || !listed(row.bind_group_id, 5) || row.bind_group_id === '0' ||
        !listed(row.resource_id, row.kind) || !uint64(row.offset) || !uint64(row.size) ||
        !uint32(row.binding) || ![1, 3, 4].includes(row.kind))
      failures.push('binding_entry_rows');
  const drawUintFields = 'frame_index op_index command_index renderer_kind index_format vertex_count index_count instance_count dst_alpha has_clear_color reserved'.split(' ');
  const drawIdFields = 'pipeline_key pipeline_id index_buffer_id index_offset index_size'.split(' ');
  const drawFields = [...drawUintFields, ...drawIdFields, 'bind_group_ids', 'dynamic_offset_count',
    'dynamic_offsets', 'viewport_bits', 'scissor', 'clear_color_bits'];
  const events = Array.isArray(receipt.events) ? receipt.events : [];
  const commands = events.filter(e => e?.kind === 19), details = events.filter(e => e?.kind === 20);
  if (commands.length !== renderer?.command_record_count || details.length !== renderer?.actual_draw_count)
    failures.push('command_draw_cardinality');
  if (commands.some(e => e.flags > 4 || e.value0 > 1)) failures.push('command_rows');
  const eventFields = 'kind frame_index op_index item_index flags value0 value1 value2 value3 handle_a handle_b'.split(' ');
  if (events.some(e => !exact(e, eventFields))) failures.push('event_schema2_fields');
  if (Array.isArray(receipt.handles) && receipt.handles.some(h => !exact(h, ['capture_id', 'kind', 'reserved'])))
    failures.push('handle_schema2_fields');
  for (const [i, d] of (Array.isArray(receipt.draws) ? receipt.draws : []).entries()) {
    if (!exact(d, drawFields) || !drawUintFields.every(key => uint32(d?.[key])) ||
        !drawIdFields.every(key => uint64(d?.[key])) || d.frame_index !== renderer?.frame_index ||
        d.renderer_kind < 1 || d.renderer_kind > 2 || d.reserved !== 0 || d.has_clear_color > 1 ||
        !listed(d.pipeline_id, 6) || !listed(d.index_buffer_id, 3) ||
        !vector(d.bind_group_ids, 4, id => listed(id, 5)) ||
        !vector(d.dynamic_offset_count, 4, n => uint32(n) && n <= 4) ||
        !vector(d.dynamic_offsets, 4, row => vector(row, 4, uint32)) ||
        !vector(d.viewport_bits, 6, uint32) || !vector(d.scissor, 4, uint32) ||
        !vector(d.clear_color_bits, 4, uint64)) { failures.push('draw_rows'); continue; }
    const e = details[i];
    if (!e || e.op_index !== d.op_index || e.item_index !== d.command_index || e.flags !== d.renderer_kind ||
        e.value0 !== d.vertex_count || e.value1 !== d.index_count || e.value2 !== d.instance_count ||
        e.value3 !== d.dst_alpha || e.handle_a !== d.pipeline_id || e.handle_b !== d.index_buffer_id ||
        !commands.some(c => c.op_index === d.op_index && c.item_index === d.command_index && c.flags === 2))
      failures.push('draw_event_identity');
    const index = events.indexOf(e);
    for (let slot = 0; slot < 4; ++slot) {
      const b = events[index + 1 + slot];
      if (!b || b.kind !== 21 || b.flags !== slot || b.op_index !== d.op_index ||
          b.item_index !== d.command_index || b.handle_a !== d.bind_group_ids[slot] || b.handle_b !== '0' ||
          b.value0 !== d.dynamic_offset_count[slot] || b.value1 !== d.dynamic_offsets[slot][0] ||
          b.value2 !== d.dynamic_offsets[slot][1] || b.value3 !== d.dynamic_offsets[slot][2])
        failures.push('draw_binding_event_identity');
    }
  }
  // Depth event handle slots also carry packed dimensions/owner counters.
  // Validate only slots the producer actually authors as resource references.
  for (const e of events) {
    if (e?.kind === 20 && (!listed(e.handle_a, 6) || !listed(e.handle_b, 3))) failures.push('unlisted_draw_handle');
    if (e?.kind === 21 && !listed(e.handle_a, 5)) failures.push('unlisted_bind_group');
    if (e?.kind === 22) {
      const action = e.flags & 0xff;
      if (![1,2,3,4,5,6,7,0x102,0x103,0x104,0x105,0x205].includes(e.flags)) failures.push('depth_event_action');
      if (e.flags & 0x200) { if (!listed(e.handle_a, 3)) failures.push('unlisted_depth_handle'); }
      else if (e.flags & 0x100) { if (!listed(e.handle_a, 3)) failures.push('unlisted_depth_handle'); }
      else if (action !== 6) {
        const next = events[events.indexOf(e) + 1];
        const slotPair = next?.kind === 22 && next.flags === (action | 0x100) &&
          next.op_index === e.op_index && next.item_index === e.item_index;
        if (slotPair ? (!listed(e.handle_a, 3) || !listed(e.handle_b, 3)) : e.handle_b !== '0')
          failures.push('unlisted_depth_handle');
      }
    }
    if (e?.kind === 24 && (!uint32(e.flags) || e.flags < 1 || e.flags > 27 || (mask & (1n << BigInt(e.flags))) === 0n))
      failures.push('finding_event');
  }
  const registrations = new Map();
  const completions = new Set();
  for (const row of Array.isArray(receipt.events) ? receipt.events : []) {
    if (row?.kind !== 22) continue;
    if (row.flags === 0x205) {
      if (!uint32(row.value2) || !uint32(row.value3)) {
        failures.push('selected_map_registration_identity'); continue;
      }
      const token = (BigInt(row.value3) << 32n | BigInt(row.value2)).toString();
      if (token === '0' || token !== row.handle_b || registrations.has(token))
        failures.push('selected_map_registration_identity');
      registrations.set(token, row.item_index);
    } else if (row.flags === 6) {
      if (!registrations.has(row.handle_b) || registrations.get(row.handle_b) !== row.item_index ||
          completions.has(row.handle_b)) failures.push('selected_map_callback_identity');
      completions.add(row.handle_b);
      // Pinned emdawn webgpu.h WGPUMapAsyncStatus_Success == 0x00000001.
      if (row.value0 !== 1 || row.value1 !== 1) failures.push('selected_map_result_failed');
    }
  }
  if (registrations.size !== owners?.registered || completions.size !== owners?.completed)
    failures.push('selected_map_callback_accounting');
  if (currentOwners) failures.push(...rendererOwnerObservationFailures(receipt));
  return [...new Set(failures)];

}

export function compareFirstDrawObservations(reference, candidate) {
  const failures = [...firstDrawObservationFailures(reference).map(x => `reference:${x}`),
    ...firstDrawObservationFailures(candidate).map(x => `candidate:${x}`)];
  if (failures.length) return failures;
  function first(a, b, path) {
    if (Array.isArray(a)) {
      if (!Array.isArray(b) || a.length !== b.length) return path;
      for (let i = 0; i < a.length; ++i) { const p = first(a[i], b[i], `${path}[${i}]`); if (p) return p; }
      return null;
    }
    if (a && typeof a === 'object') {
      if (!b || typeof b !== 'object') return path;
      for (const key of Object.keys(a)) { const p = first(a[key], b[key], `${path}.${key}`); if (p) return p; }
      return null;
    }
    return Object.is(a, b) ? null : path;
  }
  const path = first(reference, candidate, 'receipt');
  return path ? [`different:${path}`] : [];
}
