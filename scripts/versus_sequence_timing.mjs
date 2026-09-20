/**
 * Pure timing-plan accounting shared by the visible sequence runner and its
 * focused boundary tests.  This module does not read the DOM or reset runtime
 * state; callers provide the metrics snapshot and browser-driver errors.
 */

const finite = value => typeof value === 'number' && Number.isFinite(value);

function numeric(value) {
  return finite(value) ? value : null;
}

function clone(value) {
  return value === undefined || value === null ? value : JSON.parse(JSON.stringify(value));
}

/**
 * Merge completed preparation records into a sorted set of half-open-ish
 * wall-time intervals.  Incomplete profiles are omitted: the runtime state
 * gate separately rejects an interval that is still preparing.
 */
export function mergePreparationIntervals(entries = []) {
  if (!Array.isArray(entries)) throw new TypeError('Preparation entries must be an array');
  const intervals = [];
  for (const entry of entries) {
    const start = numeric(entry?.requested_at);
    const end = numeric(entry?.preparation_done);
    if (start === null || end === null) continue;
    if (end < start) throw new RangeError('Preparation interval ends before it starts');
    intervals.push([start, end]);
  }
  intervals.sort((left, right) => left[0] - right[0] || left[1] - right[1]);
  const merged = [];
  for (const interval of intervals) {
    const previous = merged.at(-1);
    if (previous && interval[0] <= previous[1]) previous[1] = Math.max(previous[1], interval[1]);
    else merged.push([...interval]);
  }
  return merged;
}

function longTaskBounds(record) {
  const started = numeric(record?.started ?? record?.start_time);
  const duration = numeric(record?.duration_ms ?? record?.duration);
  if (started === null || duration === null || duration < 0) return null;
  return {started, ended: started + duration, duration};
}

/**
 * Account for long-task records without deleting the raw observations.
 *
 * A task is excluded only when its measured interval is wholly contained in a
 * merged preparation interval.  A task crossing either preparation boundary
 * remains a live violation, even when most of its duration overlaps setup.
 */
export function accountLongTasks(records = [], preparationEntries = [], options = {}) {
  if (!Array.isArray(records)) throw new TypeError('Long-task records must be an array');
  const sequenceStart = numeric(options.sequenceStart);
  const sequenceEnd = numeric(options.sequenceEnd);
  if (sequenceStart !== null && sequenceEnd !== null && sequenceEnd < sequenceStart) {
    throw new RangeError('Timing sequence ends before it starts');
  }
  const preparationIntervals = mergePreparationIntervals(preparationEntries);
  const rawRecords = clone(records);
  const measured = [];
  let invalid = 0;
  for (const record of records) {
    const bounds = longTaskBounds(record);
    if (!bounds) {
      invalid++;
      measured.push({record: clone(record), invalid: true, excluded: false});
      continue;
    }
    const started = Math.max(bounds.started, sequenceStart ?? -Infinity);
    const ended = Math.min(bounds.ended, sequenceEnd ?? Infinity);
    if (ended <= started) continue;
    const entirelyPreparation = preparationIntervals.some(([prepStart, prepEnd]) =>
      started >= prepStart && ended <= prepEnd);
    measured.push({record: clone(record), started, ended,
      duration_ms: ended - started, entirely_preparation: entirelyPreparation,
      excluded: entirelyPreparation, invalid: false});
  }
  const remaining = measured.filter(item => item.invalid ||
    (!item.excluded && item.duration_ms > 0));
  return {
    raw_records: rawRecords,
    preparation_intervals: preparationIntervals,
    records: measured,
    excluded_count: measured.filter(item => item.excluded).length,
    invalid_count: invalid,
    remaining,
  };
}

function longTaskCapability(metrics) {
  const longtasks = metrics?.browser?.longtasks;
  if (typeof longtasks?.supported === 'boolean') return {supported: longtasks.supported};
  const direct = longtasks?.capability;
  const capture = metrics?.diagnostic_capture?.capabilities?.longtask;
  return direct ?? capture ?? null;
}

function requirePositive(failures, value, label) {
  if (!finite(value) || value <= 0) failures.push(`${label}: ${value ?? 'missing'}`);
}

function requireZero(failures, value, label) {
  if (!finite(value) || value !== 0) failures.push(`${label}: ${value ?? 'missing'}`);
}

/**
 * Return timing-only failures for a completed functional sequence.
 * Functional route checks (CSS/SSS/VS/Results/CSS) remain the caller's job.
 * `errors` is the browser driver's captured page/console error list.
 */
export function timingFailures(metrics, {errors = []} = {}) {
  const failures = [];
  if (!metrics || typeof metrics !== 'object') return ['timing metrics missing'];
  if (metrics.schema !== 'melee-web-menu-sequence-metrics') {
    failures.push(`timing metrics schema: ${metrics.schema ?? 'missing'}`);
  }
  if (!Array.isArray(errors)) throw new TypeError('timingFailures errors must be an array');
  if (errors.length) failures.push(`browser diagnostics errors: ${errors.length}`);

  requirePositive(failures, metrics.source?.steps, 'source steps');
  requirePositive(failures, metrics.source?.draws, 'source draws');
  requirePositive(failures, metrics.native?.callbacks?.total, 'native callbacks');
  requirePositive(failures, metrics.browser?.callbacks, 'browser callbacks');
  requireZero(failures, metrics.native?.budget?.total, 'native callback target misses');
  requireZero(failures, metrics.native?.over_33ms?.total, 'native hard gaps');
  requireZero(failures, metrics.browser?.gaps_over_33ms, 'browser hard gaps');
  requireZero(failures, metrics.pipelines?.queued, 'live pipeline queues');
  requireZero(failures, metrics.pipelines?.created, 'live pipeline creation');
  requireZero(failures, metrics.interval?.audio_underruns, 'audio underruns');
  requireZero(failures, metrics.interval?.audio_overflows, 'audio overflows');

  const capability = longTaskCapability(metrics);
  if (capability?.supported !== true) {
    failures.push(capability ? 'long-task capability unsupported' : 'long-task capability missing');
  }
  const longtasks = metrics.browser?.longtasks;
  const records = longtasks?.records;
  if (!Array.isArray(records)) {
    failures.push('long-task records missing');
  } else {
    if (longtasks.overflow === true) failures.push('long-task record overflow');
    if (!Number.isInteger(longtasks.count) || longtasks.count < 0) {
      failures.push('long-task record count missing or invalid');
    } else if (longtasks.overflow !== true && longtasks.count !== records.length) {
      failures.push(`long-task record count mismatch: ${longtasks.count} != ${records.length}`);
    }
    const account = accountLongTasks(records, metrics.scenes?.entries || [], {
      sequenceStart: metrics.sequence?.started_ms,
      sequenceEnd: metrics.sequence?.ended_ms,
    });
    if (account.invalid_count) failures.push(`invalid long-task records: ${account.invalid_count}`);
    if (account.remaining.length) failures.push(`live browser long tasks: ${account.remaining.length}`);
  }

  const state = metrics.state || {};
  const focus = state.focus || {};
  if (state.fatal) failures.push('fatal runtime');
  if (state.timing_invalid) failures.push('invalid timing');
  if (state.preparation_active) failures.push('unfinished preparation');
  if (focus.sequence_lost) failures.push('focus loss');
  if (focus.hidden) failures.push('hidden page');
  if (focus.has_focus !== true) failures.push('unfocused page');
  if (state.running !== true) failures.push('stopped runtime');
  return failures;
}
