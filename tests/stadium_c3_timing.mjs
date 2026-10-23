export const monotonicNowNs = () => process.hrtime.bigint();

export const elapsedMs = (start, end = monotonicNowNs()) => Number(end - start) / 1e6;

export const roundMs = value => Number(value.toFixed(3));

export function summarizeTimingValues(values) {
  const sorted = values.filter(Number.isFinite).sort((a, b) => a - b);
  if (!sorted.length)
    return {count: 0, total_ms: null, mean_ms: null, p50_ms: null, p95_ms: null, max_ms: null};
  const total = sorted.reduce((sum, value) => sum + value, 0);
  const percentile = fraction => sorted[Math.min(sorted.length - 1,
    Math.ceil(sorted.length * fraction) - 1)];
  return {count: sorted.length, total_ms: roundMs(total),
    mean_ms: roundMs(total / sorted.length), p50_ms: roundMs(percentile(0.5)),
    p95_ms: roundMs(percentile(0.95)), max_ms: roundMs(sorted.at(-1))};
}

export async function measureAsync(events, stage, operation, now = monotonicNowNs) {
  const started = now();
  let completed = false;
  try {
    const result = await operation();
    completed = true;
    return result;
  } finally {
    const finished = now();
    events.push({stage, elapsed_ms: elapsedMs(started, finished), completed});
  }
}

export function measureSync(events, stage, operation, now = monotonicNowNs) {
  const started = now();
  let completed = false;
  try {
    const result = operation();
    completed = true;
    return result;
  } finally {
    const finished = now();
    events.push({stage, elapsed_ms: elapsedMs(started, finished), completed});
  }
}

export function summarizeStageEvents(rows) {
  const byStage = new Map();
  for (const row of rows) {
    for (const event of row.stage_events) {
      const group = byStage.get(event.stage) ?? [];
      group.push(event);
      byStage.set(event.stage, group);
    }
  }
  return Object.fromEntries([...byStage].sort(([a], [b]) => a.localeCompare(b)).map(([stage, events]) =>
    [stage, {
      attempted_count: events.length,
      completed_count: events.filter(event => event.completed).length,
      failed_count: events.filter(event => !event.completed).length,
      timings_ms: summarizeTimingValues(events.map(event => event.elapsed_ms)),
    }]));
}

export function summarizeFailedActionSamples(rows) {
  return rows.filter(row => !row.completed).map(row => {
    const failedEvents = row.stage_events.filter(event => !event.completed);
    const failedEvent = failedEvents[0] ?? null;
    return {
      schedule_index: row.schedule_index,
      action: row.action,
      phase: row.phase,
      completed: false,
      stage: failedEvent?.stage ?? 'action_control',
      failed_stages: failedEvents.map(event => event.stage),
      failed_operation_ms: failedEvent ? roundMs(failedEvent.elapsed_ms) : null,
      action_wall_ms: Number.isFinite(row.action_wall_ms) ? roundMs(row.action_wall_ms) : null,
    };
  });
}
