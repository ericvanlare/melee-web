import test from 'node:test';
import assert from 'node:assert/strict';
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
