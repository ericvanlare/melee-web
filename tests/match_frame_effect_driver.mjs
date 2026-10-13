// Private, one-shot observer driver. These functions execute in page context.
// Do not call any native export until the generated-runtime predicate passes.
export function armNativeFrameEffect({minimumFrame = 180, maximumFrame = 240} = {}) {
  const module = globalThis.Module;
  if (module?.matchFrameEffectNativeIdle?.() !== true)
    return {armed: false, reason: 'native_call_pending'};
  if (typeof module._melee_web_native_menu_frame_effect_arm !== 'function' ||
      typeof module._melee_web_native_menu_frame_effect_status !== 'function')
    return {armed: false, reason: 'private_exports_missing'};
  const match = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_match_observe()));
  if (match.ready !== true || !Number.isInteger(match.frame) ||
      match.frame < minimumFrame || match.frame > maximumFrame)
    return {armed: false, reason: 'source_frame_outside_declared_window', match};
  const armed = module._melee_web_native_menu_frame_effect_arm(match.frame) === 1;
  return {armed, reason: armed ? null : 'native_arm_refused', match};
}

export function readNativeFrameEffect() {
  const module = globalThis.Module;
  // No native getter even on a timeout until the actual callback returns.
  if (module?.matchFrameEffectNativeIdle?.() !== true ||
      module.matchFrameEffectCallbackReturned !== true)
    return {settled: false};
  const status = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_frame_effect_status()));
  if (status.busy === true) return {settled: false};
  const renderer = status.renderer;
  const completed = status.terminal === true && (status.error !== '' ||
    renderer?.state === 5 || renderer?.fence_count === 1);
  return {settled: true, completed, status};
}

export async function retireNativeFrameEffect() {
  const module = globalThis.Module;
  if (module?.matchFrameEffectNativeIdle?.() !== true ||
      module.matchFrameEffectCallbackReturned !== true)
    return {retired: false, reason: 'native_call_pending'};
  // ccall must be awaited: native close/shutdown may Asyncify-yield. A caller
  // timeout does not cancel this promise or grant permission for native cleanup.
  const result = await module.ccall('melee_web_native_menu_frame_effect_retire', 'number', [], [], {async: true});
  if (module.matchFrameEffectNativeIdle() !== true)
    return {retired: false, reason: 'native_retirement_continuation_pending'};
  const status = JSON.parse(module.UTF8ToString(module._melee_web_native_menu_frame_effect_status()));
  return {retired: result === 1 && status.native_retired === true, result, status};
}

export function frameEffectObservationFailures(receipt) {
  const failures = [];
  const uint32 = value => Number.isInteger(value) && value >= 0 && value <= 0xffffffff;
  const uint64 = value => typeof value === 'string' && /^(0|[1-9][0-9]{0,19})$/.test(value) &&
    BigInt(value) <= 0xffffffffffffffffn;
  if (receipt?.scope !== 'one_normal_callback_observation_only' || receipt.schema_version !== 1)
    return ['wrong_scope_or_schema'];
  for (const key of ['requested', 'admitted', 'terminal', 'renderer_armed'])
    if (receipt[key] !== true) failures.push(key);
  if (receipt.resumable !== false) failures.push('resumable');
  if (receipt.error !== '') failures.push(`native:${receipt.error}`);
  for (const key of ['tick_returns', 'draw_returns', 'planned_steps', 'callback_steps', 'callback_draws'])
    if (receipt[key] !== 1) failures.push(key);
  if (!['requested_source_frame', 'source_frame_before', 'source_frame_after', 'owner_generation']
      .every(key => uint32(receipt[key])) ||
      receipt.source_frame_before !== receipt.requested_source_frame ||
      receipt.source_frame_after !== receipt.source_frame_before + 1) failures.push('source_cursor');
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
  if (renderer?.schema_version !== 1 || renderer?.state !== 4 ||
      renderer.submit_count !== 1 || renderer.fence_count !== 1 || renderer.fence_success !== 1)
    failures.push('normal_submit_and_fence');
  if (!Array.isArray(receipt.events) || !Array.isArray(receipt.handles) ||
      !Number.isInteger(renderer?.event_count) || renderer.event_count < 1 || renderer.event_count > 1024 ||
      !Number.isInteger(renderer?.handle_count) || renderer.handle_count < 0 || renderer.handle_count > 512 ||
      receipt.events?.length !== renderer?.event_count || receipt.handles?.length !== renderer?.handle_count)
    failures.push('incomplete_rows');
  if (!Number.isInteger(renderer?.first_blocker) || renderer.first_blocker < 0 || renderer.first_blocker > 16)
    failures.push('unknown_blocker');
  if (!uint32(renderer?.target_frame_index) || !uint32(renderer?.frame_index) ||
      renderer.target_frame_index !== renderer.frame_index || !uint64(renderer?.frame_id))
    failures.push('renderer_frame_identity');
  if (Array.isArray(receipt.events)) {
    const fields = ['kind', 'frame_index', 'op_index', 'item_index', 'flags', 'value0', 'value1', 'value2', 'value3'];
    if (receipt.events.some(row => !row || !fields.every(key => uint32(row[key])) ||
        row.kind < 1 || row.kind > 18 || row.frame_index !== renderer?.frame_index ||
        !uint64(row.handle_a) || !uint64(row.handle_b))) failures.push('event_rows');
    const submits = receipt.events.flatMap((row, index) => row?.kind === 17 ? [index] : []);
    const fences = receipt.events.flatMap((row, index) => row?.kind === 18 ? [index] : []);
    if (submits.length !== 1 || fences.length !== 1 || submits[0] >= fences[0] ||
        receipt.events[fences[0]]?.flags !== 1) failures.push('submit_fence_events');
  }
  if (Array.isArray(receipt.handles)) {
    if (receipt.handles.some((row, index) => !row || row.capture_id !== String(index + 1) ||
        !uint32(row.kind) || row.kind < 1 || row.kind > 3 || row.reserved !== 0))
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
  return failures;
}
