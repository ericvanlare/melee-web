import {rendererOwnerObservationFailures} from './match_frame_effect_renderer_owners.mjs';
// Explicit first-draw scope. The historical single-tick driver/validator is unchanged.
// Reuses the reviewed schema2 row/value validation with a distinct exact outer
// field allowlist and selected-map checks. No historical receipt/schema coercion.
export function armNativeFrameEffectFirstDraw({minimumFrame = 180, maximumFrame = 240, componentEligibility = false, naturalOneEligibility = false} = {}) {
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
  if(componentEligibility){
    const depth=module.__meleeWebDepthNoWorkV1?.status?.();
    if(globalThis.__meleeWebFirstDrawComponentModuleV1!==undefined||
       depth?.phase!=='READY'||depth.firstFailure||depth.bridgeFailure||
       typeof module._melee_web_native_menu_frame_effect_components!=='function'||
       typeof module._aurora_browser_depth_no_work_arm_component!=='function')
      return {armed:false,reason:'component_premain_or_exports_missing'};
  }
  if(naturalOneEligibility&&(!componentEligibility||minimumFrame!==180||maximumFrame!==240||
      typeof module._melee_web_native_menu_frame_effect_request_natural_one!=='function'||
      typeof module._melee_web_native_menu_frame_effect_natural_one_enrollment!=='function'))
    return {armed:false,reason:'natural_one_private_exports_or_window_missing'};
  const armed = naturalOneEligibility
    ?module._melee_web_native_menu_frame_effect_request_natural_one(match.frame)===1
    :module._melee_web_native_menu_frame_effect_arm_first_draw(match.frame)===1;
  if(armed&&componentEligibility)Object.defineProperty(globalThis,'__meleeWebFirstDrawComponentModuleV1',
    {value:module,writable:false,configurable:false});
  return {armed, reason: armed ? null : 'native_arm_refused', match,...(naturalOneEligibility?{selection_pending:armed,enrollment_mode:'naturally_due_one_tick'}:{})};
}

export function readNativeFrameEffectFirstDraw({naturalOneEligibility=false}={}) {
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
  let enrollment;
  if(naturalOneEligibility&&completed){
    if(typeof module._melee_web_native_menu_frame_effect_natural_one_enrollment!=='function')return {settled:false};
    enrollment=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_frame_effect_natural_one_enrollment()));
    if(module!==globalThis.Module||module.matchFrameEffectNativeIdle?.()!==true||module.matchFrameEffectCallbackReturned!==true)return {settled:false};
  }
  return {settled: true, completed, status,...(naturalOneEligibility&&completed?{enrollment}:{})};
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

// Serialized by Playwright: all dependencies are looked up in this page call.
export function readFirstDrawComponents(){
 const module=globalThis.Module;
 const idle=()=>globalThis.Module===module&&globalThis.__meleeWebFirstDrawComponentModuleV1===module&&
   module?.matchFrameEffectNativeIdle?.()===true&&module.matchFrameEffectCallbackReturned===true;
 if(!idle())return {status:'refused',reason:'same_module_or_returned_idle_missing',composite_eligible:false};
 const depthBefore=module.__meleeWebDepthNoWorkV1?.status?.();
 if(depthBefore?.phase!=='READY'||depthBefore.firstFailure||depthBefore.bridgeFailure)
  return {status:'refused',reason:'depth_history_not_clean',depth_before:depthBefore??null,composite_eligible:false};
 if(typeof module._melee_web_native_menu_frame_effect_components!=='function'||
    typeof module._aurora_browser_depth_no_work_arm_component!=='function')
  return {status:'refused',reason:'private_exports_missing',composite_eligible:false};
 const native=JSON.parse(module.UTF8ToString(module._melee_web_native_menu_frame_effect_components()));
 // Never activate even this component after a native refusal.
 if(native?.copied!==true||native.component_ready!==true)
  return {status:'refused',reason:'native_component_not_ready',native,depth_before:depthBefore,composite_eligible:false};
 if(!idle())return {status:'refused',reason:'module_or_idle_changed',native,composite_eligible:false};
 return {schema:'melee-web-first-draw-components-read-v1',status:'read',same_module:true,returned_idle:true,
   native,depth_before:depthBefore,composite_eligible:false,
   remaining_gates:['LOAD-content','sampled-texture-continuous-guard','device-host-futures','native-refs','restore']};
}
// Separate serialized call, reached only after the host strictly decodes read.
export function armFirstDrawComponents({expectedDepthSequence}={}){
 const module=globalThis.Module;
 const idle=()=>globalThis.Module===module&&globalThis.__meleeWebFirstDrawComponentModuleV1===module&&
   module?.matchFrameEffectNativeIdle?.()===true&&module.matchFrameEffectCallbackReturned===true;
 if(!idle())return {status:'refused',reason:'same_module_or_returned_idle_missing',composite_eligible:false};
 const depthBefore=module.__meleeWebDepthNoWorkV1?.status?.();
 if(!Number.isSafeInteger(expectedDepthSequence)||depthBefore?.sequence!==expectedDepthSequence||
    depthBefore.phase!=='READY'||depthBefore.firstFailure||depthBefore.bridgeFailure||
    typeof module._aurora_browser_depth_no_work_arm_component!=='function')
  return {status:'refused',reason:'depth_history_changed_before_arm',depth_before:depthBefore??null,composite_eligible:false};
 const depthArm=module._aurora_browser_depth_no_work_arm_component();
 const depthAfter=module.__meleeWebDepthNoWorkV1.status();
 if(depthArm!==1||depthAfter.phase!=='ACTIVE'||depthAfter.firstFailure||depthAfter.bridgeFailure||!idle())
  return {status:'refused',reason:'depth_arm_or_final_idle_refused',depth_after:depthAfter,composite_eligible:false};
 return {schema:'melee-web-first-draw-components-arm-v1',status:'armed',same_module:true,returned_idle:true,
   depth_after:depthAfter,composite_eligible:false};
}
export function firstDrawComponentFailures(value,{stage='armed'}={}){
 const read=stage==='read';
 if(!read&&stage!=='armed')return ['component_decoder_stage'];
 const failures=[];const bad=reason=>failures.push(reason);
 const exact=(o,keys)=>o&&typeof o==='object'&&!Array.isArray(o)&&Object.keys(o).length===keys.length&&keys.every(k=>Object.hasOwn(o,k));
 const u32=v=>Number.isInteger(v)&&v>=0&&v<=0xffffffff;
 const u64=v=>typeof v==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(v)&&BigInt(v)<=0xffffffffffffffffn;
 if(!exact(value,['schema','status','same_module','returned_idle','native','depth_before',...(read?[]:['depth_after']),'composite_eligible','remaining_gates'])||
    value.schema!==(read?'melee-web-first-draw-components-read-v1':'melee-web-first-draw-components-v1')||value.status!==(read?'read':'observed')||value.same_module!==true||value.returned_idle!==true||value.composite_eligible!==false)
  return ['component_observation_refused_or_malformed'];
 const n=value.native;
 if(!exact(n,['schema','copied','component_ready','refusal','composite_eligible','frame_slot_capacity','staging_slot_capacity','facts'])||
    n.schema!=='melee-web-native-first-draw-components-v1'||n.copied!==true||n.component_ready!==true||n.refusal!==''||n.composite_eligible!==false||
    n.frame_slot_capacity!==4||n.staging_slot_capacity!==4)return ['native_component_receipt']; // authored Emscripten constants, not runtime guesses
 const f=n.facts;
 if(!exact(f,['schema_version','owner_book_present','before','after','packets','owners'])||f.schema_version!==1||f.owner_book_present!==1)return ['native_component_facts'];
 const stamp64=['generation','registration_sequence','callback_sequence','stale_callback_count','queue_handle','last_begin_frame_id','last_end_frame_id'];
 const stamp32=['frame_index','active_frame_count','active_staging_count','free_frame_count','free_staging_count','worker_idle','process_events_queued'];
 for(const v of [f.before,f.after])if(!exact(v,[...stamp64,...stamp32])||!stamp64.every(k=>u64(v[k]))||!stamp32.every(k=>u32(v[k])))bad('native_stamp');
 if(![...stamp64,...stamp32].every(k=>f.before?.[k]===f.after?.[k]))bad('native_stamp_drift');
 const p=f.packets;
 if(!exact(p,['schema_version','active_slot_count','recorder','slots'])||p.schema_version!==1||!u32(p.active_slot_count)||p.active_slot_count<1||p.active_slot_count>4||
    !exact(p.recorder,['active','in_offscreen','suspended_efb_pass'])||Object.values(p.recorder).some(v=>v!==0)||!Array.isArray(p.slots)||p.slots.length!==4)bad('native_packets');
 const slot64=['frame_id','render_pass_count','texture_copy_count','encoder_task_count','op_count','texture_upload_count','after_submit_callback_count','verts_bytes','uniforms_bytes','indices_bytes','storage_bytes','texture_upload_bytes'];
 for(const v of Array.isArray(p?.slots)?p.slots:[])if(!exact(v,['slot','frame_index','encoder_present',...slot64])||!['slot','frame_index','encoder_present'].every(k=>u32(v[k]))||!slot64.every(k=>u64(v[k])))bad('native_slot');
 const owner64=['future_id','registration_sequence','callback_sequence','captured_generation','queue_handle','encoder_handle'];
 if(!Array.isArray(f.owners)||f.owners.length!==4)bad('native_owners');
 for(const v of Array.isArray(f.owners)?f.owners:[])if(!exact(v,['state',...owner64])||!u32(v.state)||!owner64.every(k=>u64(v[k])))bad('native_owner');
 // Do not accept a claimed native-ready bit with contradictory copied facts.
 if(!failures.length){
  const b=f.before;
  if(b.generation==='0'||b.queue_handle==='0'||b.stale_callback_count!=='0'||b.worker_idle!==1||b.process_events_queued!==0||b.frame_index===0xffffffff||
     b.active_frame_count<1||b.active_frame_count>4||b.active_staging_count<1||b.active_staging_count>4||
     b.free_frame_count!==b.active_frame_count||b.free_staging_count!==b.active_staging_count||p.active_slot_count!==b.active_frame_count)bad('native_not_ready');
  p.slots.forEach((row,i)=>{
   if(row.encoder_present!==0||slot64.slice(1).some(k=>row[k]!=='0')||
      (i<p.active_slot_count?row.slot!==i:row.slot!==0||row.frame_index!==0||row.frame_id!=='0'))bad('native_slot_not_empty');
  });
  f.owners.forEach((row,i)=>{
   if(i>=b.active_staging_count||row.state===0){if(row.state!==0||owner64.some(k=>row[k]!=='0'))bad('native_owner_not_zero');}
   else if(row.state===2){if(row.captured_generation!==b.generation||row.queue_handle!==b.queue_handle||row.encoder_handle==='0'||row.future_id==='0'||
     row.registration_sequence==='0'||BigInt(row.registration_sequence)>BigInt(b.registration_sequence)||row.callback_sequence==='0'||BigInt(row.callback_sequence)>BigInt(b.callback_sequence))bad('native_completed_owner');}
   else if(row.state===5){if(row.captured_generation!==b.generation||owner64.filter(k=>k!=='captured_generation').some(k=>row[k]!=='0'))bad('native_immediate_owner');}
   else bad('native_pending_or_unknown_owner');
  });
 }
 const pristine=(words,enabled)=>Array.isArray(words)&&words.length===35&&words.every(u32)&&
   words[0]===1&&words[1]===enabled&&words[2]===0&&words[3]===0&&words[4]===3&&words.slice(5,8).every(v=>v===0)&&
   [0,1,2].every(i=>words[8+i*9]===i&&words.slice(9+i*9,17+i*9).every(v=>v===0));
 for(const [v,phase] of (read?[[value.depth_before,'READY']]:[[value.depth_before,'READY'],[value.depth_after,'ACTIVE']])){
  if(v?.schema!=='melee-web-private-depth-no-work-v1'||v.phase!==phase||v.firstFailure!==null||v.bridgeFailure!==false||v.gameplayAdmission!==false||v.componentOnly!==true||v.initBegins!==1||v.initEnds!==1||
     !Number.isSafeInteger(v.sequence)||v.sequence<3||!pristine(v.snapshots?.preMain,0)||!pristine(v.snapshots?.initialized,1)||
     (phase==='ACTIVE'?!pristine(v.snapshots?.arm,1):v.snapshots?.arm!==null))bad('depth_component');
 }
 if(!read&&(value.depth_after?.sequence!==value.depth_before?.sequence+1||
    !['preMain','initialized'].every(k=>Array.isArray(value.depth_before?.snapshots?.[k])&&Array.isArray(value.depth_after?.snapshots?.[k])&&value.depth_before.snapshots[k].length===35&&value.depth_before.snapshots[k].every((v,i)=>v===value.depth_after.snapshots[k][i]))))bad('depth_history_drift');
 const gates=['LOAD-content','sampled-texture-continuous-guard','device-host-futures','native-refs','restore'];
 if(!Array.isArray(value.remaining_gates)||value.remaining_gates.length!==gates.length||!gates.every((v,i)=>value.remaining_gates[i]===v))bad('composite_gates');
 return failures;
}

// The supplied invoke is the existing owner's bounded page.evaluate transport.
// These helpers never retire native state or resume/tick/restore a match.
function readSelectedTextureBoundary(){
 const m=globalThis.Module,idle=()=>m&&globalThis.Module===m&&globalThis.__meleeWebFirstDrawComponentModuleV1===m&&
   m.matchFrameEffectNativeIdle?.()===true&&m.matchFrameEffectCallbackReturned===true;
 if(!idle()||typeof m._melee_web_native_menu_frame_effect_components!=='function')return {status:'refused'};
 const native=JSON.parse(m.UTF8ToString(m._melee_web_native_menu_frame_effect_components()));
 return {status:idle()?'read':'refused',native,depth:m.__meleeWebDepthNoWorkV1?.status()};
}
function issueSelectedTextureBoundary(){
 const m=globalThis.Module;
 if(!m||m!==globalThis.__meleeWebFirstDrawComponentModuleV1||m.matchFrameEffectNativeIdle?.()!==true||
    m.matchFrameEffectCallbackReturned!==true||
    typeof m._melee_web_native_menu_frame_effect_issue_selected_texture!=='function'||
    m.__meleeWebTextureReadonlyGuardV1?.status().phase!=='INSTALLED')return {issued:0};
 const issued=m._melee_web_native_menu_frame_effect_issue_selected_texture();
 const result={issued,guard:m.__meleeWebTextureReadonlyGuardV1.status()};
 if(issued!==0)return result; // Successful dispatch retains the historical response exactly.
 const idle=()=>globalThis.Module===m&&globalThis.__meleeWebFirstDrawComponentModuleV1===m&&
   m.matchFrameEffectNativeIdle?.()===true&&m.matchFrameEffectCallbackReturned===true;
 const unavailable=reason=>({...result,native_refusal:{status:'unavailable',reason}});
 // This is inline after a known synchronous zero return, never a second page
 // call after timeout/transport loss. Uncertain native state forbids the getter.
 if(!idle())return unavailable('same_module_or_returned_idle_missing');
 if(typeof m._melee_web_native_menu_frame_effect_status!=='function')return unavailable('status_export_missing');
 try{
  const receipt=JSON.parse(m.UTF8ToString(m._melee_web_native_menu_frame_effect_status()));
  if(!idle())return unavailable('same_module_or_returned_idle_changed');
  if(!receipt||typeof receipt!=='object'||Array.isArray(receipt)||receipt.terminal!==true||
     receipt.native_retired!==false||typeof receipt.error!=='string'||receipt.error.length===0)
   return unavailable('native_refusal_receipt_malformed');
  // Copied raw diagnostic only: it never grants issuance or interval admission.
  return {...result,native_refusal:{status:'copied_raw',receipt}};
 }catch{return unavailable('native_refusal_receipt_unavailable');}
}
async function fenceSelectedTextureBoundary({end=false}={}){
 const m=globalThis.Module,idle=()=>m&&globalThis.Module===m&&m===globalThis.__meleeWebFirstDrawComponentModuleV1&&
   m.matchFrameEffectNativeIdle?.()===true&&m.matchFrameEffectCallbackReturned===true;
 if(!idle())return {status:'refused'};
 const api=m.__meleeWebTextureReadonlyGuardV1,cap=api?.capability();
 if(!cap)return {status:'refused'};
 const guard=await (end?api.endSelectedComponent(cap):api.armSelectedComponent(cap));
 return {status:idle()?'fenced':'refused',guard};
}
function closeSelectedTextureBoundary(){
 const m=globalThis.Module;
 if(!m||m!==globalThis.__meleeWebFirstDrawComponentModuleV1||m.matchFrameEffectNativeIdle?.()!==true||
    m.matchFrameEffectCallbackReturned!==true)return {status:'refused'};
 const api=m.__meleeWebTextureReadonlyGuardV1;
 return {status:'closed',guard:api.close(api.capability())};
}
function selectedTextureBoundaryFailures(base,read){
 if(read?.status!=='read')return ['selected_texture_boundary_refused'];
 const failures=firstDrawComponentFailures({...base,native:read.native,depth_after:read.depth});
 if(!failures.length&&JSON.stringify(base.native.facts.before)!==JSON.stringify(read.native.facts.before))
  failures.push('selected_texture_native_boundary_drift');
 return failures;
}
function selectedTextureGuardFailures(value,phase,reference){
 const exact=(v,keys)=>v&&typeof v==='object'&&!Array.isArray(v)&&Object.keys(v).length===keys.length&&keys.every(k=>Object.hasOwn(v,k));
 const uint=v=>Number.isInteger(v)&&v>=0&&v<=0xffffffff;
 const count=v=>Number.isSafeInteger(v)&&v>=0;
 const pair=v=>Array.isArray(v)&&v.length===2&&v.every(uint)&&(v[0]!==0||v[1]!==0);
 const component=phase!=='ISSUED';
 const keys=['identity','schema','scope','phase','epoch','attempts','dispatches','fenceComplete','firstFailure','gameplayAdmission',
   ...(component?['endFenceComplete','commandEpoch','commandDispatches','submitChecks']:[])];
 if(!exact(value,keys)||value.schema!==(component?2:1)||value.scope!==(component?'selected-texture-readonly-component-only':'native-issued-unarmed-component-only')||
    value.phase!==phase||!count(value.epoch)||value.epoch===0||!count(value.attempts)||!count(value.dispatches)||
    value.fenceComplete!==component||value.firstFailure!==null||value.gameplayAdmission!==false)return ['selected_texture_guard_status'];
 const id=value.identity;
 if(!exact(id,['nativeKeys','textureSerial','viewSerial','deviceEpoch'])||!Array.isArray(id.nativeKeys)||id.nativeKeys.length!==4||
    !id.nativeKeys.every(v=>uint(v)&&v!==0)||new Set(id.nativeKeys).size!==4||
    !pair(id.textureSerial)||!pair(id.viewSerial)||!pair(id.deviceEpoch)||JSON.stringify(id.textureSerial)===JSON.stringify(id.viewSerial))
  return ['selected_texture_guard_identity'];
 if(component&&(value.commandEpoch!==value.epoch||!count(value.commandDispatches)||!count(value.submitChecks)||
    value.endFenceComplete!==(phase!=='COMPONENT_ACTIVE')))return ['selected_texture_guard_fences'];
 if(reference&&(value.epoch!==reference.epoch||JSON.stringify(id)!==JSON.stringify(reference.identity)))return ['selected_texture_guard_identity_drift'];
 return [];
}
export async function startFirstDrawSelectedTextureInterval({invoke,components}){
 let failures=firstDrawComponentFailures(components);
 if(typeof invoke!=='function'||failures.length)return {status:'refused',failures,composite_eligible:false};
 try{
  const before=await invoke(readSelectedTextureBoundary);failures=selectedTextureBoundaryFailures(components,before);
  if(failures.length)return {status:'refused',before,failures,composite_eligible:false};
  const issue=await invoke(issueSelectedTextureBoundary);
  if(issue?.issued!==1||selectedTextureGuardFailures(issue.guard,'ISSUED').length||
     BigInt(issue.guard.identity.nativeKeys[3])!==BigInt(components.native.facts.before.queue_handle))return {status:'refused',issue,failures:['selected_texture_issue_refused'],composite_eligible:false};
  const start=await invoke(fenceSelectedTextureBoundary,{end:false});
  const after=await invoke(readSelectedTextureBoundary);failures=selectedTextureBoundaryFailures(components,after);
  if(start?.status!=='fenced')failures.push('selected_texture_start_fence_refused');
  failures.push(...selectedTextureGuardFailures(start?.guard,'COMPONENT_ACTIVE',issue.guard));
  return {status:failures.length?'refused':'active',before,issue,start,after,failures,composite_eligible:false};
 }catch(error){return {status:'refused',failures:['selected_texture_start_response_failed'],error:String(error?.message||error),composite_eligible:false};}
}
export async function endFirstDrawSelectedTextureInterval({invoke,components,started}){
 if(typeof invoke!=='function'||started?.status!=='active'||started.composite_eligible!==false||
    firstDrawComponentFailures(components).length||selectedTextureGuardFailures(started?.issue?.guard,'ISSUED').length)
  return {status:'refused',failures:['selected_texture_interval_not_active'],composite_eligible:false};
 try{
  const before=await invoke(readSelectedTextureBoundary);let failures=selectedTextureBoundaryFailures(components,before);
  if(failures.length)return {status:'refused',before,failures,composite_eligible:false};
  const end=await invoke(fenceSelectedTextureBoundary,{end:true});
  const after=await invoke(readSelectedTextureBoundary);failures=selectedTextureBoundaryFailures(components,after);
  if(end?.status!=='fenced')failures.push('selected_texture_end_fence_refused');
  failures.push(...selectedTextureGuardFailures(end?.guard,'COMPONENT_ENDED',started.issue.guard));
  // No close or native retirement after malformed/claimed-ready post-fence facts.
  if(failures.length)return {status:'refused',before,end,after,failures,composite_eligible:false};
  const closed=await invoke(closeSelectedTextureBoundary);
  if(closed?.status!=='closed')failures.push('selected_texture_close_refused');
  failures.push(...selectedTextureGuardFailures(closed?.guard,'CLOSED',started.issue.guard));
  return {status:failures.length?'refused':'closed',before,end,after,closed,failures,composite_eligible:false};
 }catch(error){return {status:'refused',failures:['selected_texture_end_response_failed'],error:String(error?.message||error),composite_eligible:false};}
}

// Independent private copied enrollment schema; historical outer3..6/renderer2..5 are unchanged.
export function naturalOneEnrollmentFailures(v,receipt,{originFrame}={}){
 const fail=()=>['natural_one_enrollment_receipt'];
 const exact=(o,keys)=>o&&typeof o==='object'&&!Array.isArray(o)&&Object.keys(o).length===keys.length&&keys.every(k=>Object.hasOwn(o,k));
 const u=x=>Number.isInteger(x)&&x>=0&&x<=0xffffffff;
 const wide=x=>typeof x==='string'&&/^(0|[1-9][0-9]{0,19})$/.test(x)&&BigInt(x)<=0xffffffffffffffffn;
 const totals=['callback_sequence','selection_callback','skipped_callbacks','skipped_ticks','skipped_draws','skipped_audio','skipped_pcm_frames','source_free_callbacks','source_free_audio','source_free_pcm_frames'];
 const keys=['schema','copied','state','minimum_frame','maximum_frame','origin_frame','selected_frame','owner_generation','origin_audio_phase','row_capacity','row_count',...totals,'callback_open','error','rows'];
 if(!exact(v,keys)||v.schema!=='melee-web-natural-one-tick-enrollment-v1'||v.copied!==true||v.state!==3||v.callback_open!==false||v.error!==''||
    v.minimum_frame!==180||v.maximum_frame!==240||v.row_capacity!==61||!u(v.origin_frame)||v.origin_frame!==originFrame||v.origin_frame<180||v.origin_frame>240||
    !u(v.selected_frame)||v.selected_frame<v.origin_frame||v.selected_frame>240||!u(v.owner_generation)||!u(v.origin_audio_phase)||v.origin_audio_phase>=60||
    !u(v.row_count)||v.row_count<1||v.row_count>61||!Array.isArray(v.rows)||v.rows.length!==v.row_count||!totals.every(k=>wide(v[k])))return fail();
 if(v.owner_generation!==receipt?.owner_generation||v.selected_frame!==receipt?.requested_source_frame||v.selected_frame!==receipt?.source_frame_before||
    receipt?.source_frame_after!==v.selected_frame+1||!['planned_steps','tick_returns','draw_returns','callback_steps','callback_draws'].every(k=>receipt?.[k]===1))return fail();
 const z=k=>BigInt(v[k]);
 if(z('callback_sequence')===0n||z('selection_callback')!==z('callback_sequence')||z('skipped_callbacks')+1n!==z('callback_sequence')||
    z('source_free_callbacks')+BigInt(v.row_count)!==z('callback_sequence')||BigInt(v.selected_frame-v.origin_frame)!==z('skipped_ticks'))return fail();
 let frame=v.origin_frame,last=0n,ticks=0n,draws=0n,audio=0n,pcm=0n;
 const rowKeys=['callback_sequence','source_before','source_after','planned_steps','planned_audio_steps','tick_returns','draw_returns','audio_returns','pcm_frames','audio_phase_before','audio_phase_after','selected'];
 for(let i=0;i<v.rows.length;++i){const row=v.rows[i],selected=i===v.rows.length-1;
  if(!exact(row,rowKeys)||!wide(row.callback_sequence)||BigInt(row.callback_sequence)<=last||BigInt(row.callback_sequence)>z('selection_callback')||row.selected!==selected||
     !rowKeys.filter(k=>!['callback_sequence','selected'].includes(k)).every(k=>u(row[k]))||row.source_before!==frame||row.source_after!==frame+row.tick_returns||
     row.tick_returns<1||row.planned_steps<1||row.planned_steps>8||row.tick_returns>row.planned_steps||row.draw_returns>row.tick_returns||
     row.planned_audio_steps>8||row.audio_returns!==row.planned_audio_steps||row.audio_phase_before>=60||row.audio_phase_after>=60||
     row.pcm_frames!==Math.floor((row.audio_phase_before+32000*row.audio_returns)/60)||row.audio_phase_after!==(row.audio_phase_before+32000*row.audio_returns)%60)return fail();
  if(selected){
   if(BigInt(row.callback_sequence)!==z('selection_callback')||row.planned_steps!==1||row.tick_returns!==1||row.draw_returns!==1||row.source_before!==v.selected_frame||
      row.audio_phase_before!==receipt.audio_phase_before||row.audio_phase_after!==receipt.audio_phase_after||row.audio_returns!==receipt.audio_returns||row.pcm_frames!==receipt.pcm_frames)return fail();
  }else{if(row.planned_steps===1)return fail();ticks+=BigInt(row.tick_returns);draws+=BigInt(row.draw_returns);audio+=BigInt(row.audio_returns);pcm+=BigInt(row.pcm_frames);}
  frame=row.source_after;last=BigInt(row.callback_sequence);
 }
 if(ticks!==z('skipped_ticks')||draws!==z('skipped_draws')||audio+z('source_free_audio')!==z('skipped_audio')||pcm+z('source_free_pcm_frames')!==z('skipped_pcm_frames')||
    z('skipped_pcm_frames')!==(BigInt(v.origin_audio_phase)+32000n*z('skipped_audio'))/60n||
    BigInt(receipt.audio_phase_before)!==(BigInt(v.origin_audio_phase)+32000n*z('skipped_audio'))%60n)return fail();
 return [];
}

// Opt-in point observation only; no pair arm, advancement or pumping.
export function readFirstDrawHostOwners(){
 const refuse=(reason,extra={})=>({schema:'melee-web-first-draw-host-owners-v1',status:'refused',reason,...extra,composite_eligible:false});
 try{
 const module=globalThis.Module, host=globalThis.window;
 const reader=host?.menuFirstPairHostReadSnapshot;
 const native=module?._melee_web_native_menu_first_pair_file_owners_quiescent;
 const idle=()=>globalThis.Module===module&&globalThis.__meleeWebFirstDrawComponentModuleV1===module&&
   module?.matchFrameEffectNativeIdle?.()===true&&module.matchFrameEffectCallbackReturned===true&&
   typeof module._melee_web_native_menu_running==='function'&&module._melee_web_native_menu_running()===0;
 const stable=()=>idle()&&globalThis.window===host&&host.menuFirstPairHostReadSnapshot===reader&&
   module._melee_web_native_menu_first_pair_file_owners_quiescent===native&&host.__meleeWebFirstPairFileGuardArmed===false;
 const valid=v=>v&&typeof v==='object'&&!Array.isArray(v)&&
   Object.keys(v).sort().join(',')==='assetTransfer,discSession,generation,preparing,resultCount,unresolvedReads'&&
   typeof v.generation==='string'&&/^(0|[1-9][0-9]*)$/.test(v.generation)&&
   Number.isSafeInteger(v.unresolvedReads)&&v.unresolvedReads>=0&&Number.isSafeInteger(v.resultCount)&&v.resultCount>=0&&
   ['discSession','assetTransfer','preparing'].every(k=>typeof v[k]==='boolean');
 if(!idle())return refuse('same_module_or_returned_idle_missing');
 if(typeof reader!=='function'||typeof native!=='function')return refuse('host_owner_readers_missing');
 if(!stable())return refuse('module_reader_or_arm_changed');
  const before=reader();
  if(!valid(before))return refuse('host_owner_snapshot_malformed');
  if(!stable())return refuse('module_reader_or_arm_changed');
  const native_quiescent=native();
  if(!stable())return refuse('module_reader_or_arm_changed');
  const after=reader();
  if(!valid(after))return refuse('host_owner_snapshot_malformed',{before});
  if(!stable())return refuse('module_reader_or_arm_changed',{before,after});
  if(native_quiescent!==1)return refuse('native_file_owners_not_quiescent',{before,after,native_quiescent});
  if(Object.keys(before).some(k=>before[k]!==after[k]))return refuse('host_owner_snapshot_drift',{before,after,native_quiescent});
  if(before.unresolvedReads!==0||before.resultCount!==0||!before.discSession||before.assetTransfer||before.preparing)
   return refuse('host_file_work_pending',{before,after,native_quiescent});
  return {schema:'melee-web-first-draw-host-owners-v1',status:'observed',before,after,native_quiescent,
    same_module:true,returned_idle:true,point_observation_only:true,composite_eligible:false};
 }catch{return refuse('host_owner_reader_failed');}
}
