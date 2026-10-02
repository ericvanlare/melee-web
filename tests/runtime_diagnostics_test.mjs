import assert from 'node:assert/strict';
import {createRuntimeDiagnostics} from '../web/runtime-diagnostics.mjs';

const nativeRow = (timestamp, sourceFrame = 12) => [
  timestamp, sourceFrame, 3, 2, 1, 1, 1, 1, 0, 4, 3, 18, 0, 0, 0, 0, 6, 6, 1,
];

// The production mapping accepts the two fixed public hosts and immutable
// lowercase hexadecimal deployment names, while arbitrary hosts stay unknown.
{
  const production = createRuntimeDiagnostics({origin: 'https://a1b2c3d4.webmelee.pages.dev'});
  assert.equal(production.exportReports().environment.kind, 'production');
  assert.equal(production.exportReports().environment.host, 'a1b2c3d4.webmelee.pages.dev');

  const staging = createRuntimeDiagnostics({origin: 'https://staging.webmelee.gg'});
  assert.equal(staging.exportReports().environment.kind, 'staging');
  const retiredStaging = createRuntimeDiagnostics({origin: 'https://staging.webmelee.pages.dev'});
  assert.equal(retiredStaging.exportReports().environment.kind, 'unknown');

  const unknown = createRuntimeDiagnostics({origin: 'https://a1b2c3d4.example.test/private-user-path'});
  assert.deepEqual(unknown.exportReports().environment, {
    kind: 'unknown', host: null, origin: 'https://a1b2c3d4.example.test', secure: false,
  });
}

{
  const unavailable = createRuntimeDiagnostics({nativeAvailable: false, audioAvailable: false});
  unavailable.observeNative(...nativeRow(0));
  unavailable.audio({timestamp: 0, underruns: 1});
  const report = unavailable.exportReports();
  assert.equal(report.capabilities.native.available, false);
  assert.equal(report.capabilities.native.reason, 'unavailable');
  assert.equal(report.capabilities.audio.available, false);
  assert.equal(report.capabilities.audio.reason, 'unavailable');
}

// Positional observations use the fixed numeric ring and keep a ten-second,
// ten-Hz history.  Source frame -1 is an unavailable value in exported data.
{
  let timestamp = 0;
  const diagnostics = createRuntimeDiagnostics({
    identity: {
      sourceCommit: 'ABCDEF1234567890ABCDEF1234567890ABCDEF12',
      runtimeHash: '1234567890ABCDEF', buildProfile: 'production',
    },
    origin: 'https://webmelee.gg',
    now: () => timestamp,
    historySeconds: 1,
    historyHz: 10,
    maxIncidents: 4,
  });
  for (let i = 0; i < 25; i++) {
    timestamp = i * 100;
    assert.equal(diagnostics.observeNative(...nativeRow(timestamp, -1)), i === 0 || i >= 1);
  }
  const incidentIds = [];
  for (let i = 0; i < 6; i++) incidentIds.push(diagnostics.trigger(1, i, 8, -1, 3, 1));
  const report = diagnostics.exportReports();
  assert.equal(report.identity.source_commit, 'abcdef1234567890abcdef1234567890abcdef12');
  assert.equal(report.identity.runtime_hash, '1234567890abcdef');
  assert.equal(report.identity.build_profile, 'production');
  assert.equal(report.incidents.length, 4);
  assert.equal(report.flags.incident_evicted, true);
  assert.equal(report.flags.history_evicted, true);
  assert.equal(report.incidents[0].source_frame, null);
  assert.equal(report.incidents[0].history.rows.length, 10);
  assert.equal(report.native.callback_count, 25);
  assert.equal(report.native.max_total_ms, 18);
  assert.equal(report.incidents[0].reason, 'simulation_debt');
  assert.equal(report.incidents[0].clock_owner, 'simulation');
  assert.deepEqual(incidentIds.slice(-4), report.incidents.map(incident => incident.id));
}

// Trigger and recovery context are bounded structured records.  Unknown
// reason/detail strings do not cross the export boundary.
{
  let timestamp = 10;
  const diagnostics = createRuntimeDiagnostics({
    origin: 'https://unknown.example', now: () => timestamp, postEventCap: 2,
  });
  const id = diagnostics.trigger('raw error: /synthetic-private/alice/save.gci', NaN, Infinity, NaN,
    'https://evil.example', 99);
  assert.equal(id, 'incident-1');
  diagnostics.longtask({startTime: 12, duration: 44, name: 'secret-file-name'});
  diagnostics.lifecycle('recovered', {timestamp: 14, sourceFrame: 4, detail: 'private'});
  diagnostics.lifecycle('another-private-event', {timestamp: 15});
  const report = diagnostics.exportReports();
  const incident = report.incidents[0];
  assert.equal(report.environment.host, null);
  assert.equal(incident.reason, 'unknown');
  assert.equal(incident.value, null);
  assert.equal(incident.threshold, null);
  assert.equal(incident.source_frame, null);
  assert.equal(incident.scene, 'unknown');
  assert.equal(incident.clock_owner, 'other');
  assert.equal(incident.recovery, 'recovered');
  assert.equal(incident.post_events.length, 2);
  assert.equal(incident.post_events[0].type, 'longtask');
  assert.equal(incident.post_events[1].kind, 'recovered');
  assert.equal(JSON.stringify(report).includes('secret-file-name'), false);
  assert.equal(JSON.stringify(report).includes('/Users/alice'), false);
}

// Scheduled pause is expected lifecycle context and must not become an
// unexpected incident; clock regression remains an explicit incident reason.
{
  let timestamp = 100;
  const diagnostics = createRuntimeDiagnostics({now: () => timestamp});
  assert.equal(diagnostics.trigger(9, 0, 0, -1, 1, 1), null);
  assert.equal(diagnostics.exportReports().incidents.length, 0);
  assert.equal(diagnostics.trigger(8, 12, 10, -1, 3, 0), 'incident-1');
  assert.equal(diagnostics.exportReports().incidents[0].reason, 'clock_regression');
}

// Storage failure and malformed records are reported as bounded flags.  They
// never escape the async persist boundary as a gameplay exception.
{
  const denied = createRuntimeDiagnostics({
    storage: {
      load: () => [{id: 'malformed', incident: 'nope'}],
      save: () => { throw Object.assign(new Error('denied'), {name: 'SecurityError'}); },
    },
  });
  denied.trigger(4, 1, 0, -1, 4, 0);
  const result = await denied.setActive(false);
  assert.equal(result.persisted, false);
  const report = denied.exportReports();
  assert.equal(report.flags.persistence_failed, true);
  assert.equal(report.flags.storage_denied, true);
  assert.equal(report.flags.malformed_record, true);
  assert.equal('message' in report, false);
}

{
  const quota = createRuntimeDiagnostics({
    storage: {save: () => Promise.reject(Object.assign(new Error('full'), {name: 'QuotaExceededError'}))},
  });
  quota.trigger(2, 60, 60, -1, 3, 2);
  const result = await quota.setActive(false);
  assert.equal(result.reason, 'quota');
  assert.equal(quota.exportReports().flags.quota_exceeded, true);
}

{
  let saved = null;
  const boundedStorage = createRuntimeDiagnostics({
    maxStorageBytes: 512,
    storage: {save: records => { saved = records; }},
  });
  for (let i = 0; i < 4; i++) boundedStorage.trigger(1, i, 8, -1, 7, 1);
  const result = await boundedStorage.setActive(false);
  assert.equal(result.persisted, true);
  assert.ok(result.bytes <= 512);
  assert.ok(Array.isArray(saved));
  assert.ok(saved.length <= 4);
  assert.equal(boundedStorage.exportReports().flags.storage_evicted, true);
}

{
  let retained = [];
  const storage = {
    load: () => retained,
    save: records => { retained = records; },
  };
  const first = createRuntimeDiagnostics({storage});
  first.trigger(1, 9, 8, -1, 7, 1);
  await first.setActive(false);
  assert.equal(retained.length, 1);
  const second = createRuntimeDiagnostics({storage});
  second.trigger(2, 60, 60, -1, 7, 2);
  await second.setActive(false);
  assert.equal(retained.length, 2, 'bounded persistence keeps prior incidents across sessions');
  assert.deepEqual(Object.keys(retained[0]).sort(), [
    'audio', 'capabilities', 'client', 'environment', 'flags', ' identity'.trim(), 'incident',
    'limits', 'schema', 'session_id', 'version', 'id',
  ].sort(), 'stored records carry only the safe metadata envelope');
}

// Session identifiers are ephemeral and coarse client identity is derived from
// explicit user-agent fields without persisting a browser fingerprint.
{
  const makeRoot = (ua, platform, crypto) => ({navigator: {userAgent: ua, platform}, crypto});
  const chromeAndroid = createRuntimeDiagnostics({
    globalThis: makeRoot('Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 Chrome/125.0.0.0 Mobile Safari/537.36', 'Linux armv8l', {
      getRandomValues(bytes) { bytes.fill(7); return bytes; },
    }),
  });
  const androidReport = chromeAndroid.exportReports();
  assert.deepEqual(androidReport.client, {platform: 'android', browser: 'chrome', browser_major: 125});
  const edge = createRuntimeDiagnostics({
    globalThis: makeRoot('Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Edg/120.1.2.3 Safari/537.36', 'Win32', {
      getRandomValues(bytes) { bytes.fill(8); return bytes; },
    }),
  });
  assert.deepEqual(edge.exportReports().client, {platform: 'windows', browser: 'edge', browser_major: 120});
  assert.notEqual(chromeAndroid.exportReports().session_id, edge.exportReports().session_id);
  const fallback = createRuntimeDiagnostics({globalThis: makeRoot('', '', {getRandomValues() { throw new Error('blocked'); }})});
  assert.equal(fallback.exportReports().flags.crypto_unavailable, true);
  assert.match(fallback.exportReports().session_id, /^session-[a-z0-9-]{16,96}$/);
}

// Native unavailable sentinels remain null, while valid transient deltas are
// summed into the bounded 100ms sample window.
{
  let timestamp = 0;
  const diagnostics = createRuntimeDiagnostics({now: () => timestamp, historySeconds: 1, historyHz: 10});
  diagnostics.observeNative(0, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1);
  timestamp = 50;
  diagnostics.observeNative(50, -1, 7, 3, 2, 2, 2, 2, 5, 1, 2, 3, 4, 2, 3, 4, 5, 6, 1);
  timestamp = 100;
  diagnostics.observeNative(100, -1, 7, 3, 2, 2, 2, 2, 6, 2, 3, 4, 5, 1, 2, 3, 4, 5, 1);
  const report = diagnostics.exportReports();
  assert.equal(report.native.max_total_ms, 4);
  assert.equal(report.native.interval_sum_ms, 100);
  const row = report.incidents.length ? report.incidents[0].history.rows.at(-1) : null;
  diagnostics.trigger(1, 10, 8, -1, 7, 1);
  const incident = diagnostics.exportReports().incidents[0];
  const last = incident.history.rows.at(-1);
  assert.equal(last[8], 6);
  assert.equal(last[13], 3);
  assert.equal(last[14], 5);
  assert.equal(last[15], 7);
  assert.equal(last[16], 9);
  assert.equal(last[17], 11);
  assert.equal(last[1], null);
  assert.equal(last[3], 3);
  assert.equal(row, null);
}

// Intentional nonfinite clock values are sanitized without turning into a
// malformed input flag; lifecycle timestamps and scenes remain deterministic.
{
  let timestamp = 42;
  const diagnostics = createRuntimeDiagnostics({now: () => timestamp});
  diagnostics.observeNative(0, 4, 7, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1);
  diagnostics.lifecycle('visibility_hidden');
  diagnostics.lifecycle('asset_preparation', {duration_ms: 4, bytes: 8, files: 1});
  diagnostics.trigger(3, NaN, Infinity, 4, undefined, 0);
  const report = diagnostics.exportReports();
  assert.equal(report.flags.malformed_record, false);
  assert.equal(report.incidents[0].value, null);
  assert.equal(report.incidents[0].threshold, null);
  assert.equal(report.incidents[0].pre_events[0].timestamp, 42);
  assert.equal(report.incidents[0].pre_events[0].scene, 'match');
  assert.deepEqual(report.incidents[0].pre_events[1].duration_ms, 4);
}

// Retained records are strictly validated, bounded by both record count and
// storage bytes, and reject unknown keys or oversized numeric payloads.
{
  let retained = [];
  const writer = createRuntimeDiagnostics({storage: {load: () => retained, save: records => { retained = records; }}, wallNow: () => 100});
  writer.trigger(1, 4, 8, -1, 7, 1);
  await writer.setActive(false);
  const valid = retained[0];
  const tampered = structuredClone(valid);
  tampered.incident.reason = 'private arbitrary text';
  const oversized = structuredClone(valid);
  oversized.incident.history.rows = Array.from({length: 600}, () => Array(20).fill(1e15));
  const records = Array.from({length: 6}, (_, index) => {
    const record = structuredClone(valid);
    record.session_id = `session-${String(index + 1).padStart(32, '0')}`;
    record.id = `${record.session_id}:incident-${index + 1}`;
    record.incident.id = `incident-${index + 1}`;
    record.incident.captured_at_ms = 100 + index;
    return record;
  });
  const reader = createRuntimeDiagnostics({storage: {load: () => [tampered, oversized, ...records]}});
  const exported = await reader.exportRetained();
  assert.equal(exported.records.length, 4);
  assert.equal(exported.flags.malformed_record, true);
  assert.equal(exported.flags.storage_evicted, true);
  assert.equal(exported.records[0].incident.captured_at_ms, 105);
}

// Resuming before the deferred inactive handoff cancels that save; the next
// genuine inactive transition persists exactly once.
{
  let merges = 0;
  const diagnostics = createRuntimeDiagnostics({storage: {
    merge(records) { merges++; return {records, bytes: 1, evictedCount: 0, malformedCount: 0}; },
  }});
  diagnostics.trigger(1, 1, 8, -1, 7, 1);
  const pending = diagnostics.setActive(false);
  await diagnostics.setActive(true);
  assert.equal((await pending).reason, 'resumed');
  assert.equal(merges, 0);
  await diagnostics.setActive(false);
  assert.equal(merges, 1);
}

console.log('Runtime diagnostics bounds, privacy, host mapping, recovery and storage failure tests passed.');
