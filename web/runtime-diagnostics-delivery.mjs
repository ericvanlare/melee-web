/*
 * Inactive-only delivery for the bounded browser diagnostics recorder.
 *
 * The recorder remains local and synchronous on the simulation path.  This
 * adapter maps one bounded local incident to the strict backend v1 schema,
 * queues it in a separate small outbox, and performs network work only from a
 * task scheduled while the player is inactive.
 */

import {
  DIAGNOSTICS_SCHEMA,
  DIAGNOSTICS_VERSION,
  MAX_REPORT_BYTES,
  MAX_HISTORY_ROWS,
  HISTORY_COLUMNS,
  MAX_POST_EVENTS,
  MAX_PRE_EVENTS,
  AUDIO_CONTEXT_STATES,
  CLOCK_OWNERS,
  LIFECYCLE_EVENT_NAMES,
  PLATFORMS,
  BROWSER_FAMILIES,
  REASONS,
  SCENES,
  normalizeDiagnosticReport,
} from './diagnostics-schema.mjs';

export const DELIVERY_DB_NAME = 'melee-web-diagnostics-delivery';
export const DELIVERY_STORE_NAME = 'outbox';
export const DELIVERY_TOMBSTONE_STORE_NAME = 'sent';
export const DELIVERY_MAX_RECORDS = 4;
export const DELIVERY_MAX_BYTES = 256 * 1024;
export const DELIVERY_MAX_TOMBSTONES = 4;
export const DELIVERY_RETENTION_MS = 7 * 24 * 60 * 60 * 1000;
export const DELIVERY_MAX_RETRIES = 3;
export const DELIVERY_MAX_SESSION_UPLOADS = 4;
export const DELIVERY_REQUEST_TIMEOUT_MS = 5000;

const PRODUCTION_HOSTS = new Set(['webmelee.gg', 'www.webmelee.gg', 'webmelee.pages.dev']);
const STAGING_HOSTS = new Set(['staging.webmelee.gg', 'webmelee-staging.pages.dev']);
const PRODUCTION_PAGES_HOST = /^[0-9a-f]{8}\.webmelee\.pages\.dev$/;
const STAGING_PAGES_HOST = /^[0-9a-f]{8}\.webmelee-staging\.pages\.dev$/;
const PROFILE_RE = /^(?:player|audio-preview|audio-player)$/;
const OUTBOX_ID_RE = /^[a-f0-9]{32}$/;
const EVENT_LIFECYCLE = new Set(LIFECYCLE_EVENT_NAMES);
const CAPABILITY_NAMES = Object.freeze(['native', 'audio', 'longtask']);
const SCENE_CODES = Object.freeze({unknown: 0, css: 1, preparing: 2, sss: 3, unloaded: 6, match: 7, results: 8, prize: 9, title: 10, main: 11, opening: 12, opening_vs: 13});
const REASON_CODES = Object.freeze({simulation_debt: 1, audio_debt: 2, nonfinite_clock: 3, runtime_failure: 4, manual_pause: 5, manual_resume: 6, render_preparation: 7, clock_regression: 8, scheduled_pause: 9});
const OWNER_CODES = Object.freeze({other: 0, simulation: 1, audio: 2});
let DELIVERY_SESSION_SEQUENCE = 0;

const finite = value => typeof value === 'number' && Number.isFinite(value) ? value : null;
const integer = value => finite(value) !== null && Number.isSafeInteger(value) ? value : null;

function clone(value, seen = new WeakMap()) {
  if (value === null || typeof value !== 'object') return value;
  if (seen.has(value)) return seen.get(value);
  if (Array.isArray(value)) {
    const result = [];
    seen.set(value, result);
    for (const item of value) result.push(clone(item, seen));
    return result;
  }
  const result = {};
  seen.set(value, result);
  for (const [key, item] of Object.entries(value)) result[key] = clone(item, seen);
  return result;
}

function freeze(value, seen = new WeakSet()) {
  if (value === null || typeof value !== 'object' || seen.has(value)) return value;
  seen.add(value);
  for (const item of Object.values(value)) freeze(item, seen);
  return Object.freeze(value);
}

function immutable(value) {
  return freeze(clone(value));
}

function environmentForOrigin(origin) {
  if (typeof origin !== 'string') return null;
  let parsed;
  try { parsed = new URL(origin); } catch { return null; }
  if (parsed.protocol !== 'https:' || parsed.origin !== origin || parsed.pathname !== '/' ||
      parsed.search || parsed.hash || parsed.username || parsed.password || parsed.port) return null;
  const host = parsed.hostname.toLowerCase();
  if (PRODUCTION_HOSTS.has(host) || PRODUCTION_PAGES_HOST.test(host)) {
    return {env: 'production', origin: parsed.origin, host};
  }
  if (STAGING_HOSTS.has(host) || STAGING_PAGES_HOST.test(host)) {
    return {env: 'staging', origin: parsed.origin, host};
  }
  return null;
}

function canonicalOrigin(origin) {
  if (typeof origin !== 'string') return null;
  try {
    const parsed = new URL(origin);
    if (!/^https?:$/.test(parsed.protocol) || parsed.username || parsed.password) return null;
    return parsed.origin;
  } catch {
    return null;
  }
}

function normalizeIdentity(value) {
  if (!value || typeof value !== 'object') return null;
  const sourceCommit = typeof value.source_commit === 'string' ? value.source_commit.toLowerCase() : '';
  const runtimeHash = typeof value.runtime_hash === 'string' ? value.runtime_hash.toLowerCase() : '';
  const profile = typeof value.build_profile === 'string' ? value.build_profile.toLowerCase() : '';
  if (!/^[a-f0-9]{40}$/.test(sourceCommit) || !/^[a-f0-9]{16}$/.test(runtimeHash) || !PROFILE_RE.test(profile)) return null;
  return {source_commit: sourceCommit, runtime_hash: runtimeHash, build_profile: profile};
}

function hashText(value) {
  const text = typeof value === 'string' ? value : '';
  let a = 0x811c9dc5;
  let b = 0x9e3779b9;
  for (let index = 0; index < text.length; index += 1) {
    const code = text.charCodeAt(index);
    a = Math.imul(a ^ code, 0x01000193) >>> 0;
    b = Math.imul(b ^ ((code << 1) | (code >>> 15)), 0x85ebca6b) >>> 0;
  }
  return `${a.toString(16).padStart(8, '0')}${b.toString(16).padStart(8, '0')}`;
}

function randomHex(root, random, byteCount = 16) {
  const bytes = new Uint8Array(byteCount);
  const fallback = () => {
    for (let index = 0; index < bytes.length; index += 1)
      bytes[index] = Math.floor(Math.max(0, Math.min(0.999999999,
        Number(random?.() ?? Math.random()))) * 256);
  };
  try {
    if (root.crypto?.getRandomValues) root.crypto.getRandomValues(bytes);
    else fallback();
  } catch { fallback(); }
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
}

function normalizeBrowserFamily(value) {
  const text = typeof value === 'string' ? value.toLowerCase() : '';
  if (BROWSER_FAMILIES.includes(text)) return text;
  if (text === 'opera' || text === 'operabrowser') return 'opera';
  return BROWSER_FAMILIES.includes('unknown') ? 'unknown' : BROWSER_FAMILIES[0];
}

function normalizePlatform(value) {
  const text = typeof value === 'string' ? value.toLowerCase() : '';
  if (PLATFORMS.includes(text)) return text;
  if (text === 'mac' || text === 'osx') return 'macos';
  return PLATFORMS.includes('unknown') ? 'unknown' : PLATFORMS[0];
}

function capabilityState(name, report, root, events, overrides = {}) {
  const source = report?.capabilities && typeof report.capabilities === 'object' ? report.capabilities : {};
  const explicit = overrides && typeof overrides === 'object' ? overrides[name] : undefined;
  const local = source[name];
  const available = typeof explicit?.available === 'boolean' ? explicit.available :
    typeof local?.available === 'boolean' ? local.available :
    name === 'native' ? false :
    name === 'audio' ? Boolean(source.audio_worklet || source.audio?.available) :
    name === 'longtask' ? typeof root.PerformanceObserver === 'function' : true;
  const observed = typeof explicit?.observed === 'boolean' ? explicit.observed :
    name === 'native' ? Boolean(local?.observed) :
    name === 'audio' ? events.some(event => event?.type === 'audio') :
    name === 'longtask' ? events.some(event => event?.type === 'longtask') : true;
  const reason = observed ? null : available ? (name === 'longtask' ? 'supported_no_events' : 'not_observed') : 'unavailable';
  return {available, observed, reason};
}

function localCapabilities(report, root, events, overrides = {}) {
  const allEvents = [...(events?.pre || []), ...(events?.post || [])];
  return Object.fromEntries(CAPABILITY_NAMES.map(name => [name, capabilityState(name, report, root, allEvents, overrides)]));
}

function mapScene(value) {
  const numeric = integer(value);
  const text = typeof value === 'string' ? value.toLowerCase() : '';
  const nativeName = numeric === 1 ? 'css' : numeric === 3 ? 'sss' :
    numeric === 7 ? 'match' : numeric === 8 || numeric === 9 ? (numeric === 9 ? 'prize' : 'results') :
    numeric === 2 || numeric === 4 || numeric === 5 ? 'preparing' :
    numeric === 6 ? 'unloaded' : numeric === 10 ? 'title' : numeric === 11 ? 'main' :
    numeric === 12 ? 'opening' : numeric === 13 ? 'opening_vs' : '';
  const candidate = text || nativeName;
  if (candidate === 'gameplay') return SCENES.includes('match') ? 'match' : 'unknown';
  if (candidate === 'match') return SCENES.includes('match') ? 'match' : 'unknown';
  if (candidate === 'prize') return SCENES.includes('prize') ? 'prize' : 'unknown';
  if (candidate === 'opening-vs' || candidate === 'opening_vs') return SCENES.includes('opening_vs') ? 'opening_vs' : 'unknown';
  return SCENES.includes(candidate) ? candidate : 'unknown';
}

function mapReason(value) {
  // Only the recorder's canonical reason names are deliverable.  In
  // particular, an audio underrun/overflow or a native deadline is not an
  // observed guard incident and must not be inferred as audio/simulation debt.
  return typeof value === 'string' && REASONS.includes(value) ? value : null;
}

function finiteOrNull(value) {
  return finite(value);
}

function mapHistory(incident) {
  const source = incident?.history?.rows;
  const flags = incident?.history || {};
  if (!Array.isArray(source)) return {
    columns: [...HISTORY_COLUMNS], rows: [], evicted: Boolean(flags.evicted),
    evicted_count: Number.isSafeInteger(flags.evicted_count) && flags.evicted_count >= 0 ? flags.evicted_count : 0,
    truncated: Boolean(flags.truncated),
  };
  const result = [];
  for (const row of source) {
    if (!Array.isArray(row) || result.length >= MAX_HISTORY_ROWS) continue;
    const mapped = [];
    for (const cell of row.slice(0, HISTORY_COLUMNS.length)) {
      const number = finite(cell);
      mapped.push(number);
    }
    while (mapped.length < HISTORY_COLUMNS.length) mapped.push(null);
    result.push(mapped.slice(0, HISTORY_COLUMNS.length));
  }
  return {
    columns: [...HISTORY_COLUMNS], rows: result,
    evicted: Boolean(flags.evicted),
    evicted_count: Number.isSafeInteger(flags.evicted_count) && flags.evicted_count >= 0 ? flags.evicted_count : 0,
    truncated: Boolean(flags.truncated),
  };
}

function mapEvent(event) {
  if (!event || typeof event !== 'object') return null;
  const timestamp = finite(event.timestamp ?? event.timestamp_ms);
  const sourceFrame = event.source_frame === null || event.source_frame === undefined ? null : integer(event.source_frame);
  if (timestamp !== null && timestamp < 0) return null;
  if (event.type === 'native') return {
    type: 'native', timestamp, source_frame: sourceFrame,
    total_ms: finite(event.total_ms), interval_ms: finite(event.interval_ms),
  };
  if (event.type === 'longtask') return {
    type: 'longtask', timestamp, duration_ms: finite(event.duration_ms), source_frame: sourceFrame,
  };
  if (event.type === 'audio') {
    const contextState = AUDIO_CONTEXT_STATES.includes(event.context_state) ? event.context_state : 'unknown';
    return {
      type: 'audio', timestamp, callback_ms: finite(event.callback_ms), interval_ms: finite(event.interval_ms),
      queue_depth: finite(event.queue_depth), underruns: finite(event.underruns), overflows: finite(event.overflows),
      clock_seconds: finite(event.clock_seconds), context_state: contextState,
      enabled: event.enabled === null || typeof event.enabled === 'boolean' ? event.enabled : null,
      source_frame: sourceFrame,
    };
  }
  if (event.type === 'lifecycle') {
    const name = EVENT_LIFECYCLE.has(event.kind) ? event.kind : null;
    if (!name) return null;
    const scene = mapScene(event.scene);
    return {
      type: 'lifecycle', kind: name, timestamp, source_frame: sourceFrame,
      scene, scene_code: SCENE_CODES[scene] ?? 0,
      duration_ms: finite(event.duration_ms), bytes: finite(event.bytes), files: finite(event.files),
    };
  }
  return null;
}

function mapEvents(incident) {
  const pre = [];
  const post = [];
  for (const event of Array.isArray(incident?.pre_events) ? incident.pre_events : []) {
    const mapped = mapEvent(event);
    if (mapped && pre.length < MAX_PRE_EVENTS) pre.push(mapped);
  }
  for (const event of Array.isArray(incident?.post_events) ? incident.post_events : []) {
    const mapped = mapEvent(event);
    if (mapped && post.length < MAX_POST_EVENTS) post.push(mapped);
  }
  return {pre, post};
}

function reportIncomplete(report, incident) {
  const flags = report?.flags || {};
  return Boolean(flags.report_truncated || flags.history_evicted || flags.post_truncated || flags.malformed_record ||
    flags.persistence_failed || incident?.recovery_timed_out || incident?.post_truncated);
}

function incidentClockOwner(incident) {
  const value = incident?.clock_owner ?? incident?.clockOwner;
  if (value === 1 || value === '1' || value === 'simulation') return 'simulation';
  if (value === 2 || value === '2' || value === 'audio') return 'audio';
  return 'other';
}

function mapOwner(owner, reason) {
  return CLOCK_OWNERS.includes(owner) ? owner : 'other';
}

const SOURCE_INCIDENT_ID_RE = /^(?:incident-[0-9]+|session-[a-z0-9-]{1,64}:incident-[0-9]+)$/;

function mapSourceIncidentId(incident, sessionId) {
  const sourceId = typeof incident?.id === 'string' ? incident.id : '';
  if (!SOURCE_INCIDENT_ID_RE.test(sourceId)) return null;
  return sourceId.startsWith('session-') ? sourceId : `${sessionId}:${sourceId}`;
}

function mapWireReport(localReport, incident, {root, capabilities, sessionId} = {}) {
  if (!localReport || typeof localReport !== 'object' || !incident || typeof incident !== 'object') return null;
  const identity = normalizeIdentity(localReport.identity);
  if (!identity) return null;
  const environment = environmentForOrigin(localReport.environment?.origin);
  if (!environment || localReport.environment?.kind !== environment.env) return null;
  const reportOrigin = environment.origin;
  const client = localReport.client || {};
  const browserMajor = integer(client.browser_major ?? client.browserMajor);
  const scene = mapScene(incident.scene);
  const sourceFrame = incident.source_frame === null ? null : integer(incident.source_frame);
  const clockOwner = incidentClockOwner(incident);
  const events = mapEvents(incident);
  const reason = mapReason(incident.reason);
  if (!reason) return null;
  const wire = {
    schema: DIAGNOSTICS_SCHEMA,
    version: DIAGNOSTICS_VERSION,
    session_id: typeof localReport.session_id === 'string' && /^session-[a-z0-9-]{1,64}$/.test(localReport.session_id) ? localReport.session_id : (sessionId || 'session-' + hashText('session')),
    incident_id: null,
    identity,
    environment: {env: environment.env, origin: reportOrigin},
    client: {
      browser_family: normalizeBrowserFamily(client.browser ?? client.browser_family),
      browser_major: browserMajor === null ? null : Math.max(0, Math.min(999, browserMajor)),
      platform: normalizePlatform(client.platform),
    },
    capabilities: localCapabilities(localReport, root, events, capabilities),
    incident: {
      reason,
      reason_code: REASON_CODES[reason] ?? 4,
      value: finiteOrNull(incident.value),
      threshold: finiteOrNull(incident.threshold),
      source_frame: sourceFrame !== null && sourceFrame !== undefined && sourceFrame >= 0 ? sourceFrame : null,
      scene,
      scene_code: SCENE_CODES[scene] ?? 0,
      clock_owner: mapOwner(clockOwner, incident.reason),
      clock_owner_code: OWNER_CODES[mapOwner(clockOwner, incident.reason)] ?? 0,
    },
    history: mapHistory(incident),
    events,
    flags: {
      incomplete: reportIncomplete(localReport, incident),
      persistence_failure: Boolean(localReport.flags?.persistence_failed),
    },
  };
  return wire;
}

function utf8Bytes(value) {
  try { return new TextEncoder().encode(JSON.stringify(value)).byteLength; } catch { return Number.POSITIVE_INFINITY; }
}

function normalizeMappedReport(mapped) {
  // The backend schema is strict v1.  Retrying with a legacy nullable-history
  // shape would shift columns and silently change the meaning of measurements.
  return normalizeDiagnosticReport(mapped);
}

function fitReportSize(report) {
  if (utf8Bytes(report) <= MAX_REPORT_BYTES) return report;
  const candidate = clone(report);
  candidate.flags.incomplete = true;
  candidate.history.truncated = true;
  while (utf8Bytes(candidate) > MAX_REPORT_BYTES && candidate.history.rows.length) candidate.history.rows.shift();
  while (utf8Bytes(candidate) > MAX_REPORT_BYTES && candidate.events.pre.length) candidate.events.pre.shift();
  while (utf8Bytes(candidate) > MAX_REPORT_BYTES && candidate.events.post.length) candidate.events.post.shift();
  if (utf8Bytes(candidate) > MAX_REPORT_BYTES) throw new Error('diagnostic_report_too_large');
  return normalizeDiagnosticReport(candidate);
}

function recordKeysValid(value) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
  const expected = ['id', 'fingerprint', 'environment', 'report', 'created_at', 'expires_at', 'attempts', 'next_attempt_at'];
  const actual = Object.keys(value);
  return actual.length === expected.length && expected.every(key => actual.includes(key));
}

function normalizeStoredRecord(value, now) {
  try {
    if (!recordKeysValid(value) || !OUTBOX_ID_RE.test(value.id) || typeof value.fingerprint !== 'string' || value.fingerprint.length > 128 ||
        (value.environment !== 'staging' && value.environment !== 'production') ||
        !Number.isSafeInteger(value.created_at) || !Number.isSafeInteger(value.expires_at) ||
        !Number.isSafeInteger(value.attempts) || value.attempts < 0 || value.attempts > DELIVERY_MAX_RETRIES ||
        !Number.isSafeInteger(value.next_attempt_at) || !value.report) return null;
    const report = normalizeDiagnosticReport(value.report);
    if (utf8Bytes(report) > MAX_REPORT_BYTES) return null;
    if (report.environment.env !== value.environment) return null;
    if (value.expires_at <= now) return null;
    return {
      id: value.id,
      fingerprint: value.fingerprint,
      environment: value.environment,
      report: immutable(report),
      created_at: value.created_at,
      expires_at: value.expires_at,
      attempts: value.attempts,
      next_attempt_at: value.next_attempt_at,
    };
  } catch {
    return null;
  }
}

function normalizeTombstone(value, now) {
  try {
    const expected = ['incident_id', 'fingerprint', 'sent_at', 'expires_at'];
    if (!value || typeof value !== 'object' || Array.isArray(value) ||
        Object.keys(value).length !== expected.length || expected.some(key => !Object.hasOwn(value, key)) ||
        !SOURCE_INCIDENT_ID_RE.test(value.incident_id) || !/^[a-f0-9]{16}$/.test(value.fingerprint) ||
        !Number.isSafeInteger(value.sent_at) || !Number.isSafeInteger(value.expires_at) || value.expires_at <= now) return null;
    return {incident_id: value.incident_id, fingerprint: value.fingerprint,
      sent_at: value.sent_at, expires_at: value.expires_at};
  } catch { return null; }
}

function errorKind(error) {
  const name = typeof error?.name === 'string' ? error.name.toLowerCase() : '';
  if (/quota/.test(name)) return 'quota';
  if (/security|denied|permission|notallowed/.test(name)) return 'denied';
  return 'failed';
}

/** Create an inactive-only, bounded diagnostics delivery adapter. */
export function createDiagnosticsDelivery(options = {}) {
  const root = options.globalThis || globalThis;
  const now = typeof options.now === 'function' ? options.now : () => Date.now();
  const random = typeof options.random === 'function' ? options.random : Math.random;
  const origin = options.origin ?? root.location?.origin;
  const environment = environmentForOrigin(origin);
  const endpoint = environment ? `${environment.origin}/api/diagnostics` : null;
  const fetchImpl = options.fetch ?? root.fetch;
  const indexedDB = options.indexedDB ?? root.indexedDB ?? null;
  const storage = options.storage ?? null;
  const requestedRetryMs = Number(options.retryBaseMs ?? 1000);
  const retryBaseMs = Number.isFinite(requestedRetryMs) ? Math.max(0, requestedRetryMs) : 1000;
  const requestedRetentionMs = Number(options.retentionMs ?? DELIVERY_RETENTION_MS);
  const retentionMs = Number.isFinite(requestedRetentionMs)
    ? Math.min(DELIVERY_RETENTION_MS, Math.max(1, requestedRetentionMs))
    : DELIVERY_RETENTION_MS;
  const sessionId = `session-${randomHex(root, random)}${(++DELIVERY_SESSION_SEQUENCE).toString(36)}`;
  let inactive = false;
  let online = options.online !== false && root.navigator?.onLine !== false;
  let optOut = options.optOut === true;
  let disposed = false;
  let loaded = false;
  let loadingPromise = null;
  let flushPromise = null;
  let persistencePromise = null;
  let activePersistenceGeneration = null;
  let optOutClearPromise = null;
  let initialOptOutClearTimer = null;
  let flushTimer = null;
  let records = [];
  let tombstones = [];
  let dirty = false;
  let tombstoneDirty = false;
  let tombstonesLoaded = false;
  let tombstoneLoadingPromise = null;
  let sessionUploadCount = 0;
  const sentIncidentIds = new Set();
  const rememberedIncidentIds = new Set();
  const controllers = new Set();
  let queueMutationGeneration = 0;
  const flags = {
    evicted: false, evicted_count: 0, truncated: false, expired: false,
    rejected: false, persistence_failed: false, storage_unavailable: false,
    malformed: false,
  };

  const canPersist = () => !disposed && !optOut && inactive && Boolean(environment);
  const canDeliver = () => canPersist() && online;

  function rememberIncidentId(incidentId) {
    if (typeof incidentId !== 'string') return;
    rememberedIncidentIds.add(incidentId);
    const maxRemembered = DELIVERY_MAX_RECORDS + DELIVERY_MAX_SESSION_UPLOADS;
    while (rememberedIncidentIds.size > maxRemembered) rememberedIncidentIds.delete(rememberedIncidentIds.values().next().value);
  }

  function schedule(callback, delay = 0) {
    const timer = (root.setTimeout || setTimeout)(callback, delay);
    if (typeof timer?.unref === 'function' && delay > 0) timer.unref();
    return timer;
  }

  function storageAdapter() {
    if (storage && typeof storage === 'object') return storage;
    if (!indexedDB || typeof indexedDB.open !== 'function') return null;
    const open = () => new Promise((resolve, reject) => {
      let request;
      let blocked = false;
      try { request = indexedDB.open(DELIVERY_DB_NAME, 2); } catch (error) { reject(error); return; }
      request.onupgradeneeded = () => {
        try {
          const db = request.result;
          if (!db.objectStoreNames.contains(DELIVERY_STORE_NAME)) db.createObjectStore(DELIVERY_STORE_NAME, {keyPath: 'id'});
          if (!db.objectStoreNames.contains(DELIVERY_TOMBSTONE_STORE_NAME)) db.createObjectStore(DELIVERY_TOMBSTONE_STORE_NAME, {keyPath: 'incident_id'});
        } catch (error) { reject(error); }
      };
      request.onsuccess = () => {
        const db = request.result;
        if (blocked) { try { db.close(); } catch {} return; }
        resolve(db);
      };
      request.onerror = () => reject(request.error || Object.assign(new Error(), {name: 'OperationError'}));
      request.onblocked = () => { blocked = true; reject(Object.assign(new Error(), {name: 'NotAllowedError'})); };
    });
    return {
      async load() {
        const db = await open();
        try {
          return await new Promise((resolve, reject) => {
            const tx = db.transaction(DELIVERY_STORE_NAME, 'readonly');
            const request = tx.objectStore(DELIVERY_STORE_NAME).openCursor();
            const values = [];
            let overflowed = false;
            request.onsuccess = () => {
              const cursor = request.result;
              if (!cursor) { resolve({values, overflowed}); return; }
              if (values.length < DELIVERY_MAX_RECORDS) values.push(cursor.value);
              else { overflowed = true; resolve({values, overflowed}); return; }
              cursor.continue();
            };
            request.onerror = () => reject(request.error || new Error());
            tx.onerror = () => reject(tx.error || new Error());
          });
        } finally { try { db.close(); } catch {} }
      },
      async save(values) {
        const db = await open();
        try {
          await new Promise((resolve, reject) => {
            const tx = db.transaction(DELIVERY_STORE_NAME, 'readwrite');
            const store = tx.objectStore(DELIVERY_STORE_NAME);
            store.clear();
            for (const value of values) store.put(value);
            tx.oncomplete = resolve;
            tx.onerror = () => reject(tx.error || new Error());
            tx.onabort = () => reject(tx.error || new Error());
          });
        } finally { try { db.close(); } catch {} }
      },
      async loadTombstones() {
        const db = await open();
        try {
          return await new Promise((resolve, reject) => {
            const tx = db.transaction(DELIVERY_TOMBSTONE_STORE_NAME, 'readonly');
            const request = tx.objectStore(DELIVERY_TOMBSTONE_STORE_NAME).openCursor();
            const values = [];
            let overflowed = false;
            request.onsuccess = () => {
              const cursor = request.result;
              if (!cursor) { resolve({values, overflowed}); return; }
              if (values.length < DELIVERY_MAX_TOMBSTONES) values.push(cursor.value);
              else { overflowed = true; resolve({values, overflowed}); return; }
              cursor.continue();
            };
            request.onerror = () => reject(request.error || new Error());
            tx.onerror = () => reject(tx.error || new Error());
          });
        } finally { try { db.close(); } catch {} }
      },
      async saveTombstones(values) {
        const db = await open();
        try {
          await new Promise((resolve, reject) => {
            const tx = db.transaction(DELIVERY_TOMBSTONE_STORE_NAME, 'readwrite');
            const store = tx.objectStore(DELIVERY_TOMBSTONE_STORE_NAME);
            store.clear();
            for (const value of values) store.put(value);
            tx.oncomplete = resolve;
            tx.onerror = () => reject(tx.error || new Error());
            tx.onabort = () => reject(tx.error || new Error());
          });
        } finally { try { db.close(); } catch {} }
      },
    };
  }

  async function loadOutbox() {
    if (loaded) return;
    if (loadingPromise) return loadingPromise;
    const adapter = storageAdapter();
    if (!adapter) {
      flags.storage_unavailable = true;
      loaded = true;
      return;
    }
    loadingPromise = (async () => {
      try {
        const loadedValues = typeof adapter.load === 'function' ? await adapter.load() : [];
        mergeStoredValues(loadedValues);
        loaded = true;
      } catch (error) {
        flags.storage_unavailable = true;
        flags.persistence_failed = true;
        loaded = true;
      } finally { loadingPromise = null; }
    })();
    return loadingPromise;
  }

  function mergeStoredValues(loadedValues) {
    const values = Array.isArray(loadedValues) ? loadedValues : loadedValues?.values ?? loadedValues?.records;
    if (!Array.isArray(loadedValues) && loadedValues?.overflowed) {
      flags.evicted = true;
      flags.evicted_count += 1;
    }
    if (!Array.isArray(values)) { flags.malformed = true; return; }
    const pendingCount = records.length;
    let inspected = 0;
    for (const value of values) {
      inspected += 1;
      if (inspected > DELIVERY_MAX_RECORDS) {
        flags.evicted = true;
        flags.evicted_count += 1;
        break;
      }
      const normalized = normalizeStoredRecord(value, now());
      if (!normalized) { if (value) flags.malformed = true; continue; }
      const incidentId = normalized.report.incident_id;
      if (sentIncidentIds.has(incidentId) || tombstones.some(value => value.incident_id === incidentId)) continue;
      const duplicate = records.find(existing => existing.fingerprint === normalized.fingerprint || existing.report.incident_id === incidentId);
      if (!duplicate) {
        while (records.some(existing => existing.id === normalized.id)) {
          const collision = normalized.id;
          normalized.id = hashText(`${collision}:${incidentId}`) + hashText(`${collision}:${incidentId}:retry`);
        }
        records.push(normalized);
        queueMutationGeneration += 1;
        rememberIncidentId(incidentId);
      }
    }
    if (pendingCount) dirty = true;
  }

  function mergeStoredTombstones(loadedValues) {
    const values = Array.isArray(loadedValues) ? loadedValues : loadedValues?.values ?? loadedValues?.tombstones;
    if (!Array.isArray(loadedValues) && loadedValues?.overflowed) {
      flags.evicted = true;
      flags.evicted_count += 1;
    }
    if (!Array.isArray(values)) { flags.malformed = true; return; }
    let inspected = 0;
    for (const value of values) {
      inspected += 1;
      if (inspected > DELIVERY_MAX_TOMBSTONES) {
        flags.evicted = true;
        flags.evicted_count += 1;
        break;
      }
      const normalized = normalizeTombstone(value, now());
      if (!normalized) {
        if (value && Number.isSafeInteger(value.expires_at) && value.expires_at <= now()) flags.expired = true;
        else if (value) flags.malformed = true;
        continue;
      }
      if (!tombstones.some(existing => existing.incident_id === normalized.incident_id)) {
        tombstones.push(normalized);
        queueMutationGeneration += 1;
      }
      rememberIncidentId(normalized.incident_id);
    }
    boundTombstones();
  }

  async function loadTombstones() {
    if (tombstonesLoaded) return;
    if (tombstoneLoadingPromise) return tombstoneLoadingPromise;
    const adapter = storageAdapter();
    if (!adapter || typeof adapter.loadTombstones !== 'function') {
      tombstonesLoaded = true;
      return;
    }
    tombstoneLoadingPromise = (async () => {
      try {
        mergeStoredTombstones(await adapter.loadTombstones());
        tombstonesLoaded = true;
      } catch {
        flags.storage_unavailable = true;
        flags.persistence_failed = true;
        tombstonesLoaded = true;
      } finally { tombstoneLoadingPromise = null; }
    })();
    return tombstoneLoadingPromise;
  }

  function boundTombstones() {
    tombstones.sort((left, right) => left.sent_at - right.sent_at);
    while (tombstones.length > DELIVERY_MAX_TOMBSTONES) {
      tombstones.shift();
      queueMutationGeneration += 1;
    }
  }

  function boundRecords() {
    records.sort((left, right) => left.created_at - right.created_at);
    while (records.length > DELIVERY_MAX_RECORDS) {
      records.shift(); flags.evicted = true; flags.evicted_count += 1; dirty = true; queueMutationGeneration += 1;
    }
    let bytes = 0;
    for (let index = records.length - 1; index >= 0; index -= 1) {
      let size;
      try { size = new TextEncoder().encode(JSON.stringify(records[index])).byteLength; } catch { size = DELIVERY_MAX_BYTES + 1; }
      if (size > DELIVERY_MAX_BYTES || bytes + size > DELIVERY_MAX_BYTES) {
        records.splice(index, 1); flags.truncated = true; flags.evicted = true; flags.evicted_count += 1; dirty = true; queueMutationGeneration += 1;
      } else bytes += size;
    }
  }

  async function persistOutbox(force = false, mergeStored = true) {
    if (!dirty && !tombstoneDirty && !force) return {persisted: false, reason: 'clean', count: records.length};
    if (persistencePromise) {
      const priorPersistence = persistencePromise;
      const priorGeneration = activePersistenceGeneration;
      return priorPersistence.then(result =>
        (dirty || tombstoneDirty) && queueMutationGeneration !== priorGeneration
          ? persistOutbox(force, mergeStored)
          : result);
    }
    const clearAtStart = optOutClearPromise;
    const operationGeneration = queueMutationGeneration;
    activePersistenceGeneration = operationGeneration;
    persistencePromise = Promise.resolve().then(async () => {
      try {
        // Opt-out clears are asynchronous.  A quick off -> on transition can
        // enqueue a fresh report while the clear is still in flight; wait for
        // that clear before reading or writing so its empty write cannot erase
        // the new bounded outbox.
        if (clearAtStart) await clearAtStart.catch(() => {});
        if (mergeStored && !loaded) await loadOutbox();
        if (mergeStored && !tombstonesLoaded) await loadTombstones();
        const adapter = storageAdapter();
        if (!adapter || typeof adapter.save !== 'function') {
          flags.storage_unavailable = true;
          return {persisted: false, reason: 'unavailable', count: records.length};
        }
        // Refresh before each write to reduce cross-tab loss when writes are
        // sequential. This is best effort: concurrent tab writes can still
        // race because the adapter contract does not provide a transaction
        // spanning both bounded reads and the replacement writes.
        if (mergeStored && typeof adapter.load === 'function') {
          try { mergeStoredValues(await adapter.load()); }
          catch { flags.storage_unavailable = true; flags.persistence_failed = true; }
        }
        if (mergeStored && typeof adapter.loadTombstones === 'function') {
          try { mergeStoredTombstones(await adapter.loadTombstones()); }
          catch { flags.storage_unavailable = true; flags.persistence_failed = true; }
        }
        if (!force && !canPersist()) return {persisted: false, reason: 'resumed', count: records.length};
        const recordsBeforeFilter = records.length;
        records = records.filter(record => !sentIncidentIds.has(record.report.incident_id) &&
          !tombstones.some(value => value.incident_id === record.report.incident_id));
        if (records.length !== recordsBeforeFilter) {
          dirty = true;
          queueMutationGeneration += 1;
        }
        boundRecords();
        boundTombstones();
        const writeGeneration = queueMutationGeneration;
        activePersistenceGeneration = writeGeneration;
        const values = records.map(record => clone(record));
        const sentValues = tombstones.map(tombstone => clone(tombstone));
        await adapter.save(values);
        if (typeof adapter.saveTombstones === 'function') await adapter.saveTombstones(sentValues);
        if (queueMutationGeneration === writeGeneration) {
          dirty = false;
          tombstoneDirty = false;
        }
        return {persisted: true, count: values.length};
      } catch (error) {
        flags.persistence_failed = true;
        return {persisted: false, reason: errorKind(error), count: records.length};
      } finally {
        persistencePromise = null;
        activePersistenceGeneration = null;
      }
    });
    return persistencePromise;
  }

  async function clearPersistedOutbox() {
    if (optOutClearPromise) return optOutClearPromise;
    const adapter = storageAdapter();
    if (!adapter || typeof adapter.save !== 'function') {
      flags.storage_unavailable = true;
      return {persisted: false, reason: 'unavailable'};
    }
    const priorPersistence = persistencePromise;
    const clearGeneration = queueMutationGeneration;
    optOutClearPromise = Promise.resolve().then(async () => {
      try {
        // Only wait for work that was already running when opt-out began.
        // A newly re-enabled persistence task captures this clear promise and
        // waits behind it; capturing prevents the two operations from forming
        // a cycle.
        if (priorPersistence) await priorPersistence.catch(() => {});
        await adapter.save([]);
        if (typeof adapter.saveTombstones === 'function') await adapter.saveTombstones([]);
        if (queueMutationGeneration === clearGeneration) {
          dirty = false;
          tombstoneDirty = false;
        }
        loaded = true;
        return {persisted: true, count: 0};
      } catch (error) {
        flags.persistence_failed = true;
        return {persisted: false, reason: errorKind(error)};
      } finally { optOutClearPromise = null; }
    });
    return optOutClearPromise;
  }

  function scheduleFlush(delay = 0) {
    if (flushTimer !== null || disposed || optOut || !inactive || !environment) return;
    flushTimer = schedule(() => {
      flushTimer = null;
      void flushWhenInactive();
    }, delay);
  }

  function enqueue(value) {
    if (disposed || optOut) return {accepted: false, reason: 'disabled'};
    const source = value && typeof value === 'object' ? value : null;
    const suppliedReport = source?.report && typeof source.report === 'object' ? source.report : null;
    const localReport = Array.isArray(source?.incidents) ? source : source?.incident ? (suppliedReport ? {...suppliedReport, incidents: [source.incident], incident: source.incident} : source) :
      (suppliedReport && (Array.isArray(suppliedReport.incidents) || suppliedReport.incident) ? suppliedReport :
      (source?.reason && options.report && typeof options.report === 'object' ? {...options.report, incidents: [source], incident: source} : null));
    if (!localReport || !environment) return {accepted: false, reason: environment ? 'malformed' : 'local_only'};
    const incidents = Array.isArray(localReport.incidents) ? localReport.incidents : [localReport.incident];
    const ids = [];
    let duplicateSent = false;
    let sessionLimit = false;
    let expired = false;
    for (const incident of incidents.slice(0, DELIVERY_MAX_RECORDS)) {
      const capturedAt = incident?.captured_at_ms;
      if (Number.isSafeInteger(capturedAt) && capturedAt >= 0 &&
          capturedAt + retentionMs <= now()) {
        flags.expired = true;
        expired = true;
        continue;
      }
      const mapped = mapWireReport(localReport, incident, {root, capabilities: options.capabilities, sessionId});
      if (!mapped) { flags.malformed = true; continue; }
      const incidentId = mapSourceIncidentId(incident, mapped.session_id);
      if (!incidentId) { flags.malformed = true; continue; }
      mapped.incident_id = incidentId;
      const fingerprint = hashText(JSON.stringify([mapped.session_id, mapped.identity, mapped.environment, mapped.incident_id,
        mapped.incident, mapped.history, mapped.events]));
      const duplicate = records.find(record => record.environment === environment.env &&
        (record.report.incident_id === incidentId || record.fingerprint === fingerprint));
      if (duplicate) { ids.push(duplicate.id); continue; }
      if (sentIncidentIds.has(incidentId) || rememberedIncidentIds.has(incidentId)) { duplicateSent = true; continue; }
      if (sessionUploadCount >= DELIVERY_MAX_SESSION_UPLOADS) { sessionLimit = true; continue; }
      let id = randomHex(root, random);
      if (records.some(record => record.id === id)) {
        id = hashText(`${fingerprint}:${now()}:${records.length}`) + hashText(`${fingerprint}:${now()}:${records.length}:retry`);
      }
      if (records.some(record => record.id === id)) { flags.malformed = true; continue; }
      let report;
      try {
        report = immutable(fitReportSize(normalizeMappedReport(mapped)));
      } catch {
        flags.malformed = true;
        continue;
      }
      const created = Math.max(0, Math.trunc(Number(now()) || 0));
      records.push({id, fingerprint, environment: environment.env, report,
        created_at: created, expires_at: Math.min(created,
          Number.isSafeInteger(capturedAt) && capturedAt >= 0 ? capturedAt : created) + retentionMs,
        attempts: 0, next_attempt_at: created});
      queueMutationGeneration += 1;
      rememberIncidentId(incidentId);
      ids.push(id);
      dirty = true;
    }
    boundRecords();
    if (inactive && environment) scheduleFlush(0);
    return {accepted: ids.length > 0, ids, count: ids.length,
      reason: ids.length ? null : sessionLimit ? 'session_limit' : duplicateSent ? 'duplicate_sent' : expired ? 'expired' : 'malformed'};
  }

  function enqueueIncident(incident, localReport = options.report) {
    if (!incident || typeof incident !== 'object') return {accepted: false, reason: 'malformed'};
    if (localReport && typeof localReport === 'object') return enqueue({report: {...localReport, incidents: [incident], incident}, incident});
    return enqueue(incident);
  }

  function abortControllers() {
    for (const controller of controllers) { try { controller.abort(); } catch {} }
    controllers.clear();
  }

  function completeRecord(record, timestamp) {
    if (!record.report?.incident_id) return;
    // Completion covers successful, rejected and exhausted work. Otherwise a
    // refresh from storage could resurrect the removed row during this write.
    sentIncidentIds.add(record.report.incident_id);
    rememberIncidentId(record.report.incident_id);
    tombstones = tombstones.filter(value => value.incident_id !== record.report.incident_id);
    tombstones.push({incident_id: record.report.incident_id, fingerprint: record.fingerprint,
      sent_at: timestamp, expires_at: timestamp + retentionMs});
    queueMutationGeneration += 1;
    boundTombstones();
    tombstoneDirty = true;
  }

  async function sendRecord(record) {
    if (!canDeliver()) return 'skipped';
    if (typeof fetchImpl !== 'function') return 'unavailable';
    const controller = typeof root.AbortController === 'function' ? new root.AbortController() : null;
    if (controller) controllers.add(controller);
    const timeout = controller ? schedule(() => controller.abort(), DELIVERY_REQUEST_TIMEOUT_MS) : null;
    try {
      const response = await fetchImpl(endpoint, {
        method: 'POST',
        headers: {'content-type': 'application/json'},
        body: JSON.stringify(record.report),
        signal: controller?.signal,
        credentials: 'omit',
        mode: 'same-origin',
        redirect: 'error',
        cache: 'no-store',
        referrerPolicy: 'no-referrer',
      });
      if (response?.status >= 200 && response?.status < 300) return 'sent';
      if (response?.status === 408 || response?.status === 425 || response?.status === 429 || response?.status >= 500) return 'retry';
      return 'rejected';
    } catch {
      return 'retry';
    } finally {
      if (timeout !== null) (root.clearTimeout || clearTimeout)(timeout);
      if (controller) controllers.delete(controller);
    }
  }

  async function flushWhenInactive() {
    if (flushPromise) return flushPromise;
    if (!canPersist()) return {sent: 0, skipped: true};
    flushPromise = (async () => {
      let sent = 0;
      try {
        await loadOutbox();
        await loadTombstones();
        if (!canPersist()) return {sent: 0, skipped: true};
        const timestamp = Math.max(0, Math.trunc(Number(now()) || 0));
        for (let index = records.length - 1; index >= 0; index -= 1) {
          const record = records[index];
          if (record.expires_at <= timestamp) { records.splice(index, 1); flags.expired = true; dirty = true; queueMutationGeneration += 1; continue; }
          if (tombstones.some(tombstone => tombstone.incident_id === record.report.incident_id)) {
            records.splice(index, 1); dirty = true; queueMutationGeneration += 1;
          }
        }
        boundRecords();
        if (!online) {
          if (canPersist()) await persistOutbox();
          return {sent: 0, pending: records.length, skipped: true, offline: true};
        }
        for (const record of [...records]) {
          if (record.environment !== environment.env || record.next_attempt_at > timestamp) continue;
          if (!canPersist()) return {sent, pending: records.length, skipped: true};
          if (!online) break;
          if (sessionUploadCount >= DELIVERY_MAX_SESSION_UPLOADS) break;
          sessionUploadCount += 1;
          const result = await sendRecord(record);
          if (!canPersist()) return {sent, pending: records.length, skipped: true};
          if (!online) break;
          if (result === 'skipped') return {sent, pending: records.length, skipped: true};
          if (result === 'sent' || result === 'rejected') {
            const index = records.indexOf(record);
            if (index >= 0) { records.splice(index, 1); queueMutationGeneration += 1; }
            dirty = true;
            completeRecord(record, timestamp);
            if (result === 'sent') {
              sent += 1;
            }
            else flags.rejected = true;
            continue;
          }
          record.attempts += 1;
          if (record.attempts >= DELIVERY_MAX_RETRIES) {
            const index = records.indexOf(record);
            if (index >= 0) { records.splice(index, 1); queueMutationGeneration += 1; }
            flags.rejected = true;
            completeRecord(record, timestamp);
            dirty = true;
          } else {
            const jitter = 0.8 + Math.max(0, Math.min(1, Number(random()) || 0)) * 0.4;
            record.next_attempt_at = timestamp + Math.trunc(retryBaseMs * (2 ** (record.attempts - 1)) * jitter);
            queueMutationGeneration += 1;
            dirty = true;
            scheduleFlush(Math.max(0, record.next_attempt_at - timestamp));
          }
        }
        if (canPersist()) await persistOutbox();
        return {sent, pending: records.length, ...(online ? {} : {skipped: true, offline: true})};
      } catch {
        flags.persistence_failed = true;
        return {sent, pending: records.length};
      } finally { flushPromise = null; }
    })();
    return flushPromise;
  }

  function setActive(value) {
    inactive = value !== true;
    if (inactive) scheduleFlush(0);
    else {
      if (flushTimer !== null) { (root.clearTimeout || clearTimeout)(flushTimer); flushTimer = null; }
      abortControllers();
    }
    return inactive;
  }

  function setOnline(value) {
    online = value === true;
    if (online && inactive) scheduleFlush(0);
    return online;
  }

  function setOptOut(value) {
    optOut = value === true;
    if (!optOut && initialOptOutClearTimer !== null) {
      (root.clearTimeout || clearTimeout)(initialOptOutClearTimer);
      initialOptOutClearTimer = null;
      // Construction with optOut=true still has to clear the old outbox. If
      // reporting is re-enabled before the deferred cleanup runs, start that
      // cleanup now so fresh work waits behind the same clear promise.
      void clearPersistedOutbox();
    }
    if (optOut) {
      abortControllers();
      records = [];
      queueMutationGeneration += 1;
      if (flushTimer !== null) { (root.clearTimeout || clearTimeout)(flushTimer); flushTimer = null; }
      dirty = false;
      void clearPersistedOutbox();
    }
    return optOut;
  }

  function getStatus() {
    return {
      enabled: !optOut && !disposed,
      eligible: !disposed && Boolean(environment),
      ready_to_deliver: !disposed && !optOut && inactive && online && Boolean(environment),
      inactive,
      online,
      environment: environment?.env ?? 'unknown',
      queued: records.length,
      queue_count: records.length,
      max_records: DELIVERY_MAX_RECORDS,
      session_uploads: sessionUploadCount,
      max_session_uploads: DELIVERY_MAX_SESSION_UPLOADS,
    };
  }

  function exportPending() {
    boundRecords();
    return immutable({
      enabled: !optOut && !disposed,
      environment: environment?.env ?? 'unknown',
      origin: canonicalOrigin(origin),
      endpoint: environment ? endpoint : null,
      records: records.map(record => ({
        id: record.id, fingerprint: record.fingerprint, environment: record.environment,
        created_at: record.created_at, expires_at: record.expires_at,
        attempts: record.attempts, next_attempt_at: record.next_attempt_at, report: record.report,
      })),
      flags: {...flags},
      limits: {max_records: DELIVERY_MAX_RECORDS, max_bytes: DELIVERY_MAX_BYTES, retention_ms: retentionMs,
        max_retries: DELIVERY_MAX_RETRIES, max_session_uploads: DELIVERY_MAX_SESSION_UPLOADS},
    });
  }

  function dispose() {
    disposed = true;
    abortControllers();
    if (flushTimer !== null) { (root.clearTimeout || clearTimeout)(flushTimer); flushTimer = null; }
  }

  if (optOut) {
    initialOptOutClearTimer = schedule(() => {
      initialOptOutClearTimer = null;
      void clearPersistedOutbox();
    }, 0);
  }

  return Object.freeze({
    enqueue,
    enqueueIncident,
    onIncident: enqueueIncident,
    setActive,
    setOnline,
    setOptOut,
    getStatus,
    flushWhenInactive,
    exportPending,
    persist: () => persistOutbox(true),
    dispose,
    constants: Object.freeze({db_name: DELIVERY_DB_NAME, store_name: DELIVERY_STORE_NAME,
      tombstone_store_name: DELIVERY_TOMBSTONE_STORE_NAME, max_records: DELIVERY_MAX_RECORDS,
      max_bytes: DELIVERY_MAX_BYTES, max_tombstones: DELIVERY_MAX_TOMBSTONES,
      retention_ms: retentionMs, max_retries: DELIVERY_MAX_RETRIES,
      max_session_uploads: DELIVERY_MAX_SESSION_UPLOADS}),
  });
}

export default createDiagnosticsDelivery;
