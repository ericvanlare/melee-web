import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { DatabaseSync } from 'node:sqlite';
import { test } from 'node:test';

import { fetch as diagnosticsFetch } from '../worker.mjs';
import {
  DIAGNOSTICS_SCHEMA,
  DIAGNOSTICS_VERSION,
  HISTORY_COLUMNS,
  canonicalizeReport,
  parseStrictJson,
} from '../schema.mjs';
import {
  PURGE_BATCH_SIZE,
  PURGE_CRON,
  PURGE_DAILY_CAPACITY,
  PURGE_RUNS_PER_DAY,
} from '../purge-config.mjs';

const MIGRATION = readFileSync(new URL('../migrations/0001_diagnostics.sql', import.meta.url), 'utf8');
const PURGE_CONFIG = JSON.parse(readFileSync(new URL('../purge-wrangler.jsonc', import.meta.url), 'utf8'));
const SOURCE_COMMIT = '0123456789abcdef0123456789abcdef01234567';
const RUNTIME_HASH = '0123456789abcdef';
const ADMIN_TOKEN = 'local-test-admin-token-0123456789';

class D1Statement {
  constructor(statement) { this.statement = statement; this.values = []; }
  bind(...values) { this.values = values; return this; }
  run() { const result = this.statement.run(...this.values); return { meta: { changes: Number(result.changes) } }; }
  all() { return { results: this.statement.all(...this.values) }; }
  first() { return this.statement.get(...this.values) ?? null; }
}

class SqliteD1 {
  constructor() { this.database = new DatabaseSync(':memory:'); this.database.exec(MIGRATION); }
  prepare(sql) { return new D1Statement(this.database.prepare(sql)); }
  batch(statements) {
    this.database.exec('BEGIN');
    try {
      const results = statements.map(statement => /^\s*SELECT\b/i.test(statement.statement.sourceSQL) ? statement.all() : statement.run());
      this.database.exec('COMMIT');
      return results;
    } catch (error) {
      this.database.exec('ROLLBACK');
      throw error;
    }
  }
}

function baseReport(overrides = {}) {
  const report = {
    schema: DIAGNOSTICS_SCHEMA,
    version: DIAGNOSTICS_VERSION,
    session_id: 'session-aaaaaaaaaaaaaaaa',
    incident_id: 'incident-1',
    identity: { source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player' },
    environment: { env: 'staging', origin: 'https://staging.webmelee.gg' },
    client: { browser_family: 'chrome', browser_major: 140, platform: 'macos' },
    capabilities: {
      native: { available: true, observed: true, reason: null },
      audio: { available: false, observed: false, reason: 'not_observed' },
      longtask: { available: true, observed: false, reason: 'supported_no_events' },
    },
    incident: {
      reason: 'simulation_debt', reason_code: 1, value: 45.5, threshold: 33.333,
      source_frame: 120, scene: 'match', scene_code: 7, clock_owner: 'simulation', clock_owner_code: 1,
    },
    history: {
      columns: [...HISTORY_COLUMNS],
      rows: [[45.5, 120, 7, 0, 1, 1, 0, 0, 4, 10, 5, 45.5, 0, 0, 0, 0, 1, 1, 1, 16.6]],
      evicted: false, evicted_count: 0, truncated: false,
    },
    events: {
      pre: [{ type: 'lifecycle', kind: 'active', timestamp: 1000, source_frame: 119, scene: 'match', scene_code: 7, duration_ms: null, bytes: null, files: null }],
      post: [{ type: 'longtask', timestamp: 1045, duration_ms: 45.5, source_frame: 120 }],
    },
    flags: { incomplete: false, persistence_failure: false },
  };
  return deepMerge(report, overrides);
}

function deepMerge(base, overrides) {
  if (!overrides || typeof overrides !== 'object' || Array.isArray(overrides)) return overrides ?? base;
  const output = { ...base };
  for (const [key, value] of Object.entries(overrides)) {
    output[key] = value && typeof value === 'object' && !Array.isArray(value)
      ? deepMerge(base[key] ?? {}, value) : value;
  }
  return output;
}

function environment() {
  return {
    DIAGNOSTICS_DB: new SqliteD1(),
    DIAGNOSTICS_ADMIN_TOKEN: ADMIN_TOKEN,
    DIAGNOSTICS_RATE_LIMIT: '60',
    DIAGNOSTICS_ALLOWED_RELEASES: JSON.stringify({
      staging: [{ source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player' }],
      production: [],
    }),
  };
}

function requestFor(report, { method = 'POST', origin = report.environment.origin, body = JSON.stringify(report), path = '/api/diagnostics', token = null, headers = {} } = {}) {
  const requestHeaders = { 'content-type': 'application/json; charset=utf-8', origin, ...headers };
  if (token) requestHeaders.authorization = `Bearer ${token}`;
  return new Request(`https://staging.webmelee.gg${path}`, {
    method, headers: requestHeaders, body: method === 'GET' || method === 'HEAD' ? undefined : body,
  });
}

async function responseJson(response) { return { status: response.status, body: await response.json() }; }

test('strict parser rejects duplicate keys, unknown keys, and lossy dimensions', () => {
  assert.throws(() => parseStrictJson('{"schema":"x","schema":"y"}'), /duplicate object key/);
  assert.throws(() => parseStrictJson('['.repeat(17) + '0' + ']'.repeat(17)), /nesting/);
  assert.throws(() => parseStrictJson('[' + Array.from({ length: 129 }, () => '0').join(',') + ']'), /array items/);
  assert.throws(() => parseStrictJson(JSON.stringify(Object.fromEntries(Array.from({ length: 65 }, (_, index) => [`k${index}`, 0])))), /object keys/);
  const report = baseReport();
  assert.equal(typeof canonicalizeReport(report), 'string');
  assert.throws(() => canonicalizeReport({ ...report, unexpected: true }), error => error?.code === 'unknown_key');
  assert.throws(() => canonicalizeReport({ ...report, history: { ...report.history, rows: [[1]] } }), error => error?.code === 'history_width');
  assert.throws(() => canonicalizeReport({ ...report, incident: { ...report.incident, reason: 'runtime_failure', reason_code: 1 } }), error => error?.code === 'reason');
});

test('accepts a bounded report and makes the exact retry idempotent', async () => {
  const env = environment();
  const report = baseReport();
  const first = await responseJson(await diagnosticsFetch(requestFor(report), env));
  assert.equal(first.status, 201);
  assert.equal(first.body.accepted, true);
  assert.match(first.body.report_id, /^[a-f0-9]{64}$/);
  const retry = await responseJson(await diagnosticsFetch(requestFor(report), env));
  assert.equal(retry.status, 200);
  assert.deepEqual(retry.body, { accepted: false, duplicate: true, report_id: first.body.report_id });
});

test('rejects incident-key reuse with changed canonical content', async () => {
  const env = environment();
  const report = baseReport();
  assert.equal((await diagnosticsFetch(requestFor(report), env)).status, 201);
  const changed = baseReport({ incident: { reason: 'audio_debt', reason_code: 2, clock_owner: 'audio', clock_owner_code: 2 } });
  const response = await responseJson(await diagnosticsFetch(requestFor(changed), env));
  assert.equal(response.status, 409);
  assert.equal(response.body.error, 'incident_conflict');
});

test('enforces method, content type, UTF-8 size, origin, host, and release checks', async () => {
  const env = environment();
  const report = baseReport();
  const outsideApi = await diagnosticsFetch(new Request('https://staging.webmelee.gg/runtime.js', { method: 'GET' }), env);
  assert.equal(outsideApi.status, 404);
  const get = await responseJson(await diagnosticsFetch(requestFor(report, { method: 'GET', token: ADMIN_TOKEN }), env));
  assert.equal(get.status, 200);
  const wrongContentType = await responseJson(await diagnosticsFetch(requestFor(report, { headers: { 'content-type': 'text/plain' } }), env));
  assert.equal(wrongContentType.status, 415);
  const malformed = await responseJson(await diagnosticsFetch(requestFor(report, { body: '{' }), env));
  assert.equal(malformed.status, 400);
  const duplicateKeys = await responseJson(await diagnosticsFetch(requestFor(report, { body: '{"schema":"melee-web-diagnostics","schema":"melee-web-diagnostics"}' }), env));
  assert.equal(duplicateKeys.status, 400);
  const invalidUtf8 = await responseJson(await diagnosticsFetch(requestFor(report, { body: new Uint8Array([0xc3, 0x28]) }), env));
  assert.equal(invalidUtf8.status, 400);
  assert.equal(invalidUtf8.body.error, 'invalid_utf8');
  const tooLarge = await responseJson(await diagnosticsFetch(requestFor(report, { body: 'x'.repeat(65537) }), env));
  assert.equal(tooLarge.status, 413);
  const overrunStream = new ReadableStream({
    start(controller) {
      controller.enqueue(new Uint8Array(65536));
      controller.enqueue(new Uint8Array([0x7b]));
      controller.close();
    },
  });
  const capped = await diagnosticsFetch(new Request('https://staging.webmelee.gg/api/diagnostics', {
    method: 'POST', headers: { 'content-type': 'application/json', origin: 'https://staging.webmelee.gg' }, body: overrunStream, duplex: 'half',
  }), env);
  assert.equal(capped.status, 413);
  const badOrigin = await responseJson(await diagnosticsFetch(requestFor(report, { origin: 'https://evil.example' }), env));
  assert.equal(badOrigin.status, 403);
  const badEnvironment = baseReport({ environment: { env: 'production', origin: 'https://staging.webmelee.gg' } });
  const environmentMismatch = await responseJson(await diagnosticsFetch(requestFor(badEnvironment), env));
  assert.equal(environmentMismatch.status, 403);
  const badHost = await diagnosticsFetch(new Request('https://staging.webmelee.pages.dev/api/diagnostics', {
    method: 'POST', headers: { 'content-type': 'application/json', origin: 'https://staging.webmelee.pages.dev' }, body: JSON.stringify(report),
  }), env);
  assert.equal(badHost.status, 404);
  const unknownRelease = baseReport({ identity: { runtime_hash: 'fedcba9876543210' } });
  const unknown = await responseJson(await diagnosticsFetch(requestFor(unknownRelease), env));
  assert.equal(unknown.status, 422);
  assert.equal(unknown.body.error, 'unknown_release');
});

test('body deadline remains bounded when a client keeps sending fragments', async () => {
  let cancelled = false, interval, finish;
  const stream = new ReadableStream({
    start(controller) {
      interval = setInterval(() => controller.enqueue(new Uint8Array([0x20])), 100);
      finish = setTimeout(() => { clearInterval(interval); controller.close(); }, 6000);
    },
    cancel() { cancelled = true; clearInterval(interval); clearTimeout(finish); },
  });
  const response = await responseJson(await diagnosticsFetch(new Request('https://staging.webmelee.gg/api/diagnostics', {
    method: 'POST', headers: {'content-type': 'application/json', origin: 'https://staging.webmelee.gg'},
    body: stream, duplex: 'half',
  }), environment()));
  assert.equal(response.status, 408, 'Periodic fragments cannot renew the total request body deadline');
  assert.equal(response.body.error, 'body_timeout');
  assert.equal(cancelled, true);
});

test('rate caps are transactional and admin reads remain private', async () => {
  const env = environment();
  env.DIAGNOSTICS_RATE_LIMIT = '2';
  const first = baseReport();
  const second = baseReport({ session_id: 'session-cccccccccccccccc', incident_id: 'incident-2' });
  assert.equal((await diagnosticsFetch(requestFor(first), env)).status, 201);
  assert.equal((await diagnosticsFetch(requestFor(second), env)).status, 201);
  const third = baseReport({ session_id: 'session-eeeeeeeeeeeeeeee', incident_id: 'incident-3' });
  const limited = await responseJson(await diagnosticsFetch(requestFor(third), env));
  assert.equal(limited.status, 429);
  assert.match(limited.body.error, /rate_limited/);
  const unauthorized = await diagnosticsFetch(requestFor(first, { method: 'GET' }), env);
  assert.equal(unauthorized.status, 401);
  const query = await responseJson(await diagnosticsFetch(requestFor(first, {
    method: 'GET', token: ADMIN_TOKEN, body: undefined,
    path: '/api/diagnostics?environment=staging&limit=1&reason=simulation_debt&source_commit=' + SOURCE_COMMIT,
  }), env));
  assert.equal(query.status, 200);
  assert.equal(query.body.reports.length, 1);
  assert.equal(typeof query.body.next_cursor, 'string');
  const unsafeCursor = await responseJson(await diagnosticsFetch(requestFor(first, {
    method: 'GET', token: ADMIN_TOKEN, body: undefined,
    path: '/api/diagnostics?cursor=9999999999999999.' + 'a'.repeat(64),
  }), env));
  assert.equal(unsafeCursor.status, 400);
  const next = await responseJson(await diagnosticsFetch(requestFor(first, {
    method: 'GET', token: ADMIN_TOKEN, body: undefined,
    path: '/api/diagnostics?environment=staging&limit=1&cursor=' + encodeURIComponent(query.body.next_cursor),
  }), env));
  assert.equal(next.status, 200);
  assert.equal(next.body.reports.length, 1);
  const id = query.body.reports[0].report_id;
  const deleted = await responseJson(await diagnosticsFetch(requestFor(first, {
    method: 'DELETE', token: ADMIN_TOKEN, body: undefined, path: `/api/diagnostics/${id}`,
  }), env));
  assert.deepEqual(deleted, { status: 200, body: { deleted: true } });
  const purged = await responseJson(await diagnosticsFetch(requestFor(first, {
    method: 'DELETE', token: ADMIN_TOKEN, body: undefined, path: '/api/diagnostics?before=9999999999999&limit=100',
  }), env));
  assert.equal(purged.status, 200);
  assert.equal(typeof purged.body.deleted, 'number');
});

test('admin reads hide expired rows while bounded physical purge catches up', async () => {
  const env = environment();
  const insert = env.DIAGNOSTICS_DB.database.prepare(`
    INSERT INTO diagnostic_reports (
      report_id, canonical_hash, environment, origin, source_commit, runtime_hash,
      build_profile, session_id, incident_id, reason, received_at, expires_at,
      bytes, canonical_json
    ) VALUES (?, ?, 'staging', 'https://staging.webmelee.gg', ?, ?, 'player', ?, ?, 'simulation_debt', ?, 0, 2, '{}')
  `);
  for (let index = 0; index < 101; index += 1) {
    const id = index.toString(16).padStart(64, '0');
    insert.run(id, id, SOURCE_COMMIT, RUNTIME_HASH, `session-expired-${index}`, `incident-${index}`, Date.now());
  }
  const response = await responseJson(await diagnosticsFetch(requestFor(baseReport(), {
    method: 'GET', token: ADMIN_TOKEN, body: undefined,
    path: '/api/diagnostics?environment=staging&limit=100',
  }), env));
  assert.equal(response.status, 200);
  assert.deepEqual(response.body.reports, []);
  assert.equal(env.DIAGNOSTICS_DB.database.prepare('SELECT COUNT(*) AS count FROM diagnostic_reports').get().count, 1);
});

test('scheduled purge has headroom over the combined daily intake cap', () => {
  assert.equal(PURGE_CONFIG.triggers.crons[0], PURGE_CRON);
  assert.equal(PURGE_BATCH_SIZE, 100);
  assert.equal(PURGE_RUNS_PER_DAY, 48);
  assert.equal(PURGE_DAILY_CAPACITY, 4800);
  assert.ok(PURGE_DAILY_CAPACITY >= 2 * 1000);
});

test('daily report caps do not count exact idempotent retries', async () => {
  const env = environment();
  env.DIAGNOSTICS_DAILY_REPORT_CAP = '1';
  const first = baseReport();
  assert.equal((await diagnosticsFetch(requestFor(first), env)).status, 201);
  assert.equal((await diagnosticsFetch(requestFor(first), env)).status, 200);
  const second = baseReport({ session_id: 'session-cccccccccccccccc', incident_id: 'incident-2' });
  const response = await responseJson(await diagnosticsFetch(requestFor(second), env));
  assert.equal(response.status, 429);
  assert.equal(response.body.error, 'daily_cap');
});

test('production host mapping accepts www apex and only eight-hex Pages deployments', async () => {
  const env = environment();
  env.DIAGNOSTICS_ALLOWED_RELEASES = JSON.stringify({
    staging: [], production: [{ source_commit: SOURCE_COMMIT, runtime_hash: RUNTIME_HASH, build_profile: 'player' }],
  });
  const report = baseReport({ environment: { env: 'production', origin: 'https://www.webmelee.gg' } });
  const response = await diagnosticsFetch(new Request('https://www.webmelee.gg/api/diagnostics', {
    method: 'POST', headers: { 'content-type': 'application/json', origin: 'https://www.webmelee.gg' }, body: JSON.stringify(report),
  }), env);
  assert.equal(response.status, 201);
  const eightHex = baseReport({ session_id: 'session-cccccccccccccccc', incident_id: 'incident-2', environment: { env: 'production', origin: 'https://928714aa.webmelee.pages.dev' } });
  const deployment = await diagnosticsFetch(new Request(eightHex.environment.origin + '/api/diagnostics', {
    method: 'POST', headers: { 'content-type': 'application/json', origin: eightHex.environment.origin }, body: JSON.stringify(eightHex),
  }), env);
  assert.equal(deployment.status, 201);
  const sevenHex = await diagnosticsFetch(new Request('https://928714a.webmelee.pages.dev/api/diagnostics', {
    method: 'POST', headers: { 'content-type': 'application/json', origin: 'https://928714a.webmelee.pages.dev' }, body: JSON.stringify(eightHex),
  }), env);
  assert.equal(sevenHex.status, 404);
});
