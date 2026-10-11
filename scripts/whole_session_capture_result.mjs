/** Final transport gates only; exact game-state comparison is a separate step. */
export const REQUIRED_SESSION_DOWNLOADS = Object.freeze([
  'retail-port.jsonl', 'retail-browser-report.json',
]);

export const FIRST_REPLAY_BOUNDARY_MARKER_PREFIX = 'MELEE_WEB_FIRST_REPLAY_CALLBACK_V1 ';
const REPLAY_BOUNDARY_PREFIX = FIRST_REPLAY_BOUNDARY_MARKER_PREFIX;
const REPLAY_BOUNDARY_PAIR_BASES = Object.freeze([
  'header_emit', 'header_onLog', 'menu_replay_started_dispatch',
  'menu_replay_started_js', 'native_memory_snapshot', 'css_tick',
  'session_frame_emit', 'session_frame_onLog', 'frame0_emit', 'frame0_onLog',
  'ordinary_audio_boundary', 'ordinary_audio_tick', 'source_frames_finish',
  'aurora_begin_frame', 'css_host_draw', 'aurora_end_frame',
  'first_replay_callback_tail', 'native_pause',
]);
export const FIRST_REPLAY_BOUNDARY_MARKER_NAMES = Object.freeze(
  REPLAY_BOUNDARY_PAIR_BASES.flatMap(base => [`${base}_begin`, `${base}_returned`]));
const REPLAY_BOUNDARY_MARKER_NAME_SET = new Set(FIRST_REPLAY_BOUNDARY_MARKER_NAMES);
const REQUIRED_REPLAY_BOUNDARY_PAIRS = Object.freeze([
  'header_emit', 'header_onLog', 'menu_replay_started_dispatch',
  'menu_replay_started_js', 'native_memory_snapshot', 'css_tick',
  'session_frame_emit', 'session_frame_onLog', 'frame0_emit', 'frame0_onLog',
  'ordinary_audio_boundary', 'source_frames_finish', 'aurora_begin_frame',
  'css_host_draw', 'aurora_end_frame', 'first_replay_callback_tail', 'native_pause',
]);

export function parseFirstReplayBoundaryMarker(text) {
  if (typeof text !== 'string' || !text.startsWith(REPLAY_BOUNDARY_PREFIX)) return null;
  let marker;
  try { marker = JSON.parse(text.slice(REPLAY_BOUNDARY_PREFIX.length)); }
  catch (error) { throw Error(`Malformed first-replay-callback marker JSON: ${error.message}`); }
  if (!marker || !Number.isSafeInteger(marker.sequence) || marker.sequence < 1 ||
      !REPLAY_BOUNDARY_MARKER_NAME_SET.has(marker.marker) ||
      !Number.isFinite(marker.page_timestamp_ms) || !Array.isArray(marker.values) ||
      marker.values.length !== 3 || marker.values.some(value => !Number.isSafeInteger(value)))
    throw Error('Malformed first-replay-callback marker fields');
  return marker;
}

export function inspectFirstReplayBoundaryMarkers(markers) {
  const errors = [];
  const open = [];
  const pairCounts = Object.fromEntries(REPLAY_BOUNDARY_PAIR_BASES.map(base => [base, 0]));
  const returnedAt = {};
  for (let index = 0; index < markers.length; index++) {
    const row = markers[index];
    if (row.sequence !== index + 1)
      errors.push(`Marker sequence gap at receipt ${index + 1}: observed ${row.sequence}`);
    const isBegin = row.marker.endsWith('_begin');
    const isReturned = row.marker.endsWith('_returned');
    if (!isBegin && !isReturned) continue;
    const base = row.marker.replace(/_(?:begin|returned)$/, '');
    if (isBegin) open.push({base, marker: row.marker, sequence: row.sequence});
    else if (open.at(-1)?.base === base) {
      open.pop();
      pairCounts[base]++;
      returnedAt[base] ??= row.sequence;
    } else errors.push(`Unexpected ${row.marker} at sequence ${row.sequence}`);
  }
  for (const base of REQUIRED_REPLAY_BOUNDARY_PAIRS) {
    if (!pairCounts[base]) errors.push(`Required marker pair did not return: ${base}`);
  }
  const pause = [...markers].reverse().find(row => row.marker === 'native_pause_returned');
  if (pause?.values[0] !== 0) errors.push('Native pause did not report running=0');
  const firstSequence = name => markers.find(row => row.marker === name)?.sequence ?? null;
  const sourceOrder = [
    ['css_tick_returned', 'session_frame_emit_begin'],
    ['frame0_emit_returned', 'ordinary_audio_boundary_begin'],
    ['ordinary_audio_boundary_returned', 'source_frames_finish_begin'],
    ['source_frames_finish_returned', 'first_replay_callback_tail_begin'],
    ['first_replay_callback_tail_begin', 'native_pause_begin'],
  ];
  for (const [before, after] of sourceOrder) {
    const beforeSequence = firstSequence(before), afterSequence = firstSequence(after);
    if (beforeSequence === null || afterSequence === null || beforeSequence >= afterSequence)
      errors.push(`Source boundary order differs: ${before} must precede ${after}`);
  }
  const callbackTail = [...markers].reverse().find(row => row.marker === 'first_replay_callback_tail_begin');
  if (!callbackTail || callbackTail.values[0] < 1 || callbackTail.values[1] < 1)
    errors.push('First replay callback did not report at least one source step and source draw');
  const cssTick = markers.find(row => row.marker === 'css_tick_begin');
  if (!cssTick || cssTick.values[1] !== 0 || cssTick.values[2] !== 1)
    errors.push('First replay callback did not begin at the CSS cursor-zero boundary');
  const frameZero = markers.find(row => row.marker === 'frame0_emit_begin');
  if (!frameZero || frameZero.values[0] !== 0 || frameZero.values[1] !== 1)
    errors.push('Frame-zero diagnostic did not identify CSS cursor zero');
  const drawStageNames = [
    'aurora_begin_frame_begin', 'aurora_begin_frame_returned',
    'css_host_draw_begin', 'css_host_draw_returned',
    'aurora_end_frame_begin', 'aurora_end_frame_returned',
  ];
  const drawRows = new Map();
  for (const row of markers) {
    if (!drawStageNames.includes(row.marker)) continue;
    const index = row.values[0];
    const draw = drawRows.get(index) || {};
    if (draw[row.marker]) errors.push(`Duplicate ${row.marker} for source draw ${index}`);
    else draw[row.marker] = row;
    drawRows.set(index, draw);
  }
  const sourceDrawBoundaries = [...drawRows.entries()].sort(([a], [b]) => a - b)
    .map(([index, draw]) => ({index, markers: Object.fromEntries(drawStageNames
      .filter(name => draw[name]).map(name => [name, draw[name].sequence]))}));
  for (let index = 0; index < sourceDrawBoundaries.length; index++) {
    const row = sourceDrawBoundaries[index];
    const draw = drawRows.get(row.index);
    if (row.index !== index) errors.push(`Source draw index gap: expected ${index}, observed ${row.index}`);
    const required = ['aurora_begin_frame_begin', 'aurora_begin_frame_returned',
      'aurora_end_frame_begin', 'aurora_end_frame_returned'];
    if (index === 0) required.push('css_host_draw_begin', 'css_host_draw_returned');
    for (const name of required) if (!draw[name])
      errors.push(`Source draw ${row.index} is missing ${name}`);
    const ordered = ['aurora_begin_frame_begin', 'aurora_begin_frame_returned',
      'css_host_draw_begin', 'css_host_draw_returned', 'aurora_end_frame_begin',
      'aurora_end_frame_returned'].filter(name => draw[name]);
    for (let stage = 1; stage < ordered.length; stage++) {
      if (draw[ordered[stage - 1]].sequence >= draw[ordered[stage]].sequence)
        errors.push(`Source draw ${row.index} boundary order differs at ${ordered[stage]}`);
    }
    if (draw.aurora_begin_frame_returned?.values[2] !== 1)
      errors.push(`Source draw ${row.index} did not enter aurora_begin_frame`);
    if (draw.css_host_draw_returned && draw.css_host_draw_returned.values[1] !== 1)
      errors.push(`CSS host draw ${row.index} did not report a completed draw`);
    if (draw.aurora_end_frame_returned &&
        (draw.aurora_end_frame_returned.values[1] !== row.index ||
         draw.aurora_end_frame_returned.values[2] !== 1))
      errors.push(`Source draw ${row.index} counters do not match its returned boundary`);
  }
  const finish = markers.find(row => row.marker === 'source_frames_finish_returned');
  const finishBegin = markers.find(row => row.marker === 'source_frames_finish_begin');
  const tailReturn = markers.find(row => row.marker === 'first_replay_callback_tail_returned');
  if (!finish || !finishBegin || !callbackTail ||
      finish.values[0] !== callbackTail.values[0] ||
      finish.values[1] !== callbackTail.values[1] ||
      finish.values[2] !== callbackTail.values[2])
    errors.push('SourceFrameSequence finish and callback-tail counters differ');
  if (finish && finish.values[1] !== sourceDrawBoundaries.length)
    errors.push('SourceFrameSequence draw count differs from observed draw boundaries');
  if (pause && finish && (pause.values[1] !== finish.values[0] || pause.values[2] !== finish.values[2]))
    errors.push('Native pause counters differ from completed source-frame counters');
  if (finishBegin && finish) {
    const drawsBeforeFinish = sourceDrawBoundaries.filter(row =>
      drawRows.get(row.index).aurora_end_frame_returned?.sequence < finishBegin.sequence).length;
    if (finishBegin.values[0] !== finish.values[0] || finishBegin.values[2] !== finish.values[2] ||
        finishBegin.values[1] !== drawsBeforeFinish)
      errors.push('SourceFrameSequence finish-start counters do not match prior source steps/draws/cursor');
    if (sourceDrawBoundaries.some(row =>
      drawRows.get(row.index).aurora_end_frame_returned?.sequence >= finish.sequence))
      errors.push('A source draw returned after SourceFrameSequence finish');
  }
  if (tailReturn && callbackTail && (tailReturn.values[0] !== callbackTail.values[0] ||
      tailReturn.values[1] !== callbackTail.values[1] || tailReturn.values[2] !== 0))
    errors.push('Callback tail returned with changed counters or non-paused state');
  return {
    complete: errors.length === 0 && open.length === 0,
    errors,
    pair_counts: pairCounts,
    returned_at: returnedAt,
    first_unmatched_marker: open[0] || null,
    deepest_unmatched_marker: open.at(-1) || null,
    first_missing_pair: REQUIRED_REPLAY_BOUNDARY_PAIRS.find(base => !pairCounts[base]) || null,
    callback_source_steps: finish?.values[0] ?? null,
    callback_source_draws: finish?.values[1] ?? null,
    replay_cursor_at_pause: pause?.values[2] ?? null,
    css_tick_markers: markers.filter(row => row.marker === 'css_tick_begin').length,
    source_draw_boundaries: sourceDrawBoundaries,
    native_pause_running: pause?.values[0] ?? null,
  };
}

export function validateRuntimeDataAbort(evidence) {
  if (!evidence || typeof evidence !== 'object') return false;
  const positive = value => Number.isSafeInteger(value) && value > 0;
  const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
  return evidence.requestCount === 1 && evidence.responseCount === 1 &&
    evidence.failureCount === 1 && evidence.finishedCount === 0 &&
    evidence.url === evidence.expectedUrl && evidence.method === 'GET' &&
    evidence.resourceType === 'fetch' && evidence.errorText === 'net::ERR_ABORTED' &&
    evidence.responseStatus === 200 && positive(evidence.expectedBytes) &&
    positive(evidence.contentLength) && evidence.contentLength === evidence.expectedBytes &&
    evidence.loadedBytes === evidence.expectedBytes &&
    evidence.totalBytes === evidence.expectedBytes &&
    evidence.fileBytes === evidence.expectedBytes && evidence.fromCache === false &&
    digest(evidence.expectedSha256) && evidence.actualSha256 === evidence.expectedSha256;
}

// Bind requested mode and its final Match span; native remains the full
// context/setup/PAD validator. This never derives expectations from a report.
// Existing tools/whole_session_replay.py / gameplay_retail_recipe.hpp layout:
// 20-byte envelope + 8-byte v2 context header + context + StartMeleeData (0x138)
// + full PAD (822) + 44 bytes/input frame + u16 span count + 12 bytes/span.
export function readRequestedEntityPrefix(bytes, recipeSha256) {
  if (bytes.length < 24 || ![1, 2].includes(bytes.readUInt16BE(22))) return null;
  const activeClock = bytes.readUInt16BE(22) === 2;
  if (bytes.readUInt32BE(4) !== 8 || bytes.length < 28 ||
      bytes.readUInt16BE(20) !== 2 ||
      bytes.readUInt32BE(24) !== 0x18 + 0x55e8 + 0x148 + 6 ||
      !/^[a-f0-9]{64}$/.test(recipeSha256 || ''))
    throw Error('Invalid requested entity-prefix envelope');
  const frames = bytes.readUInt32BE(12);
  const spans = 28 + bytes.readUInt32BE(24) + 0x138 + 822 + frames * 44;
  if (frames < 1 || frames > 108000 || spans + 2 + 3 * 12 !== bytes.length ||
      bytes.readUInt16BE(spans) !== 3)
    throw Error('Entity prefix lacks its bound CSS/SSS/Match interval');
  let next = 0, observations = 0;
  for (let index = 0; index < 3; index++) {
    const offset = spans + 2 + index * 12;
    const first = bytes.readUInt32BE(offset + 4), last = bytes.readUInt32BE(offset + 8);
    if (bytes[offset] !== index + 1 || bytes[offset + 1] !== 0 ||
        bytes.readUInt16BE(offset + 2) !== 0 || first !== next || last < first || last >= frames)
      throw Error('Entity prefix scene spans differ from the requested interval');
    next = last + 1;
    if (index === 2) observations = last - first + 1;
  }
  if (next !== frames || observations < (activeClock ? 61 : 60) || observations > (activeClock ? 604 : 64))
    throw Error('Entity prefix Match interval is outside its observed-source bound');
  return {name: activeClock ? 'jiggly-ice-mario-fox-active60-v1' : 'jiggly-ice-mario-fox-v1',
    frames, observations, recipe_sha256: recipeSha256};
}

export function sessionReplayReportCompleted(value, requestedPrefix = null) {
  if (value?.pass !== true || !Array.isArray(value.failures) || value.failures.length ||
      (value.errors !== undefined && (!Array.isArray(value.errors) || value.errors.length))) return false;
  if (!requestedPrefix)
    return value.complete === true && ['diagnostic_prefix', 'diagnostic_prefix_complete',
      'whole_session_equivalent', 'comparison_source_ticks', 'comparison_active_clock_ticks',
      'source_progress'].every(key => value[key] === undefined);
  const progress = value.source_progress, metrics = value.metrics;
  const activeClock = requestedPrefix.name === 'jiggly-ice-mario-fox-active60-v1';
  return value.schema === 'melee-web-browser-retail-replay' && value.version === 1 &&
    (requestedPrefix.name === 'jiggly-ice-mario-fox-v1' || activeClock) &&
    Number.isSafeInteger(requestedPrefix.frames) && requestedPrefix.frames >= 1 &&
    /^[a-f0-9]{64}$/.test(requestedPrefix.recipe_sha256 || '') &&
    Number.isInteger(requestedPrefix.observations) && requestedPrefix.observations >= (activeClock ? 61 : 60) &&
    requestedPrefix.observations <= (activeClock ? 604 : 64) && value.diagnostic_prefix === requestedPrefix.name &&
    value.diagnostic_prefix_complete === true && value.complete === false &&
    value.whole_session_equivalent === false &&
    value.comparison_source_ticks === (activeClock ? requestedPrefix.observations : 60) &&
    (activeClock ? value.comparison_active_clock_ticks === 60 : value.comparison_active_clock_ticks === undefined) &&
    value.mode === 'state_capture' && value.final_scene === 3 &&
    value.recipe_sha256 === requestedPrefix.recipe_sha256 && value.frames === requestedPrefix.frames &&
    progress?.observations === requestedPrefix.observations &&
    progress.bound_observations === requestedPrefix.observations &&
    progress.first_source_tick === 0 && progress.last_source_tick === requestedPrefix.observations - 1 &&
    metrics?.sourceFrames === requestedPrefix.frames && metrics.sourceSteps === requestedPrefix.frames &&
    metrics.sourceDraws === requestedPrefix.frames &&
    value.source_match?.complete === false && value.source_match.outcome === null && value.source_match.winner === null;
}

export function finalizeSessionCapture(report) {
  const failures = [];
  if (!report.phases?.some(row => row.name === 'whole-session-replay' && row.result === 'pass'))
    failures.push('Whole-session replay did not complete successfully');
  if (!sessionReplayReportCompleted(report.browser_report, report.requested_entity_prefix))
    failures.push('Browser replay report is incomplete or failed');
  if (report.first_error || report.failure || report.first_mismatch)
    failures.push('A fatal harness diagnostic was recorded');
  if (report.browser_errors?.length)
    failures.push('Browser errors were recorded');
  if (report.verified_runtime_data_aborts !== undefined &&
      (!Array.isArray(report.verified_runtime_data_aborts) ||
       report.verified_runtime_data_aborts.some(evidence => !validateRuntimeDataAbort(evidence))))
    failures.push('Runtime package abort evidence was malformed or incomplete');
  if (report.unexpected_requests?.length)
    failures.push('Unexpected non-GET requests were recorded');
  for (const key of ['download_error', 'cpu_download_error', 'owner_trace_error',
    'source_allocation_trace_error', 'rng_draw_probe_error', 'page_dump_error',
    'screenshot_error', 'close_error']) {
    if (report[key]) failures.push(`${key}: ${report[key]}`);
  }
  for (const name of REQUIRED_SESSION_DOWNLOADS) {
    const rows = (report.saved_downloads || []).filter(row => row.name === name);
    if (rows.length !== 1 || !Number.isSafeInteger(rows[0].bytes) || rows[0].bytes <= 0 ||
        !/^[a-f0-9]{64}$/.test(rows[0].sha256 || ''))
      failures.push(`Required artifact was not exported exactly once with nonempty hashed bytes: ${name}`);
  }
  if (report.deliberate_prefix_stop)
    failures.push('Deliberate prefix stop is incomplete session evidence');
  report.finalization_failures = failures;
  report.result = report.deliberate_prefix_stop ? 'incomplete' : failures.length ? 'fail' : 'pass';
  return report.result === 'pass' ? 0 : 1;
}

/** Bound an observation without assuming it cancels work in a stuck renderer. */
export async function boundedCaptureOperation(operation, timeoutMs, label) {
  let timer;
  try {
    return await Promise.race([operation, new Promise((_, reject) => {
      timer = setTimeout(() => {
        const error = Error(`${label} exceeded ${timeoutMs} ms`);
        error.captureOperationTimeout = true;
        reject(error);
      }, timeoutMs);
    })]);
  } finally { clearTimeout(timer); }
}

export function retainFirstCaptureError(report, kind, message, phase, details = null) {
  if (!report.first_error)
    report.first_error = {kind, message: String(message), phase, details};
  return report.first_error;
}
