// Pure fixture builder; imports never register tests.
export function schema2Fixture() {
  const event = (kind, extra = {}) => ({kind, frame_index: 3, op_index: 0, item_index: 0,
    flags: 0, value0: 0, value1: 0, value2: 0, value3: 0, handle_a: '0', handle_b: '0', ...extra});
  const draw = {frame_index: 3, op_index: 0, command_index: 0, renderer_kind: 2,
    pipeline_key: '18446744073709551615', pipeline_id: '2', bind_group_ids: ['4', '0', '0', '0'],
    dynamic_offset_count: [4, 0, 0, 0], dynamic_offsets: [[1, 2, 3, 4294967295], [0,0,0,0], [0,0,0,0], [0,0,0,0]],
    index_buffer_id: '3', index_offset: '9007199254740993', index_size: '18446744073709551615', index_format: 1,
    vertex_count: 3, index_count: 6, instance_count: 1, dst_alpha: 255, has_clear_color: 1, reserved: 0,
    viewport_bits: [2147483648, 2143289345, 1065353216, 1065353216, 0, 1065353216], scissor: [0,0,640,480],
    clear_color_bits: ['9223372036854775808', '9221120237041090561', '0', '4607182418800017408']};
  const events = [event(19, {flags: 2, value0: 1}),
    event(20, {flags: 2, value0: 3, value1: 6, value2: 1, value3: 255, handle_a: '2', handle_b: '3'}),
    ...Array.from({length: 4}, (_, slot) => event(21, {flags: slot,
      value0: draw.dynamic_offset_count[slot], value1: draw.dynamic_offsets[slot][0],
      value2: draw.dynamic_offsets[slot][1], value3: draw.dynamic_offsets[slot][2], handle_a: draw.bind_group_ids[slot]})),
    event(17), event(18, {flags: 1})];
  const counters = Object.fromEntries('op_count render_pass_count draw_command_count custom_draw_count encoder_task_count texture_copy_count texture_upload_count palette_conversion_count resolve_count snapshot_count staging_copy_count queue_write_count after_submit_callback_count depth_trace_count depth_request_count'.split(' ').map(k => [k, 0]));
  return {scope: 'one_normal_callback_observation_only', schema_version: 2,
    requested: true, admitted: true, terminal: true, native_retired: false, renderer_armed: true, resumable: false, error: '',
    tick_returns: 1, draw_returns: 1, planned_steps: 1, callback_steps: 1, callback_draws: 1,
    owner_generation: 1, requested_source_frame: 200, source_frame_before: 200, source_frame_after: 201,
    planned_audio_steps: 1, audio_returns: 1, pcm_frames: 533, audio_phase_before: 0, audio_phase_after: 20,
    pre_tick_callbacks: 0, pre_tick_audio_returns: 0, pre_tick_pcm_frames: 0, raw_pad_bytes: Array(64).fill(0), rows_complete: true,
    renderer: {...counters, schema_version: 2, state: 4, submit_count: 1, fence_count: 1, fence_success: 1,
      first_blocker: 7, finding_mask: '134217856', event_count: events.length, handle_count: 4,
      target_frame_index: 3, frame_index: 3, frame_id: '9007199254740993', queue_write_bytes: '18446744073709551615',
      command_record_count: 1, actual_draw_count: 1, resource_count: 3, binding_entry_count: 1,
      op_count: 1, render_pass_count: 1, draw_command_count: 1},
    events, handles: [7,6,3,5].map((kind, i) => ({capture_id: String(i + 1), kind, reserved: 0})),
    resources: [{capture_id: '2', owner_id: '1', kind: 6, word_count: 2, words: [4294967295,4294967295,...Array(14).fill(0)]},
      {capture_id: '3', owner_id: '1', kind: 3, word_count: 5, words: [128,0,4,0,0,...Array(11).fill(0)]},
      {capture_id: '4', owner_id: '1', kind: 5, word_count: 5, words: [0,0,1,0,0,...Array(11).fill(0)]}],
    binding_entries: [{bind_group_id: '4', resource_id: '3', offset: '9007199254740993', size: '18446744073709551615', binding: 2, kind: 3}],
    draws: [draw]};
}

