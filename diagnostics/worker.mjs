import {
  DIAGNOSTICS_SCHEMA,
  DIAGNOSTICS_VERSION,
  ENVIRONMENTS,
  MAX_REPORT_BYTES,
  REASONS,
  canonicalizeReport,
  normalizeDiagnosticReport,
  parseStrictJson,
} from './schema.mjs';

export const RETENTION_MS = 30 * 24 * 60 * 60 * 1000;
export const RATE_WINDOW_MS = 60 * 1000;
export const DEFAULT_RATE_LIMIT = 60;
export const DAY_MS = 24 * 60 * 60 * 1000;
export const DEFAULT_DAILY_REPORT_CAP = 1000;
export const DEFAULT_DAILY_BYTE_CAP = 16 * 1024 * 1024;
export const MAX_RETAINED_REPORTS = 10000;
export const MAX_RETAINED_BYTES = 512 * 1024 * 1024;
export const MAX_ADMIN_LIMIT = 100;
export const MAX_PURGE_LIMIT = 1000;
export const BODY_READ_DEADLINE_MS = 5000;

const PRODUCTION_HOSTS = new Set(['webmelee.gg', 'www.webmelee.gg', 'webmelee.pages.dev']);
const STAGING_HOSTS = new Set(['staging.webmelee.gg', 'webmelee-staging.pages.dev']);
const PRODUCTION_PAGES_HOST = /^[0-9a-f]{8}\.webmelee\.pages\.dev$/;
const STAGING_PAGES_HOST = /^[0-9a-f]{8}\.webmelee-staging\.pages\.dev$/;
const REPORT_ID_RE = /^[a-f0-9]{64}$/;
const COMMIT_RE = /^[a-f0-9]{40}$/;
const RUNTIME_HASH_RE = /^[a-f0-9]{16}$/;
const PROFILE_RE = /^(?:player|audio-preview|audio-player)$/;

class BodyTooLargeError extends Error {}
class BodyReadTimeoutError extends Error {}

function jsonResponse(value, status = 200, extraHeaders = {}) {
  const headers = new Headers({
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store',
    'x-content-type-options': 'nosniff',
    'referrer-policy': 'no-referrer',
    'permissions-policy': 'camera=(), microphone=(), geolocation=()',
    ...extraHeaders,
  });
  return new Response(JSON.stringify(value), { status, headers });
}

function errorResponse(error, status) {
  return jsonResponse({ error }, status);
}

function databaseFrom(env) {
  return env?.DIAGNOSTICS_DB ?? null;
}

function configuredRateLimit(env) {
  if (env?.DIAGNOSTICS_RATE_LIMIT === undefined) return DEFAULT_RATE_LIMIT;
  const value = Number(env.DIAGNOSTICS_RATE_LIMIT);
  if (!Number.isInteger(value) || value < 1 || value > DEFAULT_RATE_LIMIT) return DEFAULT_RATE_LIMIT;
  return value;
}

function boundedConfigInt(env, name, fallback, minimum, maximum) {
  if (env?.[name] === undefined) return fallback;
  const value = Number(env[name]);
  if (!Number.isInteger(value) || value < minimum || value > maximum) return fallback;
  return value;
}

function configuredDailyReportCap(env) {
  return boundedConfigInt(env, 'DIAGNOSTICS_DAILY_REPORT_CAP', DEFAULT_DAILY_REPORT_CAP, 1, DEFAULT_DAILY_REPORT_CAP);
}

function configuredDailyByteCap(env) {
  return boundedConfigInt(env, 'DIAGNOSTICS_DAILY_BYTE_CAP', DEFAULT_DAILY_BYTE_CAP, MAX_REPORT_BYTES, DEFAULT_DAILY_BYTE_CAP);
}

function requestHost(request) {
  const url = new URL(request.url);
  if (url.protocol !== 'https:' || url.port) return null;
  const headerHost = request.headers.get('host');
  if (headerHost && headerHost !== url.hostname) return null;
  return url.hostname;
}

export function environmentForHost(host) {
  if (!host) return null;
  if (PRODUCTION_HOSTS.has(host) || PRODUCTION_PAGES_HOST.test(host)) return 'production';
  if (STAGING_HOSTS.has(host) || STAGING_PAGES_HOST.test(host)) return 'staging';
  return null;
}

function identityKey(identity) {
  return `${identity.source_commit}:${identity.runtime_hash}:${identity.build_profile}`;
}

function configuredReleases(env) {
  const raw = env?.DIAGNOSTICS_ALLOWED_RELEASES;
  if (raw === undefined || raw === null || raw === '') return new Map();
  let parsed;
  try {
    parsed = typeof raw === 'string' ? parseStrictJson(raw) : raw;
  } catch (_) {
    return new Map();
  }
  const entries = [];
  if (Array.isArray(parsed)) {
    entries.push(...parsed);
  } else if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
    for (const environment of ENVIRONMENTS) {
      const releases = parsed[environment];
      if (!Array.isArray(releases)) continue;
      for (const release of releases) entries.push({ ...release, env: environment });
    }
  }
  const result = new Map(ENVIRONMENTS.map(environment => [environment, new Set()]));
  for (const release of entries) {
    if (!release || typeof release !== 'object' || Array.isArray(release)) continue;
    const environment = release.env ?? release.environment;
    const sourceCommit = release.source_commit;
    const runtimeHash = release.runtime_hash;
    const buildProfile = release.build_profile;
    if (!ENVIRONMENTS.includes(environment) || typeof sourceCommit !== 'string' || !COMMIT_RE.test(sourceCommit) || typeof runtimeHash !== 'string' || !RUNTIME_HASH_RE.test(runtimeHash) || typeof buildProfile !== 'string' || !PROFILE_RE.test(buildProfile)) continue;
    result.get(environment).add(`${sourceCommit}:${runtimeHash}:${buildProfile}`);
  }
  return result;
}

function releaseIsAllowed(identity, environment, env) {
  return configuredReleases(env).get(environment)?.has(identityKey(identity)) ?? false;
}

async function sha256Hex(text) {
  const bytes = new TextEncoder().encode(text);
  const digest = await crypto.subtle.digest('SHA-256', bytes);
  return [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('');
}

async function boundedBodyText(request) {
  const contentLength = request.headers.get('content-length');
  if (contentLength !== null) {
    if (!/^\d+$/.test(contentLength) || Number(contentLength) > MAX_REPORT_BYTES) throw new BodyTooLargeError();
  }
  if (!request.body) return '';
  const reader = request.body.getReader();
  const deadline = performance.now() + BODY_READ_DEADLINE_MS;
  const chunks = [];
  let total = 0;
  try {
    while (true) {
      const remaining = deadline - performance.now();
      if (remaining <= 0) throw new BodyReadTimeoutError();
      let timer;
      const next = await Promise.race([
        reader.read(),
        new Promise((_, reject) => {
          timer = setTimeout(() => reject(new BodyReadTimeoutError()), remaining);
        }),
      ]).finally(() => clearTimeout(timer));
      if (next.done) break;
      const chunk = next.value instanceof Uint8Array ? next.value : new Uint8Array(next.value);
      total += chunk.byteLength;
      if (total > MAX_REPORT_BYTES) throw new BodyTooLargeError();
      chunks.push(chunk);
    }
  } finally {
    if (typeof reader.cancel === 'function') void reader.cancel().catch(() => {});
    reader.releaseLock?.();
  }
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  try {
    return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
  } catch (_) {
    throw new TypeError('invalid_utf8');
  }
}

function requireDatabase(env) {
  const db = databaseFrom(env);
  if (!db || typeof db.prepare !== 'function') throw new Error('database_unavailable');
  return db;
}

function prepare(db, sql, ...values) {
  return db.prepare(sql).bind(...values);
}

async function purgeExpired(db, now, limit = MAX_PURGE_LIMIT) {
  const boundedLimit = Math.max(1, Math.min(MAX_PURGE_LIMIT, Math.trunc(limit)));
  const reports = prepare(db, `
    DELETE FROM diagnostic_reports
    WHERE report_id IN (
      SELECT report_id FROM diagnostic_reports
      WHERE expires_at <= ?
      ORDER BY expires_at ASC
      LIMIT ?
    )
  `, now, boundedLimit);
  const usage = prepare(db, `
    DELETE FROM diagnostic_daily_usage WHERE day_start < ?
  `, now - RETENTION_MS);
  if (typeof db.batch === 'function') {
    const results = await db.batch([reports, usage]);
    const result = results?.[0];
    return Number(result?.meta?.changes ?? result?.changes ?? 0);
  }
  const result = await reports.run();
  await usage.run();
  return Number(result?.meta?.changes ?? result?.changes ?? 0);
}

// A separate Worker Cron Trigger may call this bounded maintenance entrypoint.
// Pages Functions only expose request handlers; public traffic keeps the lazy
// purge as a fallback when that scheduled Worker is not configured.
export async function purgeExpiredReports(env, now = Date.now(), limit = MAX_PURGE_LIMIT) {
  const db = requireDatabase(env);
  const boundedLimit = Math.max(1, Math.min(MAX_PURGE_LIMIT, Math.trunc(limit)));
  const result = await prepare(db, `
    DELETE FROM diagnostic_reports
    WHERE report_id IN (
      SELECT report_id FROM diagnostic_reports
      WHERE expires_at <= ?
      ORDER BY expires_at ASC
      LIMIT ?
    )
  `, now, boundedLimit).run();
  await prepare(db, 'DELETE FROM diagnostic_daily_usage WHERE day_start < ?', now - RETENTION_MS).run();
  return Number(result?.meta?.changes ?? result?.changes ?? 0);
}

async function takeRateSlot(db, environment, now, env) {
  const bucketKey = `environment:${environment}`;
  const windowStart = Math.floor(now / RATE_WINDOW_MS) * RATE_WINDOW_MS;
  const limit = configuredRateLimit(env);
  const upsert = prepare(db, `
    INSERT INTO diagnostic_rate_buckets (bucket_key, window_start, count)
    VALUES (?, ?, 1)
    ON CONFLICT(bucket_key) DO UPDATE SET
      window_start = excluded.window_start,
      count = CASE
        WHEN diagnostic_rate_buckets.window_start = excluded.window_start
        THEN diagnostic_rate_buckets.count + 1
        ELSE 1
      END
  `, bucketKey, windowStart);
  const read = prepare(db, 'SELECT window_start, count FROM diagnostic_rate_buckets WHERE bucket_key = ?', bucketKey);
  const results = typeof db.batch === 'function'
    ? await db.batch([upsert, read])
    : [await upsert.run(), await read.all()];
  const row = results?.[1]?.results?.[0] ?? results?.[1]?.[0] ?? null;
  if (!row) throw new Error('rate_limit_unavailable');
  return {
    allowed: Number(row.count) <= limit,
    retryAfter: Math.max(1, Math.ceil((Number(row.window_start) + RATE_WINDOW_MS - now) / 1000)),
  };
}

async function reserveDailyBudget(db, environment, now, bytes, env) {
  const dayStart = Math.floor(now / DAY_MS) * DAY_MS;
  const reportCap = configuredDailyReportCap(env);
  const byteCap = configuredDailyByteCap(env);
  const statement = prepare(db, `
    INSERT INTO diagnostic_daily_usage (environment, day_start, report_count, byte_count)
    VALUES (?, ?, 1, ?)
    ON CONFLICT(environment, day_start) DO UPDATE SET
      report_count = diagnostic_daily_usage.report_count + 1,
      byte_count = diagnostic_daily_usage.byte_count + ?
    WHERE diagnostic_daily_usage.report_count < ?
      AND diagnostic_daily_usage.byte_count + ? <= ?
  `, environment, dayStart, bytes, bytes, reportCap, bytes, byteCap);
  const result = await statement.run();
  const changes = Number(result?.meta?.changes ?? result?.changes ?? 0);
  return changes === 1 ? { dayStart, bytes } : null;
}

async function releaseDailyBudget(db, environment, reservation) {
  if (!reservation) return;
  await prepare(db, `
    UPDATE diagnostic_daily_usage
    SET report_count = MAX(0, report_count - 1),
        byte_count = MAX(0, byte_count - ?)
    WHERE environment = ? AND day_start = ?
  `, reservation.bytes, environment, reservation.dayStart).run();
}

function exactOrigin(request, report) {
  const requestOrigin = new URL(request.url).origin;
  return request.headers.get('origin') === requestOrigin && report.environment.origin === requestOrigin;
}

function authTokenMatches(request, env) {
  const configured = env?.DIAGNOSTICS_ADMIN_TOKEN;
  const authorization = request.headers.get('authorization');
  if (typeof configured !== 'string' || configured.length < 16 || typeof authorization !== 'string') return false;
  if (!authorization.startsWith('Bearer ') || authorization.length !== 7 + configured.length) return false;
  const supplied = authorization.slice(7);
  let difference = 0;
  for (let index = 0; index < configured.length; index += 1) difference |= configured.charCodeAt(index) ^ supplied.charCodeAt(index);
  return difference === 0;
}

function parseEpoch(value, path) {
  if (value === null || value === undefined || !/^\d{1,16}$/.test(value)) throw new Error(`invalid_${path}`);
  const number = Number(value);
  if (!Number.isSafeInteger(number) || number < 0) throw new Error(`invalid_${path}`);
  return number;
}

function parseAdminFilters(url) {
  const environment = url.searchParams.get('environment');
  if (environment !== null && !ENVIRONMENTS.includes(environment)) throw new Error('invalid_environment');
  const reason = url.searchParams.get('reason');
  if (reason !== null && !REASONS.includes(reason)) throw new Error('invalid_reason');
  const sourceCommit = url.searchParams.get('source_commit');
  if (sourceCommit !== null && !COMMIT_RE.test(sourceCommit)) throw new Error('invalid_source_commit');
  const runtimeHash = url.searchParams.get('runtime_hash');
  if (runtimeHash !== null && !RUNTIME_HASH_RE.test(runtimeHash)) throw new Error('invalid_runtime_hash');
  const build = url.searchParams.get('build');
  if (build !== null && !(COMMIT_RE.test(build) || RUNTIME_HASH_RE.test(build) || PROFILE_RE.test(build))) throw new Error('invalid_build');
  const from = url.searchParams.has('from') ? parseEpoch(url.searchParams.get('from'), 'from') : null;
  const to = url.searchParams.has('to') ? parseEpoch(url.searchParams.get('to'), 'to') : null;
  if (from !== null && to !== null && from > to) throw new Error('invalid_time_range');
  const cursor = url.searchParams.get('cursor');
  if (cursor !== null && !/^\d{1,16}\.[a-f0-9]{64}$/.test(cursor)) throw new Error('invalid_cursor');
  const limitText = url.searchParams.get('limit') ?? '100';
  if (!/^\d{1,3}$/.test(limitText)) throw new Error('invalid_limit');
  const limit = Number(limitText);
  if (!Number.isInteger(limit) || limit < 1 || limit > MAX_ADMIN_LIMIT) throw new Error('invalid_limit');
  const parsedCursor = cursor ? cursor.match(/^(\d{1,16})\.([a-f0-9]{64})$/) : null;
  if (parsedCursor) parseEpoch(parsedCursor[1], 'cursor');
  return {
    environment,
    reason,
    sourceCommit,
    runtimeHash,
    build,
    from,
    to,
    limit,
    cursor: parsedCursor ? { receivedAt: Number(parsedCursor[1]), reportId: parsedCursor[2] } : null,
  };
}

function reportResponse(row) {
  let report;
  try {
    report = parseStrictJson(row.canonical_json);
  } catch (_) {
    report = null;
  }
  return {
    report_id: row.report_id,
    received_at: Number(row.received_at),
    expires_at: Number(row.expires_at),
    bytes: Number(row.bytes),
    report,
  };
}

async function queryReports(db, filters, now) {
  const predicates = [];
  const values = [now];
  // Expiry is an availability boundary, not a deletion boundary. A scheduled
  // purge can be delayed or batched, so expired rows must never be returned
  // while their physical deletion catches up.
  predicates.push('expires_at > ?');
  if (filters.environment) {
    predicates.push('environment = ?');
    values.push(filters.environment);
  }
  if (filters.reason) {
    predicates.push('reason = ?');
    values.push(filters.reason);
  }
  if (filters.sourceCommit) {
    predicates.push('source_commit = ?');
    values.push(filters.sourceCommit);
  }
  if (filters.runtimeHash) {
    predicates.push('runtime_hash = ?');
    values.push(filters.runtimeHash);
  }
  if (filters.build) {
    predicates.push('(source_commit = ? OR runtime_hash = ? OR build_profile = ?)');
    values.push(filters.build, filters.build, filters.build);
  }
  if (filters.from !== null) {
    predicates.push('received_at >= ?');
    values.push(filters.from);
  }
  if (filters.to !== null) {
    predicates.push('received_at <= ?');
    values.push(filters.to);
  }
  if (filters.cursor) {
    predicates.push('(received_at < ? OR (received_at = ? AND report_id < ?))');
    values.push(filters.cursor.receivedAt, filters.cursor.receivedAt, filters.cursor.reportId);
  }
  const where = predicates.length ? `WHERE ${predicates.join(' AND ')}` : '';
  const rows = await prepare(db, `
    SELECT report_id, received_at, expires_at, bytes, canonical_json
    FROM diagnostic_reports
    ${where}
    ORDER BY received_at DESC, report_id DESC
    LIMIT ?
  `, ...values, filters.limit).all();
  const reports = (rows?.results ?? rows ?? []).map(reportResponse);
  const next = reports.length === filters.limit ? reports[reports.length - 1] : null;
  return {
    reports,
    next_cursor: next ? `${next.received_at}.${next.report_id}` : null,
  };
}

async function getReport(db, reportId, now) {
  const row = await prepare(db, `
    SELECT report_id, received_at, expires_at, bytes, canonical_json
    FROM diagnostic_reports WHERE report_id = ? AND expires_at > ?
  `, reportId, now).first();
  return row ? reportResponse(row) : null;
}

async function deleteReport(db, reportId) {
  const result = await prepare(db, 'DELETE FROM diagnostic_reports WHERE report_id = ?', reportId).run();
  return Number(result?.meta?.changes ?? result?.changes ?? 0);
}

async function deleteBefore(db, before, limit) {
  const result = await prepare(db, `
    DELETE FROM diagnostic_reports
    WHERE report_id IN (
      SELECT report_id FROM diagnostic_reports
      WHERE received_at <= ?
      ORDER BY received_at ASC
      LIMIT ?
    )
  `, before, limit).run();
  return Number(result?.meta?.changes ?? result?.changes ?? 0);
}

async function handlePublicPost(request, env, hostEnvironment) {
  const db = requireDatabase(env);
  let parsed;
  try {
    const text = await boundedBodyText(request);
    parsed = parseStrictJson(text);
  } catch (error) {
    if (error instanceof BodyTooLargeError) return errorResponse('body_too_large', 413);
    if (error instanceof BodyReadTimeoutError) return errorResponse('body_timeout', 408);
    if (error instanceof TypeError && error.message === 'invalid_utf8') return errorResponse('invalid_utf8', 400);
    return errorResponse('invalid_json', 400);
  }
  let report;
  try {
    report = normalizeDiagnosticReport(parsed);
  } catch (_) {
    return errorResponse('invalid_report', 422);
  }
  if (report.environment.env !== hostEnvironment || !exactOrigin(request, report)) return errorResponse('origin_or_environment_mismatch', 403);
  if (!releaseIsAllowed(report.identity, hostEnvironment, env)) return errorResponse('unknown_release', 422);
  // Malformed, mismatched and unknown-release packets must not consume the
  // shared valid-report quota. The body size and total read deadline still bound
  // validation before this transactional per-environment limit.
  const now = Date.now();
  const rate = await takeRateSlot(db, hostEnvironment, now, env);
  if (!rate.allowed) return jsonResponse({ error: 'rate_limited' }, 429, { 'retry-after': String(rate.retryAfter) });
  await purgeExpired(db, now, 100);
  const canonicalJson = canonicalizeReport(report);
  const reportId = await sha256Hex(`${hostEnvironment}\n${canonicalJson}`);
  const canonicalHash = reportId;
  const expiresAt = now + RETENTION_MS;
  const reportBytes = new TextEncoder().encode(canonicalJson).byteLength;
  const existing = await prepare(db, `
    SELECT report_id, canonical_hash FROM diagnostic_reports
    WHERE environment = ? AND session_id = ? AND incident_id = ?
  `, hostEnvironment, report.session_id, report.incident_id).first();
  if (existing) {
    if (existing.canonical_hash !== canonicalHash) return errorResponse('incident_conflict', 409);
    return jsonResponse({ accepted: false, duplicate: true, report_id: existing.report_id });
  }
  const reservation = await reserveDailyBudget(db, hostEnvironment, now, reportBytes, env);
  if (!reservation) return jsonResponse({ error: 'daily_cap' }, 429, { 'retry-after': String(Math.max(1, Math.ceil((Math.floor(now / DAY_MS) * DAY_MS + DAY_MS - now) / 1000))) });
  try {
    await prepare(db, `
      INSERT INTO diagnostic_reports (
        report_id, canonical_hash, environment, origin, source_commit, runtime_hash,
        build_profile, session_id, incident_id, reason, received_at, expires_at,
        bytes, canonical_json
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    `,
    reportId,
    canonicalHash,
    hostEnvironment,
    report.environment.origin,
    report.identity.source_commit,
    report.identity.runtime_hash,
    report.identity.build_profile,
    report.session_id,
    report.incident_id,
    report.incident.reason,
    now,
    expiresAt,
    reportBytes,
    canonicalJson).run();
  } catch (error) {
    await releaseDailyBudget(db, hostEnvironment, reservation);
    if (String(error?.message ?? '').includes('retention_budget')) return errorResponse('storage_cap', 507);
    const concurrent = await prepare(db, `
      SELECT report_id, canonical_hash FROM diagnostic_reports
      WHERE environment = ? AND session_id = ? AND incident_id = ?
    `, hostEnvironment, report.session_id, report.incident_id).first();
    if (concurrent?.canonical_hash === canonicalHash) return jsonResponse({ accepted: false, duplicate: true, report_id: concurrent.report_id });
    if (concurrent) return errorResponse('incident_conflict', 409);
    throw new Error('storage_failure');
  }
  return jsonResponse({ accepted: true, duplicate: false, report_id: reportId }, 201);
}

async function handleAdmin(request, env, pathRemainder, url) {
  if (!authTokenMatches(request, env)) return errorResponse('unauthorized', 401);
  const db = requireDatabase(env);
  const now = Date.now();
  await purgeExpired(db, now, 100);
  if (request.method === 'GET') {
    if (pathRemainder) {
      if (!REPORT_ID_RE.test(pathRemainder)) return errorResponse('not_found', 404);
      const report = await getReport(db, pathRemainder, now);
      return report ? jsonResponse(report) : errorResponse('not_found', 404);
    }
    let filters;
    try {
      filters = parseAdminFilters(url);
    } catch (error) {
      return errorResponse(error.message, 400);
    }
    return jsonResponse(await queryReports(db, filters, now));
  }
  if (request.method === 'DELETE') {
    if (pathRemainder) {
      if (!REPORT_ID_RE.test(pathRemainder)) return errorResponse('not_found', 404);
      return jsonResponse({ deleted: (await deleteReport(db, pathRemainder)) > 0 });
    }
    const beforeText = url.searchParams.get('before');
    const limitText = url.searchParams.get('limit') ?? '100';
    try {
      const before = parseEpoch(beforeText, 'before');
      if (!/^\d{1,4}$/.test(limitText)) throw new Error('invalid_limit');
      const limit = Number(limitText);
      if (!Number.isInteger(limit) || limit < 1 || limit > MAX_PURGE_LIMIT) throw new Error('invalid_limit');
      return jsonResponse({ deleted: await deleteBefore(db, before, limit) });
    } catch (error) {
      return errorResponse(error.message, 400);
    }
  }
  return errorResponse('method_not_allowed', 405);
}

export async function fetch(request, env = {}, _ctx = {}) {
  const url = new URL(request.url);
  if (!url.pathname.startsWith('/api/diagnostics')) return errorResponse('not_found', 404);
  const hostEnvironment = environmentForHost(requestHost(request));
  if (!hostEnvironment) return errorResponse('unknown_host', 404);
  const suffix = url.pathname.slice('/api/diagnostics'.length);
  if (suffix !== '' && !/^\/[a-f0-9]{64}$/.test(suffix)) return errorResponse('not_found', 404);
  const pathRemainder = suffix ? suffix.slice(1) : '';
  try {
    if (request.method === 'POST' && !pathRemainder) {
      const contentType = request.headers.get('content-type')?.split(';', 1)[0].trim().toLowerCase();
      if (contentType !== 'application/json') return errorResponse('unsupported_media_type', 415);
      if (!request.headers.get('origin') || request.headers.get('origin') !== url.origin) return errorResponse('origin_required', 403);
      return await handlePublicPost(request, env, hostEnvironment);
    }
    if (request.method === 'GET' || request.method === 'DELETE') return await handleAdmin(request, env, pathRemainder, url);
    return errorResponse('method_not_allowed', 405);
  } catch (error) {
    if (error?.message === 'database_unavailable' || error?.message === 'rate_limit_unavailable' || error?.message === 'storage_failure') return errorResponse('service_unavailable', 503);
    return errorResponse('internal_error', 500);
  }
}

export default { fetch };
