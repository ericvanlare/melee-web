import assert from 'node:assert/strict';
import {createDiagnosticsDelivery, DELIVERY_MAX_BYTES, DELIVERY_MAX_RECORDS,
  DELIVERY_MAX_SESSION_UPLOADS, DELIVERY_RETENTION_MS} from '../web/runtime-diagnostics-delivery.mjs';
import {MAX_REPORT_BYTES, REASONS} from '../diagnostics/schema.mjs';

const tick = () => new Promise(resolve => setTimeout(resolve, 0));

function rootFor(fetch, online = true) {
  return {
    navigator: {onLine: online},
    WebAssembly: {},
    SharedArrayBuffer,
    crossOriginIsolated: false,
    AbortController,
    setTimeout,
    clearTimeout,
    fetch,
  };
}

function reportFor({origin = 'https://webmelee.gg', kind = null, incidents = null, session_id = 'c'.repeat(16),
  browser_major = 125, scene = 'gameplay'} = {}) {
  const incident = {
    id: 'incident-1', reason: 'simulation_debt', value: 9, threshold: 8, clock_owner: 'simulation',
    source_frame: 12, scene,
    history: {rows: [[1, undefined, 3], [4, 5, 6]]},
    pre_events: [{type: 'lifecycle', kind: 'active', timestamp: 10}],
    post_events: [{type: 'longtask', timestamp: 20, duration_ms: 5}],
  };
  return {
    schema: 'local-recorder', session_id, identity: {
      source_commit: 'a'.repeat(40), runtime_hash: 'b'.repeat(16), build_profile: 'player', schema_version: 1,
    },
    environment: {kind: kind || (origin.includes('staging') ? 'staging' : 'production'), origin},
    client: {browser: 'Chrome', browser_major, platform: 'macos'},
    capabilities: {audio: {available: true}},
    flags: {},
    incidents: incidents || [incident],
  };
}

function storage(initial = []) {
  const state = {values: initial, tombstones: [], loads: 0, saves: 0, saved: []};
  return {
    state,
    async load() { state.loads += 1; return state.values; },
    async save(values) { state.saves += 1; state.saved = structuredClone(values); state.values = structuredClone(values); },
    async loadTombstones() { return state.tombstones; },
    async saveTombstones(values) { state.tombstones = structuredClone(values); },
  };
}

function deterministicRandom() {
  let value = 0.03;
  return () => {
    value += 0.07;
    if (value >= 1) value -= 1;
    return value;
  };
}

async function testKnownHostsAndAllowlist() {
  const requests = [];
  const fetch = async (url, options) => {
    requests.push({url, options, body: JSON.parse(options.body)});
    return {status: 201};
  };
  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(fetch), origin: 'https://webmelee.gg', storage: storage(), random: deterministicRandom(), now: () => 100,
  });
  assert.equal(delivery.enqueue(reportFor()).accepted, true);
  assert.equal(requests.length, 0, 'active enqueue cannot send');
  delivery.setActive(false);
  const result = await delivery.flushWhenInactive();
  assert.equal(result.sent, 1);
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, 'https://webmelee.gg/api/diagnostics');
  assert.equal(requests[0].options.headers.origin, undefined);
  assert.equal(requests[0].options.credentials, 'omit');
  assert.equal(requests[0].options.mode, 'same-origin');
  assert.equal(requests[0].options.redirect, 'error');
  assert.equal(requests[0].options.cache, 'no-store');
  assert.equal(requests[0].options.referrerPolicy, 'no-referrer');
  const body = requests[0].body;
  assert.deepEqual(Object.keys(body).sort(), ['capabilities', 'client', 'environment', 'events', 'flags', 'history', 'identity', 'incident', 'incident_id', 'schema', 'session_id', 'version']);
  assert.equal(body.identity.source_commit, 'a'.repeat(40));
  assert.equal(body.incident.source_frame, 12);
  assert.equal(body.incident.reason, REASONS.includes('simulation_debt') ? 'simulation_debt' : 'native_callback_over_budget');
  assert.equal(body.history.rows[0].includes(0), false, 'missing values are never invented as zero');
  assert.equal(body.history.rows[0][1], null);
  assert.equal(new TextEncoder().encode(JSON.stringify(body)).byteLength <= MAX_REPORT_BYTES, true);
  delivery.dispose();

  const mappedUnknown = [];
  const unknownMajor = createDiagnosticsDelivery({globalThis: rootFor(async (url, options) => {
    mappedUnknown.push(JSON.parse(options.body)); return {status: 201};
  }), origin: 'https://webmelee.gg', storage: storage(), now: () => 100});
  unknownMajor.enqueue(reportFor({browser_major: 'unavailable', scene: 13}));
  unknownMajor.setActive(false);
  await unknownMajor.flushWhenInactive();
  assert.equal(mappedUnknown[0].client.browser_major, null);
  assert.equal(mappedUnknown[0].incident.scene, 'opening_vs');
  unknownMajor.dispose();

  const wwwCalls = [];
  const www = createDiagnosticsDelivery({globalThis: rootFor(async url => { wwwCalls.push(url); return {status: 201}; }),
    origin: 'https://www.webmelee.gg', storage: storage(), random: deterministicRandom(), now: () => 100});
  assert.equal(www.enqueue(reportFor({origin: 'https://www.webmelee.gg'})).accepted, true);
  www.setActive(false);
  await www.flushWhenInactive();
  assert.deepEqual(wwwCalls, ['https://www.webmelee.gg/api/diagnostics']);
  www.dispose();

  for (const origin of ['https://staging.webmelee.pages.dev', 'https://example.test', 'http://webmelee.gg', 'https://webmelee.gg/path']) {
    const calls = [];
    const localOnly = createDiagnosticsDelivery({globalThis: rootFor(async () => { calls.push(1); return {status: 201}; }), origin, storage: storage()});
    assert.equal(localOnly.enqueue(reportFor({origin})).accepted, false);
    assert.equal(localOnly.exportPending().environment, 'unknown');
    assert.equal(localOnly.exportPending().origin, new URL(origin).origin);
    localOnly.setActive(false);
    await localOnly.flushWhenInactive();
    assert.equal(calls.length, 0);
    localOnly.dispose();
  }
}

async function testBoundsDuplicatesAndMalformedStorage() {
  const persistent = storage();
  const firstVisit = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg', storage: persistent, now: () => 5});
  assert.equal(firstVisit.enqueue(reportFor()).accepted, true);
  await firstVisit.persist();
  firstVisit.dispose();
  let restoredCalls = 0;
  const secondVisit = createDiagnosticsDelivery({globalThis: rootFor(async () => { restoredCalls += 1; return {status: 201}; }), origin: 'https://webmelee.gg', storage: persistent, now: () => 6});
  secondVisit.setActive(false);
  assert.equal((await secondVisit.flushWhenInactive()).sent, 1, 'separate visits reload the bounded outbox');
  assert.equal(restoredCalls, 1);
  secondVisit.dispose();

  const saved = storage([
    {bad: true},
    {id: 'f'.repeat(32), report: {schema: 'bad'}},
  ]);
  let now = 1000;
  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://staging.webmelee.gg', storage: saved,
    now: () => now, random: () => 0.1,
  });
  const first = delivery.enqueue(reportFor({origin: 'https://staging.webmelee.gg'}));
  const duplicate = delivery.enqueue(reportFor({origin: 'https://staging.webmelee.gg'}));
  assert.equal(first.accepted, true);
  assert.deepEqual(duplicate.ids, first.ids);
  assert.equal(delivery.enqueue({reason: 'arbitrary_error', value: 'secret'}).accepted, false);
  for (const [index, reason] of ['audio_underrun', 'audio_overflow', 'native_deadline'].entries()) {
    assert.equal(delivery.enqueue(reportFor({incidents: [{...reportFor().incidents[0], id: `incident-${index + 2}`, reason}]})).accepted, false,
      `unsupported reason ${reason} must not be inferred as debt`);
  }
  for (let index = 1; index < 7; index += 1) {
    now += 1;
    assert.equal(delivery.enqueue(reportFor({origin: 'https://staging.webmelee.gg', session_id: `session-${index}`})).accepted, true);
  }
  const pending = delivery.exportPending();
  assert.equal(pending.records.length, DELIVERY_MAX_RECORDS);
  assert.equal(pending.flags.evicted, true);
  assert.equal(saved.state.loads, 0, 'active enqueue does not read storage on the simulation path');
  assert.equal(JSON.stringify(pending.records).length <= DELIVERY_MAX_BYTES, true);
  delivery.setActive(false);
  await delivery.flushWhenInactive();
  assert.equal(delivery.exportPending().flags.malformed, true, 'malformed old records are rejected before export');
  delivery.dispose();

  const unavailable = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: {async load() { throw Object.assign(new Error('denied'), {name: 'NotAllowedError'}); }, async save() { throw new Error('denied'); }}});
  unavailable.setActive(false);
  await assert.doesNotReject(() => unavailable.flushWhenInactive());
  assert.equal(unavailable.exportPending().flags.storage_unavailable, true);
  unavailable.dispose();
}

async function testOfflineRetryExpiryAndOptOut() {
  let now = 0;
  let calls = 0;
  const retryStorage = storage();
  const retry = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { calls += 1; return {status: calls === 3 ? 201 : 503}; }), origin: 'https://webmelee.pages.dev',
    storage: retryStorage, now: () => now, retryBaseMs: 100, random: () => 0.5,
  });
  retry.enqueue(reportFor({origin: 'https://webmelee.pages.dev'}));
  retry.setOnline(false);
  retry.setActive(false);
  assert.deepEqual(await retry.flushWhenInactive(), {sent: 0, pending: 1, skipped: true, offline: true});
  assert.equal(calls, 0);
  assert.equal(retryStorage.state.saves > 0, true, 'offline inactive reports are persisted for a later visit');
  retry.setOnline(true);
  const first = await retry.flushWhenInactive();
  assert.equal(first.pending, 1);
  assert.equal(calls, 1);
  now = 100;
  await retry.flushWhenInactive();
  assert.equal(calls, 2);
  now = 300;
  const sent = await retry.flushWhenInactive();
  assert.equal(sent.sent, 1);
  assert.equal(calls, 3);
  retry.dispose();

  now = 0;
  const expired = createDiagnosticsDelivery({globalThis: rootFor(async () => { throw new Error('must not send'); }), origin: 'https://webmelee.gg',
    storage: storage(), now: () => now, retentionMs: 10});
  expired.enqueue(reportFor());
  now = 11;
  expired.setActive(false);
  const expiredResult = await expired.flushWhenInactive();
  assert.equal(expiredResult.sent, 0);
  assert.equal(expired.exportPending().records.length, 0);
  assert.equal(expired.exportPending().flags.expired, true);
  expired.dispose();

  const optOutStorage = storage();
  const opted = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg', storage: optOutStorage});
  opted.enqueue(reportFor());
  assert.equal(opted.setOptOut(true), true);
  assert.equal(opted.enqueue(reportFor()).reason, 'disabled');
  assert.equal(opted.exportPending().records.length, 0);
  await opted.persist();
  assert.deepEqual(optOutStorage.state.saved, []);
  opted.dispose();
}

async function testCallbackAndPersistenceFailure() {
  let calls = 0;
  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { calls += 1; return {status: 201}; }), origin: 'https://webmelee-staging.pages.dev',
    report: reportFor({origin: 'https://webmelee-staging.pages.dev'}), storage: storage(), random: deterministicRandom(),
  });
  assert.equal(delivery.onIncident({id: 'incident-2', reason: 'audio_debt', value: 61, threshold: 60, clock_owner: 2, source_frame: null, scene: 7}).accepted, true);
  assert.equal(calls, 0);
  delivery.setActive(false);
  await tick();
  assert.equal(calls, 1, 'inactive transition may schedule a later task');
  delivery.dispose();

  const failing = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: {async load() { return []; }, async save() { throw Object.assign(new Error('quota'), {name: 'QuotaExceededError'}); }}});
  failing.enqueue(reportFor());
  await assert.doesNotReject(() => failing.persist());
  assert.equal(failing.exportPending().flags.persistence_failed, true);
  failing.dispose();
}

async function testResumeCancellationAndSessionBudget() {
  let resolveFetch;
  let calls = 0;
  const fetch = (url, options) => {
    calls += 1;
    return new Promise(resolve => { resolveFetch = () => resolve({status: 201}); });
  };
  const delivery = createDiagnosticsDelivery({globalThis: rootFor(fetch), origin: 'https://webmelee.gg', storage: storage(), now: () => 100});
  delivery.enqueue(reportFor());
  delivery.setActive(false);
  const flushing = delivery.flushWhenInactive();
  await tick();
  assert.equal(calls, 1);
  delivery.setActive(true);
  resolveFetch();
  const cancelled = await flushing;
  assert.equal(cancelled.skipped, true, 'resuming while fetch is in flight prevents completion-side mutation/persistence');
  assert.equal(delivery.exportPending().records.length, 1);
  delivery.dispose();

  let sent = 0;
  const budget = createDiagnosticsDelivery({globalThis: rootFor(async () => { sent += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: storage(), now: () => 200, random: deterministicRandom()});
  for (let index = 0; index < DELIVERY_MAX_SESSION_UPLOADS + 2; index += 1) {
    const result = budget.enqueue(reportFor({session_id: `session-${index + 1}`}));
    assert.equal(result.accepted, true);
  }
  budget.setActive(false);
  await budget.flushWhenInactive();
  assert.equal(sent, DELIVERY_MAX_SESSION_UPLOADS);
  assert.equal(budget.enqueue(reportFor({session_id: 'session-after-budget'})).reason, 'session_limit');
  budget.dispose();
}

async function testIncidentIdentityStatusAndMultiTabMerge() {
  const shared = storage();
  const first = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: shared, now: () => 10, random: deterministicRandom()});
  const source = reportFor({session_id: 'session-stable'});
  const accepted = first.enqueue(source);
  assert.equal(accepted.accepted, true);
  assert.equal(first.getStatus().queued, 1);
  assert.equal(first.getStatus().eligible, true, 'known host Settings remains available during active play');
  assert.equal(first.getStatus().ready_to_deliver, false);
  const originalValue = first.exportPending().records[0].report.incident.value;
  source.incidents[0].value = 9999;
  source.incidents[0].post_events.push({type: 'longtask', timestamp: 30, duration_ms: 7});
  assert.equal(first.exportPending().records[0].report.incident.value, originalValue,
    'queued packet is immutable after enqueue');
  const expanded = reportFor({session_id: 'session-stable'});
  expanded.incidents[0].post_events.push({type: 'longtask', timestamp: 31, duration_ms: 8});
  assert.deepEqual(first.enqueue(expanded).ids, accepted.ids,
    'same source incident id deduplicates despite changed post history');
  assert.equal(first.exportPending().records[0].report.incident_id, 'session-stable:incident-1');
  assert.equal(first.enqueue({...source, incidents: [{...source.incidents[0], id: undefined}]}).accepted, false,
    'missing native incident id is rejected');
  first.setActive(false);
  assert.equal(first.getStatus().eligible, true);
  await first.persist();
  first.dispose();

  const second = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: shared, now: () => 11, random: () => 0.21});
  const third = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: shared, now: () => 12, random: () => 0.21});
  second.enqueue(reportFor({session_id: 'session-taba'}));
  third.enqueue(reportFor({session_id: 'session-tabb'}));
  await second.persist();
  await third.persist();
  assert.equal(shared.state.values.length, 3, 'multi-tab persistence merges bounded pending records');
  assert.equal(new Set(shared.state.values.map(item => item.report.incident_id)).size, 3);
  second.dispose(); third.dispose();
}

async function testAttemptBudgetAndInitialOptOutClear() {
  let attempts = 0;
  let now = 0;
  const failed = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { attempts += 1; return {status: 503}; }),
    origin: 'https://webmelee.gg', storage: storage(), now: () => now, retryBaseMs: 100000,
  });
  for (let index = 0; index < DELIVERY_MAX_RECORDS; index += 1) {
    assert.equal(failed.enqueue(reportFor({session_id: `session-failed${index}`})).accepted, true);
  }
  failed.setActive(false);
  for (let index = 0; index < 5; index += 1) {
    now += 100001;
    await failed.flushWhenInactive();
  }
  assert.equal(attempts, DELIVERY_MAX_SESSION_UPLOADS,
    'session upload budget bounds failed and retried request attempts');
  assert.equal(failed.getStatus().session_uploads, DELIVERY_MAX_SESSION_UPLOADS);
  failed.dispose();

  const sentStorage = storage();
  let firstVisitCalls = 0;
  const firstVisit = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { firstVisitCalls += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: sentStorage, now: () => 20,
  });
  firstVisit.enqueue(reportFor({session_id: 'session-repeat'}));
  firstVisit.setActive(false);
  await firstVisit.flushWhenInactive();
  assert.equal(firstVisitCalls, 1);
  assert.equal(sentStorage.state.tombstones.length, 1, 'sent incident tombstone is retained');
  firstVisit.dispose();
  let secondVisitCalls = 0;
  const secondVisit = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { secondVisitCalls += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: sentStorage, now: () => 21,
  });
  assert.equal(secondVisit.enqueue(reportFor({session_id: 'session-repeat'})).accepted, true);
  secondVisit.setActive(false);
  await secondVisit.flushWhenInactive();
  assert.equal(secondVisitCalls, 0, 'sent native incident is not re-uploaded on a later visit');
  assert.equal(secondVisit.exportPending().records.length, 0);
  secondVisit.dispose();

  const persisted = storage();
  const seed = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: persisted, now: () => 1});
  seed.enqueue(reportFor());
  await seed.persist();
  seed.dispose();
  const optedOut = createDiagnosticsDelivery({globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: persisted, optOut: true});
  await tick();
  assert.deepEqual(persisted.state.values, [], 'construction opt-out clears persisted unsent reports');
  optedOut.dispose();
}

async function testOptOutClearSerializesReenabledWrites() {
  const state = {
    values: [],
    tombstones: [],
    clearStarted: false,
    clearReleased: false,
    releaseClear: null,
    saveLog: [],
  };
  const adapter = {
    async load() { return structuredClone(state.values); },
    async save(values) {
      const next = structuredClone(values);
      if (!next.length && !state.clearReleased) {
        state.clearStarted = true;
        state.saveLog.push('clear-start');
        await new Promise(resolve => { state.releaseClear = resolve; });
        state.clearReleased = true;
      }
      state.saveLog.push(next.length ? 'write' : 'clear-complete');
      state.values = next;
    },
    async loadTombstones() { return structuredClone(state.tombstones); },
    async saveTombstones(values) { state.tombstones = structuredClone(values); },
  };
  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: adapter, now: () => 100, random: deterministicRandom(),
  });
  assert.equal(delivery.enqueue(reportFor({session_id: 'session-beforeoptout'})).accepted, true);
  await delivery.persist();
  assert.deepEqual(state.saveLog, ['write']);

  delivery.setOptOut(true);
  await tick();
  assert.equal(state.clearStarted, true, 'opt-out starts its asynchronous clear task');
  delivery.setOptOut(false);
  assert.equal(delivery.enqueue(reportFor({session_id: 'session-afteroptout'})).accepted, true);
  const pendingPersist = delivery.persist();
  await tick();
  assert.deepEqual(state.saveLog, ['write', 'clear-start'],
    'reenabled persistence waits instead of writing while opt-out clear is pending');

  state.releaseClear();
  const result = await pendingPersist;
  assert.equal(result.persisted, true);
  assert.deepEqual(state.saveLog, ['write', 'clear-start', 'clear-complete', 'write']);
  assert.equal(state.values.length, 1);
  assert.equal(state.values[0].report.session_id, 'session-afteroptout');
  delivery.dispose();

  const inFlight = {
    values: [],
    tombstones: [],
    oldStarted: false,
    oldReleased: false,
    clearStarted: false,
    clearReleased: false,
    releaseOld: null,
    releaseClear: null,
    saveLog: [],
  };
  const inFlightAdapter = {
    async load() { return structuredClone(inFlight.values); },
    async save(values) {
      const next = structuredClone(values);
      if (next.length && !inFlight.oldReleased) {
        inFlight.oldStarted = true;
        inFlight.saveLog.push('old-start');
        await new Promise(resolve => { inFlight.releaseOld = resolve; });
        inFlight.oldReleased = true;
      }
      if (!next.length && !inFlight.clearReleased) {
        inFlight.clearStarted = true;
        inFlight.saveLog.push('clear-start');
        await new Promise(resolve => { inFlight.releaseClear = resolve; });
        inFlight.clearReleased = true;
      }
      inFlight.saveLog.push(next.length ? 'write' : 'clear-complete');
      inFlight.values = next;
    },
    async loadTombstones() { return structuredClone(inFlight.tombstones); },
    async saveTombstones(values) { inFlight.tombstones = structuredClone(values); },
  };
  const concurrent = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: inFlightAdapter, now: () => 100, random: deterministicRandom(),
  });
  assert.equal(concurrent.enqueue(reportFor({session_id: 'session-oldwrite'})).accepted, true);
  const oldPersist = concurrent.persist();
  await tick();
  assert.equal(inFlight.oldStarted, true);
  concurrent.setOptOut(true);
  concurrent.setOptOut(false);
  assert.equal(concurrent.enqueue(reportFor({session_id: 'session-newwrite'})).accepted, true);
  const newPersist = concurrent.persist();
  await tick();
  assert.deepEqual(inFlight.saveLog, ['old-start'], 'new persistence waits for the prior write');
  inFlight.releaseOld();
  await tick();
  assert.deepEqual(inFlight.saveLog, ['old-start', 'write', 'clear-start'],
    'opt-out clear waits for the write that was already in flight');
  inFlight.releaseClear();
  await Promise.all([oldPersist, newPersist]);
  assert.deepEqual(inFlight.saveLog, ['old-start', 'write', 'clear-start', 'clear-complete', 'write']);
  assert.equal(inFlight.values.length, 1);
  assert.equal(inFlight.values[0].report.session_id, 'session-newwrite');
  assert.equal(concurrent.getStatus().queued, 1, 'fresh queue remains dirty after the clear');
  concurrent.dispose();
}

async function testInitialOptOutClearSerializesReenabledWrites() {
  const state = {
    values: [], tombstones: [], clearStarted: false, clearReleased: false,
    releaseClear: null, saveLog: [],
  };
  const adapter = {
    async load() { return structuredClone(state.values); },
    async save(values) {
      const next = structuredClone(values);
      if (!next.length && !state.clearReleased) {
        state.clearStarted = true;
        state.saveLog.push('clear-start');
        await new Promise(resolve => { state.releaseClear = resolve; });
        state.clearReleased = true;
      }
      state.saveLog.push(next.length ? 'write' : 'clear-complete');
      state.values = next;
    },
    async loadTombstones() { return structuredClone(state.tombstones); },
    async saveTombstones(values) { state.tombstones = structuredClone(values); },
  };

  // Seed the adapter with work from the prior visit. Construction opt-out must
  // remove it even when the user enables reporting again before the deferred
  // cleanup callback gets a turn.
  const seed = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: adapter, now: () => 10,
  });
  assert.equal(seed.enqueue(reportFor({session_id: 'session-initialold'})).accepted, true);
  await seed.persist();
  seed.dispose();
  state.saveLog.length = 0;

  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: adapter, now: () => 20,
    optOut: true,
  });
  delivery.setOptOut(false);
  assert.equal(delivery.enqueue(reportFor({session_id: 'session-initialfresh'})).accepted, true);
  const pendingPersist = delivery.persist();
  await tick();
  assert.deepEqual(state.saveLog, ['clear-start'],
    're-enabling before the constructor cleanup still starts the old-work clear first');
  state.releaseClear();
  const result = await pendingPersist;
  assert.equal(result.persisted, true);
  assert.deepEqual(state.saveLog, ['clear-start', 'clear-complete', 'write']);
  assert.deepEqual(state.values.map(value => value.report.session_id), ['session-initialfresh']);

  let requests = 0;
  const later = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { requests += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: adapter, now: () => 21,
  });
  later.setActive(false);
  const flushed = await later.flushWhenInactive();
  assert.equal(flushed.sent, 1, 'fresh work survives the initial opt-out cleanup for a later visit');
  assert.equal(requests, 1);
  later.dispose();
  delivery.dispose();
}

async function testPersistenceSettlementAfterSyncStorageFailure() {
  const failingStorage = {
    load() { return []; },
    save() { throw Object.assign(new Error('denied'), {name: 'NotAllowedError'}); },
    loadTombstones() { return []; },
    saveTombstones() {},
  };
  const failing = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: failingStorage, now: () => 100,
  });
  await assert.doesNotReject(() => failing.persist());
  await assert.doesNotReject(() => failing.persist());
  assert.equal(failing.enqueue(reportFor({session_id: 'session-syncfresh'})).accepted, true);
  const thirdPersist = failing.persist();
  const settled = await Promise.race([
    thirdPersist,
    new Promise(resolve => setTimeout(() => resolve('timeout'), 100)),
  ]);
  assert.notEqual(settled, 'timeout', 'sync storage failures must not leave a stale persistence promise');
  assert.equal(settled.persisted, false);
  failing.dispose();

  const recoveredState = {values: [], clears: 0};
  const recoveredStorage = {
    load() { return recoveredState.values; },
    save(values) {
      if (!values.length) {
        recoveredState.clears += 1;
        if (recoveredState.clears === 1) throw Object.assign(new Error('denied'), {name: 'NotAllowedError'});
      }
      recoveredState.values = structuredClone(values);
    },
    loadTombstones() { return []; },
    saveTombstones() {},
  };
  const recovered = createDiagnosticsDelivery({
    globalThis: rootFor(async () => ({status: 201})), origin: 'https://webmelee.gg',
    storage: recoveredStorage, now: () => 100,
  });
  recovered.setOptOut(true);
  await tick();
  assert.equal(recoveredState.clears, 1);
  recovered.setOptOut(false);
  recovered.setOptOut(true);
  await tick();
  assert.equal(recoveredState.clears, 2, 'a later opt-out retries after a synchronous clear failure');
  assert.deepEqual(recoveredState.values, [], 'the recovered clear removes persisted work');
  recovered.dispose();
}

async function testFlushMutationSurvivesPendingWrite() {
  const state = {
    values: [],
    tombstones: [],
    saveStarted: false,
    releaseSave: null,
    outboxSaves: [],
    tombstoneSaves: [],
  };
  const adapter = {
    async load() { return structuredClone(state.values); },
    async save(values) {
      const next = structuredClone(values);
      if (next.length && !state.saveStarted) {
        state.saveStarted = true;
        await new Promise(resolve => { state.releaseSave = resolve; });
      }
      state.outboxSaves.push(next.length);
      state.values = next;
    },
    async loadTombstones() { return structuredClone(state.tombstones); },
    async saveTombstones(values) {
      const next = structuredClone(values);
      state.tombstoneSaves.push(next.length);
      state.tombstones = next;
    },
  };
  let requests = 0;
  const delivery = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { requests += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: adapter, now: () => 100,
    random: deterministicRandom(),
  });
  assert.equal(delivery.enqueue(reportFor({session_id: 'session-reducer'})).accepted, true);
  const pendingWrite = delivery.persist();
  await tick();
  await tick();
  assert.equal(state.saveStarted, true);

  delivery.setActive(false);
  const flush = delivery.flushWhenInactive();
  await tick();
  assert.equal(requests, 1, 'flush can send while an older persistence write is deferred');
  state.releaseSave();
  const [persisted, flushed] = await Promise.all([pendingWrite, flush]);
  assert.equal(persisted.persisted, true);
  assert.equal(flushed.sent, 1);
  assert.deepEqual(state.values, [], 'the post-send outbox mutation is persisted');
  assert.equal(state.tombstones.length, 1);
  assert.equal(state.tombstoneSaves.at(-1), 1);

  let laterRequests = 0;
  const freshVisit = createDiagnosticsDelivery({
    globalThis: rootFor(async () => { laterRequests += 1; return {status: 201}; }),
    origin: 'https://webmelee.gg', storage: adapter, now: () => 101,
  });
  assert.equal(freshVisit.enqueue(reportFor({session_id: 'session-reducer'})).accepted, true);
  freshVisit.setActive(false);
  const later = await freshVisit.flushWhenInactive();
  assert.equal(later.sent, 0, 'fresh visit honors the persisted sent tombstone');
  assert.equal(laterRequests, 0, 'a sent native incident is not retransmitted');
  freshVisit.dispose();
  delivery.dispose();
}

async function testTerminalWorkCannotResurrectFromPersistedMerge() {
  for (const status of [422, 503]) {
    let at = 0, calls = 0;
    const adapter = {values: [], terminal: [],
      async load() { return structuredClone(this.values); },
      async save(values) { this.values = structuredClone(values); },
      async loadTombstones() { return structuredClone(this.terminal); },
      async saveTombstones(values) { this.terminal = structuredClone(values); }};
    const make = () => createDiagnosticsDelivery({globalThis: rootFor(async () => {calls++; return {status};}),
      origin: 'https://webmelee.gg', storage: adapter, now: () => at, random: () => 0.5,
      retryBaseMs: 100000});
    const first = make(); first.enqueue(reportFor({session_id: 'session-terminal'})); first.setActive(false);
    await first.flushWhenInactive();
    if (status === 503) {
      at = 100000; await first.flushWhenInactive();
      at = 300000; await first.flushWhenInactive();
    }
    assert.equal(calls, status === 503 ? 3 : 1);
    assert.equal(first.getStatus().queued, 0, 'terminal row is removed after storage refresh');
    assert.deepEqual(adapter.values, []);
    assert.equal(adapter.terminal.length, 1);
    first.dispose();
    const second = make(); second.enqueue(reportFor({session_id: 'session-terminal'})); second.setActive(false);
    await second.flushWhenInactive();
    assert.equal(calls, status === 503 ? 3 : 1, 'later visit also honors terminal work');
    assert.equal(second.getStatus().queued, 0);
    second.dispose();
  }
}

async function testRetainedCaptureCannotRestartExpiry() {
  let at = DELIVERY_RETENTION_MS + 100;
  const adapter = storage();
  const requests = [];
  const delivery = createDiagnosticsDelivery({globalThis: rootFor(async () => {
    requests.push(1); return {status: 201};
  }), origin: 'https://webmelee.gg', storage: adapter, now: () => at});
  const expired = reportFor({session_id: 'session-oldretained'});
  expired.incidents[0].captured_at_ms = 100;
  assert.equal(delivery.enqueue(expired).reason, 'expired',
    'Local retention must not restart the seven-day delivery lifetime on a later visit');
  const recent = reportFor({session_id: 'session-recentretained'});
  recent.incidents[0].captured_at_ms = 101;
  assert.equal(delivery.enqueue(recent).accepted, true);
  delivery.setActive(false);
  await delivery.persist();
  assert.equal(adapter.state.values[0].expires_at, DELIVERY_RETENTION_MS + 101);
  at++;
  await delivery.flushWhenInactive();
  assert.equal(requests.length, 0, 'Expiry uses the original capture time');
  assert.equal(delivery.getStatus().queued, 0);
  delivery.dispose();
}

function testFallbackIdentifiersUseRandomBytes() {
  const root = rootFor(async () => ({status: 201}));
  root.crypto = {getRandomValues() { throw new Error('unavailable'); }};
  const delivery = createDiagnosticsDelivery({globalThis: root,
    origin: 'https://webmelee.gg', storage: storage(), random: () => 0.5});
  assert.equal(delivery.enqueue(reportFor()).accepted, true);
  const record = delivery.exportPending().records[0];
  assert.equal(record.id, '80'.repeat(16), 'Random fallback is converted to bytes before rounding');
  assert.match(record.report.session_id, /^session-(?:80){16}[a-z0-9]+$/);
  delivery.dispose();
}

testFallbackIdentifiersUseRandomBytes();
await testRetainedCaptureCannotRestartExpiry();
await testTerminalWorkCannotResurrectFromPersistedMerge();
await testKnownHostsAndAllowlist();
await testBoundsDuplicatesAndMalformedStorage();
await testOfflineRetryExpiryAndOptOut();
await testCallbackAndPersistenceFailure();
await testResumeCancellationAndSessionBudget();
await testIncidentIdentityStatusAndMultiTabMerge();
await testAttemptBudgetAndInitialOptOutClear();
await testOptOutClearSerializesReenabledWrites();
await testInitialOptOutClearSerializesReenabledWrites();
await testPersistenceSettlementAfterSyncStorageFailure();
await testFlushMutationSurvivesPendingWrite();
console.log('Runtime diagnostics delivery bounds, privacy, scheduling, persistence and environment checks passed');
