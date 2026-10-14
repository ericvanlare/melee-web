import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {incidentRecordingUrl, requireIncidentRecorderReadiness,
  summarizeRuntimeIncidents} from '../scripts/a3_runtime_incidents.mjs';

const base = 'http://127.0.0.1:8787/runtime.html';
const ready = () => ({requested: true, export_available: true, ring_status: {selection: {
  staging_diagnostics_requested: true, staging_diagnostics_enabled: true,
  staging_diagnostics_reason: 'explicit_loopback_opt_in',
}}});
const exported = (incidents = []) => ({current: {schema: 'melee-web-runtime-diagnostics',
  session_id: 'current-session', incidents}, retained: {records: []}});

test('room capture opts into staging diagnostics without changing its origin or other options', () => {
  const url = incidentRecordingUrl(base + '?example=kept');
  assert.equal(url.origin, new URL(base).origin);
  assert.equal(url.searchParams.get('example'), 'kept');
  assert.equal(url.searchParams.get('melee-web-staging-diagnostics'), '1');
  assert.throws(() => incidentRecordingUrl(base + '?melee-web-staging-diagnostics=0'));
  assert.throws(() => incidentRecordingUrl(base + '?melee-web-staging-diagnostics=1&melee-web-staging-diagnostics=0'));
  assert.throws(() => incidentRecordingUrl('https://example.com/runtime.html'));
});

test('missing recorder or declined staging opt-in refuses readiness', () => {
  assert.deepEqual(requireIncidentRecorderReadiness(ready()), ready());
  for (const corrupt of [row => row.requested = false, row => row.export_available = false,
    row => row.ring_status = null,
    row => row.ring_status.selection.staging_diagnostics_enabled = false,
    row => row.ring_status.selection.staging_diagnostics_reason = 'disabled']) {
    const row = ready(); corrupt(row);
    assert.throws(() => requireIncidentRecorderReadiness(row));
  }
});

test('only the exact isolated HTTPS staging origin can opt into hosted incidents', () => {
  const origin = 'https://melee-web-room-relay-staging-288.mywebcomic.workers.dev';
  for (const pathname of ['/', '/runtime.html'])
    assert.equal(incidentRecordingUrl(origin + pathname).origin, origin);
  for (const invalid of [origin.replace('https:', 'http:') + '/', origin + '.invalid/',
    origin + ':444/', origin + '/other.html', origin.replace('https://', 'https://user@') + '/',
    origin + '/#invite=anything']) assert.throws(() => incidentRecordingUrl(invalid));
  const hosted = ready();
  hosted.ring_status.selection.staging_diagnostics_reason = 'explicit_hosted_staging_opt_in';
  assert.equal(requireIncidentRecorderReadiness(hosted), hosted);
});

test('native and host recorder gates require opt-in on an allowed origin', () => {
  const patch = readFileSync(new URL('../patches/aurora-browser.patch', import.meta.url), 'utf8');
  const native = patch.slice(patch.indexOf('+bool resolve_browser_staging_diagnostics_enabled() noexcept {'));
  const source = native.slice(native.indexOf('+    try {'), native.indexOf('+  }) != 0;'))
    .split('\n').map(line => line.startsWith('+') ? line.slice(1) : line).join('\n');
  const runNative = new Function('window', source);
  const host = readFileSync(new URL('../web/melee-runtime.mjs', import.meta.url), 'utf8');
  const start = host.indexOf('  let stagingDiagnosticsEnabled = false;');
  const body = host.slice(start, host.indexOf('  function diagnosticDeliveryBlocked()', start));
  const runHost = new Function('globalThis', 'recordDiagnostics', body + '\nreturn stagingDiagnosticsEnabled;');
  const staging = 'https://melee-web-room-relay-staging-288.mywebcomic.workers.dev';
  for (const origin of ['http://127.0.0.1:8787', staging, 'https://example.com', staging + ':444']) {
    for (const requested of [false, true]) for (const recorder of [false, true]) {
      const url = new URL(origin + '/' + (requested ? '?melee-web-staging-diagnostics=1' : ''));
      const expected = requested && recorder && ['http://127.0.0.1:8787', staging].includes(origin);
      const window = {location: url, __meleeWebStagingRingSelection: {}, menuStagingDiagnosticsEnabled: () => recorder};
      assert.equal(runNative(window), expected ? 1 : 0);
      assert.equal(runHost({location: url}, recorder), expected);
      if (expected) assert.equal(window.__meleeWebStagingRingSelection.staging_diagnostics_reason,
        origin === staging ? 'explicit_hosted_staging_opt_in' : 'explicit_loopback_opt_in');
    }
  }
});

test('current timing incidents retain staging evidence and classify H1 without inferring combat failure', () => {
  const incident = {id: 'incident-1', reason: 'simulation_debt', source_frame: 87,
    value: 71, threshold: 60, staging: {peak_occupied_slots: 2,
      peak_queue_completion_callback_ms: 42}};
  const row = summarizeRuntimeIncidents(exported([incident]), null);
  assert.equal(row.h1_timing_pause_observed, true);
  assert.equal(row.timing_incident_count, 1);
  assert.deepEqual(row.staging_incident_summaries, [incident]);
  assert.equal(row.missing_timing_staging_count, 0);
  assert.equal(summarizeRuntimeIncidents(exported([{...incident, staging: null}]), null)
    .missing_timing_staging_count, 1);
});

test('unrelated retained sessions and runtime failures do not become current H1 timing evidence', () => {
  const data = exported([{reason: 'runtime_failure'}]);
  data.retained.records.push({session_id: 'older', incident: {reason: 'simulation_debt'}});
  assert.equal(summarizeRuntimeIncidents(data, null).h1_timing_pause_observed, false);
  assert.equal(summarizeRuntimeIncidents(exported(), {native_running: 1,
    native_message: 'Paused after a timing disruption'}).h1_timing_pause_observed, false);
  assert.equal(summarizeRuntimeIncidents(exported(), {native_running: 0,
    native_message: 'Paused after a timing disruption'}).h1_timing_pause_observed, true);
});

test('unavailable or malformed export remains an evidence gap', () => {
  for (const data of [null, {}, {...exported(), current: null},
    {...exported(), retained: null}]) assert.throws(() => summarizeRuntimeIncidents(data, null));
});
