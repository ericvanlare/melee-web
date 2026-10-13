/** Retain the existing runtime recorder without adding a live gameplay observer. */
export function incidentRecordingUrl(value) {
  const url = new URL(value);
  if (url.protocol !== 'http:' || url.hostname !== '127.0.0.1' ||
      url.port !== '8787' || url.pathname !== '/runtime.html')
    throw Error('Incident recording requires the supported loopback runtime page');
  const requested = url.searchParams.getAll('melee-web-staging-diagnostics');
  if (requested.some(value => value !== '1'))
    throw Error('Room captures require staging incident diagnostics enabled');
  url.searchParams.set('melee-web-staging-diagnostics', '1');
  return url;
}

export async function readIncidentRecorderReadiness(page) {
  return page.evaluate(() => ({
    requested: new URL(location.href).searchParams.get('melee-web-staging-diagnostics') === '1',
    export_available: typeof window.meleeNetRuntimeDiagnosticExport === 'function',
    ring_status: window.__meleeWebStagingRingStatus ?? null,
  }));
}

export function requireIncidentRecorderReadiness(value) {
  const selection = value?.ring_status?.selection;
  if (value?.requested !== true || value?.export_available !== true ||
      selection?.staging_diagnostics_requested !== true ||
      selection?.staging_diagnostics_enabled !== true ||
      selection?.staging_diagnostics_reason !== 'explicit_loopback_opt_in')
    throw Error('Runtime staging incident recorder readiness was not verified');
  return value;
}

export function summarizeRuntimeIncidents(exported, beforeLeave) {
  const current = exported?.current;
  if (current?.schema !== 'melee-web-runtime-diagnostics' ||
      typeof current.session_id !== 'string' || !Array.isArray(current.incidents) ||
      !Array.isArray(exported?.retained?.records))
    throw Error('Existing runtime incident recorder did not export its current session');
  const timingReasons = new Set(['simulation_debt', 'audio_debt', 'nonfinite_clock', 'clock_regression']);
  const timing = current.incidents.filter(row => timingReasons.has(row.reason));
  const stoppedTimingMessage = beforeLeave?.native_running === 0 &&
    beforeLeave?.native_message?.startsWith('Paused after a timing disruption') === true;
  return {
    session_id: current.session_id,
    h1_timing_pause_observed: timing.length > 0 || stoppedTimingMessage,
    timing_incident_count: timing.length,
    stopped_timing_message: stoppedTimingMessage,
    staging_incident_summaries: current.incidents.map(row => ({
      id: row.id, reason: row.reason, source_frame: row.source_frame,
      value: row.value, threshold: row.threshold, staging: row.staging ?? null,
    })),
    missing_timing_staging_count: timing.filter(row => row.staging == null).length,
    scope: 'H1 observation only; callback latency includes GPU queue and browser delivery, not hardware GPU duration',
  };
}
